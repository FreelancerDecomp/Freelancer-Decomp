// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// The NeuroNet player-information sheet (retail NN_PlayerInfo, freelancer.exe 0x004a7800..
// 0x004a9230; shares NN_Help's CHAR_infowindow.cmp mesh): "PLAYER STATUS", the player's
// statistics on the right (level, worth, missions, kills by ship class, play time, visits) and
// six rows of the factions the player has met on the left, each with its reputation bar; a
// click on a row replaces the statistics with that faction's infocard. Opened by the NeuroNet
// main button (panel 0x65, nn_main_button.cpp) through the "NN_PlayerInfo" factory.
//
// RTTI .?AVNN_PlayerInfo@@ : WinControl. operator_new(0x428) in the factory's Create
// FUN_004a9100, which inlines the ctor: WinControl's FUN_0059f860, the payload's low byte at
// +0x40c, the empty faction vector, vftable 0x005d4794, the IDataViewOwner face +0x7c its own
// copy (0x005d4788) of WinControl's two slots, FirstRow = 0, the visit flags 0x41 of the
// "li_p_grp" reputation group (FUN_004c4e00, told to the server) and DAT_006721bc = this.
// WINFACTORY<NN_PlayerInfo> DAT_00672180 "NN_PlayerInfo": static init LAB_004a7820 (ctor
// FUN_004a9090 -- the WinFactory head, FUN_0059dab0, vftable 0x005d49dc -- then atexit
// LAB_004a7840 -> FUN_00579380).
//
// vftable 0x005d4794 -- WinControl's (0x005e5e44) with these overrides (raw dword diff):
//   0 FUN_004a7850 scalar deleting dtor (dtor FUN_004a7870), 9 FUN_004a78c0 Create,
//   13 LAB_004a8200 Draw, 23 LAB_00475430 Slot23 (the body NN_Help's slot 23 shares by ICF),
//   42 LAB_004a80e0 SetProperty.

#include "win_factory.h"
#include "wincontrol_base.h"

#include <stddef.h>
#include <stdint.h>
#include <vector>

class RichTextWin;
class UiScroll;

// The REQUEST_PLAYER_STATS block (IClient slot 94, LAB_0053c3d0): this 0x34-byte head, then
// ShipKillCount FNnPlayerStatsShipKills, then ReputationCount FPlayerStatsRepEntry (nn_dealer.h).
// The field names are the headings NN_PlayerInfo shows each value under (resources.dll ids).
struct FNnPlayerStatsBlock
{
    int TotalMissions;          // +0x00 -- ids 0x64b "Total Missions:"
    int Field_0x04;             // +0x04 TODO: not read by NN_PlayerInfo or NN_Dealer
    int Field_0x08;             // +0x08 TODO: not read by NN_PlayerInfo or NN_Dealer
    int Field_0x0c;             // +0x0c TODO: not read by NN_PlayerInfo or NN_Dealer
    float TotalTimeSeconds;     // +0x10 -- ids 0x646 "Total Time:"
    int SystemsVisited;         // +0x14 -- ids 0x647
    int BasesVisited;           // +0x18 -- ids 0x648
    int JumpHolesFound;         // +0x1c -- ids 0x649
    int ShipKillCount;          // +0x20 -- entries of the first table
    int Level;                  // +0x24 -- ids 0x61d "Current Level:"
    int Worth;                  // +0x28 -- ids 0x61e "Current Worth:"
    int NextLevelRequirement;   // +0x2c -- ids 0x61f; 0x7fffffff = max level, <= 0 = mission
    int ReputationCount;        // +0x30 -- entries of the second table
};
static_assert(sizeof(FNnPlayerStatsBlock) == 0x34, "player-stats block head is 0x34 bytes");

// One entry of the block's first table: kills per ship archetype.
struct FNnPlayerStatsShipKills
{
    unsigned ShipArchId; // +0x00 -- Archetype::GetShip
    int Count;           // +0x04
};
static_assert(sizeof(FNnPlayerStatsShipKills) == 8, "ship-kill entry is 8 bytes");

