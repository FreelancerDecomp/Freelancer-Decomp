// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"
#include "common_ini.h"      // Vector
#include "common_csys.h"     // Matrix
#include "common_goods.h"    // BaseWatcher
#include "common_aisteer.h"  // ObstacleAvoidance, IBehaviorManager

#include <cstddef>

// ============================================================================
// PathfindManager -- the per-ship AI path/obstacle state IBehaviorManager owns at +0x0C.
//
// Identity: IBehaviorManager's ctor FUN_062d2220 does `PUSH 0x580; CALL operator_new;
// PUSH ESI; MOV ECX,EAX; CALL FUN_062db3b0` and stores the result at IBehaviorManager+0x0C
// (.text:062d2327-062d2369). Server.dll's pub::AI::SubmitState loads that word into ECX and
// calls the exported PathfindManager::clear_user_zone / submit_user_zone on it
// (Server .text:06d5c999, 06d5c9ef, 06d5cb00, 06d5cb42), so FUN_062db3b0 is
// PathfindManager::PathfindManager(IBehaviorManager*) and the object is 0x580 bytes.
// No RTTI: the class has no vtable (the ctor never writes one; +0x00 is a plain dword).
//
// Destructor: never emitted out of line. IBehaviorManager's dtor FUN_062d2530 inlines it at
// .text:062d2573-062d25bc: `if (+0x218) set_pointer(0)`, `if (+0x1c8) set_pointer(0)`, then
// operator delete -- i.e. the implicit destructor of the two BaseWatcher members below, in
// reverse declaration order. The port's implicit destructor does exactly that.
//
// EXPORTS: member-level. Retail exports clear_user_zone / get_user_zone / submit_user_zone and
// nothing else of the class (no ctor, no operator=), so the class carries no export macro.
//
// Layout: every field below is placed from the ctor's own stores (Common.dll.txt
// 225461-225695) and, where named, from the readers cited on the field. Offset-named members
// are fields whose type the ctor or a reader confirms but whose meaning is not recovered.
// Some per-tick methods of this class are still ported as free functions over a raw byte
// pointer (common_obstacleavoidance.cpp: ClassifyShipObstaclePhase, UpdateShipHeadingBlend, ...) --
// see their own comments. Definitions: common_pathfind.cpp.
// ============================================================================
class PathfindManager
{
public:
    // Size from get_user_zone / submit_user_zone's dword copy loops (0x16 dwords = 0x58
    // bytes). Nested in PathfindManager, as the retail mangling says
    // (UUserZone@1@ in ?get_user_zone@PathfindManager@@QAE_NAAUUserZone@1@@Z).
    //
    // Field meanings come from the one reader of the zone, the volume probe FUN_062db830
    // (UserZoneContainsPoint, item 1672), which reads it in place at PathfindManager+0x0C:
    //   Mode       0 = sphere at TargetPoint / Radius, 1 = oriented box, 2 = live target,
    //              anything else = no test;
    //   Invert     the probe's answer is flipped when non-zero (tested as a whole dword);
    //   TargetKey  an object id; mode 2 looks it up with Owner->ObjectDb->FindById(&TargetKey).
    // Its one builder, Server.dll pub::AI::SubmitState (.text:06d5c957..06d5cb56), fills
    // exactly these fields from SetZoneBehaviorParams (see server_pub.cpp).
    struct UserZone
    {
        // Not exported: every module compiles its own copy. Common's is FUN_062db740
        // (called on PathfindManager+0x24 == Zone+0x18 at .text:062db3da): the matrix at
        // +0x00..+0x20 of the record set to identity (+0x00/+0x10/+0x20 = 1.0f, the other
        // six words 0) and the Vector at +0x24..+0x2C zeroed -- i.e. BoxRotation = identity,
        // BoxCenter = 0. Server's copy is FUN_06d0a850 (identity matrix at S+0x6c) followed by
        // three inline zero stores to the Vector (.text:06d5c969..06d5c97f). No other field
        // is initialised.
        UserZone()
        {
            BoxRotation.E[0][0] = 1.0f;
            BoxRotation.E[0][1] = 0.0f;
            BoxRotation.E[0][2] = 0.0f;
            BoxRotation.E[1][0] = 0.0f;
            BoxRotation.E[1][1] = 1.0f;
            BoxRotation.E[1][2] = 0.0f;
            BoxRotation.E[2][0] = 0.0f;
            BoxRotation.E[2][1] = 0.0f;
            BoxRotation.E[2][2] = 1.0f;
            BoxCenter.x = 0.0f;
            BoxCenter.y = 0.0f;
            BoxCenter.z = 0.0f;
        }

