#include "OilTankSettings.h"
#include "OilTank.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	TSharedPtr<FJsonObject> ParseJsonFile(const FString& Path, FString& OutError)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			OutError = FString::Printf(TEXT("cannot read %s"), *Path);
			return nullptr;
		}
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			OutError = FString::Printf(TEXT("bad json in %s: %s"), *Path, *Reader->GetErrorMessage());
			return nullptr;
		}
		return Root;
	}

	void GetF(const FJsonObject& O, const TCHAR* Key, float& V)
	{
		double D;
		if (O.TryGetNumberField(Key, D)) { V = (float)D; }
	}
	void GetI(const FJsonObject& O, const TCHAR* Key, int32& V)
	{
		double D;
		if (O.TryGetNumberField(Key, D)) { V = FMath::RoundToInt((float)D); }
	}
	void GetB(const FJsonObject& O, const TCHAR* Key, bool& V)
	{
		bool B;
		if (O.TryGetBoolField(Key, B)) { V = B; }
	}
	void GetS(const FJsonObject& O, const TCHAR* Key, FString& V)
	{
		FString S;
		if (O.TryGetStringField(Key, S)) { V = S; }
	}
	bool GetRGB(const FJsonObject& O, const TCHAR* Key, FLinearColor& V)
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (O.TryGetArrayField(Key, Arr) && Arr && Arr->Num() >= 3)
		{
			V = FLinearColor((float)(*Arr)[0]->AsNumber(), (float)(*Arr)[1]->AsNumber(), (float)(*Arr)[2]->AsNumber(), 1.0f);
			return true;
		}
		return false;
	}
}

bool FOilTankDump::LoadFromFile(const FString& Path, FString& OutError)
{
	const TSharedPtr<FJsonObject> Root = ParseJsonFile(Path, OutError);
	if (!Root.IsValid())
	{
		return false;
	}
	const FJsonObject& O = *Root;
	GetI(O, TEXT("version"), Version);
	double D;
	if (O.TryGetNumberField(TEXT("t"), D)) { T = D; }
	GetI(O, TEXT("width"), Width);
	GetI(O, TEXT("height"), Height);
	GetRGB(O, TEXT("film_rgb"), FilmRGB);
	GetRGB(O, TEXT("accent_rgb"), AccentRGB);
	GetS(O, TEXT("height_png"), HeightPng);
	GetS(O, TEXT("film_png"), FilmPng);

	// Relative image paths resolve against the dump's own folder.
	const FString DumpDir = FPaths::GetPath(Path);
	if (!HeightPng.IsEmpty() && FPaths::IsRelative(HeightPng)) { HeightPng = FPaths::Combine(DumpDir, HeightPng); }
	if (!FilmPng.IsEmpty() && FPaths::IsRelative(FilmPng)) { FilmPng = FPaths::Combine(DumpDir, FilmPng); }

	Droplets.Reset();
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (O.TryGetArrayField(TEXT("droplets"), Arr) && Arr)
	{
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			const TSharedPtr<FJsonObject>* DO = nullptr;
			if (!V.IsValid() || !V->TryGetObject(DO) || !DO || !DO->IsValid())
			{
				continue;
			}
			FOilTankDroplet Dr;
			GetF(**DO, TEXT("x"), Dr.X);
			GetF(**DO, TEXT("y"), Dr.Y);
			GetF(**DO, TEXT("r"), Dr.R);
			GetF(**DO, TEXT("depth"), Dr.Depth);
			Droplets.Add(Dr);
		}
	}
	if (Version != 1)
	{
		UE_LOG(LogOilTank, Warning, TEXT("dump %s has version %d (expected 1); reading it anyway"), *Path, Version);
	}
	if (HeightPng.IsEmpty())
	{
		OutError = TEXT("dump has no height_png");
		return false;
	}
	return true;
}

