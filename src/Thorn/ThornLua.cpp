// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "portable_integer_conversion.h"
#include "portable_vector_load.h"
#include "Thorn.h"
#include "readfile.h"
#include "ThornScene.h"
#include "ThornModernAdapters.h"
#include "LuaTableProbe.h"
#include "PortableComparison.h"
#include "LuaPropertyMath.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <new>
#include <memory>
#include <map>
#include <string>
#include <cstdint>

extern "C"
{
#include "lua.h"
#include "lualib.h"
}

// Lua 3.2.2 glue — DllMain lua_open/lua_setstate (FUN_06f24c80 / FUN_06f24cb0)
// and IScriptEngine slot 4 load (FUN_06f250e0). Do not put spline/curve math here.

namespace
{
    const int kFdumpError = 0x100001;
    const char* const kLuaParserCpp = "LuaScriptParser.cpp";

    lua_State* G_ThornLuaState = nullptr;
    bool G_bLuaOpened = false;
    bool G_bLuaConstantsRegistered = false; // DAT_06f61e08, module lifetime.
    lua_Object G_ThornLuaNil = LUA_NOOBJECT; // DAT_06f61de8, parser fallback object.
    FThornScene* G_LoadScene = nullptr;
    int G_LoadEntityHandle = -1; // DAT_06f5e1e4: actual Slot35 output alias.
    IScriptEngine* G_LoadScriptEngine = nullptr; // DAT_06f61dec, borrowed module state.
    IThornComContext* G_LoadComContext = nullptr; // DAT_06f61e00, borrowed current context; nested Builds replace it.
    // DAT_06f61dd4..61de7: module-lifetime name -> last published handle.
    // 24a30 constructs it, 24a60 destroys it; loads do not clear this map.
    std::map<std::string, int> G_LuaEntityHandles;

    bool FindLuaEntityHandle(const char* Name, int* OutHandle)
    {
        const std::map<std::string, int>::const_iterator Found = G_LuaEntityHandles.find(Name);
        if (Found == G_LuaEntityHandles.end())
            return false;
        *OutHandle = Found->second;
        return true;
    }
    unsigned int G_LoadDurationMs = 0; // DAT_06f61df8, SCENE descriptor duration.
    unsigned int G_TableEntryCount = 0; // DAT_06f61e04, first-pass table entries.

    // LAB_06f25320: count argument 2 only when it is a table.
    void CountTableEntry()
    {
        if (lua_istable(lua_lua2C(2)) != 0)
            ++G_TableEntryCount;
    }


    void FdumpLuaError(int Line, const char* Message);
    void LuaErrorMethod();

    lua_Object GetField(lua_Object Table, const char* Key)
    {
        lua_pushobject(Table);
        lua_pushstring(const_cast<char*>(Key));
        return lua_gettable();
    }

    lua_Object GetIndex(lua_Object Table, double Index)
    {
        lua_pushobject(Table);
        lua_pushnumber(Index);
        return lua_gettable();
    }

    // MSVCRT _ftol saves CW, forces RC=11 (chop), FISTP qword, restores CW,
    // and returns EAX (low 32 bits). Qword indefinite on invalid input is
    // 0x8000000000000000, whose low word is zero.
    int RetailFtol(double Value)
    {
        if (!(Value >= -9223372036854775808.0 && Value < 9223372036854775808.0))
            return 0;
        return static_cast<int>(static_cast<unsigned int>(static_cast<long long>(Value)));
    }

    // Scale and truncate without an intermediate binary64 product store.
    int LuaMillisecondsToRetailInt(lua_Object Object)
    {
        const double Number = lua_getnumber(Object);
        return ThornLuaPropertyMath::Milliseconds(Number);
    }

    // Retail per-type constructor tails use a 32-bit ADD. Preserve its bits
    // for overflowing Lua times without signed C++ arithmetic.
    int AddEventMilliseconds(int StartMs, int DurationMs)
    {
        const unsigned int SumBits = static_cast<unsigned int>(StartMs) +
                                     static_cast<unsigned int>(DurationMs);
        int Sum;
        std::memcpy(&Sum, &SumBits, sizeof(Sum));
        return Sum;
    }

    int LuaNumberToRetailInt(lua_Object Object)
    {
        const double Number = lua_getnumber(Object);
        return static_cast<int>(static_cast<unsigned int>(PortableIntegerConversion::ToInt64Bits(&Number)));
    }

    // The inline LIGHT vectors use three float locals, then integer copies.
    __declspec(noinline) void StoreLuaNumberFloat(lua_Object Object, float* Destination)
    {
        *Destination = static_cast<float>(lua_getnumber(Object));
    }

    // 2819c..281bf loads the next float before publishing the preceding int.
    void StoreLightColorIntegers(const float* Source, int* Output)
    {
        PortableIntegerConversion::StoreFloatTriplet(Source, Output, false);
    }

    void ScaleLightAngle(const float* Source, float* Destination)
    {
        // Retail 06f286e2/06f2871b: load float, multiply by raw 3c8efa35,
        // then store float. IA32 compilation preserves these x87 stages.
        *Destination = *Source * 0.01745329238474369f;
    }

    // Retail parser readers branch on the parent table before each lookup.
    lua_Object GetFieldOrNil(lua_Object Table, const char* Key)
    {
        return lua_istable(Table) != 0 ? GetField(Table, Key) : G_ThornLuaNil;
    }

    lua_Object GetIndexOrNil(lua_Object Table, double Index)
    {
        return lua_istable(Table) != 0 ? GetIndex(Table, Index) : G_ThornLuaNil;
    }

    // FUN_06f2e140: table/key lookup, cached fallback, nil gate, _ftol.
    bool ReadRetailIntField(lua_Object Table, const char* Key, int* OutValue)
    {
        const lua_Object Value = GetFieldOrNil(Table, Key);
        if (lua_isnil(Value) != 0)
            return false;
        *OutValue = LuaNumberToRetailInt(Value);
        return true;
    }

    // FUN_06f2e060: the float store happens before caller-side conversion.
    bool ReadRetailFloatField(lua_Object Table, const char* Key, float* OutValue)
    {
        const lua_Object Value = GetFieldOrNil(Table, Key);
        if (lua_isnil(Value) != 0)
            return false;
        StoreLuaNumberFloat(Value, OutValue);
        return true;
    }

    char* DupCString(const char* Src)
    {
        if (Src == nullptr)
            return nullptr;
        const unsigned int Len = static_cast<unsigned int>(std::strlen(Src));
        char* Dst = new (std::nothrow) char[Len + 1];
        if (Dst == nullptr)
            return nullptr;
        std::memcpy(Dst, Src, Len + 1);
        return Dst;
    }

    char* DupLuaString(lua_Object Obj)
    {
        if (lua_isstring(Obj) == 0)
            return nullptr;
        return DupCString(lua_getstring(Obj));
    }

    bool ReadVec3(lua_Object Table, float* Out)
    {
        if (lua_istable(Table) == 0 || Out == nullptr)
            return false;
        Out[0] = static_cast<float>(lua_getnumber(GetIndex(Table, 1.0)));
        Out[1] = static_cast<float>(lua_getnumber(GetIndex(Table, 2.0)));
        Out[2] = static_cast<float>(lua_getnumber(GetIndex(Table, 3.0)));
        return true;
    }

    bool ReadRetailIntVec3(lua_Object Table, float* Out)
    {
        if (lua_istable(Table) == 0 || Out == nullptr)
            return false;
        Out[0] = static_cast<float>(LuaNumberToRetailInt(GetIndex(Table, 1.0)));
        Out[1] = static_cast<float>(LuaNumberToRetailInt(GetIndex(Table, 2.0)));
        Out[2] = static_cast<float>(LuaNumberToRetailInt(GetIndex(Table, 3.0)));
        return true;
    }

    int ReadAxisId(lua_Object Obj, int DefaultId)
    {
        if (lua_isnil(Obj) != 0 || lua_isnumber(Obj) == 0)
            return DefaultId;
        // The retail loader reaches this value through the same x87 FTOL
        // conversion used by FUN_06f2e140.  A C++ cast truncates toward zero,
        // which changes the result for fractional (and negative fractional)
        // script values before the range check.
        const int Axis = LuaNumberToRetailInt(Obj);
        if (Axis < 0 || Axis > 5)
            return DefaultId;
        return Axis;
    }

    bool ReadMat9(lua_Object Table, float* Out)
    {
        if (lua_istable(Table) == 0 || Out == nullptr)
            return false;
        for (int Row = 0; Row < 3; ++Row)
        {
            lua_Object RowObj = GetIndex(Table, static_cast<double>(Row + 1));
            if (lua_istable(RowObj) == 0)
                return false;
            Out[Row * 3 + 0] = static_cast<float>(lua_getnumber(GetIndex(RowObj, 1.0)));
            Out[Row * 3 + 1] = static_cast<float>(lua_getnumber(GetIndex(RowObj, 2.0)));
            Out[Row * 3 + 2] = static_cast<float>(lua_getnumber(GetIndex(RowObj, 3.0)));
        }
        return true;
    }

    // FUN_06f2a1e0: the component is temporary; QI publishes into owner+24.
    int CreateCurveFromLua(IParamCurve** Output, lua_Object CurveTable)
    {
        struct FCurveComponentOwner
        {
            IDAComponent* Object = nullptr;
            ~FCurveComponentOwner() noexcept(false)
            {
                if (Object != nullptr)
                {
                    Object->Release();
                    Object = nullptr;
                }
            }
        };

        ICOManager* Manager = DACOM_Acquire();
        FCurveComponentOwner Component;
        const lua_Object ClsidObj = GetFieldOrNil(CurveTable, "CLSID");
        const lua_Object Points = GetFieldOrNil(CurveTable, "points");
        if (lua_isnil(ClsidObj) != 0 || lua_isnil(Points) != 0)
            return -1;

        char Message[8192];
        const char* const DiagnosticSource = "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\THORN\\LuaScriptParser.cpp";
        FThornAggDesc14 Desc;
        Desc.Size = G_AggDescSizeThorn;
        Desc.NameAt4 = lua_getstring(ClsidObj);
        Desc.Outer = nullptr;
        Desc.InnerOut = nullptr;
        Desc.Field_0x10 = 0;
        if (Manager->CreateInstance(reinterpret_cast<AGGDESC*>(&Desc),
                                    reinterpret_cast<void**>(&Component.Object)) == GR_OK)
        {
            if (Component.Object->QueryInterface(Iids::ParamCurve,
                    reinterpret_cast<void**>(Output)) == GR_OK)
            {
                unsigned int Point = 1;
                lua_Object Row = GetIndexOrNil(Points, 1.0);
                while (lua_isnil(Row) == 0)
                {
                    const lua_Object Field1 = GetIndexOrNil(Row, 1.0);
                    const lua_Object Field2 = GetIndexOrNil(Row, 2.0);
                    const lua_Object Field3 = GetIndexOrNil(Row, 3.0);
                    float F2;
                    StoreLuaNumberFloat(Field3, &F2);
                    const lua_Object Field4 = GetIndexOrNil(Row, 4.0);
                    float F3;
                    StoreLuaNumberFloat(Field4, &F3);
                    // Raw2a438/2a445 both test EDI (field1), including
                    // the repeated call; field2 has no nil predicate.
                    if (lua_isnil(Field1) == 0 && lua_isnil(Field1) == 0)
                    {
                        float F1;
                        StoreLuaNumberFloat(Field2, &F1);
                        float F0;
                        StoreLuaNumberFloat(Field1, &F0);
                        // Numeric conversion cannot run Lua callbacks or change this curve.
                        (*Output)->AddKey(F0, F1, F2, F3);
                    }
                    ++Point;
                    Row = GetIndexOrNil(Points, static_cast<double>(Point));
                }
                return 0;
            }

            std::sprintf(Message, "THORN: Unable to query IParamCurve interface for %s.\n",
                         lua_getstring(ClsidObj));
            FDUMP(0x100002, "%s(%d) : WARNING:General:%s", DiagnosticSource, 0x978, Message);
        }
        // A failed QI also falls through to the create-failure warning.
        std::sprintf(Message, "THORN: Unable to create param curve %s.\n", lua_getstring(ClsidObj));
        FDUMP(0x100002, "%s(%d) : WARNING:General:%s", DiagnosticSource, 0x97b, Message);
        return -1;
    }

