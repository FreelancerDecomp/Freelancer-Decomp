# freelancer.exe — the client

`src/freelancer/` builds into `freelancer.exe`: the entry point, the window and message loop, the client
state machine, the menu game flow (Gf\Script, Thorn scenes), interface widgets (NeuroNet `NN_*`,
`WinControl`), the HUD, space (cameras, rendering, client objects), sound, effects, and the connection to
the server. This document is for whoever ports or edits the client: it describes the architecture and the
contracts between subsystems, not individual functions — their status and line-by-line notes live in
decomp-tracker (module `freelancer.exe`, listing `EXE/Disasm/freelancer.exe.{txt,c}`, image
`0x00400000`). Rendering, materials, scenes and physics live in other modules (RP8, RendComp, Shading,
Thorn, EngBase, alchemy, Common) — the client gives them data and calls their interfaces rather than
duplicating them.

Related documents: [freelancer_cliobj.md](freelancer_cliobj.md) (client companions of archetypes),
[equip_fx_factory.md](equip_fx_factory.md) (equipment FX), [options_screen.md](options_screen.md),
[dealer_equip_stats.md](dealer_equip_stats.md), [winmain.md](winmain.md), [deviations.md](deviations.md).

## Startup and the main loop

- `WinMain` (`main.cpp`): instance mutex, DATA check, `LoadStartupConfig`
  (`startup_config.cpp`), window creation, `InitializeEngine` (`engine_init.cpp`), the loop
  (`message_pump.cpp`). `InitializeEngine` brings up `DALib`, the render pipeline and buffers, calls
  `DataConfig::startup` with an `ICliObjFactory` and `ProgressCB` (`dataconfig_progress.cpp`: the `[Data]`
  keys that Common does not know — fonts, sounds, voices, effects, HUD, `intro` — are handled by the client
  as they are read), shows the splash screen, and loads `movie.dll` through `GetProcAddress`.
- **Client states** are objects `IEnvironment : IInputEvent` (15 slots, `client_environment.h`):
  `GF` (the base game flow, `gameflow.cpp`), `SpaceApp` (`space_app.h`), `IntroFocus`
  (`client_state.cpp`), `MissionBriefing` (`mission_briefing.cpp`). `EnterClientState(id, prev)`
  (`client_state.cpp`): 1 — exit, 2 — space, 3 — base (GF), 4/5 — slot 10 of GF/SpaceApp (empty),
  6 — menu, 7 — game load, 8 — mission briefing; other ids return the previous state.
  The next transition is set by `G_PendingClientStateId`.
- **Frame**: `TickClientFrame` updates the timer (`client_timing.cpp`), the current state, lighting, the HUD
  and rendering; in the menu the state itself presents the frame (3D → clear Z → UI).
- **The order of static initializers is an input contract.** KeyMap event handlers of one id are called in
  reverse order of registration, and the first one to return `true` "eats" the event; some handlers are
  registered by static initializers, so the order of the `_initterm` table determines who sees the mouse
  first (in retail the NeuroNet window intercepts a click before the room hotspot). In the port the order of
  the `.CRT$XCU` segment is set by linking, so `gameflow.cpp` is pinned with `#pragma init_seg(lib)`; a new
  static handler that shares an id with an existing one has to be checked against the table
  `0x0060f000..0x006101bc`.

## Menu and base game flow

Base scenes are Thorn scripts (`.thn`) executed by `Thorn.dll`; for their entities the client creates
`FAttachObject`/`Gf\Script` objects (`gf_script*.cpp`, `gf_thorn_*.cpp`): types 1–9 and 13 (compound,
deformable, camera, light, scene, psys, marker, sound, hardpoint wrapper). `RoomControl`
(`room_control*.cpp`) draws the room: layers, object traversal, the Alchemy pass (`DAT_00679a10` is the
`FRoomObject` registry), room cameras (`room_camera.cpp`, `find_camera` after `RoomData::load`),
hotspots. The camera, `SetView` and `GeoSetPos/Orient` come from Thorn/EngBase: if the frame is empty after
slot 22 `SET_CAMERA`, they are to blame, not the exe. The base's location and state are
`menu_universe_base.cpp` (`LocationController`: entry/exit, room music and ambience from `RoomData`).
The character/server/account selection screens are `page_*.cpp`, the connection is `connect_*.cpp`,
`server_connect.cpp`, `login_mgr.cpp`, `intro_connect.cpp`.

