#include "ModTierEditor.h"

#include "ModTierOverrides.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "GameFramework/PlayerController.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateBrush.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SDPIScaler.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STileView.h"

TSharedPtr<SWidget> FModTierEditor::ViewportContent;
TWeakObjectPtr<APlayerController> FModTierEditor::OwningPlayer;

namespace
{
	const FLinearColor BackgroundColor(0.02f, 0.02f, 0.02f, 0.96f);
	const FLinearColor PanelColor(0.06f, 0.06f, 0.06f, 1.f);
	const FLinearColor AccentColor(0.955f, 0.3f, 0.066f, 1.f); // FICSIT orange
	const FLinearColor DimTextColor(0.6f, 0.6f, 0.6f, 1.f);

	FSlateFontInfo Font(int32 Size, bool bBold = false)
	{
		return FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size);
	}

	/** A row of the list on the left: a target with the brush its icon is drawn with. */
	struct FTargetRow
	{
		TSharedPtr<FModTierTarget> Target;
		FText Name;
		FSlateBrush Brush;

		void Refresh()
		{
			const UClass* Class = Target->Class.Get();
			Name = Class ? FModTierOverrides::GetDisplayName(Class) : FText::GetEmpty();
			Brush = FSlateBrush();
			Brush.ImageSize = FVector2D(64., 64.);
			UTexture2D* Icon = Class ? FModTierOverrides::GetIcon(Class->GetDefaultObject()) : nullptr;
			if (Icon)
			{
				Brush.SetResourceObject(Icon);
			}
			else
			{
				Brush.DrawAs = ESlateBrushDrawType::NoDrawType;
			}
		}
	};

	/** Where the icons offered on the right come from. */
	enum class EIconSource : uint8 { Unlocks, Items, Shop, Files };

	/** An icon that can be chosen. */
	struct FIconChoice
	{
		FString Spec;  // what is stored in Overrides.json
		FString Label; // what it is the icon of, for searching
		EIconSource Source = EIconSource::Items;
		FSlateBrush Brush;
	};

	class SModTierEditor : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SModTierEditor) {}
		SLATE_END_ARGS()

		void Construct(const FArguments& Args)
		{
			GatherRows();
			GatherIconChoices();

			ChildSlot
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
				.BorderBackgroundColor(BackgroundColor)
				.Padding(16.f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 12.f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text(FText::FromString(TEXT("Names and icons"))).Font(Font(18, true)).ColorAndOpacity(AccentColor)
						]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							MakeButton(TEXT("Close"), FOnClicked::CreateLambda([]() { FModTierEditor::Close(); return FReply::Handled(); }))
						]
					]
					+ SVerticalBox::Slot().FillHeight(1.f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(0.45f).Padding(0.f, 0.f, 12.f, 0.f)[ MakeTargetPanel() ]
						+ SHorizontalBox::Slot().FillWidth(0.55f)[ MakeDetailPanel() ]
					]
				]
			];

			RefreshTargetList();
			RefreshIconList();
		}

		virtual bool SupportsKeyboardFocus() const override { return true; }

		virtual FReply OnKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent) override
		{
			if (KeyEvent.GetKey() == EKeys::Escape)
			{
				FModTierEditor::Close();
				return FReply::Handled();
			}
			return FReply::Unhandled();
		}

		// Clicks next to the controls must not reach the game underneath.
		virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& MouseEvent) override { return FReply::Handled(); }

	private:
		// ------------------------------------------------------------------ building blocks

		static TSharedRef<SWidget> MakeButton(const TCHAR* Label, FOnClicked OnClicked)
		{
			return SNew(SButton)
				.OnClicked(OnClicked)
				.ContentPadding(FMargin(10.f, 4.f))
				[
					SNew(STextBlock).Text(FText::FromString(Label)).Font(Font(10)).ColorAndOpacity(FLinearColor::Black)
				];
		}

		TSharedRef<SWidget> MakeToggle(const TCHAR* Label, bool& bValue, void (SModTierEditor::*OnChanged)())
		{
			bool* Value = &bValue;
			return SNew(SCheckBox)
				.IsChecked_Lambda([Value]() { return *Value ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this, Value, OnChanged](ECheckBoxState State)
				{
					*Value = State == ECheckBoxState::Checked;
					(this->*OnChanged)();
				})
				.Padding(FMargin(4.f, 0.f, 12.f, 0.f))
				[
					SNew(STextBlock).Text(FText::FromString(Label)).Font(Font(10)).ColorAndOpacity(FLinearColor::White)
				];
		}

		static TSharedRef<SWidget> MakePanel(const TSharedRef<SWidget>& Content)
		{
			return SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
				.BorderBackgroundColor(PanelColor)
				.Padding(10.f)
				[
					Content
				];
		}

		// ------------------------------------------------------------------ left: what to change

		TSharedRef<SWidget> MakeTargetPanel()
		{
			return MakePanel(
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
				[
					SNew(SEditableTextBox)
					.HintText(FText::FromString(TEXT("Search by name or mod")))
					.Font(Font(11))
					.OnTextChanged_Lambda([this](const FText& Text) { TargetSearch = Text.ToString(); RefreshTargetList(); })
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()[ MakeToggle(TEXT("Milestones"), bShowMilestones, &SModTierEditor::RefreshTargetList) ]
					+ SHorizontalBox::Slot().AutoWidth()[ MakeToggle(TEXT("MAM"), bShowResearch, &SModTierEditor::RefreshTargetList) ]
					+ SHorizontalBox::Slot().AutoWidth()[ MakeToggle(TEXT("Items"), bShowItems, &SModTierEditor::RefreshTargetList) ]
					+ SHorizontalBox::Slot().AutoWidth()[ MakeToggle(TEXT("Other"), bShowOther, &SModTierEditor::RefreshTargetList) ]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()[ MakeToggle(TEXT("Changed only"), bChangedOnly, &SModTierEditor::RefreshTargetList) ]
					+ SHorizontalBox::Slot().AutoWidth()[ MakeToggle(TEXT("Base game too"), bShowBaseGame, &SModTierEditor::RefreshTargetList) ]
				]
				+ SVerticalBox::Slot().FillHeight(1.f)
				[
					SAssignNew(TargetList, SListView<TSharedPtr<FTargetRow>>)
					.ListItemsSource(&VisibleRows)
					.SelectionMode(ESelectionMode::Single)
					.OnGenerateRow(this, &SModTierEditor::MakeTargetRow)
					.OnSelectionChanged(this, &SModTierEditor::OnTargetSelected)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
				[
					SNew(STextBlock).Font(Font(9)).ColorAndOpacity(DimTextColor)
					.Text_Lambda([this]() { return FText::FromString(FString::Printf(TEXT("%d shown"), VisibleRows.Num())); })
				]);
		}

		TSharedRef<ITableRow> MakeTargetRow(TSharedPtr<FTargetRow> Row, const TSharedRef<STableViewBase>& Owner)
		{
			const TCHAR* Kind = Row->Target->Kind == EModTierTargetKind::Milestone ? TEXT("Milestone")
				: Row->Target->Kind == EModTierTargetKind::Research ? TEXT("MAM")
				: Row->Target->Kind == EModTierTargetKind::Item ? TEXT("Item") : TEXT("Other");
			const FString Source = Row->Target->Mod.IsEmpty() ? TEXT("Base game") : Row->Target->Mod;
			return SNew(STableRow<TSharedPtr<FTargetRow>>, Owner)
				.Padding(FMargin(2.f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SBox).WidthOverride(36.f).HeightOverride(36.f)
						[
							SNew(SImage).Image_Lambda([Row]() { return &Row->Brush; })
						]
					]
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(8.f, 0.f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(STextBlock).Font(Font(11)).ColorAndOpacity(FLinearColor::White)
							.Text_Lambda([Row]() { return Row->Name; })
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(STextBlock).Font(Font(8)).ColorAndOpacity(DimTextColor)
							.Text(FText::FromString(FString::Printf(TEXT("%s  -  %s"), Kind, *Source)))
						]
					]
				];
		}

		void GatherRows()
		{
			TArray<TSharedPtr<FModTierTarget>> Targets;
			FModTierOverrides::GatherTargets(Targets);
			for (const TSharedPtr<FModTierTarget>& Target : Targets)
			{
				const TSharedPtr<FTargetRow> Row = MakeShared<FTargetRow>();
				Row->Target = Target;
				Row->Refresh();
				AllRows.Add(Row);
			}
			AllRows.Sort([](const TSharedPtr<FTargetRow>& A, const TSharedPtr<FTargetRow>& B)
			{
				// Some mods have names that start with a space; those must not all sort to the top.
				return A->Name.ToString().TrimStart().Compare(B->Name.ToString().TrimStart(), ESearchCase::IgnoreCase) < 0;
			});
		}

		void RefreshTargetList()
		{
			VisibleRows.Reset();
			for (const TSharedPtr<FTargetRow>& Row : AllRows)
			{
				const FModTierTarget& Target = *Row->Target;
				const bool bKind = Target.Kind == EModTierTargetKind::Milestone ? bShowMilestones
					: Target.Kind == EModTierTargetKind::Research ? bShowResearch
					: Target.Kind == EModTierTargetKind::Item ? bShowItems : bShowOther;
				const bool bChanged = !FModTierOverrides::GetNameOverride(Target.Path).IsEmpty() || !FModTierOverrides::GetIconOverride(Target.Path).IsEmpty();
				if (bChangedOnly && !bChanged)
				{
					continue;
				}
				// With no kind ticked at all, every kind is shown: an empty list helps nobody.
				// A changed entry of the base game is always listed under "Changed only".
				const bool bAnyKind = bShowMilestones || bShowResearch || bShowItems || bShowOther;
				if ((bAnyKind && !bKind) || (Target.Mod.IsEmpty() && !bShowBaseGame && !bChangedOnly))
				{
					continue;
				}
				if (!TargetSearch.IsEmpty() && !Row->Name.ToString().Contains(TargetSearch) && !Target.Mod.Contains(TargetSearch))
				{
					continue;
				}
				VisibleRows.Add(Row);
			}
			if (TargetList.IsValid())
			{
				TargetList->RequestListRefresh();
			}
		}

		void OnTargetSelected(TSharedPtr<FTargetRow> Row, ESelectInfo::Type)
		{
			Selected = Row;
			if (Row.IsValid())
			{
				// Offer the icons that fit: the HUB and the MAM draw theirs as flat shapes, items are in colour.
				IconSource = Row->Target->Kind == EModTierTargetKind::Item ? EIconSource::Items : EIconSource::Unlocks;
				RefreshIconList();
			}
			if (NameBox.IsValid())
			{
				NameBox->SetText(Row.IsValid() ? Row->Name : FText::GetEmpty());
			}
		}

		// ------------------------------------------------------------------ right: the change

		TSharedRef<SWidget> MakeDetailPanel()
		{
			return MakePanel(
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 10.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SBox).WidthOverride(96.f).HeightOverride(96.f)
						[
							SNew(SImage).Image_Lambda([this]() { return Selected.IsValid() ? &Selected->Brush : FCoreStyle::Get().GetBrush("NoBrush"); })
						]
					]
					+ SHorizontalBox::Slot().FillWidth(1.f).Padding(12.f, 0.f, 0.f, 0.f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
						[
							SNew(STextBlock).Font(Font(9)).ColorAndOpacity(DimTextColor)
							.Text_Lambda([this]()
							{
								return FText::FromString(Selected.IsValid() ? Selected->Target->Path : FString(TEXT("Pick something on the left.")));
							})
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
						[
							SAssignNew(NameBox, SEditableTextBox)
							.HintText(FText::FromString(TEXT("Name (Enter to apply)")))
							.Font(Font(12))
							.IsEnabled_Lambda([this]() { return Selected.IsValid(); })
							.OnTextCommitted(this, &SModTierEditor::OnNameCommitted)
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
							[
								MakeButton(TEXT("Original name"), FOnClicked::CreateSP(this, &SModTierEditor::OnResetName))
							]
							+ SHorizontalBox::Slot().AutoWidth()
							[
								MakeButton(TEXT("Original icon"), FOnClicked::CreateSP(this, &SModTierEditor::OnResetIcon))
							]
						]
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 10.f, 0.f)
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("Icons from:"))).Font(Font(10)).ColorAndOpacity(FLinearColor::White)
					]
					+ SHorizontalBox::Slot().AutoWidth()[ MakeSourceToggle(TEXT("Milestones and MAM"), EIconSource::Unlocks) ]
					+ SHorizontalBox::Slot().AutoWidth()[ MakeSourceToggle(TEXT("Items"), EIconSource::Items) ]
					+ SHorizontalBox::Slot().AutoWidth()[ MakeSourceToggle(TEXT("Shop and other"), EIconSource::Shop) ]
					+ SHorizontalBox::Slot().AutoWidth()[ MakeSourceToggle(TEXT("My PNG files"), EIconSource::Files) ]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
				[
					SNew(SEditableTextBox)
					.HintText(FText::FromString(TEXT("Search icons")))
					.Font(Font(11))
					.OnTextChanged_Lambda([this](const FText& Text) { IconSearch = Text.ToString(); RefreshIconList(); })
				]
				+ SVerticalBox::Slot().FillHeight(1.f)
				[
					SAssignNew(IconList, STileView<TSharedPtr<FIconChoice>>)
					.ListItemsSource(&VisibleIcons)
					.ItemWidth(76.f)
					.ItemHeight(76.f)
					.SelectionMode(ESelectionMode::Single)
					.OnGenerateTile(this, &SModTierEditor::MakeIconTile)
					.OnSelectionChanged(this, &SModTierEditor::OnIconSelected)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
				[
					SNew(STextBlock).Font(Font(9)).ColorAndOpacity(DimTextColor).AutoWrapText(true)
					.Text_Lambda([this]()
					{
						return FText::FromString(IconSource == EIconSource::Files
							? FString::Printf(TEXT("PNG files in %s. Reopen this window after adding files."), *(FModTierOverrides::GetDirectory() / TEXT("Icons")))
							: FString(TEXT("Milestone and MAM icons are drawn as flat white shapes by the game; item icons keep their colours.")));
					})
				]);
		}

		TSharedRef<SWidget> MakeSourceToggle(const TCHAR* Label, EIconSource Source)
		{
			return SNew(SCheckBox)
				.IsChecked_Lambda([this, Source]() { return IconSource == Source ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this, Source](ECheckBoxState)
				{
					IconSource = Source;
					RefreshIconList();
				})
				.Padding(FMargin(4.f, 0.f, 12.f, 0.f))
				[
					SNew(STextBlock).Text(FText::FromString(Label)).Font(Font(10)).ColorAndOpacity(FLinearColor::White)
				];
		}

		TSharedRef<ITableRow> MakeIconTile(TSharedPtr<FIconChoice> Choice, const TSharedRef<STableViewBase>& Owner)
		{
			return SNew(STableRow<TSharedPtr<FIconChoice>>, Owner)
				.Padding(FMargin(4.f))
				.ToolTipText(FText::FromString(Choice->Label))
				[
					SNew(SBox).WidthOverride(64.f).HeightOverride(64.f)
					[
						SNew(SImage).Image(&Choice->Brush)
					]
				];
		}

		void AddIconChoice(const FString& Spec, const FString& Label, EIconSource Source, UTexture2D* Texture, TSet<const UTexture2D*>& Seen)
		{
			if (!Texture || Seen.Contains(Texture))
			{
				return; // many things share an icon: offer it once
			}
			Seen.Add(Texture);
			const TSharedPtr<FIconChoice> Choice = MakeShared<FIconChoice>();
			Choice->Spec = Spec;
			Choice->Label = Label;
			Choice->Source = Source;
			Choice->Brush.ImageSize = FVector2D(64., 64.);
			Choice->Brush.SetResourceObject(Texture);
			AllIcons.Add(Choice);
		}

		void GatherIconChoices()
		{
			TSet<const UTexture2D*> Seen;
			for (const TSharedPtr<FTargetRow>& Row : AllRows)
			{
				const UClass* Class = Row->Target->Class.Get();
				if (!Class)
				{
					continue;
				}
				// ResolveIcon gives the icon it came with, also when it has been replaced.
				const FString Spec = TEXT("copy:") + Row->Target->Path;
				AddIconChoice(Spec, Row->Name.ToString(),
					Row->Target->Kind == EModTierTargetKind::Item ? EIconSource::Items
					: Row->Target->Kind == EModTierTargetKind::Other ? EIconSource::Shop : EIconSource::Unlocks,
					FModTierOverrides::ResolveIcon(Spec), Seen);
			}
			TArray<FString> Files;
			FModTierOverrides::GetIconFiles(Files);
			for (const FString& File : Files)
			{
				const FString Spec = TEXT("file:") + File;
				AddIconChoice(Spec, File, EIconSource::Files, FModTierOverrides::ResolveIcon(Spec), Seen);
			}
		}

		void RefreshIconList()
		{
			VisibleIcons.Reset();
			for (const TSharedPtr<FIconChoice>& Choice : AllIcons)
			{
				if (Choice->Source == IconSource && (IconSearch.IsEmpty() || Choice->Label.Contains(IconSearch)))
				{
					VisibleIcons.Add(Choice);
				}
			}
			if (IconList.IsValid())
			{
				IconList->ClearSelection();
				IconList->RequestListRefresh();
			}
		}

		void OnIconSelected(TSharedPtr<FIconChoice> Choice, ESelectInfo::Type SelectInfo)
		{
			if (Choice.IsValid() && Selected.IsValid() && SelectInfo != ESelectInfo::Direct)
			{
				FModTierOverrides::SetIconOverride(Selected->Target->Path, Choice->Spec);
				AfterChange();
			}
		}

		void OnNameCommitted(const FText& Text, ETextCommit::Type CommitType)
		{
			if (!Selected.IsValid() || CommitType != ETextCommit::OnEnter)
			{
				return;
			}
			const FString Name = Text.ToString().TrimStartAndEnd();
			if (!Name.IsEmpty() && Name != Selected->Name.ToString())
			{
				FModTierOverrides::SetNameOverride(Selected->Target->Path, Name);
				AfterChange();
			}
		}

		FReply OnResetName()
		{
			if (Selected.IsValid())
			{
				FModTierOverrides::SetNameOverride(Selected->Target->Path, FString());
				AfterChange();
			}
			return FReply::Handled();
		}

		FReply OnResetIcon()
		{
			if (Selected.IsValid())
			{
				FModTierOverrides::SetIconOverride(Selected->Target->Path, FString());
				AfterChange();
			}
			return FReply::Handled();
		}

		void AfterChange()
		{
			Selected->Refresh();
			if (NameBox.IsValid())
			{
				NameBox->SetText(Selected->Name);
			}
		}

		TArray<TSharedPtr<FTargetRow>> AllRows;
		TArray<TSharedPtr<FTargetRow>> VisibleRows;
		TSharedPtr<SListView<TSharedPtr<FTargetRow>>> TargetList;
		TSharedPtr<FTargetRow> Selected;
		FString TargetSearch;
		bool bShowMilestones = true;
		bool bShowResearch = true;
		bool bShowItems = true;
		bool bShowOther = false;
		bool bChangedOnly = false;
		bool bShowBaseGame = false;

		TArray<TSharedPtr<FIconChoice>> AllIcons;
		TArray<TSharedPtr<FIconChoice>> VisibleIcons;
		TSharedPtr<STileView<TSharedPtr<FIconChoice>>> IconList;
		EIconSource IconSource = EIconSource::Unlocks;
		FString IconSearch;

		TSharedPtr<SEditableTextBox> NameBox;
	};
}

