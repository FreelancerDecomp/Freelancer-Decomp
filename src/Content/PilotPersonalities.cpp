// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Content.dll TU45 (06f36f10-06f46940): pilot personalities.
//
// Listing: DLLS\BIN\Content.dll.txt. Every function below names the retail
// address it ports. The std::map template instantiations retail emits for the
// seventeen maps (_Insert/_Buynode/_Erase/_Lrotate/_Rrotate/iterator steps,
// 06f3e550-06f46920) and the per-map construct/atexit-destroy thunks
// (06f38f40-06f39f70) are the std::map objects themselves.

#include "PilotPersonalities.h"

#include "ContentStaticIds.h"
#include "common_ini.h"

#include <cstdlib>
#include <cstring>
#include <map>
#include <utility>

namespace
{
    typedef pub::AI::Personality FPersonality;

    // Value of a DAT_06fd0aa0 node (node+0x10, 0x11 dwords), filled by
    // FUN_06f3cc10 in the order of its locals local_50..local_10.
    struct FPilotRefs
    {
        ID_String Inherit;           // +0x00 inherit
        ID_String EvadeDodge;        // +0x04 evade_dodge_id
        ID_String EvadeBreak;        // +0x08 evade_break_id
        ID_String BuzzHeadToward;    // +0x0C buzz_head_toward_id
        ID_String BuzzPassBy;        // +0x10 buzz_pass_by_id
        ID_String Trail;             // +0x14 trail_id
        ID_String Strafe;            // +0x18 strafe_id
        ID_String EngineKill;        // +0x1C engine_kill_id
        ID_String Repair;            // +0x20 repair_id
        ID_String Gun;               // +0x24 gun_id
        ID_String Mine;              // +0x28 mine_id
        ID_String Missile;           // +0x2C missile_id
        ID_String DamageReaction;    // +0x30 damage_reaction_id
        ID_String MissileReaction;   // +0x34 missile_reaction_id
        ID_String Countermeasure;    // +0x38 countermeasure_id
        ID_String Formation;         // +0x3C formation_id
        ID_String Job;               // +0x40 job_id
    };

    static_assert(sizeof(FPilotRefs) == 0x44, "FPilotRefs: 0x11 dwords (MOVSD 0x11)");

    // Retail construction order (_initterm 06fcdca0.., one construct/atexit
    // thunk pair per map, 06f38f40..06f39f70): pilots first, Job last.
    std::map<unsigned int, FPilotRefs> GPilots;                                             // DAT_06fd0aa0
    std::map<unsigned int, FPersonality::EvadeDodgeUseStruct> GEvadeDodgeBlocks;           // DAT_06fd0a8c
    std::map<unsigned int, FPersonality::EvadeBreakUseStruct> GEvadeBreakBlocks;           // DAT_06fd0a78
    std::map<unsigned int, FPersonality::BuzzHeadTowardUseStruct> GBuzzHeadTowardBlocks;   // DAT_06fd0a64
    std::map<unsigned int, FPersonality::BuzzPassByUseStruct> GBuzzPassByBlocks;           // DAT_06fd0a50
    std::map<unsigned int, FPersonality::TrailUseStruct> GTrailBlocks;                     // DAT_06fd0a3c
    std::map<unsigned int, FPersonality::StrafeUseStruct> GStrafeBlocks;                   // DAT_06fd0a28
    std::map<unsigned int, FPersonality::EngineKillUseStruct> GEngineKillBlocks;           // DAT_06fd0a14
    std::map<unsigned int, FPersonality::RepairUseStruct> GRepairBlocks;                   // DAT_06fd0a00
    std::map<unsigned int, FPersonality::GunUseStruct> GGunBlocks;                         // DAT_06fd09ec
    std::map<unsigned int, FPersonality::MineUseStruct> GMineBlocks;                       // DAT_06fd09d8
    std::map<unsigned int, FPersonality::MissileUseStruct> GMissileBlocks;                 // DAT_06fd09c4
    std::map<unsigned int, FPersonality::DamageReactionStruct> GDamageReactionBlocks;      // DAT_06fd09b0
    std::map<unsigned int, FPersonality::MissileReactionStruct> GMissileReactionBlocks;    // DAT_06fd099c
    std::map<unsigned int, FPersonality::CountermeasureUseStruct> GCountermeasureBlocks;   // DAT_06fd0988
    std::map<unsigned int, FPersonality::FormationUseStruct> GFormationBlocks;             // DAT_06fd0974
    std::map<unsigned int, FPersonality::JobStruct> GJobBlocks;                            // DAT_06fd0960

    // .rdata DAT_06fb6060: degrees -> radians (FMUL float ptr).
    float const GDegToRad = 0.017453292f;

    // DAT_06fd0ab8: FUN_06f3c9b0 copies the weapon-type parameter here and
    // _strupr's it before the strstr tests.
    char GAttackPreferenceWeapons[256];

    // .rdata 06fb95e8..06fb97b8: 12 records of {char Name[0x20]; int ClassId;}
    // (stride 0x24), read by attack_order (06f3c190) and FUN_06f3c9b0.
    struct FShipClassName
    {
        char const* Name;
        int ClassId;
    };

    FShipClassName const GShipClassNames[12] = {
        { "FIGHTER", 0 },
        { "FREIGHTER", 1 },
        { "TRANSPORT", 2 },
        { "GUNBOAT", 3 },
        { "CRUISER", 4 },
        { "CAPITAL", 5 },
        { "TRADELANE", 6 },
        { "JUMPGATE", 7 },
        { "WEAPONS_PLATFORM", 8 },
        { "DESTROYABLE_DEPOT", 9 },
        { "SOLAR", 10 },
        { "ANYTHING", 11 },
    };

    // JobStruct +0x10: attack preference records {class, range, weapon flags},
    // stride 0xC, terminated by class 0xC.
    typedef FPersonality::JobStruct::FAttackPreference FAttackPreference;

    // FUN_06f3c970. Also inlined verbatim at every other TRUE/FALSE key of the
    // block loaders: "TRUE" -> true, otherwise "not FALSE".
    bool ParseTrueFalse(char const* Text)
    {
        if (_stricmp(Text, "TRUE") == 0)
            return true;
        return _stricmp(Text, "FALSE") != 0;
    }

    // JobBlock EASIEST..HARDEST keys (inlined at 06f3c190): unknown -> 2.
    int ParseToughness(char const* Text)
    {
        if (_stricmp(Text, "EASIEST") == 0)
            return 0;
        if (_stricmp(Text, "EASY") == 0)
            return 1;
        if (_stricmp(Text, "EQUAL") == 0)
            return 2;
        if (_stricmp(Text, "HARD") == 0)
            return 3;
        return _stricmp(Text, "HARDEST") == 0 ? 4 : 2;
    }

