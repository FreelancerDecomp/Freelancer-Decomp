// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll RoomData (retail RoomData.cpp) and the Thorn script helpers it reads rooms through
// (retail ThornScript.cpp): ThornGetUser*, ThornScriptLoad / Destroy / GetCsys, the room-record
// templates and the FlashLightSet / FlashLightLine key reader. Split out of common_goods.cpp.

#include "common_goods.h"
#include "common_goods_internal.h" // kPathChars, MallocCopy
#include "msvcrt_compat.h"         // MsvcrtAtoi: retail MSVCRT.DLL wraps mod 2^32, the UCRT saturates
#include "thorn_iface.h"
#include "common_fuses.h"   // PetalInterfaceDatabase::get_category (prop categories)
#include "common_id.h"
#include "common_ini.h"
#include "common_misc.h"    // SUBOBJ_ID_NONE, GetFileSysPath
#include "common_newchar.h" // RtcSlider::validate_setting (RoomData priority tokens)
#include "DALib.h"
#include "dacom.h"
#include "x86math.h"        // I3DMathEngine::TransformMatrix (RoomData set_script lights)

#include <cmath>
#include <cstring>
#include <vector>

// common_dacom_stricmp.cpp: DACOM.DLL!stricmp (Common IAT PTR 0639902c).
int CallDacomStricmp(const char* Left, const char* Right);

using CmnGoods::kPathChars;
using CmnGoods::MallocCopy;

namespace
{
    const char kRoomDataSource[] = "E:\\FL\\Scratch\\Source\\Common\\RoomData.cpp";
    const char kThornScriptSource[] = "E:\\FL\\Scratch\\Source\\Common\\ThornScript.cpp";

    // The %s argument of read_Hotspot_block's zoom_in / zoom_out errors (06276a80 / 06276abf):
    // retail pushes the ADDRESS of INI_Reader::get_name (PUSH 06263450), not its result, so the
    // message prints that function's code bytes (in retail the 5-byte JMP to get_file_name,
    // e9 eb cb 0a 00 -> four garbage characters). Very strange, but retail: the port passes the
    // code address of its own INI_Reader::get_name the same way.
    char const* IniReaderGetNameCode()
    {
        char const* (INI_Reader::*Method)() const = &INI_Reader::get_name;
        static_assert(sizeof(Method) == sizeof(char const*),
                      "INI_Reader member pointer is a plain code address");
        char const* Code = nullptr;
        std::memcpy(&Code, &Method, sizeof(Code));
        return Code;
    }

    // Shared body of RoomData::read_FlashLightSet_block (0x06276c00) and
    // read_FlashLightLine_block (0x06276df0). The two functions differ only in the Mode they
    // stamp and in whether they accept the "numlights" key, so retail's two near-identical
    // key chains (06276c30-06276dd6 and 06276e20-06276ff0) are folded into one here.
    // Every offset below is the literal store in the listing.
    void ReadFlashLightKeys(INI_Reader& Reader, FlashLightSetInfo& Set, bool bAcceptNumLights)
    {
        // 1.0f/255.0f, the .rdata float at 0639ac78 (raw bytes 81 80 80 3b = 0x3b808081),
        // applied to all three components of "icolor".
        const float kByteToUnit = 0.003921568859368563f;

        if (!Reader.read_value())
            return;

        do
        {
            if (Reader.is_value("color"))
            {
                // 06276c42-06276c7a / 06276e32-06276e6a
                Set.Param0 = Reader.get_value_float(0);
                Set.Param1 = Reader.get_value_float(1);
                Set.Param2 = Reader.get_value_float(2);
            }
            else if (Reader.is_value("icolor"))
            {
                // 06276c92-06276cec / 06276e82-06276edc: same three slots, each scaled.
                // Values 0 and 1 are spilled with FSTP float and reloaded before the FMUL;
                // value 2 is still the unrounded x87 result when its FMUL runs (06276cdb /
                // 06276ecb), so it is read without the float rounding (value_num).
                Set.Param0 = Reader.get_value_float(0) * kByteToUnit;
                Set.Param1 = Reader.get_value_float(1) * kByteToUnit;
                Set.Param2 = static_cast<float>(Reader.value_num(2) * static_cast<double>(kByteToUnit));
            }
            else if (Reader.is_value("scale"))
            {
                Set.Param3 = Reader.get_value_float(0);   // +0x0c, 06276d0b / 06276efb
            }
            else if (Reader.is_value("gap"))
            {
                Set.Param4 = Reader.get_value_float(0);   // +0x10, 06276d2a / 06276f1a
            }
            else if (Reader.is_value("blink"))
            {
                Set.Param5 = Reader.get_value_float(0);   // +0x14, 06276d49 / 06276f39
            }
            else if (Reader.is_value("endpause"))
            {
                Set.Param6 = Reader.get_value_float(0);   // +0x18, 06276d65 / 06276f58
            }
            else if (Reader.is_value("hardpoint"))
            {
                // 06276d7a-06276da0 / 06276f70-06276f96. Note both name keys share ONE
                // counter at +0xe20, so "hardpoint" and "setpoint" advance the same slot
                // index; that is retail's behaviour, not a transcription slip.
                // Retail does not bound-check the 64-slot arrays (06276d7f..06276d9a): a 65th
                // name runs on into the next bank, reproduced as is.
                malloc_strcpy(const_cast<char**>(&Set.HardpointNames[Set.NumLights]),
                              Reader.get_value_string());
                Set.NumLights += 1;
            }
            else if (Reader.is_value("setpoint"))
            {
                // 06276db2-06276dc6 / 06276fa8-06276fbc: same tail, SetpointNames at +0x120.
                malloc_strcpy(const_cast<char**>(&Set.SetpointNames[Set.NumLights]),
                              Reader.get_value_string());
                Set.NumLights += 1;
            }
            else if (bAcceptNumLights && Reader.is_value("numlights"))
            {
                // 06276fce-06276fda: float read, then ftol into +0xe24. Line blocks only --
                // read_FlashLightSet_block's chain ends at "setpoint" (06276dc8).
                // ftol on the unrounded x87 value (value_num = get_value_float, no rounding).
                Set.NumLightsAlt = static_cast<long>(Reader.value_num(0));
            }
            // The final else in retail (06276bd9 / 06276dc8 / 06276fe2) calls
            // INI_Reader::get_file_name and throws the result away -- the remains of a
            // diagnostic the optimiser gutted. Nothing to port.
        } while (Reader.read_value());
    }
}

char const* __cdecl ThornGetUserString(IScriptEngine* Engine, int Index, char const* Key)
{
    // 0627bc60: slot 7 (+0x1c, entity name by index) then slot 33 (+0x84, user property) with
    // a zeroed out-pointer; a non-zero result gives 0. No null test on anything.
    void* EntityName = Engine->Slot7EntityHandle(Index);
    char* Out = nullptr;
    GENRESULT Cr = Engine->Slot33UserProp(EntityName, Key, &Out);
    if (Cr != 0)
        return nullptr;
    return Out;
}


bool __cdecl ThornGetUserInt(IScriptEngine* Engine, int Index, char const* Key, int* Out)
{
    // 0627bd00: the slot 7 / slot 33 pair of ThornGetUserString inlined; a zero result with a
    // non-null string stores MSVCRT atol into *Out (no null test on Out) and returns true.
    char const* Text = ThornGetUserString(Engine, Index, Key);
    if (Text == nullptr)
        return false;
    *Out = static_cast<int>(MsvcrtAtoi(Text));
    return true;
}

bool __cdecl ThornGetUserFloat(IScriptEngine* Engine, int Index, char const* Key, float* Out)
{
    // 0627bca0: likewise, then MSVCRT atof stored as float (FSTP float [Out]).
    char const* Text = ThornGetUserString(Engine, Index, Key);
    if (Text == nullptr)
        return false;
    *Out = static_cast<float>(atof(Text));
    return true;
}

unsigned long __cdecl ThornGetRenderType(ThornEntity const* Entity, char const* Unused)
{
    // 0627bd60. Retail ignores the second argument entirely and reads one flag byte at +0x42
    // (UsrFlg) with no null test: bit 0 gives 1, bit 1 gives 2, and bit 1 wins when both are set.
    (void)Unused;
    unsigned char const Flags = Entity->UsrFlg;
    unsigned long Result = (Flags & 1u) != 0 ? 1ul : 0ul;
    if (((Flags >> 1) & 1u) != 0)
        Result = 2ul;
    return Result;
}

