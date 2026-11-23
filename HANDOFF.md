# Mod Tier — handoff

Working name and ID (`ModTier`) are placeholders. Pick the real name and icon before publishing.

## What it does

- Puts every mod milestone in its own HUB tier ("Tier M", left of Tier 0 by default, always visible).
- In-game editor (Ctrl+Shift+M) to rename and re-icon mod milestones, MAM research, items, buildings and vehicles.
- Mod Options page: tab on/off, tier name, left/right, show all at once, names and icons on/off, editor hotkey.

## How it works

- The HUB widget is `BPW_Hub`. The Blueprint entry points of `GetTechTier`, `GetHighestAvailableTechTier`, `Conv_IntToText` and `AddChildToHorizontalBox` are replaced (`UFunction::SetNativeFunc`) and answer differently only when the caller is `BPW_Hub_C` / `BPW_Hub_TierButton_C`. Schematics keep their real tier, so saves are untouched.
- Names and icons are written into class default objects and reverted from saved originals. Buildings and vehicles take their name from the built class, not the descriptor.
- Overrides live in `<game>/FactoryGame/Configs/ModTier/Overrides.json` (global, not per save). `Docs/Overrides.example.json` is the current set.

## Build

SML 3.12.1, UE 5.6.1-CSS. Copy this folder to `<starter project>/Mods/ModTier`, then:

- Editor build (only after adding or removing a UCLASS): `Build.bat FactoryEditor Win64 Development -project=<starter>/FactoryGame.uproject`
- Package: `RunUAT.bat -ScriptsForProject=<uproject> PackagePlugin -project=<uproject> -clientconfig=Shipping -serverconfig=Shipping "-DLCName=ModTier" -build -platform=Win64 -nocompileeditor -installed`
- Install: unzip `Saved/ArchivedPlugins/ModTier/ModTier-Windows.zip` into `<game>/FactoryGame/Mods/ModTier` with the game closed.

## Known traps

- An `AChatCommandInstance` subclass in this module crashed the game at startup (async loading thread). Cause not found; the editor uses a hotkey instead.
- After removing the last UCLASS, delete `Intermediate/Build/Win64/*/Inc/ModTier/UHT` by hand.
- A config built in C++ needs SML's `BP_ConfigProperty*` classes swapped in, and `FlushPendingSaves` on every dirty mark.

## Not yet checked in-game

- Building and vehicle renames.
- "Name of the mod tier" and "Mod tier on the left" settings.
- "Show all at once" with a milestone whose real tier is locked.
- Multiplayer (only the host can open the editor).

## To do

- Real name and icon.
- README, then publish prep: mod manager install test, multiplayer check, mod page.
- Sorting inside the mod tier, gamepad order of the tier bar, editor styling.
