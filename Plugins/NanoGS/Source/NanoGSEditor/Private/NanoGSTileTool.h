// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

/** Parameters driving the one-click "load PLY -> (tile) -> import -> Nanite -> stream" pipeline. */
struct FNanoGSTileToolParams
{
	FString PlyPath;                              // source .ply on disk
	bool    bSplit = true;                        // split into tiles?
	int32   TileCount = 12;                       // target number of tiles (approx)
	bool    bBuildNanite = true;                  // build Nanite cluster LOD per asset
	bool    bSetupStreamer = true;                // create a level with a GaussianTileStreamer
	FString ContentDir = TEXT("/Game/NanoGSTiles"); // where imported assets land
};

/**
 * Editor-only pipeline: slices a large 3DGS PLY into tiles (streaming, PCA up-axis),
 * imports them, builds Nanite, and drops a distance streamer — or, when bSplit is off,
 * imports the whole PLY as a single asset. Pure C++/RHI; no external deps.
 */
class FNanoGSTileTool
{
public:
	/** Runs the full pipeline on the game thread (shows a slow-task dialog). */
	static bool Run(const FNanoGSTileToolParams& Params, FString& OutMessage);

private:
	// Slices PlyPath into <=TileCount centred tile PLYs under OutDir + writes manifest.json.
	static bool SliceToTiles(const FNanoGSTileToolParams& Params, const FString& OutDir,
	                         TArray<FString>& OutTileFiles, FString& OutManifestPath, FString& OutError);

	// Imports the given PLY files as GaussianSplatAssets into ContentDir (+ optional Nanite).
	static bool ImportTiles(const FNanoGSTileToolParams& Params, const FString& TilesDir,
	                        const TArray<FString>& TileFiles, TArray<FString>& OutAssetPaths, FString& OutError);

	// Spawns a configured GaussianTileStreamer into a fresh level.
	static bool SetupStreamer(const FNanoGSTileToolParams& Params, const FString& ManifestPath, FString& OutError);
};
