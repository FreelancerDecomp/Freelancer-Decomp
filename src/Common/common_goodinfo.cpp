// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll goods list (retail GoodList.cpp): GoodDesc / GoodDescList and the good-item helpers,
// TradeResponseInfo, GoodInfo / GoodInfoList, GoodList_* and Good2Arch / Arch2Good, plus the
// exported OwnerList<CargoDesc>, MetaList<GoodDesc> and OwnerList<GoodDesc> instantiations.
// Split out of common_goods.cpp.
//
// Static-initialisation order: GGoodInfoList is dynamically initialised; this TU follows
// common_equipdesc.cpp, common_constants.cpp and common_loadout.cpp, as in common_goods.cpp.

#include "common_goods.h"
#include "common_goods_internal.h" // kPathChars
#include "common_id.h"
#include "common_ini.h"
#include "common_misc.h"   // GetFileSysPath
#include "common_server.h" // OWNERLIST_CARGODESC_EXPORTS
#include "DALib.h"
#include "dacom.h"

#include <cstdlib>
#include <cstring>
#include <list>
#include <map>
#include <new>
#include <vector>

// common_dacom_stricmp.cpp: DACOM.DLL!stricmp (Common IAT PTR 0639902c).
int CallDacomStricmp(const char* Left, const char* Right);

using CmnGoods::kPathChars;

namespace
{
    const char kGoodListSource[] = "E:\\FL\\Scratch\\Source\\Common\\GoodList.cpp";
}

// ---------------------------------------------------------------------------
// GoodDesc / GoodDescList (Common.dll GOODS batch). GoodDescList is
// OwnerList<GoodDesc> plus retail's own copy members; the list operations are
// the MetaList<T> / OwnerList<T> templates (common_metalist.h).
// ---------------------------------------------------------------------------

// ??2GoodDesc (0x9d560): JMP msvcrt!malloc.
void* GoodDesc::operator new(size_t Size)
{
    return malloc(Size);
}

// ??3GoodDesc (0x9d570): JMP msvcrt!free.
void GoodDesc::operator delete(void* Ptr)
{
    free(Ptr);
}

// .text:0627be30
GoodDescList::GoodDescList()
    : OwnerList<GoodDesc>()
{
}

// .text:0630e350: OwnerList<GoodDesc>() (bUseHeap = 0), then bUseHeap taken from
// Other, then operator='s append loop inlined.
GoodDescList::GoodDescList(GoodDescList const& Other)
    : OwnerList<GoodDesc>()
{
    bUseHeap = Other.bUseHeap;
    *this = Other;
}

// .text:0630e450: walks Other forward and appends a default-constructed GoodDesc
// overwritten with each source element. This list is not cleared first, there is
// no self-assignment check, and bUseHeap is left alone.
GoodDescList const& GoodDescList::operator=(GoodDescList const& Other)
{
    MetaNode<GoodDesc>* Node = nullptr;
    while (Other.traverse(Node, true))
    {
        GoodDesc* Copy = new GoodDesc;
        *Copy = *Node->Data;
        append(Copy);
    }
    return *this;
}

// 0xaa9d0. The walk keeps the LAST element whose GoodId matches. That element is
// combined into (`CMP CL,1` on bCombinable) and 1 returned; otherwise a new
// GoodDesc is appended and 0 returned. No null check on List.
COMMON_API int __cdecl add_good_item(unsigned int GoodId, int Count, float Price, float Field0C,
                                     bool bCombinable, GoodDescList* List)
{
    GoodDesc* Found = nullptr;
    MetaNode<GoodDesc>* Node = nullptr;
    while (List->traverse(Node, true))
    {
        if (Node->Data->GoodId == GoodId)
            Found = Node->Data;
    }

    if (Found != nullptr && bCombinable == true)
    {
        Found->Count += Count;
        return 1;
    }

    GoodDesc* Item = new GoodDesc;
    Item->GoodId = GoodId;
    Item->Count = Count;
    Item->Price = Price;
    Item->Field0C = Field0C;
    List->append(Item);
    return 0;
}

