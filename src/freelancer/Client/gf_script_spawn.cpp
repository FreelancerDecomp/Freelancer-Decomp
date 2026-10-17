// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Gf script spawn -- E:\FL\Scratch\Source\Client\Gf\Script.cpp, freelancer.exe
// 0x0044d630..0x0044f238: FGfScript::Spawn (FUN_0044ed90) and the per-entity factories it
// switches to. Every factory is thiscall on the script; each concrete Thorn object is built
// with (script frame = the script's +0x00, owner = the script, entity snapshot), which is the
// `PUSH snap; PUSH ESI; PUSH ESI` shape of every constructor call below.

#include "gf_script_object.h"
#include "msvcrt_compat.h" // MsvcrtAtoi: retail MSVCRT.DLL wraps mod 2^32, the UCRT saturates

#include "common_csys.h"
#include "common_fuses.h"   // PetalInterfaceDatabase::get_category
#include "common_goods.h"   // ThornGetUser*, ThornGetRenderType, RoomData, SetpointProperties
#include "common_misc.h"    // malloc_strcpy
#include "common_newchar.h" // RtcSlider::is_enabled
#include "dacom.h"
#include "gf_character_object.h"
#include "gf_object_ship.h"
#include "gf_thorn_camera.h"
#include "gf_thorn_component.h"
#include "gf_thorn_compound.h"
#include "gf_thorn_deformable.h"
#include "gf_thorn_light.h"
#include "gf_thorn_marker.h"
#include "gf_thorn_psys.h"
#include "gf_thorn_scene.h"
#include "gf_thorn_sound.h"
#include "nn_arrival_text.h"
#include "object_prop.h"
#include "room_control.h"
#include "thorn_iface.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    // 0x005cc32c -- the file name every FDUMP of this unit passes.
    char const* const GScriptFile = "E:\\FL\\Scratch\\Source\\Client\\Gf\\Script.cpp";

    FThornEntitySnap const* EntitySnap(IScriptEngine* Engine, int Index)
    {
        return static_cast<FThornEntitySnap const*>(Engine->Slot9Entity(Index));
    }

    // The geo facet (+0x0c) slots 3 / 4 take non-const pointers; retail hands them the
    // snapshot's / a local's floats directly.
    void SetGeoPose(FThornComponent* Object, float const* Pos3, float const* Orient9)
    {
        Object->SetPosition(const_cast<float*>(Pos3));
        Object->SetOrientation(const_cast<float*>(Orient9));
    }

}

