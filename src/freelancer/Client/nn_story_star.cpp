// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Retail NN_StoryStar.cpp (0x004b9890..0x004bd5d0). See nn_story_star.h.

#include "nn_story_star.h"

#include "best_path.h"            // G_BestPaths (DAT_00672950), BestPathGetWaypoint(Count)
#include "client_state.h"         // QueueLoadGameCharacter (FUN_005b2c70)
#include "font_string.h"          // FFontString (+0x28 LayoutGen, +0x30 bMultiLine)
#include "globals.h"              // GetClientFrameTimeCapped (FUN_0042d680), ShouldSkipLocalServerTick
#include "hud_command_dispatch.h" // HudPostCommand (FUN_005763b0)
#include "hud_target.h"           // HudIsMousePickBlocked (FUN_0045a740)
#include "ids_rdl.h"              // RdlAppendNode, AppendTextMaybeXmlRdl, LoadIdsIntoRdl
#include "ids_strings.h"          // FormatFmtStrMadLib, GetIdsStringWWithRawFallback
#include "input_manager.h"        // Get/SetClientMouseCursorDrawFlag, MouseSetCursor
#include "knowledge_map.h"        // KnowledgeMapRevealFmtStr (FUN_00459950)
#include "login_mgr.h"            // G_LoginMgr (DAT_0067e7b8), G_Server (DAT_0067ecd0)
#include "main_command_handler.h" // SetClientQuitRequested (FUN_005b2c50)
#include "menu_enter_state.h"     // MenuNavSetIntroModeRequest (FUN_00564b10)
#include "menu_ui_init.h"         // PlayOrRewindUiSoundSlot (FUN_005645c0)
#include "nav_bar.h"              // NavBarCloseService (FUN_004424f0), NavBarHasServiceWindow (FUN_00442720)
#include "nn_arrival_text.h"      // NnArrivalText::IsCaptionVisible (FUN_0045b2c0)
#include "nn_load_save.h"         // OpenNnLoadSave (FUN_00488970)
#include "nn_main_button.h"       // NN_MainButton, G_bNnKeepPanelOpen (DAT_0066d359)
#include "nn_modal.h"             // G_NnMainButton, G_bNnScreenUp, NnFloatBits, ...
#include "nn_text_control.h"
#include "nn_text_scratch.h"      // G_NnTextScratchA / B (DAT_0066dc60 / DAT_0066fc60)
#include "nn_ui_pulse.h"          // G_NnPaletteArgb_679bb0
#include "player_spawn.h"         // GetPlayerConnectionId (DAT_00673344)
#include "rich_text_win.h"
#include "server_connect.h"       // ShutdownServerConnection (FUN_005a8260)
#include "space_letterbox.h"      // SetLetterboxTarget (FUN_0041a0f0), IsLetterboxVisible
#include "win3d.h"
#include "win_factory.h"

#include "common_goods.h"         // GoodList::find_by_id
#include "common_reputation.h"    // Reputation::get_name, Reputation::Vibe::GetName
#include "common_server.h"        // IServer::SetMissionLog (slot 76)
#include "common_misc.h"          // SinglePlayer
#include "common_universe.h"      // Universe::get_system / get_base / get_zone
#include "dacom.h"                // FDUMP

#include <stdint.h>
#include <string.h>
#include <wchar.h>

// ---------------------------------------------------------------------------------------------
// The data model (static initializers LAB_004b9950 / LAB_004b9a00 / LAB_004b9ab0; the atexit
// bodies LAB_004b9990 / LAB_004b9a40 / LAB_004b9b10 are the lists' and the FmtStr's dtors).

std::list<FmtStr> G_StoryStarHistory;                    // DAT_00672330
std::list<FmtStr> G_StoryStarPending;                    // DAT_00672324
FmtStr G_StoryStarLastObjective;                         // DAT_0067230c
unsigned char G_bStoryStarHasLastObjective = 0;          // DAT_00672404
MissionObjectiveWindow* G_MissionObjectiveWindow = nullptr; // DAT_00672400
NeuroNetStoryStar* G_NeuroNetStoryStar = nullptr;        // DAT_006723fc
float G_StoryStarIdleTime = 0.0f;                        // DAT_00672408
int G_StoryStarRankUp = 0;                               // DAT_0067240c

// The four WINFACTORY statics (LAB_004b9890 / LAB_004b98c0 / LAB_004b98f0 / LAB_004b9920, each
// with its atexit thunk to FUN_00579380).
WINFACTORY<NeuroNetStoryStar> G_NeuroNetStoryStarFactory("NeuroNetStoryStar");       // DAT_006723c0
WINFACTORY<MissionObjectiveWindow> G_MissionObjectiveWindowFactory("MissionObjectiveWindow"); // DAT_00672394
WINFACTORY<MissionLogMoveToNNWindow> G_MissionLogMoveToNNWindowFactory("MissionLogMoveToNNWindow"); // DAT_00672368
WINFACTORY<MissionLogMoveFromNNWindow> G_MissionLogMoveFromNNWindowFactory("MissionLogMoveFromNNWindow"); // DAT_0067233c

namespace
{
    char const kStoryStarFrameMesh[] = "Interface\\NeuroNet\\Inventory\\CHAR_infowindow.cmp"; // 005ce994
    char const kStoryStarCloseMesh[] = "Interface\\NeuroNet\\Inventory\\x.3db";              // 005d01d0
    char const kStoryStarIconMesh[] = "interface\\NeuroNet\\icons\\NN_storystar.3db";        // 005cec64
    char const kSelectSound[] = "ui_select_item";                                           // 005ced30
    char const kButtonTextStyle[] = "ButtonText";                                           // 005d0410

    // The mad-lib ids the data model tests.
    unsigned const kIdsDeferredObjective = 0x549c; // kept aside, never queued (FUN_004b9f90)
    unsigned const kIdsRankUp = 0x3d3;             // the rank-up banner (FUN_004bc990)
    unsigned const kIdsNoLastObjective = 0x762;    // not remembered by FUN_004bb6b0
    unsigned const kIdsLogHeading = 0x723;         // highlighted without a prefix (FUN_004bc2a0)

    // One of the windows the banner and the driver yield to (0x004bb3ae.., 0x004bb46c..,
    // 0x004bca4a..): the mission message, the trade window, the scanner and the commodity list.
    bool StoryStarMissionMessageUp()
    {
        return Win3DFindByName("NeuroNetMissionMessage") != nullptr;
    }

    // The clone-into-list idiom (StyleNode ctor, vt+0x1c Clone, then FUN_0046c800 / the inline
    // FUN_0042a7e0 + FUN_0046ea00 insert clones it again, and the first clone is deleted).
    void AppendNodeClone(RenderDisplayList& List, RDLNode const& Node)
    {
        RDLNode* const Temp = Node.Clone();
        RdlAppendNode(List, Temp->Clone());
        delete Temp;
    }

    // IServer slot 76 SetMissionLog with the packed buffer -- the three single-player sends
    // (0x004ba118.., 0x004ba1e1.., 0x004ba407..).
    void StoryStarSendMissionLog(int Type, FmtStr const* Text)
    {
        int const Size = PackMissionLogData(Type, Text,
                                            reinterpret_cast<unsigned char*>(G_NnTextScratchB),
                                            G_NnTextScratchCapacity * 2);
        G_Server->SetMissionLog(GetPlayerConnectionId(),
                                reinterpret_cast<unsigned char*>(G_NnTextScratchB), Size);
    }
}

// ---------------------------------------------------------------------------------------------
// The cdecl data-model functions.

void StoryStarSetRankUp(int Rank)
{
    // FUN_004b9b20
    G_StoryStarRankUp = Rank;
}

void StoryStarForgetLastObjective()
{
    // FUN_004b9b30
    G_bStoryStarHasLastObjective = 0;
}

void StoryStarRequeueLastObjective()
{
    // FUN_004b9b40: a remembered objective, nothing pending and no banner -> a copy of it at
    // the back of the pending list (0x004b9b64..0x004b9ba3; FUN_004bd5d0 copy-constructs).
    if (G_bStoryStarHasLastObjective == 0)
        return;
    if (G_StoryStarPending.size() != 0)
        return;
    if (Win3DFindByName("MissionObjectiveWindow") != nullptr)
        return;
    G_StoryStarPending.push_back(G_StoryStarLastObjective);
}