// 0xaaab0. The walk keeps the LAST node whose GoodId matches; none -> -1. Count
// -1 zeroes the element, anything else is subtracted. A non-zero result returns 1;
// at zero the element is deleted (??3GoodDesc), Data nulled, and the node
// removed (MetaList::remove inlined), returning 0. No null check on List.
COMMON_API int __cdecl remove_good_item(unsigned int GoodId, int Count, GoodDescList* List)
{
    GoodDesc* Found = nullptr;
    MetaNode<GoodDesc>* FoundNode = nullptr;
    MetaNode<GoodDesc>* Node = nullptr;
    while (List->traverse(Node, true))
    {
        if (Node->Data->GoodId == GoodId)
        {
            Found = Node->Data;
            FoundNode = Node;
        }
    }
    if (Found == nullptr)
        return -1;

    if (Count == -1)
        Found->Count = 0;
    else
        Found->Count -= Count;

    if (Found->Count != 0)
        return 1;

    delete FoundNode->Data;
    FoundNode->Data = nullptr;
    List->remove(FoundNode);
    return 0;
}

// 0xaa990. Re-derived from raw disasm (Common.dll.txt:303057-303098): walks the
// *entire* list and keeps overwriting its result on every match, so it returns
// the LAST node/GoodDesc with a matching GoodId, not the first. OutNode, when
// non-null, receives that node (nullptr on no match). No null-check on List,
// matching retail.
COMMON_API GoodDesc* __cdecl find_good_item(GoodDescList* List, unsigned int GoodId,
                                            MetaNode<GoodDesc>** OutNode)
{
    MetaNode<GoodDesc>* Found = nullptr;
    GoodDesc* Result = nullptr;
    MetaNode<GoodDesc>* Node = nullptr;
    while (List->traverse(Node, true))
    {
        if (Node->Data->GoodId == GoodId)
        {
            Result = Node->Data;
            Found = Node;
        }
    }
    if (OutNode != nullptr)
        *OutNode = Found;
    return Result;
}

// ---------------------------------------------------------------------------
// TradeResponseInfo — retail Common.dll GOODS batch network message.
// ---------------------------------------------------------------------------

// 0x1c2b0
void TradeResponseInfo::marshal(MarshalBuffer* Buf) const
{
    Buf->put_int(Field00);
    Buf->put_int(Field04);
    Buf->put_bool(bFlag08);
    Buf->put_GoodDescList(OfferedGoods);
    Buf->put_GoodDescList(RequestedGoods);
}

// 0x1c310
void TradeResponseInfo::unmarshal(MarshalBuffer* Buf)
{
    Field00 = Buf->get_int();
    Field04 = Buf->get_int();
    bFlag08 = Buf->get_bool();
    OfferedGoods = Buf->get_GoodDescList();
    RequestedGoods = Buf->get_GoodDescList();
}
// ---------------------------------------------------------------------------
// GoodInfo / GoodList
// ---------------------------------------------------------------------------

static GoodInfoList GGoodInfoList;

GoodInfo::GoodInfo()
    : CommReferrable()
    , Id(CreateID(nullptr))
    , Category(GoodCategoryUnknown)
    , ArchId(0)
    , ShipArchId(0)
    , Price(0.0f)
    , GoodBuyPrice(0.0f)
    , BadBuyPrice(0.0f)
    , GoodSellPrice(0.0f)
    , BadSellPrice(0.0f)
    , JumpDist(0x32)
    , bCombinable(false)
    , IdsName(0)
    , IdsInfo(0)
    , HullGoodId(0)
    , FreeAmmoArchId(0)
    , FreeAmmoCount(0)
{
    // 0626ff79-0626ffa2: CacheString::clear on +0x74..+0x84 -- the string pool's shared empty
    // string, not a literal.
    ShopArchetype.clear();
    ItemIcon.clear();
    MaterialLibrary.clear();
    HpChild.clear();
    AttachmentArchetype.clear();
}

GoodInfo::~GoodInfo()
{
}

GoodInfoList::GoodInfoList()
{
}

GoodInfoList::~GoodInfoList()
{
    destroy();
}

