// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll PUB batch — pub::AI namespace implementations.

#include "common_pub.h"
#include "common_iface.h" // BehaviorTypes::get_behavior_id
#include <windows.h> // OutputDebugStringA
#include <cstdio>    // snprintf
#include <cstring>   // memcpy

namespace pub { namespace AI {

// ============================================================================
// Static constant definitions
// ============================================================================

// ============================================================================
// GSplinePath
// ============================================================================

GSplinePath::GSplinePath()
{
    // .text:06261260: `MOV EAX,this; RET` -- nothing is initialised.
}

GSplinePath& GSplinePath::operator=(GSplinePath const& Other)
{
    // .text:06274100: `MOV ECX,0xc; MOV EDI,this; REP MOVSD` -- 12 dwords, no self-check.
    std::memcpy(this, &Other, sizeof(GSplinePath));
    return *this;
}

// ============================================================================
// Personality nested structs
// ============================================================================

// RE NOTE (2026-09-05): every sub-struct below was rebuilt byte-for-byte from
// the real retail default ctors in Common.dll.c (see common_pub.h for the
// full methodology note and per-struct addresses). Each ctor here writes the
// exact retail hex constant at each offset; operator= is a raw memberwise
// copy (equivalent to retail's dword-copy loops, since every one of these
// structs is a plain-old-data value type with no pointers).

Personality::EvadeDodgeUseStruct::EvadeDodgeUseStruct()
    : EvadeActivateRange(500.0f)
    , EvadeDodgeStyleWeightWaggle(0.0f)
    , EvadeDodgeStyleWeightWaggleRandom(0.0f)
    , EvadeDodgeStyleWeightSlide(0.0f)
    , EvadeDodgeStyleWeightCorkscrew(0.0f)
    , EvadeDodgeConeAngle(0.7853981852531433f)
    , EvadeDodgeConeAngleVariancePercent(0.0f)
    , EvadeDodgeWaggleAxisConeAngle(0.0f)
    , EvadeDodgeRollAngle(0.0f)
    , EvadeDodgeIntervalTime(1.0f)
    , EvadeDodgeIntervalTimeVariancePercent(0.0f)
    , EvadeDodgeDistance(100.0f)
    , EvadeDodgeTime(10.0f)
    , EvadeDodgeSlideThrottle(0.6f)
    , EvadeDodgeTurnThrottle(0.6f)
    , EvadeDodgeCorkscrewTurnThrottle(0.15f)
    , EvadeDodgeCorkscrewRollThrottle(1.0f)
    , bEvadeDodgeCorkscrewRollFlipDirection(0)
    , EvadeDodgeDirectionWeightLeft(0.0f)
    , EvadeDodgeDirectionWeightRight(0.0f)
    , EvadeDodgeDirectionWeightUp(0.0f)
    , EvadeDodgeDirectionWeightDown(0.0f)
{
}

Personality::EvadeDodgeUseStruct& Personality::EvadeDodgeUseStruct::operator=(EvadeDodgeUseStruct const& Other)
{
    std::memcpy(this, &Other, sizeof(*this));
    return *this;
}

Personality::EvadeBreakUseStruct::EvadeBreakUseStruct()
    : EvadeBreakTime(3.0f)
    , EvadeBreakIntervalTime(100000.0f)
    , EvadeBreakAfterburnerDelay(0.0f)
    , EvadeBreakAfterburnerDelayVariancePercent(0.0f)
    , EvadeBreakDirectionWeightLeft(0.0f)
    , EvadeBreakDirectionWeightRight(0.0f)
    , EvadeBreakDirectionWeightUp(0.0f)
    , EvadeBreakDirectionWeightDown(0.0f)
    , EvadeBreakRollThrottle(0.0f)
    , EvadeBreakTurnThrottle(1.0f)
    , EvadeBreakStyleWeightSideways(0.0f)
    , EvadeBreakStyleWeightOutrun(0.0f)
    , EvadeBreakStyleWeightReverse(0.0f)
    , EvadeBreakAttemptReverseTime(4.0f)
    , EvadeBreakReverseDistance(150.0f)
{
}

Personality::EvadeBreakUseStruct& Personality::EvadeBreakUseStruct::operator=(EvadeBreakUseStruct const& Other)
{
    std::memcpy(this, &Other, sizeof(*this));
    return *this;
}

Personality::BuzzHeadTowardUseStruct::BuzzHeadTowardUseStruct()
    : BuzzMaxTimeToHeadAway(4.0f)
    , BuzzHeadTowardStyleWeightStraightTo(0.0f)
    , BuzzHeadTowardStyleWeightSlide(0.0f)
    , BuzzHeadTowardStyleWeightWaggle(0.0f)
    , BuzzMinDistanceToHeadToward(200.0f)
    , BuzzMinDistanceToHeadTowardVariancePercent(1.0f)
    , BuzzHeadTowardEngineThrottle(1.0f)
    , BuzzHeadTowardTurnThrottle(1.0f)
    , BuzzHeadTowardRollThrottle(0.0f)
    , bBuzzHeadTowardRollFlipDirection(0)
    , BuzzDodgeDirectionWeightLeft(0.0f)
    , BuzzDodgeDirectionWeightRight(0.0f)
    , BuzzDodgeDirectionWeightUp(0.0f)
    , BuzzDodgeDirectionWeightDown(0.0f)
    , BuzzDodgeTurnThrottle(1.0f)
    , BuzzDodgeConeAngle(0.7853981852531433f)
    , BuzzDodgeConeAngleVariancePercent(0.0f)
    , BuzzDodgeWaggleAxisConeAngle(0.0f)
    , BuzzDodgeRollAngle(0.0f)
    , BuzzDodgeIntervalTime(2.0f)
    , BuzzDodgeIntervalTimeVariancePercent(0.0f)
    , BuzzSlideThrottle(1.0f)
    , BuzzSlideIntervalTime(2.0f)
    , BuzzSlideIntervalTimeVariancePercent(0.4000000059604645f)
{
}

Personality::BuzzHeadTowardUseStruct& Personality::BuzzHeadTowardUseStruct::operator=(BuzzHeadTowardUseStruct const& Other)
{
    std::memcpy(this, &Other, sizeof(*this));
    return *this;
}

Personality::BuzzPassByUseStruct::BuzzPassByUseStruct()
    : BuzzPassByStyleWeightStraightBy(0.0f)
    , BuzzPassByStyleWeightBreakAway(0.0f)
    , BuzzPassByStyleWeightEngineKill(0.0f)
    , BuzzDistanceToPassBy(100.0f)
    , BuzzPassByTime(3.0f)
    , bBuzzDropBombOnPassBy(0)
    , BuzzBreakDirectionWeightLeft(0.0f)
    , BuzzBreakDirectionWeightRight(0.0f)
    , BuzzBreakDirectionWeightUp(0.0f)
    , BuzzBreakDirectionWeightDown(0.0f)
    , BuzzBreakDirectionConeAngle(0.0f)
    , BuzzBreakTurnThrottle(1.0f)
    , BuzzPassByRollThrottle(0.0f)
{
}

Personality::BuzzPassByUseStruct& Personality::BuzzPassByUseStruct::operator=(BuzzPassByUseStruct const& Other)
{
    std::memcpy(this, &Other, sizeof(*this));
    return *this;
}

Personality::TrailUseStruct::TrailUseStruct()
    : TrailLockConeAngle(0.5235987901687622f)
    , TrailBreakTime(2.5f)
    , TrailMinNoLockTime(4.0f)
    , TrailBreakRollThrottle(0.0f)
    , bTrailBreakAfterburner(0)
    , TrailMaxTurnThrottle(1.0f)
    , TrailDistance(150.0f)
{
}

Personality::TrailUseStruct& Personality::TrailUseStruct::operator=(TrailUseStruct const& Other)
{
    std::memcpy(this, &Other, sizeof(*this));
    return *this;
}

Personality::StrafeUseStruct::StrafeUseStruct()
    : StrafeRunAwayDistance(150.0f)
    , StrafeAttackThrottle(1.0f)
    , StrafeTurnThrottle(1.0f)
{
}

Personality::StrafeUseStruct& Personality::StrafeUseStruct::operator=(StrafeUseStruct const& Other)
{
    std::memcpy(this, &Other, sizeof(*this));
    return *this;
}

Personality::EngineKillUseStruct::EngineKillUseStruct()
    : EngineKillSearchTime(6.0f)
    , EngineKillFaceTime(3.0f)
    , EngineKillUseAfterburner(0.0f)
    , EngineKillAfterburnerTime(0.5f)
    , EngineKillMaxTargetDistance(700.0f)
{
}

Personality::EngineKillUseStruct& Personality::EngineKillUseStruct::operator=(EngineKillUseStruct const& Other)
{
    std::memcpy(this, &Other, sizeof(*this));
    return *this;
}

Personality::RepairUseStruct::RepairUseStruct()
    : UseShieldRepairPreDelay(2.0f)
    , UseShieldRepairAtDamagePercent(0.5f)
    , UseShieldRepairPostDelay(3.0f)
    , UseHullRepairPreDelay(1.0f)
    , UseHullRepairAtDamagePercent(0.5f)
    , UseHullRepairPostDelay(1.0f)
{
}

Personality::RepairUseStruct& Personality::RepairUseStruct::operator=(RepairUseStruct const& Other)
{
    // 06263b40 (ICF-shared with MarshalBuffer::operator=): REP MOVSD of 6 dwords, no self test.
    UseShieldRepairPreDelay = Other.UseShieldRepairPreDelay;
    UseShieldRepairAtDamagePercent = Other.UseShieldRepairAtDamagePercent;
    UseShieldRepairPostDelay = Other.UseShieldRepairPostDelay;
    UseHullRepairPreDelay = Other.UseHullRepairPreDelay;
    UseHullRepairAtDamagePercent = Other.UseHullRepairAtDamagePercent;
    UseHullRepairPostDelay = Other.UseHullRepairPostDelay;
    return *this;
}

Personality::GunUseStruct::GunUseStruct()
    : FireStyle(0)
    , GunFireIntervalTime(0.0f)
    , GunFireIntervalVariancePercent(0.5f)
    , GunFireBurstIntervalTime(1.5f)
    , GunFireBurstIntervalVariancePercent(0.1f)
    , GunFireNoBurstIntervalTime(0.1f)
    , GunFireAccuracyConeAngle(0.0f)
    , GunFireAccuracyPower(1.0f)
    , GunFireAccuracyPowerNpc(6.0f)
    , GunRangeThreshold(1.0f)
    , GunRangeThresholdVariancePercent(0.20000000298023224f)
    , GunTargetPointSwitchTime(1.5f)
    , AutoTurretIntervalTime(0.0f)
    , AutoTurretBurstIntervalTime(1.0f)
    , AutoTurretNoBurstIntervalTime(0.0f)
    , AutoTurretBurstIntervalVariancePercent(0.1f)
{
}

Personality::GunUseStruct& Personality::GunUseStruct::operator=(GunUseStruct const& Other)
{
    std::memcpy(this, &Other, sizeof(*this));
    return *this;
}

Personality::MineUseStruct::MineUseStruct()
    : MineLaunchInterval(0.0f)
    , MineLaunchConeAngle(0.39269909262657166f)
    , MineLaunchRange(400.0f)
{
}

Personality::MineUseStruct& Personality::MineUseStruct::operator=(MineUseStruct const& Other)
{
    std::memcpy(this, &Other, sizeof(*this));
    return *this;
}

Personality::MissileUseStruct::MissileUseStruct()
    : MissileLaunchRange(-1.0f)
    , bMissileLaunchAllowOutOfRange(0)
    , MissileLaunchIntervalTime(5.0f)
    , MissileLaunchIntervalVariancePercent(0.4000000059604645f)
    , MissileLaunchConeAngle(0.39269909262657166f)
    , AntiCruiseMissileMinDistance(150.0f)
    , AntiCruiseMissileMaxDistance(2500.0f)
    , AntiCruiseMissilePreFireDelay(2.0f)
    , AntiCruiseMissileIntervalTime(5.0f)
{
}

Personality::MissileUseStruct& Personality::MissileUseStruct::operator=(MissileUseStruct const& Other)
{
    std::memcpy(this, &Other, sizeof(*this));
    return *this;
}

Personality::DamageReactionStruct::DamageReactionStruct()
    : EvadeBreakDamageTriggerPercent(1.0f)
    , EvadeDodgeMoreDamageTriggerPercent(0.20000000298023224f)
    , DropMinesDamageTriggerPercent(1.0f)
    , DropMinesDamageTriggerTime(1.0f)
    , EngineKillFaceDamageTriggerPercent(1.0f)
    , EngineKillFaceDamageTriggerTime(2.0f)
    , RollDamageTriggerPercent(1.0f)
    , RollDamageTriggerTime(1.0f)
    , AfterburnerDamageTriggerPercent(1.0f)
    , AfterburnerDamageTriggerTime(1.5f)
    , BrakeReverseDamageTriggerPercent(1.0f)
    , FireMissilesDamageTriggerPercent(1.0f)
    , FireMissilesDamageTriggerTime(0.5f)
    , FireGunsDamageTriggerPercent(1.0f)
    , FireGunsDamageTriggerTime(1.0f)
    , Field_3C(0.0f) // not written by retail ctor
    , Field_40(0.0f) // not written by retail ctor
{
}

Personality::DamageReactionStruct& Personality::DamageReactionStruct::operator=(DamageReactionStruct const& Other)
{
    std::memcpy(this, &Other, sizeof(*this));
    return *this;
}

Personality::MissileReactionStruct::MissileReactionStruct()
    : EvadeBreakMissileReactionTime(0.0f)
    , EvadeSlideMissileReactionTime(0.0f)
    , EvadeAfterburnMissileReactionTime(0.0f)
    , EvadeMissileDistance(250.0f)
{
}

Personality::MissileReactionStruct& Personality::MissileReactionStruct::operator=(MissileReactionStruct const& Other)
{
    std::memcpy(this, &Other, sizeof(*this));
    return *this;
}

Personality::CountermeasureUseStruct::CountermeasureUseStruct()
    : CountermeasureActiveTime(1.0f)
    , CountermeasureUnactiveTime(0.0f)
{
}

Personality::CountermeasureUseStruct& Personality::CountermeasureUseStruct::operator=(CountermeasureUseStruct const& Other)
{
    std::memcpy(this, &Other, sizeof(*this));
    return *this;
}

Personality::FormationUseStruct::FormationUseStruct()
    : ForceAttackFormationActiveTime(1.0f)
    , ForceAttackFormationUnactiveTime(0.0f)
    , BreakFormationDamageTriggerPercent(1.0f)
    , BreakFormationDamageTriggerTime(4.0f)
    , BreakApartFormationDamageTriggerPercent(1.0f)
    , BreakApartFormationDamageTriggerTime(4.0f)
    , BreakFormationMissileReactionTime(0.0f)
    , BreakApartFormationMissileReactionTime(4.0f)
    , bBreakApartFormationOnBuzzHeadToward(0)
    , BreakFormationOnBuzzHeadTowardTime(4.0f)
    , bRegroupFormationOnBuzzHeadToward(0)
    , bBreakApartFormationOnBuzzPassBy(0)
    , BreakFormationOnBuzzPassByTime(4.0f)
    , bRegroupFormationOnBuzzPassBy(0)
    , bBreakApartFormationOnEvadeDodge(0)
    , BreakFormationOnEvadeDodgeTime(4.0f)
    , bRegroupFormationOnEvadeDodge(0)
    , bBreakApartFormationOnEvadeBreak(0)
    , BreakFormationOnEvadeBreakTime(4.0f)
    , bRegroupFormationOnEvadeBreak(0)
    , FormationExitMode(5)
    , FormationExitTopTurnBreakAwayThrottle(1.0f)
    , FormationExitRollOutrunThrottle(0.0f)
    , FormationExitMaxTime(0.0f)
    , bLeaderMakesMeTougher(0)
{
}

Personality::FormationUseStruct& Personality::FormationUseStruct::operator=(FormationUseStruct const& Other)
{
    std::memcpy(this, &Other, sizeof(*this));
    return *this;
}

// .text:062da980. Retail writes AttackPreferences[0], AttackPreferences[1].ShipClass and
// AttackSubtargetOrder[0..1] only; the rest of both arrays, bWaitForLeaderTarget and
// ScanForCargo are left as they were. The port zeroes them (over-initialisation).
Personality::JobStruct::JobStruct()
    : Field_00(0)
    , SceneToughnessThreshold(2)
    , TargetToughnessPreference(2)
    , Field_0C(10000.0f)
    , AttackPreferences()
    , AttackSubtargetOrder()
    , bWaitForLeaderTarget(0)
    , MaximumLeaderTargetDistance(5000.0f)
    , FieldTargeting(3)
    , LootPreference(0)
    , LootFleeThreshold(2)
    , ScanForCargoCount(0)
    , ScanForCargo()
    , FleeSceneThreatStyle(3)
    , FleeWhenHullDamagedPercent(0.0f)
    , bFleeWhenLeaderFleesStyle(1)
    , bFleeNoWeaponsStyle(1)
    , bAllowPlayerTargeting(1)
    , ScanChance(SCAN_CHANCE_DEFAULT)
    , bForceAttackFormation(0)
    , bForceAttackFormationSet(0)
{
    AttackPreferences[0].ShipClass = 11;      // ANYTHING
    AttackPreferences[0].Range = 5000.0f;
    AttackPreferences[0].WeaponFlags = 0xF;   // every weapon type
    AttackPreferences[1].ShipClass = 12;      // list terminator
    AttackSubtargetOrder[0] = 6;              // ANYTHING
    AttackSubtargetOrder[1] = 7;              // list terminator
}

Personality::JobStruct& Personality::JobStruct::operator=(JobStruct const& Other)
{
    // .text:062a8890: REP MOVSD of 0x4E dwords, no self test.
    std::memcpy(this, &Other, sizeof(*this));
    return *this;
}

// ============================================================================
// Personality
// ============================================================================

// Export ordinal 1947, retail .rdata 0x063a05a8 = -1.0f. Seed for JobStruct::ScanChance
// (loaded at 062da9c1 and stored to JobStruct+0x130); see common_pub.h for the write-up.
float const Personality::SCAN_CHANCE_DEFAULT = -1.0f;

Personality::Personality()
    : EvadeDodgeParams()
    , EvadeBreakParams()
    , BuzzHeadParams()
    , BuzzPassParams()
    , TrailParams()
    , StrafeParams()
    , EngineKillParams()
    , RepairParams()
    , GunParams()
    , MineParams()
    , MissileParams()
    , DamageReaction()
    , MissileReaction()
    , CMParams()
    , FormationParams()
    , JobParams()
{
}

// No user copy constructor: retail exports none (see common_pub.h); the
// implicit one is the same bitwise copy this file used to spell out by hand.

Personality& Personality::operator=(Personality const& Other)
{
    std::memcpy(this, &Other, sizeof(*this));
    return *this;
}

float Personality::get_toughness_ratio(TOUGHNESS_TYPE Type)
{
    // Common.dll.c 0x48780 — a hardcoded switch, not a table lookup.
    switch (Type)
    {
    case ToughnessDefault: return 0.15f;
    case ToughnessHull:    return 0.35f;
    case ToughnessShield:  return 0.65f;
    case ToughnessMax:     return 0.85f;
    case 4:                return 1.0f;
    default:               return 0.5f;
    }
}

double Personality::get_range_threshold_value()
{
    // .text:062a87e0: FLD float 5000.0 (DAT_0639e0c8), then for JobParams.Field_00 == 1 /
    // == 2 FMUL double 2/3 (DAT_0639e0f0) / 1/3 (DAT_0639e0e8), returned unrounded in ST0.
    double const Base = 5000.0f;
    if (JobParams.Field_00 == 1)
        return Base * 0.66666666666666663;
    if (JobParams.Field_00 == 2)
        return Base * 0.33333333333333331;
    return Base;
}

// ============================================================================
// BaseOp
// ============================================================================

BaseOp::BaseOp(OP_TYPE Type)
    : OpType(Type)
    , Priority(0)
{
}

BaseOp::BaseOp(BaseOp const& Other)
    : OpType(Other.OpType)
    , Priority(Other.Priority)
{
}

// ============================================================================
// Directive operations
// ============================================================================

DirectiveGotoOp::DirectiveGotoOp()
    : BaseOp(OpType_DirectiveGotoOp)
    , Field_0C(3)
    , Destination{0.0f, 0.0f, 0.0f}
    , Field_1C(0)
    , Field_20(0), Field_24(0), Field_28(0), Field_2C(0)
    , Field_30(0), Field_34(0), Field_38(0), Field_3C(0)
    , Field_40(0), Field_44(0), Field_48(0), Field_4C(0)
    , Field_50(500.0f)
    , Field_54(-1.0f)
    , Field_58(0), Field_59(1), Field_5A(0), Field_5B(0)
    , Field_5C(0)
    , Field_60(200.0f)
    , Field_64(500.0f)
    , Field_68(0)
    , Field_6C(0)
{
}

DirectiveGotoOp::DirectiveGotoOp(DirectiveGotoOp const& Other)
    : BaseOp(Other)
    , Field_0C(Other.Field_0C)
    , Destination(Other.Destination)
    , Field_1C(Other.Field_1C)
    , Field_20(Other.Field_20), Field_24(Other.Field_24), Field_28(Other.Field_28), Field_2C(Other.Field_2C)
    , Field_30(Other.Field_30), Field_34(Other.Field_34), Field_38(Other.Field_38), Field_3C(Other.Field_3C)
    , Field_40(Other.Field_40), Field_44(Other.Field_44), Field_48(Other.Field_48), Field_4C(Other.Field_4C)
    , Field_50(Other.Field_50)
    , Field_54(Other.Field_54)
    , Field_58(Other.Field_58), Field_59(Other.Field_59), Field_5A(Other.Field_5A), Field_5B(Other.Field_5B)
    , Field_5C(Other.Field_5C)
    , Field_60(Other.Field_60)
    , Field_64(Other.Field_64)
    , Field_68(Other.Field_68)
    , Field_6C(Other.Field_6C)
{
}

bool DirectiveGotoOp::validate()
{
    // Common.dll.c 0x7aea0 — the NaN-safe-squared-magnitude checks in retail
    // collapse to plain "!= 0.0f" comparisons under IEEE-754 (NaN compares
    // unequal to everything, including itself).
    if (Field_1C != 0 &&
        (Destination.x != 0.0f || Destination.y != 0.0f || Destination.z != 0.0f))
        return false;
    if (Field_50 < 0.0f)
        return false;
    if (Field_5A != 0 && Field_5B != 0)
        return false;
    if (Field_64 < Field_60)
        return false;
    return true;
}

DirectiveFollowOp::DirectiveFollowOp()
    : BaseOp(OpType_DirectiveFollowOp)
    , TargetId(0)
    , FollowDistance(150.0f)
    , Field_14(0.0f)
    , Field_18(0.0f)
    , Field_1C(0.0f)
    , Field_20(400.0f)
{
}

DirectiveFollowOp::DirectiveFollowOp(DirectiveFollowOp const& Other)
    : BaseOp(Other)
    , TargetId(Other.TargetId)
    , FollowDistance(Other.FollowDistance)
    , Field_14(Other.Field_14)
    , Field_18(Other.Field_18)
    , Field_1C(Other.Field_1C)
    , Field_20(Other.Field_20)
{
}

bool DirectiveFollowOp::validate()
{
    return TargetId != 0;
}

DirectiveFaceOp::DirectiveFaceOp()
    // .text:062dafb0 stores Mode, TargetId, FacePoint (z, y, x) and Field_44 = 1 only;
    // Orientation stays as it was. The port zeroes it (over-initialisation).
    : BaseOp(OpType_DirectiveFaceOp)
    , Mode(0)
    , TargetId(0)
    , FacePoint{0.0f, 0.0f, 0.0f}
    , Orientation(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f)
    , Field_44(1)
{
}

DirectiveFaceOp::DirectiveFaceOp(DirectiveFaceOp const& Other)
    // .text:062a8b70: BaseOp words, Mode, TargetId, FacePoint (3 dwords), Orientation
    // (REP MOVSD 9), the byte at +0x44.
    : BaseOp(Other)
    , Mode(Other.Mode)
    , TargetId(Other.TargetId)
    , FacePoint(Other.FacePoint)
    , Orientation(Other.Orientation)
    , Field_44(Other.Field_44)
{
}

bool DirectiveFaceOp::validate()
{
    // Common.dll.c 0x7afe0
    if (Mode == 0)
        return false;
    if (Mode == 1 && TargetId == 0)
        return false;
    return true;
}

DirectiveStrafeOp::DirectiveStrafeOp()
    : BaseOp(OpType_DirectiveStrafeOp)
    , TargetId(0)
{
}

DirectiveStrafeOp::DirectiveStrafeOp(DirectiveStrafeOp const& Other)
    : BaseOp(Other)
    , TargetId(Other.TargetId)
{
}

bool DirectiveStrafeOp::validate()
{
    return TargetId != 0;
}

DirectiveTrailOp::DirectiveTrailOp()
    : BaseOp(OpType_DirectiveTrailOp)
    , TargetId(0)
    , TrailDistance(150.0f)
    , Field_14(0.0f)
    , Field_18(0.0f)
{
}

DirectiveTrailOp::DirectiveTrailOp(DirectiveTrailOp const& Other)
    : BaseOp(Other)
    , TargetId(Other.TargetId)
    , TrailDistance(Other.TrailDistance)
    , Field_14(Other.Field_14)
    , Field_18(Other.Field_18)
{
}

bool DirectiveTrailOp::validate()
{
    return TargetId != 0;
}

DirectiveRamOp::DirectiveRamOp()
    : BaseOp(OpType_DirectiveRamOp)
    , TargetId(0)
{
}

DirectiveRamOp::DirectiveRamOp(DirectiveRamOp const& Other)
    : BaseOp(Other)
    , TargetId(Other.TargetId)
{
}

bool DirectiveRamOp::validate()
{
    return TargetId != 0;
}

DirectiveTractorBeamOp::DirectiveTractorBeamOp()
    : BaseOp(OpType_DirectiveTractorBeamOp)
    , TargetId(0)
    , Field_10(1){
}

DirectiveTractorBeamOp::DirectiveTractorBeamOp(DirectiveTractorBeamOp const& Other)
    : BaseOp(Other)
    , TargetId(Other.TargetId)
    , Field_10(Other.Field_10){
}

bool DirectiveTractorBeamOp::validate()
{
    return TargetId != 0;
}

DirectiveFormationOp::DirectiveFormationOp()
    : BaseOp(OpType_DirectiveFormationOp)
{
}

DirectiveFormationOp::DirectiveFormationOp(DirectiveFormationOp const& Other)
    : BaseOp(Other)
{
}

bool DirectiveFormationOp::validate()
{
    return true; // Common.dll.c 0x1a710 (shared with Cancel/DrasticEvade/Idle)
}

DirectiveEvadeOp::DirectiveEvadeOp()
    : BaseOp(OpType_DirectiveEvadeOp)
    , ThreatId(0)
{
}

DirectiveEvadeOp::DirectiveEvadeOp(DirectiveEvadeOp const& Other)
    : BaseOp(Other)
    , ThreatId(Other.ThreatId)
{
}

bool DirectiveEvadeOp::validate()
{
    return ThreatId != 0;
}

DirectiveDrasticEvadeOp::DirectiveDrasticEvadeOp()
    : BaseOp(OpType_DirectiveDrasticEvadeOp)
    , ThreatId(0)
    , Field_10(0)
    , Field_14(0)
    , Field_18(0)
    , Field_1C(1.0f)
    , Field_20(0)
    , Field_24(0)
    , Field_28(0)
    , Field_2C(0)
{
}

DirectiveDrasticEvadeOp::DirectiveDrasticEvadeOp(DirectiveDrasticEvadeOp const& Other)
    : BaseOp(Other)
    , ThreatId(Other.ThreatId)
    , Field_10(Other.Field_10)
    , Field_14(Other.Field_14)
    , Field_18(Other.Field_18)
    , Field_1C(Other.Field_1C)
    , Field_20(Other.Field_20)
    , Field_24(Other.Field_24)
    , Field_28(Other.Field_28)
    , Field_2C(Other.Field_2C)
{
}

bool DirectiveDrasticEvadeOp::validate()
{
    return true; // Common.dll.c 0x1a710 (shared with Cancel/Formation/Idle)
}

DirectiveDockOp::DirectiveDockOp()
    : BaseOp(OpType_DirectiveDockOp)
    , DockObjectId(0)
    , Field_10(0)
    , Field_14(0)    , DockPortIndex(-1)
    , Field_1C(0)
    , Field_20(200.0f)
    , Field_24(500.0f)
    , Field_28(0)
{
}

DirectiveDockOp::DirectiveDockOp(DirectiveDockOp const& Other)
    : BaseOp(Other)
    , DockObjectId(Other.DockObjectId)
    , Field_10(Other.Field_10)
    , Field_14(Other.Field_14)    , DockPortIndex(Other.DockPortIndex)
    , Field_1C(Other.Field_1C)
    , Field_20(Other.Field_20)
    , Field_24(Other.Field_24)
    , Field_28(Other.Field_28)
{
}

bool DirectiveDockOp::validate()
{
    // Common.dll.c 0x7b100
    if (DockObjectId == 0)
        return false;
    if (Field_14 != 0 && DockPortIndex == -1)
        return false;
    if (Field_24 < Field_20)
        return false;
    return true;
}

DirectiveLaunchOp::DirectiveLaunchOp()
    : BaseOp(OpType_DirectiveLaunchOp)
    , LaunchObjectId(0)
    , Field_10(-1)
    , Field_14(0)
{
}

DirectiveLaunchOp::DirectiveLaunchOp(DirectiveLaunchOp const& Other)
    : BaseOp(Other)
    , LaunchObjectId(Other.LaunchObjectId)
    , Field_10(Other.Field_10)
    , Field_14(Other.Field_14)
{
}

bool DirectiveLaunchOp::validate()
{
    // Common.dll.c 0x7b160
    if (LaunchObjectId == 0)
        return false;
    if (Field_10 == -1)
        return false;
    return Field_14 != 0;
}

DirectiveInstantTradelaneOp::DirectiveInstantTradelaneOp()
    : BaseOp(OpType_DirectiveInstantTradelaneOp)
    , TradeLaneId(0)
    , Field_10(0)
    , Field_14(0)
{
}

DirectiveInstantTradelaneOp::DirectiveInstantTradelaneOp(DirectiveInstantTradelaneOp const& Other)
    : BaseOp(Other)
    , TradeLaneId(Other.TradeLaneId)
    , Field_10(Other.Field_10)
    , Field_14(Other.Field_14)
{
}

bool DirectiveInstantTradelaneOp::validate()
{
    return TradeLaneId != 0;
}

DirectiveDelayOp::DirectiveDelayOp()
    : BaseOp(OpType_DirectiveDelayOp)
    , DelaySeconds(0.0f)
{
}

DirectiveDelayOp::DirectiveDelayOp(DirectiveDelayOp const& Other)
    : BaseOp(Other)
    , DelaySeconds(Other.DelaySeconds)
{
}

bool DirectiveDelayOp::validate()
{
    return DelaySeconds != 0.0f;
}

DirectiveCancelOp::DirectiveCancelOp()
    : BaseOp(OpType_DirectiveCancelOp)
{
}

DirectiveCancelOp::DirectiveCancelOp(DirectiveCancelOp const& Other)
    : BaseOp(Other)
{
}

bool DirectiveCancelOp::validate()
{
    return true; // Common.dll.c 0x1a710 (shared with DrasticEvade/Formation/Idle)
}

DirectiveWaitForShip::DirectiveWaitForShip()
    : BaseOp(OpType_DirectiveWaitForShip)
    , ShipId(0)
    , Field_10(0)
{
}

DirectiveWaitForShip::DirectiveWaitForShip(DirectiveWaitForShip const& Other)
    : BaseOp(Other)
    , ShipId(Other.ShipId)
    , Field_10(Other.Field_10)
{
}

bool DirectiveWaitForShip::validate()
{
    return ShipId != 0;
}

DirectiveWaitForPlayerManeuverOp::DirectiveWaitForPlayerManeuverOp()
    : BaseOp(OpType_DirectiveWaitForPlayerManeuverOp)
    , BehaviorName(nullptr)
{
}

DirectiveWaitForPlayerManeuverOp::DirectiveWaitForPlayerManeuverOp(DirectiveWaitForPlayerManeuverOp const& Other)
    : BaseOp(Other)
    , BehaviorName(Other.BehaviorName)
{
}

bool DirectiveWaitForPlayerManeuverOp::validate()
{
    // Common.dll.c 0x7b230
    int id = BehaviorTypes::get_behavior_id(BehaviorName);
    return id < 0x15 && id != -1;
}

DirectiveIdleOp::DirectiveIdleOp()
    : BaseOp(OpType_DirectiveIdleOp)
{
}

DirectiveIdleOp::DirectiveIdleOp(DirectiveIdleOp const& Other)
    : BaseOp(Other)
{
}

bool DirectiveIdleOp::validate()
{
    return true; // Common.dll.c 0x1a710 (shared with Cancel/DrasticEvade/Formation)
}

// DirectiveGuideOp — Common.dll.c 0x7b2a0. Embeds a default-constructed
// DirectiveGotoOp at +0x0C and a default-constructed DirectiveFollowOp at
// +0x7C (retail writes the latter's fields inline rather than calling
// DirectiveFollowOp's ctor, but the values it writes are byte-for-byte
// DirectiveFollowOp's own defaults).
DirectiveGuideOp::DirectiveGuideOp()
    : BaseOp(OpType_DirectiveGuideOp)
    , GotoParams()
    , FollowParams()
    , Field_A0(1)    , Field_A4(10.0f)
    , Field_A8(0.3926991f)
    , Field_AC(3)
    , Field_B0(10.0f)
    , Field_B4(-10.0f)
    , Field_B8(400.0f)
{
}

DirectiveGuideOp::DirectiveGuideOp(DirectiveGuideOp const& Other)
    : BaseOp(Other)
    , GotoParams(Other.GotoParams)
    , FollowParams(Other.FollowParams)
    , Field_A0(Other.Field_A0)    , Field_A4(Other.Field_A4)
    , Field_A8(Other.Field_A8)
    , Field_AC(Other.Field_AC)
    , Field_B0(Other.Field_B0)
    , Field_B4(Other.Field_B4)
    , Field_B8(Other.Field_B8)
{
}

bool DirectiveGuideOp::validate()
{
    // Common.dll.c 0x7b350 — dispatches through both embedded objects' own
    // (real) vtables; calling their validate() directly is equivalent.
    return GotoParams.validate() && FollowParams.validate();
}

// ============================================================================
// Parameter structs
// ============================================================================
//
// RE NOTE (2026-09-05): SetFlagParams and SetZoneBehaviorParams did not
// derive from BaseOp in the previous port despite their real copy ctors
// (Common.dll.c 0x48DA0 / 0x48D10) showing the same BaseOp::_vftable_-then-
// own-vtable patch pattern as every other class here. See common_pub.h.

SetFlagParams::SetFlagParams()
    : BaseOp(OpType_SetFlagParams)
    , FlagId(2)
    , bValue(0){
}

SetFlagParams::SetFlagParams(SetFlagParams const& Other)
    : BaseOp(Other)
    , FlagId(Other.FlagId)
    , bValue(Other.bValue){
}

bool SetFlagParams::validate()
{
    return FlagId != 2;
}

SetPersonalityParams::SetPersonalityParams()
    : BaseOp(OpType_SetPersonalityParams)
    , StateGraphId(-1)
    , ContentCallbackObject((ContentCallback*)-1)
    , DirectiveCallbackObject((DirectiveCallback*)-1)
    , bSetPersonality(0)
    , Value()
{
}

SetPersonalityParams::SetPersonalityParams(SetPersonalityParams const& Other)
    : BaseOp(Other)
    , StateGraphId(Other.StateGraphId)
    , ContentCallbackObject(Other.ContentCallbackObject)
    , DirectiveCallbackObject(Other.DirectiveCallbackObject)
    , bSetPersonality(Other.bSetPersonality)
    , Value(Other.Value)
{
}

bool SetPersonalityParams::validate()
{
    // Common.dll.c 0x7ad50 — checks StateGraphId against its -1 default sentinel.
    return StateGraphId != -1;
}

SetZoneBehaviorParams::SetZoneBehaviorParams()
    // .text:062dad60 writes neither Mode nor the box; the port zeroes them (over-initialisation).
    : BaseOp(OpType_SetZoneBehaviorParams)
    , Mode(0)
    , InvertMode(2)
    , TargetPoint{0.0f, 0.0f, 0.0f}
    , TargetId(0)
    , BoxMin{0.0f, 0.0f, 0.0f}
    , BoxMax{0.0f, 0.0f, 0.0f}
    , Radius(0.0f)
{
}

SetZoneBehaviorParams::SetZoneBehaviorParams(SetZoneBehaviorParams const& Other)
    : BaseOp(Other)
    , Mode(Other.Mode)
    , InvertMode(Other.InvertMode)
    , TargetPoint(Other.TargetPoint)
    , TargetId(Other.TargetId)
    , BoxMin(Other.BoxMin)
    , BoxMax(Other.BoxMax)
    , Radius(Other.Radius)
{
}

bool SetZoneBehaviorParams::validate()
{
    // .text:062dad90. Radius: `TEST AH,0x44; JP` continues on not-equal and on NaN.
    // TargetPoint: ((x*x + y*y) + z*z) on the FPU stack, `TEST AH,0x41; JNZ` -> false
    // unless the sum is greater than 0 (NaN -> false).
    if (InvertMode != 2)
    {
        if (Radius == 0.0f)
            return false;
        if (TargetId == 0)
        {
            return ((double)TargetPoint.x * TargetPoint.x + (double)TargetPoint.y * TargetPoint.y) +
                   (double)TargetPoint.z * TargetPoint.z > 0.0;
        }
    }
    return true;
}


// ============================================================================
// ContentCallback -- the ten virtuals are header-inline (common_pub.h), as in
// retail, which compiled and exported them in each module that saw the header.
// ============================================================================

// ============================================================================
// DirectiveCallback
// ============================================================================

// Pure in retail (no DirectiveCallback vftable exists); nothing to define here.

}} // namespace pub::AI
