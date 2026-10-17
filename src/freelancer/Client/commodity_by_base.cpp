// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "commodity_by_base.h"

#include "character_select_equip.h" // GetPlayerVisitFlagsRetail (FUN_004c4da0)
#include "globals.h"                // IsClientServerTickPaused (FUN_0042d730)
#include "hud_command_dispatch.h"   // HudPostCommand (FUN_005763b0)
#include "hud_target.h"             // HudIsMousePickBlocked (FUN_0045a740)
#include "ids_rdl.h"                // AppendTextMaybeXmlRdl (FUN_0057e2c0)
#include "ids_strings.h"            // GetIdsStringWWithRawFallback (FUN_004347e0)
#include "input_manager.h"          // GetClientSoftwareCursor, GetClientMouseCursorDrawFlag
#include "misc_util.h"              // FormatGroupedNumber (FUN_004779a0)
#include "nav_bar.h"                // NavBarHasServiceWindow (FUN_00442720), NavBarIsAnimating (FUN_00442790)
#include "nn_inventory.h"           // IsPlayerDockedRetail (FUN_004c4810)
#include "nn_modal.h"               // NnFloatBits, NnMainButtonHasActivePanel, G_bNnScreenUp
#include "nn_navmap.h"              // NavMapRequestFocus (FUN_004a2aa0)
#include "nn_text_scratch.h"        // DAT_0066dc60 / DAT_0066fc60 / DAT_006119f8
#include "nn_ui_pulse.h"            // NnUiPulseBlendArgb (FUN_00561da0), palette colours
#include "player_behavior.h"        // GetPlayerObjRW (FUN_0054baf0)
#include "space_camera.h"           // FSpaceCamera
#include "space_letterbox.h"        // IsLetterboxVisible (FUN_0041a3e0)
#include "ui_scroll.h"
#include "win3d.h"                  // Win3DFindByName, Win3DDeleteTree, Win3DGetCamera
#include "win_factory.h"

#include "common_archetype.h"
#include "common_csys.h"            // Acquire3DMathEngine (FUN_005b73e0)
#include "common_goods.h"
#include "common_universe.h"
#include "x86math.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>

std::vector<FCommodityByBaseRow> G_CommodityByBaseRows;
std::map<unsigned, std::wstring> G_CommodityByBaseBaseNames;
std::map<unsigned, std::wstring> G_CommodityByBaseSystemNames;

// Retail DAT_0066dacc -- WINFACTORY<NN_CommodityByBase> static (LAB_0046ed70 -> FUN_00470720;
// Create FUN_00470790; atexit LAB_0046ed90 -> FUN_00579380).
WINFACTORY<NnCommodityByBase> G_NnCommodityByBaseFactory("NN_CommodityByBase");

namespace
{
    // DAT_0066db04 -- this TU's 3DMathEngine (FUN_005b73e0 on first use, LAB_0046fd20).
    I3DMathEngine* G_CommodityByBaseMath = nullptr;

    char const kCommodityByBaseMesh[] = "Interface\\NeuroNet\\Inventory\\CHAR_infowindow.cmp"; // 005ce994
    char const kCommodityByBaseCloseMesh[] = "Interface\\NeuroNet\\Inventory\\x.3db";           // 005d01d0

    // FUN_0046fc90 -- true when the window may not stay open: docked with a base-service window
    // or a moving NavBar, in space without a player ship, a "NeuroNetMissionMessage",
    // "NN_Trade" or "NN_Scanner" window, an NN_MainButton panel, a NeuroNet screen, a held
    // server tick, the letterbox, or the software cursor hidden.
    bool CommodityByBaseMustClose()
    {
        if (IsPlayerDockedRetail()) // FUN_004c4810
        {
            if (NavBarHasServiceWindow()) // FUN_00442720
                return true;
            if (NavBarIsAnimating()) // FUN_00442790
                return true;
        }
        else if (GetPlayerObjRW() == nullptr) // FUN_0054baf0
        {
            return true;
        }
        if (Win3DFindByName("NeuroNetMissionMessage") != nullptr)
            return true;
        if (Win3DFindByName("NN_Trade") != nullptr)
            return true;
        if (Win3DFindByName("NN_Scanner") != nullptr)
            return true;
        if (NnMainButtonHasActivePanel()) // FUN_0045a470
            return true;
        if (G_bNnScreenUp) // DAT_0067dcc8
            return true;
        if (IsClientServerTickPaused()) // FUN_0042d730
            return true;
        if (IsLetterboxVisible()) // FUN_0041a3e0
            return true;
        if (GetClientMouseCursorDrawFlag() == 0) // FUN_0041dd90
            return true;
        return false;
    }

