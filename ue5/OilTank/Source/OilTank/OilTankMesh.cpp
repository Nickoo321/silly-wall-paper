#include "OilTankMesh.h"
#include "OilTank.h"

#include "Engine/Texture2D.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Math/RandomStream.h"
#include "Misc/FileHelper.h"
#include "Modules/ModuleManager.h"

float FOilTankImage::Bilinear(float U, float V) const
{
	const float X = FMath::Clamp(U * Width - 0.5f, 0.0f, (float)(Width - 1));
	const float Y = FMath::Clamp(V * Height - 0.5f, 0.0f, (float)(Height - 1));
	const int32 X0 = FMath::FloorToInt(X);
	const int32 Y0 = FMath::FloorToInt(Y);
	const int32 X1 = FMath::Min(X0 + 1, Width - 1);
	const int32 Y1 = FMath::Min(Y0 + 1, Height - 1);
	const float FX = X - X0;
	const float FY = Y - Y0;
	const float A = Pixels[Y0 * Width + X0];
	const float B = Pixels[Y0 * Width + X1];
	const float C = Pixels[Y1 * Width + X0];
	const float D = Pixels[Y1 * Width + X1];
	return FMath::Lerp(FMath::Lerp(A, B, FX), FMath::Lerp(C, D, FX), FY);
}

namespace
{
	TSharedPtr<IImageWrapper> OpenPng(const FString& Path, TArray<uint8>& FileData, FString& OutError)
	{
		if (!FFileHelper::LoadFileToArray(FileData, *Path))
		{
			OutError = FString::Printf(TEXT("cannot read %s"), *Path);
			return nullptr;
		}
		IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		TSharedPtr<IImageWrapper> Wrapper = Module.CreateImageWrapper(EImageFormat::PNG);
		if (!Wrapper.IsValid() || !Wrapper->SetCompressed(FileData.GetData(), FileData.Num()))
		{
			OutError = FString::Printf(TEXT("not a PNG: %s"), *Path);
			return nullptr;
		}
		return Wrapper;
	}

	// Sum of |neighbour differences| over a subsample of rows: a smooth 16-bit field decoded with the
	// wrong byte order turns its low byte into the high byte and becomes noise, so the smaller sum wins.
	double Roughness16(const uint16* Data, int32 W, int32 H, bool bSwap)
	{
		double Sum = 0.0;
		const int32 RowStep = FMath::Max(1, H / 64);
		for (int32 Y = 0; Y < H; Y += RowStep)
		{
			const uint16* Row = Data + (int64)Y * W;
			for (int32 X = 1; X < W; ++X)
			{
				uint16 A = Row[X - 1];
				uint16 B = Row[X];
				if (bSwap)
				{
					A = (uint16)((A >> 8) | (A << 8));
					B = (uint16)((B >> 8) | (B << 8));
				}
				Sum += FMath::Abs((int32)A - (int32)B);
			}
		}
		return Sum;
	}
}

