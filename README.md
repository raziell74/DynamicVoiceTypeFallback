**Dynamic Voice Type Fallback** is an SKSE plugin for Skyrim Special Edition. When an NPC's VoiceType changes — by ESP override, SkyPatcher, or Papyrus — and a spoken line has no file in the new VoiceType folder, the plugin plays the line from the NPC's original (defining-plugin) VoiceType folder instead of going silent.

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

Works with ESP VoiceType overrides and ESP-less patchers such as SkyPatcher. Compatible with Fuz Ro D-oh: if the original VoiceType file is also missing, that plugin can still supply silence.

## Credits

SKSE Source Code: Dynamic Voice Type Fallback - Licensed under GPL-3.0-or-later, same as [CommonLibSSE-NG](https://github.com/alandtse/CommonLibSSE-NG).
