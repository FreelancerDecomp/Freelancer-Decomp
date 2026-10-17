// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "LuaProfileInternal.h"

#include <new>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

extern "C"
{
#include "lua.h"
}

class FLuaObject;

class FInnerUnknown final : public IDAComponent
{
public:
    FInnerUnknown();

    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
    unsigned long DACOM_API AddRef() override;
    unsigned long DACOM_API Release() override;

    unsigned long RefCount;
    FLuaObject* Owner;
};

class FLuaObject final : public ILua, public IProfileParser, public IAggregateComponent
{
public:
    FLuaObject();
    ~FLuaObject();

    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
    unsigned long DACOM_API AddRef() override;
    unsigned long DACOM_API Release() override;

    GENRESULT DACOM_API InvokeGlobal(const char* Name, FLuaTaggedValue* OutValue,
                                     int Argc, FLuaTaggedValue* const* Argv) override;

    GENRESULT DACOM_API LoadFile(const char* Path, unsigned int Flags) override; // formerly UnnamedSlot3
    int DACOM_API EnumerateSections(FSectionEnumCallback Callback, void* Context) override; // formerly UnnamedSlot4
    void* DACOM_API OpenSection(const char* Name, unsigned int Flags) override;
    int DACOM_API CloseSection(void* Handle) override;
    int DACOM_API ReadLine(void* Handle, int Index, char* Buffer, unsigned int BufferSize) override;
    int DACOM_API GetString(void* Handle, const char* Key, char* Buffer,
                            unsigned int BufferSize) override;

    GENRESULT DACOM_API Initialize() override;

    void BindOuter(FLuaAggDesc* Desc);
    void OpenLuaVm();
    void CloseLuaVm();

    lua_State* LuaState;
    FSectionHandle* SectionList;
    int ExtraLuaRef;
    IDAComponent* Controlling;
    FInnerUnknown Inner;
};

static_assert(offsetof(FLuaObject, LuaState) == 0x0C, "lua_State* @ +0xC");
static_assert(offsetof(FLuaObject, SectionList) == 0x10, "section list @ +0x10");
static_assert(offsetof(FLuaObject, ExtraLuaRef) == 0x14, "extra Lua ref @ +0x14");
static_assert(offsetof(FLuaObject, Controlling) == 0x18, "controlling unknown @ +0x18");
static_assert(offsetof(FLuaObject, Inner) == 0x1C, "inner IUnknown @ +0x1C");
static_assert(sizeof(FLuaObject) == kLuaObjectSize, "calloc(1, 0x28)");
static_assert(offsetof(FInnerUnknown, RefCount) == 4, "inner refcount @ object+0x20");
static_assert(offsetof(FInnerUnknown, Owner) == 8, "inner back-ptr @ object+0x24");
static_assert(offsetof(FLuaTaggedValue, Payload) == 8, "FUN_06801690 payload @ +8");

class FLuaFactory final : public IComponentFactory
{
public:
    FLuaFactory();

    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
    unsigned long DACOM_API AddRef() override;
    unsigned long DACOM_API Release() override;
    GENRESULT DACOM_API CreateInstance(AGGDESC* Desc, void** Instance) override;

    unsigned long RefCount;
    const char* Name;
};

static_assert(sizeof(FLuaFactory) == kFactorySize, "factory operator_new(0xC)");
static_assert(offsetof(FLuaFactory, RefCount) == 4, "factory refcount @ +4");
static_assert(offsetof(FLuaFactory, Name) == 8, "factory name ILua @ +8");

// PE file 0xD118 / 0xD020 — TAB-separated, 0-based i, next/nextvar, string|number only.
static char kGetProfileLine[] =
    "function __get_profile_line (i)\tlocal n, v = nextvar(nil)\twhile n do\t\tlocal t = type(v);\t\t"
    "if (t == \"string\" or t == \"number\") then\t\t\tif i == 0 then\t\t\t\treturn n, v;\t\t\telse\t\t\t\t"
    "i = i - 1;\t\t\tend\t\tend\t\tn, v = nextvar(n);\tend\treturn nil;end";

static char kGetTableLine[] =
    "function __get_table_line (table, i)\tlocal n, v = next(table, nil)\twhile n do\t\tlocal t = type(v);\t\t"
    "if (t == \"string\" or t == \"number\") then\t\t\tif i == 0 then\t\t\t\treturn n, v;\t\t\telse\t\t\t\t"
    "i = i - 1;\t\t\tend\t\tend\t\tn, v = next(table, n);\tend\treturn nil;end";

