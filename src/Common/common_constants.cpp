// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll ReadConstants and the constants it fills (EngineEquipConsts, ShieldEquipConsts,
// ThrusterEquipConsts, PhySys, the [Constants] TestValue / TestString). Split out of
// common_goods.cpp.
//
// Static-initialisation order: GTestString and ThrusterEquipConsts::INTERIOR/EXTERIOR_SOUND_NAME
// are dynamically initialised; this TU follows common_equipdesc.cpp and precedes
// common_loadout.cpp and common_goodinfo.cpp, as in the former common_goods.cpp.

#include "common_goods.h"
#include "common_goods_internal.h" // kPathChars
#include "common_id.h"
#include "common_ini.h"
#include "common_misc.h"   // GetFileSysPath
#include "common_physys.h" // PhySys constants (DEFAULT_LINEAR_DAMPING, etc.)
#include "DALib.h"
#include "dacom.h"

#include <cstring>

using CmnGoods::kPathChars;

namespace
{
    const char kConstantsSource[] = "E:\\FL\\Scratch\\Source\\Common\\Constants.cpp";

    struct FFloatKey
    {
        char const* Name;
        float* Dest;
    };

    struct FUintKey
    {
        char const* Name;
        unsigned* Dest;
    };

    struct FIntKey
    {
        char const* Name;
        int* Dest;
    };

    struct FDoubleKey
    {
        char const* Name;
        double* Dest;
    };

    bool ApplyFloatKey(INI_Reader& Reader, FFloatKey const* Keys, unsigned Count)
    {
        unsigned Index = 0;
        for (Index = 0; Index < Count; ++Index)
        {
            if (Reader.is_value(Keys[Index].Name))
            {
                if (Keys[Index].Dest != nullptr)
                    *Keys[Index].Dest = Reader.get_value_float(0);
                return true;
            }
        }
        return false;
    }

    bool ApplyUintCreateId(INI_Reader& Reader, FUintKey const* Keys, unsigned Count)
    {
        unsigned Index = 0;
        for (Index = 0; Index < Count; ++Index)
        {
            if (Reader.is_value(Keys[Index].Name))
            {
                if (Keys[Index].Dest != nullptr)
                    *Keys[Index].Dest = CreateID(Reader.get_value_string());
                return true;
            }
        }
        return false;
    }

    bool ApplyIntFtol(INI_Reader& Reader, FIntKey const* Keys, unsigned Count)
    {
        unsigned Index = 0;
        for (Index = 0; Index < Count; ++Index)
        {
            if (Reader.is_value(Keys[Index].Name))
            {
                if (Keys[Index].Dest != nullptr)
                    // ReadConstants: get_value_float then MSVCRT ftol on the unrounded x87 value
                    // (value_num = the same read without the float rounding).
                    *Keys[Index].Dest = (int)Reader.value_num(0);
                return true;
            }
        }
        return false;
    }

    bool ApplyDoubleKey(INI_Reader& Reader, FDoubleKey const* Keys, unsigned Count)
    {
        unsigned Index = 0;
        for (Index = 0; Index < Count; ++Index)
        {
            if (Reader.is_value(Keys[Index].Name))
            {
                if (Keys[Index].Dest != nullptr)
                    // ReadConstants: FSTP double of the unrounded x87 value.
                    *Keys[Index].Dest = Reader.value_num(0);
                return true;
            }
        }
        return false;
    }

    // ReadConstants' per-block "unknown key" warning: one FDUMP site per block, each with its
    // own source line (Constants 0x86, EngineEquipConsts 0x9f, ShieldEquipConsts 0xa8,
    // ThrusterEquipConsts 0xb8, PhySysConsts 0xe8, CommConsts 0xfa) and the raw line text.
    void DumpUnknownConstant(INI_Reader& Reader, int Line)
    {
        FDUMP(0x100002, "%s(%d) : *** WARNING: Unknown Constant definition: %s",
              kConstantsSource, Line, Reader.get_line_ptr());
    }
}

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

