// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail E:\FL\Scratch\Source\Client\space\StarSys.cpp and StarSphere.h -- freelancer.exe
// .text 0x00552b60..0x00555760. The per-system hub: entering a system (FUN_005545a0) loads the
// system INI (FUN_00553740), keeps the preload lists that pin archetypes, sounds and voice
// lines for the stay, owns the nebula list, the [TexturePanels] shape DB and the background
// star sphere, and ticks nebulae, texture panels, asteroid fields, space dust and the music
// state every frame (FUN_00554eb0).
//
// Retail globals (all in .data 0x00679870..0x00679978):
//   0x00679870/74/78  std::list<Archetype::Root*> reloaded on leave     (static 005536a0)
//   0x0067987c/80/84  std::list<{voice, msg}>     [Archetype] voice =   (static 005534f0)
//   0x00679888/8c/90  std::list<unsigned>         [Archetype] snd =     (static 00553450)
//   0x00679894/98/9c  std::list<Archetype::Root*> [Archetype] preloads  (static 005533b0)
//   0x006798a0/a4/a8  std::list<Archetype::Root*> [Object] archetypes   (static 00553060)
//   0x006798ac        ShapeDB for [TexturePanels]                        (static 00552e40)
//   0x006798c4/c8/cc  std::list<Nebula*>                                 (static 00552da0)
//   0x006798d0..f4    music: override block (d0..e4), battle/danger/space ids (ec/f0/f4)
//   0x006798f8..fa    [Ambient] color bytes
//   0x00679900        StarSphere background object (0x44 bytes)          (static 00552b60)
//   0x00679954        Universe::ISystem* of the current system
//   0x00679958..70    music state machine
//   0x00679978        double: player collisions suppressed until this time (FUN_005544d0)
//
// The VC6 std::list globals are modern std::lists here (No retail ABI); the static
// constructor / atexit destructor pairs retail emits for them are the lists' own ctors/dtors.

#include "common_archetype.h" // CacheString
#include "client_mesh_load.h" // FClientMeshHolder

#include <list>

struct FNebulaEffect;
class Vector;
struct IObjRW;

namespace Universe
{
    struct ISystem;
}

// ------------------------------------------------------------------------ StarSphere.h

// One background layer: FUN_00420d40's 0x10-byte ObjectHandle plus the CacheString name at
// +0x10 (retail stride 0x14, vtable PTR_FUN_005e0e20 whose only slot FUN_00552c30 resets the
// vptr). A bare ObjectHandle, not an Object3D: its destructor releases nothing.
struct FStarSphereLayer
{
    FMeshHolder Holder;
    CacheString Name;
};

// DAT_00679900. Constructor FUN_00552b80, destructor FUN_00552c70.
//   Layers[0] (+0x00, name DAT_00679910) = [Background] nebulae
//   Layers[1] (+0x14, name DAT_00679924) = [Background] basic_stars
//   Layers[2] (+0x28, name DAT_00679938) = [Background] complex_stars / file
struct FStarSphereBackground
{
    FStarSphereLayer Layers[3];
    int CurrentQuality; // +0x3c  DAT_0067993c
    int DesiredQuality; // +0x40  DAT_00679940, written by the star_sphere perf option

    FStarSphereBackground();  // FUN_00552b80
    ~FStarSphereBackground(); // FUN_00552c70
};

extern FStarSphereBackground G_StarSphereBackground; // DAT_00679900

// FUN_00552e70 -- draw the background when [BACKGROUND] and STARS are on (options 0x19/0x1a).
void StarSphereDraw(void* Camera);

// LAB_00552cd0 -- the `star_sphere` perf option (IDS 0x57b): pick the desired quality.
void StarSphereApplyDetail(float Value);

// ------------------------------------------------------------------------ StarSys.cpp

// DAT_006798f8 -- [Ambient] color. DAT_0067986c -- the working copy FUN_00555470 and
// FUN_005554d0 compute from it.
extern unsigned char G_StarSysAmbientRgb[3];
extern unsigned char G_StarSysAmbientWorkRgb[3];