    // FUN_06f3a2d0: [EvadeDodgeBlock] values.
    void LoadEvadeDodgeBlock(INI_Reader* Reader, FPersonality::EvadeDodgeUseStruct* Out, ID_String* OutNickname)
    {
        char Name[256];
        if (!Reader->read_value())
            return;
        do
        {
            if (Reader->is_value("nickname"))
                OutNickname->Id = CreateID(Reader->get_value_string());
            else if (Reader->is_value("evade_activate_range"))
                Out->EvadeActivateRange = Reader->get_value_float(0);
            else if (Reader->is_value("evade_dodge_cone_angle"))
                Out->EvadeDodgeConeAngle = Reader->get_value_float(0) * GDegToRad;
            else if (Reader->is_value("evade_dodge_cone_angle_variance_percent"))
                Out->EvadeDodgeConeAngleVariancePercent = Reader->get_value_float(0);
            else if (Reader->is_value("evade_dodge_waggle_axis_cone_angle"))
                Out->EvadeDodgeWaggleAxisConeAngle = Reader->get_value_float(0) * GDegToRad;
            else if (Reader->is_value("evade_dodge_roll_angle"))
                Out->EvadeDodgeRollAngle = Reader->get_value_float(0) * GDegToRad;
            else if (Reader->is_value("evade_dodge_interval_time"))
                Out->EvadeDodgeIntervalTime = Reader->get_value_float(0);
            else if (Reader->is_value("evade_dodge_interval_time_variance_percent"))
                Out->EvadeDodgeIntervalTimeVariancePercent = Reader->get_value_float(0);
            else if (Reader->is_value("evade_dodge_distance"))
                Out->EvadeDodgeDistance = Reader->get_value_float(0);
            else if (Reader->is_value("evade_dodge_time"))
                Out->EvadeDodgeTime = Reader->get_value_float(0);
            else if (Reader->is_value("evade_dodge_slide_throttle"))
                Out->EvadeDodgeSlideThrottle = Reader->get_value_float(0);
            else if (Reader->is_value("evade_dodge_turn_throttle"))
                Out->EvadeDodgeTurnThrottle = Reader->get_value_float(0);
            else if (Reader->is_value("evade_dodge_corkscrew_roll_throttle"))
                Out->EvadeDodgeCorkscrewRollThrottle = Reader->get_value_float(0);
            else if (Reader->is_value("evade_dodge_corkscrew_roll_flip_direction"))
                Out->bEvadeDodgeCorkscrewRollFlipDirection = ParseTrueFalse(Reader->get_value_string());
            else if (Reader->is_value("evade_dodge_corkscrew_turn_throttle"))
                Out->EvadeDodgeCorkscrewTurnThrottle = Reader->get_value_float(0);
            else if (Reader->is_value("evade_dodge_direction_weight"))
            {
                std::strcpy(Name, Reader->get_value_string(0));
                float Weight = Reader->get_value_float(1);
                if (_stricmp(Name, "RIGHT") == 0)
                    Out->EvadeDodgeDirectionWeightRight = Weight;
                else if (_stricmp(Name, "LEFT") == 0)
                    Out->EvadeDodgeDirectionWeightLeft = Weight;
                else if (_stricmp(Name, "UP") == 0)
                    Out->EvadeDodgeDirectionWeightUp = Weight;
                else if (_stricmp(Name, "DOWN") == 0)
                    Out->EvadeDodgeDirectionWeightDown = Weight;
            }
            else if (Reader->is_value("evade_dodge_style_weight"))
            {
                std::strcpy(Name, Reader->get_value_string(0));
                float Weight = Reader->get_value_float(1);
                if (_stricmp(Name, "WAGGLE") == 0)
                    Out->EvadeDodgeStyleWeightWaggle = Weight;
                else if (_stricmp(Name, "WAGGLE_RANDOM") == 0)
                    Out->EvadeDodgeStyleWeightWaggleRandom = Weight;
                else if (_stricmp(Name, "SLIDE") == 0)
                    Out->EvadeDodgeStyleWeightSlide = Weight;
                else if (_stricmp(Name, "CORKSCREW") == 0)
                    Out->EvadeDodgeStyleWeightCorkscrew = Weight;
            }
        } while (Reader->read_value());
    }

    // FUN_06f3a690: [EvadeBreakBlock] values.
    void LoadEvadeBreakBlock(INI_Reader* Reader, FPersonality::EvadeBreakUseStruct* Out, ID_String* OutNickname)
    {
        char Name[256];
        if (!Reader->read_value())
            return;
        do
        {
            if (Reader->is_value("nickname"))
                OutNickname->Id = CreateID(Reader->get_value_string());
            else if (Reader->is_value("evade_break_time"))
                Out->EvadeBreakTime = Reader->get_value_float(0);
            else if (Reader->is_value("evade_break_interval_time"))
                Out->EvadeBreakIntervalTime = Reader->get_value_float(0);
            else if (Reader->is_value("evade_break_afterburner_delay"))
                Out->EvadeBreakAfterburnerDelay = Reader->get_value_float(0);
            else if (Reader->is_value("evade_break_afterburner_delay_variance_percent"))
                Out->EvadeBreakAfterburnerDelayVariancePercent = Reader->get_value_float(0);
            else if (Reader->is_value("evade_break_direction_weight"))
            {
                std::strcpy(Name, Reader->get_value_string(0));
                float Weight = Reader->get_value_float(1);
                if (_stricmp(Name, "RIGHT") == 0)
                    Out->EvadeBreakDirectionWeightRight = Weight;
                else if (_stricmp(Name, "LEFT") == 0)
                    Out->EvadeBreakDirectionWeightLeft = Weight;
                else if (_stricmp(Name, "UP") == 0)
                    Out->EvadeBreakDirectionWeightUp = Weight;
                else if (_stricmp(Name, "DOWN") == 0)
                    Out->EvadeBreakDirectionWeightDown = Weight;
            }
            else if (Reader->is_value("evade_break_roll_throttle"))
                Out->EvadeBreakRollThrottle = Reader->get_value_float(0);
            else if (Reader->is_value("evade_break_turn_throttle"))
                Out->EvadeBreakTurnThrottle = Reader->get_value_float(0);
            else if (Reader->is_value("evade_break_style_weight"))
            {
                std::strcpy(Name, Reader->get_value_string(0));
                float Weight = Reader->get_value_float(1);
                if (_stricmp(Name, "SIDEWAYS") == 0)
                    Out->EvadeBreakStyleWeightSideways = Weight;
                else if (_stricmp(Name, "OUTRUN") == 0)
                    Out->EvadeBreakStyleWeightOutrun = Weight;
                else if (_stricmp(Name, "REVERSE") == 0)
                    Out->EvadeBreakStyleWeightReverse = Weight;
            }
            else if (Reader->is_value("evade_break_attempt_reverse_time"))
                Out->EvadeBreakAttemptReverseTime = Reader->get_value_float(0);
            else if (Reader->is_value("evade_break_reverse_distance"))
                Out->EvadeBreakReverseDistance = Reader->get_value_float(0);
        } while (Reader->read_value());
    }

    // FUN_06f3a940: [BuzzHeadTowardBlock] values.
    void LoadBuzzHeadTowardBlock(INI_Reader* Reader, FPersonality::BuzzHeadTowardUseStruct* Out,
                                 ID_String* OutNickname)
    {
        char Name[256];
        if (!Reader->read_value())
            return;
        do
        {
            if (Reader->is_value("nickname"))
                OutNickname->Id = CreateID(Reader->get_value_string());
            else if (Reader->is_value("buzz_head_toward_style_weight"))
            {
                std::strcpy(Name, Reader->get_value_string(0));
                float Weight = Reader->get_value_float(1);
                if (_stricmp(Name, "STRAIGHT_TO") == 0)
                    Out->BuzzHeadTowardStyleWeightStraightTo = Weight;
                else if (_stricmp(Name, "SLIDE") == 0)
                    Out->BuzzHeadTowardStyleWeightSlide = Weight;
                else if (_stricmp(Name, "WAGGLE") == 0)
                    Out->BuzzHeadTowardStyleWeightWaggle = Weight;
            }
            else if (Reader->is_value("buzz_min_distance_to_head_toward"))
                Out->BuzzMinDistanceToHeadToward = Reader->get_value_float(0);
            else if (Reader->is_value("buzz_min_distance_to_head_toward_variance_percent"))
                Out->BuzzMinDistanceToHeadTowardVariancePercent = Reader->get_value_float(0);
            else if (Reader->is_value("buzz_max_time_to_head_away"))
                Out->BuzzMaxTimeToHeadAway = Reader->get_value_float(0);
            else if (Reader->is_value("buzz_head_toward_engine_throttle"))
                Out->BuzzHeadTowardEngineThrottle = Reader->get_value_float(0);
            else if (Reader->is_value("buzz_head_toward_turn_throttle"))
                Out->BuzzHeadTowardTurnThrottle = Reader->get_value_float(0);
            else if (Reader->is_value("buzz_head_toward_roll_throttle"))
                Out->BuzzHeadTowardRollThrottle = Reader->get_value_float(0);
            else if (Reader->is_value("buzz_head_toward_roll_flip_direction"))
                Out->bBuzzHeadTowardRollFlipDirection = ParseTrueFalse(Reader->get_value_string());
            else if (Reader->is_value("buzz_slide_throttle"))
                Out->BuzzSlideThrottle = Reader->get_value_float(0);
            else if (Reader->is_value("buzz_slide_interval_time"))
                Out->BuzzSlideIntervalTime = Reader->get_value_float(0);
            else if (Reader->is_value("buzz_slide_interval_time_variance_percent"))
                Out->BuzzSlideIntervalTimeVariancePercent = Reader->get_value_float(0);
            else if (Reader->is_value("buzz_dodge_direction_weight"))
            {
                std::strcpy(Name, Reader->get_value_string(0));
                float Weight = Reader->get_value_float(1);
                if (_stricmp(Name, "RIGHT") == 0)
                    Out->BuzzDodgeDirectionWeightRight = Weight;
                else if (_stricmp(Name, "LEFT") == 0)
                    Out->BuzzDodgeDirectionWeightLeft = Weight;
                else if (_stricmp(Name, "UP") == 0)
                    Out->BuzzDodgeDirectionWeightUp = Weight;
                else if (_stricmp(Name, "DOWN") == 0)
                    Out->BuzzDodgeDirectionWeightDown = Weight;
            }
            else if (Reader->is_value("buzz_dodge_turn_throttle"))
                Out->BuzzDodgeTurnThrottle = Reader->get_value_float(0);
            else if (Reader->is_value("buzz_dodge_cone_angle"))
                Out->BuzzDodgeConeAngle = Reader->get_value_float(0) * GDegToRad;
            else if (Reader->is_value("buzz_dodge_cone_angle_variance_percent"))
                Out->BuzzDodgeConeAngleVariancePercent = Reader->get_value_float(0);
            else if (Reader->is_value("buzz_dodge_waggle_axis_cone_angle"))
                Out->BuzzDodgeWaggleAxisConeAngle = Reader->get_value_float(0) * GDegToRad;
            else if (Reader->is_value("buzz_dodge_roll_angle"))
                Out->BuzzDodgeRollAngle = Reader->get_value_float(0) * GDegToRad;
            else if (Reader->is_value("buzz_dodge_interval_time"))
                Out->BuzzDodgeIntervalTime = Reader->get_value_float(0);
            else if (Reader->is_value("buzz_dodge_interval_time_variance_percent"))
                Out->BuzzDodgeIntervalTimeVariancePercent = Reader->get_value_float(0);
        } while (Reader->read_value());
    }

