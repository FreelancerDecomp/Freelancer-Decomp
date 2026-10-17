// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "jump_effect.h"

#include "cli_projectile_fx.h" // CliEffectCompanionArmStop / Clear, CliEffectListRemove
#include "client_gameobj.h"
#include "common_cobject.h"    // CObject / CSolar
#include "common_goods.h"      // FindHardpoint_PS / HardpointToWorld / HardpointInfo
#include "common_ini.h"        // INI_Reader, StringAlloc
#include "common_misc.h"       // GetFileSysPath
#include "gate_tunnel.h"
#include "globals.h"           // g_display (DAT_00679bc0), [Data] jump_effect at +0x770
#include "particle_effect.h"   // the effect factories, FUN_00503f40 / FUN_00503070 / FUN_004f7d20
#include "player_behavior.h"   // GetPlayerObjRW (FUN_0054baf0)
#include "vis_effect_arch.h"   // FindEffectArch (FUN_0040f080)

#include "dacom.h" // FDUMP
#include "soundmanager.h"      // ISoundSource::SetPose

#include <list>
#include <new>

namespace
{
    // .rdata 0x005da80c.
    char const* const kJumpEffectCpp = "E:\\FL\\Scratch\\Source\\Client\\Space\\JumpEffect.cpp";

    // DAT_00674fd0 -- the loaded (or lazily defaulted) JumpShipEffectDesc. Freed by
    // FUN_00502aa0 (JumpEffectDescsClear) at space teardown.
    JumpShipEffectDesc* G_JumpShipEffectDesc = nullptr;

    // DAT_00674fd8 (sentinel) / DAT_00674fdc (count) -- the [JumpGateEffect] records, appended
    // by JumpEffectIniLoad (FUN_005020a0, 0x0054ac15) and emptied by JumpEffectDescsClear
    // (FUN_00502aa0, 0x0054afb1).
    std::list<FJumpEffectDesc> G_JumpEffectDescs;

    // DAT_005d1f04 -- the jump camera-shake amplitude.
    constexpr float kJumpShakeAmplitude = 0.015f;

    // FUN_004215b0 on a Transform's rotation, then a zero translation (the inline sequences
    // at 0x005034a7..0x005034cb / 0x0050366e..0x005036a0 / 0x00503730..0x00503758).
    void SetIdentityTransform(Transform& Xform)
    {
        Xform.Rotation.E[0][0] = 1.0f;
        Xform.Rotation.E[0][1] = 0.0f;
        Xform.Rotation.E[0][2] = 0.0f;
        Xform.Rotation.E[1][0] = 0.0f;
        Xform.Rotation.E[1][1] = 1.0f;
        Xform.Rotation.E[1][2] = 0.0f;
        Xform.Rotation.E[2][0] = 0.0f;
        Xform.Rotation.E[2][1] = 0.0f;
        Xform.Rotation.E[2][2] = 1.0f;
        Xform.Translation.x = 0.0f;
        Xform.Translation.y = 0.0f;
        Xform.Translation.z = 0.0f;
    }

    // The inline CSolar fetch (0x00502cb0..0x00502cd3 / 0x0050363e..0x00503663): the object
    // when its class has all of 0x303, else null.
    CSolar const* AsSolar(CObject const* Obj)
    {
        if (Obj == nullptr || (static_cast<unsigned>(Obj->ObjectClass) & 0x303u) != 0x303u)
            return nullptr;
        return static_cast<CSolar const*>(Obj);
    }
}

FJumpFxCompanion::~FJumpFxCompanion()
{
    // FUN_00502d90.
    CliEffectCompanionClear(this);
}

FJumpGateRingFx::FJumpGateRingFx()
{
    // LAB_00502de0: byte +0 = 0, dwords +4..+0x20 = 0 (the companion by its constructor).
    bCreated = 0;
    CreateTime = 0.0f;
    Position.x = 0.0f;
    Position.y = 0.0f;
    Position.z = 0.0f;
}