bool OilTankMesh::LoadHeightPng(const FString& Path, FOilTankImage& Out, FString& OutError)
{
	TArray<uint8> FileData;
	TSharedPtr<IImageWrapper> Wrapper = OpenPng(Path, FileData, OutError);
	if (!Wrapper.IsValid())
	{
		return false;
	}
	const int32 W = (int32)Wrapper->GetWidth();
	const int32 H = (int32)Wrapper->GetHeight();
	const int32 SrcBits = Wrapper->GetBitDepth();
	Out.Width = W;
	Out.Height = H;
	Out.Pixels.SetNumUninitialized(W * H);

	TArray64<uint8> Raw;
	// VERIFY (5.8): GetRaw(Gray, 16) on a 16-bit grey PNG. UE's PNG wrapper hands back native-endian
	// uint16 on the platforms we know of; we do not trust that and detect the byte order from the data.
	if (SrcBits == 16 && Wrapper->GetRaw(ERGBFormat::Gray, 16, Raw) && Raw.Num() >= (int64)W * H * 2)
	{
		const uint16* Data = reinterpret_cast<const uint16*>(Raw.GetData());
		const bool bSwap = Roughness16(Data, W, H, true) < Roughness16(Data, W, H, false);
		uint16 MinV = 0xFFFF, MaxV = 0;
		for (int64 I = 0; I < (int64)W * H; ++I)
		{
			uint16 V = Data[I];
			if (bSwap)
			{
				V = (uint16)((V >> 8) | (V << 8));
			}
			MinV = FMath::Min(MinV, V);
			MaxV = FMath::Max(MaxV, V);
			Out.Pixels[I] = V / 65535.0f;
		}
		UE_LOG(LogOilTank, Log, TEXT("height %s: %dx%d 16-bit grey, byte swap=%d, range %u..%u"), *Path, W, H, bSwap ? 1 : 0, MinV, MaxV);
		return true;
	}
	if (SrcBits == 16 && Wrapper->GetRaw(ERGBFormat::RGBA, 16, Raw) && Raw.Num() >= (int64)W * H * 8)
	{
		// Some writers emit 16-bit RGB(A) instead of grey: take red, same byte-order detection.
		TArray<uint16> Red;
		Red.SetNumUninitialized(W * H);
		const uint16* Data = reinterpret_cast<const uint16*>(Raw.GetData());
		for (int64 I = 0; I < (int64)W * H; ++I)
		{
			Red[I] = Data[I * 4];
		}
		const bool bSwap = Roughness16(Red.GetData(), W, H, true) < Roughness16(Red.GetData(), W, H, false);
		for (int64 I = 0; I < (int64)W * H; ++I)
		{
			uint16 V = Red[I];
			if (bSwap)
			{
				V = (uint16)((V >> 8) | (V << 8));
			}
			Out.Pixels[I] = V / 65535.0f;
		}
		UE_LOG(LogOilTank, Warning, TEXT("height %s: 16-bit RGBA, using red (swap=%d)"), *Path, bSwap ? 1 : 0);
		return true;
	}
	if (Wrapper->GetRaw(ERGBFormat::Gray, 8, Raw) && Raw.Num() >= (int64)W * H)
	{
		for (int64 I = 0; I < (int64)W * H; ++I)
		{
			Out.Pixels[I] = Raw[I] / 255.0f;
		}
		UE_LOG(LogOilTank, Warning, TEXT("height %s is %d-bit, read as 8-bit grey: expect terraces in the oil thickness (not the silhouette)"), *Path, SrcBits);
		return true;
	}
	OutError = FString::Printf(TEXT("could not decode %s as grey"), *Path);
	return false;
}

bool OilTankMesh::LoadPngRGBA8(const FString& Path, int32& OutW, int32& OutH, TArray<uint8>& OutRGBA, FString& OutError)
{
	TArray<uint8> FileData;
	TSharedPtr<IImageWrapper> Wrapper = OpenPng(Path, FileData, OutError);
	if (!Wrapper.IsValid())
	{
		return false;
	}
	TArray64<uint8> Raw;
	if (!Wrapper->GetRaw(ERGBFormat::RGBA, 8, Raw))
	{
		OutError = FString::Printf(TEXT("could not decode %s as RGBA8"), *Path);
		return false;
	}
	OutW = (int32)Wrapper->GetWidth();
	OutH = (int32)Wrapper->GetHeight();
	OutRGBA.SetNumUninitialized((int32)Raw.Num());
	FMemory::Memcpy(OutRGBA.GetData(), Raw.GetData(), Raw.Num());
	return true;
}

namespace
{
	UTexture2D* MakeTextureBGRA(int32 W, int32 H, const uint8* BGRA)
	{
		UTexture2D* Tex = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8);
		if (!Tex)
		{
			return nullptr;
		}
		Tex->SRGB = true;
		Tex->Filter = TF_Bilinear;
		Tex->AddressX = TA_Mirror;
		Tex->AddressY = TA_Mirror;
		Tex->NeverStream = true;
		FTexture2DMipMap& Mip = Tex->GetPlatformData()->Mips[0];
		void* Dst = Mip.BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memcpy(Dst, BGRA, (SIZE_T)W * H * 4);
		Mip.BulkData.Unlock();
		Tex->UpdateResource();
		return Tex;
	}
}

UTexture2D* OilTankMesh::LoadColorTexture(const FString& Path, FString& OutError)
{
	TArray<uint8> FileData;
	TSharedPtr<IImageWrapper> Wrapper = OpenPng(Path, FileData, OutError);
	if (!Wrapper.IsValid())
	{
		return nullptr;
	}
	TArray64<uint8> Raw;
	if (!Wrapper->GetRaw(ERGBFormat::BGRA, 8, Raw))
	{
		OutError = FString::Printf(TEXT("could not decode %s as BGRA8"), *Path);
		return nullptr;
	}
	return MakeTextureBGRA((int32)Wrapper->GetWidth(), (int32)Wrapper->GetHeight(), Raw.GetData());
}

