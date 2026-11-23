#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"

class AFGSchematicManager;
class UFGSchematic;
struct FFrame;

/**
 * Shows the milestones added by mods in a HUB tier of their own instead of spread over the
 * game's tiers. Only what the HUB shows is changed: the schematics themselves keep their tier,
 * so progression, saves and multiplayer behave as without the mod.
 *
 * How: the HUB (BPW_Hub) is a Blueprint widget that
 *  - makes one tier button for 0..GetHighestAvailableTechTier(),
 *  - lists for a tier the available milestones whose GetTechTier() equals it,
 *  - writes the tier number on the button with Conv_IntToText().
 * Those three functions get a replacement Blueprint entry point that answers differently when,
 * and only when, the HUB's widgets are the caller. Calls from C++ and other Blueprints are untouched.
 */
class FModTier
{
public:
	static void RegisterHooks();

private:
	/** A milestone that comes from a mod, whatever tier it is in. */
	static bool IsMovedSchematic(TSubclassOf<UFGSchematic> Schematic);

	/** The tier number of the mod tab: one above the game's highest tier. */
	static int32 GetModTier(const AFGSchematicManager* Manager);

	/** True if the milestone is shown in the mod tab right now (its own tier is reached, or gating is off). */
	static bool IsShownInModTab(const AFGSchematicManager* Manager, TSubclassOf<UFGSchematic> Schematic);

	/** The milestones shown in the mod tab right now. */
	static void GetModTabSchematics(const AFGSchematicManager* Manager, TArray<TSubclassOf<UFGSchematic>>& Out);

	/** True if the Blueprint code running in this frame belongs to the named widget class. */
	static bool IsCalledBy(const FFrame& Stack, const FName& ClassName);

	// Replacement Blueprint entry points.
	static void ExecGetTechTier(UObject* Context, FFrame& Stack, void* const Result);
	static void ExecGetHighestAvailableTechTier(UObject* Context, FFrame& Stack, void* const Result);
	static void ExecConvIntToText(UObject* Context, FFrame& Stack, void* const Result);
	static void ExecAddChildToHorizontalBox(UObject* Context, FFrame& Stack, void* const Result);

	/** Draws the last tier button (the mod tier) in front of the others. */
	static void MoveModTierButtonToFront(class UHorizontalBox* TierList);

	/** Registers the Mod Configuration page, once the game instance exists. */
	static void RegisterConfig(class UGameInstance* GameInstance);
	/** Reads the settings of the Mod Configuration page into the fields below. */
	static void RefreshSettings();

	static TWeakObjectPtr<class UConfigManager> ConfigManager;
	/** Mod milestones are shown in their own tier. */
	static bool bTabEnabled;
	/** False: a moved milestone stays hidden until the tier it came from is reached. */
	static bool bUnlockAllAtOnce;
};