    void FdumpLuaError(int Line, const char* Message)
    {
        if (Message != nullptr)
            OutputDebugStringA(Message);
        if (FDUMP != nullptr)
        {
            FDUMP(kFdumpError, "%s(%d) : ERROR:General:%s", kLuaParserCpp, Line,
                  Message != nullptr ? Message : "");
        }
    }

    void SetGlobalNumber(const char* Name, double Value)
    {
        lua_pushnumber(Value);
        lua_setglobal(const_cast<char*>(Name));
    }

    void RegisterThornLuaConstants()
    {
        // FUN_06f24ce0: register once in the parser-selected VM, not DllMain.
        if (G_bLuaConstantsRegistered)
            return;
        lua_pushcfunction(&LuaErrorMethod);
        lua_seterrormethod();
        // FUN_06f24ce0 — names/IDs from listing. Dump aliases at 06f5e1e8 are not setglobal.
        static const char* const EntityNames[] = {
            "UNKNOWN_ENTITY", "COMPOUND",     "DEFORMABLE", "CAMERA",      "MONITOR",
            "LIGHT",          "SOUND",        "MARKER",     "HARDPOINT",   "SCENE",
            "SUB_SCENE",      "MOTION_PATH",  "DELETED",    "PSYS"};
        const int EntityCount = static_cast<int>(sizeof(EntityNames) / sizeof(EntityNames[0]));
        for (int Index = 0; Index < EntityCount; ++Index)
            SetGlobalNumber(EntityNames[Index], static_cast<double>(Index));

        struct FNamedId
        {
            const char* Name;
            int Id;
        };
        static const FNamedId EventIds[] = {
            {"UNDEFINED_EVENT", 0},
            {"START_MOTION", 10},
            {"START_SOUND", 3},
            {"START_IK", 11},
            {"START_PATH_ANIMATION", 6},
            {"ATTACH_ENTITY", 8},
            {"START_LIGHT_PROP_ANIM", 4},
            {"START_CAMERA_PROP_ANIM", 5},
            {"START_SPATIAL_PROP_ANIM", 7},
            {"SET_CAMERA", 2},
            {"CONNECT_HARDPOINTS", 9},
            {"START_SUB_SCENE", 12},
            {"USER_EVENT", 1},
            {"START_PSYS", 13},
            {"START_PSYS_PROP_ANIM", 14},
            {"START_AUDIO_PROP_ANIM", 15},
            {"START_FOG_PROP_ANIM", 16},
            {"START_REVERB_PROP_ANIM", 17},
            {"START_FLR_HEIGHT_ANIM", 18},
            {"SUBTITLE", 19},
        };
        const int EventCount = static_cast<int>(sizeof(EventIds) / sizeof(EventIds[0]));
        for (int Index = 0; Index < EventCount; ++Index)
            SetGlobalNumber(EventIds[Index].Name, static_cast<double>(EventIds[Index].Id));
        // PTR_s_UNDEFINED_06f5e1e8 (SET_MONITOR, START_LIGHT_PROPERTY_ANIM, …): dump only, not setglobal.

        static const char* const AxisNames[] = {"X_AXIS",     "Y_AXIS",     "Z_AXIS",
                                                "NEG_X_AXIS", "NEG_Y_AXIS", "NEG_Z_AXIS"};
        const int AxisCount = static_cast<int>(sizeof(AxisNames) / sizeof(AxisNames[0]));
        for (int Index = 0; Index < AxisCount; ++Index)
            SetGlobalNumber(AxisNames[Index], static_cast<double>(Index));

        SetGlobalNumber("REFERENCE", 1.0);
        SetGlobalNumber("HIDDEN", 16.0);
        SetGlobalNumber("FOG_PROPS_REMOVED", 32.0);
        SetGlobalNumber("SPATIAL", 2.0);
        SetGlobalNumber("STREAM", 4.0);
        SetGlobalNumber("LOOP", 8.0);
        SetGlobalNumber("LIT_AMBIENT", 2.0);
        SetGlobalNumber("LIT_DYNAMIC", 4.0);
        SetGlobalNumber("USE_SCRIPT_DURATION", 1.0);
        SetGlobalNumber("POSITION", 2.0);
        SetGlobalNumber("PATH_POSITION", 2.0);
        SetGlobalNumber("ORIENTATION", 4.0);
        SetGlobalNumber("LOOK_AT", 8.0);
        SetGlobalNumber("PARENT_CHILD", 64.0);
        SetGlobalNumber("ENTITY_RELATIVE", 32.0);
        SetGlobalNumber("ORIENTATION_RELATIVE", 128.0);
        SetGlobalNumber("ROOT", 0.0);
        SetGlobalNumber("PART", 2.0);
        SetGlobalNumber("HARDPOINT", 1.0);
        SetGlobalNumber("ADD_PATH", 6.0);
        SetGlobalNumber("LOOKAT_ENTITY", 8.0);
        SetGlobalNumber("STOP", 21.0);
        SetGlobalNumber("STOP_IK", 22.0);
        SetGlobalNumber("START", 23.0);
        SetGlobalNumber("PROPERTY_ANIM", 0.0);
        G_ThornLuaNil = lua_getglobal(const_cast<char*>(
            "I hope this doesnt exist because I am counting on it being nil"));
        SetGlobalNumber("F_NONE", 0.0);
        SetGlobalNumber("F_EXP", 1.0);
        SetGlobalNumber("F_EXP2", 2.0);
        SetGlobalNumber("F_LINEAR", 3.0);
        SetGlobalNumber("L_POINT", 1.0);
        SetGlobalNumber("L_SPOT", 2.0);
        SetGlobalNumber("L_DIRECT", 3.0);
        SetGlobalNumber("Y", 1.0);
        SetGlobalNumber("N", 0.0);
        G_bLuaConstantsRegistered = true;
    }

    void LuaErrorMethod()
    {
        // LAB_06f24c20 — lua_isstring(1) then FDUMP line 0x16a.
        lua_Object Param = lua_lua2C(1);
        if (lua_isstring(Param) == 0)
            return;
        char Line[8192];
        FThornModernAdapters::FormatDiagnostic(Line, "THORN: LUA error <%s>.\n", lua_getstring(Param));
        FDUMP(kFdumpError, "%s(%d) : ERROR:General:%s",
              "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\THORN\\LuaScriptParser.cpp",
              0x16a, Line);
    }

    void ForeachUserProp()
    {
        // LAB_06f289e0: value nil, key nil, then value string, in that order.
        if (G_LoadEntityHandle == -1)
            return;
        lua_Object KeyObj = lua_lua2C(1);
        lua_Object ValueObj = lua_lua2C(2);
        if (lua_isnil(ValueObj) != 0)
            return;
        if (lua_isnil(KeyObj) != 0)
            return;
        if (lua_isstring(ValueObj) == 0)
            return;
        const char* Value = lua_getstring(ValueObj);
        const char* Key = lua_getstring(KeyObj);
        // Nested loads may change the receiver; every engine uses the same implementation.
        G_LoadScriptEngine->Slot34SetUserProp(G_LoadEntityHandle, Key, Value);
    }

    void LoadSpatialProps(lua_Object Table, float* Pos, float* Orient, bool* bPos, bool* bOrient)
    {
        lua_Object Spatial = GetField(Table, "spatialprops");
        if (lua_istable(Spatial) == 0)
            return;
        lua_Object PosObj = GetField(Spatial, "pos");
        if (lua_istable(PosObj) != 0 && ReadVec3(PosObj, Pos) && bPos != nullptr)
            *bPos = true;
        lua_Object OrientObj = GetField(Spatial, "orient");
        if (lua_istable(OrientObj) != 0 && ReadMat9(OrientObj, Orient) && bOrient != nullptr)
            *bOrient = true;
    }

    // FUN_06f28a60 case 0xB builds a descriptor; the actual Slot35
    // constructor binds its runtime path later. No early ApplyDesc/QI here.
    lua_Object GetLightDoubleProbe(lua_Object Table, unsigned int Index);
    lua_Object GetLightFloatIndex(lua_Object Table, const float* Index);

    // ForeachEntity state0: queried Persist lives through factory/userprops.
    // Its normal destruction follows descriptor deletion (2a0d7..2a0ee).
    struct FParsedEntityPersistOwner
    {
        IPersistString* Object = nullptr;
        ~FParsedEntityPersistOwner() noexcept(false)
        {
            if (Object != nullptr)
            {
                Object->Release();
                Object = nullptr;
            }
        }
    };

    void InstantiateMotionPath(FThornEntitySnap* Descriptor, lua_Object PathTable,
                               FParsedEntityPersistOwner& Persist)
    {
        if (lua_istable(PathTable) == 0)
            return;
        ICOManager* Manager = DACOM_Acquire();
        struct FComponentOwner
        {
            IDAComponent* Object = nullptr;
            ~FComponentOwner() noexcept(false)
            {
                if (Object != nullptr)
                {
                    Object->Release();
                    Object = nullptr;
                }
            }
        } Component;

        const lua_Object TypeObject = GetFieldOrNil(PathTable, "path_type");
        const bool bExplicitType = lua_isnil(TypeObject) == 0;
        const char* FactoryName = bExplicitType ? lua_getstring(TypeObject) : "CV_CRSplinePath";
        Descriptor->MotionPath.PathType = FactoryName;
        FThornAggDesc14 Creation;
        Creation.Size = G_AggDescSizeThorn;
        Creation.NameAt4 = FactoryName;
        Creation.Outer = nullptr;
        Creation.InnerOut = nullptr;
        Creation.Field_0x10 = 0;
        const GENRESULT Created = Manager->CreateInstance(reinterpret_cast<AGGDESC*>(&Creation),
                                                          reinterpret_cast<void**>(&Component.Object));
        if (Created != GR_OK && !bExplicitType)
            return;
        if (Created == GR_OK)
            Component.Object->QueryInterface(Iids::PersistString, reinterpret_cast<void**>(&Persist.Object));
        Descriptor->MotionPath.MotionPath = Persist.Object;
        if (bExplicitType)
        {
            if (Persist.Object != nullptr)
            {
                const lua_Object Data = GetFieldOrNil(PathTable, "path_data");
                if (lua_isnil(Data) == 0)
                {
                    const char* Text = lua_getstring(Data);
                    // This descriptor is unpublished; Persist retains its interface through GC.
                    Descriptor->MotionPath.MotionPath->Slot3Load(Text);
                }
            }
            return;
        }

        const lua_Object Points = GetFieldOrNil(PathTable, "control_pts");
        if (lua_isnil(Points) != 0)
            return;
        unsigned int Point = 1;
        if (lua_isnil(GetIndexOrNil(Points, 1.0)) == 0)
        {
            do
            {
                ++Point;
            }
            while (lua_isnil(GetLightDoubleProbe(Points, Point)) == 0);
        }
        const unsigned int Count = Point - 1u;
        const unsigned int Bytes = Count * 12u;
        float* Packed = static_cast<float*>(::operator new(Bytes));
        float* Output = Packed;
        for (unsigned int Index = 1; Index <= Count; ++Index)
        {
            const lua_Object Row = GetLightDoubleProbe(Points, Index);
            bool bComplete = true;
            for (unsigned int Axis = 1; Axis <= 3; ++Axis)
            {
                if (lua_isnil(GetLightDoubleProbe(Row, Axis)) != 0)
                {
                    bComplete = false;
                    break;
                }
            }
            float Coordinates[3];
            if (bComplete)
            {
                // 29ab9..29af4: float-index reads AFTER the double probes.
                const float Indices[3] = {1.0f, 2.0f, 3.0f};
                for (int Axis = 0; Axis < 3; ++Axis)
                    StoreLuaNumberFloat(GetLightFloatIndex(Row, &Indices[Axis]), &Coordinates[Axis]);
            }
            else
                std::memset(Coordinates, 0, sizeof(Coordinates));
            std::memcpy(Output, Coordinates, sizeof(Coordinates));
            Output += 3;
        }
        struct FSplineOwner
        {
            ICRSpline* Spline = nullptr;
            ~FSplineOwner() noexcept(false)
            {
                if (Spline != nullptr)
                {
                    Spline->Release();
                    Spline = nullptr;
                }
            }
        } Spline;
        Component.Object->QueryInterface(Iids::CRSpline, reinterpret_cast<void**>(&Spline.Spline));
        if (Spline.Spline != nullptr)
            Spline.Spline->Slot4LoadPoints(Packed, Count);
        // Packed has only normal-path deletion; Spline then Component unwind.
        ::operator delete(Packed);
    }