// FUN_0044ed90 (thiscall, no arguments).
//   0x0044ed98..0x0044edd1: Count = engine slot 6; +0x48 = operator new(Count * 4), +0x4c =
//     Count, every entry zeroed.
//   Per entity (0x0044ede3..0x0044f06c): the snapshot (slot 9); bit 0 of +0x04 the reference
//     flag, bit 1 / bit 2 two of the four SetFlags bytes; the fog byte 1 unless "nofog" is set,
//     the running-lights byte 0 unless "running_lights" is set. A "priority" the RtcSlider does
//     not enable skips the entity (0x0044ee72 -> 0x0044f065, the entry stays null). The type
//     switch (0x0044ee8f, table 0x0044f23c) picks the factory; a built object gets identity slot
//     20 (0x0044f05c) and lands in the table.
//   0x0044f078..0x0044f0a0: every object gets slot 19 (the last scene built) and slot 15 (the
//     table and its count).
//   0x0044f0a2..0x0044f0c9: engine slot 12 binds each object to its entity; +0x24 = its index.
//   0x0044f0cb..0x0044f22b: the initial writes, skipped for a background sound (slot 22) and for
//     a reference entity: a CAMERA's fov / near / far (camera facet 3 / 6 / 7), a SOUND's
//     audio props (sound facet 9, mask -1); with +0x56 the pose (geo facet 3 / 4) of COMPOUND,
//     DEFORMABLE, CAMERA, SOUND and PSYS, and a DEFORMABLE's floor height (identity slot 5);
//     then the "special/" marker and "camera/" camera remaps.
void FGfScript::Spawn()
{
    int const Count = static_cast<int>(Engine->Slot6EntityCount());
    Spawned = new FThornComponent*[Count];
    SpawnedCount = Count;
    int Index = 0;
    for (Index = 0; Index < Count; ++Index)
        Spawned[Index] = nullptr;

    FThornComponent* LastScene = nullptr;
    for (Index = 0; Index < Count; ++Index)
    {
        FThornEntitySnap const* const Snap = EntitySnap(Engine, Index);
        unsigned char const bReference = static_cast<unsigned char>(Snap->Flags & 1);
        unsigned char const bFlagBit1 = static_cast<unsigned char>((Snap->Flags >> 1) & 1);
        unsigned char const bFlagBit2 = static_cast<unsigned char>((Snap->Flags >> 2) & 1);
        unsigned char bFog = 1;
        if (ThornGetUserString(Engine, Index, "nofog") != nullptr)
            bFog = 0;
        unsigned char bRunningLights = 0;
        if (ThornGetUserString(Engine, Index, "running_lights") != nullptr)
            bRunningLights = 1;
        char const* const PriorityName = ThornGetUserString(Engine, Index, "priority");
        if (PriorityName != nullptr && !RtcSlider::is_enabled(PriorityName))
            continue;

        FThornComponent* Object = nullptr;
        switch (Snap->Type)
        {
        case kThornEntityScene:
        {
            // 0x0044ee96..0x0044eedd: both user values found -> FUN_0045b430(TextStart,
            // TextString).
            float TextStart = 0.0f;
            int TextString = 0;
            bool const bHasStart = ThornGetUserFloat(Engine, Index, "TextStart", &TextStart);
            bool const bHasString = ThornGetUserInt(Engine, Index, "TextString", &TextString);
            if (bHasStart && bHasString)
                NnArrivalTextShowRaw(TextStart, static_cast<uintptr_t>(TextString));
            // 0x0044eee5..0x0044ef06: the scene's "priority" (read again) is the script's.
            char const* const ScenePriority = ThornGetUserString(Engine, Index, "priority");
            if (ScenePriority != nullptr)
                Priority = MsvcrtAtoi(ScenePriority);
            Object = SpawnScene(Snap);
            LastScene = Object; // 0x0044ef12, null or not
            break;
        }
        case kThornEntityMarker:
            Object = SpawnMarker(Snap);
            break;
        case kThornEntitySound:
            if (bReference != 0)
                Object = SpawnSoundReference(Snap);
            else if (_stricmp(Snap->TemplateName, "PlayerShipSound") != 0)
                Object = SpawnSound(Index, Snap);
            else
                Object = SpawnPlayerEngineSound(Snap);
            break;
        case kThornEntityDeformable:
            Object = SpawnDeformable(Index, Snap);
            break;
        case kThornEntityCompound:
            Object = SpawnCompound(Index, Snap);
            break;
        case kThornEntityMonitor:
            Object = SpawnMonitor(Snap);
            break;
        case kThornEntityCamera:
            Object = bReference != 0 ? SpawnCameraReference(Snap) : SpawnCamera(Snap);
            break;
        case kThornEntityLight:
            Object = bReference != 0 ? SpawnLightReference(Snap) : SpawnLight(Snap);
            break;
        case kThornEntityPsys:
        {
            // 0x0044efd3: without a "priority" the "script_particles" slider decides.
            if (PriorityName == nullptr && !RtcSlider::is_enabled("script_particles"))
                break;
            char const* const Template = Snap->TemplateName;
            if (_stricmp(Template, "PlayerShipEngines") == 0)
            {
                Object = SpawnPlayerEnginePsys(Snap);
                break;
            }
            // 0x0044f014..0x0044f033: "PlayerShipHover" only while the Gf ship exists.
            if (_stricmp(Template, "PlayerShipHover") == 0 && GfObjectShip() == nullptr)
                break;
            Object = SpawnPsys(Snap);
            break;
        }
        default:
            // 8 (hardpoint), 10, 11, 12 and anything else: no object (caseD_8).
            break;
        }

        if (Object != nullptr)
            Object->SetFlags(bFlagBit1, bFlagBit2, bFog, bRunningLights); // 0x0044f05c slot 20
        Spawned[Index] = Object;
    }

    for (Index = 0; Index < Count; ++Index)
    {
        if (Spawned[Index] == nullptr)
            continue;
        Spawned[Index]->SetScene(LastScene);         // 0x0044f08d slot 19
        Spawned[Index]->SetSpawnTable(Spawned, Count); // 0x0044f09a slot 15
    }

    for (Index = 0; Index < Count; ++Index)
    {
        if (Spawned[Index] == nullptr)
            continue;
        // 0x0044f0ba: engine slot 12 (entity index, the object's identity pointer).
        Engine->Slot12Attach(Index, static_cast<ICompoundControl*>(Spawned[Index]));
        Spawned[Index]->EntityIndex = Index; // 0x0044f0c3
    }

    for (Index = 0; Index < Count; ++Index)
    {
        FThornComponent* const Object = Spawned[Index];
        if (Object == nullptr)
            continue;
        FThornEntitySnap const* const Snap = EntitySnap(Engine, Index);
        unsigned char const bReference = static_cast<unsigned char>(Snap->Flags & 1);
        if (Object->IsBackgroundSound() || bReference != 0)
            continue;

        int const Type = Snap->Type;
        if (Type == kThornEntityCamera)
        {
            Object->SetFov(&Snap->Fovh);       // camera facet slot 3
            Object->SetNear(&Snap->NearPlane); // slot 6
            Object->SetFar(&Snap->FarPlane);   // slot 7
        }
        else if (Type == kThornEntitySound)
        {
            Object->SetAudioProps(Snap->Audio, -1); // sound facet slot 9
        }

        if (bApplySpatial != 0)
        {
            if (Type == kThornEntityCompound || Type == kThornEntityDeformable ||
                Type == kThornEntityCamera || Type == kThornEntitySound || Type == kThornEntityPsys)
            {
                SetGeoPose(Object, Snap->Pos, Snap->Orient);
            }
            if (Type == kThornEntityDeformable)
                Object->SetFloorHeight(Snap->FloorHeight); // identity slot 5
        }

        if (Type == kThornEntityMarker)
        {
            ApplySpecialMarker(Snap, Object);
        }
        else if (Type == kThornEntityCamera)
        {
            // 0x0044f1f2..0x0044f20f: a stack copy of the four camera floats.
            float CameraProps[4];
            CameraProps[0] = Snap->Fovh;
            CameraProps[1] = Snap->NearPlane;
            CameraProps[2] = Snap->FarPlane;
            CameraProps[3] = Snap->Aspect;
            ApplySpecialCamera(Snap, Object, CameraProps);
        }
    }
}

