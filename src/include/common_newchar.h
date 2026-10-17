// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"

// Common.dll NewChar::Database + RtcSlider + pub::StateGraph (state_graph.db).
// Linked into freelancer.exe. Not a Common.dll project. Not MSVCP60 STL.
// See docs/re/common.md (NewChar, RtcSlider, StateGraph).

#include "common_id.h"

#include <map>
#include <set>
#include <string>
#include <vector>

class INI_Reader;

// ---------------------------------------------------------------------------
// NewChar::Database — DAT_064012e0, TheDB = &object.
// DataConfig vcall +0x38 (slot 14) = Load; shutdown vcall +0x14 (slot 5) = Shutdown.
// Retail vtable is 15 thiscall slots; this port is not layout-compatible.
//
// Retail ctor is FUN_06313890 (stamps PTR_LAB_063a37bc, inits all 4
// collections empty). Retail dtor is FUN_06313c80 (D92 2026-09-09,
// decomp-tracker item 2164 -- previously misread by an earlier pass as a
// *constructor* purely from the vtable-pointer store at this+0, without
// noticing the function's body destroys every element of all 4 collections
// before returning; MSVC destructors re-stamp their own vtable too, so that
// store alone doesn't distinguish ctor from dtor). Confirmed dtor: iterates
// and frees every node of Extras (this+0x40), Pilots (this+0x2c), Packages
// (this+0x18) then Factions (this+0x4) via the same erase/free helpers Shutdown
// uses (FUN_06318070/FUN_06317bd0/FUN_06317730 + FUN_06391d5a per node),
// which is exactly this port's `~Database() { Shutdown(); }` (line ~138)
// calling `Pilots.clear(); Packages.clear(); Factions.clear(); Extras.clear();`
// -- a full functional rewrite (STL container dtors instead of manual RB-tree
// walk), not a byte-for-byte match, so item 2164 is tracked `skipped` with
// ported_name NewChar::Database::~Database rather than `ready_unverified`.
//
// SLOT LABELS CORRECTED 2026-09-24 (claude:srv-pdb). Retail's tree at this+0x04
// is the FACTION tree and the one at this+0x2c the PILOT tree, not the other way
// round. Proof, Common.dll's own Load (.text:063149d0):
//   [Faction] (.text:06314a40) -> CALL [EDX+0x18] (.text:06314d42, slot 6) and
//             CALL [EAX+0x24] (.text:06314c3c, slot 9, the home-base table)
//   [Package] (.text:06314d8d) -> CALL [EAX+0x1c] (.text:06314fb2, slot 7)
//   [Pilot]   (.text:06314fd8) -> CALL [EDX+0x20] (.text:0631543c, slot 8)
// and slot 6 LAB_06314100 inserts into `LEA ESI,[EDI+0x4]`, slot 8
// LAB_063144c0 into `LEA ESI,[EDI+0x2c]`. The finds agree: slot 0
// LAB_06313e10 reads EDI+0x4, slot 2 LAB_06313eb0 reads EDI+0x2c; so do the
// removes (slot 10 LAB_06314850: EDI+0x4, slot 12 FUN_06314910: EDI+0x2c).
// Server.dll's FUN_06d4b790 confirms it from outside: slot 0 gets the
// request's faction id, slot 2 its pilot id. Every "Pilot"/"Faction" below
// that names an offset or a slot has been swapped to match.
//
// Retail slots 0-3 (vcall +0x00../+0x0c, LAB_06313e10/e60/eb0/f00) are
// FindFaction/FindPackage/FindPilot/FindExtra respectively -- identical
// std::_Tree find-via-comparator (FUN_062fcfb0) shape, one per collection
// offset (this+0x4/0x18/0x2c/0x40).
//
// Retail slot 4 (vcall +0x10, LAB_06313f50) is ForEachFaction below. It is a
// classic in-order (ascending NicknameId) walk of the tree at this+0x04 --
// `MOV EAX,[ECX+0x8]` is that tree's head -- invoking callback(NicknameId,
// &Faction, Context) per node and stopping early if the callback returns
// false in AL. (D95 2026-09-09 read the walk correctly but called the tree
// "Pilots".) The previously-blocking "byte flag at [node+0x8+0x61]" is NOT
// record data at all -- it is the tree's own internal nil/header-
// sentinel marker: Common.dll.txt:319958 (inside the ctor, FUN_06313890)
// forces it to 1 immediately after allocating the +0x04 tree's header node,
// while every real-node allocator in this cluster (FUN_06319370:329670,
// FUN_06319120:329423, FUN_063195e0's equivalent) zeroes it on construction.
// LAB_06313f50 tests this bit purely to detect subtree boundaries while
// computing the in-order successor (classic descend-right-then-leftmost /
// ascend-via-parent algorithm) -- it never reads or writes anything belonging
// to the record's own fields. Confirmed real external caller:
// Freelancer.exe.txt 0x005aaebc (`NewChar::TheDB` vcall+0x10) with
// callback = LAB_005aaee0 (Freelancer.exe.txt:662949+): that callback reads
// only its 3rd argument (the context struct, ignoring the per-node args on
// this pre-flight path) and grows a caller-owned output array (classic
// operator_new-based vector growth, magic-number /5-ish divide at
// Freelancer.exe.txt:662957-662963) -- i.e. Freelancer.exe's new-character-
// creation UI uses ForEachFaction to build one output record per entry into
// its own growable list -- one entry per FACTION (the tree this slot walks),
// i.e. the multiplayer new-character faction picker. std::map already iterates in ascending
// key order, matching the retail RB-tree walk exactly, so a plain range-for
// is a faithful (not merely equivalent-effect) port of this method.
//
// Retail slots 6-13 (vcall +0x18.. +0x34, LAB_06314100.. FUN_06314970) are a
// uniform Insert/Remove pair per collection, confirmed by reading each one
// against the raw disasm:
//   slot 6  +0x18 LAB_06314100  Insert Faction (record @ this+0x04)
//   slot 7  +0x1c LAB_06314280  Insert Package (record @ this+0x18)
//   slot 8  +0x20 LAB_063144c0  Insert Pilot   (record @ this+0x2c)
//   slot 9  +0x24 LAB_063146c0  Insert 4th     (record @ this+0x40)
//   slot 10 +0x28 LAB_06314850  Remove Faction (erases by key, shares the
//   slot 11 +0x2c FUN_063148b0  Remove Package  std::_Tree erase helper also
//   slot 12 +0x30 FUN_06314910  Remove Pilot    used by Shutdown's own loop)
//   slot 13 +0x34 FUN_06314970  Remove 4th
// All four collections are MSVCP60 std::_Tree (red-black tree / std::map),
// not VC6 std::list -- confirmed by the shared node-erase helper
// (FUN_06317c90 for the 4th collection) doing classic RB-tree rotation with
// a node "color" byte at +0x25, and by Shutdown (LAB_06313fd0) walking all
// four collections through the same erase-in-a-loop pattern.
//
// The 4th collection (this+0x40, LAB_063146c0 / FUN_06314970) is a real,
// lifecycle-managed member -- Shutdown clears it right after Factions.
// CORRECTED 2026-09-24 (claude:srv-save-2): Load() DOES populate it -- every
// [Faction] `base = X` inserts {CreateID(X), X} through slot 9 (Common.dll
// .text:06314ba8 .. .text:06314c3c), so it is the home-base table and
// Server.dll's FUN_06d4b790 reads it as %%Home_Base%% / %%Home_System%%.
// The paragraph below that says otherwise is superseded. Its Insert record layout,
// read directly off LAB_063146c0, is `{ ID_String Key; std::basic_string<char>
// Value; }` -- the node constructor (FUN_06316330) default-inits the key via
// `ID_String::clear` (see common_id.h: ID_String is Common's own 4-byte
// nickname-id wrapper, sizeof 4), the comparator FUN_062fcfb0 does a plain
// unsigned dword compare, and the insert path inlines std::basic_string::
// assign to copy Value. So Key follows the exact same CreateID()-style
// nickname-id convention as Pilots/Packages/Factions. Insert fails (first
// wins) on a duplicate key, matching the other three collections and this
// port's ReadPilot/ReadPackage/ReadFaction semantics.
//
// What is NOT resolved: what the id/string actually represent. NewChar::TheDB
// (DAT_063ed464) has zero CALL-through xrefs inside Common.dll.txt, but the
// real caller has since been traced in Server.dll.txt: FUN_06d4b790, the
// server-side CreateNewCharacter handler (builds NewCharacter.fl /
// MPNewCharacter.fl from a template, logs "LOG: Create character on account
// ..."). It Finds all 4 collections (Pilot/Package/this 4th one/Faction) from
// four consecutive dword ids in a request struct as an OR-gate before
// proceeding, then fills template placeholders %%Faction%%/%%Package%%/
// %%Base_Costume%%/%%Comm_Costume%%/%%Voice%%/%%Money%%/%%Home_System%%/
// %%Home_Base%%/%%Name%%. Load() fills the collection from every [Faction]
// `base` ({CreateID(base), base}); FindExtra returns that stored record and
// Server.dll writes its string as %%Home_Base%% (.text:06d4be80-06d4be8b).
// ---------------------------------------------------------------------------
// The [Pilot] record, 0x64 bytes in retail. Layout read off Common.dll's Load
// (.text:06314ff1..06315082: the local at [ESP+0x40] -- ID_String::clear on +0x00, string ctors
// on +0x04 / +0x24 / +0x44 / +0x54, the vector at +0x14 zeroed inline, a vector ctor on +0x34)
// and the slot 8 insert LAB_063144c0 (copies +0x00, assigns +0x04, vector operator= on +0x14,
// assigns +0x24, FUN_06315b90 on +0x34, assigns +0x44 / +0x54). The nickname is not stored.
// Server.dll's FUN_06d4b790 reads +0x08 / +0x28 / +0x48 (the three strings' pointers).
struct FNewCharPilot
{
    unsigned NicknameId;                // +0x00 "nickname" -> CreateID (.text:063150c2)
    std::string Body;                   // +0x04 "body"
    std::vector<std::string> BodyAnims; // +0x14 "body.anim", every parameter
    std::string Comm;                   // +0x24 "comm"
    std::vector<std::string> CommAnims; // +0x34 "comm.anim", every parameter
    std::string Voice;                  // +0x44 "voice"
    std::string Thumb;                  // +0x54 "thumb"
};

