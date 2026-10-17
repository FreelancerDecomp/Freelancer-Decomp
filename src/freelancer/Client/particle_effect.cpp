// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "particle_effect.h"

#include "alchemy_float_forward.h"

#include "cli_projectile_fx.h"
#include "client_gameobj.h"
#include "effect_base.h"
#include "effect_type_info.h"
#include "effect_type_list.h"
#include "effect_vertex_batch.h" // G_EffectMathEngine (DAT_00674f50)
#include "engine_light.h"
#include "fade_to_black.h"       // FadeOutColoured / FadeInColoured / IsFadeOpaque (CameraParticles)
#include "game_options.h"
#include "globals.h"             // ShouldSkipLocalServerTick (FUN_0042d740)
#include "menu_sound_arch.h"
#include "perf_options.h"
#include "space_camera.h"
#include "space_scene_obj_draw.h" // the field controller DAT_00675244 / 0067524c / 00675248

#include "DALib.h"
#include "common_cobject.h"
#include "common_csys.h"          // Acquire3DMathEngine (FUN_005b73e0)
#include "dacom.h"
#include "soundmanager.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>

namespace
{


    void CallKillSlot(FParticleEffect* This)
    {
        This->Kill();
    }

    // DAT_00674f50, filled by FUN_005b73e0 on first use at each site (0x004f88e1, ...).
    I3DMathEngine* ParticleMath()
    {
        if (G_EffectMathEngine == nullptr)
            G_EffectMathEngine = Acquire3DMathEngine();
        return G_EffectMathEngine;
    }

    FEffectBase* AsEffectBase(FParticleEffect* This)
    {
        return This;
    }

    // IFxEffect slots take their float argument as the raw dword.
    void* FloatArg(float Value)
    {
        void* Arg = nullptr;
        std::memcpy(&Arg, &Value, sizeof(float));
        return Arg;
    }

    // These setters retain the original DWORD after their ordered comparisons.
    constexpr float GEffectFloatZero = 0.0f;
    constexpr float GEffectFloatOne = 1.0f;

    const char kEffectCpp[] = "E:\\FL\\Scratch\\Source\\Client\\space\\Effect.cpp";
} // namespace

unsigned char FJumpParticleEffect::UpdateVisibility(float /*Dt*/, FSpaceCamera* /*Cam*/)
{
    FJumpParticleEffect* const Self = this;
    // LAB_005033e0 (RET 8): +0x7c |= 1, and that bit is the result. Neither argument is read.
    Self->Flags7C = static_cast<unsigned char>(Self->Flags7C | 1u);
    return static_cast<unsigned char>(Self->Flags7C & 1u);
}

unsigned char FParticleTunnel::UpdateVisibility(float /*Dt*/, FSpaceCamera* Cam)
{
    FParticleTunnel* const Self = this;
    // FUN_005134c0 (RET 8): bit 0 of +0x7c = Cam is one of the in-flight controller's
    // first-person (FUN_0054bc30), third-person (FUN_0054bc40) or turret (FUN_0054bc50)
    // cameras, tested in that order; that bit is the result. Dt is not read.
    unsigned char bCockpitView = 0;
    if (Cam == GetFirstPersonSpaceCamera() || Cam == GetThirdPersonSpaceCamera() ||
        Cam == GetTurretSpaceCamera())
    {
        bCockpitView = 1;
    }
    Self->Flags7C = static_cast<unsigned char>(((Self->Flags7C ^ bCockpitView) & 1u) ^
                                               Self->Flags7C);
    return static_cast<unsigned char>(Self->Flags7C & 1u);
}

unsigned char FParticleTunnel::TickDraw(float Dt, FSpaceCamera* Cam)
{
    FParticleTunnel* const Self = this;
    // LAB_00513510 (RET 8): the world transform (FUN_004f7ef0), its orientation kept and its
    // translation replaced by the camera's position (vt+0x0c, stdcall), copied into the
    // local transform +0x10; then the particle's own tick FUN_004f8a20(Dt, Cam), whose
    // answer is returned.
    FTransform World;
    EffectGetWorldTransform(reinterpret_cast<FEffectBase const*>(Self), &World);
    typedef float const*(__stdcall * FCamGetPosFn)(FSpaceCamera* CameraSelf);
    float const* const CamPos = AsCamera(Cam)->GetPosition();
    FTransform Local;
    Local.Rotation = World.Rotation;
    Local.Translation.X = CamPos[0];
    Local.Translation.Y = CamPos[1];
    Local.Translation.Z = CamPos[2];
    Self->LocalXform = Local;
    return ParticleEffectTickDraw(Self, Dt, Cam);
}

FParticleEffect::FParticleEffect(unsigned TypeId) : FEffectBase(TypeId)
{
    FxEffect = nullptr;
    FxNode = nullptr;
    Flags7C = 0;
    std::memset(Pad7D, 0, sizeof(Pad7D));
    RunTime78 = -1.0f;
    Float80 = 1.0f;
}

void ParticleEffectConstruct(FParticleEffect* This, unsigned TypeId)
{
    ::new (This) FParticleEffect(TypeId);
}

FParticleEffectCruise::FParticleEffectCruise() : FParticleEffect(0)
{
    CruiseScale = 1.0f;
}

float FParticleEffect::GetWorldPos(FVector* Out)
{
    FTransform World;
    EffectGetWorldTransform(this, &World);
    *Out = World.Translation;
    return Radius74;
}


void ParticleEffectCleanupAlchemy(FParticleEffect* This)
{
    // FUN_004f8110: IFxEffect vt+0x34 (7), Release; then IAlchemy vt+0x58 (DetachNode) with
    // the node -- no test of DALib::Alchemy -- and the node's Release.
    if (This->FxEffect != nullptr)
    {
        This->FxEffect->Slot13(reinterpret_cast<void*>(7));
        static_cast<IDAComponent*>(This->FxEffect)->Release();
        This->FxEffect = nullptr;
    }
    if (This->FxNode != nullptr)
    {
        DALib::Alchemy->DetachNode(This->FxNode);
        This->FxNode->Release();
        This->FxNode = nullptr;
    }
}

