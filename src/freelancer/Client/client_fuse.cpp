// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// freelancer.exe fuse database and fuse-action classes -- retail .text:00529e10-0052c7f1.
// See client_fuse.h for what is and is not ported.

#include "client_fuse.h"

#include "cli_projectile_fx.h" // CliEffectListRemove (FUN_004fcef0), CliEffectCompanionClear
#include "client_explosion.h" // Action_Explode::Create (FUN_005361e0)
#include "client_gameobj.h"   // FClientGameobj, ExplosionArchAddRef / Release
#include "client_objdb.h"     // ClientObjDb_GetKindHead (FUN_00540fc0)
#include "common_goods.h"     // FindHardpoint_OS / _PS / HardpointToWorld / HardpointInfo
#include "common_physys.h"    // PhySys::LinearImpulse
#include "particle_effect.h"  // CliCreateEffectOneShot / CliCreateEffectWorldOneShot(On)
#include "vis_effect_arch.h"  // FindEffectArch / EffectArchAddRef / EffectArchRelease
#include "common_ini.h"
#include "DALib.h"
#include "engine/engbase.h"   // IEngine::GetInstanceTransform (vt+0xa0)
#include "x86math.h"          // I3DMathEngine

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace
{
    // DAT_00675430 -- this file's 3DMathEngine, FUN_005b73e0 on first use (0x0052b1bd).
    I3DMathEngine* G_FuseMath = nullptr;

    I3DMathEngine* FuseMath()
    {
        if (G_FuseMath == nullptr)
            G_FuseMath = Acquire3DMathEngine();
        return G_FuseMath;
    }

    // .rdata:005c77f4 -- pi/180 as a float (0x3c8efa35).
    float const kDegToRad = 0.01745329238474369f;

    // .rdata:005dbc98 -- double 0.0001: ClientImpulseAction::Trigger's minimum radius / force
    // and its "on top of the point" distance.
    double const kImpulseMinimum = 0.0001;

    // DAT_00675420 -- StartEffectAction::Trigger's "ROOT" hardpoint name, StringAlloc'd (not
    // temporary) by the static initializer LAB_0052b4f0.
    char* G_FuseRootHardpoint = StringAlloc("ROOT", false);

    // One pass of the row rotation StartEffectAction::Slot08 runs for "ori_offset"
    // (.text:0052b800-0052b847 and 0052b84b-0052b88b). The angle is value * DEG in x87 double
    // precision; its cosine is stored to a float temporary, its sine stays on the FPU stack.
    // For each column: A = cos*A - sin*B, B = sin*A + cos*B (both from the old A and B).
    void RotateRowPair(float* RowA, float* RowB, float Degrees)
    {
        double const Angle = static_cast<double>(Degrees) * static_cast<double>(kDegToRad);
        float const Cos = static_cast<float>(std::cos(Angle));
        double const Sin = std::sin(Angle);
        for (int Column = 0; Column < 3; ++Column)
        {
            float const A = RowA[Column];
            float const B = RowB[Column];
            RowA[Column] = static_cast<float>(static_cast<double>(Cos) * static_cast<double>(A) -
                                              Sin * static_cast<double>(B));
            RowB[Column] = static_cast<float>(Sin * static_cast<double>(A) +
                                              static_cast<double>(Cos) * static_cast<double>(B));
        }
    }
}

// ---------------------------------------------------------------------------
// ClientFuse
// ---------------------------------------------------------------------------

ClientFuse::ClientFuse(ID_String const& Nickname, float Lifetime)
    : Fuse(Nickname, Lifetime)
    , Host(nullptr)
{
}

ClientFuse::ClientFuse(ClientFuse const& Other)
    : Fuse(Other)
    , Host(Other.Host)
{
}

ClientFuse::~ClientFuse()
{
    // FUN_0052aab0: Fuse::~Fuse, then operator delete when bit 0 of the flag is set.
}

bool ClientFuse::Read(INI_Reader& Reader)
{
    // FUN_00529e10.
    if (!ReadFuseValues(Reader))
        return false;
    if (!Reader.read_header())
        return true;

    for (;;)
    {
        ClientFuseAction* Action = nullptr;
        if (Reader.is_header("start_explosion"))
            Action = new StartExplosionAction();
        else if (Reader.is_header("start_cam_particles"))
            Action = new StartCameraParticlesAction();
        else if (Reader.is_header("start_effect"))
            Action = new StartEffectAction();
        else if (Reader.is_header("impulse"))
            Action = new ClientImpulseAction();
        else if (Reader.is_header("ignite_fuse"))
            Action = new ClientIgniteFuseAction();
        else if (Reader.is_header("fuse"))
            return true; // the next fuse starts here; the caller reads it (0052a0bd)

        if (Action != nullptr)
        {
            // 0052a071 vt+8; a failed read returns false and leaks the action (retail).
            if (!Action->Slot08(Reader))
                return false;
            // 0052a078-0052a094: FLD [action+4] -> key, FUN_0052aae0 multimap insert.
            Actions.insert(std::multimap<float, FuseAction*>::value_type(
                Action->GetTriggerTime(), Action));
            // 0052a09d vt+0x24.
            if (Action->ShouldRandomize())
                bNeedsRandomize = true;
        }

        if (!Reader.read_header())
            return true;
    }
}