    // DAT_005d0214 L"???" -- what every name lookup below falls back to on a miss.
    wchar_t const kCommodityUnknownName[] = L"???";

    // The inlined `find(key)` + `== end ? L"???" : node+0x10` of FUN_00470040 / FUN_004704d0.
    wchar_t const* CommodityByBaseName(std::map<unsigned, std::wstring> const& Names,
                                       unsigned SpaceObjectId)
    {
        std::map<unsigned, std::wstring>::const_iterator Found = Names.find(SpaceObjectId);
        if (Found == Names.end())
            return kCommodityUnknownName;
        return Found->second.c_str();
    }

    void SwapCommodityByBaseRows(FCommodityByBaseRow& A, FCommodityByBaseRow& B)
    {
        FCommodityByBaseRow const Temp = A;
        A = B;
        B = Temp;
    }
}

bool IsCommodityByBaseListedName(int Ids)
{
    // FUN_004a2310: 0x004a2314..0x004a235c, one compare each, in retail's order.
    if (Ids == 0x3fc27 || Ids == 0x3fc2e || Ids == 0x3fc28 || Ids == 0xeb41 || Ids == 0 ||
        Ids == 0xeb3f || Ids == 0x300ea || Ids == 0x30052 || Ids == 0x3009c ||
        Ids == 0x3009d || Ids == 0x300bf)
        return false;
    return true;
}

void BuildCommodityByBaseRows(unsigned ArchId)
{
    // FUN_0046f6d0. 0046f6f7..0046f72e: the rows (FUN_004708d0 erase(first, last)), the base
    // names (FUN_00470bf0 clear) and the system names (FUN_00470b30 erase(begin, end)).
    G_CommodityByBaseRows.clear();
    G_CommodityByBaseBaseNames.clear();
    G_CommodityByBaseSystemNames.clear();

    // 0046f733..0046fa35: every base of the universe.
    for (Universe::IBase* Base = Universe::GetFirstBase(); Base != nullptr;
         Base = Universe::GetNextBase())
    {
        // 0046f747..0046f771: the base's space object (vt+0x5c) and its system (vt+0x10 ->
        // Universe::get_system); either missing skips the base.
        unsigned const SpaceObjectId = Base->SpaceObjectId;
        Universe::ISystem const* System = Universe::get_system(Base->SystemId);
        if (System == nullptr || SpaceObjectId == 0)
            continue;

        // 0046f777..0046f789: the base's name ids (vt+8) must not be one of the unlisted ids.
        if (!IsCommodityByBaseListedName(Base->StridName))
            continue;

        // 0046f78f..0046f7bb: the player's visit flags for the space object (FUN_004c4da0,
        // nothing written on a miss), OR the object's own visit byte (vt+0x60); bit 4 (known)
        // is required.
        unsigned char VisitFlags = 0;
        GetPlayerVisitFlagsRetail(SpaceObjectId, &VisitFlags, 0, 0); // 0x0046f79f
        VisitFlags = static_cast<unsigned char>(VisitFlags | Base->SpaceObjectVisit);
        if ((VisitFlags & 4) == 0)
            continue;

        // 0046f7c1..0046f894: the system's name (ids = ISystem slot 3, FUN_004347e0 into
        // DAT_0066fc60 with DAT_006119f8) under the space object, when not there yet.
        if (G_CommodityByBaseSystemNames.find(SpaceObjectId) == G_CommodityByBaseSystemNames.end())
        {
            GetIdsStringWWithRawFallback((unsigned)System->GetStridName(),
                                         G_NnTextScratchB, G_NnTextScratchCapacity);
            G_CommodityByBaseSystemNames.insert(
                std::pair<unsigned const, std::wstring>(SpaceObjectId, G_NnTextScratchB));
        }

        // 0046f89c..0046f96f: the base's name (ids = vt+8) the same way.
        if (G_CommodityByBaseBaseNames.find(SpaceObjectId) == G_CommodityByBaseBaseNames.end())
        {
            GetIdsStringWWithRawFallback(static_cast<unsigned>(Base->StridName),
                                         G_NnTextScratchB, G_NnTextScratchCapacity);
            G_CommodityByBaseBaseNames.insert(
                std::pair<unsigned const, std::wstring>(SpaceObjectId, G_NnTextScratchB));
        }

        // 0046f973..0046f995: the base's market (BaseData +0x10) by the base id (vt[0]); no
        // base data skips the base.
        BaseData* Data = BaseDataList_get()->get_base_data(Base->NicknameId);
        if (Data == nullptr)
            continue;
        std::map<unsigned, MarketGoodInfo> const* Market = &Data->Market;

        // 0046f99b..0046f9fb: the good's market entry gives the price (+0x04, ftol) and
        // "sells" = its +0x0c is non-zero. Without an entry the good list's price (+0x58,
        // ftol) is used and "sells" is false -- unless the archetype has no good at all, which
        // leaves price 0 and "sells" true.
        unsigned const GoodId = Arch2Good(ArchId);
        int Price = 0;
        bool bBaseSells = true;
        std::map<unsigned, MarketGoodInfo>::const_iterator Entry = Market->find(GoodId);
        if (Entry != Market->end())
        {
            Price = static_cast<int>(Entry->second.Price);
            if (Entry->second.MaxInventory == 0)
                bBaseSells = false;
        }
        else
        {
            GoodInfo const* Good = GoodList::find_by_archetype(ArchId);
            if (Good != nullptr)
            {
                Price = static_cast<int>(Good->Price);
                bBaseSells = false;
            }
        }

        // 0046f9ff..0046fa26: push_back {space object, price, sells} (FUN_00470c90 at end).
        FCommodityByBaseRow Row;
        Row.SpaceObjectId = SpaceObjectId;
        Row.Price = Price;
        Row.bBaseSells = bBaseSells;
        G_CommodityByBaseRows.push_back(Row);
    }
}

