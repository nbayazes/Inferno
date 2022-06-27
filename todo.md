# TODO
## Texture Resources
Improve texture resource system. Able to create SRV, UAV, RTV at will
Multiple views can be created of the same resource.
To optimize / simplify material loading in shaders, bind one table per material.
Material: Diffuse, Specular, Supertransparency, Emissive...
- when creating material, add views for each sequentially
- set starting index of material in table (diffuse) and others will bind
- create views of black / empty resource to fill empty slots

Resources:
- texture upload heap -> copies resources to other heaps
- level textures
- robots
- UI textures - always loaded
- frame buffers
- white / black / empty

intermediate resource manager to store all level resources

- level resources are indexed by TID
    - sub-maps also get uploaded
    - are any maps shared except for defaults?
- when materials are loaded, sequential views are allocated for that material

It is suggested to only have a single descriptor heap of each type and set it once per frame
- I believe NVIDIA says the overhead of switching is minimal on modern hardware
- sub-ranges of descriptor heaps?
- one view heap, one sampler heap

Resource
- CreateComittedResource: allocates memory for the resource to the default (GPU) or upload (CPU) heap
- CreatePlacedResource: light weight. needs active/inactive tracking.
- Releasing the handle frees the memory
- A resource is just a blob of data. Can be a texture or a raw buffer depending on description.

After allocating a resource, data needs to be copied into it

Can map / memcpy data one resource at a time but is slow.

Copying data to buffers use different functions
- Map / Unmap / CopyBufferRegion
- device->CreateConstantBufferView
- cbuffers can be used to share state for entire frame (such as camera transforms and time)

A DescriptorHeap allocates a range of descriptors on both the GPU / CPU
- Both handles are fetched by index relative to a starting location in memory
    - The index should be the same when getting GPU/CPU handles of a resource
- it is up to the app to properly place views into the heap
    - order matters when binding DescriptorTables in shaders
    - for example all 5 texture SRVs should be allocated in a row for a material
- UAV/SRV/CBV heaps can mix views
- Descriptors can be copied mid-frame

ResourceUploadBatch - Async uploads of data into a resource
- Begin / End -> std::future
- Upload(resource)
- Transition(resource)

Resources having handles is not a great abstraction
- Resources can have multiple handles of multiple types


load TID 1
- queue resource uploads
- allocate 5 sequential view handles in descriptor heap
    - CPU handles used for
        - device->CreateShaderResourceView (also is a helper)
        - device->CreateUnorderedAccessView
        - all Create*View functions create a CPU handle to an existing resource
    - GPU handles used for shaders
- init handles to corresponding resources (use defaults if nothing loaded)



MaterialHandles { Diffuse, Specular, Emissive, etc }



Handle Heaps:
Loading resources is a separate process from creating views
Views are pointers to resources, can have multiple views for the same resource (UAV, SRV, RTV)
- Views should be invalidated if resource is unloaded. Ref counting?

Only one heap of each type (UAV, SRV, RTV) can be active at a time



## Bloom / HDR
- Ramp exposure for flash missiles and reactor going critical

## SSAO


## HUD
Projected text image shader -> Adds glow and applies scanline filter. Additive
- distort on heavy hit / death

bloom dirt mask
    - projected text should slightly affect this as well

normal mapping
'frame' is alpha blended

3D planes instead of flat images? images already perspective corrected...


## Heat distortion
- investigate volumetric fog (uses depth buffer / drawing fog volumes)
- create intermediate render target (half screen res) for mask
    - transition from target to pixel shader source
    - transition scene buffer from target to shader source
    - set target to final buffer
    - draw full screen triangle using intermediate + scene textures, apply effect
- draw segments with lava textures in them
- apply distortion in postprocess


Create level meshes from contiguous geometry. Walls break continuity.
- Average normals on co-linear faces under an angle
- Mark all segments in room
    - Track sides for each segment until all are added
- `Select room` functionality -> select all segments until a door is encountered. option to ignore walls / transparent walls.
- Scan all rooms in level

seg 0
while side child != none && not wall (ignoring triggers) do 
    room.add(seg)

