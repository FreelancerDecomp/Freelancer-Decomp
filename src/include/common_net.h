// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"

// Common.dll NET batch — MarshalBuffer, Exclusion, StateMachine*, CPlayerAccount,
// MPCritSec, MessageHandler, CommReferrable, Watchable, MD5Hash (forward).
//
// MD5Hash itself lives in src/include/md5hash.h (MD5HASH_API). We forward-declare only.
// FmtStr, EquipDescList, GoodDescList are not yet ported; opaque stubs here.

#include <cstddef>
#include <cstring>
#include <list>
#include <string>
#include "common_universe.h" // Universe::IZone (FULLPORT-MOVE[OBJ] from here)
#include "common_reputation.h" // TString<N> (used by CommReferrable::GetMsgIdPrefix)
#include <windows.h>           // CRITICAL_SECTION (MPCritSec); already reached via common_text.h

// ---------------------------------------------------------------------------
// Forward declarations for types used by MarshalBuffer but not yet ported
// ---------------------------------------------------------------------------

struct FmtStr;          // TEXT batch
class EquipDescList;    // EQUIP batch
struct GoodDescList;    // GOODS/ROOM batch -- real definition in common_goods.h

// Costume is already defined in common_reputation.h
#include "common_reputation.h"

class INI_Reader;       // common_ini.h — full definition available at link time

COMMON_API bool __cdecl IsMPServer();
COMMON_API void __cdecl SetIsMPServer(bool bIsMPServer);

// ---------------------------------------------------------------------------
// Watchable — intrusive doubly-linked watcher list node
// Layout: single pointer at +0x00 (head of watcher chain)
// sizeof == 4
// ---------------------------------------------------------------------------

struct BaseWatcher; // common_goods.h

struct COMMON_API Watchable
{
    BaseWatcher* Head; // +0x00 — first BaseWatcher of the chain watching this object (linked by BaseWatcher::Next)

    Watchable();
    ~Watchable();
    Watchable& operator=(const Watchable& Other);
    // 0x063415e0 ?unwatch@Watchable@@QAEIXZ: detach every watcher, return how many.
    unsigned int unwatch();
};

static_assert(sizeof(Watchable) == 0x04, "Watchable size");

// ---------------------------------------------------------------------------
// CommReferrable - holder of a voice "msg_id_prefix" (base of Archetype::Ship at +0x94 and
// of GoodInfo at +0x00). Layout: vptr +0x00, TString<64> +0x04 (sizeof 0x48; the copy ctor
// .text:06261080 copies 0x11 dwords from +4). Retail vftable ??_7CommReferrable@@6B@
// (.rdata:063993e8): HasVoiceInfo, GetMsgIdPrefix, read_comm_ini_data -- no virtual
// destructor, and retail exports no ??1 at all.
// ---------------------------------------------------------------------------

struct COMMON_API CommReferrable
{
    CommReferrable();
    CommReferrable(const CommReferrable& Other);
    CommReferrable& operator=(const CommReferrable& Other);

    // Slot 0. .text:0629d860 (ICF-folded with TractorArm::IsOn): `return MsgIdPrefix.Length != 0`.
    virtual bool HasVoiceInfo() const;

    // Slot 1. .text:0632e7a0 (`LEA EAX,[ECX+4]; RET`, ICF-folded with unrelated getters).
    virtual TString<64> const& GetMsgIdPrefix() const;

    // Slot 2. .text:0632e7b0: "msg_id_prefix" -> MsgIdPrefix; false for any other key.
    virtual bool read_comm_ini_data(INI_Reader& Reader);

    TString<64> MsgIdPrefix;    // +0x04
};

static_assert(sizeof(CommReferrable) == 0x48, "CommReferrable size");
static_assert(offsetof(CommReferrable, MsgIdPrefix) == 0x04, "CommReferrable+0x04 msg_id_prefix");

// ---------------------------------------------------------------------------
// MPCritSec — lazy-initialized CRITICAL_SECTION wrapper
// Layout: CRITICAL_SECTION (0x18 bytes on x86) + byte flag at +0x18 (MOV/TEST byte
// [this+0x18] in the ctor/dtor/lock, 06304750..0630479a). Total sizeof == 0x1C.
// ---------------------------------------------------------------------------

struct COMMON_API MPCritSec
{
public:
    MPCritSec();
    ~MPCritSec();

    // Common.dll.txt 0x06261d70 -- retail's operator= is a raw 7-dword memberwise copy of
    // the whole object (CRITICAL_SECTION bytes and the bInitialized flag included). That
    // aliases the OS critical section between the two objects; retail carries the same
    // footgun (whichever copy is destroyed first invalidates the other). No caller in the
    // port copies a live MPCritSec today, so the raw copy is kept for fidelity.
    MPCritSec& operator=(MPCritSec const& Other);

    // 0x06304780 ?lock@MPCritSec@@QAEXXZ / 0x063047b0 ?unlock@MPCritSec@@QAEXXZ.
    // Both are no-ops while DAT_063ed17c (single-player flag) is set.
    void lock();
    void unlock();

private:
    CRITICAL_SECTION CriticalSection; // +0x00, initialised on the first lock()
    bool bInitialized;                // +0x18
};

// The two asserts together pin bInitialized at +0x18 (offsetof cannot name the private field here).
static_assert(sizeof(CRITICAL_SECTION) == 0x18, "CRITICAL_SECTION is 0x18 bytes on x86");
static_assert(sizeof(MPCritSec) == 0x1C, "MPCritSec size");

