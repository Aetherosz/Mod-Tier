#include "ModTier.h"

#include "FGSchematic.h"
#include "Configuration/ConfigManager.h"
#include "Configuration/Properties/ConfigPropertyBool.h"
#include "Configuration/Properties/ConfigPropertyInteger.h"
#include "Configuration/Properties/ConfigPropertySection.h"
#include "Configuration/Properties/ConfigPropertyString.h"
#include "Engine/GameInstance.h"
#include "ModTierConfig.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Engine/World.h"
#include "FGCharacterPlayer.h"
#include "FGSchematicManager.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "ModTierEditor.h"
#include "ModTierOverrides.h"
#include "Kismet/KismetTextLibrary.h"
#include "Patching/NativeHookManager.h"
#include "TimerManager.h"
#include "UObject/Script.h"
#include "UObject/Stack.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogModTier, Log, All);

TWeakObjectPtr<UConfigManager> FModTier::ConfigManager;
bool FModTier::bTabEnabled = true;
bool FModTier::bUnlockAllAtOnce = false;

namespace
{
	const FName HubClassName(TEXT("BPW_Hub_C"));
	const FName TierButtonClassName(TEXT("BPW_Hub_TierButton_C"));

	/** Cached per schematic manager (one per loaded save). */
	TWeakObjectPtr<const AFGSchematicManager> CachedManager;
	int32 CachedModTier = INDEX_NONE;

	/** The loaded save's world: the names and icons window can be opened while it is alive. */
	TWeakObjectPtr<UWorld> EditorWorld;

	/** What the mod tier's button shows in place of a number, and on which side of the tiers it is drawn. */
	FString TierLabel = TEXT("M");
	bool bTierOnLeft = true;

	/** The key choice of the Mod Configuration page. */
	EModTierEditorKey EditorKeyChoice = EModTierEditorKey::CtrlShiftM;

	/** Ctrl+Shift+<key> opens and closes the names and icons window. Sees keys before the game does. */
	class FEditorKey : public IInputProcessor
	{
	public:
		virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}

		virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& KeyEvent) override
		{
			const FKey Wanted = EditorKeyChoice == EModTierEditorKey::CtrlShiftM ? EKeys::M
				: EditorKeyChoice == EModTierEditorKey::CtrlShiftN ? EKeys::N
				: EditorKeyChoice == EModTierEditorKey::CtrlShiftK ? EKeys::K : EKeys::Invalid;
			if (!Wanted.IsValid() || KeyEvent.GetKey() != Wanted || !KeyEvent.IsControlDown() || !KeyEvent.IsShiftDown() || KeyEvent.IsRepeat())
			{
				return false;
			}
			if (FModTierEditor::IsOpen())
			{
				FModTierEditor::Close();
				return true;
			}
			UWorld* World = EditorWorld.Get();
			APlayerController* Player = World ? World->GetFirstPlayerController() : nullptr;
			if (!Player || !Player->IsLocalController())
			{
				return false; // main menu, or no save loaded yet
			}
			FModTierEditor::Open(Player);
			return true;
		}
	};

	void RegisterEditorKey()
	{
		static TSharedPtr<FEditorKey> EditorKey;
		if (!EditorKey.IsValid() && FSlateApplication::IsInitialized())
		{
			EditorKey = MakeShared<FEditorKey>();
			FSlateApplication::Get().RegisterInputPreProcessor(EditorKey);
		}
	}

	FNativeFuncPtr OriginalConvIntToText = nullptr;
	FNativeFuncPtr OriginalAddChildToHorizontalBox = nullptr;
	const FName TierListName(TEXT("mTierList"));

	/** Swaps the Blueprint entry point of a native function. C++ callers don't go through it. */
	FNativeFuncPtr ReplaceEntryPoint(UClass* Class, const TCHAR* FunctionName, FNativeFuncPtr Replacement)
	{
		UFunction* Function = Class ? Class->FindFunctionByName(FunctionName) : nullptr;
		if (!Function || !Function->HasAnyFunctionFlags(FUNC_Native))
		{
			UE_LOG(LogModTier, Error, TEXT("Function %s not found; the mod tab will not work"), FunctionName);
			return nullptr;
		}
		const FNativeFuncPtr Original = Function->GetNativeFunc();
		Function->SetNativeFunc(Replacement);
		return Original;
	}
}