rooms can be very large...

for each seg in room
    for each side in seg
        side... adjacent side is same texture and within tolerance
        average normals ( < 5 deg ?) share verts

Update level geometry processing to support culling
Instead of drawing all walls of a specific material, split based on 'rooms'
Meshes need a bounding box for culling
Rooms with lava in them will also cause a split
Segment Side -> Mesh*


Extract render code to library

## Property Editors
Seg props
- triggered by wall/trigger (nav to)

Reactor props
- Triggers (Level::ControlCenterTriggers)
- Countdown
- Strength

use a secondary vertex buffer for color data in level shader and only update that one dynamically

Context menu
- Add segment (matcen, fuel cen, reactor)
- Add wall (energy, illusion, force field, fan, grate, waterfall, lava fall)
- Add door (standard, exit, secret exit, prison, guide bot)
- Add trigger (open door, robot maker, control panel)
- Select (other side, source trigger)
- Delete (segment, wall, trigger, both walls)
- Properties

Matcen props
- Robot picker / list of used / unused robots

Wall props
- wclip picker (doors / blastable)
    - generic texture browser that accepts a filter as input


Editor debug drawing
- orientation markers on objects (long range)
- segment outlines (based on automap colors). (reactor, energy, matcen)

Materials store parameters for shaders
- Add blend mode to forcefield material...
- Reflectance for lights / metal

on-demand texture loading for objects
- switching to a robot not present in level won't load textures
- use similar logic to the texture browser

vertex welding -> scan vertices in model, average if within tolerance (green guy most obvious problem)

# Level Navigator
Special segments are highlighted (reactor, energy, matcen)
Walls with triggers are highlighted

- Tree of all segments
    - Walls
        - Triggers
    - Objects

- List of all walls, triggers, objects
    - triggers are technically a property of walls

Reactor
Level name

## Objects
rotation gizmo
ctrl+drag to duplicate
highlight effect on mouse-over

insert object workflow
- where on ui? toolbar?
    - add, delete, move to segment, reset (clear contains, texture, AI?)

    - default -> hxm
- object mode -> insert -> use center of selected segment
    - selected segment should fade when in object mode
    

object translation:
- moving an object through the side of a segment should update parent segment
- requires hit testing
- parent segment is missing from UI

to fix gizmo snapping oddities, track cursor screen position and only update gizmo if coords changed

HDR sample appears to handle window resizing in realtime

## file / mission browser
expand file list height to window height - use child window with selectable items (simple layout)
toggle for only missions / all files
save / delete

