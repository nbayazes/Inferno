# Game mods
Mods provide a way to override data in the base game for all missions.
Currently mods are applied in arbitrary order, but an ordering system will be added.

Mods can be read directly from a zip file or from a subfolder

Mods can be placed in a d1 or d2 subfolder to only apply to a specific game.
If placed in the top level of the mods folder it will apply to both games.

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