    lua_Object GetLightDoubleProbe(lua_Object Table, unsigned int Index);
    lua_Object GetLightFloatIndex(lua_Object Table, const float* Index);
    void StoreSceneAmbientIntegers(const float* Source, void* Output);

    // 27cab/27d12: TEST AH,44h + JNP selects false only for equality.
    // Unordered/nonzero is true, with the original x87 comparison effects.
    unsigned char ReadFogByte(lua_Object Object)
    {
        const double Number = lua_getnumber(Object);
        return static_cast<unsigned char>(ThornPortableComparison::Nonzero(&Number));
    }

    void LoadFogKeys(lua_Object Fog, FThornFog8* Target, int* Mask)
    {
        int LoadedMask = 0;
        const lua_Object FogOn = GetFieldOrNil(Fog, "fogon");
        if (lua_isnil(FogOn) == 0)
        {
            Target->On = ReadFogByte(FogOn);
            LoadedMask |= 0x1;
        }
        const lua_Object FogTable = GetFieldOrNil(Fog, "fogtable");
        if (lua_isnil(FogTable) == 0)
        {
            Target->Table = ReadFogByte(FogTable);
            LoadedMask |= 0x2;
        }
        const lua_Object FogColor = GetFieldOrNil(Fog, "fogcolor");
        if (lua_isnil(FogColor) == 0)
        {
            bool bComplete = true;
            for (unsigned int Index = 1; Index <= 3; ++Index)
            {
                if (lua_isnil(GetLightDoubleProbe(FogColor, Index)) != 0)
                {
                    bComplete = false;
                    break;
                }
            }
            float Coordinates[3];
            if (bComplete)
            {
                const float NumericIndices[3] = {1.0f, 2.0f, 3.0f};
                for (int Index = 0; Index < 3; ++Index)
                    StoreLuaNumberFloat(GetLightFloatIndex(FogColor, &NumericIndices[Index]), &Coordinates[Index]);
            }
            else
                std::memset(Coordinates, 0, sizeof(Coordinates));
            // All numeric callbacks/FSTPfloat stores precede ANY RGB output.
            // The exact retained-Z ftol core is shared with SCENE ambient.
            StoreSceneAmbientIntegers(Coordinates, Target->Color);
            LoadedMask |= 0x4;
        }
        const lua_Object FogMode = GetFieldOrNil(Fog, "fogmode");
        if (lua_isnil(FogMode) == 0)
        {
            Target->Mode = LuaNumberToRetailInt(FogMode);
            LoadedMask |= 0x8;
        }
        const lua_Object FogStart = GetFieldOrNil(Fog, "fogstart");
        if (lua_isnil(FogStart) == 0)
        {
            StoreLuaNumberFloat(FogStart, &Target->Start);
            LoadedMask |= 0x10;
        }
        const lua_Object FogEnd = GetFieldOrNil(Fog, "fogend");
        if (lua_isnil(FogEnd) == 0)
        {
            StoreLuaNumberFloat(FogEnd, &Target->End);
            LoadedMask |= 0x20;
        }
        const lua_Object FogDensity = GetFieldOrNil(Fog, "fogdensity");
        if (lua_isnil(FogDensity) == 0)
        {
            StoreLuaNumberFloat(FogDensity, &Target->Density);
            LoadedMask |= 0x40;
        }
        if (Mask != nullptr)
            *Mask = LoadedMask;
    }

    // LIGHT color/diffuse and 28750 probes convert the unsigned index only
    // after the table predicate and lua_pushobject callback.
    lua_Object GetLightDoubleProbe(lua_Object Table, unsigned int Index)
    {
        return ThornLuaDetail::GetLightDoubleProbe(Table, Index, G_ThornLuaNil);
    }

    // FUN_06f27c10, shared by traced LIGHT/entity consumers. A pointer keeps the float
    // argument's bits intact until the retail FLD after lua_pushobject.
    lua_Object GetLightFloatIndex(lua_Object Table, const float* Index)
    {
        return ThornLuaDetail::GetLightFloatIndex(Table, Index, G_ThornLuaNil);
    }

    // Specular/ambient/direction convert their loop index before 27c10's
    // table predicate, even when that predicate selects the cached nil.
    lua_Object GetLightFloatProbe(lua_Object Table, unsigned int Index)
    {
        return ThornLuaDetail::GetLightFloatProbe(Table, Index, G_ThornLuaNil);
    }

    // FUN_06f28750: X/Y are float locals; Z remains ST0 until publication.
    bool ReadVec3Complete(lua_Object Table, float* Output)
    {
        for (unsigned int Index = 1; Index <= 3; ++Index)
        {
            const lua_Object Value = GetLightDoubleProbe(Table, Index);
            if (lua_isnil(Value) != 0)
            {
                std::memset(Output, 0, 3 * sizeof(float));
                return true;
            }
        }
        float Coordinates[2];
        StoreLuaNumberFloat(GetIndexOrNil(Table, 1.0), &Coordinates[0]);
        StoreLuaNumberFloat(GetIndexOrNil(Table, 2.0), &Coordinates[1]);
        const lua_Object Z = GetIndexOrNil(Table, 3.0);
        const double Number = lua_getnumber(Z);
        PortableVectorLoad::CopyMixed(Output, Coordinates, &Number);
        return true;
    }

    // ForeachEntity29d5d..2a010 publishes one column after its own callbacks.
    // Row1 probes use unsigned->double after pushobject; Row2/3 instead store
    // an unsigned->float index before 27c10's repeated table predicate.
    void ReadEntityOrientationColumn(lua_Object Table, int Column, float* Orientation)
    {
        float Coordinates[3];
        bool bComplete = true;
        for (unsigned int Index = 1; Index <= 3; ++Index)
        {
            const lua_Object Value = Column == 0
                ? GetLightDoubleProbe(Table, Index)
                : GetLightFloatProbe(Table, Index);
            if (lua_isnil(Value) != 0)
            {
                bComplete = false;
                break;
            }
        }
        if (bComplete)
        {
            const float Indices[3] = {1.0f, 2.0f, 3.0f};
            for (int Index = 0; Index < 3; ++Index)
                StoreLuaNumberFloat(GetLightFloatIndex(Table, &Indices[Index]), &Coordinates[Index]);
        }
        else
            std::memset(Coordinates, 0, sizeof(Coordinates));
        // 22d50 is three raw DWORD copies, not a floating-point reload.
        for (int Index = 0; Index < 3; ++Index)
            std::memcpy(&Orientation[Index * 3 + Column], &Coordinates[Index], sizeof(float));
    }

    // Actual entity-only SPATIAL block29c3d..2a010 runs after all typed tails.
    // Nonnil malformed vectors become zero; an absent orientation column is
    // left unchanged, while a present incomplete column is individually zeroed.
    void LoadEntitySpatialDesc(lua_Object Table, FThornEntitySnap* Descriptor)
    {
        const lua_Object Spatial = GetFieldOrNil(Table, "spatialprops");
        if (lua_isnil(Spatial) != 0)
            return;
        const lua_Object Position = GetFieldOrNil(Spatial, "pos");
        if (lua_isnil(Position) == 0)
        {
            float Coordinates[3];
            ReadVec3Complete(Position, Coordinates);
            std::memcpy(Descriptor->Pos, Coordinates, sizeof(Coordinates));
        }
        const lua_Object Orientation = GetFieldOrNil(Spatial, "orient");
        if (lua_isnil(Orientation) != 0)
            return;
        const double RowIndices[3] = {1.0, 2.0, 3.0};
        for (int Column = 0; Column < 3; ++Column)
        {
            const lua_Object Row = GetIndexOrNil(Orientation, RowIndices[Column]);
            if (lua_isnil(Row) == 0)
                ReadEntityOrientationColumn(Row, Column, Descriptor->Orient);
        }
    }

    // The inline vectors in SPATIAL event case269da use complete double
    // probes, then independent 27c10 numeric reads staged as float locals.
    void ReadSpatialEventVector(lua_Object Table, void* Output, unsigned int Count)
    {
        float Coordinates[4];
        bool bComplete = true;
        for (unsigned int Index = 1; Index <= Count; ++Index)
        {
            if (lua_isnil(GetLightDoubleProbe(Table, Index)) != 0)
            {
                bComplete = false;
                break;
            }
        }
        if (bComplete)
        {
            const float Indices[4] = {1.0f, 2.0f, 3.0f, 4.0f};
            for (unsigned int Index = 0; Index < Count; ++Index)
                StoreLuaNumberFloat(GetLightFloatIndex(Table, &Indices[Index]), &Coordinates[Index]);
        }
        else
            std::memset(Coordinates, 0, Count * sizeof(float));
        std::memcpy(Output, Coordinates, Count * sizeof(float));
    }

    // FUN_06f25340 SPATIAL: axisrot, pos, q_orient, else orient; nil and
    // table predicates differ by branch. No parser-time quaternion conversion.
    void LoadSpatialEventPose(lua_Object Props, FThornSpatialEventDesc* Descriptor)
    {
        const lua_Object Spatial = GetFieldOrNil(Props, "spatialprops");
        if (lua_isnil(Spatial) != 0)
            return;
        const lua_Object AxisRot = GetFieldOrNil(Spatial, "axisrot");
        if (lua_isnil(AxisRot) == 0 && lua_istable(AxisRot) != 0)
        {
            Descriptor->Mask |= kThornSpatialBitAxisRot;
            StoreLuaNumberFloat(GetIndexOrNil(AxisRot, 1.0), &Descriptor->AxisRotAngle);
            Descriptor->AxisRotAxis = LuaNumberToRetailInt(GetIndexOrNil(AxisRot, 2.0));
        }
        const lua_Object Position = GetFieldOrNil(Spatial, "pos");
        if (lua_isnil(Position) == 0)
        {
            Descriptor->Mask |= kThornSpatialBitPos;
            ReadSpatialEventVector(Position, Descriptor->Position, 3);
        }
        const lua_Object Quaternion = GetFieldOrNil(Spatial, "q_orient");
        if (lua_isnil(Quaternion) == 0)
        {
            Descriptor->Mask |= kThornSpatialBitOrient;
            ReadSpatialEventVector(Quaternion, &Descriptor->Quaternion, 4);
            Descriptor->Common.Flags |= kThornFlagQuatOrient;
            return;
        }
        const lua_Object Orientation = GetFieldOrNil(Spatial, "orient");
        if (lua_isnil(Orientation) == 0)
        {
            Descriptor->Mask |= kThornSpatialBitOrient;
            float Matrix[9];
            // The complete modern object supplies retained columns for absent rows.
            std::memcpy(Matrix, Descriptor->Orient, sizeof(Matrix));
            const double RowIndices[3] = {1.0, 2.0, 3.0};
            for (int Column = 0; Column < 3; ++Column)
            {
                const lua_Object Row = GetIndexOrNil(Orientation, RowIndices[Column]);
                if (lua_isnil(Row) == 0)
                {
                    float Coordinates[3];
                    ReadSpatialEventVector(Row, Coordinates, 3);
                    for (int Index = 0; Index < 3; ++Index)
                        std::memcpy(&Matrix[Index * 3 + Column], &Coordinates[Index], sizeof(float));
                }
            }
            std::memcpy(Descriptor->Orient, Matrix, sizeof(Matrix));
        }
    }