IScriptEngine* __cdecl ThornScriptLoad(char const* Path)
{
    // 0x1b8f0. Retail checks the pointer and then the length with a REPNE SCASB, so an empty
    // string takes the same warning path as a null one.
    if (Path == nullptr || Path[0] == '\0')
    {
        FDUMP(0x100002, "%s(%d) : *** WARNING: %s", kThornScriptSource, 0x18,
              "Skipping null script filename...\n");
        return nullptr;
    }

    // 0627b91f-0627b96c: CreateInstance on *DALib::Dacom, QueryInterface of the result for
    // IScriptEngine, Release of the instance -- none of the three results is tested.
    ICOManager* const Manager = DALib::Dacom;

    // The descriptor retail builds at 0627b929 is five dwords with Size 0x14 -- dacom.h's
    // aggregate descriptor, here with no outer / inner unknown.
    static_assert(sizeof(FAGGDESC14) == 0x14, "retail descriptor is 0x14 bytes");
    FAGGDESC14 Desc;
    Desc.Size = 0x14;
    Desc.NameAt4 = "IScriptEngine";
    Desc.OuterUnknown = nullptr;
    Desc.InnerUnknown = nullptr;
    Desc.Reserved10 = 0;

    void* Instance = nullptr;
    Manager->CreateInstance(reinterpret_cast<AGGDESC*>(&Desc), &Instance);

    // 0627b963: QueryInterface off the fresh instance, then drop the instance reference — the
    // queried interface is what the caller gets back.
    void* Queried = nullptr;
    IDAComponent* const Component = static_cast<IDAComponent*>(Instance);
    Component->QueryInterface(Iids::ScriptEngine, &Queried);
    Component->Release();
    IScriptEngine* Engine = static_cast<IScriptEngine*>(Queried);

    IFileSystem* Filesys = nullptr;
    DAEngine_create_filesystem(Path, &Filesys, "DOS");
    if (Filesys != nullptr)
    {
        // Slot 4 (+0x10) hands the script file to the engine.
        Engine->Slot4Load(Filesys);
        DAEngine_release_filesystem(&Filesys);
        return Engine;
    }

    FDUMP(0x100001, "%s(%d) : *** ERROR: Cannot load script file %s\n", kThornScriptSource, 0x30,
          Path);
    Engine->Release();
    return nullptr;
}

void __cdecl ThornScriptDestroy(IScriptEngine** Engine)
{
    // 0x1ba10 — the same VA as DAEngine_release_filesystem: identical code, folded by the
    // linker. A non-null *Engine is released through slot 2 and nulled; the pointer to it is
    // not tested.
    if (*Engine != nullptr)
    {
        (*Engine)->Release();
        *Engine = nullptr;
    }
}

Csys __cdecl ThornScriptGetCsys(IScriptEngine* Engine, char const* ActorName)
{
    // 0627ba30. Starts from the identity frame; a null engine returns it. Otherwise the type-2
    // entities are walked in index order and the first one that matches gives the frame:
    //  - ActorName set: its "actor" user property must exist and equal ActorName (DACOM
    //    stricmp);
    //  - ActorName null: an entity without an "actor" property matches, one whose actor is
    //    "player" is skipped, any other actor matches.
    // No match leaves the identity frame.
    Csys Out;
    Out.set_identity();
    if (Engine == nullptr)
        return Out;

    int const Count = static_cast<int>(Engine->Slot6EntityCount());
    int Index = 0;
    for (Index = 0; Index < Count; ++Index)
    {
        ThornEntity const* const Entity = static_cast<ThornEntity const*>(Engine->Slot9Entity(Index));
        if (Entity == nullptr || Entity->Type != kThornEntityDeformable)
            continue;

        float const FloorHeight = Entity->FloorHeight; // +0x44
        void* const Name = Engine->Slot7EntityHandle(Index);
        char* Value = nullptr;
        char const* Actor = nullptr;
        if (Engine->Slot33UserProp(Name, "actor", &Value) == 0)
            Actor = Value;

        bool bMatch = false;
        if (ActorName == nullptr)
            bMatch = Actor == nullptr || CallDacomStricmp(Actor, "player") != 0;
        else
            bMatch = Actor != nullptr && CallDacomStricmp(Actor, ActorName) == 0;
        if (!bMatch)
            continue;

        // +0x08 and +0x10 give x and z; y comes from +0x44 (the floor height), and the 3x3
        // from +0x14.
        float const Position[3] = {Entity->Pos[0], FloorHeight, Entity->Pos[2]};
        CsysFromPosOrient(Out, Position, Entity->Orient);
        return Out;
    }

    Out.set_identity();
    return Out;
}

namespace
{
    // The record allocators FUN_062783e0 (+0x134), FUN_06278640 (+0x144), FUN_062788b0
    // (+0x154), FUN_06278b30 (+0x164), FUN_06278e20 (+0x174), FUN_06279120 (+0x184),
    // FUN_06279400 (+0x194), FUN_06279670 (+0x1a4) and FUN_06279920 (+0x1b4) are one shape:
    // operator new(sizeof record), the record's ctor inlined, push_back of the pointer
    // (VC6 vector insert), return the record. Only the growth policy of the vector differs.
    template <class TRecord>
    TRecord* AppendRoomRecord(std::vector<TRecord*>& Records)
    {
        TRecord* const Record = new TRecord();
        Records.push_back(Record);
        return Record;
    }

    // The per-vector purges ~RoomData runs after destroy() (FUN_06278300 +0x134, 062785e0
    // +0x144, 06278830 +0x154, 06278ac0 +0x164, 06278d30 +0x174, 06279020 +0x184, 06279310
    // +0x194, 062795d0 +0x1a4, 062798b0 +0x1b4): every non-null element destroyed and freed
    // (the record's dtor body inlined), its slot nulled, the size re-read each pass, then the
    // vector emptied.
    template <class TRecord>
    void PurgeRoomRecords(std::vector<TRecord*>& Records)
    {
        size_t Index = 0;
        for (; Index < Records.size(); ++Index)
        {
            delete Records[Index];
            Records[Index] = nullptr;
        }
        Records.clear();
    }

    // Retail clears the Csys-sized float pattern of its stack copies with 0.0f/1.0f stores;
    // this builds the identity frame with a zero position, as FUN_0628b030(0,0,0) +
    // FUN_062748a0 do at +0x1cc / +0x1d8.
    void ResetRoomCamera(RoomCameraInfo& Camera)
    {
        Camera.Frame.Pos.x = 0.0f;
        Camera.Frame.Pos.y = 0.0f;
        Camera.Frame.Pos.z = 0.0f;
        MatrixSetIdentity(Camera.Frame.Orient);
        Camera.Fov = 35.0f;
        Camera.NearPlane = 0.25f;
        Camera.FarPlane = 1000000.0f;
    }

    float FloatFromBits(unsigned Bits)
    {
        float Value = 0.0f;
        memcpy(&Value, &Bits, sizeof(Value));
        return Value;
    }
}

// 06274150. The two ID_Strings cleared, the nine vectors constructed empty, the camera
// cleared (RoomCameraInfo's ctor; retail calls RoomCameraInfo::clear at 062742da), then
// clear(). +0x08 is not written (see the header).
RoomData::RoomData()
{
    Music.clear();
    Ambient.clear();
    clear();
}

// 06274300. destroy(), then the members in reverse order: the camera (+0x1c8: malloc_strfree
// of the name and the default frame -- RoomCameraInfo's dtor), and for every record vector
// from +0x1b4 down to +0x134 its purge and the vector dtor. The vectors are already empty
// after destroy(), so the purges find nothing, as in retail. The port runs the purges in this
// body and leaves the camera to its member dtor, which runs after them; the two touch
// disjoint members.
RoomData::~RoomData()
{
    destroy();
    PurgeRoomRecords(AmbientScripts);
    PurgeRoomRecords(FlashLightSets);
    PurgeRoomRecords(Hotspots);
    PurgeRoomRecords(ShipPlacements);
    PurgeRoomRecords(CharacterPlacements);
    PurgeRoomRecords(Setpoints);
    PurgeRoomRecords(Props);
    PurgeRoomRecords(Lights);
    PurgeRoomRecords(Cameras);
}

namespace
{
    // 1/255 as the double at .rdata 0639a1b8 (10 10 10 10 10 10 70 3f).
    double const kInv255Double = 0.00392156862745098;

    // One channel of the scene entity's ambient colour (062751aa..062751f2): the dword FILD'ed
    // as a signed int, times 1/255.0 in double, FSTP'd as float.
    float AmbientChannel(unsigned int Raw)
    {
        int const Value = static_cast<int>(Raw);
        return static_cast<float>(Value * kInv255Double);
    }

    // 06274ec9..06274f6f (shadow) / 06274ff7..0627509d (glare): the blob direction turned by
    // the entity's 3x3 through the 3DMathEngine (lazy DAT_063fbbc0, slot 11 +0x2c
    // TransformMatrix(out, orient, dir)), then each component times 1.0f / sqrt(x*x + y*y +
    // z*z) (FSQRT, FDIVR by the 1.0f at .rdata 0639a1d0). No zero-length test: a zero direction
    // gives infinities times zero, as in retail. The x87 sum is kept in double here.
    void TurnLightDirection(Csys const& Frame, Vector const& BlobDirection, Vector& Out)
    {
        Vector Turned;
        I3DMathEngine* const Math = Acquire3DMathEngine();
        Math->TransformMatrix(reinterpret_cast<FVector&>(Turned),
                              reinterpret_cast<FMatrix const&>(Frame.Orient),
                              reinterpret_cast<FVector const&>(BlobDirection));
        double const LengthSq = static_cast<double>(Turned.x) * Turned.x +
                                static_cast<double>(Turned.y) * Turned.y +
                                static_cast<double>(Turned.z) * Turned.z;
        double const InvLength = 1.0 / sqrt(LengthSq);
        Out.x = static_cast<float>(Turned.x * InvLength);
        Out.y = static_cast<float>(Turned.y * InvLength);
        Out.z = static_cast<float>(Turned.z * InvLength);
    }
}

