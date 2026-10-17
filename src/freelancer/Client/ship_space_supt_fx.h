// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail freelancer.exe .text 0x00540930-0x0055fea0, continuing directly on from
// space_client_objects_fx.h's IClientS.cpp entry (which ends "0x0053d940-0x0053f880
// ... continues to ~0x005437d0 where retail's ship.cpp begins"). Discovered while
// auditing this range (decomp-tracker agent claude-sonnet-freelancer-batch49a).
//
// This confirms and supersedes that header's guess: the range is NOT one single
// ship.cpp file. It is a run of EIGHT distinct retail files, back to back, matching
// the same "client-side wrapper/subsystem, none ported" pattern as
// space_client_objects_fx.h and tradelane_fx.h. Boundaries were derived the same way:
// for each embedded "E:\FL\Scratch\Source\Client\..." path string, walking back to the
// nearest preceding function header in freelancer.exe.c; where no new path string
// appears for a long stretch, address contiguity + shared subject matter (verified by
// reading representative functions, not assumed) place the gap with its nearest
// confirmed neighbour, exactly as CliAsteroid's leading list-walk functions were folded
// into its range in space_client_objects_fx.h.
//
// ----------------------------------------------------------------------------------------------
// 1) ship.cpp -- freelancer.exe ~0x00540930-0x0054a97f
// CORRECTION (2026-09-25): the reading below is wrong. ship.cpp is the CLIENT Ship gameobj
// (FClientShipGameobj, client_gameobj.h); Common's CShip is only reached through the IAT.
// FUN_00543b40 is FClientShipGameobj::RequestLand and its IAction is Action_Land
// (client_land_action.h, FUN_00542b70..LAB_00542e10). The rest is kept as history.
// Confirmed via "E:\\FL\\Scratch\\Source\\Client\\space\\ship.cpp" embedded in
// FUN_005437d0 (freelancer.exe.c line ~205132, "invalid jump state set in ship") and
// FUN_00543b40 (line ~205183, "Cannot land on BASE_ID_NONE") and FUN_00546d00 (line
// ~206472, "stale request detected"). This is retail's CShip class: the single most
// important game-object class in Freelancer (ship flight/combat/docking state machine).
// The decompiler recovered real C++ names throughout this range from embedded debug
// info -- CShip::jump_in, CShip::set_target, CShip::launch, CShip::airlock_in,
// CShip::is_launching, CShip::is_jumping, CShip::get_tgt_lead_fire_pos, CShip::is_enemy,
// CShip::open_bay, CShip::get_pilot_name, CShip::VerifyTarget, CShip::get_target, etc.
// (freelancer.exe.c has 140+ CShip:: call-site hits total, spanning far outside this
// batch's range too -- CShip is used everywhere). FUN_005437d0 is CShip's jump-state
// dispatcher (switch on IObjRW jump-state code 0-5: jump_in, cancel via set_target,
// FDUMP fallback); FUN_00543b40 is the docking/land request handler (allocates an
// IAction via ActionDB::add, checks BASE_ID_NONE). The huge run of LAB_/vtable_method
// items throughout this range (LAB_00545050 385insn, LAB_00546050 187insn,
// LAB_00546b50 147insn, etc.) is CShip's IObjRW vtable override table -- consistent with
// CShip being one of retail's largest polymorphic classes.
// No CShip class exists in the port. src/include/common_cequip.h only forward-declares
// `class CShip;`; src/include/server_cship.h documents a *server-side* FCShip field
// layout (reconstructed from Common.dll, used by Server.dll via IAT imports) but that is
// a different concept from THIS client-side CShip -- freelancer.exe embeds its own full
// CShip implementation (matches Freelancer's known architecture: single-player games run
// an embedded server inside freelancer.exe itself, so the client binary carries a
// complete copy of the game-object model, not just a thin network proxy). Building a
// real port would mean reverse-engineering and standing up all of CShip from scratch --
// out of scope for a single pass; correctly `simplified`, not `blocked` (the retail
// state-machine logic in the sampled functions is fully understood).
//
// ----------------------------------------------------------------------------------------------
// 2) space.cpp (connect/disconnect session lifecycle) -- freelancer.exe ~0x0054ab20-0x0054be1f
// Confirmed via "E:\\FL\\Scratch\\Source\\Client\\space\\space.cpp" embedded in
// FUN_0054ab40 (freelancer.exe.c line ~208448, "ship has no cockpit definition") --
// already tracked separately as `partial`, ported_name "InitSpaceConnectSlice"
// (src/freelancer/Client/player_spawn.cpp). The items in THIS batch bracket that
// function: FUN_0054aec0 (space.cpp's *disconnect/teardown* counterpart -- releases
// dust zones, resets three IDPMsgHandler connections, calls CObject::ReleaseAll,
// AnimDB::Shutdown, and asteroid/animation cleanup helpers, guarded by the same
// "IDR_SPACE" resource string InitSpaceConnectSlice uses) and several smaller
// behavior-manager/state-flag helpers (FUN_0054b000 family) that only make sense as
// space.cpp's session bookkeepping alongside connect/disconnect. Not ported -- the
// port's player_spawn.cpp only implements the connect half; there is no
// TeardownSpaceConnectSlice equivalent.
//
// ----------------------------------------------------------------------------------------------
// 3) SpaceDust.cpp -- freelancer.exe 0x0054bda0-0x0054c361
// PORTED 2026-09-25 in space_dust.h/.cpp (static inits 0054bda0/0054bdb0, FUN_0054bdc0,
// FUN_0054bde0, FUN_0054be20, FUN_0054beb0, FUN_0054c320), wired from star_sys.cpp.
// 0x0054c370 onward is the perf-options "space_dust_detail" registration, not SpaceDust.
//
// ----------------------------------------------------------------------------------------------
// 4) SpaceTerrainRenderComp.cpp -- freelancer.exe ~0x0054de10-0x00552ecf
// Confirmed via "E:\\FL\\Scratch\\Source\\Client\\Space\\SpaceTerrainRenderComp.cpp"
// (freelancer.exe.c lines ~210635, 211502, 212278, 212418). Client-side terrain/asteroid-
// field billboard rendering component: FUN_0054de10 computes a rock count from an
// INI-driven field ("Billboard rock count is not > 0 ..." assertion) and allocates a
// per-rock array (operator_new(rockCount << 5), i.e. 0x20-byte records). This is the
// asteroid-field billboard-rock renderer ([AsteroidBillboards] of the field ini).
// Ported: the block and its setup (FUN_0054de10 BillboardEnsureRocks, FUN_00550000
// BillboardSetQuality) live in cli_asteroid.cpp; the draw FUN_0054e900, its lighting
// FUN_0054e130, the per-rock emitter FUN_0054f9f0 and the immediate-mode batch object
// DAT_006797d0 (FUN_0054daf0..FUN_0054fd30) live in asteroid_billboard.h/.cpp.
//
// ----------------------------------------------------------------------------------------------
// 5) StarSphere.h / StarSys.cpp -- freelancer.exe ~0x00552fd0-0x005561ff
// Confirmed via "E:\\FL\\Scratch\\Source\\Client\\space\\StarSphere.h" (FUN_00552fd0,
// "couldnt create starsphere %s") and "E:\\FL\\Scratch\\Source\\Client\\space\\StarSys.cpp"
// (freelancer.exe.c lines ~213971-215273, six separate FDUMP call sites). This is
// retail's per-system rendering/tick orchestrator: FUN_00552fd0 creates the skybox
// starsphere mesh via the Engine_exref singleton; a later huge function (traced at
// freelancer.exe.c:215260+, "No active camera available upon StarSys update") is
// StarSys::Update, the main per-frame space-simulation driver -- it calls SpaceDust's
// FUN_0054beb0 once per active camera, walks two node lists via vtable+8/+0xc callbacks,
// calls the already-ported CmnAsteroid::Update (multiplayer path) and FUN_00520130
// (CliAsteroid, space_client_objects_fx.h cluster 1), and resolves several camera-state
// DAT_006798xx globals. This confirms StarSys.cpp is the architectural hub tying
// together nearly every other unported client-space subsystem in this file (dust,
// terrain, asteroids) plus the already-ported CliAsteroid/CmnAsteroid pair -- it cannot
// be meaningfully ported piecemeal while its callees remain unported. FUN_00554ab0
// (Archetype::EnumSimples/EnumShips/EnumSolars/EnumAsteroids/EnumDynamicAsteroids/
// EnumEquipment enumerator-registration) is StarSys.cpp's per-archetype visual-reload
// hook, folded into this cluster by contiguity + shared subject (archetype visual state,
// not any other identified file). Not ported; no StarSphere/StarSys equivalent exists in
// src/ (space_scene_load.cpp/space_scene_draw.h cover System.ini parsing and the actual
// D3D scene-graph registration the port DOES have, but not this orchestrator).
//
// ----------------------------------------------------------------------------------------------
// 6) Hotspot.cpp -- freelancer.exe ~0x00556200-0x005572ff
// Confirmed via "E:\\FL\\Scratch\\Source\\Client\\supt\\Hotspot.cpp" (freelancer.exe.c
// line ~216328, "hotspot = %p id = %d not freed"). Client "supt" (support) library's
// 3D-cursor/mouse-hit-test hotspot registry: a global doubly-linked list (DAT_0067999c)
// of hotspot records, each holding a 2D screen-space AABB-ish region (compared via
// squared-distance math against a cursor point in the sampled FUN_00556090-area code)
// used to resolve which in-space object the mouse is currently over (ship/solar
// picking, tooltip targets). Not ported; no hotspot/picking-registry exists in src/
// (input_manager.h covers raw input, not this space-picking layer).
//
// ----------------------------------------------------------------------------------------------
// 7) LightsCamera.cpp -- freelancer.exe ~0x00557360-0x00558bff
// Confirmed via "E:\\FL\\Scratch\\Source\\Client\\supt\\LightsCamera.cpp" (freelancer.exe.c
// lines ~217054, 217529). Client "supt" library's FX-effect/lighting-camera bootstrap:
// the sampled function acquires the Alchemy_exref/Dacom_exref/FxEffectLibrary_exref
// singletons and drives an FX-effect-library load sequence (matches retail's "Alchemy"
// FX engine integration referenced elsewhere in this codebase, e.g. engine_light.h).
// Not ported; no LightsCamera-equivalent bootstrap exists in src/ (engine_light.cpp
// covers the already-ported D3DLIGHT8 point-light object itself, not this FX-library
// camera/lighting setup layer).
//
// ----------------------------------------------------------------------------------------------
// 8) Support.cpp -- freelancer.exe ~0x00558c10-0x0055fea0+ (continues past this batch's
// 0x00560000 boundary; the next distinct file found is Interface\CharCreate.cpp at
// freelancer.exe.c line ~222713, decomp line 222713, just after this range's last item
// at decomp line 222073 -- out of this batch's scope).
// Confirmed via "E:\\FL\\Scratch\\Source\\Client\\supt\\Support.cpp" (freelancer.exe.c
// line ~218139). Client "supt" library's grab-bag of generic support utilities used
// across many client visual-object classes: sampled functions include frame-time-capped
// update helpers (FUN_00558c10, reusing the already-ported FUN_0042d680/
// FUN_0042d6e0/FUN_0042d740 frame-time primitives from GetClientFrameTimeCapped's
// family) and a large (432 insn) INI_Reader-driven per-object visual constructor
// (FUN_0055a6b0) that reads generic "mesh"/"mesh2" keys via INI_Reader::is_value +
// get_value_string + std::basic_string::_Grow -- i.e. the shared base-class ini-driven
// mesh-assignment loader reused by many of retail's client Gf/Cli visual wrapper
// classes (ObjectShip.cpp, EquippedObj.cpp, etc. -- all themselves unported, see
// space_client_objects_fx.h). This is a large (~140-function) file; not individually
// traced function-by-function given its generic/support nature, but every sampled
// function confirms the same theme (shared client-object support code, no dedicated
// class of its own). Not ported; no Support.cpp equivalent exists in src/ as a single
// file (its individual concepts are scattered/duplicated ad hoc across the port's own
// client files where each was independently needed).
//
// None of these eight files/subsystems exist in the port. All items in this range are
// classified `simplified` (retail behaviour understood from the sampled evidence above)
// rather than `blocked` or `ready_unverified`, per the same rationale used throughout
// space_client_objects_fx.h and tradelane_fx.h: porting any of them for real would mean
// standing up an entire missing class/subsystem (CShip itself, in cluster 1's case, is
// the single largest such gap found anywhere in this decompilation-tracker marathon so
// far) rather than filling a scoped hole in something that already exists.
