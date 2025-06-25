![Inferno](/docs/banner.jpg)
Inferno is a single player remaster of the 1995 classic Descent 1. It is built on a completely new engine using modern technology and includes a level editor. It is not a source port.

![hud](/docs/hud.jpg)
![automap](/docs/automap.png)
![escape](/docs/escape02.jpg)

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
Extract the archive and run inferno.exe. The shareware mission containing the first 7 levels is included.
To play the full game, copy the retail `descent.hog` and `descent.pig` to the `/d1` folder.

### Music
MIDI music is currently not supported and instead a song pack can be used. 
Several song packs are available, generously hosted by [PuMo](https://pumosoftware.com/). Copy a single music DXA to the `/d1` folder. SC55 is the 'default' pick.

[SC55 MIDI](https://pumosoftware.com/files/descent/dxa/d1xr-sc55-music.dxa)
[OPL3 MIDI](https://pumosoftware.com/files/descent/dxa/d1xr-opl3-music.dxa)
[Roland SC MIDI](https://pumosoftware.com/files/descent/dxa/d1midi-rolandsc.dxa)
[AWE64 MIDI](https://pumosoftware.com/files/descent/dxa/d1midi-awe64.dxa)
[Playstation 1 CD](https://pumosoftware.com/files/descent/dxa/d1-playstation.dxa)
[Mac CD](https://pumosoftware.com/files/descent/dxa/d1cda-mac.dxa)

### Inferno Enhanced Missions
These missions have custom lighting and music to enhance the Inferno experience. 
Simply place the `zip` file next to the mission `hog` file in the `d1` folder and it will be loaded automatically.

Enhanced missions require retail game data.

- [Vignettes](https://sectorgame.com/dxma/mission/?m=560) Addon data is bundled with release. Credit to Fiendzy
- [Cererian Expedition](https://sectorgame.com/dxma/mission/?m=1561) [Addon](https://pumosoftware.com/files/descent/inferno_addons/cererian.zip) Credit to PuMo
- [Orion Nebula Project](https://sectorgame.com/dxma/mission/?m=220) [Addon](https://pumosoftware.com/files/descent/inferno_addons/orion.zip) Credit to PuMo

## System Requirements
- Windows 10 64-bit
- DirectX 12 capable GPU (Feature level 12 required)
- A retail copy of Descent 1 is needed to play the full campaign and use the level editor

## Major gameplay changes
- PTMC stopped skimping on basic equipment! The headlight and afterburner are now baseline on all PyroGX models.
- Robot stunning has been reworked. In the original game tough robots could be stunlocked by shooting them constantly, trivializing their presence.
	- Primary weapons no longer stun enemies, except for the fusion cannon
	- Missiles deal more damage to robots and inflict stun. Concussion missiles are very effective at this.
	- Weak enemies are more likely to be stunned, while tougher ones are nearly immune
- Proximity mines have been reworked and their damage against robots is significantly increased. 
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