    // FUN_06f3ad40: [BuzzPassByBlock] values.
    void LoadBuzzPassByBlock(INI_Reader* Reader, FPersonality::BuzzPassByUseStruct* Out, ID_String* OutNickname)
    {
        char Name[256];
        if (!Reader->read_value())
            return;
        do
        {
            if (Reader->is_value("nickname"))
                OutNickname->Id = CreateID(Reader->get_value_string());
            else if (Reader->is_value("buzz_pass_by_style_weight"))
            {
                std::strcpy(Name, Reader->get_value_string(0));
                float Weight = Reader->get_value_float(1);
                if (_stricmp(Name, "BREAK_AWAY") == 0)
                    Out->BuzzPassByStyleWeightBreakAway = Weight;
                else if (_stricmp(Name, "STRAIGHT_BY") == 0)
                    Out->BuzzPassByStyleWeightStraightBy = Weight;
                else if (_stricmp(Name, "ENGINE_KILL") == 0)
                    Out->BuzzPassByStyleWeightEngineKill = Weight;
            }
            else if (Reader->is_value("buzz_distance_to_pass_by"))
                Out->BuzzDistanceToPassBy = Reader->get_value_float(0);
            else if (Reader->is_value("buzz_pass_by_time"))
                Out->BuzzPassByTime = Reader->get_value_float(0);
            else if (Reader->is_value("buzz_break_direction_weight"))
            {
                std::strcpy(Name, Reader->get_value_string(0));
                float Weight = Reader->get_value_float(1);
                if (_stricmp(Name, "RIGHT") == 0)
                    Out->BuzzBreakDirectionWeightRight = Weight;
                else if (_stricmp(Name, "LEFT") == 0)
                    Out->BuzzBreakDirectionWeightLeft = Weight;
                else if (_stricmp(Name, "UP") == 0)
                    Out->BuzzBreakDirectionWeightUp = Weight;
                else if (_stricmp(Name, "DOWN") == 0)
                    Out->BuzzBreakDirectionWeightDown = Weight;
            }
            else if (Reader->is_value("buzz_break_direction_cone_angle"))
                Out->BuzzBreakDirectionConeAngle = Reader->get_value_float(0);
            else if (Reader->is_value("buzz_break_turn_throttle"))
                Out->BuzzBreakTurnThrottle = Reader->get_value_float(0);
            else if (Reader->is_value("buzz_pass_by_roll_throttle"))
                Out->BuzzPassByRollThrottle = Reader->get_value_float(0);
            else if (Reader->is_value("buzz_drop_bomb_on_pass_by"))
                Out->bBuzzDropBombOnPassBy = ParseTrueFalse(Reader->get_value_string());
        } while (Reader->read_value());
    }

    // FUN_06f3b030: [TrailBlock] values.
    void LoadTrailBlock(INI_Reader* Reader, FPersonality::TrailUseStruct* Out, ID_String* OutNickname)
    {
        if (!Reader->read_value())
            return;
        do
        {
            if (Reader->is_value("nickname"))
                OutNickname->Id = CreateID(Reader->get_value_string());
            else if (Reader->is_value("trail_lock_cone_angle"))
                Out->TrailLockConeAngle = Reader->get_value_float(0) * GDegToRad;
            else if (Reader->is_value("trail_break_time"))
                Out->TrailBreakTime = Reader->get_value_float(0);
            else if (Reader->is_value("trail_min_no_lock_time"))
                Out->TrailMinNoLockTime = Reader->get_value_float(0);
            else if (Reader->is_value("trail_break_roll_throttle"))
                Out->TrailBreakRollThrottle = Reader->get_value_float(0);
            else if (Reader->is_value("trail_break_afterburner"))
                Out->bTrailBreakAfterburner = ParseTrueFalse(Reader->get_value_string());
            else if (Reader->is_value("trail_max_turn_throttle"))
                Out->TrailMaxTurnThrottle = Reader->get_value_float(0);
            else if (Reader->is_value("trail_distance"))
                Out->TrailDistance = Reader->get_value_float(0);
        } while (Reader->read_value());
    }

    // FUN_06f3b190: [StrafeBlock] values.
    void LoadStrafeBlock(INI_Reader* Reader, FPersonality::StrafeUseStruct* Out, ID_String* OutNickname)
    {
        if (!Reader->read_value())
            return;
        do
        {
            if (Reader->is_value("nickname"))
                OutNickname->Id = CreateID(Reader->get_value_string());
            else if (Reader->is_value("strafe_run_away_distance"))
                Out->StrafeRunAwayDistance = Reader->get_value_float(0);
            else if (Reader->is_value("strafe_attack_throttle"))
                Out->StrafeAttackThrottle = Reader->get_value_float(0);
            else if (Reader->is_value("strafe_turn_throttle"))
                Out->StrafeTurnThrottle = Reader->get_value_float(0);
        } while (Reader->read_value());
    }

    // FUN_06f3b240: [EngineKillBlock] values.
    void LoadEngineKillBlock(INI_Reader* Reader, FPersonality::EngineKillUseStruct* Out, ID_String* OutNickname)
    {
        if (!Reader->read_value())
            return;
        do
        {
            if (Reader->is_value("nickname"))
                OutNickname->Id = CreateID(Reader->get_value_string());
            else if (Reader->is_value("engine_kill_search_time"))
                Out->EngineKillSearchTime = Reader->get_value_float(0);
            else if (Reader->is_value("engine_kill_face_time"))
                Out->EngineKillFaceTime = Reader->get_value_float(0);
            else if (Reader->is_value("engine_kill_use_afterburner"))
            {
                // 06f3b2ee-06f3b31d: the bool goes through FILD into a float.
                Out->EngineKillUseAfterburner = (float)(ParseTrueFalse(Reader->get_value_string()) ? 1 : 0);
            }
            else if (Reader->is_value("engine_kill_afterburner_time"))
                Out->EngineKillAfterburnerTime = Reader->get_value_float(0);
            else if (Reader->is_value("engine_kill_max_target_distance"))
                Out->EngineKillMaxTargetDistance = Reader->get_value_float(0);
        } while (Reader->read_value());
    }

    // FUN_06f3b370: [RepairBlock] values.
    void LoadRepairBlock(INI_Reader* Reader, FPersonality::RepairUseStruct* Out, ID_String* OutNickname)
    {
        if (!Reader->read_value())
            return;
        do
        {
            if (Reader->is_value("nickname"))
                OutNickname->Id = CreateID(Reader->get_value_string());
            else if (Reader->is_value("use_shield_repair_pre_delay"))
                Out->UseShieldRepairPreDelay = Reader->get_value_float(0);
            else if (Reader->is_value("use_shield_repair_post_delay"))
                Out->UseShieldRepairPostDelay = Reader->get_value_float(0);
            else if (Reader->is_value("use_shield_repair_at_damage_percent"))
                Out->UseShieldRepairAtDamagePercent = Reader->get_value_float(0);
            else if (Reader->is_value("use_hull_repair_pre_delay"))
                Out->UseHullRepairPreDelay = Reader->get_value_float(0);
            else if (Reader->is_value("use_hull_repair_post_delay"))
                Out->UseHullRepairPostDelay = Reader->get_value_float(0);
            else if (Reader->is_value("use_hull_repair_at_damage_percent"))
                Out->UseHullRepairAtDamagePercent = Reader->get_value_float(0);
        } while (Reader->read_value());
    }

