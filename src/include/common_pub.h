// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"

// Common.dll PUB batch — pub::AI namespace types and interfaces.
// Contract layer used across DLL boundaries (Content/Server/RPC).

#include "common_ini.h" // Vector
#include "common_csys.h" // Matrix (DirectiveFaceOp)
#include <cstddef>
#include <cstdint>

typedef unsigned int uint;

// FORMATION_RTYPE is a GLOBAL enum in retail, not a pub::AI one. Both its users
// mangle it without a scope:
//   Common  ?add_formation_follower@CShip@@QAE?AW4FORMATION_RTYPE@@PAUIObjRW@@@Z
//   Server  ?update_formation_state@AI@pub@@YA?AW4FORMATION_RTYPE@@IIABVVector@@@Z
//   Server  ?FormationResponse@SpaceObj@pub@@YAHABIW4FORMATION_RTYPE@@@Z
// It used to be declared twice: here inside pub::AI (FormRType0..2, invented
// values) and globally in common_cequip.h. Enumerators leave no symbols, so the
// NAMES are the port's, chosen from what each value means at the call site; the
// VALUES are the ones CShip::add/remove_formation_follower actually return.
enum FORMATION_RTYPE
{
    FORMATION_RTYPE_OK = 0,
    FORMATION_RTYPE_SELF = -1,
    FORMATION_RTYPE_NOT_LEADER = -2,
    FORMATION_RTYPE_NOT_FOUND = -3
};

namespace pub { namespace AI {

// ============================================================================
// Enums
// ============================================================================

// OP_TYPE — real values recovered (2026-09-05) from every BaseOp-derived
// class's own default ctor in Common.dll.c, each of which sets `this+4` to a
// distinct literal matching the class it belongs to (e.g.
// DirectiveGotoOp::DirectiveGotoOp @ 0x7AE20 sets `this+4 = 3`). All 22
// literals were found (0-21, no gaps), one per BaseOp-derived class that
// exists in this header. The previous 3-value enum (OpType0/1/2) was
// fabricated.
enum OP_TYPE
{
    OpType_SetPersonalityParams          = 0,
    OpType_SetZoneBehaviorParams         = 1,
    OpType_SetFlagParams                 = 2,
    OpType_DirectiveGotoOp                = 3,
    OpType_DirectiveFollowOp              = 4,
    OpType_DirectiveTrailOp               = 5,
    OpType_DirectiveStrafeOp              = 6,
    OpType_DirectiveFaceOp                = 7,
    OpType_DirectiveRamOp                 = 8,
    OpType_DirectiveTractorBeamOp         = 9,
    OpType_DirectiveEvadeOp               = 10,
    OpType_DirectiveDrasticEvadeOp        = 11,
    OpType_DirectiveDelayOp               = 12,
    OpType_DirectiveDockOp                = 13,
    OpType_DirectiveLaunchOp              = 14,
    OpType_DirectiveInstantTradelaneOp    = 15,
    OpType_DirectiveFormationOp           = 16,
    OpType_DirectiveGuideOp               = 17,
    OpType_DirectiveWaitForShip           = 18,
    OpType_DirectiveWaitForPlayerManeuverOp = 19,
    OpType_DirectiveIdleOp                = 20,
    OpType_DirectiveCancelOp              = 21,
};

// OP_RTYPE: retail carries no enumerator names (only the mangled enum name
// W4OP_RTYPE@AI@pub@@), and no module in EXE\Disasm consumes the values --
// SubmitState / SubmitDirective / set_directive_priority have no callers in any
// listing (their consumer is Content.dll). The names below are therefore the
// port's, chosen by what each literal means at the retail return sites:
//   0  Server SubmitState: personality applied (.text:06d5cba5 path) / user
//      zone cleared (mode 2).
//   1  Server SubmitDirective: directive accepted -- forwarded to the player's
//      client or set on the AI (06d5d43b..06d5d754); Common
//      IBehaviorManager::set_directive_priority (0x062d3e80): priority stored on
//      the current directive. NOT a failure: this value used to be named
//      OpRTypeFailed, which inverted its meaning.
//   2  Server SubmitDirective: the AI already has a current directive
//      (06d5d471, word 0 of get_current_directive() != 0).
//   3  Common set_directive_priority: there is no current directive to change.
//   4  bad id in both Server functions, and every SubmitState zone / flag /
//      unknown-op exit -- mixed meaning, so no name is given to it.
//   5  SubmitDirective: op failed validate(), no object, not a CSimple, or no
//      behavior for this op.
enum OP_RTYPE
{
    OpRTypeSuccess = 0,
    OpRTypeAccepted = 1,
    OpRTypeDirectiveBusy = 2,
    OpRTypeNoDirective = 3,
    OpRTypeInvalid = 5,
};

enum ScanResponse
{
    ScanRespNone = 0,
    ScanRespFriendly = 1,
    ScanRespHostile = 2,
    ScanRespNeutral = 3,
};

enum DirectivePriority
{
    DirPriNone = 0,
    DirPriLow = 1,
    DirPriNormal = 2,
    DirPriHigh = 3,
    DirPriCritical = 4,
};

// The eight ContentCallback enums (CSNType, FollowStatusType, ...) are nested in
// pub::AI::ContentCallback below: retail mangles them `W4CSNType@123@`, i.e.
// CSNType in ContentCallback in AI in pub.

// ============================================================================
// Forward declarations for nested Personality structs
// ============================================================================

class BaseOp;
struct ContentCallback;
struct DirectiveCallback;

// ============================================================================
// GSplinePath — spline path for AI navigation
// ============================================================================

// Retail exports exactly two members:
//   ??0GSplinePath@AI@pub@@QAE@XZ            .text:06261260  `MOV EAX,this; RET` -- ICF-folded
//                                            with a dozen trivial getters: the default ctor
//                                            initialises NOTHING.
//   ??4GSplinePath@AI@pub@@QAEAAU012@ABU012@@Z .text:06274100 `MOV ECX,0xc; REP MOVSD` --
//                                            ICF-folded with Csys::operator=: a flat copy
//                                            of 12 dwords, so sizeof == 0x30.
// No copy constructor is exported (the implicit trivial one). The previous port was a
// fabricated 0x0C struct {Points, NumPoints, Length} with a zeroing ctor. Both retail
// bodies are ICF-shared, so their call sites cannot be attributed to GSplinePath, no
// retail module imports either export, and no port code names the type: the content of
// the 0x30 bytes is not recovered and stays opaque. Open TODO: field layout.
struct COMMON_API GSplinePath
{
    uint32_t Opaque[12];   // +0x00 .. +0x2F  meaning not recovered, see above

    GSplinePath();
    GSplinePath& operator=(GSplinePath const& Other);
};

static_assert(sizeof(GSplinePath) == 0x30, "GSplinePath size (operator= copies 0xC dwords)");

// ============================================================================
// Personality — AI personality configuration
// ============================================================================
//
// RE NOTE (2026-09-05): the struct family below was previously fabricated
// (plausible-looking field names/sizes invented, not reverse-engineered).
// It has been rebuilt byte-for-byte from the real retail constructors in
// Common.dll.c: each nested Personality::*Struct's default ctor was
// transcribed directly, and the outer Personality::Personality() ctor
// (Common.dll.c 0x7AA30) was used to pattern-match which sub-struct occupies
// which byte range (several sub-structs are inlined directly into the outer
// ctor rather than called out separately; their exact byte sequences were
// matched 1:1 against their own standalone ctors to fix their offsets).
// pub::AI::Personality::operator= (Common.dll.c 0x488b0) does a flat copy of
// 0xF6 dwords (0x3D8 bytes), which is the authoritative total size and was
// used to confirm there is zero slack/undiscovered space anywhere in the
// struct. There is no faction-attitude table, no toughness-ratio table, and
// no standalone AggressionLevel/CautionLevel/ScanChance/FactionId/
// RangeThreshold fields anywhere in retail's Personality — those were all
// invented. Field names below are the INI keys Content.dll's pilot-personality
// loaders store at each offset (see the per-struct comments); the few offsets no
// loader key writes stay offset-based (Field_XX) with a TODO.


// Personality — Common.dll.c 0x7AA30 (pub::AI::Personality::Personality).
// Real layout is exactly these 16 sub-structs back to back, in this order,
// with no gaps and no additional top-level scalar fields (confirmed by
// Personality::operator= at Common.dll.c 0x488B0, which flat-copies 0xF6
// dwords = 0x3D8 bytes = the sum of all 16 sub-struct sizes below).
//
// A CLASS in retail, not a struct: every export that names it mangles it `V`
//   Common  ??4Personality@AI@pub@@QAEAAV012@ABV012@@Z
//   Common  ?get_personality@IBehaviorManager@@QAEXAAVPersonality@AI@pub@@@Z
//   Server  ?get_personality@AI@pub@@YA_NIAAVPersonality@12@@Z
// (its sixteen nested *Struct members are structs, `U`). Retail exports no copy
// constructor for it -- the copy is the implicit bitwise one -- so the port
// declares none either; operator= stays user-declared because retail exports it
// (0x488B0, a flat 0xF6-dword copy).
class COMMON_API Personality
{
public:
    // Nested in retail: every export names them `...@Personality@AI@pub@@`, e.g.
    // ??0GunUseStruct@Personality@AI@pub@@QAE@XZ, and get_toughness_ratio takes
    // `W4TOUGHNESS_TYPE@123@` -- TOUGHNESS_TYPE in Personality in AI in pub.
    enum TOUGHNESS_TYPE
    {
        ToughnessDefault = 0,
        ToughnessHull = 1,
        ToughnessShield = 2,
        ToughnessMax = 3,
    };


    // Field names of the fourteen *UseStruct / *ReactionStruct members below are the
    // [<Name>Block] keys of Content's pilot-personality loaders (FUN_06f3a2d0 ..
    // FUN_06f3bb00, CountermeasureBlock inline at 06f3db9e) that store to each offset.
    // Angles are stored in radians (the loader multiplies the INI degrees by GDegToRad)
    // unless noted. "*Weight<Dir>" fields are the per-name weights of one
    // "<key>, <NAME>, <weight>" key. The former Field_NN offset names follow each.