    // The case continues with target_part/target_type even when spatialprops
    // is absent. These are nil gates; retail accepts numeric strings too.
    void LoadSpatialPropAnim(lua_Object Props, FThornEvent* Event,
                             FThornSpatialEventDesc* Descriptor)
    {
        LoadSpatialEventPose(Props, Descriptor);
        const lua_Object Part = GetFieldOrNil(Props, "target_part");
        if (lua_isnil(Part) == 0)
        {
            Event->SpatialTargetPartStorage.Assign(lua_getstring(Part));
            Descriptor->TargetPart = Event->SpatialTargetPartStorage.GetPublishedData();
        }
        const lua_Object TargetType = GetFieldOrNil(Props, "target_type");
        if (lua_isnil(TargetType) == 0)
            Descriptor->TargetType = LuaNumberToRetailInt(TargetType);
    }

    // The five inline vectors in 27fe0 instead store all three numeric results
    // before publishing the complete vector. The probe loop still stops at nil.
    void ReadLightInlineVector(lua_Object Table, float* Out, bool bFloatProbe)
    {
        for (unsigned int Index = 1; Index <= 3; ++Index)
        {
            const lua_Object Value = bFloatProbe
                ? GetLightFloatProbe(Table, Index)
                : GetLightDoubleProbe(Table, Index);
            if (lua_isnil(Value) != 0)
            {
                std::memset(Out, 0, 3 * sizeof(float));
                return;
            }
        }
        static const float NumericIndices[3] = {1.0f, 2.0f, 3.0f};
        float Coordinates[3];
        for (int Index = 0; Index < 3; ++Index)
            StoreLuaNumberFloat(GetLightFloatIndex(Table, &NumericIndices[Index]), &Coordinates[Index]);
        std::memcpy(Out, Coordinates, sizeof(Coordinates));
    }

    // FUN_06f27fe0 — entity LIGHT and event type 4 share this table.
    void LoadLightProps(lua_Object Light, FThornLightProps* Props, int* Mask)
    {
        int LoadedMask = 0;
        // Retail FUN_06f27fe0 (on/infinite fields): the gate is FUN_06f4b410
        // (lua_isnil, tag -6), not lua_isnumber -- any present, non-nil value
        // is read via FUN_06f4b540/ftol (LuaNumberToRetailInt) and still sets
        // the mask bit even when it is not actually numeric.
        lua_Object On = GetFieldOrNil(Light, "on");
        if (lua_isnil(On) == 0)
        {
            Props->On = LuaNumberToRetailInt(On);
            LoadedMask = LoadedMask | kThornLightBitOn;
        }
        lua_Object Infinite = GetFieldOrNil(Light, "infinite");
        if (lua_isnil(Infinite) == 0)
        {
            Props->Infinite = LuaNumberToRetailInt(Infinite);
            LoadedMask = LoadedMask | kThornLightBitInfinite;
        }
        lua_Object Color = GetFieldOrNil(Light, "color");
        if (lua_isnil(Color) == 0)
        {
            float Rgb[3];
            Rgb[0] = 0.0f;
            Rgb[1] = 0.0f;
            Rgb[2] = 0.0f;
            ReadLightInlineVector(Color, Rgb, false);
            StoreLightColorIntegers(Rgb, Props->Color);
            LoadedMask = LoadedMask | kThornLightBitColor;
        }
        lua_Object Diffuse = GetFieldOrNil(Light, "diffuse");
        if (lua_isnil(Diffuse) == 0)
        {
            ReadLightInlineVector(Diffuse, Props->Diffuse, false);
            Props->Diffuse[3] = 1.0f;
            LoadedMask = LoadedMask | kThornLightBitDiffuse;
        }
        lua_Object Spec = GetFieldOrNil(Light, "specular");
        if (lua_isnil(Spec) == 0)
        {
            ReadLightInlineVector(Spec, Props->Specular, true);
            Props->Specular[3] = 1.0f;
            LoadedMask = LoadedMask | kThornLightBitSpecular;
        }
        lua_Object Amb = GetFieldOrNil(Light, "ambient");
        if (lua_isnil(Amb) == 0)
        {
            ReadLightInlineVector(Amb, Props->Ambient, true);
            Props->Ambient[3] = 1.0f;
            LoadedMask = LoadedMask | kThornLightBitAmbient;
        }
        lua_Object Dir = GetFieldOrNil(Light, "direction");
        if (lua_isnil(Dir) == 0)
        {
            ReadLightInlineVector(Dir, Props->Direction, true);
            LoadedMask = LoadedMask | kThornLightBitDirection;
        }
        lua_Object Atten = GetFieldOrNil(Light, "atten");
        if (lua_isnil(Atten) == 0)
        {
            float Coordinates[3];
            ReadVec3Complete(Atten, Coordinates);
            std::memcpy(Props->Atten, Coordinates, sizeof(Coordinates));
            LoadedMask = LoadedMask | kThornLightBitAtten;
        }
        // Retail FUN_06f2e140 ("type" field): same FUN_06f4b410 nil gate as
        // on/infinite -- not lua_isnumber. A present, non-nil value is still
        // read via FUN_06f4b540/ftol (LuaNumberToRetailInt) and sets the mask
        // bit even when it is not actually numeric.
        if (ReadRetailIntField(Light, "type", &Props->Type))
        {
            LoadedMask = LoadedMask | kThornLightBitType;
        }
        // Retail FUN_06f2e060 (shared by range/cutoff/theta): the gate is
        // FUN_06f4b410 (lua_isnil, tag -6), not lua_isnumber -- any present,
        // non-nil value is read via lua_getnumber (which itself defaults a
        // non-numeric value to 0.0) and still sets the mask bit.
        if (ReadRetailFloatField(Light, "range", &Props->RangeTemp))
        {
            std::memcpy(&Props->Range, &Props->RangeTemp, sizeof(Props->Range));
            LoadedMask = LoadedMask | kThornLightBitRange;
        }
        if (ReadRetailFloatField(Light, "cutoff", &Props->CutoffTemp))
        {
            ScaleLightAngle(&Props->CutoffTemp, &Props->Cutoff);
            LoadedMask = LoadedMask | kThornLightBitCutoff;
        }
        if (ReadRetailFloatField(Light, "theta", &Props->Theta))
        {
            ScaleLightAngle(&Props->Theta, &Props->Theta);
            LoadedMask = LoadedMask | kThornLightBitTheta;
        }
        if (Mask != nullptr)
            *Mask = LoadedMask;
    }

    // FUN_06f24b40 — event type 4 only (after FUN_06f27fe0).
    void ApplyLightEventPost(FThornLightProps* Props, int* Mask)
    {
        const int EntryMask = *Mask;
        if ((EntryMask & kThornLightBitColor) != 0)
        {
            const unsigned int ColorScaleBits = 0x3b808081u;
            const int* Colors = Props->Color;
            float* Diffuse = Props->Diffuse;
            unsigned int ColorBits[4];
            // Actual C++ Debug/Release publishes R/G before FILD blue and
            // B/alpha before mask800. Retail24b80..95 instead publishes R/G
            // after FILD blue, then mask800 before its waiting FSTP32.
            __asm
            {
                mov ecx, Colors
                mov edx, Diffuse
                lea ebx, ColorBits
                mov eax, EntryMask
                fild dword ptr [ecx]
                mov dword ptr [ebx + 0ch], 03f800000h
                or eax, 0800h
                fmul dword ptr ColorScaleBits
                fstp dword ptr [ebx]
                mov esi, dword ptr [ebx]
                fild dword ptr [ecx + 4]
                fmul dword ptr ColorScaleBits
                fstp dword ptr [ebx + 4]
                fild dword ptr [ecx + 8]
                mov dword ptr [edx], esi
                mov esi, dword ptr [ebx + 4]
                mov dword ptr [edx + 4], esi
                fmul dword ptr ColorScaleBits
                mov ecx, Mask
                mov dword ptr [ecx], eax
                fstp dword ptr [ebx + 8]
                mov esi, dword ptr [ebx + 8]
                mov dword ptr [edx + 8], esi
                mov esi, dword ptr [ebx + 0ch]
                mov dword ptr [edx + 0ch], esi
            }
        }
        const int InfiniteMask = *Mask;
        if ((InfiniteMask & kThornLightBitInfinite) != 0)
        {
            Props->Type = (Props->Infinite == 0) ? 2 : 3;
            *Mask = (InfiniteMask & ~kThornLightBitInfinite) | kThornLightBitType;
        }
        // FCOMP +0.0, TEST AH,05, JP skips greater/equal/unordered.
        // Only ordered less (C0=1,C2=0, odd parity) falls through here.
        const int CutoffMask = *Mask;
        if ((CutoffMask & kThornLightBitCutoff) != 0)
        {
            const unsigned int ZeroBits = 0;
            const float* Cutoff = &Props->Cutoff;
            unsigned char bOrderedNegative;
            // Actual optimized C++ compares zero to cutoff and executes a
            // waiting FSTP on the unordered return path. Retail24be4..c10
            // has no waiting instruction after FNSTSW/TEST AH,05/JP.
            __asm
            {
                mov ecx, Cutoff
                fld dword ptr [ecx]
                fcomp dword ptr ZeroBits
                fnstsw ax
                test ah, 05h
                setnp bOrderedNegative
            }
            if (bOrderedNegative != 0)
            {
                Props->Type = 1;
                std::memcpy(&Props->Cutoff, &ZeroBits, sizeof(ZeroBits));
                std::memcpy(&Props->Theta, &ZeroBits, sizeof(ZeroBits));
                *Mask = CutoffMask | kThornLightBitType;
            }
        }
    }

    // FUN_06f28880 full117 instructions: keep mask local through callbacks,
    // publish it once at the end; env is a signed DWORD, the other words float.
    void LoadReverbProps(lua_Object Reverb, FThornReverb4* Target, int* Mask)
    {
        int LoadedMask = 0;
        const lua_Object Environment = GetFieldOrNil(Reverb, "env");
        if (lua_isnil(Environment) == 0)
        {
            const int Value = LuaNumberToRetailInt(Environment);
            std::memcpy(&Target->Environment, &Value, sizeof(Value));
            LoadedMask = 1;
        }
        const lua_Object Volume = GetFieldOrNil(Reverb, "vol");
        if (lua_isnil(Volume) == 0)
        {
            StoreLuaNumberFloat(Volume, &Target->Volume);
            LoadedMask |= 2;
        }
        const lua_Object Decay = GetFieldOrNil(Reverb, "decay");
        if (lua_isnil(Decay) == 0)
        {
            StoreLuaNumberFloat(Decay, &Target->Decay);
            LoadedMask |= 4;
        }
        const lua_Object Damping = GetFieldOrNil(Reverb, "damping");
        if (lua_isnil(Damping) == 0)
        {
            StoreLuaNumberFloat(Damping, &Target->Damping);
            LoadedMask |= 8;
        }
        if (Mask != nullptr)
            *Mask = LoadedMask;
    }

    bool IsIkLuaNumberNonzero(lua_Object Object)
    {
        const double Number = lua_getnumber(Object);
        return ThornPortableComparison::Nonzero(&Number);
    }

    // FUN_06f2a5b0: mutate the live packet in the order of the Lua callbacks.
    int LoadIkProps(lua_Object Props, FThornStartIkDesc* Destination)
    {
        lua_Object Value = GetFieldOrNil(Props, "end_effector");
        if (lua_isnil(Value) == 0)
            Destination->EndEffector = lua_getstring(Value);
        Value = GetFieldOrNil(Props, "target_part");
        if (lua_isnil(Value) == 0)
            Destination->TargetPart = lua_getstring(Value);
        Value = GetFieldOrNil(Props, "target_type");
        if (lua_isnil(Value) == 0)
            Destination->TargetType = LuaNumberToRetailInt(Value);
        Value = GetFieldOrNil(Props, "ik_id");
        if (lua_isnil(Value) == 0)
        {
            // The parser's transient ID occupies the key field. The factory
            // replaces it with the new event's own key; this is never dereferenced.
            const unsigned int IdBits = static_cast<unsigned int>(LuaNumberToRetailInt(Value));
            Destination->Key = reinterpret_cast<const void*>(static_cast<std::uintptr_t>(IdBits));
        }
        Value = GetFieldOrNil(Props, "count_to_root");
        if (lua_isnil(Value) == 0)
            Destination->CountToRoot = LuaNumberToRetailInt(Value);
        Value = GetFieldOrNil(Props, "damping");
        if (lua_isnil(Value) == 0)
            StoreLuaNumberFloat(Value, &Destination->Damping);

        int Up = Destination->LookAtUpAxis;
        Value = GetFieldOrNil(Props, "up");
        if (lua_isnil(Value) == 0)
            Up = LuaNumberToRetailInt(Value);
        Destination->LookAtUpAxis = Up;
        int Front = Destination->LookAtFrontAxis;
        Value = GetFieldOrNil(Props, "front");
        if (lua_isnil(Value) == 0)
            Front = LuaNumberToRetailInt(Value);
        Destination->LookAtFrontAxis = Front;

        Value = GetFieldOrNil(Props, "point_at");
        if (lua_isnil(Value) == 0)
        {
            // FCOMP zero; TEST AH,44; JNP selects false for equality.
            // Fall-through selects true for less, greater and unordered.
            Destination->bPointAt = IsIkLuaNumberNonzero(Value);
        }
        Value = GetFieldOrNil(Props, "move_to");
        if (lua_isnil(Value) == 0)
            Destination->bMoveTo = IsIkLuaNumberNonzero(Value);
        Value = GetFieldOrNil(Props, "transition_duration");
        if (lua_isnil(Value) == 0)
            StoreLuaNumberFloat(Value, &Destination->TransitionDuration);
        return 0;
    }

