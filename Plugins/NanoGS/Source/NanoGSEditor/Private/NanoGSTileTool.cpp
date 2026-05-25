// Copyright Epic Games, Inc. All Rights Reserved.

#include "NanoGSTileTool.h"
#include "GaussianSplatAsset.h"
#include "GaussianSplatAssetFactory.h"
#include "GaussianTileStreamer.h"

#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetImportTask.h"
#include "Editor.h"
#include "Engine/World.h"
#include "LevelEditorSubsystem.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformFile.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "UObject/SavePackage.h"

#define LOCTEXT_NAMESPACE "NanoGSTileTool"

namespace
{
	// --- compact symmetric 3x3 Jacobi eigen-decomposition ---
	void Jacobi3x3(double A[3][3], double EVal[3], double EVec[3][3])
	{
		for (int i = 0; i < 3; ++i) { for (int j = 0; j < 3; ++j) EVec[i][j] = (i == j) ? 1.0 : 0.0; }
		for (int sweep = 0; sweep < 50; ++sweep)
		{
			double off = FMath::Abs(A[0][1]) + FMath::Abs(A[0][2]) + FMath::Abs(A[1][2]);
			if (off < 1e-12) break;
			for (int p = 0; p < 2; ++p)
			{
				for (int q = p + 1; q < 3; ++q)
				{
					if (FMath::Abs(A[p][q]) < 1e-15) continue;
					double theta = (A[q][q] - A[p][p]) / (2.0 * A[p][q]);
					double t = FMath::Sign(theta) / (FMath::Abs(theta) + FMath::Sqrt(theta * theta + 1.0));
					if (theta == 0.0) t = 1.0;
					double c = 1.0 / FMath::Sqrt(t * t + 1.0);
					double s = t * c;
					double app = A[p][p], aqq = A[q][q], apq = A[p][q];
					A[p][p] = c * c * app - 2.0 * s * c * apq + s * s * aqq;
					A[q][q] = s * s * app + 2.0 * s * c * apq + c * c * aqq;
					A[p][q] = A[q][p] = 0.0;
					for (int i = 0; i < 3; ++i)
					{
						if (i != p && i != q)
						{
							double aip = A[i][p], aiq = A[i][q];
							A[i][p] = A[p][i] = c * aip - s * aiq;
							A[i][q] = A[q][i] = s * aip + c * aiq;
						}
						double vip = EVec[i][p], viq = EVec[i][q];
						EVec[i][p] = c * vip - s * viq;
						EVec[i][q] = s * vip + c * viq;
					}
				}
			}
		}
		for (int i = 0; i < 3; ++i) EVal[i] = A[i][i];
	}
}

