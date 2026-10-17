// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "gf_object_ship.h"

#include "character_select_equip.h" // GetPlayerEquipDescListRetail (FUN_004c4060) & co.
#include "client_cliobj.h"          // CliObjMaterialPaths, CliEngine*, CliObjRecomputeLodPickExplicit
#include "fade_to_black.h"          // SetFadeImmediate (FUN_00412070), SetFadeTarget (FUN_00412050)
#include "game_options.h"           // GetGameOption (FUN_005792b0)
#include "gameflow.h"               // FUN_00558a30 / FUN_00558c00
#include "gf_script_object.h"       // G_GfScriptManager (DAT_0066d2d0)
#include "menu_sound_arch.h"        // LookupClientSoundArch (FUN_0042ae40), PlayClientSoundArch
#include "misc_util.h"              // MiscUtilLoadEngineInstance (FUN_0041c540)
#include "vis_effect_arch.h"        // FindEffectArch (FUN_0040f080)

#include "DALib.h"
#include "alchemy.h"
#include "common_archetype.h"
#include "common_cobject.h"   // CompoundInstanceList
#include "common_goods.h"
#include "common_ini.h"       // StringAlloc
#include "common_misc.h"      // malloc_strcpy / malloc_strfree
#include "common_newchar.h"   // RtcSlider
#include "dacom.h"
#include "engine/engbase.h"
#include "soundmanager.h"
#include "x86math.h"

#include <list>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace
{
    // DAT_00668a9c -- the ObjectShip singleton. Written by RebuildModel (0x00447219), cleared by
    // the destructor (0x004470e4).
    FObjectShip* G_GfObjectShip = nullptr;

    // DAT_00668a98 -- ObjectShip.cpp's 3DMathEngine, FUN_005b73e0 on first use (FUN_00447be0,
    // FUN_00446d90, FUN_004478c0).
    I3DMathEngine* G_ObjectShipMath = nullptr;
    // DAT_00668aac -- the second one, FUN_00448c40's.
    I3DMathEngine* G_ObjectShipMountMath = nullptr;

    // .rdata 0x005cbdbc.
    char const kObjectShipCpp[] = "E:\\FL\\Scratch\\Source\\Client\\Gf\\ObjectShip.cpp";

    I3DMathEngine* ObjectShipMath()
    {
        if (G_ObjectShipMath == nullptr)
            G_ObjectShipMath = Acquire3DMathEngine();
        return G_ObjectShipMath;
    }

    I3DMathEngine* ObjectShipMountMath()
    {
        if (G_ObjectShipMountMath == nullptr)
            G_ObjectShipMountMath = Acquire3DMathEngine();
        return G_ObjectShipMountMath;
    }

    // IFxEffect slots 7 / 8 still use the existing Dt argument adapter.
    void* FloatArg(float Value)
    {
        void* Out = nullptr;
        memcpy(&Out, &Value, sizeof(Value));
        return Out;
    }

    // The inverse frame the ObjectShip functions build inline: FUN_00447be0 (R^T P), FUN_00411350
    // (negated), FUN_00447b60 with the nine elements pushed transposed, FUN_00447bb0.
    void ShipInverseFrame(FTransform& Out, FMatrix const& R, FVector const& P)
    {
        FVector Rotated;
        ShipTransposeRotate(Rotated, R, P);
        FVector Negated;
        Negated.X = -Rotated.X;
        Negated.Y = -Rotated.Y;
        Negated.Z = -Rotated.Z;
        int Row = 0;
        for (Row = 0; Row < 3; ++Row)
        {
            int Col = 0;
            for (Col = 0; Col < 3; ++Col)
                Out.Rotation.D[Row][Col] = R.D[Col][Row];
        }
        Out.Translation = Negated;
    }

    void ToFMatrix(FMatrix& Out, Matrix const& In)
    {
        memcpy(&Out, &In, sizeof(Out));
    }

    void ToFVector(FVector& Out, Vector const& In)
    {
        Out.X = In.x;
        Out.Y = In.y;
        Out.Z = In.z;
    }

    // FUN_00447ec0 (cdecl Out, Format, ...) -- vsprintf into a 0x100-byte local, then
    // Out->Value = StringAlloc(Buffer, false). The previous value is not freed (retail).
    void FormatHardpointName(CacheString* Out, char const* Format, ...)
    {
        char Buffer[0x100];
        va_list Args;
        va_start(Args, Format);
        vsprintf(Buffer, Format, Args);
        va_end(Args);
        Out->Value = StringAlloc(Buffer, false);
    }

    // FUN_005595a0 (cdecl) -- FUN_00406cd0(Handle, 0, 0) on the companion unless Handle is -1.
    void RecomputeShipCompanionLod(Archetype::ICliObj* Companion, long Handle)
    {
        if (Handle == -1)
            return;
        Archetype::CliObjRecomputeLodPickExplicit(Companion, Handle, nullptr, false);
    }

    // The sound play key FUN_00447f90 builds on the stack (0x18 bytes, FUN_00428680's layout:
    // offset, flags, the position, +0x14).
    struct FEngineSoundKey
    {
        unsigned Offset;    // +0x00 0
        unsigned Flags;     // +0x04 low byte (garbage & 0xf8) | 3: looping, spatial
        float Pos[3];       // +0x08 the hardpoint instance's position
        unsigned Field14;   // +0x14 0
    };
}

// FUN_00447be0 (cdecl Out, M, V) -- 0x00447be0..0x00447bf1 DAT_00668a98 tested and filled
// (FUN_005b73e0), TransposeTransform (slot +0x38) into a scratch, copied to *Out.
void ShipTransposeRotate(FVector& Out, FMatrix const& M, FVector const& V)
{
    FVector Scratch;
    ObjectShipMath()->TransposeTransform(Scratch, M, V);
    Out = Scratch;
}

// ---------------------------------------------------------------------------------------
// The description
// ---------------------------------------------------------------------------------------

FObjectShipDesc::FObjectShipDesc()
    : Ship(nullptr), LaunchScript(nullptr), bShadow(0), Float1C(0.0f), Equipment(nullptr)
{
    // FUN_00446bf0: +0x00 = 0, +0x04..+0x0c = 0, the orientation identity (+0x10 / +0x20 /
    // +0x30 = 1.0f), +0x34 = 0, +0x38 = 0, +0x3c = 0, +0x40 = 0.
    Pos.x = 0.0f;
    Pos.y = 0.0f;
    Pos.z = 0.0f;
    MatrixSetIdentity(Orient);
}