    // FUN_06f3b470: [GunBlock] values.
    void LoadGunBlock(INI_Reader* Reader, FPersonality::GunUseStruct* Out, ID_String* OutNickname)
    {
        if (!Reader->read_value())
            return;
        do
        {
            if (Reader->is_value("nickname"))
                OutNickname->Id = CreateID(Reader->get_value_string());
            else if (Reader->is_value("gun_fire_interval_time"))
                Out->GunFireIntervalTime = Reader->get_value_float(0);
            else if (Reader->is_value("gun_fire_interval_variance_percent"))
                Out->GunFireIntervalVariancePercent = Reader->get_value_float(0);
            else if (Reader->is_value("gun_fire_burst_interval_time"))
                Out->GunFireBurstIntervalTime = Reader->get_value_float(0);
            else if (Reader->is_value("gun_fire_burst_interval_variance_percent"))
                Out->GunFireBurstIntervalVariancePercent = Reader->get_value_float(0);
            else if (Reader->is_value("auto_turret_burst_interval_variance_percent"))
                Out->AutoTurretBurstIntervalVariancePercent = Reader->get_value_float(0);
            else if (Reader->is_value("gun_fire_no_burst_interval_time"))
                Out->GunFireNoBurstIntervalTime = Reader->get_value_float(0);
            else if (Reader->is_value("gun_fire_accuracy_cone_angle"))
                Out->GunFireAccuracyConeAngle = Reader->get_value_float(0) * GDegToRad;
            else if (Reader->is_value("gun_fire_accuracy_power"))
                Out->GunFireAccuracyPower = Reader->get_value_float(0);
            else if (Reader->is_value("gun_fire_accuracy_power_npc"))
                Out->GunFireAccuracyPowerNpc = Reader->get_value_float(0);
            else if (Reader->is_value("gun_target_point_switch_time"))
                Out->GunTargetPointSwitchTime = Reader->get_value_float(0);
            else if (Reader->is_value("gun_range_threshold"))
                Out->GunRangeThreshold = Reader->get_value_float(0);
            else if (Reader->is_value("gun_range_threshold_variance_percent"))
                Out->GunRangeThresholdVariancePercent = Reader->get_value_float(0);
            else if (Reader->is_value("auto_turret_interval_time"))
                Out->AutoTurretIntervalTime = Reader->get_value_float(0);
            else if (Reader->is_value("auto_turret_burst_interval_time"))
                Out->AutoTurretBurstIntervalTime = Reader->get_value_float(0);
            else if (Reader->is_value("auto_turret_no_burst_interval_time"))
                Out->AutoTurretNoBurstIntervalTime = Reader->get_value_float(0);
            else if (Reader->is_value("fire_style"))
            {
                // 06f3b677-06f3b6a8: "MULTIPLE" stores stricmp's 0; anything else
                // (the "SINGLE" compare result is discarded) stores 1.
                char const* Style = Reader->get_value_string();
                if (_stricmp(Style, "MULTIPLE") == 0)
                    Out->FireStyle = 0;
                else
                {
                    _stricmp(Style, "SINGLE");
                    Out->FireStyle = 1;
                }
            }
        } while (Reader->read_value());
    }

    // FUN_06f3b6c0: [MineBlock] values.
    void LoadMineBlock(INI_Reader* Reader, FPersonality::MineUseStruct* Out, ID_String* OutNickname)
    {
        if (!Reader->read_value())
            return;
        do
        {
            if (Reader->is_value("nickname"))
                OutNickname->Id = CreateID(Reader->get_value_string());
            else if (Reader->is_value("mine_launch_interval"))
                Out->MineLaunchInterval = Reader->get_value_float(0);
            else if (Reader->is_value("mine_launch_cone_angle"))
                Out->MineLaunchConeAngle = Reader->get_value_float(0) * GDegToRad;
            else if (Reader->is_value("mine_launch_range"))
                Out->MineLaunchRange = Reader->get_value_float(0);
        } while (Reader->read_value());
    }

    // FUN_06f3b770: [MissileBlock] values.
    void LoadMissileBlock(INI_Reader* Reader, FPersonality::MissileUseStruct* Out, ID_String* OutNickname)
    {
        if (!Reader->read_value())
            return;
        do
        {
            if (Reader->is_value("nickname"))
                OutNickname->Id = CreateID(Reader->get_value_string());
            else if (Reader->is_value("missile_launch_range"))
                Out->MissileLaunchRange = Reader->get_value_float(0);
            else if (Reader->is_value("missile_launch_allow_out_of_range"))
                Out->bMissileLaunchAllowOutOfRange = ParseTrueFalse(Reader->get_value_string());
            else if (Reader->is_value("missile_launch_interval_time"))
                Out->MissileLaunchIntervalTime = Reader->get_value_float(0);
            else if (Reader->is_value("missile_launch_interval_variance_percent"))
                Out->MissileLaunchIntervalVariancePercent = Reader->get_value_float(0);
            else if (Reader->is_value("missile_launch_cone_angle"))
                Out->MissileLaunchConeAngle = Reader->get_value_float(0) * GDegToRad;
            else if (Reader->is_value("anti_cruise_missile_min_distance"))
                Out->AntiCruiseMissileMinDistance = Reader->get_value_float(0);
            else if (Reader->is_value("anti_cruise_missile_max_distance"))
                Out->AntiCruiseMissileMaxDistance = Reader->get_value_float(0);
            else if (Reader->is_value("anti_cruise_missile_pre_fire_delay"))
                Out->AntiCruiseMissilePreFireDelay = Reader->get_value_float(0);
            else if (Reader->is_value("anti_cruise_missile_interval_time"))
                Out->AntiCruiseMissileIntervalTime = Reader->get_value_float(0);
        } while (Reader->read_value());
    }

    // FUN_06f3b900: [DamageReactionBlock] values.
    void LoadDamageReactionBlock(INI_Reader* Reader, FPersonality::DamageReactionStruct* Out,
                                 ID_String* OutNickname)
    {
        if (!Reader->read_value())
            return;
        do
        {
            if (Reader->is_value("nickname"))
                OutNickname->Id = CreateID(Reader->get_value_string());
            else if (Reader->is_value("evade_break_damage_trigger_percent"))
                Out->EvadeBreakDamageTriggerPercent = Reader->get_value_float(0);
            else if (Reader->is_value("evade_dodge_more_damage_trigger_percent"))
                Out->EvadeDodgeMoreDamageTriggerPercent = Reader->get_value_float(0);
            else if (Reader->is_value("drop_mines_damage_trigger_percent"))
                Out->DropMinesDamageTriggerPercent = Reader->get_value_float(0);
            else if (Reader->is_value("drop_mines_damage_trigger_time"))
                Out->DropMinesDamageTriggerTime = Reader->get_value_float(0);
            else if (Reader->is_value("engine_kill_face_damage_trigger_percent"))
                Out->EngineKillFaceDamageTriggerPercent = Reader->get_value_float(0);
            else if (Reader->is_value("engine_kill_face_damage_trigger_time"))
                Out->EngineKillFaceDamageTriggerTime = Reader->get_value_float(0);
            else if (Reader->is_value("roll_damage_trigger_percent"))
                Out->RollDamageTriggerPercent = Reader->get_value_float(0);
            else if (Reader->is_value("roll_damage_trigger_time"))
                Out->RollDamageTriggerTime = Reader->get_value_float(0);
            else if (Reader->is_value("afterburner_damage_trigger_percent"))
                Out->AfterburnerDamageTriggerPercent = Reader->get_value_float(0);
            else if (Reader->is_value("afterburner_damage_trigger_time"))
                Out->AfterburnerDamageTriggerTime = Reader->get_value_float(0);
            else if (Reader->is_value("brake_reverse_damage_trigger_percent"))
                Out->BrakeReverseDamageTriggerPercent = Reader->get_value_float(0);
            else if (Reader->is_value("fire_guns_damage_trigger_percent"))
                Out->FireGunsDamageTriggerPercent = Reader->get_value_float(0);
            else if (Reader->is_value("fire_guns_damage_trigger_time"))
                Out->FireGunsDamageTriggerTime = Reader->get_value_float(0);
            else if (Reader->is_value("fire_missiles_damage_trigger_percent"))
                Out->FireMissilesDamageTriggerPercent = Reader->get_value_float(0);
            else if (Reader->is_value("fire_missiles_damage_trigger_time"))
                Out->FireMissilesDamageTriggerTime = Reader->get_value_float(0);
        } while (Reader->read_value());
    }