    // ForeachEntity AUDIO case28ede..29270. This object is parse-owned;
    // Audio is staging storage, not the eventual entity's runtime cache.
    FThornEntitySnap* LoadAudioEntityDesc(lua_Object Table, char*& TemplateName)
    {
        const lua_Object Audio = GetFieldOrNil(Table, "audioprops");
        if (TemplateName == nullptr && lua_istable(Audio) != 0)
            TemplateName = lua_getstring(GetFieldOrNil(Audio, "archetype"));
        // Value-init defines the modern packet's unwritten tail/padding.
        // Retail initializes only Audio0/1 here, after archetype conversion.
        FThornEntitySnap* Descriptor = new FThornEntitySnap{};
        std::memset(&Descriptor->Audio[0], 0, 2 * sizeof(float));
        if (lua_istable(Audio) == 0)
            return Descriptor;
        const char* const Keys[8] = {
            "attenuation", "pan", "dmin", "dmax", "ain", "aout", "atout", "rmix"};
        for (int Index = 0; Index < 8; ++Index)
        {
            const lua_Object Value = GetFieldOrNil(Audio, Keys[Index]);
            if (lua_isnil(Value) == 0)
            {
                if (Index == 4 || Index == 5)
                {
                    const int Bits = LuaNumberToRetailInt(Value);
                    std::memcpy(&Descriptor->Audio[Index], &Bits, sizeof(Bits));
                }
                else
                    StoreLuaNumberFloat(Value, &Descriptor->Audio[Index]);
            }
        }
        // 291d9..29269 repeats attenuation/pan after all eight callbacks.
        for (int Index = 0; Index < 2; ++Index)
        {
            const lua_Object Value = GetFieldOrNil(Audio, Keys[Index]);
            if (lua_isnil(Value) == 0)
                StoreLuaNumberFloat(Value, &Descriptor->Audio[Index]);
        }
        return Descriptor;
    }

    // 29550..29578: nil still executes FLD zero / FMUL 1000 / retail ftol.
    int ReadSceneStartMilliseconds(lua_Object Object)
    {
        const int bNil = lua_isnil(Object);
        const unsigned int ScaleBits = 0x447a0000u;
        unsigned short OldControl;
        unsigned short ChopControl;
        long long Result;
        const double Number = bNil != 0 ? 0.0 : lua_getnumber(Object);
        __asm {
            fld qword ptr Number
            fmul dword ptr ScaleBits
            fwait
            fnstcw OldControl
            fwait
            mov ax, OldControl
            or ax, 0c00h
            mov ChopControl, ax
            fldcw ChopControl
            fistp qword ptr Result
            fldcw OldControl
        }
        return static_cast<int>(static_cast<unsigned int>(Result));
    }

    // 2964b..2966c retains Z below the X/Y ftol operations. Y is loaded
    // before publishing X; the third conversion consumes the retained Z.
    void StoreSceneAmbientIntegers(const float* Source, void* Output)
    {
        PortableIntegerConversion::StoreFloatTriplet(Source, Output, true);
    }

    void LoadSceneEntityDesc(lua_Object Table, FThornEntitySnap* Descriptor)
    {
        // Defaults and the saved duration precede every property callback.
        std::memset(Descriptor->SceneBlob, 0, 3 * sizeof(unsigned int));
        Descriptor->SceneBlob[3] = G_LoadDurationMs;
        Descriptor->SceneBlob[4] = 0;
        Descriptor->SceneBlob[5] = 6;
        Descriptor->SceneBlob[6] = 6;
        FThornFog8 FogTarget = {};
        Descriptor->SceneBlob[4] = static_cast<unsigned int>(
            ReadSceneStartMilliseconds(GetFieldOrNil(Table, "start_time")));
        const lua_Object Ambient = GetFieldOrNil(Table, "ambient");
        if (lua_isnil(Ambient) == 0)
        {
            float Coordinates[3];
            bool bComplete = true;
            for (unsigned int Index = 1; Index <= 3; ++Index)
            {
                if (lua_isnil(GetLightFloatProbe(Ambient, Index)) != 0)
                {
                    bComplete = false;
                    break;
                }
            }
            if (bComplete)
            {
                const float Indices[3] = {1.0f, 2.0f, 3.0f};
                for (int Index = 0; Index < 3; ++Index)
                    StoreLuaNumberFloat(GetLightFloatIndex(Ambient, &Indices[Index]), &Coordinates[Index]);
            }
            else
                std::memset(Coordinates, 0, sizeof(Coordinates));
            StoreSceneAmbientIntegers(Coordinates, Descriptor->SceneBlob);
        }
        const lua_Object Up = GetFieldOrNil(Table, "up");
        if (lua_isnil(Up) == 0)
            Descriptor->SceneBlob[5] = static_cast<unsigned int>(LuaNumberToRetailInt(Up));
        const lua_Object Front = GetFieldOrNil(Table, "front");
        if (lua_isnil(Front) == 0)
            Descriptor->SceneBlob[6] = static_cast<unsigned int>(LuaNumberToRetailInt(Front));
        LoadFogKeys(Table, &FogTarget, nullptr);
        // A genuine typed fog object supplies its raw packet words.
        std::memcpy(&Descriptor->SceneBlob[7], &FogTarget, sizeof(FogTarget));
    }

    void ForeachEntity()
    {
        lua_beginblock();
        const lua_Object FallbackName = lua_lua2C(1);
        const lua_Object Table = lua_lua2C(2);
        if (lua_istable(Table) == 0)
        {
            lua_endblock();
            return;
        }
        const lua_Object TypeObject = GetFieldOrNil(Table, "type");
        // 28aee jumps directly to2a0f8, deliberately bypassing endblock.
        if (lua_isnil(TypeObject) != 0)
            return;
        const unsigned int Identity[9] = {
            0x3f800000u, 0, 0, 0, 0x3f800000u, 0, 0, 0, 0x3f800000u};
        const unsigned int PositionBits[3] = {0, 0, 0};
        const int Type = LuaNumberToRetailInt(TypeObject);
        const int Flags = LuaNumberToRetailInt(GetFieldOrNil(Table, "flags"));
        const lua_Object NameObject = GetFieldOrNil(Table, "entity_name");
        char* Name = lua_getstring(lua_isstring(NameObject) != 0 ? NameObject : FallbackName);
        char* TemplateName = nullptr;
        const lua_Object TemplateObject = GetFieldOrNil(Table, "template_name");
        if (lua_isstring(TemplateObject) != 0)
        {
            TemplateName = lua_getstring(TemplateObject);
            if (std::strcmp(TemplateName, "NULL") == 0)
                TemplateName = const_cast<char*>("");
        }
        unsigned char LtGroup = 0;
        unsigned char SortGroup = 0;
        unsigned char UserFlags = 0;
        const lua_Object Lt = GetFieldOrNil(Table, "lt_grp");
        if (lua_isnil(Lt) == 0)
            LtGroup = static_cast<unsigned char>(LuaNumberToRetailInt(Lt));
        const lua_Object Sort = GetFieldOrNil(Table, "srt_grp");
        if (lua_isnil(Sort) == 0)
            SortGroup = static_cast<unsigned char>(LuaNumberToRetailInt(Sort));
        const lua_Object User = GetFieldOrNil(Table, "usr_flg");
        if (lua_isnil(User) == 0)
            UserFlags = static_cast<unsigned char>(LuaNumberToRetailInt(User));

        {
            FParsedEntityPersistOwner Persist;
            FThornEntitySnap* Descriptor = nullptr;
            switch (Type)
            {
            case 1:
            case 2:
            case 13:
            {
                if (TemplateName == nullptr)
                    TemplateName = Name;
                Descriptor = new FThornEntitySnap{};
                float* Output = Type == 13 ? &Descriptor->PsysSparam : &Descriptor->FloorHeight;
                std::memset(Output, 0, sizeof(float));
                const lua_Object Properties = GetFieldOrNil(Table, Type == 13 ? "psysprops" : "compoundprops");
                if (lua_istable(Properties) != 0)
                {
                    const lua_Object Value = GetFieldOrNil(Properties, Type == 13 ? "sparam" : "floor_height");
                    if (lua_isnil(Value) == 0)
                        StoreLuaNumberFloat(Value, Output);
                }
                break;
            }
            case 3:
            case 4:
            {
                Descriptor = new FThornEntitySnap{};
                const unsigned int Defaults[4] = {
                    0x42700000u, 0x3dcccccdu, 0x47c35000u, 0x3faaaaabu};
                std::memcpy(&Descriptor->Fovh, Defaults, sizeof(Defaults));
                const lua_Object Camera = GetFieldOrNil(Table, "cameraprops");
                if (lua_istable(Camera) != 0)
                {
                    const char* const Keys[4] = {"fovh", "hvaspect", "nearplane", "farplane"};
                    float* const Outputs[4] = {&Descriptor->Fovh, &Descriptor->Aspect,
                                               &Descriptor->NearPlane, &Descriptor->FarPlane};
                    for (int Index = 0; Index < 4; ++Index)
                    {
                        const lua_Object Value = GetFieldOrNil(Camera, Keys[Index]);
                        if (lua_isnil(Value) == 0)
                            StoreLuaNumberFloat(Value, Outputs[Index]);
                    }
                }
                break;
            }
            case 5:
            {
                Descriptor = new FThornEntitySnap{};
                ThornInitLightEntityDefaults(&Descriptor->Light);
                const lua_Object Light = GetFieldOrNil(Table, "lightprops");
                if (lua_istable(Light) != 0)
                {
                    int LoadedMask;
                    LoadLightProps(Light, &Descriptor->Light, &LoadedMask);
                    if ((LoadedMask & kThornLightBitType) == 0)
                        ThornConvertCompactLight(&Descriptor->Light);
                }
                break;
            }
            case 6:
                Descriptor = LoadAudioEntityDesc(Table, TemplateName);
                break;
            case 7:
            case 8:
                Descriptor = new FThornEntitySnap{};
                break;
            case 9:
            case 10:
                Descriptor = new FThornEntitySnap{};
                LoadSceneEntityDesc(Table, Descriptor);
                break;
            case 11:
                Descriptor = new FThornEntitySnap{};
                Descriptor->MotionPath.PathType = nullptr;
                Descriptor->MotionPath.MotionPath = nullptr;
                InstantiateMotionPath(Descriptor, GetFieldOrNil(Table, "pathprops"), Persist);
                break;
            default:
                break;
            }

            if (Descriptor != nullptr)
            {
                Descriptor->Type = Type;
                Descriptor->Flags = Flags;
                std::memcpy(Descriptor->Pos, PositionBits, sizeof(PositionBits));
                std::memcpy(Descriptor->Orient, Identity, sizeof(Identity));
                Descriptor->Name = Name;
                Descriptor->TemplateName = TemplateName;
                Descriptor->LtGrp = LtGroup;
                Descriptor->SrtGrp = SortGroup;
                Descriptor->UsrFlg = UserFlags;
                LoadEntitySpatialDesc(Table, Descriptor);
                G_LoadScriptEngine->Slot35AddEntity(Descriptor, &G_LoadEntityHandle);
                {
                    // 2a028 retains Name before key construction; 2a06d
                    // reads the shared handle AFTER map[] and BEFORE key cleanup.
                    const char* PublishedName = Descriptor->Name;
                    std::string Key;
                    Key.assign(PublishedName);
                    int& PublishedHandle = G_LuaEntityHandles[Key];
                    PublishedHandle = G_LoadEntityHandle;
                }
                const lua_Object UserProperties = GetFieldOrNil(Table, "userprops");
                if (lua_istable(UserProperties) != 0)
                {
                    lua_pushobject(UserProperties);
                    lua_pushcfunction(&ForeachUserProp);
                    lua_callfunction(lua_getglobal(const_cast<char*>("foreach")));
                }
                G_LoadEntityHandle = -1;
                // No descriptor unwind owner exists after typed construction.
                delete Descriptor;
            }
        }
        lua_endblock();
    }