// 0x062704a0: .text:062704b1..062704cd -- every non-null GoodInfo* in the list gets
// ~GoodInfo + free (a null one is skipped, .text:062704b6 TEST EBX,EBX / JZ); then
// .text:062704cf..062704eb erases every list node; then the ById tree (+0x0c,
// .text:062704ed..06270576) and the ByArch tree (+0x20, .text:0627057c..) are cleared.
void GoodInfoList::destroy()
{
    std::list<GoodInfo*>::iterator It = Goods.begin();
    for (; It != Goods.end(); ++It)
    {
        if (*It != nullptr)
            delete *It;
    }
    Goods.clear();
    ById.clear();
    ByArch.clear();
}

GoodInfo const* GoodInfoList::find_by_id(unsigned Id) const
{
    std::map<unsigned, GoodInfo*>::const_iterator Found = ById.find(Id);
    if (Found == ById.end())
        return nullptr;
    return Found->second;
}

// 0x06271290: .text:0627129d CALL CreateID, then the ById (+0x0c) lookup that
// find_by_id does (.text:062712b5 FUN_062fcfb0 lower_bound, key compare at +0xc, value
// at node+0x10, end -> 0).
GoodInfo const* GoodInfoList::find_by_name(char const* Name) const
{
    return find_by_id(CreateID(Name));
}

GoodInfo const* GoodInfoList::find_by_archetype(unsigned ArchId) const
{
    std::map<unsigned, GoodInfo*>::const_iterator Found = ByArch.find(ArchId);
    if (Found == ByArch.end())
        return nullptr;
    return Found->second;
}

GoodInfo const* GoodInfoList::find_by_ship_arch(unsigned ArchId) const
{
    // 0x06271300: .text:06271301 ESI = list head (this+4); .text:06271311..0627131d walk
    // the nodes in insertion order and return the first GoodInfo whose ShipArchId
    // (+0x54) equals ArchId; .text:06271306 XOR EAX,EAX -- nullptr when none matches.
    // Retail dereferences each entry without a null test.
    std::list<GoodInfo*>::const_iterator It = Goods.begin();
    for (; It != Goods.end(); ++It)
    {
        if ((*It)->ShipArchId == ArchId)
            return *It;
    }
    return nullptr;
}

std::list<GoodInfo*> const* GoodInfoList::get_list() const
{
    return &Goods;
}