void SortCommodityByBaseRows(int Column)
{
    // FUN_00470040. 0047004b..0047006b: the row count; every mode runs passes over the first
    // Count - 1 neighbours until a pass swaps nothing, and does nothing below two rows.
    int const Count = static_cast<int>(G_CommodityByBaseRows.size());
    int const Last = Count - 1;
    if (Column != 1 && Column != 2 && Column != 3 && Column != 4)
        return;
    if (Last <= 0)
        return;

    bool bSorted = false;
    while (!bSorted)
    {
        bSorted = true;
        for (int Index = 0; Index < Last; ++Index)
        {
            FCommodityByBaseRow& A = G_CommodityByBaseRows[Index];
            FCommodityByBaseRow& B = G_CommodityByBaseRows[Index + 1];
            bool bSwap = false;

            if (Column == 2)
            {
                // 004700b0..00470150: base name, ascending (_wcsicmp(A, B) > 0 swaps).
                wchar_t const* NameB = CommodityByBaseName(G_CommodityByBaseBaseNames, B.SpaceObjectId);
                wchar_t const* NameA = CommodityByBaseName(G_CommodityByBaseBaseNames, A.SpaceObjectId);
                if (_wcsicmp(NameA, NameB) > 0)
                    bSwap = true;
            }
            else if (Column == 1)
            {
                // 004701b0..00470260: price, highest first; equal prices by base name.
                if (A.Price > B.Price)
                {
                    bSwap = false;
                }
                else if (A.Price < B.Price)
                {
                    bSwap = true;
                }
                else
                {
                    wchar_t const* NameB = CommodityByBaseName(G_CommodityByBaseBaseNames, B.SpaceObjectId);
                    wchar_t const* NameA = CommodityByBaseName(G_CommodityByBaseBaseNames, A.SpaceObjectId);
                    if (_wcsicmp(NameA, NameB) > 0)
                        bSwap = true;
                }
            }
            else if (Column == 3)
            {
                // 004702d0..004703f3: system name ascending; equal systems by price (highest
                // first), then by base name.
                wchar_t const* SystemB = CommodityByBaseName(G_CommodityByBaseSystemNames, B.SpaceObjectId);
                wchar_t const* SystemA = CommodityByBaseName(G_CommodityByBaseSystemNames, A.SpaceObjectId);
                int const SystemOrder = _wcsicmp(SystemA, SystemB);
                if (SystemOrder != 0)
                {
                    if (SystemOrder > 0)
                        bSwap = true;
                }
                else if (A.Price > B.Price)
                {
                    bSwap = false;
                }
                else if (A.Price < B.Price)
                {
                    bSwap = true;
                }
                else
                {
                    wchar_t const* NameB = CommodityByBaseName(G_CommodityByBaseBaseNames, B.SpaceObjectId);
                    wchar_t const* NameA = CommodityByBaseName(G_CommodityByBaseBaseNames, A.SpaceObjectId);
                    if (_wcsicmp(NameA, NameB) > 0)
                        bSwap = true;
                }
            }
            else
            {
                // 00470450..004704a8: selling bases first; within the same flag, price highest
                // first (A.Price >= B.Price keeps the order).
                if (A.bBaseSells && !B.bBaseSells)
                    bSwap = false;
                else if (!A.bBaseSells && B.bBaseSells)
                    bSwap = true;
                else if (A.Price < B.Price)
                    bSwap = true;
            }

            if (bSwap)
            {
                SwapCommodityByBaseRows(A, B);
                bSorted = false;
            }
        }
    }
}