        int Mode;                // +0x00  0 sphere / 1 box / 2 live target
        int Invert;              // +0x04  non-zero flips the probe result
        Vector TargetPoint;      // +0x08  mode 0 centre (mode 2 caches the live position here)
        float Radius;            // +0x14  sphere radius (modes 0 and 2)
        Matrix BoxRotation;      // +0x18  mode 1 box orientation
        Vector BoxCenter;        // +0x3C  mode 1 box centre
        Vector BoxHalfExtent;    // +0x48  mode 1 box half extents
        unsigned int TargetKey;  // +0x54  mode 2 object id (IObjDB::FindById(&TargetKey) via Owner+0xEC)
    };

    // FUN_062db3b0 (item 1667). Not exported.
    explicit PathfindManager(IBehaviorManager* NewOwner);

    // 0x062db820 ?clear_user_zone@PathfindManager@@QAEXXZ
    COMMON_API void clear_user_zone();
    // 0x062db800 ?submit_user_zone@PathfindManager@@QAEXABUUserZone@1@@Z
    COMMON_API void submit_user_zone(UserZone const& Zone);
    // 0x062c5ac0 ?get_user_zone@PathfindManager@@QAE_NAAUUserZone@1@@Z (non-const: QAE)
    COMMON_API bool get_user_zone(UserZone& OutZone);

    // FUN_062de160 (item 1689), not exported, fastcall on `this`, 16 instructions
    // (Common.dll.txt 229612-229629):
    //   AL = (byte [ECX+0x574] == 1);                        // Avoidance.HasContact
    //   if ([ECX+0x16c] != 3 && byte [ECX+0x180] != 0) AL = 1; // Reaction.State / bInReaction
    //   return AL == 0;                                       // SETZ
    // True while the obstacle avoidance has no contact and no obstacle reaction is running.
    // Callers: SystemManager's tick (.text:062e677e) stores it in ThrustManager::bCruiseUnobstructed,
    // the gate engage_cruise checks; .text:062d3c01 and .text:062fe296. Retail name unknown.
    bool IsCruiseUnobstructed() const
    {
        bool bObstructed = (Avoidance.HasContact == 1);
        if (Reaction.State != 3 && Reaction.bInReaction)
            bObstructed = true;
        return !bObstructed;
    }

    // FUN_062dd3d0 (item 1682), not exported, thiscall, `RET 8`, 108 instructions
    // (Common.dll.txt 228376-228515). Reached only through Behavior::SetCruiseLock
    // (FUN_062d21d0, 20 call sites across the Behavior family), which tail-jumps here with
    // this = Behavior+0x20 (IBehaviorManager) -> +0x0C. Only the low byte of Value is read
    // (`MOV CL,byte ptr [ESP+param_1]`). Mask bit 1 latches it into Field_254, bit 0 into
    // Field_24C; a zero Value then resets the +0x214..+0x243 record (after notifying the
    // ThrustManager with a zero vector when bit 0 was given and Field_214 bit 0 was set).
    // Retail name unknown; the port name follows the Behavior-side callers.
    void SetCruiseLock(int Value, unsigned int Mask);