// The [Package] record, 0x30 bytes in retail. Layout read off Common.dll's Load
// (.text:06314da6..06314df3: the local at [ESP+0xa4] -- ID_String::clear on +0x00, string ctors
// on +0x0c / +0x1c; +0x04 / +0x08 / +0x2c are not initialised) and the slot 7 insert
// LAB_06314280 (copies +0x00 / +0x04 / +0x08, assigns +0x0c / +0x1c, copies +0x2c). The
// nickname is not stored. Server.dll's FUN_06d4b790 reads +0x10 / +0x20 (the strings'
// pointers) and +0x2c.
struct FNewCharPackage
{
    unsigned NicknameId; // +0x00 "nickname" -> CreateID (.text:06314e2a)
    int StridName;       // +0x04 "strid_name", ftol(get_value_float(0))
    int StridDesc;       // +0x08 "strid_desc", ftol(get_value_float(0))
    std::string Ship;    // +0x0c "ship"
    std::string Loadout; // +0x1c "loadout"
    int Money;           // +0x2c "money", ftol(get_value_float(0))
};

// The fourth tree's record (slot 9 LAB_063146c0): {+0x00 id, +0x04 std::string}. Load builds one
// for every [Faction] "base" (.text:06314ba8..06314c3c).
struct FNewCharExtra
{
    unsigned Id;       // +0x00 CreateID(base)
    std::string Value; // +0x04 the base nickname
};

