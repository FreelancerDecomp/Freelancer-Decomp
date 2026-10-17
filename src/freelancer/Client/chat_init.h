// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// The client chat core -- retail `E:\FL\Scratch\Source\Client\nn\NN_Chat.cpp` (the FDUMP path
// of FUN_00463630 / FUN_0045f860 / FUN_0046a150). One set of heap objects, built by
// ChatInit (FUN_004690c0) and torn down by ChatShutdown (FUN_00469b30, from the atexit
// teardown 0x005b222d):
//
//   DAT_0066da60  FChatMessageStore*   -- the 0x44 message store (FUN_004699d0)
//   DAT_0066da64  FChatDisplaySnapshot* -- &store+0x18, every active message
//   DAT_0066da68  FChatDisplaySnapshot* -- &store+0x28, the recent (age/cap trimmed) ones
//   DAT_0066da70 / da74 / da78          -- three id -> name RDL caches (FUN_004c5a10)
//   DAT_0066da7c  FChatChannelTable*   -- the 0x2c channel / player-name table
//   DAT_0066da80  MsgWin*              -- the chat display window (FUN_0046b580 builds it)
//   DAT_0066da84  NnGroupControl*      -- NN_GroupControl (ChatInit builds it)
//   DAT_0066da88  bool                 -- "chat window wanted" (FUN_0046a4a0)
//
// Chat addresses are CHAT_IDs (common_server_types.h): a dword whose low word is a player id
// or channel number and whose byte 2 says "channel" (1) or "player" (0). Every comparison in
// the listing is a full dword CMP, so the port compares CHAT_ID::Id.

#include "common_server_types.h"
#include "common_text.h"

#include <functional>
#include <list>
#include <map>
#include <string>

class MsgWin;
class NnGroupControl;
class RichTextWin;

// ---------------------------------------------------------------------------------------------
// The static chat ids (CRT initializers 0x0045de20 .. 0x0045deb0). Byte 3 is .bss zero.
extern const CHAT_ID G_ChatIdChannel3;      // DAT_0066da38 {3, 1}
extern const CHAT_ID G_ChatIdChannel2;      // DAT_0066da3c {2, 1}
extern const CHAT_ID G_ChatIdChannel1;      // DAT_0066da40 {1, 1}
extern const CHAT_ID G_ChatIdChannel0;      // DAT_0066da44 {0, 1}
extern const CHAT_ID G_ChatIdNone;          // DAT_0066da48 {0, 0}
extern const CHAT_ID G_ChatIdGroupInvite;   // DAT_0066da34 {4, 1} -- the group wire channel

// FUN_00468c20 -- registers the compiled-in chat text styles with StyleCollection: 27 attribute
// styles from the static records DAT_0066d94c..DAT_0066da14 (four of them recoloured with the
// HUD chrome / pulse amber palette first) and the list style 0x4433 {TRANode, JustifyNode(1)}.
// Called once from InitializeEngine (0x005b1ca0).
void ChatRegisterTextStyles();

// DAT_0066d8e4 -- "Interface\NeuroNet\Chat\" (ChatInit assigns it).
extern std::string G_ChatImageRoot;

// DAT_0066d3fc -- the chat input's current target. ChatInit and the disconnect resets set it to
// G_ChatIdChannel1; ChatTextInputDisplay reads and writes it.
extern CHAT_ID G_ChatInputTarget;

// FUN_0046ad40 (thiscall on the id, RET 4) -- {PlayerId, 0, 0}.
inline CHAT_ID ChatIdForPlayer(unsigned PlayerId)
{
    return CHAT_ID(PlayerId & 0xffffu);
}

// ---------------------------------------------------------------------------------------------
// The channel / player-name table, DAT_0066da7c (0x2c bytes, built at 0x00469760):
//   +0x00 map<CHAT_ID, RenderDisplayList*> -- the name of every channel and player (ctor
//         FUN_004b6800; node 0x18: key +0x0c, value +0x10)
//   +0x14 generation -- bumped by every change; the chat input and NN_GroupControl poll it
//   +0x18 map<CHAT_ID, FChatPlayerInfo> -- per-player state (ctor FUN_0046ce80; value +0x10)
// Lookups are std::map::find (FUN_0046cd50 / the inlined lower_bound + key test).
struct FChatPlayerInfo
{
    // Node +0x10: MISCOBJUPDATE 0x0005 (IClient slot 118 -> FUN_0046ad60). TODO: meaning.
    unsigned Field_0x0;
    // Node +0x14: MISCOBJUPDATE 0x0044 (slot 117 -> FUN_0046add0) -- the player's rank.
    unsigned Rank;
    // Node +0x18: MISCOBJUPDATE 0x0084 (slot 119 -> FUN_0046ae40). TODO: meaning.
    unsigned Field_0x8;
};

