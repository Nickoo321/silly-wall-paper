#include "OilTankGameMode.h"
#include "OilTank.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/LightComponentBase.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Math/RandomStream.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "ProceduralMeshComponent.h"
#include "UnrealClient.h"

#if WITH_EDITORONLY_DATA
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#endif
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif
#if OILTANK_WITH_LFV
#include "Components/LocalFogVolumeComponent.h"   // VERIFY: header path of ULocalFogVolumeComponent in 5.8
#endif
#if OILTANK_PERF_COUNTERS
#include "RHI.h"
#include "RenderCore.h"
#endif

namespace
{
	FString PerfFile() { return TEXT("oiltank-perf.txt"); }
	FString CalibFile() { return TEXT("oiltank-calib.txt"); }

	bool CmdValue(const TCHAR* Key, FString& Out)
	{
		// bShouldStopOnSeparator=false: keep ',' and ')' inside paths
		if (FParse::Value(FCommandLine::Get(), Key, Out, false))
		{
			Out.TrimQuotesInline();
			if (!Out.IsEmpty() && FPaths::IsRelative(Out))
			{
				Out = FPaths::ConvertRelativePathToFull(Out);
			}
			return !Out.IsEmpty();
		}
		return false;
	}

	float SrgbToLinear(float C)
	{
		return C <= 0.04045f ? C / 12.92f : FMath::Pow((C + 0.055f) / 1.055f, 2.4f);
	}

	// Per-channel median and 1st percentile of an RGBA8 image (sRGB code values).
	void ChannelStats(const TArray<uint8>& RGBA, FVector3f& Median, FVector3f& P01)
	{
		const int64 N = RGBA.Num() / 4;
		for (int32 C = 0; C < 3; ++C)
		{
			int64 Hist[256] = {};
			for (int64 I = 0; I < N; ++I) { ++Hist[RGBA[I * 4 + C]]; }
			int64 Acc = 0;
			int32 Med = -1, Low = -1;
			for (int32 V = 0; V < 256; ++V)
			{
				Acc += Hist[V];
				if (Low < 0 && Acc >= N / 100) { Low = V; }
				if (Med < 0 && Acc >= N / 2) { Med = V; }
			}
			Median[C] = (float)FMath::Max(Med, 0);
			P01[C] = (float)FMath::Max(Low, 0);
		}
	}
}

AOilTankGameMode::AOilTankGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	// No pawn: the Entry map has no PlayerStart and a DefaultPawn would put a sphere in the shot.
	DefaultPawnClass = nullptr;
}

// ------------------------------------------------------------------------------------------------
// Setup
// ------------------------------------------------------------------------------------------------

void AOilTankGameMode::BeginPlay()
{
	Super::BeginPlay();

	LoadInputs();
	AppendLine(PerfFile(), FString::Printf(TEXT("=== OilTank %s  cmd: %s"), *FDateTime::Now().ToString(), FCommandLine::Get()));
	AppendLine(PerfFile(), FString::Printf(TEXT("FluidWallpaper.exe running: %s  (UE timings are NOT valid while the wallpaper or a game shares the GPU)"),
		FPlatformProcess::IsApplicationRunning(TEXT("FluidWallpaper.exe")) ? TEXT("YES") : TEXT("no")));
	if (bInputError)
	{
		Finish(TEXT("input error (see log)"));
		return;
	}

	ApplyCVars();
	RemoveForeignActors();

	SceneActor = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity);
	SceneRoot = NewObject<USceneComponent>(SceneActor, TEXT("OilTankRoot"));
	SceneRoot->SetMobility(EComponentMobility::Movable);
	SceneActor->SetRootComponent(SceneRoot);
	SceneRoot->RegisterComponent();
	OwnedActors.Add(SceneActor);

	CreateMaterials();
	SpawnCameraAndPost();
	SpawnLight();
	SpawnWater();
	SpawnFloor();
	BuildOil();
	SpawnDroplets();
	SpawnMotes();
	UpdateMotes(BaseSimT - (double)S.WarmupFrames / (ClipDir.IsEmpty() ? S.StillSimFps : S.ClipFps));

#if WITH_EDITOR
	// The runtime material compiles asynchronously; block once here so warmup frames are real frames.
	if (GShaderCompilingManager)
	{
		GShaderCompilingManager->FinishAllCompilation();
	}
#endif

	const FString Summary = FString::Printf(TEXT("settings: %s | dump=%s t=%.3f synthetic=%d film_png=%s | cam dist=%.1fcm fov=%.3fdeg fstop=%.2f | lux=%.3f boost=%.3f | tiles=%d tris=%d verts=%d segs=%d"),
		*S.Summary(), *Dump.HeightPng, Dump.T, Dump.bSynthetic ? 1 : 0, *Dump.FilmPng, CameraDistanceCm, CameraFovDeg, ApertureFstop,
		LightLuxCalibrated() * FloorBoost, FloorBoost, OilTiles.Num(), OilMesh.Triangles.Num() / 3, OilMesh.Vertices.Num(), OilMesh.NumBoundarySegments);
	UE_LOG(LogOilTank, Log, TEXT("%s"), *Summary);
	AppendLine(PerfFile(), Summary);

	LastWall = FPlatformTime::Seconds();
	PerfWindowStart = LastWall;
}

bool AOilTankGameMode::LoadDumpFile(const FString& Path)
{
	FString Err;
	FOilTankDump D;
	if (!D.LoadFromFile(Path, Err))
	{
		UE_LOG(LogOilTank, Error, TEXT("dump %s: %s"), *Path, *Err);
		return false;
	}
	FOilTankImage Img;
	if (!OilTankMesh::LoadHeightPng(D.HeightPng, Img, Err))
	{
		UE_LOG(LogOilTank, Error, TEXT("height_png: %s"), *Err);
		return false;
	}
	if (Img.Width != D.Width || Img.Height != D.Height)
	{
		UE_LOG(LogOilTank, Warning, TEXT("height_png is %dx%d, dump says %dx%d (mapping is uv-based, continuing)"), Img.Width, Img.Height, D.Width, D.Height);
	}
	OilTankMesh::ResampleToGrid(Img, S.OilGridX, S.OilGridY, HeightGrid);
	Dump = D;
	return true;
}

