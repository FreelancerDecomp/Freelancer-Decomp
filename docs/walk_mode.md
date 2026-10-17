# WalkMode

A new gameplay feature from the `WalkModeTest` branch: controlling Trent in base rooms.
It is a client extension, not a restoration of the original game's behaviour.

Build: the `FreelancerWalkMode` property (`src/Directory.Build.props`,
`src/Freelancer.Diagnostics.props`, VS property page "Features"), `0` by default. All of the code
(`base_walk.cpp` and the hooks in `gameflow`, `gf_character_object`, `input_manager`, `menu_keymap`,
`nav_bar`, `nn_main_button`, `room_control`, `screen_quad_state`, `win3d*`) sits under
`#ifdef FREELANCER_WALK_MODE`; with `0` the client is identical to master.

## Controls

- A person-silhouette icon to the left of the room bar at the top toggles the mode. It is off by
  default. A yellow highlight means the mode is on; the hover tooltip shows `Walk mode: OFF/ON`.
  The icon follows the size of the stock room bar and updates its placement when the window
  changes.
- `W/S` — forward/back, `A/D` — strafe, `Shift` — run.
- Holding the right mouse button hides the cursor; the mouse turns Trent and the camera behind
  him. Holding the left button over an empty part of the scene orbits the camera only, keeping
  Trent's facing and the WASD directions. Clicks on buttons and NPCs keep the stock interaction.
  With both buttons held, the right one wins. After a free look the right button turns Trent to
  the current camera direction. Releasing both buttons puts the cursor back where it was, for the
  usual interaction.
- While turning with the mouse the interface fades out smoothly over one second; interface input
  is blocked immediately. On release the cursor and the interface reappear at once. A new hold
  starts the fade again; losing focus and leaving the mode reset it.
- The existing room, trade and launch buttons keep working. For a stock interaction the
  controller temporarily releases the character and restores the classic camera.
- Turning the mode off restores the character's original pose and the room's camera settings.
  Leaving the base resets the mode.

## Design

`src/freelancer/Client/base_walk.cpp` owns the mode state, the Win3D button, input, animations and
the camera. `base_walk_math.h` holds the engine-independent movement and camera math. The hooks
are called from Gameflow, the keyboard/mouse handlers and the room/character life cycle. The
controls are applied before the RoomObject update; after the animation mixer the root pose is set
again and the camera is updated, so that root motion from the animation clip does not pull the
character away from the controller's position.

The camera direction is kept separately from the character's. The fade is a one-second smoothstep
and acts only inside the Win3D draw: the opacity is applied to frame materials, icons, text and
lines. Colour caches are not modified; the base scene and the cursor are outside that pass. Panel
visibility during the fade is decoupled from the cursor flag, so the stock click blocking stays.

The controller uses the player's stock character in the current room. If there is none, WalkMode
creates its own CharacterObject from the `trent` costume and the `pc` metadata in RoomData: a
standing spot is preferred, otherwise the first one available. The own character uses a separate
deformable and is registered as an ordinary RoomObject for drawing. It does not take server
character slots and does not create base population/NPCs. It is deleted when the room or the mode
is released. A room without a `pc` spot stays classic.

Transitions, fades to black, cutscenes, modal windows and focus loss suspend the controls. Before
the stock Trent is handed to a script, his original pose is restored. Animation names are taken in
their original spelling from CharacterInfoDb; their references and handles are released together
with the controller. Continuous walking and running use the cycles `Sc_MLBODY_WLKG_000LV_XA_01`
and `Sc_MLBODY_RNNG_CYCL_000LV_XD_00`: their end poses match their start poses. Short steps and
transitions out of a standing pose are not suitable for looping. The step rate matches the
controller's speeds. The Trent model's local forward is minus Z; its rotation relative to the
controller's direction keeps the camera behind him.

The mechanism uses the current room's data and has no special cases for Manhattan. The first base
tested is Manhattan; the other bases still need in-game testing. The WalkMode state and the walking
position are not written to the save game.

## Limitations of the first version

The height is fixed at the character's spawn level. There are no collisions with walls, furniture
or camera geometry yet: you can walk through an obstacle or out of the set. Stairs and jumping are
not supported. Strafing and walking backwards use the same forward-walk clip. The free camera can
show surfaces that the original game's fixed cameras kept hidden.

## Testing the math

In PowerShell from the repository root:

```powershell
$env:TMP = 'D:\Temp\cl_tmp'
$env:TEMP = 'D:\Temp\cl_tmp'
& 'D:\Programs\Microsoft Visual Studio 2022\Msbuild\Current\Bin\amd64\MSBuild.exe' tools\tests\base_walk_math_tests.vcxproj -t:Build -p:Configuration=Release -p:Platform=Win32 -v:minimal
& 'D:\Temp\base_walk_tests\base_walk_math_tests.exe'
```

The test checks the icon placement to the left of the bar at various window aspect ratios, the
speeds, diagonal normalisation, height preservation, frame-rate independence, the pitch limits,
the independent look, mouse-button priority, the fade duration, the model's facing and the camera
geometry. In-game testing starts by loading a save made on a base; the main-menu scene is not a
base room.
