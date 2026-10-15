#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "OilTankSettings.h"
#include "OilTankMesh.h"
#include "OilTankGameMode.generated.h"

class ACameraActor;
class ADirectionalLight;
class AExponentialHeightFog;
class APostProcessVolume;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UProceduralMeshComponent;
class USceneComponent;
class UTexture2D;

// Spawns the whole top-down tank at BeginPlay (no assets, no .umap): oil occluder at z=0 from the
// dump's height PNG, water column, lit floor receiver, motes, one directional light, a narrow-FOV
// (or ortho) camera looking straight down, an unbound post volume. Then captures and exits.
UCLASS()
class OILTANK_API AOilTankGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AOilTankGameMode();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	enum class EPhase : uint8 { Warmup, Clip, WaitFiles, Perf, Done };

	// setup
	void LoadInputs();
	bool LoadDumpFile(const FString& Path);
	void ApplyFilm();
	void ApplyCVars();
	void RemoveForeignActors();
	void CreateMaterials();
	void SpawnCameraAndPost();
	void SpawnLight();
	void SpawnWater();
	void SpawnFloor();
	void SpawnFilmLayer();
	void BuildOil();
	void SpawnDroplets();
	void SpawnMotes();
	void UpdateMotes(double SimTime);
	void EnsureViewTarget();

	// capture / perf
	void TickPerf();
	void WriteCalibReport(const FString& ShotPath);
	void AppendLine(const FString& FileName, const FString& Line) const;
	void Finish(const TCHAR* Why);

	// helpers
	FVector ToLightDir() const;              // unit vector pointing TOWARD the light (world)
	FVector2D VisibleFloorSizeCm() const;    // (X = screen-up extent, Y = screen-right extent)
	float LightLuxCalibrated() const;
	UMaterialInstanceDynamic* MakeMID(const FLinearColor& Color, float Specular, float Roughness, UTexture2D* Albedo);
	FLinearColor WaterTint() const;

	FOilTankSettings S;
	FOilTankDump Dump;
	FString ShotPath;
	FString ClipDir;
	FString DumpDirSeq;
	TArray<float> HeightGrid;
	FOilMeshData OilMesh;

	bool bInputError = false;
	double BaseSimT = 0.0;
	float FloorBoost = 1.0f;     // film_rgb > 1 moves into the light (BaseColor saturates at 1)
	int32 FoundFrames = 0;
	double CameraDistanceCm = 0.0;
	double CameraFovDeg = 3.0;
	double ApertureFstop = 8.0;
	int32 FrameIndex = 0;
	int32 ClipIndex = 0;
	int32 WaitFrames = 0;
	EPhase Phase = EPhase::Warmup;
	TArray<FString> PendingFiles;

	// perf
	double LastWall = 0.0;
	double PerfWindowStart = 0.0;
	double PerfStart = 0.0;
	double PerfSumMs = 0.0;
	double PerfMaxMs = 0.0;
	int32 PerfFrames = 0;

	// motes (analytic positions: x0, y0, z, phase) so any frame is reproducible from SimTime alone
	TArray<FVector4f> MoteSeeds;
	TArray<FTransform> MoteXforms;

	UPROPERTY() TObjectPtr<AActor> SceneActor;
	UPROPERTY() TObjectPtr<USceneComponent> SceneRoot;
	UPROPERTY() TObjectPtr<ACameraActor> Camera;
	UPROPERTY() TObjectPtr<ADirectionalLight> Light;
	UPROPERTY() TObjectPtr<AExponentialHeightFog> HeightFog;
	UPROPERTY() TObjectPtr<AActor> WaterVolumeActor;
	UPROPERTY() TObjectPtr<APostProcessVolume> PostVolume;
	UPROPERTY() TArray<TObjectPtr<UProceduralMeshComponent>> OilTiles;
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> Floor;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Motes;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Droplets;
	UPROPERTY() TObjectPtr<UMaterialInterface> BaseMaterial;
	UPROPERTY() TObjectPtr<UMaterialInterface> FilmMaterial;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> FilmMID;
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> FilmLayer;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> OilMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> FloorMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> MoteMID;
	UPROPERTY() TObjectPtr<UTexture2D> WhiteTex;
	UPROPERTY() TObjectPtr<UTexture2D> FilmTex;
	UPROPERTY() TArray<TObjectPtr<AActor>> OwnedActors;
};