static_assert(sizeof(kGetTableLine) == 0xF8, "PE __get_table_line 0xF7 + NUL");
static_assert(sizeof(kGetProfileLine) == 0xEB, "PE __get_profile_line 0xEA + NUL");

static const char kFdumpPath[] =
    "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\LuaProfile\\LuaProfile.cpp";
static const char kFdumpFmt[] = "%s(%d) : ERROR:General:%s";
static const int kFdumpLine = 0x167;
static const int kFdumpError = 0x100001;

// LAB_06801120: FDUMP is called through the import with no null test.
extern "C" static void LuaErrorMessage(void)
{
    lua_Object Param = lua_getparam(1);
    if (lua_isstring(Param) == 0)
        return;
    char* Message = lua_getstring(Param);
    FDUMP(kFdumpError, kFdumpFmt, kFdumpPath, kFdumpLine, Message);
}

// Factory QI 068018e0: REPE CMPSB over 0x17 bytes, stops at the first mismatch.
static bool BytesMatch(const char* Left, const char* Right, unsigned int Count)
{
    for (unsigned int Index = 0; Index < Count; ++Index)
    {
        if (Left[Index] != Right[Index])
            return false;
    }
    return true;
}

// Inlined strcmp (06801970, 06801a45): no null tests.
static bool ExactMatch(const char* Left, const char* Right)
{
    unsigned int Index = 0;
    for (;;)
    {
        if (Left[Index] != Right[Index])
            return false;
        if (Left[Index] == '\0')
            return true;
        ++Index;
    }
}

// Inlined REPNE SCASB strlen: every retail site has a non-null string here.
static int StringLength(const char* Text)
{
    return (int)(strlen(Text));
}

enum ELuaValueTag : unsigned int
{
    LuaFloatValue = 5,
    LuaDoubleValue = 6,
    LuaStringValue = 9
};

static void PushInvokeArg(FLuaTaggedValue* Rec)
{
    unsigned int Tag = Rec->Tag;
    if (Tag == LuaStringValue)
    {
        lua_pushstring(Rec->Payload.String);
        return;
    }

    double Number = 0.0;
    if (((Tag & 0x4000) == 0) && ((Tag & 0x1000) != 0))
    {
        if ((Tag & 0xfff) == 4)
            Number = (double)(Rec->Payload.LongLong);
        else if ((char)(Tag >> 8) < 0)
            Number = (double)(Rec->Payload.Int);
        else
            Number = (double)(Rec->Payload.UInt);
    }
    else if (Tag == LuaFloatValue)
        Number = (double)(Rec->Payload.Float);
    else if (Tag == LuaDoubleValue)
        Number = Rec->Payload.Number;
    lua_pushnumber(Number);
}

// LAB_068013b0 tail (068014e2..06801587). The value string is fetched only where retail
// fetches it: after the key copy, and never when the key alone fills the buffer.
static int FormatKeyValue(char* Buffer, unsigned int BufferSize, const char* Key, lua_Object ValueObj)
{
    if (Buffer == nullptr || BufferSize == 0)
    {
        int Length = StringLength(Key);
        char* Value = lua_getstring(ValueObj);
        if (Value != nullptr)
            Length += StringLength(Value);
        return Length + 1;
    }

    unsigned int KeyLen = (unsigned int)(strlen(Key));
    if (KeyLen >= BufferSize)
    {
        strncpy(Buffer, Key, BufferSize);
    }
    else
    {
        strncpy(Buffer, Key, KeyLen);
        unsigned int Used = KeyLen;
        if (KeyLen + 1 < BufferSize)
        {
            Buffer[KeyLen] = '=';
            Used = KeyLen + 1;
        }
        char* Value = lua_getstring(ValueObj);
        if (Value != nullptr)
            strncpy(Buffer + Used, Value, BufferSize - Used);
    }
    Buffer[BufferSize - 1] = '\0';
    return StringLength(Buffer);
}

FInnerUnknown::FInnerUnknown() : RefCount(1), Owner(nullptr) {}

GENRESULT DACOM_API FInnerUnknown::QueryInterface(const char* Iid, void** Out)
{
    IDAComponent* Facet = nullptr;
    if (ExactMatch(kInnerILua, Iid))
        Facet = (ILua*)(Owner);
    else if (ExactMatch(kInnerProfileParser, Iid))
        Facet = (IProfileParser*)(Owner);
    else if (ExactMatch(kInnerAggregate, Iid))
        Facet = (IAggregateComponent*)(Owner);
    else
    {
        *Out = nullptr;
        return GR_INTERFACE_UNSUPPORTED;
    }

    Facet->AddRef();
    *Out = Facet;
    return GR_OK;
}