FJumpGateEffect::FJumpGateEffect(FClientSolarGameobj* InSolar)
{
    // FUN_00502ba0 (thiscall, RET 4). 0x00502bc4..0x00502be9: +0x1c..+0x28 zeroed and the
    // five rings built by the eh vector constructor (LAB_00502de0) -- the member constructors.
    // 0x00502bee..0x00502c20: +4 = the solar; a null one is reported (line 0xe3) but used.
    Solar = InSolar;
    if (InSolar == nullptr)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: %s", kJumpEffectCpp, 0xe3,
              "null solar passed to JumpGateEffect constructor");
    }

    // 0x00502c23..0x00502c76: +0 = 0; the solar's +0x200 id, or CreateID("jump_effect_default")
    // when it is empty, looked up in the DAT_00674fd8 list (payload at node +8).
    Desc = nullptr;
    ID_String Wanted = Solar->Field_0x200;
    if (Wanted.IsEmpty())
        Wanted.Id = CreateID("jump_effect_default");
    for (std::list<FJumpEffectDesc>::iterator It = G_JumpEffectDescs.begin();
         It != G_JumpEffectDescs.end(); ++It)
    {
        if (Wanted.Id == It->Nickname.Id)
        {
            Desc = &*It;
            break;
        }
    }
    // 0x00502c76..0x00502ca2: not found -> FDUMP 0x100001, line 0xf8.
    if (Desc == nullptr)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: Missing jump effect: %s\n", kJumpEffectCpp, 0xf8,
              Wanted.get_string());
    }

    // 0x00502ca5..0x00502cad.
    Clock = 0.0f;
    bFire = 0;

    // 0x00502cb0..0x00502d0c: the solar's CSolar archetype +0xa4 (JumpOutHp) through
    // FindHardpoint_PS on get_index (vt+0x1c); the result is not tested, the info's position
    // is copied to +0x0c either way. The archetype read has no test (a non-solar crashes).
    CSolar const* const SolarObj = AsSolar(Solar != nullptr ? Solar->Object : nullptr);
    Archetype::Solar const* const Arch = static_cast<Archetype::Solar const*>(SolarObj->Archetype);
    HardpointInfo Info;
    long HpHandle = 0;
    FindHardpoint_PS(Solar->get_index(), Arch->JumpOutHp.Value, HpHandle, Info);
    JumpOutPosition = Info.Position;

    // 0x00502d0f..0x00502d6e: each descriptor ring (count re-read from +0x40 every lap, the
    // descriptor used with no test): its hardpoint position and create time.
    for (int Ring = 0; Ring < Desc->RingCount; ++Ring)
    {
        FindHardpoint_PS(Solar->get_index(), Desc->Rings[Ring].Hp.Value, HpHandle, Info);
        Rings[Ring].Position = Info.Position;
        Rings[Ring].CreateTime = Desc->Rings[Ring].CreateTime;
    }
}

FJumpGateEffect::~FJumpGateEffect()
{
    // FUN_00502e40 (thiscall). 0x00502e5f..0x00502e99: with a solar, every descriptor ring's
    // companion leaves the solar's +0x28 effect list (FUN_004fcef0); the descriptor is used
    // with no test. 0x00502e9a..0x00502eea: the rings (FUN_00502e00, eh vector destructor)
    // and then +0x1c (inline FUN_00502d90) -- the member destructors.
    if (Solar != nullptr)
    {
        for (int Ring = 0; Ring < Desc->RingCount; ++Ring)
            CliEffectListRemove(&Solar->EffectList, &Rings[Ring].Fx);
    }
}

void FJumpGateEffect::Update(float Dt)
{
    // FUN_00502f00 (thiscall, RET 4). 0x00502f07..0x00502f15: the clock stops once fired.
    if (bFire == 0)
        Clock = Clock + Dt;

    // 0x00502f18..0x00503052: every descriptor ring, the count re-read from +0x40 after each
    // lap and the descriptor used with no test.
    for (int Ring = 0; Ring < Desc->RingCount; ++Ring)
    {
        FJumpGateRingFx& RingFx = Rings[Ring];
        // 0x00502f33..0x00502f47: once only, and only when the clock is above the ring's create
        // time (TEST AH,0x41 / JNZ: not-above and NaN skip).
        if (RingFx.bCreated != 0)
            continue;
        if (!(Clock > RingFx.CreateTime))
            continue;
        RingFx.bCreated = 1;

        // 0x00502f4d..0x00502fc6: identity orientation at the ring's hardpoint position, on the
        // solar's own instance (its vt+0x1c).
        FTransform Local;
        for (int Row = 0; Row < 3; ++Row)
        {
            for (int Col = 0; Col < 3; ++Col)
                Local.Rotation.D[Row][Col] = (Row == Col) ? 1.0f : 0.0f;
        }
        Local.Translation.X = RingFx.Position.x;
        Local.Translation.Y = RingFx.Position.y;
        Local.Translation.Z = RingFx.Position.z;
        int const Index = static_cast<int>(Solar->get_index());

        // 0x00502fc0..0x00502ffd: the descriptor ring's effect (+4 + 0xc * Ring) through
        // DAT_00674c60 (FUN_00503070) into the ring's companion, the solar as the list owner.
        ID_String const& Effect = Desc->Rings[Ring].Effect;
        if (!Effect.IsEmpty() &&
            CliCreateEffectOneShot(FindEffectArch(&Effect.Id), Solar, Index, &Local,
                                   &RingFx.Fx) != 0)
            continue;

        // 0x00502fff..0x00503031: an empty name or a failed start warns, line 0x12c.
        FDUMP(0x100002,
              "%s(%d) : *** WARNING: jump gate effect number (%d) named (%s) did not create.",
              kJumpEffectCpp, 0x12c, Ring, Effect.get_string());
    }
}

void JumpGateEffectFire(FJumpGateEffect* Gate)
{
    // FUN_00503380.
    Gate->bFire = 1;
}