void AOilTankGameMode::LoadInputs()
{
	FString ConfigPath;
	if (!CmdValue(TEXT("OilConfig="), ConfigPath))
	{
		ConfigPath = FPaths::Combine(FPaths::ProjectConfigDir(), TEXT("OilTank.json"));
	}
	FString Err;
	if (!S.LoadFromFile(ConfigPath, Err))
	{
		UE_LOG(LogOilTank, Warning, TEXT("config %s: %s -- using built-in defaults"), *ConfigPath, *Err);
	}

	FParse::Value(FCommandLine::Get(), TEXT("ResX="), S.ResX);   // DoF px maths follows the real output width
	CmdValue(TEXT("OilShot="), ShotPath);
	CmdValue(TEXT("OilClip="), ClipDir);
	CmdValue(TEXT("OilDumpDir="), DumpDirSeq);
	FString DumpPath;
	CmdValue(TEXT("OilDump="), DumpPath);
	if (DumpPath.IsEmpty() && !DumpDirSeq.IsEmpty())
	{
		DumpPath = FPaths::Combine(DumpDirSeq, TEXT("dump_0000.json"));
	}

	if (!DumpPath.IsEmpty())
	{
		if (!LoadDumpFile(DumpPath))
		{
			// An A/B must never silently fall back to a made-up scene.
			bInputError = true;
			return;
		}
	}
	else
	{
		UE_LOG(LogOilTank, Warning, TEXT("no -OilDump: using the SYNTHETIC test field (not an A/B input)"));
		Dump = FOilTankDump();
		Dump.bSynthetic = true;
		OilTankMesh::MakeSyntheticGrid(S.OilGridX, S.OilGridY, S.FrameWidthCm / S.FrameHeightCm, S.SyntheticBlobs, S.SyntheticSeed, HeightGrid);
	}
	BaseSimT = Dump.T;

	const float MaxFilm = FMath::Max3(Dump.FilmRGB.R, Dump.FilmRGB.G, Dump.FilmRGB.B);
	FloorBoost = Dump.FilmPng.IsEmpty() ? FMath::Max(1.0f, MaxFilm) : 1.0f;

	if (!ClipDir.IsEmpty())
	{
		IFileManager::Get().MakeDirectory(*ClipDir, true);
	}
	if (!ShotPath.IsEmpty())
	{
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(ShotPath), true);
	}
}

void AOilTankGameMode::ApplyCVars()
{
	auto Set = [](const FString& Name, const FString& Value)
	{
		IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(*Name);
		if (!Var)
		{
			UE_LOG(LogOilTank, Warning, TEXT("cvar %s not found"), *Name);
			return;
		}
		Var->Set(*Value, ECVF_SetByCode);
		UE_LOG(LogOilTank, Log, TEXT("cvar %s = %s (now %s)"), *Name, *Value, *Var->GetString());
	};

	// Trap 7: say which. ssaa2 = no temporal AA, render at 200% and downsample (stills: no TSR
	// history smear on the motes, no ghosting); tsr = what a real-time game would ship.
	if (S.AA.Equals(TEXT("ssaa2"), ESearchCase::IgnoreCase))
	{
		Set(TEXT("r.AntiAliasingMethod"), TEXT("0"));
		Set(TEXT("r.ScreenPercentage"), TEXT("200"));   // VERIFY: >100 honoured in -game without TSR
	}
	else if (S.AA.Equals(TEXT("tsr"), ESearchCase::IgnoreCase))
	{
		Set(TEXT("r.AntiAliasingMethod"), TEXT("4"));
		Set(TEXT("r.ScreenPercentage"), TEXT("100"));
	}
	else if (S.AA.Equals(TEXT("taa"), ESearchCase::IgnoreCase))
	{
		Set(TEXT("r.AntiAliasingMethod"), TEXT("2"));
		Set(TEXT("r.ScreenPercentage"), TEXT("100"));
	}
	else
	{
		Set(TEXT("r.AntiAliasingMethod"), TEXT("0"));
		Set(TEXT("r.ScreenPercentage"), TEXT("100"));
	}

	for (const TPair<FString, FString>& KV : S.CVars)
	{
		Set(KV.Key, KV.Value);
	}
}

void AOilTankGameMode::RemoveForeignActors()
{
	// Whatever /Engine/Maps/Entry carries (lights, sky, fog, post) would break "one light, no ambient".
	TArray<AActor*> Kill;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		AActor* A = *It;
		if (A == this || OwnedActors.Contains(A))
		{
			continue;
		}
		if (A->FindComponentByClass<ULightComponentBase>() || A->FindComponentByClass<UExponentialHeightFogComponent>() ||
			A->FindComponentByClass<USkyAtmosphereComponent>() || A->IsA<APostProcessVolume>())
		{
			Kill.Add(A);
		}
	}
	for (AActor* A : Kill)
	{
		UE_LOG(LogOilTank, Warning, TEXT("removing map actor %s"), *A->GetName());
		A->Destroy();
	}
}

