// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include <cstddef>
#include <cstdint>

#include "../ivp_utility/ivp_u_vector.h"
#include "../ivp_utility/ivp_vhash.h"

// ============================================================================
// IVP active values (2026-10-08, claude-code:cmn-phy4): named float / int
// values that notify listeners when they change, and the manager the
// environment owns at +0x98. Moved here from the raw-offset port in
// src/Common/common_stubs.cpp (the former "CActiveVarRegistry" cluster).
//
// Retail has no RTTI for the IVP code; the classes are fixed by their
// constructors and vtables:
//
//   IVP_U_Active_Value            0x0c, ctor FUN_06361e00, vtable 063a57e8
//                                 (one slot: deleting dtor 06361e30 -> 06361e50)
//   IVP_U_Active_Float            0x20, ctor FUN_06361e80, vtable 063a57ec
//                                 (deleting dtor 06361ee0 -> 06361f80, slot 1
//                                 _purecall)
//   IVP_U_Active_Int              0x20, ctor FUN_06361f00, vtable 063a57f4
//                                 (deleting dtor 06361f60 -> 06361ff0, slot 1
//                                 _purecall)
//   IVP_U_Active_Terminal_Double  0x28, ctor FUN_06362080, vtables 063a5800
//                                 (primary) and 063a57fc (at +0x20)
//   IVP_U_Active_Terminal_Int     0x28, ctor FUN_063620f0, vtables 063a5814
//                                 (primary) and 063a5810 (at +0x20)
//   IVP_U_Active_Value_Hash       0x10, IVP_VHash with vtable 063a57a4
//                                 (compare 06361580, deleting dtor 06361510 ->
//                                 063614a0)
//   IVP_U_Active_Value_Manager    0x28, ctor FUN_063615a0, vtable 063a57ac
//                                 (15 slots)
//
// Names: "insert_active_float" / "insert_active_int" are the retail printf
// strings; "current_time" / "double_null" are the two values the manager
// creates. The class names and the remaining member names are the IVP SDK's
// as far as they are known; names marked PORT-LOCAL are descriptive names
// for slots whose SDK name is not recovered.
// ============================================================================

class IVP_Environment;
class IVP_U_Active_Float;
class IVP_U_Active_Int;
class IVP_U_Active_Value_Manager;

// .data:063ee640, initial value -1. Incremented by both terminal setters
// (063621a9-063621b6, 06362219-06362226) and read by nothing else in the
// module. PORT-LOCAL NAME.
extern int GActiveValueChangeCounter;

// The listeners an active float / int calls when its value changes:
// FUN_06362060 calls slot 0 of every element with the value as argument.
// No implementer is ported yet. PORT-LOCAL slot names.
class IVP_U_Active_Float_Listener
{
public:
    virtual void ActiveFloatChanged(IVP_U_Active_Float* Value) = 0; // slot 0
};

class IVP_U_Active_Int_Listener
{
public:
    virtual void ActiveIntChanged(IVP_U_Active_Int* Value) = 0; // slot 0
};

// The second base of the terminal values (at +0x20). Its own vtable
// (063a580c float / 063a5820 int) is a single _purecall slot; the terminal
// classes override it. The manager queues these sub-objects (slots 6/7) and
// calls slot 0 on each in slot 8. PORT-LOCAL class and slot names.
class IVP_U_Active_Float_Delayed
{
public:
    virtual void FireIfChanged() = 0; // slot 0
};

class IVP_U_Active_Int_Delayed
{
public:
    virtual void FireIfChanged() = 0; // slot 0
};

class IVP_U_Active_Value
{
public:
    char* Name;           // +0x04: FUN_06356920 copy (null for a null name)
    int32_t ReferenceCount; // +0x08: 0 at construction; the manager's
                            // insert_active_float adds 1, insert_active_int 2

    // FUN_06361e00 (thiscall, RET 4).
    explicit IVP_U_Active_Value(const char* NameIn);
    // slot 0: FUN_06361e30 (deleting) -> FUN_06361e50.
    virtual ~IVP_U_Active_Value();
};
static_assert(sizeof(IVP_U_Active_Value) == 0x0c, "IVP_U_Active_Value is operator_new(0xc)");

class IVP_U_Active_Float : public IVP_U_Active_Value
{
public:
    IVP_U_Vector<IVP_U_Active_Float_Listener> Listeners; // +0x0c
    IVP_U_Active_Value_Manager* Manager;                 // +0x14: set by insert_active_float
    uint32_t Field_0x18;                                 // +0x18: zeroed by the ctor, no other
                                                         // access found yet. TODO.
    float DoubleValue;                                   // +0x1c