extern float COLLISION_DAMAGE_FACTOR ;
extern float LOOT_OWNER_SAFE_TIME ;
extern float LOOT_UNSEEN_LIFE_TIME ;
extern float LOOT_UNSEEN_RADIUS ;
extern float MUSIC_CROSS_FADE_DELAY ;
extern float JETTISONED_CARGO_VELOCITY ;
extern ID_String SND_CARGO_JETTISONED ;
extern float MUZZLE_CONE_ANGLE ;
extern ID_String FIRE_FAILED_SOUND ;
extern float FIRE_FAILED_DELAY ;
extern float PLAYER_COLLISION_GROUP_HIT_PTS_SCALE ;
extern float PLAYER_ATTACHED_EQUIP_HIT_PTS_SCALE ;
extern int MAX_PLAYER_AMMO ;
extern float COMM_PLAYER_FAR_DIST ;
extern float COMM_PLAYER_FAR_DIST_ATTEN ;
extern float CHATTER_MAX_DIST ;
extern float CHATTER_MAX_DIST_ATTEN ;
extern float CHATTER_START_ATTEN ;
extern int COMM_CONFLICT_PRIORITY_CUTOFF ;
extern float WALLA_MAX_DIST ;
extern float WALLA_MAX_DIST_ATTEN ;
extern float WALLA_START_ATTEN ;
extern int WALLA_PRIORITY_CUTOFF;

namespace
{
    // [Constants] TestValue: .data:063ed180, compiled-in 0x40490e56 (3.1415f). Written by
    // ReadConstants (06304a53), read by nothing.
    float GTestValue = 3.1415f;

    // [Constants] TestString: a fixed 0x80-byte string -- length at .data:063fce20, text at
    // .data:063fce24 -- seeded with "sample" by the static initializer 063048d0 and refilled
    // by ReadConstants through FUN_06306020 (copy at most size-1 chars, NUL, return the
    // length). Read by nothing else in the module.
    struct FFixedString80
    {
        unsigned Length;
        char Text[0x80];
    };

    // FUN_06306020(dest, size, src): a null source copies nothing and returns 0 (the
    // destination is left as it was); otherwise min(strlen, size - 1) chars, a NUL, that
    // count returned.
    unsigned CopyBounded(char* Dest, unsigned Size, char const* Source)
    {
        if (Source == nullptr)
            return 0;
        unsigned Length = (unsigned)strlen(Source);
        if (Length >= Size)
            Length = Size - 1;
        memcpy(Dest, Source, Length);
        Dest[Length] = '\0';
        return Length;
    }

    FFixedString80 MakeTestString()
    {
        FFixedString80 Out;
        Out.Text[0] = '\0';
        Out.Length = CopyBounded(Out.Text, 0x80, "sample");
        return Out;
    }

    FFixedString80 GTestString = MakeTestString();
}

namespace EngineEquipConsts
{
    // Common.dll.txt 0x63eb5cc-0x63eb5dc: these 5 constants live in .data with real nonzero
    // compiled-in defaults (verified from Ghidra's own float annotations), unlike the rest of
    // this namespace's fields below which are genuinely zero-initialized .bss. Found while
    // porting CEEngine::GetCruiseDrag (Common.dll.c 0x363e0, which FADDs this global directly)
    // -- a prior port revision had all of these defaulted to 0.0f and, worse, some call sites
    // (GetCruiseDrag, GetCruiseThrust) had hardcoded the retail constant as a literal instead
    // of reading the (wrongly-zeroed) global, masking the bug for those two call sites only.
    // Each is still INI-overridable via the values-loader around Common.dll.c line ~142193.
    float CRUISING_SPEED = 300.0f;
    float CRUISE_ACCEL_TIME = 5.0f;
    float CRUISE_DRAG = 3.0f;
    float CRUISE_DISRUPT_TIME = 0.0f;
    float MAX_DELTA_FX_THROTTLE = 0.1f;
    float MAX_ENGINE_FX_THROTTLE = 0.9f;
    float THROTTLE_STEADY_TIME = 0.0f;
    float THROTTLE_ATTEN_MOD_RANGE = 0.0f;
    float DELTA_THROTTLE_ATTEN_MOD_CHANGING = 0.0f;
    float DELTA_THROTTLE_ATTEN_MOD_STEADY = 0.0f;
    float CRUISE_STEADY_TIME = 0.0f;
    float DELTA_CRUISE_ATTEN_MOD_STEADY = 0.0f;
    float CRUISE_ATTEN_MOD_RANGE = 0.0f;
}