void AOilTankGameMode::CreateMaterials()
{
	WhiteTex = OilTankMesh::MakeSolidTexture(FColor::White);

#if WITH_EDITORONLY_DATA
	// One runtime lit material: BaseColor = Color * Albedo(tex), Specular, Roughness. Built in C++ (no
	// assets); compiles because -game runs inside UnrealEditor.exe. VERIFY (5.8): GetExpressionCollection,
	// GetEditorOnlyData, FExpressionInput::Connect, PostEditChange compiling a transient material.
	UMaterial* Mat = NewObject<UMaterial>(GetTransientPackage(), TEXT("M_OilTankRuntime"), RF_Transient);

	UMaterialExpressionVectorParameter* ColorP = NewObject<UMaterialExpressionVectorParameter>(Mat);
	ColorP->ParameterName = TEXT("Color");
	ColorP->DefaultValue = FLinearColor::White;

	UMaterialExpressionTextureSampleParameter2D* TexP = NewObject<UMaterialExpressionTextureSampleParameter2D>(Mat);
	TexP->ParameterName = TEXT("Albedo");
	TexP->Texture = WhiteTex;
	TexP->SamplerType = SAMPLERTYPE_Color;

	UMaterialExpressionMultiply* Mul = NewObject<UMaterialExpressionMultiply>(Mat);
	Mul->A.Connect(0, ColorP);
	Mul->B.Connect(0, TexP);   // output 0 = RGB

	UMaterialExpressionScalarParameter* SpecP = NewObject<UMaterialExpressionScalarParameter>(Mat);
	SpecP->ParameterName = TEXT("Specular");
	SpecP->DefaultValue = 0.0f;

	UMaterialExpressionScalarParameter* RoughP = NewObject<UMaterialExpressionScalarParameter>(Mat);
	RoughP->ParameterName = TEXT("Roughness");
	RoughP->DefaultValue = 1.0f;

	for (UMaterialExpression* E : TArray<UMaterialExpression*>{ ColorP, TexP, Mul, SpecP, RoughP })
	{
		E->Material = Mat;
		Mat->GetExpressionCollection().AddExpression(E);
	}
	UMaterialEditorOnlyData* ED = Mat->GetEditorOnlyData();
	ED->BaseColor.Connect(0, Mul);
	ED->Specular.Connect(0, SpecP);
	ED->Roughness.Connect(0, RoughP);

	Mat->SetShadingModel(MSM_DefaultLit);
	Mat->TwoSided = true;                         // winding-proof for the procedural meshes
	Mat->bUsedWithInstancedStaticMeshes = true;   // motes + droplets
	Mat->PreEditChange(nullptr);
	Mat->PostEditChange();
	BaseMaterial = Mat;
#else
	// Packaged build fallback: engine material with a "Color" parameter (no texture, default specular).
	BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	UE_LOG(LogOilTank, Warning, TEXT("no WITH_EDITORONLY_DATA: film_png and specular=0 are unavailable; run via UnrealEditor.exe -game"));
#endif

	OilMID = MakeMID(FLinearColor::Black, 0.0f, 1.0f, WhiteTex);
	MoteMID = MakeMID(S.MoteAlbedo, S.MoteSpecular, S.MoteRoughness, WhiteTex);
	FloorMID = MakeMID(FLinearColor::White, S.FloorSpecular, S.FloorRoughness, WhiteTex);
}

UMaterialInstanceDynamic* AOilTankGameMode::MakeMID(const FLinearColor& Color, float Specular, float Roughness, UTexture2D* Albedo)
{
	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(BaseMaterial, this);
	MID->SetVectorParameterValue(TEXT("Color"), Color);
	MID->SetScalarParameterValue(TEXT("Specular"), Specular);
	MID->SetScalarParameterValue(TEXT("Roughness"), Roughness);
	if (Albedo)
	{
		MID->SetTextureParameterValue(TEXT("Albedo"), Albedo);
	}
	return MID;
}

FVector AOilTankGameMode::ToLightDir() const
{
	// Screen space: right = +Y world, up = +X world. Azimuth 0 = light comes FROM screen right,
	// 90 = from screen top (counter-clockwise on screen). Elevation above the water surface.
	const double A = FMath::DegreesToRadians((double)S.LightAzimDeg);
	const double E = FMath::DegreesToRadians((double)S.LightElevDeg);
	return FVector(FMath::Sin(A) * FMath::Cos(E), FMath::Cos(A) * FMath::Cos(E), FMath::Sin(E)).GetSafeNormal();
}

FVector2D AOilTankGameMode::VisibleFloorSizeCm() const
{
	const double Scale = S.CameraMode.Equals(TEXT("ortho"), ESearchCase::IgnoreCase) || CameraDistanceCm <= 0.0
		? 1.0 : (CameraDistanceCm + S.TankDepthCm) / CameraDistanceCm;
	return FVector2D(S.FrameHeightCm * Scale, S.FrameWidthCm * Scale);
}

float AOilTankGameMode::LightLuxCalibrated() const
{
	if (S.LightLux > 0.0f)
	{
		return S.LightLux;
	}
	// Trap 3: light x albedo calibrated so an unshadowed white-albedo floor displays 1.0 (so a film-
	// coloured floor displays the film colour). Lambert: L = albedo * E * sin(elev) / pi.
	// VERIFY: manual exposure with physical-camera exposure OFF gives pixel = L * 2^bias / 1.2.
	// The oiltank-calib.txt readback closes the loop: adjust calib_gain if it reports a ratio != 1.
	const float SinE = FMath::Sin(FMath::DegreesToRadians(S.LightElevDeg));
	float T = 1.0f;
	if (S.bCalibThroughWater && !S.bDebugNoWater)
	{
		const float Tau = S.WaterOpticalDepth;
		T = FMath::Exp(-Tau / SinE) * FMath::Exp(-Tau);   // slant path down, vertical path up
	}
	const float ExposureScale = FMath::Pow(2.0f, S.ExposureBias) / 1.2f;
	return PI * S.CalibGain / (SinE * ExposureScale * T);
}

FLinearColor AOilTankGameMode::WaterTint() const
{
	FLinearColor C = S.bWaterColorFromFilm ? Dump.FilmRGB : S.WaterColor;
	const float M = FMath::Max3(C.R, C.G, C.B);
	if (M > 1e-6f)
	{
		C = C * (1.0f / M);
	}
	C = C * S.WaterAlbedo;
	return FLinearColor(FMath::Clamp(C.R, 0.0f, 1.0f), FMath::Clamp(C.G, 0.0f, 1.0f), FMath::Clamp(C.B, 0.0f, 1.0f), 1.0f);
}