void FModTier::RegisterHooks()
{
#if !WITH_EDITOR
	// Register the mod config as soon as the game instance exists, so it is listed in the main
	// menu's Mods -> Mod Configuration before any save loads.
	FWorldDelegates::OnStartGameInstance.AddStatic(&FModTier::RegisterConfig);

	// Write config changes right away, and use them right away. SML otherwise saves on a timer that
	// doesn't run while the game is paused, and its flush at shutdown crashes.
	SUBSCRIBE_METHOD_AFTER(UConfigManager::MarkConfigurationDirty,
		[](UConfigManager* Self, const FConfigId& Id)
		{
			if (Id == UModTierConfig::GetId())
			{
				Self->FlushPendingSaves();
				RefreshSettings();
			}
		});

	ReplaceEntryPoint(UFGSchematic::StaticClass(), TEXT("GetTechTier"), &FModTier::ExecGetTechTier);
	ReplaceEntryPoint(AFGSchematicManager::StaticClass(), TEXT("GetHighestAvailableTechTier"), &FModTier::ExecGetHighestAvailableTechTier);
	OriginalConvIntToText = ReplaceEntryPoint(UKismetTextLibrary::StaticClass(), TEXT("Conv_IntToText"), &FModTier::ExecConvIntToText);
	OriginalAddChildToHorizontalBox = ReplaceEntryPoint(UHorizontalBox::StaticClass(), TEXT("AddChildToHorizontalBox"), &FModTier::ExecAddChildToHorizontalBox);

	// The game's own list of a tier's milestones, used for the tier's state: leave the moved ones out,
	// so a tier doesn't stay "not finished" because of milestones that are shown elsewhere.
	SUBSCRIBE_METHOD_AFTER(AFGSchematicManager::GetHubSchematicsForTier,
		[](const AFGSchematicManager* Self, int32 Tier, TArray<TSubclassOf<UFGSchematic>>& Out)
		{
			if (Tier == GetModTier(Self))
			{
				GetModTabSchematics(Self, Out);
			}
			else
			{
				Out.RemoveAll([](const TSubclassOf<UFGSchematic>& Schematic) { return IsMovedSchematic(Schematic); });
			}
		});

	// Whether a tier is locked, has milestones left, or is done.
	SUBSCRIBE_METHOD(AFGSchematicManager::GetTechTierState,
		[](auto& Scope, const AFGSchematicManager* Self, int32 Tier)
		{
			if (!bTabEnabled || Tier != GetModTier(Self))
			{
				return; // the game's function runs as usual
			}
			TArray<TSubclassOf<UFGSchematic>> Schematics;
			GetModTabSchematics(Self, Schematics);
			// Never locked: the mod tier is open from the start, also while it is still empty.
			ETechTierState State = ETechTierState::ETTS_FullyPurchased;
			for (const TSubclassOf<UFGSchematic>& Schematic : Schematics)
			{
				if (!Self->IsSchematicPurchased(Schematic))
				{
					State = ETechTierState::ETTS_Available;
					break;
				}
			}
			Scope.Override(State);
		});

	// Names and icons are replaced once per loaded world, when the first player appears (by then the
	// content of all mods is registered). Clients do it too: what is replaced is only what is shown.
	SUBSCRIBE_METHOD_VIRTUAL_AFTER(AFGCharacterPlayer::BeginPlay, GetMutableDefault<AFGCharacterPlayer>(),
		[](AFGCharacterPlayer* Self)
		{
			static TWeakObjectPtr<UWorld> AppliedWorld;
			UWorld* World = Self->GetWorld();
			if (World && AppliedWorld.Get() != World)
			{
				AppliedWorld = World;
				if (!ConfigManager.IsValid())
				{
					RegisterConfig(Self->GetGameInstance()); // fallback if OnStartGameInstance fired before we loaded
				}
				RefreshSettings();
				FModTierOverrides::Apply();
				EditorWorld = World;
				RegisterEditorKey();
			}
		});

	FWorldDelegates::OnWorldCleanup.AddLambda([](UWorld*, bool, bool) { FModTierEditor::Close(); });

	UE_LOG(LogModTier, Log, TEXT("Hooks registered"));
#endif
}

bool FModTier::IsMovedSchematic(TSubclassOf<UFGSchematic> Schematic)
{
	if (!bTabEnabled || !Schematic || UFGSchematic::GetType(Schematic) != ESchematicType::EST_Milestone)
	{
		return false;
	}
	// Everything of the base game lives under /Game/; a mod's content under its own root.
	return !Schematic->GetPathName().StartsWith(TEXT("/Game/"));
}

int32 FModTier::GetModTier(const AFGSchematicManager* Manager)
{
	if (CachedManager.Get() != Manager)
	{
		CachedManager = Manager;
		CachedModTier = const_cast<AFGSchematicManager*>(Manager)->GetHighestAvailableTechTier() + 1;
		UE_LOG(LogModTier, Log, TEXT("Mod tier is %d"), CachedModTier);
	}
	return CachedModTier;
}