// ---------------------------------------------------------------------------
// MarshalBuffer — binary serialization buffer
// Layout from Ghidra (ctor writes 5 DWORDs + 1 byte):
//   +0x00  BufferBase     (unsigned char*)
//   +0x04  BufferSize     (int)
//   +0x08  Cursor         (unsigned char*)
//   +0x0C  Remaining      (int)
//   +0x10  NumWritten     (int)
//   +0x14  bReadMode      (unsigned char / bool)
// sizeof == 0x15 → padded to 0x18 by MSVC alignment
// operator= copies 6 DWORDs = 24 bytes → confirms 0x18 with padding
// ---------------------------------------------------------------------------

class COMMON_API MarshalBuffer
{
public:
    MarshalBuffer();
    MarshalBuffer& operator=(const MarshalBuffer& Other);

    void clear();

    // Buffer setup
    void use_default();
    void use_read_buffer(const unsigned char* Data, int Size);
    void use_write_buffer(unsigned char* Data, int Size);

    // Primitive read/write
    void put_void(const void* Src, int Count);
    void get_void(void* Dst, int Count);
    void skip_over(int Count);

    void put_U8(unsigned char Value);
    unsigned char get_U8();

    void put_U16(unsigned short Value);
    unsigned short get_U16();

    void put_U32(unsigned long Value);
    unsigned long get_U32();

    void put_int(int Value);
    int get_int();

    void put_float(float Value);
    float get_float();

    void put_bool(bool Value);
    bool get_bool();

    // String serialization (length-prefixed; -1 = null)
    void put_string(const char* Str);
    char* get_string();                              // malloc'd — caller frees
    void get_string_to_buffer(char* Buf, int MaxLen);

    // Retail put_wstring/get_wstring (PBG/PAG = VC6 wchar_t); wchar_t here.
    void put_wstring(const wchar_t* Str);
    wchar_t* get_wstring();                          // malloc'd — caller frees

    // Image helpers (trivial wrappers around put/get_void)
    void put_image(const unsigned char& V);
    void get_image(unsigned char* V);
    void put_image(const unsigned short& V);
    void get_image(unsigned short* V);
    void put_image(const int& V);
    void get_image(int* V);
    void put_image(const unsigned long& V);
    void get_image(unsigned long* V);
    void put_image(const float& V);
    void get_image(float* V);

    // FL_ID (same as U32 but semantically distinct)
    void put_FL_ID(unsigned int Id);
    unsigned int get_FL_ID();

    // Complex types — real logic where evident, stubs otherwise
    void put_FmtStr(const FmtStr& Fmt);
    FmtStr get_FmtStr();

    void put_EquipDescList(const EquipDescList& List);
    EquipDescList get_EquipDescList();

    void put_GoodDescList(const GoodDescList& List);
    GoodDescList get_GoodDescList();

    void put_Costume(const Costume& C);
    Costume get_Costume();

    // Status
    bool is_empty() const;
    int  get_num_written() const;
    unsigned char const* get_buffer() const;    // .text:06261d90 (ICF fold): BufferBase
    int  get_buffer_size() const;               // .text:062d1ad0 (ICF fold): BufferSize

private:
    unsigned char* BufferBase;   // +0x00
    int            BufferSize;   // +0x04
    unsigned char* Cursor;       // +0x08
    int            Remaining;    // +0x0C
    int            NumWritten;   // +0x10
    unsigned char  bReadMode;    // +0x14
    // padding to 0x18
};

static_assert(sizeof(MarshalBuffer) == 0x18, "MarshalBuffer size");

// ---------------------------------------------------------------------------
// STATE_MACHINE_EVENT_TYPE enum
// ---------------------------------------------------------------------------

enum STATE_MACHINE_EVENT_TYPE
{
    SMET_None           = 0,
    SMET_Tick           = 1,
    SMET_Message         = 2,
    SMET_UserEvent      = 3,
    SMET_Controller     = 4,
};

// ---------------------------------------------------------------------------
// State machine family. Retail exports these classes member by member: no vftable, no
// compiler-generated copy/assignment and no destructor it did not declare is exported, so
// COMMON_API sits on the members retail exports, not on the classes.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// SMEventData — base event passed to state machine process()
// Layout: vtable+0, EventType+4 → sizeof == 8. The vtable (.rdata:063a4998) has one slot,
// get_event_type -- no virtual destructor. SMMessage (063a49f8) and SMControllerEvent
// (063a4a00) have their own one-slot tables holding the same body.
// ---------------------------------------------------------------------------

struct SMEventData
{
    COMMON_API SMEventData(STATE_MACHINE_EVENT_TYPE Type);             // 0x06333040

    // Slot 0. 0x062d1ad0 (ICF-shared): MOV EAX,[this+4].
    COMMON_API virtual STATE_MACHINE_EVENT_TYPE get_event_type() const;

    COMMON_API bool is_sm_event() const;                               // 0x06333060
    COMMON_API bool is_user_event() const;                             // 0x06333070

    STATE_MACHINE_EVENT_TYPE EventType;  // +0x04
};

static_assert(sizeof(SMEventData) == 0x08, "SMEventData size");