unsigned char ParticleEffectCreateAlchemyNodes(FParticleEffect* This)
{
    // FUN_004f8360 — listing 004f8360..004f84c7. No null tests on `this`, the DALib globals
    // or the VisEffect: retail reads them straight.
    // 0x004f8366: already created -> 1; not started (bit 4 clear) -> 0.
    if ((This->Flags7C & 4u) != 0)
        return 1;
    if ((This->Flags7C & 0x10u) == 0)
        return 0;

    // 0x004f8382..0x004f83a5: Dacom CreateInstance({8, "FxNode"}) straight into +0x70.
    AGGDESC Desc = {8, "FxNode"};
    GENRESULT const NodeHr = DALib::Dacom->CreateInstance(
        &Desc, reinterpret_cast<void**>(&This->FxNode));
    if (NodeHr != GR_OK)
    {
        // 0x004f83af: WARNING with the VisEffect nickname, bit 4 cleared.
        FDUMP(0x100002,
              "%s(%d) : *** WARNING: ParticleEffect::start_effect: couldnt create ale.tree "
              "for %s",
              "E:\\FL\\Scratch\\Source\\Client\\space\\Effect.cpp", 0x3dc, This->Vis->Nickname);
        This->Flags7C = static_cast<unsigned char>(This->Flags7C & 0xefu);
        return 0;
    }

    // 0x004f83e1: FUN_0042e080 on the VisEffect; its answer is not read. Bit 3 records that
    // this effect holds a reference (released by the destructor, 0x004f7fe5).
    VisEffectArchAddRef(This->Vis);
    This->Flags7C = static_cast<unsigned char>(This->Flags7C | 8u);

    // 0x004f83ea..0x004f8403: FxEffectLibrary vt+0x18 CreateEffect(FxNode, effect_crc,
    // &+0x6c) -- the out pointer is the member itself.
    unsigned long const Key = This->Vis->EffectCrc;
    GENRESULT const Hr = DALib::FxEffectLibrary->CreateEffect(
        This->FxNode, reinterpret_cast<void*>(Key), &This->FxEffect);
    if (Hr != GR_OK)
    {
        if (Hr == static_cast<GENRESULT>(-8))
        {
            // 0x004f840f: no flag change.
            FDUMP(0x100001, "%s(%d) : *** ERROR: could not find alchemy effect (%s) in file (%s)",
                  "E:\\FL\\Scratch\\Source\\Client\\space\\Effect.cpp", 0x3eb, This->Vis->Nickname,
                  This->Vis->AlchemyPath);
            return 0;
        }
        // 0x004f8441: bit 4 cleared.
        FDUMP(0x100001,
              "%s(%d) : *** ERROR: ParticleEffect::start_effect: couldnt create ale.eff "
              "(%s) in file (%s) ... Effect is most likely corrupted",
              "E:\\FL\\Scratch\\Source\\Client\\space\\Effect.cpp", 0x3f0,
              This->Vis->Nickname, This->Vis->AlchemyPath);
        This->Flags7C = static_cast<unsigned char>(This->Flags7C & 0xefu);
        return 0;
    }

    // 0x004f847b..0x004f84b2: Alchemy vt+0x64 GetRootNode, vt+0x50 AttachNode(root, FxNode),
    // root vt+0x8 Release -- no null test on the root.
    void* RootRaw = nullptr;
    DALib::Alchemy->GetRootNode(&RootRaw);
    DALib::Alchemy->AttachNode(RootRaw, This->FxNode);
    reinterpret_cast<IDAComponent*>(RootRaw)->Release();

    // 0x004f84b5..0x004f84c1: bit 2 set, and the answer is that bit.
    This->Flags7C = static_cast<unsigned char>(This->Flags7C | 4u);
    return static_cast<unsigned char>((This->Flags7C >> 2) & 1u);
}

unsigned char FParticleEffect::Start(FEffectArch* EffectArch)
{
    FParticleEffect* const This = this;
    // FUN_004f8160 — listing 004f8160..004f8351 (FUN_005b7e90 is only the stack probe for the
    // 0x2000-byte format buffer). No null test on `this`. Every failure calls the object's own
    // slot +4 (`MOV EAX,[ESI] / CALL [EAX+4]`), not a fixed function.
    This->Flags08 = static_cast<unsigned char>(This->Flags08 | 2u);
    This->Flags7C = static_cast<unsigned char>(This->Flags7C & 0xefu);

    if (EffectArch == nullptr)
    {
        FDUMP(0x100002, "%s(%d) : *** WARNING: %s", kEffectCpp, 0x37c,
              "ParticleEffect::start_effect: archetype invalid (null archetype passed "
              "specified)");
        CallKillSlot(This);
        return 0;
    }

    // 0x004f819c: FUN_005792b0(0x26).
    if (GetGameOption(kGameOptParticles) == 0)
    {
        CallKillSlot(This);
        return 0;
    }
    // 0x004f81c2.
    if (DALib::Alchemy == nullptr)
    {
        CallKillSlot(This);
        return 0;
    }

    // 0x004f81e0: JG -- a positive id is kept, zero or negative takes the arch's.
    if (static_cast<int>(This->EffectTypeId) <= 0)
        This->EffectTypeId = static_cast<unsigned>(EffectArch->EffectType);

    // 0x004f81eb and 0x004f8201: FUN_004fd560 is called twice; the first result only seeds
    // the priority.
    FEffectTypeInfo const* const FirstInfo =
        EffectTypeInfoByIndex(static_cast<int>(This->EffectTypeId));
    if (FirstInfo != nullptr)
        This->Priority0C = FirstInfo->Priority;

    FEffectTypeInfo const* const TypeInfo =
        EffectTypeInfoByIndex(static_cast<int>(This->EffectTypeId));
    if (TypeInfo == nullptr)
    {
        // 0x004f820f: the name comes from the VisEffect already at +0x04 (its first field),
        // not from the arch -- retail formats it with FUN_00402180 into the stack buffer and
        // passes that through "%s".
        FDUMP(0x100002, "%s(%d) : *** WARNING: no type info specified for %s", kEffectCpp,
              0x395, This->Vis->Nickname);
        CallKillSlot(This);
        return 0;
    }

    // 0x004f8259: FCOMP Priority vs DAT_006129c8, TEST AH,5 / JP -> the full effect unless the
    // priority is strictly (ordered) below the perf level; then FCOMP GenericPriority,
    // TEST AH,1 / JNZ -> the full effect when that is below or unordered; then the generic one
    // only if the arch has one (+0x10).
    float const Perf = GetPerfGenericEffectPerf();
    if (!(TypeInfo->Priority < Perf) || !(TypeInfo->GenericPriority >= Perf) ||
        EffectArch->VisGeneric == nullptr)
    {
        This->Vis = EffectArch->VisEffect;                 // +0x08
        This->Priority0C = TypeInfo->Priority;
    }
    else
    {
        This->Vis = EffectArch->VisGeneric;                // +0x10
        This->Priority0C = TypeInfo->GenericPriority;
    }

    if (This->Vis == nullptr)
    {
        FDUMP(0x100002, "%s(%d) : *** WARNING: %s", kEffectCpp, 0x3b0,
              "ParticleEffect::start_effect: archetype invalid (no visarch specified)");
        CallKillSlot(This);
        return 0;
    }

    This->Flags7C = static_cast<unsigned char>(This->Flags7C | 0x10u);
    This->Flags08 = static_cast<unsigned char>(This->Flags08 & 0xfdu);
    This->RunTime78 = TypeInfo->RunTime;                   // +0x18
    This->Radius74 = TypeInfo->Radius;                     // +0x10

    // 0x004f82f4: the object's slot +0x10; when it says yes, FUN_004f7af0 inlined (which is
    // where the one RefCount increment happens).
    if (This->IsAlive() != 0)
        EffectTypeListLink(This);

    return static_cast<unsigned char>((This->Flags7C >> 4) & 1u);
}

unsigned char ParticleEffectStart(FParticleEffect* This, FEffectArch* EffectArch)
{
    return This->FParticleEffect::Start(EffectArch);
}