    // EvadeDodgeUseStruct — Common.dll.c 0x7A5B0; [EvadeDodgeBlock] (Content 06f3a2d0)
    struct COMMON_API EvadeDodgeUseStruct
    {
        float   EvadeActivateRange;                      // +0x00  500.0   formerly Field_00
        float   EvadeDodgeStyleWeightWaggle;             // +0x04  0.0     formerly Field_04
        float   EvadeDodgeStyleWeightWaggleRandom;       // +0x08  0.0     formerly Field_08
        float   EvadeDodgeStyleWeightSlide;              // +0x0C  0.0     formerly Field_0C
        float   EvadeDodgeStyleWeightCorkscrew;          // +0x10  0.0     formerly Field_10
        float   EvadeDodgeConeAngle;                     // +0x14  pi/4    formerly Field_14
        float   EvadeDodgeConeAngleVariancePercent;      // +0x18  0.0     formerly Field_18
        float   EvadeDodgeWaggleAxisConeAngle;           // +0x1C  0.0     formerly Field_1C
        float   EvadeDodgeRollAngle;                     // +0x20  0.0     formerly Field_20
        float   EvadeDodgeIntervalTime;                  // +0x24  1.0     formerly Field_24
        float   EvadeDodgeIntervalTimeVariancePercent;   // +0x28  0.0     formerly Field_28
        float   EvadeDodgeDistance;                      // +0x2C  100.0   formerly Field_2C
        float   EvadeDodgeTime;                          // +0x30  10.0    formerly Field_30
        float   EvadeDodgeSlideThrottle;                 // +0x34  0.6     formerly Field_34
        float   EvadeDodgeTurnThrottle;                  // +0x38  0.6     formerly Field_38
        float   EvadeDodgeCorkscrewTurnThrottle;         // +0x3C  0.15    formerly Field_3C
        float   EvadeDodgeCorkscrewRollThrottle;         // +0x40  1.0     formerly Field_40
        uint8_t bEvadeDodgeCorkscrewRollFlipDirection;   // +0x44  0       formerly Field_44
        // +0x45..+0x47 alignment padding: the ctor never writes it, operator= copies it.
        float   EvadeDodgeDirectionWeightLeft;           // +0x48  0.0     formerly Field_48
        float   EvadeDodgeDirectionWeightRight;          // +0x4C  0.0     formerly Field_4C
        float   EvadeDodgeDirectionWeightUp;             // +0x50  0.0     formerly Field_50
        float   EvadeDodgeDirectionWeightDown;           // +0x54  0.0     formerly Field_54

        EvadeDodgeUseStruct();
        EvadeDodgeUseStruct& operator=(EvadeDodgeUseStruct const& Other);
    };

    static_assert(offsetof(EvadeDodgeUseStruct, bEvadeDodgeCorkscrewRollFlipDirection) == 0x44, "EvadeDodgeUseStruct +0x44");
    static_assert(offsetof(EvadeDodgeUseStruct, EvadeDodgeDirectionWeightDown) == 0x54, "EvadeDodgeUseStruct +0x54");
    static_assert(sizeof(EvadeDodgeUseStruct) == 0x58, "EvadeDodgeUseStruct size");

    // EvadeBreakUseStruct — Common.dll.c 0x7A620; also inlined at Personality +0x58;
    // [EvadeBreakBlock] (Content 06f3a690)
    struct COMMON_API EvadeBreakUseStruct
    {
        float EvadeBreakTime;                             // +0x00  3.0       formerly Field_00
        float EvadeBreakIntervalTime;                     // +0x04  100000.0  formerly Field_04
        float EvadeBreakAfterburnerDelay;                 // +0x08  0.0       formerly Field_08
        float EvadeBreakAfterburnerDelayVariancePercent;  // +0x0C  0.0       formerly Field_0C
        float EvadeBreakDirectionWeightLeft;              // +0x10  0.0       formerly Field_10
        float EvadeBreakDirectionWeightRight;             // +0x14  0.0       formerly Field_14
        float EvadeBreakDirectionWeightUp;                // +0x18  0.0       formerly Field_18
        float EvadeBreakDirectionWeightDown;              // +0x1C  0.0       formerly Field_1C
        float EvadeBreakRollThrottle;                     // +0x20  0.0       formerly Field_20
        float EvadeBreakTurnThrottle;                     // +0x24  1.0       formerly Field_24
        float EvadeBreakStyleWeightSideways;              // +0x28  0.0       formerly Field_28
        float EvadeBreakStyleWeightOutrun;                // +0x2C  0.0       formerly Field_2C
        float EvadeBreakStyleWeightReverse;               // +0x30  0.0       formerly Field_30
        float EvadeBreakAttemptReverseTime;               // +0x34  4.0       formerly Field_34
        float EvadeBreakReverseDistance;                  // +0x38  150.0     formerly Field_38

        EvadeBreakUseStruct();
        EvadeBreakUseStruct& operator=(EvadeBreakUseStruct const& Other);
    };

    static_assert(sizeof(EvadeBreakUseStruct) == 0x3C, "EvadeBreakUseStruct size");

    // BuzzHeadTowardUseStruct — Common.dll.c 0x7A670; also inlined at Personality +0x94;
    // [BuzzHeadTowardBlock] (Content 06f3a940)
    struct COMMON_API BuzzHeadTowardUseStruct
    {
        float   BuzzMaxTimeToHeadAway;                       // +0x00  4.0    formerly Field_00
        float   BuzzHeadTowardStyleWeightStraightTo;         // +0x04  0.0    formerly Field_04
        float   BuzzHeadTowardStyleWeightSlide;              // +0x08  0.0    formerly Field_08
        float   BuzzHeadTowardStyleWeightWaggle;             // +0x0C  0.0    formerly Field_0C
        float   BuzzMinDistanceToHeadToward;                 // +0x10  200.0  formerly Field_10
        float   BuzzMinDistanceToHeadTowardVariancePercent;  // +0x14  1.0    formerly Field_14
        float   BuzzHeadTowardEngineThrottle;                // +0x18  1.0    formerly Field_18
        float   BuzzHeadTowardTurnThrottle;                  // +0x1C  1.0    formerly Field_1C
        float   BuzzHeadTowardRollThrottle;                  // +0x20  0.0    formerly Field_20
        uint8_t bBuzzHeadTowardRollFlipDirection;            // +0x24  0      formerly Field_24
        // +0x25..+0x27 alignment padding
        float   BuzzDodgeDirectionWeightLeft;                // +0x28  0.0    formerly Field_28
        float   BuzzDodgeDirectionWeightRight;               // +0x2C  0.0    formerly Field_2C
        float   BuzzDodgeDirectionWeightUp;                  // +0x30  0.0    formerly Field_30
        float   BuzzDodgeDirectionWeightDown;                // +0x34  0.0    formerly Field_34
        float   BuzzDodgeTurnThrottle;                       // +0x38  1.0    formerly Field_38
        float   BuzzDodgeConeAngle;                          // +0x3C  pi/4   formerly Field_3C
        float   BuzzDodgeConeAngleVariancePercent;           // +0x40  0.0    formerly Field_40
        float   BuzzDodgeWaggleAxisConeAngle;                // +0x44  0.0    formerly Field_44
        float   BuzzDodgeRollAngle;                          // +0x48  0.0    formerly Field_48
        float   BuzzDodgeIntervalTime;                       // +0x4C  2.0    formerly Field_4C
        float   BuzzDodgeIntervalTimeVariancePercent;        // +0x50  0.0    formerly Field_50
        float   BuzzSlideThrottle;                           // +0x54  1.0    formerly Field_54
        float   BuzzSlideIntervalTime;                       // +0x58  2.0    formerly Field_58
        float   BuzzSlideIntervalTimeVariancePercent;        // +0x5C  0.4    formerly Field_5C

        BuzzHeadTowardUseStruct();
        BuzzHeadTowardUseStruct& operator=(BuzzHeadTowardUseStruct const& Other);
    };

    static_assert(offsetof(BuzzHeadTowardUseStruct, bBuzzHeadTowardRollFlipDirection) == 0x24, "BuzzHeadTowardUseStruct +0x24");
    static_assert(sizeof(BuzzHeadTowardUseStruct) == 0x60, "BuzzHeadTowardUseStruct size");

    // BuzzPassByUseStruct — Common.dll.c 0x7A6E0; also inlined at Personality +0xF4;
    // [BuzzPassByBlock] (Content 06f3ad40)
    struct COMMON_API BuzzPassByUseStruct
    {
        float   BuzzPassByStyleWeightStraightBy;   // +0x00  0.0    formerly Field_00
        float   BuzzPassByStyleWeightBreakAway;    // +0x04  0.0    formerly Field_04
        float   BuzzPassByStyleWeightEngineKill;   // +0x08  0.0    formerly Field_08
        float   BuzzDistanceToPassBy;              // +0x0C  100.0  formerly Field_0C
        float   BuzzPassByTime;                    // +0x10  3.0    formerly Field_10
        uint8_t bBuzzDropBombOnPassBy;             // +0x14  0      formerly Field_14
        // +0x15..+0x17 alignment padding
        float   BuzzBreakDirectionWeightLeft;      // +0x18  0.0    formerly Field_18
        float   BuzzBreakDirectionWeightRight;     // +0x1C  0.0    formerly Field_1C
        float   BuzzBreakDirectionWeightUp;        // +0x20  0.0    formerly Field_20
        float   BuzzBreakDirectionWeightDown;      // +0x24  0.0    formerly Field_24
        float   BuzzBreakDirectionConeAngle;       // +0x28  0.0    INI value as is (no GDegToRad); formerly Field_28
        float   BuzzBreakTurnThrottle;             // +0x2C  1.0    formerly Field_2C
        float   BuzzPassByRollThrottle;            // +0x30  0.0    formerly Field_30

        BuzzPassByUseStruct();
        BuzzPassByUseStruct& operator=(BuzzPassByUseStruct const& Other);
    };