struct FPlayerStatsRepEntry; // nn_dealer.h

// One faction row (the 8-byte element of the vector at +0x410).
struct FNnPlayerInfoFaction
{
    unsigned GroupId; // +0x00
    float Attitude;   // +0x04
};

class NnPlayerInfo : public WinControl
{
public:
    // The ctor FUN_004a9100 inlines (0x004a9143..0x004a91d6); PayloadByte is the factory
    // payload's low byte.
    explicit NnPlayerInfo(unsigned char PayloadByte);
    // FUN_004a7870 -- DAT_006721bc = 0 (no test that it is this one), the vector freed, then
    // WinControl's dtor FUN_0059fa50.
    ~NnPlayerInfo() override;

    // slot 9 FUN_004a78c0 (RET 8) -- the mesh, the close button, the titles, the infocard (hidden)
    // and its close button / text (hidden), six faction rows (frame, name, two bars), the scroll
    // and the thirteen heading / value pairs; Worth and the next-level line hidden outside
    // single player. Then Count = 0, SetOffset(0, 0, 0), SetPosition(Position), the stats timer
    // 1.0 and Win3D's Create.
    bool Create(Win3D* NewParent, void* Data) override;
    // slot 13 LAB_004a8200 -- with the HUD option on: fill the six rows from FirstRow, join the
    // frame column, WinControl's Draw, a darkening quad over each visible row's reputation bar,
    // then once a second (+0x424) ask the server for fresh stats (IServer slot 73).
    void Draw() override;
    // slot 23 LAB_00475430 (RET 0xc) -- 0xef02 removes the window from its parent and deletes it
    // (every child first); anything else is WinControl's Slot23.
    bool Slot23(unsigned Msg, unsigned A, unsigned B) override;
    // slot 42 LAB_004a80e0 (RET 0xc) -- 0x3e from the scroll: FirstRow = B. 0x26: the close
    // button closes the NeuroNet screen; the infocard's close button restores the statistics;
    // a faction frame opens that faction's infocard. Everything else returns 0.
    intptr_t SetProperty(int Msg, uintptr_t A, uintptr_t B) override;

    // FUN_004a8820 (thiscall, RET 0xc) -- the statistics texts from Stats and ShipKills, then the
    // faction list rebuilt from Reputations: every group the player has visit bit 0 for (the
    // visit map, FUN_004c4da0) that is not a story faction; sorted, and the scroll's range set.
    void OnPlayerStats(FNnPlayerStatsBlock const* Stats, FNnPlayerStatsShipKills const* ShipKills,
                       FPlayerStatsRepEntry const* Reputations);
    // FUN_004a8750 (thiscall) -- bubble passes over the faction rows until one swaps nothing,
    // ascending by attitude, the player's own affiliation (multiplayer) counted 10 higher.
    void SortFactions();
    // FUN_004a8e80 (thiscall, RET 4) -- the infocard of the faction in Row (not tested against
    // the count): the title becomes its name, the statistics are hidden, the card and its close
    // button shown. Nothing without rows or without an infocard.
    void ShowFactionInfocard(int Row);