## Interface layer

- **Win3D / WinControl**: `win3d*.cpp` is the viewport host (perspective, matrices); `wincontrol_base`,
  `wincontrol_children` are the base of GUI trees (child list, create/render/destroy cascade, ~98 concrete
  vtables in retail).
- **NeuroNet (`NN_*`)**: the window factories `NN_Main`, `NN_Inventory`, `NN_Dealer`, `NN_Trade`,
  `NN_Repair`, `NN_NavMap`, `NN_Pause`, `NN_LoadSave`, `NN_Preferences`, `NN_Scanner`, `NN_News`,
  `NN_Exit`, `NN_Help` … (`nn_*.cpp`); the controls are `nn_text_control`, `nn_static_control`,
  `nn_button_control`, `nn_group_control`. Button captions are drawn by `font_atlas`/`font_imp`
  (`FontAtlasRasterize` uses `ETO_OPAQUE`, otherwise the letters of previous buttons remain in the shared
  bitmap).
- **KeyMap**: `interface/KeyMap.ini` + the user's `UserKeyMap.ini` (`menu_keymap.cpp`); the controls
  settings page is part of `NN_Preferences` (`nn_preferences.*`, see
  [options_screen.md](options_screen.md)); `perf_options.cpp` is the performance options registry
  (`PerfOptions.ini` in the user directory; sections `[PerfVersion]`, `[PerfCount]`, `[PerfOptions]`,
  `[Display]`).
- **Text**: `ids_*`/`infocard_map` (IDS strings of the resource DLLs), `rich_text_win`/`ui_rich_text_win`
  (RDL text), `font_string` (`FontString::Paint` takes the clip from the surface size and shifts the shadow
  by 1 or 2 pixels depending on the screen width). The `TextControl 0x23` property sets horizontal
  cropping (left and right, in pixels) — the news ticker relies on it.
- **Transition scene setting**: the PerfOptions registry and the `GfScriptSequence` player share one state
  `skip_transition_scripts` (a value below 0.5 enables auto-skip of landing and take-off; 1.0 shows the
  scenes).
- **Sound**: `menu_sound_arch.cpp` is the client mixer, sources (`Sound::Source`, streaming, spatial),
  ducking, bgm; `sound_config.cpp` is `audio\SoundCFG.ini` (`[CFG]`) and the Gf fader;
  `voice_profiles.cpp` is the voice registry (`[Data] voices=`, `[Voice]`, `[VoiceSound]`).

## HUD

`HUD=` in `[Data]` gives the path to `interface\hud.ini`; when the cockpit is initialised,
`LoadHudElementsFromIni` (`hud_element_list.cpp`) reads it and creates elements from the section headers
through a chain of factories; active elements receive the Notify/Event/Render/Shutdown broadcast, and the
frame is drawn by `RenderHudElements`. Sections/classes: `[Steer]` (the mouse-flight arrow), `[Core]` and
`[Status]` (ship gauges, `hud_status_core.cpp`, weapon groups — `hud_weapon_groups.cpp`), `[Target]`
(`hud_target*.cpp`), `[Targetable_Objects]` (brackets and lead markers, `hud_targetable_objects.cpp`),
`[DamageIndicator]`, `[Receiver]` (`HUD_Comm`, radio queue and portrait, `hud_comm.cpp`),
`[ContactList]`, `[Maneuvers]`, `[Waypoint]`, `[CruiseProgress]`. The `HUD=` path is remembered separately
from the registry load itself. In widescreen mode the corner HUD windows are moved apart toward the edges
(`hud_widescreen.*`).

## Space

- **Entering a system**: `space_scene_load.cpp` reads the system INI (`[Object]`, `[Archetype]`,
  `[Nebula]`, stars, `star_sys`) and builds the furniture scene; in single player Server's
  `StarSystem::add_player` itself sends `CREATESHIP`/`CREATELOOT` (`IClient` slots 47/48), while
  `CREATESOLAR` is sent only in multiplayer. Poses of foreign objects from the `UPDATEOBJECT` packet arrive
  only in MP (in SP `SystemPlayerView_Update` returns at once, the objects share a process with the
  server). Client game objects are `client_gameobj.*` (`FClientGameobj`, ~0x208 bytes), the `IObjDB`
  registry is `client_objdb.cpp`, player spawn is `player_spawn.cpp`.