bool FModTier::IsShownInModTab(const AFGSchematicManager* Manager, TSubclassOf<UFGSchematic> Schematic)
{
	if (!IsMovedSchematic(Schematic))
	{
		return false;
	}
	return bUnlockAllAtOnce || Manager->GetTechTierState(UFGSchematic::GetTechTier(Schematic)) != ETechTierState::ETTS_Locked;
}

void FModTier::GetModTabSchematics(const AFGSchematicManager* Manager, TArray<TSubclassOf<UFGSchematic>>& Out)
{
	Out.Reset();
	TArray<TSubclassOf<UFGSchematic>> All;
	Manager->GetAllSchematics(All);
	for (const TSubclassOf<UFGSchematic>& Schematic : All)
	{
		if (IsShownInModTab(Manager, Schematic))
		{
			Out.Add(Schematic);
		}
	}
}

bool FModTier::IsCalledBy(const FFrame& Stack, const FName& ClassName)
{
	// For a native function called from Blueprint code, the frame is the calling Blueprint function's.
	const UClass* Class = Stack.Node ? Stack.Node->GetOuterUClass() : nullptr;
	for (; Class && !Class->HasAnyClassFlags(CLASS_Native); Class = Class->GetSuperClass())
	{
		if (Class->GetFName() == ClassName)
		{
			return true;
		}
	}
	return false;
}

// static int32 UFGSchematic::GetTechTier( TSubclassOf< UFGSchematic > inClass )
void FModTier::ExecGetTechTier(UObject* Context, FFrame& Stack, RESULT_DECL)
{
	P_GET_OBJECT(UClass, InClass);
	P_FINISH;
	int32 Tier = UFGSchematic::GetTechTier(InClass);
	if (IsCalledBy(Stack, HubClassName) && IsMovedSchematic(InClass))
	{
		// In the HUB a moved milestone belongs to the mod tier, or to no tier while it is still gated.
		const AFGSchematicManager* Manager = AFGSchematicManager::Get(Stack.Object);
		Tier = Manager && IsShownInModTab(Manager, InClass) ? GetModTier(Manager) : INDEX_NONE;
	}
	*static_cast<int32*>(RESULT_PARAM) = Tier;
}

// int32 AFGSchematicManager::GetHighestAvailableTechTier()
void FModTier::ExecGetHighestAvailableTechTier(UObject* Context, FFrame& Stack, RESULT_DECL)
{
	P_FINISH;
	AFGSchematicManager* Manager = CastChecked<AFGSchematicManager>(Context);
	int32 Tier = Manager->GetHighestAvailableTechTier();
	if (bTabEnabled && IsCalledBy(Stack, HubClassName))
	{
		// One more tier button, from the start of the game: also while it has nothing to show yet.
		Tier = GetModTier(Manager);
	}
	*static_cast<int32*>(RESULT_PARAM) = Tier;
}

// static FText UKismetTextLibrary::Conv_IntToText( int32 Value, ... )
void FModTier::ExecConvIntToText(UObject* Context, FFrame& Stack, RESULT_DECL)
{
	const bool bTierButton = CachedModTier != INDEX_NONE && CachedManager.IsValid() && IsCalledBy(Stack, TierButtonClassName);
	OriginalConvIntToText(Context, Stack, RESULT_PARAM);
	if (bTierButton)
	{
		// The number on the mod tier's button.
		FText& Text = *static_cast<FText*>(RESULT_PARAM);
		if (Text.ToString() == FString::FromInt(CachedModTier))
		{
			Text = FText::FromString(TierLabel);
		}
	}
}

// UHorizontalBoxSlot* UHorizontalBox::AddChildToHorizontalBox( UWidget* Content )
void FModTier::ExecAddChildToHorizontalBox(UObject* Context, FFrame& Stack, RESULT_DECL)
{
	const bool bTierList = CachedModTier != INDEX_NONE && Context && Context->GetFName() == TierListName && IsCalledBy(Stack, HubClassName);
	OriginalAddChildToHorizontalBox(Context, Stack, RESULT_PARAM);
	if (!bTierList || !bTierOnLeft)
	{
		return; // on the right is where the HUB puts it by itself
	}
	// The mod tier's button is the last one the HUB adds; once it is in, put it in front.
	UHorizontalBox* TierList = CastChecked<UHorizontalBox>(Context);
	const UWidget* Last = TierList->GetChildAt(TierList->GetChildrenCount() - 1);
	const FIntProperty* TierProperty = Last ? FindFProperty<FIntProperty>(Last->GetClass(), TEXT("mTier")) : nullptr;
	if (!TierProperty || TierProperty->GetPropertyValue_InContainer(Last) != CachedModTier)
	{
		return;
	}
	MoveModTierButtonToFront(TierList);
	// Again once the buttons have been laid out, in case their sizes weren't known yet.
	if (UWorld* World = TierList->GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(TierList, [TierList]() { MoveModTierButtonToFront(TierList); }));
	}
}