void FParticleEffect::Kill()
{
    FParticleEffect* const This = this;
    // LAB_004f84d0 -- slot +4 of both particle tables (.rdata 0x005d9ad8, 0x005dd148).
    FEffectBase* const Base = reinterpret_cast<FEffectBase*>(This);

    if (This->FxEffect == nullptr)
    {
        // 0x004f84e6: no running alchemy effect -- link if needed, FUN_004f7db0, dead.
        EffectTypeListLink(This);
        EffectFreezeToWorld(Base);
        This->Flags08 = static_cast<unsigned char>(This->Flags08 | 2u);
        return;
    }

    // 0x004f853a: link if needed.
    EffectTypeListLink(This);

    if (This->EngineIndex != -1)
    {
        if (EffectWatchedObject(Base) == nullptr)
        {
            // 0x004f8592: the watched object is gone -- link (FUN_004fc920 + RefCount++, a
            // no-op now), FUN_004f7db0, dead; then on to the fade-out below.
            EffectTypeListLink(This);
            EffectFreezeToWorld(Base);
            This->Flags08 = static_cast<unsigned char>(This->Flags08 | 2u);
        }
        else
        {
            // 0x004f85c7..0x004f867f: FUN_004f7db0's body inlined (velocity, world transform,
            // index = -1). The effect is NOT marked dead: it keeps drawing while the alchemy
            // effect winds down.
            EffectFreezeToWorld(Base);
        }
    }

    // 0x004f8680: the remaining run time is pinned to 1.0 when it is below 0.0 (TEST AH,5 /
    // JNP: strictly less, ordered) or above 1.0 (TEST AH,0x41 / JNZ skips on <=, = and
    // unordered); a NaN is left alone.
    if (This->RunTime78 < 0.0f || This->RunTime78 > 1.0f)
        This->RunTime78 = 1.0f;

    // 0x004f86a7: IFxEffect vt+0x34 with 1 -- stop emitting, let the live particles finish.
    This->FxEffect->Slot13(reinterpret_cast<void*>(1));
}

void ParticleEffectArmDeath(FParticleEffect* This)
{
    This->FParticleEffect::Kill();
}

void FParticleEffect::SetIntensity(float Intensity)
{
    FParticleEffect* const This = this;
    // LAB_004f8c20 (__thiscall, RET 4). Nothing without the IFxEffect at +0x6c; otherwise the
    // value is clamped to [0, 1] -- below 0.0f (DAT_005c7474, TEST AH,5 / JP) becomes 0, above
    // 1.0f (DAT_005c75dc, TEST AH,0x41 / JNZ) becomes 1 -- and handed to its vt+0x18 as the
    // raw float dword.
    IFxEffect* const Fx = This->FxEffect;
    if (Fx == nullptr)
        return;
    float Value;
    std::memcpy(&Value, &Intensity, sizeof(Value));
    if (Value < 0.0f)
        std::memcpy(&Value, &GEffectFloatZero, sizeof(Value));
    else if (Value > 1.0f)
        std::memcpy(&Value, &GEffectFloatOne, sizeof(Value));
    ForwardAlchemyEffectFloat(Fx, &IFxEffect::Slot6, Value);
}

void ParticleEffectSetIntensity(FParticleEffect* This, float Intensity)
{
    This->FParticleEffect::SetIntensity(Intensity);
}

unsigned char FParticleEffect::UpdateVisibility(float Dt, FSpaceCamera* Cam)
{
    FParticleEffect* const This = this;
    // FUN_004f86c0 (__thiscall, RET 8; EBX = this, EBP = Cam). Dt is not read.
    // 0x004f86c9: FUN_005792b0(0x26).
    if (GetGameOption(kGameOptParticles) == 0)
        return 0;

    FEffectBase* const Base = AsEffectBase(This);
    ICamera* const CamIface = AsCamera(Cam);

    // 0x004f86e7..0x004f870f: a watched gameobj ([+0x44] and that minus 8 both non-null) of
    // ManagerKind 1 or 2.
    FClientGameobj* const Watched = static_cast<FClientGameobj*>(EffectWatchedObject(Base));
    if (Watched != nullptr && (Watched->ManagerKind == 1 || Watched->ManagerKind == 2))
    {
        // 0x004f8715..0x004f8752: IObjInspect::cobject() (vt+0x150); an equipped object
        // (class & 0x103 == 0x103) whose CEqObj +0x140 is at least 1.0 (FCOMP 1.0,
        // TEST AH,1 / JNZ goes on when below or unordered) hides the effect.
        CObject const* const Obj = Watched->cobject();
        if (Obj != nullptr && (static_cast<unsigned>(Obj->ObjectClass) & 0x103u) == 0x103u &&
            static_cast<CEqObj const*>(Obj)->CloakPercent >= 1.0f)
            return 0;

        // 0x004f8755: only with a field controller (DAT_00675244).
        void* const Field = GetSpaceFieldController();
        if (Field != nullptr)
        {
            // 0x004f8762: vt+0x18c with a zeroed out-dword.
            unsigned DrawStateB = 0;
            unsigned const DrawStateA = Watched->Slot18c(&DrawStateB);
            if (DrawStateA == 0)
            {
                // 0x004f877d: outside every field while the camera is fully inside one.
                if (GetSpaceFieldEnclosure() == 2)
                    return 0;
            }
            else
            {
                // 0x004f8798: inside another field than the camera's.
                if (DrawStateA != static_cast<unsigned>(reinterpret_cast<uintptr_t>(Field)))
                    return 0;

                // 0x004f87ae..0x004f8827: the world transform (FUN_004f7ef0 inlined; the
                // no-op FUN_0041c1a0 on the stack local is dropped), then camera slot +0x0c.
                FTransform World;
                EffectGetWorldTransform(Base, &World);
                float const* const CamPos = CamIface->GetPosition();

                // 0x004f8832..0x004f886e: each difference is rounded to a float (FSTP into
                // FUN_00422b80's arguments); 0x004f8873..0x004f888e: (z*z + y*y) + x*x on the
                // FPU against DAT_00675248; TEST AH,0x41 / JNZ goes on when below, equal or
                // unordered, so only strictly beyond the radius hides the effect.
                float const Dx = World.Translation.X - CamPos[0];
                float const Dy = World.Translation.Y - CamPos[1];
                float const Dz = World.Translation.Z - CamPos[2];
                double const DistSq = (static_cast<double>(Dz) * Dz + static_cast<double>(Dy) * Dy) +
                                      static_cast<double>(Dx) * Dx;
                if (DistSq > static_cast<double>(GetSpaceFieldRadiusSq()))
                    return 0;
            }
        }
    }

    // LAB_004f88ab: FCOMP Priority0C vs DAT_006129c8, TEST AH,1 / JNZ -- below the perf level
    // (or unordered) returns the visibility bit untouched.
    if (!(This->Priority0C >= GetPerfGenericEffectPerf()))
        return static_cast<unsigned char>(This->Flags7C & 1u);

    // 0x004f88bf: own slot +8 -> position, radius in ST0 (FSTP float).
    FVector WorldPos;
    float const Radius = This->GetWorldPos(&WorldPos);

    // 0x004f88cf..0x004f88f9: camera slot +0x40 (view transform), I3DMathEngine +0x28.
    FTransform const* const ViewXform =
        reinterpret_cast<FTransform const*>(CamIface->GetViewTransform());
    FVector ViewPos;
    ParticleMath()->TransformXform(ViewPos, *ViewXform, WorldPos);

    // 0x004f88fc..0x004f891e: NegZ = -z (FST float), far edge = NegZ + Radius (FSTP float);
    // FCOMP ZNear vs far edge, TEST AH,5 / JP -- on only when ZNear is strictly below it.
    float const NegZ = -ViewPos.Z;
    float const FarEdge = NegZ + Radius;
    if (!(CamIface->GetZNear() < FarEdge))
        return static_cast<unsigned char>(This->Flags7C & 1u);

    // 0x004f8924..0x004f895a: FUN_004fd560 (no null test here). Near distance NegZ - Radius
    // against pbubble near + (far - near) * (1.0 (double DAT_005c89b8) - perf), all on the FPU;
    // FCOMPP + TEST AH,0x41 / JNZ goes on only when the threshold is strictly greater.
    FEffectTypeInfo const* const TypeInfo =
        EffectTypeInfoByIndex(static_cast<int>(This->EffectTypeId));
    double const NearDistance = static_cast<double>(NegZ) - Radius;
    double const Threshold =
        (static_cast<double>(TypeInfo->PbubbleFar) - TypeInfo->PbubbleNear) *
            (1.0 - static_cast<double>(GetPerfGenericEffectPerf())) +
        TypeInfo->PbubbleNear;
    if (!(Threshold > NearDistance))
        return static_cast<unsigned char>(This->Flags7C & 1u);

    // 0x004f8960..0x004f899f: ALCHEMY_LOD (option 0x40) -> FUN_0042dcd0 fills +0x80; a false
    // return zeroes +0x80 and leaves the visibility bit as it is.
    if (GetGameOption(kGameOptAlchemyLod) != 0)
    {
        if (!EffectTypeLodBlend(static_cast<int>(This->EffectTypeId), CamIface, NegZ,
                                &This->Float80))
        {
            This->Float80 = 0.0f;
            return static_cast<unsigned char>(This->Flags7C & 1u);
        }
    }

    // 0x004f89a2..0x004f89fd: the two side planes (camera +0x80/+0x88, +0x90/+0x94) against
    // the radius; TEST AH,0x41 / JP -- inside only when the distance is below or equal.
    double const DistX = std::fabs(static_cast<double>(ViewPos.X)) * Cam->PlaneX[0] +
                         static_cast<double>(ViewPos.Z) * Cam->PlaneX[2];
    unsigned char Inside = 0;
    if (DistX <= Radius)
    {
        double const DistY = std::fabs(static_cast<double>(ViewPos.Y)) * Cam->PlaneY[1] +
                             static_cast<double>(ViewPos.Z) * Cam->PlaneY[2];
        if (DistY <= Radius)
            Inside = 1;
    }
    This->Flags7C = static_cast<unsigned char>((This->Flags7C & 0xfeu) | Inside);
    return static_cast<unsigned char>(This->Flags7C & 1u);
}

