// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Archetype::Asteroid, DynamicAsteroid, MotorData, Explosion and the explosion map -- split out of
// common_archetype.cpp (retail Archetype.cpp, one TU) in CMN-SPLIT phase B.

#include "common_archetype.h"
#include "common_archetype_internal.h"

#include "common_cobject.h" // CollisionGroupDesc (get_undamaged_collision_group_list)
#include "common_fuses.h"
#include "common_ini.h"
#include "common_misc.h"
#include "common_phyarch.h"
#include "common_universe.h"
#include "DALib.h"
#include "dacom.h"
#include "readfile.h"

#include <cstddef>
#include <list>
#include <map>
#include <math.h>
#include <memory>
#include <stdlib.h>
#include <string.h>
#include <vector>

namespace Archetype
{
    std::map<unsigned, Explosion> GExplosionDb;

    Asteroid::Asteroid(ICliObj* InCompanion)
        : Root(InCompanion)
        , DetectRadius(25.0f)
        , ExplosionOffset(0.0f)
        , RechargeTime(1000.0f)
    {
        ObjectType = 0x20000000;
    }

    bool Asteroid::read(INI_Reader& Reader)
    {
        // No "inherit" case: LoadAsteroids handles that key before calling read() (062fa2c0).
        if (Root::read(Reader))
            return true;

        if (Reader.is_value("detect_radius"))
        {
            DetectRadius = Reader.get_value_float(0);
            return true;
        }

        if (Reader.is_value("explosion_offset"))
        {
            ExplosionOffset = Reader.get_value_float(0);
            return true;
        }

        if (Reader.is_value("recharge_time"))
        {
            RechargeTime = Reader.get_value_float(0);
            return true;
        }

        return false;
    }

    AClassType Asteroid::get_class_type() const
    {
        // Common.dll.c 0xfd90 (Archetype::Asteroid::get_class_type): return 4.
        return AClassAsteroid;
    }

    DynamicAsteroid::DynamicAsteroid(ICliObj* InCompanion)
        : Root(InCompanion)
    {
        ObjectType = 0x40000000;
    }

    // Common.dll.txt 0x062fa0b0-0x062fa0bf: Root::read(Reader), result normalised with SETNZ.
    bool DynamicAsteroid::read(INI_Reader& Reader)
    {
        return Root::read(Reader) != false;
    }

    AClassType DynamicAsteroid::get_class_type() const
    {
        // Common.dll.c 0xfe80 (Archetype::DynamicAsteroid::get_class_type): return 5.
        return AClassDynamicAsteroid;
    }

    MotorData::MotorData()
        : VtableUnused(0)
        , Delay(0.0f)
        , Accel(15.0f)
        , Lifetime(1.5f)
    {
        Nickname.clear();
    }

    void MotorData::redefine(MotorData const& Other)
    {
        // Retail vtable 063a1ecc slot 1 = 062f7ba0 (left undisassembled by Ghidra; decoded from
        // the raw bytes): for accel (+0x0c), lifetime (+0x10) and delay (+0x08) in that order,
        // `FLD Other; FCOMP this; FNSTSW; TEST AH,0x44; JNP skip` -- equal skips, unequal or
        // unordered copies the dword. The nickname is not touched. RET 4.
        if (Other.Accel != Accel)
            Accel = Other.Accel;
        if (Other.Lifetime != Lifetime)
            Lifetime = Other.Lifetime;
        if (Other.Delay != Delay)
            Delay = Other.Delay;
    }

    bool MotorData::read(INI_Reader& Reader)
    {
        // Retail 062f7b00..062f7b99. "nickname" uses the no-argument get_value_string
        // (062f7b1a -> 06310b70, the whole value).
        if (Reader.is_value("nickname"))
        {
            Nickname.Id = CreateID(Reader.get_value_string());
            return true;
        }

        if (Reader.is_value("accel"))
        {
            Accel = Reader.get_value_float(0);
            return true;
        }

        if (Reader.is_value("lifetime"))
        {
            Lifetime = Reader.get_value_float(0);
            return true;
        }

        if (Reader.is_value("delay"))
        {
            Delay = Reader.get_value_float(0);
            return true;
        }

        return false;
    }