// The [Faction] record, 0x50 bytes in retail. Layout read off Common.dll's Load
// (.text:06314a59..06314d42: the local at [ESP+0xf4] -- ID_String::clear on +0x00, a
// basic_string ctor on +0x04, FUN_063159e0 (set ctor) on +0x14 / +0x28 / +0x3c) and the
// slot 6 insert LAB_06314100 (copies +0x00, assigns the string at +0x04, then the three
// trees at +0x14 / +0x28 / +0x3c). freelancer.exe copies the same 0x50-byte record into
// CLoginMgr+0x1d8 (LAB_005aaee0 / FUN_005af120) and reads the first key of each tree
// (0x0056aaa7..0x0056aae3); Server.dll's FUN_06d4b790 reads RepGroup (+0x08, the string's
// pointer) for %%Faction%%.
struct FNewCharFaction
{
    unsigned NicknameId;          // +0x00 "nickname" -> CreateID (.text:06314b1e)
    std::string RepGroup;         // +0x04 "rep_group" (.text:06314b5e assign)
    std::set<unsigned> Bases;     // +0x14 "base" -> CreateID, FUN_063167e0 insert (.text:06314c5b)
    std::set<unsigned> Packages;  // +0x28 "package" -> CreateID (.text:06314d19 via 0x06314cc1)
    std::set<unsigned> Pilots;    // +0x3c "pilot" -> CreateID (.text:06314d19 via 0x06314d12)
};

