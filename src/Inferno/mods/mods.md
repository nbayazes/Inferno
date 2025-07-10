# Game mods
Mods provide a way to override data in the base game for all missions.

Mods can be read directly from a zip file or from a subfolder

Mods are loaded after the base game data (descent.hog, assets in the d1 folder) but 
before the mission or level specific data.

If a mod provides a material.yml, game.yml, or lights.yml it will be merged with the existing table.

## manifest.yml

A mod must contain a manifest.yml file. It specifies metadata about the mod and which games it supports.

```yml
name: Mod Name
version: 1.0
supports: [ descent1, descent2 ]
author: Author
description: The description
```

## mods.yml
Contains a sequence that specifies the mods to load and their order.
The game will search for both a zip file and a folder with that name. The folder contents take priority.

```yml
mods: [ mod1, mod2, mod3 ]
```

## Asset load order
```
d1\descent.hog         base hog
d1\*.dxa               D1X addon data (high res backgrounds, fonts)
assets\                common assets
d1\                    game specific assets

mods\mod.zip           packaged mod
mods\mod\              unpackaged mod. Skips mod.zip if present.

d1\missions\mission.hog          the mission hog
d1\missions\mission.zip          packaged mission addon data (can contain level folders)
d1\missions\mission\             unpacked mission addon data
d1\missions\mission\level01\     level specific data (level01 matches level01.rdl)
```