void StoryStarReset()
{
    // FUN_004b9bb0. 0x004b9bb0..0x004b9c03: unless DAT_0066d35a, every pending node erased and
    // the last-objective flag cleared.
    if (G_bNnStoryStarKeepPending == 0)
    {
        G_StoryStarPending.clear();
        G_bStoryStarHasLastObjective = 0;
    }
    // 0x004b9c0b..0x004b9c4f: the banner unlinked (FUN_0059dd00(w, 0)) and deleted with its
    // subtree (FUN_0043d2a0 inline).
    if (G_MissionObjectiveWindow != nullptr)
    {
        Win3DReparent(G_MissionObjectiveWindow, nullptr);
        Win3DDeleteTree(G_MissionObjectiveWindow);
    }
}

void StoryStarOnMissionMessage(FmtStr const* Text, int Type)
{
    // FUN_004b9c60 (cdecl).
    // 0x004b9c7e..0x004b9c9a: type 0xa prunes the history.
    if (Type == 0xa)
    {
        StoryStarPruneHistory();
        return;
    }

    if (Type != 0 && Type != 1 && Type != 2 && Type != 3)
    {
        if (Type == 5)
        {
            // 0x004b9cfa..0x004b9d07: a history entry, then the mission log opens (below).
            StoryStarAddHistory(Text);
        }
        else
        {
            // 0x004b9cb5..0x004b9cf4: 7 queues, 8 records, 9 does both; 4, 6 and the rest
            // nothing.
            if (Type == 7 || Type == 9)
                StoryStarPushPending(Text);
            if (Type == 8 || Type == 9)
                StoryStarAddHistory(Text);
            return;
        }
    }
    else
    {
        // 0x004b9d0c..0x004b9d57: a zeroed local FmtStr, then the jump table 0x004b9f7c.
        FmtStr Heading;
        switch (Type)
        {
        case 0:
            // 0x004b9d64: the message's own template, ids 0x412 when it is 1.
            Heading.begin_mad_lib(Text->MadLibId == 1 ? 0x412 : Text->MadLibId);
            break;
        case 1:
            // 0x004b9d7a: ids 0x4cf when it is 1.
            Heading.begin_mad_lib(Text->MadLibId == 1 ? 0x4cf : Text->MadLibId);
            break;
        case 2:
        case 3:
            // 0x004b9d90 / 0x004b9dd7: FUN_004b9bb0 written out, except that the banner goes
            // through FUN_0043d2a0 itself; 3 then begins ids 0x4cf, 2 nothing.
            if (G_bNnStoryStarKeepPending == 0)
            {
                G_StoryStarPending.clear();
                G_bStoryStarHasLastObjective = 0;
            }
            if (G_MissionObjectiveWindow != nullptr)
            {
                Win3DReparent(G_MissionObjectiveWindow, nullptr);
                Win3DDeleteTree(G_MissionObjectiveWindow);
            }
            if (Type == 3)
                Heading.begin_mad_lib(0x4cf);
            break;
        }

        // 0x004b9e2b..0x004b9e85: with a heading, it is queued and recorded (the message itself
        // is recorded first for 2 and 3); without one the message is queued and recorded.
        Heading.end_mad_lib();
        if (Heading.MadLibId != 0)
        {
            StoryStarPushPending(&Heading);
            if (Type == 2 || Type == 3)
                StoryStarAddHistory(Text);
            StoryStarAddHistory(&Heading);
        }
        else
        {
            StoryStarPushPending(Text);
            StoryStarAddHistory(Text);
        }
    }

    // 0x004b9e8b..0x004b9f62: types 0 and 5 bring the mission log up.
    if (Type != 0 && Type != 5)
        return;
    if (!SinglePlayer() && Win3DAnyCaptured())
    {
        // 0x004b9eab..0x004b9ebf: a control holds the mouse in multiplayer -- NN_MainButton opens
        // it on its own later.
        G_NnMainButton->SetForceMissionLog(true); // FUN_0045dcc0(1)
        SetClientMouseCursorDrawFlag(1);           // FUN_0041dda0(1)
        return;
    }
    // 0x004b9ed4..0x004b9eec
    SetLetterboxTarget(0.0f, 3.0f); // FUN_0041a0f0
    SetClientMouseCursorDrawFlag(1);
    MouseSetCursor("Arrow", false); // FUN_0041dde0
    // 0x004b9ef1..0x004b9f3f: an open story star goes, and NN_MainButton drops its panel.
    if (G_NeuroNetStoryStar != nullptr)
    {
        Win3DReparent(G_NeuroNetStoryStar, nullptr);
        Win3DDeleteTree(G_NeuroNetStoryStar);
        G_NnMainButton->ResetPanel(); // FUN_0045cef0
    }
    // 0x004b9f44..0x004b9f62
    G_NnMainButton->SetForceMissionLog(true);
    G_bNnScreenUp = false;
    HudPostCommand(0x67, 0);
    G_bNnScreenUp = true;
}

void StoryStarPushPending(FmtStr const* Text)
{
    // FUN_004b9f90 (cdecl).
    // 0x004b9f98..0x004b9fbc: the 0x549c objective is only remembered.
    if (Text->MadLibId == kIdsDeferredObjective)
    {
        G_StoryStarLastObjective = *Text;
        G_bStoryStarHasLastObjective = 1;
        return;
    }

    // 0x004b9fbd..0x004b9ffe: under the letterbox the queue starts over.
    if (IsLetterboxVisible())
    {
        while (G_StoryStarPending.begin() != G_StoryStarPending.end())
            G_StoryStarPending.erase(G_StoryStarPending.begin()); // FUN_004bd4b0
    }

    // 0x004ba004..0x004ba012: the previous last entry (JBE on the unsigned size).
    FmtStr const* Previous = nullptr;
    if (G_StoryStarPending.size() != 0)
        Previous = &G_StoryStarPending.back();

    // 0x004ba015..0x004ba067: append a copy, reveal its words, resolve its ids.
    G_StoryStarPending.push_back(*Text);
    KnowledgeMapRevealFmtStr(Text);
    FmtStr* const Added = &G_StoryStarPending.back();
    StoryStarResolveIds(Added);

    // 0x004ba06a..0x004ba0dd: the same text twice in a row is dropped.
    if (Previous == nullptr)
        return;
    FormatFmtStrMadLib(Added, G_NnTextScratchB, G_NnTextScratchCapacity, true);
    FormatFmtStrMadLib(Previous, G_NnTextScratchA, G_NnTextScratchCapacity, true);
    if (wcscmp(G_NnTextScratchB, G_NnTextScratchA) == 0)
        G_StoryStarPending.pop_back();
}

void StoryStarPruneHistory()
{
    // FUN_004ba0f0 (cdecl).
    // 0x004ba0f4..0x004ba12f: single player -- the server's copy is cleared first (type 0).
    if (SinglePlayer())
        StoryStarSendMissionLog(0, nullptr);

    // 0x004ba135..0x004ba19e: every entry that is not a log line ('*') goes.
    std::list<FmtStr>::iterator It = G_StoryStarHistory.begin();
    while (It != G_StoryStarHistory.end())
    {
        FormatFmtStrMadLib(&*It, G_NnTextScratchB, G_NnTextScratchCapacity, true);
        if (G_NnTextScratchB[0] != L'*')
            It = G_StoryStarHistory.erase(It); // FUN_004bd620(0) + free
        else
            ++It;
    }

    // 0x004ba1a0..0x004ba209: single player -- the rest re-sent oldest first (type 1). The
    // formatted text is written into the buffer before PackMissionLogData overwrites it.
    if (SinglePlayer() && !G_StoryStarHistory.empty())
    {
        std::list<FmtStr>::reverse_iterator Back = G_StoryStarHistory.rbegin();
        for (; Back != G_StoryStarHistory.rend(); ++Back)
        {
            FmtStr const* const Entry = &*Back;
            FormatFmtStrMadLib(Entry, G_NnTextScratchB, G_NnTextScratchCapacity, true);
            StoryStarSendMissionLog(1, Entry);
        }
    }

    // 0x004ba20b..0x004ba26f: the banner goes unless it shows the rank-up. The front read is
    // guarded by the size test (the `&front + 8` null test at 0x004ba229 is dead).
    if (G_MissionObjectiveWindow != nullptr && G_StoryStarPending.size() != 0 &&
        G_StoryStarPending.front().MadLibId != kIdsRankUp)
    {
        Win3DReparent(G_MissionObjectiveWindow, nullptr);
        Win3DDeleteTree(G_MissionObjectiveWindow);
    }

    // 0x004ba271..0x004ba296: the pending list emptied node by node (whatever DAT_0066d35a).
    while (G_StoryStarPending.begin() != G_StoryStarPending.end())
        G_StoryStarPending.erase(G_StoryStarPending.begin()); // FUN_004bd4b0
}