void ClientFuse::BindHost(FClientGameobj* NewHost)
{
    // FUN_0052a4d0: +0x34 = host, then every action's +0x24 = host (walks the action tree).
    Host = NewHost;
    for (std::multimap<float, FuseAction*>::iterator It = Actions.begin(); It != Actions.end(); ++It)
        static_cast<ClientFuseAction*>(It->second)->Host = NewHost;
}

// ---------------------------------------------------------------------------
// ClientFuseAction
// ---------------------------------------------------------------------------

ClientFuseAction::ClientFuseAction()
    : Host(nullptr)
{
}

ClientFuseAction::~ClientFuseAction()
{
}

void ClientFuseAction::UnTriggerAndSettle(unsigned short SubObjId)
{
    // LAB_0052b430: find SubObjId, erase it (shift the tail down, _Last -= 2); then when the
    // vector has no storage or is empty, clear bTriggered.
    std::vector<unsigned short>::iterator It =
        std::find(TriggeredSubObjs.begin(), TriggeredSubObjs.end(), SubObjId);
    if (It != TriggeredSubObjs.end())
        TriggeredSubObjs.erase(It);
    if (TriggeredSubObjs.empty())
        bTriggered = false;
}

// ---------------------------------------------------------------------------
// StartExplosionAction
// ---------------------------------------------------------------------------

StartExplosionAction::StartExplosionAction()
    : ExplosionArch(nullptr)
{
    // .text:00529ea1-00529ebf / 0052a151-0052a171.
    Explosion.clear();
    Hardpoint.clear();
    PosOffset.x = 0.0f;
    PosOffset.y = 0.0f;
    PosOffset.z = 0.0f;
}

FuseAction* StartExplosionAction::Slot04() const
{
    // FUN_0052a0f0: new 0x40 object, then its slot 12 copies *this.
    StartExplosionAction* Copy = new StartExplosionAction();
    Copy->Slot30(*this);
    return Copy;
}

bool StartExplosionAction::Slot08(INI_Reader& Reader)
{
    // FUN_0052b020.
    while (Reader.read_value())
    {
        if (ReadFuseActionValue(Reader))
            continue;
        if (Reader.is_value("explosion"))
        {
            Explosion.Id = CreateID(Reader.get_value_string());
        }
        else if (Reader.is_value("hardpoint"))
        {
            Hardpoint.Value = StringAlloc(Reader.get_value_string(), false);
        }
        else if (Reader.is_value("pos_offset"))
        {
            float const X = Reader.get_value_float(0);
            float const Y = Reader.get_value_float(1);
            float const Z = Reader.get_value_float(2);
            PosOffset.x = X;
            PosOffset.y = Y;
            PosOffset.z = Z;
        }
    }
    return true;
}

int StartExplosionAction::Load()
{
    // FUN_0052b290: Archetype::GetExplosion(+0x28) into +0x3c; a found one is AddRef'd
    // (FUN_00536100); then the tail JMP to FuseAction::Load.
    ExplosionArch = Archetype::GetExplosion(Explosion);
    if (ExplosionArch != nullptr)
        ExplosionArchAddRef(ExplosionArch);
    return FuseAction::Load();
}

int StartExplosionAction::UnLoad()
{
    // FUN_0052b2c0: +0x3c released when set (FUN_00536170, the pointer is left as it is); then
    // the tail JMP to FuseAction::UnLoad.
    if (ExplosionArch != nullptr)
        ExplosionArchRelease(ExplosionArch);
    return FuseAction::UnLoad();
}