    // FUN_06f3bb00: [MissileReactionBlock] values.
    void LoadMissileReactionBlock(INI_Reader* Reader, FPersonality::MissileReactionStruct* Out,
                                  ID_String* OutNickname)
    {
        if (!Reader->read_value())
            return;
        do
        {
            if (Reader->is_value("nickname"))
                OutNickname->Id = CreateID(Reader->get_value_string());
            else if (Reader->is_value("evade_missile_distance"))
                Out->EvadeMissileDistance = Reader->get_value_float(0);
            else if (Reader->is_value("evade_break_missile_reaction_time"))
                Out->EvadeBreakMissileReactionTime = Reader->get_value_float(0);
            else if (Reader->is_value("evade_slide_missile_reaction_time"))
                Out->EvadeSlideMissileReactionTime = Reader->get_value_float(0);
            else if (Reader->is_value("evade_afterburn_missile_reaction_time"))
                Out->EvadeAfterburnMissileReactionTime = Reader->get_value_float(0);
        } while (Reader->read_value());
    }

    // FUN_06f3bbd0: [FormationBlock] values.
    void LoadFormationBlock(INI_Reader* Reader, FPersonality::FormationUseStruct* Out, ID_String* OutNickname)
    {
        if (!Reader->read_value())
            return;
        do
        {
            if (Reader->is_value("nickname"))
                OutNickname->Id = CreateID(Reader->get_value_string());
            else if (Reader->is_value("force_attack_formation_active_time"))
                Out->ForceAttackFormationActiveTime = Reader->get_value_float(0);
            else if (Reader->is_value("force_attack_formation_unactive_time"))
                Out->ForceAttackFormationUnactiveTime = Reader->get_value_float(0);
            else if (Reader->is_value("break_formation_damage_trigger_percent"))
                Out->BreakFormationDamageTriggerPercent = Reader->get_value_float(0);
            else if (Reader->is_value("break_formation_damage_trigger_time"))
                Out->BreakFormationDamageTriggerTime = Reader->get_value_float(0);
            else if (Reader->is_value("break_apart_formation_damage_trigger_percent"))
                Out->BreakApartFormationDamageTriggerPercent = Reader->get_value_float(0);
            else if (Reader->is_value("break_apart_formation_damage_trigger_time"))
                Out->BreakApartFormationDamageTriggerTime = Reader->get_value_float(0);
            else if (Reader->is_value("break_formation_missile_reaction_time"))
                Out->BreakFormationMissileReactionTime = Reader->get_value_float(0);
            else if (Reader->is_value("break_apart_formation_missile_reaction_time"))
                Out->BreakApartFormationMissileReactionTime = Reader->get_value_float(0);
            else if (Reader->is_value("break_apart_formation_on_buzz_head_toward"))
                Out->bBreakApartFormationOnBuzzHeadToward = ParseTrueFalse(Reader->get_value_string());
            else if (Reader->is_value("break_formation_on_buzz_head_toward_time"))
                Out->BreakFormationOnBuzzHeadTowardTime = Reader->get_value_float(0);
            else if (Reader->is_value("regroup_formation_on_buzz_head_toward"))
                Out->bRegroupFormationOnBuzzHeadToward = ParseTrueFalse(Reader->get_value_string());
            else if (Reader->is_value("break_apart_formation_on_buzz_pass_by"))
                Out->bBreakApartFormationOnBuzzPassBy = ParseTrueFalse(Reader->get_value_string());
            else if (Reader->is_value("break_formation_on_buzz_pass_by_time"))
                Out->BreakFormationOnBuzzPassByTime = Reader->get_value_float(0);
            else if (Reader->is_value("regroup_formation_on_buzz_pass_by"))
                Out->bRegroupFormationOnBuzzPassBy = ParseTrueFalse(Reader->get_value_string());
            else if (Reader->is_value("break_apart_formation_on_evade_dodge"))
                Out->bBreakApartFormationOnEvadeDodge = ParseTrueFalse(Reader->get_value_string());
            else if (Reader->is_value("break_formation_on_evade_dodge_time"))
                Out->BreakFormationOnEvadeDodgeTime = Reader->get_value_float(0);
            else if (Reader->is_value("regroup_formation_on_evade_dodge"))
                Out->bRegroupFormationOnEvadeDodge = ParseTrueFalse(Reader->get_value_string());
            else if (Reader->is_value("break_apart_formation_on_evade_break"))
                Out->bBreakApartFormationOnEvadeBreak = ParseTrueFalse(Reader->get_value_string());
            else if (Reader->is_value("break_formation_on_evade_break_time"))
                Out->BreakFormationOnEvadeBreakTime = Reader->get_value_float(0);
            else if (Reader->is_value("regroup_formation_on_evade_break"))
                Out->bRegroupFormationOnEvadeBreak = ParseTrueFalse(Reader->get_value_string());
            else if (Reader->is_value("formation_exit_mode"))
            {
                // 06f3c02a-06f3c0d8: get_value_string() is re-read for every compare;
                // no match leaves the field as it was.
                if (_stricmp(Reader->get_value_string(), "BREAK_AWAY_FROM_CENTER") == 0)
                    Out->FormationExitMode = 0;
                else if (_stricmp(Reader->get_value_string(), "BREAK_AWAY_FROM_CENTER_AFTERBURNER") == 0)
                    Out->FormationExitMode = 1;
                else if (_stricmp(Reader->get_value_string(), "BRAKE_REVERSE") == 0)
                    Out->FormationExitMode = 2;
                else if (_stricmp(Reader->get_value_string(), "OUTRUN") == 0)
                    Out->FormationExitMode = 3;
                else if (_stricmp(Reader->get_value_string(), "NOTHING") == 0)
                    Out->FormationExitMode = 5;
            }
            else if (Reader->is_value("formation_exit_top_turn_break_away_throttle"))
                Out->FormationExitTopTurnBreakAwayThrottle = Reader->get_value_float(0);
            else if (Reader->is_value("formation_exit_roll_outrun_throttle"))
                Out->FormationExitRollOutrunThrottle = Reader->get_value_float(0);
            else if (Reader->is_value("formation_exit_max_time"))
                Out->FormationExitMaxTime = Reader->get_value_float(0);
            else if (Reader->is_value("leader_makes_me_tougher"))
                Out->bLeaderMakesMeTougher = ParseTrueFalse(Reader->get_value_string());
        } while (Reader->read_value());
    }

    // FUN_06f3c9b0 (body continues at 06f3c9d8): one attack_preference line
    // "<class>, <range>, <weapon types>" into record Index.
    void LoadAttackPreference(FPersonality::JobStruct* Job, INI_Reader* Reader, int Index)
    {
        if (Index == 0xC)
            return;

        Reader->get_num_parameters();

        FAttackPreference* Records = Job->AttackPreferences;
        for (int ClassIndex = 0; ClassIndex < 12; ++ClassIndex)
        {
            if (_stricmp(Reader->get_value_string(0), GShipClassNames[ClassIndex].Name) == 0)
            {
                FAttackPreference* Record = &Records[Index];
                Record->ShipClass = GShipClassNames[ClassIndex].ClassId;
                Record->Range = Reader->get_value_float(1);
                Record->WeaponFlags = 0;
                std::strcpy(GAttackPreferenceWeapons, Reader->get_value_string(2));
                _strupr(GAttackPreferenceWeapons);
                if (std::strstr(GAttackPreferenceWeapons, "GUNS") != nullptr)
                    Record->WeaponFlags |= 1;
                if (std::strstr(GAttackPreferenceWeapons, "GUIDED") != nullptr)
                    Record->WeaponFlags |= 2;
                if (std::strstr(GAttackPreferenceWeapons, "UNGUIDED") != nullptr)
                    Record->WeaponFlags |= 4;
                if (std::strstr(GAttackPreferenceWeapons, "TORPEDO") != nullptr)
                    Record->WeaponFlags |= 8;
                break;
            }
        }

        // LAB_06f3caaf: terminator after this record, matched or not.
        Records[Index + 1].ShipClass = 0xC;
    }

    // FUN_06f3cad0: attack_subtarget_order token list -> JobStruct +0xAC ints,
    // terminated by 7.
    void LoadAttackSubtargetOrder(FPersonality::JobStruct* Job, char* Text)
    {
        char Delimiters[] = " ,\t\n";
        int32_t* Order = Job->AttackSubtargetOrder;
        char* Token = std::strtok(Text, Delimiters);
        int Count = 0;
        if (Token == nullptr)
        {
            Order[0] = 7;
            return;
        }
        do
        {
            if (_stricmp(Token, "ANYTHING") == 0)
                Order[Count++] = 6;
            if (_stricmp(Token, "GUNS") == 0)
                Order[Count++] = 0;
            if (_stricmp(Token, "TURRETS") == 0)
                Order[Count++] = 1;
            if (_stricmp(Token, "LAUNCHERS") == 0)
                Order[Count++] = 2;
            if (_stricmp(Token, "TOWERS") == 0)
                Order[Count++] = 3;
            if (_stricmp(Token, "ENGINES") == 0)
                Order[Count++] = 4;
            if (_stricmp(Token, "HULL") == 0)
                Order[Count++] = 5;
            Token = std::strtok(nullptr, Delimiters);
        } while (Token != nullptr);
        Order[Count] = 7;
    }