unsigned long DACOM_API FInnerUnknown::AddRef()
{
    ++RefCount;
    return RefCount;
}

unsigned long DACOM_API FInnerUnknown::Release()
{
    if (RefCount > 0)
        --RefCount;
    unsigned long Remaining = RefCount;
    if (Remaining == 0)
    {
        // LAB_06801b00: the back-pointer is read but not cleared; the object dies with it.
        RefCount = 1;
        FLuaObject* Object = Owner;
        if (Object != nullptr)
        {
            // Pairs with the zero-filled allocation and placement construction.
            Object->~FLuaObject();
            free(Object);
        }
        Remaining = 0;
    }
    return Remaining;
}

FLuaObject::FLuaObject()
    : LuaState(nullptr)
    , SectionList(nullptr)
    , ExtraLuaRef(-1)
    , Controlling(nullptr)
    , Inner()
{
    Inner.Owner = this;
    Inner.RefCount = 1;
    Controlling = &Inner;
    OpenLuaVm();
}

FLuaObject::~FLuaObject()
{
    CloseLuaVm();
}

void FLuaObject::OpenLuaVm()
{
    lua_State* Previous = lua_setstate(nullptr);
    lua_open();
    lua_dostring(kGetProfileLine);
    lua_dostring(kGetTableLine);
    // Retail FUN_06801000: lua_pushcclosure(LAB_06801120, 0) + FUN_06801e80
    // (lua_seterrormethod — hardcoded _ERRORMESSAGE).
    lua_pushcfunction(&LuaErrorMessage);
    lua_seterrormethod();
    LuaState = lua_setstate(Previous);
}

void FLuaObject::CloseLuaVm()
{
    lua_State* Ours = LuaState;
    lua_State* Previous = lua_setstate(Ours);

    // FUN_06801080: the list head (+0x10) and the state (+0xC) are left as they are; the
    // object is freed right after (LAB_06801b00).
    FSectionHandle* Node = SectionList;
    while (Node != nullptr)
    {
        FSectionHandle* Next = Node->Next;
        if (Node->LuaRef != -1)
            lua_unref(Node->LuaRef);
        Node->LuaRef = -1;
        delete Node;
        Node = Next;
    }

    lua_close();
    if (Previous != Ours)
        lua_setstate(Previous);
    if (ExtraLuaRef != -1)
        lua_unref(ExtraLuaRef);
    ExtraLuaRef = -1;
}

// LAB_06801950 @ 068019e3: +0x18 = Desc->Outer; a null outer means +0x18 = &inner, else
// *Desc->InnerOut = &inner (no null test on InnerOut).
void FLuaObject::BindOuter(FLuaAggDesc* Desc)
{
    Controlling = Desc->Outer;
    if (Desc->Outer == nullptr)
        Controlling = &Inner;
    else
        *Desc->InnerOut = &Inner;
}

// LAB_06801b40/b50/b60 (+ the -4/-8 adjustors b70..bc0): tail-jump through +0x18, no test.
GENRESULT DACOM_API FLuaObject::QueryInterface(const char* Iid, void** Out)
{
    return Controlling->QueryInterface(Iid, Out);
}

unsigned long DACOM_API FLuaObject::AddRef()
{
    return Controlling->AddRef();
}

unsigned long DACOM_API FLuaObject::Release()
{
    return Controlling->Release();
}

GENRESULT DACOM_API FLuaObject::InvokeGlobal(const char* Name, FLuaTaggedValue* OutValue,
                                           int Argc, FLuaTaggedValue* const* Argv)
{
    // Lua 3.2 accepts mutable char pointers for these read-only string inputs.
    lua_State* Previous = lua_setstate(LuaState);
    lua_Object Func = lua_getglobal(const_cast<char*>(Name));
    if (Func == LUA_NOOBJECT || lua_isfunction(Func) == 0)
        return GR_INVALID_PARMS;

    int ArgIndex = 0;
    while (ArgIndex < Argc)
    {
        FLuaTaggedValue* Rec = Argv[ArgIndex];
        PushInvokeArg(Rec);
        ++ArgIndex;
    }

    lua_callfunction(Func);

    FLuaTaggedValue* Out = OutValue;
    if (Out != nullptr)
    {
        lua_Object Result = lua_getparam(1);
        if (lua_isnil(Result) == 0)
        {
            if (lua_isstring(Result) != 0)
            {
                char* Text = lua_getstring(Result);
                Out->Tag = LuaStringValue;
                Out->Unused = 0;
                Out->Payload.String = _strdup(Text);
                lua_setstate(Previous);
                return GR_OK;
            }
            Out->Tag = LuaDoubleValue;
            Out->Unused = 0;
            Out->Payload.Number = lua_getnumber(Result);
        }
    }

    lua_setstate(Previous);
    return GR_OK;
}