// ---------------------------------------------------------------------------
// SMControllerEvent — controller-type event (type==4)
// Layout: vtable+0, type(4)+4, ControllerId+8, DataPtr+0xC → sizeof == 0x10
// ---------------------------------------------------------------------------

struct SMControllerEvent : SMEventData
{
    int         ControllerId;   // +0x08
    const void* DataPtr;        // +0x0C

    COMMON_API SMControllerEvent(int Id, const void* Data);           // 0x06333180
};

static_assert(sizeof(SMControllerEvent) == 0x10, "SMControllerEvent size");

// ---------------------------------------------------------------------------
// SMMessage — wraps a StateMachineMessage* as an event (type==3)
// Layout: vtable+0, type(3)+4, MessagePtr+8 → sizeof == 0x0C
// ---------------------------------------------------------------------------

class StateMachineMessage; // forward

struct SMMessage : SMEventData
{
    StateMachineMessage* Message;  // +0x08

    COMMON_API SMMessage(StateMachineMessage* Msg);                    // 0x06333160
};

static_assert(sizeof(SMMessage) == 0x0C, "SMMessage size");

// ---------------------------------------------------------------------------
// SM_MESSAGE_SCOPE enum
// ---------------------------------------------------------------------------

enum SM_MESSAGE_SCOPE
{
    SMMS_Broadcast  = 0,
    SMMS_Self       = 1,
    SMMS_Target     = 2,
};

// ---------------------------------------------------------------------------
// StateMachineMessage — network/delayed message base class
// Layout from clone() which allocates 0x1c and copies offsets 4..0x18:
//   +0x00  vtable  (.rdata:063a4980: [0] scalar deleting dtor, [1] clone)
//   +0x04  MessageType      (unsigned int)
//   +0x08  Sender           (unsigned int)
//   +0x0C  Receiver         (unsigned int)
//   +0x10  DeliveryState    (int, default -1)
//   +0x14  DeliveryTime     (float)
//   +0x18  bDelivered       (bool/byte)
// sizeof == 0x1C
// ---------------------------------------------------------------------------

class StateMachineMessage
{
public:
    COMMON_API StateMachineMessage();                                   // 0x06333290
    COMMON_API StateMachineMessage(unsigned int Type);                  // 0x063332c0
    // Common.dll.txt 0xd32f0 (??0StateMachineMessage@@QAE@MIIIH@Z -- float,uint,uint,uint,int).
    // +0x04 = Type, +0x08 = Sender, +0x0C = Receiver, +0x10 = State, +0x14 = Time, and
    // bDelivered = 0 unconditionally.
    COMMON_API StateMachineMessage(float DeliveryTime, unsigned int Type, unsigned int Sender,
                                   unsigned int Receiver, int State);    // 0x063332f0
    COMMON_API StateMachineMessage(const StateMachineMessage& Other);   // 0x06333330
    COMMON_API virtual ~StateMachineMessage();                          // 0x06332ee0
    COMMON_API virtual StateMachineMessage* clone() const;              // 0x06332ef0

    // ??2/??3StateMachineMessage: a bare jump to msvcrt malloc / free (0x062fd560 /
    // 0x062fd570, ICF-shared with Async::Action, GoodDesc, IAction, FmtStr::Val).
    // noexcept: a VC6 new-expression tests the result for null before constructing
    // (clone 06332f00 / 06332fe0 return null on a failed malloc); a non-throwing allocation
    // function gives the same test.
    COMMON_API static void* operator new(size_t Size) noexcept;
    COMMON_API static void operator delete(void* Ptr);

    // Field accessors. Most bodies are ICF-shared one-liners elsewhere in the image.
    COMMON_API unsigned int get_message_type() const;                   // 0x062d1ad0  [+0x04]
    COMMON_API void set_message_type(unsigned int Type);                // 0x06309b30
    COMMON_API unsigned int get_sender() const;                         // 0x062a66a0  [+0x08]
    COMMON_API void set_sender(unsigned int InSender);                  // 0x0631d940
    COMMON_API unsigned int get_receiver() const;                       // 0x0629fb90  [+0x0C]
    COMMON_API void set_receiver(unsigned int InReceiver);              // 0x06332f40
    COMMON_API int get_delivery_state() const;                          // 0x06263a70  [+0x10]
    COMMON_API void set_delivery_state(int State);                      // 0x06332f50
    COMMON_API float get_delivery_time() const;                         // 0x06332f60  [+0x14]
    COMMON_API void set_delivery_time(float Time);                      // 0x0633bad0
    COMMON_API bool is_delivered() const;                               // 0x06263bc0  [+0x18]
    COMMON_API void set_delivered(bool bInDelivered);                   // 0x06263bd0
    COMMON_API void set_delivery_options(float Time, unsigned int InSender,
                                         unsigned int InReceiver, int State); // 0x06333370

    COMMON_API static unsigned int GenerateUniqueMessageType();         // 0x06332f70

    unsigned int  MessageType;      // +0x04
    unsigned int  Sender;           // +0x08
    unsigned int  Receiver;         // +0x0C
    int           DeliveryState;    // +0x10
    float         DeliveryTime;     // +0x14
    unsigned char bDelivered;       // +0x18

private:
    // Retail: ?s_unique_message_type_generator@StateMachineMessage@@0IA (0 = private static).
    COMMON_API static unsigned int s_unique_message_type_generator;
};

static_assert(sizeof(StateMachineMessage) == 0x1C, "StateMachineMessage size");