void StartExplosionAction::Trigger(unsigned SourceId, unsigned short SubObjId)
{
    // LAB_0052b150 (RET 8). 0x0052b156..0x0052b15e: slot 6 (IsTriggered()) true skips to the
    // tail.
    if (!IsTriggered())
    {
        // 0x0052b164..0x0052b17f: the explosion looked up again by name (not +0x3c); only one
        // whose process (+0x14) is 2 starts.
        Archetype::Explosion* const Arch = Archetype::GetExplosion(Explosion);
        if (Arch != nullptr && Arch->Process == 2)
        {
            // 0x0052b185..0x0052b1a7: the host's hardpoint in object space (FindHardpoint_OS
            // on its engine index, vt+0x1c).
            long HpHandle = 0;
            HardpointInfo Info;
            if (FindHardpoint_OS(static_cast<long>(Host->get_index()), Hardpoint.Value, HpHandle,
                                 Info))
            {
                // 0x0052b1ad..0x0052b1b7: into world space on the handle it came back with.
                HardpointToWorld(HpHandle, Info);

                // 0x0052b1bd..0x0052b211: pos_offset turned by the hardpoint's orientation
                // (DAT_00675430 vt+0x2c TransformMatrix), added to its position.
                FVector Turned;
                FuseMath()->TransformMatrix(Turned, reinterpret_cast<FMatrix const&>(Info.Orient),
                                            reinterpret_cast<FVector const&>(PosOffset));
                Vector Pos;
                Pos.x = Turned.X + Info.Position.x;
                Pos.y = Turned.Y + Info.Position.y;
                Pos.z = Turned.Z + Info.Position.z;

                // 0x0052b215..0x0052b228: FUN_005361e0(Arch, Host, &Pos); a made action goes
                // on the host's action list (+0x20).
                Action_Explode* const Explode = Action_Explode::Create(Arch, Host, &Pos);
                if (Explode != nullptr)
                    Host->Actions.add(Explode);
            }
        }
    }

    // 0x0052b22e..0x0052b281: FuseAction::Trigger's body, inlined.
    FuseAction::Trigger(SourceId, SubObjId);
}

void StartExplosionAction::UnTrigger(unsigned short SubObjId)
{
    UnTriggerAndSettle(SubObjId);
}

void StartExplosionAction::Slot30(StartExplosionAction const& Other)
{
    // FUN_0052b110: CopyArchProperties, +0x28, +0x2c, +0x30..+0x38. +0x3c is not copied.
    FuseAction::CopyArchProperties(Other);
    Explosion = Other.Explosion;
    Hardpoint = Other.Hardpoint;
    PosOffset = Other.PosOffset;
}

// ---------------------------------------------------------------------------
// StartCameraParticlesAction
// ---------------------------------------------------------------------------

StartCameraParticlesAction::StartCameraParticlesAction()
    : EffectArch(nullptr)
{
    // .text:00529f0d-00529f17 / 0052a211-0052a21f.
    Effect.clear();
}

FuseAction* StartCameraParticlesAction::Slot04() const
{
    // FUN_0052a1b0.
    StartCameraParticlesAction* Copy = new StartCameraParticlesAction();
    Copy->Slot30(*this);
    return Copy;
}

bool StartCameraParticlesAction::Slot08(INI_Reader& Reader)
{
    // FUN_0052b2e0.
    while (Reader.read_value())
    {
        if (ReadFuseActionValue(Reader))
            continue;
        if (Reader.is_value("effect"))
            Effect.Id = CreateID(Reader.get_value_string());
    }
    return true;
}

int StartCameraParticlesAction::Load()
{
    // FUN_0052b4a0: FUN_0040f080(+0x28) into +0x2c; a found arch gets FUN_0040e960(0); then the
    // tail JMP to FuseAction::Load.
    EffectArch = FindEffectArch(&Effect.Id);
    if (EffectArch != nullptr)
        EffectArchAddRef(EffectArch, 0);
    return FuseAction::Load();
}

int StartCameraParticlesAction::UnLoad()
{
    // FUN_0052b4d0: +0x2c released (FUN_0040e990) when set -- the pointer is not cleared; then
    // the tail JMP to FuseAction::UnLoad.
    if (EffectArch != nullptr)
        EffectArchRelease(EffectArch);
    return FuseAction::UnLoad();
}

void StartCameraParticlesAction::Trigger(unsigned SourceId, unsigned short SubObjId)
{
    // LAB_0052b370 (RET 8). 0x0052b377..0x0052b37f: slot 6 (IsTriggered()) true skips to the
    // tail.
    if (!IsTriggered())
    {
        // 0x0052b381..0x0052b39e: a zero Vector on the stack (Pos3), then the host's vt+0x10
        // (IObjInspect::get_orientation, folded with get_transform: the object's whole
        // Transform), called before the id is tested.
        float const Pos3[3] = {0.0f, 0.0f, 0.0f};
        Matrix const& HostOrient = Host->get_orientation();

        // 0x0052b3a1..0x0052b3c9: a non-empty +0x28 is looked up again (FUN_0040f080, not +0x2c)
        // and handed to FUN_00503f40 on DAT_00674c5c with no companion out.
        if (!Effect.IsEmpty())
        {
            CliCreateEffectWorldOneShotOn(&G_CliEffectFactoryCameraParticles,
                                          FindEffectArch(&Effect.Id),
                                          reinterpret_cast<FTransform const*>(&HostOrient), Pos3,
                                          nullptr);
        }
    }

    // 0x0052b3ce..0x0052b421: FuseAction::Trigger's body, inlined.
    FuseAction::Trigger(SourceId, SubObjId);
}

void StartCameraParticlesAction::UnTrigger(unsigned short SubObjId)
{
    UnTriggerAndSettle(SubObjId);
}

void StartCameraParticlesAction::Slot30(StartCameraParticlesAction const& Other)
{
    // FUN_0052b350: CopyArchProperties, +0x28.
    FuseAction::CopyArchProperties(Other);
    Effect = Other.Effect;
}