bool FModTierEditor::IsOpen()
{
	return ViewportContent.IsValid();
}

void FModTierEditor::Open(APlayerController* Player)
{
	if (IsOpen() || !Player || !Player->IsLocalController() || !GEngine || !GEngine->GameViewport)
	{
		return;
	}
	// Sized for a 1080p screen and scaled with the height of the game window.
	FVector2D ViewportSize(1920., 1080.);
	GEngine->GameViewport->GetViewportSize(ViewportSize);
	const float Scale = FMath::Clamp(static_cast<float>(ViewportSize.Y) / 1080.f, 0.5f, 4.f);

	const TSharedRef<SModTierEditor> Editor = SNew(SModTierEditor);
	ViewportContent = SNew(SDPIScaler)
		.DPIScale(Scale)
		[
			SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(1200.f).HeightOverride(760.f)
				[
					Editor
				]
			]
		];
	GEngine->GameViewport->AddViewportWidgetContent(ViewportContent.ToSharedRef(), 1000);

	OwningPlayer = Player;
	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(Editor);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	Player->SetInputMode(InputMode);
	Player->SetShowMouseCursor(true);
}

void FModTierEditor::Close()
{
	if (!IsOpen())
	{
		return;
	}
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(ViewportContent.ToSharedRef());
	}
	ViewportContent.Reset();
	if (APlayerController* Player = OwningPlayer.Get())
	{
		Player->SetInputMode(FInputModeGameOnly());
		Player->SetShowMouseCursor(false);
	}
	OwningPlayer.Reset();
}
