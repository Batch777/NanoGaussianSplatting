// Copyright Epic Games, Inc. All Rights Reserved.

#include "GaussianTileStreamer.h"
#include "GaussianSplatActor.h"
#include "GaussianSplatComponent.h"
#include "GaussianSplatAsset.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Async/Async.h"

#if WITH_EDITOR
#include "Editor.h"
#include "LevelEditorViewport.h"
#endif

AGaussianTileStreamer::AGaussianTileStreamer()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}

bool AGaussianTileStreamer::ShouldTickIfViewportsOnly() const
{
	// Enables ticking in the editor viewport (outside PIE) when previewing.
	return bStreamInEditor;
}

void AGaussianTileStreamer::BeginPlay()
{
	Super::BeginPlay();
	RebuildTileList();
}

void AGaussianTileStreamer::EndPlay(const EEndPlayReason::Type Reason)
{
	UnloadAllTiles();
	Super::EndPlay(Reason);
}

void AGaussianTileStreamer::RebuildTileList()
{
	UnloadAllTiles();
	Tiles.Reset();
	LoadManifest();
}

void AGaussianTileStreamer::LoadManifest()
{
	FString Resolved = ManifestPath;
	if (FPaths::IsRelative(Resolved))
	{
		Resolved = FPaths::Combine(FPaths::ProjectDir(), Resolved);
	}

	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *Resolved))
	{
		UE_LOG(LogTemp, Warning, TEXT("GaussianTileStreamer: cannot read manifest '%s'"), *Resolved);
		return;
	}

	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("GaussianTileStreamer: manifest JSON parse failed"));
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>* TilesArr = nullptr;
	if (!Root->TryGetArrayField(TEXT("tiles"), TilesArr))
	{
		UE_LOG(LogTemp, Warning, TEXT("GaussianTileStreamer: manifest has no 'tiles' array"));
		return;
	}

	for (const TSharedPtr<FJsonValue>& V : *TilesArr)
	{
		const TSharedPtr<FJsonObject> O = V->AsObject();
		if (!O.IsValid()) continue;

		FString File;
		const TArray<TSharedPtr<FJsonValue>>* Off = nullptr;
		if (!O->TryGetStringField(TEXT("file"), File) || !O->TryGetArrayField(TEXT("offset"), Off) || Off->Num() < 3)
		{
			continue;
		}

		const double Ox = (*Off)[0]->AsNumber();   // PLY centroid x
		const double Oy = (*Off)[1]->AsNumber();   // PLY centroid y
		const double Oz = (*Off)[2]->AsNumber();   // PLY centroid z

		FGaussianTileEntry T;
		FString Name = File.Replace(TEXT(".ply"), TEXT(""));
		T.AssetPath = FString::Printf(TEXT("%s/%s.%s"), *TileContentDir, *Name, *Name);
		// Calibrated PLY->UE world transform (import remap + Pitch90 upright), m -> cm.
		T.WorldLocation = FVector(100.0 * Oy, 100.0 * Ox, 100.0 * Oz);
		T.Asset = LoadObject<UGaussianSplatAsset>(nullptr, *T.AssetPath);
		if (!T.Asset)
		{
			UE_LOG(LogTemp, Warning, TEXT("GaussianTileStreamer: missing asset '%s'"), *T.AssetPath);
		}
		Tiles.Add(MoveTemp(T));
	}

	bManifestLoaded = true;
	UE_LOG(LogTemp, Log, TEXT("GaussianTileStreamer: loaded %d tiles from manifest"), Tiles.Num());
}

bool AGaussianTileStreamer::GetViewLocation(FVector& OutLocation) const
{
#if WITH_EDITOR
	if (GEditor && !GEditor->IsPlaySessionInProgress() && GCurrentLevelEditingViewportClient)
	{
		OutLocation = GCurrentLevelEditingViewportClient->GetViewLocation();
		return true;
	}
#endif
	if (const UWorld* W = GetWorld())
	{
		if (APlayerController* PC = W->GetFirstPlayerController())
		{
			FVector Loc; FRotator Rot;
			PC->GetPlayerViewPoint(Loc, Rot);
			OutLocation = Loc;
			return true;
		}
	}
	return false;
}

void AGaussianTileStreamer::BeginLoadTile(int32 Index)
{
	if (!Tiles.IsValidIndex(Index)) return;
	FGaussianTileEntry& Tile = Tiles[Index];
	if (!Tile.Asset)
	{
		Tile.Asset = LoadObject<UGaussianSplatAsset>(nullptr, *Tile.AssetPath);
		if (!Tile.Asset) return;
	}

	if (!bAsyncLoad)
	{
		// Synchronous: pack happens during proxy creation on the game thread.
		FinishLoadTile(Index);
		return;
	}

	// Pack the (heavy) shared render data on a background thread, then hop back to
	// the game thread to spawn the actor — proxy creation then finds it ready and
	// only uploads, so the game thread never stalls on the pack loop.
	Tile.bLoading = true;
	UGaussianSplatAsset* Asset = Tile.Asset;
	TWeakObjectPtr<AGaussianTileStreamer> WeakThis(this);
	Async(EAsyncExecution::ThreadPool, [Asset, WeakThis, Index]()
	{
		if (Asset)
		{
			Asset->GetOrCreateRenderData();   // pack on background thread
		}
		AsyncTask(ENamedThreads::GameThread, [WeakThis, Index]()
		{
			if (AGaussianTileStreamer* Self = WeakThis.Get())
			{
				Self->FinishLoadTile(Index);
			}
		});
	});
}