void AOilTankGameMode::SpawnCameraAndPost()
{
	UWorld* World = GetWorld();
	const bool bOrtho = S.CameraMode.Equals(TEXT("ortho"), ESearchCase::IgnoreCase);
	const double W = S.FrameWidthCm;

	if (bOrtho)
	{
		CameraDistanceCm = S.OrthoHeightCm;
		CameraFovDeg = 0.0;
	}
	else if (S.CameraDistanceCm > 0.0f)
	{
		CameraDistanceCm = S.CameraDistanceCm;
		CameraFovDeg = FMath::RadiansToDegrees(2.0 * FMath::Atan((W * 0.5) / CameraDistanceCm));
	}
	else
	{
		CameraFovDeg = S.CameraFovDeg;
		CameraDistanceCm = (W * 0.5) / FMath::Tan(FMath::DegreesToRadians(CameraFovDeg * 0.5));
	}

	// Looking straight down: pitch -90 -> forward = -Z, camera up = +X (= screen up = the rise).
	Camera = World->SpawnActor<ACameraActor>(FVector(0.0, 0.0, CameraDistanceCm), FRotator(-90.0, 0.0, 0.0));
	OwnedActors.Add(Camera);
	UCameraComponent* CC = Camera->GetCameraComponent();
	CC->SetConstraintAspectRatio(false);
	CC->PostProcessBlendWeight = 0.0f;   // the unbound volume owns all post
	if (bOrtho)
	{
		// VERIFY (5.8): ortho near/far with bAutoCalculateOrthoPlanes; DoF/volumetric fog in ortho.
		CC->SetProjectionMode(ECameraProjectionMode::Orthographic);
		CC->SetOrthoWidth(W);
		CC->SetOrthoNearClipPlane(1.0f);
		CC->SetOrthoFarClipPlane(CameraDistanceCm + S.TankDepthCm + 100.0);
	}
	else
	{
		CC->SetProjectionMode(ECameraProjectionMode::Perspective);
		CC->SetFieldOfView(CameraFovDeg);   // horizontal FOV: the frame width fills the view at z=0
	}

	// Aperture: either fixed, or solved so a mote ON THE FLOOR has a CoC of dof_px_at_floor px
	// (thin lens: c = f^2 / (N (s - f)) * |d - s| / d), matching our own dof_max_px for the A/B.
	if (!bOrtho)
	{
		const double SensorMm = S.SensorWidthMm;
		const double FMm = (SensorMm * 0.5) / FMath::Tan(FMath::DegreesToRadians(CameraFovDeg * 0.5));
		const double SMm = CameraDistanceCm * 10.0;
		const double DMm = (CameraDistanceCm + S.TankDepthCm) * 10.0;
		const double CocMmAtN1 = (FMm * FMm) / (SMm - FMm) * (DMm - SMm) / DMm;
		const double CocPxAtN1 = CocMmAtN1 / SensorMm * S.ResX;
		ApertureFstop = S.ApertureFstop > 0.0f ? S.ApertureFstop : FMath::Clamp(CocPxAtN1 / FMath::Max(0.5f, S.DofPxAtFloor), 1.0, 64.0);
	}

	PostVolume = World->SpawnActor<APostProcessVolume>(APostProcessVolume::StaticClass(), FTransform::Identity);
	OwnedActors.Add(PostVolume);
	PostVolume->bUnbound = true;
	PostVolume->BlendWeight = 1.0f;
	PostVolume->Priority = 1000.0f;
	FPostProcessSettings& P = PostVolume->Settings;

	// Trap 2: fixed exposure, no bloom / vignette / flare / motion blur / fringe / AO, neutral tone.
	P.bOverride_AutoExposureMethod = true;
	P.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
	P.bOverride_AutoExposureBias = true;
	P.AutoExposureBias = S.ExposureBias;
#if OILTANK_PP_EXTRAS
	// VERIFY (5.8) field names. ToneCurveAmount=0 removes the filmic S-curve (no ACES toe/shoulder):
	// the closest UE gets to "linear * exposure -> sRGB".
	P.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	P.AutoExposureApplyPhysicalCameraExposure = 0;
	P.bOverride_ToneCurveAmount = true;
	P.ToneCurveAmount = 0.0f;
	P.bOverride_LocalExposureHighlightContrastScale = true;
	P.LocalExposureHighlightContrastScale = 1.0f;
	P.bOverride_LocalExposureShadowContrastScale = true;
	P.LocalExposureShadowContrastScale = 1.0f;
	P.bOverride_LocalExposureDetailStrength = true;
	P.LocalExposureDetailStrength = 1.0f;
#endif
	P.bOverride_BlueCorrection = true;
	P.BlueCorrection = 0.0f;
	P.bOverride_ExpandGamut = true;
	P.ExpandGamut = 0.0f;
	P.bOverride_BloomIntensity = true;
	P.BloomIntensity = 0.0f;
	P.bOverride_VignetteIntensity = true;
	P.VignetteIntensity = 0.0f;
	P.bOverride_LensFlareIntensity = true;
	P.LensFlareIntensity = 0.0f;
	P.bOverride_MotionBlurAmount = true;
	P.MotionBlurAmount = 0.0f;
	P.bOverride_SceneFringeIntensity = true;
	P.SceneFringeIntensity = 0.0f;
	P.bOverride_AmbientOcclusionIntensity = true;
	P.AmbientOcclusionIntensity = 0.0f;
	P.bOverride_FilmGrainIntensity = true;
	P.FilmGrainIntensity = S.FilmGrain;

	// Cinematic (diaphragm) DoF focused on the water surface; deep motes and the floor defocus.
	P.bOverride_DepthOfFieldFocalDistance = true;
	if (!bOrtho && S.bDof)
	{
		P.DepthOfFieldFocalDistance = CameraDistanceCm;
		P.bOverride_DepthOfFieldFstop = true;
		P.DepthOfFieldFstop = ApertureFstop;
		P.bOverride_DepthOfFieldMinFstop = true;
		P.DepthOfFieldMinFstop = 1.2f;
		P.bOverride_DepthOfFieldSensorWidth = true;
		P.DepthOfFieldSensorWidth = S.SensorWidthMm;
		P.bOverride_DepthOfFieldBladeCount = true;
		P.DepthOfFieldBladeCount = FMath::Clamp(S.DofBladeCount, 4, 16);
	}
	else
	{
		P.DepthOfFieldFocalDistance = 0.0f;   // 0 = DoF off
		if (bOrtho && S.bDof)
		{
			UE_LOG(LogOilTank, Warning, TEXT("camera_mode=ortho: DoF disabled (no physical CoC in ortho); use narrow for the A/B"));
		}
	}

	EnsureViewTarget();
}

void AOilTankGameMode::EnsureViewTarget()
{
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		PC->bAutoManageActiveCameraTarget = false;
		if (Camera && PC->GetViewTarget() != Camera)
		{
			PC->SetViewTarget(Camera);
		}
	}
}

void AOilTankGameMode::SpawnLight()
{
	const FVector ToLight = ToLightDir();
	Light = GetWorld()->SpawnActor<ADirectionalLight>(ADirectionalLight::StaticClass(),
		FTransform((-ToLight).Rotation(), FVector(0.0, 0.0, 500.0)));
	OwnedActors.Add(Light);
	UDirectionalLightComponent* L = Cast<UDirectionalLightComponent>(Light->GetLightComponent());
	check(L);
	L->SetMobility(EComponentMobility::Movable);   // dynamic only; r.AllowStaticLighting=False anyway
	L->SetIntensity(LightLuxCalibrated() * FloorBoost);   // lux
	L->SetLightColor(FLinearColor::White);
	L->SetCastShadows(true);
	L->SetLightSourceAngle(S.LightSourceAngleDeg);         // penumbra grows with depth (VSM SMRT)
	L->SetVolumetricScatteringIntensity(S.LightVolumetricScattering);
	L->bCastVolumetricShadow = true;                        // the oil's shafts in the water
	L->ContactShadowLength = S.ContactShadowLength;
	L->SetEnableLightShaftOcclusion(true);                  // screen-space shafts (only if the sun is in view)
	L->bEnableLightShaftBloom = false;
	L->DynamicShadowDistanceMovableLight = (float)(CameraDistanceCm + S.TankDepthCm * 4.0);   // CSM fallback only
	L->MarkRenderStateDirty();
}