FObjectShipDesc::~FObjectShipDesc()
{
    // FUN_00446c50: malloc_strfree(&+0x34).
    malloc_strfree(&LaunchScript);
}

// ---------------------------------------------------------------------------------------
// Engine flame effects (+0xa0)
// ---------------------------------------------------------------------------------------

FObjectShipEngineFx::FObjectShipEngineFx()
    : Instance(kRoomObjectNoInstance), bStopping(0), Lifetime(0.0f)
{
    // LAB_00446c60: +0x00 = +0x04 = 0, +0x08 = -1, +0x3c = 0, +0x40 = 0. The frame (+0x0c..
    // +0x3b) is not written.
    Particles.Effect = nullptr;
    Particles.Vis = nullptr;
}

FObjectShipEngineFx::~FObjectShipEngineFx()
{
    // FUN_00446c80: FUN_005574a0, +0x08 = -1, +0x3c = 0, +0x40 = 0.
    Particles.Destroy();
    Instance = kRoomObjectNoInstance;
    bStopping = 0;
    Lifetime = 0.0f;
}

void FObjectShipEngineFx::Start(unsigned ArchId, float InLifetime, void* InInstance,
                                Vector const& InPos, Matrix const& InOrient)
{
    // FUN_00446ca0. 0x00446ca5: FUN_005574a0.
    Particles.Destroy();
    // 0x00446caa..0x00446ce3: +0x18 = the orientation, +0x08 = the instance, +0x0c = the
    // position, +0x3c = 0, +0x40 = 0.
    Orient = InOrient;
    Instance = InInstance;
    Pos = InPos;
    bStopping = 0;
    Lifetime = 0.0f;

    // 0x00446ce6..0x00446cfb: the archetype's class through slot 4 (no null test).
    Archetype::Equipment* const Equip = Archetype::GetEquipment(ArchId);
    if (Equip->get_class_type() == Archetype::AClassEngine)
    {
        // 0x00446cfd..0x00446d10: the companion's +0x9c flame_effect, unless empty.
        ID_String const* const Flame = Archetype::CliEngineFlameEffect(Equip->Companion);
        if (!Flame->IsEmpty())
        {
            // 0x00446d12..0x00446d1d: FUN_0040f080 on it.
            FEffectArch* const Arch = FindEffectArch(&Flame->Id);
            if (Arch != nullptr)
            {
                // 0x00446d1f..0x00446d45: FUN_00557360 with EffectArch +0x08; on success
                // +0x3c = 0, +0x40 = InLifetime and FUN_00446d90(0).
                if (Particles.Create(Arch->VisEffect))
                {
                    bStopping = 0;
                    Lifetime = InLifetime;
                    Update(0.0f);
                    return;
                }
            }
            else
            {
                // 0x00446d48..0x00446d71.
                FDUMP(0x100001,
                      "%s(%d) : *** ERROR: Cannot load engine flame effect ID=%u, name=%s\n",
                      kObjectShipCpp, 0x84, Flame->Id, Flame->get_string());
            }
        }
    }
    // 0x00446d74..0x00446d78.
    Update(0.0f);
}

void FObjectShipEngineFx::Update(float Delta)
{
    // FUN_00446d90. 0x00446d99..0x00446d9c.
    if (Particles.Effect == nullptr)
        return;

    // 0x00446da2..0x00446e0a: the effect's frame = the instance's transform (Engine slot 40)
    // times the local frame (DAT_00668a98 MulTransform, slot +0x1c), copied (FUN_00406880).
    FTransform Local;
    ToFMatrix(Local.Rotation, Orient);
    ToFVector(Local.Translation, Pos);
    FTransform const* const InstanceXform =
        reinterpret_cast<FTransform const*>(DALib::Engine->GetInstanceTransform(Instance));
    FTransform Product;
    ObjectShipMath()->MulTransform(Product, *InstanceXform, Local);
    FTransform Xform = Product;

    // 0x00446e0f..0x00446e34: slot 3 (0, &frame), slot 7 (Delta), slot 8 (Delta).
    Particles.Effect->Slot3(nullptr, &Xform);
    Particles.Effect->Slot7(FloatArg(Delta));
    Particles.Effect->Slot8(FloatArg(Delta));

    if (bStopping == 0)
    {
        // 0x00446e40..0x00446e4e: FCOMP 0.0; TEST AH,1; JNZ -> less (or unordered): endless.
        if (!(static_cast<double>(Lifetime) >= 0.0))
            return;
        // 0x00446e50..0x00446e68: FSUB, FST (the float stored, the extended value compared);
        // TEST AH,0x41; JP -> greater or unordered: still counting.
        double const Remaining = static_cast<double>(Lifetime) - static_cast<double>(Delta);
        Lifetime = static_cast<float>(Remaining);
        if (Remaining > 0.0 || Remaining != Remaining)
            return;
        // 0x00446e6a..0x00446e7c: slot 13 (1) on the effect, +0x3c = 1, +0x40 = 0.
        if (Particles.Effect != nullptr)
            Particles.Effect->Slot13(reinterpret_cast<void*>(1));
        bStopping = 1;
        Lifetime = 0.0f;
        return;
    }

    // 0x00446e8d..0x00446e9d: stopping -- slot 11 (4); AL set -> FUN_005574a0.
    GENRESULT const Done = Particles.Effect->Slot11(reinterpret_cast<void*>(4));
    if ((static_cast<unsigned>(Done) & 0xffu) != 0)
        Particles.Destroy();
}

// ---------------------------------------------------------------------------------------
// Engine sounds (+0xa4 / +0xa8)
// ---------------------------------------------------------------------------------------

FObjectShipEngineSound::FObjectShipEngineSound()
    : Instance(kRoomObjectNoInstance), Source(nullptr)
{
    // LAB_00446eb0.
}

FObjectShipEngineSound::~FObjectShipEngineSound()
{
    // FUN_00446ec0: slot 34 (+0x88) with 0 on a source, then -1 / 0.
    if (Source != nullptr)
        Source->SetLooping(0);
    Instance = kRoomObjectNoInstance;
    Source = nullptr;
}

// ---------------------------------------------------------------------------------------
// ObjectShip
// ---------------------------------------------------------------------------------------

FObjectShip* GfObjectShip()
{
    // FUN_00446be0. MOV EAX,[DAT_00668a9c]; RET.
    return G_GfObjectShip;
}