    // Common.dll.txt 0x0630b730-0x0630b7b2: Nickname = *InNickname; Radius, Impulse,
    // HullDamage, EnergyDamage = 0; Process = 1; Lifetime = 0; LifetimeMax = 10000.0f; empty
    // effect list; NumChildPieces = 0; DebrisImpulse = 5.0f; InnardsDebrisStartTime = -1.0f;
    // InnardsDebrisNum/Radius = 0; both vectors empty -- exactly reset()'s values.
    Explosion::Explosion(ID_String const& InNickname)
        : Nickname(InNickname)
    {
        reset();
    }

    void Explosion::reset()
    {
        Radius = 0.0f;
        Impulse = 0.0f;
        HullDamage = 0.0f;
        EnergyDamage = 0.0f;
        Process = 1;
        Lifetime = 0.0f;
        LifetimeMax = 10000.0f;
        Effects.clear();
        NumChildPieces = 0;
        DebrisImpulse = 5.0f;
        InnardsDebrisStartTime = -1.0f;
        InnardsDebrisNum = 0.0f;
        InnardsDebrisRadius = 0.0f;
        InnardsDebrisObjects.clear();
        DebrisTypes.clear();
    }

    bool Explosion::read(INI_Reader& Reader)
    {
        if (Reader.is_value("radius"))
        {
            Radius = Reader.get_value_float(0);
            return true;
        }
        if (Reader.is_value("impulse"))
        {
            Impulse = Reader.get_value_float(0);
            return true;
        }
        if (Reader.is_value("hull_damage"))
        {
            HullDamage = Reader.get_value_float(0);
            return true;
        }
        if (Reader.is_value("energy_damage"))
        {
            EnergyDamage = Reader.get_value_float(0);
            return true;
        }
        if (Reader.is_value("lifetime"))
        {
            Lifetime = Reader.get_value_float(0);
            if (Reader.is_value_empty(1))
                LifetimeMax = Lifetime;
            else
                LifetimeMax = Reader.get_value_float(1);
            return true;
        }
        if (Reader.is_value("effect"))
        {
            FExplosionEffect Effect;
            Effect.Fx.Id = CreateID(Reader.get_value_string(0));
            if (Reader.is_value_empty(1))
                Effect.Strength = -1.0f;
            else
                Effect.Strength = Reader.get_value_float(1);
            Effects.push_back(Effect);
            return true;
        }
        if (Reader.is_value("process"))
        {
            // 0630b94c..0630b99b: the 3-entry table 063ed218 {none 2, shatter 0, disappear 1},
            // DACOM.DLL stricmp, first match stored at +0x14; no match leaves it.
            char const* Text = Reader.get_value_string();
            if (CallDacomStricmp(Text, "none") == 0)
                Process = 2;
            else if (CallDacomStricmp(Text, "shatter") == 0)
                Process = 0;
            else if (CallDacomStricmp(Text, "disappear") == 0)
                Process = 1;
            return true;
        }
        if (Reader.is_value("debris_type"))
        {
            FExplosionDebrisType Debris;
            Debris.Debris.Id = CreateID(Reader.get_value_string(0));
            Debris.Count = 0.0f;
            if (!Reader.is_value_empty(1))
                Debris.Count = (float)Reader.get_value_int(1);
            DebrisTypes.push_back(Debris);
            return true;
        }
        if (Reader.is_value("num_child_pieces"))
        {
            // 0630ba34: ftol on the unrounded get_value_float result (value_num = same read).
            NumChildPieces = (int)Reader.value_num(0);
            return true;
        }
        if (Reader.is_value("debris_impulse"))
        {
            DebrisImpulse = Reader.get_value_float(0);
            return true;
        }
        if (Reader.is_value("innards_debris_object"))
        {
            InnardsDebrisObjects.push_back(CreateID(Reader.get_value_string()));
            return true;
        }
        if (Reader.is_value("innards_debris_radius"))
        {
            InnardsDebrisRadius = Reader.get_value_float(0);
            return true;
        }
        if (Reader.is_value("innards_debris_start_time"))
        {
            InnardsDebrisStartTime = Reader.get_value_float(0);
            return true;
        }
        if (Reader.is_value("innards_debris_num"))
        {
            InnardsDebrisNum = Reader.get_value_float(0);
            return true;
        }
        return true;
    }
}
