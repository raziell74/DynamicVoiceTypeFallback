**Dynamic Voice Type Fallback** is an SKSE plugin for Skyrim Special Edition. When an NPC's VoiceType changes, the plugin will look up missing voice files in the original VoiceType folder so those lines are not silent.

This repository is currently a CommonLibSSE-NG plugin skeleton. The VoiceType fallback is not implemented yet.

## Planned

- Detect VoiceType changes on NPCs.
- If a voice file is missing from the new VoiceType, fall back to the original VoiceType folder.

## Requirements

- [SKSE](https://skse.silverlock.org/)
- [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444)

## Installation

Install using your favorite mod manager or manually extract the contents of the archive to your Skyrim Special Edition Data folder. Please also make sure you also have all of the required mods installed.

Load order doesn't matter. There is no plugin file.

## Uninstallation

Uninstall using your favorite mod manager or manually delete the files from your Skyrim Special Edition Data folder.

## Compatibility

The SKSE plugin is built using [CommonLibSSE NG](https://github.com/alandtse/CommonLibSSE-NG). It targets Skyrim SE and AE. VR is not supported.

## Credits

SKSE Source Code: Dynamic Voice Type Fallback - Licensed under GPL-3.0-or-later, same as [CommonLibSSE-NG](https://github.com/alandtse/CommonLibSSE-NG).
