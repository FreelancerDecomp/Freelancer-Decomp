// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"

// Common.dll FuseDB + PetalDB + WeaponModDB.
// Linked into freelancer.exe. Not a Common.dll project. Not MSVCP60 STL.
// See docs/re/common.md (Fuse, Petal, WeaponMod).

#include "common_id.h"
#include "common_reputation.h" // TString<N> (FuseDB::m_FuseINIFiles)

#include <list>
#include <map>
#include <string>
#include <vector>

// Retail Common.dll exports every member of this STL instantiation (VC6 std::vector, 47
// members, imported by name by FreeLancer.exe, Server.dll and rpclocal.dll). The port
// exports the modern MSVC instantiation instead: the definition is in common_fuses.cpp; other modules
// import it through this declaration. It must precede the first use that would implicitly
// instantiate the class, which is why it sits right after
// the includes. Retail's user is FuseAction::TriggeredSubObjs (sub-object ids, copied by
// FuseAction's copy ctor 0x5d460 and operator= 0x5d4e0), so the element stays unsigned
// short: it is not a VC6 wide string.
#ifndef COMMON_EXPORTS
extern template class COMMON_API std::vector<unsigned short>;
#endif

class INI_Reader;

// ---------------------------------------------------------------------------
// FuseAction / Fuse / FuseDB -- retail Common.dll .text:062bd020-062bef64.
//
// Common.dll owns only the shared base classes and the list of fuse INI paths. Retail never
// parses a fuse file here: FuseDB::AddFuseINIFile (.text:062be260) just remembers the path,
// and each consumer module builds its own fuse database from FuseDB::m_FuseINIFiles with its
// own FuseAction subclasses -- freelancer.exe FUN_0052a7a0 (ClientFuse / StartEffectAction /
// StartExplosionAction / ...), Server.dll FUN_06cfd4b0. Those subclasses read their INI
// sections through the protected helpers Fuse::ReadFuseValues and
// FuseAction::ReadFuseActionValue, and clone themselves through the pure virtual at
// FuseAction vftable slot 1, which copies the shared fields with CopyArchProperties.
//
// Layout (retail offsets; the port uses modern std containers, see "No retail ABI"):
//   FuseAction (sizeof 0x24): +0x00 vptr, +0x04 float AtT, +0x08 float AtTEnd (-1 = not a
//     range), +0x0c int RefCount, +0x10 bool bTriggered, +0x14 std::vector<unsigned short>
//     TriggeredSubObjs.
//   Fuse (sizeof 0x34): +0x00 vptr, +0x04 ID_String Nickname, +0x08 float Lifetime,
//     +0x0c bool bDeathFuse, +0x10 std::multimap<float, FuseAction*> Actions (keyed by the
//     action's trigger time; _Multi byte = 1 at 062bd064), +0x24 bool bNeedsRandomize,
//     +0x28 std::list<FFuseBurn> Burns (node value 0x14 bytes, MOVSD count 5 at 062bde0b).
// ---------------------------------------------------------------------------

// One live ignition of a Fuse: the std::list value at Fuse+0x28 (node+0x08 .. node+0x1c).
// Written by Fuse::IgniteAt (.text:062bdd90): {SourceId, SubObjId, AtT, AtT, AtT}. Burn
// widens the [Lower, Upper] window by Dt/Lifetime each tick (Lower clamped at 0, Upper at 1).
struct FFuseBurn
{
    unsigned SourceId;       // +0x00 (node+0x08)
    unsigned short SubObjId; // +0x04 (node+0x0c)
    float IgniteT;           // +0x08 (node+0x10) -- the AtT the burn was lit at
    float Lower;             // +0x0c (node+0x14)
    float Upper;             // +0x10 (node+0x18)
};

// ??_7FuseAction@@6B@ (.rdata:0639ef0c), 12 slots:
//   0 scalar deleting dtor (062bd690)   6 IsTriggered() const (06286e60, ICF)
//   1 pure (_purecall)                  7 Trigger(unsigned, unsigned short)
//   2 pure (_purecall)                  8 UnTrigger(unsigned short)
//   3 Load                              9 ShouldRandomize() const
//   4 UnLoad                           10 Randomize()
//   5 IsTriggered(unsigned short) const 11 CopyArchProperties(FuseAction const&)
class COMMON_API FuseAction
{
public:
    FuseAction();
    FuseAction(FuseAction const& Other);
    virtual ~FuseAction();

    FuseAction& operator=(FuseAction const& Other);