    // FUN_062dd760, not exported, __thiscall, RET 4. The per-tick update; its one caller is
    // IBehaviorManager::UpdateBehaviorTick (.text:062d2d00) with the tick's Dt. Only for an
    // owner type in 0x7f0000: a 1 s zone-property probe (Field_000 / Field_004), a reset
    // of the obstacle state while behaviour 6 / 8 / 0x13 / -1 runs, the 0.1 s
    // RefreshAvoidance and the nudge hand-off to the ThrustManager, then (with Flag245 and
    // Field_254 set) UpdateShipHeadingBlend and the ObstacleAvoidance tick on its own period.
    void Update(float Dt);

    // FUN_062dd580, not exported, __thiscall, RET 0. Rebuilds the +0x214 record: a point
    // from SelectAvoidancePoint (Field_214 = 2), else for owner types in 0x130000 a nudge
    // direction from SelectObstacleDirection (Field_214 = 1, the obstacle watched in
    // Watcher218), else nothing (Field_214 = 0).
    void RefreshAvoidance();

    // FUN_062dda10, not exported, __thiscall, RET 0xc. 0 with *OutPoint / *OutScale /
    // *OutKind filled (kind 2 = user-zone exit, 3 = obstacle reaction, 1 = avoidance
    // point), 2 when nothing applies (*OutKind then 0 on the avoidance path).
    int SelectAvoidancePoint(Vector* OutPoint, float* OutScale, int* OutKind);

    // FUN_062de050, not exported, __thiscall, RET 0xc. 0 with a nudge direction and the
    // obstacle it avoids (Field_255 = 1, Field_258 = its id), 2 otherwise.
    int SelectObstacleDirection(Vector* OutDirection, IObjRW** OutObstacle, unsigned ExcludeId);

    // FUN_062db830 (item 1672), not exported, __thiscall, RET 8 (the second stack argument is
    // a float* the body never reads; dropped). Is ProbePoint inside the user zone? A zone that
    // is not active (bUserZoneActive clear) admits everything; Invert flips the answer.
    // Formerly the file-static AiVolumeProbeContainsPoint in the (since dissolved) common_stubs.cpp.
    bool UserZoneContainsPoint(Vector const* ProbePoint);

    // FUN_062ddcd0 (item 1686, also reached through the thunk 0x062de040), not exported,
    // __thiscall, RET 4. 0 with the unit direction from the owner towards the user zone (away
    // from it when Invert is set) in *OutDir, 2 when the zone is inactive or does not apply.
    // Formerly the free function AiVolumeProbeApproachDirection(This, OutDir).
    int GetUserZoneApproachDirection(Vector* OutDir);

    // formerly the free ClassifyShipObstaclePhase(Self) -- FUN_062dc840 (item 1681), not
    // exported, __thiscall. Resets Reaction, then picks the obstacle whose deflection capsule
    // (padded by 750 / 250 and Field_1FC) holds the ship and lies closest to the target from
    // the own scan list and a 20000-radius spatial query; commits a State 1 escape or asks
    // NudgeObstaclePathPoint for a deflection point (State 0). 4 = nothing reacts, 0 = a commit,
    // otherwise NudgeObstaclePathPoint's non-zero result. Definition: common_obstacleavoidance.cpp.
    int ClassifyObstaclePhase();

    // formerly the free UpdateShipHeadingBlend(Self, Dt) -- FUN_062dc590 (item 1680), RET 4.
    // Phase 0 latches Reaction.Heading into BlendStart and restarts the 4 s BlendTimer; phase 1,
    // an expired timer or a clear bBlendEnabled reset Reaction (State 3, Heading 0); otherwise
    // Reaction.Heading turns from BlendStart towards TargetPosition by the elapsed fraction of
    // the blend (snapping when within acos(0.999)), State 2. Definition:
    // common_obstacleavoidance.cpp.
    void UpdateHeadingBlend(float Dt);