void AOilTankGameMode::SpawnWater()
{
	UWorld* World = GetWorld();
	const double D = S.TankDepthCm;
	const bool bLfv = S.WaterMode.Equals(TEXT("localfog"), ESearchCase::IgnoreCase);
	const FLinearColor Tint = WaterTint();

	// Volumetric fog lives on the ExponentialHeightFog component, so one is always spawned.
	HeightFog = World->SpawnActor<AExponentialHeightFog>(AExponentialHeightFog::StaticClass(), FTransform(FVector(0.0, 0.0, 0.0)));
	OwnedActors.Add(HeightFog);
	UExponentialHeightFogComponent* F = HeightFog->GetComponent();
	F->SetVolumetricFog(true);
	F->SetVolumetricFogDistance((float)(CameraDistanceCm + D + S.FogGridMarginCm));
	F->SetVolumetricFogScatteringDistribution(S.WaterPhaseG);
	F->SetVolumetricFogAlbedo(Tint.ToFColor(true));
	F->SetVolumetricFogExtinctionScale(1.0f);
	F->SetFogMaxOpacity(1.0f);
	F->SetFogInscatteringColor(FLinearColor::Black);          // VERIFY (5.8 name; 5.1 renamed the property to FogInscatteringLuminance)
	F->SetDirectionalInscatteringColor(FLinearColor::Black);  // VERIFY (same)
	F->SetFogHeightFalloff(0.001f);                           // ~uniform over 1 m
#if OILTANK_HAS_VFOG_START
	// VERIFY (5.8): property name. Starts the froxel fog at the water surface (camera -> z=0 distance):
	// in heightfog mode this is what keeps the AIR above the water clear.
	F->VolumetricFogStartDistance = (float)FMath::Max(0.0, CameraDistanceCm - (bLfv ? S.FogGridMarginCm : 0.0));
#endif

	const float Tau = S.bDebugNoWater ? 0.0f : S.WaterOpticalDepth;
	if (!bLfv)
	{
		// VERIFY: UE height fog density is per 1000 uu (lessons: 0.006 washed a 10 km scene white).
		const float Raw = S.WaterDensityRaw > 0.0f ? S.WaterDensityRaw : Tau / (float)D * 1000.0f;
		F->SetFogDensity(S.bDebugNoWater ? 0.0f : Raw);
		F->SetStartDistance((float)CameraDistanceCm);
		UE_LOG(LogOilTank, Log, TEXT("water: heightfog density=%.5f start=%.1fcm"), Raw, CameraDistanceCm);
		return;
	}

	F->SetFogDensity(1e-6f);   // the global fog only hosts the volumetric grid

#if OILTANK_WITH_LFV
	if (S.bDebugNoWater)
	{
		return;
	}
	// A local fog volume is a sphere; squash it into a flat ellipsoid: centre at z=-D, half-height D
	// (top touches the surface), XY radius R >> the lit region so the top is flat to <1 mm over it.
	const FVector ToLight = ToLightDir();
	const double Shift = D * FVector2D(ToLight.X, ToLight.Y).Size() / FMath::Max(ToLight.Z, 0.05);
	const FVector2D Vis = VisibleFloorSizeCm();
	const double Reach = Shift + FMath::Max(Vis.X, Vis.Y);
	const double R = 20.0 * Reach;
	const double Unit = FMath::Max(1.0f, S.LfvUnitRadiusCm);

	WaterVolumeActor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity);
	OwnedActors.Add(WaterVolumeActor);
	ULocalFogVolumeComponent* LFV = NewObject<ULocalFogVolumeComponent>(WaterVolumeActor, TEXT("Water"));
	WaterVolumeActor->SetRootComponent(LFV);
	// VERIFY (5.8): property names/units. Extinction assumed per METRE; height fog with ~0 falloff =
	// uniform density inside the volume; radial part off.
	const float ExtPerM = S.WaterDensityRaw > 0.0f ? S.WaterDensityRaw : Tau / (float)(D / 100.0);
	LFV->RadialFogExtinction = 0.0f;
	LFV->HeightFogExtinction = ExtPerM;
	LFV->HeightFogFalloff = 0.01f;
	LFV->HeightFogOffset = 0.0f;
	LFV->FogPhaseG = S.WaterPhaseG;
	LFV->FogAlbedo = Tint;
	LFV->FogEmissive = FLinearColor::Black;
	LFV->RegisterComponent();
	WaterVolumeActor->SetActorTransform(FTransform(FRotator::ZeroRotator, FVector(0.0, 0.0, -D), FVector(R / Unit, R / Unit, D / Unit)));
	LFV->MarkRenderStateDirty();
	UE_LOG(LogOilTank, Log, TEXT("water: localfog ext=%.4f/m R=%.0fcm half-height=%.0fcm unit=%.0fcm"), ExtPerM, R, D, Unit);
#else
	UE_LOG(LogOilTank, Error, TEXT("water_mode=localfog but OILTANK_WITH_LFV=0: NO WATER. Use water_mode=heightfog."));
#endif
}