    // Slot 1 (+0x04): pure. Called with no arguments by the Fuse copy constructor
    // (.text:062bda55 CALL [EAX+0x4]) and its result stored as the clone's action pointer; every
    // freelancer.exe override (FUN_0052a0f0 / 0052a1b0 / 0052a280 / 0052a360 / 0052a430) is
    // `new <same class>` + slot-12 copy of *this. Retail name unknown.
    virtual FuseAction* Slot04() const = 0;
    // Slot 2 (+0x08): pure. Called by freelancer.exe FUN_00529e10 (.text:0052a071 CALL
    // [EDX+0x8], one INI_Reader& argument, bool result) right after the action object is
    // created for its section header; every override loops read_value and calls
    // ReadFuseActionValue first. Retail name unknown.
    virtual bool Slot08(INI_Reader& Reader) = 0;

    virtual int Load();
    virtual int UnLoad();

    // MSVC lays overloaded virtuals out in reverse declaration order: declaring the no-argument
    // overload first puts IsTriggered(unsigned short) in slot 5 and IsTriggered() in slot 6, as
    // in ??_7FuseAction@@6B@.
    virtual bool IsTriggered() const;
    virtual bool IsTriggered(unsigned short SubObjId) const;
    virtual void Trigger(unsigned SourceId, unsigned short SubObjId);
    virtual void UnTrigger(unsigned short SubObjId);

    virtual bool ShouldRandomize() const;
    virtual float Randomize();

    float GetTriggerTime() const;

protected:
    virtual void CopyArchProperties(FuseAction const& Other);
    bool ReadFuseActionValue(INI_Reader& Reader);

    float AtT;                                   // +0x04
    float AtTEnd;                                // +0x08
    int RefCount;                                // +0x0c
    bool bTriggered;                             // +0x10
    std::vector<unsigned short> TriggeredSubObjs; // +0x14
};

// ??_7Fuse@@6B@ (.rdata:0639ef00): scalar deleting dtor, FreeActions.
class COMMON_API Fuse
{
public:
    // Retail has no separate default ctor: ??_FFuse@@QAEXXZ (.text:062bd280) is the default
    // constructor closure, which calls this ctor with ID_String{CreateID("")} (.rdata:063997b0
    // is an empty string) and 0xbf800000 = -1.0f.
    Fuse(ID_String const& Nickname = ID_String{CreateID("")}, float Lifetime = -1.0f);
    Fuse(Fuse const& Other);
    virtual ~Fuse();

    virtual void FreeActions();

    Fuse& operator=(Fuse const& Other);

    void Load() const;
    void UnLoad() const;

    bool IgniteAt(unsigned SourceId, unsigned short SubObjId, float AtT);
    void Burn(float Dt);
    bool UnBurn(unsigned short SubObjId, float AtT);

    void RandomizeActions();
    bool OverrideLifetime(float NewLifetime);

    bool IsBurning() const;
    bool IsSpent() const;
    bool IsSpentAt(float T) const;

    // Inline getters retail exports from ICF-shared one-instruction bodies.
    // ?IsDeathFuse@Fuse@@QBE_NXZ .text:06263b90 `MOV AL,[ECX+0xc]`.
    bool IsDeathFuse() const { return bDeathFuse; }
    // ?Lifetime@Fuse@@QBEMXZ .text:06261da0 `FLD [ECX+0x8]`.
    float Lifetime() const { return LifetimeSec; }
    // ?ActionCount@Fuse@@QBEHXZ .text:0629d880 `MOV EAX,[ECX+0x20]` (the action tree's size).
    int ActionCount() const { return (int)Actions.size(); }
    // ?Name@Fuse@@QBEABUID_String@@XZ .text:0632e7a0 `LEA EAX,[ECX+4]`.
    ID_String const& Name() const { return Nickname; }

protected:
    bool ReadFuseValues(INI_Reader& Reader);

    ID_String Nickname;                          // +0x04
    float LifetimeSec;                           // +0x08
    bool bDeathFuse;                             // +0x0c
    std::multimap<float, FuseAction*> Actions;   // +0x10
    bool bNeedsRandomize;                        // +0x24
    std::list<FFuseBurn> Burns;                  // +0x28
};

// Retail FuseDB is a class, not a namespace: ?AddFuseINIFile@FuseDB@@SAXPBD@Z and
// ?Shutdown@FuseDB@@SAXXZ are public static members, ??4FuseDB@@QAEAAV0@ABV0@@Z is its
// (implicit) copy assignment, and the INI path list is a protected static data member,
// ?m_FuseINIFiles@FuseDB@@1V?$list@U?$TString@$0BAE@@@...@std@@A (.data:063fc4c4). Consumer
// modules read the list from a class derived from FuseDB (freelancer.exe FUN_0052a7a0 walks it
// directly).
class COMMON_API FuseDB
{
public:
    // User-declared so that no implicit move members are generated and exported: VC6 had
    // none, and retail exports exactly the copy assignment.
    FuseDB& operator=(FuseDB const&) = default;

    static void __cdecl AddFuseINIFile(char const* Path);
    static void __cdecl Shutdown();

protected:
    static std::list<TString<260>> m_FuseINIFiles;
};

