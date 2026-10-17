// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// IVP active values and their manager. See ivp_active_value.h for the class
// map (vtables, constructors, sizes).

#include "ivp_active_value.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "ivp_core.h"
#include "ivp_environment.h"
#include "../ivp_utility/ivp_crc32.h" // g_Crc32Table_063edcdc (GetHashIndex, FUN_06361530)
#include "../ivp_utility/ivp_hash.h"  // FUN_063569e0, the three-way name compare

namespace
{
    // Common.dll.c 0x63569c0 / Common.dll.txt:212233-212250 (moved here from
    // src/Common/common_stubs.cpp, 2026-10-14; this TU is its only port
    // caller). Returns 0 for a null or empty string, else its length -- the
    // "has a name" validation gate used by InsertActiveFloat /
    // InsertActiveInt (FUN_06361860 / FUN_063618f0) before hashing /
    // inserting.
    int IVP_StringLengthOrZero(const char* String) // formerly FUN_063569c0
    {
        if (String == nullptr || *String == '\0') // 063569c5-063569cc
        {
            return 0;
        }
        return (int)strlen(String);               // 063569ce-063569d8 (REPNE SCASB)
    }
}

// .data:063ee640.
int GActiveValueChangeCounter = -1;

// ============================================================================
// IVP_U_Active_Value
// ============================================================================

// FUN_06361e00 (.text:06361e00-06361e23): vtable 063a57e8, +4 = FUN_06356920
// (null-safe strdup: null in, null out), +8 = 0.
IVP_U_Active_Value::IVP_U_Active_Value(const char* NameIn)
    : Name(NameIn != nullptr ? _strdup(NameIn) : nullptr)
    , ReferenceCount(0)
{
}

// FUN_06361e50 (.text:06361e50-06361e71): stamps 063a57e8; frees Name
// (FUN_06343fb0) and nulls it when it is set.
IVP_U_Active_Value::~IVP_U_Active_Value()
{
    if (Name != nullptr)
    {
        free(Name);
        Name = nullptr;
    }
}

// ============================================================================
// IVP_U_Active_Float / IVP_U_Active_Int
// ============================================================================

// FUN_06361e80 (.text:06361e80-06361ecf): base ctor, then +0x0c/+0x0e (u16),
// +0x10, +0x18, +0x14, +0x1c all zero, vtable 063a57ec.
IVP_U_Active_Float::IVP_U_Active_Float(const char* NameIn)
    : IVP_U_Active_Value(NameIn)
    , Manager(nullptr)
    , Field_0x18(0)
    , DoubleValue(0.0f)
{
}

// FUN_06362060 (.text:06362060-0636207d): for i = n_elems-1 down to 0,
// elems[i]->slot0(this).
void IVP_U_Active_Float::FireListeners()
{
    for (int Index = static_cast<int>(Listeners.n_elems) - 1; Index >= 0; --Index)
    {
        Listeners.elems[Index]->ActiveFloatChanged(this);
    }
}

// FUN_06361f00 (.text:06361f00-06361f4f): identical to FUN_06361e80 with
// vtable 063a57f4.
IVP_U_Active_Int::IVP_U_Active_Int(const char* NameIn)
    : IVP_U_Active_Value(NameIn)
    , Manager(nullptr)
    , Field_0x18(0)
    , IntValue(0)
{
}

// FUN_06362060 (shared body).
void IVP_U_Active_Int::FireListeners()
{
    for (int Index = static_cast<int>(Listeners.n_elems) - 1; Index >= 0; --Index)
    {
        Listeners.elems[Index]->ActiveIntChanged(this);
    }
}

// ============================================================================
// IVP_U_Active_Terminal_Double
// ============================================================================

// FUN_06362080 (.text:06362080-063620b0): FUN_06361e80(Name); +0x20 =
// 063a580c (base), vtable 063a5800, +0x20 = 063a57fc, +0x1c = +0x24 = Value.
IVP_U_Active_Terminal_Double::IVP_U_Active_Terminal_Double(const char* NameIn, float Value)
    : IVP_U_Active_Float(NameIn)
{
    DoubleValue = Value;
    LastFiredValue = Value;
}

// LAB_063621f0: printf("DoubleVal"); return 0.
int IVP_U_Active_Terminal_Double::Slot1()
{
    printf("DoubleVal");
    return 0;
}