void StoryStarAddHistory(FmtStr const* Text)
{
    // FUN_004ba2a0 (cdecl). 0x004ba2a1..0x004ba2a9: no text, nothing.
    if (Text == nullptr)
        return;

    // 0x004ba2af..0x004ba2da: a deferred 0x549c entry at the front is replaced.
    if (G_StoryStarHistory.size() != 0 && G_StoryStarHistory.front().MadLibId == kIdsDeferredObjective)
        G_StoryStarHistory.erase(G_StoryStarHistory.begin()); // FUN_004bd4b0

    // 0x004ba2df..0x004ba322: the newest entry that is not a log line ('*').
    FmtStr const* Newest = nullptr;
    if (G_StoryStarHistory.size() != 0)
    {
        for (std::list<FmtStr>::iterator It = G_StoryStarHistory.begin();
             It != G_StoryStarHistory.end(); ++It)
        {
            FormatFmtStrMadLib(&*It, G_NnTextScratchA, G_NnTextScratchCapacity, true);
            if (G_NnTextScratchA[0] != L'*')
            {
                Newest = &*It;
                break;
            }
        }
    }

    // 0x004ba324..0x004ba367: a copy in front (FUN_004bd5a0 node + FUN_004bd5d0), ids resolved.
    G_StoryStarHistory.push_front(*Text);
    FmtStr* const Added = &G_StoryStarHistory.front();
    StoryStarResolveIds(Added);

    // 0x004ba36a..0x004ba3e4: the same text as that entry is dropped again, unsent.
    if (Newest != nullptr)
    {
        FormatFmtStrMadLib(Added, G_NnTextScratchB, G_NnTextScratchCapacity, true);
        FormatFmtStrMadLib(Newest, G_NnTextScratchA, G_NnTextScratchCapacity, true);
        if (wcscmp(G_NnTextScratchB, G_NnTextScratchA) == 0)
        {
            G_StoryStarHistory.pop_front();
            return;
        }
    }

    // 0x004ba3e5..0x004ba41e: single player -- the new entry goes to the server (type 1).
    if (SinglePlayer())
        StoryStarSendMissionLog(1, Added);
}

void StoryStarLoadHistory(unsigned char* Data, int Size)
{
    // FUN_004ba430 (cdecl). 0x004ba44b..0x004ba46d: the history emptied.
    while (G_StoryStarHistory.begin() != G_StoryStarHistory.end())
        G_StoryStarHistory.erase(G_StoryStarHistory.begin()); // FUN_004bd4b0

    // 0x004ba46f..0x004ba49e: unless DAT_0066d35a, the pending list too.
    if (G_bNnStoryStarKeepPending == 0)
    {
        while (G_StoryStarPending.begin() != G_StoryStarPending.end())
            G_StoryStarPending.erase(G_StoryStarPending.begin());
        G_bStoryStarHasLastObjective = 0;
    }

    // 0x004ba4a4..0x004ba4bf: the count at +4, the records from +8 (signed JLE on the count).
    int Remaining = Size - 8;
    int Count = *reinterpret_cast<int const*>(Data + 4);
    unsigned char* Cursor = Data + 8;
    if (Count <= 0)
        return;

    // 0x004ba4d0..0x004ba586: each record into a zeroed local, a copy appended.
    do
    {
        FmtStr Record;
        ExtractMissionLogData(&Record, Cursor, Remaining);
        G_StoryStarHistory.push_back(Record);
        --Count;
    } while (Count != 0);
}

void StoryStarResolveIds(FmtStr* Text)
{
    // FUN_004ba5a0 (cdecl): each value's TypeTag through the byte table 0x004ba7e8 / jump table
    // 0x004ba7c4 (codes '!'..'Z'); everything else is left alone.
    for (FmtStr::Val* Node = Text->Head; Node != nullptr; Node = Node->Next)
    {
        unsigned const Id = Node->Value;
        switch (Node->TypeTag)
        {
        case L'!':
            // 0x004ba79a: a nested FmtStr, recursively.
            StoryStarResolveIds(Node->Subtree);
            break;
        case L'S':
        {
            // 0x004ba5f0: the system's strid_name (ISystem slot 3), 0 without a system.
            Universe::ISystem const* const System = Universe::get_system(Id);
            Node->Value = System == nullptr ? 0 : (unsigned)System->GetStridName();
            break;
        }
        case L'B':
        {
            // 0x004ba618: the base's strid_name (IBase slot 2), 0 without a base.
            Universe::IBase* const Base = Universe::get_base(Id);
            Node->Value = Base == nullptr ? 0 : (unsigned)Base->StridName;
            break;
        }
        case L'G':
        {
            // 0x004ba640: the good's ids_name (+0x88), 0 without a good.
            GoodInfo const* const Good = GoodList::find_by_id(Id);
            Node->Value = Good == nullptr ? 0 : (unsigned)Good->IdsName;
            break;
        }
        case L'N':
            // 0x004ba667: a data block is freed (operator delete FUN_005b7e1d) and the node
            // becomes an 'M' carrying the block's second dword.
            if (Node->Marker != nullptr)
            {
                unsigned const Second = Node->Marker->Data[1];
                operator delete(Node->Marker);
                Node->Value = Second;
                Node->TypeTag = 0x4d;
            }
            break;
        case L'R':
        {
            // 0x004ba689: the reputation instance's first name's template, 0 when GetName fails.
            int const GroupId = static_cast<int>(Id);
            FmtStr Name1;
            FmtStr Name2;
            wchar_t const* DisplayName; // not read
            int const bFound = Reputation::Vibe::GetName(GroupId, Name1, Name2, DisplayName);
            Node->Value = bFound != 0 ? Name1.MadLibId : 0u;
            break;
        }
        case L'F':
        {
            // 0x004ba75d: the faction's name ids; -1 becomes 0 (SETE / DEC / AND).
            unsigned const Name = Reputation::get_name(Id);
            Node->Value = Name == 0xffffffffu ? 0u : Name;
            break;
        }
        case L'Z':
        {
            // 0x004ba77a: the zone's name ids (IZone +0x84); an unknown zone keeps its id.
            Universe::IZone const* const Zone = Universe::get_zone(Id);
            if (Zone != nullptr)
                Node->Value = (unsigned)Zone->GetName();
            break;
        }
        default:
            break;
        }
    }
}

