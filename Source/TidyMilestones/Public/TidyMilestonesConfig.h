#pragma once

#include "CoreMinimal.h"
#include "Configuration/ModConfiguration.h"
#include "TidyMilestonesConfig.generated.h"

/** The keys that open the names and icons window. The stored value is the index. */
UENUM()
enum class ETidyMilestonesEditorKey : uint8
{
	CtrlShiftM,
	CtrlShiftN,
	CtrlShiftK,
	Off,
};

/**
 * Shows up in the game's Mods -> Mod Configuration menu (main menu and pause menu).
 * SML saves it to FactoryGame/Configs/TidyMilestones.cfg.
 */
UCLASS()
class UTidyMilestonesConfig : public UModConfiguration
{
	GENERATED_BODY()

public:
	UTidyMilestonesConfig();

	/** Mod milestones get their own tier in the HUB. */
	static const FString TabKey;
	/** What the mod tier's button shows in place of a tier number. */
	static const FString LabelKey;
	static const FString DefaultLabel;
	/** The mod tier comes before Tier 0 (on) or after the last tier (off). */
	static const FString LeftKey;
	/** Show every mod milestone in that tier at once, instead of when its own tier is reached. */
	static const FString ShowAllKey;
	/** Replace names and icons with the ones chosen by the player. */
	static const FString OverridesKey;
	/** ETidyMilestonesEditorKey as an integer. */
	static const FString EditorKeyKey;

	static FConfigId GetId();

	/**
	 * SML's C++ property classes have no menu widget (only its Blueprint subclasses do), so a
	 * config built in C++ shows up empty. Swaps the default object's properties for SML's
	 * BP_ConfigProperty* classes. Call before registering, once SML's content is loaded.
	 */
	static void UseMenuWidgets();
};