void AGaussianTileStreamer::FinishLoadTile(int32 Index)
{
	if (!Tiles.IsValidIndex(Index)) return;
	FGaussianTileEntry& Tile = Tiles[Index];
	Tile.bLoading = false;

	UWorld* W = GetWorld();
	if (!W || !Tile.Asset || Tile.bLoaded) return;

	// If the camera moved away while packing, don't spawn (and drop the packed data).
	FVector ViewLoc;
	if (GetViewLocation(ViewLoc) &&
		FVector::DistSquared(ViewLoc, Tile.WorldLocation) > UnloadRadius * UnloadRadius)
	{
		Tile.Asset->ReleaseRenderData();
		return;
	}

	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transient;   // streamer-managed; never saved into the level
	AGaussianSplatActor* A = W->SpawnActor<AGaussianSplatActor>(
		Tile.WorldLocation, FRotator(90.0f, 0.0f, 0.0f), Params);
	if (!A) return;

	if (A->GaussianSplatComponent)
	{
		A->GaussianSplatComponent->SetSplatAsset(Tile.Asset);
	}
#if WITH_EDITOR
	A->SetActorLabel(FPaths::GetBaseFilename(Tile.AssetPath));
#endif
	Tile.Actor = A;
	Tile.bLoaded = true;
}

void AGaussianTileStreamer::UnloadTile(FGaussianTileEntry& Tile)
{
	if (Tile.Actor)
	{
		Tile.Actor->Destroy();
		Tile.Actor = nullptr;
	}
	if (Tile.Asset)
	{
		// Frees the shared per-asset GPU buffers (proxy already gone above).
		Tile.Asset->ReleaseRenderData();
	}
	Tile.bLoaded = false;
	Tile.bLoading = false;
}

void AGaussianTileStreamer::UnloadAllTiles()
{
	for (FGaussianTileEntry& T : Tiles)
	{
		if (T.bLoaded)
		{
			UnloadTile(T);
		}
	}
}

void AGaussianTileStreamer::UpdateStreaming()
{
	if (!bManifestLoaded && !ManifestPath.IsEmpty())
	{
		LoadManifest();
	}
	if (Tiles.Num() == 0) return;

	FVector ViewLoc;
	if (!GetViewLocation(ViewLoc)) return;

	const float LoadR2 = LoadRadius * LoadRadius;
	// Effective unload distance is always >= LoadRadius (with 10% hysteresis) so that
	// raising LoadRadius alone widens the loaded set instead of thrashing the boundary.
	const float EffUnload = FMath::Max(UnloadRadius, LoadRadius * 1.1f);
	const float UnloadR2 = EffUnload * EffUnload;

	// 1) Unload everything out of range (cheap; frees VRAM).
	for (FGaussianTileEntry& T : Tiles)
	{
		if (T.bLoaded && FVector::DistSquared(ViewLoc, T.WorldLocation) > UnloadR2)
		{
			UnloadTile(T);
		}
	}

	// 2) Start at most one load per pass — the nearest in-range, not-yet-loaded tile —
	//    so we never enqueue a burst of packs/uploads in a single frame.
	int32 BestIdx = INDEX_NONE;
	float BestD2 = LoadR2;
	for (int32 i = 0; i < Tiles.Num(); ++i)
	{
		const FGaussianTileEntry& T = Tiles[i];
		if (T.bLoaded || T.bLoading) continue;
		const float D2 = FVector::DistSquared(ViewLoc, T.WorldLocation);
		if (D2 <= BestD2)
		{
			BestD2 = D2;
			BestIdx = i;
		}
	}
	if (BestIdx != INDEX_NONE)
	{
		BeginLoadTile(BestIdx);
	}
}

void AGaussianTileStreamer::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (UpdateEveryNFrames > 1 && (FrameCounter++ % UpdateEveryNFrames) != 0)
	{
		return;
	}
	UpdateStreaming();
}

#if WITH_EDITOR
void AGaussianTileStreamer::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	const FName Name = PropertyChangedEvent.GetPropertyName();
	if (Name == GET_MEMBER_NAME_CHECKED(AGaussianTileStreamer, ManifestPath) ||
		Name == GET_MEMBER_NAME_CHECKED(AGaussianTileStreamer, TileContentDir))
	{
		RebuildTileList();
	}
}
#endif