void StoryStarTick()
{
    // FUN_004bc990 (cdecl).
    // 0x004bc996..0x004bc9c8: the arrival caption showing or a banner up -- the idle time
    // restarts (0x004bcbca).
    if ((G_NnArrivalText != nullptr && G_NnArrivalText->IsCaptionVisible()) ||
        G_MissionObjectiveWindow != nullptr)
    {
        G_StoryStarIdleTime = 0.0f;
        return;
    }

    // 0x004bc9ce..0x004bc9fe: nothing to show -- the idle time runs (FADD in the FPU, stored as
    // float).
    if (G_StoryStarPending.size() == 0 && G_StoryStarRankUp == 0)
    {
        G_StoryStarIdleTime =
            static_cast<float>(GetClientFrameTimeCapped() + static_cast<double>(G_StoryStarIdleTime));
        return;
    }

    // 0x004bc9ff..0x004bca98: wait while a NeuroNet screen is up, the local server is held, a
    // panel or a NavBar service window is open, the cursor is hidden, or one of the four windows
    // is showing.
    bool bCanShow = true;
    if (G_bNnScreenUp || ShouldSkipLocalServerTick())
        bCanShow = false;
    if (NnMainButtonHasActivePanel())
        return;
    if (NavBarHasServiceWindow()) // FUN_00442720
        return;
    if (GetClientMouseCursorDrawFlag() == 0) // FUN_0041dd90
        return;
    if (!bCanShow)
        return;
    if (StoryStarMissionMessageUp())
        return;
    if (Win3DFindByName("NN_Trade") != nullptr)
        return;
    if (Win3DFindByName("NN_Scanner") != nullptr)
        return;
    if (Win3DFindByName("NN_CommodityByBase") != nullptr)
        return;

    // 0x004bca9e..0x004bcb89: a pending rank-up goes to the FRONT of the queue as ids 0x3d3
    // with the rank, resolved, with the rank-up sound (slot 0x27).
    if (G_StoryStarRankUp != 0)
    {
        FmtStr RankUp;
        RankUp.begin_mad_lib(kIdsRankUp);
        RankUp.append_int(static_cast<unsigned>(G_StoryStarRankUp));
        RankUp.end_mad_lib();
        G_StoryStarRankUp = 0;
        G_StoryStarPending.push_front(RankUp);
        StoryStarResolveIds(&G_StoryStarPending.front());
        PlayOrRewindUiSoundSlot(0x27);
    }

    // 0x004bcb91..0x004bcbb5: the banner, idle time back to 0, the front objective shown.
    Win3D* const Window = WinFactoryCreate("MissionObjectiveWindow", "MissionObjectiveWindow",
                                           nullptr, nullptr);
    G_StoryStarIdleTime = 0.0f;
    if (Window == nullptr)
        return;
    static_cast<MissionObjectiveWindow*>(Window)->ShowFrontObjective();
}

// ---------------------------------------------------------------------------------------------
// MissionObjectiveWindow

MissionObjectiveWindow::MissionObjectiveWindow()
    : Elapsed(0.0f)
    , bHolding(0)
    , bFadingIn(1)
    , Alpha(0xff)
    , Pad333(0)
    , HoldTime(2.5f)
    , bInterrupted(0)
    , Text(nullptr)
{
    // FUN_004bd170 0x004bd1bc: DAT_00672400 = this. +0x33c is left to Create (the port zeroes it
    // above).
    Pad339[0] = 0;
    Pad339[1] = 0;
    Pad339[2] = 0;
    G_MissionObjectiveWindow = this;
}

MissionObjectiveWindow::~MissionObjectiveWindow()
{
    // FUN_004bb1e0: DAT_00672400 = 0; with nothing more pending the log icon flies into the
    // mission-log button (unless it already does); then FUN_0059fa50 as the base dtor.
    G_MissionObjectiveWindow = nullptr;
    if (G_StoryStarPending.size() == 0 && Win3DFindByName("MissionLogMoveToNNWindow") == nullptr)
    {
        WinFactoryCreate("MissionLogMoveToNNWindow", "MissionLogMoveToNNWindow", nullptr, nullptr);
    }
}

bool MissionObjectiveWindow::Create(Win3D* NewParent, void* Data)
{
    // FUN_004bb270 (RET 8).
    // 0x004bb274..0x004bb2bb: FUN_005a0e30("MissionObjectiveText", "MissionObjective",
    // {-0.516, 0.257, 0}, 0, 1.023, 0.03, 2.0, 0, 1, 0, 1) -> +0x33c.
    float const Position[3] = {NnFloatBits(0xbf041893), NnFloatBits(0x3e83126f), 0.0f};
    Text = static_cast<NnTextControl*>(CreateTextControl(
        "MissionObjectiveText", "MissionObjective", Position, nullptr, NnFloatBits(0x3f82f1aa),
        NnFloatBits(0x3cf5c28f), 2.0f, 0, 1, 0, 1));

    // 0x004bb2c1..0x004bb2d1: the text's Font::String goes multi-line. No null test.
    FFontString* const Font = Text->GetFontString();
    if (Font->bMultiLine != 1)
    {
        Font->bMultiLine = 1;
        Font->LayoutGen = 0;
    }

    // 0x004bb2d8..0x004bb2f5: the banner sound, unless it announces a rank-up. The only maker
    // (FUN_004bc990) creates it with the pending list non-empty; the `&front + 8` null test at
    // 0x004bb2e3 is dead.
    if (G_StoryStarPending.front().MadLibId != kIdsRankUp)
        PlayOrRewindUiSoundSlot(0xf);

    // 0x004bb2f8..0x004bb32d: after more than half a second idle the log icon flies back out
    // of the button (unless it already does).
    if (G_StoryStarIdleTime > 0.5f &&
        Win3DFindByName("MissionLogMoveFromNNWindow") == nullptr)
    {
        WinFactoryCreate("MissionLogMoveFromNNWindow", "MissionLogMoveFromNNWindow", nullptr,
                         nullptr);
    }

    // 0x004bb330..0x004bb37b: slot 31 SetOffset(0, 0, -1), then FUN_004b5060.
    SetOffset(0.0f, 0.0f, -1.0f);
    return Win3D::Create(NewParent, Data);
}

void MissionObjectiveWindow::Update()
{
    // LAB_004bb390.
    // 0x004bb398..0x004bb457: with the HUD not picking (FUN_0045a740) or the local server held
    // (FUN_0042d740) and no mission message: a banner still fading in waits; otherwise it goes
    // (its objective popped unless it was interrupted).
    if (HudIsMousePickBlocked() || ShouldSkipLocalServerTick())
    {
        if (!StoryStarMissionMessageUp())
        {
            if (bFadingIn != 0)
                return;
            if (bInterrupted == 0)
                G_StoryStarPending.pop_front();
            Win3DDeleteTree(this);
            return;
        }
    }

    // 0x004bb458..0x004bb4eb: another NeuroNet window cuts a banner still in its first half
    // short -- straight to the fade-out, the objective kept.
    if (NnMainButtonHasActivePanel() || NavBarHasServiceWindow() ||
        StoryStarMissionMessageUp() || Win3DFindByName("NN_Trade") != nullptr ||
        Win3DFindByName("NN_Scanner") != nullptr ||
        Win3DFindByName("NN_CommodityByBase") != nullptr)
    {
        // TEST AH,0x41 / JNZ: taken when HoldTime * 0.5 <= Elapsed.
        if ((bHolding != 0 || bFadingIn != 0) && HoldTime * 0.5f > Elapsed)
        {
            bFadingIn = 0;
            bHolding = 0;
            Elapsed = 0.0f;
            bInterrupted = 1;
        }
    }

    // 0x004bb4f2..0x004bb530: the frame time, capped at 0.05f (FCOM double 0.05 / TEST AH,0x41
    // / JNZ: kept when not greater), doubled while fading in, added in the FPU, stored as float.
    double Step = GetClientFrameTimeCapped();
    if (Step > 0.05)
        Step = static_cast<double>(NnFloatBits(0x3d4ccccd)); // DAT_005cb88c 0.05f
    if (bFadingIn != 0)
        Step = Step + Step;
    Elapsed = static_cast<float>(Step + static_cast<double>(Elapsed));

    // 0x004bb536..0x004bb55d: holding -- until HoldTime is reached (TEST AH,5 / JNP: returns
    // when Elapsed < HoldTime), then into the fade-out with the overshoot.
    if (bHolding != 0)
    {
        if (Elapsed < HoldTime)
            return;
        bHolding = 0;
        Elapsed = Elapsed - HoldTime;
    }

    // 0x004bb563..0x004bb670: the alpha of DAT_00679bb0 (its top byte) scaled by the phase.
    unsigned long Colour = G_NnPaletteArgb_679bb0;
    unsigned char Bytes[4];
    memcpy(Bytes, &Colour, sizeof(Bytes));
    unsigned char const BaseAlpha = Bytes[3];
    unsigned char NewAlpha;
    if (bFadingIn != 0)
    {
        // 0x004bb57e..0x004bb5c0: past 1.0 the fade-in ends (the overshoot kept, holding
        // starts) at full alpha; otherwise alpha * Elapsed, truncated (_ftol).
        double Scale = static_cast<double>(Elapsed);
        if (Elapsed > 1.0f)
        {
            Elapsed = Elapsed - 1.0f;
            bFadingIn = 0;
            bHolding = 1;
            Scale = 1.0;
        }
        NewAlpha = static_cast<unsigned char>(static_cast<int>(static_cast<double>(BaseAlpha) * Scale));
    }
    else
    {
        // 0x004bb5c5..0x004bb637: past 1.0 the fade-out is over -- the objective popped unless
        // interrupted, and the banner goes.
        if (Elapsed > 1.0f)
        {
            if (bInterrupted == 0)
                G_StoryStarPending.pop_front();
            Win3DDeleteTree(this);
            return;
        }
        // 0x004bb638..0x004bb653: alpha - (char)ftol(alpha * Elapsed).
        NewAlpha = static_cast<unsigned char>(
            BaseAlpha - static_cast<unsigned char>(static_cast<int>(static_cast<double>(BaseAlpha) *
                                                                     static_cast<double>(Elapsed))));
    }

    // 0x004bb655..0x004bb670: +0x332 and the colour's alpha, then the text's property 0x33 with
    // the colour and 1.
    Bytes[3] = NewAlpha;
    memcpy(&Colour, Bytes, sizeof(Colour));
    Alpha = NewAlpha;
    Text->SetProperty(0x33, reinterpret_cast<uintptr_t>(&Colour), 1);
}