JumpShipEffectDesc* JumpShipEffectDescGet()
{
    // FUN_00502b00 (0x00502b16..0x00502b97): DAT_00674fd0 when set; otherwise FDUMP 0x100002
    // (line 0xd5) and operator new(0x14) with the five ids cleared (the inline form of
    // FUN_00503a50); a failed new stores and returns null.
    if (G_JumpShipEffectDesc == nullptr)
    {
        FDUMP(0x100002, "%s(%d) : *** WARNING: %s", kJumpEffectCpp, 0xd5,
              "no JumpShipEffectDesc was loaded, creating a default");
        G_JumpShipEffectDesc = new (std::nothrow) JumpShipEffectDesc();
    }
    return G_JumpShipEffectDesc;
}

void JumpEffectIniLoad()
{
    // FUN_005020a0 (no arguments). 0x005020c1..0x00502104: an INI_Reader on the stack, the
    // [Data] jump_effect path (DAT_0067a334 = g_display.jump_effect.Text) through
    // GetFileSysPath(.., 0), open(path, false).
    INI_Reader Reader;
    char Path[MAX_PATH];
    GetFileSysPath(Path, g_display.jump_effect.Text, nullptr);
    bool const bOpened = Reader.open(Path, false);

    // 0x00502106..0x0050211c: the previous JumpShipEffectDesc is freed and DAT_00674fd0 cleared
    // whether or not the file opened.
    if (G_JumpShipEffectDesc != nullptr)
        delete G_JumpShipEffectDesc;
    G_JumpShipEffectDesc = nullptr;

    // 0x00502122..0x0050212a: the three per-ring key counts ([ESP+0x14] glow_ring_effect,
    // [ESP+0x10] glow_ring_hp, [ESP+0x1c] glow_create_time) are zeroed ONCE, and the record
    // [ESP+0x24] lives for the whole function: a later [JumpGateEffect] section that omits a
    // key inherits the previous section's value, as in retail. Only the ids are re-cleared per
    // section (0x0050217a..0x005021b0). Retail leaves the first section's unset fields as stack
    // garbage; the port starts them at zero.
    int EffectCount = 0;
    int HpCount = 0;
    int TimeCount = 0;
    FJumpEffectDesc Record = FJumpEffectDesc();

    if (!bOpened)
        return;
    while (Reader.read_header())
    {
        if (Reader.is_header("JumpGateEffect"))
        {
            Record.Nickname.clear();
            for (int Ring = 0; Ring < 5; ++Ring)
            {
                // FUN_00502a80.
                Record.Rings[Ring].Effect.clear();
                Record.Rings[Ring].Hp.clear();
            }
            Record.JumpTunnelEffect.clear();
            Record.JumpTunnel.clear();

            while (Reader.read_value())
            {
                if (Reader.is_value("nickname"))
                {
                    Record.Nickname.Id = CreateID(Reader.get_value_string());
                }
                else if (Reader.is_value("glow_ring_effect"))
                {
                    // 0x00502205..0x00502246: every non-empty value i of the five sets the
                    // count to i + 1.
                    for (int Index = 0; Index < 5; ++Index)
                    {
                        if (!Reader.is_value_empty(static_cast<unsigned>(Index)))
                        {
                            EffectCount = Index + 1;
                            Record.Rings[Index].Effect.Id =
                                CreateID(Reader.get_value_string(static_cast<unsigned>(Index)));
                        }
                    }
                }
                else if (Reader.is_value("glow_ring_hp"))
                {
                    // 0x0050226b..0x005022ad: StringAlloc(value, false) into the CacheString.
                    for (int Index = 0; Index < 5; ++Index)
                    {
                        if (!Reader.is_value_empty(static_cast<unsigned>(Index)))
                        {
                            HpCount = Index + 1;
                            Record.Rings[Index].Hp.Value = StringAlloc(
                                Reader.get_value_string(static_cast<unsigned>(Index)), false);
                        }
                    }
                }
                else if (Reader.is_value("glow_create_time"))
                {
                    for (int Index = 0; Index < 5; ++Index)
                    {
                        if (!Reader.is_value_empty(static_cast<unsigned>(Index)))
                        {
                            TimeCount = Index + 1;
                            Record.Rings[Index].CreateTime =
                                Reader.get_value_float(static_cast<unsigned>(Index));
                        }
                    }
                }
                else if (Reader.is_value("jump_out_time"))
                {
                    Record.JumpOutTime = Reader.get_value_float(0);
                }
                else if (Reader.is_value("jump_out_tunnel_time"))
                {
                    Record.JumpOutTunnelTime = Reader.get_value_float(0);
                }
                else if (Reader.is_value("jump_in_tunnel_time"))
                {
                    Record.JumpInTunnelTime = Reader.get_value_float(0);
                }
                else if (Reader.is_value("jump_in_time"))
                {
                    Record.JumpInTime = Reader.get_value_float(0);
                }
                else if (Reader.is_value("kill_time_before_done"))
                {
                    Record.KillTimeBeforeDone = Reader.get_value_float(0);
                }
                else if (Reader.is_value("jump_tunnel_effect"))
                {
                    Record.JumpTunnelEffect.Id = CreateID(Reader.get_value_string());
                }
                else if (Reader.is_value("jump_tunnel"))
                {
                    Record.JumpTunnel.Id = CreateID(Reader.get_value_string());
                }
                else if (Reader.is_value("jump_ambient"))
                {
                    // 0x00502425..0x00502445: ftol of each float, low byte stored.
                    for (int Index = 0; Index < 3; ++Index)
                    {
                        Record.JumpAmbient[Index] = static_cast<unsigned char>(
                            static_cast<int>(Reader.get_value_float(static_cast<unsigned>(Index))));
                    }
                }
                else if (Reader.is_value("jump_background_color"))
                {
                    for (int Index = 0; Index < 3; ++Index)
                    {
                        Record.JumpBackgroundColor[Index] = static_cast<unsigned char>(
                            static_cast<int>(Reader.get_value_float(static_cast<unsigned>(Index))));
                    }
                }
                else
                {
                    // 0x00502490..0x005024b2.
                    FDUMP(0x100002, "%s(%d) : *** WARNING: %s", kJumpEffectCpp, 0x84,
                          "UNKNOWN jumpeffect parameter");
                }
            }

            // 0x005024ca..0x005024e8: the ring count is the smallest of the three key counts
            // (signed compares: glow_create_time vs glow_ring_hp, then glow_ring_effect).
            int Count = (TimeCount < HpCount) ? TimeCount : HpCount;
            if (EffectCount < Count)
                Count = EffectCount;
            Record.RingCount = Count;

            // 0x005024e2..0x00502541: a 0x70 node linked in front of the sentinel (push_back),
            // the record copied in by FUN_005048a0 (a plain memberwise copy: the CacheString
            // pointers are shared, not duplicated), ++DAT_00674fdc.
            G_JumpEffectDescs.push_back(Record);
        }
        else if (Reader.is_header("JumpShipEffect"))
        {
            // 0x00502565..0x005025c8: the previous descriptor freed, then operator new(0x14) with
            // the five ids cleared (null on a failed new) into DAT_00674fd0.
            if (G_JumpShipEffectDesc != nullptr)
            {
                delete G_JumpShipEffectDesc;
                G_JumpShipEffectDesc = nullptr;
            }
            G_JumpShipEffectDesc = new (std::nothrow) JumpShipEffectDesc();

            // 0x005025ce..0x005026c9: each key stores into DAT_00674fd0 with no test of it; the
            // id at +0 is never written here.
            while (Reader.read_value())
            {
                if (Reader.is_value("jump_out_effect"))
                {
                    G_JumpShipEffectDesc->Ids[1].Id = CreateID(Reader.get_value_string());
                }
                else if (Reader.is_value("jump_in_effect"))
                {
                    G_JumpShipEffectDesc->Ids[2].Id = CreateID(Reader.get_value_string());
                }
                else if (Reader.is_value("jump_done_effect_player"))
                {
                    G_JumpShipEffectDesc->Ids[3].Id = CreateID(Reader.get_value_string());
                }
                else if (Reader.is_value("jump_done_effect_nonplayer"))
                {
                    G_JumpShipEffectDesc->Ids[4].Id = CreateID(Reader.get_value_string());
                }
                else
                {
                    // 0x00502696..0x005026b7.
                    FDUMP(0x100002, "%s(%d) : *** WARNING: %s", kJumpEffectCpp, 0xb1,
                          "UNKNOWN jump ship effect parameter");
                }
            }
        }
    }
    // 0x005026e4..: the INI_Reader's destructor (inlined in retail).
}