// FUN_0044d630 (thiscall, RET 8) -- operator new(0x78), Thorn_Scene ctor FUN_00454390. The index
// argument is not read.
FThornComponent* FGfScript::SpawnScene(FThornEntitySnap const* Snap)
{
    return new FThorn_Scene(Frame, this, Snap);
}

// FUN_0044d6a0 (thiscall, RET 8). A marker named "special/<segment>" is moved to the room's
// setpoint the script's own name maps to with that segment (SetpointProperties
// extract_from_name(+0xa0) / set_segment / combine_to_name, RoomData::find_setpoint_generic
// with +0xa4), in the script frame (Csys::trans_to_frame). "special/p" also hands the player
// character (FUN_00449cd0) the local height (RoomObject slot 3 with the frame's y, 0x0044d7a5).
// No setpoint: the retail FDUMP (line 0x80).
void FGfScript::ApplySpecialMarker(FThornEntitySnap const* Snap, FThornComponent* Obj)
{
    char const* const EntityName = Snap->Name;
    size_t const PrefixLength = strlen("special/");
    if (_strnicmp(EntityName, "special/", PrefixLength) != 0)
        return;
    char const* const Segment = EntityName + PrefixLength;

    SetpointProperties Props;
    Props.extract_from_name(Name);
    Props.set_segment(Segment);
    char const* const Mapped = Props.combine_to_name();
    SetpointInfo const* const Info =
        GetRoomRuntimeRoomData(Room)->find_setpoint_generic(Mapped, ScriptFile);
    if (Info == nullptr)
    {
        FDUMP(0x100001,
              "%s(%d) : *** ERROR: Generic script %s applied on trigger %s uses special marker %s "
              "mapping to %s which not exist in setpoint layer\n",
              GScriptFile, 0x80, ScriptFile, Name, EntityName, Mapped);
        return;
    }

    Csys const Local = Frame.trans_to_frame(Info->Frame);
    SetGeoPose(Obj, &Local.Pos.x, &Local.Orient.E[0][0]);
    if (_stricmp(EntityName, "special/p") != 0)
        return;
    FCharacterObject* const Player = RoomControlPlayerCharacter(Room);
    if (Player != nullptr)
        Player->SetFloat1C(Local.Pos.y);
}