UTexture2D* OilTankMesh::MakeSolidTexture(const FColor& Color)
{
	uint8 Data[4 * 4 * 4];
	for (int32 I = 0; I < 16; ++I)
	{
		Data[I * 4 + 0] = Color.B;
		Data[I * 4 + 1] = Color.G;
		Data[I * 4 + 2] = Color.R;
		Data[I * 4 + 3] = Color.A;
	}
	return MakeTextureBGRA(4, 4, Data);
}

void OilTankMesh::ResampleToGrid(const FOilTankImage& Src, int32 Nx, int32 Ny, TArray<float>& OutGrid)
{
	OutGrid.SetNumUninitialized(Nx * Ny);
	// Box prefilter when the image is much larger than the grid (the bilinear tap alone would alias
	// small droplets in and out); the iso-contour is then taken on the filtered field.
	const float StepX = (float)Src.Width / (float)Nx;
	const float StepY = (float)Src.Height / (float)Ny;
	const int32 Taps = (StepX > 1.5f || StepY > 1.5f) ? 2 : 1;
	for (int32 J = 0; J < Ny; ++J)
	{
		const float V = (float)J / (float)(Ny - 1);
		for (int32 I = 0; I < Nx; ++I)
		{
			const float U = (float)I / (float)(Nx - 1);
			float Sum = 0.0f;
			for (int32 TY = 0; TY < Taps; ++TY)
			{
				for (int32 TX = 0; TX < Taps; ++TX)
				{
					const float OU = Taps > 1 ? ((TX - 0.5f) * 0.5f * StepX / Src.Width) : 0.0f;
					const float OV = Taps > 1 ? ((TY - 0.5f) * 0.5f * StepY / Src.Height) : 0.0f;
					Sum += Src.Bilinear(U + OU, V + OV);
				}
			}
			OutGrid[J * Nx + I] = Sum / (float)(Taps * Taps);
		}
	}
}

void OilTankMesh::MakeSyntheticGrid(int32 Nx, int32 Ny, float AspectWH, int32 NumBlobs, int32 Seed, TArray<float>& OutGrid)
{
	struct FBlob { float U, V, R, Sign; };
	FRandomStream Rng(Seed);
	TArray<FBlob> Blobs;
	for (int32 B = 0; B < NumBlobs; ++B)
	{
		const bool bBig = B < NumBlobs / 3;
		Blobs.Add({ Rng.FRandRange(0.05f, 0.95f), Rng.FRandRange(0.05f, 0.95f),
			bBig ? Rng.FRandRange(0.10f, 0.20f) : Rng.FRandRange(0.015f, 0.05f), 1.0f });
	}
	// One ring: a big blob with a negative core.
	Blobs.Add({ 0.62f, 0.45f, 0.13f, 1.0f });
	Blobs.Add({ 0.62f, 0.45f, 0.07f, -1.6f });

	OutGrid.SetNumUninitialized(Nx * Ny);
	for (int32 J = 0; J < Ny; ++J)
	{
		const float V = (float)J / (float)(Ny - 1);
		for (int32 I = 0; I < Nx; ++I)
		{
			const float U = (float)I / (float)(Nx - 1);
			float F = 0.0f;
			for (const FBlob& B : Blobs)
			{
				// distances in units of frame HEIGHT so blobs are round on screen
				const float DX = (U - B.U) * AspectWH;
				const float DY = (V - B.V);
				const float Q = (DX * DX + DY * DY) / (B.R * B.R);
				if (Q < 1.0f)
				{
					const float K = 1.0f - Q;
					F += B.Sign * K * K * K;   // C2 Wyvill kernel
				}
			}
			OutGrid[J * Nx + I] = FMath::Clamp((F - 0.3f) / 0.7f, 0.0f, 1.0f);
		}
	}
}