// ---------------------------------------------------------------------------
// SMMChangeState — change-state message (extends StateMachineMessage)
// Adds NewState at +0x1C → sizeof == 0x20. Vtable .rdata:063a498c: [0] scalar deleting dtor
// (implicit, not exported), [1] clone.
// ---------------------------------------------------------------------------

class SMMChangeState : public StateMachineMessage
{
public:
    COMMON_API SMMChangeState(int NewStateVal);                         // 0x06332fa0
    COMMON_API virtual StateMachineMessage* clone() const override;     // 0x06332fd0

    int NewState;   // +0x1C
};

static_assert(sizeof(SMMChangeState) == 0x20, "SMMChangeState size");

// ---------------------------------------------------------------------------
// IMessageRouter — abstract message routing interface. Nothing of it is exported and nothing
// in Common.dll implements it; the class only stores the pointer and calls through it.
// Slots from the call sites -- there is NO virtual destructor in front of them:
//   +0x00  MessageHandler::send_immediate_message 0x06333429 `CALL [EDX]` (5 args)
//   +0x04  MessageHandler::MessageHandler         0x063333ad `CALL [EAX+0x4]`, result = id
//   +0x08  MessageHandler::~MessageHandler        0x063333fe `CALL [EDX+0x8]`
//   +0x0C  StateMachineGroup::remove_state_machine 0x0633380d `CALL [EAX+0xc]`(Id), result
//          is the registered MessageHandler sub-object (the group subtracts 0x10)
// The handler passed to and returned from slots 1-3 is always the MessageHandler (sub-)object
// that registered itself. Method names are this port's own.
// ---------------------------------------------------------------------------

class MessageHandler;

class IMessageRouter
{
public:
    virtual void RouteMessage(StateMachineMessage* Msg, float Delay,
                              unsigned int Target, unsigned int Sender,
                              int Scope) = 0;                       // +0x00
    virtual unsigned int RegisterHandler(MessageHandler* Handler) = 0; // +0x04
    virtual void UnregisterHandler(MessageHandler* Handler) = 0;       // +0x08
    virtual MessageHandler* LookupHandlerById(unsigned int Id) = 0;    // +0x0C
};

// ---------------------------------------------------------------------------
// MessageHandler — sends messages via IMessageRouter
// Layout: vtable+0, Router+4, HandlerId+8 → sizeof == 0x0C
// Vtable .rdata:063a49cc: [0] scalar deleting dtor, [1] receive_message = MSVCRT purecall
// (retail MessageHandler is abstract), [2] send_immediate_message, [3] send_delayed_message,
// [4] send_delayed_message_to_me.
// ---------------------------------------------------------------------------

class MessageHandler
{
public:
    COMMON_API virtual ~MessageHandler();                               // 0x063333f0
    virtual void receive_message(StateMachineMessage* Msg) = 0;         // slot 1, pure

    COMMON_API MessageHandler(IMessageRouter* Router);                  // 0x063333a0

    // 0x062a66a0 (ICF-shared): MOV EAX,[this+8].
    COMMON_API unsigned int get_id() const;

protected:
    COMMON_API virtual void send_immediate_message(StateMachineMessage* Msg, unsigned int Target);          // 0x06333410
    COMMON_API virtual void send_delayed_message(StateMachineMessage* Msg, float Delay, unsigned int Target); // 0x06333430
    COMMON_API virtual void send_delayed_message_to_me(StateMachineMessage* Msg, float Delay, int Scope);  // 0x06333460

    IMessageRouter* Router;         // +0x04
    unsigned int    HandlerId;      // +0x08
};

static_assert(sizeof(MessageHandler) == 0x0C, "MessageHandler size");

// ---------------------------------------------------------------------------
// StateMachine — simple two-state FSM
// Layout: vtable+0, CurrentState+4, PendingState+8, bPendingChange+0xC, bProcessing+0xD
// sizeof == 0x10 (with alignment padding)
// ---------------------------------------------------------------------------

class StateMachine
{
public:
    COMMON_API virtual ~StateMachine();                                 // slot 0, 0x06333220
    COMMON_API virtual void process(const SMEventData* Event);          // slot 1, 0x063339c0

    // Retail vtable slot 2, read out of StateMachine's own vtable at .rdata:0x063a49e4:
    //     063a49e4  LAB_06333200                      <- slot 0, scalar deleting dtor
    //     063a49e8  StateMachine::process             <- slot 1
    //     063a49ec  LAB_06391fb0                      <- slot 2, MSVCRT purecall
    //     063a49f0  StateMachine::handle_state_change <- slot 3
    // The same purecall entry is still present in both derived tables
    // (StateMachineMessageHandler 0x063a49a8, StateMachineGroup 0x063a4a10), so all three are
    // abstract in retail; concrete machines in other modules supply it. Common.dll never
    // implements it, so there is nothing to name it from. Its shape comes from the calls in
    // process (0x063339d5 / 063339e6) and handle_state_change (06333a2d / 06333a4f):
    // (event, pointer to a state -- &CurrentState, or the .rdata -1 "any state"), returning
    // a bool that process tests. The second argument is taken as a const reference.
    virtual bool Slot2(const SMEventData* Event, int const& State) = 0;

    COMMON_API StateMachine();                                          // 0x063331e0