    WinControl* CloseButton;                 // +0x32c "PlayerInfoCloseButton"
    WinControl* PlayerStatsTitle;            // +0x330 "PlayerStatsTitle"
    WinControl* FactionFrames[6];            // +0x334 "PlayerInfoFactionFrame%d"
    WinControl* FactionNames[6];             // +0x34c "PlayerInfoFactionName%d"
    WinControl* UnfriendlyBars[6];           // +0x364 "PlayerInfoUnfriendlyBar%d"
    WinControl* FriendlyBars[6];             // +0x37c "PlayerInfofriendlyBar%d"
    UiScroll* Scroll;                        // +0x394 "PlayerInfoScroll"
    WinControl* StatValues[13];              // +0x398 "PlayerInfoStat%d"
    WinControl* StatHeadings[13];            // +0x3cc "PlayerInfoHeading%d"
    RichTextWin* FactionInfoCard;            // +0x400 "FactionInfoCard"
    WinControl* CloseInfocardButton;         // +0x404 "NavMapCloseInfocardButton"
    WinControl* CloseInfocardText;           // +0x408 "NavMapCloseInfocardText"
    unsigned char Field_0x40c;               // +0x40c TODO: the factory payload's low byte;
                                             //        nothing in the PE reads a byte here
    std::vector<FNnPlayerInfoFaction> Factions; // +0x410 (retail begin / end / capacity)
    int FactionCount;                        // +0x41c -- kept next to the vector, as retail does
    int FirstRow;                            // +0x420 -- the scroll's value
    float StatsRequestTimer;                 // +0x424

private:
    NnPlayerInfo(NnPlayerInfo const&) = delete;
    NnPlayerInfo& operator=(NnPlayerInfo const&) = delete;
};

// Retail's offsets up to the vector; the port's std::vector is not retail's 12 bytes (no retail
// ABI), so the fields after it and the class size are not asserted.
static_assert(offsetof(NnPlayerInfo, CloseButton) == 0x32c, "NN_PlayerInfo +0x32c");
static_assert(offsetof(NnPlayerInfo, PlayerStatsTitle) == 0x330, "NN_PlayerInfo +0x330");
static_assert(offsetof(NnPlayerInfo, FactionFrames) == 0x334, "NN_PlayerInfo +0x334");
static_assert(offsetof(NnPlayerInfo, FactionNames) == 0x34c, "NN_PlayerInfo +0x34c");
static_assert(offsetof(NnPlayerInfo, UnfriendlyBars) == 0x364, "NN_PlayerInfo +0x364");
static_assert(offsetof(NnPlayerInfo, FriendlyBars) == 0x37c, "NN_PlayerInfo +0x37c");
static_assert(offsetof(NnPlayerInfo, Scroll) == 0x394, "NN_PlayerInfo +0x394");
static_assert(offsetof(NnPlayerInfo, StatValues) == 0x398, "NN_PlayerInfo +0x398");
static_assert(offsetof(NnPlayerInfo, StatHeadings) == 0x3cc, "NN_PlayerInfo +0x3cc");
static_assert(offsetof(NnPlayerInfo, FactionInfoCard) == 0x400, "NN_PlayerInfo +0x400");
static_assert(offsetof(NnPlayerInfo, CloseInfocardText) == 0x408, "NN_PlayerInfo +0x408");
static_assert(offsetof(NnPlayerInfo, Field_0x40c) == 0x40c, "NN_PlayerInfo +0x40c");

// WINFACTORY<NN_PlayerInfo> (DAT_00672180). Its one slot FUN_004a9100 is not the generic
// WINFACTORY<T>::Create: the ctor takes the payload's low byte.
class NnPlayerInfoFactory : public WinFactory
{
public:
    explicit NnPlayerInfoFactory(char const* Name)
        : WinFactory(Name)
    {
    }

    // FUN_004a9100 (RET 8): operator_new(0x428), the ctor, +0x10 = the factory's name (+0xc),
    // slot 9; on failure the scalar deleting dtor with 1 and null.
    Win3D* Create(Win3D* Parent, void* Payload) override;
};

// DAT_006721bc -- the open NN_PlayerInfo, or null. Written by its ctor (0x004a91d6) and dtor
// (0x004a7883); read only by FUN_004a8e40.
extern NnPlayerInfo* G_NnPlayerInfo;

// FUN_004a8e40 (cdecl, 3 args) -- REQUEST_PLAYER_STATS arrived (IClient slot 94, LAB_0053c3d0,
// with the block, its ship-kill table and its reputation table): an open NN_PlayerInfo takes
// all three (FUN_004a8820); otherwise NN_Dealer's FUN_00483f30 gets ([Stats+0x30], the
// reputation table).
void NnPlayerInfoOnPlayerStats(unsigned char const* Stats, void const* ShipKills,
                               void const* Reputations);