    // FUN_06f3c190: [JobBlock] values.
    void LoadJobBlock(INI_Reader* Reader, FPersonality::JobStruct* Out, ID_String* OutNickname)
    {
        int PreferenceIndex = 0;
        if (!Reader->read_value())
            return;
        do
        {
            if (Reader->is_value("nickname"))
                OutNickname->Id = CreateID(Reader->get_value_string());
            else if (Reader->is_value("loot_preference"))
            {
                char const* Text = Reader->get_value_string();
                Out->LootPreference = 0;
                if (std::strstr(Text, "LT_ALL") != nullptr)
                    Out->LootPreference = 7;
                else if (std::strstr(Text, "LT_NONE") != nullptr)
                    Out->LootPreference = 0;
                else
                {
                    unsigned int Flags = std::strstr(Text, "LT_COMMODITIES") != nullptr ? 1u : 0u;
                    if (std::strstr(Text, "LT_EQUIPMENT") != nullptr)
                        Flags |= 2;
                    if (std::strstr(Text, "LT_POTIONS") != nullptr)
                        Flags |= 4;
                    Out->LootPreference = (int32_t)Flags;
                }
            }
            else if (Reader->is_value("loot_flee_threshold"))
                Out->LootFleeThreshold = ParseToughness(Reader->get_value_string());
            else if (Reader->is_value("flee_scene_threat_style"))
                Out->FleeSceneThreatStyle = ParseToughness(Reader->get_value_string());
            else if (Reader->is_value("flee_when_hull_damaged_percent"))
                Out->FleeWhenHullDamagedPercent = Reader->get_value_float(0);
            else if (Reader->is_value("flee_when_leader_flees_style"))
                Out->bFleeWhenLeaderFleesStyle = ParseTrueFalse(Reader->get_value_string());
            else if (Reader->is_value("flee_no_weapons_style"))
                Out->bFleeNoWeaponsStyle = ParseTrueFalse(Reader->get_value_string());
            else if (Reader->is_value("scene_toughness_threshold"))
                Out->SceneToughnessThreshold = ParseToughness(Reader->get_value_string());
            else if (Reader->is_value("target_toughness_preference"))
                Out->TargetToughnessPreference = ParseToughness(Reader->get_value_string());
            else if (Reader->is_value("wait_for_leader_target"))
                Out->bWaitForLeaderTarget = ParseTrueFalse(Reader->get_value_string());
            else if (Reader->is_value("maximum_leader_target_distance"))
                Out->MaximumLeaderTargetDistance = Reader->get_value_float(0);
            else if (Reader->is_value("combat_drift_distance"))
            {
                // 06f3c700: key recognised, nothing stored.
            }
            else if (Reader->is_value("attack_order"))
            {
                char Delimiters[] = " ,\t\n";
                FAttackPreference* Records = Out->AttackPreferences;
                int Count = 0;
                char* Token = std::strtok(const_cast<char*>(Reader->get_value_string()), Delimiters);
                while (Token != nullptr)
                {
                    if (_stricmp(Token, "ANYTHING") == 0)
                        Records[Count++].ShipClass = 0xB;
                    // The table also holds ANYTHING (11): an ANYTHING token takes
                    // two records, exactly as in retail.
                    for (int ClassIndex = 0; ClassIndex < 12; ++ClassIndex)
                    {
                        if (_stricmp(Token, GShipClassNames[ClassIndex].Name) == 0)
                        {
                            Records[Count].ShipClass = GShipClassNames[ClassIndex].ClassId;
                            Records[Count].Range = 5000.0f; // 0x459c4000
                            Records[Count].WeaponFlags = 0xF;
                            ++Count;
                        }
                    }
                    Token = std::strtok(nullptr, Delimiters);
                }
                Records[Count].ShipClass = 0xC;
            }
            else if (Reader->is_value("attack_preference"))
            {
                LoadAttackPreference(Out, Reader, PreferenceIndex);
                ++PreferenceIndex;
            }
            else if (Reader->is_value("attack_subtarget_order"))
                LoadAttackSubtargetOrder(Out, const_cast<char*>(Reader->get_value_string()));
            else if (Reader->is_value("field_targeting"))
            {
                char const* Text = Reader->get_value_string();
                if (_stricmp(Text, "NEVER") == 0)
                    Out->FieldTargeting = 0;
                else if (_stricmp(Text, "LOW_DENSITY") == 0)
                    Out->FieldTargeting = 1;
                else if (_stricmp(Text, "HIGH_DENSITY") == 0)
                    Out->FieldTargeting = 2;
                else
                    Out->FieldTargeting = _stricmp(Text, "ALWAYS") == 0 ? 3 : 1;
            }
            else if (Reader->is_value("force_attack_formation"))
            {
                Out->bForceAttackFormation = ParseTrueFalse(Reader->get_value_string());
                Out->bForceAttackFormationSet = 1;
            }
            else if (Reader->is_value("allow_player_targeting"))
                Out->bAllowPlayerTargeting = ParseTrueFalse(Reader->get_value_string());
        } while (Reader->read_value());
    }

    // FUN_06f3cc10: one [Pilot] section into DAT_06fd0aa0.
    void LoadPilot(INI_Reader* Reader)
    {
        FPilotRefs Refs;
        ID_String Nickname;
        Refs.Inherit.clear();
        Refs.EvadeDodge.clear();
        Refs.EvadeBreak.clear();
        Refs.BuzzHeadToward.clear();
        Refs.BuzzPassBy.clear();
        Refs.Trail.clear();
        Refs.Strafe.clear();
        Refs.EngineKill.clear();
        Refs.Repair.clear();
        Refs.Gun.clear();
        Refs.Mine.clear();
        Refs.Missile.clear();
        Refs.DamageReaction.clear();
        Refs.MissileReaction.clear();
        Refs.Countermeasure.clear();
        Refs.Formation.clear();
        Refs.Job.clear();
        Nickname.clear();

        if (Reader->read_value())
        {
            do
            {
                if (Reader->is_value("nickname"))
                    Nickname.Id = CreateID(Reader->get_value_string());
                else if (Reader->is_value("inherit"))
                {
                    // Every non-empty reference of the (already loaded) parent
                    // overwrites ours; the parent's own inherit is not copied.
                    Refs.Inherit.Id = CreateID(Reader->get_value_string());
                    if (!Refs.Inherit.IsEmpty())
                    {
                        std::map<unsigned int, FPilotRefs>::iterator Parent = GPilots.find(Refs.Inherit.Id);
                        if (Parent != GPilots.end())
                        {
                            FPilotRefs From = Parent->second;
                            if (!From.EvadeDodge.IsEmpty())
                                Refs.EvadeDodge = From.EvadeDodge;
                            if (!From.EvadeBreak.IsEmpty())
                                Refs.EvadeBreak = From.EvadeBreak;
                            if (!From.BuzzHeadToward.IsEmpty())
                                Refs.BuzzHeadToward = From.BuzzHeadToward;
                            if (!From.BuzzPassBy.IsEmpty())
                                Refs.BuzzPassBy = From.BuzzPassBy;
                            if (!From.Trail.IsEmpty())
                                Refs.Trail = From.Trail;
                            if (!From.Strafe.IsEmpty())
                                Refs.Strafe = From.Strafe;
                            if (!From.EngineKill.IsEmpty())
                                Refs.EngineKill = From.EngineKill;
                            if (!From.Repair.IsEmpty())
                                Refs.Repair = From.Repair;
                            if (!From.Gun.IsEmpty())
                                Refs.Gun = From.Gun;
                            if (!From.Mine.IsEmpty())
                                Refs.Mine = From.Mine;
                            if (!From.Missile.IsEmpty())
                                Refs.Missile = From.Missile;
                            if (!From.DamageReaction.IsEmpty())
                                Refs.DamageReaction = From.DamageReaction;
                            if (!From.MissileReaction.IsEmpty())
                                Refs.MissileReaction = From.MissileReaction;
                            if (!From.Countermeasure.IsEmpty())
                                Refs.Countermeasure = From.Countermeasure;
                            if (!From.Formation.IsEmpty())
                                Refs.Formation = From.Formation;
                            if (!From.Job.IsEmpty())
                                Refs.Job = From.Job;
                        }
                    }
                }
                else if (Reader->is_value("evade_dodge_id"))
                    Refs.EvadeDodge.Id = CreateID(Reader->get_value_string());
                else if (Reader->is_value("evade_break_id"))
                    Refs.EvadeBreak.Id = CreateID(Reader->get_value_string());
                else if (Reader->is_value("buzz_head_toward_id"))
                    Refs.BuzzHeadToward.Id = CreateID(Reader->get_value_string());
                else if (Reader->is_value("buzz_pass_by_id"))
                    Refs.BuzzPassBy.Id = CreateID(Reader->get_value_string());
                else if (Reader->is_value("trail_id"))
                    Refs.Trail.Id = CreateID(Reader->get_value_string());
                else if (Reader->is_value("strafe_id"))
                    Refs.Strafe.Id = CreateID(Reader->get_value_string());
                else if (Reader->is_value("engine_kill_id"))
                    Refs.EngineKill.Id = CreateID(Reader->get_value_string());
                else if (Reader->is_value("repair_id"))
                    Refs.Repair.Id = CreateID(Reader->get_value_string());
                else if (Reader->is_value("gun_id"))
                    Refs.Gun.Id = CreateID(Reader->get_value_string());
                else if (Reader->is_value("mine_id"))
                    Refs.Mine.Id = CreateID(Reader->get_value_string());
                else if (Reader->is_value("missile_id"))
                    Refs.Missile.Id = CreateID(Reader->get_value_string());
                else if (Reader->is_value("damage_reaction_id"))
                    Refs.DamageReaction.Id = CreateID(Reader->get_value_string());
                else if (Reader->is_value("missile_reaction_id"))
                    Refs.MissileReaction.Id = CreateID(Reader->get_value_string());
                else if (Reader->is_value("countermeasure_id"))
                    Refs.Countermeasure.Id = CreateID(Reader->get_value_string());
                else if (Reader->is_value("formation_id"))
                    Refs.Formation.Id = CreateID(Reader->get_value_string());
                else if (Reader->is_value("job_id"))
                    Refs.Job.Id = CreateID(Reader->get_value_string());
            } while (Reader->read_value());
        }

        // 06f3d0a0..: unique insert; a pilot nickname seen before keeps its
        // first definition.
        if (!Nickname.IsEmpty())
            GPilots.insert(std::make_pair(Nickname.Id, Refs));
    }
}