    static_assert(offsetof(BuzzPassByUseStruct, bBuzzDropBombOnPassBy) == 0x14, "BuzzPassByUseStruct +0x14");
    static_assert(sizeof(BuzzPassByUseStruct) == 0x34, "BuzzPassByUseStruct size");

    // TrailUseStruct — Common.dll.c 0x7A720; also inlined at Personality +0x128;
    // [TrailBlock] (Content 06f3b030)
    struct COMMON_API TrailUseStruct
    {
        float   TrailLockConeAngle;       // +0x00  pi/6   formerly Field_00
        float   TrailBreakTime;           // +0x04  2.5    formerly Field_04
        float   TrailMinNoLockTime;       // +0x08  4.0    formerly Field_08
        float   TrailBreakRollThrottle;   // +0x0C  0.0    formerly Field_0C
        uint8_t bTrailBreakAfterburner;   // +0x10  0      formerly Field_10
        // +0x11..+0x13 alignment padding
        float   TrailMaxTurnThrottle;     // +0x14  1.0    formerly Field_14
        float   TrailDistance;            // +0x18  150.0  formerly Field_18

        TrailUseStruct();
        TrailUseStruct& operator=(TrailUseStruct const& Other);
    };

    static_assert(offsetof(TrailUseStruct, bTrailBreakAfterburner) == 0x10, "TrailUseStruct +0x10");
    static_assert(sizeof(TrailUseStruct) == 0x1C, "TrailUseStruct size");

    // StrafeUseStruct — Common.dll.c 0x7A750; also inlined at Personality +0x144;
    // [StrafeBlock] (Content 06f3b190)
    struct COMMON_API StrafeUseStruct
    {
        float StrafeRunAwayDistance;   // +0x00  150.0  formerly Field_00
        float StrafeAttackThrottle;    // +0x04  1.0    formerly Field_04
        float StrafeTurnThrottle;      // +0x08  1.0    formerly Field_08

        StrafeUseStruct();
        StrafeUseStruct& operator=(StrafeUseStruct const& Other);
    };

    static_assert(sizeof(StrafeUseStruct) == 0x0C, "StrafeUseStruct size");

    // EngineKillUseStruct — Common.dll.c 0x7A770; also inlined at Personality +0x150;
    // [EngineKillBlock] (Content 06f3b240)
    struct COMMON_API EngineKillUseStruct
    {
        float EngineKillSearchTime;          // +0x00  6.0    formerly Field_00
        float EngineKillFaceTime;            // +0x04  3.0    formerly Field_04
        float EngineKillUseAfterburner;      // +0x08  0.0    TRUE/FALSE stored as 1.0f/0.0f (FILD); formerly Field_08
        float EngineKillAfterburnerTime;     // +0x0C  0.5    formerly Field_0C
        float EngineKillMaxTargetDistance;   // +0x10  700.0  formerly Field_10

        EngineKillUseStruct();
        EngineKillUseStruct& operator=(EngineKillUseStruct const& Other);
    };

    static_assert(sizeof(EngineKillUseStruct) == 0x14, "EngineKillUseStruct size");

    // RepairUseStruct — Common.dll.c 0x7A7A0; also inlined at Personality +0x164;
    // [RepairBlock] (Content 06f3b370)
    struct COMMON_API RepairUseStruct
    {
        float UseShieldRepairPreDelay;          // +0x00  2.0  formerly Field_00
        float UseShieldRepairAtDamagePercent;   // +0x04  0.5  formerly Field_04
        float UseShieldRepairPostDelay;         // +0x08  3.0  formerly Field_08
        float UseHullRepairPreDelay;            // +0x0C  1.0  formerly Field_0C
        float UseHullRepairAtDamagePercent;     // +0x10  0.5  formerly Field_10
        float UseHullRepairPostDelay;           // +0x14  1.0  formerly Field_14

        RepairUseStruct();
        RepairUseStruct& operator=(RepairUseStruct const& Other);
    };

    static_assert(sizeof(RepairUseStruct) == 0x18, "RepairUseStruct size");

    // GunUseStruct — Common.dll.c 0x7A7D0; also inlined at Personality +0x17C;
    // [GunBlock] (Content 06f3b470)
    struct COMMON_API GunUseStruct
    {
        int32_t FireStyle;                                // +0x00  0    "fire_style": 0 MULTIPLE, 1 anything else; formerly Field_00
        float   GunFireIntervalTime;                      // +0x04  0.0  formerly Field_04
        float   GunFireIntervalVariancePercent;           // +0x08  0.5  formerly Field_08
        float   GunFireBurstIntervalTime;                 // +0x0C  1.5  formerly Field_0C
        float   GunFireBurstIntervalVariancePercent;      // +0x10  0.1  formerly Field_10
        float   GunFireNoBurstIntervalTime;               // +0x14  0.1  formerly Field_14
        float   GunFireAccuracyConeAngle;                 // +0x18  0.0  formerly Field_18
        float   GunFireAccuracyPower;                     // +0x1C  1.0  formerly Field_1C
        float   GunFireAccuracyPowerNpc;                  // +0x20  6.0  formerly Field_20
        float   GunRangeThreshold;                        // +0x24  1.0  formerly Field_24
        float   GunRangeThresholdVariancePercent;         // +0x28  0.2  formerly Field_28
        float   GunTargetPointSwitchTime;                 // +0x2C  1.5  formerly Field_2C
        float   AutoTurretIntervalTime;                   // +0x30  0.0  formerly Field_30
        float   AutoTurretBurstIntervalTime;              // +0x34  1.0  formerly Field_34
        float   AutoTurretNoBurstIntervalTime;            // +0x38  0.0  formerly Field_38
        float   AutoTurretBurstIntervalVariancePercent;   // +0x3C  0.1  formerly Field_3C

        GunUseStruct();
        GunUseStruct& operator=(GunUseStruct const& Other);
    };

    static_assert(sizeof(GunUseStruct) == 0x40, "GunUseStruct size");

    // MineUseStruct — Common.dll.c 0x7A830; also inlined at Personality +0x1BC;
    // [MineBlock] (Content 06f3b6c0)
    struct COMMON_API MineUseStruct
    {
        float MineLaunchInterval;    // +0x00  0.0    formerly Field_00
        float MineLaunchConeAngle;   // +0x04  pi/8   formerly Field_04
        float MineLaunchRange;       // +0x08  400.0  formerly Field_08

        MineUseStruct();
        MineUseStruct& operator=(MineUseStruct const& Other);
    };

    static_assert(sizeof(MineUseStruct) == 0x0C, "MineUseStruct size");

    // MissileUseStruct — Common.dll.c 0x7A850; also inlined at Personality +0x1C8;
    // [MissileBlock] (Content 06f3b770)
    struct COMMON_API MissileUseStruct
    {
        float   MissileLaunchRange;                     // +0x00  -1.0    formerly Field_00
        uint8_t bMissileLaunchAllowOutOfRange;          // +0x04  0       formerly Field_04
        // +0x05..+0x07 alignment padding
        float   MissileLaunchIntervalTime;              // +0x08  5.0     formerly Field_08
        float   MissileLaunchIntervalVariancePercent;   // +0x0C  0.4     formerly Field_0C
        float   MissileLaunchConeAngle;                 // +0x10  pi/8    formerly Field_10
        float   AntiCruiseMissileMinDistance;           // +0x14  150.0   formerly Field_14
        float   AntiCruiseMissileMaxDistance;           // +0x18  2500.0  formerly Field_18
        float   AntiCruiseMissilePreFireDelay;          // +0x1C  2.0     formerly Field_1C
        float   AntiCruiseMissileIntervalTime;          // +0x20  5.0     formerly Field_20

        MissileUseStruct();
        MissileUseStruct& operator=(MissileUseStruct const& Other);
    };

    static_assert(offsetof(MissileUseStruct, bMissileLaunchAllowOutOfRange) == 0x04, "MissileUseStruct +0x04");
    static_assert(sizeof(MissileUseStruct) == 0x24, "MissileUseStruct size");

    // DamageReactionStruct — Common.dll.c 0x7A890; also inlined at Personality +0x1EC;
    // [DamageReactionBlock] (Content 06f3b900). operator= (Common.dll.c 0x48850) copies
    // 0x11 dwords (0x44 bytes); the default ctor only ever writes the first 0xF (15) of
    // them, so Field_3C/Field_40 are left uninitialized by retail. Zero-initialized here
    // instead. No [DamageReactionBlock] key writes +0x3C/+0x40 (TODO: meaning).
    struct COMMON_API DamageReactionStruct
    {
        float EvadeBreakDamageTriggerPercent;       // +0x00  1.0  formerly Field_00
        float EvadeDodgeMoreDamageTriggerPercent;   // +0x04  0.2  formerly Field_04
        float DropMinesDamageTriggerPercent;        // +0x08  1.0  formerly Field_08
        float DropMinesDamageTriggerTime;           // +0x0C  1.0  formerly Field_0C
        float EngineKillFaceDamageTriggerPercent;   // +0x10  1.0  formerly Field_10
        float EngineKillFaceDamageTriggerTime;      // +0x14  2.0  formerly Field_14
        float RollDamageTriggerPercent;             // +0x18  1.0  formerly Field_18
        float RollDamageTriggerTime;                // +0x1C  1.0  formerly Field_1C
        float AfterburnerDamageTriggerPercent;      // +0x20  1.0  formerly Field_20
        float AfterburnerDamageTriggerTime;         // +0x24  1.5  formerly Field_24
        float BrakeReverseDamageTriggerPercent;     // +0x28  1.0  formerly Field_28
        float FireMissilesDamageTriggerPercent;     // +0x2C  1.0  formerly Field_2C
        float FireMissilesDamageTriggerTime;        // +0x30  0.5  formerly Field_30
        float FireGunsDamageTriggerPercent;         // +0x34  1.0  formerly Field_34
        float FireGunsDamageTriggerTime;            // +0x38  1.0  formerly Field_38
        float Field_3C;   // +0x3C  not written by retail ctor
        float Field_40;   // +0x40  not written by retail ctor

        DamageReactionStruct();
        DamageReactionStruct& operator=(DamageReactionStruct const& Other);
    };