class FChatChannelTable
{
public:
    // The name map's order: FUN_0046cd50 / FUN_0046d920 walk left while `node key > target`
    // (the VC6 lower_bound with a greater-than comparator), so the tree is ordered by DESCENDING
    // CHAT_ID::Id. ChatTextInputDisplay's Up / Down walks (FUN_0045e800 / FUN_0045e940) depend
    // on that order.
    typedef std::map<unsigned, RenderDisplayList*, std::greater<unsigned> > FNameMap;

    // 0x0046977c..0x004697a8: both maps empty, generation 0.
    FChatChannelTable();
    // FUN_00469e60
    ~FChatChannelTable();

    // FUN_0045f860 (thiscall, RET 8) -- a copy of Name under Id. A new id also gets a zeroed
    // FChatPlayerInfo; an existing one gets its name replaced and its info zeroed. Either way
    // the generation goes up and an FDUMP notice says which case it was.
    void AddChannel(CHAT_ID Id, RenderDisplayList const& Name);
    // FUN_0045fb40 -- drop every player entry (byte 2 == 0 and not G_ChatIdNone) with its info
    // and name; generation + 1.
    void RemovePlayerChannels();
    // The inlined find at every caller: the name RDL for Id, or null.
    RenderDisplayList* FindName(CHAT_ID Id) const;
    // FUN_0046e610-based lookups of the +0x18 map.
    FChatPlayerInfo* FindPlayerInfo(CHAT_ID Id);

    FNameMap Names;                                 // +0x00
    int Generation;                                 // +0x14
    std::map<unsigned, FChatPlayerInfo> PlayerInfo; // +0x18

private:
    FChatChannelTable(FChatChannelTable const&) = delete;
    FChatChannelTable& operator=(FChatChannelTable const&) = delete;
};

// ---------------------------------------------------------------------------------------------
// The message store, DAT_0066da60 (0x44 bytes, FUN_004699d0(0x20, 0x20, 10000)).
//   +0x00 list<entry*> active    +0x0c list<entry*> free (preloaded with Capacity entries)
//   +0x18 snapshot of every active message (DAT_0066da64)
//   +0x28 snapshot of the recent ones (DAT_0066da68)
//   +0x38 clock of the oldest message shown, +0x3c max age (ms), +0x40 max message count
// An entry is 0x10: RenderDisplayList + a second-rounded clock stamp at +0x0c.
struct FChatMessageEntry
{
    RenderDisplayList Body;     // +0x00
    int TimestampMs = 0;        // +0x0c
};

// +0x18 / +0x28: a generation followed by the RenderDisplayList it versions. The two DAT_
// pointers and MsgWin +0x498 point at the generation and reach the list at +4.
struct FChatDisplaySnapshot
{
    int Generation = 0;
    RenderDisplayList Display;
};

enum
{
    kChatMessageStoreCapacity = 0x20,   // FUN_004699d0 arg 0: entries preloaded into the pool
    kChatMessageStoreMaxCount = 0x20,   // arg 1 -> +0x40
    kChatMessageStoreMaxAgeMs = 10000   // arg 2 -> +0x3c
};

struct FChatMessageStore
{
    // FUN_004699d0 (thiscall, RET 0xc)
    FChatMessageStore(int Capacity, int InMaxCount, int InMaxAgeMs);
    // FUN_004688b0
    ~FChatMessageStore();

    // FUN_004683b0 -- the entry to write the next message into (see chat_init.cpp).
    FChatMessageEntry* Acquire();
    // FUN_0046a300 -- recycle the oldest active entry into the pool (no-op when none).
    void RecycleOldest();
    // FUN_004684e0 -- rebuild Recent from the newest messages within the age and count limits.
    void RebuildRecent();
    // FUN_004686e0 -- rebuild All from every active message.
    void RebuildAll();
    // The inlined "unlink this entry from the active list and return it to the pool" tail of
    // FUN_00463630 (0x00463706 / 0x00464c83) and FUN_0046a150 (0x0046a25e). False when Entry
    // is not on the active list.
    bool Discard(FChatMessageEntry* Entry);
    // FUN_0046b170 / FUN_0046b2b0 (inlined): every active entry cleared and pooled, then both
    // snapshots rebuilt.
    void DiscardAll();