// 06274b40 ?read_set_script@RoomData@@AAEXPBD@Z. Walks every entity of the set script and
// files it by type; the bodies of add_set_script_Prop / _Camera / _Setpoint / _light are
// inlined in retail and are the calls below in the port (the same stores in the same order).
// Then one CharPlaceInfo per setpoint whose name follows the zs/zg convention.
void RoomData::read_set_script(char const* RelPath)
{
    // 06274b58..06274b66: no script -> nothing at all (not even the placement pass).
    IScriptEngine* Engine = ThornScriptLoad(RelPath);
    if (Engine == nullptr)
        return;

    // 06274b6c..06274b80: slot 6, compared signed.
    int const EntityCount = static_cast<int>(Engine->Slot6EntityCount());
    int Index = 0;
    for (Index = 0; Index < EntityCount; ++Index)
    {
        // 06274b86..06274bf0: slot 9; the frame (12 dwords from +0x08), the template (+0x3c),
        // the name (+0x38), the light group / layer bytes (+0x40 / +0x41), type, flags and the
        // render type.
        ThornEntity const* const Entity = static_cast<ThornEntity const*>(Engine->Slot9Entity(Index));
        Csys Frame;
        CsysFromPosOrient(Frame, Entity->Pos, Entity->Orient);
        char const* const Template = Entity->TemplateName;
        char const* const Name = Entity->Name;
        unsigned char const LightGroup = Entity->LtGrp;
        signed char const Layer = static_cast<signed char>(Entity->SrtGrp);
        int const Type = Entity->Type;
        unsigned const Flags = static_cast<unsigned>(Entity->Flags);
        unsigned long const RenderType = ThornGetRenderType(Entity, RelPath);
        // 06274bfd..06274c1a: flag bits 2 (lit) and 1 (visible), and the "category" user
        // property, for every entity before the type test.
        bool const bLit = ((Flags >> 2) & 1u) != 0;
        bool const bVisible = ((Flags >> 1) & 1u) != 0;
        char const* const CategoryName = ThornGetUserString(Engine, Index, "category");

        if (Type == kThornEntityCompound)
        {
            // 06274c2d..06274c6e: fog unless "nofog"; the room's main prop with "hardpoint".
            bool bFog = true;
            if (ThornGetUserString(Engine, Index, "nofog") != nullptr)
                bFog = false;
            bool bRoomMain = false;
            if (ThornGetUserString(Engine, Index, "hardpoint") != nullptr)
                bRoomMain = true;
            // 06274c73..06274ca3: a compound without a category is reported and skipped.
            if (CategoryName == nullptr)
            {
                FDUMP(0x100001,
                      "%s(%d) : *** ERROR: Entity %s in hardpoint script %s does not have a "
                      "category.\nIt might be incorrectly marked as a reference.",
                      kRoomDataSource, 0x293, Name, RelPath);
                continue;
            }
            // 06274ca8..06274cbf: category 0 is the room's main prop as well.
            int const Category = PetalInterfaceDatabase::get_category(CategoryName);
            if (Category == 0)
                bRoomMain = true;
            // 06274cc4..06274cdf: the loadout, "" (.rdata 063997b0) without one.
            char const* Loadout = ThornGetUserString(Engine, Index, "loadout");
            if (Loadout == nullptr)
                Loadout = "";
            // 06274ce7..06274d1f: RtcSlider::validate_setting(priority, RelPath) (0x0627a710,
            // labelled AttachedEquipment::is_attached by ICF), then CreateID; 0 without one.
            unsigned long PriorityId = 0;
            char const* const PriorityText = ThornGetUserString(Engine, Index, "priority");
            if (PriorityText != nullptr)
            {
                RtcSlider::validate_setting(PriorityText, RelPath);
                PriorityId = CreateID(PriorityText);
            }
            // 06274d23..06274d3f.
            bool bRunningLights = false;
            if (ThornGetUserString(Engine, Index, "running_lights") != nullptr)
                bRunningLights = true;
            // 06274d44..06274dcb: add_set_script_Prop inlined.
            add_set_script_Prop(Template, Name, Category, Frame, bVisible, bLit, bFog, bRoomMain,
                                LightGroup, Layer, RenderType, Loadout, PriorityId,
                                bRunningLights);
        }
        else if (Type == kThornEntityCamera)
        {
            // 06274de0..06274e71: the camera (props at +0x44: fov, near, far) and a setpoint of
            // the same name and frame. The tail sits in FThornEntitySnap's anonymous union, so
            // its address is taken from its first member.
            add_set_script_Camera(0, Frame, Name,
                                  reinterpret_cast<ThornCameraProperties const*>(&Entity->Fovh));
            add_set_script_Setpoint(0, Frame, Name, Entity);
        }
        else if (Type == kThornEntityLight)
        {
            // 06274e84: the 0x90-byte light blob at +0x44. +0x0c direction, +0x20 the light's on
            // flag, +0x28 a D3DLIGHT8 (its type at +0x28, the diffuse colour at +0x2c).
            FThornLightProps const& Blob = Entity->Light;
            Vector BlobDirection;
            BlobDirection.x = Blob.Direction[0];
            BlobDirection.y = Blob.Direction[1];
            BlobDirection.z = Blob.Direction[2];
            int const LightType = Blob.Type;
            Vector BlobColor;
            BlobColor.x = Blob.Diffuse[0];
            BlobColor.y = Blob.Diffuse[1];
            BlobColor.z = Blob.Diffuse[2];
            if (CallDacomStricmp(Name, "LtShadowSource") == 0)
            {
                // 06274eb1..06274fc4.
                bShadowLight = true;
                if (LightType == 3)
                {
                    TurnLightDirection(Frame, BlobDirection, ShadowLightDirection);
                    bShadowLightWorldSpaceDir = true;
                }
                else
                {
                    ShadowLightPosition = Frame.Pos;
                    bShadowLightWorldSpaceDir = false;
                }
                ShadowLightColor = BlobColor;
            }
            else if (CallDacomStricmp(Name, "LtGlareSource") == 0)
            {
                // 06274fdf..062750f2.
                bGlareLight = true;
                if (LightType == 3)
                {
                    TurnLightDirection(Frame, BlobDirection, GlareLightDirection);
                    bGlareLightWorldSpaceDir = true;
                }
                else
                {
                    GlareLightPosition = Frame.Pos;
                    bGlareLightWorldSpaceDir = false;
                }
                GlareLightColor = BlobColor;
            }
            else if (Blob.On != 0)
            {
                // 062750f7..0627515c: add_set_script_light inlined, the entity's +0x40 byte as
                // the record's +0xa4.
                add_set_script_light(0, Frame, static_cast<ThornLightProperties const*>(&Blob),
                                     LightGroup);
            }
        }
        else if (Type == kThornEntityMarker)
        {
            // 06275166..062751a3: add_set_script_Setpoint inlined.
            add_set_script_Setpoint(0, Frame, Name, Entity);
        }
        else if (Type == kThornEntityScene)
        {
            // 062751aa..062751f2: three ints at +0x44 (the head of the scene blob), each FILD
            // (signed) * 1/255.0 (double) and FSTP'd as float into +0xd8..+0xe0.
            AmbientColor.x = AmbientChannel(Entity->SceneBlob[0]);
            AmbientColor.y = AmbientChannel(Entity->SceneBlob[1]);
            AmbientColor.z = AmbientChannel(Entity->SceneBlob[2]);
        }
    }

    // 06275211..0627521b: DAEngine_release_filesystem(&engine) -- the same folded body as
    // ThornScriptDestroy.
    ThornScriptDestroy(&Engine);

    // 0627521e..06275292: every setpoint whose name follows the zs/zg convention (+0x34
    // bValid) gets a placement (FUN_06278e20 on +0x174): the name, the frame, CharType (+0x38)
    // -> +0x34, Posture (+0x54) -> +0x38, bZoneScript (+0x35) -> +0x3c. The size is re-read
    // on every pass.
    size_t SetpointIndex = 0;
    for (SetpointIndex = 0; SetpointIndex < Setpoints.size(); ++SetpointIndex)
    {
        SetpointInfo const* const Setpoint = Setpoints[SetpointIndex];
        if (!Setpoint->Props.bValid)
            continue;
        CharPlaceInfo* const Place = AppendRoomRecord(CharacterPlacements);
        malloc_strcpy(const_cast<char**>(&Place->Name), Setpoint->Name);
        Place->Frame = Setpoint->Frame;
        Place->CharType = Setpoint->Props.CharType;
        Place->Posture = Setpoint->Props.Posture;
        Place->bZoneScript = Setpoint->Props.bZoneScript;
    }
}