// ---------------------------------------------------------------------------
// PetalInterfaceDatabase — DAT_06401768, 7 category maps stride 0x14.
// ObjectTable value names (stricmp): room=0, cart=5, prop=6.
// get_category names are case-sensitive: Room/Spaceship/Solar/Asteroid/
// Equipment/Equipment Cart/Prop.
// ---------------------------------------------------------------------------

enum EPetalCategory
{
    PetalCategoryRoom = 0,
    PetalCategorySpaceship = 1,
    PetalCategorySolar = 2,
    PetalCategoryAsteroid = 3,
    PetalCategoryEquipment = 4,
    PetalCategoryEquipmentCart = 5,
    PetalCategoryProp = 6,
    PetalCategoryCount = 7,
};

class COMMON_API PetalInterfaceDatabase
{
public:
    PetalInterfaceDatabase();
    ~PetalInterfaceDatabase();

    // Retail exports both (06272e00 copy ctor, 06272f90 operator=): each is the compiler's
    // memberwise copy -- a 7-iteration loop (stride 0x14) of inlined std::map copy
    // construction / assignment (clear, then tree copy FUN_06278290). Defaulted members do
    // the same, petal_record::Mesh included (a std::string deep copy, as retail).
    PetalInterfaceDatabase(PetalInterfaceDatabase const&) = default;
    PetalInterfaceDatabase& operator=(PetalInterfaceDatabase const&) = default;

    void load_from_ini(char const* RelPath);
    void destroy();

    static int __cdecl get_category(char const* Name);
    // ?find_record@PetalInterfaceDatabase@@QBEPBUpetal_record@1@HK@Z -- the record type
    // is nested in the database, not a free struct.
    //
    // Retail layout, proven by the pair copy ctor FUN_0631d440 (0631d468-0631d4e0) and by
    // find_record (0631cb45 LEA EAX,[EAX+EAX*4] / 0631cb48 LEA ESI,[ECX+EAX*4] -> stride
    // 0x14 = sizeof(std::map); 0631cb69 CMP EDX,[EAX+0xc] -> key at node+0xc; 0631cb84
    // ADD EAX,0x10 -> value at node+0x10). The record is 0x24 bytes:
    //   +0x00 unsigned NicknameId   (0631d46e MOV [EBP],ECX = map key, 0631d473 [EBP+4] = rec+0)
    //   +0x04 std::basic_string<char> Mesh  (allocator byte copied at 0631d476/0631d482,
    //         _Ptr/_Len/_Res zeroed at 0631d484-0631d48a, then basic_string::assign 0631d49c)
    //   +0x14 std::vector<char*> Extras     (allocator byte at 0631d4a2, _First at 0631d4d2,
    //         _Last/_End from [ESI+0x1c]/[ESI+0x18]; element stride 4 -- 0631d4bc SAR EAX,2)
    // There is NO separate Nickname string in retail: add_record only feeds the nickname to
    // CreateID (0631c89b) and drops it. Mesh is the record's own std::string copy of the ini
    // value (add_record 0631c8e1-0631c91d: basic_string assign of strlen bytes); readers take
    // c_str() (freelancer.exe FUN_00446160 at 0x004461ed). Extras are StringAlloc(name, true).
    struct petal_record
    {
        unsigned NicknameId;
        std::string Mesh;
        std::vector<char const*> Extras;
    };

    petal_record const* find_record(int Category, unsigned long NicknameId) const;

private:
    // ?add_record@PetalInterfaceDatabase@@AAEXHPBD0PAPADH@Z -- the fourth parameter is
    // PAPAD (char **), not a pointer-to-const array.
    void add_record(int Category, char const* Nickname, char const* Mesh, char** ExtraNames,
                    int ExtraCount);

    std::map<unsigned, petal_record> Categories[PetalCategoryCount];
};

COMMON_API void __cdecl PetalDB_load(char const* RelPath);
COMMON_API void __cdecl PetalDB_destroy();
COMMON_API PetalInterfaceDatabase const* __cdecl PetalDB_get();

// ---------------------------------------------------------------------------
// WeaponModDB — unnamed FUN_06341790 / FUN_06341bb0 / FUN_06341c70.
// [WeaponType] nickname + shield_mod = shield_nick, float.
// ---------------------------------------------------------------------------
struct FWeaponType
{
    unsigned NicknameId;
    std::map<unsigned, float> ShieldMods;
};

// Unexported retail functions (FUN_*): Common.dll-internal, no caller outside Common.dll.
void __cdecl WeaponModDB_load(char const* Path);
void __cdecl WeaponModDB_destroy();
// FUN_06341c70: the weapon's shield map lookup; a miss at either level gives 1.0f
// (.rdata:0639a1d0).
float __cdecl WeaponModDB_GetShieldMod(unsigned WeaponTypeId, unsigned ShieldTypeId);
