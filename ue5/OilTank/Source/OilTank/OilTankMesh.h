#pragma once

#include "CoreMinimal.h"

class UTexture2D;

// A float image, row-major, row 0 = top of the frame.
struct FOilTankImage
{
	int32 Width = 0;
	int32 Height = 0;
	TArray<float> Pixels;

	bool IsValid() const { return Width > 0 && Height > 0 && Pixels.Num() == Width * Height; }
	float Bilinear(float U, float V) const;   // u,v in 0..1 (pixel-centre convention, clamped)
};

struct FOilMeshData
{
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UV0;
	int32 NumTopVertices = 0;
	int32 NumBoundarySegments = 0;
};

namespace OilTankMesh
{
	// 16-bit grey PNG -> 0..1 floats. 8-bit grey/RGB(A) is accepted (red channel) and logged.
	bool LoadHeightPng(const FString& Path, FOilTankImage& Out, FString& OutError);

	// 8-bit sRGB PNG -> a transient BGRA8 sRGB texture (mirror addressing, bilinear, no mips).
	UTexture2D* LoadColorTexture(const FString& Path, FString& OutError);

	// A small solid-colour transient texture (used as the default "Albedo" of the runtime material).
	UTexture2D* MakeSolidTexture(const FColor& Color);

	// Decode any PNG to sRGB8 RGBA bytes (for the calibration readback of our own screenshot).
	bool LoadPngRGBA8(const FString& Path, int32& OutW, int32& OutH, TArray<uint8>& OutRGBA, FString& OutError);

	// Resample the height image onto an Nx x Ny vertex grid. Sample (i,j) sits at
	// u = i/(Nx-1), v = j/(Ny-1), so the grid reaches the frame edges exactly (mirror tiles meet).
	void ResampleToGrid(const FOilTankImage& Src, int32 Nx, int32 Ny, TArray<float>& OutGrid);

	// Test field when no dump is given: a few Wyvill-kernel blobs with smooth heights.
	void MakeSyntheticGrid(int32 Nx, int32 Ny, float AspectWH, int32 NumBlobs, int32 Seed, TArray<float>& OutGrid);

	// Marching squares on the grid at iso = Threshold, with LINEARLY INTERPOLATED edge crossings
	// (sub-cell accurate silhouette, no stair-steps). Builds a closed body:
	//   top    = the h>Threshold region at z = 0,
	//   bottom = the same triangulation at z = -h * ThicknessCm (crossing verts at -Threshold*Thick),
	//   walls  = one quad per boundary segment joining the two.
	// Frame centred at the origin: world X = (0.5 - v) * HeightCm (screen up), Y = (u - 0.5) * WidthCm.
	void BuildOilMesh(const TArray<float>& Grid, int32 Nx, int32 Ny, float WidthCm, float HeightCm,
		float Threshold, float ThicknessCm, FOilMeshData& Out);
}