bool FNanoGSTileTool::SliceToTiles(const FNanoGSTileToolParams& Params, const FString& OutDir,
	TArray<FString>& OutTileFiles, FString& OutManifestPath, FString& OutError)
{
	IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();

	// --- read & parse the ASCII header ---
	TUniquePtr<IFileHandle> In(PF.OpenRead(*Params.PlyPath));
	if (!In) { OutError = TEXT("cannot open PLY"); return false; }
	const int64 FileSize = In->Size();
	const int32 HeadProbe = (int32)FMath::Min<int64>(FileSize, 16384);
	TArray<uint8> Probe; Probe.SetNumUninitialized(HeadProbe);
	In->Read(Probe.GetData(), HeadProbe);
	FString HeaderStr;
	{
		// header is ASCII up to and including "end_header\n"
		FString All(HeadProbe, (const ANSICHAR*)Probe.GetData());
		int32 EndIdx = All.Find(TEXT("end_header"));
		if (EndIdx == INDEX_NONE) { OutError = TEXT("no end_header"); return false; }
		int32 NL = All.Find(TEXT("\n"), ESearchCase::IgnoreCase, ESearchDir::FromStart, EndIdx);
		HeaderStr = All.Left(NL + 1);
	}
	const int64 DataOffset = FTCHARToUTF8(*HeaderStr).Length();

	int64 VertexCount = 0; int32 NumProps = 0; int32 XIdx = -1, YIdx = -1, ZIdx = -1;
	{
		TArray<FString> Lines; HeaderStr.ParseIntoArrayLines(Lines);
		for (const FString& L : Lines)
		{
			if (L.StartsWith(TEXT("element vertex")))
			{
				VertexCount = FCString::Atoi64(*L.RightChop(15).TrimStartAndEnd());
			}
			else if (L.StartsWith(TEXT("property float")))
			{
				FString Name = L.RightChop(15).TrimStartAndEnd();
				if (Name == TEXT("x")) XIdx = NumProps;
				else if (Name == TEXT("y")) YIdx = NumProps;
				else if (Name == TEXT("z")) ZIdx = NumProps;
				++NumProps;
			}
			else if (L.StartsWith(TEXT("property")))
			{
				++NumProps; // non-float property (unexpected for this format)
			}
		}
	}
	if (VertexCount <= 0 || NumProps <= 0 || XIdx < 0 || YIdx < 0 || ZIdx < 0)
	{ OutError = TEXT("unsupported PLY header (need float x/y/z)"); return false; }

	const int32 Stride = NumProps * 4;                 // bytes per vertex (all float32)
	const int64 N = VertexCount;

	// --- pass 1: stream xyz into RAM + accumulate moments for PCA ---
	TArray<float> Xyz; Xyz.SetNumUninitialized((int32)(N * 3));   // 3 floats/vertex
	double Sum[3] = { 0,0,0 }; double Outer[3][3] = { {0,0,0},{0,0,0},{0,0,0} };
	{
		const int64 RecsPerChunk = 1 << 20;            // ~1M records/chunk
		TArray<uint8> Buf; Buf.SetNumUninitialized((int32)(RecsPerChunk * Stride));
		In->Seek(DataOffset);
		int64 done = 0;
		while (done < N)
		{
			const int64 recs = FMath::Min(RecsPerChunk, N - done);
			In->Read(Buf.GetData(), recs * Stride);
			for (int64 r = 0; r < recs; ++r)
			{
				const float* F = (const float*)(Buf.GetData() + r * Stride);
				const double x = F[XIdx], y = F[YIdx], z = F[ZIdx];
				const int64 g = (done + r) * 3;
				Xyz[(int32)g] = (float)x; Xyz[(int32)g + 1] = (float)y; Xyz[(int32)g + 2] = (float)z;
				Sum[0] += x; Sum[1] += y; Sum[2] += z;
				Outer[0][0] += x * x; Outer[0][1] += x * y; Outer[0][2] += x * z;
				Outer[1][1] += y * y; Outer[1][2] += y * z; Outer[2][2] += z * z;
			}
			done += recs;
		}
	}
	const double Mean[3] = { Sum[0] / N, Sum[1] / N, Sum[2] / N };
	double Cov[3][3];
	Cov[0][0] = Outer[0][0] / N - Mean[0] * Mean[0];
	Cov[1][1] = Outer[1][1] / N - Mean[1] * Mean[1];
	Cov[2][2] = Outer[2][2] / N - Mean[2] * Mean[2];
	Cov[0][1] = Cov[1][0] = Outer[0][1] / N - Mean[0] * Mean[1];
	Cov[0][2] = Cov[2][0] = Outer[0][2] / N - Mean[0] * Mean[2];
	Cov[1][2] = Cov[2][1] = Outer[1][2] / N - Mean[1] * Mean[2];

	double EVal[3], EVec[3][3]; Jacobi3x3(Cov, EVal, EVec);
	// order eigenvectors: up = smallest eigenvalue, g0 = largest, g1 = middle
	int order[3] = { 0,1,2 };
	if (EVal[order[0]] > EVal[order[1]]) Swap(order[0], order[1]);
	if (EVal[order[1]] > EVal[order[2]]) Swap(order[1], order[2]);
	if (EVal[order[0]] > EVal[order[1]]) Swap(order[0], order[1]);
	auto Col = [&](int c, double v[3]) { v[0] = EVec[0][c]; v[1] = EVec[1][c]; v[2] = EVec[2][c]; };
	double up[3], g0[3], g1[3];
	Col(order[0], up); Col(order[2], g0); Col(order[1], g1);
	if (up[2] < 0) { up[0] = -up[0]; up[1] = -up[1]; up[2] = -up[2]; }

	// --- grid sizing for ~TileCount tiles in the ground plane ---
	auto Dot = [](const double a[3], const float* p, const double m[3])
	{ return a[0] * (p[0] - m[0]) + a[1] * (p[1] - m[1]) + a[2] * (p[2] - m[2]); };
	double umin = 1e30, umax = -1e30, vmin = 1e30, vmax = -1e30;
	for (int64 i = 0; i < N; ++i)
	{
		const float* p = &Xyz[(int32)(i * 3)];
		const double u = Dot(g0, p, Mean), v = Dot(g1, p, Mean);
		umin = FMath::Min(umin, u); umax = FMath::Max(umax, u);
		vmin = FMath::Min(vmin, v); vmax = FMath::Max(vmax, v);
	}
	const double uExt = FMath::Max(1e-3, umax - umin), vExt = FMath::Max(1e-3, vmax - vmin);
	int32 NU = FMath::Max(1, FMath::RoundToInt(FMath::Sqrt((double)Params.TileCount * uExt / vExt)));
	int32 NV = FMath::Max(1, FMath::DivideAndRoundUp(Params.TileCount, NU));
	const double du = uExt / NU, dv = vExt / NV;

	// --- accumulate per-cell count + centroid ---
	const int32 NumCells = NU * NV;
	TArray<int64> Count; Count.Init(0, NumCells);
	TArray<double> Cx; Cx.Init(0, NumCells); TArray<double> CyA; CyA.Init(0, NumCells); TArray<double> Cz; Cz.Init(0, NumCells);
	auto CellOf = [&](const float* p) -> int32
	{
		const double u = Dot(g0, p, Mean), v = Dot(g1, p, Mean);
		int32 iu = FMath::Clamp((int32)FMath::FloorToInt((u - umin) / du), 0, NU - 1);
		int32 iv = FMath::Clamp((int32)FMath::FloorToInt((v - vmin) / dv), 0, NV - 1);
		return iu * NV + iv;
	};
	for (int64 i = 0; i < N; ++i)
	{
		const float* p = &Xyz[(int32)(i * 3)];
		const int32 c = CellOf(p);
		Count[c]++; Cx[c] += p[0]; CyA[c] += p[1]; Cz[c] += p[2];
	}

	// open a tile file per non-empty cell (>=50 splats), write headers
	IFileManager::Get().MakeDirectory(*OutDir, true);
	struct FTileOut { TUniquePtr<IFileHandle> H; FString File; double Off[3]; int64 Cnt; };
	TMap<int32, TSharedPtr<FTileOut>> Tiles;
	for (int32 iu = 0; iu < NU; ++iu)
	{
		for (int32 iv = 0; iv < NV; ++iv)
		{
			const int32 c = iu * NV + iv;
			if (Count[c] < 50) continue;
			TSharedPtr<FTileOut> T = MakeShared<FTileOut>();
			T->File = FString::Printf(TEXT("tile_%d_%d.ply"), iu, iv);
			T->Off[0] = Cx[c] / Count[c]; T->Off[1] = CyA[c] / Count[c]; T->Off[2] = Cz[c] / Count[c];
			T->Cnt = Count[c];
			T->H.Reset(PF.OpenWrite(*FPaths::Combine(OutDir, T->File)));
			if (!T->H) continue;
			FString H = HeaderStr.Replace(*FString::Printf(TEXT("element vertex %lld"), N),
			                              *FString::Printf(TEXT("element vertex %lld"), T->Cnt));
			FTCHARToUTF8 Utf8(*H);
			T->H->Write((const uint8*)Utf8.Get(), Utf8.Length());
			Tiles.Add(c, T);
		}
	}

	// --- pass 2: stream records, route to tile file with xyz centred ---
	{
		const int64 RecsPerChunk = 1 << 20;
		TArray<uint8> Buf; Buf.SetNumUninitialized((int32)(RecsPerChunk * Stride));
		In->Seek(DataOffset);
		int64 done = 0;
		while (done < N)
		{
			const int64 recs = FMath::Min(RecsPerChunk, N - done);
			In->Read(Buf.GetData(), recs * Stride);
			for (int64 r = 0; r < recs; ++r)
			{
				uint8* Rec = Buf.GetData() + r * Stride;
				const float* p = &Xyz[(int32)((done + r) * 3)];
				TSharedPtr<FTileOut>* TP = Tiles.Find(CellOf(p));
				if (!TP) continue;
				float* F = (float*)Rec;
				F[XIdx] = (float)(p[0] - (*TP)->Off[0]);
				F[YIdx] = (float)(p[1] - (*TP)->Off[1]);
				F[ZIdx] = (float)(p[2] - (*TP)->Off[2]);
				(*TP)->H->Write(Rec, Stride);
			}
			done += recs;
		}
	}

	// --- manifest.json (matches the Python slicer schema) ---
	FString J = TEXT("{\n  \"up_axis_ply\": [");
	J += FString::Printf(TEXT("%f, %f, %f],\n  \"tile_size\": %f,\n  \"num_tiles\": %d,\n  \"total\": %lld,\n  \"tiles\": [\n"),
	                     up[0], up[1], up[2], du, Tiles.Num(), N);
	int32 idx = 0;
	for (auto& KV : Tiles)
	{
		TSharedPtr<FTileOut> T = KV.Value;
		OutTileFiles.Add(T->File);
		J += FString::Printf(TEXT("    {\"file\": \"%s\", \"offset\": [%f, %f, %f], \"count\": %lld}%s\n"),
		                     *T->File, T->Off[0], T->Off[1], T->Off[2], T->Cnt,
		                     (++idx < Tiles.Num()) ? TEXT(",") : TEXT(""));
		T->H.Reset(); // close
	}
	J += TEXT("  ]\n}\n");
	OutManifestPath = FPaths::Combine(OutDir, TEXT("manifest.json"));
	FFileHelper::SaveStringToFile(J, *OutManifestPath);
	return OutTileFiles.Num() > 0;
}