void AppendCommodityTradeReport(RenderDisplayList* Card, unsigned ArchId)
{
    // FUN_004704d0. 004704d9..004704e1: rebuild the rows, sort by price.
    BuildCommodityByBaseRows(ArchId);
    SortCommodityByBaseRows(1);

    // 004704e6..0047069f: one line per row, built in DAT_0066fc60 with DAT_0066dc60 as the
    // scratch for the number and the ids strings.
    int const Count = static_cast<int>(G_CommodityByBaseRows.size());
    for (int Index = 0; Index < Count; ++Index)
    {
        FCommodityByBaseRow const& Row = G_CommodityByBaseRows[Index];

        // 00470537..0047056f: "\n" (DAT_005d0254), the price with the ids 0x2140 prefix
        // (FUN_004779a0(.., -1)), " " (DAT_005cef90).
        wcscpy(G_NnTextScratchB, L"\n");
        FormatGroupedNumber(G_NnTextScratchA, Row.Price, -1);
        wcscat(G_NnTextScratchB, G_NnTextScratchA);
        wcscat(G_NnTextScratchB, L" ");

        // 00470571..004705b6: the base name or "???", then ", " (DAT_005d024c).
        wcscat(G_NnTextScratchB,
               CommodityByBaseName(G_CommodityByBaseBaseNames, Row.SpaceObjectId));
        wcscat(G_NnTextScratchB, L", ");

        // 004705b8..0047060f: the system name or "???" (FUN_0041bd10 lower_bound + the key
        // compare = find).
        wcscat(G_NnTextScratchB,
               CommodityByBaseName(G_CommodityByBaseSystemNames, Row.SpaceObjectId));

        // 00470611..00470643: ids 0x3e5 when the base sells the good.
        if (Row.bBaseSells)
        {
            GetIdsStringWWithRawFallback(0x3e5, G_NnTextScratchA, G_NnTextScratchCapacity);
            wcscat(G_NnTextScratchB, G_NnTextScratchA);
        }

        // 00470646..00470672: ids 0x3dd (" (Best Known Price)") on the first row.
        if (Index == 0)
        {
            GetIdsStringWWithRawFallback(0x3dd, G_NnTextScratchA, G_NnTextScratchCapacity);
            wcscat(G_NnTextScratchB, G_NnTextScratchA);
        }

        // 00470675..00470690: FUN_0057e2c0(line, wcslen(line), Card, 0).
        AppendTextMaybeXmlRdl(G_NnTextScratchB, static_cast<unsigned>(wcslen(G_NnTextScratchB)),
                              *Card, false);
    }
}

NnCommodityByBase::NnCommodityByBase()
{
    // FUN_00470790 004707c9..004707e7: FUN_0059f860 and the three vptrs. Nothing else is
    // written before Create.
}

NnCommodityByBase::~NnCommodityByBase()
{
    // FUN_00470870: FUN_00576010 on the +0x32c face, then FUN_0059fa50 -- the EventHandler and
    // WinControl destructors, which C++ runs in that order.
}

void NnCommodityByBase::Close()
{
    // e.g. 0046f595..0046f5bf: FUN_0043d2a0 on every child (next sibling read first), slot 10,
    // then slot 0 with 1.
    Win3D* Child = ChildHead;
    while (Child != nullptr)
    {
        Win3D* const Next = Child->NextSibling;
        Win3DDeleteTree(Child);
        Child = Next;
    }
    Destroy();
    delete this;
}