namespace ShieldEquipConsts
{
    float HULL_DAMAGE_FACTOR = 1.0f;
}

namespace ThrusterEquipConsts
{
    // Common.dll.c LAB_0629d3c0/LAB_0629d3e0: retail's static initializers set these to
    // CreateID("interior_thruster_sound")/CreateID("exterior_thruster_sound") as hardcoded
    // defaults *before* ReadConstants() applies any ini override below, not zero.
    ID_String INTERIOR_SOUND_NAME = {CreateID("interior_thruster_sound")};
    ID_String EXTERIOR_SOUND_NAME = {CreateID("exterior_thruster_sound")};
    float MAX_VOLUME_FORCE = 20000.0f;
    float MIN_VOLUME_FORCE = 5000.0f;
    float INSIDE_CONE_ANGLE = 60.0f;
    float OUTSIDE_CONE_ANGLE = 220.0f;
    float OUTSIDE_CONE_ATTENUATION = -10.0f;
}

namespace PhySys
{
    // Retail defaults re-derived by reading the raw .data bytes at each DAT_ address in
    // Common.dll.txt (0x063eb318-0x063eb35c) - a prior pass had left every one of these
    // zero-initialized, which did not match the compiled-in retail values.
    float PHYSICAL_SIM_RATE = 16.0f;
    int MAX_SPAWNED_MINDIST_COUNT = 100;
    float ANOM_LIMITS_MAX_VELOCITY = 1000.0f;
    float ANOM_LIMITS_MAX_ANGULAR_VELOCITY_PER_PSI = 0.94247788f;
    double MATERIAL_FRICTION = 0.1;
    double MATERIAL_ELASTICITY = 0.9;
    double MIN_TIME_BETWEEN_COLLISIONS = 0.5;
    float RMGR_LOOK_AHEAD_TIME_WORLD = 1.0f;
    float RMGR_LOOK_AHEAD_MAX_RADIUS_WORLD = 5.0f;
    float RMGR_LOOK_AHEAD_MIN_DISTANCE_WORLD = 100.0f;
    float RMGR_LOOK_AHEAD_MAX_DISTANCE_WORLD = 1000.0f;
    // CRT initializer LAB_06290890: DAT_0639c418 (2.1f) / PHYSICAL_SIM_RATE,
    // rounded by FSTP into DAT_063fc0f8. The PE's zero-filled storage is not the runtime default.
    float RMGR_LOOK_AHEAD_MIN_SECONDS_WORLD = (float)((double)2.1f / PHYSICAL_SIM_RATE);
    float RMGR_LOOK_AHEAD_TIME_INTRA = 0.5f;
    float RMGR_LOOK_AHEAD_MAX_RADIUS_INTRA = 0.89999998f;
    float RMGR_LOOK_AHEAD_MIN_DISTANCE_INTRA = 5.0f;
    float RMGR_LOOK_AHEAD_MAX_DISTANCE_INTRA = 50.0f;
    // CRT initializer LAB_062908b0 performs the same division for DAT_063fc0f4.
    float RMGR_LOOK_AHEAD_MIN_SECONDS_INTRA = (float)((double)2.1f / PHYSICAL_SIM_RATE);
}