// FUN_0044d810 (thiscall, RET 8).
//   Not a reference: operator new(0x9c) Thorn_Marker, posed from the snapshot.
//   A reference: a created MARKER of that name in a running script (FUN_00456790 with 7) ->
//     operator new(0x70) Thorn_MarkerReference, +0x6c = it. Else a Thorn_Marker posed at the
//     room setpoint of that name (RoomData::find_setpoint, in the script frame), or at the
//     snapshot when the room has none.
FThornComponent* FGfScript::SpawnMarker(FThornEntitySnap const* Snap)
{
    if ((Snap->Flags & 1) == 0)
    {
        FThornMarker* const Marker = new FThornMarker(Frame, this, Snap);
        SetGeoPose(Marker, Snap->Pos, Snap->Orient);
        return Marker;
    }

    FThornComponent* const Found = G_GfScriptManager.FindCreatedEntity(kThornEntityMarker, Snap->Name);
    if (Found != nullptr)
    {
        FThornMarkerReference* const Reference = new FThornMarkerReference(Frame, this, Snap);
        Reference->Target = Found;
        return Reference;
    }

    FThornMarker* const Marker = new FThornMarker(Frame, this, Snap);
    SetpointInfo const* const Info =
        GetRoomRuntimeRoomData(Room)->find_setpoint(Snap->Name, ScriptFile);
    if (Info != nullptr)
    {
        Csys const Local = Frame.trans_to_frame(Info->Frame);
        SetGeoPose(Marker, &Local.Pos.x, &Local.Orient.E[0][0]);
    }
    else
    {
        SetGeoPose(Marker, Snap->Pos, Snap->Orient);
    }
    return Marker;
}

// FUN_0044d9b0 (thiscall, RET 8) -- a MONITOR: no object for a reference; else operator new(0x70)
// Thorn_Camera registered on the room camera (Room +0x500) at the script's priority, allowed to
// become the active camera (FUN_00450830 with 1).
FThornComponent* FGfScript::SpawnMonitor(FThornEntitySnap const* Snap)
{
    if ((Snap->Flags & 1) != 0)
        return nullptr;
    FThornCamera* const Camera = new FThornCamera(Frame, this, Snap);
    Camera->Register(GetRoomRuntimeRoomCamera(Room), Priority, 1);
    return Camera;
}

// FUN_0044da40 (thiscall, RET 0xc). A camera named "camera/<segment>" is moved to the room
// camera the script's own name maps to with that segment (RoomData::find_camera_generic), in
// the script frame, and takes CameraProps' fov / near / far (camera facet 3 / 6 / 7). No such
// camera: the retail FDUMP (line 0x121).
void FGfScript::ApplySpecialCamera(FThornEntitySnap const* Snap, FThornComponent* Obj,
                                   float const* CameraProps4)
{
    char const* const EntityName = Snap->Name;
    size_t const PrefixLength = strlen("camera/");
    if (_strnicmp(EntityName, "camera/", PrefixLength) != 0)
        return;
    char const* const Segment = EntityName + PrefixLength;

    SetpointProperties Props;
    Props.extract_from_name(Name);
    Props.set_segment(Segment);
    char const* const Mapped = Props.combine_to_name();
    RoomCameraInfo const* const Info =
        GetRoomRuntimeRoomData(Room)->find_camera_generic(Mapped, ScriptFile);
    if (Info == nullptr)
    {
        FDUMP(0x100001,
              "%s(%d) : *** ERROR: Cannot find remapped special camera %s implied by special "
              "camera name %s and character hardpoint name %s\n",
              GScriptFile, 0x121, Mapped, EntityName, Name);
        return;
    }

    Csys const Local = Frame.trans_to_frame(Info->Frame);
    SetGeoPose(Obj, &Local.Pos.x, &Local.Orient.E[0][0]);
    Obj->SetFov(&CameraProps4[0]);
    Obj->SetNear(&CameraProps4[1]);
    Obj->SetFar(&CameraProps4[2]);
}

// FUN_0044dba0 (thiscall, RET 8) -- operator new(0x70) Thorn_Camera registered on the room camera
// at the script's priority, never the active one by itself (FUN_00450830 with 0).
FThornComponent* FGfScript::SpawnCamera(FThornEntitySnap const* Snap)
{
    FThornCamera* const Camera = new FThornCamera(Frame, this, Snap);
    Camera->Register(GetRoomRuntimeRoomCamera(Room), Priority, 0);
    return Camera;
}

