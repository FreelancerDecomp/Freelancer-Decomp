// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll base-side records: MissionVendorProperties / MissionVendorAcceptance,
// NewsBroadcastProperties, CharacterProperties, AmbientScriptInfo / AmbientScriptProperties,
// CostumeHint / BaseHint and CBase. Split out of common_goods.cpp.
//
// The property bags share one shape in retail: the constructor zero-constructs the FmtStr
// members inline and then runs clear() inlined; destroy() releases what the record owns and
// then runs clear() inlined; the destructor is destroy() inlined plus the members' ~FmtStr;
// operator= is destroy() inlined (no self test) followed by a memberwise copy. marshal and
// unmarshal take the MarshalBuffer unchecked.

#include "common_goods.h"
#include "common_goods_internal.h" // AssignCostume
#include "common_cobject.h"        // CObject (CBase)
#include "common_id.h"
#include "common_misc.h"           // malloc_strcpy / malloc_wstrcpy and their frees

#include <cstdint>
#include <cstdlib>
#include <cstring>

using CmnGoods::AssignCostume;

namespace
{
    // The per-member reset retail inlines into every clear(): FmtStr::destroy, then REP STOSD
    // over the six dwords (e.g. .text:0631a98e-0631a99a).
    void DestroyAndZero(FmtStr& Text)
    {
        Text.destroy();
        memset(&Text, 0, sizeof(Text));
    }
}

// ---------------------------------------------------------------------------
// MissionVendorProperties
// ---------------------------------------------------------------------------

// .text:0631a770: the three FmtStr members zero-constructed inline, then clear() inlined.
MissionVendorProperties::MissionVendorProperties()
{
    clear();
}

// .text:06308d10: memberwise -- the five scalars, FmtStr's copy constructor for each text,
// then IconTextureId.
MissionVendorProperties::MissionVendorProperties(MissionVendorProperties const& Other)
    : Key(Other.Key)
    , BaseId(Other.BaseId)
    , TargetId(Other.TargetId)
    , SystemId(Other.SystemId)
    , MissionClass(Other.MissionClass)
    , Title(Other.Title)
    , Description(Other.Description)
    , Briefing(Other.Briefing)
    , IconTextureId(Other.IconTextureId)
{
}

// .text:0631a890: destroy() inlined, then the three ~FmtStr.
MissionVendorProperties::~MissionVendorProperties()
{
    destroy();
}

// .text:0631ac40: destroy() inlined, then a memberwise copy; no self test.
MissionVendorProperties& MissionVendorProperties::operator=(MissionVendorProperties const& Other)
{
    destroy();
    Key = Other.Key;
    BaseId = Other.BaseId;
    TargetId = Other.TargetId;
    SystemId = Other.SystemId;
    MissionClass = Other.MissionClass;
    Title = Other.Title;
    Description = Other.Description;
    Briefing = Other.Briefing;
    IconTextureId = Other.IconTextureId;
    return *this;
}

// .text:0631a970
void MissionVendorProperties::clear()
{
    Key = -1;
    BaseId = 0;
    TargetId = 0;
    SystemId = 0;
    MissionClass = 0;
    DestroyAndZero(Title);
    DestroyAndZero(Description);
    DestroyAndZero(Briefing);
    IconTextureId = 0;
}

// .text:0631a9d0: the three texts released, then clear() inlined (which releases them again,
// a no-op on an empty chain).
void MissionVendorProperties::destroy()
{
    Title.destroy();
    Description.destroy();
    Briefing.destroy();
    clear();
}

unsigned long MissionVendorProperties::get_key() const
{
    return (unsigned long)Key;
}

// .text:0631add0: a jump table on MissionClass - 1 (0..5); anything else is null.
char const* MissionVendorProperties::get_icon_texture() const
{
    switch (MissionClass)
    {
    case 1: return "MIS_destroy";
    case 2: return "MIS_destroyinstallation";
    case 3: return "MIS_assassinate";
    case 4: return "MIS_destroycontraband";
    case 5: return "MIS_bountyoffer";
    case 6: return "MIS_retrievemissingitem";
    default: return nullptr;
    }
}