// 062770a0 ?read_from_ini@RoomData@@AAEXPBD@Z. The path through GetFileSysPath with
// DALib::Data, an INI_Reader on the stack; an unopenable file is silently nothing. Every
// header of the room ini in retail order; the obsolete ones only warn (FDUMP 0x100002) and
// an unknown header is passed over.
void RoomData::read_from_ini(char const* RelPath)
{
    char FullPath[kPathChars];
    GetFileSysPath(FullPath, RelPath, DALib::Data);

    INI_Reader Reader;
    if (!Reader.open(FullPath, false))
        return;

    while (Reader.read_header())
    {
        if (Reader.is_header("Room") || Reader.is_header("Room_Info"))
        {
            read_RoomInfo_block(&Reader);
        }
        else if (Reader.is_header("Cloud"))
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: [Cloud] is obsolete in %s", kRoomDataSource,
                  0x7fe, FullPath);
        }
        else if (Reader.is_header("Room_Sound") || Reader.is_header("Sound"))
        {
            read_Sound_block(&Reader);
        }
        else if (Reader.is_header("Fog"))
        {
            FDUMP(0x100002,
                  "%s(%d) : *** WARNING: [Fog] is obsolete in %s, fog settings should be in scripts",
                  kRoomDataSource, 0x807, FullPath);
        }
        else if (Reader.is_header("Camera"))
        {
            read_Camera_block(&Reader);
        }
        else if (Reader.is_header("CharacterPlacement"))
        {
            read_CharacterPlacement_block(&Reader);
        }
        else if (Reader.is_header("GoodsCartPlacement"))
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: [GoodsCartPlacement] is obsolete in %s",
                  kRoomDataSource, 0x813, FullPath);
        }
        else if (Reader.is_header("RepairRobotPlacement"))
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: [RepairRobotPlacement] is obsolete in %s",
                  kRoomDataSource, 0x817, FullPath);
        }
        else if (Reader.is_header("GoodsPilePlacement"))
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: [GoodsPilePlacement] is obsolete in %s",
                  kRoomDataSource, 0x81b, FullPath);
        }
        else if (Reader.is_header("ShipPlacement"))
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: %s", kRoomDataSource, 0x81f,
                  "[ShipPlacement] is obsolete");
        }
        else if (Reader.is_header("PlayerShipPlacement"))
        {
            read_PlayerShipPlacement_block(&Reader);
        }
        else if (Reader.is_header("ForSaleShipPlacement"))
        {
            // 06277397..06277464: read_ForSaleShipPlacement_block inlined.
            read_ForSaleShipPlacement_block(&Reader);
        }
        else if (Reader.is_header("MonitorPlacement"))
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: %s", kRoomDataSource, 0x82b,
                  "[MonitorPlacement] is obsolete");
        }
        else if (Reader.is_header("Effect"))
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: %s", kRoomDataSource, 0x82f,
                  "[Effect] is obsolete");
        }
        else if (Reader.is_header("AvailableShip"))
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: %s", kRoomDataSource, 0x833,
                  "[AvailableShip] is obsolete");
        }
        else if (Reader.is_header("Hotspot"))
        {
            read_Hotspot_block(&Reader);
        }
        else if (Reader.is_header("FlashlightSet"))
        {
            read_FlashLightSet_block(&Reader);
        }
        else if (Reader.is_header("FlashlightLine"))
        {
            read_FlashLightLine_block(&Reader);
        }
        else if (Reader.is_header("Billboard"))
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: %s", kRoomDataSource, 0x843,
                  "[Billboard] is obsolete");
        }
        else if (Reader.is_header("Light"))
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: %s", kRoomDataSource, 0x847,
                  "[Light] is obsolete");
        }
        else if (Reader.is_header("Lighting"))
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: %s", kRoomDataSource, 0x84b,
                  "[Lighting] is obsolete");
        }
    }
    Reader.close();
}
// 06277c50 ?load@RoomData@@QAEXXZ.
void RoomData::load()
{
    if (!bLoaded)
        read_from_ini(File);
    bLoaded = true;
}

// 06275bb0 ?setup_Camera@RoomData@@AAEXXZ. The camera's name pointer is zeroed WITHOUT a free
// (06275bc9; a name left by an earlier setup_Camera with no unsetup_Camera in between leaks,
// as in retail), the frame reset to a zero position and identity 3x3, 35 / 0.25 / 1e6.
// Without a camera name the defaults are written once more. The three debug names (DACOM
// stricmp) install a fixed pose, read here off the immediate stores; any other name takes the
// FIRST camera of +0x134 with that name (06275e72 leaves the loop on the first hit).
void RoomData::setup_Camera()
{
    Camera.Name = nullptr;
    ResetRoomCamera(Camera);

    if (CameraName == nullptr)
    {
        // 06275c13..06275c68.
        ResetRoomCamera(Camera);
        return;
    }

    if (CallDacomStricmp(CameraName, "debug_detailtest") == 0)
    {
        // 06275c86..06275cde, then 06275ce6..06275d32: position and the 3x3 row by row.
        Camera.Frame.Pos.x = FloatFromBits(0xbf804189u);
        Camera.Frame.Pos.y = FloatFromBits(0x3fb58106u);
        Camera.Frame.Pos.z = FloatFromBits(0x4033a5e3u);
        Camera.Frame.Orient.E[0][0] = FloatFromBits(0x3f7c28f6u);
        Camera.Frame.Orient.E[0][1] = FloatFromBits(0xbcfc5048u);
        Camera.Frame.Orient.E[0][2] = FloatFromBits(0x3e2dab9fu);
        Camera.Frame.Orient.E[1][0] = FloatFromBits(0xbaaa64c3u);
        Camera.Frame.Orient.E[1][1] = FloatFromBits(0x3f7b8bacu);
        Camera.Frame.Orient.E[1][2] = FloatFromBits(0x3e3dbf48u);
        Camera.Frame.Orient.E[2][0] = FloatFromBits(0xbe3089a0u);
        Camera.Frame.Orient.E[2][1] = FloatFromBits(0xbe3b2fecu);
        Camera.Frame.Orient.E[2][2] = FloatFromBits(0x3f77c84bu);
        Camera.Fov = 35.0f;
        Camera.NearPlane = 0.25f;
        Camera.FarPlane = 1000000.0f;
        return;
    }

    if (CallDacomStricmp(CameraName, "debug_straight_up") == 0)
    {
        // 06275d4c..06275d88: zero position, rows (1,0,0), (0,0,-1), (0,1,0).
        Camera.Frame.Pos.x = 0.0f;
        Camera.Frame.Pos.y = 0.0f;
        Camera.Frame.Pos.z = 0.0f;
        Camera.Frame.Orient.E[0][0] = 1.0f;
        Camera.Frame.Orient.E[0][1] = 0.0f;
        Camera.Frame.Orient.E[0][2] = 0.0f;
        Camera.Frame.Orient.E[1][0] = 0.0f;
        Camera.Frame.Orient.E[1][1] = 0.0f;
        Camera.Frame.Orient.E[1][2] = -1.0f;
        Camera.Frame.Orient.E[2][0] = 0.0f;
        Camera.Frame.Orient.E[2][1] = 1.0f;
        Camera.Frame.Orient.E[2][2] = 0.0f;
        Camera.Fov = 35.0f;
        Camera.NearPlane = 0.25f;
        Camera.FarPlane = 1000000.0f;
        return;
    }

    if (CallDacomStricmp(CameraName, "debug_anim") == 0)
    {
        // 06275da6..06275e33: the position, then FUN_06275ee0 builds the 3x3 from three
        // COLUMN vectors (local_30, local_3c, local_48).
        Camera.Frame.Pos.x = FloatFromBits(0xc0343958u);
        Camera.Frame.Pos.y = FloatFromBits(0x3f9851ecu);
        Camera.Frame.Pos.z = FloatFromBits(0x409ff7cfu);
        Camera.Frame.Orient.E[0][0] = FloatFromBits(0x3d252157u);
        Camera.Frame.Orient.E[1][0] = FloatFromBits(0xbdd2dcb1u);
        Camera.Frame.Orient.E[2][0] = FloatFromBits(0xbf7e6e44u);
        Camera.Frame.Orient.E[0][1] = FloatFromBits(0xbe333db0u);
        Camera.Frame.Orient.E[1][1] = FloatFromBits(0x3f7a8390u);
        Camera.Frame.Orient.E[2][1] = FloatFromBits(0xbdde2ac3u);
        Camera.Frame.Orient.E[0][2] = FloatFromBits(0x3f7bd662u);
        Camera.Frame.Orient.E[1][2] = FloatFromBits(0x3e369ec3u);
        Camera.Frame.Orient.E[2][2] = FloatFromBits(0x3caf8bdfu);
        Camera.Fov = 35.0f;
        Camera.NearPlane = 0.25f;
        Camera.FarPlane = 1000000.0f;
        return;
    }

    // 06275e3f..06275ec8: DACOM stricmp(CameraName, camera name); on the first hit
    // malloc_strfree + RoomCameraInfo::clear, malloc_strcpy of the name, the frame and the
    // three floats -- RoomCameraInfo::operator= (06273370) step for step.
    size_t Index = 0;
    for (Index = 0; Index < Cameras.size(); ++Index)
    {
        RoomCameraInfo const* const Candidate = Cameras[Index];
        if (CallDacomStricmp(CameraName, Candidate->Name) == 0)
        {
            Camera = *Candidate;
            return;
        }
    }
}

// ?unload@RoomData@@QAEXXZ, ordinal 3777 at .text:062a8920 -- a lone `RET` (0xc3), ICF-folded
// with twenty other empty `void()` members (Common.dll.txt 137410-137423). Retail's unload
// does nothing: it neither clears bLoaded nor frees the parsed room contents, so a later
// load() finds bLoaded set and does not re-read the INI (load, .text:06277c50). The contents
// are owned by the RoomData itself and are freed only by destroy()/clear(), which ~RoomData
// runs (Common.dll.c:28139) when BaseData tears its Rooms list down.
//
// CORRECTED 2026-09-24: the port used to run an invented teardown here (bLoaded = false, the
// camera/setpoint/hotspot/ambient-script lists freed, several strings nulled -- the strings
// without being freed). Its callers are freelancer.exe's FUN_00439b50 (.text:00439bda, per
// room when the player leaves a base) and Server.dll's BaseDB leave-base walk; in retail both
// reach this empty body.
void RoomData::unload()
{
}

// ---------------------------------------------------------------------------
// Extended RoomData methods
// ---------------------------------------------------------------------------

// 06277c10 ?init@RoomData@@QAEXIIPBD0@Z: +0x00 / +0x04, malloc_strcpy into +0x0c and +0x10
// (the previous strings are not freed), +0x08 = 0. read_Room_block (0626504f) passes the base
// id first and the room id second.
void RoomData::init(unsigned NewBaseId, unsigned NewRoomId,
                    char const* NewNickname, char const* NewFile)
{
    BaseId = NewBaseId;
    RoomId = NewRoomId;
    malloc_strcpy(&Nickname, NewNickname);
    malloc_strcpy(&File, NewFile);
    bLoaded = false;
}