    static_assert(sizeof(DamageReactionStruct) == 0x44, "DamageReactionStruct size");

    // MissileReactionStruct — Common.dll.c 0x7A8E0; also inlined at Personality +0x230;
    // [MissileReactionBlock] (Content 06f3bb00)
    struct COMMON_API MissileReactionStruct
    {
        float EvadeBreakMissileReactionTime;       // +0x00  0.0    formerly Field_00
        float EvadeSlideMissileReactionTime;       // +0x04  0.0    formerly Field_04
        float EvadeAfterburnMissileReactionTime;   // +0x08  0.0    formerly Field_08
        float EvadeMissileDistance;                // +0x0C  250.0  formerly Field_0C

        MissileReactionStruct();
        MissileReactionStruct& operator=(MissileReactionStruct const& Other);
    };

    static_assert(sizeof(MissileReactionStruct) == 0x10, "MissileReactionStruct size");

    // CountermeasureUseStruct — Common.dll.c 0x7A900; also inlined at Personality +0x240;
    // [CountermeasureBlock] (read inline by Content at 06f3db9e / 06f3dbb9)
    struct COMMON_API CountermeasureUseStruct
    {
        float CountermeasureActiveTime;     // +0x00  1.0  formerly Field_00
        float CountermeasureUnactiveTime;   // +0x04  0.0  formerly Field_04

        CountermeasureUseStruct();
        CountermeasureUseStruct& operator=(CountermeasureUseStruct const& Other);
    };

    static_assert(sizeof(CountermeasureUseStruct) == 0x08, "CountermeasureUseStruct size");

    // FormationUseStruct — ctor .text:062da910; also constructed at Personality +0x248
    // by the outer ctor. Field names are the [FormationBlock] keys Content.dll's loader
    // (FUN_06f3bbd0) stores at each offset; the former Field_NN offset names follow each.
    struct COMMON_API FormationUseStruct
    {
        float    ForceAttackFormationActiveTime;            // +0x00  1.0   formerly Field_00
        float    ForceAttackFormationUnactiveTime;          // +0x04  0.0   formerly Field_04
        float    BreakFormationDamageTriggerPercent;        // +0x08  1.0   formerly Field_08
        float    BreakFormationDamageTriggerTime;           // +0x0C  4.0   formerly Field_0C
        float    BreakApartFormationDamageTriggerPercent;   // +0x10  1.0   formerly Field_10
        float    BreakApartFormationDamageTriggerTime;      // +0x14  4.0   formerly Field_14
        float    BreakFormationMissileReactionTime;         // +0x18  0.0   formerly Field_18
        float    BreakApartFormationMissileReactionTime;    // +0x1C  4.0   formerly Field_1C
        // The bytes between the uint8_t fields and the next float are alignment
        // padding: the ctor never writes them, operator= copies them.
        uint8_t  bBreakApartFormationOnBuzzHeadToward;      // +0x20  0     formerly Field_20
        float    BreakFormationOnBuzzHeadTowardTime;        // +0x24  4.0   formerly Field_24
        uint8_t  bRegroupFormationOnBuzzHeadToward;         // +0x28  0     formerly Field_28
        uint8_t  bBreakApartFormationOnBuzzPassBy;          // +0x29  0     formerly Field_29
        float    BreakFormationOnBuzzPassByTime;            // +0x2C  4.0   formerly Field_2C
        uint8_t  bRegroupFormationOnBuzzPassBy;             // +0x30  0     formerly Field_30
        uint8_t  bBreakApartFormationOnEvadeDodge;          // +0x31  0     formerly Field_31
        float    BreakFormationOnEvadeDodgeTime;            // +0x34  4.0   formerly Field_34
        uint8_t  bRegroupFormationOnEvadeDodge;             // +0x38  0     formerly Field_38
        uint8_t  bBreakApartFormationOnEvadeBreak;          // +0x39  0     formerly Field_39
        float    BreakFormationOnEvadeBreakTime;            // +0x3C  4.0   formerly Field_3C
        uint8_t  bRegroupFormationOnEvadeBreak;             // +0x40  0     formerly Field_40
        // 0 BREAK_AWAY_FROM_CENTER, 1 ..._AFTERBURNER, 2 BRAKE_REVERSE, 3 OUTRUN, 5 NOTHING.
        int32_t  FormationExitMode;                         // +0x44  5     formerly Field_44
        float    FormationExitTopTurnBreakAwayThrottle;     // +0x48  1.0   formerly Field_48
        float    FormationExitRollOutrunThrottle;           // +0x4C  0     formerly Field_4C
        float    FormationExitMaxTime;                      // +0x50  0     formerly Field_50
        uint8_t  bLeaderMakesMeTougher;                     // +0x54  0     formerly Field_54

        FormationUseStruct();
        FormationUseStruct& operator=(FormationUseStruct const& Other);
    };

    static_assert(offsetof(FormationUseStruct, FormationExitMode) == 0x44, "FormationUseStruct +0x44");
    static_assert(offsetof(FormationUseStruct, bLeaderMakesMeTougher) == 0x54, "FormationUseStruct +0x54");
    static_assert(sizeof(FormationUseStruct) == 0x58, "FormationUseStruct size");

    // JobStruct — ctor .text:062da980; also constructed at Personality +0x2A0 by the
    // outer ctor; operator= .text:062a8890 copies 0x4E dwords = 0x138 bytes. Field
    // names are the [JobBlock] keys Content.dll's loader (FUN_06f3c190 and its helpers
    // 06f3c9b0 / 06f3cad0) stores at each offset. The toughness fields hold 0..4
    // (EASIEST..HARDEST, unknown text -> 2).
    struct COMMON_API JobStruct
    {
        // One "attack_preference" / "attack_order" record (Content 06f3c9d8): the
        // ship class (11 = ANYTHING; 12 terminates the list), the range and the
        // weapon-type mask (1 GUNS, 2 GUIDED, 4 UNGUIDED, 8 TORPEDO). Retail names
        // no type for it; the name is the port's.
        struct FAttackPreference
        {
            int32_t  ShipClass;   // +0x0
            float    Range;       // +0x4
            uint32_t WeaponFlags; // +0x8
        };

        int32_t  Field_00;                  // +0x000  0   read by get_range_threshold_value; no
                                            //             [JobBlock] key writes it (TODO: meaning)
        int32_t  SceneToughnessThreshold;   // +0x004  2   formerly Field_04
        int32_t  TargetToughnessPreference; // +0x008  2   formerly Field_08
        float    Field_0C;                  // +0x00C  10000.0  no [JobBlock] key writes it (TODO)
        // +0x010: 12 records plus the terminator record. The ctor writes record 0 =
        // {11, 5000.0, 0xF} and record 1's class = 12 only; the port zeroes the rest.
        FAttackPreference AttackPreferences[13]; // +0x010  formerly Field_10..Field_1C + Reserved_20
        // +0x0AC: "attack_subtarget_order" (0 GUNS .. 5 HULL, 6 ANYTHING), terminated by 7.
        // The ctor writes {6, 7}; the port zeroes the rest.
        int32_t  AttackSubtargetOrder[8];   // +0x0AC  formerly Field_AC/Field_B0 + Reserved_B4
        uint8_t  bWaitForLeaderTarget;      // +0x0CC  never written by the ctor; the port zeroes it
        float    MaximumLeaderTargetDistance; // +0x0D0  5000.0  formerly Field_D0
        int32_t  FieldTargeting;            // +0x0D4  3   NEVER/LOW_DENSITY/HIGH_DENSITY/ALWAYS; formerly Field_D4
        int32_t  LootPreference;            // +0x0D8  0   LT_* mask (1 commodities, 2 equipment, 4 potions); formerly Field_D8
        int32_t  LootFleeThreshold;         // +0x0DC  2   formerly Field_DC
        // +0x0E0 / +0x0E4: the faction's "scan_for_cargo" archetype ids (Content NpcShip
        // 06f33ae1); LootBehavior matches a commodity's good id against them.
        int32_t  ScanForCargoCount;         // +0x0E0  0   formerly Field_E0
        uint32_t ScanForCargo[16];          // +0x0E4  never written by the ctor; the port zeroes it
        int32_t  FleeSceneThreatStyle;      // +0x124  3   formerly Field_124
        float    FleeWhenHullDamagedPercent; // +0x128 0   formerly Field_128
        uint8_t  bFleeWhenLeaderFleesStyle; // +0x12C  1   formerly Field_12C
        uint8_t  bFleeNoWeaponsStyle;       // +0x12D  1   formerly Field_12D
        uint8_t  bAllowPlayerTargeting;     // +0x12E  1   formerly Field_12E
        // +0x130: seeded from the exported SCAN_CHANCE_DEFAULT (062da9c1), which Content's
        // PatrolGuides compares it against before using it as the scan chance.
        float    ScanChance;                // +0x130  -1.0  formerly Field_130
        uint8_t  bForceAttackFormation;     // +0x134  0   formerly Field_134
        uint8_t  bForceAttackFormationSet;  // +0x135  0   set to 1 with the key; formerly Field_135

        JobStruct();
        JobStruct& operator=(JobStruct const& Other);
    };

    static_assert(sizeof(JobStruct::FAttackPreference) == 0x0C, "attack preference stride 0xC");
    static_assert(offsetof(JobStruct, AttackPreferences) == 0x10, "JobStruct +0x10");
    static_assert(offsetof(JobStruct, AttackSubtargetOrder) == 0xAC, "JobStruct +0xAC");
    static_assert(offsetof(JobStruct, bWaitForLeaderTarget) == 0xCC, "JobStruct +0xCC");
    static_assert(offsetof(JobStruct, MaximumLeaderTargetDistance) == 0xD0, "JobStruct +0xD0");
    static_assert(offsetof(JobStruct, ScanForCargo) == 0xE4, "JobStruct +0xE4");
    static_assert(offsetof(JobStruct, FleeSceneThreatStyle) == 0x124, "JobStruct +0x124");
    static_assert(offsetof(JobStruct, ScanChance) == 0x130, "JobStruct +0x130");
    static_assert(offsetof(JobStruct, bForceAttackFormationSet) == 0x135, "JobStruct +0x135");
    static_assert(sizeof(JobStruct) == 0x138, "JobStruct size");