// .text:0631ad10: destroy() inlined, Key = -1, then the description copied over.
void MissionVendorProperties::init(MissionVendorOfferDescription const& Desc)
{
    destroy();
    Key = -1;
    BaseId = Desc.BaseId;
    TargetId = Desc.TargetId;
    SystemId = Desc.SystemId;
    MissionClass = Desc.MissionClass;
    Title = Desc.Title;
    Description = Desc.Description;
    Briefing = Desc.Briefing;
    IconTextureId = Desc.IconTextureId;
}

// .text:0631aa50
void MissionVendorProperties::marshal(MarshalBuffer* Buf) const
{
    Buf->put_void(&Key, 4);
    Buf->put_void(&BaseId, 4);
    Buf->put_void(&TargetId, 4);
    Buf->put_void(&SystemId, 4);
    Buf->put_void(&MissionClass, 4);
    Buf->put_FmtStr(Title);
    Buf->put_FmtStr(Description);
    Buf->put_FmtStr(Briefing);
    Buf->put_void(&IconTextureId, 4);
}

// .text:0631ab00. Every dword before the texts goes through retail's own `param_1` stack cell
// (initially the MarshalBuffer pointer) and is then stored, so when get_void has too few bytes
// left and writes nothing the field receives the cell's previous content. IconTextureId goes
// through a different, never-initialised cell (local_28, .text:0631ac14); the port starts that
// one at 0 -- the only point where it differs from retail, and only on a truncated buffer.
void MissionVendorProperties::unmarshal(MarshalBuffer* Buf)
{
    unsigned int Scratch = (unsigned int)(std::uintptr_t)Buf;
    Buf->get_void(&Scratch, 4);
    Key = (int)Scratch;
    Buf->get_void(&Scratch, 4);
    BaseId = Scratch;
    Buf->get_void(&Scratch, 4);
    TargetId = Scratch;
    Buf->get_void(&Scratch, 4);
    SystemId = Scratch;
    Buf->get_void(&Scratch, 4);
    MissionClass = Scratch;
    Title = Buf->get_FmtStr();
    Description = Buf->get_FmtStr();
    Briefing = Buf->get_FmtStr();
    unsigned int TrailingScratch = 0;
    Buf->get_void(&TrailingScratch, 4);
    IconTextureId = TrailingScratch;
}

// ---------------------------------------------------------------------------
// MissionVendorAcceptance
// ---------------------------------------------------------------------------

// .text:0631ae20: Reason zero-constructed inline, then clear() inlined.
MissionVendorAcceptance::MissionVendorAcceptance()
{
    clear();
}

// .text:06308da0: memberwise.
MissionVendorAcceptance::MissionVendorAcceptance(MissionVendorAcceptance const& Other)
    : Key(Other.Key)
    , TargetId(Other.TargetId)
    , bAccepted(Other.bAccepted)
    , Reason(Other.Reason)
{
}

// .text:0631aeb0: destroy() inlined, then ~FmtStr.
MissionVendorAcceptance::~MissionVendorAcceptance()
{
    destroy();
}

// .text:0631b080: destroy() inlined, then a memberwise copy; no self test.
MissionVendorAcceptance& MissionVendorAcceptance::operator=(MissionVendorAcceptance const& Other)
{
    destroy();
    Key = Other.Key;
    TargetId = Other.TargetId;
    bAccepted = Other.bAccepted;
    Reason = Other.Reason;
    return *this;
}

// .text:0631af30
void MissionVendorAcceptance::clear()
{
    Key = -1;
    TargetId = 0;
    bAccepted = false;
    DestroyAndZero(Reason);
}

// .text:0631af60: Reason released, then clear() inlined.
void MissionVendorAcceptance::destroy()
{
    Reason.destroy();
    clear();
}