unsigned char ParticleEffectUpdateVisibility(FParticleEffect* This, float Dt, FSpaceCamera* Cam)
{
    return This->FParticleEffect::UpdateVisibility(Dt, Cam);
}

unsigned char FParticleEffect::TickDraw(float Dt, FSpaceCamera* Cam)
{
    FParticleEffect* const This = this;
    // FUN_004f8a20 (__thiscall, RET 8; EBP = this). Cam is not read.
    // 0x004f8a24: FUN_005792b0(0x26).
    if (GetGameOption(kGameOptParticles) == 0)
        return 0;

    // 0x004f8a3d: FUN_0042d740.
    if (ShouldSkipLocalServerTick())
        return 1;

    // 0x004f8a4f: CL = +0x7c, read once. Without the start bit (0x10) the effect is finished.
    unsigned char const Flags = This->Flags7C;
    bool bFinished = true;
    if ((Flags & 0x10u) != 0)
    {
        bFinished = false;
        // 0x004f8a57: a run time above zero (TEST AH,0x41 / JNZ skips <= 0 and NaN) counts
        // down by Dt; the FPU result (FST float kept on the stack) at or below zero
        // (TEST AH,0x41 / JNP) finishes the effect.
        if (This->RunTime78 > 0.0f)
        {
            double const Left = static_cast<double>(This->RunTime78) - Dt;
            This->RunTime78 = static_cast<float>(Left);
            if (Left <= 0.0)
                bFinished = true;
        }
        // LAB_004f8a7e: with the ale bit (4), IFxEffect slot +0x2c(4) returning a non-zero
        // byte also finishes it.
        if (!bFinished && (Flags & 4u) != 0 &&
            static_cast<unsigned char>(This->FxEffect->Slot11(reinterpret_cast<void*>(4))) != 0)
            bFinished = true;
    }
    if (bFinished)
    {
        // LAB_004f8a92: FUN_004f7af0 (link), FUN_004f7db0 (ECX still this), dead.
        EffectTypeListLink(This);
        EffectFreezeToWorld(AsEffectBase(This));
        This->Flags08 = static_cast<unsigned char>(This->Flags08 | 2u);
        return 0;
    }

    // LAB_004f8aab: FUN_004fd560 and retail's own null test.
    FEffectTypeInfo const* const TypeInfo =
        EffectTypeInfoByIndex(static_cast<int>(This->EffectTypeId));
    if (TypeInfo == nullptr)
    {
        FDUMP(0x100002, "%s(%d) : *** WARNING: %s", kEffectCpp, 0x4c8,
              "effect shouldnt have gotten created w/o type info");
        return 0;
    }

    // 0x004f8ae9: neither UPDATE_OFFSCREEN (+0x15) nor visible (+0x7c bit 0, re-read) -> 0.
    if (TypeInfo->UpdateOffscreen == 0 && (This->Flags7C & 1u) == 0)
        return 0;

    // 0x004f8b04: FUN_004f8360; on failure the object's own slot +4 (kill), then 0.
    if (ParticleEffectCreateAlchemyNodes(This) == 0)
    {
        CallKillSlot(This);
        return 0;
    }

    // 0x004f8b23: with an IFxEffect -- the world transform (FUN_004f7ef0 inlined, copied to a
    // stack local), own slot +0x24 with +0x80 (the LOD blend), then the effect's +0x0c(0,
    // &xform), +0x1c(0) and +0x20(Dt).
    if (This->FxEffect != nullptr)
    {
        FTransform World;
        EffectGetWorldTransform(AsEffectBase(This), &World);
        This->SetLod(This->Float80);
        IFxEffect* const Fx = This->FxEffect;
        Fx->Slot3(nullptr, &World);
        Fx->Slot7(nullptr);
        Fx->Slot8(FloatArg(Dt));
    }
    return 1;
}

unsigned char ParticleEffectTickDraw(FParticleEffect* This, float Dt, FSpaceCamera* Cam)
{
    return This->FParticleEffect::TickDraw(Dt, Cam);
}

unsigned char FParticleEffect::BuildGeometry(FSpaceCamera* Cam)
{
    FParticleEffect* const This = this;
    // FUN_004f8be0 (__thiscall, RET 4): the visibility bit is cleared first, then
    // FUN_005792b0(0x26); with an IFxEffect its slot +0x24(Cam). Returns 1 past the option.
    This->Flags7C = static_cast<unsigned char>(This->Flags7C & 0xfeu);
    if (GetGameOption(kGameOptParticles) == 0)
        return 0;
    if (This->FxEffect != nullptr)
        This->FxEffect->Slot9(Cam);
    return 1;
}

unsigned char ParticleEffectBuildGeometry(FParticleEffect* This, FSpaceCamera* Cam)
{
    return This->FParticleEffect::BuildGeometry(Cam);
}

void FParticleEffect::SetLod(float Lod)
{
    FParticleEffect* const This = this;
    // LAB_004f8c80 (__thiscall, RET 4): no IFxEffect -> the warning, nothing else. Otherwise
    // the same [0, 1] clamp as LAB_004f8c20 (TEST AH,5 / JP for < 0, TEST AH,0x41 / JNZ for
    // > 1; NaN passes through) into the effect's slot +0x38.
    IFxEffect* const Fx = This->FxEffect;
    if (Fx == nullptr)
    {
        FDUMP(0x100002, "%s(%d) : *** WARNING: %s", kEffectCpp, 0x511,
              "Trying to set LOD on effect that does not exist - ADO");
        return;
    }
    float Value;
    std::memcpy(&Value, &Lod, sizeof(Value));
    if (Value < 0.0f)
        std::memcpy(&Value, &GEffectFloatZero, sizeof(Value));
    else if (Value > 1.0f)
        std::memcpy(&Value, &GEffectFloatOne, sizeof(Value));
    ForwardAlchemyEffectFloat(Fx, &IFxEffect::Slot14, Value);
}