    COMMON_API int get_state() const;                                   // 0x062d1ad0 [+0x04]
    COMMON_API void start();                                            // 0x06333230
    COMMON_API void reset();                                            // 0x06333260

protected:
    COMMON_API virtual void handle_state_change();                      // slot 3, 0x06333a00
    COMMON_API void set_state(int NewState);                            // 0x06333270

    int           CurrentState;     // +0x04
    int           PendingState;     // +0x08
    unsigned char bPendingChange;   // +0x0C
    unsigned char bProcessing;      // +0x0D
};

static_assert(sizeof(StateMachine) == 0x10, "StateMachine size");

// ---------------------------------------------------------------------------
// StateMachineMessageHandler — receives messages, extends MessageHandler
//
// Confirmed via retail's vtable dump (Common.dll.txt @0x63a49a0/0x63a49b4) and
// the ctor (Common.dll.c @0xd3080, 0x186640): this is MULTIPLE inheritance,
// StateMachine first (offset 0x00, its vtable) then MessageHandler second
// (offset 0x10, its own vtable) — sizeof 0x10 + 0x0C == 0x1C matches exactly,
// and no other fields are needed. The destructor is implicit (not exported).
//
// This is also why every "Handler->vtable[1](event)" call site elsewhere
// (StateMachineGroup::add_state_machine, ::process's machine-iteration loop) is
// really just `Handler->process(Event)`: StateMachine is the first base, so a
// StateMachineMessageHandler* converts to StateMachine* with no adjustment.
// ---------------------------------------------------------------------------

class StateMachineMessageHandler : public StateMachine, public MessageHandler
{
public:
    COMMON_API StateMachineMessageHandler(IMessageRouter* Router);     // 0x06333080

    COMMON_API virtual void receive_message(StateMachineMessage* Msg) override; // 0x06333480

protected:
    // Not virtual: ?send_delayed_message_to_me@StateMachineMessageHandler@@IAEX... (IAE).
    COMMON_API void send_delayed_message_to_me(StateMachineMessage* Msg, float Delay,
                                               SM_MESSAGE_SCOPE Scope, int TargetId); // 0x063334b0
};

static_assert(sizeof(StateMachineMessageHandler) == 0x1C, "StateMachineMessageHandler size");

// ---------------------------------------------------------------------------
// StateMachineGroup — manages multiple StateMachineMessageHandlers
//
// Confirmed via retail's ctor (Common.dll.c @0xd3530): it performs the exact
// same base-construction vtable sequence as StateMachineMessageHandler's own
// ctor before patching in its own final vtables — i.e. StateMachineGroup
// inherits StateMachineMessageHandler (single inheritance).
// Vtable .rdata:063a4a08: [0] dtor, [1] process, [2] purecall, [3] handle_state_change,
// [4] add_state_machine, [5] remove_state_machine(unsigned), [6] remove_state_machine(const*).
// MSVC lays out overloaded virtuals in reverse declaration order, hence the pointer overload
// is declared first.
// Layout: StateMachineMessageHandler (0x1C), then two VC6 std::lists (allocator byte, head
// node, size; nodes are {next, prev, value}): +0x1C the machines, +0x28 the machines marked
// for removal, held as iterators into the first list (remove_state_machine 0x063337a0 stores
// the found node). Retail sizeof 0x34; the port's lists are the current CRT's, so this class
// is smaller here and nothing reaches its fields by offset.
// ---------------------------------------------------------------------------

class StateMachineGroup : public StateMachineMessageHandler
{
public:
    COMMON_API virtual ~StateMachineGroup();                            // 0x06333610

    COMMON_API StateMachineGroup(IMessageRouter* Router);              // 0x06333530

    COMMON_API virtual void add_state_machine(StateMachineMessageHandler* Handler);          // 0x06333730
    COMMON_API virtual void remove_state_machine(const StateMachineMessageHandler* Handler); // 0x063337a0
    COMMON_API virtual void remove_state_machine(unsigned int Id);                          // 0x06333800

protected:
    COMMON_API virtual void process(const SMEventData* Event) override; // 0x06333840
    COMMON_API void remove_dying_machines();                            // 0x06333950

    typedef std::list<StateMachineMessageHandler*> FMachineList;

    FMachineList                     Machines;      // +0x1C (retail)
    std::list<FMachineList::iterator> DyingMachines; // +0x28 (retail)
};

// ---------------------------------------------------------------------------
// CPlayerAccount — player authentication/account hash manager
// Layout: vtable+0, HashNamePtr+4, HashVerifyPtr+8 → sizeof == 0x0C
// HashNamePtr and HashVerifyPtr point to heap-allocated MD5Hash objects (sizeof 0xDC each)
// Static: g_dwAccountIndex at RVA 0x19CEB4
// ---------------------------------------------------------------------------

class MD5Hash; // md5hash.h (DALib)

class COMMON_API CPlayerAccount
{
public:
    virtual ~CPlayerAccount();

    CPlayerAccount();
    CPlayerAccount(const CPlayerAccount& Other);

    void GenerateAccount(const char* Name);
    bool LoadAccount();
    bool LoadAccountFromStrings(const char* NameHash, const char* VerifyHash);
    bool VerifyAccount();

    // Text key management (static methods)
    static bool GenerateTextKey(char* OutKey);
    static bool GetTextKey(char* OutKey);
    static bool SetTextKey(char* Key);
    // 0x062a8920 ?SetAcctIndex@CPlayerAccount@@SAXK@Z: retail body is a bare RET (ICF-folded
    // with other empty functions); it does not store into g_dwAccountIndex.
    static void SetAcctIndex(unsigned long Index);