// FUN_06f3d400
bool __cdecl LoadPilotPersonalities(INI_Reader* Reader)
{
    if (Reader->read_header())
    {
        do
        {
            // Each arm: default-construct the block, clear the nickname, read the
            // values, and file the block under its nickname with a unique insert
            // (a nickname already present keeps its first block).
            if (Reader->is_header("EvadeDodgeBlock"))
            {
                FPersonality::EvadeDodgeUseStruct Block;
                ID_String Nickname;
                Nickname.clear();
                LoadEvadeDodgeBlock(Reader, &Block, &Nickname);
                if (!Nickname.IsEmpty())
                    GEvadeDodgeBlocks.insert(std::make_pair(Nickname.Id, Block));
            }
            else if (Reader->is_header("EvadeBreakBlock"))
            {
                FPersonality::EvadeBreakUseStruct Block;
                ID_String Nickname;
                Nickname.clear();
                LoadEvadeBreakBlock(Reader, &Block, &Nickname);
                if (!Nickname.IsEmpty())
                    GEvadeBreakBlocks.insert(std::make_pair(Nickname.Id, Block));
            }
            else if (Reader->is_header("BuzzHeadTowardBlock"))
            {
                FPersonality::BuzzHeadTowardUseStruct Block;
                ID_String Nickname;
                Nickname.clear();
                LoadBuzzHeadTowardBlock(Reader, &Block, &Nickname);
                if (!Nickname.IsEmpty())
                    GBuzzHeadTowardBlocks.insert(std::make_pair(Nickname.Id, Block));
            }
            else if (Reader->is_header("BuzzPassByBlock"))
            {
                FPersonality::BuzzPassByUseStruct Block;
                ID_String Nickname;
                Nickname.clear();
                LoadBuzzPassByBlock(Reader, &Block, &Nickname);
                if (!Nickname.IsEmpty())
                    GBuzzPassByBlocks.insert(std::make_pair(Nickname.Id, Block));
            }
            else if (Reader->is_header("TrailBlock"))
            {
                FPersonality::TrailUseStruct Block;
                ID_String Nickname;
                Nickname.clear();
                LoadTrailBlock(Reader, &Block, &Nickname);
                if (!Nickname.IsEmpty())
                    GTrailBlocks.insert(std::make_pair(Nickname.Id, Block));
            }
            else if (Reader->is_header("StrafeBlock"))
            {
                FPersonality::StrafeUseStruct Block;
                ID_String Nickname;
                Nickname.clear();
                LoadStrafeBlock(Reader, &Block, &Nickname);
                if (!Nickname.IsEmpty())
                    GStrafeBlocks.insert(std::make_pair(Nickname.Id, Block));
            }
            else if (Reader->is_header("EngineKillBlock"))
            {
                FPersonality::EngineKillUseStruct Block;
                ID_String Nickname;
                Nickname.clear();
                LoadEngineKillBlock(Reader, &Block, &Nickname);
                if (!Nickname.IsEmpty())
                    GEngineKillBlocks.insert(std::make_pair(Nickname.Id, Block));
            }
            else if (Reader->is_header("RepairBlock"))
            {
                FPersonality::RepairUseStruct Block;
                ID_String Nickname;
                Nickname.clear();
                LoadRepairBlock(Reader, &Block, &Nickname);
                if (!Nickname.IsEmpty())
                    GRepairBlocks.insert(std::make_pair(Nickname.Id, Block));
            }
            else if (Reader->is_header("GunBlock"))
            {
                FPersonality::GunUseStruct Block;
                ID_String Nickname;
                Nickname.clear();
                LoadGunBlock(Reader, &Block, &Nickname);
                if (!Nickname.IsEmpty())
                    GGunBlocks.insert(std::make_pair(Nickname.Id, Block));
            }
            else if (Reader->is_header("MineBlock"))
            {
                FPersonality::MineUseStruct Block;
                ID_String Nickname;
                Nickname.clear();
                LoadMineBlock(Reader, &Block, &Nickname);
                if (!Nickname.IsEmpty())
                    GMineBlocks.insert(std::make_pair(Nickname.Id, Block));
            }
            else if (Reader->is_header("MissileBlock"))
            {
                FPersonality::MissileUseStruct Block;
                ID_String Nickname;
                Nickname.clear();
                LoadMissileBlock(Reader, &Block, &Nickname);
                if (!Nickname.IsEmpty())
                    GMissileBlocks.insert(std::make_pair(Nickname.Id, Block));
            }
            else if (Reader->is_header("DamageReactionBlock"))
            {
                FPersonality::DamageReactionStruct Block;
                ID_String Nickname;
                Nickname.clear();
                LoadDamageReactionBlock(Reader, &Block, &Nickname);
                if (!Nickname.IsEmpty())
                    GDamageReactionBlocks.insert(std::make_pair(Nickname.Id, Block));
            }
            else if (Reader->is_header("MissileReactionBlock"))
            {
                FPersonality::MissileReactionStruct Block;
                ID_String Nickname;
                Nickname.clear();
                LoadMissileReactionBlock(Reader, &Block, &Nickname);
                if (!Nickname.IsEmpty())
                    GMissileReactionBlocks.insert(std::make_pair(Nickname.Id, Block));
            }
            else if (Reader->is_header("CountermeasureBlock"))
            {
                // Values read inline (06f3dbc0..), no separate loader.
                FPersonality::CountermeasureUseStruct Block;
                ID_String Nickname;
                Nickname.clear();
                if (Reader->read_value())
                {
                    do
                    {
                        if (Reader->is_value("nickname"))
                            Nickname.Id = CreateID(Reader->get_value_string());
                        else if (Reader->is_value("countermeasure_active_time"))
                            Block.CountermeasureActiveTime = Reader->get_value_float(0);
                        else if (Reader->is_value("countermeasure_unactive_time"))
                            Block.CountermeasureUnactiveTime = Reader->get_value_float(0);
                    } while (Reader->read_value());
                }
                if (!Nickname.IsEmpty())
                    GCountermeasureBlocks.insert(std::make_pair(Nickname.Id, Block));
            }
            else if (Reader->is_header("FormationBlock"))
            {
                FPersonality::FormationUseStruct Block;
                ID_String Nickname;
                Nickname.clear();
                LoadFormationBlock(Reader, &Block, &Nickname);
                if (!Nickname.IsEmpty())
                    GFormationBlocks.insert(std::make_pair(Nickname.Id, Block));
            }
            else if (Reader->is_header("JobBlock"))
            {
                FPersonality::JobStruct Block;
                ID_String Nickname;
                Nickname.clear();
                LoadJobBlock(Reader, &Block, &Nickname);
                if (!Nickname.IsEmpty())
                    GJobBlocks.insert(std::make_pair(Nickname.Id, Block));
            }
            else if (Reader->is_header("Pilot"))
                LoadPilot(Reader);
        } while (Reader->read_header());
    }
    return true;
}

