#include "TidyMilestonesConfig.h"

#include "Configuration/Properties/ConfigPropertyBool.h"
#include "Configuration/Properties/ConfigPropertyInteger.h"
#include "Configuration/Properties/ConfigPropertySection.h"
#include "Configuration/Properties/ConfigPropertyString.h"
#include "Configuration/Properties/WidgetExtension/CP_Integer.h"
#include "Configuration/Properties/WidgetExtension/CP_Section.h"

#define LOCTEXT_NAMESPACE "TidyMilestones"

const FString UTidyMilestonesConfig::TabKey = TEXT("ModTierTab");
const FString UTidyMilestonesConfig::LabelKey = TEXT("TierLabel");
const FString UTidyMilestonesConfig::DefaultLabel = TEXT("M");
const FString UTidyMilestonesConfig::LeftKey = TEXT("TierOnLeft");
const FString UTidyMilestonesConfig::ShowAllKey = TEXT("ShowAllAtOnce");
const FString UTidyMilestonesConfig::OverridesKey = TEXT("NamesAndIcons");
const FString UTidyMilestonesConfig::EditorKeyKey = TEXT("EditorKey");

FConfigId UTidyMilestonesConfig::GetId()
{
	FConfigId Id;
	Id.ModReference = TEXT("TidyMilestones");
	Id.ConfigCategory = TEXT("");
	return Id;
}

UTidyMilestonesConfig::UTidyMilestonesConfig()
{
	ConfigId = GetId();
	DisplayName = LOCTEXT("ConfigName", "Tidy Milestones & OCD Tweaks");
	Description = LOCTEXT("ConfigDescription",
		"Puts the milestones added by mods in their own HUB tier, and lets you change names and icons. Changes apply right away. Every player has their own settings.");

	RootSection = CreateDefaultSubobject<UConfigPropertySection>(TEXT("RootSection"));

	UConfigPropertyBool* Tab = CreateDefaultSubobject<UConfigPropertyBool>(TEXT("ModTierTab"));
	Tab->DisplayName = LOCTEXT("TabName", "Mod tier in the HUB");
	Tab->Tooltip = LOCTEXT("TabTooltip", "Milestones added by mods are listed under a tier of their own instead of in the game's tiers. Reopen the HUB after changing this.");
	Tab->DefaultValue = true;
	Tab->Value = true;
	Tab->bRequiresWorldReload = false;
	RootSection->SectionProperties.Add(TabKey, Tab);

	UConfigPropertyString* Label = CreateDefaultSubobject<UConfigPropertyString>(TEXT("TierLabel"));
	Label->DisplayName = LOCTEXT("LabelName", "Name of the mod tier");
	Label->Tooltip = LOCTEXT("LabelTooltip", "What the tier button shows where the other tiers show their number. Short works best: the button is small. Reopen the HUB after changing this.");
	Label->DefaultValue = DefaultLabel;
	Label->Value = DefaultLabel;
	Label->bRequiresWorldReload = false;
	RootSection->SectionProperties.Add(LabelKey, Label);

	UConfigPropertyBool* Left = CreateDefaultSubobject<UConfigPropertyBool>(TEXT("TierOnLeft"));
	Left->DisplayName = LOCTEXT("LeftName", "Mod tier on the left");
	Left->Tooltip = LOCTEXT("LeftTooltip", "On: the mod tier comes before Tier 0. Off: it comes after the last tier. Reopen the HUB after changing this.");
	Left->DefaultValue = true;
	Left->Value = true;
	Left->bRequiresWorldReload = false;
	RootSection->SectionProperties.Add(LeftKey, Left);

	UConfigPropertyBool* ShowAll = CreateDefaultSubobject<UConfigPropertyBool>(TEXT("ShowAllAtOnce"));
	ShowAll->DisplayName = LOCTEXT("ShowAllName", "Show all mod milestones at once");
	ShowAll->Tooltip = LOCTEXT("ShowAllTooltip",
		"Off: a mod milestone appears in Tier M once the tier it was made for is unlocked.\n"
		"On: all of them are listed from the start. Reopen the HUB after changing this.");
	ShowAll->DefaultValue = false;
	ShowAll->Value = false;
	ShowAll->bRequiresWorldReload = false;
	RootSection->SectionProperties.Add(ShowAllKey, ShowAll);

	UConfigPropertyBool* Overrides = CreateDefaultSubobject<UConfigPropertyBool>(TEXT("NamesAndIcons"));
	Overrides->DisplayName = LOCTEXT("OverridesName", "Use my names and icons");
	Overrides->Tooltip = LOCTEXT("OverridesTooltip", "Off: everything shows the name and icon it came with. Your changes are kept.");
	Overrides->DefaultValue = true;
	Overrides->Value = true;
	Overrides->bRequiresWorldReload = false;
	RootSection->SectionProperties.Add(OverridesKey, Overrides);

	UConfigPropertyInteger* EditorKey = CreateDefaultSubobject<UConfigPropertyInteger>(TEXT("EditorKey"));
	EditorKey->DisplayName = LOCTEXT("EditorKeyName", "Names and icons window");
	EditorKey->Tooltip = LOCTEXT("EditorKeyTooltip", "The keys that open and close the window for changing names and icons, in a loaded save.");
	EditorKey->DefaultValue = static_cast<int32>(ETidyMilestonesEditorKey::CtrlShiftM);
	EditorKey->Value = EditorKey->DefaultValue;
	EditorKey->bRequiresWorldReload = false;
	RootSection->SectionProperties.Add(EditorKeyKey, EditorKey);
}