void MissionObjectiveWindow::Draw()
{
    // FUN_004bb680: with the HUD not picking or the local server held, drawn only under a
    // mission message; otherwise WinControl's draw (tail JMP FUN_005a0760).
    if ((HudIsMousePickBlocked() || ShouldSkipLocalServerTick()) && !StoryStarMissionMessageUp())
        return;
    WinControl::Draw();
}

void MissionObjectiveWindow::ShowFrontObjective()
{
    // FUN_004bb6b0 (thiscall). 0x004bb6b0..0x004bb6ba: nothing pending, nothing.
    if (G_StoryStarPending.size() == 0)
        return;

    // 0x004bb6c0..0x004bb6e1: the front objective is remembered unless it is ids 0x762.
    FmtStr const* const Front = &G_StoryStarPending.front();
    if (Front->MadLibId != kIdsNoLastObjective)
    {
        G_StoryStarLastObjective = *Front;
        G_bStoryStarHasLastObjective = 1;
    }

    // 0x004bb6e8..0x004bb73f: formatted (without 'M' values) into DAT_0066dc60, upper-cased
    // (CharUpperW, FUN_005b74d9), and copied into DAT_0066fc60 without its new lines.
    FormatFmtStrMadLib(Front, G_NnTextScratchA, G_NnTextScratchCapacity, false);
    CharUpperW(G_NnTextScratchA);
    int const Length = static_cast<int>(wcslen(G_NnTextScratchA));
    wchar_t* Out = G_NnTextScratchB;
    for (int Index = 0; Index < Length; ++Index)
    {
        wchar_t const Ch = G_NnTextScratchA[Index];
        if (Ch != L'\n')
        {
            *Out = Ch;
            ++Out;
        }
    }
    *Out = L'\0';

    // 0x004bb744..0x004bb753: the text's property 0x1d.
    Text->SetProperty(0x1d, reinterpret_cast<uintptr_t>(G_NnTextScratchB), 0);
}

// ---------------------------------------------------------------------------------------------
// NeuroNetStoryStar

NeuroNetStoryStar::NeuroNetStoryStar()
    : Field_0x32c(0)
    , CloseButton(nullptr)
    , HistoryText(nullptr)
    , HistoryShownCount(0)
    , LoadAutosaveButton(nullptr)
    , LoadSavegameButton(nullptr)
    , MainMenuButton(nullptr)
    , LoadAutosaveText(nullptr)
    , LoadSavegameText(nullptr)
    , MainMenuText(nullptr)
    , RespawnButton(nullptr)
    , NewPlayerButton(nullptr)
    , NewServerButton(nullptr)
    , RespawnText(nullptr)
    , NewPlayerText(nullptr)
    , NewServerText(nullptr)
    , MissionButton(nullptr)
    , MissionText(nullptr)
    , LogButton(nullptr)
    , LogText(nullptr)
    , bMissionMode(1)
    , SavedCursorDrawFlag(0)
    , MissionScrollY(0)
    , LogScrollY(0)
    , PendingScrollY(-1)
    , bPausedGame(0)
{
    // FUN_004bd030 0x004bd07c..0x004bd09b: DAT_006723fc = this; +0x338 = 0, +0x37c = 1, +0x36c
    // = 0, +0x374 = 0, +0x38c = 0. Retail leaves the other control pointers (and +0x32c,
    // +0x37d, +0x380..+0x388) to Create, which writes only the layout's own; SetProperty
    // compares the clicked control against all of them, so the port zeroes them rather than
    // compare against indeterminate values.
    Pad37e[0] = 0;
    Pad37e[1] = 0;
    Pad38d[0] = 0;
    Pad38d[1] = 0;
    Pad38d[2] = 0;
    G_NeuroNetStoryStar = this;
}

NeuroNetStoryStar::~NeuroNetStoryStar()
{
    // FUN_004bb780: DAT_006723fc = 0, DAT_0066d359 = 0; a game this window paused resumes
    // (FUN_004a7690 with the saved cursor flag) and DAT_0067dcc8 drops. Then FUN_0059fa50.
    G_NeuroNetStoryStar = nullptr;
    G_bNnKeepPanelOpen = 0;
    if (bPausedGame != 0)
    {
        NnModalResumeGame(SavedCursorDrawFlag);
        bPausedGame = 0;
        G_bNnScreenUp = false;
    }
}