// LAB_063621a0 (.text:063621a0-063621e7): store the value, bump the change
// counter; when delayed and owned by a manager, queue the +0x20 sub-object in
// the manager (slot 6; the null test at 063621c5 is the derived-to-base
// adjustment), otherwise call the sub-object's slot 0 at once.
void IVP_U_Active_Terminal_Double::SetDoubleValue(float Value, int32_t bDelayed)
{
    DoubleValue = Value;
    ++GActiveValueChangeCounter;
    if (bDelayed != 0 && Manager != nullptr)
    {
        Manager->AddDelayedFloat(static_cast<IVP_U_Active_Float_Delayed*>(this));
        return;
    }
    static_cast<IVP_U_Active_Float_Delayed*>(this)->FireIfChanged();
}

// LAB_06362160 (.text:06362160-0636217b): FLD [this+0x1c]; FCOMP [this+0x24];
// TEST AH,0x40 (C3); JNZ -> return. C3 is set for equal and for unordered, so
// the listeners fire only when the two values are ordered and different.
// Then +0x24 = +0x1c (dword copy) and FUN_06362060 on the primary object.
void IVP_U_Active_Terminal_Double::FireIfChanged()
{
    if (!(DoubleValue < LastFiredValue) && !(DoubleValue > LastFiredValue))
    {
        return;
    }
    LastFiredValue = DoubleValue;
    FireListeners();
}

// ============================================================================
// IVP_U_Active_Terminal_Int
// ============================================================================

// FUN_063620f0 (.text:063620f0-0636211e): FUN_06361f00(Name); +0x20 =
// 063a5820 (base), +0x1c = +0x24 = Value, vtable 063a5814, +0x20 = 063a5810.
IVP_U_Active_Terminal_Int::IVP_U_Active_Terminal_Int(const char* NameIn, int32_t Value)
    : IVP_U_Active_Int(NameIn)
{
    IntValue = Value;
    LastFiredValue = Value;
}

// LAB_06362260: printf("IntVal"); return 0.
int IVP_U_Active_Terminal_Int::Slot1()
{
    printf("IntVal");
    return 0;
}

// LAB_06362210 (.text:06362210-06362257): as SetDoubleValue, queueing through
// the manager's slot 7.
void IVP_U_Active_Terminal_Int::SetIntValue(int32_t Value, int32_t bDelayed)
{
    IntValue = Value;
    ++GActiveValueChangeCounter;
    if (bDelayed != 0 && Manager != nullptr)
    {
        Manager->AddDelayedInt(static_cast<IVP_U_Active_Int_Delayed*>(this));
        return;
    }
    static_cast<IVP_U_Active_Int_Delayed*>(this)->FireIfChanged();
}

// LAB_06362180 (.text:06362180-06362193): integer compare, JZ -> return;
// else copy and FUN_06362060.
void IVP_U_Active_Terminal_Int::FireIfChanged()
{
    if (IntValue == LastFiredValue)
    {
        return;
    }
    LastFiredValue = IntValue;
    FireListeners();
}

// ============================================================================
// IVP_U_Active_Value_Hash
// ============================================================================

// LAB_06361580 (.text:06361580-0636159d): FUN_063569e0(Elem0->Name,
// Elem1->Name); NEG / SBB / INC -> 1 when it returned 0, else 0.
int IVP_U_Active_Value_Hash::Compare(void* Elem0, void* Elem1)
{
    const IVP_U_Active_Value* Value0 = static_cast<const IVP_U_Active_Value*>(Elem0);
    const IVP_U_Active_Value* Value1 = static_cast<const IVP_U_Active_Value*>(Elem1);
    const int Result = IVP_CompareStringsNullsLast(Value0->Name, Value1->Name);
    return Result == 0 ? 1 : 0;
}

// FUN_063614a0 (.text:063614a0-0636150b): stamps 063a57a4; for i = SizeMm
// down to 0: a stored value loses one reference and, at zero, is deleted
// through its slot 0 with 1. Then the base destructor (FUN_063522f0).
IVP_U_Active_Value_Hash::~IVP_U_Active_Value_Hash()
{
    struct FElem
    {
        unsigned HashIndex;
        IVP_U_Active_Value* Value;
    };
    FElem* Entries = static_cast<FElem*>(Elems);
    for (int Index = SizeMm; Index >= 0; --Index)
    {
        IVP_U_Active_Value* Value = Entries[Index].Value;
        if (Value != nullptr)
        {
            --Value->ReferenceCount;
            if (Value->ReferenceCount == 0)
            {
                delete Value;
            }
        }
    }
}

