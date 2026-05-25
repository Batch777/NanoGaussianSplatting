// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GaussianTileStreamer.generated.h"

class UGaussianSplatAsset;
class AGaussianSplatActor;

/** One streamable tile: its asset, world placement, and current load state. */
USTRUCT()
struct FGaussianTileEntry
{
	GENERATED_BODY()

	UPROPERTY() FString AssetPath;
	UPROPERTY() FVector WorldLocation = FVector::ZeroVector;
	UPROPERTY() TObjectPtr<UGaussianSplatAsset> Asset = nullptr;
	UPROPERTY(Transient) TObjectPtr<AGaussianSplatActor> Actor = nullptr;
	bool bLoaded = false;
	bool bLoading = false;   // async pack in flight
};

/**
 * Distance-based streamer for a tiled Gaussian-Splat scene (CityGaussian-style block
 * selection). Reads tiles_pca/manifest.json, places each tile's actor only when the
 * camera is within LoadRadius, and destroys it + releases its asset's GPU buffers when
 * beyond UnloadRadius. This actually frees VRAM (the shared per-asset render data),
 * unlike plain frustum culling. Works in the editor viewport and in PIE.
 */
UCLASS()
class NANOGS_API AGaussianTileStreamer : public AActor
{
	GENERATED_BODY()

public:
	AGaussianTileStreamer();

	/** Path to the slicer's manifest.json. Absolute, or relative to the project dir. */
	UPROPERTY(EditAnywhere, Category = "Tile Streaming")
	FString ManifestPath;

	/** Content folder the tiles were imported into. */
	UPROPERTY(EditAnywhere, Category = "Tile Streaming")
	FString TileContentDir = TEXT("/Game/TilesPCA");

	/** Load a tile when the camera is within this distance of its centre (cm). */
	UPROPERTY(EditAnywhere, Category = "Tile Streaming", meta = (ClampMin = "100.0"))
	float LoadRadius = 18000.0f;

	/** Unload a tile when the camera is beyond this distance (cm). Must be > LoadRadius
	 *  to create hysteresis and avoid load/unload thrashing at the boundary. */
	UPROPERTY(EditAnywhere, Category = "Tile Streaming", meta = (ClampMin = "100.0"))
	float UnloadRadius = 24000.0f;

	/** Re-evaluate streaming every N frames (throttle). */
	UPROPERTY(EditAnywhere, Category = "Tile Streaming", meta = (ClampMin = "1"))
	int32 UpdateEveryNFrames = 8;

	/** Pack tile data on a background thread before spawning, so a new tile never
	 *  stalls the game thread (it pops in a few frames later instead of hitching). */
	UPROPERTY(EditAnywhere, Category = "Tile Streaming")
	bool bAsyncLoad = true;

	/** Also stream while previewing in the editor viewport (not just PIE). */
	UPROPERTY(EditAnywhere, Category = "Tile Streaming")
	bool bStreamInEditor = true;

	/** (Re)parse the manifest and load referenced assets. */
	UFUNCTION(CallInEditor, Category = "Tile Streaming")
	void RebuildTileList();

	/** Destroy all spawned tile actors and release their GPU buffers. */
	UFUNCTION(CallInEditor, Category = "Tile Streaming")
	void UnloadAllTiles();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual bool ShouldTickIfViewportsOnly() const override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	UPROPERTY(Transient) TArray<FGaussianTileEntry> Tiles;
	int32 FrameCounter = 0;
	bool bManifestLoaded = false;

	void LoadManifest();
	bool GetViewLocation(FVector& OutLocation) const;
	void BeginLoadTile(int32 Index);     // kicks async pack (or loads inline if !bAsyncLoad)
	void FinishLoadTile(int32 Index);    // game thread: spawn actor once data is packed
	void UnloadTile(FGaussianTileEntry& Tile);
	void UpdateStreaming();
};
