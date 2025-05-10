![Inferno](/docs/banner.png)
Inferno is a single player remaster of the 1995 classic Descent 1. It is built on a completely new engine using modern technology and includes a level editor. It is not a source port.

![hud](/docs/hud.jpg)
![automap](/docs/automap.png)
![escape](/docs/escape.jpg)

## Features
- Per pixel dynamic lighting for all light sources
	- Comes with full color lighting for First Strike and Vignettes. Lighting contributions for user missions are welcome.
- New PyroGX model based on the cinematics. Credit to SaladBadger.
- Descent 3 inspired HUD that works properly with widescreen and ultra-widescreen resolutions
- Polygon accurate weapon collision against robots
- Added materials for every texture in the game. Defining roughness, metalness, emissiveness, and other properties.
- Completely reworked AI to give each robot unique behaviors and quirks
	- Robots will use covering fire, call for backup, sneak up from behind, or run for help!
- Rebalances and modernizes several aspects of the game that were exploitable or tedious
- Redesigned Automap and adds shortcuts to find points of interest like the exit or energy centers
- Autosaves between levels with full mission info. _Currently in-level saving is not supported._
- Enhanced point filtering mode that anti-aliases pixel edges without blurring them
- Photo Mode

## Installation
Extract the archive and run inferno.exe. The shareware mission containing the first 7 level is included.
To play the full game, copy the retail `descent.hog` and `descent.pig` to the `/d1` folder.

MIDI music is currently not supported and instead a song pack can be used. 
Several song packs are available `TODO: here`, generously hosted by Pumo. Copy a music DXA to the `/d1` folder. 

## System Requirements
- Windows 10 64-bit
- DirectX 12 capable GPU (Feature level 12 required)
- A retail copy of Descent 1 is needed to play the full campaign and use the level editor

## Major gameplay changes
- PTMC stopped skimping on basic equipment! The headlight and afterburner are now baseline on all PyroGX models.
- 'Stunning' has been reworked. In the original game quickly firing weapons could stunlock tough robots, trivializing their presence.
	- Primary weapons no longer stun enemies, except for the fusion cannon
	- Missiles deal more damage to robots and inflict stun. Concussion missiles are very effective at this.
- Proximity mines have been reworked and their damage against is robots significantly increased. 
	- Mines now attract homing weapons when placed which allows them to function as chaff
	- Mines will lock onto nearby targets and propel themselves like a seeker mine
	- Both of these behaviors affect enemy mines, be careful not to waste homing missiles around gophers!

## Balance changes
- Player vulcan damage significantly increased, spreadfire damage increased, concussion missile speed increased
- Plasma now fires in a burst pattern similar to Descent 3's Black Pyro
- Fusion now slows time while charging (can be disabled in options)
- Robot fusion and vulcan damage now scales with difficulty
- Robot concussion and homing missile damage reduced to Descent 2 values

## Level Editor 
Inferno contains a fully featured level editor, but requires retail game data to save changes.
It is accessible from the main menu.

- Interactive extrusion, translation, rotation, and scaling
- Entirely new radiosity light model with support for per-side settings
- Real-time renderer that animates powerups, scrolling textures and flickering lights
- Quickly mark and edit geometry using the mouse and various selection tools
- Copy marked segments and their contents along with support for mirroring
- Edit multiple objects at once. Supports copying and transforming across segments.
- Shows connections between triggers and their targets

## Building
Requires Visual Studio 2022 with VCPKG integration

Open `Inferno.sln` file and build. If set up correctly dependencies will be fetched automatically using the VCPKG manifest.

## Linux
Should run in Wine after installing `vkd3d-proton`, `d3dcompiler_47` (with winetricks) and copying `segoeui.ttf` to `c:\windows\fonts`