    std::list<FChatMessageEntry*> Active;   // +0x00
    std::list<FChatMessageEntry*> Free;     // +0x0c
    FChatDisplaySnapshot All;               // +0x18
    FChatDisplaySnapshot Recent;            // +0x28
    int LastClockMs;                        // +0x38
    int MaxAgeMs;                           // +0x3c
    int MaxCount;                           // +0x40

private:
    FChatMessageStore(FChatMessageStore const&) = delete;
    FChatMessageStore& operator=(FChatMessageStore const&) = delete;
};

// ---------------------------------------------------------------------------------------------
// Globals (see the table at the top).
extern FChatMessageStore* G_ChatMessageStore;                 // DAT_0066da60
extern FChatDisplaySnapshot* G_ChatAllMessages;               // DAT_0066da64
extern FChatDisplaySnapshot* G_ChatRecentMessages;            // DAT_0066da68
extern std::map<unsigned, RenderDisplayList*>* G_ChatNameCache70; // DAT_0066da70
extern std::map<unsigned, RenderDisplayList*>* G_ChatNameCache74; // DAT_0066da74
extern std::map<unsigned, RenderDisplayList*>* G_ChatNameCache78; // DAT_0066da78
extern FChatChannelTable* G_ChatChannelTable;                 // DAT_0066da7c
extern MsgWin* G_ChatMsgWin;                                  // DAT_0066da80 (chat_msg_win.h)
extern NnGroupControl* G_ChatGroupControl;                    // DAT_0066da84
extern bool G_bChatMsgWinWanted;                              // DAT_0066da88

// The four "system notice" prefixes FUN_004690c0 builds (style 0x3ea8 + an IDS string, or the
// English debug text when the resource is missing).
extern RenderDisplayList G_ChatPrefixNewPlayer;        // DAT_0066d424, IDS 0x60052
extern RenderDisplayList G_ChatPrefixDepartingPlayer;  // DAT_0066d418, IDS 0x60053
extern RenderDisplayList G_ChatPrefixNewAffiliation;   // DAT_0066d40c, IDS 0x6004f
extern RenderDisplayList G_ChatPrefixLostAffiliation;  // DAT_0066d400, IDS 0x60050

// DAT_0066d440 -- the entry messages go to when there is no store (guarded static, dtor
// LAB_00468bf0). The store is created by ChatInit before anything can post, so this is the
// pre-init / post-shutdown sink.
FChatMessageEntry* ChatScratchEntry();

// The "acquire" idiom every poster inlines: the store's Acquire() or the scratch entry.
FChatMessageEntry* ChatAcquireEntry();

// The "commit" idiom every poster inlines after writing an entry: when there is a store,
// RebuildAll + RebuildRecent (FUN_004686e0 + FUN_004684e0).
void ChatCommitEntries();

// Appends a clone of every node of Source to Dst (the FUN_0046c800 copy loop the posters inline).
void ChatAppendRdlCopy(RenderDisplayList& Dst, RenderDisplayList const& Source);

// ---------------------------------------------------------------------------------------------
// FUN_004690c0 -- build everything above (called once from the pre-init tail).
void ChatInit();
// FUN_00469b30 -- tear it all down (atexit teardown 0x005b222d).
void ChatShutdown();

// FUN_0046a150 (cdecl) -- IChatClient slot 0: a chat message from Sender. The group channel goes
// to NN_GroupControl's wire handler; everything else is decoded into a new message entry.
void ChatDispatchIncoming(CHAT_ID Sender, void const* Buffer, unsigned SizeBytes);