FObjectShip::FObjectShip(FRoomRuntime* InOwner, FObjectShipDesc const& Desc)
    : FRoomObject(InOwner),
      Ship(nullptr),
      LaunchScript(nullptr),
      bShadow(0),
      Equipment(nullptr),
      Instance(kRoomObjectNoInstance),
      EngineFx(nullptr),
      LoopSounds(nullptr),
      RumbleSounds(nullptr),
      ScriptState(0)
{
    // FUN_00446ef0. 0x00446f14: FUN_005593d0 (the base); 0x00446f24: FUN_00457d60 on +0x74 (the
    // member); 0x00446f36..0x00446f42: +0x30 / +0x64 / +0x68 / +0x6c = 0, +0x70 = -1.
    // 0x00446f49: FUN_00457e20 on +0x74.
    Visuals.ResetFields();
    // 0x00446f55..0x00446f67: +0xa0..+0xac = 0 (above); 0x00446f6d: FUN_00447120, which writes
    // +0x34 / +0x40 (the ctor leaves them).
    Load(Desc);
}

FObjectShip::~FObjectShip()
{
    // FUN_00446fb0. 0x00446fd8..0x0044701d: the +0x6c list's nodes erased, its head node and
    // the list itself freed.
    if (Equipment != nullptr)
        delete Equipment;
    // 0x00447020..0x0044702a.
    malloc_strfree(&LaunchScript);
    // 0x0044702a..0x0044704e: FUN_00458160 on every hardpoint of +0x74.
    int Index = 0;
    for (Index = 0; Index < Visuals.Count; ++Index)
        Visuals.Detach(Index);
    // 0x00447050..0x004470a6: the three arrays (FUN_00446c80 / FUN_00446ec0 per element).
    delete[] EngineFx;
    delete[] LoopSounds;
    delete[] RumbleSounds;
    // 0x004470af..0x004470c6: the three pointers cleared, FUN_00457e30 on +0x74.
    EngineFx = nullptr;
    LoopSounds = nullptr;
    RumbleSounds = nullptr;
    Visuals.Clear();
    // 0x004470cb..0x004470df: Engine slot 24 on a live model (+0x70 keeps its value).
    if (Instance != kRoomObjectNoInstance)
        DALib::Engine->DestroyInstance(Instance);
    // 0x004470e4: DAT_00668a9c = 0, whichever ship this is.
    G_GfObjectShip = nullptr;
    // 0x004470ef / 0x004470fe: FUN_00457dc0 on +0x74 and FUN_005594b0 -- the member and base
    // destructors.
}

bool FObjectShip::IsVisible()
{
    // FUN_0044fd00: XOR AL,AL.
    return false;
}

void FObjectShip::Load(FObjectShipDesc const& Desc)
{
    // FUN_00447120. 0x0044713a..0x00447165: +0x30 = the archetype, +0x34 the position, +0x40
    // the orientation.
    Ship = Desc.Ship;
    Pos = Desc.Pos;
    Orient = Desc.Orient;
    // 0x00447167..0x0044717b: malloc_strcpy into +0x64, +0x68 = the byte.
    malloc_strcpy(&LaunchScript, Desc.LaunchScript);
    bShadow = Desc.bShadow;
    // 0x0044717e..0x00447187: slot 3 with the float.
    SetFloat1C(Desc.Float1C);
    // 0x0044718a..0x004471d4: operator new(0xc) with a fresh head, then the Common list
    // operator= from the description's list.
    Equipment = new EquipDescList();
    Equipment->Items = Desc.Equipment->Items;
    // 0x004471dc..0x004471e2: +0xac = 0, FUN_00447210.
    ScriptState = 0;
    RebuildModel();
    // 0x004471eb..0x004471f8.
    RenderMode = 0;
    LightGroup = 3;
    Layer = 0;
    Flag14 = 0;
}

void FObjectShip::RebuildModel()
{
    // FUN_00447210. 0x00447219.
    G_GfObjectShip = this;
    // 0x0044721f..0x0044723e: FUN_00458160 on every hardpoint of +0x74.
    int Index = 0;
    for (Index = 0; Index < Visuals.Count; ++Index)
        Visuals.Detach(Index);
    // 0x00447240..0x00447298: the three arrays deleted and cleared.
    delete[] EngineFx;
    delete[] LoopSounds;
    delete[] RumbleSounds;
    EngineFx = nullptr;
    LoopSounds = nullptr;
    RumbleSounds = nullptr;
    // 0x00447289..0x0044729e: FUN_00457e30 on +0x74.
    Visuals.Clear();
    // 0x004472a3..0x004472bc: Engine slot 24 on a live model, +0x70 = -1.
    if (Instance != kRoomObjectNoInstance)
    {
        DALib::Engine->DestroyInstance(Instance);
        Instance = kRoomObjectNoInstance;
    }

    // 0x004472bf..0x004472ef: FUN_0041c540 on the archetype's mesh with the companion's
    // material list, the material entries into RoomObject +0x20 and the handle into a local
    // (-1 first).
    Archetype::ICliObj* const Companion = Ship->Companion;
    long Handle = -1;
    Instance = MiscUtilLoadEngineInstance(Ship->DaArchetype.Value, DALib::Data, nullptr, nullptr,
                                          Archetype::CliObjMaterialPaths(Companion),
                                          &MaterialEntries, &Handle);
    if (Instance != kRoomObjectNoInstance)
    {
        // 0x004472f8: FUN_004482b0.
        GfShipApplyCollisionDamage(Instance);
        // 0x004472fe..0x0044732c: FUN_00448c40 with {+0x34, +0x40} as a Csys.
        Csys Frame;
        Frame.Pos = Pos;
        Frame.Orient = Orient;
        GfShipPlaceOnMount(Instance, &Frame);
        // 0x00447331..0x00447347: +0x13 = the companion's envmap material is not empty.
        bEnvMap = Archetype::CliObjEnvmapMaterial(Companion)[0] != '\0' ? 1 : 0;
        // 0x0044734a: FUN_00457f10 on +0x74 with the model.
        Visuals.Populate(static_cast<long>(reinterpret_cast<intptr_t>(Instance)));
        // 0x0044734f..0x00447372: with "ship_loadouts", FUN_004581b0 and FUN_00458220 on +0x74
        // with the +0x6c list.
        if (RtcSlider::is_enabled("ship_loadouts"))
        {
            Visuals.AttachAll(Equipment);
            Visuals.AddFlashLights(Equipment);
        }
        // 0x00447379: FUN_00447690.
        BuildHardpointArrays();
    }
    // 0x0044737e..0x00447384: FUN_005595a0 (companion, handle) on both paths.
    RecomputeShipCompanionLod(Companion, Handle);
}