namespace NewChar
{
    // -----------------------------------------------------------------------
    // Retail's IDatabase is pure-abstract with EXACTLY SIXTEEN virtuals, and
    // the order below is retail's own slot order, not a convenience ordering.
    // Evidence (2026-09-17, claude:common-vtable-id; decomp-tracker interface
    // catalog "NewChar::IDatabase", id 66):
    //   - the interface's vftable PTR_LAB_063a3804 (063a3804..063a3840) holds
    //     sixteen slots that ALL point at the shared _purecall stub
    //     LAB_06391fb0 -- that is what proves "pure-abstract, sixteen methods";
    //   - the concrete vftable PTR_LAB_063a37bc (063a37bc..063a37f8)
    //     independently has sixteen slots matching 1:1;
    //   - this is NOT the usual "byte_size overcounted by one slot" case:
    //     063a37fc..063a37ff is zero padding and 063a3800 is the COL belonging
    //     to the 063a3804 vtable;
    //   - RTTI: [063a3804-4] -> COL 063aa0d4 -> TypeDescriptor 063ed468 =
    //     ".?AVIDatabase@NewChar@@"; the implementation's TypeDescriptor
    //     063ed488 = ".?AVDatabase@NewChar@@".
    //
    // THERE IS NO VIRTUAL DESTRUCTOR. Slot 0 is FindFaction. The port used to
    // declare one, which shifted every slot by one and demoted thirteen of the
    // sixteen methods to non-virtual members of Database -- corrected here.
    // Dropping it is safe for the same reason retail gets away with it: the
    // sole instance is the static GNewCharDatabase singleton (retail
    // .data:064012e0), published through TheDB and never deleted through the
    // interface pointer.
    // -----------------------------------------------------------------------
    class IDatabase
    {
    public:
        virtual FNewCharFaction const* FindFaction(unsigned NicknameId) const = 0;  // slot 0,  vcall +0x00
        virtual FNewCharPackage const* FindPackage(unsigned NicknameId) const = 0;  // slot 1,  vcall +0x04
        virtual FNewCharPilot const* FindPilot(unsigned NicknameId) const = 0;      // slot 2,  vcall +0x08
        virtual FNewCharExtra const* FindExtra(unsigned Id) const = 0;              // slot 3,  vcall +0x0c
        virtual bool ForEachFaction(bool(__cdecl* Callback)(unsigned, FNewCharFaction const*, void*),
                                    void* Context) const = 0;                       // slot 4,  vcall +0x10
        virtual void Shutdown() = 0;                                                // slot 5,  vcall +0x14
        // Slots 6..9 take a finished record by pointer (RET 4) and answer true when it was
        // inserted, false when its key is already there (first wins). Load parses each INI
        // section inline and hands the record over through the vtable.
        virtual bool InsertFaction(FNewCharFaction const& Faction) = 0;             // slot 6,  vcall +0x18
        virtual bool InsertPackage(FNewCharPackage const& Package) = 0;             // slot 7,  vcall +0x1c
        virtual bool InsertPilot(FNewCharPilot const& Pilot) = 0;                   // slot 8,  vcall +0x20
        virtual bool InsertExtra(FNewCharExtra const& Extra) = 0;                   // slot 9,  vcall +0x24
        virtual bool RemoveFaction(unsigned NicknameId) = 0;                        // slot 10, vcall +0x28
        virtual bool RemovePackage(unsigned NicknameId) = 0;                        // slot 11, vcall +0x2c
        virtual bool RemovePilot(unsigned NicknameId) = 0;                          // slot 12, vcall +0x30
        virtual bool RemoveExtra(unsigned Id) = 0;                                  // slot 13, vcall +0x34
        virtual bool Load(char const* Path) = 0;                                    // slot 14, vcall +0x38