// FUN_0044dc20 (thiscall, RET 8).
//   A created CAMERA of that name in a running script (FUN_00456790 with 3) -> operator
//     new(0x70) Thorn_CameraReference, +0x6c = it.
//   Else the room camera of that name (RoomData::find_camera with +0xa4): a Thorn_Camera
//     registered like FUN_0044dba0, posed at the room camera's frame as it is (no script-frame
//     transform, 0x0044dd06..0x0044dd22), with the snapshot's fov / near / far.
//   Neither: a Thorn_CameraReference with no target.
FThornComponent* FGfScript::SpawnCameraReference(FThornEntitySnap const* Snap)
{
    FThornComponent* const Found = G_GfScriptManager.FindCreatedEntity(kThornEntityCamera, Snap->Name);
    if (Found != nullptr)
    {
        FThornCameraReference* const Reference = new FThornCameraReference(Frame, this, Snap);
        Reference->Target = Found;
        return Reference;
    }

    RoomCameraInfo const* const Info =
        GetRoomRuntimeRoomData(Room)->find_camera(Snap->Name, ScriptFile);
    if (Info == nullptr)
    {
        FThornCameraReference* const Reference = new FThornCameraReference(Frame, this, Snap);
        Reference->Target = nullptr;
        return Reference;
    }

    FThornCamera* const Camera = new FThornCamera(Frame, this, Snap);
    Camera->Register(GetRoomRuntimeRoomCamera(Room), Priority, 0);
    // 0x0044dcf2..0x0044dd02: the three floats are read before the calls.
    float Fov;
    float Near;
    float Far;
    memcpy(&Fov, &Snap->Fovh, sizeof(Fov));
    memcpy(&Near, &Snap->NearPlane, sizeof(Near));
    memcpy(&Far, &Snap->FarPlane, sizeof(Far));
    SetGeoPose(Camera, &Info->Frame.Pos.x, &Info->Frame.Orient.E[0][0]);
    Camera->SetFov(&Fov);
    Camera->SetNear(&Near);
    Camera->SetFar(&Far);
    return Camera;
}

// FUN_0044dd80 (thiscall, RET 8).
//   0x0044dda1..0x0044dde3: a "category" naming 1 on the "PlayerShip" template while the Gf ship
//     does not exist -> no object.
//   0x0044ddf9: FUN_0044e430 resolves the room object, instance, animation script, offset,
//     category and ownership; operator new(0x190) Thorn_Compound(frame, offset, script, snap);
//     +0x6c the object, FUN_00451330 the instance, +0x74 the script, +0x78 the category, +0x7c
//     the ownership byte.
//   0x0044de85..0x0044deae: with an object, its light group (+0x15) and layer (+0x16) from the
//     entity's LtGrp / SrtGrp and its render mode (+0x18) from ThornGetRenderType.
FThornComponent* FGfScript::SpawnCompound(int Index, FThornEntitySnap const* Snap)
{
    int const PetalCategory =
        PetalInterfaceDatabase::get_category(ThornGetUserString(Engine, Index, "category"));
    if (PetalCategory == 1 && _stricmp(Snap->TemplateName, "PlayerShip") == 0 &&
        GfObjectShip() == nullptr)
    {
        return nullptr;
    }

    FRoomObject* Object = nullptr;
    long Instance = -1;
    void* AnimScript = nullptr;
    Csys Offset;
    // Retail's stack slot for the category is not initialized by FUN_0044e430 when a reference
    // prop is missing (0x0044e534 path); the port starts it at the ctor's default.
    int Category = -1;
    unsigned char bUseScene = 0;
    ResolveCompoundTarget(Index, &Object, &Instance, &AnimScript, &Offset, &Category, &bUseScene);

    FThornCompound* const Compound = new FThornCompound(Frame, Offset, this, Snap);
    Compound->Object = Object;
    Compound->SetInstance(Instance);
    Compound->AnimScript = AnimScript;
    Compound->Category = Category;
    Compound->bUseScene = bUseScene;
    if (Object != nullptr)
    {
        Object->LightGroup = Snap->LtGrp;
        Object->Layer = Snap->SrtGrp;
        Object->RenderMode = static_cast<int>(
            ThornGetRenderType(reinterpret_cast<ThornEntity const*>(Snap), ScriptFile));
    }
    return Compound;
}