void FObjectShip::BuildHardpointArrays()
{
    // FUN_00447690. 0x004476ac..0x004476fd: operator new(count * 0x44 + 4), the count stored,
    // LAB_00446c60 on each (FUN_005b7fba) -> +0xa0.
    int const HpCount = Visuals.Count;
    EngineFx = new FObjectShipEngineFx[HpCount];
    // 0x004476ee..0x0044777b: the same with 8-byte LAB_00446eb0 elements -> +0xa4, +0xa8.
    LoopSounds = new FObjectShipEngineSound[HpCount];
    RumbleSounds = new FObjectShipEngineSound[HpCount];
    // 0x00447781..0x004477b2: each sound bound to its hardpoint's instance (get_instance twice).
    int Index = 0;
    for (Index = 0; Index < HpCount; ++Index)
    {
        LoopSounds[Index].Instance =
            reinterpret_cast<void*>(static_cast<intptr_t>(Visuals.Summary->get_instance(Index)));
        RumbleSounds[Index].Instance =
            reinterpret_cast<void*>(static_cast<intptr_t>(Visuals.Summary->get_instance(Index)));
    }
}

void FObjectShip::Update(double DeltaSeconds)
{
    // LAB_004473a0. 0x004473a5..0x004473c5: the [OBJECTS] group and SHIPS options.
    if (GetGameOption(kGameOptObjectsGroup) == 0)
        return;
    if (GetGameOption(kGameOptShips) == 0)
        return;

    float const Delta = static_cast<float>(DeltaSeconds);
    int Index = 0;
    for (Index = 0; Index < Visuals.Count; ++Index)
    {
        // 0x004473e6..0x00447433: a bound loop sound follows its instance (Engine slot 36 into
        // a local, source slot 35).
        FObjectShipEngineSound& Loop = LoopSounds[Index];
        if (Loop.Instance != kRoomObjectNoInstance && Loop.Source != nullptr)
        {
            float const* const At = DALib::Engine->GetInstancePosition(Loop.Instance);
            float Where[3] = {At[0], At[1], At[2]};
            Loop.Source->Slot35(Where);
        }
        // 0x00447439..0x0044747e: the same for the rumble.
        FObjectShipEngineSound& Rumble = RumbleSounds[Index];
        if (Rumble.Instance != kRoomObjectNoInstance && Rumble.Source != nullptr)
        {
            float const* const At = DALib::Engine->GetInstancePosition(Rumble.Instance);
            float Where[3] = {At[0], At[1], At[2]};
            Rumble.Source->Slot35(Where);
        }
        // 0x00447484..0x00447495: FUN_00446d90 on the flame effect.
        EngineFx[Index].Update(Delta);
    }

    // 0x004474b0..0x004474cc: Anim +0x6c (model, Delta).
    DALib::Anim->MixerWalk(static_cast<unsigned long>(reinterpret_cast<uintptr_t>(Instance)), Delta);
    // 0x004474cf..0x004474e0: Engine +0x6c (model, 0, Delta).
    DALib::Engine->UpdateInstance(Instance, 0, AsOpaqueDword(Delta));
    // 0x004474e3..0x004474ef: FUN_004582c0 on +0x74 while +0x14.
    if (Flag14 != 0)
        Visuals.UpdateFlashLights(Delta);
}

void* FObjectShip::GetEngineInstance()
{
    // LAB_00451360: MOV EAX,[ECX+0x70].
    return Instance;
}

void FObjectShip::DrawOpaque(ICamera* Camera)
{
    // LAB_00447500. 0x00447501..0x0044752b: [OBJECTS], SHIPS, and not LIKE_PETAL.
    if (GetGameOption(kGameOptObjectsGroup) == 0)
        return;
    if (GetGameOption(kGameOptShips) == 0)
        return;
    if (GetGameOption(kGameOptLikePetal) != 0)
        return;
    // 0x0044752d..0x00447539.
    if (Instance == kRoomObjectNoInstance)
        return;
    if (Camera == nullptr)
        return;
    // 0x0044753b..0x00447556: with +0x68, FUN_0044b370 (model, slot 4's float, 0).
    if (bShadow != 0)
    {
        float const Height = GetFloat1C();
        unsigned long HeightBits = 0;
        memcpy(&HeightBits, &Height, sizeof(HeightBits));
        RoomControlDrawShadow(Owner, static_cast<unsigned long>(reinterpret_cast<uintptr_t>(Instance)),
                              HeightBits, 0);
    }
    // 0x0044755b..0x0044755f: FUN_0044af60.
    RoomControlDrawObject(Owner, this);
}

void FObjectShip::DrawTranslucent(ICamera* Camera)
{
    // LAB_00447570: [OBJECTS], SHIPS, +0x14 -> FUN_004582b0 on +0x74.
    if (GetGameOption(kGameOptObjectsGroup) == 0)
        return;
    if (GetGameOption(kGameOptShips) == 0)
        return;
    if (Flag14 == 0)
        return;
    Visuals.DrawFlashLights(Camera);
}

void FObjectShip::DrawEffects(ICamera* Camera)
{
    // LAB_004475b0: [OBJECTS], SHIPS, [EFFECTS], PARTICLES -> FUN_005574d0 on every flame.
    if (GetGameOption(kGameOptObjectsGroup) == 0)
        return;
    if (GetGameOption(kGameOptShips) == 0)
        return;
    if (GetGameOption(kGameOptEffectsGroup) == 0)
        return;
    if (GetGameOption(kGameOptParticles) == 0)
        return;
    int Index = 0;
    for (Index = 0; Index < Visuals.Count; ++Index)
        EngineFx[Index].Particles.Draw(Camera);
}