bool NeuroNetStoryStar::Create(Win3D* NewParent, void* Data)
{
    // FUN_004bb810 (RET 8).
    // 0x004bb815..0x004bb83f: the NavBar collapses; the frame mesh (slot 38) and its half
    // extents (FUN_005a1340).
    NavBarCloseService(true); // FUN_004424f0(1)
    SetMesh(kStoryStarFrameMesh, 0);
    WinControlHalfExtentsFromMesh(HalfExtents, &MeshHolder);

    // 0x004bb844..0x004bb8d4: normally the close button; when NN_MainButton forces the log
    // (FUN_0045dcd0, no null test on DAT_0066d3f0) the game pauses instead.
    if (!G_NnMainButton->GetForceMissionLog())
    {
        float const ClosePosition[3] = {NnFloatBits(0x3ecf5c29), NnFloatBits(0x3e74bc6a), 0.0f};
        float const CloseExtents[3] = {NnFloatBits(0x3c9374bc), NnFloatBits(0x3c9374bc), 0.0f};
        CloseButton = CreateButtonControl("StoryStarCloseButton", kStoryStarCloseMesh, nullptr,
                                          kSelectSound, nullptr, ClosePosition, nullptr,
                                          CloseExtents, 0, 0, 0, 0, 0x409, 0, 0);
    }
    else
    {
        G_bNnScreenUp = true;
        SavedCursorDrawFlag = NnModalPauseGame(); // FUN_004a7670
        bPausedGame = 1;
    }

    // 0x004bb8db..0x004bb949: the title, ids 0x610 in single player, 0x62f in multiplayer.
    bool const bSinglePlayer = SinglePlayer();
    float const TitlePosition[3] = {NnFloatBits(0xbed70a3d), NnFloatBits(0x3e808312), 0.0f};
    CreateTextControl("StoryStarTitle", "PageHeader", TitlePosition, nullptr,
                      NnFloatBits(0x3f59db23), NnFloatBits(0x3cf5c28f), 2.0f,
                      bSinglePlayer ? 0x610u : 0x62fu, 1, 0, 1);

    // 0x004bb94e..0x004bb9c2: the history text, shorter when the log is forced in
    // multiplayer (room for three button rows).
    float HistoryHeight = NnFloatBits(0x3ebc6a7f);
    if (G_NnMainButton->GetForceMissionLog() && !SinglePlayer())
        HistoryHeight = NnFloatBits(0x3e9e353f);
    float const HistoryOffset[3] = {NnFloatBits(0xbec7ae14), NnFloatBits(0x3e4ccccd), 0.0f};
    HistoryText = CreateRichTextWin("StoryStarHistoryText", NnFloatBits(0x3f451eb8),
                                    HistoryHeight, HistoryOffset, true);
    MissionScrollY = 0;
    LogScrollY = 0;
    PendingScrollY = -1;

    if (G_NnMainButton->GetForceMissionLog())
    {
        // 0x004bb9df..0x004bbdaa: the player died -- DAT_0066d359 = 1 and the choice buttons.
        G_bNnKeepPanelOpen = 1;
        float const Extents[3] = {NnFloatBits(0x3e083127), NnFloatBits(0x3cd4fdf4), 0.0f};
        if (SinglePlayer())
        {
            float const AutosavePosition[3] = {NnFloatBits(0xbe8e5604), NnFloatBits(0xbe570a3d), 0.0f};
            LoadAutosaveButton = CreateWireButtonControl("LoadAutosaveButton", kSelectSound,
                                                         AutosavePosition, Extents, 0, 0, 0x320,
                                                         0, 0);
            float const SavegamePosition[3] = {NnFloatBits(0x3e8c49ba), NnFloatBits(0xbe570a3d), 0.0f};
            LoadSavegameButton = CreateWireButtonControl("LoadSavegameButton", kSelectSound,
                                                         SavegamePosition, Extents, 0, 0, 0x321,
                                                         0, 0);
        }
        float const MainMenuPosition[3] = {NnFloatBits(0xba83126f), NnFloatBits(0xbe570a3d), 0.0f};
        MainMenuButton = CreateWireButtonControl("MainMenuButton", kSelectSound, MainMenuPosition,
                                                 Extents, 0, 0, 0x322, 0, 0);
        if (!SinglePlayer())
        {
            float const RespawnPosition[3] = {NnFloatBits(0xbe8e5604), NnFloatBits(0xbe178d50), 0.0f};
            RespawnButton = CreateWireButtonControl("RespawnButtonButton", kSelectSound,
                                                    RespawnPosition, Extents, 0, 0, 0x5d0, 0, 0);
            float const NewPlayerPosition[3] = {NnFloatBits(0xba83126f), NnFloatBits(0xbe178d50), 0.0f};
            NewPlayerButton = CreateWireButtonControl("NewPlayerButtonButton", kSelectSound,
                                                      NewPlayerPosition, Extents, 0, 0, 0x5d1, 0,
                                                      0);
            float const NewServerPosition[3] = {NnFloatBits(0x3e8c49ba), NnFloatBits(0xbe178d50), 0.0f};
            NewServerButton = CreateWireButtonControl("NewServerButtonButton", kSelectSound,
                                                      NewServerPosition, Extents, 0, 0, 0x5d2, 0,
                                                      0);
        }

        float const TextWidth = NnFloatBits(0x3e8624dd);
        float const TextHeight = NnFloatBits(0x3cf5c28f);
        float const TextArg6 = NnFloatBits(0x3ff33333);
        if (SinglePlayer())
        {
            float const AutosaveTextPosition[3] = {NnFloatBits(0xbed16873), NnFloatBits(0xbe428f5c), 0.0f};
            LoadAutosaveText = CreateTextControl("LoadAutosaveText", kButtonTextStyle,
                                                 AutosaveTextPosition, nullptr, TextWidth,
                                                 TextHeight, TextArg6, 0x652, 1, 0, 1);
            float const SavegameTextPosition[3] = {NnFloatBits(0x3e126e98), NnFloatBits(0xbe428f5c), 0.0f};
            LoadSavegameText = CreateTextControl("LoadSavegameText", kButtonTextStyle,
                                                 SavegameTextPosition, nullptr, TextWidth,
                                                 TextHeight, TextArg6, 0x653, 1, 0, 1);
        }
        float const MainMenuTextPosition[3] = {NnFloatBits(0xbe051eb8), NnFloatBits(0xbe428f5c), 0.0f};
        MainMenuText = CreateTextControl("MainMenuText", kButtonTextStyle, MainMenuTextPosition,
                                         nullptr, TextWidth, TextHeight, TextArg6, 0x655, 1, 0, 1);
        if (!SinglePlayer())
        {
            float const RespawnTextPosition[3] = {NnFloatBits(0xbed16873), NnFloatBits(0xbe03126f), 0.0f};
            RespawnText = CreateTextControl("RespawnTextText", kButtonTextStyle,
                                            RespawnTextPosition, nullptr, TextWidth, TextHeight,
                                            TextArg6, 0x751, 1, 0, 1);
            float const NewPlayerTextPosition[3] = {NnFloatBits(0xbe051eb8), NnFloatBits(0xbe03126f), 0.0f};
            NewPlayerText = CreateTextControl("NewPlayerTextText", kButtonTextStyle,
                                              NewPlayerTextPosition, nullptr, TextWidth,
                                              TextHeight, TextArg6, 0x5ce, 1, 0, 1);
            float const NewServerTextPosition[3] = {NnFloatBits(0x3e126e98), NnFloatBits(0xbe03126f), 0.0f};
            NewServerText = CreateTextControl("NewServerTextText", kButtonTextStyle,
                                              NewServerTextPosition, nullptr, TextWidth,
                                              TextHeight, TextArg6, 0x5cf, 1, 0, 1);
        }
    }
    else if (SinglePlayer())
    {
        // 0x004bbdaf..0x004bbf0f: single player -- the Mission / Log toggles.
        float const Extents[3] = {NnFloatBits(0x3e083127), NnFloatBits(0x3cd4fdf4), 0.0f};
        float const MissionPosition[3] = {NnFloatBits(0xbe4fdf3b), NnFloatBits(0xbe570a3d), 0.0f};
        MissionButton = CreateWireButtonControl("MissionButton", kSelectSound, MissionPosition,
                                                Extents, 0, 0, 0x323, 0, 0);
        float const MissionTextPosition[3] = {NnFloatBits(0xbeab020c), NnFloatBits(0xbe408312), 0.0f};
        MissionText = CreateTextControl("MissionText", kButtonTextStyle, MissionTextPosition,
                                        nullptr, NnFloatBits(0x3e883127), NnFloatBits(0x3cf5c28f),
                                        2.0f, 0x628, 1, 0, 1);
        float const LogPosition[3] = {NnFloatBits(0x3e4dd2f2), NnFloatBits(0xbe570a3d), 0.0f};
        LogButton = CreateWireButtonControl("LogButton", kSelectSound, LogPosition, Extents, 0, 0,
                                            0x324, 0, 0);
        float const LogTextPosition[3] = {NnFloatBits(0x3d8b4396), NnFloatBits(0xbe408312), 0.0f};
        LogText = CreateTextControl("LogText", kButtonTextStyle, LogTextPosition, nullptr,
                                    NnFloatBits(0x3e89374c), NnFloatBits(0x3cf5c28f), 2.0f, 0x629,
                                    1, 0, 1);
    }

    // 0x004bbf15..0x004bbf42: single player with no best path and no forced log opens on the
    // log (+0x37c = 0 first); otherwise on the objectives.
    bool bMission = true;
    if (G_BestPaths.empty() && !G_NnMainButton->GetForceMissionLog() && SinglePlayer())
    {
        bMissionMode = 0;
        bMission = false;
    }
    SetMissionMode(bMission);

    // 0x004bbf47..0x004bbf9b: slot 31 SetOffset(0, 0, 0), hidden (flags &= 0xfc), FUN_004b5060.
    SetOffset(0.0f, 0.0f, 0.0f);
    Flags = static_cast<unsigned char>(Flags & 0xfc);
    return Win3D::Create(NewParent, Data);
}

