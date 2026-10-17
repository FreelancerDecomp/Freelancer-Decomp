// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Kept from the old header: several includers still reach common_goods.h / <windows.h> through
// it (the removed menu_input.h used to bring <windows.h>).
#include "common_goods.h"

#include <windows.h>

// Retail functions that live in menu_ui_widgets.cpp. The port-only flat menu layer this header
// used to declare (FMenuUiWidget registry, MenuUiCreate*, RenderMenuUi, MenuUiHitTest / hover /
// focus, NormLayoutToScreenRect, the NN_Main shell) was removed 2026-10-07: retail's menu input
// goes through the Win3D manager (FUN_0059d140 / FUN_0059e080) and FUN_00577850.

// Pre-init 005b1f86..005b1fac — the two FUN_0059db20 calls for ToolTip and Rollover.
void MenuUiInitToolTipAndRollover();

// The Rollover (DAT_00672288) and its global entry points live in nn_rollover.h.

// FUN_0059da10 (find by name) is Win3DFindByName and FUN_0059d9c0 (find by class name) is
// Win3DFindByClassName, both in win3d.h.

// FUN_004c55f0 — the rank level (DAT_0067338c) = -1, then IServer::RequestRankLevel(connection,
// {0}, 4). Called by the NN_Dealer ctor (0x0047fce1) and NN_ShipTrader::Create (0x004b850f).
void HudMessageQueueRequestRankRefresh();

// FUN_004c5680 / FUN_004c56b0 — clear and push_back on the retail dword vector at
// DAT_006728cc..d4. Only caller: IClient slot 120 (LAB_0053e270).
void RetailU32QueueClear();
void RetailU32QueuePushBack(unsigned Value);
// FUN_004c5890 / FUN_004c58b0 — size() and bounds-checked at() (0 out of range). HUD_Target's
// formation test (0x004e1e2d..) reads the entries as object ids of the player's group.
unsigned RetailU32QueueSize();
unsigned RetailU32QueueAt(unsigned Index);