void FObjectShip::StartEngineEffects()
{
    // FUN_00447d70. 0x00447d7d..0x00447dc8: the first item of the player's list (FUN_004c4060)
    // whose archetype is an engine.
    EquipDescList& List = GetPlayerEquipDescListRetail();
    std::list<EquipDesc>::iterator It = List.Items.begin();
    for (; It != List.Items.end(); ++It)
    {
        Archetype::Equipment* const Equip = Archetype::GetEquipment(It->get_arch_id());
        if (Equip != nullptr && Equip->get_class_type() == Archetype::AClassEngine)
            break;
    }
    if (It == List.Items.end())
        return;

    // 0x00447dd2..0x00447df4: the ship's NumExhaustNozzles (+0xfc), a cleared CacheString.
    unsigned const ArchId = It->get_arch_id();
    int const Nozzles = Ship->NumExhaustNozzles;
    CacheString HpName;
    HpName.clear();
    int Number = 0;
    for (Number = 1; Number <= Nozzles; ++Number)
    {
        // 0x00447e00..0x00447e26: "HpEngine0<n>" (FUN_00447ec0), found on the summary.
        FormatHardpointName(&HpName, "HpEngine0%d", Number);
        int const Index = Visuals.Summary->find_name(HpName.Value);
        if (Index == -1)
            continue;
        // 0x00447e28..0x00447e92: FUN_00446ca0 on +0xa0[index] with the item, -1.0f, the
        // hardpoint's instance, position and orientation.
        HardpointInfo const& Info = Visuals.Summary->get_info(Index);
        void* const HpInstance =
            reinterpret_cast<void*>(static_cast<intptr_t>(Visuals.Summary->get_instance(Index)));
        EngineFx[Index].Start(ArchId, -1.0f, HpInstance, Info.Position, Info.Orient);
    }
}

void FObjectShip::StopEngineEffects()
{
    // FUN_00447f00: slot 13 (1) on each live effect; +0x3c = 1, +0x40 = 0 on every element.
    int Index = 0;
    for (Index = 0; Index < Visuals.Count; ++Index)
    {
        FObjectShipEngineFx& Fx = EngineFx[Index];
        if (Fx.Particles.Effect != nullptr)
            Fx.Particles.Effect->Slot13(reinterpret_cast<void*>(1));
        Fx.bStopping = 1;
        Fx.Lifetime = 0.0f;
    }
}

void FObjectShip::StartEngineSound()
{
    // FUN_00447f90. Unlike StartEngineEffects, EVERY engine of the player's list is walked
    // (0x00448188..0x00448196 continues with the next node); a later engine overwrites the
    // sources of an earlier one without releasing them.
    EquipDescList& List = GetPlayerEquipDescListRetail();
    std::list<EquipDesc>::iterator It = List.Items.begin();
    for (; It != List.Items.end(); ++It)
    {
        // 0x00447fc0..0x00447fe5.
        Archetype::Equipment* const Equip = Archetype::GetEquipment(It->get_arch_id());
        if (Equip == nullptr)
            continue;
        if (Equip->get_class_type() != Archetype::AClassEngine)
            continue;

        // 0x00447feb..0x00448014: the companion, the ship's NumExhaustNozzles.
        Archetype::ICliObj* const Companion = Equip->Companion;
        int const Nozzles = Ship->NumExhaustNozzles;
        CacheString HpName;
        HpName.clear();
        int Number = 0;
        for (Number = 1; Number <= Nozzles; ++Number)
        {
            // 0x00448020..0x00448044.
            FormatHardpointName(&HpName, "HpEngine0%d", Number);
            int const Index = Visuals.Summary->find_name(HpName.Value);
            if (Index == -1)
                continue;

            // 0x0044804a..0x004480db: character_loop_sound (+0x78) on +0xa4[index] --
            // FUN_00428680 with the key {0, loop | spatial, the instance's position (Engine
            // slot 36), 0} and the element's source as the handle. Retail keeps bits 3..7 of
            // the flag byte from an unwritten stack slot (AND CL,0xf8); the port writes 0.
            FObjectShipEngineSound& Loop = LoopSounds[Index];
            float const* const LoopAt = DALib::Engine->GetInstancePosition(Loop.Instance);
            FEngineSoundKey LoopKey;
            LoopKey.Offset = 0;
            LoopKey.Flags = 3;
            LoopKey.Pos[0] = LoopAt[0];
            LoopKey.Pos[1] = LoopAt[1];
            LoopKey.Pos[2] = LoopAt[2];
            LoopKey.Field14 = 0;
            PlayClientSoundArch(LookupClientSoundArch(Archetype::CliEngineCharacterLoopSound(Companion)),
                                reinterpret_cast<unsigned*>(&LoopKey),
                                reinterpret_cast<void**>(&Loop.Source), 0);

            // 0x004480e0..0x00448167: rumble_sound (+0x60) on +0xa8[index], the same way.
            FObjectShipEngineSound& Rumble = RumbleSounds[Index];
            float const* const RumbleAt = DALib::Engine->GetInstancePosition(Rumble.Instance);
            FEngineSoundKey RumbleKey;
            RumbleKey.Offset = 0;
            RumbleKey.Flags = 3;
            RumbleKey.Pos[0] = RumbleAt[0];
            RumbleKey.Pos[1] = RumbleAt[1];
            RumbleKey.Pos[2] = RumbleAt[2];
            RumbleKey.Field14 = 0;
            PlayClientSoundArch(LookupClientSoundArch(Archetype::CliEngineRumbleSound(Companion)),
                                reinterpret_cast<unsigned*>(&RumbleKey),
                                reinterpret_cast<void**>(&Rumble.Source), 0);
        }
    }
}

void FObjectShip::StopEngineSound()
{
    // FUN_004481b0: slot 34 (+0x88) with 0 on each source of +0xa4 and +0xa8.
    int Index = 0;
    for (Index = 0; Index < Visuals.Count; ++Index)
    {
        if (LoopSounds[Index].Source != nullptr)
            LoopSounds[Index].Source->SetLooping(0);
        if (RumbleSounds[Index].Source != nullptr)
            RumbleSounds[Index].Source->SetLooping(0);
    }
}

void FObjectShip::SetEngineSoundAttenuation(float Attenuation)
{
    // FUN_00448210: slot 32 (+0x80) with the value on each source of +0xa4 and +0xa8.
    int Index = 0;
    for (Index = 0; Index < Visuals.Count; ++Index)
    {
        if (LoopSounds[Index].Source != nullptr)
            LoopSounds[Index].Source->SetAttenuation(Attenuation);
        if (RumbleSounds[Index].Source != nullptr)
            RumbleSounds[Index].Source->SetAttenuation(Attenuation);
    }
}

void FObjectShip::SetEquipment(EquipDescList const* List)
{
    // FUN_00448540. 0x0044855c..0x0044859d: the old list's nodes erased, its head and the
    // list freed.
    if (Equipment != nullptr)
        delete Equipment;
    // 0x004485a0..0x00448668: operator new(0xc) with a fresh head (+0x6c), then the VC6
    // element-wise assign from List (existing nodes overwritten, surplus erased, the rest
    // inserted) -- std::list assignment.
    Equipment = new EquipDescList();
    Equipment->Items = List->Items;
}