// ---------------------------------------------------------------------------
// StartEffectAction
// ---------------------------------------------------------------------------

StartEffectAction::StartEffectAction()
    : bAttached(false)
    , EffectArch(nullptr)
{
    // .text:00529f65-00529f8e / 0052a2e1-0052a311: the companion by FUN_005154a0(0,0,0,0).
    Effect.clear();
    Companion[0] = nullptr;
    Companion[1] = nullptr;
    Companion[2] = nullptr;
    Companion[3] = nullptr;
}

StartEffectAction::~StartEffectAction()
{
    // FUN_0052b510. 0x0052b534..0x0052b56c and again 0x0052b56f..0x0052b5a2 (EH state 2 -> 1:
    // the class body's release, then the companion member's destructor): particle +0x4c
    // (FUN_004f7a90), sound +0x48 (vt+8), light +0x50 (FUN_00537ad0), each zeroed, then +0x44
    // zeroed -- the FUN_00502d90 sequence. The second pass finds everything null. Then the
    // hardpoint vector is freed (0x0052b5a5) and ~FuseAction runs (0x0052b5b7).
    CliEffectCompanionClear(Companion);
    CliEffectCompanionClear(Companion);
}

FuseAction* StartEffectAction::Slot04() const
{
    // FUN_0052a280.
    StartEffectAction* Copy = new StartEffectAction();
    Copy->Slot30(*this);
    return Copy;
}