void AOilTankGameMode::SpawnFloor()
{
	const FVector2D Vis = VisibleFloorSizeCm();
	const double HX = Vis.X * (0.5 + S.FloorMargin);
	const double HY = Vis.Y * (0.5 + S.FloorMargin);
	auto UV = [&](double X, double Y) { return FVector2D(0.5 + Y / Vis.Y, 0.5 - X / Vis.X); };

	// Clockwise from above: TL, TR, BR, BL (screen up = +X, right = +Y).
	const TArray<FVector> V = { FVector(HX, -HY, 0), FVector(HX, HY, 0), FVector(-HX, HY, 0), FVector(-HX, -HY, 0) };
	const TArray<int32> T = { 0, 1, 2, 0, 2, 3 };
	const TArray<FVector> N = { FVector::UpVector, FVector::UpVector, FVector::UpVector, FVector::UpVector };
	const TArray<FVector2D> UV0 = { UV(HX, -HY), UV(HX, HY), UV(-HX, HY), UV(-HX, -HY) };

	Floor = NewObject<UProceduralMeshComponent>(SceneActor, TEXT("Floor"));
	Floor->SetMobility(EComponentMobility::Movable);
	Floor->SetupAttachment(SceneRoot);
	Floor->RegisterComponent();
	SceneActor->AddInstanceComponent(Floor);
	Floor->SetRelativeLocation(FVector(0.0, 0.0, -S.TankDepthCm));
	Floor->CreateMeshSection(0, V, T, N, UV0, TArray<FColor>(), TArray<FProcMeshTangent>(), false);
	Floor->SetCastShadow(false);
	Floor->SetMaterial(0, FloorMID);
	ApplyFilm();
}

void AOilTankGameMode::ApplyFilm()
{
	// The receiver: film_png (the film/ink without oil) as albedo, else the flat film colour.
	if (!Dump.FilmPng.IsEmpty())
	{
		FString Err;
		UTexture2D* Tex = OilTankMesh::LoadColorTexture(Dump.FilmPng, Err);
		if (Tex)
		{
			FilmTex = Tex;
			FloorMID->SetTextureParameterValue(TEXT("Albedo"), FilmTex);
			FloorMID->SetVectorParameterValue(TEXT("Color"), FLinearColor::White);
			return;
		}
		UE_LOG(LogOilTank, Warning, TEXT("film_png: %s -- using film_rgb"), *Err);
	}
	FloorMID->SetTextureParameterValue(TEXT("Albedo"), WhiteTex);
	FloorMID->SetVectorParameterValue(TEXT("Color"), Dump.FilmRGB * (1.0f / FloorBoost));
}

void AOilTankGameMode::BuildOil()
{
	if (S.bDebugNoOil)
	{
		for (UProceduralMeshComponent* Tile : OilTiles)
		{
			Tile->ClearAllMeshSections();
		}
		return;
	}

	const double W = S.FrameWidthCm;
	const double H = S.FrameHeightCm;
	const double StartMs = FPlatformTime::Seconds() * 1000.0;
	OilTankMesh::BuildOilMesh(HeightGrid, S.OilGridX, S.OilGridY, (float)W, (float)H, S.OilThreshold, S.OilThicknessCm, OilMesh);

	if (OilTiles.Num() == 0)
	{
		// Which frame-sized tiles must exist so every visible floor point and every lit column point
		// has its occluder: the visible floor shifted TOWARD the light by D*cot(elev), plus penumbra.
		// At 1 m depth and a 22.5 cm frame that is 3+ frame heights away: with "none", no in-frame oil
		// can shadow the in-frame floor at all (see README).
		int32 I0 = 0, I1 = 0, J0 = 0, J1 = 0;
		if (S.OilSurround.Equals(TEXT("mirror"), ESearchCase::IgnoreCase))
		{
			const FVector ToLight = ToLightDir();
			const double D = S.TankDepthCm;
			const double SX = D * ToLight.X / ToLight.Z;
			const double SY = D * ToLight.Y / ToLight.Z;
			const double Pen = D * FMath::Tan(FMath::DegreesToRadians((double)S.LightSourceAngleDeg)) + 1.0;
			const FVector2D Vis = VisibleFloorSizeCm();
			const double XMin = FMath::Min(-H * 0.5, -Vis.X * 0.5 + SX) - Pen, XMax = FMath::Max(H * 0.5, Vis.X * 0.5 + SX) + Pen;
			const double YMin = FMath::Min(-W * 0.5, -Vis.Y * 0.5 + SY) - Pen, YMax = FMath::Max(W * 0.5, Vis.Y * 0.5 + SY) + Pen;
			I0 = FMath::FloorToInt32(XMin / H + 0.5); I1 = FMath::FloorToInt32(XMax / H + 0.5);
			J0 = FMath::FloorToInt32(YMin / W + 0.5); J1 = FMath::FloorToInt32(YMax / W + 0.5);
		}
		for (int32 I = I0; I <= I1; ++I)
		{
			for (int32 J = J0; J <= J1; ++J)
			{
				// Mirror tiling keeps the oil continuous across tile edges.
				UProceduralMeshComponent* Tile = NewObject<UProceduralMeshComponent>(SceneActor, *FString::Printf(TEXT("Oil_%d_%d"), I, J));
				Tile->SetMobility(EComponentMobility::Movable);
				Tile->SetupAttachment(SceneRoot);
				Tile->RegisterComponent();
				SceneActor->AddInstanceComponent(Tile);
				Tile->SetRelativeTransform(FTransform(FRotator::ZeroRotator, FVector(I * H, J * W, 0.0),
					FVector((I & 1) ? -1.0 : 1.0, (J & 1) ? -1.0 : 1.0, 1.0)));
				Tile->SetCastShadow(true);
				Tile->bCastShadowAsTwoSided = true;
				OilTiles.Add(Tile);
			}
		}
		UE_LOG(LogOilTank, Log, TEXT("oil tiles X %d..%d  Y %d..%d (%d)"), I0, I1, J0, J1, OilTiles.Num());
	}

	for (UProceduralMeshComponent* Tile : OilTiles)
	{
		if (OilMesh.Triangles.Num() > 0)
		{
			Tile->CreateMeshSection(0, OilMesh.Vertices, OilMesh.Triangles, OilMesh.Normals, OilMesh.UV0,
				TArray<FColor>(), TArray<FProcMeshTangent>(), false);
			Tile->SetMaterial(0, OilMID);
		}
		else
		{
			Tile->ClearAllMeshSections();
		}
	}
	UE_LOG(LogOilTank, Log, TEXT("oil mesh: %d verts, %d tris, %d boundary segments, %.1f ms"),
		OilMesh.Vertices.Num(), OilMesh.Triangles.Num() / 3, OilMesh.NumBoundarySegments, FPlatformTime::Seconds() * 1000.0 - StartMs);
}