    // Account name accessors
    const char* GetAccountNameChar();
    const char* GetAccountNameSigChar();

    // Retail returns VC6 basic_string<unsigned short> (G = VC6 wchar_t) by value; ours is
    // the same function returning std::wstring:
    //   0xa6940 ?GetAccountName@CPlayerAccount@@QAE?AV?$basic_string@G...@std@@XZ
    //   0xa6ad0 ?GetAccountNameSig@CPlayerAccount@@QAE?AV?$basic_string@G...@std@@XZ
    //   0xa6c40 ?GetServerSignature@CPlayerAccount@@QAE?AV?$basic_string@G...@std@@PBD@Z
    std::wstring GetAccountName();
    std::wstring GetAccountNameSig();
    std::wstring GetServerSignature(const char* Text);

protected:
    void TossHashes();

    MD5Hash* HashName;      // +0x04 (heap, sizeof 0xDC)
    MD5Hash* HashVerify;    // +0x08 (heap, sizeof 0xDC)

    static unsigned long g_dwAccountIndex;
};

static_assert(sizeof(CPlayerAccount) == 0x0C, "CPlayerAccount size");

// ---------------------------------------------------------------------------
// Exclusion namespace classes
// ---------------------------------------------------------------------------

// Freelancer.exe's inlined copy of AsteroidExclusionZoneMgr::set_max_cliasteroid_exclusion_
// distance (defined in cli_asteroid.cpp; befriended below, not exported).
struct FCliAsteroidExclusionInline;
// Port-only reader of NebulaExclusionZoneMgr's zone list for the Direct3D 9 volumetric nebula
// (freelancer.exe Client/nebula_volume.cpp; befriended below, not exported, read-only).
struct FNebulaVolumeExclusionAccess;

namespace Exclusion
{

// ExcludeObjectType — used by is_excluded virtual
enum ExcludeObjectType
{
    EOT_CliAsteroid       = 0,
    EOT_CmnAsteroid       = 1,
    EOT_Billboard         = 2,
    EOT_DynamicAsteroid   = 3,
};

// AsteroidExclusionZone — per-zone exclusion data
// Layout from ctor: zone_ptr+0, max_dist+4, exclude_billboards+8(byte),
//                   exclude_dynamic_asteroids+9(byte), padding, dist_sq_cache+0xC,
//                   another_float+0x10
// sizeof == 0x14
struct COMMON_API AsteroidExclusionZone
{
    // User-declared so that no implicit move members are generated and exported:
    // VC6 had none, and retail exports exactly the copy assignment.
    AsteroidExclusionZone& operator=(AsteroidExclusionZone const&) = default;

    Universe::IZone const* ZonePtr;   // +0x00 — IZone const* (get_zone returns it as such)
    // Despite the "MaxDist" name, this is a LIVE per-frame proximity cache, not a static
    // config value: update_dist() (Common.dll.c 94101-94154) overwrites it every call with
    // (distance-to-observer minus zone radius)^2 (or 0 if inside/unrecognized shape), and
    // AsteroidExclusionZoneMgr::is_cliasteroid_excluded/is_cmnasteroid_excluded
    // (common_exclusionzone.cpp) compare it against the Mgr's static MaxCliDist/MaxDynDist. Ctor default
    // 10000000.0f (0x4b189680) is a "far away" sentinel before the first update_dist call.
    float         MaxDistSq;          // +0x04
    unsigned char bExcludeBillboards; // +0x08 — read by is_billboard_excluded (common_exclusionzone.cpp)
    // +0x09 — a SECOND, separate per-zone bool flag, distinct from bExcludeBillboards. Confirmed
    // by direct disasm cross-check (Common.dll.txt is_dynamic_asteroid_excluded @0x5c370 and the
    // inlined AsteroidExclusionZoneMgr::is_excluded case-3 body @0x5be70 both read byte[Zone+0x9]
    // to gate the InZone() test; the ctor @0x5c480 and the manual raw-alloc path inside
    // AsteroidExclusionZoneMgr::push_back @0x5bde0 both explicitly zero-init it right alongside
    // bExcludeBillboards). This field was previously undiscovered and mis-documented as padding.
    // Like bExcludeBillboards, nothing in the recovered call graph ever sets it to true -- it's
    // presumably populated by the still-BLOCKED ~500-line [AsteroidField]/exclusion-zone INI
    // parser (Common.dll.c 0x44940), so it is always false in this port until that loader exists.
    unsigned char bExcludeDynamicAsteroids; // +0x09 — read by is_dynamic_asteroid_excluded
    // padding +0x0A..+0x0B
    // "empty_cube_frequency" of the [Exclusion Zones] entry (CAsteroidField::load 062a4fc0 stores
    // it when >= 0). is_cliasteroid_excluded / is_cmnasteroid_excluded hand it out when it is
    // >= 0 (formerly DistSqCache).
    float         EmptyCubeFrequency; // +0x0C — default -1.0f
    // "billboard_count" (CAsteroidField::load 062a4f40 stores float(ftol(value)) when > 0);
    // normalize_billboard_density rescales it, is_billboard_excluded hands it out (formerly
    // Field_0x10).
    float         BillboardCount;     // +0x10 — default -1.0f