// .text:0631af90
void MissionVendorAcceptance::marshal(MarshalBuffer* Buf) const
{
    Buf->put_void(&Key, 4);
    Buf->put_void(&TargetId, 4);
    Buf->put_bool(bAccepted);
    Buf->put_FmtStr(Reason);
}

// .text:0631afe0. Both dwords go through retail's `param_1` stack cell, initially the
// MarshalBuffer pointer (see MissionVendorProperties::unmarshal).
void MissionVendorAcceptance::unmarshal(MarshalBuffer* Buf)
{
    unsigned int Scratch = (unsigned int)(std::uintptr_t)Buf;
    Buf->get_void(&Scratch, 4);
    Key = (int)Scratch;
    Buf->get_void(&Scratch, 4);
    TargetId = Scratch;
    bAccepted = Buf->get_bool();
    Reason = Buf->get_FmtStr();
}

// 0x06261d90: MOV EAX,dword ptr [ECX] / RET.
unsigned long MissionVendorAcceptance::get_key() const
{
    return (unsigned long)Key;
}

// ---------------------------------------------------------------------------
// NewsBroadcastProperties
// ---------------------------------------------------------------------------

// .text:0631b0e0: the three FmtStr members zero-constructed inline, then clear() inlined.
NewsBroadcastProperties::NewsBroadcastProperties()
{
    clear();
}

// .text:06308dd0: the compiler's memberwise copy -- LogoTexture is copied as a POINTER, not
// duplicated, although destroy() frees it. Очень странно: destroying both the copy and the
// original frees the string twice, as in retail. No retail module calls this constructor.
NewsBroadcastProperties::NewsBroadcastProperties(NewsBroadcastProperties const& Other)
    : Key(Other.Key)
    , BaseId(Other.BaseId)
    , bFlag1(Other.bFlag1)
    , bFlag2(Other.bFlag2)
    , Category(Other.Category)
    , Headline(Other.Headline)
    , Body(Other.Body)
    , Logo(Other.Logo)
    , LogoTexture(Other.LogoTexture)
    , LogoTextureId(Other.LogoTextureId)
{
}

// .text:0631b210: destroy() inlined, then the three ~FmtStr.
NewsBroadcastProperties::~NewsBroadcastProperties()
{
    destroy();
}

// .text:0631b5c0: destroy() inlined, then a memberwise copy with LogoTexture duplicated
// (malloc_strcpy); no self test.
NewsBroadcastProperties& NewsBroadcastProperties::operator=(NewsBroadcastProperties const& Other)
{
    destroy();
    Key = Other.Key;
    BaseId = Other.BaseId;
    bFlag1 = Other.bFlag1;
    bFlag2 = Other.bFlag2;
    Category = Other.Category;
    Headline = Other.Headline;
    Body = Other.Body;
    Logo = Other.Logo;
    malloc_strcpy(&LogoTexture, Other.LogoTexture);
    LogoTextureId = Other.LogoTextureId;
    return *this;
}

// .text:0631b300. The texts are released and zeroed; LogoTexture is only set to null (not
// freed), so clear() on a record that still owns one leaks it, as in retail.
void NewsBroadcastProperties::clear()
{
    Key = -1;
    BaseId = 0;
    bFlag1 = false;
    bFlag2 = false;
    Category = 0;
    DestroyAndZero(Headline);
    DestroyAndZero(Body);
    DestroyAndZero(Logo);
    LogoTexture = nullptr;
    LogoTextureId = 0;
}

// .text:0631b360: the three texts and LogoTexture released, then clear() inlined.
void NewsBroadcastProperties::destroy()
{
    Headline.destroy();
    Body.destroy();
    Logo.destroy();
    malloc_strfree(&LogoTexture);
    clear();
}

unsigned long NewsBroadcastProperties::get_key() const
{
    return (unsigned long)Key;
}

// .text:0631b790
char const* NewsBroadcastProperties::get_logo_texture() const
{
    return (LogoTexture != nullptr) ? LogoTexture : "genericnews";
}