// FUN_0044ded0 (thiscall, RET 8).
//   A reference: no object.
//   0x0044df03..0x0044df34: the "Actor" user value, _strdup'd and lowered (null when absent).
//   FUN_0044e870 resolves the character; none -> no object (the actor copy is not freed,
//     0x0044df48 jumps past the free).
//   operator new(0x9f8) Thorn_Deformable; +0x6c the character; the character's light group /
//     layer from LtGrp / SrtGrp and render mode 0; malloc_strcpy of the entity name into +0x70.
//   An actor "player" (stricmp): the script's +0x61 = 1, +0x9c the character, +0x64 the
//     character frame (FUN_00452e40), +0x94 the character's slot 4 float, +0x98 the deformable
//     instance (FUN_00452eb0). The actor copy is freed (CRT free).
FThornComponent* FGfScript::SpawnDeformable(int Index, FThornEntitySnap const* Snap)
{
    if ((Snap->Flags & 1) != 0)
        return nullptr;

    char* Actor = nullptr;
    char const* const ActorValue = ThornGetUserString(Engine, Index, "Actor");
    if (ActorValue != nullptr)
    {
        Actor = _strdup(ActorValue);
        _strlwr(Actor);
    }

    FCharacterObject* const Character = ResolveCharacter(Snap, Actor);
    if (Character == nullptr)
        return nullptr;

    FThornDeformable* const Deformable = new FThornDeformable(Frame, this, Snap);
    Deformable->Character = Character;
    Character->LightGroup = Snap->LtGrp;
    Character->Layer = Snap->SrtGrp;
    Character->RenderMode = 0;
    malloc_strcpy(&Deformable->ActorName, Snap->Name);
    if (Actor == nullptr)
        return Deformable;

    if (_stricmp(Actor, "player") == 0)
    {
        bPlayerActor = 1;
        PlayerCharacter = Character;
        PlayerCsys = Deformable->GetCharacterCsys();
        PlayerField94 = Character->GetFloat1C();
        PlayerDeformInstance = Deformable->GetDeformInstance();
    }
    free(Actor);
    return Deformable;
}

// FUN_0044e030 (thiscall, RET 8).
//   A reference: no object.
//   operator new(0xd8) Thorn_Sound; the spatial byte is Flags bit 1. A template starting with
//     "VoiceProfile" (_strnicmp over its length) is a voice line: the prefix and one separator
//     cut off. FUN_00456ff0 binds the template. A voice line needs the "speaker" user value
//     (else the retail FDUMP, line 0x21f, with the full template); FUN_004572b0 takes it either
//     way. +0x84 = the script's +0x60; FUN_004572d0 the spatial byte.
FThornComponent* FGfScript::SpawnSound(int Index, FThornEntitySnap const* Snap)
{
    if ((Snap->Flags & 1) != 0)
        return nullptr;

    FThornSound* const Sound = new FThornSound(Frame, this, Snap);
    unsigned char const bSpatial = static_cast<unsigned char>((Snap->Flags >> 1) & 1);
    char const* Template = Snap->TemplateName;
    unsigned char bVoice = 0;
    size_t const PrefixLength = strlen("VoiceProfile");
    if (_strnicmp(Template, "VoiceProfile", PrefixLength) == 0)
    {
        bVoice = 1;
        Template += PrefixLength + 1;
    }
    Sound->BindTemplate(Template, bVoice);
    if (bVoice != 0)
    {
        char const* const Speaker = ThornGetUserString(Engine, Index, "speaker");
        if (Speaker == nullptr)
        {
            FDUMP(0x100001,
                  "%s(%d) : *** ERROR: Voice profile sound %s in script %s needs a speaker property",
                  GScriptFile, 0x21f, Snap->TemplateName, ScriptFile);
        }
        Sound->SetSpeaker(Speaker);
    }
    Sound->bUseScriptFade = bSoundsUseFade;
    Sound->SetSpatial(bSpatial);
    return Sound;
}

// FUN_0044e180 (thiscall, RET 8) -- operator new(0x70) Thorn_PlayerEngineSound.
FThornComponent* FGfScript::SpawnPlayerEngineSound(FThornEntitySnap const* Snap)
{
    return new FThornPlayerEngineSound(Frame, this, Snap);
}

// FUN_0044e1f0 (thiscall, RET 8) -- operator new(0x70) Thorn_SoundReference.
FThornComponent* FGfScript::SpawnSoundReference(FThornEntitySnap const* Snap)
{
    return new FThornSoundReference(Frame, this, Snap);
}

// FUN_0044e260 (thiscall, RET 8) -- operator new(0xa4) Thorn_Light (no reference check).
FThornComponent* FGfScript::SpawnLight(FThornEntitySnap const* Snap)
{
    return new FThornLight(Frame, this, Snap);
}

// FUN_0044e2d0 (thiscall, RET 8) -- operator new(0x70) Thorn_LightReference.
FThornComponent* FGfScript::SpawnLightReference(FThornEntitySnap const* Snap)
{
    return new FThornLightReference(Frame, this, Snap);
}

// FUN_0044e340 (thiscall, RET 8) -- a reference: no object; else operator new(0xac) Thorn_Psys
// bound to the [VisEffect] its template names (FUN_00453e60).
FThornComponent* FGfScript::SpawnPsys(FThornEntitySnap const* Snap)
{
    if ((Snap->Flags & 1) != 0)
        return nullptr;
    FThornPsys* const Psys = new FThornPsys(Frame, this, Snap);
    Psys->BindVisEffect(Snap->TemplateName);
    return Psys;
}