void JumpEffectDescsClear()
{
    // FUN_00502aa0 (no arguments; caller 0x0054afb1): every node of DAT_00674fd8 unlinked and
    // freed with --DAT_00674fdc (no record destructor runs -- the glow_ring_hp strings are not
    // freed), then DAT_00674fd0 freed and cleared when set.
    G_JumpEffectDescs.clear();
    if (G_JumpShipEffectDesc != nullptr)
    {
        delete G_JumpShipEffectDesc;
        G_JumpShipEffectDesc = nullptr;
    }
}

FJumpShipEffect::FJumpShipEffect(FClientShipGameobj* InShip)
{
    // FUN_005038c0 (thiscall, RET 4).
    // 0x005038dc: ID_String::clear on +0x18. 0x005038eb..0x0050391c: the four companions
    // (+0x28..+0x67) zeroed -- here their constructors.
    TunnelEffect.clear();

    // 0x0050391f..0x00503966: +0xa8 identity orientation, zero translation.
    SetIdentityTransform(Field_0xa8);

    // 0x0050396c..0x005039d7: DAT_00674fd0, lazily defaulted with the FUN_00411c50(0x10000, 2)
    // = 0x100002 warning -- FUN_00502b00 inlined.
    Desc = JumpShipEffectDescGet();
    // 0x005039d5..0x005039fd: no descriptor -> FDUMP 0x100001, line 0x1d5.
    if (Desc == nullptr)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: %s", kJumpEffectCpp, 0x1d5,
              "no JumpShipEffect descriptor found");
    }

    // 0x00503a00..0x00503a35.
    JumpOutTunnelTime = 5.0f;
    JumpInTunnelTime = 5.0f;
    KillTimeBeforeDone = 0.5f;
    JumpInTime = 0.5f;
    Tunnel = nullptr;
    Field_0x6c = 0;
    Field_0x6d = 0;
    Field_0x70 = 0.0f;
    Field_0xd8 = 0;
    JumpOutTime = 0.0f;
    Ambient = nullptr;
    Color = nullptr;
    Ship = InShip;
}