        // Slot 15 (vcall +0x3c) -- a REAL slot, not the trailing-COL artifact:
        // 063a37f8 holds a live code pointer and the padding/COL only begins at
        // 063a37fc/063a3800. Its implementation address 0x0629ba10 carries the
        // symbol CEShield::GetEquipDesc, which proves NOTHING about the
        // receiver: the entire body is `XOR AL,AL ; RET 0x4` and /OPT:ICF has
        // folded it into ~14 unrelated vtables. All that is provable: it is
        // __thiscall, takes exactly 4 bytes of argument, and returns false in
        // AL. Deliberately left under an offset name -- do NOT promote a
        // guessed semantic name.
        // TODO: identify slot 15's meaning from a caller outside Common.dll.
        virtual bool Slot15(void* Arg) = 0;                                         // slot 15, vcall +0x3c
    };

    // Not exported: retail Common.dll exports none of Database's members or its vftable,
    // only the ?TheDB@NewChar@@3PAVIDatabase@1@A pointer below. Other modules reach it
    // through IDatabase.
    class Database : public IDatabase
    {
    public:
        Database();
        ~Database();

        Database(Database const&) = delete;
        Database& operator=(Database const&) = delete;

        bool Load(char const* Path) override;
        void Shutdown() override;

        bool Slot15(void* Arg) override;

        FNewCharFaction const* FindFaction(unsigned NicknameId) const override;
        FNewCharPackage const* FindPackage(unsigned NicknameId) const override;
        FNewCharPilot const* FindPilot(unsigned NicknameId) const override;

        // Retail slot 4 (vcall +0x10, LAB_06313f50): walk Factions in ascending
        // NicknameId order, invoking Callback(NicknameId, &Faction, Context) per
        // entry and stopping early if Callback returns false. See the class
        // comment above for the full evidence trail (D95 2026-09-09,
        // decomp-tracker item 2169). Returns true if the walk completed
        // without being stopped early (retail's own callers never inspect its
        // return value, but this mirrors the early-stop semantics cleanly).
        bool ForEachFaction(bool(__cdecl* Callback)(unsigned, FNewCharFaction const*, void*), void* Context) const override;

        // Retail slot 10/11/12 (vcall +0x28/+0x2c/+0x30, LAB_06314850 /
        // FUN_063148b0 / FUN_06314910): per-key erase on Factions/Packages/
        // Pilots, same find-then-erase-by-key shape as RemoveExtra below
        // (confirmed by reading all three against the raw disasm, D92
        // 2026-09-09). Shutdown() only bulk-clears, so per-key Remove was a
        // genuine capability gap before these were filled in.
        bool RemoveFaction(unsigned NicknameId) override;
        bool RemovePackage(unsigned NicknameId) override;
        bool RemovePilot(unsigned NicknameId) override;