void FModTier::MoveModTierButtonToFront(UHorizontalBox* TierList)
{
	// The HUB finds a tier's button by its position in the list, so the order stays as it is and
	// the buttons are only drawn somewhere else: the last one first, the others one place further.
	const int32 Count = TierList->GetChildrenCount();
	if (Count < 2)
	{
		return;
	}
	const auto GetWidth = [](UWidget* Widget)
	{
		Widget->ForceLayoutPrepass();
		float Width = Widget->GetDesiredSize().X;
		if (const UHorizontalBoxSlot* Slot = Cast<UHorizontalBoxSlot>(Widget->Slot))
		{
			Width += Slot->GetPadding().Left + Slot->GetPadding().Right;
		}
		return Width;
	};
	UWidget* ModButton = TierList->GetChildAt(Count - 1);
	const float ModWidth = GetWidth(ModButton);
	float OthersWidth = 0.f;
	for (int32 Index = 0; Index < Count - 1; ++Index)
	{
		UWidget* Button = TierList->GetChildAt(Index);
		OthersWidth += GetWidth(Button);
		Button->SetRenderTranslation(FVector2D(ModWidth, 0.));
	}
	ModButton->SetRenderTranslation(FVector2D(-OthersWidth, 0.));
}

// ---------------------------------------------------------------------------
// Mod config
// ---------------------------------------------------------------------------

void FModTier::RegisterConfig(UGameInstance* GameInstance)
{
	if (!GameInstance || ConfigManager.IsValid())
	{
		return;
	}
	UConfigManager* Manager = GameInstance->GetSubsystem<UConfigManager>();
	if (!Manager)
	{
		UE_LOG(LogModTier, Error, TEXT("SML config manager not found; using default settings"));
		return;
	}
	UModTierConfig::UseMenuWidgets();
	Manager->RegisterModConfiguration(UModTierConfig::StaticClass());
	ConfigManager = Manager;
	RefreshSettings();
}

void FModTier::RefreshSettings()
{
	UConfigManager* Manager = ConfigManager.Get();
	const UConfigPropertySection* Root = Manager ? Manager->GetConfigurationRootSection(UModTierConfig::GetId()) : nullptr;
	if (!Root)
	{
		return; // defaults stay
	}
	const auto GetBool = [Root](const FString& Key, bool bFallback)
	{
		const TObjectPtr<UConfigProperty>* Property = Root->SectionProperties.Find(Key);
		const UConfigPropertyBool* Bool = Property ? Cast<UConfigPropertyBool>(Property->Get()) : nullptr;
		return Bool ? Bool->Value : bFallback;
	};
	bTabEnabled = GetBool(UModTierConfig::TabKey, true);
	bUnlockAllAtOnce = GetBool(UModTierConfig::ShowAllKey, false);
	FModTierOverrides::SetEnabled(GetBool(UModTierConfig::OverridesKey, true));
	bTierOnLeft = GetBool(UModTierConfig::LeftKey, true);

	const TObjectPtr<UConfigProperty>* LabelProperty = Root->SectionProperties.Find(UModTierConfig::LabelKey);
	const UConfigPropertyString* Label = LabelProperty ? Cast<UConfigPropertyString>(LabelProperty->Get()) : nullptr;
	TierLabel = Label ? Label->Value.TrimStartAndEnd() : UModTierConfig::DefaultLabel;
	if (TierLabel.IsEmpty())
	{
		TierLabel = UModTierConfig::DefaultLabel; // a button with nothing on it helps nobody
	}

	const TObjectPtr<UConfigProperty>* KeyProperty = Root->SectionProperties.Find(UModTierConfig::EditorKeyKey);
	const UConfigPropertyInteger* Key = KeyProperty ? Cast<UConfigPropertyInteger>(KeyProperty->Get()) : nullptr;
	const int32 KeyValue = Key ? Key->Value : 0;
	EditorKeyChoice = KeyValue >= 0 && KeyValue <= static_cast<int32>(EModTierEditorKey::Off)
		? static_cast<EModTierEditorKey>(KeyValue) : EModTierEditorKey::CtrlShiftM;
}