FJumpShipEffect::FJumpShipEffect(FClientShipGameobj* InShip, FClientSolarGameobj* GateSolar)
{
    // FUN_00503440 (thiscall, RET 8).
    // 0x00503461..0x00503496: ID_String::clear on +0x18; the companions zeroed (constructors).
    TunnelEffect.clear();
    // 0x005034a7..0x005034cb: FUN_004215b0 on +0xa8, zero translation.
    SetIdentityTransform(Field_0xa8);

    // 0x005034bc..0x005034db: a null gate solar or ship returns at once, everything else as
    // the member constructors left it.
    if (GateSolar == nullptr || InShip == nullptr)
        return;

    // 0x005034e1..0x0050350e: FUN_00502b00; none -> FDUMP 0x100001, line 0x1d5.
    Desc = JumpShipEffectDescGet();
    if (Desc == nullptr)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: %s", kJumpEffectCpp, 0x1d5,
              "no JumpShipEffect descriptor found");
    }

    // 0x00503511..0x0050354d.
    Ship = InShip;
    Tunnel = nullptr;
    Field_0x6c = 0;
    Field_0x6d = 0;
    Field_0x70 = 0.0f;
    Field_0xd8 = 0;
    JumpOutTime = 0.0f;
    JumpOutTunnelTime = 5.0f;
    JumpInTunnelTime = 5.0f;
    JumpInTime = 0.0f;
    KillTimeBeforeDone = 0.5f;
    Ambient = nullptr;
    Color = nullptr;

    // 0x00503550..0x005035a3: the solar's +0x204 JumpGateEffect (FUN_00548890); without one a
    // temporary is built (operator new(0xe0) + FUN_00502ba0), and a failed new skips to the
    // hardpoint.
    FJumpGateEffect* Gate = static_cast<FJumpGateEffect*>(GateSolar->Field_0x204);
    FJumpGateEffect* Temp = nullptr;
    FJumpEffectDesc* GateDesc = nullptr;
    bool bHaveGate = true;
    if (Gate == nullptr)
    {
        Gate = new (std::nothrow) FJumpGateEffect(GateSolar);
        Temp = Gate;
        bHaveGate = Gate != nullptr;
    }

    if (bHaveGate)
    {
        // 0x005035a9..0x0050360e: get_type (vt+0x88) on the gate solar; with the gate's
        // descriptor, copy its ship parameters (JumpOutTime 0 when the type has bit 0x800).
        unsigned Type = 0;
        GateSolar->get_type(Type);
        bool const bNoJumpOut = ((Type >> 11) & 1u) != 0;
        GateDesc = Gate->Desc;
        if (GateDesc != nullptr)
        {
            JumpOutTime = bNoJumpOut ? 0.0f : GateDesc->JumpOutTime;
            JumpOutTunnelTime = GateDesc->JumpOutTunnelTime;
            JumpInTunnelTime = GateDesc->JumpInTunnelTime;
            JumpInTime = GateDesc->JumpInTime;
            KillTimeBeforeDone = GateDesc->KillTimeBeforeDone;
            TunnelEffect = GateDesc->JumpTunnelEffect;
            Ambient = GateDesc->JumpAmbient;
            Color = GateDesc->JumpBackgroundColor;
        }
        else
        {
            // 0x00503610..0x00503632: FDUMP 0x100001, line 0x181.
            FDUMP(0x100001, "%s(%d) : *** ERROR: %s", kJumpEffectCpp, 0x181,
                  "no jump descriptor in JumpGateEffect");
        }
    }

    // 0x00503635..0x005036a0: the gate solar's cobject (vt+0x150) as a CSolar (0x303), used
    // with no test; +0xa8 reset to identity; its archetype's JumpOutHp (+0xa4) looked up on
    // its engine instance (+4).
    CSolar const* const SolarObj = AsSolar(GateSolar->cobject());
    long const Instance = SolarObj->EngineInstanceId;
    Archetype::Solar const* const Arch = static_cast<Archetype::Solar const*>(SolarObj->Archetype);
    SetIdentityTransform(Field_0xa8);
    HardpointInfo Info;
    long HpHandle = 0;
    if (FindHardpoint_PS(Instance, Arch->JumpOutHp.Value, HpHandle, Info))
    {
        // 0x005036bd..0x00503706: HardpointToWorld(instance, info), then +0xa8 = {orientation,
        // position} of the info.
        HardpointToWorld(Instance, Info);
        Field_0xa8.Rotation = Info.Orient;
        Field_0xa8.Translation = Info.Position;
    }
    else
    {
        // 0x00503708..0x00503758: FDUMP 0x100001 (line 0x196) and identity again.
        FDUMP(0x100001, "%s(%d) : *** ERROR: Hardpoint (%s) not found on jumpgate",
              kJumpEffectCpp, 0x196, Arch->JumpOutHp.Value);
        SetIdentityTransform(Field_0xa8);
    }

    // 0x0050375b..0x005037e7: for the player's ship, the gate descriptor's jump_tunnel
    // (FUN_004fedd0; not found -> FDUMP 0x100001, line 0x1a5, with the raw id dword where the
    // format wants a string, as retail pushes it) becomes +0x24; a tunnel gets the ship
    // (FUN_004ff3b0) and +0xa8 copied to its +0x230. +0xa4 = the CSolar's +0x1b4.
    if (static_cast<void*>(static_cast<IObjRW*>(Ship)) == GetPlayerObjRW())
    {
        FGateTunnel* Found = nullptr;
        if (GateDesc != nullptr)
        {
            Found = GateTunnelFind(&GateDesc->JumpTunnel);
            if (Found == nullptr)
            {
                FDUMP(0x100001, "%s(%d) : *** ERROR: gate tunnel '%s' not found.",
                      kJumpEffectCpp, 0x1a5, GateDesc->JumpTunnel.Id);
            }
        }
        Tunnel = Found;
        if (Found != nullptr)
        {
            GateTunnelSetShip(Found, Ship);
            Found->Frame = Field_0xa8;
        }
        Field_0xa4 = SolarObj->DestSystem;
    }

    // 0x005037ed..0x00503896: a temporary gate is destroyed (FUN_00502e40 inline) and freed.
    delete Temp;
}