bool FNanoGSTileTool::ImportTiles(const FNanoGSTileToolParams& Params, const FString& TilesDir,
	const TArray<FString>& TileFiles, TArray<FString>& OutAssetPaths, FString& OutError)
{
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	FScopedSlowTask Task(TileFiles.Num(), LOCTEXT("Importing", "Importing tiles..."));
	Task.MakeDialog();
	for (const FString& File : TileFiles)
	{
		Task.EnterProgressFrame(1.f);
		const FString Name = FPaths::GetBaseFilename(File);
		UAssetImportTask* T = NewObject<UAssetImportTask>();
		T->Filename = FPaths::Combine(TilesDir, File);
		T->DestinationPath = Params.ContentDir;
		T->DestinationName = Name;
		T->Factory = NewObject<UGaussianSplatAssetFactory>();
		T->bAutomated = true; T->bReplaceExisting = true; T->bSave = true;
		TArray<UAssetImportTask*> Tasks; Tasks.Add(T);
		AssetTools.ImportAssetTasks(Tasks);

		const FString AssetPath = Params.ContentDir + TEXT("/") + Name + TEXT(".") + Name;
		UGaussianSplatAsset* Asset = LoadObject<UGaussianSplatAsset>(nullptr, *AssetPath);
		if (!Asset) continue;
		OutAssetPaths.Add(AssetPath);
		if (Params.bBuildNanite && !Asset->IsNaniteEnabled())
		{
			Asset->BuildNaniteClusterHierarchy();
		}
		// save the package (asset + any Nanite data)
		UPackage* Pkg = Asset->GetOutermost();
		Pkg->MarkPackageDirty();
		const FString FileName = FPackageName::LongPackageNameToFilename(Pkg->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs; SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		UPackage::SavePackage(Pkg, Asset, *FileName, SaveArgs);
	}
	return OutAssetPaths.Num() > 0;
}

bool FNanoGSTileTool::SetupStreamer(const FNanoGSTileToolParams& Params, const FString& ManifestPath, FString& OutError)
{
	ULevelEditorSubsystem* LES = GEditor->GetEditorSubsystem<ULevelEditorSubsystem>();
	if (!LES) { OutError = TEXT("no LevelEditorSubsystem"); return false; }
	LES->NewLevel(TEXT("/Game/Maps/NanoGSStream"));
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) { OutError = TEXT("no editor world"); return false; }
	AGaussianTileStreamer* S = World->SpawnActor<AGaussianTileStreamer>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (!S) { OutError = TEXT("spawn streamer failed"); return false; }
	S->ManifestPath = ManifestPath;
	S->TileContentDir = Params.ContentDir;
	LES->SaveCurrentLevel();
	return true;
}