// 06274540 ?clear@RoomData@@QAEXXZ. Resets every field except +0x08 and frees NOTHING: the
// strings are dropped and the record vectors emptied (erase(begin, end)) with their records
// still allocated -- destroy() is the freeing path. Defaults: both loop flags 1, ambient colour
// (1,1,1), the two light directions (0,1,0) with their directional flags 1, the camera at a
// zero position with identity 3x3 and 35 / 0.25 / 1e6.
void RoomData::clear()
{
    // 0627454a..06274552.
    BaseId = 0;
    RoomId = 0;
    Nickname = nullptr;
    File = nullptr;
    // 06274560..06274570: both 0x20 arrays.
    int Slot = 0;
    for (Slot = 0; Slot < 0x20; ++Slot)
    {
        Animations[Slot] = nullptr;
        AnimationLoop[Slot] = false;
    }
    // 06274578..062745ae.
    AnimationCount = 0;
    GoodscartScript = nullptr;
    GoodscartPriorityId = 0;
    bCutoffShadowZ = false;
    CutoffShadowZ = 0.0f;
    ExtraShadowHeightOffset = 0.0f;
    Music.clear();
    bMusicLoop = true;
    Ambient.clear();
    // 062745b3..06274730.
    AmbientColor.x = 1.0f;
    AmbientColor.y = 1.0f;
    AmbientColor.z = 1.0f;
    ShadowLightPosition.x = 0.0f;
    ShadowLightPosition.y = 0.0f;
    ShadowLightPosition.z = 0.0f;
    ShadowLightDirection.x = 0.0f;
    ShadowLightDirection.y = 1.0f;
    ShadowLightDirection.z = 0.0f;
    ShadowLightColor.x = 0.0f;
    ShadowLightColor.y = 0.0f;
    ShadowLightColor.z = 0.0f;
    GlareLightPosition.x = 0.0f;
    GlareLightPosition.y = 0.0f;
    GlareLightPosition.z = 0.0f;
    GlareLightDirection.x = 0.0f;
    GlareLightDirection.y = 1.0f;
    GlareLightDirection.z = 0.0f;
    bAmbientLoop = true;
    bShadowLight = false;
    bShadowLightWorldSpaceDir = true;
    bGlareLight = false;
    bGlareLightWorldSpaceDir = true;
    GlareLightColor.x = 0.0f;
    GlareLightColor.y = 0.0f;
    GlareLightColor.z = 0.0f;
    // 06274734..06274784: the camera name pointers dropped, the camera frame reset.
    CameraName = nullptr;
    Camera.Name = nullptr;
    ResetRoomCamera(Camera);
    // 0627476a..0627488a: every record vector emptied, the records kept.
    Cameras.clear();
    Lights.clear();
    Props.clear();
    Setpoints.clear();
    CharacterPlacements.clear();
    ShipPlacements.clear();
    Hotspots.clear();
    FlashLightSets.clear();
    AmbientScripts.clear();
}

// 062748d0 ?destroy@RoomData@@QAEXXZ. Frees what the room owns, then clear().
void RoomData::destroy()
{
    // 062748d4..06274923: the two init strings, the AnimationCount animation names, the
    // goodscart script.
    malloc_strfree(&Nickname);
    malloc_strfree(&File);
    int Slot = 0;
    for (Slot = 0; Slot < AnimationCount; ++Slot)
        malloc_strfree(&Animations[Slot]);
    malloc_strfree(&GoodscartScript);
    // 06274926..06274937.
    Music.clear();
    Ambient.clear();
    // 0627493c..06274973: the camera name, then the camera (its dtor body inlined).
    malloc_strfree(&CameraName);
    Camera.destroy();
    // 0627497a..06274b09: every record deleted through its own dtor (the cameras by
    // malloc_strfree + RoomCameraInfo::clear + free, the rest by their scalar deleting dtors
    // with flag 1), the slot nulled, then the vector emptied. The size is re-read each pass.
    PurgeRoomRecords(Cameras);
    PurgeRoomRecords(Lights);
    PurgeRoomRecords(Props);
    PurgeRoomRecords(Setpoints);
    PurgeRoomRecords(CharacterPlacements);
    PurgeRoomRecords(ShipPlacements);
    // 06274b0e..06274b2a: the purges FUN_06279310 / 062795d0 / 062798b0.
    PurgeRoomRecords(Hotspots);
    PurgeRoomRecords(FlashLightSets);
    PurgeRoomRecords(AmbientScripts);
    // 06274b34: tail-jump into clear().
    clear();
}

void RoomData::warn(INI_Reader* Reader, bool bError)
{
    // 06277090: Reader->get_file_name() and return -- the diagnostic was compiled out, nothing
    // is printed and bError is unused.
    (void)bError;
    Reader->get_file_name();
}

// 06277c70 ?fixup@RoomData@@QAEXJ@Z: setup_Camera, a HardpointSummary of the instance (none
// for -1), fixup_FlashLight_list's loop inlined, the summary destroyed (destroy + dtor).
void RoomData::fixup(long Instance)
{
    setup_Camera();
    HardpointSummary HpSum;
    if (Instance != -1)
        HpSum.analyze_instance(Instance);
    fixup_FlashLight_list(Instance, &HpSum);
    HpSum.destroy();
}

// 06277d20 ?unfixup@RoomData@@QAEXXZ: unsetup_Camera and unfixup_FlashLight_list, both inlined.
void RoomData::unfixup()
{
    unsetup_Camera();
    unfixup_FlashLight_list();
}

// 06277000: FlashLightSetInfo::fixup(Instance, HpSum, this) on every set of +0x1a4, the size
// re-read each pass, no null test.
void RoomData::fixup_FlashLight_list(long Instance, HardpointSummary const* HpSum)
{
    size_t SetIndex = 0;
    for (SetIndex = 0; SetIndex < FlashLightSets.size(); ++SetIndex)
        FlashLightSets[SetIndex]->fixup(Instance, HpSum, this);
}

// 06277050: every set's +0xe28 cursor = 0 (FlashLightSetInfo::unfixup inlined), no null test.
void RoomData::unfixup_FlashLight_list()
{
    size_t SetIndex = 0;
    for (SetIndex = 0; SetIndex < FlashLightSets.size(); ++SetIndex)
        FlashLightSets[SetIndex]->unfixup();
}

// 06277960 ?enumerate_CharacterPlacement_ini@RoomData@@QBEXP6AXHPAVINI_Reader@@PAX@Z1@Z. The
// room ini (+0x10 through GetFileSysPath with DALib::Data) opened again; for every
// [CharacterPlacement] header Callback(block index, reader, Context), the index counting only
// those blocks. An unopenable file calls nothing.
void RoomData::enumerate_CharacterPlacement_ini(
    void(__cdecl* Callback)(int, INI_Reader*, void*), void* Context) const
{
    char FullPath[kPathChars];
    GetFileSysPath(FullPath, File, DALib::Data);

    INI_Reader Reader;
    if (!Reader.open(FullPath, false))
        return;

    int BlockIndex = 0;
    while (Reader.read_header())
    {
        if (Reader.is_header("CharacterPlacement"))
        {
            Callback(BlockIndex, &Reader, Context);
            ++BlockIndex;
        }
    }
    Reader.close();
}

RoomCameraInfo const* RoomData::get_camera_info() const
{
    return &Camera;
}

// 06275490 ?read_RoomInfo_block@RoomData@@AAEXPAVINI_Reader@@@Z -- [Room] / [Room_Info].
// Every obsolete key only warns; the fall-through of an unknown key calls get_file_name and
// drops the result (06275838).
void RoomData::read_RoomInfo_block(INI_Reader* Reader)
{
    while (Reader->read_value())
    {
        if (Reader->is_value("set_script"))
        {
            // 062754c2..062754cf: the value goes straight to read_set_script, nothing is kept.
            read_set_script(Reader->get_value_string());
        }
        else if (Reader->is_value("mesh"))
        {
            FDUMP(0x100002,
                  "%s(%d) : *** WARNING: mesh is obsolete in %s, use the setscript to specify the "
                  "room mesh\n",
                  kRoomDataSource, 0x3d7, Reader->get_file_name());
        }
        else if (Reader->is_value("animation"))
        {
            // 0627551e..06275552: slot AnimationCount, no bound check (a 33rd name runs into
            // the loop-flag array, as in retail), loop flag 1.
            malloc_strcpy(&Animations[AnimationCount], Reader->get_value_string());
            AnimationLoop[AnimationCount] = true;
            ++AnimationCount;
        }
        else if (Reader->is_value("animation_oneshot"))
        {
            // 06275567..0627559a: as "animation" with loop flag 0.
            malloc_strcpy(&Animations[AnimationCount], Reader->get_value_string());
            AnimationLoop[AnimationCount] = false;
            ++AnimationCount;
        }
        else if (Reader->is_value("background_script"))
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: background_script is obsolete in %s, use scene\n",
                  kRoomDataSource, 0x3f5, Reader->get_file_name());
        }
        else if (Reader->is_value("background_script_oneshot"))
        {
            FDUMP(0x100002,
                  "%s(%d) : *** WARNING: background_script_oneshot is obsolete in %s, use scene\n",
                  kRoomDataSource, 0x3f9, Reader->get_file_name());
        }
        else if (Reader->is_value("mission_vendor_type"))
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: mission_vendor_type is obsolete in %s\n",
                  kRoomDataSource, 0x3fd, Reader->get_file_name());
        }
        else if (Reader->is_value("goodscart_script"))
        {
            // 0627565f..062756ad: the script, then value 1 (if any) through
            // RtcSlider::validate_setting(priority, file name) (0x0627a710, result dropped) and
            // CreateID; 0 without one.
            malloc_strcpy(&GoodscartScript, Reader->get_value_string());
            GoodscartPriorityId = 0;
            if (!Reader->is_value_empty(1))
            {
                char const* const PriorityName = Reader->get_value_string(1);
                RtcSlider::validate_setting(PriorityName, Reader->get_file_name());
                GoodscartPriorityId = CreateID(PriorityName);
            }
        }
        else if (Reader->is_value("goodscart_script_fighter"))
        {
            FDUMP(0x100002,
                  "%s(%d) : *** WARNING: goodscart_script_fighter is obsolete in %s, use "
                  "goodscart_script\n",
                  kRoomDataSource, 0x410, Reader->get_file_name());
        }
        else if (Reader->is_value("goodscart_script_elite"))
        {
            FDUMP(0x100002,
                  "%s(%d) : *** WARNING: goodscart_script_elite is obsolete in %s, use "
                  "goodscart_script\n",
                  kRoomDataSource, 0x414, Reader->get_file_name());
        }
        else if (Reader->is_value("goodscart_script_freighter"))
        {
            FDUMP(0x100002,
                  "%s(%d) : *** WARNING: goodscart_script_freighter is obsolete in %s, use "
                  "goodscart_script\n",
                  kRoomDataSource, 0x418, Reader->get_file_name());
        }
        else if (Reader->is_value("cutoff_shadow_z"))
        {
            bCutoffShadowZ = true;
            CutoffShadowZ = Reader->get_value_float(0);
        }
        else if (Reader->is_value("extra_shadow_height_offset"))
        {
            ExtraShadowHeightOffset = Reader->get_value_float(0);
        }
        else if (Reader->is_value("news_material"))
        {
            FDUMP(0x100001, "%s(%d) : *** ERROR: news_material is obsolete in %s\n",
                  kRoomDataSource, 0x427, Reader->get_file_name());
        }
        else if (Reader->is_value("is_outside"))
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: is_outside is obsolete in %s\n",
                  kRoomDataSource, 0x42b, Reader->get_file_name());
        }
        else if (Reader->is_value("scene"))
        {
            read_ambient_script(Reader);
        }
        else
        {
            Reader->get_file_name();
        }
    }
}