GENRESULT DACOM_API FLuaObject::LoadFile(const char* Path, unsigned int Flags)
{
    // Lua 3.2 accepts mutable char pointers for these read-only string inputs.
    if (Flags != 0)
        return (GENRESULT)-1;

    lua_State* Previous = lua_setstate(LuaState);
    lua_dofile(const_cast<char*>(Path));
    lua_setstate(Previous);
    return GR_OK;
}

int DACOM_API FLuaObject::EnumerateSections(FSectionEnumCallback Callback, void* Context)
{
    if (Callback == nullptr)
        return 0;

    FSectionEnumCallback EnumCb = Callback;
    lua_State* Previous = lua_setstate(LuaState);

    char* Name = lua_nextvar(nullptr);
    while (Name != nullptr)
    {
        lua_getparam(1);
        lua_Object Global = lua_getglobal(Name);
        if (lua_istable(Global) != 0)
        {
            IProfileParser* Parser = (IProfileParser*)this;
            void* ParserOrZero = Parser;
            if ((uintptr_t)Parser == 4)
                ParserOrZero = nullptr;
            if (EnumCb(ParserOrZero, Name, Context) == 0)
            {
                // Retail LAB_068011b0: callback 0 jumps to RET without restore
                // of DAT_0680e604 (same as InvokeGlobal fail).
                return 0;
            }
        }
        Name = lua_nextvar(Name);
    }

    lua_setstate(Previous);
    return 1;
}

void* DACOM_API FLuaObject::OpenSection(const char* Name, unsigned int Flags)
{
    // Lua 3.2 accepts mutable char pointers for these read-only string inputs.
    // LAB_06801250: only Flags is tested; Name is dereferenced unconditionally.
    if (Flags != 3)
        return nullptr;

    lua_State* Previous = lua_setstate(LuaState);
    FSectionHandle* Handle = nullptr;

    if (Name[0] == '$' && Name[1] == '\0')
    {
        Handle = new (std::nothrow) FSectionHandle{};
        if (Handle != nullptr)
            Handle->LuaRef = -1;
    }
    else
    {
        lua_Object Table = lua_getglobal(const_cast<char*>(Name));
        if (Table != LUA_NOOBJECT && lua_istable(Table) != 0)
        {
            lua_pushobject(Table);
            int Ref = lua_ref(0);
            Handle = new (std::nothrow) FSectionHandle{};
            // Preserve the existing unguarded reference store on allocation failure.
            Handle->LuaRef = Ref;
        }
    }

    if (Handle != nullptr)
    {
        Handle->Next = SectionList;
        SectionList = Handle;
    }

    lua_setstate(Previous);
    return Handle;
}

int DACOM_API FLuaObject::CloseSection(void* Handle)
{
    FSectionHandle* Target = (FSectionHandle*)(Handle);
    if (Target == nullptr)
        return 0;

    FSectionHandle* PreviousNode = nullptr;
    FSectionHandle* Node = SectionList;
    while (Node != nullptr)
    {
        if (Node == Target)
        {
            if (PreviousNode != nullptr)
                PreviousNode->Next = Node->Next;
            else
                SectionList = Node->Next;

            lua_State* Previous = lua_setstate(LuaState);
            if (Node->LuaRef != -1)
                lua_unref(Node->LuaRef);
            Node->LuaRef = -1;
            delete Node;
            lua_setstate(Previous);
            return 1;
        }
        PreviousNode = Node;
        Node = Node->Next;
    }
    return 0;
}

int DACOM_API FLuaObject::ReadLine(void* Handle, int Index, char* Buffer, unsigned int BufferSize)
{
    // Lua 3.2 accepts mutable char pointers for these read-only string inputs.
    FSectionHandle* Target = (FSectionHandle*)(Handle);
    if (Target == nullptr)
        return 0;

    lua_State* Previous = lua_setstate(LuaState);
    int Copied = 0;

    if (Target->LuaRef == -1)
    {
        lua_Object Func = lua_getglobal(const_cast<char*>("__get_profile_line"));
        if (Func != LUA_NOOBJECT)
            lua_pushnumber((double)((unsigned int)(Index)));
        Copied = 0;
    }
    else
    {
        lua_Object Table = lua_getref(Target->LuaRef);
        if (Table != LUA_NOOBJECT)
        {
            lua_Object Func = lua_getglobal(const_cast<char*>("__get_table_line"));
            if (Func != LUA_NOOBJECT)
            {
                lua_pushobject(Table);
                lua_pushnumber((double)((unsigned int)(Index)));
                if (lua_callfunction(Func) == 0)
                {
                    lua_Object KeyObj = lua_getparam(1);
                    lua_Object ValueObj = lua_getparam(2);
                    if (KeyObj != LUA_NOOBJECT && ValueObj != LUA_NOOBJECT && lua_isnil(KeyObj) == 0)
                    {
                        char* Key = lua_getstring(KeyObj);
                        if (Key != nullptr)
                            Copied = FormatKeyValue(Buffer, BufferSize, Key, ValueObj);
                    }
                }
            }
        }
    }

    lua_setstate(Previous);
    return Copied;
}