bool FNanoGSTileTool::Run(const FNanoGSTileToolParams& Params, FString& OutMessage)
{
	if (!FPaths::FileExists(Params.PlyPath)) { OutMessage = TEXT("PLY not found: ") + Params.PlyPath; return false; }

	// --- no split: import the whole PLY as one asset ---
	if (!Params.bSplit)
	{
		FString Err; TArray<FString> Assets;
		TArray<FString> One; One.Add(FPaths::GetCleanFilename(Params.PlyPath));
		if (!ImportTiles(Params, FPaths::GetPath(Params.PlyPath), One, Assets, Err))
		{ OutMessage = TEXT("import failed: ") + Err; return false; }
		OutMessage = FString::Printf(TEXT("Imported whole PLY as 1 asset%s."), Params.bBuildNanite ? TEXT(" (+Nanite)") : TEXT(""));
		return true;
	}

	// --- split path ---
	FScopedSlowTask Task(3.f, LOCTEXT("Tiling", "Slicing PLY into tiles..."));
	Task.MakeDialog();

	const FString OutDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("NanoGSTiles"));
	TArray<FString> TileFiles; FString ManifestPath, Err;
	Task.EnterProgressFrame(1.f, LOCTEXT("Slicing", "Slicing (streaming)..."));
	if (!SliceToTiles(Params, OutDir, TileFiles, ManifestPath, Err))
	{ OutMessage = TEXT("slice failed: ") + Err; return false; }

	Task.EnterProgressFrame(1.f, LOCTEXT("Importing2", "Importing + Nanite..."));
	TArray<FString> Assets;
	if (!ImportTiles(Params, OutDir, TileFiles, Assets, Err))
	{ OutMessage = TEXT("import failed: ") + Err; return false; }

	Task.EnterProgressFrame(1.f, LOCTEXT("Streamer", "Setting up streamer..."));
	if (Params.bSetupStreamer)
	{
		if (!SetupStreamer(Params, ManifestPath, Err))
		{ OutMessage = FString::Printf(TEXT("Tiled %d, imported %d, but streamer failed: %s"), TileFiles.Num(), Assets.Num(), *Err); return true; }
	}
	OutMessage = FString::Printf(TEXT("Done: %d tiles, %d imported%s%s."),
		TileFiles.Num(), Assets.Num(),
		Params.bBuildNanite ? TEXT(" (+Nanite)") : TEXT(""),
		Params.bSetupStreamer ? TEXT(" (+streamer level /Game/Maps/NanoGSStream)") : TEXT(""));
	return true;
}

#undef LOCTEXT_NAMESPACE