void NeuroNetStoryStar::SetMissionMode(bool bMission)
{
    // FUN_004bbfb0 (RET 4). 0x004bbfb7..0x004bbfe6: the current page's scroll (the Y out of
    // FUN_00579970) is kept in +0x380 (objectives) or +0x384 (log).
    int ScrollX = 0;
    int ScrollY = 0;
    HistoryText->GetScrollOrigin(&ScrollX, &ScrollY);
    if (bMissionMode != 0)
        MissionScrollY = ScrollY;
    else
        LogScrollY = ScrollY;

    // 0x004bbfec..0x004bc00e: the new page, and its scroll for the next Draw.
    bMissionMode = static_cast<unsigned char>(bMission ? 1 : 0);
    PendingScrollY = bMission ? MissionScrollY : LogScrollY;

    // 0x004bc014..0x004bc075: with both toggles, the chosen one highlighted (property 0xc).
    if (MissionButton != nullptr && LogButton != nullptr)
    {
        uintptr_t const bOn = bMission ? 1u : 0u;
        uintptr_t const bOff = bMission ? 0u : 1u;
        MissionButton->SetProperty(0xc, bOn, 0);
        MissionText->SetProperty(0xc, bOn, 0);
        LogButton->SetProperty(0xc, bOff, 0);
        LogText->SetProperty(0xc, bOff, 0);
    }

    // 0x004bc07c
    RebuildHistoryText();
}

bool NeuroNetStoryStar::Slot23(unsigned Msg, unsigned A, unsigned B)
{
    // LAB_004bc090 (RET 0xc). Neither message reaches WinControl's slot 23.
    (void)A;
    (void)B;
    if (Msg == 0xef01)
    {
        // 0x004bc09e..0x004bc0b5: onto the parent's position (slot 5), shown.
        if (Parent != nullptr)
            SetPosition(Parent->Position);
        Flags = static_cast<unsigned char>(Flags | 3);
        return true;
    }
    if (Msg == 0xef02)
    {
        // 0x004bc0bf..0x004bc0fb: unlinked (FUN_0059dd00(this, 0)) and deleted with the subtree.
        Win3DReparent(this, nullptr);
        Win3DDeleteTree(this);
        return true;
    }
    return false;
}

intptr_t NeuroNetStoryStar::SetProperty(int Msg, uintptr_t A, uintptr_t B)
{
    // LAB_004bc110 (RET 0xc). Only 0x26 (a click, A = the control) is handled; everything else,
    // an unknown control included, returns 0 without reaching WinControl's slot 42.
    (void)B;
    if (Msg != 0x26)
        return 0;
    WinControl* const Clicked = reinterpret_cast<WinControl*>(A);

    if (Clicked == CloseButton)
    {
        // 0x004bc12a: NN_MainButton's panel closes (FUN_0045a460), the NavBar collapses.
        NnMainButtonOnModalOpen();
        NavBarCloseService(true); // FUN_004424f0(1)
        return 1;
    }
    if (Clicked == MissionButton)
    {
        SetMissionMode(true);
        return 1;
    }
    if (Clicked == LogButton)
    {
        SetMissionMode(false);
        return 1;
    }
    if (Clicked == LoadAutosaveButton)
    {
        NnMainButtonOnModalOpen();
        NavBarCloseService(true);
        LoadAutosave(); // FUN_004bc830
        return 1;
    }
    if (Clicked == LoadSavegameButton)
    {
        NnMainButtonOnModalOpen();
        NavBarCloseService(true);
        OpenNnLoadSave(3); // FUN_00488970(3)
        return 1;
    }
    if (Clicked == MainMenuButton)
    {
        NnMainButtonOnModalOpen();
        NavBarCloseService(true);
        SetClientQuitRequested(true); // FUN_005b2c50: DAT_0067ecbd = 1
        return 1;
    }
    if (Clicked == RespawnButton)
    {
        // 0x004bc1f1..0x004bc21f: DAT_0066d35a = 1, intro mode 4, USER_EXIT_GAME (0xc5).
        NnMainButtonOnModalOpen();
        NavBarCloseService(true);
        G_bNnStoryStarKeepPending = 1;
        MenuNavSetIntroModeRequest(4); // FUN_00564b10(4)
        HudPostCommand(0xc5, 0);
        return 1;
    }
    if (Clicked == NewPlayerButton)
    {
        NnMainButtonOnModalOpen();
        NavBarCloseService(true);
        MenuNavSetIntroModeRequest(2);
        HudPostCommand(0xc5, 0);
        return 1;
    }
    if (Clicked == NewServerButton)
    {
        // 0x004bc25e..0x004bc285: the server link goes first (FUN_005a8260).
        NnMainButtonOnModalOpen();
        NavBarCloseService(true);
        ShutdownServerConnection();
        MenuNavSetIntroModeRequest(3);
        HudPostCommand(0xc5, 0);
        return 1;
    }
    return 0;
}

void NeuroNetStoryStar::RebuildHistoryText()
{
    // FUN_004bc2a0 (thiscall).
    // 0x004bc2bd..0x004bc355: the text cleared (FUN_0057b370), a local list opened with
    // StyleNode(0x9ca4).
    RichTextWin* const Target = HistoryText;
    Target->ClearContent();
    RenderDisplayList Rdl;
    {
        StyleNode Style(0x9ca4);
        AppendNodeClone(Rdl, Style);
    }

    // 0x004bc35b..0x004bc37c: the count shown is remembered; an empty history reads ids 0x40f
    // (FUN_0057da40, nothing stripped).
    HistoryShownCount = static_cast<int>(G_StoryStarHistory.size());
    if (G_StoryStarHistory.size() == 0)
    {
        LoadIdsIntoRdl(0x40f, Rdl);
    }
    else
    {
        bool bAnyShown = false;       // [ESP+0xe]
        bool bCurrentFound = false;   // [ESP+0xf]
        for (std::list<FmtStr>::iterator It = G_StoryStarHistory.begin();
             It != G_StoryStarHistory.end(); ++It)
        {
            // 0x004bc3a0..0x004bc3c5: the entry formatted with 'M' values into DAT_0066fc60.
            FmtStr* const Entry = &*It;
            FormatFmtStrMadLib(Entry, G_NnTextScratchB, G_NnTextScratchCapacity, true);
            wchar_t const* Text = G_NnTextScratchB;

            if (bMissionMode != 0)
            {
                // 0x004bc3d3..0x004bc5a3: objectives only (log lines skipped). The first one
                // matching a waypoint of the current best path is highlighted (StyleNode
                // 0x3d0a), every other one plain (StyleNode 0x9ca4).
                if (G_NnTextScratchB[0] == L'*')
                    continue;
                bool bMatch = false;
                if (!bCurrentFound)
                {
                    int const WaypointCount = BestPathGetWaypointCount(); // FUN_004c4fb0
                    for (int Index = 0; Index < WaypointCount; ++Index)
                    {
                        FBestPathWaypoint* const Waypoint = BestPathGetWaypoint(Index); // FUN_004c4ff0
                        if (Waypoint == nullptr)
                            continue;
                        // 0x004bc40f..0x004bc461: the waypoint's text resolved, and both
                        // formatted without 'M' values into the two halves of DAT_0066dc60.
                        FmtStr Marker(Waypoint->Marker);
                        StoryStarResolveIds(&Marker);
                        int const Half = G_NnTextScratchCapacity / 2;
                        FormatFmtStrMadLib(&Marker, G_NnTextScratchA, Half, false);
                        wchar_t* const Second = G_NnTextScratchA + Half;
                        FormatFmtStrMadLib(Entry, Second, Half, false);
                        if (wcscmp(Second, G_NnTextScratchA) == 0)
                        {
                            bMatch = true;
                            break;
                        }
                    }
                }
                if (bMatch)
                {
                    bCurrentFound = true;
                    StyleNode Style(0x3d0a);
                    AppendNodeClone(Rdl, Style);
                }
                else
                {
                    StyleNode Style(0x9ca4);
                    AppendNodeClone(Rdl, Style);
                }
            }
            else
            {
                // 0x004bc5a5..0x004bc5b3: log lines only, without their '*'.
                if (G_NnTextScratchB[0] != L'*')
                    continue;
                Text = G_NnTextScratchB + 1;
            }

            // 0x004bc5bb..0x004bc5f6: a new line between entries.
            if (bAnyShown)
            {
                wcscpy(G_NnTextScratchA, L"\n"); // DAT_005d0a08
                AppendTextMaybeXmlRdl(G_NnTextScratchA,
                                      static_cast<unsigned>(wcslen(G_NnTextScratchA)), Rdl, false);
            }

            // 0x004bc5f9..0x004bc6a1: ids 0x723 entries are highlighted and not prefixed.
            bool bHeading = false;
            if (Entry->MadLibId == kIdsLogHeading)
            {
                bHeading = true;
                StyleNode Style(0x3d0a);
                AppendNodeClone(Rdl, Style);
            }

            // 0x004bc6a7..0x004bc78f: a text without ':' gets a prefix -- objectives ids 0x626
            // (0x3db for the first one while the log is forced), log lines ids 0x63c.
            if (wcschr(G_NnTextScratchB, L':') == nullptr && !bHeading)
            {
                unsigned Prefix = 0x63c;
                if (bMissionMode != 0)
                {
                    if (G_NnMainButton->GetForceMissionLog() && !bAnyShown)
                        Prefix = 0x3db;
                    else
                        Prefix = 0x626;
                }
                GetIdsStringWWithRawFallback(Prefix, G_NnTextScratchA, G_NnTextScratchCapacity);
                wcscat(G_NnTextScratchA, Text);
                AppendTextMaybeXmlRdl(G_NnTextScratchA,
                                      static_cast<unsigned>(wcslen(G_NnTextScratchA)), Rdl, false);
            }
            else
            {
                AppendTextMaybeXmlRdl(Text, static_cast<unsigned>(wcslen(Text)), Rdl, false);
            }
            bAnyShown = true;
        }
    }

    // 0x004bc7b6..0x004bc80c: the list into the text (FUN_0057b3e0); the local list freed.
    Target->AppendContent(Rdl);
}

