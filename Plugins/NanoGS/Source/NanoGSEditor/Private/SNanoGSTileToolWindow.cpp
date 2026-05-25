// Copyright Epic Games, Inc. All Rights Reserved.

#include "SNanoGSTileToolWindow.h"
#include "NanoGSTileTool.h"

#include "Widgets/SCompoundWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBox.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/Paths.h"
#include "Misc/MessageDialog.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"

#define LOCTEXT_NAMESPACE "NanoGSTileTool"

class SNanoGSTileToolWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNanoGSTileToolWidget) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		ChildSlot
		[
			SNew(SVerticalBox)
			// --- PLY path row ---
			+ SVerticalBox::Slot().AutoHeight().Padding(8, 8, 8, 2)
			[ SNew(STextBlock).Text(LOCTEXT("PlyLabel", "Source PLY:")) ]
			+ SVerticalBox::Slot().AutoHeight().Padding(8, 0, 8, 6)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f)
				[ SAssignNew(PathBox, SEditableTextBox).HintText(LOCTEXT("PlyHint", "path to .ply"))
					.OnTextCommitted_Lambda([this](const FText& T, ETextCommit::Type) { PlyPath = T.ToString(); }) ]
				+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0, 0, 0)
				[ SNew(SButton).Text(LOCTEXT("Browse", "Browse...")).OnClicked(this, &SNanoGSTileToolWidget::OnBrowse) ]
			]
			// --- split checkbox ---
			+ SVerticalBox::Slot().AutoHeight().Padding(8, 6, 8, 2)
			[
				SNew(SCheckBox).IsChecked(ECheckBoxState::Checked)
				.OnCheckStateChanged_Lambda([this](ECheckBoxState S) { bSplit = (S == ECheckBoxState::Checked); })
				[ SNew(STextBlock).Text(LOCTEXT("Split", "Split into tiles")) ]
			]
			// --- tile count ---
			+ SVerticalBox::Slot().AutoHeight().Padding(28, 2, 8, 2)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[ SNew(STextBlock).Text(LOCTEXT("Count", "Tile count: ")) ]
				+ SHorizontalBox::Slot().AutoWidth()
				[ SNew(SBox).MinDesiredWidth(90)
					[ SNew(SSpinBox<int32>).MinValue(1).MaxValue(4096).MinSliderValue(1).MaxSliderValue(256)
						.Value_Lambda([this]() { return TileCount; })
						.OnValueChanged_Lambda([this](int32 V) { TileCount = V; })
						.IsEnabled_Lambda([this]() { return bSplit; }) ] ]
			]
			// --- options ---
			+ SVerticalBox::Slot().AutoHeight().Padding(8, 6, 8, 2)
			[
				SNew(SCheckBox).IsChecked(ECheckBoxState::Checked)
				.OnCheckStateChanged_Lambda([this](ECheckBoxState S) { bNanite = (S == ECheckBoxState::Checked); })
				[ SNew(STextBlock).Text(LOCTEXT("Nanite", "Build Nanite LOD")) ]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(8, 2, 8, 2)
			[
				SNew(SCheckBox).IsChecked(ECheckBoxState::Checked)
				.OnCheckStateChanged_Lambda([this](ECheckBoxState S) { bStreamer = (S == ECheckBoxState::Checked); })
				.IsEnabled_Lambda([this]() { return bSplit; })
				[ SNew(STextBlock).Text(LOCTEXT("Streamer", "Create streaming level")) ]
			]
			// --- generate ---
			+ SVerticalBox::Slot().AutoHeight().Padding(8, 12, 8, 8).HAlign(HAlign_Right)
			[ SNew(SButton).Text(LOCTEXT("Generate", "Generate")).OnClicked(this, &SNanoGSTileToolWidget::OnGenerate) ]
		];
	}

private:
	FString PlyPath;
	bool bSplit = true;
	int32 TileCount = 12;
	bool bNanite = true;
	bool bStreamer = true;
	TSharedPtr<SEditableTextBox> PathBox;

	FReply OnBrowse()
	{
		IDesktopPlatform* DP = FDesktopPlatformModule::Get();
		if (!DP) return FReply::Handled();
		const void* Parent = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
		TArray<FString> Files;
		if (DP->OpenFileDialog(Parent, TEXT("Select PLY"), TEXT(""), TEXT(""), TEXT("PLY files|*.ply"),
		                       EFileDialogFlags::None, Files) && Files.Num() > 0)
		{
			PlyPath = Files[0];
			if (PathBox.IsValid()) PathBox->SetText(FText::FromString(PlyPath));
		}
		return FReply::Handled();
	}

	FReply OnGenerate()
	{
		if (PlyPath.IsEmpty() && PathBox.IsValid()) PlyPath = PathBox->GetText().ToString();
		FNanoGSTileToolParams P;
		P.PlyPath = PlyPath; P.bSplit = bSplit; P.TileCount = TileCount;
		P.bBuildNanite = bNanite; P.bSetupStreamer = bStreamer;
		FString Msg;
		const bool bOk = FNanoGSTileTool::Run(P, Msg);
		FMessageDialog::Open(EAppMsgType::Ok,
			FText::FromString((bOk ? TEXT("NanoGS: ") : TEXT("NanoGS failed: ")) + Msg));
		return FReply::Handled();
	}
};

void OpenNanoGSTileToolWindow()
{
	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("WinTitle", "NanoGS — Tile & Stream"))
		.ClientSize(FVector2D(480, 320))
		.SupportsMaximize(false).SupportsMinimize(false);
	Window->SetContent(SNew(SNanoGSTileToolWidget));
	FSlateApplication::Get().AddWindow(Window);
}

#undef LOCTEXT_NAMESPACE