bool NnCommodityByBase::Create(Win3D* /*NewParent*/, void* Data)
{
    // FUN_0046ef80 (RET 8). 0046ef9d..0046efa9: the archetype, first row 0, no sort column.
    ArchId = static_cast<unsigned>(reinterpret_cast<uintptr_t>(Data));
    FirstRow = 0;
    SortColumn = 0;

    // 0046ef97..0046efc2: slot 38 SetMesh, then the half extents from the mesh (FUN_005a1340).
    SetMesh(kCommodityByBaseMesh, 0);
    WinControlHalfExtentsFromMesh(HalfExtents, &MeshHolder);

    // 0046efc7..0046f002: the title.
    float const TitlePosition[3] = {NnFloatBits(0xbed58106), NnFloatBits(0x3e841893), 0.0f};
    WinControl* const Title = CreateTextControl(
        "CommodityByBaseTitle", "PageHeader", TitlePosition, nullptr, NnFloatBits(0x3f5645a2),
        NnFloatBits(0x3d75c28f), NnFloatBits(0x3fc00000), 0, 1, 0, 1);

    // 0046f007..0046f07b: ids 0x3d5, then " - " (DAT_005d01f4) and the equipment's name when
    // the archetype is equipment.
    GetIdsStringWWithRawFallback(0x3d5, G_NnTextScratchB, G_NnTextScratchCapacity);
    if (ArchId != 0)
    {
        Archetype::Equipment* const Equip = Archetype::GetEquipment(ArchId);
        if (Equip != nullptr)
        {
            wcscat(G_NnTextScratchB, L" - ");
            GetIdsStringWWithRawFallback(static_cast<unsigned>(Equip->IdsName), G_NnTextScratchA,
                                         G_NnTextScratchCapacity);
            wcscat(G_NnTextScratchB, G_NnTextScratchA);
        }
    }

    // 0046f07e..0046f096: upper case (FUN_005b74d9, the delay-loaded CharUpperW), then the
    // title's text (0x1d); the title is not null-tested.
    CharUpperW(G_NnTextScratchB);
    Title->SetProperty(0x1d, reinterpret_cast<uintptr_t>(G_NnTextScratchB), 0);

    // 0046f09c..0046f0fe: the close button -> +0x334.
    {
        float const Position[3] = {NnFloatBits(0x3ecd4fdf), NnFloatBits(0x3e72b021), 0.0f};
        float const Extents[3] = {NnFloatBits(0x3c9374bc), NnFloatBits(0x3c9374bc), 0.0f};
        CloseButton = CreateButtonControl("CommodityByBaseCloseButton", kCommodityByBaseCloseMesh,
                                          nullptr, "ui_select_item", nullptr, Position, nullptr,
                                          Extents, 0, 0, 0, 0, 0x409, 0, 0);
    }

    // 0046f103..0046f283: the four column titles, each then SetProperty(0x24, 1, 0) without a
    // null test.
    {
        float const Position[3] = {NnFloatBits(0xbec7ae14), NnFloatBits(0x3e428f5c), 0.0f};
        PriceTitle = CreateTextControl("CommodityByBasePriceTitle", "SubHeader", Position, nullptr,
                                       NnFloatBits(0x3dcccccd), NnFloatBits(0x3cf5c28f),
                                       NnFloatBits(0x3ff33333), 0x3d6, 0, 0, 1);
        PriceTitle->SetProperty(0x24, 1, 0);
    }
    {
        float const Position[3] = {NnFloatBits(0xbdcac083), NnFloatBits(0x3e428f5c), 0.0f};
        BaseTitle = CreateTextControl("CommodityByBaseBaseTitle", "SubHeader", Position, nullptr,
                                      NnFloatBits(0x3e851eb8), NnFloatBits(0x3cf5c28f),
                                      NnFloatBits(0x3ff33333), 0x3cc, 0, 0, 1);
        BaseTitle->SetProperty(0x24, 1, 0);
    }
    {
        float const Position[3] = {NnFloatBits(0x3e343958), NnFloatBits(0x3e428f5c), 0.0f};
        SystemTitle = CreateTextControl("CommodityByBaseSystemTitle", "SubHeader", Position,
                                        nullptr, NnFloatBits(0x3e4ccccd), NnFloatBits(0x3cf5c28f),
                                        NnFloatBits(0x3ff33333), 0x3cd, 0, 0, 1);
        SystemTitle->SetProperty(0x24, 1, 0);
    }
    {
        float const Position[3] = {NnFloatBits(0xbe8b4396), NnFloatBits(0x3e428f5c), 0.0f};
        BuySellTitle = CreateTextControl("CommodityByBaseBuySellTitle", "SubHeader", Position,
                                         nullptr, NnFloatBits(0x3dcccccd), NnFloatBits(0x3cf5c28f),
                                         NnFloatBits(0x3ff33333), 0x3d8, 0, 0, 1);
        BuySellTitle->SetProperty(0x24, 1, 0);
    }

    // 0046f289..0046f462: 13 lines. Y = DAT_005d0118 - i * DAT_005d011c (FILD / FMUL / FSUBR,
    // one rounding at the FST); the product and the difference are exact in double, so the
    // port's one rounding gives the same float. Each control gets SetProperty(0x24, 1, 0).
    float const LineTop = NnFloatBits(0x3e1cac08);   // DAT_005d0118 0.153
    float const LineStep = NnFloatBits(0x3cf5c28f);  // DAT_005d011c 0.03
    for (int Line = 0; Line < 13; ++Line)
    {
        float const Y = static_cast<float>(static_cast<double>(LineTop) -
                                           static_cast<double>(Line) *
                                               static_cast<double>(LineStep));
        char Name[0xf0]; // [ESP+0x44]
        FCommodityByBaseRowControls& Controls = Rows[Line];

        sprintf(Name, "CommodityByBasePrice%d", Line);
        float const PricePosition[3] = {NnFloatBits(0xbec7ae14), Y, 0.0f};
        Controls.Price = CreateTextControl(Name, "ItemName", PricePosition, nullptr,
                                           NnFloatBits(0x3dcccccd), NnFloatBits(0x3cf5c28f),
                                           NnFloatBits(0x3ff33333), 0, 0, 0, 1);
        Controls.Price->SetProperty(0x24, 1, 0);

        sprintf(Name, "CommodityByBuySellPrice%d", Line);
        float const BuySellPosition[3] = {NnFloatBits(0xbe8b4396), Y, 0.0f};
        Controls.BuySell = CreateTextControl(Name, "ItemName", BuySellPosition, nullptr,
                                             NnFloatBits(0x3dcccccd), NnFloatBits(0x3cf5c28f),
                                             NnFloatBits(0x3ff33333), 0, 0, 0, 1);
        Controls.BuySell->SetProperty(0x24, 1, 0);

        sprintf(Name, "CommodityByBaseBase%d", Line);
        float const BasePosition[3] = {NnFloatBits(0xbdcac083), Y, 0.0f};
        Controls.Base = CreateTextControl(Name, "ItemName", BasePosition, nullptr,
                                          NnFloatBits(0x3e851eb8), NnFloatBits(0x3cf5c28f),
                                          NnFloatBits(0x3ff33333), 0, 0, 0, 1);
        Controls.Base->SetProperty(0x24, 1, 0);

        sprintf(Name, "CommodityByBaseSystem%d", Line);
        float const SystemPosition[3] = {NnFloatBits(0x3e343958), Y, 0.0f};
        Controls.System = CreateTextControl(Name, "ItemName", SystemPosition, nullptr,
                                            NnFloatBits(0x3e4ccccd), NnFloatBits(0x3cf5c28f),
                                            NnFloatBits(0x3ff33333), 0, 0, 0, 1);
        Controls.System->SetProperty(0x24, 1, 0);
    }

    // 0046f468..0046f4aa: the scroll bar (FUN_005a17e0: range 0..5, page 3, vertical).
    {
        float const Position[3] = {NnFloatBits(0x3ec83127), NnFloatBits(0x3e6d9168),
                                   NnFloatBits(0xbfa4dd2f)};
        Scroll = CreateUIScroll("CommodityByBaseScroll", Position, NnFloatBits(0x3c9ba5e3),
                                NnFloatBits(0x3ecfdf3b), 0, 5, 3, 0);
    }

    // 0046f4a3..0046f4d8: the rows for the archetype, sorted by price, then shown.
    BuildCommodityByBaseRows(ArchId);
    if (SortColumn != 1)
    {
        SortColumn = 1;
        SortCommodityByBaseRows(1);
    }
    RefreshRows();

    // 0046f4dd..0046f52a: event 0xf, slot 31 SetOffset(0, 0x3d9ba5e3, 0xbfa4dd2f), slot 5
    // SetPosition(+0x60).
    RegisterEvent(0xf);
    SetOffset(0.0f, NnFloatBits(0x3d9ba5e3), NnFloatBits(0xbfa4dd2f));
    SetPosition(Position);

    // 0046f52d..0046f53f: FUN_004b5060(+0x04, 0) -- the window's current parent and no data,
    // not Create's arguments.
    return Win3D::Create(Parent, nullptr);
}