// 062752b0 ?read_ambient_script@RoomData@@AAEXPAVINI_Reader@@@Z -- one
// "scene = [all,] ambient|traffic|once, script.thn [, priority]" line, one new
// AmbientScriptInfo on +0x1b4 per accepted line.
void RoomData::read_ambient_script(INI_Reader* Reader)
{
    // 062752c4..062752f5: up to four values, each strcpy'd unbounded into a 0x100-byte stack
    // slot (a longer value overruns the next slot, as in retail); the first empty value stops.
    char Values[4][0x100];
    int ValueCount = 0;
    while (ValueCount < 4 && !Reader->is_value_empty(ValueCount))
    {
        strcpy(Values[ValueCount], Reader->get_value_string(ValueCount));
        ++ValueCount;
    }

    // 062752f7..0627531a: a leading "all" (DACOM stricmp) is dropped.
    int First = 0;
    if (ValueCount >= 1 && CallDacomStricmp(Values[0], "all") == 0)
        First = 1;
    ValueCount -= First;
    if (ValueCount != 2 && ValueCount != 3)
    {
        FDUMP(0x100001,
              "%s(%d) : *** ERROR: Invalid scene = specification in [Room] block in file %s\n"
              "Expecting: scene = ambient|traffic|once, script.thn, [priority]\n",
              kRoomDataSource, 0x38a, Reader->get_file_name());
        return;
    }

    // 0627535d..06275374: the type, the script and, with three values, the priority.
    char const* const TypeName = Values[First];
    char const* const ScriptPath = Values[First + 1];
    char const* PriorityName = nullptr;
    if (ValueCount == 3)
        PriorityName = Values[First + 2];

    // 0627537b..0627547e.
    int Type = 0;
    if (CallDacomStricmp(TypeName, "ambient") == 0)
    {
        Type = 1;
    }
    else if (CallDacomStricmp(TypeName, "traffic") == 0)
    {
        Type = 2;
        FDUMP(0x100002, "%s(%d) : *** WARNING: traffic ambient is obsolete in room .ini %s\n",
              kRoomDataSource, 0x3a3, Reader->get_file_name());
    }
    else if (CallDacomStricmp(TypeName, "once") == 0)
    {
        Type = 3;
    }
    else
    {
        FDUMP(0x100001,
              "%s(%d) : *** ERROR: Invalid scene = specification in [Room] block in file %s\n"
              "Expecting: scene = ambient|traffic|once, script.thn\n",
              kRoomDataSource, 0x3ad, Reader->get_file_name());
        return;
    }

    // 0627539b..062753bd: RtcSlider::validate_setting(priority, file name) (0x0627a710, result
    // dropped), then CreateID; 0 without a priority.
    unsigned Priority = 0;
    if (PriorityName != nullptr)
    {
        RtcSlider::validate_setting(PriorityName, Reader->get_file_name());
        Priority = CreateID(PriorityName);
    }

    // 062753bf..062753e4: FUN_06279920 appends a zeroed node, then type, script, priority.
    AmbientScriptInfo* const Script = AppendRoomRecord(AmbientScripts);
    Script->Type = Type;
    malloc_strcpy(&Script->Path, ScriptPath);
    Script->Priority = Priority;
}

// 06275aa0 ?read_Camera_block@RoomData@@AAEXPAVINI_Reader@@@Z -- [Camera]: the name into +0x1c4
// (malloc_strcpy, the previous one not freed); the rest only warns.
void RoomData::read_Camera_block(INI_Reader* Reader)
{
    while (Reader->read_value())
    {
        if (Reader->is_value("name"))
            malloc_strcpy(&CameraName, Reader->get_value_string());
        else if (Reader->is_value("index"))
            FDUMP(0x100002, "%s(%d) : *** WARNING: index is obsolete in %s, use a camera name\n",
                  kRoomDataSource, 0x4d7, Reader->get_file_name());
        else if (Reader->is_value("near_plane"))
            FDUMP(0x100002,
                  "%s(%d) : *** WARNING: near_plane is obsolete in %s, use a camera name\n",
                  kRoomDataSource, 0x4db, Reader->get_file_name());
        else if (Reader->is_value("far_plane"))
            FDUMP(0x100002,
                  "%s(%d) : *** WARNING: far_plane is obsolete in %s, use a camera name\n",
                  kRoomDataSource, 0x4df, Reader->get_file_name());
        else
            Reader->get_file_name();
    }
}

// 06275860 ?read_Sound_block@RoomData@@AAEXPAVINI_Reader@@@Z -- [Sound] / [Room_Sound]:
// "music" / "ambient" = id [, oneshot]. The id is CreateID'd straight into the ID_String; the
// loop flag is set and cleared again by a DACOM stricmp match of "oneshot".
void RoomData::read_Sound_block(INI_Reader* Reader)
{
    while (Reader->read_value())
    {
        if (Reader->is_value("music"))
        {
            Music.Id = CreateID(Reader->get_value_string(0));
            bMusicLoop = true;
            if (!Reader->is_value_empty(1) &&
                CallDacomStricmp(Reader->get_value_string(1), "oneshot") == 0)
                bMusicLoop = false;
        }
        else if (Reader->is_value("ambient"))
        {
            Ambient.Id = CreateID(Reader->get_value_string(0));
            bAmbientLoop = true;
            if (!Reader->is_value_empty(1) &&
                CallDacomStricmp(Reader->get_value_string(1), "oneshot") == 0)
                bAmbientLoop = false;
        }
        else
        {
            Reader->get_file_name();
        }
    }
}

void RoomData::read_CharacterPlacement_block(INI_Reader* Reader)
{
    // 06276540. The block does not create a placement: the placements are made from the
    // setpoints by read_set_script (06278e20); this block only gives an existing one its
    // start script.
    char* Name = nullptr;
    char* StartScript = nullptr;
    while (Reader->read_value())
    {
        // 06276568-06276626. A repeated key overwrites the local without freeing it (retail).
        if (Reader->is_value("name"))
            malloc_strcpy(&Name, Reader->get_value_string());
        else if (Reader->is_value("start_script"))
            malloc_strcpy(&StartScript, Reader->get_value_string());
        else if (Reader->is_value("zoom_in"))
            FDUMP(0x100001, "%s(%d) : *** ERROR: zoom_in in file %s is obsolete\n",
                  kRoomDataSource, 0x635, Reader->get_file_name());
        else if (Reader->is_value("zoom_out"))
            FDUMP(0x100001, "%s(%d) : *** ERROR: zoom_out in file %s is obsolete\n",
                  kRoomDataSource, 0x639, Reader->get_file_name());
        else
            Reader->get_file_name(); // 06276626: called, result unused
    }

    // 0627663a: reported, not a stop -- the lookup below still runs with the null name.
    if (Name == nullptr)
        FDUMP(0x100001, "%s(%d) : *** ERROR: [CharacterPlacement] in file %s did not supply name\n",
              kRoomDataSource, 0x647, Reader->get_file_name());

    // 0627666a-062766bb: DACOM stricmp(name, placement name) over the +0x174 vector, the last
    // match wins.
    CharPlaceInfo* Found = nullptr;
    size_t Index = 0;
    for (Index = 0; Index < CharacterPlacements.size(); ++Index)
    {
        if (CallDacomStricmp(Name, CharacterPlacements[Index]->Name) == 0)
            Found = CharacterPlacements[Index];
    }

    // 062766bd-062766fd: malloc_strcpy into +0x40 (the previous start script is not freed).
    if (Found != nullptr)
        malloc_strcpy(const_cast<char**>(&Found->StartScript), StartScript);
    else
        FDUMP(0x100001,
              "%s(%d) : *** ERROR: [CharacterPlacement] in file %s refers to setpoint %s which does "
              "not exist\n",
              kRoomDataSource, 0x656, Reader->get_file_name(), Name);

    malloc_strfree(&Name);
    malloc_strfree(&StartScript);
}