// 0x06270620 ?read_Good_block@GoodInfoList@@AAEXPAVINI_Reader@@PAUGoodInfo@@@Z. Offsets in the
// comments are GoodInfo's retail ones (+0x48 nickname id ... +0xbc free ammo count).
void GoodInfoList::read_Good_block(INI_Reader* Reader, GoodInfo* Info)
{
    // 063ea6c0: {name, category} x4, walked with DACOM stricmp; a null name is skipped.
    struct FCategoryName
    {
        char const* Name;
        unsigned Category;
    };
    static FCategoryName const kCategories[4] = {
        {"Commodity", GoodCategoryCommodity},
        {"Equipment", GoodCategoryEquipment},
        {"ShipHull", GoodCategoryShipHull},
        {"Ship", GoodCategoryShip},
    };

    unsigned EquipmentArch = 0; // 0627063b

    while (Reader->read_value())
    {
        if (Reader->is_value("nickname"))
        {
            // 06270672-062706af: a nickname already in this list's id map with a non-null
            // GoodInfo ends the whole function at once (06270e40) -- the rest of the block and
            // the post-loop equipment fix-up are skipped.
            unsigned const NicknameId = CreateID(Reader->get_value_string());
            std::map<unsigned, GoodInfo*>::const_iterator Found = ById.find(NicknameId);
            if (Found != ById.end() && Found->second != nullptr)
                return;
            Info->Id = NicknameId;
        }
        else if (Reader->is_value("equipment"))
        {
            EquipmentArch = CreateID(Reader->get_value_string());
        }
        else if (Reader->is_value("free_ammo"))
        {
            Info->FreeAmmoArchId = CreateID(Reader->get_value_string(0)); // +0xb8
            Info->FreeAmmoCount = Reader->get_value_int(1);               // +0xbc
        }
        else if (Reader->is_value("ids_name") || Reader->is_value("name"))
        {
            Info->IdsName = Reader->get_value_int(0); // +0x88
        }
        else if (Reader->is_value("ids_info") || Reader->is_value("info_desc"))
        {
            Info->IdsInfo = Reader->get_value_int(0); // +0x8c
        }
        else if (Reader->is_value("category"))
        {
            // 06270791-062707f7. No match: FDUMP line 0xab and +0x4c keeps its value.
            char const* const Text = Reader->get_value_string();
            bool bFound = false;
            unsigned Index = 0;
            for (Index = 0; Index < 4; ++Index)
            {
                if (kCategories[Index].Name != nullptr &&
                    CallDacomStricmp(Text, kCategories[Index].Name) == 0)
                {
                    Info->Category = kCategories[Index].Category;
                    bFound = true;
                    break;
                }
            }
            if (!bFound)
            {
                FDUMP(0x100001, "%s(%d) : *** ERROR: unknown value: category = %s",
                      kGoodListSource, 0xab, Reader->get_value_string());
            }
        }
        else if (Reader->is_value("ship"))
        {
            Info->ShipArchId = CreateID(Reader->get_value_string()); // +0x54
        }
        else if (Reader->is_value("price"))
        {
            // 06270844-062708c4: FILD the int, FST float +0x58 while the exact integer stays
            // on the x87 stack; each still-zero bound becomes integer * 0.95f / 1.05f
            // (0x3f733333 / 0x3f866666) rounded once to float. The double product below is
            // exact for any |price| < 2^29.
            int const Whole = Reader->get_value_int(0);
            double const Exact = Whole;
            float const kLow = 0.949999988079071f;   // .rdata:06399e78
            float const kHigh = 1.0499999523162842f; // .rdata:06399e74
            Info->Price = (float)Exact;
            if (Info->GoodBuyPrice == 0.0f)
                Info->GoodBuyPrice = (float)(kLow * Exact);
            if (Info->BadBuyPrice == 0.0f)
                Info->BadBuyPrice = (float)(kHigh * Exact);
            if (Info->GoodSellPrice == 0.0f)
                Info->GoodSellPrice = (float)(kHigh * Exact);
            if (Info->BadSellPrice == 0.0f)
                Info->BadSellPrice = (float)(Exact * kLow);
        }
        else if (Reader->is_value("good_buy_price"))
        {
            // 062708e0 (and 06270902 / 06270924 / 06270946 below): FMUL float [+0x58] on the
            // unrounded get_value_float result (value_num = the same read, no float rounding),
            // one FSTP float.
            Info->GoodBuyPrice = (float)(Reader->value_num(0) * Info->Price);
        }
        else if (Reader->is_value("bad_buy_price"))
        {
            Info->BadBuyPrice = (float)(Reader->value_num(0) * Info->Price);
        }
        else if (Reader->is_value("good_sell_price"))
        {
            Info->GoodSellPrice = (float)(Reader->value_num(0) * Info->Price);
        }
        else if (Reader->is_value("bad_sell_price"))
        {
            Info->BadSellPrice = (float)(Reader->value_num(0) * Info->Price);
        }
        else if (Reader->is_value("jump_dist"))
        {
            Info->JumpDist = Reader->get_value_int(0);
        }
        else if (Reader->is_value("shop_archetype"))
        {
            Info->ShopArchetype.Value = StringAlloc(Reader->get_value_string(), true); // +0x74
        }
        else if (Reader->is_value("material_library"))
        {
            Info->MaterialLibrary.Value = StringAlloc(Reader->get_value_string(), true); // +0x7c
        }
        else if (Reader->is_value("item_icon"))
        {
            Info->ItemIcon.Value = StringAlloc(Reader->get_value_string(), true); // +0x78
        }
        else if (Reader->is_value("HP_child"))
        {
            Info->HpChild.Value = StringAlloc(Reader->get_value_string(), false); // +0x80
        }
        else if (Reader->is_value("attachment_archetype"))
        {
            Info->AttachmentArchetype.Value = StringAlloc(Reader->get_value_string(), true); // +0x84
        }
        else if (Reader->is_value("hull"))
        {
            // 06270a8a-06270ac4: the lookup goes to the GLOBAL good list's id map
            // (DAT_063fbb80), not this one, and the found GoodInfo is dereferenced with no
            // null test (a miss reads +0x48 of address 0 -- retail's crash, kept).
            unsigned const HullId = CreateID(Reader->get_value_string());
            std::map<unsigned, GoodInfo*>::const_iterator Found = GGoodInfoList.ById.find(HullId);
            GoodInfo* Hull = nullptr;
            if (Found != GGoodInfoList.ById.end())
                Hull = Found->second;
            Info->HullGoodId = Hull->Id; // +0x90
        }
        else if (Reader->is_value("addon"))
        {
            // 06270ae3-06270d41. value(1) is StringAlloc'd before anything is decided, even
            // when it turns out to be "internal".
            unsigned const AddonArch = CreateID(Reader->get_value_string(0));
            CacheString Hp;
            Hp.Value = StringAlloc(Reader->get_value_string(1), false);
            int Count = Reader->get_value_int(2);
            if (Count <= 0)
                Count = 1;

            // 06270b2c-06270b8c: value(3) copied into a 0x40-byte buffer, at most 0x3f chars.
            char Slot[0x40];
            Slot[0] = '\0';
            if (!Reader->is_value_empty(3))
            {
                char const* const SlotText = Reader->get_value_string(3);
                if (SlotText != nullptr)
                {
                    size_t Length = strlen(SlotText);
                    if (Length >= 0x40)
                        Length = 0x3f;
                    memcpy(Slot, SlotText, Length);
                    Slot[Length] = '\0';
                }
            }

            // A default EquipDesc is appended to the chosen list (+0xa0 mid, +0xac high,
            // +0x94 otherwise) and then filled in place.
            std::vector<EquipDesc>* Target = &Info->AddonLow;
            if (CallDacomStricmp(Slot, "mid") == 0)
                Target = &Info->AddonMid;
            else if (CallDacomStricmp(Slot, "high") == 0)
                Target = &Info->AddonHigh;
            Target->push_back(EquipDesc());
            EquipDesc& Addon = Target->back();
            Addon.set_arch_id(AddonArch);
            Addon.set_count(Count);
            Addon.set_status(1.0f);
            Addon.set_equipped(true);
            if (CallDacomStricmp(Hp.Value, "internal") == 0)
                Addon.make_internal();
            else
                Addon.set_hardpoint(Hp);
        }
        else if (Reader->is_value("combinable"))
        {
            Info->bCombinable = Reader->get_value_bool(0); // +0x70
        }
        else
        {
            // .text:06270d5f: the last fallback is CommReferrable slot 2 (read_comm_ini_data),
            // which takes "msg_id_prefix" and ignores every other key.
            Info->read_comm_ini_data(*Reader);
        }
    }

    // 06270da1-06270e3d. Commodity and Equipment goods take their arch from "equipment" and
    // inherit ids and, for attached equipment (Equipment slot 9), the visual strings from the
    // archetype. GetEquipment's result is used with no null test, as in retail.
    if (Info->Category == GoodCategoryCommodity || Info->Category == GoodCategoryEquipment)
    {
        Info->ArchId = EquipmentArch;
        Archetype::Equipment* const Equip = Archetype::GetEquipment(EquipmentArch);
        if (Info->IdsName == 0)
            Info->IdsName = Equip->IdsName;
        if (Info->IdsInfo == 0)
            Info->IdsInfo = Equip->IdsInfo;
        if (Equip->is_attached())
        {
            if (Info->AttachmentArchetype.Value[0] == '\0' && Equip->DaArchetype.Value[0] != '\0')
                Info->AttachmentArchetype = Equip->DaArchetype;
            Archetype::AttachedEquipment* const Attached = (Archetype::AttachedEquipment*)Equip;
            if (Info->HpChild.Value[0] == '\0' && Attached->HpChild.Value[0] != '\0')
                Info->HpChild = Attached->HpChild;
        }
    }
    else
    {
        Info->ArchId = 0;
    }

    if (Info->ShopArchetype.Value[0] == '\0' && Info->AttachmentArchetype.Value[0] != '\0')
        Info->ShopArchetype = Info->AttachmentArchetype;
}