void FObjectShip::SetShipArchetype(unsigned ArchId)
{
    // FUN_00448680: +0x30 = Archetype::GetShip(ArchId).
    Ship = Archetype::GetShip(ArchId);
}

void FObjectShip::MountEquipment(int bCombined, unsigned ArchId, unsigned short /*Id*/,
                                 CacheString const* Hardpoint, int /*Count*/, float /*Health*/)
{
    // FUN_004477d0. 0x004477d0..0x004477e1: the cargo bay, by pointer.
    if (Hardpoint->Value == EquipDesc::CARGO_BAY_HP_NAME.Value)
        return;
    // 0x004477e3..0x0044781c.
    int const Index = Visuals.Summary->find_name(Hardpoint->Value);
    if (Index == -1)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: missing hardpoint (%s)", kObjectShipCpp, 0x275,
              Hardpoint->Value);
        return;
    }
    // 0x0044781f..0x00447839: an item that was not folded into an existing one.
    if (bCombined == 0)
        Visuals.Attach(Index, Archetype::GetEquipment(ArchId));
}

void FObjectShip::UnmountEquipment(int RemoveResult, unsigned /*Arg2*/, unsigned /*Arg3*/,
                                   CacheString const* Hardpoint, unsigned /*Arg5*/)
{
    // FUN_00447850. 0x00447850..0x00447862.
    if (Hardpoint->Value == EquipDesc::CARGO_BAY_HP_NAME.Value)
        return;
    // 0x00447864..0x0044789a.
    int const Index = Visuals.Summary->find_name(Hardpoint->Value);
    if (Index == -1)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: missing hardpoint (%s)", kObjectShipCpp, 0x295,
              Hardpoint->Value);
        return;
    }
    // 0x0044789d..0x004478a8: only when the item was erased.
    if (RemoveResult == 0)
        Visuals.Detach(Index);
}

void FObjectShip::PlayLaunchScript()
{
    // FUN_00447cd0. 0x00447cd7..0x00447cdc, 0x00447d5d..0x00447d62: no launch script -> JMP
    // FUN_00558a30.
    if (LaunchScript == nullptr)
    {
        RequestGameflowMissionBriefing();
        return;
    }
    // 0x00447cde..0x00447d11: a sequence for the room (FUN_00456540(owner, 0)), the script
    // added with an identity frame (Csys ctor + set_identity) and no engine (FUN_00454eb0).
    FGfScriptSequence* const Sequence = G_GfScriptManager.CreateSequence(Owner, 0);
    Csys Frame;
    Frame.set_identity();
    Sequence->AddScript(LaunchScript, 0, &Frame, nullptr);
    // 0x00447d16..0x00447d40.
    Sequence->BindMode = 0;
    Sequence->bLetterbox = 1;
    Sequence->bFade = 1;
    Sequence->bApplySpatial = 1;
    Sequence->bRestoreCamera = 0;
    Sequence->Priority = 1;
    Sequence->Field_0x16 = 1;
    Sequence->Callback = &FObjectShip::OnScriptStopped;
    Sequence->CallbackA = this;
    Sequence->CallbackB = nullptr;
    // 0x00447d47: FUN_00454f80.
    Sequence->Play();
    // 0x00447d4d..0x00447d58: +0xac = 1, JMP FUN_00558c00.
    ScriptState = 1;
    RequestGameflowStateFive();
}

void FObjectShip::PlayLandingScript(char const* Script)
{
    // FUN_00447c30. 0x00447c35..0x00447c3c: FUN_00412070(1.0f).
    SetFadeImmediate(1.0f);
    // 0x00447c41..0x00447c52: FUN_00412050(0, 1.0 / DAT_0061650c) -- FDIVR in double, stored
    // as a float over the argument slot.
    SetFadeTarget(0.0f, static_cast<float>(1.0 / static_cast<double>(GetFadeDuration())));
    // 0x00447c57..0x00447c86: the sequence, the script with an identity frame.
    FGfScriptSequence* const Sequence = G_GfScriptManager.CreateSequence(Owner, 0);
    Csys Frame;
    Frame.set_identity();
    Sequence->AddScript(Script, 0, &Frame, nullptr);
    // 0x00447c8b..0x00447cb5.
    Sequence->BindMode = 0;
    Sequence->bLetterbox = 1;
    Sequence->bApplySpatial = 1;
    Sequence->bRestoreCamera = 1;
    Sequence->Priority = 1;
    Sequence->Callback = &FObjectShip::OnScriptStopped;
    Sequence->CallbackA = this;
    Sequence->CallbackB = nullptr;
    Sequence->bPropsToRoom = 1;
    // 0x00447cb8..0x00447cbd: FUN_00454f80, +0xac = 2.
    Sequence->Play();
    ScriptState = 2;
}

void __cdecl FObjectShip::OnScriptStopped(void* ShipPtr, void* /*Unused*/, unsigned char /*bArg*/)
{
    // LAB_00448270.
    FObjectShip* const Ship = static_cast<FObjectShip*>(ShipPtr);
    if (Ship->ScriptState == 0)
        return;
    if (Ship->ScriptState == 1)
    {
        // 0x00448283: JMP FUN_00558a30 (+0xac is left at 1).
        RequestGameflowMissionBriefing();
        return;
    }
    if (Ship->ScriptState == 2)
        Ship->ScriptState = 0;
}

// ---------------------------------------------------------------------------------------
// DealerShipObject
// ---------------------------------------------------------------------------------------

FDealerShipDesc::FDealerShipDesc()
    : Ship(nullptr), bShadow(0), Height(0.0f), Loadout(nullptr)
{
    // FUN_00443770: +0x00 = 0, +0x04..+0x0c = 0, the orientation identity (+0x10 / +0x20 /
    // +0x30 = 1.0f), +0x34 = 0, +0x38 = 0, +0x3c = 0 (the three stack stores are dead).
    Pos.x = 0.0f;
    Pos.y = 0.0f;
    Pos.z = 0.0f;
    MatrixSetIdentity(Orient);
}

FDealerShipObject::FDealerShipObject(FRoomRuntime* InOwner, FDealerShipDesc const& Desc)
    : FRoomObject(InOwner), Instance(kRoomObjectNoInstance), bShadow(0)
{
    // FUN_004437d0. 0x004437f3: FUN_005593d0 (the base); 0x00443805: FUN_00457d60 on +0x38 (the
    // member); 0x00443817..0x0044381e: +0x30 = -1, +0x34 = 0; 0x00443822: FUN_00457e20.
    Visuals.ResetFields();
    // 0x0044382e: FUN_00443920.
    Load(Desc);
}