    // The sub-record at +0x16C. Its ctor is inlined into PathfindManager's: the member at
    // record+0x1C gets the ICF-folded empty ctor 0x06261260 (`MOV EAX,ECX; RET`), then
    // FUN_062db7a0 (ResetState, item 1670) runs on the record. FUN_062db7a0 is also called
    // from .text:062dd87b. Field names follow common_aisteer.h's "Ship heading-blend
    // classifier pair" table, whose readers (ClassifyShipObstaclePhase,
    // UpdateShipHeadingBlend) address these bytes as PathfindManager+0x16C..+0x19F.
    struct ObstacleReaction
    {
        // FUN_062db7a0: State = 3, LastObstacleTag = 0, Heading = 0 (stored +0x10, +0x0C,
        // +0x08), bInReaction = 0, bCloseEncounter = 0. ActiveSlot is not touched.
        void ResetState();

        int State;                             // +0x00  (PathfindManager+0x16C) 3 = idle
        unsigned int LastObstacleTag;          // +0x04  (+0x170)
        Vector Heading;                        // +0x08  (+0x174)
        bool bInReaction;                      // +0x14  (+0x180)
        bool bCloseEncounter;                  // +0x15  (+0x181)
        unsigned char Pad16[2];
        ObstacleAvoidance::Slot ActiveSlot;    // +0x18  (+0x184) never written by the ctor
    };

    int Field_000;                             // +0x000 ctor: 0 (.text:062db54c)
    int Field_004;                             // +0x004 ctor: 0 (.text:062db644)
    bool bUserZoneActive;                      // +0x008 submit_user_zone sets, clear_user_zone clears
    unsigned char Pad009[3];
    UserZone Zone;                             // +0x00C
    int Field_064;                             // +0x064 ctor: 0 (.text:062db519)
    unsigned int NextDeflectionSlot;           // +0x068 round-robin cursor into DeflectionSlots
    unsigned int RecentObstacleIds[8];         // +0x06C ctor: REP STOSD 8 x 0 (.text:062db3f4)
    // Built with `eh vector constructor iterator(this+0x8c, 0x1c, 8, 0x06261260)`: the
    // element ctor is the ICF-folded empty body, so nothing in the array is initialised.
    ObstacleAvoidance::Slot DeflectionSlots[8]; // +0x08C
    ObstacleReaction Reaction;                 // +0x16C
    float BlendTimer;                          // +0x1A0 ctor: 0
    Vector BlendStart;                         // +0x1A4 ctor: (0, 1, 0) (.text:062db711-062db71d)
    bool bBlendEnabled;                        // +0x1B0 ctor: 0
    unsigned char Pad1B1[3];
    Vector TargetPosition;                     // +0x1B4 never written by the ctor
    IBehaviorManager* Owner;                   // +0x1C0
    unsigned int OwnerType;                    // +0x1C4 Owner object's IObjInspect::get_type (.text:062db57a)
    BaseWatcher Watcher1C8;                    // +0x1C8 destroyed by FUN_062d2530 (.text:062d259b)
    Vector Field_1D0;                          // +0x1D0 ctor: 0 (stored z, y, x)
    int Field_1DC;                             // +0x1DC ctor: 0
    float Timer1E0;                            // +0x1E0 ctor: rand() / 32768 * Period1E4
    float Period1E4;                           // +0x1E4 ctor: 1.0f
    float Timer1E8;                            // +0x1E8 ctor: rand() / 32768 * Period1EC
    float Period1EC;                           // +0x1EC ctor: 0.5f
    int Field_1F0;                             // +0x1F0 ctor: 0
    float MaxEngineSpeed;                      // +0x1F4 Owner get_speed(1.0f, Engine) (seeded 80.0f first)
    float Field_1F8;                           // +0x1F8 get_distance_travelled(0, MaxEngineSpeed, 1000, Engine) or 0
    float Field_1FC;                           // +0x1FC get_distance_travelled(0, 300, 1000, Engine) or 0
    Vector CachedTargetPosition;               // +0x200 ctor: 0; ClassifyShipObstaclePhase reads it
    float Timer20C;                            // +0x20C ctor: rand() / 32768 * Period210
    float Period210;                           // +0x210 ctor: 0.1f
    int Field_214;                             // +0x214 ctor: 0
    BaseWatcher Watcher218;                    // +0x218 ctor FUN_062de190(nullptr); destroyed by FUN_062d2530 (.text:062d2580)
    Vector Field_220;                          // +0x220 ctor: 0 (stored z, y, x)
    Vector Field_22C;                          // +0x22C ctor: 0 (stored z, y, x)
    unsigned char Field_238;                   // +0x238 ctor: 0; RefreshAvoidance copies Avoidance.HasContact's raw byte
    unsigned char Pad239[3];
    float Field_23C;                           // +0x23C ctor: 1.0f
    int Field_240;                             // +0x240 ctor: 0
    bool Field_244;                            // +0x244 ctor: 0
    // +0x245: pub::AI::SubmitState's SetFlagParams FlagId 0 stores the raw uint8_t there
    // (Server .text:06d5c93c); Common tests it as a byte (TEST AL,AL at .text:062dd937,
    // 062dda17, 062de058). ctor: 1.
    unsigned char Flag245;                     // +0x245
    unsigned char Pad246[2];
    int Field_248;                             // +0x248 ctor: -1
    unsigned char Field_24C;                   // +0x24C ctor: 1; SetCruiseLock stores a raw byte
    unsigned char Pad24D[3];
    int Field_250;                             // +0x250 ctor: 0x7f0000
    unsigned char Field_254;                   // +0x254 ctor: 1; SetCruiseLock stores a raw byte
    bool Field_255;                            // +0x255 ctor: 0
    unsigned char Pad256[2];
    int Field_258;                             // +0x258 ctor: 0
    int Field_25C;                             // +0x25C ctor: 0
    unsigned int CachedObstacleId;             // +0x260 ctor: 0
    bool bHasCachedObstacle;                   // +0x264 ctor: 0
    unsigned char Pad265[3];
    ObstacleAvoidance Avoidance;               // +0x268 (0x318 bytes -- ends exactly at 0x580)
};