bool FOilTankSettings::LoadFromFile(const FString& Path, FString& OutError)
{
	const TSharedPtr<FJsonObject> Root = ParseJsonFile(Path, OutError);
	if (!Root.IsValid())
	{
		return false;
	}
	const FJsonObject& O = *Root;

	GetF(O, TEXT("frame_width_cm"), FrameWidthCm);
	GetF(O, TEXT("frame_height_cm"), FrameHeightCm);
	GetF(O, TEXT("tank_depth_cm"), TankDepthCm);

	const TArray<TSharedPtr<FJsonValue>>* Grid = nullptr;
	if (O.TryGetArrayField(TEXT("oil_grid"), Grid) && Grid && Grid->Num() >= 2)
	{
		OilGridX = (int32)(*Grid)[0]->AsNumber();
		OilGridY = (int32)(*Grid)[1]->AsNumber();
	}
	GetF(O, TEXT("oil_threshold"), OilThreshold);
	GetF(O, TEXT("oil_thickness_cm"), OilThicknessCm);
	GetS(O, TEXT("oil_surround"), OilSurround);
	GetF(O, TEXT("droplet_min_depth"), DropletMinDepth);

	GetF(O, TEXT("light_elev_deg"), LightElevDeg);
	GetF(O, TEXT("light_azim_deg"), LightAzimDeg);
	GetF(O, TEXT("light_source_angle_deg"), LightSourceAngleDeg);
	GetF(O, TEXT("light_lux"), LightLux);
	GetF(O, TEXT("calib_gain"), CalibGain);
	GetB(O, TEXT("calib_through_water"), bCalibThroughWater);
	GetF(O, TEXT("light_volumetric_scattering"), LightVolumetricScattering);
	GetF(O, TEXT("contact_shadow_length"), ContactShadowLength);

	GetF(O, TEXT("exposure_bias"), ExposureBias);
	GetF(O, TEXT("film_grain"), FilmGrain);
	GetS(O, TEXT("aa"), AA);

	GetS(O, TEXT("water_mode"), WaterMode);
	GetF(O, TEXT("water_optical_depth"), WaterOpticalDepth);
	GetF(O, TEXT("water_albedo"), WaterAlbedo);
	GetF(O, TEXT("water_phase_g"), WaterPhaseG);
	{
		FString S;
		if (O.TryGetStringField(TEXT("water_color"), S))
		{
			bWaterColorFromFilm = S.Equals(TEXT("film"), ESearchCase::IgnoreCase);
		}
		else if (GetRGB(O, TEXT("water_color"), WaterColor))
		{
			bWaterColorFromFilm = false;
		}
	}
	GetF(O, TEXT("water_density_raw"), WaterDensityRaw);
	GetF(O, TEXT("lfv_unit_radius_cm"), LfvUnitRadiusCm);
	GetF(O, TEXT("fog_grid_margin_cm"), FogGridMarginCm);

	GetF(O, TEXT("floor_margin"), FloorMargin);
	GetF(O, TEXT("floor_specular"), FloorSpecular);
	GetF(O, TEXT("floor_roughness"), FloorRoughness);

	GetI(O, TEXT("mote_count"), MoteCount);
	GetF(O, TEXT("mote_size_mm"), MoteSizeMm);
	GetRGB(O, TEXT("mote_albedo"), MoteAlbedo);
	GetF(O, TEXT("mote_specular"), MoteSpecular);
	GetF(O, TEXT("mote_roughness"), MoteRoughness);
	GetF(O, TEXT("mote_rise_cm_s"), MoteRiseCmS);
	GetF(O, TEXT("mote_jitter_cm"), MoteJitterCm);
	GetF(O, TEXT("mote_zmin_cm"), MoteZMinCm);
	GetI(O, TEXT("mote_seed"), MoteSeed);

	GetS(O, TEXT("camera_mode"), CameraMode);
	GetF(O, TEXT("camera_fov_deg"), CameraFovDeg);
	GetF(O, TEXT("camera_distance_cm"), CameraDistanceCm);
	GetF(O, TEXT("ortho_height_cm"), OrthoHeightCm);
	GetB(O, TEXT("dof"), bDof);
	GetF(O, TEXT("aperture_fstop"), ApertureFstop);
	GetF(O, TEXT("dof_px_at_floor"), DofPxAtFloor);
	GetF(O, TEXT("sensor_width_mm"), SensorWidthMm);
	GetI(O, TEXT("dof_blade_count"), DofBladeCount);

	GetI(O, TEXT("warmup_frames"), WarmupFrames);
	GetI(O, TEXT("clip_frames"), ClipFrames);
	GetF(O, TEXT("clip_fps"), ClipFps);
	GetF(O, TEXT("still_sim_fps"), StillSimFps);
	GetF(O, TEXT("perf_seconds"), PerfSeconds);
	GetI(O, TEXT("res_x"), ResX);

	GetI(O, TEXT("synthetic_blobs"), SyntheticBlobs);
	GetI(O, TEXT("synthetic_seed"), SyntheticSeed);

	GetB(O, TEXT("debug_no_oil"), bDebugNoOil);
	GetB(O, TEXT("debug_no_water"), bDebugNoWater);
	GetB(O, TEXT("debug_no_motes"), bDebugNoMotes);

	CVars.Reset();
	const TSharedPtr<FJsonObject>* CV = nullptr;
	if (O.TryGetObjectField(TEXT("cvars"), CV) && CV && CV->IsValid())
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& KV : (*CV)->Values)
		{
			if (KV.Value.IsValid())
			{
				CVars.Emplace(KV.Key, KV.Value->AsString());
			}
		}
	}

	// Sanity clamps (a bad json must not produce a degenerate scene).
	OilGridX = FMath::Clamp(OilGridX, 16, 5120);
	OilGridY = FMath::Clamp(OilGridY, 16, 2880);
	TankDepthCm = FMath::Max(TankDepthCm, 0.1f);
	LightElevDeg = FMath::Clamp(LightElevDeg, 5.0f, 90.0f);
	CameraFovDeg = FMath::Clamp(CameraFovDeg, 0.2f, 90.0f);
	MoteCount = FMath::Clamp(MoteCount, 0, 200000);
	ClipFps = FMath::Max(ClipFps, 1.0f);
	StillSimFps = FMath::Max(StillSimFps, 1.0f);
	return true;
}

FString FOilTankSettings::Summary() const
{
	return FString::Printf(
		TEXT("frame=%.1fx%.1fcm depth=%.1fcm grid=%dx%d thr=%.3f thick=%.2fcm surround=%s light elev=%.1f azim=%.1f src=%.3fdeg lux=%.3f "
			 "water=%s tau=%.3f albedo=%.2f g=%.2f motes=%d %.2fmm cam=%s fov=%.2f dist=%.1f dof=%d f=%.2f px@floor=%.1f aa=%s ev=%.2f"),
		FrameWidthCm, FrameHeightCm, TankDepthCm, OilGridX, OilGridY, OilThreshold, OilThicknessCm, *OilSurround,
		LightElevDeg, LightAzimDeg, LightSourceAngleDeg, LightLux, *WaterMode, WaterOpticalDepth, WaterAlbedo, WaterPhaseG,
		MoteCount, MoteSizeMm, *CameraMode, CameraFovDeg, CameraDistanceCm, bDof ? 1 : 0, ApertureFstop, DofPxAtFloor, *AA, ExposureBias);
}