// .text:0631b6a0: destroy() inlined, Key = -1, then the description copied over.
// LogoTextureId is CreateID(AudioName) with no test of AudioName (CreateID maps null to 0).
void NewsBroadcastProperties::init(NewsBroadcastDescription const& Desc)
{
    destroy();
    Key = -1;
    BaseId = Desc.BaseId;
    bFlag1 = Desc.bFlag1;
    bFlag2 = Desc.bFlag2;
    Category = Desc.Category;
    Headline = Desc.Headline;
    Body = Desc.Text;
    Logo = Desc.Audio;
    malloc_strcpy(&LogoTexture, Desc.LogoTexture);
    LogoTextureId = CreateID(Desc.AudioName);
}

// .text:0631b3f0
void NewsBroadcastProperties::marshal(MarshalBuffer* Buf) const
{
    Buf->put_void(&Key, 4);
    Buf->put_void(&BaseId, 4);
    Buf->put_bool(bFlag1);
    Buf->put_bool(bFlag2);
    Buf->put_void(&Category, 4);
    Buf->put_FmtStr(Headline);
    Buf->put_FmtStr(Body);
    Buf->put_FmtStr(Logo);
    Buf->put_string(LogoTexture);
    Buf->put_FL_ID(LogoTextureId);
}

// .text:0631b490. The dwords go through retail's `param_1` stack cell, initially the
// MarshalBuffer pointer (see MissionVendorProperties::unmarshal). LogoTexture is overwritten
// without freeing a previous string.
void NewsBroadcastProperties::unmarshal(MarshalBuffer* Buf)
{
    unsigned int Scratch = (unsigned int)(std::uintptr_t)Buf;
    Buf->get_void(&Scratch, 4);
    Key = (int)Scratch;
    Buf->get_void(&Scratch, 4);
    BaseId = Scratch;
    bFlag1 = Buf->get_bool();
    bFlag2 = Buf->get_bool();
    Buf->get_void(&Scratch, 4);
    Category = Scratch;
    Headline = Buf->get_FmtStr();
    Body = Buf->get_FmtStr();
    Logo = Buf->get_FmtStr();
    LogoTexture = Buf->get_string();
    LogoTextureId = Buf->get_FL_ID();
}

// ---------------------------------------------------------------------------
// CharacterProperties
// ---------------------------------------------------------------------------

// .text:0632d890. The inline Costume constructor zeroes the four part ids and the accessory
// count; retail leaves Cost.AccessoryIds uninitialised, the port zeroes them too.
CharacterProperties::CharacterProperties()
    : Key(0)
    , SessionId(0)
    , RoomId(0)
    , NameIds(0)
    , FactionId(0xffffffffu)
    , NameWide(nullptr)
    , NameAnsi(nullptr)
    , CharType(0)
    , Posture(0)
    , Script(nullptr)
    , BehaviorId(0xffffffffu)
    , VoiceId(0)
{
    memset(&Cost, 0, sizeof(Cost));
}

// .text:0632d8d0 (one body with destroy()).
CharacterProperties::~CharacterProperties()
{
    destroy();
}

// .text:0632db50: no self test and no destroy(): the strings are duplicated over the old
// pointers without freeing them (malloc_strcpy never frees), and the Costume goes through its
// inline operator= (the count-bounded copy).
CharacterProperties& CharacterProperties::operator=(CharacterProperties const& Other)
{
    Key = Other.Key;
    SessionId = Other.SessionId;
    RoomId = Other.RoomId;
    NameIds = Other.NameIds;
    FactionId = Other.FactionId;
    malloc_wstrcpy(&NameWide, Other.NameWide);
    malloc_strcpy(&NameAnsi, Other.NameAnsi);
    CharType = Other.CharType;
    Posture = Other.Posture;
    AssignCostume(Cost, Other.Cost);
    malloc_strcpy(&Script, Other.Script);
    BehaviorId = Other.BehaviorId;
    VoiceId = Other.VoiceId;
    return *this;
}