static_assert(offsetof(PathfindManager::UserZone, Invert) == 0x04, "UserZone+0x04 Invert");
static_assert(offsetof(PathfindManager::UserZone, TargetPoint) == 0x08, "UserZone+0x08 TargetPoint");
static_assert(offsetof(PathfindManager::UserZone, Radius) == 0x14, "UserZone+0x14 Radius");
static_assert(offsetof(PathfindManager::UserZone, BoxRotation) == 0x18, "UserZone+0x18 BoxRotation");
static_assert(offsetof(PathfindManager::UserZone, BoxCenter) == 0x3C, "UserZone+0x3C BoxCenter");
static_assert(offsetof(PathfindManager::UserZone, BoxHalfExtent) == 0x48, "UserZone+0x48 BoxHalfExtent");
static_assert(offsetof(PathfindManager::UserZone, TargetKey) == 0x54, "UserZone+0x54 TargetKey");
static_assert(sizeof(PathfindManager::UserZone) == 0x58, "UserZone sizeof 0x58 (22-dword copy)");

static_assert(offsetof(PathfindManager::ObstacleReaction, Heading) == 0x08, "ObstacleReaction+0x08 Heading");
static_assert(offsetof(PathfindManager::ObstacleReaction, bInReaction) == 0x14, "ObstacleReaction+0x14");
static_assert(offsetof(PathfindManager::ObstacleReaction, ActiveSlot) == 0x18, "ObstacleReaction+0x18 ActiveSlot");
static_assert(sizeof(PathfindManager::ObstacleReaction) == 0x34, "ObstacleReaction sizeof 0x34");