bool StartEffectAction::Slot08(INI_Reader& Reader)
{
    // FUN_0052b5f0.
    while (Reader.read_value())
    {
        if (ReadFuseActionValue(Reader))
            continue;
        if (Reader.is_value("effect"))
        {
            Effect.Id = CreateID(Reader.get_value_string());
        }
        else if (Reader.is_value("attached"))
        {
            bAttached = Reader.get_value_bool(0);
        }
        else if (Reader.is_value("hardpoint"))
        {
            // 0052b67b-0052b6cb: {cleared CacheString, identity matrix (FUN_004215b0), zero
            // offset}, the hardpoint name StringAlloc'd, inserted at end (FUN_0052c4d0).
            FStartEffectHardpoint Entry;
            Entry.Hardpoint.clear();
            Entry.Orientation = Matrix(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
            Entry.PosOffset.x = 0.0f;
            Entry.PosOffset.y = 0.0f;
            Entry.PosOffset.z = 0.0f;
            Entry.Hardpoint.Value = StringAlloc(Reader.get_value_string(), false);
            Hardpoints.insert(Hardpoints.end(), 1, Entry);
        }
        else if (Reader.is_value("pos_offset"))
        {
            if (Hardpoints.empty())
            {
                Reader.log_link("StartEffectAction::Read() pos_offset precedes any hardpoint.  Ignored.");
            }
            else
            {
                float const X = Reader.get_value_float(0);
                float const Y = Reader.get_value_float(1);
                float const Z = Reader.get_value_float(2);
                FStartEffectHardpoint& Last = Hardpoints.back();
                Last.PosOffset.x = X;
                Last.PosOffset.y = Y;
                Last.PosOffset.z = Z;
            }
        }
        else if (Reader.is_value("ori_offset"))
        {
            if (Hardpoints.empty())
            {
                Reader.log_link("StartEffectAction::Read() ori_offset precedes any hardpoint.  Ignored.");
            }
            else
            {
                // 0052b79b-0052b88b. Value 2 builds a rotation about Y; value 0 then rotates
                // rows 1/2, value 1 rows 0/1. The Y angle's cosine comes from the unrounded
                // product, its sine from the product stored as a float (FST, FLD, FSIN).
                float const RotRows12 = Reader.get_value_float(0);
                float const RotRows01 = Reader.get_value_float(1);
                double const AngleY =
                    static_cast<double>(Reader.get_value_float(2)) * static_cast<double>(kDegToRad);
                float const AngleYStored = static_cast<float>(AngleY);
                Matrix& M = Hardpoints.back().Orientation;
                double const CosY = std::cos(AngleY);
                M.E[0][1] = 0.0f;
                M.E[1][1] = 1.0f;
                M.E[1][2] = 0.0f;
                M.E[2][1] = 0.0f;
                M.E[1][0] = 0.0f;
                double const SinY = std::sin(static_cast<double>(AngleYStored));
                M.E[0][0] = static_cast<float>(CosY);
                M.E[0][2] = static_cast<float>(SinY);
                M.E[2][0] = static_cast<float>(-SinY);
                M.E[2][2] = static_cast<float>(CosY);
                RotateRowPair(M.E[1], M.E[2], RotRows12);
                RotateRowPair(M.E[0], M.E[1], RotRows01);
            }
        }
    }
    return true;
}

int StartEffectAction::Load()
{
    // FUN_0052bf40: FUN_0040f080(+0x28) into +0x40; a found arch gets FUN_0040e960(0); then the
    // tail JMP to FuseAction::Load.
    EffectArch = FindEffectArch(&Effect.Id);
    if (EffectArch != nullptr)
        EffectArchAddRef(EffectArch, 0);
    return FuseAction::Load();
}

int StartEffectAction::UnLoad()
{
    // FUN_0052bf70: +0x40 released (FUN_0040e990) when set -- the pointer is not cleared; then
    // the tail JMP to FuseAction::UnLoad.
    if (EffectArch != nullptr)
        EffectArchRelease(EffectArch);
    return FuseAction::UnLoad();
}

void StartEffectAction::Trigger(unsigned SourceId, unsigned short SubObjId)
{
    // LAB_0052bb00 (RET 8). 0x0052bb0b..0x0052bb14: slot 6 (IsTriggered()) true skips to the
    // tail.
    if (!IsTriggered())
    {
        // 0x0052bb1a..0x0052bb42: engine index -1, the local transform identity (FUN_004215b0)
        // with a zero position, nothing found yet (BL).
        long EngineIndex = -1;
        FTransform Local;
        reinterpret_cast<Matrix&>(Local.Rotation) =
            Matrix(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
        Local.Translation.X = 0.0f;
        Local.Translation.Y = 0.0f;
        Local.Translation.Z = 0.0f;
        bool bFound = false;

        // 0x0052bb2b..0x0052bb66: no hardpoints (no storage, or a zero count) -> the host's root.
        int const Count = static_cast<int>(Hardpoints.size());
        if (Count == 0)
        {
            // 0x0052bb68..0x0052bb78.
            EngineIndex = Host->get_index();
            bFound = true;
        }
        else
        {
            // 0x0052bba4..0x0052bbbf: start = rand() * count / 32768 (IMUL, then CDQ / AND 0x7fff
            // / ADD / SAR 15 -- a signed division rounding toward zero).
            int const Start = (std::rand() * Count) / 32768;
            int Index = Start;
            for (;;)
            {
                FStartEffectHardpoint const& Entry = Hardpoints[Index];
                // 0x0052bbd0..0x0052bbf3: DACOM stricmp against the "ROOT" string (DAT_00675420).
                if (_stricmp(Entry.Hardpoint.Value, G_FuseRootHardpoint) == 0) // DACOM stricmp
                {
                    // 0x0052bbf5..0x0052bc0a, 0x0052bce7: the host's own index and the entry's
                    // orientation + offset copied as the local transform (0xc dwords from +4).
                    EngineIndex = Host->get_index();
                    reinterpret_cast<Matrix&>(Local.Rotation) = Entry.Orientation;
                    Local.Translation = reinterpret_cast<FVector const&>(Entry.PosOffset);
                    bFound = true;
                }
                else
                {
                    // 0x0052bc0f..0x0052bc2d: FindHardpoint_OS on the host's index; the handle it
                    // returns becomes the engine index.
                    HardpointInfo Info;
                    if (FindHardpoint_OS(Host->get_index(), Entry.Hardpoint.Value, EngineIndex,
                                         Info))
                    {
                        // 0x0052bc33..0x0052bcad: the offset turned by the hardpoint's
                        // orientation (vt+0x2c TransformMatrix), plus its position, through
                        // Vector(x,y,z) (FUN_00422b80) into the local position.
                        FVector Turned;
                        FuseMath()->TransformMatrix(Turned,
                                                    reinterpret_cast<FMatrix const&>(Info.Orient),
                                                    reinterpret_cast<FVector const&>(Entry.PosOffset));
                        Local.Translation.X = Turned.X + Info.Position.x;
                        Local.Translation.Y = Turned.Y + Info.Position.y;
                        Local.Translation.Z = Turned.Z + Info.Position.z;
                        // 0x0052bcb1..0x0052bce7: rotation = hardpoint orientation * entry
                        // orientation (vt+0x20 MulMatrix), copied (9 dwords) into the local one.
                        FMatrix Rotation;
                        FuseMath()->MulMatrix(Rotation,
                                              reinterpret_cast<FMatrix const&>(Info.Orient),
                                              reinterpret_cast<FMatrix const&>(Entry.Orientation));
                        Local.Rotation = Rotation;
                        bFound = true;
                    }
                }

                // 0x0052bceb..0x0052bd1a: next index, wrapping to 0 at the count. Back at the start
                // with nothing found goes straight to the tail; otherwise a found entry ends the
                // search.
                ++Index;
                if (Index == Count)
                    Index = 0;
                if (Index == Start)
                {
                    if (!bFound)
                    {
                        FuseAction::Trigger(SourceId, SubObjId);
                        return;
                    }
                    break;
                }
                if (bFound)
                    break;
            }
        }

        if (bAttached && EngineIndex != -1)
        {
            // 0x0052bd24..0x0052bd6d: attached -- a non-empty +0x28 is looked up again
            // (FUN_0040f080, not +0x40) and started on the host's effect list through
            // FUN_00503070 (DAT_00674c60) with the companion at +0x44; the result is unused.
            if (!Effect.IsEmpty())
            {
                CliCreateEffectOneShot(FindEffectArch(&Effect.Id), Host,
                                       static_cast<int>(EngineIndex), &Local, Companion);
            }
        }
        else if (bFound)
        {
            // 0x0052bd72..0x0052bdc5: world transform = the engine instance's (DALib::Engine
            // vt+0xa0) * local (vt+0x1c MulTransform), copied (0xc dwords).
            float* const InstanceRaw = DALib::Engine->GetInstanceTransform(
                reinterpret_cast<void*>(static_cast<intptr_t>(EngineIndex)));
            FTransform World;
            FuseMath()->MulTransform(World, *reinterpret_cast<FTransform const*>(InstanceRaw),
                                     Local);
            // 0x0052bdc7..0x0052bdd1: the host's velocity (vt+4), taken before the id test.
            Vector const Velocity = Host->get_velocity();
            // 0x0052bdd4..0x0052bdfc: a non-empty id looked up again and started as a world
            // one-shot (FUN_00503f40 on DAT_00674c60) moving with the host, no companion out.
            if (!Effect.IsEmpty())
            {
                CliCreateEffectWorldOneShot(FindEffectArch(&Effect.Id), &World, &Velocity.x,
                                            nullptr);
            }
        }
    }

    // 0x0052be01..0x0052be8d: FuseAction::Trigger's body, inlined.
    FuseAction::Trigger(SourceId, SubObjId);
}

void StartEffectAction::UnTrigger(unsigned short SubObjId)
{
    // FUN_0052be90 (RET 4). 0x0052be95..0x0052bed1: find SubObjId, erase it (tail shifted down,
    // _Last -= 2).
    std::vector<unsigned short>::iterator It =
        std::find(TriggeredSubObjs.begin(), TriggeredSubObjs.end(), SubObjId);
    if (It != TriggeredSubObjs.end())
        TriggeredSubObjs.erase(It);
    // 0x0052bed5..0x0052bee7: only once the list has no storage or is empty.
    if (!TriggeredSubObjs.empty())
        return;
    // 0x0052bee9..0x0052bf2a: the companion taken off the host's effect list (FUN_004fcef0);
    // when it was not on it, released right here -- particle +0x4c (FUN_004f7a90), sound +0x48
    // (vt+8), light +0x50 (FUN_00537ad0), then +0x44 zeroed (the FUN_00502d90 sequence).
    if (!CliEffectListRemove(&Host->EffectList, Companion))
        CliEffectCompanionClear(Companion);
    // 0x0052bf2c.
    bTriggered = false;
}

void StartEffectAction::Slot30(StartEffectAction const& Other)
{
    // FUN_0052b8c0: CopyArchProperties, +0x28, +0x2c, then the inlined vector::operator=
    // (skipped for self-assignment; elements are copied bitwise, FUN_0052c7d0).
    FuseAction::CopyArchProperties(Other);
    Effect = Other.Effect;
    bAttached = Other.bAttached;
    if (&Hardpoints != &Other.Hardpoints)
        Hardpoints = Other.Hardpoints;
}

// ---------------------------------------------------------------------------
// ClientImpulseAction
// ---------------------------------------------------------------------------

ClientImpulseAction::ClientImpulseAction()
    : Force(0.0f)
    , Radius(0.0f)
{
    // .text:00529fde-00529ff8 / 0052a3c1-0052a3db.
    Hardpoint.clear();
    PosOffset.x = 0.0f;
    PosOffset.y = 0.0f;
    PosOffset.z = 0.0f;
}

FuseAction* ClientImpulseAction::Slot04() const
{
    // FUN_0052a360.
    ClientImpulseAction* Copy = new ClientImpulseAction();
    Copy->Slot30(*this);
    return Copy;
}

bool ClientImpulseAction::Slot08(INI_Reader& Reader)
{
    // FUN_0052bf90.
    while (Reader.read_value())
    {
        if (ReadFuseActionValue(Reader))
            continue;
        if (Reader.is_value("hardpoint"))
        {
            Hardpoint.Value = StringAlloc(Reader.get_value_string(), false);
        }
        else if (Reader.is_value("force"))
        {
            Force = Reader.get_value_float(0);
        }
        else if (Reader.is_value("radius"))
        {
            Radius = Reader.get_value_float(0);
        }
        else if (Reader.is_value("pos_offset"))
        {
            // 0052c026-0052c05a: values read 2, 1, 0.
            float const Z = Reader.get_value_float(2);
            float const Y = Reader.get_value_float(1);
            float const X = Reader.get_value_float(0);
            PosOffset.x = X;
            PosOffset.y = Y;
            PosOffset.z = Z;
        }
    }
    return true;
}

void ClientImpulseAction::Trigger(unsigned SourceId, unsigned short SubObjId)
{
    // LAB_0052c0c0 (RET 8). 0x0052c0c6..0x0052c0ce: slot 6 (IsTriggered()) true skips to the
    // tail.
    if (!IsTriggered())
    {
        // 0x0052c0d4..0x0052c0fe: FindHardpoint_PS (import 0x005c65e8, not _OS) on the host's
        // index, the out handle starting at -1; not found goes to the tail.
        long HpHandle = -1;
        HardpointInfo Info;
        if (FindHardpoint_PS(Host->get_index(), Hardpoint.Value, HpHandle, Info))
        {
            // 0x0052c104..0x0052c112: into world space on the host's index (not the handle).
            HardpointToWorld(Host->get_index(), Info);

            // 0x0052c118..0x0052c16c: pos_offset through vt+0x38 (TransposeTransform) with the
            // hardpoint's orientation, plus its position, through Vector(x,y,z) (FUN_00422b80).
            FVector Turned;
            FuseMath()->TransposeTransform(Turned, reinterpret_cast<FMatrix const&>(Info.Orient),
                                           reinterpret_cast<FVector const&>(PosOffset));
            Vector Pos;
            Pos.x = Turned.X + Info.Position.x;
            Pos.y = Turned.Y + Info.Position.y;
            Pos.z = Turned.Z + Info.Position.z;

            // 0x0052c171..0x0052c193: radius, then force, against 1e-4 (double DAT_005dbc98);
            // TEST AH,0x41 / JNZ -- on to the tail when either is <= it (or unordered).
            if (static_cast<double>(Radius) > kImpulseMinimum &&
                static_cast<double>(Force) > kImpulseMinimum)
            {
                // 0x0052c199..0x0052c1b2: the ObjDB kind-10 bucket (FUN_00540fc0).
                int Kind = 10;
                FClientGameobj* Go = ClientObjDb_GetKindHead(&Kind);
                while (Go != nullptr)
                {
                    // 0x0052c1bf..0x0052c1d8: the object at +0x10 when its class has bit 0 set,
                    // else null -- and no null test before 0x0052c1da reads it (retail).
                    CObject* Obj = Go->Object;
                    if (Obj == nullptr || (static_cast<unsigned>(Obj->ObjectClass) & 1u) != 1u)
                        Obj = nullptr;

                    // 0x0052c1da..0x0052c229: the offset from the impulse point, each component
                    // stored as a float; its length from x87 sums of the float squares, stored
                    // as a float.
                    float const Dz = Obj->WorldPosition.z - Pos.z;
                    float const Dy = Obj->WorldPosition.y - Pos.y;
                    float const Dx = Obj->WorldPosition.x - Pos.x;
                    Vector Impulse;
                    Impulse.x = Dx;
                    Impulse.y = Dy;
                    Impulse.z = Dz;
                    float const Distance = static_cast<float>(
                        std::sqrt(static_cast<double>(Dx) * static_cast<double>(Dx) +
                                  static_cast<double>(Dy) * static_cast<double>(Dy) +
                                  static_cast<double>(Dz) * static_cast<double>(Dz)));

                    // 0x0052c22d..0x0052c23b: force against 0.0f (DAT_005c7474), TEST AH,0x41 /
                    // JNZ -- skipped when <= 0. 0x0052c241..0x0052c24d: distance against the
                    // radius, TEST AH,5 / JP -- skipped unless strictly less.
                    if (Force > 0.0f && Distance < Radius)
                    {
                        // 0x0052c24f..0x0052c25e: TEST AH,5 / JP -- a distance below 1e-4 pushes
                        // straight up by the force.
                        if (static_cast<double>(Distance) < kImpulseMinimum)
                        {
                            // 0x0052c260..0x0052c277.
                            Impulse.x = 0.0f;
                            Impulse.y = Force;
                            Impulse.z = 0.0f;
                        }
                        else
                        {
                            // 0x0052c279..0x0052c2ab: (1.0 (double DAT_005c89b8) - d / radius) *
                            // force / d, kept on the FPU stack, times each stored component.
                            double const Scale =
                                (1.0 - static_cast<double>(Distance) / static_cast<double>(Radius)) *
                                static_cast<double>(Force) / static_cast<double>(Distance);
                            Impulse.x = static_cast<float>(static_cast<double>(Dx) * Scale);
                            Impulse.y = static_cast<float>(static_cast<double>(Dy) * Scale);
                            Impulse.z = static_cast<float>(static_cast<double>(Dz) * Scale);
                        }
                        // 0x0052c2ad..0x0052c2bf: only an object with a physics body (+0x54).
                        if (Obj->BodyPtr != nullptr)
                            PhySys::LinearImpulse(Obj, Impulse, 1.0f);
                    }

                    // 0x0052c2c4..0x0052c2c9: the bucket link at +0x14.
                    Go = Go->ListNext;
                }
            }
        }
    }

    // 0x0052c2d0..0x0052c331: FuseAction::Trigger's body, inlined.
    FuseAction::Trigger(SourceId, SubObjId);
}

void ClientImpulseAction::Slot30(ClientImpulseAction const& Other)
{
    // FUN_0052c080: CopyArchProperties, +0x2c, +0x30, +0x34..+0x3c. The hardpoint at +0x28 is
    // NOT copied, so a clone keeps the cleared CacheString of its constructor (retail).
    FuseAction::CopyArchProperties(Other);
    Force = Other.Force;
    Radius = Other.Radius;
    PosOffset = Other.PosOffset;
}

// ---------------------------------------------------------------------------
// ClientIgniteFuseAction
// ---------------------------------------------------------------------------

ClientIgniteFuseAction::ClientIgniteFuseAction()
    : bFuseLoaded(false)
{
    // .text:0052a045-0052a04f / 0052a491-0052a49f.
    FuseId.clear();
}

FuseAction* ClientIgniteFuseAction::Slot04() const
{
    // FUN_0052a430.
    ClientIgniteFuseAction* Copy = new ClientIgniteFuseAction();
    Copy->Slot30(*this);
    return Copy;
}

bool ClientIgniteFuseAction::Slot08(INI_Reader& Reader)
{
    // FUN_0052c340.
    while (Reader.read_value())
    {
        if (ReadFuseActionValue(Reader))
            continue;
        if (Reader.is_value("fuse"))
            FuseId.Id = CreateID(Reader.get_value_string());
    }
    return true;
}

int ClientIgniteFuseAction::Load()
{
    // LAB_0052c3b0: first time only, Fuse::Load on the target fuse in the exe database; always
    // FuseAction::Load, whose result is returned.
    if (!bFuseLoaded)
    {
        bFuseLoaded = true;
        ClientFuse* Target = ClientFuseDB::Find(FuseId.Id);
        if (Target != nullptr)
            Target->Load();
    }
    return FuseAction::Load();
}

int ClientIgniteFuseAction::UnLoad()
{
    // LAB_0052c420: mirror of Load.
    if (bFuseLoaded)
    {
        bFuseLoaded = false;
        ClientFuse* Target = ClientFuseDB::Find(FuseId.Id);
        if (Target != nullptr)
            Target->UnLoad();
    }
    return FuseAction::UnLoad();
}

void ClientIgniteFuseAction::Slot30(ClientIgniteFuseAction const& Other)
{
    // FUN_0052b350 (same body as StartCameraParticlesAction's): CopyArchProperties, +0x28.
    // bFuseLoaded is not copied.
    FuseAction::CopyArchProperties(Other);
    FuseId = Other.FuseId;
}

// ---------------------------------------------------------------------------
// ClientFuseDB
// ---------------------------------------------------------------------------

// DAT_00675400. Its dynamic initializer (LAB_0052a510 -> FUN_0052a610 tree ctor, atexit
// LAB_0052a530 tree dtor) is the std::map's own construction/destruction.
std::map<unsigned, ClientFuse*> ClientFuseDB::Fuses;

std::map<unsigned, ClientFuse*>& ClientFuseDB::Get()
{
    // FUN_0052a680: `MOV EAX,DAT_00675400`.
    return Fuses;
}

ClientFuse* ClientFuseDB::Find(unsigned FuseId)
{
    std::map<unsigned, ClientFuse*>::iterator It = Fuses.find(FuseId);
    if (It == Fuses.end())
        return nullptr;
    return It->second;
}

void ClientFuseDB::Clear()
{
    // FUN_0052a690: virtual-delete every non-null prototype, then erase all nodes.
    for (std::map<unsigned, ClientFuse*>::iterator It = Fuses.begin(); It != Fuses.end(); ++It)
    {
        if (It->second != nullptr)
            delete It->second;
    }
    Fuses.clear();
}

void ClientFuseDB::LoadAll()
{
    // FUN_0052a7a0: one FUN_0052a7d0 per path in FuseDB::m_FuseINIFiles (node+0xc = the
    // TString's characters).
    for (std::list<TString<260>>::iterator It = m_FuseINIFiles.begin(); It != m_FuseINIFiles.end(); ++It)
        LoadFile(It->Data);
}

void ClientFuseDB::LoadFile(char const* Path)
{
    // FUN_0052a7d0.
    INI_Reader Reader;
    if (!Reader.open(Path, false))
        return;
    Reader.read_header(); // result unused (0052a827)
    while (!Reader.is_end())
    {
        if (Reader.is_header("fuse"))
        {
            ClientFuse* NewFuse = new ClientFuse(ID_String{CreateID("")}, -1.0f);
            // A failed Read, or a nickname already in the map, leaks NewFuse (retail).
            if (NewFuse->Read(Reader))
                Fuses.insert(std::map<unsigned, ClientFuse*>::value_type(NewFuse->Name().Id, NewFuse));
        }
        else
        {
            Reader.read_header();
        }
    }
}