    AsteroidExclusionZone();
    // .text:062a8920: an empty RET shared with ~NebulaExclusionZone; retail exports it.
    ~AsteroidExclusionZone();
    void update_dist(const Vector& Pos);
    bool set_zone(Universe::IZone const* Zone, Universe::IZone const* Parent);
    // .text:06261d90 (ICF fold): MOV EAX,[this]; RET.
    Universe::IZone const* get_zone();
};

static_assert(sizeof(AsteroidExclusionZone) == 0x14, "AsteroidExclusionZone size");

// NebulaExclusionZone -- sizeof 0x10. The ctor (.text:062bcbc0) and the inlined copy in
// NebulaExclusionZoneMgr::push_back (.text:062bc73b: operator_new(0x10)) write +0x00 = 0,
// +0x04 = 1e7, +0x08 = -1.0f and clear the ID_String at +0x0c; operator= (.text:06287290,
// folded with CEquipTraverser::operator=) copies exactly four dwords.
struct COMMON_API NebulaExclusionZone
{
    // User-declared so that no implicit move members are generated and exported:
    // VC6 had none, and retail exports exactly the copy assignment.
    NebulaExclusionZone& operator=(NebulaExclusionZone const&) = default;

    Universe::IZone const* ZonePtr;   // +0x00
    // NOTE: despite sharing AsteroidExclusionZone's field name/offset, this is NOT a static
    // per-zone config limit here -- update_dist() (Common.dll.c 94511-94545) overwrites it every
    // call with the current shell-distance-squared (0 if inside the shell, computed distance^2
    // otherwise), and NebulaExclusionZoneMgr::is_in_exclusion_zone reads it back as a live proximity
    // cache (`Zone->MaxDistSq <= 10.0f`, common_exclusionzone.cpp). Ctor default 10000000.0f
    // (0x4b189680) is just a "far away" sentinel before the first update_dist call.
    float         MaxDistSq;          // +0x04
    // "fog_far" from the nebula INI: get_fog_far .text:06261da0 (FLD [this+8]) and
    // set_fog_far .text:0631d940; freelancer.exe stores it inline (.text:00505bcd).
    float         FogFar;             // +0x08 — default -1.0f
    ID_String     ZoneShellName;      // +0x0C — CreateID of the "zone_shell" mesh name

    NebulaExclusionZone();
    // .text:062a8920: an empty RET shared with ~AsteroidExclusionZone; retail exports it.
    ~NebulaExclusionZone();
    void update_dist(const Vector& Pos);
    bool set_zone(Universe::IZone const* Zone, Universe::IZone const* Parent);
    // .text:06261d90 (ICF fold): MOV EAX,[this]; RET.
    Universe::IZone const* get_zone();
    float get_fog_far();
    void set_fog_far(float Far);

    // .text:062a2db0 returns the ID_String at +0x0C by value; .text:062bcd50 stores
    // CreateID(Name) into it.
    ID_String get_zone_shell_name();
    void set_zone_shell_name(const char* Name);
};

static_assert(sizeof(NebulaExclusionZone) == 0x10, "NebulaExclusionZone size");

// ExclusionZoneMgr -- the abstract base of both managers (RTTI '.?AVExclusionZoneMgr@Exclusion@@',
// COL .rdata:063a80b4 at vftable [-4]). Its vftable .rdata:0639dcf0 holds three _purecall slots
// (LAB_06391fb0) in the order below; the derived copy constructors store it first
// (.text:062a2b98 / 062a2dc8) before their own table. No destructor slot, and the destructor
// is not virtual (both managers' are QAE). Retail exports nothing of this class, so it carries
// no COMMON_API (C4275 on the exported managers is expected).
class ExclusionZoneMgr
{
public:
    virtual void push_back(Universe::IZone const* Zone, Universe::IZone const* Parent) = 0; // +0x00
    // Retail declares OutValue / Observer as void*; every caller passes a float* (or null)
    // and a Vector* respectively.
    virtual bool is_excluded(const Vector& Pos, ExcludeObjectType Type, float* OutValue) = 0; // +0x04
    virtual void sort(Vector const* Observer) = 0;                                           // +0x08

protected:
    ~ExclusionZoneMgr() = default;
};

#pragma warning(push)
#pragma warning(disable : 4275)

// AsteroidExclusionZoneMgr — vector<AsteroidExclusionZone*> + vtable
// Layout: vtable+0, field_4+4, begin+8, end+0xC, capacity+0x10,
//         max_cli_dist+0x14, max_dyn_dist+0x18
// sizeof == 0x1C
class COMMON_API AsteroidExclusionZoneMgr : public ExclusionZoneMgr
{
public:
    // NOT virtual in retail: ??1AsteroidExclusionZoneMgr@Exclusion@@QAE@XZ (Q, not U), and
    // ??_7AsteroidExclusionZoneMgr@Exclusion@@6B@ holds no destructor slot.
    ~AsteroidExclusionZoneMgr();
    // Declaration order is the retail vftable ??_7AsteroidExclusionZoneMgr (.rdata:0639dce0):
    // slot 0 push_back 0x5bde0, slot 1 is_excluded 0x5be70, slot 2 sort 0x5bfe0. Freelancer.exe
    // calls is_excluded as slot +0x4 (FUN_0054e900 0x0054ef7d / 0x0054f1cd).
    void push_back(Universe::IZone const* Zone, Universe::IZone const* Parent) override;
    bool is_excluded(const Vector& Pos, ExcludeObjectType Type, float* OutValue) override;
    // update_dist(*Observer) on every zone (no null test), then the zones sorted by MaxDistSq.
    void sort(Vector const* Observer) override;