// Common.dll.c 0x6361530 / Common.dll.txt:06361530-06361570 (formerly the
// extern "C" FUN_06361530 in src/Common/common_stubs.cpp; thiscall, RET 4,
// the receiver is unused). Name-hash of an active value: Value+0x4 is the
// value's `char* Name` (see InsertActiveFloat / InsertActiveInt and every
// hash call site). Byte-at-a-time CRC-32 over the name string via the IEEE
// 802.3 table (g_Crc32Table_063edcdc), OR'd with 0x80000000 (the IVP_VHash
// "occupied" marker, the same convention as IVP_VHash_Store's own hashes).
// Retail has no null-name guard here (callers gate on FUN_063569c0 first);
// kept unguarded to match.
unsigned IVP_U_Active_Value_Hash::GetHashIndex(IVP_U_Active_Value* Value) // formerly FUN_06361530
{
    const unsigned char* s = (const unsigned char*)Value->Name;
    unsigned crc = 0xffffffffu;
    while (*s != 0)
    {
        crc = (crc >> 8) ^ g_Crc32Table_063edcdc[(*s ^ crc) & 0xffu];
        ++s;
    }
    return crc | 0x80000000u;
}

// ============================================================================
// IVP_U_Active_Value_Manager
// ============================================================================

// FUN_063615a0 (.text:063615a0-0636169d): both vectors empty, vtable
// 063a57ac, +4 = argument, +8 / +0xc = new IVP_U_Active_Value_Hash(0x10)
// (operator_new(0x10), FUN_063522a0(0x10), vtable 063a57a4; null when the
// allocation returned null), +0x24 = new IVP_U_Active_Value(null)
// (operator_new(0xc), FUN_06361e00(0)), +0x20 = 0, then FUN_063617b0 (called
// directly, the constructor's own slot 9).
IVP_U_Active_Value_Manager::IVP_U_Active_Value_Manager(int32_t DeleteOnEnvDeleteIn)
    : DeleteOnEnvDelete(DeleteOnEnvDeleteIn)
    , FloatHash(nullptr)
    , IntHash(nullptr)
    , CurrentTime(nullptr)
    , SearchTemplate(nullptr)
{
    FloatHash = new IVP_U_Active_Value_Hash(0x10);
    IntHash = new IVP_U_Active_Value_Hash(0x10);
    SearchTemplate = new IVP_U_Active_Value(nullptr);
    CurrentTime = nullptr;
    InitActiveValuesGeneric();
}

// FUN_063616e0 (.text:063616e0-063617a0): stamps 063a57ac; deletes FloatHash
// and IntHash through their slot 1 when set and nulls both; deletes
// SearchTemplate through its slot 0 when set and nulls it; then the two vector
// destructors, +0x18 first (inline address +0x20) and +0x10 second (inline
// address +0x18) -- the member destructors below in reverse declaration
// order. CurrentTime is not deleted here: it lives in FloatHash, whose
// destructor drops the reference.
IVP_U_Active_Value_Manager::~IVP_U_Active_Value_Manager()
{
    if (FloatHash != nullptr)
    {
        delete FloatHash;
    }
    FloatHash = nullptr;
    if (IntHash != nullptr)
    {
        delete IntHash;
    }
    IntHash = nullptr;
    if (SearchTemplate != nullptr)
    {
        delete SearchTemplate;
    }
    SearchTemplate = nullptr;
}

// LAB_063616a0 (.text:063616a0-063616b1): `if (+4 != 0 && this != 0)
// this->slot0(1)`.
void IVP_U_Active_Value_Manager::EnvironmentWillBeDeleted(IVP_Environment* Environment)
{
    (void)Environment;
    if (DeleteOnEnvDelete != 0)
    {
        delete this;
    }
}