// FUN_0044e3c0 (thiscall, RET 8) -- operator new(0x70) Thorn_PlayerEnginePsys.
FThornComponent* FGfScript::SpawnPlayerEnginePsys(FThornEntitySnap const* Snap)
{
    return new FThornPlayerEnginePsys(Frame, this, Snap);
}

// FUN_0044e430 (thiscall, RET 0x1c).
//   0x0044e44b..0x0044e4b2: *OutObject 0, *OutInstance -1, *OutAnimScript -1, *OutOffset
//     identity, *bOutUseScene 0 (the category is not written here).
//   A reference (0x0044e4e2): the room prop of that name (FUN_0044a2f0) -> the prop, its
//     instance (slot 6), its main script (+0x58), category 6, not owned. None: the retail FDUMP
//     (line 0x2b4).
//   Otherwise the category from "category" (PetalInterfaceDatabase::get_category).
//     1 on the "PlayerShip" template: the Gf ship (FDUMP "No player ship", line 0x2d8, when
//       there is none -- retail goes on with the null pointer), its instance, script -1, and the
//       offset of its "HpMount" hardpoint (FUN_0043d940 / FUN_004478c0).
//     Anything else: category 6, owned; a Gf::ObjectProp (operator new(0x80), FUN_00445f40) from
//       {template, name, the old category, the snapshot pose, "loadout" or ""}; its instance and
//       main script; handed to the sequence (FUN_00455b80, with the name) unless the script's
//       +0x5c sends it to the room (FUN_0044a380).
void FGfScript::ResolveCompoundTarget(int Index, FRoomObject** OutObject, long* OutInstance,
                                      void** OutAnimScript, Csys* OutOffset, int* OutCategory,
                                      unsigned char* bOutUseScene)
{
    *OutObject = nullptr;
    *OutInstance = -1;
    *OutAnimScript = reinterpret_cast<void*>(static_cast<intptr_t>(-1));
    OutOffset->set_identity();
    *bOutUseScene = 0;

    FThornEntitySnap const* const Snap = EntitySnap(Engine, Index);
    if ((Snap->Flags & 1) != 0)
    {
        FObjectProp* const Prop = RoomControlFindProp(Room, Snap->Name);
        if (Prop == nullptr)
        {
            FDUMP(0x100001, "%s(%d) : *** ERROR: Cannot find referenced prop %s in the set layer",
                  GScriptFile, 0x2b4, Snap->Name);
            return;
        }
        *OutObject = Prop;
        *OutInstance = static_cast<long>(reinterpret_cast<intptr_t>(Prop->GetEngineInstance()));
        *OutAnimScript = reinterpret_cast<void*>(static_cast<uintptr_t>(Prop->GetMainScript()));
        *OutCategory = 6;
        *bOutUseScene = 0;
        return;
    }

    *OutCategory =
        PetalInterfaceDatabase::get_category(ThornGetUserString(Engine, Index, "category"));
    if (*OutCategory == 1 && _stricmp(Snap->TemplateName, "PlayerShip") == 0)
    {
        FObjectShip* const Ship = GfObjectShip();
        if (Ship == nullptr)
            FDUMP(0x100001, "%s(%d) : *** ERROR: %s", GScriptFile, 0x2d8, "No player ship");
        // 0x0044e5e0..0x0044e632 (retail goes on with a null ship): the ship, its instance
        // (slot 6), script -1, and the "HpMount" offset from the ship's hardpoint summary
        // (FUN_0043d940) against the instance (slot 6 again) through FUN_004478c0.
        *OutObject = Ship;
        *OutInstance = static_cast<long>(reinterpret_cast<intptr_t>(Ship->GetEngineInstance()));
        *OutAnimScript = reinterpret_cast<void*>(static_cast<intptr_t>(-1));
        void* const ShipInstance = Ship->GetEngineInstance();
        *OutOffset = GfShipMountOffset(Ship->GetHardpointSummary(), "HpMount", ShipInstance);
        return;
    }

    int const OldCategory = *OutCategory;
    *OutCategory = 6;
    *bOutUseScene = 1;
    FThornEntitySnap const* const PropSnap = EntitySnap(Engine, Index); // 0x0044e65d, again
    FPropData Data;
    Data.Template = PropSnap->TemplateName;
    Data.Name = PropSnap->Name;
    Data.Type = OldCategory;
    Data.Pos.x = PropSnap->Pos[0];
    Data.Pos.y = PropSnap->Pos[1];
    Data.Pos.z = PropSnap->Pos[2];
    memcpy(&Data.Orient.E[0][0], PropSnap->Orient, sizeof(Data.Orient.E));
    char const* const Loadout = ThornGetUserString(Engine, Index, "loadout");
    Data.Loadout = Loadout != nullptr ? Loadout : "";

    FObjectProp* const Prop = new FObjectProp(Room, Data);
    *OutObject = Prop;
    *OutInstance = static_cast<long>(reinterpret_cast<intptr_t>(Prop->GetEngineInstance()));
    *OutAnimScript = reinterpret_cast<void*>(static_cast<uintptr_t>(Prop->GetMainScript()));
    *bOutUseScene = 1;
    if (bPropsToRoom == 0)
        Sequence->AddProp(Prop, Data.Name.c_str());
    else
        RoomControlAddProp(Room, Prop);
}