namespace
{
	/** Copy of Native as SML's Blueprint version of its class, or Native itself if there is none. */
	UConfigProperty* ToMenuProperty(UConfigProperty* Native, UObject* Outer, const FString& Key)
	{
		const UClass* NativeClass = Native->GetClass();
		const TCHAR* BlueprintName =
			NativeClass == UConfigPropertySection::StaticClass() ? TEXT("BP_ConfigPropertySection") :
			NativeClass == UConfigPropertyInteger::StaticClass() ? TEXT("BP_ConfigPropertyInteger") :
			NativeClass == UConfigPropertyString::StaticClass() ? TEXT("BP_ConfigPropertyString") :
			NativeClass == UConfigPropertyBool::StaticClass() ? TEXT("BP_ConfigPropertyBool") : nullptr;
		UClass* BlueprintClass = BlueprintName ? LoadClass<UConfigProperty>(nullptr,
			*FString::Printf(TEXT("/SML/Interface/UI/Menu/Mods/ConfigProperties/%s.%s_C"), BlueprintName, BlueprintName)) : nullptr;
		if (!BlueprintClass)
		{
			UE_LOG(LogTemp, Error, TEXT("TidyMilestones: no menu widget for %s, the config menu will be incomplete"), *NativeClass->GetName());
			return Native;
		}

		// Unique name: the native property may live in the same outer under its name (e.g. RootSection),
		// and UE refuses to replace an object with one of a different class. The name isn't used anyway;
		// sections look properties up by their map key.
		UConfigProperty* Menu = NewObject<UConfigProperty>(Outer, BlueprintClass,
			MakeUniqueObjectName(Outer, BlueprintClass, FName(*(Native->GetName() + TEXT("_Menu")))));
		Menu->DisplayName = Native->DisplayName;
		Menu->Tooltip = Native->Tooltip;
		Menu->bRequiresWorldReload = Native->bRequiresWorldReload;
		Menu->bHidden = Native->bHidden;
		Menu->bAllowUserReset = Native->bAllowUserReset;

		if (const UConfigPropertyInteger* Int = Cast<UConfigPropertyInteger>(Native))
		{
			UConfigPropertyInteger* MenuInt = CastChecked<UConfigPropertyInteger>(Menu);
			MenuInt->DefaultValue = Int->DefaultValue;
			MenuInt->Value = Int->Value;
			if (UCP_Integer* Widget = Cast<UCP_Integer>(MenuInt))
			{
				// The only integer is the key choice: a dropdown. SML's default is a text box.
				Widget->WidgetType = ECP_IntegerWidgetType::CPI_Enum;
				Widget->EnumClass = StaticEnum<ETidyMilestonesEditorKey>();
				Widget->MinValue = 0;
				Widget->MaxValue = static_cast<int32>(ETidyMilestonesEditorKey::Off);
			}
		}
		else if (const UConfigPropertyBool* Bool = Cast<UConfigPropertyBool>(Native))
		{
			UConfigPropertyBool* MenuBool = CastChecked<UConfigPropertyBool>(Menu);
			MenuBool->DefaultValue = Bool->DefaultValue;
			MenuBool->Value = Bool->Value;
		}
		else if (const UConfigPropertyString* String = Cast<UConfigPropertyString>(Native))
		{
			UConfigPropertyString* MenuString = CastChecked<UConfigPropertyString>(Menu);
			MenuString->DefaultValue = String->DefaultValue;
			MenuString->Value = String->Value;
		}
		else if (const UConfigPropertySection* Section = Cast<UConfigPropertySection>(Native))
		{
			UConfigPropertySection* MenuSection = CastChecked<UConfigPropertySection>(Menu);
			if (UCP_Section* Widget = Cast<UCP_Section>(MenuSection))
			{
				// SML's default is one horizontal row with a scroll bar.
				Widget->WidgetType = ECP_SectionWidgetType::CPS_Vertical;
			}
			for (const TPair<FString, TObjectPtr<UConfigProperty>>& Child : Section->SectionProperties)
			{
				MenuSection->SectionProperties.Add(Child.Key, Child.Value ? ToMenuProperty(Child.Value, MenuSection, Child.Key) : nullptr);
			}
		}
		return Menu;
	}

	/** Shipping builds have no UMETA display names, so without this the dropdown shows "CtrlShiftM". */
	FText GetEditorKeyDisplayName(int32 Index)
	{
		switch (static_cast<ETidyMilestonesEditorKey>(Index))
		{
		case ETidyMilestonesEditorKey::CtrlShiftM: return LOCTEXT("KeyM", "Ctrl + Shift + M");
		case ETidyMilestonesEditorKey::CtrlShiftN: return LOCTEXT("KeyN", "Ctrl + Shift + N");
		case ETidyMilestonesEditorKey::CtrlShiftK: return LOCTEXT("KeyK", "Ctrl + Shift + K");
		case ETidyMilestonesEditorKey::Off: return LOCTEXT("KeyOff", "No key");
		}
		return FText::GetEmpty();
	}
}

void UTidyMilestonesConfig::UseMenuWidgets()
{
	StaticEnum<ETidyMilestonesEditorKey>()->SetEnumDisplayNameFn(&GetEditorKeyDisplayName);

	UTidyMilestonesConfig* Default = GetMutableDefault<UTidyMilestonesConfig>();
	if (Default->RootSection)
	{
		Default->RootSection = CastChecked<UConfigPropertySection>(ToMenuProperty(Default->RootSection, Default, FString()));
	}
}

#undef LOCTEXT_NAMESPACE