// 0632d8d0 (ICF-shared with ~CharacterProperties): malloc_strfree on +0x14 (the wide name;
// malloc_wstrfree is the same VA), +0x18 and +0x58, then those three are stored 0. No other
// field is touched.
void CharacterProperties::destroy()
{
    malloc_wstrfree(&NameWide);
    malloc_strfree(&NameAnsi);
    malloc_strfree(&Script);
    NameWide = nullptr;
    Script = nullptr;
    NameAnsi = nullptr;
}

unsigned long CharacterProperties::get_key() const
{
    return Key;
}

// .text:0632dc10. No destroy(): NameWide is set to null and the two strings are duplicated
// over the old pointers, so init() on a populated record leaks them, as in retail.
void CharacterProperties::init(CharacterDescription const& Desc)
{
    Key = 0;
    SessionId = 0;
    RoomId = Desc.RoomId;
    NameIds = Desc.NameIds;
    FactionId = Desc.FactionId;
    NameWide = nullptr;
    malloc_strcpy(&NameAnsi, Desc.Location);
    CharType = 0;
    Posture = 0;
    AssignCostume(Cost, Desc.Cost);
    malloc_strcpy(&Script, Desc.Script);
    BehaviorId = Desc.BehaviorId;
    VoiceId = Desc.VoiceId;
}

// .text:0632d910
void CharacterProperties::marshal(MarshalBuffer* Buf) const
{
    Buf->put_void(&Key, 4);
    Buf->put_void(&SessionId, 4);
    Buf->put_void(&RoomId, 4);
    Buf->put_void(&NameIds, 4);
    Buf->put_void(&FactionId, 4);
    Buf->put_wstring(NameWide);
    Buf->put_Costume(Cost);
    Buf->put_string(Script);
    Buf->put_void(&BehaviorId, 4);
    Buf->put_string(NameAnsi);
    Buf->put_void(&CharType, 4);
    Buf->put_void(&Posture, 4);
    Buf->put_void(&VoiceId, 4);
}

// .text:0632da10. Every dword goes through retail's `param_1` stack cell, initially the
// MarshalBuffer pointer (see MissionVendorProperties::unmarshal). The strings are overwritten
// without freeing previous ones; the Costume goes through its inline operator= from the
// get_Costume temporary.
void CharacterProperties::unmarshal(MarshalBuffer* Buf)
{
    unsigned int Scratch = (unsigned int)(std::uintptr_t)Buf;
    Buf->get_void(&Scratch, 4);
    Key = Scratch;
    Buf->get_void(&Scratch, 4);
    SessionId = Scratch;
    Buf->get_void(&Scratch, 4);
    RoomId = Scratch;
    Buf->get_void(&Scratch, 4);
    NameIds = Scratch;
    Buf->get_void(&Scratch, 4);
    FactionId = Scratch;
    NameWide = Buf->get_wstring();
    AssignCostume(Cost, Buf->get_Costume());
    Script = Buf->get_string();
    Buf->get_void(&Scratch, 4);
    BehaviorId = Scratch;
    NameAnsi = Buf->get_string();
    Buf->get_void(&Scratch, 4);
    CharType = Scratch;
    Buf->get_void(&Scratch, 4);
    Posture = Scratch;
    Buf->get_void(&Scratch, 4);
    VoiceId = Scratch;
}

// ---------------------------------------------------------------------------
// AmbientScriptInfo
// ---------------------------------------------------------------------------

// 0x06274130: MOV EAX,this / XOR ECX,ECX / MOV [EAX],ECX / MOV [EAX+4],ECX /
// MOV [EAX+8],ECX / RET.
AmbientScriptInfo::AmbientScriptInfo()
    : Type(0)
    , Path(nullptr)
    , Priority(0)
{
}

// 0x06274140 (??1 and ?destroy share the body): ADD ECX,4 / PUSH ECX /
// CALL malloc_strfree / POP ECX / RET -- nothing else is touched.
AmbientScriptInfo::~AmbientScriptInfo()
{
    malloc_strfree(&Path);
}