void NnCommodityByBase::RefreshRows()
{
    // FUN_0046fa80. 0046fa83..0046fab4: the row count.
    int const Count = static_cast<int>(G_CommodityByBaseRows.size());

    // 0046fab8..0046fc19: line i shows row FirstRow + i (signed compare with the count).
    int Row = FirstRow;
    for (int Line = 0; Line < 13; ++Line, ++Row)
    {
        FCommodityByBaseRowControls& Controls = Rows[Line];
        if (Row >= Count)
        {
            // 0046fae1..0046faf5: past the end -- all four hidden.
            Controls.Base->Flags = static_cast<unsigned char>(Controls.Base->Flags & 0xfc);
            Controls.Price->Flags = static_cast<unsigned char>(Controls.Price->Flags & 0xfc);
            Controls.System->Flags = static_cast<unsigned char>(Controls.System->Flags & 0xfc);
            Controls.BuySell->Flags = static_cast<unsigned char>(Controls.BuySell->Flags & 0xfc);
            continue;
        }

        // 0046fafe..0046fb0f: all four shown.
        Controls.Base->Flags = static_cast<unsigned char>(Controls.Base->Flags | 3);
        Controls.Price->Flags = static_cast<unsigned char>(Controls.Price->Flags | 3);
        Controls.System->Flags = static_cast<unsigned char>(Controls.System->Flags | 3);
        Controls.BuySell->Flags = static_cast<unsigned char>(Controls.BuySell->Flags | 3);

        FCommodityByBaseRow const& Data = G_CommodityByBaseRows[Row];

        // 0046fb12..0046fb3d: the price (FUN_004779a0(DAT_0066fc60, price, -1), text 0x1d).
        FormatGroupedNumber(G_NnTextScratchB, Data.Price, -1);
        Controls.Price->SetProperty(0x1d, reinterpret_cast<uintptr_t>(G_NnTextScratchB), 0);

        // 0046fb43..0046fb5d: ids 0x3d9 when the base sells the good, 0x3da otherwise (0x1e).
        unsigned const SellsIds = Data.bBaseSells ? 0x3d9u : 0x3dau;
        Controls.BuySell->SetProperty(0x1e, SellsIds, 0);

        // 0046fb63..0046fb9d: the base name or "???".
        Controls.Base->SetProperty(
            0x1d,
            reinterpret_cast<uintptr_t>(
                CommodityByBaseName(G_CommodityByBaseBaseNames, Data.SpaceObjectId)),
            0);

        // 0046fba3..0046fbf7: the system name or "???" (FUN_0052c7a0 lower_bound + the key
        // compare = find).
        Controls.System->SetProperty(
            0x1d,
            reinterpret_cast<uintptr_t>(
                CommodityByBaseName(G_CommodityByBaseSystemNames, Data.SpaceObjectId)),
            0);
    }

    // 0046fc1f..0046fc8d: the scroll bar -- hidden (property 2 = 1, flag bit 1 off) while the
    // rows fit; otherwise shown (property 2 = 0, flag bit 1 on) with the range 0x37
    // (0, Count - 13 + 2).
    int const Extra = Count - 13;
    if (Extra <= 0)
    {
        Scroll->SetProperty(2, 1, 0);
        Scroll->Flags = static_cast<unsigned char>(Scroll->Flags & 0xfd);
        return;
    }
    Scroll->SetProperty(2, 0, 0);
    Scroll->Flags = static_cast<unsigned char>(Scroll->Flags | 2);
    Scroll->SetProperty(0x37, 0, static_cast<uintptr_t>(Extra + 2));
}