void AOilTankGameMode::SpawnDroplets()
{
	// Droplets with depth ~0 are already in height_png; deeper ones become black spheres in the column.
	if (!Droplets)
	{
		Droplets = NewObject<UInstancedStaticMeshComponent>(SceneActor, TEXT("Droplets"));
		Droplets->SetMobility(EComponentMobility::Movable);
		Droplets->SetupAttachment(SceneRoot);
		Droplets->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")));
		Droplets->SetMaterial(0, OilMID);
		Droplets->SetCastShadow(true);
		Droplets->RegisterComponent();
		SceneActor->AddInstanceComponent(Droplets);
	}
	Droplets->ClearInstances();
	if (S.bDebugNoOil)
	{
		return;
	}
	int32 Count = 0;
	for (const FOilTankDroplet& Dr : Dump.Droplets)
	{
		if (Dr.Depth < S.DropletMinDepth || Dr.R <= 0.0f)
		{
			continue;
		}
		const double RCm = Dr.R * S.FrameWidthCm;
		const FVector P((0.5 - Dr.Y) * S.FrameHeightCm, (Dr.X - 0.5) * S.FrameWidthCm, -Dr.Depth * S.TankDepthCm);
		Droplets->AddInstance(FTransform(FRotator::ZeroRotator, P, FVector(RCm * 2.0 / 100.0)));   // engine sphere = 100 cm across
		++Count;
	}
	UE_LOG(LogOilTank, Log, TEXT("droplets: %d of %d below the surface"), Count, Dump.Droplets.Num());
}

void AOilTankGameMode::SpawnMotes()
{
	if (S.bDebugNoMotes || S.MoteCount <= 0)
	{
		return;
	}
	// Trap 6: opaque meshes (no translucency) so the cinematic DoF blurs them by depth.
	Motes = NewObject<UInstancedStaticMeshComponent>(SceneActor, TEXT("Motes"));
	Motes->SetMobility(EComponentMobility::Movable);
	Motes->SetupAttachment(SceneRoot);
	Motes->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")));
	Motes->SetMaterial(0, MoteMID);
	Motes->SetCastShadow(false);   // receive the oil's shadow, cast none
	Motes->RegisterComponent();
	SceneActor->AddInstanceComponent(Motes);

	const FVector2D Vis = VisibleFloorSizeCm() * 1.05;
	FRandomStream Rng(S.MoteSeed);
	MoteSeeds.SetNum(S.MoteCount);
	MoteXforms.SetNum(S.MoteCount);
	const double Scale = S.MoteSizeMm / 1000.0;   // mm -> cm, / 100 cm sphere
	for (int32 K = 0; K < S.MoteCount; ++K)
	{
		MoteSeeds[K] = FVector4f(
			Rng.FRandRange(-0.5f, 0.5f) * (float)Vis.X,
			Rng.FRandRange(-0.5f, 0.5f) * (float)Vis.Y,
			-Rng.FRandRange(S.MoteZMinCm, (float)S.TankDepthCm),
			Rng.FRandRange(0.0f, 2.0f * PI));
		MoteXforms[K] = FTransform(FRotator::ZeroRotator, FVector(MoteSeeds[K].X, MoteSeeds[K].Y, MoteSeeds[K].Z), FVector(Scale));
		Motes->AddInstance(MoteXforms[K]);
	}
}

void AOilTankGameMode::UpdateMotes(double SimTime)
{
	if (!Motes || MoteSeeds.Num() == 0)
	{
		return;
	}
	// Positions are a pure function of SimTime (reproducible stills and clips): a slow rise up the
	// screen (+X), wrapping from the top back in at the bottom, plus a small smooth wander.
	const FVector2D Vis = VisibleFloorSizeCm() * 1.05;
	const double Scale = S.MoteSizeMm / 1000.0;
	for (int32 K = 0; K < MoteSeeds.Num(); ++K)
	{
		const FVector4f& M = MoteSeeds[K];
		const double Ph = M.W;
		double X = M.X + S.MoteRiseCmS * SimTime + S.MoteJitterCm * FMath::Sin(0.71 * SimTime + Ph);
		X -= Vis.X * FMath::FloorToDouble(X / Vis.X + 0.5);
		const double Y = M.Y + S.MoteJitterCm * FMath::Sin(0.53 * SimTime + 2.1 * Ph);
		MoteXforms[K] = FTransform(FRotator::ZeroRotator, FVector(X, Y, M.Z), FVector(Scale));
	}
	// VERIFY (5.8 signature): (StartIndex, Transforms, bWorldSpace, bMarkRenderStateDirty, bTeleport)
	Motes->BatchUpdateInstancesTransforms(0, MoteXforms, false, true, true);
}

// ------------------------------------------------------------------------------------------------
// Frame loop: warmup -> still and/or clip -> wait for files -> exit (or perf-only run)
// ------------------------------------------------------------------------------------------------

void AOilTankGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Phase == EPhase::Done || bInputError)
	{
		return;
	}
	TickPerf();
	EnsureViewTarget();

	const bool bClipRun = !ClipDir.IsEmpty();
	const double SimFps = bClipRun ? S.ClipFps : S.StillSimFps;

	switch (Phase)
	{
	case EPhase::Warmup:
	{
		// SimTime reaches the dump's t exactly on the capture frame.
		UpdateMotes(BaseSimT + (double)(FrameIndex - S.WarmupFrames) / SimFps);
		if (FrameIndex >= S.WarmupFrames)
		{
			if (!ShotPath.IsEmpty())
			{
				FScreenshotRequest::RequestScreenshot(ShotPath, false, false);
				PendingFiles.Add(ShotPath);
				UE_LOG(LogOilTank, Log, TEXT("still requested: %s"), *ShotPath);
			}
			if (bClipRun)
			{
				Phase = EPhase::Clip;
			}
			else if (!ShotPath.IsEmpty())
			{
				Phase = EPhase::WaitFiles;
			}
			else
			{
				Phase = EPhase::Perf;
				PerfStart = FPlatformTime::Seconds();
			}
		}
		break;
	}
	case EPhase::Clip:
	{
		if (FScreenshotRequest::IsScreenshotRequested())
		{
			break;   // previous capture not consumed yet; render the same instant again
		}
		if (!DumpDirSeq.IsEmpty() && ClipIndex > 0)
		{
			const FString Next = FPaths::Combine(DumpDirSeq, FString::Printf(TEXT("dump_%04d.json"), ClipIndex));
			if (FPaths::FileExists(Next) && LoadDumpFile(Next))
			{
				BuildOil();
				SpawnDroplets();
				ApplyFilm();
			}
		}
		UpdateMotes(BaseSimT + (double)ClipIndex / S.ClipFps);
		const FString Frame = FPaths::Combine(ClipDir, FString::Printf(TEXT("frame_%04d.png"), ClipIndex));
		FScreenshotRequest::RequestScreenshot(Frame, false, false);
		PendingFiles.Add(Frame);
		if (++ClipIndex >= S.ClipFrames)
		{
			Phase = EPhase::WaitFiles;
		}
		break;
	}
	case EPhase::WaitFiles:
	{
		++WaitFrames;
		bool bAll = !FScreenshotRequest::IsScreenshotRequested();
		for (const FString& P : PendingFiles)
		{
			if (!bAll) { break; }
			bAll = IFileManager::Get().FileSize(*P) > 0;
		}
		FoundFrames = bAll ? FoundFrames + 1 : 0;
		if (FoundFrames >= 5)   // a few frames of grace so the last PNG is fully written
		{
			if (!ShotPath.IsEmpty())
			{
				WriteCalibReport(ShotPath);
			}
			Finish(TEXT("captured"));
		}
		else if (WaitFrames > 900)
		{
			UE_LOG(LogOilTank, Error, TEXT("timed out waiting for %d capture file(s)"), PendingFiles.Num());
			Finish(TEXT("ERROR: capture files missing"));
		}
		break;
	}
	case EPhase::Perf:
	{
		UpdateMotes(BaseSimT + (double)(FrameIndex - S.WarmupFrames) / SimFps);
		if (FPlatformTime::Seconds() - PerfStart >= S.PerfSeconds)
		{
			Finish(TEXT("perf run complete"));
		}
		break;
	}
	default:
		break;
	}
	++FrameIndex;
}

