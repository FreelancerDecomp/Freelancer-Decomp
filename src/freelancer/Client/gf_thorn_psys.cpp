// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "gf_thorn_psys.h"

#include "alchemy_float_forward.h"
#include "alchemy.h"
#include "common_ini.h" // StringAlloc
#include "dacom.h"
#include "gf_object_ship.h"
#include "vis_effect_arch.h"

#include <cstdint>
#include <cstring>

namespace
{
    char const* const GScriptPsysFile = "E:\\FL\\Scratch\\Source\\Client\\GF\\ScriptPsys.cpp";

    // IFxEffect slots 7 / 8 still use the existing Dt argument adapter.
    void* FloatArg(float Value)
    {
        void* Out = nullptr;
        memcpy(&Out, &Value, sizeof(Value));
        return Out;
    }

}

// ---------------------------------------------------------------------------------------
// Thorn_Psys
// ---------------------------------------------------------------------------------------

// FUN_00453d70. Base ctor; +0x6c / +0x70 / +0x74 = 0; vptrs; +0x78 = 0 and +0x84 identity;
// +0xa8 = 0.
FThornPsys::FThornPsys(Csys const& ScriptFrame, FGfScript* InOwner, FThornEntitySnap const* Desc)
    : FThornComponent(ScriptFrame, InOwner, Desc),
      Vis(nullptr),
      Sparam(0.0f)
{
    Particles.Effect = nullptr;
    Particles.Vis = nullptr;
    Local.Pos.x = 0.0f;
    Local.Pos.y = 0.0f;
    Local.Pos.z = 0.0f;
    MatrixSetIdentity(Local.Orient);
}

// FUN_00453e60. 0x00453e62..0x00453e7a: StringAlloc(EffectName, 0), FUN_0042dd20 on it (the
// VisEffect map lookup); +0x74 = the answer; non-null -> FUN_0042e080, else FDUMP(0x100001,
// "Thorn_Psys: Failed to find [VisEffect] %s.", ScriptPsys.cpp, 0x4c, EffectName). The
// StringAlloc'd copy is never freed, as retail.
void FThornPsys::BindVisEffect(char const* EffectName)
{
    Vis = FindVisEffectByNickname(StringAlloc(EffectName, false));
    if (Vis != nullptr)
    {
        VisEffectArchAddRef(Vis);
        return;
    }
    FDUMP(0x100001, "%s(%d) : *** ERROR: Thorn_Psys: Failed to find [VisEffect] %s.",
          GScriptPsysFile, 0x4c, EffectName);
}

// LAB_00453f00 (identity, RET 8). +0x78 = Transform+0x24, +0x84 = the rotation. No frame.
GENRESULT DACOM_API FThornPsys::SetTransform(float const* Transform12)
{
    Local.Pos.x = Transform12[9];
    Local.Pos.y = Transform12[10];
    Local.Pos.z = Transform12[11];
    memcpy(&Local.Orient, Transform12, sizeof(Local.Orient));
    return GR_OK;
}

// LAB_00453f60 (identity, RET 8). {+0x84 rotation, +0x78 position} copied out as 0xc dwords.
GENRESULT DACOM_API FThornPsys::GetTransform(float* OutTransform12)
{
    memcpy(OutTransform12, &Local.Orient, sizeof(Local.Orient));
    OutTransform12[9] = Local.Pos.x;
    OutTransform12[10] = Local.Pos.y;
    OutTransform12[11] = Local.Pos.z;
    return GR_OK;
}

// LAB_00453ec0 (geo facet, RET 8). Facet +0x6c = +0x78.
GENRESULT DACOM_API FThornPsys::SetPosition(float* Position)
{
    Local.Pos.x = Position[0];
    Local.Pos.y = Position[1];
    Local.Pos.z = Position[2];
    return GR_OK;
}

// LAB_00453ee0 (geo facet, RET 8). REP MOVSD 9 into facet +0x78 (= +0x84).
GENRESULT DACOM_API FThornPsys::SetOrientation(float* Orient9)
{
    memcpy(&Local.Orient, Orient9, sizeof(Local.Orient));
    return GR_OK;
}

// LAB_00454770 (geo facet, RET 8).
GENRESULT DACOM_API FThornPsys::GetPosition(float* OutPosition)
{
    OutPosition[0] = Local.Pos.x;
    OutPosition[1] = Local.Pos.y;
    OutPosition[2] = Local.Pos.z;
    return GR_OK;
}

// LAB_00453f40 (geo facet, RET 8).
GENRESULT DACOM_API FThornPsys::GetOrientation(float* OutOrient9)
{
    memcpy(OutOrient9, &Local.Orient, sizeof(Local.Orient));
    return GR_OK;
}

// LAB_00453fb0 (psys facet, RET 4). Facet +0x5c (= +0x74) non-null -> FUN_00557360 on facet
// +0x54 (= +0x6c) with it. 0.
GENRESULT DACOM_API FThornPsys::Start()
{
    if (Vis != nullptr)
        Particles.Create(Vis);
    return GR_OK;
}