    // FUN_06361e80 (thiscall, RET 4).
    explicit IVP_U_Active_Float(const char* NameIn);
    // slot 0: FUN_06361ee0 (deleting) -> FUN_06361f80. The vector member's
    // destructor is the retail body (+0x10 freed unless it is the inline
    // address +0x14, then the counts zeroed).
    virtual ~IVP_U_Active_Float() {}
    // slot 1: _purecall in this class; the terminal returns 0 after printing
    // its type name. PORT-LOCAL NAME.
    virtual int Slot1() = 0;

    // FUN_06362060: calls every listener, highest index first.
    void FireListeners();
};
static_assert(offsetof(IVP_U_Active_Float, Listeners) == 0x0c, "IVP_U_Active_Float+0x0c");
static_assert(offsetof(IVP_U_Active_Float, Manager) == 0x14, "IVP_U_Active_Float+0x14");
static_assert(offsetof(IVP_U_Active_Float, DoubleValue) == 0x1c, "IVP_U_Active_Float+0x1c");
static_assert(sizeof(IVP_U_Active_Float) == 0x20, "IVP_U_Active_Float is 0x20 bytes");

class IVP_U_Active_Int : public IVP_U_Active_Value
{
public:
    IVP_U_Vector<IVP_U_Active_Int_Listener> Listeners; // +0x0c
    IVP_U_Active_Value_Manager* Manager;               // +0x14: set by insert_active_int
    uint32_t Field_0x18;                               // +0x18: zeroed by the ctor. TODO.
    int32_t IntValue;                                  // +0x1c

    // FUN_06361f00 (thiscall, RET 4).
    explicit IVP_U_Active_Int(const char* NameIn);
    // slot 0: FUN_06361f60 (deleting) -> FUN_06361ff0.
    virtual ~IVP_U_Active_Int() {}
    // slot 1: _purecall. PORT-LOCAL NAME.
    virtual int Slot1() = 0;

    // FUN_06362060 (the same body as the float version, folded by the linker).
    void FireListeners();
};
static_assert(offsetof(IVP_U_Active_Int, Manager) == 0x14, "IVP_U_Active_Int+0x14");
static_assert(offsetof(IVP_U_Active_Int, IntValue) == 0x1c, "IVP_U_Active_Int+0x1c");
static_assert(sizeof(IVP_U_Active_Int) == 0x20, "IVP_U_Active_Int is 0x20 bytes");

class IVP_U_Active_Terminal_Double : public IVP_U_Active_Float, public IVP_U_Active_Float_Delayed
{
public:
    float LastFiredValue; // +0x24: the value the listeners were last told

    // FUN_06362080 (thiscall, RET 8): DoubleValue = LastFiredValue = Value.
    IVP_U_Active_Terminal_Double(const char* NameIn, float Value);
    // slot 0: FUN_063620c0 (deleting) -> FUN_06361f80 directly.
    virtual ~IVP_U_Active_Terminal_Double() {}
    // slot 1: LAB_063621f0.
    virtual int Slot1() override;
    // slot 2: LAB_063621a0 (RET 8). PORT-LOCAL NAME.
    virtual void SetDoubleValue(float Value, int32_t bDelayed);
    // secondary slot 0: LAB_06362160 (this adjusted by -0x20).
    virtual void FireIfChanged() override;
};
static_assert(sizeof(IVP_U_Active_Terminal_Double) == 0x28, "IVP_U_Active_Terminal_Double is operator_new(0x28)");
static_assert(offsetof(IVP_U_Active_Terminal_Double, LastFiredValue) == 0x24, "IVP_U_Active_Terminal_Double+0x24");

class IVP_U_Active_Terminal_Int : public IVP_U_Active_Int, public IVP_U_Active_Int_Delayed
{
public:
    int32_t LastFiredValue; // +0x24

    // FUN_063620f0 (thiscall, RET 8): IntValue = LastFiredValue = Value.
    IVP_U_Active_Terminal_Int(const char* NameIn, int32_t Value);
    // slot 0: FUN_06362130 (deleting) -> FUN_06361ff0 directly.
    virtual ~IVP_U_Active_Terminal_Int() {}
    // slot 1: LAB_06362260.
    virtual int Slot1() override;
    // slot 2: LAB_06362210 (RET 8). PORT-LOCAL NAME.
    virtual void SetIntValue(int32_t Value, int32_t bDelayed);
    // secondary slot 0: LAB_06362180.
    virtual void FireIfChanged() override;
};
static_assert(sizeof(IVP_U_Active_Terminal_Int) == 0x28, "IVP_U_Active_Terminal_Int is operator_new(0x28)");
static_assert(offsetof(IVP_U_Active_Terminal_Int, LastFiredValue) == 0x24, "IVP_U_Active_Terminal_Int+0x24");

// The name-keyed set both manager hashes use (one vtable for both).
class IVP_U_Active_Value_Hash : public IVP_VHash
{
public:
    explicit IVP_U_Active_Value_Hash(int Size)
        : IVP_VHash(Size)
    {
    }