void OilTankMesh::BuildOilMesh(const TArray<float>& Grid, int32 Nx, int32 Ny, float WidthCm, float HeightCm,
	float Threshold, float ThicknessCm, FOilMeshData& Out)
{
	Out = FOilMeshData();
	if (Nx < 2 || Ny < 2 || Grid.Num() != Nx * Ny)
	{
		return;
	}

	auto H = [&](int32 I, int32 J) { return Grid[J * Nx + I]; };
	auto Pos = [&](float U, float V) { return FVector((0.5 - V) * HeightCm, (U - 0.5) * WidthCm, 0.0); };

	TArray<int32> CornerIdx;  CornerIdx.Init(INDEX_NONE, Nx * Ny);
	TArray<int32> HEdgeIdx;   HEdgeIdx.Init(INDEX_NONE, (Nx - 1) * Ny);   // (i,j)-(i+1,j)
	TArray<int32> VEdgeIdx;   VEdgeIdx.Init(INDEX_NONE, Nx * (Ny - 1));   // (i,j)-(i,j+1)
	TArray<float> BottomZ;
	TArray<int32> TopTris;
	TArray<int32> Segments;   // pairs of top vertex indices on the iso line

	BottomZ.Reserve(Nx * Ny / 3);
	Out.Vertices.Reserve(Nx * Ny / 3);
	Out.UV0.Reserve(Nx * Ny / 3);
	TopTris.Reserve(Nx * Ny);

	const float DU = 1.0f / (float)(Nx - 1);
	const float DV = 1.0f / (float)(Ny - 1);

	auto Corner = [&](int32 I, int32 J) -> int32
	{
		int32& Idx = CornerIdx[J * Nx + I];
		if (Idx == INDEX_NONE)
		{
			const float U = I * DU, V = J * DV;
			Idx = Out.Vertices.Add(Pos(U, V));
			Out.UV0.Add(FVector2D(U, V));
			BottomZ.Add(-H(I, J) * ThicknessCm);
		}
		return Idx;
	};
	// Iso crossing on the edge between grid points A and B, linearly interpolated.
	auto Edge = [&](int32& Slot, int32 IA, int32 JA, int32 IB, int32 JB) -> int32
	{
		if (Slot == INDEX_NONE)
		{
			const float HA = H(IA, JA), HB = H(IB, JB);
			const float Den = HB - HA;
			const float T = FMath::Abs(Den) > 1e-12f ? FMath::Clamp((Threshold - HA) / Den, 0.0f, 1.0f) : 0.5f;
			const float U = FMath::Lerp(IA * DU, IB * DU, T);
			const float V = FMath::Lerp(JA * DV, JB * DV, T);
			Slot = Out.Vertices.Add(Pos(U, V));
			Out.UV0.Add(FVector2D(U, V));
			BottomZ.Add(-Threshold * ThicknessCm);
		}
		return Slot;
	};

	int32 Poly[8];
	for (int32 J = 0; J < Ny - 1; ++J)
	{
		for (int32 I = 0; I < Nx - 1; ++I)
		{
			// Corners clockwise as seen from above (= on screen): TL, TR, BR, BL.
			const float C[4] = { H(I, J), H(I + 1, J), H(I + 1, J + 1), H(I, J + 1) };
			const int32 Mask = (C[0] > Threshold ? 1 : 0) | (C[1] > Threshold ? 2 : 0) | (C[2] > Threshold ? 4 : 0) | (C[3] > Threshold ? 8 : 0);
			if (Mask == 0)
			{
				continue;
			}
			const int32 CI[4][2] = { { I, J }, { I + 1, J }, { I + 1, J + 1 }, { I, J + 1 } };
			auto CornerV = [&](int32 K) { return Corner(CI[K][0], CI[K][1]); };
			auto EdgeV = [&](int32 K) -> int32
			{
				switch (K)
				{
				case 0: return Edge(HEdgeIdx[J * (Nx - 1) + I], I, J, I + 1, J);               // top
				case 1: return Edge(VEdgeIdx[J * Nx + I + 1], I + 1, J, I + 1, J + 1);         // right
				case 2: return Edge(HEdgeIdx[(J + 1) * (Nx - 1) + I], I + 1, J + 1, I, J + 1); // bottom (B->A order is fine: same slot, T clamps)
				default: return Edge(VEdgeIdx[J * Nx + I], I, J + 1, I, J);                    // left
				}
			};

			if (Mask == 15)
			{
				const int32 A = CornerV(0), B = CornerV(1), Cc = CornerV(2), D = CornerV(3);
				TopTris.Append({ A, B, Cc, A, Cc, D });
				continue;
			}

			const bool bSaddle = (Mask == 5 || Mask == 10);
			if (bSaddle)
			{
				const float Centre = 0.25f * (C[0] + C[1] + C[2] + C[3]);
				const bool bJoined = Centre > Threshold;
				if (Mask == 5) // TL + BR inside
				{
					const int32 C0 = CornerV(0), C2 = CornerV(2);
					const int32 E0 = EdgeV(0), E1 = EdgeV(1), E2 = EdgeV(2), E3 = EdgeV(3);
					if (bJoined)
					{
						const int32 Hex[6] = { C0, E0, E1, C2, E2, E3 };
						for (int32 K = 1; K < 5; ++K) { TopTris.Append({ Hex[0], Hex[K], Hex[K + 1] }); }
						Segments.Append({ E0, E1, E2, E3 });
					}
					else
					{
						TopTris.Append({ C0, E0, E3, C2, E2, E1 });
						Segments.Append({ E0, E3, E1, E2 });
					}
				}
				else // TR + BL inside
				{
					const int32 C1 = CornerV(1), C3 = CornerV(3);
					const int32 E0 = EdgeV(0), E1 = EdgeV(1), E2 = EdgeV(2), E3 = EdgeV(3);
					if (bJoined)
					{
						const int32 Hex[6] = { E0, C1, E1, E2, C3, E3 };
						for (int32 K = 1; K < 5; ++K) { TopTris.Append({ Hex[0], Hex[K], Hex[K + 1] }); }
						Segments.Append({ E1, E2, E3, E0 });
					}
					else
					{
						TopTris.Append({ E0, C1, E1, E2, C3, E3 });
						Segments.Append({ E0, E1, E2, E3 });
					}
				}
				continue;
			}

			// Non-saddle: walk the cell clockwise, emitting inside corners and crossings. The result
			// is one convex polygon with exactly two crossings (the boundary segment).
			int32 N = 0;
			int32 SegA = INDEX_NONE, SegB = INDEX_NONE;
			for (int32 K = 0; K < 4; ++K)
			{
				const bool bIn = (Mask >> K) & 1;
				const bool bNextIn = (Mask >> ((K + 1) & 3)) & 1;
				if (bIn)
				{
					Poly[N++] = CornerV(K);
				}
				if (bIn != bNextIn)
				{
					const int32 E = EdgeV(K);
					Poly[N++] = E;
					if (SegA == INDEX_NONE) { SegA = E; } else { SegB = E; }
				}
			}
			for (int32 K = 1; K + 1 < N; ++K)
			{
				TopTris.Append({ Poly[0], Poly[K], Poly[K + 1] });
			}
			if (SegA != INDEX_NONE && SegB != INDEX_NONE)
			{
				Segments.Append({ SegA, SegB });
			}
		}
	}

	const int32 NumTop = Out.Vertices.Num();
	Out.NumTopVertices = NumTop;
	Out.NumBoundarySegments = Segments.Num() / 2;
	if (NumTop == 0)
	{
		return;
	}

	// Bottom copies.
	Out.Vertices.Reserve(NumTop * 2);
	Out.UV0.Reserve(NumTop * 2);
	for (int32 K = 0; K < NumTop; ++K)
	{
		const FVector P = Out.Vertices[K];
		Out.Vertices.Add(FVector(P.X, P.Y, BottomZ[K]));
		Out.UV0.Add(Out.UV0[K]);
	}
	Out.Normals.SetNumUninitialized(NumTop * 2);
	for (int32 K = 0; K < NumTop; ++K)
	{
		Out.Normals[K] = FVector::UpVector;
		Out.Normals[NumTop + K] = -FVector::UpVector;
	}

	Out.Triangles.Reserve(TopTris.Num() * 2 + Segments.Num() * 3);
	Out.Triangles.Append(TopTris);
	for (int32 K = 0; K + 2 < TopTris.Num(); K += 3)
	{
		// reversed winding: faces down
		Out.Triangles.Append({ TopTris[K] + NumTop, TopTris[K + 2] + NumTop, TopTris[K + 1] + NumTop });
	}
	for (int32 K = 0; K + 1 < Segments.Num(); K += 2)
	{
		const int32 A = Segments[K], B = Segments[K + 1];
		Out.Triangles.Append({ A, B, B + NumTop, A, B + NumTop, A + NumTop });
	}
}