intptr_t NnCommodityByBase::SetProperty(int Msg, uintptr_t A, uintptr_t B)
{
    // LAB_0046f550 (RET 0xc). 0046f554..0046f58a: 0x3e from the scroll bar moves the first row;
    // 0x3e from anything else and 0x26 are clicks; every other message returns 0.
    if (Msg != 0x26)
    {
        if (Msg != 0x3e)
            return 0;
        if (A == reinterpret_cast<uintptr_t>(Scroll))
        {
            FirstRow = static_cast<int>(B);
            RefreshRows();
            return 1;
        }
    }

    // 0046f58d..0046f5d4: the close button -- close, then command 0x66 (FUN_005763b0).
    if (A == reinterpret_cast<uintptr_t>(CloseButton))
    {
        Close();
        HudPostCommand(0x66, 0);
        return 1;
    }

    // 0046f5d7..0046f644: a column title sorts by its column (unless it already does) and
    // redraws; these return 0.
    int Column = 0;
    if (A == reinterpret_cast<uintptr_t>(PriceTitle))
        Column = 1;
    else if (A == reinterpret_cast<uintptr_t>(BaseTitle))
        Column = 2;
    else if (A == reinterpret_cast<uintptr_t>(SystemTitle))
        Column = 3;
    else if (A == reinterpret_cast<uintptr_t>(BuySellTitle))
        Column = 4;
    if (Column != 0)
    {
        if (SortColumn != Column)
        {
            SortColumn = Column;
            SortCommodityByBaseRows(Column);
        }
        RefreshRows();
        return 0;
    }

    // 0046f646..0046f6c4: any control of line i -- close, then FUN_004a2aa0(the space object of
    // row FirstRow + i, 0).
    for (int Line = 0; Line < 13; ++Line)
    {
        FCommodityByBaseRowControls const& Controls = Rows[Line];
        if (A == reinterpret_cast<uintptr_t>(Controls.Base) ||
            A == reinterpret_cast<uintptr_t>(Controls.Price) ||
            A == reinterpret_cast<uintptr_t>(Controls.System) ||
            A == reinterpret_cast<uintptr_t>(Controls.BuySell))
        {
            // 0046f673..0046f68a: Id = rows[FirstRow + i].SpaceObjectId, read before the close
            // (no bounds test, as in retail).
            unsigned const Id = G_CommodityByBaseRows[FirstRow + Line].SpaceObjectId;
            Close();
            // 0046f6b1..0046f6b4: FUN_004a2aa0(Id, 0) -- the system or system object named Id
            // becomes the nav map's pending focus.
            NavMapRequestFocus(Id, 0);
            return 1;
        }
    }
    return 0;
}