// FUN_0044e870 (thiscall, RET 8).
//   0x0044e8a4..0x0044e908: with an actor, "player" and the four room roles (bartender, trader,
//     shipdealer, equipment) are told apart (stricmp).
//   "player": the room's player character (FUN_00449cd0); none -> the retail FDUMP (line
//     0x358) and no character.
//   A role: the room's character of that name (FUN_00449d50); none -> no character.
//   Otherwise by the script's +0x50: 0 the sequence's characters (FUN_00455ed0); 1 the mirror's
//     first (FUN_00453b50), 2 the mirror's of that actor (FUN_00453b60, no actor -> the
//     sequence's); anything else FDUMPs (line 0x382) and uses the sequence's. A mirror miss
//     falls to the sequence's too.
//   Still none: a new character from the template's costume at the snapshot pose (FUN_00435b80)
//     added to the sequence (FUN_00455d70).
//   Every character found or made gets FUN_004454e0 with the script's +0x55.
FCharacterObject* FGfScript::ResolveCharacter(FThornEntitySnap const* Snap, char const* Actor)
{
    FCharacterObject* Character = nullptr;
    bool bPlayer = false;
    bool bRole = false;
    if (Actor != nullptr)
    {
        if (_stricmp(Actor, "player") == 0)
            bPlayer = true;
        if (_stricmp(Actor, "bartender") == 0 || _stricmp(Actor, "trader") == 0 ||
            _stricmp(Actor, "shipdealer") == 0 || _stricmp(Actor, "equipment") == 0)
        {
            bRole = true;
        }
    }

    if (bPlayer)
    {
        Character = RoomControlPlayerCharacter(Room);
        if (Character == nullptr)
        {
            FDUMP(0x100001, "%s(%d) : *** ERROR: %s", GScriptFile, 0x358,
                  "Player hardpoint is missing, so script cannot connect to the player.\n");
            return nullptr;
        }
        Character->TakeOverForScript(Field_0x55);
        return Character;
    }
    if (bRole)
    {
        Character = RoomControlFindCharacterByName(Room, Actor);
        if (Character == nullptr)
            return nullptr;
        Character->TakeOverForScript(Field_0x55);
        return Character;
    }

    if (BindMode == 1)
    {
        Character = Mirror->First();
    }
    else if (BindMode == 2)
    {
        if (Actor != nullptr)
            Character = Mirror->Find(Actor);
    }
    else if (BindMode != 0)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: %s", GScriptFile, 0x382,
              "Invalid character binding method\n");
    }
    if (Character == nullptr)
        Character = Sequence->FindCharacter(Actor);

    if (Character == nullptr)
    {
        // 0x0044e99c..0x0044ea35: a fresh CharacterProperties (Common ctor), its Costume (+0x24)
        // from CostumeDescriptions::get_costume(template +0x3c); the frame by value from the
        // snapshot's +0x08 position and +0x14 orientation; FUN_00435b80(&props, placement 0,
        // index -1, room +0x30, 1, frame); FUN_00455d70(character, actor) on the sequence
        // (+0x34); the properties destroyed (EH state -1 then ~CharacterProperties).
        CharacterProperties Props;
        GetCostumeDescriptions()->get_costume(Snap->TemplateName, Props.Cost);
        Csys CharacterFrame;
        CharacterFrame.Pos.x = Snap->Pos[0];
        CharacterFrame.Pos.y = Snap->Pos[1];
        CharacterFrame.Pos.z = Snap->Pos[2];
        memcpy(&CharacterFrame.Orient.E[0][0], Snap->Orient, sizeof(CharacterFrame.Orient.E));
        Character = CreateCharacterObject(Props, nullptr, -1, Room, 1, CharacterFrame);
        Sequence->AddCharacter(Character, Actor);
    }
    Character->TakeOverForScript(Field_0x55);
    return Character;
}