// FUN_0046aa10 (cdecl) -- IClient slot 110: a player joined. Name becomes the player's channel
// name; with bNotify the "New player:" notice is posted.
void ChatPlayerJoined(wchar_t const* Name, unsigned PlayerId, bool bNotify);
// FUN_0046aeb0 (cdecl) -- IClient slot 111: a player left. With bNotify the "Departing player:"
// notice (with the player's name) is posted; the player's info and name are dropped.
void ChatPlayerLeft(unsigned PlayerId, bool bNotify);
// FUN_0046ad60 / FUN_0046add0 / FUN_0046ae40 (cdecl) -- set one FChatPlayerInfo field of an
// existing player; generation + 1. No-op for an unknown player.
void ChatSetPlayerInfoField0(unsigned PlayerId, unsigned Value);
void ChatSetPlayerRank(unsigned PlayerId, unsigned Rank);
void ChatSetPlayerInfoField8(unsigned PlayerId, unsigned Value);
// FUN_0046b400 (cdecl) -- the player's name RDL or null. No null test on the table (retail's
// caller, HUD 0x004e2d53, only runs with the chat built).
RenderDisplayList* ChatFindPlayerName(unsigned PlayerId);

// FUN_0046a4d0 / FUN_0046a770 (cdecl) -- post "Lost affiliation:" / "New affiliation:" + Name.
void ChatPostLostAffiliation(wchar_t const* Name);
void ChatPostNewAffiliation(wchar_t const* Name);

// FUN_0046a480 -- true when the chat input window ("CTID", 0x005cf04c) exists.
bool ChatIsInputOpen();
// FUN_0046a4a0 (cdecl) -- remember bShow (DAT_0066da88) and show / hide the MsgWin.
void ChatShowMsgWin(bool bShow);
// FUN_0046b650 -- destroy the MsgWin tree (caller 0x005739ef).
void ChatDestroyMsgWin();
// FUN_0046b450 (cdecl) -- NN_Trade moves the chat window: Mode 0 to the normal place, 1 to the
// trade layout. No-op without a MsgWin or for any other Mode.
void ChatPlaceMsgWinForTrade(int Mode);

// FUN_0046b500 / FUN_0046b520 (cdecl) -- the NN_GroupControl forwarders IClient slots 107,
// 108 and 101 use: a trade request from ShipId arrived / was withdrawn.
void ChatGroupTradeRequestReceived(unsigned ShipId);
void ChatGroupTradeRequestRemoved(unsigned ShipId);

// ---------------------------------------------------------------------------------------------
// The server's message of the day: RenderDisplayList static DAT_0066d430 (ctor 0x00468fb0, dtor
// 0x00468fd0 via atexit) and its change counter DAT_0066d43c.
// FUN_0046b0f0 (IClient slot 115): empty the list, FUN_0057db50(Text, wcslen(Text), &list, 0),
// ++counter. No null test on Text.
void ChatSetServerMotd(const wchar_t* Text);
// FUN_0046b0d0
RenderDisplayList* ChatGetServerMotdRdl();
// FUN_0046b0e0
int ChatGetServerMotdVersion();

// ---------------------------------------------------------------------------------------------
// The three id -> name caches (DAT_0066da70 / 74 / 78). Each lookup returns the cached list, or
// builds it: FUN_0057da40 on the object's name IDS (no trailing-paragraph strip), and when there
// is no object or no resource the list is emptied and holds TextPtrNode(u"????", -1). The map
// pointer is not tested (retail reads it straight after ChatInit).
// FUN_0046b6a0 -> FUN_00466770: Universe::get_system(Id), ISystem slot 3 (+0xc, strid_name).
RenderDisplayList* ChatGetSystemName(unsigned SystemId);
// FUN_0046b6c0 -> FUN_0046b6e0: Archetype::GetShip(Id), Root +0x14 (ids_name).
RenderDisplayList* ChatGetShipArchName(unsigned ShipArchId);
// FUN_0046b890 -> FUN_0046b8b0: Universe::get_base(Id), IBase vt+8 (strid_name).
RenderDisplayList* ChatGetBaseName(unsigned BaseId);

// FUN_0046a3c0 (cdecl) -- open (bOpen) or close the chat input line "CTID" (ChatTextInputDisplay).
void ChatSetInputOpen(bool bOpen);

// FUN_0046b170 / FUN_0046b2b0 (cdecl) -- multiplayer only: forget the group and its requests,
// drop every player name, hide the MsgWin, close the input line, discard every message and
// point the input line back at channel {1, 1}. The two bodies are identical apart from how
// the set's begin is fetched. Callers: 0x005a8098 / 0x005a8269 / 0x005a87e3 / 0x005a88d7
// (b170) and 0x0056ab93 / 0x005a894c (b2b0).
void ChatResetForDisconnect();
void ChatResetForConnect();