FJumpShipEffect::~FJumpShipEffect()
{
    // FUN_00503a80 (thiscall). The three stop-and-clear blocks are the inline
    // CliEffectCompanionArmStop sequence followed by FUN_00502d90.
    // 0x00503aa2..0x00503b10: +0x28.
    CliEffectCompanionArmStop(&FxA);
    CliEffectCompanionClear(&FxA);
    // 0x00503b15..0x00503b7b: +0x48.
    CliEffectCompanionArmStop(&FxC);
    CliEffectCompanionClear(&FxC);
    // 0x00503b80..0x00503b8e: FUN_004fcef0 on the ship's +0x28 effect list with +0x58; the
    // ship pointer is used with no test.
    CliEffectListRemove(&Ship->EffectList, &FxD);
    // 0x00503b93..0x00503bf9: +0x38.
    CliEffectCompanionArmStop(&FxB);
    CliEffectCompanionClear(&FxB);
    // 0x00503bfe..0x00503cae: the member destructors (FUN_00502d90) in reverse order --
    // +0x58, +0x48, +0x38, +0x28 -- run as the companions go out of scope.
}

double FJumpShipEffect::ShakeAmplitude() const
{
    // FUN_00503e20: switch on the ship's +0x158 - 1 (JA past 3 -> the 0.0f already loaded
    // from DAT_005c7474).
    switch (Ship->Field_0x158)
    {
    case 1:
        // 0x00503e3c..0x00503e5a: JumpOutTime FCOMP 0.0f, TEST AH,0x41 / JNZ -- not above
        // (NaN included) returns the amplitude itself; otherwise amplitude / JumpOutTime * +0x70.
        if (JumpOutTime > 0.0f)
            return static_cast<double>(kJumpShakeAmplitude) / JumpOutTime * Field_0x70;
        return kJumpShakeAmplitude;
    case 2:
    case 3:
        // 0x00503e87..0x00503e8f.
        return kJumpShakeAmplitude;
    case 4:
        // 0x00503e5b..0x00503e86: JumpInTime not above 0 (NaN included) -> 0.0f; otherwise
        // (1.0f (DAT_005c75dc) - +0x70 / JumpInTime) * amplitude.
        if (JumpInTime > 0.0f)
            return (1.0 - static_cast<double>(Field_0x70) / JumpInTime) * kJumpShakeAmplitude;
        return 0.0;
    default:
        return 0.0;
    }
}

namespace
{
    // The local Transform the four state hooks build on the stack (identity rotation: 1.0f
    // stored to the three diagonal dwords, 0 to the other six) as the x86math FTransform the
    // effect calls take.
    FTransform MakeLocalXform(float X, float Y, float Z)
    {
        FTransform Xform;
        for (int Row = 0; Row < 3; ++Row)
        {
            for (int Col = 0; Col < 3; ++Col)
                Xform.Rotation.D[Row][Col] = (Row == Col) ? 1.0f : 0.0f;
        }
        Xform.Translation.X = X;
        Xform.Translation.Y = Y;
        Xform.Translation.Z = Z;
        return Xform;
    }