// 0x062ee230: XOR EAX,EAX / MOV [this],EAX / MOV [this+4],EAX / MOV [this+8],EAX /
// RET -- the string pointer is dropped, not freed.
void AmbientScriptInfo::clear()
{
    Type = 0;
    Path = nullptr;
    Priority = 0;
}

// 0x06274140, the dtor's body.
void AmbientScriptInfo::destroy()
{
    malloc_strfree(&Path);
}

// ---------------------------------------------------------------------------
// AmbientScriptProperties
// ---------------------------------------------------------------------------

// .text:0631b7a0: the clear() stores.
AmbientScriptProperties::AmbientScriptProperties()
    : Key(-1)
    , RoomId(0)
    , ScriptId(0)
    , bFlag(true)
    , Name(nullptr)
{
}

// .text:0631b7c0 (one body with destroy()).
AmbientScriptProperties::~AmbientScriptProperties()
{
    destroy();
}

// .text:0631b7f0. Name is set to null, not freed.
void AmbientScriptProperties::clear()
{
    Key = -1;
    RoomId = 0;
    ScriptId = 0;
    bFlag = true;
    Name = nullptr;
}

// .text:0631b7c0: Name released, then the clear() stores.
void AmbientScriptProperties::destroy()
{
    malloc_strfree(&Name);
    clear();
}

// 0x06261d90 (ICF-folded with BaseData::get_base_id and other first-dword getters).
unsigned long AmbientScriptProperties::get_key() const
{
    return (unsigned long)Key;
}

// .text:0631b8e0: destroy() inlined, then a memberwise copy with Name duplicated. With no self
// test, self-assignment frees Name before reading Other.Name, which is then null.
AmbientScriptProperties& AmbientScriptProperties::operator=(AmbientScriptProperties const& Other)
{
    destroy();
    Key = Other.Key;
    RoomId = Other.RoomId;
    ScriptId = Other.ScriptId;
    bFlag = Other.bFlag;
    malloc_strcpy(&Name, Other.Name);
    return *this;
}

// .text:0631b930. Key becomes 0 here (clear() makes it -1); Server's
// FRoomEntry::create_ambient_script (FUN_06d12520) stores the real handle afterwards. The
// flag is the low byte of Desc.Flags (`MOV CL,[EAX+8]`); the port's bool normalises a byte
// other than 0/1 to 1 -- every producer (Content's ambient-scene actions) passes 0.
void AmbientScriptProperties::init(AmbientScriptDescription const& Desc)
{
    malloc_strfree(&Name);
    RoomId = 0;
    ScriptId = 0;
    Key = 0;
    bFlag = true;
    RoomId = Desc.RoomId;
    ScriptId = Desc.ScriptId;
    bFlag = (unsigned char)Desc.Flags != 0;
    malloc_strcpy(&Name, Desc.Name);
}

// .text:0631b810
void AmbientScriptProperties::marshal(MarshalBuffer* Buf) const
{
    Buf->put_void(&Key, 4);
    Buf->put_void(&RoomId, 4);
    Buf->put_void(&ScriptId, 4);
    Buf->put_bool(bFlag);
    Buf->put_string(Name);
}

// .text:0631b880. The dwords go through retail's `param_1` stack cell, initially the
// MarshalBuffer pointer (see MissionVendorProperties::unmarshal). Name is overwritten without
// freeing a previous string.
void AmbientScriptProperties::unmarshal(MarshalBuffer* Buf)
{
    unsigned int Scratch = (unsigned int)(std::uintptr_t)Buf;
    Buf->get_void(&Scratch, 4);
    Key = (int)Scratch;
    Buf->get_void(&Scratch, 4);
    RoomId = Scratch;
    Buf->get_void(&Scratch, 4);
    ScriptId = Scratch;
    bFlag = Buf->get_bool();
    Name = Buf->get_string();
}

// ---------------------------------------------------------------------------
// CostumeHint / BaseHint
// ---------------------------------------------------------------------------

