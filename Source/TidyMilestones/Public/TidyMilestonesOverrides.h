#pragma once

#include "CoreMinimal.h"

class UTexture2D;

/** The kinds of thing whose name and icon can be replaced. */
enum class ETidyMilestonesTargetKind : uint8
{
	Milestone,
	Research, // MAM research and MAM trees
	Item,
	Other,    // other unlocks: alternate recipes, AWESOME Shop, ...
};

/** One thing whose name and icon can be replaced. */
struct FTidyMilestonesTarget
{
	TWeakObjectPtr<UClass> Class;
	FString Path;
	/** The mod it comes from (the root of its path); empty for the base game. */
	FString Mod;
	ETidyMilestonesTargetKind Kind = ETidyMilestonesTargetKind::Other;
};

/**
 * Replaces the names and icons of milestones, MAM research (trees and their nodes) and items.
 * The replacements are written into the default objects of the classes when a save is loaded,
 * which is where the menus of the game read them from. Nothing is stored in the save game, and
 * in multiplayer every player sees their own replacements.
 *
 * Files, in <game>/FactoryGame/Configs/TidyMilestones/:
 *   Overrides.json  the replacements
 *   Icons/          PNG files of the player
 *   Targets.txt     written by the mod: everything from mods that can be replaced
 */
class FTidyMilestonesOverrides
{
public:
	/** Reads Overrides.json and applies it. Safe to call again: earlier replacements are undone first. */
	static void Apply();

	/** Off: everything shows the name and icon it came with. The replacements are kept. */
	static void SetEnabled(bool bInEnabled);

	/** Everything loaded whose name and icon can be replaced. */
	static void GatherTargets(TArray<TSharedPtr<FTidyMilestonesTarget>>& Out);

	/** The name and icon a target shows now. */
	static FText GetDisplayName(const UClass* Class);
	static UTexture2D* GetIcon(const UObject* DefaultObject);

	/** The replacement name and icon of a target; empty when it has none. */
	static FString GetNameOverride(const FString& Target);
	static FString GetIconOverride(const FString& Target);

	/** Sets (or with an empty value: removes) a replacement, saves the file and applies it. */
	static void SetNameOverride(const FString& Target, const FString& Name);
	static void SetIconOverride(const FString& Target, const FString& IconSpec);

	/** "file:name.png" (in Icons/), "copy:<class path>" (the icon of another target) or the asset path of a texture. */
	static UTexture2D* ResolveIcon(const FString& Spec);

	/** The PNG files in the Icons folder. */
	static void GetIconFiles(TArray<FString>& Out);
	static FString GetDirectory();

private:
	struct FOverride
	{
		FString Target;
		FString Name;
		FString Icon;
	};

	static void Load();
	static void Save();
	static void WriteTargets();
	static FOverride& FindOrAddOverride(const FString& Target);
	static void OnOverridesChanged();

	/** Writes the loaded replacements into the default objects. */
	static void ApplyLoaded();
	/** Puts back every value replaced so far. */
	static void Revert();

	static UClass* FindTargetClass(const FString& Path);
	static void SetName(UObject* DefaultObject, const FString& Name);
	static void SetIcon(UObject* DefaultObject, UTexture2D* Icon);

	static TArray<FOverride> Overrides;
	static bool bEnabled;
	/** True once a save has been loaded: before that nothing is applied. */
	static bool bHasApplied;
};