// FUN_06f3a040
void __cdecl ClearPilotPersonalities()
{
    GEvadeDodgeBlocks.clear();
    GEvadeBreakBlocks.clear();
    GBuzzHeadTowardBlocks.clear();
    GBuzzPassByBlocks.clear();
    GTrailBlocks.clear();
    GStrafeBlocks.clear();
    GEngineKillBlocks.clear();
    GRepairBlocks.clear();
    GGunBlocks.clear();
    GMineBlocks.clear();
    GMissileBlocks.clear();
    GDamageReactionBlocks.clear();
    GMissileReactionBlocks.clear();
    GCountermeasureBlocks.clear();
    GFormationBlocks.clear();
    GJobBlocks.clear();
    GPilots.clear();
}

// FUN_06f3df10
bool __cdecl ApplyPilotJob(ID_String* JobId, pub::AI::Personality* OutPersonality)
{
    if (JobId->IsEmpty())
        return false;

    std::map<unsigned int, FPersonality::JobStruct>::iterator Found = GJobBlocks.find(JobId->Id);
    if (Found == GJobBlocks.end())
        return false;

    // 06f3df79-06f3dfad: force_attack_formation (+0x134) only replaces ours when
    // the block set it (+0x135); after a copy with +0x135 set, +0x135 stays 1.
    FPersonality::JobStruct Job = Found->second;
    bool bForceAttackFormationSet = false;
    unsigned char ForceAttackFormation = Job.bForceAttackFormation;
    if (Job.bForceAttackFormationSet != 0)
        bForceAttackFormationSet = true;
    else
        ForceAttackFormation = OutPersonality->JobParams.bForceAttackFormation;

    OutPersonality->JobParams = Job;
    OutPersonality->JobParams.bForceAttackFormation = ForceAttackFormation;
    if (bForceAttackFormationSet)
        OutPersonality->JobParams.bForceAttackFormationSet = 1;
    return true;
}

// FUN_06f3e020
bool __cdecl GetPilotPersonality(unsigned int* PilotId, pub::AI::Personality* OutPersonality)
{
    std::map<unsigned int, FPilotRefs>::iterator Pilot = GPilots.find(*PilotId);
    if (Pilot == GPilots.end())
        return false;

    FPilotRefs Refs = Pilot->second;

    if (!Refs.EvadeDodge.IsEmpty())
    {
        std::map<unsigned int, FPersonality::EvadeDodgeUseStruct>::iterator Found =
            GEvadeDodgeBlocks.find(Refs.EvadeDodge.Id);
        if (Found != GEvadeDodgeBlocks.end())
            OutPersonality->EvadeDodgeParams = Found->second;
    }
    if (!Refs.EvadeBreak.IsEmpty())
    {
        std::map<unsigned int, FPersonality::EvadeBreakUseStruct>::iterator Found =
            GEvadeBreakBlocks.find(Refs.EvadeBreak.Id);
        if (Found != GEvadeBreakBlocks.end())
            OutPersonality->EvadeBreakParams = Found->second;
    }
    if (!Refs.BuzzHeadToward.IsEmpty())
    {
        std::map<unsigned int, FPersonality::BuzzHeadTowardUseStruct>::iterator Found =
            GBuzzHeadTowardBlocks.find(Refs.BuzzHeadToward.Id);
        if (Found != GBuzzHeadTowardBlocks.end())
            OutPersonality->BuzzHeadParams = Found->second;
    }
    if (!Refs.BuzzPassBy.IsEmpty())
    {
        std::map<unsigned int, FPersonality::BuzzPassByUseStruct>::iterator Found =
            GBuzzPassByBlocks.find(Refs.BuzzPassBy.Id);
        if (Found != GBuzzPassByBlocks.end())
            OutPersonality->BuzzPassParams = Found->second;
    }
    if (!Refs.Trail.IsEmpty())
    {
        std::map<unsigned int, FPersonality::TrailUseStruct>::iterator Found = GTrailBlocks.find(Refs.Trail.Id);
        if (Found != GTrailBlocks.end())
            OutPersonality->TrailParams = Found->second;
    }
    if (!Refs.Strafe.IsEmpty())
    {
        std::map<unsigned int, FPersonality::StrafeUseStruct>::iterator Found = GStrafeBlocks.find(Refs.Strafe.Id);
        if (Found != GStrafeBlocks.end())
            OutPersonality->StrafeParams = Found->second;
    }
    if (!Refs.EngineKill.IsEmpty())
    {
        std::map<unsigned int, FPersonality::EngineKillUseStruct>::iterator Found =
            GEngineKillBlocks.find(Refs.EngineKill.Id);
        if (Found != GEngineKillBlocks.end())
            OutPersonality->EngineKillParams = Found->second;
    }
    if (!Refs.Repair.IsEmpty())
    {
        std::map<unsigned int, FPersonality::RepairUseStruct>::iterator Found = GRepairBlocks.find(Refs.Repair.Id);
        if (Found != GRepairBlocks.end())
            OutPersonality->RepairParams = Found->second;
    }
    if (!Refs.Gun.IsEmpty())
    {
        std::map<unsigned int, FPersonality::GunUseStruct>::iterator Found = GGunBlocks.find(Refs.Gun.Id);
        if (Found != GGunBlocks.end())
            OutPersonality->GunParams = Found->second;
    }
    if (!Refs.Mine.IsEmpty())
    {
        std::map<unsigned int, FPersonality::MineUseStruct>::iterator Found = GMineBlocks.find(Refs.Mine.Id);
        if (Found != GMineBlocks.end())
            OutPersonality->MineParams = Found->second;
    }
    if (!Refs.Missile.IsEmpty())
    {
        std::map<unsigned int, FPersonality::MissileUseStruct>::iterator Found =
            GMissileBlocks.find(Refs.Missile.Id);
        if (Found != GMissileBlocks.end())
            OutPersonality->MissileParams = Found->second;
    }
    if (!Refs.DamageReaction.IsEmpty())
    {
        std::map<unsigned int, FPersonality::DamageReactionStruct>::iterator Found =
            GDamageReactionBlocks.find(Refs.DamageReaction.Id);
        if (Found != GDamageReactionBlocks.end())
            OutPersonality->DamageReaction = Found->second;
    }
    if (!Refs.MissileReaction.IsEmpty())
    {
        std::map<unsigned int, FPersonality::MissileReactionStruct>::iterator Found =
            GMissileReactionBlocks.find(Refs.MissileReaction.Id);
        if (Found != GMissileReactionBlocks.end())
            OutPersonality->MissileReaction = Found->second;
    }
    if (!Refs.Countermeasure.IsEmpty())
    {
        std::map<unsigned int, FPersonality::CountermeasureUseStruct>::iterator Found =
            GCountermeasureBlocks.find(Refs.Countermeasure.Id);
        if (Found != GCountermeasureBlocks.end())
            OutPersonality->CMParams = Found->second;
    }
    if (!Refs.Formation.IsEmpty())
    {
        std::map<unsigned int, FPersonality::FormationUseStruct>::iterator Found =
            GFormationBlocks.find(Refs.Formation.Id);
        if (Found != GFormationBlocks.end())
            OutPersonality->FormationParams = Found->second;
    }

    // 06f3e4ac-06f3e531: with a job_id, the inherited pilot's job is applied
    // first, then our own. (Retail also default-constructs an unused local
    // Personality at 06f3e508 on the inherit path; it is never read.)
    if (!Refs.Job.IsEmpty())
    {
        if (!Refs.Inherit.IsEmpty())
        {
            std::map<unsigned int, FPilotRefs>::iterator Parent = GPilots.find(Refs.Inherit.Id);
            if (Parent != GPilots.end())
            {
                FPilotRefs ParentRefs = Parent->second;
                ApplyPilotJob(&ParentRefs.Job, OutPersonality);
            }
        }
        ApplyPilotJob(&Refs.Job, OutPersonality);
    }
    return true;
}

// FUN_06f3dfd0
bool __cdecl GetPilotPersonalityWithJob(unsigned int* PilotId, ID_String* JobId,
                                        pub::AI::Personality* OutPersonality)
{
    if (!GetPilotPersonality(PilotId, OutPersonality))
        return false;
    if (JobId->IsEmpty())
        return true;
    return ApplyPilotJob(JobId, OutPersonality);
}