static_assert(offsetof(PathfindManager, bUserZoneActive) == 0x008, "PathfindManager+0x008 bUserZoneActive");
static_assert(offsetof(PathfindManager, Zone) == 0x00C, "PathfindManager+0x00C Zone");
static_assert(offsetof(PathfindManager, Field_064) == 0x064, "PathfindManager+0x064");
static_assert(offsetof(PathfindManager, NextDeflectionSlot) == 0x068, "PathfindManager+0x068");
static_assert(offsetof(PathfindManager, RecentObstacleIds) == 0x06C, "PathfindManager+0x06C");
static_assert(offsetof(PathfindManager, DeflectionSlots) == 0x08C, "PathfindManager+0x08C");
static_assert(offsetof(PathfindManager, Reaction) == 0x16C, "PathfindManager+0x16C");
static_assert(offsetof(PathfindManager, BlendTimer) == 0x1A0, "PathfindManager+0x1A0");
static_assert(offsetof(PathfindManager, BlendStart) == 0x1A4, "PathfindManager+0x1A4");
static_assert(offsetof(PathfindManager, bBlendEnabled) == 0x1B0, "PathfindManager+0x1B0");
static_assert(offsetof(PathfindManager, TargetPosition) == 0x1B4, "PathfindManager+0x1B4");
static_assert(offsetof(PathfindManager, Owner) == 0x1C0, "PathfindManager+0x1C0 Owner");
static_assert(offsetof(PathfindManager, OwnerType) == 0x1C4, "PathfindManager+0x1C4");
static_assert(offsetof(PathfindManager, Watcher1C8) == 0x1C8, "PathfindManager+0x1C8");
static_assert(offsetof(PathfindManager, Field_1D0) == 0x1D0, "PathfindManager+0x1D0");
static_assert(offsetof(PathfindManager, Timer1E0) == 0x1E0, "PathfindManager+0x1E0");
static_assert(offsetof(PathfindManager, MaxEngineSpeed) == 0x1F4, "PathfindManager+0x1F4");
static_assert(offsetof(PathfindManager, CachedTargetPosition) == 0x200, "PathfindManager+0x200");
static_assert(offsetof(PathfindManager, Timer20C) == 0x20C, "PathfindManager+0x20C");
static_assert(offsetof(PathfindManager, Field_214) == 0x214, "PathfindManager+0x214");
static_assert(offsetof(PathfindManager, Watcher218) == 0x218, "PathfindManager+0x218");
static_assert(offsetof(PathfindManager, Field_238) == 0x238, "PathfindManager+0x238");
static_assert(offsetof(PathfindManager, Field_244) == 0x244, "PathfindManager+0x244");
static_assert(offsetof(PathfindManager, Flag245) == 0x245, "PathfindManager+0x245");
static_assert(offsetof(PathfindManager, Field_248) == 0x248, "PathfindManager+0x248");
static_assert(offsetof(PathfindManager, Field_250) == 0x250, "PathfindManager+0x250");
static_assert(offsetof(PathfindManager, Field_255) == 0x255, "PathfindManager+0x255");
static_assert(offsetof(PathfindManager, CachedObstacleId) == 0x260, "PathfindManager+0x260");
static_assert(offsetof(PathfindManager, bHasCachedObstacle) == 0x264, "PathfindManager+0x264");
static_assert(offsetof(PathfindManager, Avoidance) == 0x268, "PathfindManager+0x268 Avoidance");
static_assert(offsetof(PathfindManager, Avoidance) + offsetof(ObstacleAvoidance, HasContact) == 0x574,
              "PathfindManager+0x574 Avoidance.HasContact (FUN_062de160)");
static_assert(sizeof(PathfindManager) == 0x580,
    "PathfindManager must match retail's operator_new(0x580) at .text:062d2327");

// FUN_062db7c0 (sole caller FUN_062d2830 at .text:062d2879), thiscall on the
// PathfindManager, `RET 4`. With a database: Avoidance.PhysSystemId (+0x26C) =
// ObjectDb->GetZoneDb()->slot 0() -- retail makes the GetZoneDb / slot-0 pair twice and keeps
// the second result (.text:062db7cc-062db7e6). Without one: PhysSystemId = 0. The zone
// database's type has no published interface; its slot 0 is the PhySys system id getter the
// launcher line-of-fire probe also calls (common_aifirecontrol.cpp).
struct IObjDB;
void SetPathfindObjectDb(PathfindManager* Self, IObjDB* ObjectDb);