FDealerShipObject::~FDealerShipObject()
{
    // FUN_004438a0. 0x004438d1: FUN_00457e30 on +0x38.
    Visuals.Clear();
    // 0x004438d6..0x004438ea: Engine slot 24 on a live model.
    if (Instance != kRoomObjectNoInstance)
        DALib::Engine->DestroyInstance(Instance);
    // 0x004438f4 / 0x00443903: FUN_00457dc0 and FUN_005594b0 -- the member and base dtors.
}

bool FDealerShipObject::IsVisible()
{
    // FUN_0044fd00: XOR AL,AL.
    return false;
}

void FDealerShipObject::Load(FDealerShipDesc const& Desc)
{
    // FUN_00443920. 0x00443928..0x00443931: +0x34 = the byte.
    bShadow = Desc.bShadow;
    // 0x00443934..0x00443942: slot 3 with Height - 1.6f (FSUB float DAT_005cb908, FSTP float).
    SetFloat1C(static_cast<float>(static_cast<double>(Desc.Height) - static_cast<double>(1.6f)));

    // 0x00443945..0x00443982: FUN_0041c540 on the ship's mesh with the companion's material
    // list, the material entries into RoomObject +0x20 and the handle into a local (-1 first).
    Archetype::ICliObj* const Companion = Desc.Ship->Companion;
    long Handle = -1;
    Instance = MiscUtilLoadEngineInstance(Desc.Ship->DaArchetype.Value, DALib::Data, nullptr,
                                          nullptr, Archetype::CliObjMaterialPaths(Companion),
                                          &MaterialEntries, &Handle);
    if (Instance != kRoomObjectNoInstance)
    {
        // 0x00443984..0x004439b0: FUN_00448c40 with {Pos, Orient} as a Csys.
        Csys Frame;
        Frame.Pos = Desc.Pos;
        Frame.Orient = Desc.Orient;
        GfShipPlaceOnMount(Instance, &Frame);
        // 0x004439b5..0x004439c9: +0x13 = the companion's envmap material is not empty.
        bEnvMap = Archetype::CliObjEnvmapMaterial(Companion)[0] != '\0' ? 1 : 0;
        // 0x004439cc..0x004439dd: FUN_00457f10 with the model, FUN_004581b0 with the loadout
        // (no "ship_loadouts" test and no flashlights here, unlike the player's ship).
        Visuals.Populate(static_cast<long>(reinterpret_cast<intptr_t>(Instance)));
        Visuals.AttachAll(Desc.Loadout);
    }
    // 0x004439e7..0x004439fc: +0x15 = 3, +0x16 = 0, +0x18 = 0, FUN_005595a0 (companion,
    // handle) on both paths.
    LightGroup = 3;
    Layer = 0;
    RenderMode = 0;
    RecomputeShipCompanionLod(Companion, Handle);
}

void FDealerShipObject::Update(double DeltaSeconds)
{
    // LAB_00443a10: Engine slot 27 (+0x30, 0, (float)dt). No option test.
    DALib::Engine->UpdateInstance(Instance, 0, AsOpaqueDword(static_cast<float>(DeltaSeconds)));
}

void* FDealerShipObject::GetEngineInstance()
{
    // LAB_00443870: MOV EAX,[ECX+0x30].
    return Instance;
}

void FDealerShipObject::DrawOpaque(ICamera* /*Camera*/)
{
    // FUN_00443a30. No option, model or camera test. +0x34 -> FUN_0044b370 (model, slot 4's
    // float, 0).
    if (bShadow != 0)
    {
        float const Height = GetFloat1C();
        unsigned long HeightBits = 0;
        memcpy(&HeightBits, &Height, sizeof(HeightBits));
        RoomControlDrawShadow(Owner, static_cast<unsigned long>(reinterpret_cast<uintptr_t>(Instance)),
                              HeightBits, 0);
    }
    // 0x00443a51..0x00443a55: FUN_0044af60.
    RoomControlDrawObject(Owner, this);
}

// ---------------------------------------------------------------------------------------
// Free functions
// ---------------------------------------------------------------------------------------

void GfShipApplyCollisionDamage(void* InstancePtr)
{
    // FUN_004482b0. 0x004482cc..0x004482da: the player's ship archetype (FUN_004c3e10).
    long const Instance = static_cast<long>(reinterpret_cast<intptr_t>(InstancePtr));
    Archetype::Ship* const Ship = Archetype::GetShip(GetPlayerShipArchetypeIdRetail());
    // 0x004482de..0x00448309: the player's collision groups (FUN_004c3f00) and every instance
    // of the compound.
    std::list<CollisionGroupDesc>& Groups = GetPlayerCollisionGroupListRetail();
    int PartCount = 0;
    long* Parts = nullptr;
    CompoundInstanceList(Instance, &PartCount, &Parts);

    // 0x00448320..0x004483c1: a destroyed group (health == 0.0f: TEST AH,0x44 + JP skips the
    // not-equal and unordered cases) removes every part whose engine name (Engine slot 26, ""
    // for none) is the group's obj (DACOM stricmp) -- Engine slot 24, the slot set to -1.
    std::list<CollisionGroupDesc>::iterator It = Groups.begin();
    for (; It != Groups.end(); ++It)
    {
        if (!(It->Health == 0.0f))
            continue;
        int PartIndex = 0;
        for (PartIndex = 0; PartIndex < PartCount; ++PartIndex)
        {
            void* const Part = reinterpret_cast<void*>(static_cast<intptr_t>(Parts[PartIndex]));
            FEngineName NameOut;
            char const* Name = DALib::Engine->GetInstanceName(&NameOut, Part)->Text;
            if (Name == nullptr)
                Name = "";
            Archetype::CollisionGroup const* const Group = Ship->get_group_by_id(It->Id);
            if (Name == nullptr || Group == nullptr)
                continue;
            if (_stricmp(Name, Group->ObjName) != 0) // DACOM stricmp
                continue;
            DALib::Engine->DestroyInstance(Part);
            Parts[PartIndex] = -1;
        }
    }

    // 0x004483cb..0x004483e6: a HardpointSummary of the model.
    HardpointSummary Summary;
    Summary.analyze_instance(Instance);
    // 0x00448406..0x004484e5: each destroyed group's dmg_obj meshes: the archetype
    // (GetSimple) loaded through FUN_0041c540 (no scripts, entries released), its
    // "DpConnect" connected to the dmg_hp hardpoint (Hardpoint slot 6 on the summary's
    // instance of that name, unchecked).
    for (It = Groups.begin(); It != Groups.end(); ++It)
    {
        if (!(It->Health == 0.0f))
            continue;
        Archetype::CollisionGroup const* const Group = Ship->get_group_by_id(It->Id);
        std::vector<Archetype::DamageObjInfo>::const_iterator Dmg = Group->DmgObjs.Items->begin();
        for (; Dmg != Group->DmgObjs.Items->end(); ++Dmg)
        {
            char const* const HpName = Dmg->Hardpoint.Value;
            Archetype::Root* const Arch = Archetype::GetSimple(Dmg->Arch.Id);
            void* const Child = MiscUtilLoadEngineInstance(
                Arch->DaArchetype.Value, DALib::Data, nullptr, nullptr,
                Archetype::CliObjMaterialPaths(Arch->Companion), nullptr, nullptr);
            FHardpointKey ChildKey;
            ChildKey.Name = "DpConnect";
            ChildKey.Crc = DACOM_CRC::GetCRC32("DpConnect");
            FHardpointKey ParentKey;
            ParentKey.Name = HpName;
            ParentKey.Crc = DACOM_CRC::GetCRC32(HpName);
            long const Parent = Summary.find_name_instance(HpName);
            DALib::Hardpoint->ConnectHardpoints(reinterpret_cast<void*>(static_cast<intptr_t>(Parent)),
                                                &ParentKey, Child, &ChildKey);
        }
    }

    // 0x004484eb..0x00448518: destroy, the instance array freed, ~HardpointSummary.
    Summary.destroy();
    ::operator delete(Parts);
}