void NnCommodityByBase::Update()
{
    // LAB_0046fd20. 0046fd27..0046fd68: close when the window may not stand (the `this` test
    // at 0x0046fd30 cannot fail).
    if (CommodityByBaseMustClose())
    {
        Close();
        return;
    }

    // 0046fd6c: the children (FUN_005a17b0).
    Win3D::Update();

    // 0046fd71..0046fe89: the cursor ray -- the cursor pixel onto the near plane of the Win3D
    // camera DAT_0067dbf8, through a copy of its orientation DAT_0067dbfc (this TU's lazy
    // 3DMathEngine DAT_0066db04, slot +0x2c), scaled by 1.0f / length.
    FSpaceCamera* const Cam = Win3DGetCamera();
    int CursorX = 0;
    int CursorY = 0;
    GetClientSoftwareCursor(&CursorX, &CursorY, nullptr); // DAT_00616840 / DAT_00616844
    FVector Local;
    Local.X = static_cast<float>((static_cast<double>(CursorX) - Cam->ScreenCenterX) /
                                 Cam->XScale * Cam->ZNear);
    Local.Y = static_cast<float>((static_cast<double>(CursorY) - Cam->ScreenCenterY) /
                                 Cam->YScale * Cam->ZNear);
    Local.Z = -Cam->ZNear;
    FMatrix Orientation;
    memcpy(&Orientation, Cam->World, sizeof(Orientation));
    if (G_CommodityByBaseMath == nullptr)
        G_CommodityByBaseMath = Acquire3DMathEngine(); // FUN_005b73e0
    FVector World;
    G_CommodityByBaseMath->TransformMatrix(World, Orientation, Local);
    double const Inv = 1.0 / sqrt(static_cast<double>(World.X) * World.X +
                                  static_cast<double>(World.Y) * World.Y +
                                  static_cast<double>(World.Z) * World.Z);
    float Ray[3];
    Ray[0] = static_cast<float>(World.X * Inv);
    Ray[1] = static_cast<float>(World.Y * Inv);
    Ray[2] = static_cast<float>(World.Z * Inv);

    // 0046fe90..0046fefa: the column titles (slot 0 of the local table is skipped). A visible
    // title is the idle colour, the sorted column's the amber DAT_00679ba4, and one under the
    // mouse the pulse (FUN_00561da0); then SetProperty(0x33, &colour, 1).
    WinControl* const Titles[5] = {nullptr, PriceTitle, BaseTitle, SystemTitle, BuySellTitle};
    for (int Column = 1; Column < 5; ++Column)
    {
        WinControl* const Title = Titles[Column];
        if (Title == nullptr || (Title->Flags & kWin3DVisible) == 0)
            continue;
        unsigned long Color = G_NnPaletteArgb_679bb0;
        if (SortColumn == Column)
        {
            Color = G_NnPaletteArgb_679ba4;
        }
        else if (Title->HitTest(Ray))
        {
            unsigned long Pulse = 0;
            NnUiPulseBlendArgb(&Pulse);
            Color = Pulse;
        }
        Title->SetProperty(0x33, reinterpret_cast<uintptr_t>(&Color), 1);
    }

    // 0046fefc..0046ffcc: a visible line (tested on its base name) is the idle colour, or the
    // pulse when the mouse is over any of its four controls; all four get the colour.
    for (int Line = 0; Line < 13; ++Line)
    {
        FCommodityByBaseRowControls const& Controls = Rows[Line];
        if (Controls.Base == nullptr || (Controls.Base->Flags & kWin3DVisible) == 0)
            continue;
        unsigned long Color = G_NnPaletteArgb_679bb0;
        if (Controls.Base->HitTest(Ray) || Controls.BuySell->HitTest(Ray) ||
            Controls.Price->HitTest(Ray) || Controls.System->HitTest(Ray))
        {
            unsigned long Pulse = 0;
            NnUiPulseBlendArgb(&Pulse);
            Color = Pulse;
        }
        Controls.Base->SetProperty(0x33, reinterpret_cast<uintptr_t>(&Color), 1);
        Controls.BuySell->SetProperty(0x33, reinterpret_cast<uintptr_t>(&Color), 1);
        Controls.Price->SetProperty(0x33, reinterpret_cast<uintptr_t>(&Color), 1);
        Controls.System->SetProperty(0x33, reinterpret_cast<uintptr_t>(&Color), 1);
    }
}

bool NnCommodityByBase::OnEvent(unsigned MsgId)
{
    // FUN_0046ffe0 (RET 4) on the +0x32c face.
    if (HudIsMousePickBlocked()) // FUN_0045a740
        return false;
    if (MsgId != 0xf)
        return false;
    // 0046ffff..00470033: the owner (this - 0x32c, whose null test cannot fail) closes.
    Close();
    return true;
}