        // Retail slot 9 (vcall +0x24, LAB_063146c0) / slot 13 (vcall +0x34,
        // FUN_06314970): the home-base table at retail this+0x40, filled by
        // Load from every [Faction] "base".
        bool InsertExtra(FNewCharExtra const& Extra) override;
        bool RemoveExtra(unsigned Id) override;
        // LAB_06313f00 returns the whole stored record (node+0x10); Server.dll reads its
        // string (+0x08 = the string's pointer) as %%Home_Base%%.
        FNewCharExtra const* FindExtra(unsigned Id) const override;

        // Retail slots 6/7/8 (vcall +0x18/+0x1c/+0x20, LAB_06314100 /
        // LAB_06314280 / LAB_063144c0): the Faction / Package / Pilot inserts of a
        // finished record -- find by the +0 key, false when present, else a new
        // node with every member copied, true. Load() (slot 14) is their only
        // in-module caller.
        bool InsertFaction(FNewCharFaction const& Faction) override;
        bool InsertPackage(FNewCharPackage const& Package) override;
        bool InsertPilot(FNewCharPilot const& Pilot) override;

    private:
        std::map<unsigned, FNewCharPilot> Pilots;
        std::map<unsigned, FNewCharPackage> Packages;
        std::map<unsigned, FNewCharFaction> Factions;
        std::map<unsigned, FNewCharExtra> Extras;
    };

    // ?TheDB@NewChar@@3PAVIDatabase@1@A
    COMMON_API extern IDatabase* TheDB;
}

// ---------------------------------------------------------------------------
// RtcSlider — DAT_063fbbc4 slider float, map at +4 (DAT_063fbbc8).
// set → CompareMode 0 (enabled if Threshold <= slider).
// set_less → CompareMode 1 (enabled if slider < Threshold).
// ---------------------------------------------------------------------------
struct FRtcSliderSetting
{
    unsigned NameId;
    float Threshold;
    int CompareMode;
};

namespace RtcSlider
{
    COMMON_API void __cdecl load(char const* Path);
    COMMON_API bool __cdecl is_enabled(char const* Name);
    COMMON_API bool __cdecl is_enabled(unsigned long NameCrc);
    COMMON_API void __cdecl set_slider(float Value);
    COMMON_API float __cdecl get_slider();
    COMMON_API float __cdecl get_fps_cap();
    COMMON_API float __cdecl get_default_fps_cap();
    // 0x0627a710 ?validate_setting@RtcSlider@@YA_NPBD0@Z: `MOV AL,1; RET` (ICF-folded with
    // AttachedEquipment::is_attached and four Directive*Op::validate). Both arguments are
    // ignored. RoomData's loaders call it on every priority token before CreateID.
    COMMON_API bool __cdecl validate_setting(char const* Name, char const* FileName);
}

// ---------------------------------------------------------------------------
// pub::StateGraph — FUN_062df5d0 / FUN_062df7a0 / DAT_063fca18+DAT_063fca1c.
// Record stride 0x72c: Id + Type + Name[0x40] + float[21][21].
// ---------------------------------------------------------------------------
enum
{
    kStateGraphBehaviors = 0x15,
    kStateGraphNameChars = 0x40
};

struct IStateGraph
{
    int Id;
    int Type;
    char Name[kStateGraphNameChars];
    float Weights[kStateGraphBehaviors][kStateGraphBehaviors];
};

namespace pub
{
    namespace StateGraph
    {
        enum Type
        {
            LEADER = 0,
            ESCORT = 1
        };

        // Not exported by retail (see the FUN_062df5d0 / FUN_062df7a0 note above).
        void __cdecl LoadFromFile(char const* Path);
        void __cdecl Free();
        COMMON_API void __cdecl refresh_state_graph();
        COMMON_API int __cdecl get_state_graph(char const* Name, Type GraphType);
        COMMON_API int __cdecl get_state_graph(int GraphId, Type GraphType);
        COMMON_API IStateGraph const* __cdecl get_state_graph_internal(int GraphId);
    }
}