// FUN_06361860 (.text:06361860-063618df).
void IVP_U_Active_Value_Manager::InsertActiveFloat(IVP_U_Active_Float* Value)
{
    char* ValueName = Value->Name;
    if (IVP_StringLengthOrZero(ValueName) == 0)
    {
        printf("insert_active_float: tried to insert active_IVP_FLOAT without name!\n");
        return;
    }
    if (FloatHash->FindElem(Value, FloatHash->GetHashIndex(Value)) != nullptr)
    {
        printf("insert_active_float: name '%s' already exists in name_hash!\n", ValueName);
        return;
    }
    FloatHash->AddElem(Value, FloatHash->GetHashIndex(Value));
    Value->Manager = this;
    Value->ReferenceCount = Value->ReferenceCount + 1;
}

// LAB_06361980 (.text:06361980-063619b1): remove from FloatHash (retail
// writes to address 0 when absent), drop one reference, and delete through
// slot 0 at zero. The reference is decremented before the null test of the
// removed pointer, as in retail.
void IVP_U_Active_Value_Manager::RemoveActiveFloat(IVP_U_Active_Float* Value)
{
    IVP_U_Active_Value* Removed =
        static_cast<IVP_U_Active_Value*>(FloatHash->RemoveElem(Value, FloatHash->GetHashIndex(Value)));
    Removed->ReferenceCount = Removed->ReferenceCount - 1;
    if (Removed->ReferenceCount == 0 && Removed != nullptr)
    {
        delete Removed;
    }
}

// FUN_063618f0 (.text:063618f0-06361971). Same as InsertActiveFloat on
// IntHash, except that the reference count grows by 2 (`ADD EAX,0x2` at
// .text:06361963), so one RemoveActiveInt never deletes the value. Retail
// behaviour, reproduced.
void IVP_U_Active_Value_Manager::InsertActiveInt(IVP_U_Active_Int* Value)
{
    char* ValueName = Value->Name;
    if (IVP_StringLengthOrZero(ValueName) == 0)
    {
        printf("insert_active_int: tried to insert active_int without name!\n");
        return;
    }
    if (IntHash->FindElem(Value, IntHash->GetHashIndex(Value)) != nullptr)
    {
        printf("insert_active_int: name '%s' already exists in name_hash!\n", ValueName);
        return;
    }
    IntHash->AddElem(Value, IntHash->GetHashIndex(Value));
    Value->Manager = this;
    Value->ReferenceCount = Value->ReferenceCount + 2;
}

// LAB_063619c0 (.text:063619c0-063619f1): RemoveActiveFloat on IntHash.
void IVP_U_Active_Value_Manager::RemoveActiveInt(IVP_U_Active_Int* Value)
{
    IVP_U_Active_Value* Removed =
        static_cast<IVP_U_Active_Value*>(IntHash->RemoveElem(Value, IntHash->GetHashIndex(Value)));
    Removed->ReferenceCount = Removed->ReferenceCount - 1;
    if (Removed->ReferenceCount == 0 && Removed != nullptr)
    {
        delete Removed;
    }
}

// LAB_06361a00 (.text:06361a00-06361a4b): backward search of +0x10; append
// (growing through FUN_063441a0 when n_elems >= memsize) only when absent.
void IVP_U_Active_Value_Manager::AddDelayedFloat(IVP_U_Active_Float_Delayed* Value)
{
    if (!DelayedFloats.Contains(Value))
    {
        DelayedFloats.Add(Value);
    }
}

// LAB_06361a50 (.text:06361a50-06361a9b): the same on +0x18.
void IVP_U_Active_Value_Manager::AddDelayedInt(IVP_U_Active_Int_Delayed* Value)
{
    if (!DelayedInts.Contains(Value))
    {
        DelayedInts.Add(Value);
    }
}

// FUN_06361aa0 (.text:06361aa0-06361b28): slot 0 of every queued float,
// lowest index first, with n_elems re-read after each call; then the vector
// is cleared (buffer freed unless inline +0x18, counts zeroed); the same for
// the ints (inline +0x20).
void IVP_U_Active_Value_Manager::FireDelayed()
{
    for (int Index = 0; Index < static_cast<int>(DelayedFloats.n_elems); ++Index)
    {
        DelayedFloats.elems[Index]->FireIfChanged();
    }
    DelayedFloats.Clear();
    for (int Index = 0; Index < static_cast<int>(DelayedInts.n_elems); ++Index)
    {
        DelayedInts.elems[Index]->FireIfChanged();
    }
    DelayedInts.Clear();
}