robot info (these require HXM changes to persist) use a different UI:
Physics: strength, mass, drag (overridden by advanced mass?), eblobs, light, glow (most of these aren't physics related)
cloaked, bright (lit by mine?)
Weapon: 
    Primary / Secondary
    5 entries of all below data: (difficulty levels)
    Primary delay
    secondary delay
    FOV, Turn, Speed, Fire rate (count?), Evade, circle dist, Aim

Sounds:
explode / alert / attack / claw / die

AI:
tactics (same as obj AI settings) / boss
flags: kamikaze / companion / thief / pursue ...
matcen AI: "wander" behavior on spawning in to reduce clumping. 
"stay away" from nearby robots by default

Death:
Score / death roll / explosion size / type
Contains obj / chance


Robot animations
- POF has list of angles
- RobotInfo has animation states (jointlist, count and offset) (index into jointpos)
    - the root submodel is never animated - only 9 slots
- jointpos was a global (1600 entires). stores submodel index + angle. can be 1 or more joints
    - this is in the hamfile as RobotJoints
- 5 animation states: AS_REST, AS_ALERT, AS_FIRE, AS_RECOIL, AS_FLINCH
- delta / goal angles are stored globally and updated each frame until goal is reached
    - current values are stored in ObjectInfo.AnimationAngles (pobj_info.anim_angles) polyobj_info (big union type)
- animation rate / scale is a fixed value
    ANIM_RATE = (F1_0/16)
    DELTA_ANG_SCALE = 16
- melee robots (attack type 1) use flinch scale 24 instead of 4 when already flinched

## Specularity
- using frustum culling, face normals, walls, pick nearest ~5 light sources
- room partitioning?
- potential problems: 'popping' when transitioning from one source to the next. could fade intensity over quarter second. Fade furthest src to newest src?
- worker thread

Center on segment (home)

## Dynamic lights
Emit light from energy and shield pickups (level shader)
Add 4-5 light slots to level mesh shader
Add to ambient value for object shader. Ambient is from segment brightness.
Explosions

## Segment tesselation
Lighting looks very rough due to low number of polygons
Per pixel point lights are not feasible due to the large, flat faces used as light sources (lava)
Lightmaps are too slow to compute, are static, and consume large amounts of memory
Tesselation would dramatically improve vertex lighting quality and allows adding noise

1. Calculate face normals of each segment. Adjacent faces should have their normals averaged if within tolerance and no walls are in between.
2. Merge faces of adjacent segments / faces into single mesh if they share the same texture. Special case: secret doors should merge with their base texture. All walls break joining between segments.
3. Apply software tesselation to meshes, using original vertices as control points. interpolate UVs
4. If texture is rock and there is no overlay texture, apply noise to all vertices except outer ring. Recalculate normals. Larger faces can have more noise. Too much noise can cause disconnect between collision meshes and render meshes.
5. Recalculate lighting using tesselated meshes. Continue using vertex lighting.

Lighting calculation:
1. Faces with light sources are fully lit
2. Cast vertices of face to nearby segments within range (stop once falloff reaches 0). Large faces will require additional sources...
3. Use average color of light source texture
4. Accumulate light to each vertex in nearby segments. Reject normals facing away.
5. Calculate destroyable / blinking light deltas

## dragging
dragging geometry causes flicker at snap boundaries (solves slightly different solution each frame)
- fix by ?

## sprites
additive blend forcefields, e-cen walls, cloak, energy, shield, extra life, invuln, hoard orb
- depth sort with walls
- finish shield orb animation

## Rotation gizmo
- add bounding box for each plane
- 360 degree circle along each axis

## Scale gizmo
- copy transform gizmo render and hit test logic

## Translate gizmo
- Add 2D planes

## Gizmos:
- show transform value in UI, popup preferred but static text / status bar ok
- Mouse Wrapping to client viewport during drag


elapsed time should be an update parameter to make intent clear

Origins:
- Alternative: Uses the origin of the alternative segment using its last selection. i.e. block mode
- Normal: For rotation / translation, uses the current side
- Local: For rotation, uses the opposite side as center (match current editor behavior). For translation, act same as normal?

- multi-select (vertices) exists in original editor as block mode
    - it uses the current segment / selection as origin
    - have to switch between modes to change origin
    - axis being used is unclear

Selections:
- clicking once sets segment, this is treated as origin
    - after multi-select, hold x and click to move origin? 
- holding control or dragging a window goes into multi-select mode
- escape clears multi-select
- Face mode has "select coplanar" with tolerance


Clicking a face should use barycentric coords to select the nearest point.
- Will need to do additional checks in stack rotation to enable this (but only on topmost face)

Hide / Show console window?

# Critical Editor Features
Finish level loading
- matcen / reactor rooms
- triggers
    - draw markers in trigger mode

UI
- level nav tree
    - segments
        - objects
    - triggers
    - walls
- segment props / type (goal, energy center)
    - matcen props
    - reactor trigger
- props: trigger, wall, object
- mission settings
- hog file browser. list contents. add / remove.



# Selections

## Traditional Selection
- Walls and Triggers
- Curve builder

Editor::Selection PrimarySelection, SecondarySelection
Render::EditorSelection - Separate editor rendering in different file / namespace?


## Geometry Selections
Hotkeys: 1-3 (Face, edge, point)
- Bulk edits
- Add multiple segments on selected faces
    - Maintain connections between new seg sides within a certain angle / tolerance. Vertices are averaged.
- must be updated after deletes

Maintain selection between each mode. Holding shift and pressing number will convert it.
- Context menu -> Convert selection -> To point, line, etc


## Drag area
Area selections -> create 4 rays and project volume into scene. Test if points are contained within it.
Optimization: Only test segments within view frustum

# Selection Renderer
Selection -> batch rendering
Gather wireframes for current selection
Open side edges are separate - ensure they are only added once
- generate lines for all segments in selection with closed sides
- gather all open sides and remove shared connections
- select multiple faces -> extrude
    - single face, extrude along normal
    - multiple face, average normals
    - multiple face, extrude independently (though still join if normals are identical)

view frustum culling
render statistics
- toggleable UI overlay

# Camera
- lock cursor position when rotating? / wrap around window
- center on level, center on segment
- problem with up vector being incorrect with off-axis initial location

# project
rename to inferno, modules

# Texture property UI
- provide zoom / large preview of image
- list all found maps and configured paths
- EXT: roughness, specular, emissivity
- avg color

# file open dialog
- hog, mn2?

# Texture loader (piggy.cpp)
- atlases: texture browser, animations. performance seems fine without

# Texture editor

# Mission Editor

# Sound Browser


# Settings
- foreground / background FPS limit
- data path
- settings file
- texture filter setting
- active mode
- editor background color

# Editing
A segment, side, and point are always selected.
All transforms are relative to these selections.
Increments are set in toolbar (rotation and translation)
- hold alt and click point / face to move gizmo?
- reset gizmo button?

`selection` vs `marked`

chamfer
- select face
- chamfer 2 or 4 edges (freeform selection? very complicated)
- chamfer value (l x w)

curve generator
make planar
- select face
- select connected points / lines / faces along same side
- project all geometry to plane

subdivide edge
- take midpoint of edge, split along entire loop |--|--|--|--|
- 1 segment -> 2 segments

subdivide face
- split segment into 4 segments, using face center as new corners
- 1 segment -> 4 segments

subdivide segment (8)
- splits segment into 8 segments, using center as new corners
- 1 segment -> 8 segments
- requires breaking connecting faces

subdivide segment (7)
- similar to 8, but inserts a new segment at center while maintaining existing sides

escape: clear selection
space: select other segment

left click: select
ctrl left click: add selection
right click:
- display snap settings. mark, properties
- join / split (lines, points, side)
- mirror / extend / extrude

Transform:
- point: center on point, align to side
- line: center on line, align to side + point
- side: center on side, align to side + point
- segment: center on segment, align to side + point
Scale
- point: only works with multiple selections
- side: center on side. only has one plane
    - want easy way to scale in just one axis
- segment: center on segment
- want to support scaling by something other than center. for example selecting multiple lines and scaling from the bottom points
Rotate

Transform orientation
global, local, normal

## Object mode
## Texture mode
Forces face selection mode
split viewport? overlay window?
- select side, select planar faces (w/ tolerance)

freeform: select multiple faces / points / edges similar to blender

# undo/redo
- command buffer

# Components
- 3D Scene
    - Gizmos
    - Objects
    - Geometry

- MNX / HOGX format - extend existing level data while maintaining compatability


## shaders
- fixed depth sub surface shader. Refer to POE graphics video.
    - ice / water
    - alien crystal walls
    - sand crystals
- tesselation + displacement mapping
- cloaking
- light shafts? -> can determine light area based on overlay texture / id. Use surface normal, extend / decay
    - shaft disappears at closer distances.
- Heat distortion
    - lava, gunshots, impact / explosions
- Thickness shader for see-through textures. Compare camera vs. surface normal. Apply thickness based on dot product (0 = no change, 1 = x). refer to displacement techniques
- Rotation randomizer for explosions
    - explosion textures are not powers of 2
- flash / shimmer when hitting robots. ghost / aura effect. fresnel doesn't look great on low poly
- post process:
    - tone mapping
    - exposure
    - HDR glow
- instead of using palettes, apply tone mapping based on selected pig
    - rather not have to maintain separate sets of textures
    - groupA -> default
    - fire -> red
    - alien2 -> green
    - alien1 -> brown
    - ice -> deep blue
    - water -> grey / light blue

## Render depth
- doors should block visibility, unless see-through
- indexing / load into TLAS based on current visible cubes + depth (5-10). Extra depth is important due to off-screen light sources.


# sound overhaul
- reverb, attenuation, prevent 'blowing out' audio due to many hits at once (quad laser on reactor). use queue, reject duplicate submission
- use room volume to determine reverb

# HUD
- Create lightweight hud similar to D3. Overlay info in center like hud mode 2 but without backdrops. Add cockpit glass effect -> bloom dirt filter


## Rendering / Architecture
split things into more files? clearer separation of concerns?
Proper double buffering / fencing. refer to DX samples D3D12SmallResources

parallelized texture file reading (nCores -> threads -> split)
Secondary thread to update level geometry


## Cubes (Hexahedrons)
Calculate volume by splitting into 6 tetrahedrons and summing the result
```
1/6 * |det(a - d, b - d, c - d)|
1/6 * |(a - d) dot ((b - d) cross (c - d))|
```
Or just use the extents for rough estimate...

Volume can be used for lighting or sound calculations (larger volume, more reverb). 
nearly all materials are rock so assume constant material reflectiveness.

# Game vs Rendering

## Game vs API rendering
Game rendering accepts game objects and translates them into API calls

All DirectX calls should ideally be abstracted behind a generic graphics layer
Alternatively, each game component renderer could be written for each API and swapped out in the global render object.

Preferably, Render:: would not expose any raw D3D types (Only game types)


# architecture
try writing renderer as namespace with raw functions
- define the interface with global functions in namespaces / headers
- declare functionality using classes (if needed) in cpp
classes for objects, not global system state, or "managers"
in cpp:
- put locally 'global' variables in anonymous namespace {}. `s_` prefix can be used
- naming something "manager" often means it should just be raw functions


doom - interfaces with virtual fns:
- RenderSystem, exposes functions related to image size, UI rendering, contexts, viewports, level loading, gpu capabilities and active RenderWorld. init gl
- RenderWorld: portals, visibility, RenderScene(), entity defs,


## RTX Features
- locally emissive textures *very important*
    - Minecraft shows this as possible (glowstones)
- 1-2 ray bounces for path tracing
- soft shadows
    - cache is suitable, not many moving objects outside of robots
    - ignore powerups / sprites
    - proper transparency / ray casting for see-through textures



## mini engine
d3d12 engine - namespaces with static fns:
- Graphics: lifecycle methods (Present(), Resize()), size, device, descriptors, framerate
namespace with many global types for buffers, samplers, resource handles
namespace with functions for init, shutdown, resize, GetFrameRate


## doom
- fileSystem. uses global def
- console
- one class per file, static members. some headers define multiple related classes
- common class for system level commands (logging, key states, bindings, gui, specs)
- master header file per library
- many classes do private then public. separate private blocks for fn vs fields. each class has a different cpp file even if header is shared.
- struct: POD  class: has members
- SaveGame - has functions for writing types. wrapper around File for read/write
- pointer parameter -> can be reassigned. no clear consistency of pointer vs ref usage
- functions sometimes have large number of parameters
- reset functions to reuse memory of parameters
- idList. custom vector implementation. combines functionality of map + vector
- math lib is class with static members. odd avoidance of namespaces.
- global systems: Sys, Common, CmdSystem, FileSystem, Network, Rendersystem, SoundSystem, RenderModelManager, UI Manager, DeclManager (shaders?), CollisionModelManager
- sound system updates every 60 Hz, async loop
- Each system is self contained and static. Has init methods called by the startup
    - only makes sense for there ever to be a single sound system, render system, etc
        - render system would switch internal impl based on API. not its external interface.

- Systems follow the pattern of public interface, then inherit to internal class definition

```C++
// futures do not block until future.get()
auto fut = std::async(std::launch::async, doSomethingThatTakesTenSeconds);
auto result1 = doSomethingThatTakesTwentySeconds();
auto result2 = fut.get();
```

bindless texturing: https://docs.microsoft.com/en-us/windows/win32/direct3d12/resource-binding-in-hlsl#resource-types-and-arrays