    // One companion moved onto the ship: FUN_004f7d20 on the particle (+8) with the ship as the
    // owner, the sound's slot 37 (+4, vt+0x94 SetPose) and FUN_005377c0 on the light (+0xc),
    // each only when present (0x00504291..0x005042c8 / 0x005043ef..0x0050442b).
    void AttachCompanionToShip(FJumpFxCompanion& Fx, FClientShipGameobj* Ship, int Index,
                               FTransform const& Xform)
    {
        if (Fx.Particle != nullptr)
            ParticleEffectAttach(Fx.Particle, Ship, Index, &Xform);
        if (Fx.Sound != nullptr)
            Fx.Sound->SetPose(Index, &Xform.Rotation.D[0][0]);
        if (Fx.Light != nullptr)
            CliExplosionAttachLocalXform(Fx.Light, Index, &Xform);
    }
}

void FJumpShipEffect::StartJumpOut()
{
    // FUN_00503ea0. Nothing without a tunnel (0x00503ea6..0x00503eab).
    if (Tunnel == nullptr)
        return;

    // 0x00503eb2: FUN_004ff360(1).
    Tunnel->SetState(1);

    // 0x00503eb9..0x00503f06: a zero velocity; a named tunnel effect starts at +0xa8 through
    // the jump-effect factory DAT_00674fcc into FxB. An empty name, or a start that fails,
    // warns (0x100002, line 0x246) with the name.
    float const Velocity[3] = {0.0f, 0.0f, 0.0f};
    bool bStarted = false;
    if (!TunnelEffect.IsEmpty())
    {
        bStarted = CliCreateEffectWorldOneShotOn(
                       &G_CliEffectFactoryJump, FindEffectArch(&TunnelEffect.Id),
                       reinterpret_cast<FTransform const*>(&Field_0xa8), Velocity, &FxB) != 0;
    }
    if (!bStarted)
    {
        FDUMP(0x100002, "%s(%d) : *** WARNING: jump out effect (%s) did not create.",
              kJumpEffectCpp, 0x246, TunnelEffect.get_string());
    }

    // 0x00503f32.
    Field_0x6d = 0;
}

void FJumpShipEffect::EnterTunnel()
{
    // FUN_00504210. 0x00504217..0x00504221: FUN_004ff360(2) when there is a tunnel.
    if (Tunnel != nullptr)
        Tunnel->SetState(2);

    // 0x00504226..0x005042c8: FxB onto the ship at its engine index, identity, no offset.
    {
        FTransform const Local = MakeLocalXform(0.0f, 0.0f, 0.0f);
        int const Index = static_cast<int>(Ship->get_index());
        AttachCompanionToShip(FxB, Ship, Index, Local);
    }

    // 0x005042cd..0x00504347: the ship's position (vt+0) as the translation, a zero velocity.
    Vector const& ShipPos = Ship->get_position();
    FTransform const World = MakeLocalXform(ShipPos.x, ShipPos.y, ShipPos.z);
    float const Velocity[3] = {0.0f, 0.0f, 0.0f};

    // 0x0050432e..0x00504381: the descriptor's jump_out_effect (+4) through DAT_00674fcc into
    // FxA; then 0x00504387..0x0050442b FxA onto the ship like FxB.
    ID_String const& JumpOutEffect = Desc->Ids[1];
    if (!JumpOutEffect.IsEmpty() &&
        CliCreateEffectWorldOneShotOn(&G_CliEffectFactoryJump, FindEffectArch(&JumpOutEffect.Id),
                                      &World, Velocity, &FxA) != 0)
    {
        FTransform const Local = MakeLocalXform(0.0f, 0.0f, 0.0f);
        int const Index = static_cast<int>(Ship->get_index());
        AttachCompanionToShip(FxA, Ship, Index, Local);
        return;
    }

    // 0x00504437..0x0050445c: the warning names the TUNNEL effect (+0x18), not the one that
    // failed, as retail does; line 0x25e.
    FDUMP(0x100002, "%s(%d) : *** WARNING: jump out effect (%s) did not create.",
          kJumpEffectCpp, 0x25e, TunnelEffect.get_string());
}

void FJumpShipEffect::StartJumpIn()
{
    // FUN_00504470. 0x00504477..0x00504481: FUN_004ff360(3) when there is a tunnel.
    if (Tunnel != nullptr)
        Tunnel->SetState(3);

    // 0x00504486..0x00504500: the ship's position as the translation, a zero velocity.
    Vector const& ShipPos = Ship->get_position();
    FTransform const World = MakeLocalXform(ShipPos.x, ShipPos.y, ShipPos.z);
    float const Velocity[3] = {0.0f, 0.0f, 0.0f};

    // 0x005044e7..0x0050453a: the descriptor's jump_in_effect (+8) through DAT_00674fcc into
    // FxC; then 0x00504540..0x005045e4 FxC onto the ship at its engine index.
    ID_String const& JumpInEffect = Desc->Ids[2];
    if (!JumpInEffect.IsEmpty() &&
        CliCreateEffectWorldOneShotOn(&G_CliEffectFactoryJump, FindEffectArch(&JumpInEffect.Id),
                                      &World, Velocity, &FxC) != 0)
    {
        FTransform const Local = MakeLocalXform(0.0f, 0.0f, 0.0f);
        int const Index = static_cast<int>(Ship->get_index());
        AttachCompanionToShip(FxC, Ship, Index, Local);
    }
    else
    {
        // 0x005045f7..0x0050461e: line 0x277, with the jump_in_effect name.
        FDUMP(0x100002, "%s(%d) : *** WARNING: jump in effect (%s) did not create.",
              kJumpEffectCpp, 0x277, JumpInEffect.get_string());
    }

    // 0x005045ea / 0x00504624: on every path.
    Field_0xd8 = 1;
}