void ParticleEffectSetLod(FParticleEffect* This, float Lod)
{
    This->FParticleEffect::SetLod(Lod);
}

float FParticleEffectCruise::GetWorldPos(FVector* Out)
{
    FParticleEffectCruise* const This = this;
    // FUN_004f91c0 (__thiscall, RET 4). Radius = +0x84 * +0x74 (FST float); the FPU product
    // strictly below 10.0 (DAT_005d49b0, TEST AH,5 / JP) becomes 10.0f.
    double const Product = static_cast<double>(This->CruiseScale) * This->Radius74;
    float Radius = static_cast<float>(Product);
    if (Product < 10.0)
        Radius = 10.0f;

    // 0x004f91e9..0x004f9263: FUN_004f7ef0 inlined; *Out = the translation.
    FTransform World;
    EffectGetWorldTransform(AsEffectBase(This), &World);
    *Out = World.Translation;
    return Radius;
}

float ParticleEffectCruiseGetWorldPos(FParticleEffectCruise* This, FVector* Out)
{
    return This->FParticleEffectCruise::GetWorldPos(Out);
}

void FParticleEffectCruise::SetIntensity(float Scale)
{
    FParticleEffectCruise* const This = this;
    // FUN_004f92f0 (__thiscall, RET 4): +0x84 = the raw argument; with an IFxEffect, the
    // LAB_004f8c20 clamp into its slot +0x18.
    std::memcpy(&This->CruiseScale, &Scale, sizeof(Scale));
    IFxEffect* const Fx = This->FxEffect;
    if (Fx == nullptr)
        return;
    float Value;
    std::memcpy(&Value, &Scale, sizeof(Value));
    if (Value < 0.0f)
        std::memcpy(&Value, &GEffectFloatZero, sizeof(Value));
    else if (Value > 1.0f)
        std::memcpy(&Value, &GEffectFloatOne, sizeof(Value));
    ForwardAlchemyEffectFloat(Fx, &IFxEffect::Slot6, Value);
}

void ParticleEffectCruiseSetScale(FParticleEffectCruise* This, float Scale)
{
    This->FParticleEffectCruise::SetIntensity(Scale);
}

unsigned char FParticleEffectCruise::UpdateVisibility(float Dt, FSpaceCamera* Cam)
{
    FParticleEffectCruise* const This = this;
    // FUN_004f9270 (__thiscall, RET 8): +0x80 saved first, then a direct call of FUN_004f86c0;
    // its low bit replaces bit 0 of +0x7c. A hidden effect whose saved +0x80 was above zero
    // (TEST AH,0x41 / JNZ) stays visible while +0x80 fades: saved - Dt * 0.5 (DAT_005c75e0),
    // strictly negative (TEST AH,5 / JP) clamped to 0.
    float const SavedLod = This->Float80;
    unsigned char const Visible = ParticleEffectUpdateVisibility(This, Dt, Cam);
    This->Flags7C = static_cast<unsigned char>((This->Flags7C & 0xfeu) | (Visible & 1u));
    if ((This->Flags7C & 1u) == 0 && SavedLod > 0.0f)
    {
        double Faded = static_cast<double>(SavedLod) - static_cast<double>(Dt) * 0.5;
        if (Faded < 0.0)
            Faded = 0.0;
        This->Float80 = static_cast<float>(Faded);
        This->Flags7C = static_cast<unsigned char>(This->Flags7C | 1u);
    }
    return static_cast<unsigned char>(This->Flags7C & 1u);
}

unsigned char ParticleEffectCruiseUpdateVisibility(FParticleEffectCruise* This, float Dt, FSpaceCamera* Cam)
{
    return This->FParticleEffectCruise::UpdateVisibility(Dt, Cam);
}

FParticleEffect::~FParticleEffect()
{
    if ((Flags7C & 4u) != 0)
        ParticleEffectCleanupAlchemy(this);
    if ((Flags7C & 8u) != 0 && Vis != nullptr)
        VisEffectArchRelease(Vis);
}

void ParticleEffectDestruct(FParticleEffect* This)
{
    This->FParticleEffect::~FParticleEffect();
}


void ParticleEffectDestroy(FParticleEffect* This, int FreeMemory)
{
    if ((FreeMemory & 1) != 0)
        delete This;
    else
        This->FParticleEffect::~FParticleEffect();
}


void ParticleEffectCruiseDestroy(FParticleEffect* This, int FreeMemory)
{
    if ((FreeMemory & 1) != 0)
        delete This;
    else
        ((FParticleEffectCruise*)This)->FParticleEffectCruise::~FParticleEffectCruise();
}


void ParticleEffectAddRef(FParticleEffect* This)
{
    // FUN_004f7a80.
    This->RefCount = This->RefCount + 1;
}

int ParticleEffectRelease(FParticleEffect* This)
{
    return EffectRelease(This);
}


void ParticleEffectAttach(FParticleEffect* This, void* WatchableOwner, int EngineIndex,
                          FTransform const* LocalXform)
{
    // FUN_004f7d20, the shared Effect attach.
    This->Attach(WatchableOwner, EngineIndex, LocalXform);
}

FParticleEffect* ParticleEffectCreate()
{
    // LAB_004f6e70: FUN_004f79a0(0x84) -- the Heap pool -- then FUN_004f8060(0) when it
    // returned memory.
    void* const Mem = EffectAlloc(0x84u);
    if (Mem == nullptr)
        return nullptr;
    FParticleEffect* const Effect = static_cast<FParticleEffect*>(Mem);
    ParticleEffectConstruct(Effect, 0);
    return Effect;
}

FParticleEffect* ParticleEffectCreateCruise()
{
    // FUN_00528980, 0x00528bd1..0x00528bf7: FUN_004f79a0(0x90) (malloc), FUN_004f8060(0),
    // +0x84 = 1.0f, vptr PTR_FUN_005dd144.
    void* const Mem = EffectAlloc(0x90u);
    if (Mem == nullptr)
        return nullptr;
    FParticleEffectCruise* Effect = static_cast<FParticleEffectCruise*>(Mem);
    ::new (Effect) FParticleEffectCruise();
    return Effect;
}

FParticleEffect* ParticleEffectCreateJump()
{
    // LAB_005033a0: FUN_004f79a0(0x84) (the Heap pool); null returns null. 0x005033c0..
    // 0x005033ca: FUN_004f8060(0x25), then the vptr PTR_FUN_005daac4.
    void* const Mem = EffectAlloc(0x84u);
    if (Mem == nullptr)
        return nullptr;
    FParticleEffect* const Effect = static_cast<FParticleEffect*>(Mem);
    ::new (Effect) FJumpParticleEffect();
    return Effect;
}

FParticleEffect* ParticleTunnelCreate()
{
    // LAB_00513440: FUN_004f79a0(0x84) (the Heap pool); null returns null, else JMP LAB_005134a0:
    // FUN_004f8060(0), then the vptr PTR_FUN_005db770 (ParticleTunnel).
    void* const Mem = EffectAlloc(0x84u);
    if (Mem == nullptr)
        return nullptr;
    FParticleEffect* const Effect = static_cast<FParticleEffect*>(Mem);
    ::new (Effect) FParticleTunnel();
    return Effect;
}

