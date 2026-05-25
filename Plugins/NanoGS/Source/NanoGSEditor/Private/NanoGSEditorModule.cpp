// Copyright Epic Games, Inc. All Rights Reserved.

#include "NanoGSEditorModule.h"
#include "GaussianSplatAssetTypeActions.h"
#include "GaussianSplatThumbnailRenderer.h"
#include "GaussianSplatAsset.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "ThumbnailRendering/ThumbnailManager.h"
#include "ToolMenus.h"
#include "SNanoGSTileToolWindow.h"
#include "NanoGSTileTool.h"
#include "HAL/IConsoleManager.h"

static void NanoGSGenerateTilesCmd(const TArray<FString>& Args)
{
	if (Args.Num() < 1)
	{
		UE_LOG(LogTemp, Warning, TEXT("Usage: nanogs.GenerateTiles <plypath> [tileCount=12] [split=1]"));
		return;
	}
	FNanoGSTileToolParams P;
	P.PlyPath = Args[0].TrimQuotes();
	P.TileCount = Args.Num() > 1 ? FCString::Atoi(*Args[1]) : 12;
	P.bSplit = Args.Num() > 2 ? (FCString::Atoi(*Args[2]) != 0) : true;
	FString Msg;
	const bool bOk = FNanoGSTileTool::Run(P, Msg);
	UE_LOG(LogTemp, Display, TEXT("NanoGSGenerateTiles %s: %s"), bOk ? TEXT("OK") : TEXT("FAIL"), *Msg);
}

#define LOCTEXT_NAMESPACE "FNanoGSEditorModule"

void FNanoGSEditorModule::StartupModule()
{
	// Register asset type actions
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	// Register Gaussian Splat asset type
	TSharedPtr<IAssetTypeActions> GaussianSplatAssetActions = MakeShareable(new FAssetTypeActions_GaussianSplatAsset());
	AssetTools.RegisterAssetTypeActions(GaussianSplatAssetActions.ToSharedRef());
	RegisteredAssetTypeActions.Add(GaussianSplatAssetActions);

	// Register custom thumbnail renderer for Gaussian Splat assets
	UThumbnailManager::Get().RegisterCustomRenderer(
		UGaussianSplatAsset::StaticClass(),
		UGaussianSplatThumbnailRenderer::StaticClass());

	// Add a "Tools" menu entry that opens the Tile & Stream window.
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateLambda([]()
	{
		FToolMenuOwnerScoped OwnerScoped(TEXT("NanoGSTileTool"));
		if (UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools")))
		{
			FToolMenuSection& Section = Menu->FindOrAddSection(TEXT("NanoGS"));
			Section.AddMenuEntry(
				TEXT("NanoGSTileTool"),
				LOCTEXT("MenuLabel", "NanoGS Tile & Stream..."),
				LOCTEXT("MenuTip", "Load a PLY, optionally split into tiles, import, build Nanite and set up streaming"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateStatic(&OpenNanoGSTileToolWindow)));
		}
	}));

	IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("nanogs.GenerateTiles"),
		TEXT("Load a PLY -> (split into tiles) -> import -> Nanite -> streamer. Args: <plypath> [tileCount=12] [split=1]"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&NanoGSGenerateTilesCmd),
		ECVF_Default);

	UE_LOG(LogTemp, Log, TEXT("GaussianSplattingEditor module started."));
}

void FNanoGSEditorModule::ShutdownModule()
{
	// Unregister asset type actions
	if (FModuleManager::Get().IsModuleLoaded("AssetTools"))
	{
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		for (auto& Action : RegisteredAssetTypeActions)
		{
			AssetTools.UnregisterAssetTypeActions(Action.ToSharedRef());
		}
	}
	RegisteredAssetTypeActions.Empty();

	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);

	UE_LOG(LogTemp, Log, TEXT("GaussianSplattingEditor module shutdown."));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FNanoGSEditorModule, NanoGSEditor)