// LAB_00453fd0 (psys facet, RET 4). FUN_005574a0 on +0x6c. 0.
GENRESULT DACOM_API FThornPsys::StopEffect()
{
    Particles.Destroy();
    return GR_OK;
}

// LAB_00453ff0 (psys facet, RET 8). Dt = DeltaMs * 0.001 (DAT_005c75f8, double, stored as a
// float); with an effect: slot 3 (1, &{+0x84 rotation, +0x78 position}), slot 7 (Dt), slot 8
// (Dt). 0.
GENRESULT DACOM_API FThornPsys::Tick(int DeltaMs)
{
    float const Dt = static_cast<float>(static_cast<double>(DeltaMs) * 0.001);
    if (Particles.Effect != nullptr)
    {
        float Xform[12];
        memcpy(Xform, &Local.Orient, sizeof(float) * 9);
        Xform[9] = Local.Pos.x;
        Xform[10] = Local.Pos.y;
        Xform[11] = Local.Pos.z;
        Particles.Effect->Slot3(reinterpret_cast<void*>(1), Xform);
        Particles.Effect->Slot7(FloatArg(Dt));
        Particles.Effect->Slot8(FloatArg(Dt));
    }
    return GR_OK;
}

// LAB_00454070 (psys facet, RET 8). Facet +0x90 (= +0xa8) = Value; with an effect, its slot 6
// with the value. 0.
GENRESULT DACOM_API FThornPsys::SetSparam(float Value)
{
    Sparam = Value;
    if (Particles.Effect != nullptr)
        ForwardAlchemyEffectFloat(Particles.Effect, &IFxEffect::Slot6, Value);
    return GR_OK;
}

// LAB_004540a0 (psys facet, RET 8).
GENRESULT DACOM_API FThornPsys::GetSparam(float* OutValue)
{
    *OutValue = Sparam;
    return GR_OK;
}

// FUN_00453e30. The psys facet's slot 4 through its vtable (0x00453e33..0x00453e3a), then
// FUN_0042e160 on +0x74 when set; +0x74 keeps its value.
void FThornPsys::Stop()
{
    static_cast<IPSysControl*>(this)->StopEffect();
    if (Vis != nullptr)
        VisEffectArchRelease(Vis);
}

// FUN_00450870.
void* FThornPsys::GetEngineInstance()
{
    return reinterpret_cast<void*>(static_cast<intptr_t>(-1));
}

// LAB_00453e50 -- ADD ECX,0x6c; JMP FUN_005574d0.
void FThornPsys::DrawEffects(ICamera* Camera)
{
    Particles.Draw(Camera);
}

// ---------------------------------------------------------------------------------------
// Thorn_PlayerEnginePsys
// ---------------------------------------------------------------------------------------

// FUN_004540c0. Base ctor, vptrs, 0x00454117 +0x6c = 0.
FThornPlayerEnginePsys::FThornPlayerEnginePsys(Csys const& ScriptFrame, FGfScript* InOwner,
                                               FThornEntitySnap const* Desc)
    : FThornComponent(ScriptFrame, InOwner, Desc),
      Sparam(0.0f)
{
}

// LAB_00454150 (psys facet, RET 4). FUN_00446be0; non-null -> FUN_00447d70 on it. 0.
GENRESULT DACOM_API FThornPlayerEnginePsys::Start()
{
    FObjectShip* const Ship = GfObjectShip();
    if (Ship != nullptr)
        Ship->StartEngineEffects();
    return GR_OK;
}

// LAB_00454170 (psys facet, RET 4). FUN_00447f00 on the ship. 0.
GENRESULT DACOM_API FThornPlayerEnginePsys::StopEffect()
{
    FObjectShip* const Ship = GfObjectShip();
    if (Ship != nullptr)
        Ship->StopEngineEffects();
    return GR_OK;
}

// LAB_004541a0 (psys facet, RET 8). Facet +0x54 (= +0x6c) = Value, then FUN_00447f50(Value) on
// the ship. 0.
GENRESULT DACOM_API FThornPlayerEnginePsys::SetSparam(float Value)
{
    Sparam = Value;
    FObjectShip* const Ship = GfObjectShip();
    if (Ship != nullptr)
        Ship->SetEngineSparam(Value);
    return GR_OK;
}

// LAB_004541d0 (psys facet, RET 8).
GENRESULT DACOM_API FThornPlayerEnginePsys::GetSparam(float* OutValue)
{
    *OutValue = Sparam;
    return GR_OK;
}

// LAB_00454130 -- the psys facet's slot 4 through its vtable.
void FThornPlayerEnginePsys::Stop()
{
    static_cast<IPSysControl*>(this)->StopEffect();
}

// LAB_00454140 -- MOV EAX,0xfffffff9: not -1, the "not implemented" code.
void* FThornPlayerEnginePsys::GetEngineInstance()
{
    return reinterpret_cast<void*>(static_cast<intptr_t>(kThornSlotNotImplemented));
}