namespace
{
    // .rdata 0x005d9a74 (0.4f): the fade duration both halves pass, and the timer's seed twice.
    constexpr float kCameraParticlesFadeTime = 0.4f;
    // .rdata 0x005d9a7c / 0x005d9a78: the near and far distances of Start's fade factor.
    constexpr float kCameraParticlesNearDistance = 2000.0f;
    constexpr float kCameraParticlesFarDistance = 3000.0f;
    // The colour both fades are given: three 0x3f800000 dwords built on the stack.
    float const kCameraParticlesWhite[3] = {1.0f, 1.0f, 1.0f};


} // namespace

unsigned char FCameraParticles::Start(FEffectArch* Arch)
{
    FCameraParticles* const Self = this;
    // LAB_004f8e10 (RET 4). 0x004f8e16..0x004f8e84: the world transform -- FUN_004f7ef0's
    // body inlined (instance x local through DAT_00674f50 when watched and attached, else
    // the local transform).
    FTransform World;
    EffectGetWorldTransform(Self, &World);

    // 0x004f8e86: FUN_0040f130, stored at +0x74 before the test.
    void* const Camera = GetClientSoundListenerObject();
    Self->Camera = Camera;
    if (Camera != nullptr)
    {
        // 0x004f8e96: the camera's vt+0x0c (stdcall, `this` pushed) -- its position.
        typedef float const*(__stdcall * FCamGetPosFn)(void* CameraSelf);
        float const* const CamPos =
            ((ICamera*)Camera)->GetPosition();
        float const Dx = CamPos[0] - World.Translation.X;
        float const Dy = CamPos[1] - World.Translation.Y;
        float const Dz = CamPos[2] - World.Translation.Z;
        // 0x004f8eb0..0x004f8ec0: (Dz*Dz + Dx*Dx) + Dy*Dy, then FSQRT.
        float const Distance = std::sqrt(Dz * Dz + Dx * Dx + Dy * Dy);
        float const Factor = (Distance - kCameraParticlesNearDistance) /
                             (kCameraParticlesFarDistance - kCameraParticlesNearDistance);

        // 0x004f8ee0: FCOMP 0.0f, TEST AH,0x41 / JP -> 0: the byte is (Factor <= 0.0f), a
        // NaN giving 0.
        Self->bStartOpaque = static_cast<unsigned char>((Factor <= 0.0f) ? 1 : 0);
        if (Self->bStartOpaque == 0)
        {
            // 0x004f8f01: FCOMP 1.0f, TEST AH,0x41 / JNZ multiplies; the fall-through
            // (strictly greater) returns 0 before anything is linked or faded.
            if (Factor > 1.0f)
                return 0;
            Self->Timer = Factor * Self->Timer;
        }
    }

    // 0x004f8f23..0x004f8f66: FUN_004f7af0 inlined (link unless linked or dead, +0x58 += 1).
    EffectTypeListLink(Self);

    // 0x004f8f69..0x004f8f8c: FUN_004122a0(white, 0.4f).
    FadeOutColoured(kCameraParticlesWhite, kCameraParticlesFadeTime);

    // 0x004f8f91..0x004f8fc3: LAB_004f7ab0's body inlined (a type id at or below zero taken
    // from the arch, the row's priority into +0x0c); returns 1.
    return EffectBaseStart(Self, Arch);
}

float FCameraParticles::GetWorldPos(FVector* Out)
{
    FCameraParticles* const Self = this;
    // LAB_004f8df0 (RET 4): three zero dwords into *Out, 100.0f (.rdata 0x005ca230) on ST0.
    Out->X = 0.0f;
    Out->Y = 0.0f;
    Out->Z = 0.0f;
    return 100.0f;
}

unsigned char FCameraParticles::TickDraw(float Dt, FSpaceCamera* Cam)
{
    FCameraParticles* const Self = this;
    // LAB_004f8fd0 (RET 8). 0x004f8fd6..0x004f8fee: a Start camera that is not the one
    // drawing re-seeds the timer with 0.4f + 0.4f.
    if (Self->Camera != nullptr && Self->Camera != static_cast<void*>(Cam))
        Self->Timer = kCameraParticlesFadeTime + kCameraParticlesFadeTime;

    // 0x004f8ff1: FUN_00412010, kept in the Dt argument slot; every return is its negation.
    // An opaque fade goes straight to the fade-in and the kill.
    if (!IsFadeOpaque())
    {
        // 0x004f8ffe: FCOMP 0.0f, TEST AH,0x41 / JNZ -> return: a timer that is not above
        // zero (or NaN) keeps the effect alive and does nothing.
        if (!(Self->Timer > 0.0f))
            return 1;
        // 0x004f9012: Timer -= Dt; FCOMP 0.0f, TEST AH,0x41 / JP -> 0x004f9097 (return 1)
        // unless the new timer is at or below zero; NaN also returns 1.
        Self->Timer = Self->Timer - Dt;
        if (!(Self->Timer <= 0.0f))
            return 1;
        // 0x004f9029: the saved byte becomes 1.
    }

    // 0x004f902e..0x004f9058: FUN_004121d0(white, 0.4f, +0x70).
    FadeInColoured(kCameraParticlesWhite, kCameraParticlesFadeTime, Self->bStartOpaque != 0);

    // 0x004f905d..0x004f919a: FUN_004f7bd0's body inlined -- link unless linked or dead; with
    // an engine index, an empty watcher calls FUN_004f7bd0 itself and a watched one is baked
    // to the world (velocity +0x4c, transform +0x10, index -1); then +0x08 |= 2.
    EffectKill(Self);
    // 0x004f919e: SETZ on the saved byte, which is 1 on this path.
    return 0;
}

FCameraParticles::FCameraParticles() : FEffectBase(0x2cu)
{
    Camera = nullptr;
    bStartOpaque = 1;
    Timer = kCameraParticlesFadeTime + kCameraParticlesFadeTime;
}

void CameraParticlesConstruct(FCameraParticles* This)
{
    ::new (This) FCameraParticles();
}


FEffectBase* CameraParticlesCreate()
{
    // LAB_004f6e40: FUN_004f79a0(0x78) (malloc); null returns null, else JMP FUN_004f8d00.
    void* const Mem = EffectAlloc(sizeof(FCameraParticles));
    if (Mem == nullptr)
        return nullptr;
    FCameraParticles* const Effect = static_cast<FCameraParticles*>(Mem);
    CameraParticlesConstruct(Effect);
    return Effect;
}

namespace
{
    FGameLight* CreateEffectGameLight()
    {
        // LAB_00513480 (slot 2 of every factory): FUN_00537a90(0x12c), then FUN_005374e0 on the
        // memory when there is some.
        void* const Mem = GameLightHeapAlloc(0x12c);
        if (Mem == nullptr)
            return nullptr;
        return GameLightConstruct(static_cast<FGameLight*>(Mem));
    }
}

