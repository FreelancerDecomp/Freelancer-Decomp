// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "client_gameobj.h"
#include "common_goods.h"
#include "x86math.h"

#include <cstddef>

struct XGoTradelane;

// Client/Space/tradelane.cpp (retail source path at .rdata 005db798) -- the client's trade lane
// ring runtime, 0x005135b0..0x00516ae3.
//
// World rings are ordinary CSolar objects (TypeBits bit 7 = CSolar::is_lane_ring). For every
// (ring gameobj, lane side) pair a ship is travelling through, the client keeps one
// FTradeLaneRingRuntime (operator_new 0x40 in FUN_00515200): the ids of the ships on it, a watcher
// on the ring gameobj, a glow fade and the ring's own "tl_ring_active" effect. Live nodes sit on
// DAT_006751c0/c4/c8, dead ones on the free list DAT_006751b4/b8/bc (both {head, tail, count},
// linked through the node's first two dwords by FUN_00404d50 / FUN_00404d90).
//
// A ship on a lane keeps its current node at ship +0x160, two effect companions at +0x164 (the
// ship's lane effects) and +0x174 (the player's tunnel effect), and flag bytes at +0x184 /
// +0x185. The effects come from the ring solar's TradeLane equipment companion (CliEQTradeLane,
// client_cliobj.cpp FCliTradeLane): the tl_player_* / tl_ship_* / tl_ring_* effect ids and the
// timings SecsBeforeEnter/Exit/Splash.
//
// The TLR joint spin and the HpRightLane / HpLeftLane tube effects of the ring itself live in
// client_equip_fx.cpp (FCliTradeLaneEquipFx, FUN_00516af0 .. FUN_00516ea0).

// FUN_00515200 node -- sizeof 0x40.
struct FTradeLaneRingRuntime
{
    FTradeLaneRingRuntime* LiveNext; // +0x00  live / free list link (FUN_00404d50 / 00404d90)
    FTradeLaneRingRuntime* LivePrev; // +0x04
    // +0x08..+0x13 -- a std::list<unsigned> of ship ids. +0x08 is its allocator byte (the new node
    // gets the Side argument's byte there, 0x005152dc; nothing reads it), +0x0c the head node
    // (FUN_0042a7e0, node {Next, Prev, Id}), +0x10 the size.
    unsigned char Side;              // +0x08
    unsigned char Pad09[3];
    void* ShipListSentinel;          // +0x0c
    int ShipListCount;               // +0x10
    BaseWatcher RingWatcher;         // +0x14  the ring gameobj's Watchable (gameobj + 8)
    unsigned char SideKey;           // +0x1c  lane side; the live list is keyed on (gameobj, side)
    // +0x1d -- 1 when the node is made or reused (FUN_00515200); FUN_00516910 stores "no ship is
    // using the lane" in it. The ring's tl_ring_active effect is only (re)started while it is set.
    unsigned char bActive;
    unsigned char Pad1e[2];
    // +0x20 -- the glow fade block FUN_004fcbe0 / 004fcbf0 / 004fcc40 run on.
    float GlowMax;                   // +0x20  the larger of CliEQTradeLane +0xa4 / +0xa0
    float GlowMin;                   // +0x24  the smaller
    float GlowScale;                 // +0x28  the current value; 4fcbe0 sets 1.0f, 515200 0
    float GlowParam;                 // +0x2c  the fade rate (per second)
    // +0x30 -- an effect companion {arch, sound, particle, light} (FUN_005154a0): the ring's
    // tl_ring_active effect.
    void* EffectSlot0;               // +0x30
    void* EffectSlot1;               // +0x34
    void* EffectSlot2;               // +0x38
    void* EffectSlot3;               // +0x3c
};
static_assert(sizeof(FTradeLaneRingRuntime) == 0x40, "TradeLaneRingRuntime 0x40");
static_assert(offsetof(FTradeLaneRingRuntime, ShipListSentinel) == 0x0c, "ship list @+0x0c");
static_assert(offsetof(FTradeLaneRingRuntime, RingWatcher) == 0x14, "watcher @+0x14");
static_assert(offsetof(FTradeLaneRingRuntime, SideKey) == 0x1c, "side key @+0x1c");
static_assert(offsetof(FTradeLaneRingRuntime, bActive) == 0x1d, "active @+0x1d");
static_assert(offsetof(FTradeLaneRingRuntime, GlowScale) == 0x28, "glow scale @+0x28");
static_assert(offsetof(FTradeLaneRingRuntime, EffectSlot0) == 0x30, "slots @+0x30");

enum : unsigned
{
    kTlShipRingRuntime = 0x160, // FTradeLaneRingRuntime*
    kTlShipFxBinder164 = 0x164, // the ship's lane effect companion
    kTlShipFxBinder174 = 0x174, // the player's tunnel effect companion
    kTlShipFlags184 = 0x184,
    kTlShipFlags185 = 0x185,
};

