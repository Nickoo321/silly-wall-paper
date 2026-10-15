#pragma once

#include "CoreMinimal.h"

// One droplet from the dump. x,y in frame uv (0..1, origin top-left, v down), r = radius as a
// fraction of the frame WIDTH, depth = 0..1 fraction of tank_depth_cm below the surface.
struct FOilTankDroplet
{
	float X = 0.5f;
	float Y = 0.5f;
	float R = 0.0f;
	float Depth = 0.0f;
};

// The DUMP the local FluidWallpaper exe writes (DUMP-SCHEMA.md).
struct FOilTankDump
{
	int32 Version = 0;
	double T = 0.0;
	int32 Width = 2560;
	int32 Height = 1440;
	FLinearColor FilmRGB = FLinearColor(0.05f, 0.30f, 0.40f);
	FLinearColor AccentRGB = FLinearColor(1.0f, 0.4f, 0.8f);
	FString HeightPng;
	FString FilmPng;
	TArray<FOilTankDroplet> Droplets;
	bool bSynthetic = false;

	bool LoadFromFile(const FString& Path, FString& OutError);
};

// Every scene knob, from Config/OilTank.json (README lists them all). Defaults here == defaults
// in the shipped json, so a missing key is never a surprise.
struct FOilTankSettings
{
	// Geometry (1 uu = 1 cm)
	float FrameWidthCm = 40.0f;
	float FrameHeightCm = 22.5f;
	float TankDepthCm = 100.0f;

	// Oil occluder
	int32 OilGridX = 1280;
	int32 OilGridY = 720;
	float OilThreshold = 0.02f;
	float OilThicknessCm = 0.3f;
	FString OilSurround = TEXT("mirror");   // "mirror" | "none"
	float DropletMinDepth = 0.02f;

	// Light
	float LightElevDeg = 55.0f;
	float LightAzimDeg = 210.0f;
	float LightSourceAngleDeg = 0.5357f;
	float LightLux = 0.0f;                  // 0 = auto-calibrate (floor reads as film colour)
	float CalibGain = 1.0f;
	bool bCalibThroughWater = true;
	float LightVolumetricScattering = 1.0f;
	float ContactShadowLength = 0.05f;

	// Exposure / post
	float ExposureBias = 0.0f;
	float FilmGrain = 0.0f;
	FString AA = TEXT("ssaa2");             // "ssaa2" | "tsr" | "taa" | "none"

	// Water
	FString WaterMode = TEXT("heightfog");  // "heightfog" (froxel grid spans only the water) | "localfog"
	float WaterOpticalDepth = 0.6f;         // total vertical extinction through the column
	float WaterAlbedo = 0.9f;
	float WaterPhaseG = 0.3f;
	bool bWaterColorFromFilm = true;
	FLinearColor WaterColor = FLinearColor(0.2f, 0.6f, 0.8f);
	float WaterDensityRaw = 0.0f;           // >0: use as the raw UE fog value, skip the unit guess
	float LfvUnitRadiusCm = 500.0f;         // unused: 5.8 fixes it (ULocalFogVolumeComponent::GetBaseVolumeSize() = 500)
	float FogGridMarginCm = 5.0f;

	// Floor
	float FloorMargin = 0.25f;
	float FloorSpecular = 0.0f;
	float FloorRoughness = 1.0f;

	// Motes
	int32 MoteCount = 3000;
	float MoteSizeMm = 0.4f;
	FLinearColor MoteAlbedo = FLinearColor(0.8f, 0.8f, 0.8f);
	float MoteSpecular = 0.3f;
	float MoteRoughness = 0.4f;
	float MoteRiseCmS = 0.6f;
	float MoteJitterCm = 0.15f;
	float MoteZMinCm = 0.5f;
	int32 MoteSeed = 1234;

	// Camera
	FString CameraMode = TEXT("narrow");    // "narrow" | "ortho"
	float CameraFovDeg = 3.0f;
	float CameraDistanceCm = 0.0f;          // narrow: 0 = from fov; >0 = fixed, fov derived
	float OrthoHeightCm = 300.0f;
	bool bDof = true;
	float ApertureFstop = 0.0f;             // 0 = auto from dof_px_at_floor
	float DofPxAtFloor = 24.0f;
	float SensorWidthMm = 24.89f;
	int32 DofBladeCount = 16;

	// Capture / run
	int32 WarmupFrames = 120;
	int32 ClipFrames = 300;
	float ClipFps = 30.0f;
	float StillSimFps = 72.0f;
	float PerfSeconds = 20.0f;
	int32 ResX = 2560;                      // only used for DoF px maths; the real size is -ResX
	int32 ResY = 1440;                      // ortho framing + DoF aspect; the real size is -ResY

	// Synthetic dump (no -OilDump)
	int32 SyntheticBlobs = 14;
	int32 SyntheticSeed = 7;

	// Debug
	bool bDebugNoOil = false;
	bool bDebugNoWater = false;
	bool bDebugNoMotes = false;

	TArray<TPair<FString, FString>> CVars;
	TArray<FString> ExecCmds;               // diagnostics: console commands run once at BeginPlay (e.g. "viewmode unlit")

	bool LoadFromFile(const FString& Path, FString& OutError);
	FString Summary() const;
};