FEffectBase* FStandardCliEffectFactory::CreateParticle() const { return ParticleEffectCreate(); }
ISoundSource* FStandardCliEffectFactory::CreateSound() const { return (ISoundSource*)CreateClientSoundSource3D(); }
FGameLight* FStandardCliEffectFactory::CreateLight() const { return CreateEffectGameLight(); }
FEffectBase* FJumpCliEffectFactory::CreateParticle() const { return ParticleEffectCreateJump(); }
FEffectBase* FCameraCliEffectFactory::CreateParticle() const { return CameraParticlesCreate(); }
ISoundSource* FCameraCliEffectFactory::CreateSound() const { return (ISoundSource*)CreateClientSoundSource(); }
FEffectBase* FTunnelCliEffectFactory::CreateParticle() const { return ParticleTunnelCreate(); }

ISoundSource* FJumpCliEffectFactory::CreateSound() const { return (ISoundSource*)CreateClientSoundSource3D(); }
FGameLight* FJumpCliEffectFactory::CreateLight() const { return CreateEffectGameLight(); }
FGameLight* FCameraCliEffectFactory::CreateLight() const { return CreateEffectGameLight(); }
ISoundSource* FTunnelCliEffectFactory::CreateSound() const { return (ISoundSource*)CreateClientSoundSource(); }
FGameLight* FTunnelCliEffectFactory::CreateLight() const { return CreateEffectGameLight(); }

FStandardCliEffectFactory const G_CliEffectFactoryC60;
FJumpCliEffectFactory const G_CliEffectFactoryJump;
FCameraCliEffectFactory const G_CliEffectFactoryCameraParticles;
FTunnelCliEffectFactory const G_CliEffectFactoryTunnel;

unsigned char CreateEffectCompanionAlive(void* CompanionLocal)
{
    // FUN_00503330 — this = &{arch, sound, particle, light}. No test of `this` (0x00503333
    // reads +8 at once); every caller passes an embedded companion's address.
    unsigned char* Base = static_cast<unsigned char*>(CompanionLocal);
    FEffectBase* Particle = *reinterpret_cast<FEffectBase**>(Base + 8);
    if (Particle != nullptr && (Particle->Flags08 & 2u) == 0)
        return 1;
    ISoundSource* Sound = *reinterpret_cast<ISoundSource**>(Base + 4);
    if (Sound != nullptr && Sound->IsActive() != 0)
        return 1;
    void* Light = *reinterpret_cast<void**>(Base + 0xc);
    if (Light != nullptr)
    {
        // Light+0xd4 == 0 → alive. Gamelit layout not owned here.
        int const FieldD4 = *reinterpret_cast<int*>(static_cast<unsigned char*>(Light) + 0xd4);
        if (FieldD4 == 0)
            return 1;
    }
    return 0;
}

unsigned char CliCreateEffectOneShot(FEffectArch* EffectArch, void* ListOwner,
                                     int EngineIndex, FTransform const* LocalXform,
                                     void* CompanionOut)
{
    return CliCreateEffectOneShotOn(&G_CliEffectFactoryC60, EffectArch, ListOwner, EngineIndex,
                                    LocalXform, CompanionOut);
}

unsigned char CliCreateEffectOneShotOn(FCliEffectFactory const* Factory, FEffectArch* EffectArch,
                                       void* ListOwner, int EngineIndex,
                                       FTransform const* LocalXform, void* CompanionOut)
{
    // FUN_00503070 — listing 00503070..0050332b (thiscall on the factory).
    if (GetGameOption(kGameOptEffectsGroup) == 0 || EffectArch == nullptr)
        return 0;

    FEffectBase* Particle = nullptr;
    ISoundSource* Sound = nullptr;
    FGameLight* Light = nullptr;

    // 0x005030e0 / 0x005030ef / 0x0050310a: the factory's slots 0, 1 and 2 (`CALL [EDX]`,
    // `[EAX+4]`, `[EDX+8]`).
    if (GetGameOption(kGameOptParticles) != 0 && EffectArch->VisEffect != nullptr &&
        EffectArch->VisEffect->Nickname[0] != '\0')
    {
        Particle = Factory->CreateParticle();
    }
    if (EffectArch->SndEffect != nullptr)
        Sound = static_cast<ISoundSource*>(Factory->CreateSound());
    if (EffectArch->LgtRadius > 0.0f)
        Light = Factory->CreateLight();

    // Retail Attach(ListOwner, eng, xform) — ListOwner is CliProjectile / Gameobj.
    if (Particle != nullptr)
        EffectAttach(Particle, ListOwner, EngineIndex, LocalXform);
    if (Sound != nullptr && LocalXform != nullptr)
        Sound->SetPose(EngineIndex, reinterpret_cast<float const*>(LocalXform));
    if (Light != nullptr && LocalXform != nullptr)
        CliExplosionAttachLocalXform(Light, EngineIndex, LocalXform);

    // 0x0050315b: the particle's vtable slot 0 (`CALL [EAX]`); a zero result releases it.
    if (Particle != nullptr)
    {
        if (Particle->Start(EffectArch) == 0)
        {
            EffectRelease(Particle);
            Particle = nullptr;
        }
    }
    if (Sound != nullptr)
    {
        Sound->SetLooping(static_cast<unsigned char>(EffectArch->Flags50 & 1u));
        if (EffectArch->SndEffect == nullptr ||
            Sound->Start(EffectArch->SndEffect, 0) == 0)
        {
            Sound->Release();
            Sound = nullptr;
        }
    }
    if (Light != nullptr)
    {
        // FUN_004110c0(1,1) + SetColorBytesAlt(arch+0x4c) + SetRadius(arch+0x48) + vt+4 Enable.
        EngineLightSetType((FEngineLight*)Light, 1, true);
        unsigned char const Rgb[3] = {EffectArch->LgtColorR, EffectArch->LgtColorG,
                                      EffectArch->LgtColorB};
        EngineLightSetColorBytesAlt((FEngineLight*)Light, Rgb);
        EngineLightSetRadius((FEngineLight*)Light, EffectArch->LgtRadius);
        if (!GameLightEnable((FEngineLight*)Light))
        {
            GameLightRelease(Light);
            Light = nullptr;
        }
        else if (EffectArch->LgtEffectLen != 0)
        {
            Light::AnimationArch* Anim = GameLightFindAnimByName(EffectArch->LgtEffect);
            if (Anim != nullptr)
                GameLightAttachAnim(Light, Anim, EffectArch->LgtRangeScale,
                                    EffectArch->LgtTimeScale, 0.0f);
        }
    }

    unsigned char LocalCompanion[0x10];
    std::memset(LocalCompanion, 0, sizeof(LocalCompanion));
    *reinterpret_cast<FEffectArch**>(LocalCompanion + 0) = EffectArch;
    *reinterpret_cast<ISoundSource**>(LocalCompanion + 4) = Sound;
    *reinterpret_cast<FEffectBase**>(LocalCompanion + 8) = Particle;
    *reinterpret_cast<FGameLight**>(LocalCompanion + 0xc) = Light;

    if (CreateEffectCompanionAlive(LocalCompanion) == 0)
    {
        if (Particle != nullptr)
            EffectRelease(Particle);
        if (Sound != nullptr)
            Sound->Release();
        if (Light != nullptr)
            GameLightRelease(Light);
        return 0;
    }

    if (CompanionOut != nullptr)
    {
        // FUN_00502d90 clear + store arch/particle/sound/light with AddRef.
        unsigned char* Out = static_cast<unsigned char*>(CompanionOut);
        std::memset(Out, 0, 0x10);
        *reinterpret_cast<FEffectArch**>(Out + 0) = EffectArch;
        *reinterpret_cast<ISoundSource**>(Out + 4) = Sound;
        *reinterpret_cast<FEffectBase**>(Out + 8) = Particle;
        *reinterpret_cast<FGameLight**>(Out + 0xc) = Light;
        if (Particle != nullptr)
            EffectAddRef(Particle);
        if (Sound != nullptr)
            Sound->AddRef();
        if (Light != nullptr)
            GameLightAddRef(Light);
    }

    // Listing 005032c7..005032d7: if ListOwner != 0 → FUN_004fcea0(ListOwner+0x28, &local).
    if (ListOwner != nullptr)
        CliEffectListInsert(static_cast<unsigned char*>(ListOwner) + 0x28, LocalCompanion);

    if (Particle != nullptr)
        EffectRelease(Particle);
    if (Sound != nullptr)
        Sound->Release();
    if (Light != nullptr)
        GameLightRelease(Light);
    return 1;
}