    EvadeDodgeUseStruct     EvadeDodgeParams;    // +0x000  size 0x58
    EvadeBreakUseStruct     EvadeBreakParams;    // +0x058  size 0x3C
    BuzzHeadTowardUseStruct BuzzHeadParams;      // +0x094  size 0x60
    BuzzPassByUseStruct     BuzzPassParams;      // +0x0F4  size 0x34
    TrailUseStruct          TrailParams;         // +0x128  size 0x1C
    StrafeUseStruct         StrafeParams;        // +0x144  size 0x0C
    EngineKillUseStruct     EngineKillParams;    // +0x150  size 0x14
    RepairUseStruct         RepairParams;        // +0x164  size 0x18
    GunUseStruct            GunParams;           // +0x17C  size 0x40
    MineUseStruct           MineParams;          // +0x1BC  size 0x0C
    MissileUseStruct        MissileParams;       // +0x1C8  size 0x24
    DamageReactionStruct    DamageReaction;      // +0x1EC  size 0x44
    MissileReactionStruct   MissileReaction;     // +0x230  size 0x10
    CountermeasureUseStruct CMParams;            // +0x240  size 0x08
    FormationUseStruct      FormationParams;     // +0x248  size 0x58
    JobStruct               JobParams;           // +0x2A0  size 0x138

    Personality();
    Personality& operator=(Personality const& Other);

    // Helper methods from exports (Common.dll.c 0x48780 / 0x487E0). Neither
    // reads a lookup table — both are hardcoded switches in retail.
    float get_toughness_ratio(TOUGHNESS_TYPE Type);
    // Returns the unrounded ST0 value as double (retail ?..@@QAEMXZ returned float; no
    // retail module calls it).
    double get_range_threshold_value();

    // Retail `public: static float const pub::AI::Personality::SCAN_CHANCE_DEFAULT`, export
    // ordinal 1947, .rdata 0x063a05a8 = -1.0f. Its one reader in Common is 062da9c1, the
    // JobStruct ctor, which stores it into JobStruct::ScanChance ("not set" sentinel).
    static float const SCAN_CHANCE_DEFAULT;
};

static_assert(offsetof(Personality, EvadeBreakParams) == 0x058, "Personality +0x058");
static_assert(offsetof(Personality, BuzzHeadParams)   == 0x094, "Personality +0x094");
static_assert(offsetof(Personality, BuzzPassParams)   == 0x0F4, "Personality +0x0F4");
static_assert(offsetof(Personality, TrailParams)      == 0x128, "Personality +0x128");
static_assert(offsetof(Personality, StrafeParams)     == 0x144, "Personality +0x144");
static_assert(offsetof(Personality, EngineKillParams) == 0x150, "Personality +0x150");
static_assert(offsetof(Personality, RepairParams)     == 0x164, "Personality +0x164");
static_assert(offsetof(Personality, GunParams)        == 0x17C, "Personality +0x17C");
static_assert(offsetof(Personality, MineParams)       == 0x1BC, "Personality +0x1BC");
static_assert(offsetof(Personality, MissileParams)    == 0x1C8, "Personality +0x1C8");
static_assert(offsetof(Personality, DamageReaction)   == 0x1EC, "Personality +0x1EC");
static_assert(offsetof(Personality, MissileReaction)  == 0x230, "Personality +0x230");
static_assert(offsetof(Personality, CMParams)         == 0x240, "Personality +0x240");
static_assert(offsetof(Personality, FormationParams)  == 0x248, "Personality +0x248");
static_assert(offsetof(Personality, JobParams)        == 0x2A0, "Personality +0x2A0");
static_assert(sizeof(Personality) == 0x3D8, "Personality size");

// ============================================================================
// BaseOp — base class for directive operations
// ============================================================================

// Retail exports ??0BaseOp@AI@pub@@QAE@W4OP_TYPE@12@@Z (.text:062a8740),
// ??0BaseOp@AI@pub@@QAE@ABV012@@Z (.text:062a8760) and ??_7BaseOp@AI@pub@@6B@
// (.rdata:0639e0d0) -- nothing else. The vftable has exactly one slot and it is
// _purecall (LAB_06391fb0 -> MSVCRT purecall): validate() is pure, and there is no
// virtual destructor. No default constructor and no operator= exist in retail;
// operator= is deleted so the compiler does not emit and export one (a class with a
// vptr has a non-trivial implicit copy assignment). No caller copy-assigns an op.
class COMMON_API BaseOp
{
public:
    BaseOp(OP_TYPE Type);
    BaseOp(BaseOp const& Other);
    BaseOp& operator=(BaseOp const& Other) = delete;

    virtual bool validate() = 0;   // vftable slot 0 (+0x00)

    OP_TYPE  OpType;               // +0x04  operation type
    // +0x08  0. The directive's priority (DirectivePriority values): Server.dll
    // pub::AI::SubmitDirective copies it into word 1 of the IDirectiveInfo it hands to
    // IBehaviorManager::set_current_directive -- the word set_directive_priority rewrites --
    // and Content's [ObjList] directives default a 0 to the ship's own priority.
    // Kept as a raw dword because every producer stores plain numbers. Formerly Field_08.
    uint32_t Priority;
};

static_assert(offsetof(BaseOp, Priority) == 0x08, "BaseOp +0x08");
static_assert(sizeof(BaseOp) == 0x0C, "BaseOp size");

// ============================================================================
// Directive operations
// ============================================================================
//
// RE NOTE (2026-09-05): this whole family was fabricated the same way
// pub::AI::Personality was — a follow-up task asked only to fix stale
// "+0x08" offset comments after BaseOp grew a Field_08 (see BaseOp above),
// but comparing these structs against their real decompiled ctors in
// Common.dll.c showed most of them have the wrong field count, wrong types,
// and wrong total size, not just a 4-byte comment drift. Rebuilt field-by-
// field from each class's own real default ctor (Common.dll.c 0x7AE20-
// 0x7B2A0) and cross-checked against each real copy ctor (0x48A60-0x48EE0)
// and each real validate() (0x7AE60-0x7B360). Every one of these classes'
// default ctor sets `this+4` (OpType) to a distinct literal 0-21 — that
// mapping is exact and is now the real OP_TYPE enum above. Fields with
// strong semantic evidence (id fields gated by a "!= 0" check in a shared
// validate(), used consistently with the class's own purpose) are named;
// everything else is Field_XX (offset-based) with the real retail default
// value in a comment, following the same convention used for Personality.
//
// EXPORTS. Retail exports per op exactly: default ctor, copy ctor, the vftable and
// `validate` as a VIRTUAL (`?validate@DirectiveGotoOp@AI@pub@@UAE_NXZ`, `UAE`). The
// Directive* ops are classes (`ABV012@` in their copy ctors); the three Set*Params
// are structs (`ABU012@`). No operator= and no destructor is exported for any of
// them, so operator= is deleted (see BaseOp).

class COMMON_API DirectiveGotoOp : public BaseOp
{
public:
    int32_t Field_0C;              // +0x0C  3
    Vector  Destination;           // +0x10  0,0,0 (validate rejects NaN components)
    int32_t Field_1C;              // +0x1C  0     gates the NaN check in validate()
    uint32_t Field_20;             // +0x20  0
    uint32_t Field_24;             // +0x24  0
    uint32_t Field_28;             // +0x28  0
    uint32_t Field_2C;             // +0x2C  0
    uint32_t Field_30;             // +0x30  0
    uint32_t Field_34;             // +0x34  0
    uint32_t Field_38;             // +0x38  0
    uint32_t Field_3C;             // +0x3C  0
    uint32_t Field_40;             // +0x40  0
    uint32_t Field_44;             // +0x44  0
    uint32_t Field_48;             // +0x48  0
    uint32_t Field_4C;             // +0x4C  0
    float   Field_50;              // +0x50  500.0  (validate: must be >= 0)
    float   Field_54;              // +0x54  -1.0
    uint8_t Field_58;              // +0x58  0
    uint8_t Field_59;              // +0x59  1
    uint8_t Field_5A;              // +0x5A  0
    uint8_t Field_5B;              // +0x5B  0
    uint32_t Field_5C;             // +0x5C  0
    float   Field_60;              // +0x60  200.0
    float   Field_64;              // +0x64  500.0  (validate: Field_64 must be >= Field_60)
    uint32_t Field_68;             // +0x68  0
    uint8_t Field_6C;              // +0x6C  0

    DirectiveGotoOp();
    DirectiveGotoOp(DirectiveGotoOp const& Other);
    DirectiveGotoOp& operator=(DirectiveGotoOp const& Other) = delete;

    bool validate() override;
};

static_assert(offsetof(DirectiveGotoOp, Field_0C) == 0x0C, "DirectiveGotoOp +0x0C");
static_assert(offsetof(DirectiveGotoOp, Destination) == 0x10, "DirectiveGotoOp +0x10");
static_assert(offsetof(DirectiveGotoOp, Field_50) == 0x50, "DirectiveGotoOp +0x50");
static_assert(offsetof(DirectiveGotoOp, Field_6C) == 0x6C, "DirectiveGotoOp +0x6C");
static_assert(sizeof(DirectiveGotoOp) == 0x70, "DirectiveGotoOp size");

class COMMON_API DirectiveFollowOp : public BaseOp
{
public:
    uint   TargetId;               // +0x0C  0      object to follow (validate: != 0)
    float  FollowDistance;         // +0x10  150.0
    float  Field_14;               // +0x14  0.0
    float  Field_18;               // +0x18  0.0
    float  Field_1C;               // +0x1C  0.0
    float  Field_20;               // +0x20  400.0

    DirectiveFollowOp();
    DirectiveFollowOp(DirectiveFollowOp const& Other);
    DirectiveFollowOp& operator=(DirectiveFollowOp const& Other) = delete;