// 06276730 ?read_PlayerShipPlacement_block@RoomData@@AAEXPAVINI_Reader@@@Z. A placement is
// appended to +0x184 first (FUN_06279120: new 0x40 + ShipPlaceInfo::clear), so even an empty
// block adds one; +0x34 = 1, +0x35 = 0. "name" names a room setpoint (find_setpoint inlined:
// file name first, then the value, the last DACOM stricmp match, the 0x5a8 error on a miss):
// its name fills an unset +0x00 and its frame +0x04. The scripts are malloc_strcpy'd.
void RoomData::read_PlayerShipPlacement_block(INI_Reader* Reader)
{
    ShipPlaceInfo* const Place = AppendRoomRecord(ShipPlacements);
    Place->bIsPlayerShip = true;
    Place->bIsForSale = false;

    while (Reader->read_value())
    {
        if (Reader->is_value("name"))
        {
            char const* const FileName = Reader->get_file_name();
            char const* const SetpointName = Reader->get_value_string();
            SetpointInfo const* const Setpoint = find_setpoint(SetpointName, FileName);
            if (Setpoint != nullptr)
            {
                if (Place->Nickname == nullptr)
                    malloc_strcpy(const_cast<char**>(&Place->Nickname), Setpoint->Name);
                Place->Frame = Setpoint->Frame;
            }
        }
        else if (Reader->is_value("landing_script"))
        {
            malloc_strcpy(const_cast<char**>(&Place->LandingScript), Reader->get_value_string());
        }
        else if (Reader->is_value("launching_script"))
        {
            malloc_strcpy(const_cast<char**>(&Place->LaunchingScript),
                          Reader->get_value_string());
        }
        else
        {
            Reader->get_file_name();
        }
    }
}

// 062768b0 ?read_ForSaleShipPlacement_block@RoomData@@AAEXPAVINI_Reader@@@Z (read_from_ini
// inlines the same body at 06277397..06277464): as the player placement with +0x34 = 0,
// +0x35 = 1 and only the "name" key.
void RoomData::read_ForSaleShipPlacement_block(INI_Reader* Reader)
{
    ShipPlaceInfo* const Place = AppendRoomRecord(ShipPlacements);
    Place->bIsPlayerShip = false;
    Place->bIsForSale = true;

    while (Reader->read_value())
    {
        if (Reader->is_value("name"))
        {
            char const* const FileName = Reader->get_file_name();
            char const* const SetpointName = Reader->get_value_string();
            SetpointInfo const* const Setpoint = find_setpoint(SetpointName, FileName);
            if (Setpoint != nullptr)
            {
                if (Place->Nickname == nullptr)
                    malloc_strcpy(const_cast<char**>(&Place->Nickname), Setpoint->Name);
                Place->Frame = Setpoint->Frame;
            }
        }
        else
        {
            Reader->get_file_name();
        }
    }
}

void RoomData::read_Hotspot_block(INI_Reader* Reader)
{
    // Retail 0x169d0. The "behavior=" value is looked up against this fixed exit-door option
    // table (Common.dll.data 0x63eabc8, 12 entries of {name, value}); select_option_from_string
    // returns 0xffffffff (i.e. Behavior stays -1 via the int cast) when the text doesn't match.
    // .data:063eabc8, in table order.
    static const option_spec kExitDoorBehaviors[] = {
        {"ExitDoor", 1},          {"NewsVendor", 2},        {"MissionVendor", 3},
        {"Launch", 4},            {"StartDealer", 5},       {"StartShipDealer", 7},
        {"FrontDesk", 6},         {"StartEquipDealer", 8},  {"MoveRight", 9},
        {"MoveLeft", 10},         {"VirtualRoom", 11},      {"Repair", 12},
    };

    // Retail pushes the new (still-empty) entry into the vector immediately (via the shared
    // "allocate+append" helper FUN_06279400), then fills it in during the value loop -- so an
    // entry is added for every [Hotspot] block, even one with no "name=" or an otherwise empty
    // body.
    RoomButtonInfo& Button = *AppendRoomRecord(Hotspots);

    while (Reader->read_value())
    {
        if (Reader->is_value("name"))
            Button.Name = MallocCopy(Reader->get_value_string());
        else if (Reader->is_value("behavior"))
        {
            char const* Text = MallocCopy(Reader->get_value_string());
            Button.Behavior = static_cast<int>(select_option_from_string(
                const_cast<char*>(Text), kExitDoorBehaviors,
                static_cast<int>(sizeof(kExitDoorBehaviors) / sizeof(kExitDoorBehaviors[0])),
                Reader->get_file_name()));
            malloc_strfree(const_cast<char**>(&Text));
        }
        // 06276a80 / 06276abf: %s is the ADDRESS of INI_Reader::get_name (see
        // IniReaderGetNameCode) -- very strange, but retail.
        else if (Reader->is_value("zoom_in"))
            FDUMP(0x100001, "%s(%d) : *** ERROR: zoom_in is obsolete in ini file %s\n",
                  kRoomDataSource, 0x6e7, IniReaderGetNameCode());
        else if (Reader->is_value("zoom_out"))
            FDUMP(0x100001, "%s(%d) : *** ERROR: zoom_out is obsolete in ini file %s\n",
                  kRoomDataSource, 0x6eb, IniReaderGetNameCode());
        else if (Reader->is_value("room_switch"))
            Button.RoomSwitch = MallocCopy(Reader->get_value_string());
        else if (Reader->is_value("virtual_room"))
            Button.VirtualRoom = MallocCopy(Reader->get_value_string());
        else if (Reader->is_value("set_virtual_room"))
            Button.SetVirtualRoom = MallocCopy(Reader->get_value_string());
        // 06276b6c / 06276b8d: get_value_float(0), MSVCRT _ftol (64-bit truncation, low dword
        // kept), OR'ed in.
        else if (Reader->is_value("state_send"))
            // The ftol runs on the unrounded x87 value: value_num is get_value_float's read
            // without the float rounding.
            Button.StateSend |= static_cast<unsigned int>(static_cast<__int64>(Reader->value_num(0)));
        else if (Reader->is_value("state_read"))
            Button.StateRead |= static_cast<unsigned int>(static_cast<__int64>(Reader->value_num(0)));
        else if (Reader->is_value("state_transition"))
        {
            // 06276baa-06276bd5: first empty slot of the four takes the value; all four in use
            // -> the value is dropped.
            int Slot = 0;
            for (Slot = 0; Slot < 4; ++Slot)
            {
                if (Button.StateTransition[Slot] == nullptr)
                {
                    Button.StateTransition[Slot] = MallocCopy(Reader->get_value_string());
                    break;
                }
            }
        }
        else
            Reader->get_file_name(); // 06276bd7: called, result unused
    }
}

void RoomData::read_FlashLightSet_block(INI_Reader* Reader)
{
    // Retail 0x06276c00-0x06276de1. Appends ONE FlashLightSetInfo to the vector at
    // RoomData+0x1a4 (06276c05 ADD ECX,0x1a4 / 06276c0b CALL FUN_06279670) and stamps
    // Mode = 1 (06276c18 MOV [EDI+0x1c],0x1) before reading the block's keys.
    FlashLightSetInfo* const Set = AppendRoomRecord(FlashLightSets);
    Set->Mode = 1;
    ReadFlashLightKeys(*Reader, *Set, false);
}

void RoomData::read_FlashLightLine_block(INI_Reader* Reader)
{
    // Retail 0x06276df0-0x06276ffb. Identical to read_FlashLightSet_block except that
    // Mode = 2 (06276e08 MOV [EDI+0x1c],0x2) and one extra key, "numlights", is accepted
    // (06276fbe-06276fe0).
    FlashLightSetInfo* const Set = AppendRoomRecord(FlashLightSets);
    Set->Mode = 2;
    ReadFlashLightKeys(*Reader, *Set, true);
}

// 06275960 ?add_set_script_light@RoomData@@AAEXKABVCsys@@PBUThornLightProperties@@E@Z. A new
// RoomLightInfo on +0x144 (FUN_06278640): the frame, the blob's +0x0c direction into +0x30,
// the 0x1a dwords from the blob's +0x28 (the D3DLIGHT8) into +0x3c, Type into +0xa4. The
// first parameter is never read (06275960..062759ad).
void RoomData::add_set_script_light(unsigned long Id, Csys const& Frame,
                                    ThornLightProperties const* Props, unsigned char Type)
{
    (void)Id;
    RoomLightInfo* const Light = AppendRoomRecord(Lights);
    Light->Frame = Frame;
    memcpy(Light->Direction, Props->Direction, sizeof(Light->Direction));
    // The blob from its +0x28 (Type) to its end: the 0x1a dwords of the D3DLIGHT8.
    static_assert(sizeof(FThornLightProps) - offsetof(FThornLightProps, Type) == 0x1a * sizeof(unsigned),
                  "the light blob's D3DLIGHT8 tail is 0x1a dwords");
    memcpy(&Light->Mode, &Props->Type, 0x1a * sizeof(unsigned));
    Light->Type = Type;
}