- **Cameras**: the base class and derived ones (`space_camera_*`): first/third/rear/turret/script/static/
  docking/death; parameters from `shiparch.ini` and the `CliShip` companion (`camera_offset`, angles,
  `camera_turn_look_ahead_slerp_amount`), cockpit ini loaders (`cockpit_load.cpp`).
- **Draw pass** (`space_scene_obj_draw.cpp`, `space_scene_draw.cpp`): the object queue is sorted by
  descending squared distance (far ones first) — as in retail, this is the MSVC6 `_Sort`: insertion sort
  for `< 17` elements, otherwise median-of-three + Hoare partitioning (strict comparisons, unstable) + a
  final insertion pass; it must not be replaced with a simple sort. Every queue "flush" begins with
  re-setting the camera perspective (`Win3DSetPerspective`). Flush order: meshes, asteroids
  (`cli_asteroid*.cpp` — field cubes, fading, immersion in the exclusion zone), nebulae, effects;
  `FUN_0051f950` is the mesh flush and the walk of the asteroid registry.
- **Nebulae** (`cli_nebula.cpp`, retail `JumpEffect.cpp`): the `[Nebula]` section of the system INI
  creates a 0x2d0 object; it loads the nebula file (`vtable[0](file, zone)`), holds two trees (`+0x04`
  nodes of 0x5c, `+0x38` nodes of 0x18 with meshes), the zone shell (`Exclusion::NebulaExclusionZone`)
  and lightning (billboard segments); jump gates are `gate_tunnel.cpp`. Planet rings
  (`texture_panels.cpp`) and the star glow (`star_sphere.cpp`) are separate classes, not parts of a
  nebula.