    bool validate() override;
};

static_assert(offsetof(DirectiveFollowOp, TargetId) == 0x0C, "DirectiveFollowOp +0x0C");
static_assert(sizeof(DirectiveFollowOp) == 0x24, "DirectiveFollowOp size");

// Field meanings from FaceBehavior's directive handler (Common .text:062c8b30): Mode 1
// watches TargetId, 2 faces FacePoint, 3 takes Orientation, 4 has no target; any other
// mode fails. The copy ctor (.text:062a8b70) copies FacePoint as a Vector (3 dwords) and
// Orientation as a Matrix (REP MOVSD 9). Content's [ObjList] "Face" parser writes
// OBJECT -> 1 / POINT -> 2.
class COMMON_API DirectiveFaceOp : public BaseOp
{
public:
    int32_t  Mode;                 // +0x0C  0  (validate: != 0); formerly Field_0C
    uint     TargetId;             // +0x10  0  mode 1 object (validate: != 0); formerly Field_10
    Vector   FacePoint;            // +0x14  0,0,0  mode 2 point; formerly Field_14..Field_1C
    Matrix   Orientation;          // +0x20  mode 3; not written by retail default ctor; formerly Reserved_20
    uint8_t  Field_44;             // +0x44  1  copied into FaceBehavior +0x1c4 (TODO: meaning)

    DirectiveFaceOp();
    DirectiveFaceOp(DirectiveFaceOp const& Other);
    DirectiveFaceOp& operator=(DirectiveFaceOp const& Other) = delete;

    bool validate() override;
};

static_assert(offsetof(DirectiveFaceOp, Mode) == 0x0C, "DirectiveFaceOp +0x0C");
static_assert(offsetof(DirectiveFaceOp, FacePoint) == 0x14, "DirectiveFaceOp +0x14");
static_assert(offsetof(DirectiveFaceOp, Orientation) == 0x20, "DirectiveFaceOp +0x20");
static_assert(offsetof(DirectiveFaceOp, Field_44) == 0x44, "DirectiveFaceOp +0x44");
static_assert(sizeof(DirectiveFaceOp) == 0x48, "DirectiveFaceOp size");

class COMMON_API DirectiveStrafeOp : public BaseOp
{
public:
    uint   TargetId;               // +0x0C  0      object to strafe past (validate: != 0)

    DirectiveStrafeOp();
    DirectiveStrafeOp(DirectiveStrafeOp const& Other);
    DirectiveStrafeOp& operator=(DirectiveStrafeOp const& Other) = delete;

    bool validate() override;
};

static_assert(offsetof(DirectiveStrafeOp, TargetId) == 0x0C, "DirectiveStrafeOp +0x0C");
static_assert(sizeof(DirectiveStrafeOp) == 0x10, "DirectiveStrafeOp size");

class COMMON_API DirectiveTrailOp : public BaseOp
{
public:
    uint   TargetId;               // +0x0C  0      object to trail (validate: != 0)
    float  TrailDistance;          // +0x10  150.0
    float  Field_14;               // +0x14  0.0
    float  Field_18;               // +0x18  0.0

    DirectiveTrailOp();
    DirectiveTrailOp(DirectiveTrailOp const& Other);
    DirectiveTrailOp& operator=(DirectiveTrailOp const& Other) = delete;

    bool validate() override;
};

static_assert(offsetof(DirectiveTrailOp, TargetId) == 0x0C, "DirectiveTrailOp +0x0C");
static_assert(sizeof(DirectiveTrailOp) == 0x1C, "DirectiveTrailOp size");

class COMMON_API DirectiveRamOp : public BaseOp
{
public:
    uint   TargetId;               // +0x0C  0      object to ram (BaseOp is 0x0C, not 0x08 — see BaseOp's Priority)

    DirectiveRamOp();
    DirectiveRamOp(DirectiveRamOp const& Other);
    DirectiveRamOp& operator=(DirectiveRamOp const& Other) = delete;

    bool validate() override;
};

static_assert(offsetof(DirectiveRamOp, TargetId) == 0x0C, "DirectiveRamOp +0x0C");
static_assert(sizeof(DirectiveRamOp) == 0x10, "DirectiveRamOp size");

class COMMON_API DirectiveTractorBeamOp : public BaseOp
{
public:
    uint    TargetId;              // +0x0C  0      object to tractor (validate: != 0)
    uint8_t Field_10;              // +0x10  1     (+0x11..+0x13 padding)

    DirectiveTractorBeamOp();
    DirectiveTractorBeamOp(DirectiveTractorBeamOp const& Other);
    DirectiveTractorBeamOp& operator=(DirectiveTractorBeamOp const& Other) = delete;

    bool validate() override;
};

static_assert(offsetof(DirectiveTractorBeamOp, TargetId) == 0x0C, "DirectiveTractorBeamOp +0x0C");
static_assert(offsetof(DirectiveTractorBeamOp, Field_10) == 0x10, "DirectiveTractorBeamOp +0x10");
static_assert(sizeof(DirectiveTractorBeamOp) == 0x14, "DirectiveTractorBeamOp size");

class COMMON_API DirectiveFormationOp : public BaseOp
{
public:
    // Real retail struct has NO fields beyond BaseOp — sizeof == sizeof(BaseOp).
    // Previously fabricated LeaderId/FormationSlot fields do not exist.

    DirectiveFormationOp();
    DirectiveFormationOp(DirectiveFormationOp const& Other);
    DirectiveFormationOp& operator=(DirectiveFormationOp const& Other) = delete;

    bool validate() override;
};

static_assert(sizeof(DirectiveFormationOp) == 0x0C, "DirectiveFormationOp size");

class COMMON_API DirectiveEvadeOp : public BaseOp
{
public:
    uint   ThreatId;               // +0x0C  0      threat to evade (BaseOp is 0x0C, not 0x08 — see BaseOp's Priority)

    DirectiveEvadeOp();
    DirectiveEvadeOp(DirectiveEvadeOp const& Other);
    DirectiveEvadeOp& operator=(DirectiveEvadeOp const& Other) = delete;

    bool validate() override;
};

static_assert(offsetof(DirectiveEvadeOp, ThreatId) == 0x0C, "DirectiveEvadeOp +0x0C");
static_assert(sizeof(DirectiveEvadeOp) == 0x10, "DirectiveEvadeOp size");

class COMMON_API DirectiveDrasticEvadeOp : public BaseOp
{
public:
    uint     ThreatId;             // +0x0C  0      threat to evade drastically (validate() always returns true)
    uint32_t Field_10;             // +0x10  0
    uint32_t Field_14;             // +0x14  0
    uint32_t Field_18;             // +0x18  0
    float    Field_1C;             // +0x1C  1.0
    uint32_t Field_20;             // +0x20  0
    uint32_t Field_24;             // +0x24  0
    uint32_t Field_28;             // +0x28  0
    uint32_t Field_2C;             // +0x2C  0

    DirectiveDrasticEvadeOp();
    DirectiveDrasticEvadeOp(DirectiveDrasticEvadeOp const& Other);
    DirectiveDrasticEvadeOp& operator=(DirectiveDrasticEvadeOp const& Other) = delete;

    bool validate() override;
};

static_assert(offsetof(DirectiveDrasticEvadeOp, ThreatId) == 0x0C, "DirectiveDrasticEvadeOp +0x0C");
static_assert(offsetof(DirectiveDrasticEvadeOp, Field_1C) == 0x1C, "DirectiveDrasticEvadeOp +0x1C");
static_assert(sizeof(DirectiveDrasticEvadeOp) == 0x30, "DirectiveDrasticEvadeOp size");

class COMMON_API DirectiveDockOp : public BaseOp
{
public:
    uint     DockObjectId;         // +0x0C  0      object to dock with
    int32_t  Field_10;             // +0x10  0
    uint8_t  Field_14;             // +0x14  0     (+0x15..+0x17 padding)
    int32_t  DockPortIndex;        // +0x18  -1
    int32_t  Field_1C;             // +0x1C  0
    float    Field_20;             // +0x20  200.0
    float    Field_24;             // +0x24  500.0  (validate: Field_24 must be >= Field_20)
    int32_t  Field_28;             // +0x28  0

    DirectiveDockOp();
    DirectiveDockOp(DirectiveDockOp const& Other);
    DirectiveDockOp& operator=(DirectiveDockOp const& Other) = delete;

    bool validate() override;
};

static_assert(offsetof(DirectiveDockOp, DockObjectId) == 0x0C, "DirectiveDockOp +0x0C");
static_assert(offsetof(DirectiveDockOp, DockPortIndex) == 0x18, "DirectiveDockOp +0x18");
static_assert(offsetof(DirectiveDockOp, Field_20) == 0x20, "DirectiveDockOp +0x20");
static_assert(sizeof(DirectiveDockOp) == 0x2C, "DirectiveDockOp size");

class COMMON_API DirectiveLaunchOp : public BaseOp
{
public:
    uint    LaunchObjectId;        // +0x0C  0      object to launch from (validate: != 0)
    int32_t Field_10;              // +0x10  -1     (validate: != -1)
    int32_t Field_14;              // +0x14  0      (validate: != 0)

    DirectiveLaunchOp();
    DirectiveLaunchOp(DirectiveLaunchOp const& Other);
    DirectiveLaunchOp& operator=(DirectiveLaunchOp const& Other) = delete;

    bool validate() override;
};

static_assert(offsetof(DirectiveLaunchOp, LaunchObjectId) == 0x0C, "DirectiveLaunchOp +0x0C");
static_assert(sizeof(DirectiveLaunchOp) == 0x18, "DirectiveLaunchOp size");

class COMMON_API DirectiveInstantTradelaneOp : public BaseOp
{
public:
    uint    TradeLaneId;           // +0x0C  0      tradelane to use (validate: != 0)
    int32_t Field_10;              // +0x10  0
    int32_t Field_14;              // +0x14  0

    DirectiveInstantTradelaneOp();
    DirectiveInstantTradelaneOp(DirectiveInstantTradelaneOp const& Other);
    DirectiveInstantTradelaneOp& operator=(DirectiveInstantTradelaneOp const& Other) = delete;