// 0x06304990 ?ReadConstants@@YA_NPBD@Z. Path through GetFileSysPath with *DALib::Data; an
// unopenable file returns false with no message; otherwise every block is read and true is
// returned (the reader is never closed explicitly, its destructor does it). The retail body is
// one long is_value chain per block; the port walks key tables per block, which tests the same
// names against the same destinations -- the order of is_value tests has no side effect.
bool __cdecl ReadConstants(char const* RelPath)
{
    char FullPath[kPathChars];
    GetFileSysPath(FullPath, RelPath, DALib::Data);

    INI_Reader Reader;
    if (!Reader.open(FullPath, false))
        return false;

    FFloatKey ConstantsFloats[] = {
        {"TestValue", &GTestValue},
        {"COLLISION_DAMAGE_FACTOR", &COLLISION_DAMAGE_FACTOR},
        {"LOOT_OWNER_SAFE_TIME", &LOOT_OWNER_SAFE_TIME},
        {"LOOT_UNSEEN_LIFE_TIME", &LOOT_UNSEEN_LIFE_TIME},
        {"LOOT_UNSEEN_RADIUS", &LOOT_UNSEEN_RADIUS},
        {"MUSIC_CROSS_FADE_DELAY", &MUSIC_CROSS_FADE_DELAY},
        {"JETTISONED_CARGO_VELOCITY", &JETTISONED_CARGO_VELOCITY},
        {"MUZZLE_CONE_ANGLE", &MUZZLE_CONE_ANGLE},
        {"FIRE_FAILED_DELAY", &FIRE_FAILED_DELAY},
        {"PLAYER_COLLISION_GROUP_HIT_PTS_SCALE", &PLAYER_COLLISION_GROUP_HIT_PTS_SCALE},
        {"PLAYER_ATTACHED_EQUIP_HIT_PTS_SCALE", &PLAYER_ATTACHED_EQUIP_HIT_PTS_SCALE},
    };
    FUintKey ConstantsIds[] = {
        {"SND_CARGO_JETTISONED", &SND_CARGO_JETTISONED.Id},
        {"FIRE_FAILED_SOUND", &FIRE_FAILED_SOUND.Id},
    };
    FIntKey ConstantsInts[] = {
        {"MAX_PLAYER_AMMO", &MAX_PLAYER_AMMO},
    };

    FFloatKey EngineFloats[] = {
        {"CRUISING_SPEED", &EngineEquipConsts::CRUISING_SPEED},
        {"CRUISE_ACCEL_TIME", &EngineEquipConsts::CRUISE_ACCEL_TIME},
        {"CRUISE_DRAG", &EngineEquipConsts::CRUISE_DRAG},
        {"CRUISE_DISRUPT_TIME", &EngineEquipConsts::CRUISE_DISRUPT_TIME},
        {"MAX_DELTA_FX_THROTTLE", &EngineEquipConsts::MAX_DELTA_FX_THROTTLE},
        {"MAX_ENGINE_FX_THROTTLE", &EngineEquipConsts::MAX_ENGINE_FX_THROTTLE},
        {"THROTTLE_STEADY_TIME", &EngineEquipConsts::THROTTLE_STEADY_TIME},
        {"THROTTLE_ATTEN_MOD_RANGE", &EngineEquipConsts::THROTTLE_ATTEN_MOD_RANGE},
        {"DELTA_THROTTLE_ATTEN_MOD_CHANGING", &EngineEquipConsts::DELTA_THROTTLE_ATTEN_MOD_CHANGING},
        {"DELTA_THROTTLE_ATTEN_MOD_STEADY", &EngineEquipConsts::DELTA_THROTTLE_ATTEN_MOD_STEADY},
        {"CRUISE_STEADY_TIME", &EngineEquipConsts::CRUISE_STEADY_TIME},
        {"DELTA_CRUISE_ATTEN_MOD_STEADY", &EngineEquipConsts::DELTA_CRUISE_ATTEN_MOD_STEADY},
        {"CRUISE_ATTEN_MOD_RANGE", &EngineEquipConsts::CRUISE_ATTEN_MOD_RANGE},
    };

    FFloatKey ShieldFloats[] = {
        {"HULL_DAMAGE_FACTOR", &ShieldEquipConsts::HULL_DAMAGE_FACTOR},
    };

    FFloatKey ThrusterFloats[] = {
        {"MAX_VOLUME_FORCE", &ThrusterEquipConsts::MAX_VOLUME_FORCE},
        {"MIN_VOLUME_FORCE", &ThrusterEquipConsts::MIN_VOLUME_FORCE},
        {"INSIDE_CONE_ANGLE", &ThrusterEquipConsts::INSIDE_CONE_ANGLE},
        {"OUTSIDE_CONE_ANGLE", &ThrusterEquipConsts::OUTSIDE_CONE_ANGLE},
        {"OUTSIDE_CONE_ATTENUATION", &ThrusterEquipConsts::OUTSIDE_CONE_ATTENUATION},
    };
    // The two thruster sound keys are ID_String, so their id field is what the loader fills.
    FUintKey ThrusterIds[] = {
        {"INTERIOR_SOUND_NAME", &ThrusterEquipConsts::INTERIOR_SOUND_NAME.Id},
        {"EXTERIOR_SOUND_NAME", &ThrusterEquipConsts::EXTERIOR_SOUND_NAME.Id},
    };

    FFloatKey PhyFloats[] = {
        {"PHYSICAL_SIM_RATE", &PhySys::PHYSICAL_SIM_RATE},
        {"ANOM_LIMITS_MAX_VELOCITY", &PhySys::ANOM_LIMITS_MAX_VELOCITY},
        {"ANOM_LIMITS_MAX_ANGULAR_VELOCITY_PER_PSI", &PhySys::ANOM_LIMITS_MAX_ANGULAR_VELOCITY_PER_PSI},
        {"RMGR_LOOK_AHEAD_TIME_WORLD", &PhySys::RMGR_LOOK_AHEAD_TIME_WORLD},
        {"RMGR_LOOK_AHEAD_MAX_RADIUS_WORLD", &PhySys::RMGR_LOOK_AHEAD_MAX_RADIUS_WORLD},
        {"RMGR_LOOK_AHEAD_MIN_DISTANCE_WORLD", &PhySys::RMGR_LOOK_AHEAD_MIN_DISTANCE_WORLD},
        {"RMGR_LOOK_AHEAD_MAX_DISTANCE_WORLD", &PhySys::RMGR_LOOK_AHEAD_MAX_DISTANCE_WORLD},
        {"RMGR_LOOK_AHEAD_MIN_SECONDS_WORLD", &PhySys::RMGR_LOOK_AHEAD_MIN_SECONDS_WORLD},
        {"RMGR_LOOK_AHEAD_TIME_INTRA", &PhySys::RMGR_LOOK_AHEAD_TIME_INTRA},
        {"RMGR_LOOK_AHEAD_MAX_RADIUS_INTRA", &PhySys::RMGR_LOOK_AHEAD_MAX_RADIUS_INTRA},
        {"RMGR_LOOK_AHEAD_MIN_DISTANCE_INTRA", &PhySys::RMGR_LOOK_AHEAD_MIN_DISTANCE_INTRA},
        {"RMGR_LOOK_AHEAD_MAX_DISTANCE_INTRA", &PhySys::RMGR_LOOK_AHEAD_MAX_DISTANCE_INTRA},
        {"RMGR_LOOK_AHEAD_MIN_SECONDS_INTRA", &PhySys::RMGR_LOOK_AHEAD_MIN_SECONDS_INTRA},
        {"DEFAULT_LINEAR_DAMPING", &PhySys::DEFAULT_LINEAR_DAMPING},
        {"GOLEM_CHILD_MASS", &PhySys::GOLEM_CHILD_MASS},
        {"GOLEM_CHILD_LINEAR_DAMP", &PhySys::GOLEM_CHILD_LINEAR_DAMP},
        {"GOLEM_MAX_DELTA_POSITION", &PhySys::GOLEM_MAX_DELTA_POSITION},
        {"GOLEM_DELTA_ORIENTATION", &PhySys::GOLEM_DELTA_ORIENTATION},
        {"GOLEM_MAX_TORQUE", &PhySys::GOLEM_MAX_TORQUE},
        {"GOLEM_FORCE_FACTOR", &PhySys::GOLEM_FORCE_FACTOR},
        {"GOLEM_DAMP_FACTOR", &PhySys::GOLEM_DAMP_FACTOR},
        {"GOLEM_TORQUE_FACTOR", &PhySys::GOLEM_TORQUE_FACTOR},
        {"GOLEM_ANGULAR_DAMP_FACTOR", &PhySys::GOLEM_ANGULAR_DAMP_FACTOR},
        {"RC_MAX_DELTA_POSITION", &PhySys::RC_MAX_DELTA_POSITION},
        {"RC_MAX_DELTA_ORIENTATION", &PhySys::RC_MAX_DELTA_ORIENTATION},
    };
    FDoubleKey PhyDoubles[] = {
        {"MATERIAL_FRICTION", &PhySys::MATERIAL_FRICTION},
        {"MATERIAL_ELASTICITY", &PhySys::MATERIAL_ELASTICITY},
        {"MIN_TIME_BETWEEN_COLLISIONS", &PhySys::MIN_TIME_BETWEEN_COLLISIONS},
    };
    FIntKey PhyInts[] = {
        {"MAX_SPAWNED_MINDIST_COUNT", &PhySys::MAX_SPAWNED_MINDIST_COUNT},
    };

    FFloatKey CommFloats[] = {
        {"COMM_PLAYER_FAR_DIST", &COMM_PLAYER_FAR_DIST},
        {"COMM_PLAYER_FAR_DIST_ATTEN", &COMM_PLAYER_FAR_DIST_ATTEN},
        {"CHATTER_MAX_DIST", &CHATTER_MAX_DIST},
        {"CHATTER_MAX_DIST_ATTEN", &CHATTER_MAX_DIST_ATTEN},
        {"CHATTER_START_ATTEN", &CHATTER_START_ATTEN},
        {"WALLA_MAX_DIST", &WALLA_MAX_DIST},
        {"WALLA_MAX_DIST_ATTEN", &WALLA_MAX_DIST_ATTEN},
        {"WALLA_START_ATTEN", &WALLA_START_ATTEN},
    };
    FIntKey CommInts[] = {
        {"COMM_CONFLICT_PRIORITY_CUTOFF", &COMM_CONFLICT_PRIORITY_CUTOFF},
        {"WALLA_PRIORITY_CUTOFF", &WALLA_PRIORITY_CUTOFF},
    };

    while (Reader.read_header())
    {
        if (Reader.is_header("Constants"))
        {
            while (Reader.read_value())
            {
                if (Reader.is_value("TestString"))
                {
                    // 06304a70-06304a9e: a null value empties the string (length 0, first
                    // byte 0); otherwise the bounded copy into the 0x80 buffer.
                    char const* const Text = Reader.get_value_string();
                    if (Text == nullptr)
                    {
                        GTestString.Length = 0;
                        GTestString.Text[0] = '\0';
                    }
                    else
                    {
                        GTestString.Length = CopyBounded(GTestString.Text, 0x80, Text);
                    }
                    continue;
                }
                if (ApplyFloatKey(Reader, ConstantsFloats, sizeof(ConstantsFloats) / sizeof(ConstantsFloats[0])))
                    continue;
                if (ApplyUintCreateId(Reader, ConstantsIds, sizeof(ConstantsIds) / sizeof(ConstantsIds[0])))
                    continue;
                if (ApplyIntFtol(Reader, ConstantsInts, sizeof(ConstantsInts) / sizeof(ConstantsInts[0])))
                    continue;
                DumpUnknownConstant(Reader, 0x86);
            }
        }
        else if (Reader.is_header("EngineEquipConsts"))
        {
            while (Reader.read_value())
            {
                if (ApplyFloatKey(Reader, EngineFloats, sizeof(EngineFloats) / sizeof(EngineFloats[0])))
                    continue;
                DumpUnknownConstant(Reader, 0x9f);
            }
        }
        else if (Reader.is_header("ShieldEquipConsts"))
        {
            while (Reader.read_value())
            {
                if (ApplyFloatKey(Reader, ShieldFloats, sizeof(ShieldFloats) / sizeof(ShieldFloats[0])))
                    continue;
                DumpUnknownConstant(Reader, 0xa8);
            }
        }
        else if (Reader.is_header("ThrusterEquipConsts"))
        {
            while (Reader.read_value())
            {
                if (ApplyUintCreateId(Reader, ThrusterIds, sizeof(ThrusterIds) / sizeof(ThrusterIds[0])))
                    continue;
                if (ApplyFloatKey(Reader, ThrusterFloats, sizeof(ThrusterFloats) / sizeof(ThrusterFloats[0])))
                    continue;
                DumpUnknownConstant(Reader, 0xb8);
            }
        }
        else if (Reader.is_header("PhySysConsts"))
        {
            while (Reader.read_value())
            {
                if (Reader.is_value("DEFAULT_ANGULAR_DAMPING"))
                {
                    PhySys::DEFAULT_ANGULAR_DAMPING.x = Reader.get_value_float(0);
                    PhySys::DEFAULT_ANGULAR_DAMPING.y = Reader.get_value_float(1);
                    PhySys::DEFAULT_ANGULAR_DAMPING.z = Reader.get_value_float(2);
                    continue;
                }
                if (Reader.is_value("GOLEM_CHILD_ANGULAR_DAMP"))
                {
                    PhySys::GOLEM_CHILD_ANGULAR_DAMP.x = Reader.get_value_float(0);
                    PhySys::GOLEM_CHILD_ANGULAR_DAMP.y = Reader.get_value_float(1);
                    PhySys::GOLEM_CHILD_ANGULAR_DAMP.z = Reader.get_value_float(2);
                    continue;
                }
                if (Reader.is_value("GOLEM_MAX_TRANSLATION_FORCE"))
                {
                    PhySys::GOLEM_MAX_TRANSLATION_FORCE.x = Reader.get_value_float(0);
                    PhySys::GOLEM_MAX_TRANSLATION_FORCE.y = Reader.get_value_float(1);
                    PhySys::GOLEM_MAX_TRANSLATION_FORCE.z = Reader.get_value_float(2);
                    continue;
                }
                if (ApplyFloatKey(Reader, PhyFloats, sizeof(PhyFloats) / sizeof(PhyFloats[0])))
                    continue;
                if (ApplyDoubleKey(Reader, PhyDoubles, sizeof(PhyDoubles) / sizeof(PhyDoubles[0])))
                    continue;
                if (ApplyIntFtol(Reader, PhyInts, sizeof(PhyInts) / sizeof(PhyInts[0])))
                    continue;
                DumpUnknownConstant(Reader, 0xe8);
            }
        }
        else if (Reader.is_header("CommConsts"))
        {
            while (Reader.read_value())
            {
                if (ApplyFloatKey(Reader, CommFloats, sizeof(CommFloats) / sizeof(CommFloats[0])))
                    continue;
                if (ApplyIntFtol(Reader, CommInts, sizeof(CommInts) / sizeof(CommInts[0])))
                    continue;
                DumpUnknownConstant(Reader, 0xfa);
            }
        }
        else if (Reader.is_header("CollisionConsts"))
        {
            Collision::Load(Reader);
        }
    }

    return true;
}