    // Shared common stack descriptor256d4..258bf, before typed allocation/ID.
    lua_Object ParseEventCommon(lua_Object Table, FThornEventDesc& Common)
    {
        const lua_Object Props = GetIndexOrNil(Table, 4.0);
        if (lua_istable(Props) != 0)
        {
            const lua_Object Duration = GetFieldOrNil(Props, "duration");
            if (lua_isnil(Duration) != 0)
                Common.Flags |= kThornFlagUseScriptDuration;
            else
                Common.DurationMs = LuaMillisecondsToRetailInt(Duration);
            const lua_Object Flags = GetFieldOrNil(Props, "flags");
            if (lua_isnil(Flags) == 0)
                Common.Flags = LuaNumberToRetailInt(Flags);
            const lua_Object EventFlags = GetFieldOrNil(Props, "event_flags");
            if (lua_isnil(EventFlags) == 0)
                Common.EventFlags = LuaNumberToRetailInt(EventFlags);
            const lua_Object Curve = GetFieldOrNil(Props, "param_curve");
            if (lua_isnil(Curve) == 0 && lua_istable(Curve) != 0)
                CreateCurveFromLua(&Common.ParamCurve, Curve);
            const lua_Object Period = GetFieldOrNil(Props, "pcurve_period");
            if (lua_isnil(Period) == 0)
                Common.Field28 = LuaNumberToRetailInt(Period);
        }
        return Props;
    }

    // USER case25f0a constructs an unpublished typed temporary after common parsing.
    void ParseUserEvent(lua_Object Props, FThornEventDesc& Common)
    {
        FThornEvent* Temporary = new FThornEvent(kThornTypeUserEvent,
                                               &Common);
        Temporary->UserEventDesc.ExternalPayloadWord = 0; // 25f49, after typed ctor callbacks.
        if (lua_istable(Props) != 0)
        {
            const lua_Object StringObject = GetFieldOrNil(Props, "user_event_string");
            if (lua_isnil(StringObject) == 0 && G_LoadComContext != nullptr)
            {
                FThornUserEventDesc* Descriptor = &Temporary->UserEventDesc;
                int ResultId;
                G_LoadScriptEngine->Slot38AddEvent(Descriptor, &ResultId);
                // 25fc8 changes only the temporary packet after the published copy.
                Temporary->UserEventDesc.Common.UniqueId = ResultId;
                const char* String = lua_getstring(StringObject);
                // Retail leaves this context null; retain the canonical callback
                // for modern providers (see ForeachEvent in decomp-tracker).
                G_LoadComContext->Slot3(Descriptor, String);
            }
        }
        // 27b54 normal temporary delete, then27b60..6b common curve Release.
        // Retail drops unwind state after construction; do not add cleanup for
        // a later Lua/COM exception or an invented context-null fallback event.
        FThornEvent::DestroyNative(Temporary);
        if (Common.ParamCurve != nullptr)
            static_cast<IParamCurve*>(Common.ParamCurve)->Release();
    }

    // Case14: real temporary owner273ff..274d8, shared registration27b35..27b6b.
    void ParsePsysPropEvent(lua_Object Props, FThornEventDesc& Common)
    {
        FThornEvent* Temporary = new FThornEvent(kThornTypePsysPropAnim, &Common);
        const lua_Object PsysProps = GetFieldOrNil(Props, "psysprops");
        if (lua_istable(PsysProps) != 0)
        {
            const lua_Object Sparam = GetFieldOrNil(PsysProps, "sparam");
            if (lua_isnil(Sparam) == 0)
            {
                // 274c3..274ca publishes mask BEFORE the numeric callback.
                Temporary->PsysMask |= 1;
                StoreLuaNumberFloat(Sparam, &Temporary->SparamTarget);
            }
        }
        FThornPsysPropEventDesc* Descriptor = &Temporary->PsysPropDesc;
        int ResultId;
        // 27b39 reads the module engine AFTER all property callbacks.
        G_LoadScriptEngine->Slot38AddEvent(Descriptor, &ResultId);
        // 27b51 modifies only this temporary packet AFTER the factory copy.
        Descriptor->Common.UniqueId = ResultId;
        FThornEvent::DestroyNative(Temporary);
        if (Common.ParamCurve != nullptr)
            Common.ParamCurve->Release();
        // Retail EH state is -1 after temporary construction: do not add an
        // unwind cleanup for later Lua/factory exceptions.
    }

    // Ordinary terminal27b35..27b6b, shared by independently traced AUDIO/FOG.
    void RegisterParsedEvent(FThornEvent* Temporary, void* Descriptor,
                             FThornEventDesc& Common)
    {
        if (Descriptor != nullptr)
        {
            int ResultId;
            G_LoadScriptEngine->Slot38AddEvent(Descriptor, &ResultId);
            // Exact common object, not a spanning array view; word5 at +14.
            static_cast<FThornEventDesc*>(Descriptor)->UniqueId = ResultId;
        }
        FThornEvent::DestroyNative(Temporary);
        if (Common.ParamCurve != nullptr)
            Common.ParamCurve->Release();
    }

    void ParseAudioPropEvent(lua_Object Props, FThornEventDesc& Common)
    {
        FThornEvent* Temporary = new FThornEvent(kThornTypeAudioPropAnim, &Common);
        const lua_Object Audio = GetFieldOrNil(Props, "audioprops");
        if (lua_istable(Audio) != 0)
        {
            const lua_Object Attenuation = GetFieldOrNil(Audio, "attenuation");
            if (lua_isnil(Attenuation) == 0)
            {
                Temporary->AudioMask |= 1;
                StoreLuaNumberFloat(Attenuation, &Temporary->AudioTarget.Attenuation);
            }
            const lua_Object Pan = GetFieldOrNil(Audio, "pan");
            if (lua_isnil(Pan) == 0)
            {
                Temporary->AudioMask |= 2;
                StoreLuaNumberFloat(Pan, &Temporary->AudioTarget.Pan);
            }
            const lua_Object Dmin = GetFieldOrNil(Audio, "dmin");
            if (lua_isnil(Dmin) == 0)
            {
                Temporary->AudioMask |= 4;
                StoreLuaNumberFloat(Dmin, &Temporary->AudioTarget.Dmin);
            }
            const lua_Object Dmax = GetFieldOrNil(Audio, "dmax");
            if (lua_isnil(Dmax) == 0)
            {
                Temporary->AudioMask |= 8;
                StoreLuaNumberFloat(Dmax, &Temporary->AudioTarget.Dmax);
            }
            const lua_Object Ain = GetFieldOrNil(Audio, "ain");
            if (lua_isnil(Ain) == 0)
            {
                Temporary->AudioMask |= 0x10;
                Temporary->AudioTarget.Ain = LuaNumberToRetailInt(Ain);
            }
            const lua_Object Aout = GetFieldOrNil(Audio, "aout");
            if (lua_isnil(Aout) == 0)
            {
                Temporary->AudioMask |= 0x20;
                Temporary->AudioTarget.Aout = LuaNumberToRetailInt(Aout);
            }
            const lua_Object Atout = GetFieldOrNil(Audio, "atout");
            if (lua_isnil(Atout) == 0)
            {
                Temporary->AudioMask |= 0x40;
                StoreLuaNumberFloat(Atout, &Temporary->AudioTarget.Atout);
            }
            const lua_Object Rmix = GetFieldOrNil(Audio, "rmix");
            if (lua_isnil(Rmix) == 0)
            {
                Temporary->AudioMask |= 0x80;
                StoreLuaNumberFloat(Rmix, &Temporary->AudioTarget.Rmix);
            }
        }
        FThornEventDesc* Descriptor = &Temporary->AudioPropDesc.Common;
        RegisterParsedEvent(Temporary, Descriptor, Common);
    }

    void ParseFogPropEvent(lua_Object Props, FThornEventDesc& Common)
    {
        FThornEvent* Temporary = new FThornEvent(kThornTypeFogPropAnim, &Common);
        const lua_Object Fog = GetFieldOrNil(Props, "fogprops");
        if (lua_istable(Fog) != 0)
        {
            // 27894..2789d: direct canonical target and mask outputs, no local
            // mirror, deferred memcpy, or second Apply/Finalize callback.
            LoadFogKeys(Fog, &Temporary->FogTarget, &Temporary->FogMask);
        }
        FThornEventDesc* Descriptor = &Temporary->FogDesc.Common;
        RegisterParsedEvent(Temporary, Descriptor, Common);
    }

    void ParseLightPropEvent(lua_Object Props, FThornEventDesc& Common)
    {
        FThornEvent* Temporary = new FThornEvent(kThornTypeLightPropAnim, &Common);
        const lua_Object Light = GetFieldOrNil(Props, "lightprops");
        if (lua_istable(Light) != 0)
        {
            LoadLightProps(Light, &Temporary->LightTarget, &Temporary->LightMask);
            // 2687e calls this unconditionally after the accepted light table.
            ApplyLightEventPost(&Temporary->LightTarget, &Temporary->LightMask);
        }
        FThornEventDesc* Descriptor = &Temporary->LightDesc.Common;
        RegisterParsedEvent(Temporary, Descriptor, Common);
    }

    // SET_CAMERA272df..2730c has no own property lookups after common parsing.
    void ParseSetCameraEvent(lua_Object Props, FThornEventDesc& Common)
    {
        FThornEvent* Temporary = new FThornEvent(kThornTypeSetCamera, &Common);
        FThornEventDesc* Descriptor = &Temporary->SetCameraDesc;
        RegisterParsedEvent(Temporary, Descriptor, Common);
    }

    // Actual CAMERA case2688b..269a3 owns only these four property probes.
    void ParseCameraPropEvent(lua_Object Props, FThornEventDesc& Common)
    {
        FThornEvent* Temporary = new FThornEvent(kThornTypeCameraPropAnim, &Common);
        const lua_Object Camera = GetFieldOrNil(Props, "cameraprops");
        if (lua_istable(Camera) != 0)
        {
            static const char* const Keys[4] = {"fovh", "aspect", "near", "far"};
            for (int Index = 0; Index < 4; ++Index)
            {
                const lua_Object Value = GetFieldOrNil(Camera, Keys[Index]);
                if (lua_isnil(Value) == 0)
                {
                    // 26958..26960 publishes the live mask before numeric callbacks.
                    Temporary->CamMask |= 1 << Index;
                    StoreLuaNumberFloat(Value, &Temporary->CamTarget[Index]);
                }
            }
        }
        void* Descriptor = &Temporary->CameraDesc.Common;
        RegisterParsedEvent(Temporary, Descriptor, Common);
    }