    // slot 0: LAB_06361580 (RET 8): FUN_063569e0(Elem0->Name, Elem1->Name) == 0.
    virtual int Compare(void* Elem0, void* Elem1) override;
    // slot 1: FUN_06361510 (deleting) -> FUN_063614a0: drops one reference
    // from every stored value and deletes those that reach zero.
    virtual ~IVP_U_Active_Value_Hash();

    // FUN_06361530 (thiscall, RET 4; the receiver is unused): CRC-32 of the
    // value's name | 0x80000000.
    unsigned GetHashIndex(IVP_U_Active_Value* Value);
};
static_assert(sizeof(IVP_U_Active_Value_Hash) == 0x10, "IVP_U_Active_Value_Hash is operator_new(0x10)");

class IVP_U_Active_Value_Manager
{
public:
    int32_t DeleteOnEnvDelete;                             // +0x04: ctor argument
    IVP_U_Active_Value_Hash* FloatHash;                    // +0x08
    IVP_U_Active_Value_Hash* IntHash;                      // +0x0c
    IVP_U_Vector<IVP_U_Active_Float_Delayed> DelayedFloats; // +0x10
    IVP_U_Vector<IVP_U_Active_Int_Delayed> DelayedInts;     // +0x18
    IVP_U_Active_Terminal_Double* CurrentTime;             // +0x20: "current_time"
    IVP_U_Active_Value* SearchTemplate;                    // +0x24: lookup key whose
                                                           // Name is set per lookup

    // FUN_063615a0 (thiscall, RET 4).
    explicit IVP_U_Active_Value_Manager(int32_t DeleteOnEnvDeleteIn);

    // slot 0: FUN_063616c0 (deleting) -> FUN_063616e0.
    virtual ~IVP_U_Active_Value_Manager();
    // slot 1: LAB_063616a0 (RET 4): `if (+4) delete this`.
    virtual void EnvironmentWillBeDeleted(IVP_Environment* Environment);
    // slot 2: FUN_06361860.
    virtual void InsertActiveFloat(IVP_U_Active_Float* Value);
    // slot 3: LAB_06361980. PORT-LOCAL NAME.
    virtual void RemoveActiveFloat(IVP_U_Active_Float* Value);
    // slot 4: FUN_063618f0.
    virtual void InsertActiveInt(IVP_U_Active_Int* Value);
    // slot 5: LAB_063619c0. PORT-LOCAL NAME.
    virtual void RemoveActiveInt(IVP_U_Active_Int* Value);
    // slot 6: LAB_06361a00. PORT-LOCAL NAME.
    virtual void AddDelayedFloat(IVP_U_Active_Float_Delayed* Value);
    // slot 7: LAB_06361a50. PORT-LOCAL NAME.
    virtual void AddDelayedInt(IVP_U_Active_Int_Delayed* Value);
    // slot 8: FUN_06361aa0. PORT-LOCAL NAME.
    virtual void FireDelayed();
    // slot 9: FUN_063617b0. PORT-LOCAL NAME.
    virtual void InitActiveValuesGeneric();
    // slot 10: LAB_06361b30 (RET 4); IVP_Environment::simulate_psi calls it
    // (.text:0634d0d5-0634d0de). PORT-LOCAL NAME.
    virtual void RefreshPsiActiveValues(IVP_Environment* Environment);
    // slot 11: LAB_06361b80 (RET 8): the named float, created if missing.
    // PORT-LOCAL NAME.
    virtual IVP_U_Active_Float* GetActiveFloat(const char* Name, float Value);
    // slot 12: LAB_06361c20 (RET 8): a new named float, or null if the name
    // exists. PORT-LOCAL NAME.
    virtual IVP_U_Active_Float* CreateActiveFloat(const char* Name, float Value);
    // slot 13: LAB_06361d60 (RET 8). PORT-LOCAL NAME.
    virtual IVP_U_Active_Int* GetActiveInt(const char* Name, int32_t Value);
    // slot 14: LAB_06361cc0 (RET 8). PORT-LOCAL NAME.
    virtual IVP_U_Active_Int* CreateActiveInt(const char* Name, int32_t Value);
};
static_assert(offsetof(IVP_U_Active_Value_Manager, DeleteOnEnvDelete) == 0x04, "Manager+0x04");
static_assert(offsetof(IVP_U_Active_Value_Manager, DelayedFloats) == 0x10, "Manager+0x10");
static_assert(offsetof(IVP_U_Active_Value_Manager, CurrentTime) == 0x20, "Manager+0x20");
static_assert(sizeof(IVP_U_Active_Value_Manager) == 0x28, "IVP_U_Active_Value_Manager is operator_new(0x28)");
