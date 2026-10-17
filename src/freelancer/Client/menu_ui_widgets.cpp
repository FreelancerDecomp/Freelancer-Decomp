// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "menu_ui_widgets.h"

#include "character_select_equip.h" // SetPlayerRankLevelRetail (FUN_004c55e0's global)
#include "common_server.h"          // IServer::RequestRankLevel (slot 78)
#include "login_mgr.h"              // G_Server (DAT_0067ecd0)
#include "player_spawn.h"           // GetPlayerConnectionId (DAT_00673344)
#include "win_factory.h"            // WinFactoryCreate (FUN_0059db20)

#include <vector>

// 2026-10-07 (claude-code:dev-a): the port-only flat menu layer that filled this file -- the
// FMenuUiWidget registry and its factories, RegisterMainMenuUiPlaceholders (a port-only
// "NN_Main" Win3D root), RenderMenuUi, the screen-rect hit test / hover / focus walk
// (MenuUiHitTest, MenuUiSetHover, MenuUiFocusDelta, ...), NormLayoutToScreenRect (the last
// g_display reader of the letterbox entry) and the dead MenuUiCreate* helpers -- is gone.
// Retail's menu input is the Win3D manager's: MouseDispatchEvents (FUN_0041ef20) ->
// KeyMapRouteMouseEvent -> commands 4..13 -> FWin3DInputHandler::Handle (FUN_0059d140), the
// hover pick in Win3DUpdateAll (FUN_0059e080), keys through KeyMapRouteInputMessage
// (FUN_00577850) from the message pump. What remains here are retail functions that happened
// to live in this file.

// FUN_004c55f0 — the player's rank level (DAT_0067338c) becomes -1, pending, and
// IServer::RequestRankLevel(connection, {0}, 4) (vtable slot 78, +0x138) asks the server for
// it again; NN_ShipTrader::Update (FUN_004b93d0) notices the change. Call sites 0x0047fce1
// (NN_Dealer ctor, nn_dealer.cpp) and 0x004b850f (NN_ShipTrader::Create).
void HudMessageQueueRequestRankRefresh()
{
    // 0x004c5609: DAT_0067338c = -1; 0x004c5613: the 4-byte local zeroed; 0x004c55f6 /
    // 0x004c561b: ECX = DAT_0067ecd0 with no null test, slot 78 (+0x138).
    SetPlayerRankLevelRetail(-1);
    unsigned char Data[4] = {0, 0, 0, 0};
    G_Server->RequestRankLevel(GetPlayerConnectionId(), Data, 4);
}

namespace
{
    // FUN_004c5630(ctor)/FUN_004c5660(dtor)/FUN_004c5680(clear)/FUN_004c56b0(push_back)/
    // FUN_004c5890(size)/FUN_004c58b0(at) — retail keeps a small std::vector<unsigned>-shaped
    // object (globals DAT_006728cc/d0/d4, begin/end/capacity-end) plus a leading flag byte
    // (DAT_006728c8). Ghidra folded the vector's internal destroy-range helper onto an
    // unrelated symbol "IDPMsgHandler::OnReceive" (identical-code-folding of two same-shaped
    // template instantiations). External call sites: 0x004d074a/0x004d076c/0x004d0791
    // (HUD_ContactList), 0x004e1e2d/0x004e1e4b/0x004e1e71 (HUD_Target) read via size()+at(),
    // and 0x0053e27c/0x0053e29e (IClient slot 120) clear then push_back.
    struct FRetailU32QueueStub
    {
        bool bFlag = false;
        std::vector<unsigned> Items;
    };
    // LAB_004c5630 (static init: flag 0, empty vector) and its atexit LAB_004c5660 (free the
    // buffer, FUN_005b7e1d) are this static object's own constructor and destructor.
    FRetailU32QueueStub GRetailU32Queue;
}

// FUN_004c5680 — clear() without releasing storage (matches retail's end=begin reset).
void RetailU32QueueClear()
{
    GRetailU32Queue.Items.clear();
}

// FUN_004c56b0 — push_back(value).
void RetailU32QueuePushBack(unsigned Value)
{
    GRetailU32Queue.Items.push_back(Value);
}

// FUN_004c5890 — size(): 0 for an unallocated vector, else (end - begin) >> 2.
unsigned RetailU32QueueSize()
{
    return static_cast<unsigned>(GRetailU32Queue.Items.size());
}

// FUN_004c58b0 — at(index): a negative index (JL), an unallocated vector or index >= size
// (unsigned JNC) give 0. A negative int converts to an unsigned above any size, so the one
// unsigned compare covers retail's two tests.
unsigned RetailU32QueueAt(unsigned Index)
{
    if (Index >= GRetailU32Queue.Items.size())
        return 0;
    return GRetailU32Queue.Items[Index];
}

// FUN_004c5940 is the exe's copy of BaseWatcher::BaseWatcher(IObjRW*); its one caller, the
// static init LAB_004c5060 (0x004c5067), passes null for DAT_006728d8 -- best_path.cpp's
// G_BestPathBaseWatcher, whose default constructor is the port of that call.

void MenuUiInitToolTipAndRollover()
{
    // Pre-init 005b1f86..005b1fac: FUN_0059db20("ToolTip","ToolTip",0,0) then
    // FUN_0059db20("Rollover","Rollover",0,0). Both use the nickname as the class name, and
    // retail ignores the return value in both cases.
    // Both are WINFACTORY objects reached through FUN_0059db20 itself: WINFACTORY<ToolTip>
    // (nn_tooltip.cpp, DAT_00672410) and WINFACTORY<Rollover> (nn_rollover.cpp, DAT_0067224c).
    WinFactoryCreate("ToolTip", "ToolTip", nullptr, nullptr);
    WinFactoryCreate("Rollover", "Rollover", nullptr, nullptr);
}
