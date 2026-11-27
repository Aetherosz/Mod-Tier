# Tidy Milestones & OCD Tweaks — handoff

Name: Tidy Milestones & OCD Tweaks. Mod reference and module: `TidyMilestones` (was `ModTier`; renamed before first publish). "Mod tier" in the code and settings still means the HUB tier this mod adds. Icon: Media/logo.png (Sir's handcrafted logo).

## What it does

- Puts every mod milestone in its own HUB tier ("Tier M", left of Tier 0 by default, always visible).
- In-game editor (Ctrl+Shift+M) to rename and re-icon mod milestones, MAM research, items, buildings and vehicles.
- Mod Options page: tab on/off, tier name, left/right, show all at once, names and icons on/off, "Use Aetherosz's names" (off by default), editor hotkey.
- Aetherosz's names: a preset of 40 milestone renames (checked as Milestone in a runtime Targets.txt dump, 2026-11-27), compiled in from `Docs/AetheroszNames.json` (run `Tools/make_preset.py` after editing it). Applied under the player's own overrides; only targets that are milestones at runtime are touched; missing mods are skipped silently.

## How it works

- The HUB widget is `BPW_Hub`. The Blueprint entry points of `GetTechTier`, `GetHighestAvailableTechTier`, `Conv_IntToText` and `AddChildToHorizontalBox` are replaced (`UFunction::SetNativeFunc`) and answer differently only when the caller is `BPW_Hub_C` / `BPW_Hub_TierButton_C`. Schematics keep their real tier, so saves are untouched.
- Names and icons are written into class default objects and reverted from saved originals. Buildings and vehicles take their name from the built class, not the descriptor.
- Overrides live in `<game>/FactoryGame/Configs/TidyMilestones/Overrides.json` (global, not per save). Sir's personal set (79 entries, incl. MAM research and icons) is installed as Overrides.json on his PCs, not shipped.

## Build

SML 3.12.1, UE 5.6.1-CSS. Copy this folder to `<starter project>/Mods/TidyMilestones`, then:

- Editor build (only after adding or removing a UCLASS): `Build.bat FactoryEditor Win64 Development -project=<starter>/FactoryGame.uproject`
- Package: `RunUAT.bat -ScriptsForProject=<uproject> PackagePlugin -project=<uproject> -clientconfig=Shipping -serverconfig=Shipping "-DLCName=TidyMilestones" -build -platform=Win64 -nocompileeditor -installed`
- Install: unzip `Saved/ArchivedPlugins/TidyMilestones/TidyMilestones-Windows.zip` into `<game>/FactoryGame/Mods/TidyMilestones` with the game closed.

## Known traps

- An `AChatCommandInstance` subclass in this module crashed the game at startup (async loading thread). Cause not found; the editor uses a hotkey instead.
- After removing the last UCLASS, delete `Intermediate/Build/Win64/*/Inc/TidyMilestones/UHT` by hand.
- A config built in C++ needs SML's `BP_ConfigProperty*` classes swapped in, and `FlushPendingSaves` on every dirty mark.

## Not yet checked in-game

- "Use Aetherosz's names" on/off, and own names winning over it.

- Building and vehicle renames.
- "Name of the mod tier" and "Mod tier on the left" settings.
- "Show all at once" with a milestone whose real tier is locked.
- Multiplayer (only the host can open the editor).

## To do

- Icon: done (Media/logo.png).
- README, then publish prep: mod manager install test, multiplayer check, mod page.
- Sorting inside the mod tier, gamepad order of the tier bar, editor styling.