void FJumpShipEffect::FinishJump()
{
    // FUN_00504640. 0x00504648..0x00504652: FUN_004ff360(0) when there is a tunnel.
    if (Tunnel != nullptr)
        Tunnel->SetState(0);

    // 0x00504657..0x00504669: the player's ship takes jump_done_effect_player (+0xc), any
    // other jump_done_effect_nonplayer (+0x10); each with its own warning.
    bool const bPlayer = static_cast<void*>(static_cast<IObjRW*>(Ship)) == GetPlayerObjRW();
    ID_String const& DoneEffect = bPlayer ? Desc->Ids[3] : Desc->Ids[4];

    // 0x0050466f..0x00504676 / 0x00504771..0x00504778: an empty name returns quietly.
    if (DoneEffect.IsEmpty())
        return;

    // 0x0050467c..0x00504737 / 0x0050477e..0x00504839: identity, no offset, the ship's engine
    // index; the name tested a second time, then FUN_00503070 on DAT_00674c60 with the ship as
    // the list owner into FxD.
    FTransform const Local = MakeLocalXform(0.0f, 0.0f, 0.0f);
    int const Index = static_cast<int>(Ship->get_index());
    if (!DoneEffect.IsEmpty() &&
        CliCreateEffectOneShot(FindEffectArch(&DoneEffect.Id), Ship, Index, &Local, &FxD) != 0)
        return;

    if (bPlayer)
    {
        // 0x0050483b..0x00504862: line 0x293.
        FDUMP(0x100002, "%s(%d) : *** WARNING: player jump done effect (%s) did not create.",
              kJumpEffectCpp, 0x293, DoneEffect.get_string());
    }
    else
    {
        // 0x0050473d..0x00504764: line 0x28b.
        FDUMP(0x100002, "%s(%d) : *** WARNING: non-player jump done effect (%s) did not create.",
              kJumpEffectCpp, 0x28b, DoneEffect.get_string());
    }
}

void FJumpShipEffect::Update(float Dt)
{
    // FUN_00503cd0 (thiscall, RET 4). The effect's clock (+0x70) is not advanced here.
    // 0x00503cd7..0x00503cf8: in the tunnel states 2 and 3 the tunnel, when there is one,
    // advances (FUN_004ff3c0).
    int const State = Ship->Field_0x158;
    if ((State == 2 || State == 3) && Tunnel != nullptr)
        Tunnel->Update(Dt);

    // 0x00503cfd..0x00503d07: everything else only in state 4 (jump in). The state is read
    // again from the ship; FGateTunnel::Update does not touch it.
    if (Ship->Field_0x158 != 4)
        return;

    // 0x00503d0d..0x00503d13 / 0x00503e11: the first update after StartJumpIn (which sets
    // +0xd8) only clears the flag.
    if (Field_0xd8 != 0)
    {
        Field_0xd8 = 0;
        return;
    }

    // 0x00503d19..0x00503d30: once only (+0x6d), and only when (JumpInTime - KillTimeBeforeDone)
    // FCOMP clock gives C0 = 1: TEST AH,0x5 / JP skips on greater, equal and unordered, so the
    // fall-through is "JumpInTime - KillTimeBeforeDone < clock". The difference stays on the
    // x87 stack (not stored as float), hence the double arithmetic.
    if (Field_0x6d != 0)
        return;
    if (!(static_cast<double>(JumpInTime) - static_cast<double>(KillTimeBeforeDone) <
          static_cast<double>(Field_0x70)))
        return;

    // 0x00503d36..0x00503dd1: the tunnel effect companion FxB is stopped -- particle vt+4,
    // sound SetPose(-1, identity with zero translation) + Stop (vt+0x94 / vt+0x60), light
    // FUN_005379b0 + vt+8 -- which is CliEffectCompanionArmStop's sequence, in the same order.
    CliEffectCompanionArmStop(&FxB);
    // 0x00503dd4..0x00503e02: then released -- particle FUN_004f7a90, sound Release (vt+8),
    // light FUN_00537ad0, each zeroed, and slot 0 (+0x38) zeroed: FUN_00502d90's body.
    CliEffectCompanionClear(&FxB);
    // 0x00503e05.
    Field_0x6d = 1;
}