int DACOM_API FLuaObject::GetString(void* Handle, const char* Key, char* Buffer, unsigned int BufferSize)
{
    // Lua 3.2 accepts mutable char pointers for these read-only string inputs.
    FSectionHandle* Target = (FSectionHandle*)(Handle);
    if (Target == nullptr)
        return 0;

    lua_State* Previous = lua_setstate(LuaState);
    lua_Object Value = LUA_NOOBJECT;

    if (Target->LuaRef == -1)
    {
        Value = lua_getglobal(const_cast<char*>(Key));
    }
    else
    {
        lua_Object Table = lua_getref(Target->LuaRef);
        if (Table == LUA_NOOBJECT)
        {
            lua_setstate(Previous);
            return 0;
        }
        lua_pushobject(Table);
        lua_pushstring(const_cast<char*>(Key));
        Value = lua_gettable();
    }

    char* Source = lua_getstring(Value);
    if (Source == nullptr)
    {
        lua_setstate(Previous);
        return 0;
    }

    if (Buffer != nullptr && BufferSize != 0)
    {
        strncpy(Buffer, Source, BufferSize);
        Buffer[BufferSize - 1] = '\0';
        Source = Buffer;
    }

    int Length = StringLength(Source);
    lua_setstate(Previous);
    return Length;
}

GENRESULT DACOM_API FLuaObject::Initialize()
{
    return GR_OK;
}

FLuaFactory::FLuaFactory() : RefCount(1), Name(kComponentName) {}

// LAB_068018e0: *Out = 0, compare 0x17 bytes, then *Out = this and AddRef via vt+4.
GENRESULT DACOM_API FLuaFactory::QueryInterface(const char* Iid, void** Out)
{
    *Out = nullptr;
    if (!BytesMatch(Iid, Iids::ComponentFactory, kIidComponentFactoryBytes))
        return GR_INTERFACE_UNSUPPORTED;
    *Out = (IComponentFactory*)(this);
    AddRef();
    return GR_OK;
}

unsigned long DACOM_API FLuaFactory::AddRef()
{
    ++RefCount;
    return RefCount;
}

unsigned long DACOM_API FLuaFactory::Release()
{
    if (RefCount > 0)
        --RefCount;
    unsigned long Remaining = RefCount;
    if (Remaining == 0)
    {
        RefCount = 1;
        delete this;
        Remaining = 0;
    }
    return Remaining;
}

GENRESULT DACOM_API FLuaFactory::CreateInstance(AGGDESC* Desc, void** Instance)
{
    FLuaAggDesc* LuaDesc = (FLuaAggDesc*)(Desc);
    if (LuaDesc->Size != kAggDescSize || !ExactMatch(LuaDesc->Name, Name))
    {
        *Instance = nullptr;
        return GR_INTERFACE_UNSUPPORTED;
    }

    // Keep zero-filled storage and the original construction-failure cleanup.
    void* Memory = calloc(kLuaObjectSize, 1);
    if (Memory == nullptr)
    {
        *Instance = nullptr;
        return GR_OUT_OF_MEMORY;
    }

    FLuaObject* Object = new (Memory) FLuaObject();
    Object->BindOuter(LuaDesc);
    *Instance = (ILua*)(Object);
    return GR_OK;
}

BOOL WINAPI DllMain(HINSTANCE Module, DWORD Reason, LPVOID /*Reserved*/)
{
    if (Reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(Module);

        FLuaFactory* Factory = new (std::nothrow) FLuaFactory();

        // FUN_06801870: RegisterComponent (vt+0x10) and the factory Release (vt+8) run with
        // no null test on either the factory or DACOM_Acquire's result.
        ICOManager* Dacom = DACOM_Acquire();
        Dacom->RegisterComponent(Factory, kComponentName,
                                 (void*)(kRegisterPriority));
        Factory->Release();
    }
    return TRUE;
}