    AsteroidExclusionZoneMgr();
    AsteroidExclusionZoneMgr(const AsteroidExclusionZoneMgr& Other);
    AsteroidExclusionZoneMgr& operator=(const AsteroidExclusionZoneMgr& Other);

    AsteroidExclusionZone* back();
    int  get_max_billboard_count();
    void normalize_billboard_density(int Count);
    void set_max_cliasteroid_exclusion_distance(float Dist);
    void set_max_dynamicasteroid_exclusion_distance(float Dist);

private:
    // Freelancer.exe inlines set_max_cliasteroid_exclusion_distance at 0x00524919..0x00524939
    // (FUN_00524880): the distance stays on the x87 stack, is squared there (FLD ST0 / FMUL
    // ST1) and rounded to float once by FSTP [mgr+0x14]. The exported setter takes a float,
    // which rounds the distance before squaring; the exe's copy reaches MaxCliDist through
    // this friend instead. No export and no Common behaviour change.
    friend struct ::FCliAsteroidExclusionInline;

    // Private in retail (AAE): ?is_cliasteroid_excluded@...@AAE_NABVVector@@PAM@Z etc.
    bool is_cliasteroid_excluded(const Vector& Pos, float* OutDist);
    bool is_cmnasteroid_excluded(const Vector& Pos, float* OutDist);
    bool is_billboard_excluded(const Vector& Pos, float* OutDist);
    bool is_dynamic_asteroid_excluded(const Vector& Pos);

    unsigned char           ZonesAllocator;       // +0x04 VC6 vector allocator byte, never read
    AsteroidExclusionZone** Begin;                // +0x08
    AsteroidExclusionZone** End;                  // +0x0C
    AsteroidExclusionZone** CapacityEnd;          // +0x10
    // Both hold SQUARED distances (compared directly against AsteroidExclusionZone::MaxDistSq,
    // itself squared). Ctor defaults confirmed from Common.dll.c 93536-93553: this+0x14 =
    // 0x4a742400 = 4000000.0f (2000^2), this+0x18 = 0x48742400 = 250000.0f (500^2). The setters
    // (Common.dll.txt 126483-126506) square their float argument before storing here.
    float                   MaxCliDist;           // +0x14 — default 4000000.0f (2000^2)
    float                   MaxDynDist;           // +0x18 — default 250000.0f (500^2)
};

static_assert(sizeof(AsteroidExclusionZoneMgr) == 0x1C, "AsteroidExclusionZoneMgr size");

// NebulaExclusionZoneMgr — same shape as AsteroidExclusionZoneMgr but only
// ONE distance float (+0x14) — copy ctor 0x42dc0 copies through +0x14 only.
// sizeof == 0x18
class COMMON_API NebulaExclusionZoneMgr : public ExclusionZoneMgr
{
public:
    // ??1NebulaExclusionZoneMgr@Exclusion@@QAE@XZ -- Q, so NOT virtual. The class still has a
    // vptr because is_excluded / push_back / sort are; only the destructor stays out of it.
    ~NebulaExclusionZoneMgr();
    // Retail vftable ??_7NebulaExclusionZoneMgr (.rdata:0639dd00): slot 0 push_back 0x5c720,
    // slot 1 is_excluded 0x5c840, slot 2 sort 0x5c890.
    void push_back(Universe::IZone const* Zone, Universe::IZone const* Parent) override;
    bool is_excluded(const Vector& Pos, ExcludeObjectType Type, float* OutValue) override;
    // update_dist inlined on every zone with a ZonePtr (no null test of Observer), then the
    // zones sorted by MaxDistSq.
    void sort(Vector const* Observer) override;

    NebulaExclusionZoneMgr();
    NebulaExclusionZoneMgr(const NebulaExclusionZoneMgr& Other);
    NebulaExclusionZoneMgr& operator=(const NebulaExclusionZoneMgr& Other);

    NebulaExclusionZone* back();
    bool is_in_exclusion_zone(const Vector& Pos, NebulaExclusionZone** OutZone,
                              float* OutDist, bool Flag);
    void set_max_distance(float Dist);

private:
    // Port-only: reads [Begin, End) (Client/nebula_volume.cpp). No layout or export change.
    friend struct ::FNebulaVolumeExclusionAccess;

    unsigned char         ZonesAllocator;         // +0x04 VC6 vector allocator byte, never read
    NebulaExclusionZone** Begin;                  // +0x08
    NebulaExclusionZone** End;                    // +0x0C
    NebulaExclusionZone** CapacityEnd;            // +0x10
    // SQUARED distance (set_max_distance squares its argument, confirmed ICF-shared code with
    // AsteroidExclusionZoneMgr's setters, Common.dll.txt 126483-126506). Retail's default ctor
    // (.text:062bc660) never writes it: indeterminate until set_max_distance (named UB; the
    // port starts it at 0.0f). Nothing in Common reads it.
    float                 MaxDist;                // +0x14
};

static_assert(sizeof(NebulaExclusionZoneMgr) == 0x18, "NebulaExclusionZoneMgr size");

#pragma warning(pop)

} // namespace Exclusion