// 0x06270e50 ?load@GoodInfoList@@QAEXPBD@Z.
void GoodInfoList::load(char const* RelPath)
{
    char FullPath[kPathChars];
    GetFileSysPath(FullPath, RelPath, DALib::Data);

    INI_Reader Reader;
    if (!Reader.open(FullPath, false))
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: Cannot open goods file %s\n", kGoodListSource,
              0x1c2, FullPath);
        return;
    }

    while (Reader.read_header())
    {
        if (Reader.is_header("Good"))
        {
            GoodInfo* Info = new GoodInfo();
            read_Good_block(&Reader, Info);
            // .text:06270f42 FUN_0627f280: the list insert at end(), unconditional.
            Goods.push_back(Info);
            if (Info->Id != 0 && Info->ArchId != 0 && Info->Id != Info->ArchId)
            {
                ID_String Name;
                Name.Id = Info->Id;
                FDUMP(0x100002, "%s(%d) : *** WARNING: Good (%s) nickname different from equipment",
                      kGoodListSource, 0x1b0, Name.get_string());
            }
            // .text:06270f9c/06270fc0 and .text:06270fd4/06270ff4: map insert of
            // (key, 0) then the node's value set to Info -- operator[] on the ById tree
            // (+0x0c, key GoodInfo+0x48) and the ByArch tree (+0x20, key +0x50), with no
            // zero-key test: a later good with the same key overwrites the earlier one.
            ById[Info->Id] = Info;
            ByArch[Info->ArchId] = Info;
        }
        else if (Reader.is_header("GoodType"))
        {
            // 0627100e: the argument is the caller's relative path, not the reader's name.
            FDUMP(0x100002, "%s(%d) : *** WARNING: [GoodType] is obsolete in ini file %s\n",
                  kGoodListSource, 0x1ba, RelPath);
        }
    }
    Reader.close();
}