// Ship +0x184 (byte): bit 0 the entrance ring's side key (FUN_00515890), bit 1 the ring side the
// ship was on (get_ring_side), 0x20 travel effect started (FUN_00514750), 0x40 the player's tunnel
// effect is carried over to the next ring, 0x80 riding the lane. Ship +0x185: bit 0 splash done
// (FUN_00514960), bit 1 enter done (FUN_005145a0), bit 2 exit / disrupt done (FUN_00514bf0 /
// FUN_00514f20).

// FUN_00515200 (cdecl) -- the live node for (RingGameobj, Side), or a new one (the free list first,
// else operator_new) when bCreate; null for a solar that is not a lane ring.
FTradeLaneRingRuntime* TradeLaneRingFindOrCreate(FClientGameobj* RingGameobj,
                                                  unsigned char Side,
                                                  bool bCreate);

// FUN_00514110 (cdecl) -- HpRightLane (Side != 0) / HpLeftLane position of the ring, cached in
// DAT_006751dc / DAT_006751d0 on the first call for the process (DAT_006751fc).
void TradeLaneRingLaneHardpoint(FVector* Out, FClientGameobj* RingGameobj, unsigned char Side);

// FUN_00514010 (thiscall on Out, RET 4) -- identity orientation, Translation as the position.
void TradeLaneRingMakeTranslationXform(FTransform* Out, FVector const* Translation);

// FUN_00515890 (cdecl) -- the start tradelane command: the rings must be contiguous (else FDUMP,
// false); the ship moves onto the (entrance or exit, side) node.
bool TradeLaneStartCommand(FClientGameobj* EntranceRing, FClientGameobj* ExitRing,
                           FClientGameobj* Ship, unsigned char SideByte);

// FUN_005135b0 (cdecl) -- per frame: a player ship that stopped using the lane leaves its node;
// then every live node runs FUN_00513b40 and the ones it rejects go to the free list.
void TradeLaneDetachTick(float Dt);

// FUN_00515b60 / FUN_00515e20 (cdecl) -- the ship leaves its node (exit or disrupt effect when it
// was riding the lane). 515e20 is the entry the stop-tradelane paths call (IClient slot 7
// LAB_0053c760, CShip client slot LAB_00546f80 at 0x005470d9); 515b60 is FUN_00515890's.
void TradeLaneShipStop(FClientGameobj* Ship);

// FUN_00515500 (cdecl) -- the star system unload (0x00554bab): live nodes to the free list with
// their ships detached, then free-list nodes destroyed (FUN_00515730) and freed.
void TradeLaneShutdown();

// LAB_0053c830 -- IClient slot 6 (GoTradelane): the player's behavior manager hears event 4,
// then the ship uses the lane and FUN_00515890(Ring1, Ring2, Ship, Info->Field_0x0c).
void TradeLaneIClientGoTradelane(XGoTradelane const* Info);

// LAB_0053c760 -- IClient slot 7 (StopTradelane, RET 0x10): event 4 (leaving) to the player's
// behavior manager, then use_tradelane(false) and FUN_00515e20 on the ship.
void TradeLaneIClientStopTradelane(unsigned ShipId, unsigned EntranceId);

// LAB_00546e90 tail -- after CShip::go_tradelane succeeds: StartCommand(RingA, RingB, Ship, 0).
bool TradeLaneShipGoHandler(FClientGameobj* Ship, FClientGameobj* RingA,
                            FClientGameobj* RingB);

FTradeLaneRingRuntime* TradeLaneRingLiveHead();
int TradeLaneRingLiveCount();

// FUN_00516060 (cdecl) -- a lane ring (de)activated for a ship: leave (bActivate false) or join
// the node of the ring's lane direction.
void TradeLaneRingSideCommand(FClientGameobj* RingGameobj, unsigned ShipId, bool bActivate);

// FUN_005484c0 (thiscall on the solar gameobj, RET 8) -- a lane ring runs FUN_00516060, then, out
// of single player, CSolar::activate.
void TradeLaneClientSolarActivate(FClientGameobj* SolarGo, bool bActivate, unsigned ShipId);

// FUN_0053d940 -- IClient SPObjActivate: FindById(ObjectId); ManagerKind 2 -> 005484c0.
void TradeLaneSPObjActivate(unsigned ObjectId, bool bActivate, unsigned ShipId);

// FUN_00516910 (cdecl) -- a lane ring solar's notify 0x11 (0x00547979, the event byte as
// bShipLeft): its CliTradeLaneEquip ship count steps -1 / +1, the lane effects follow, and both
// side nodes learn whether any ship still uses the lane. Returns 1 when none does; the caller
// stores it in FClientSolarGameobj +0x1f0.
unsigned char TradeLaneSolarLaneSwitch(FClientGameobj* SolarGo, unsigned char bShipLeft);