void NeuroNetStoryStar::LoadAutosave()
{
    // FUN_004bc830 (thiscall; `this` unused).
    if (!SinglePlayer())
    {
        // 0x004bc917..0x004bc92a: multiplayer -- intro mode 4 and USER_EXIT_GAME.
        MenuNavSetIntroModeRequest(4);
        HudPostCommand(0xc5, 0);
        return;
    }

    // 0x004bc849..0x004bc8c1: the character list (FUN_005aa680 == 1), then the first record
    // flagged as the auto-save (+0x201) copied out.
    CHARACTER_ID CharId;
    if (G_LoginMgr.RequestCharacterInfo() == 1)
    {
        int Count = 0;
        MetaNode<CCharacterSet::CharacterInfo>* Node = nullptr;
        while (G_LoginMgr.Characters.traverse(Node, true))
            Count = Count + 1;
        for (int Index = 0; Index < Count; ++Index)
        {
            CCharacterSet::CharacterInfo* const Info = G_LoginMgr.CharacterAt(Index); // FUN_005aaca0
            if (Info->bIsAutoSave != 0)
            {
                CharId = Info->Id;
                break;
            }
        }
    }
    else
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: %s", "E:\\FL\\Scratch\\Source\\Client\\nn\\NN_StoryStar.cpp",
              0x6d1, "Failed to retrieve available save games\n");
    }

    // 0x004bc8c4..0x004bc8dd: found -- reload it (FUN_005b2c70 with 1).
    if (CharId.Buffer[0] != 0)
    {
        QueueLoadGameCharacter(&CharId, true);
        return;
    }
    // 0x004bc8e4..0x004bc908: not found -- the load/save screen instead.
    FDUMP(0x100001, "%s(%d) : *** ERROR: %s", "E:\\FL\\Scratch\\Source\\Client\\nn\\NN_StoryStar.cpp",
          0x6db, "Failed to find character to reload\n");
    OpenNnLoadSave(3);
}

void NeuroNetStoryStar::Draw()
{
    // FUN_004bc940: WinControl's draw, then a pending scroll applied (FUN_005798b0(0, y)).
    WinControl::Draw();
    if (PendingScrollY >= 0)
    {
        HistoryText->SetScrollOrigin(0, PendingScrollY);
        PendingScrollY = -1;
    }
}

void NeuroNetStoryStar::Update()
{
    // FUN_004bc970: the base update (FUN_005a17b0), then a rebuild when the history changed
    // size (tail JMP FUN_004bc2a0).
    Win3D::Update();
    if (HistoryShownCount != static_cast<int>(G_StoryStarHistory.size()))
        RebuildHistoryText();
}

// ---------------------------------------------------------------------------------------------
// MissionLogMoveToNNWindow / MissionLogMoveFromNNWindow

MissionLogMoveToNNWindow::MissionLogMoveToNNWindow()
    : Icon(nullptr)
{
    // FUN_004bd2c0 0x004bd2fc..0x004bd31f: FUN_0059f860, FUN_0055dec0, the vptrs. +0x34c is
    // left to Create (the port zeroes it above).
}

MissionLogMoveToNNWindow::~MissionLogMoveToNNWindow()
{
    // FUN_004bd510: the mover's entry vector freed (the port's std::vector member), then
    // FUN_0059fa50.
}

bool MissionLogMoveToNNWindow::Create(Win3D* NewParent, void* Data)
{
    // FUN_004bcd30 (RET 8).
    // 0x004bcd35..0x004bcda5: FUN_005a0c90("DummyMissionLog", icon mesh, 0, 0, 0,
    // {0, 0.29, -1.25}, 0, {0, 0, 0}, 0, 0, 0, 0, 0, 0, 0) -> +0x34c.
    float const Start[3] = {0.0f, NnFloatBits(0x3e947ae1), NnFloatBits(0xbfa00000)};
    float const Zero[3] = {0.0f, 0.0f, 0.0f};
    Icon = CreateButtonControl("DummyMissionLog", kStoryStarIconMesh, nullptr, nullptr, nullptr,
                               Start, nullptr, Zero, 0, 0, 0, 0, 0, 0, 0);

    // 0x004bcd98..0x004bcdf3: the mover over 0.3 s (FUN_0055e120), the icon from the middle to
    // the mission-log button {0.408, 0.459, -1.25} (FUN_0055dee0).
    float const End[3] = {NnFloatBits(0x3ed0e561), NnFloatBits(0x3eeb020c), NnFloatBits(0xbfa00000)};
    ButtonMover::Start(NnFloatBits(0x3e99999a));
    AddButton(Icon, Start, End);

    // 0x004bcdf8..0x004bce44: slot 31 SetOffset(0, 0, 0), then FUN_004b5060.
    SetOffset(0.0f, 0.0f, 0.0f);
    return Win3D::Create(NewParent, Data);
}

void MissionLogMoveToNNWindow::Update()
{
    // FUN_004bce50: once the slide is over (FUN_0055e280) the window deletes itself with its
    // subtree; otherwise the base update (FUN_005a17b0) and the mover (tail JMP FUN_0055e170).
    if (!ButtonMover::IsActive())
    {
        Win3DDeleteTree(this);
        return;
    }
    Win3D::Update();
    ButtonMover::Update();
}

MissionLogMoveFromNNWindow::MissionLogMoveFromNNWindow()
{
    // FUN_004bd3f0 0x004bd42c..0x004bd44f: the same construction with the FromNN vptrs.
}

bool MissionLogMoveFromNNWindow::Create(Win3D* NewParent, void* Data)
{
    // FUN_004bceb0 (RET 8). 0x004bceb5..0x004bcf25: the icon made at the button
    // {0.408, 0.459, -1.25} -> +0x34c.
    float const Start[3] = {NnFloatBits(0x3ed0e561), NnFloatBits(0x3eeb020c), NnFloatBits(0xbfa00000)};
    float const Zero[3] = {0.0f, 0.0f, 0.0f};
    Icon = CreateButtonControl("DummyMissionLog", kStoryStarIconMesh, nullptr, nullptr, nullptr,
                               Start, nullptr, Zero, 0, 0, 0, 0, 0, 0, 0);

    // 0x004bcf18..0x004bcf5b: over 0.2 s back to the middle {0, 0.29, -1.25}.
    float const End[3] = {0.0f, NnFloatBits(0x3e947ae1), NnFloatBits(0xbfa00000)};
    ButtonMover::Start(NnFloatBits(0x3e4ccccd));
    AddButton(Icon, Start, End);

    // 0x004bcf60..0x004bcfac
    SetOffset(0.0f, 0.0f, 0.0f);
    return Win3D::Create(NewParent, Data);
}