void GfShipPlaceOnMount(void* Instance, Csys const* Frame)
{
    // FUN_00448c40. 0x00448c46..0x00448c90: the model's archetype (Engine slot 25) asked for
    // "HpMount" (Hardpoint slot 3); AL clear -> position 0, orientation identity.
    void* const ArchHandle = DALib::Engine->GetInstanceArchetype(Instance);
    FHardpointKey Key;
    Key.Name = "HpMount";
    Key.Crc = DACOM_CRC::GetCRC32("HpMount");
    HardpointInfo Mount;
    if ((DALib::Hardpoint->GetHardpoint(ArchHandle, &Key, &Mount) & 0xffu) == 0)
    {
        Mount.Position.x = 0.0f;
        Mount.Position.y = 0.0f;
        Mount.Position.z = 0.0f;
        MatrixSetIdentity(Mount.Orient);
    }
    // 0x00448d0f..0x00448d1a: Engine slot 15 on the archetype handle.
    DALib::Engine->ReleaseHandle(ArchHandle);

    // 0x00448d1d..0x00448dd1: the mount's inverse frame.
    FMatrix MountRotation;
    ToFMatrix(MountRotation, Mount.Orient);
    FVector MountPos;
    ToFVector(MountPos, Mount.Position);
    FTransform Inverse;
    ShipInverseFrame(Inverse, MountRotation, MountPos);

    // 0x00448dd6..0x00448e39: DAT_00668aac MulTransform (Frame as a transform, the inverse).
    FTransform FrameXform;
    ToFMatrix(FrameXform.Rotation, Frame->Orient);
    ToFVector(FrameXform.Translation, Frame->Pos);
    FTransform Placed;
    ObjectShipMountMath()->MulTransform(Placed, FrameXform, Inverse);

    // 0x00448e3c..0x00448e79: Engine slot 39 with a copy, then slot 27 (model, 0, 0).
    FTransform Copy = Placed;
    DALib::Engine->SetInstanceMatrix(Instance, &Copy);
    DALib::Engine->UpdateInstance(Instance, 0, nullptr);
}

Csys GfShipMountOffset(HardpointSummary const* Summary, char const* HpName, void* RootInstance)
{
    // FUN_004478c0. 0x004478c0..0x0044791c: unknown hardpoint -> {0, identity (FUN_004215b0)}.
    Csys Out;
    int const Index = Summary->find_name(HpName);
    if (Index == -1)
    {
        Out.Pos.x = 0.0f;
        Out.Pos.y = 0.0f;
        Out.Pos.z = 0.0f;
        MatrixSetIdentity(Out.Orient);
        return Out;
    }

    // 0x00447921..0x0044797b: the hardpoint's instance and its 0x48-byte record; its local
    // frame {orientation, position} kept.
    void* const HpInstance =
        reinterpret_cast<void*>(static_cast<intptr_t>(Summary->get_instance(Index)));
    HardpointInfo const Info = Summary->get_info(Index);
    FMatrix LocalRotation;
    ToFMatrix(LocalRotation, Info.Orient);
    FVector LocalPos;
    ToFVector(LocalPos, Info.Position);

    // 0x0044797f..0x004479a5: Engine slot 40 on the hardpoint's instance, then on the root.
    FTransform const* const HpXform =
        reinterpret_cast<FTransform const*>(DALib::Engine->GetInstanceTransform(HpInstance));
    FTransform const* const RootXform =
        reinterpret_cast<FTransform const*>(DALib::Engine->GetInstanceTransform(RootInstance));

    // 0x004479a7..0x00447a4f: the instance transform's inverse, times the root's
    // (DAT_00668a98 MulTransform(Out, inverse, root)), copied (FUN_00406880).
    FTransform HpInverse;
    ShipInverseFrame(HpInverse, HpXform->Rotation, HpXform->Translation);
    FTransform Relative;
    ObjectShipMath()->MulTransform(Relative, HpInverse, *RootXform);
    FTransform RelativeCopy = Relative;

    // 0x00447a54..0x00447aff: the local frame's inverse, times that.
    FTransform LocalInverse;
    ShipInverseFrame(LocalInverse, LocalRotation, LocalPos);
    FTransform Result;
    ObjectShipMath()->MulTransform(Result, LocalInverse, RelativeCopy);

    // 0x00447b02..0x00447b47: {translation, rotation} out as a Csys.
    Out.Pos.x = Result.Translation.X;
    Out.Pos.y = Result.Translation.Y;
    Out.Pos.z = Result.Translation.Z;
    memcpy(&Out.Orient, &Result.Rotation, sizeof(Out.Orient));
    return Out;
}