// .text:06311370: Kind = 1, and the inline Costume constructor zeroes the four part ids and
// the accessory count. Retail leaves Cost.AccessoryIds uninitialised; the port zeroes them.
CostumeHint::CostumeHint()
{
    memset(&Cost, 0, sizeof(Cost));
    Kind = 1;
}

// .text:06311280: BaseHint's copy (Kind), then all thirteen Costume dwords (REP MOVSD 0xd).
CostumeHint::CostumeHint(CostumeHint const& Other)
    : BaseHint(Other)
    , Cost(Other.Cost)
{
}

CostumeHint::~CostumeHint()
{
}

// .text:063112b0: Kind, then the Costume's inline operator= -- the four part ids and the
// count always, and only `count` accessory ids. No self test, no bound on the count.
CostumeHint& CostumeHint::operator=(CostumeHint const& Other)
{
    Kind = Other.Kind;
    AssignCostume(Cost, Other.Cost);
    return *this;
}

// 063113a0-063113c7: operator new(0x34), the 13 dwords at +0x08 copied (MOVSD x0xd), the
// block stored through OutBuf, 0x34 returned. Neither pointer is tested. CleanUp frees it.
unsigned CostumeHint::Serialize(unsigned char** OutBuf)
{
    unsigned char* Buf = new unsigned char[sizeof(Costume)];
    memcpy(Buf, &Cost, sizeof(Costume));
    *OutBuf = Buf;
    return sizeof(Costume);
}

// 063113d0-063113ef: Size >> 2 dwords then Size & 3 bytes from Buf into +0x08 (MOVSD /
// MOVSB); no test of Buf or of Size. Size above 0x34 runs past the Costume into the next
// object, as in retail.
void CostumeHint::UnSerialize(unsigned char* Buf, unsigned Size)
{
    memcpy(&Cost, Buf, Size);
}

// 06311400-0631140f: operator delete of a non-null block (Serialize's operator new).
void CostumeHint::CleanUp(unsigned char* Buf)
{
    if (Buf != nullptr)
        delete[] Buf;
}

// .text:063111e0: only the vptr. Retail leaves Kind uninitialised; the port zeroes it.
BaseHint::BaseHint()
    : Kind(0)
{
}

// .text:063111f0: Kind copied, then the vptr.
BaseHint::BaseHint(BaseHint const& Other)
    : Kind(Other.Kind)
{
}

BaseHint::~BaseHint()
{
}

// .text:06325980 (ICF-shared with CBase / ImageNode / ... operator=): the dword at +0x04
// copied, no self test.
BaseHint& BaseHint::operator=(BaseHint const& Other)
{
    Kind = Other.Kind;
    return *this;
}

// ---------------------------------------------------------------------------
// CBase
// ---------------------------------------------------------------------------

// .text:062a8250: Object = 0, then the vptr.
CBase::CBase()
    : Object(nullptr)
{
}

// .text:062a8260: Object copied, then the vptr.
CBase::CBase(CBase const& Other)
    : Object(Other.Object)
{
}

CBase::~CBase()
{
}

// .text:06325980 (ICF-shared, see BaseHint::operator=).
CBase& CBase::operator=(CBase const& Other)
{
    Object = Other.Object;
    return *this;
}

void CBase::advise(bool bFlag)
{
    // ICF-folded with CostumeDescriptions::warn — no-op in retail
    (void)bFlag;
}

void CBase::cobject(CObject* Obj)
{
    Object = Obj;
}

CObject* CBase::cobject() const
{
    return Object;
}

long CBase::get_index() const
{
    // .text:062a82e0-062a82e6 `MOV EAX,[ECX+4]; MOV EAX,[EAX+4]; RET`: Object's
    // EngineInstanceId (EngineObject+0x04), not a field of CBase itself; no null test.
    return Object->EngineInstanceId;
}

void CBase::notify_of_destruction(void* Ptr)
{
    // ICF-folded with CostumeDescriptions::warn — no-op in retail
    (void)Ptr;
}