    // FLOOR case27942..27b27, then the ordinary terminal27b2a..27b6b.
    void ParseFloorHeightEvent(lua_Object Props, FThornEventDesc& Common)
    {
        FThornEvent* Temporary = new FThornEvent(kThornTypeFloorHeightAnim, &Common);
        if (lua_istable(Props) != 0)
        {
            const lua_Object Floor = GetFieldOrNil(Props, "floor_height");
            if (lua_isnil(Floor) == 0)
                StoreLuaNumberFloat(Floor, &Temporary->FloorTarget);
        }
        // These two probes still run when Props fails the outer floor-height gate.
        const lua_Object TargetPart = GetFieldOrNil(Props, "target_part");
        if (lua_isnil(TargetPart) == 0)
        {
            const char* Name = lua_getstring(TargetPart);
            // 27a3b..27ad8: assign the constructor-owned string first, then
            // publish its pointer; the descriptor setter's early clear is absent.
            Temporary->FloorTargetPartStorage.Assign(Name);
            Temporary->TargetPart = Temporary->FloorTargetPartStorage.GetPublishedData();
        }
        const lua_Object TargetType = GetFieldOrNil(Props, "target_type");
        if (lua_isnil(TargetType) == 0)
            Temporary->TargetType = LuaNumberToRetailInt(TargetType);

        FThornEventDesc* Descriptor = &Temporary->FloorDesc.Common;
        RegisterParsedEvent(Temporary, Descriptor, Common);
    }

    void ParseReverbPropEvent(lua_Object Props, FThornEventDesc& Common)
    {
        FThornEvent* Temporary = new FThornEvent(kThornTypeReverbPropAnim, &Common);
        const lua_Object Reverb = GetFieldOrNil(Props, "reverbprops");
        if (lua_istable(Reverb) != 0)
            LoadReverbProps(Reverb, &Temporary->ReverbTarget, &Temporary->ReverbMask);
        FThornEventDesc* Descriptor = &Temporary->ReverbDesc.Common;
        RegisterParsedEvent(Temporary, Descriptor, Common);
    }

    void ParseStartIkEvent(lua_Object Props, FThornEventDesc& Common)
    {
        if (Common.StartMs == 0)
            Common.StartMs = 1; // 2595a..25962: packet+4, before the typed temporary.
        FThornEvent* Temporary = new FThornEvent(kThornTypeStartIk, &Common);
        FThornStartIkDesc* Descriptor = static_cast<FThornStartIkDesc*>(Temporary->GetDesc());
        if (lua_istable(Props) != 0)
        {
            LoadIkProps(Props, Descriptor);
            // 259d2..25a83: both unconditional string assignments happen only
            // inside the outer table branch, after the borrowed Lua pointers.
            Temporary->IkEndEffectorStorage.Assign(Descriptor->EndEffector);
            Descriptor->EndEffector = Temporary->IkEndEffectorStorage.GetPublishedData();
            Temporary->IkTargetPartStorage.Assign(Descriptor->TargetPart);
            Descriptor->TargetPart = Temporary->IkTargetPartStorage.GetPublishedData();
        }
        // Keep this exact packet: GetDesc would repack and replace transient ik_id.
        RegisterParsedEvent(Temporary, Descriptor, Common);
    }

    // Each ordinary family owns one unpublished typed packet through Slot38.
    void ParseMotionEvent(lua_Object Props, FThornEventDesc& Common,
                          const FThornEntitySnap* PrimaryDesc)
    {
        FThornEvent* Temporary = new FThornEvent(kThornTypeStartMotion, &Common);
        FThornMotionEventDesc* Descriptor = static_cast<FThornMotionEventDesc*>(Temporary->GetDesc());
        if (lua_istable(Props) != 0)
        {
            const lua_Object Animation = GetFieldOrNil(Props, "animation");
            if (lua_isstring(Animation) != 0)
            {
                Temporary->MotionAnimationStorage.Assign(lua_getstring(Animation));
                Descriptor->Animation = Temporary->MotionAnimationStorage.GetPublishedData();
            }
            else
                Descriptor->Animation = nullptr;
            const lua_Object LockedBone = GetFieldOrNil(Props, "locked_bone");
            if (lua_isstring(LockedBone) != 0)
            {
                Temporary->MotionLockedBoneStorage.Assign(lua_getstring(LockedBone));
                Descriptor->LockedBone = Temporary->MotionLockedBoneStorage.GetPublishedData();
            }
            else
                Descriptor->LockedBone = nullptr;
            ReadRetailFloatField(Props, "start_time", &Descriptor->StartTime);
            ReadRetailFloatField(Props, "trans_time", &Descriptor->TransTime);
            ReadRetailFloatField(Props, "time_scale", &Descriptor->TimeScale);
            ReadRetailFloatField(Props, "weight", &Descriptor->Weight);
            const lua_Object Loop = GetFieldOrNil(Props, "loop");
            // 25dba FCOMP 0; TEST AH,41h/JNZ skips not-greater, fallthrough sets bit2.
            if (lua_isnil(Loop) == 0 && lua_getnumber(Loop) > 0.0)
                Descriptor->Common.Flags |= 2;
            // The primary type is read after all preceding property callbacks.
            if (PrimaryDesc->Type == kThornEntityCompound)
                ReadRetailFloatField(Props, "trans_scale", &Descriptor->TransScale);
            else
                ReadRetailFloatField(Props, "heading", &Descriptor->Heading);
        }
        RegisterParsedEvent(Temporary, Descriptor, Common);
    }

    void ParseSoundEvent(lua_Object Props, FThornEventDesc& Common)
    {
        FThornEvent* Temporary = new FThornEvent(kThornTypeStartSound, &Common);
        FThornSoundEventDesc* Descriptor = static_cast<FThornSoundEventDesc*>(Temporary->GetDesc());
        Descriptor->SoundStartTime = 0; // 25ebd, after the typed constructor.
        if (lua_istable(Props) != 0)
            ReadRetailIntField(Props, "start_time", &Descriptor->SoundStartTime);
        RegisterParsedEvent(Temporary, Descriptor, Common);
    }

    void ParseSubtitleEvent(lua_Object Props, FThornEventDesc& Common)
    {
        FThornEvent* Temporary = new FThornEvent(kThornTypeSubtitle, &Common);
        FThornSubtitleEventDesc* Descriptor = static_cast<FThornSubtitleEventDesc*>(Temporary->GetDesc());
        Descriptor->Strid = 0; // 26027, raw integer default after construction.
        if (lua_istable(Props) != 0)
        {
            const lua_Object Strid = GetFieldOrNil(Props, "strid");
            if (lua_isnil(Strid) == 0)
                Descriptor->Strid = static_cast<unsigned int>(LuaNumberToRetailInt(Strid));
        }
        RegisterParsedEvent(Temporary, Descriptor, Common);
    }

    void ParsePathEvent(lua_Object Props, FThornEventDesc& Common)
    {
        FThornEvent* Temporary = new FThornEvent(kThornTypePathAnim, &Common);
        FThornPathEventDesc* Descriptor = static_cast<FThornPathEventDesc*>(Temporary->GetDesc());
        if (lua_istable(Props) != 0)
        {
            // Both aliases are always probed, and overwrite the first value when present.
            ReadRetailFloatField(Props, "start_percent", &Descriptor->StartPercent);
            ReadRetailFloatField(Props, "percent1", &Descriptor->StartPercent);
            ReadRetailFloatField(Props, "stop_percent", &Descriptor->StopPercent);
            ReadRetailFloatField(Props, "percent2", &Descriptor->StopPercent);
            int UpAxis = Descriptor->LookAtUpAxis;
            ReadRetailIntField(Props, "up", &UpAxis);
            Descriptor->LookAtUpAxis = UpAxis;
            int FrontAxis = Descriptor->LookAtFrontAxis;
            ReadRetailIntField(Props, "front", &FrontAxis);
            Descriptor->LookAtFrontAxis = FrontAxis;
            const lua_Object Offset = GetFieldOrNil(Props, "offset");
            if (lua_isnil(Offset) == 0)
                ReadSpatialEventVector(Offset, Descriptor->Offset, 3);
        }
        RegisterParsedEvent(Temporary, Descriptor, Common);
    }

    void ParseAttachEvent(lua_Object Props, FThornEventDesc& Common)
    {
        FThornEvent* Temporary = new FThornEvent(kThornTypeAttachEntity, &Common);
        FThornAttachEventDesc* Descriptor = static_cast<FThornAttachEventDesc*>(Temporary->GetDesc());
        if (lua_istable(Props) != 0)
        {
            const lua_Object Offset = GetFieldOrNil(Props, "offset");
            if (lua_isnil(Offset) == 0)
                ReadSpatialEventVector(Offset, Descriptor->Offset, 3);
            const lua_Object Part = GetFieldOrNil(Props, "target_part");
            if (lua_isnil(Part) == 0)
            {
                Temporary->AttachTargetPartStorage.Assign(lua_getstring(Part));
                Descriptor->TargetPart = Temporary->AttachTargetPartStorage.GetPublishedData();
            }
            ReadRetailIntField(Props, "target_type", &Descriptor->TargetType);
            int UpAxis = Descriptor->UpAxis;
            ReadRetailIntField(Props, "up", &UpAxis);
            Descriptor->UpAxis = UpAxis;
            int FrontAxis = Descriptor->FrontAxis;
            ReadRetailIntField(Props, "front", &FrontAxis);
            Descriptor->FrontAxis = FrontAxis;
        }
        RegisterParsedEvent(Temporary, Descriptor, Common);
    }

    void ParseConnectEvent(lua_Object Props, FThornEventDesc& Common)
    {
        FThornEvent* Temporary = new FThornEvent(kThornTypeConnectHardpoints, &Common);
        FThornConnectEventDesc* Descriptor = static_cast<FThornConnectEventDesc*>(Temporary->GetDesc());
        if (lua_istable(Props) != 0)
        {
            // 26796/267e2 publish borrowed Lua names; the factory copies them later.
            const lua_Object Hardpoint = GetFieldOrNil(Props, "hardpoint");
            if (lua_isnil(Hardpoint) == 0)
                Descriptor->Hardpoint = lua_getstring(Hardpoint);
            const lua_Object ParentHardpoint = GetFieldOrNil(Props, "parent_hardpoint");
            if (lua_isnil(ParentHardpoint) == 0)
                Descriptor->ParentHardpoint = lua_getstring(ParentHardpoint);
        }
        RegisterParsedEvent(Temporary, Descriptor, Common);
    }

    void ParseSpatialEvent(lua_Object Props, FThornEventDesc& Common)
    {
        FThornEvent* Temporary = new FThornEvent(kThornTypeSpatialPropAnim, &Common);
        FThornSpatialEventDesc* Descriptor = static_cast<FThornSpatialEventDesc*>(Temporary->GetDesc());
        if (lua_istable(Props) != 0)
            LoadSpatialPropAnim(Props, Temporary, Descriptor);
        RegisterParsedEvent(Temporary, Descriptor, Common);
    }

    void ParseSubSceneEvent(lua_Object Props, FThornEventDesc& Common)
    {
        // A genuine complete modern input object supplies the constructor's float parameter.
        FThornSubSceneEventDesc Input = {};
        Input.Common = Common;
        FThornEvent* Temporary = new FThornEvent(kThornTypeStartSubScene, &Input);
        FThornSubSceneEventDesc* Descriptor = static_cast<FThornSubSceneEventDesc*>(Temporary->GetDesc());
        Descriptor->StartTime = 0.0f; // 2734c, after the constructor.
        if (lua_istable(Props) != 0)
            ReadRetailFloatField(Props, "start_time", &Descriptor->StartTime);
        RegisterParsedEvent(Temporary, Descriptor, Common);
    }

    void ParseStartPsysEvent(FThornEventDesc& Common)
    {
        FThornEvent* Temporary = new FThornEvent(kThornTypeStartPsys, &Common);
        void* Descriptor = Temporary->GetDesc();
        RegisterParsedEvent(Temporary, Descriptor, Common);
    }

