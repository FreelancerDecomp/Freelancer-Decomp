// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "connect_lists.h"

#include "best_path.h"              // G_BestPaths -- DAT_0067294c
#include "character_select_equip.h" // the player-state lists (DAT_00672960 / DAT_0067296c / ...)
#include "common_goods.h"           // EquipDescList
#include "dacom.h"                  // FDUMP
#include "nn_story_star.h"          // StoryStarReset (FUN_004b9bb0), StoryStarSetRankUp (FUN_004b9b20)
#include "player_spawn.h"           // ResetPlayerWaypointsRetail (FUN_004c46c0)

#include <list>

namespace
{
    // The steps FUN_004c4a80 and FUN_004c4bc0 share, in listing order.
    void ClearPlayerConnectLists()
    {
        // 0x004c4ab1..0x004c4ad7 / 0x004c4bc5..0x004c4be8: every node of the collision-group
        // list DAT_0067296c erased (Common's list erase, one node at a time).
        GetPlayerCollisionGroupListRetail().clear();
        // 0x004c4ad9..0x004c4afc / 0x004c4bea..0x004c4c0d: every node of the EquipDescList
        // DAT_00672960 erased. Neither revision counter is touched.
        GetPlayerEquipDescListRetail().Items.clear();
        // 0x004c4afe..0x004c4b11 / 0x004c4c0f..0x004c4c22: FUN_004c5a80 erases the objective map
        // DAT_0067294c from its first node to its head -- the whole map (G_BestPaths,
        // best_path.h).
        G_BestPaths.clear();
        // 0x004c4b16..0x004c4ba1 / 0x004c4c27..0x004c4ca2: the visit map DAT_00673320 cleared
        // (the whole tree at once when the erase range is begin..end, else node by node).
        ClearPlayerVisitMapRetail();
        // 0x004c4ba3..0x004c4baa / 0x004c4ca4..0x004c4cab: the NN_StoryStar data model --
        // FUN_004b9bb0() (the pending objectives and the banner) and FUN_004b9b20(0) (no rank-up
        // pending).
        StoryStarReset();
        StoryStarSetRankUp(0);
    }
}

void PreConnectReset()
{
    // FUN_004c4a80. 0x004c4a8a / 0x004c4a94: DAT_0067337c = 0, DAT_00673380 = 0.
    ClearPlayerShipArchAndHealthRetail();
    // 0x004c4a80 / 0x004c4a9e..0x004c4aaa: a positive waypoint count DAT_00673374 goes to 0 and
    // FUN_004f4cd0 (HudWaypointOnPlayerWaypointsCleared) runs -- FUN_004c46c0's body, inlined.
    ResetPlayerWaypointsRetail();
    ClearPlayerConnectLists();
}

void TeardownConnectLists()
{
    // FUN_004c4bc0 (callers 0x005a80e8, 0x005a82b9, 0x005a8825, 0x005a891a): FUN_004c4a80
    // without the two stores and the waypoint reset.
    ClearPlayerConnectLists();
}