unsigned char CliCreateEffectWorldOneShot(FEffectArch* EffectArch, FTransform const* LocalXform,
                                          float const* Pos3, void* CompanionOut)
{
    return CliCreateEffectWorldOneShotOn(&G_CliEffectFactoryC60, EffectArch, LocalXform, Pos3,
                                         CompanionOut);
}

unsigned char CliCreateEffectWorldOneShotOn(FCliEffectFactory const* Factory,
                                            FEffectArch* EffectArch, FTransform const* LocalXform,
                                            float const* Pos3, void* CompanionOut)
{
    // FUN_00503f40 — listing 00503f40..005041ad. 0x00503f5c..0x00503f7a: the effects option
    // (FUN_005792b0(0x25)) and a null EffectArch return 0; neither pointer argument is tested.
    if (GetGameOption(kGameOptEffectsGroup) == 0 || EffectArch == nullptr)
        return 0;

    FEffectBase* Particle = nullptr;
    ISoundSource* Sound = nullptr;
    FGameLight* Light = nullptr;

    // 0x00503f94..0x00503ff3: the factory's slot 0 (particles option and a named VisEffect),
    // slot 1 (a SndEffect) and slot 2 (LgtRadius above 0.0f, TEST AH,0x41 / JNZ skips).
    if (GetGameOption(kGameOptParticles) != 0 && EffectArch->VisEffect != nullptr &&
        EffectArch->VisEffect->Nickname[0] != '\0')
    {
        Particle = Factory->CreateParticle();
    }
    if (EffectArch->SndEffect != nullptr)
        Sound = static_cast<ISoundSource*>(Factory->CreateSound());
    if (EffectArch->LgtRadius > 0.0f)
        Light = Factory->CreateLight();

    // Particle: memcpy 0xc dwords of LocalXform into +0x10 (listing 00503fff..00504009).
    if (Particle != nullptr)
        std::memcpy(&Particle->LocalXform, LocalXform, sizeof(FTransform));

    // Sound: 0x0050401f vt+0x8c (slot 35) with xform+0x24 -- the world position (Source3D
    // LAB_00429ca0; a plain Source from DAT_00674c5c's slot folds it onto a RET 4) -- and
    // 0x0050407f vt+0x90 (slot 36, SetVelocity) with Pos3.
    if (Sound != nullptr)
    {
        Sound->Slot35(const_cast<float*>(&LocalXform->Translation.X));
        Sound->SetVelocity(Pos3);
    }

    // Light: EngIndex==-1 → FUN_00410fe0(xform); else memcpy into +0xf0 (00504029..00504052).
    if (Light != nullptr)
    {
        if (static_cast<int>(Light->EngineIndex) == -1)
        {
            EngineLightSetTransform((FEngineLight*)Light, &LocalXform->Rotation.D[0][0],
                                    &LocalXform->Translation.X);
        }
        else
        {
            std::memcpy(Light->LocalXform, LocalXform, sizeof(FTransform));
        }
        GameLightSetFreeMotionOffset(Light, Pos3);
    }

    // Pos3 → particle Pad4C / +0x4c (listing 0050405b..0050406f).
    if (Particle != nullptr)
    {
        float* const Pad = reinterpret_cast<float*>(Particle->Pad4C);
        Pad[0] = Pos3[0];
        Pad[1] = Pos3[1];
        Pad[2] = Pos3[2];
    }

    // 0x00504099..0x005040ad: the particle's own vtable slot 0 (`CALL [EDX]`, the factory's
    // class decides -- CameraParticles for DAT_00674c5c); a zero result releases it.
    if (Particle != nullptr)
    {
        if (Particle->Start(EffectArch) == 0)
        {
            EffectRelease(Particle);
            Particle = nullptr;
        }
    }
    if (Sound != nullptr)
    {
        Sound->SetLooping(static_cast<unsigned char>(EffectArch->Flags50 & 1u));
        if (EffectArch->SndEffect == nullptr ||
            Sound->Start(EffectArch->SndEffect, 0) == 0)
        {
            Sound->Release();
            Sound = nullptr;
        }
    }
    if (Light != nullptr)
    {
        EngineLightSetType((FEngineLight*)Light, 1, true);
        unsigned char const Rgb[3] = {EffectArch->LgtColorR, EffectArch->LgtColorG,
                                      EffectArch->LgtColorB};
        EngineLightSetColorBytesAlt((FEngineLight*)Light, Rgb);
        EngineLightSetRadius((FEngineLight*)Light, EffectArch->LgtRadius);
        if (!GameLightEnable((FEngineLight*)Light))
        {
            GameLightRelease(Light);
            Light = nullptr;
        }
        else if (EffectArch->LgtEffectLen != 0)
        {
            Light::AnimationArch* Anim = GameLightFindAnimByName(EffectArch->LgtEffect);
            if (Anim != nullptr)
                GameLightAttachAnim(Light, Anim, EffectArch->LgtRangeScale,
                                    EffectArch->LgtTimeScale, 0.0f);
        }
    }

    unsigned char LocalCompanion[0x10];
    std::memset(LocalCompanion, 0, sizeof(LocalCompanion));
    *reinterpret_cast<FEffectArch**>(LocalCompanion + 0) = EffectArch;
    *reinterpret_cast<ISoundSource**>(LocalCompanion + 4) = Sound;
    *reinterpret_cast<FEffectBase**>(LocalCompanion + 8) = Particle;
    *reinterpret_cast<FGameLight**>(LocalCompanion + 0xc) = Light;

    if (CreateEffectCompanionAlive(LocalCompanion) == 0)
    {
        if (Particle != nullptr)
            EffectRelease(Particle);
        if (Sound != nullptr)
            Sound->Release();
        if (Light != nullptr)
            GameLightRelease(Light);
        return 0;
    }

    if (CompanionOut != nullptr)
    {
        unsigned char* Out = static_cast<unsigned char*>(CompanionOut);
        std::memset(Out, 0, 0x10);
        *reinterpret_cast<FEffectArch**>(Out + 0) = EffectArch;
        *reinterpret_cast<ISoundSource**>(Out + 4) = Sound;
        *reinterpret_cast<FEffectBase**>(Out + 8) = Particle;
        *reinterpret_cast<FGameLight**>(Out + 0xc) = Light;
        if (Particle != nullptr)
            EffectAddRef(Particle);
        if (Sound != nullptr)
            Sound->AddRef();
        if (Light != nullptr)
            GameLightAddRef(Light);
    }

    // No ListOwner → no FUN_004fcea0 (unlike 00503070).
    if (Particle != nullptr)
        EffectRelease(Particle);
    if (Sound != nullptr)
        Sound->Release();
    if (Light != nullptr)
        GameLightRelease(Light);
    return 1;
}