void AOilTankGameMode::TickPerf()
{
	const double Now = FPlatformTime::Seconds();
	const double Ms = (Now - LastWall) * 1000.0;
	LastWall = Now;
	if (FrameIndex > 0)
	{
		PerfSumMs += Ms;
		PerfMaxMs = FMath::Max(PerfMaxMs, Ms);
		++PerfFrames;
	}
	if (Now - PerfWindowStart >= 1.0 && PerfFrames > 0)
	{
		const TCHAR* PhaseName = Phase == EPhase::Warmup ? TEXT("warmup") : Phase == EPhase::Clip ? TEXT("clip(png writes: not real-time)")
			: Phase == EPhase::WaitFiles ? TEXT("wait") : TEXT("perf");
		float GpuMs = 0.0f, GameMs = 0.0f, RenderMs = 0.0f;
#if OILTANK_PERF_COUNTERS
		// VERIFY (5.8): RHIGetGPUFrameCycles / GGameThreadTime / GRenderThreadTime
		GpuMs = (float)FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0));
		GameMs = (float)FPlatformTime::ToMilliseconds(GGameThreadTime);
		RenderMs = (float)FPlatformTime::ToMilliseconds(GRenderThreadTime);
#endif
		AppendLine(PerfFile(), FString::Printf(TEXT("frame=%d phase=%s frames=%d avg_ms=%.2f max_ms=%.2f fps=%.1f gpu_ms=%.2f game_ms=%.2f render_ms=%.2f"),
			FrameIndex, PhaseName, PerfFrames, PerfSumMs / PerfFrames, PerfMaxMs, 1000.0 * PerfFrames / PerfSumMs, GpuMs, GameMs, RenderMs));
		PerfWindowStart = Now;
		PerfSumMs = 0.0;
		PerfMaxMs = 0.0;
		PerfFrames = 0;
	}
}

void AOilTankGameMode::WriteCalibReport(const FString& Path)
{
	int32 W = 0, H = 0;
	TArray<uint8> Shot;
	FString Err;
	if (!OilTankMesh::LoadPngRGBA8(Path, W, H, Shot, Err))
	{
		AppendLine(CalibFile(), FString::Printf(TEXT("calib: cannot read shot: %s"), *Err));
		return;
	}
	FVector3f Med, P01;
	ChannelStats(Shot, Med, P01);

	FVector3f Target;
	FString TargetSrc;
	TArray<uint8> Film;
	int32 FW = 0, FH = 0;
	if (!Dump.FilmPng.IsEmpty() && OilTankMesh::LoadPngRGBA8(Dump.FilmPng, FW, FH, Film, Err))
	{
		FVector3f Dummy;
		ChannelStats(Film, Target, Dummy);
		TargetSrc = TEXT("film_png median");
	}
	else
	{
		const FColor C = Dump.FilmRGB.ToFColor(true);
		Target = FVector3f(C.R, C.G, C.B);
		TargetSrc = TEXT("film_rgb");
	}
	auto Luma = [](const FVector3f& Srgb8)
	{
		return 0.2126f * SrgbToLinear(Srgb8.X / 255.0f) + 0.7152f * SrgbToLinear(Srgb8.Y / 255.0f) + 0.0722f * SrgbToLinear(Srgb8.Z / 255.0f);
	};
	const float Ratio = Luma(Target) / FMath::Max(1e-5f, Luma(Med));
	AppendLine(CalibFile(), FString::Printf(
		TEXT("%s shot=%s %dx%d median_srgb8=(%.0f,%.0f,%.0f) p01_srgb8=(%.0f,%.0f,%.0f) target_srgb8=(%.0f,%.0f,%.0f) [%s] "
			 "linear_luma_ratio target/shot=%.3f -> calib_gain %.3f would match (meaningful only with debug_no_oil=true debug_no_motes=true, "
			 "and debug_no_water=true or calib_through_water=true). Black level p01 must be ~0 on an oil-covered frame."),
		*FDateTime::Now().ToString(), *Path, W, H, Med.X, Med.Y, Med.Z, P01.X, P01.Y, P01.Z, Target.X, Target.Y, Target.Z, *TargetSrc,
		Ratio, S.CalibGain * Ratio));
}

void AOilTankGameMode::AppendLine(const FString& FileName, const FString& Line) const
{
	const FString Path = FPaths::Combine(FPaths::ProjectLogDir(), FileName);
	FFileHelper::SaveStringToFile(Line + LINE_TERMINATOR, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
		&IFileManager::Get(), FILEWRITE_Append);
}

void AOilTankGameMode::Finish(const TCHAR* Why)
{
	Phase = EPhase::Done;
	UE_LOG(LogOilTank, Log, TEXT("finish: %s"), Why);
	AppendLine(PerfFile(), FString::Printf(TEXT("finish: %s"), Why));
	FPlatformMisc::RequestExit(false);
}