    bool validate() override;
};

static_assert(offsetof(DirectiveInstantTradelaneOp, TradeLaneId) == 0x0C, "DirectiveInstantTradelaneOp +0x0C");
static_assert(sizeof(DirectiveInstantTradelaneOp) == 0x18, "DirectiveInstantTradelaneOp size");

class COMMON_API DirectiveDelayOp : public BaseOp
{
public:
    float  DelaySeconds;           // +0x0C  0.0    (validate: != 0.0 — BaseOp is 0x0C, not 0x08, see BaseOp's Priority)

    DirectiveDelayOp();
    DirectiveDelayOp(DirectiveDelayOp const& Other);
    DirectiveDelayOp& operator=(DirectiveDelayOp const& Other) = delete;

    bool validate() override;
};

static_assert(offsetof(DirectiveDelayOp, DelaySeconds) == 0x0C, "DirectiveDelayOp +0x0C");
static_assert(sizeof(DirectiveDelayOp) == 0x10, "DirectiveDelayOp size");

class COMMON_API DirectiveCancelOp : public BaseOp
{
public:
    // Real retail struct has NO fields beyond BaseOp — sizeof == sizeof(BaseOp).
    // Previously fabricated DirectivePriority Priority field does not exist.

    DirectiveCancelOp();
    DirectiveCancelOp(DirectiveCancelOp const& Other);
    DirectiveCancelOp& operator=(DirectiveCancelOp const& Other) = delete;

    bool validate() override;
};

static_assert(sizeof(DirectiveCancelOp) == 0x0C, "DirectiveCancelOp size");

class COMMON_API DirectiveWaitForShip : public BaseOp
{
public:
    uint     ShipId;               // +0x0C  0      ship to wait for
    uint32_t Field_10;             // +0x10  not written by retail default ctor

    DirectiveWaitForShip();
    DirectiveWaitForShip(DirectiveWaitForShip const& Other);
    DirectiveWaitForShip& operator=(DirectiveWaitForShip const& Other) = delete;

    bool validate() override;
};

static_assert(offsetof(DirectiveWaitForShip, ShipId) == 0x0C, "DirectiveWaitForShip +0x0C");
static_assert(sizeof(DirectiveWaitForShip) == 0x14, "DirectiveWaitForShip size");

class COMMON_API DirectiveWaitForPlayerManeuverOp : public BaseOp
{
public:
    // Real field is a `char const*` passed to BehaviorTypes::get_behavior_id()
    // in validate() — NOT an int bitmask as previously fabricated.
    char const* BehaviorName;      // +0x0C  not written by retail default ctor (BaseOp is 0x0C, not 0x08, see BaseOp's Priority)

    DirectiveWaitForPlayerManeuverOp();
    DirectiveWaitForPlayerManeuverOp(DirectiveWaitForPlayerManeuverOp const& Other);
    DirectiveWaitForPlayerManeuverOp& operator=(DirectiveWaitForPlayerManeuverOp const& Other) = delete;

    bool validate() override;
};

static_assert(offsetof(DirectiveWaitForPlayerManeuverOp, BehaviorName) == 0x0C, "DirectiveWaitForPlayerManeuverOp +0x0C");
static_assert(sizeof(DirectiveWaitForPlayerManeuverOp) == 0x10, "DirectiveWaitForPlayerManeuverOp size");

class COMMON_API DirectiveIdleOp : public BaseOp
{
public:
    // Real retail struct has NO fields beyond BaseOp — sizeof == sizeof(BaseOp).

    DirectiveIdleOp();
    DirectiveIdleOp(DirectiveIdleOp const& Other);
    DirectiveIdleOp& operator=(DirectiveIdleOp const& Other) = delete;

    bool validate() override;
};

static_assert(sizeof(DirectiveIdleOp) == 0x0C, "DirectiveIdleOp size");

// DirectiveGuideOp — Common.dll.c 0x7B060 (default ctor). Previously ported
// as a struct holding a single GuidedObjectId field; retail actually embeds
// a full DirectiveGotoOp BY VALUE at +0x0C and a DirectiveFollowOp-shaped
// object BY VALUE at +0x7C (each with its own vtable, independently
// dispatchable — DirectiveGuideOp::validate() calls both embedded objects'
// own validate() through their vtables), plus its own trailing fields.
class COMMON_API DirectiveGuideOp : public BaseOp
{
public:
    DirectiveGotoOp   GotoParams;      // +0x0C  size 0x70
    DirectiveFollowOp FollowParams;    // +0x7C  size 0x24
    uint8_t Field_A0;                  // +0xA0  1     (+0xA1..+0xA3 padding)
    float   Field_A4;                  // +0xA4  10.0
    float   Field_A8;                  // +0xA8  0.3927  (pi/8)
    int32_t Field_AC;                  // +0xAC  3
    float   Field_B0;                  // +0xB0  10.0
    float   Field_B4;                  // +0xB4  -10.0
    float   Field_B8;                  // +0xB8  400.0

    DirectiveGuideOp();
    DirectiveGuideOp(DirectiveGuideOp const& Other);
    DirectiveGuideOp& operator=(DirectiveGuideOp const& Other) = delete;

    bool validate() override;
};

static_assert(offsetof(DirectiveGuideOp, GotoParams) == 0x0C, "DirectiveGuideOp +0x0C");
static_assert(offsetof(DirectiveGuideOp, FollowParams) == 0x7C, "DirectiveGuideOp +0x7C");
static_assert(offsetof(DirectiveGuideOp, Field_A0) == 0xA0, "DirectiveGuideOp +0xA0");
static_assert(offsetof(DirectiveGuideOp, Field_B8) == 0xB8, "DirectiveGuideOp +0xB8");
static_assert(sizeof(DirectiveGuideOp) == 0xBC, "DirectiveGuideOp size");

// ============================================================================
// Parameter structs for IBehaviorManager methods
// ============================================================================
//
// RE NOTE (2026-09-05): both structs below did NOT inherit from BaseOp in
// the header despite the task's premise that they did — but their real copy
// ctors (Common.dll.c 0x48DA0 / 0x48D10) show the same BaseOp::_vftable_-
// then-own-vtable patch pattern as every other BaseOp-derived class here
// (the same fabrication bug already fixed for SetPersonalityParams). Fixed
// to actually derive from BaseOp, with real fields from their own default
// ctors (0x7B3C0 / 0x7B330) and validate() (0x7B3F0 / 0x7B360).

struct COMMON_API SetFlagParams : public BaseOp
{
    int32_t  FlagId;               // +0x0C  2      (validate: != 2)
    uint8_t  bValue;               // +0x10  0     (+0x11..+0x13 padding)

    SetFlagParams();
    SetFlagParams(SetFlagParams const& Other);
    SetFlagParams& operator=(SetFlagParams const& Other) = delete;

    bool validate() override;
};

static_assert(offsetof(SetFlagParams, FlagId) == 0x0C, "SetFlagParams +0x0C");
static_assert(offsetof(SetFlagParams, bValue) == 0x10, "SetFlagParams +0x10");
static_assert(sizeof(SetFlagParams) == 0x14, "SetFlagParams size");

// SetPersonalityParams — Common.dll.c 0x7AD10 (default ctor) / 0x48930 (copy
// ctor). Previously ported as a struct holding a Personality*; retail
// actually embeds a full Personality BY VALUE at +0x1C (the ctor default-
// constructs it in place with a placement-new-style direct call, and the
// copy ctor flat-copies all 0xF6 dwords of it). Derives from BaseOp (vtable
// is set to BaseOp::_vftable_ first, then patched to SetPersonalityParams's
// own vtable after the embedded Personality is constructed — the classic
// base-then-derived vtable-patch pattern).
// Field meanings from their one consumer, Server.dll pub::AI::SubmitState
// (.text:06d5cb57..06d5cba5): +0x0C -> IBehaviorManager::set_state_graph(id, false);
// +0x14 / +0x10 -> set_directive_callback / set_content_callback unless -1; byte +0x18
// gates set_personality(Value). Content fills the callbacks with its NPC objects' callback
// subobjects or 0.
struct COMMON_API SetPersonalityParams : public BaseOp
{
    int32_t            StateGraphId;             // +0x0C  -1 (validate: != -1); formerly Field_0C
    ContentCallback*   ContentCallbackObject;    // +0x10  (ContentCallback*)-1 = keep; formerly Field_10
    DirectiveCallback* DirectiveCallbackObject;  // +0x14  (DirectiveCallback*)-1 = keep; formerly Field_14
    uint8_t            bSetPersonality;          // +0x18  0  (+0x19..+0x1B padding); formerly Field_18
    Personality Value;              // +0x1C  size 0x3D8

    SetPersonalityParams();
    SetPersonalityParams(SetPersonalityParams const& Other);
    SetPersonalityParams& operator=(SetPersonalityParams const& Other) = delete;

    bool validate() override;
};

static_assert(offsetof(SetPersonalityParams, StateGraphId) == 0x0C, "SetPersonalityParams +0x0C");
static_assert(offsetof(SetPersonalityParams, DirectiveCallbackObject) == 0x14, "SetPersonalityParams +0x14");
static_assert(offsetof(SetPersonalityParams, bSetPersonality) == 0x18, "SetPersonalityParams +0x18");
static_assert(offsetof(SetPersonalityParams, Value) == 0x1C, "SetPersonalityParams +0x1C");
static_assert(sizeof(SetPersonalityParams) == 0x3F4, "SetPersonalityParams size");

// Field names follow PathfindManager::UserZone (common_pathfind.h), which Server.dll
// pub::AI::SubmitState (.text:06d5c957..06d5cb56) builds from these fields; Content's
// mission [ObjList] "StayInRange" parser fills them.
struct COMMON_API SetZoneBehaviorParams : public BaseOp
{
    int32_t Mode;                  // +0x0C  0 sphere (TargetPoint, Radius), 1 box (BoxMin..BoxMax),
                                   //        2 live object (TargetId, Radius); formerly Field_0C.
                                   //        Not written by the retail default ctor.
    int32_t InvertMode;            // +0x10  2      0 normal, 1 inverted, 2 clear the user zone
                                   //        (validate skips its checks); formerly Field_10
    Vector  TargetPoint;           // +0x14  0,0,0  sphere centre; formerly Field_14..Field_1C
    uint    TargetId;              // +0x20  0      object of mode 2; formerly Field_20
    Vector  BoxMin;                // +0x24  box corners of mode 1; not written by the
    Vector  BoxMax;                // +0x30  retail default ctor; formerly Reserved_24
    float   Radius;                // +0x3C  0.0    (validate: != 0.0); formerly Field_3C