void __cdecl GoodList_load(char const* RelPath)
{
    GGoodInfoList.load(RelPath);
}

void __cdecl GoodList_destroy()
{
    GGoodInfoList.destroy();
}

GoodInfoList* __cdecl GoodList_get()
{
    return &GGoodInfoList;
}

GoodInfo const* __cdecl GoodList::find_by_id(unsigned Id)
{
    return GGoodInfoList.find_by_id(Id);
}

GoodInfo const* __cdecl GoodList::find_by_nickname(char const* Name)
{
    return GGoodInfoList.find_by_name(Name);
}

GoodInfo const* __cdecl GoodList::find_by_archetype(unsigned ArchId)
{
    return GGoodInfoList.find_by_archetype(ArchId);
}

unsigned int __cdecl Good2Arch(unsigned int GoodId)
{
    GoodInfo const* Info = GGoodInfoList.find_by_id(GoodId);
    if (Info == nullptr)
        return 0;
    return Info->ArchId;
}

unsigned int __cdecl Arch2Good(unsigned int ArchId)
{
    GoodInfo const* Info = GGoodInfoList.find_by_archetype(ArchId);
    if (Info == nullptr)
        return 0;
    return Info->Id;
}

bool __cdecl arch_is_combinable(unsigned int ArchId)
{
    GoodInfo const* Info = GGoodInfoList.find_by_archetype(ArchId);
    if (Info == nullptr)
        return false;
    return Info->bCombinable;
}

// ===========================================================================
// OwnerList<pub::SpaceObj::CargoDesc> -- template defined in src/include/
// common_server.h. This explicit instantiation definition makes Common.dll emit
// and export the six members retail's Common.dll exports: ??0 (ord 4,
// .text:062ed4d0), copy ??0 (ord 3, .text:062ed4f0), ??1 (ord 495, .text:062eed20),
// ??4 (ord 697, .text:062ed500, ICF-shared with OwnerList<GoodDesc>::operator= and
// others), ??_7 (ord 962, .rdata:063a0880) and free (ord 2562, .text:062eecb0).
// Server.dll and freelancer.exe carry the same instantiation.
// ===========================================================================
// Member-level export (common_server.h): ??_7 is the one retail export this cannot emit.
OWNERLIST_CARGODESC_EXPORTS

// MetaList<GoodDesc> and OwnerList<GoodDesc>: Common.dll exports every member of both
// (MetaList .text:0627bde0 ??_F, 0627c5c0-0627ca17, getters 062a66a0 / 062d1ad0,
// operator= 062ed730; OwnerList 0627be00, 0627be20, 0627be50, 062ed500, 0627ca20).
// The importing declarations are in common_goods.h.
template class COMMON_API MetaList<GoodDesc>;
template class COMMON_API OwnerList<GoodDesc>;