- **Lights and fog**: equipment lights go through a shared batch queue of screen quads (vertices
  `XYZRHW | DIFFUSE | SPECULAR | TEX1`), the fog for additive effects is black; the choice between vertex
  and table fog (`FogApplyColorAndRange`) depends on `QueryDeviceAbility(13)` (the device's W-fog), not on
  `GetPipelineState(13)`. With vertex fog the alpha is taken from `SPECULAR`, which the batch zeroes — the
  wrong mode hides glints even on nearby objects.
- **Effects** (`effect_type_*.cpp`, `effect_base`, `particle_effect.cpp`, `beam_effect.cpp`,
  `effect_vertex_batch.cpp`): effects lie in "buckets" by type (`G_EffectTypeBuckets`, type `0x2c` is a
  beam) that `SpaceEffectsDrawFrame` walks at three points of the frame; a beam (`BeamEffect`) is drawn as
  a cross of two perpendicular billboards (texture `code_beam`, FVF `0x142`, additive blending) in a shared
  growing CPU vertex/index buffer that is flushed once per frame (`DrawIndexedPrimitiveUP`). The firing
  chain: `CliShipFireWeapons` → the `FCliLauncherFx`/`FCliGunFx` sockets
  → `CliLauncherFireProjectiles` → `FClientBeamGameobj` → `CliBeamActivateEffects` → `BeamEffectStart`
  → flush of bucket `0x2c` (see [equip_fx_factory.md](equip_fx_factory.md)).
- **Alchemy FX and particles** (`vis_effect_arch.cpp`, `.ale` files): `LoadAlePath` opens an `.ale` as a
  UTF `IFileSystem`; inside are two directory nodes `ALEffectLib\ALEffectLib` and
  `AlchemyNodeLibrary\AlchemyNodeLibrary` — the data is read "inside" the directory (`OpenNode` followed by
  `OpenByDesc` of the same name), and `..` is called only at the end. `LoadNodeLibrary` and
  `LoadAle` do not clear the accumulated libraries but merge them (duplicates are dropped; re-loading an
  already cached path is skipped). An effect is created by `CreateEffect(lib, FxNode, crc)`,
  where `crc` is the INI `effect_crc` = the hash of the effect name by the alchemy CRC table (case-sensitive,
  **not** `DACOM_CRC::GetCRC32` and not IEEE); the `FxNode` attaches to the Alchemy root and stays there
  when stopped. Every frame ScriptPsys calls `Slot3` (Thorn transforms → nodes), `Slot7`, `Slot8`
  (`SubmitNode` for a flagged node); nobody in the menu calls `Slot5` (emission).

## Single player and the server

The SP server runs in the client process (`Server.dll` + `rpclocal`), the client side of the protocol is
`client_iclient*.cpp` (`IClient`, ~90 slots; packets `CREATE*`, `UPDATEOBJECT`, `DESTROY*`,
`LAND`/`LAUNCH`, `SYSTEM_SWITCH_*`, `SETREPUTATION` …). The server sends the answer to `RequestCreateShip`
by a direct call of `REQUESTCREATESHIPRESP` (slot 61), not by a controller message. `connect_flow.cpp`
builds the connection, `character_select_equip.cpp` builds equipment from `Loadout`/`EquipDescList`,
`player_spawn.cpp` handles the first space.

## Widescreen mode

`FREELANCER_WIDESCREEN_SUPPORT` (property `FreelancerWidescreenSupport`, default 1; read only by the
client): cameras work as "Hor+" — half of the horizontal angle means the angle on a 4:3 surface of the same
height, and with a full-width viewport and a surface wider than 4:3 the frustum is widened by the factor
`WidescreenHorPlusScale`; the vertical angle and the whole Win3D interface stay as on 4:3, and the extra
width is added at the sides (`widescreen.h`, `hud_widescreen.*`). With the value 0 a retail 4:3 client is
built. Deviations from retail are recorded in [deviations.md](deviations.md).
See also [../hd_edition.md](../hd_edition.md).

## Code outside the Ghidra inventory

Part of `.text` that Ghidra did not disassemble has no rows in the tracker and the metric does not see it;
such ranges were analysed from raw PE bytes (by prologue and call site), and the conclusion is left in a
comment at the nearest tracked function of the port. Known ranges:

| Range | What it is |
|---|---|
| `0x00412e41..0x0041353f` | `FontString` vt+0x24 `Paint` (clip by surface size, 1–2 pixel shadow, left/right string cropping) |
| `0x00504e5e..0x00504faf` | head of the nebula load (`Nebula` vt[0] `Load`) up to the section loop |
| `0x00534e5c..0x00534f43` | `IObjAffect` slot 16 — tractor onto a single target |
| `0x00573eae..0x00573f75` | the `IntroFocus` driver: state exit/enter loop and polling |
| `0x00574380..0x0057490c` | menu `EnterState`: prologue, case table, case 1 (TypeSelect column, entering SP) |
| `0x00574fbe..0x00575293` | case 18: `PageCharacterSelectSingle` |
| `0x005753ac..0x0057542f` | `TextMsgButton::SetFlagsE4E5`, `Init` |
| `0x0059a69e..0x0059a75f` | `UIListBox` debug frame of slot 13 (reads an unchanging `.bss` byte, equivalent to absent) |
| `0x005a1cb8..0x005a1d3f` | `TextWin::SetFont` |
| `0x005b089a..0x005b09af` | `DALib::FatalErrorFn` handler |
| `0x005b8220..0x005b82cf` | not code: 11 `DIOBJECTDATAFORMAT` records from `dinput8.lib` |

## What is not fixed here

| Symptom on the frame | Where the cause is |
|---|---|
| No live `Csys` after slot 22 (`SET_CAMERA`, spatial) | Thorn.dll (IGeo 3/4) |
| Fix/Cons joints, star sphere not at the origin | EngBase (IEngine 47/48) |
| No PSYS flashes, `CreateEffect hr=-8` | alchemy / effect library |
| Black nebula, SphereMesh seam | Shading / RendComp |
| `front_button.cmp` VMesh handle=0 | ReadFile/RendComp (`OpenNode`) |

## Known gaps

- Not all `WinControl` classes (≈98 in retail) and NeuroNet screens are ported, or ported in full;
  the places where the port does its own thing instead of calling ported modules are analysed in
  [substitutions.md](substitutions.md) and recorded in [deviations.md](deviations.md).
- The line-by-line status of the client functions (including `UpdateSpaceState`, camera placement,
  `JumpEffect`, tradelane FX) is in decomp-tracker; where it disagrees with this document, the listing is
  right.