    SetZoneBehaviorParams();
    SetZoneBehaviorParams(SetZoneBehaviorParams const& Other);
    SetZoneBehaviorParams& operator=(SetZoneBehaviorParams const& Other) = delete;

    bool validate() override;
};

static_assert(offsetof(SetZoneBehaviorParams, Mode) == 0x0C, "SetZoneBehaviorParams +0x0C");
static_assert(offsetof(SetZoneBehaviorParams, TargetPoint) == 0x14, "SetZoneBehaviorParams +0x14");
static_assert(offsetof(SetZoneBehaviorParams, TargetId) == 0x20, "SetZoneBehaviorParams +0x20");
static_assert(offsetof(SetZoneBehaviorParams, BoxMin) == 0x24, "SetZoneBehaviorParams +0x24");
static_assert(offsetof(SetZoneBehaviorParams, BoxMax) == 0x30, "SetZoneBehaviorParams +0x30");
static_assert(offsetof(SetZoneBehaviorParams, Radius) == 0x3C, "SetZoneBehaviorParams +0x3C");
static_assert(sizeof(SetZoneBehaviorParams) == 0x40, "SetZoneBehaviorParams size");

// ============================================================================
// ContentCallback — interface for content-driven AI events
// ============================================================================

// EXPORTS. Retail compiled these ten virtuals from the header, inline, into every
// module that saw it, and each module exports its own copy -- Common.dll (ordinals
// 2382..3752, e.g. get_creation_type .text:0626c9a0), Server.dll (474..517,
// .text:06ce1030..06ce10a0) and FreeLancer.exe -- while none of them exports a
// ContentCallback constructor, destructor, operator= or ??_7. That is member-level
// export on the ten virtuals only, not a class-level one; the struct key is from
// ?set_content_callback@IBehaviorManager@@QAEXPAUContentCallback@AI@pub@@@Z.
//
// Bodies, identical in all three modules (Common / Server addresses):
//   combat_state_notification  RET 0xc               062a8910 / 06ce1080
//   combat_drift_event         RET                   062a8920 / 06ce1090
//   follow_event               RET 0x10              062a88d0 / 06ce1030
//   lead_object_event          RET 0x14              062a88e0 / 06ce1040
//   tether_object_event        RET 0x10 (= follow)   062a88d0 / 06ce1030
//   request_flee_destination   XOR AL,AL; RET 0x18   062a88f0 / 06ce1060
//   information_request        XOR AL,AL; RET 0xc    062a8900 / 06ce1070
//   scan_state_change          RET 4                 062ed730 / 06ce1050
//   get_creation_type          MOV EAX,3; RET        0626c9a0 / 06ce10a0
//   ship_in_sights_event       RET 4 (= scan)        062ed730 / 06ce1050
//
// Slot order, from Common.dll's calls through IBehaviorManager+0x110 (argument
// count and RET n of the body each lands on):
//   +0x00 follow_event              4 args  .text:062d29f6 (FUN_062d29c0)
//   +0x04 tether_object_event       4 args  .text:062d2a4d
//   +0x08 lead_object_event         5 args  .text:062d2aab
//   +0x0c ship_in_sights_event      1 arg   .text:062d2cc8 (an id: EAX of the
//                                            object's own [vptr+0x20] getter)
//   +0x10 request_flee_destination  6 args  .text:062d2ade (tail JMP)
//   +0x14 information_request       -- the one 3-argument method left; no call
//                                            site in Common.dll
//   +0x18 scan_state_change         1 bool  .text:062d2dc0 (the byte flag it
//                                            also stores to [this+0xf8])
//   +0x1c combat_state_notification 3 args  .text:062ee2d3 / 062ee2ff / 062ee35a
//   +0x20 combat_drift_event        0 args  .text:062ed96e
//   +0x24 get_creation_type         0 args  .text:062d3f03 (set_content_callback)
// Ten slots, one per exported virtual, so there is no virtual destructor. The
// earlier port declared one in slot 0 and the methods in alphabetical-ish order,
// which put every typed call on the wrong slot.
struct ContentCallback
{
    // Values seen at call sites: 0, 2, 4 (TargetManager::SetAttackState, 062ee280, PUSH 0x0 / 0x2 / 0x4 for
    // its three state changes). TODO: meanings unknown; the names are placeholders.
    enum CSNType // Combat State Notification type
    {
        CSNType0 = 0,
        CSNType2 = 2,
        CSNType4 = 4,
    };

    // TODO (all enums below): enumerators are placeholders, no values recovered.
    enum FollowStatusType
    {
        FollowStatus0 = 0,
        FollowStatus1 = 1,
        FollowStatus2 = 2,
    };

    enum LeadProgressType
    {
        LeadProgress0 = 0,
        LeadProgress1 = 1,
        LeadProgress2 = 2,
    };

    enum TetherStatusType
    {
        TetherStatus0 = 0,
        TetherStatus1 = 1,
        TetherStatus2 = 2,
    };

    enum FleeReason
    {
        FleeReason0 = 0,
        FleeReason1 = 1,
        FleeReason2 = 2,
    };

    enum FleeDestinationType
    {
        FleeDest0 = 0,
        FleeDest1 = 1,
        FleeDest2 = 2,
    };

    enum InformationType
    {
        InfoType0 = 0,
        InfoType1 = 1,
        InfoType2 = 2,
    };

    // 3 is what the base get_creation_type returns.
    enum CreationType
    {
        CreationType0 = 0,
        CreationType1 = 1,
        CreationType2 = 2,
        CreationType3 = 3,
    };

    SHARED_INLINE_API virtual void follow_event(uint ObjectId1, uint ObjectId2, FollowStatusType Status, float Distance) {}                                                           // +0x00
    SHARED_INLINE_API virtual void tether_object_event(uint ObjectId1, uint ObjectId2, TetherStatusType Status, Vector const& Position) {}                                             // +0x04
    SHARED_INLINE_API virtual void lead_object_event(uint ObjectId1, uint ObjectId2, LeadProgressType Progress, float Value, Vector const& Position) {}                                // +0x08
    SHARED_INLINE_API virtual void ship_in_sights_event(uint ObjectId) {}                                                                                                               // +0x0c
    SHARED_INLINE_API virtual bool request_flee_destination(uint ObjectId, FleeReason Reason, FleeDestinationType& OutType, Vector& OutPosition, uint& OutParam, float& OutFloat) { return false; } // +0x10
    SHARED_INLINE_API virtual bool information_request(InformationType Type, int Param, char* Buffer) { return false; }                                                                 // +0x14
    SHARED_INLINE_API virtual void scan_state_change(bool bScanning) {}                                                                                                                 // +0x18
    SHARED_INLINE_API virtual void combat_state_notification(CSNType Type, uint Param1, uint Param2) {}                                                                                 // +0x1c
    SHARED_INLINE_API virtual void combat_drift_event() {}                                                                                                                              // +0x20
    SHARED_INLINE_API virtual CreationType get_creation_type() { return CreationType3; }                                                                                                // +0x24
};

// ============================================================================
// DirectiveCallback — interface for directive-related callbacks
// ============================================================================

// CORRECTED 2026-09-17. This class used to declare a virtual destructor in slot 0
// and a zero-argument OnDirectiveComplete in slot 1. Both were wrong, and the
// comment that used to sit here had already guessed as much from retail's
// combined LaunchAction vtable having exactly ONE slot (PTR_FUN_0639e57c). The
// dispatch site proves it outright -- IBehaviorManager::UpdateBehaviorTick,
// .text:062d2ee1-062d2efd, walking DirectiveCallbacks[5]:
//     .text:062d2ee1  MOV  ECX,[EDI]          ; the callback pointer
//     .text:062d2ee7  MOV  EAX,[ESI + 0x118]
//     .text:062d2eed  MOV  EDX,[ECX]          ; its vtable
//     .text:062d2eef  PUSH EAX
//     .text:062d2ef0  MOV  EAX,[ESI + 0x11c]
//     .text:062d2ef6  PUSH EAX
//     .text:062d2ef7  CALL dword ptr [EDX]    ; SLOT 0, two arguments
// The `MOV EBX,0x5` / `DEC EBX` / `ADD EDI,4` around it is what fixes the array
// at five entries. A second identical site is at .text:062d33ca-062d33d3.
// Argument order is MSVC's right-to-left, so the LAST push is the FIRST
// parameter: manager+0x11c arrives first, manager+0x118 second. Those two
// dwords are the directive-result payload; their meaning is still unrecovered,
// which is why the parameters carry the source offsets as names rather than
// invented semantics.
//
// Dropping the virtual destructor is safe here for the same reason retail gets
// away with it: nothing in the port deletes through a bare DirectiveCallback*
// (checked), and the callbacks are registered by pointer via
// IBehaviorManager::set_directive_callback and never owned by the manager.
//
// A STRUCT in retail and not exported: ?set_directive_callback@IBehaviorManager@@QAEXPAU
// DirectiveCallback@AI@pub@@@Z names it `U`, the RTTI type descriptor at .data:063ebca0 is
// ".?AUDirectiveCallback@AI@pub@@", and no member of it is in the export table. Retail also
// has no DirectiveCallback vftable of its own (only the LaunchAction one that embeds this
// base), so the slot is pure: the no-op default body the port used to carry was invented.
struct DirectiveCallback
{
    // Slot 0, and the only virtual. Retail 0x62b01f0 is
    // LaunchAction::OnDirectiveComplete, still the only identified override
    // (see common_launchaction.cpp). Dispatched when a queued AI directive this
    // callback was registered for finishes.
    virtual void OnDirectiveComplete(int ResultWord11c, int ResultWord118) = 0;
};

}} // namespace pub::AI