// FUN_005556a0 -- DAT_00679954.
Universe::ISystem const* StarSysGetSystem();

// FUN_00554e90 -- the current system's id (ISystem slot 0), 0 when there is none.
unsigned StarSysGetActiveSystemId();

// FUN_005553a0 -- &DAT_006798c4, the nebula list.
std::list<FNebulaEffect*>& StarSysNebulaList();

// FUN_005553b0 -- the nebula whose slot +0x20 fog factor at Pos is > 0; Hint is tried first.
// OutFactor (may be null) receives the factor, 0 when none.
FNebulaEffect* StarSysFindNebula(Vector const* Pos, float* OutFactor, FNebulaEffect* Hint);

// FUN_005552e0 -- the fog colour at Pos: the nebula's slot +0x34 colour * 255 when a nebula
// covers Pos with a factor >= 1, else the system's space_color.
void StarSysGetFogColor(Vector const* Pos, unsigned char* OutRgb);

// FUN_00555460 -- &DAT_006140b4, the [System] space_color bytes.
unsigned char const* StarSysSpaceColor();

// FUN_00555470 -- ambient bytes + the AMBIENT option (0x3b), clamped at 0xff; &DAT_0067986c.
unsigned char const* StarSysAmbientColor();

// FUN_005545a0 -- enter the system: bind DAT_00679954, PhySys, the system INI and the preload
// lists.
void StarSysEnter(unsigned SystemId);

// FUN_00554eb0 -- per-frame tick.
void StarSysUpdate(float Dt);

// FUN_00553660 -- is (Voice, Msg) on the [Archetype] voice preload list?
bool StarSysHasVoiceMessage(unsigned VoiceId, unsigned MsgId);
// FUN_00553590 -- Acquire the voice message and append it to that list (the body is the
// file-local PreloadVoiceLine). Also called by the NN voice queue FUN_00489a80 (0x00489ac8).
void StarSysPreloadVoiceMessage(unsigned VoiceId, unsigned MsgId);

// FUN_00553340 -- music override (DAT_006798d8..e4, fade DAT_006798d4, flag DAT_006798d0).
void StarSysSetMusicOverride(unsigned SpaceMusic, unsigned DangerMusic, unsigned BattleMusic,
                             unsigned OverrideMusic, float Fade, bool bNoFadeFlag);
// FUN_00553380 -- drop the music override.
void StarSysClearMusicOverride();

// FUN_00553250 -- a hit between A and the object the damage record names; flags battle music
// (DAT_00679968) when one side is the player and the other is hostile to it.
void StarSysNoteDamage(IObjRW* Victim, void const* DamageRecord);
// FUN_005532d0 -- flags battle music when the player's target (ship id) is hostile.
void StarSysNoteTargetChange(unsigned ShipId);

// FUN_005544d0 -- suppress player collision reporting for Seconds from now (DAT_00679978).
void StarSysSuppressPlayerCollisions(double Seconds);

// FUN_00554ab0 -- leave the current system (FUN_00554e80 passes a null player). Partial: the
// callees that have no port yet are named in the body. Retail callers 0054aa88 / 0054af90
// (the space teardown FUN_0054aec0 = DestroySpaceState, space_state.cpp) -- 0054af90 is ported,
// 0054aa88 is not.
void StarSysLeave(IObjRW* Player);
// FUN_00554e80 -- StarSysLeave(nullptr). Retail caller 0054ab23: ShutdownSpaceState (space_state.cpp).
void StarSysLeaveWithoutPlayer();

// FUN_0045a490(param_1 = Target, param_2 = Viewer) -- Viewer's attitude towards Target
// (Viewer vt+0x68), bucketed: -1 at or below -0.6, 1 at or above 0.6, 0 otherwise and whenever
// either side is null or the query fails. The StarSys callers pass (player, other): the other
// object's attitude towards the player. Callers across the exe (HUD, scanner, StarSys music);
// defined in star_sys.cpp.
struct IObjInspect;
int GetAttitudeClass(IObjInspect const* Target, IObjInspect const* Viewer);