    void ForeachEvent()
    {
        // FUN_06f25340: common callbacks, selected family, then current-engine publication.
        lua_beginblock();
        const lua_Object Table = lua_lua2C(2);
        if (lua_istable(Table) == 0)
        {
            lua_endblock();
            return;
        }
        int Type = LuaNumberToRetailInt(GetIndexOrNil(Table, 2.0));
        FThornEventDesc Common = {};
        Common.StartMs = LuaMillisecondsToRetailInt(GetIndexOrNil(Table, 1.0));
        Common.UniqueId = -1;
        Common.PrimaryIndex = -1;
        Common.SecondaryIndex = -1;
        Common.Field28 = -1;
        if (Type == 0)
        {
            lua_endblock();
            return;
        }
        const lua_Object Names = GetIndexOrNil(Table, 3.0);
        if (lua_istable(Names) == 0)
        {
            lua_endblock();
            return;
        }
        const FThornEntitySnap* PrimaryDesc = nullptr;
        const FThornEntitySnap* SecondaryDesc = nullptr;
        const lua_Object PrimaryName = GetIndexOrNil(Names, 1.0);
        if (lua_isnil(PrimaryName) == 0 &&
            FindLuaEntityHandle(lua_getstring(PrimaryName), &Common.PrimaryIndex))
        {
            PrimaryDesc = static_cast<const FThornEntitySnap*>(
                G_LoadScriptEngine->Slot9Entity(Common.PrimaryIndex));
        }
        const lua_Object SecondaryName = GetIndexOrNil(Names, 2.0);
        if (lua_isnil(SecondaryName) == 0)
        {
            if (FindLuaEntityHandle(lua_getstring(SecondaryName), &Common.SecondaryIndex))
            {
                SecondaryDesc = static_cast<const FThornEntitySnap*>(
                    G_LoadScriptEngine->Slot9Entity(Common.SecondaryIndex));
            }
            else
            {
                char Message[8192];
                std::sprintf(Message,
                             "THORN: Failed to create event of type %d(%s).  Secondary entity not found.\n",
                             Type, ThornEventTypeDebugName(static_cast<unsigned int>(Type)));
                FDUMP(0x100002, "%s(%d) : WARNING:General:%s", kLuaParserCpp, 0x505, Message);
                Common.Type = 0;
            }
        }
        const lua_Object Props = ParseEventCommon(Table, Common);
        // 258c0..258f6 selects the descriptor returned by the corresponding Slot9.
        if (Type == kThornTypePathAnim && SecondaryDesc != nullptr &&
            SecondaryDesc->Type > 0 && SecondaryDesc->Type <= 8)
            Type = kThornTypeAttachEntity;
        else if (Type == 0x17)
            Type = PrimaryDesc->Type == kThornEntitySound
                ? kThornTypeStartSound : kThornTypeStartMotion;
        if (Type == kThornTypeStartSound)
        {
            if ((Common.Flags & kThornFlagPosition) != 0)
            {
                FThornEntitySnap SpatialSnap = *PrimaryDesc;
                const unsigned int PositionBits = static_cast<unsigned int>(SpatialSnap.Flags) | 2u;
                std::memcpy(&SpatialSnap.Pos[0], &PositionBits, sizeof(PositionBits));
                // 25904..2593c copies common17+SOUND8 before Slot37 and before any temporary.
                G_LoadScriptEngine->Slot37UpdateEntity(Common.PrimaryIndex, &SpatialSnap);
            }
            Common.Flags &= ~(kThornFlagPosition | kThornFlagOrientation);
        }
        switch (Type)
        {
        case kThornTypeUserEvent: ParseUserEvent(Props, Common); break;
        case kThornTypeSetCamera: ParseSetCameraEvent(Props, Common); break;
        case kThornTypeStartSound: ParseSoundEvent(Props, Common); break;
        case kThornTypeLightPropAnim: ParseLightPropEvent(Props, Common); break;
        case kThornTypeCameraPropAnim: ParseCameraPropEvent(Props, Common); break;
        case kThornTypePathAnim: ParsePathEvent(Props, Common); break;
        case kThornTypeSpatialPropAnim: ParseSpatialEvent(Props, Common); break;
        case kThornTypeAttachEntity: ParseAttachEvent(Props, Common); break;
        case kThornTypeConnectHardpoints: ParseConnectEvent(Props, Common); break;
        case kThornTypeStartMotion: ParseMotionEvent(Props, Common, PrimaryDesc); break;
        case kThornTypeStartIk: ParseStartIkEvent(Props, Common); break;
        case kThornTypeStartSubScene: ParseSubSceneEvent(Props, Common); break;
        case kThornTypeStartPsys: ParseStartPsysEvent(Common); break;
        case kThornTypePsysPropAnim: ParsePsysPropEvent(Props, Common); break;
        case kThornTypeAudioPropAnim: ParseAudioPropEvent(Props, Common); break;
        case kThornTypeFogPropAnim: ParseFogPropEvent(Props, Common); break;
        case kThornTypeReverbPropAnim: ParseReverbPropEvent(Props, Common); break;
        case kThornTypeFloorHeightAnim: ParseFloorHeightEvent(Props, Common); break;
        case kThornTypeSubtitle: ParseSubtitleEvent(Props, Common); break;
        default:
            if (Common.ParamCurve != nullptr)
                Common.ParamCurve->Release();
            break;
        }
        lua_endblock();
    }

    unsigned long IfsAddRef(void* FileSystem)
    {
        if (FileSystem == nullptr)
            return 0;
        return static_cast<IFileSystem*>(FileSystem)->AddRef();
    }

    unsigned long IfsRelease(void* FileSystem)
    {
        if (FileSystem == nullptr)
            return 0;
        return static_cast<IFileSystem*>(FileSystem)->Release();
    }

    unsigned long IfsGetSize(void* FileSystem)
    {
        if (FileSystem == nullptr)
            return 0;
        return static_cast<IFileSystem*>(FileSystem)->GetSize(0, 0); // +0x28
    }

    int IfsRead(void* FileSystem, void* Buffer, unsigned long Size, unsigned long* BytesRead)
    {
        if (BytesRead != nullptr)
            *BytesRead = 0;
        if (FileSystem == nullptr || Buffer == nullptr)
            return static_cast<int>(GR_GENERIC);
        return static_cast<IFileSystem*>(FileSystem)->Read(0, Buffer, Size, BytesRead, 0); // +0x14
    }

    void IfsGetFileSysPath(void* FileSystem, char* Out, unsigned long MaxChars)
    {
        if (Out == nullptr || MaxChars == 0)
            return;
        Out[0] = '\0';
        if (FileSystem == nullptr)
            return;
        static_cast<IFileSystem*>(FileSystem)->GetFileSysPath(Out, MaxChars); // +0x84
    }
}

bool ThornLuaOpen()
{
    // FUN_06f24c80: lua_setstate(0) → lua_open → mathlib → iolib → lua_setstate(old).
    lua_State* OldState = lua_setstate(nullptr);
    lua_open();
    lua_mathlibopen();
    lua_iolibopen();
    G_ThornLuaState = lua_setstate(OldState);
    G_bLuaOpened = (G_ThornLuaState != nullptr);
    return G_bLuaOpened;
}

void ThornLuaClose()
{
    // FUN_06f24cb0 unconditionally selects the current module VM before closing it.
    lua_State* OldState = lua_setstate(G_ThornLuaState);
    lua_close();
    lua_setstate(OldState);
    G_ThornLuaState = nullptr;
    G_bLuaOpened = false;
}

namespace
{
    struct FParserFileSystemRelease
    {
        void operator()(IFileSystem* FileSystem) const { FileSystem->Release(); }
    };

    class FThornLuaLoadParser final : public IThornLoadParser
    {
    public:
        explicit FThornLuaLoadParser(FThornScene& Scene)
            : Scene_(Scene), EntityCount_(0), EventCount_(0), DurationMs_(0),
              OldState_(lua_setstate(G_ThornLuaState))
        {
            // FUN_06f25070: the parser VM is selected before any filesystem work.
            RegisterThornLuaConstants();
        }

        ~FThornLuaLoadParser() { lua_setstate(OldState_); } // FUN_06f250c0.

        void* DACOM_API Destroy(unsigned int Flags) override
        {
            void* Result = this;
            if ((Flags & 1) != 0)
                delete this;
            else
                this->~FThornLuaLoadParser();
            return Result;
        }

        unsigned int DACOM_API GetEntityCount() override { return EntityCount_; }
        unsigned int DACOM_API GetEventCount() override { return EventCount_; }
        unsigned int DACOM_API GetDuration() override { return DurationMs_; }
        GENRESULT DACOM_API Load(IFileSystem* FileSystem) override;
        GENRESULT DACOM_API Build(IFileSystem* FileSystem, IScriptEngine* Engine,
                                  IThornComContext* ComContext) override;

    private:
        FThornScene& Scene_;
        unsigned int EntityCount_;
        unsigned int EventCount_;
        unsigned int DurationMs_;
        lua_State* OldState_;
    };
}

namespace
{
    GENRESULT FThornLuaLoadParser::Load(IFileSystem* FileSystem)
    {
        if (FileSystem == nullptr)
            return GR_OK;
        FileSystem->AddRef();
        std::unique_ptr<IFileSystem, FParserFileSystemRelease> Reference(FileSystem);
        const unsigned long Size = FileSystem->GetSize(0, 0);
        if (Size < 1)
        {
            FdumpLuaError(0x481, "LUA unable to read file \n");
            return static_cast<GENRESULT>(-1);
        }
        char* Buffer = new char[Size + 1];
        unsigned long BytesRead = 0;
        FileSystem->Read(0, Buffer, Size, &BytesRead, 0);
        if (BytesRead != Size)
        {
            FdumpLuaError(0x489, "LUA unable to read file \n");
            delete[] Buffer;
            return static_cast<GENRESULT>(-1);
        }
        // ReadFile::GetFileSysPath -> CopyPath writes a terminated string.
        char ChunkName[80];
        FileSystem->GetFileSysPath(ChunkName, sizeof(ChunkName));
        ChunkName[79] = '\0';
        const int ParseResult = lua_dobuffer(Buffer, Size, ChunkName);
        delete[] Buffer;
        if (ParseResult != 0)
        {
            FdumpLuaError(0x497, "LUA unable to parse file \n");
            return static_cast<GENRESULT>(-1);
        }
        G_TableEntryCount = 0;
        lua_beginblock();
        const lua_Object Entities = lua_getglobal("entities");
        // Retail leaves this block open on the invalid-entities return.
        if (lua_isnil(Entities) != 0 || lua_istable(Entities) == 0)
            return static_cast<GENRESULT>(-1);
        lua_pushobject(Entities);
        lua_pushcfunction(CountTableEntry);
        lua_callfunction(lua_getglobal("foreach"));
        EntityCount_ = G_TableEntryCount;
        G_TableEntryCount = 0;
        const lua_Object Events = lua_getglobal("events");
        if (lua_isnil(Events) == 0 && lua_istable(Events) != 0)
        {
            lua_pushobject(Events);
            lua_pushcfunction(CountTableEntry);
            lua_callfunction(lua_getglobal("foreach"));
        }
        EventCount_ = G_TableEntryCount;
        DurationMs_ = static_cast<unsigned int>(LuaMillisecondsToRetailInt(lua_getglobal("duration")));
        G_LoadDurationMs = DurationMs_;
        lua_endblock();
        return GR_OK;
    }

    GENRESULT FThornLuaLoadParser::Build(IFileSystem* FileSystem, IScriptEngine* Engine,
                                        IThornComContext* ComContext)
    {
        if (EntityCount_ == 0 && DurationMs_ == 0 && Load(FileSystem) < 0)
            return static_cast<GENRESULT>(-1);
        G_LoadScriptEngine = Engine;
        // 2a946: retain the borrowed current parser context after a successful Load.
        // Nested Builds replace this module state; no AddRef, Release or restoration.
        // USER_EVENT reads this live alias across actual Slot38 and Lua string callbacks.
        G_LoadComContext = ComContext;
        G_LoadScene = &Scene_;
        G_LoadEntityHandle = -1; // 2a94b, before lua_beginblock/foreach.
        lua_beginblock();
        if (EntityCount_ != 0)
        {
            lua_pushobject(lua_getglobal("entities"));
            lua_pushcfunction(ForeachEntity);
            lua_callfunction(lua_getglobal("foreach"));
        }
        if (EventCount_ != 0)
        {
            lua_pushobject(lua_getglobal("events"));
            lua_pushcfunction(ForeachEvent);
            lua_callfunction(lua_getglobal("foreach"));
        }
        lua_endblock();
        return GR_OK;
    }
}

IThornLoadParser* ThornCreateLuaLoadParser(FThornScene* Scene)
{
    return new (std::nothrow) FThornLuaLoadParser(*Scene);
}