// FUN_063617b0 (.text:063617b0-0636185a): new "double_null" (0) and new
// "current_time" (0) terminals (each null when operator_new(0x28) returned
// null); +0x20 = current_time; then slot 2 on double_null and on +0x20.
void IVP_U_Active_Value_Manager::InitActiveValuesGeneric()
{
    IVP_U_Active_Terminal_Double* DoubleNull = new IVP_U_Active_Terminal_Double("double_null", 0.0f);
    CurrentTime = new IVP_U_Active_Terminal_Double("current_time", 0.0f);
    InsertActiveFloat(DoubleNull);
    InsertActiveFloat(CurrentTime);
}

// LAB_06361b30 (.text:06361b30-06361b73): when +0x20 is set, its slot 2 with
// (float)(Environment->+0xfc + Environment->+0xf8) and 0 (not delayed); then
// own slot 8.
void IVP_U_Active_Value_Manager::RefreshPsiActiveValues(IVP_Environment* Environment)
{
    if (CurrentTime != nullptr)
    {
        const float Now = Environment->current_time.time_offset + Environment->current_time.time_base;
        CurrentTime->SetDoubleValue(Now, 0);
    }
    FireDelayed();
}

// LAB_06361b80 (.text:06361b80-06361c11): look the name up through
// SearchTemplate (its Name set for the lookup and nulled after it); return
// the stored value when found, else new IVP_U_Active_Terminal_Double(Name,
// Value) (null when the allocation returned null) passed to own slot 2 and
// returned.
IVP_U_Active_Float* IVP_U_Active_Value_Manager::GetActiveFloat(const char* Name, float Value)
{
    SearchTemplate->Name = const_cast<char*>(Name);
    void* Found = FloatHash->FindElem(SearchTemplate, FloatHash->GetHashIndex(SearchTemplate));
    SearchTemplate->Name = nullptr;
    if (Found != nullptr)
    {
        return static_cast<IVP_U_Active_Float*>(static_cast<IVP_U_Active_Value*>(Found));
    }
    IVP_U_Active_Terminal_Double* Created = new IVP_U_Active_Terminal_Double(Name, Value);
    InsertActiveFloat(Created);
    return Created;
}

// LAB_06361c20 (.text:06361c20-06361cb5): as GetActiveFloat but returns null
// when the name already exists.
IVP_U_Active_Float* IVP_U_Active_Value_Manager::CreateActiveFloat(const char* Name, float Value)
{
    SearchTemplate->Name = const_cast<char*>(Name);
    void* Found = FloatHash->FindElem(SearchTemplate, FloatHash->GetHashIndex(SearchTemplate));
    SearchTemplate->Name = nullptr;
    if (Found != nullptr)
    {
        return nullptr;
    }
    IVP_U_Active_Terminal_Double* Created = new IVP_U_Active_Terminal_Double(Name, Value);
    InsertActiveFloat(Created);
    return Created;
}

// LAB_06361d60 (.text:06361d60-06361df1): GetActiveFloat on IntHash with
// IVP_U_Active_Terminal_Int and own slot 4.
IVP_U_Active_Int* IVP_U_Active_Value_Manager::GetActiveInt(const char* Name, int32_t Value)
{
    SearchTemplate->Name = const_cast<char*>(Name);
    void* Found = IntHash->FindElem(SearchTemplate, IntHash->GetHashIndex(SearchTemplate));
    SearchTemplate->Name = nullptr;
    if (Found != nullptr)
    {
        return static_cast<IVP_U_Active_Int*>(static_cast<IVP_U_Active_Value*>(Found));
    }
    IVP_U_Active_Terminal_Int* Created = new IVP_U_Active_Terminal_Int(Name, Value);
    InsertActiveInt(Created);
    return Created;
}

// LAB_06361cc0 (.text:06361cc0-06361d55): CreateActiveFloat on IntHash.
IVP_U_Active_Int* IVP_U_Active_Value_Manager::CreateActiveInt(const char* Name, int32_t Value)
{
    SearchTemplate->Name = const_cast<char*>(Name);
    void* Found = IntHash->FindElem(SearchTemplate, IntHash->GetHashIndex(SearchTemplate));
    SearchTemplate->Name = nullptr;
    if (Found != nullptr)
    {
        return nullptr;
    }
    IVP_U_Active_Terminal_Int* Created = new IVP_U_Active_Terminal_Int(Name, Value);
    InsertActiveInt(Created);
    return Created;
}