// 062759b0 ?add_set_script_Prop@RoomData@@AAEXPBD0HABVCsys@@_N333EDK0K3@Z. A new RoomPropInfo
// on +0x154 (FUN_062788b0), its fields in record order, the three strings malloc_strcpy'd.
void RoomData::add_set_script_Prop(char const* Template, char const* Name, int Category,
                                   Csys const& Frame, bool bVisible, bool bLit, bool bFog,
                                   bool bRoomMain, unsigned char LightGroup, signed char Layer,
                                   unsigned long RenderType, char const* Loadout,
                                   unsigned long PriorityId, bool bRunningLights)
{
    RoomPropInfo* const Prop = AppendRoomRecord(Props);
    malloc_strcpy(const_cast<char**>(&Prop->Template), Template);
    malloc_strcpy(const_cast<char**>(&Prop->Name), Name);
    Prop->Category = Category;
    Prop->Frame = Frame;
    Prop->bVisible = bVisible;
    Prop->bLit = bLit;
    Prop->bFog = bFog;
    Prop->bRoomMain = bRoomMain;
    Prop->LightGroup = LightGroup;
    Prop->Layer = static_cast<unsigned char>(Layer);
    Prop->RenderType = RenderType;
    malloc_strcpy(const_cast<char**>(&Prop->Loadout), Loadout);
    Prop->PriorityId = PriorityId;
    Prop->bRunningLights = bRunningLights;
}

// 06275a50 ?add_set_script_Camera@RoomData@@AAEXKABVCsys@@PBDPBUThornCameraProperties@@@Z. A new
// RoomCameraInfo on +0x134 (FUN_062783e0): the name, the frame, and the camera properties'
// first three dwords (fov, near, far) into +0x34..+0x3c. No null test on the properties.
void RoomData::add_set_script_Camera(unsigned long Id, Csys const& Frame, char const* Name,
                                     ThornCameraProperties const* CamProps)
{
    (void)Id;
    RoomCameraInfo* const Info = AppendRoomRecord(Cameras);
    malloc_strcpy(const_cast<char**>(&Info->Name), Name);
    Info->Frame = Frame;
    Info->Fov = CamProps->Fovh;
    Info->NearPlane = CamProps->NearPlane;
    Info->FarPlane = CamProps->FarPlane;
}

// 06276180 ?add_set_script_Setpoint@RoomData@@AAEXKABVCsys@@PBDPBUThornEntity@@@Z. A new
// SetpointInfo on +0x164 (FUN_06278b30): the name, the frame, SetpointProperties::
// extract_from_name(name). The id and the entity are not read.
void RoomData::add_set_script_Setpoint(unsigned long Id, Csys const& Frame, char const* Name,
                                       ThornEntity const* Entity)
{
    (void)Id;
    (void)Entity;
    SetpointInfo* const Setpoint = AppendRoomRecord(Setpoints);
    malloc_strcpy(const_cast<char**>(&Setpoint->Name), Name);
    Setpoint->Frame = Frame;
    Setpoint->Props.extract_from_name(Name);
}

// 06275f30 ?unsetup_Camera@RoomData@@AAEXXZ: malloc_strfree of the camera's name, then its
// defaults (RoomCameraInfo::destroy inlined).
void RoomData::unsetup_Camera()
{
    Camera.destroy();
}

// 06275fa0 ?find_camera@RoomData@@QBEPBURoomCameraInfo@@PBD0@Z. DACOM stricmp(Name, camera name)
// over +0x134, the LAST match wins; none -> FDUMP warning 0x557 (script, name, the room ini).
RoomCameraInfo const* RoomData::find_camera(char const* Name, char const* ScriptName) const
{
    RoomCameraInfo const* Found = nullptr;
    size_t Index = 0;
    for (Index = 0; Index < Cameras.size(); ++Index)
    {
        if (CallDacomStricmp(Name, Cameras[Index]->Name) == 0)
            Found = Cameras[Index];
    }
    if (Found == nullptr)
        FDUMP(0x100002,
              "%s(%d) : *** WARNING: script %s calls for camera %s which does not exist in room "
              ".ini = %s\n",
              kRoomDataSource, 0x557, ScriptName, Name, File);
    return Found;
}

// 06276030 ?find_camera_generic@RoomData@@QBEPBURoomCameraInfo@@PBD0@Z. The pattern and every
// camera name are reduced as make_setpoint_name_generic does (inlined, into 0x400-byte stack
// buffers) and compared with DACOM stricmp(pattern, candidate); the LAST match wins. None ->
// FDUMP error 0x578 with the reduced pattern.
RoomCameraInfo const* RoomData::find_camera_generic(char const* Pattern, char const* ScriptName) const
{
    char Generic[0x400];
    make_setpoint_name_generic(Pattern, Generic);

    RoomCameraInfo const* Found = nullptr;
    size_t Index = 0;
    for (Index = 0; Index < Cameras.size(); ++Index)
    {
        char Candidate[0x400];
        make_setpoint_name_generic(Cameras[Index]->Name, Candidate);
        if (CallDacomStricmp(Generic, Candidate) == 0)
            Found = Cameras[Index];
    }
    if (Found == nullptr)
        FDUMP(0x100001,
              "%s(%d) : *** ERROR: script %s calls for a camera name matching */%s/*, but none "
              "exist in room .ini = %s\n",
              kRoomDataSource, 0x578, ScriptName, Generic, File);
    return Found;
}

// 062761c0 ?find_setpoint@RoomData@@QBEPBUSetpointInfo@@PBD0@Z. DACOM stricmp(Name, setpoint
// name) over +0x164, the LAST match wins; none -> FDUMP error 0x5a8.
SetpointInfo const* RoomData::find_setpoint(char const* Name, char const* ScriptName) const
{
    SetpointInfo const* Found = nullptr;
    size_t Index = 0;
    for (Index = 0; Index < Setpoints.size(); ++Index)
    {
        if (CallDacomStricmp(Name, Setpoints[Index]->Name) == 0)
            Found = Setpoints[Index];
    }
    if (Found == nullptr)
        FDUMP(0x100001,
              "%s(%d) : *** ERROR: script %s calls for setpoint %s which does not exist in room "
              ".ini = %s\n",
              kRoomDataSource, 0x5a8, ScriptName, Name, File);
    return Found;
}

// 06276250 ?find_setpoint_generic@RoomData@@QBEPBUSetpointInfo@@PBD0@Z. As find_camera_generic
// over +0x164; the miss error (0x5c8) names the pattern as given, not its reduction.
SetpointInfo const* RoomData::find_setpoint_generic(char const* Pattern, char const* ScriptName) const
{
    char Generic[0x400];
    make_setpoint_name_generic(Pattern, Generic);

    SetpointInfo const* Found = nullptr;
    size_t Index = 0;
    for (Index = 0; Index < Setpoints.size(); ++Index)
    {
        char Candidate[0x400];
        make_setpoint_name_generic(Setpoints[Index]->Name, Candidate);
        if (CallDacomStricmp(Generic, Candidate) == 0)
            Found = Setpoints[Index];
    }
    if (Found == nullptr)
        FDUMP(0x100001,
              "%s(%d) : *** ERROR: script %s calls for setpoint %s which does not exist in room "
              ".ini = %s\n",
              kRoomDataSource, 0x5c8, ScriptName, Pattern, File);
    return Found;
}

// 062763b0 ?find_CharacterPlacement@RoomData@@AAEPAUCharPlaceInfo@@PBD@Z. DACOM stricmp(Name,
// placement name) over +0x174, the LAST match wins, no message.
CharPlaceInfo* RoomData::find_CharacterPlacement(char const* Name)
{
    CharPlaceInfo* Found = nullptr;
    size_t Index = 0;
    for (Index = 0; Index < CharacterPlacements.size(); ++Index)
    {
        if (CallDacomStricmp(Name, CharacterPlacements[Index]->Name) == 0)
            Found = CharacterPlacements[Index];
    }
    return Found;
}

// 06276420 ?apply_setpoint@RoomData@@AAEPBUSetpointInfo@@PBDPAPADPAVCsys@@0@Z. find_setpoint
// inlined (the same 0x5a8 error and a null return on a miss); on a hit the setpoint's name is
// malloc_strcpy'd into *OutName when OutName is given and *OutName is still null, and its
// frame copied into *OutFrame (no null test on OutFrame).
SetpointInfo const* RoomData::apply_setpoint(char const* Name, char** OutName,
                                             Csys* OutFrame, char const* ScriptName)
{
    SetpointInfo const* const Setpoint = find_setpoint(Name, ScriptName);
    if (Setpoint == nullptr)
        return nullptr;
    if (OutName != nullptr && *OutName == nullptr)
        malloc_strcpy(OutName, Setpoint->Name);
    *OutFrame = Setpoint->Frame;
    return Setpoint;
}

// 062764e0 ?make_setpoint_name_generic@RoomData@@CAXPBDPAD@Z. The part between the first and
// the last '/': strchr(Name, '/'), strcpy of the rest (unbounded), strrchr cut. A name with no
// '/', or with only one, yields "" (.rdata 063997b0).
void __cdecl RoomData::make_setpoint_name_generic(char const* Name, char* OutGeneric)
{
    char const* const FirstSlash = strchr(Name, '/');
    if (FirstSlash != nullptr)
    {
        strcpy(OutGeneric, FirstSlash + 1);
        char* const LastSlash = strrchr(OutGeneric, '/');
        if (LastSlash != nullptr)
        {
            *LastSlash = '\0';
            return;
        }
    }
    strcpy(OutGeneric, "");
}
