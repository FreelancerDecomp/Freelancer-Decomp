// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// The client chat core (retail NN_Chat.cpp) -- see chat_init.h for the object map.
//
//   FUN_004690c0 ChatInit              FUN_00469b30 ChatShutdown
//   FUN_004699d0 / FUN_004688b0        the message store ctor / dtor
//   FUN_004683b0 / FUN_0046a300        Acquire / RecycleOldest
//   FUN_004684e0 / FUN_004686e0        RebuildRecent / RebuildAll
//   FUN_0045f860 / FUN_0045fb40 / FUN_00469e60   the channel table
//   FUN_00469d20                       the name-cache dtor
//   FUN_0046a150                       ChatDispatchIncoming
//   FUN_0046aa10 / FUN_0046aeb0 / FUN_0046ad60 / FUN_0046add0 / FUN_0046ae40 / FUN_0046b400
//   FUN_0046a4d0 / FUN_0046a770 / FUN_0046a480 / FUN_0046a4a0 / FUN_0046b650 / FUN_0046b450
//   FUN_0046b500 / FUN_0046b520
//   FUN_0046b0d0 / FUN_0046b0e0 / FUN_0046b0f0   the MOTD

#include "chat_init.h"
#include "event_handler.h"

#include "character_select_equip.h"   // GetPlayerSpawnSystemIdRetail (FUN_004c4670)
#include "chat_msg_win.h"
#include "chat_text_input.h"          // ChatTextInputDisplay ("CTID")
#include "hud_command_dispatch.h"     // HudRegisterCommandHandler / HudDestroyEventHandler
#include "ids_rdl.h"
#include "ids_strings.h"              // GetIdsStringWWithRawFallback (FUN_004347e0)
#include "menu_ui_widgets.h"
#include "nn_group_control.h"
#include "nn_ui_pulse.h"              // G_HudChromeColorRgba (DAT_00679bb4), G_NnPaletteArgb_679ba4
#include "player_spawn.h"             // GetPlayerConnectionId (DAT_00673344)
#include "rich_text_win.h"
#include "win_factory.h"
#include "win3d.h"

#include "common_archetype.h"          // Archetype::Ship (Root +0x14 ids_name)
#include "common_reputation.h"
#include "common_misc.h"              // SinglePlayer
#include "common_universe.h"           // Universe::get_system / get_base, Archetype::GetShip
#include "dacom.h"

#include <ctime>
#include <string>
#include <string.h>

const CHAT_ID G_ChatIdChannel3(0x00010003u);
const CHAT_ID G_ChatIdChannel2(0x00010002u);
const CHAT_ID G_ChatIdChannel1(0x00010001u);
const CHAT_ID G_ChatIdChannel0(0x00010000u);
const CHAT_ID G_ChatIdNone(0x00000000u);
const CHAT_ID G_ChatIdGroupInvite(0x00010004u);

CHAT_ID G_ChatInputTarget;

namespace
{
    // The compiled-in chat text styles FUN_00468c20 registers. Each is a TextRenderAttributes
    // seeded by its own CRT initializer (LAB_0045df30 .. LAB_0045e250, XC table 0x0060f43c..
    // 0x0060f4a0): byte +0 = the flags byte, the dword at +1 = (old & 0xff000000) | RGB over
    // zeroed .data -- so Packed = flags | RGB << 8. Retail spaces them 8 bytes apart; only the
    // first dword is ever read (StyleCollection::set_style copies Packed).
    TextRenderAttributes G_ChatStyle_66da14 = {0xffffff10u}; // LAB_0045df30
    TextRenderAttributes G_ChatStyle_66da0c = {0x1dbf3b16u}; // LAB_0045df50
    TextRenderAttributes G_ChatStyle_66da04 = {0xffffff00u}; // LAB_0045df70
    TextRenderAttributes G_ChatStyle_66d9fc = {0xe0c38700u}; // LAB_0045df90
    TextRenderAttributes G_ChatStyle_66d9f4 = {0x1dbf3b00u}; // LAB_0045dfb0
    TextRenderAttributes G_ChatStyle_66d9ec = {0x1dbf3b02u}; // LAB_0045dfd0
    TextRenderAttributes G_ChatStyle_66d9e4 = {0xffffff01u}; // LAB_0045dff0
    TextRenderAttributes G_ChatStyle_66d9dc = {0x1d1dbf01u}; // LAB_0045e010
    TextRenderAttributes G_ChatStyle_66d9d4 = {0xfd7eff00u}; // LAB_0045e030
    TextRenderAttributes G_ChatStyle_66d9cc = {0x52eaf501u}; // LAB_0045e050
    TextRenderAttributes G_ChatStyle_66d9c4 = {0x52eaf506u}; // LAB_0045e070
    TextRenderAttributes G_ChatStyle_66d9bc = {0xffffff00u}; // LAB_0045e090
    TextRenderAttributes G_ChatStyle_66d9b4 = {0xe0c38708u}; // LAB_0045e0b0
    TextRenderAttributes G_ChatStyle_66d9ac = {0xe0c38718u}; // LAB_0045e0d0
    TextRenderAttributes G_ChatStyle_66d9a4 = {0xe0c38718u}; // LAB_0045e0f0
    TextRenderAttributes G_ChatStyle_66d99c = {0xe0c38708u}; // LAB_0045e110
    TextRenderAttributes G_ChatStyle_66d994 = {0x52eaf508u}; // LAB_0045e130
    TextRenderAttributes G_ChatStyle_66d98c = {0xe0c38700u}; // LAB_0045e150
    TextRenderAttributes G_ChatStyle_66d984 = {0x52eaf500u}; // LAB_0045e170
    TextRenderAttributes G_ChatStyle_66d97c = {0xb6b6b600u}; // LAB_0045e190
    TextRenderAttributes G_ChatStyle_66d974 = {0xe0c38708u}; // LAB_0045e1b0
    TextRenderAttributes G_ChatStyle_66d96c = {0x52eaf508u}; // LAB_0045e1d0
    TextRenderAttributes G_ChatStyle_66d964 = {0xb6b6b608u}; // LAB_0045e1f0
    TextRenderAttributes G_ChatStyle_66d95c = {0xe0c38700u}; // LAB_0045e210
    TextRenderAttributes G_ChatStyle_66d954 = {0xe0c38728u}; // LAB_0045e230
    TextRenderAttributes G_ChatStyle_66d94c = {0xe0c38701u}; // LAB_0045e250

    // FUN_00468c20's colour copy (e.g. 0x00468d5b..0x00468d70): bytes 0..2 of a palette colour
    // into bytes 1..3 of the attributes; the flags byte stays.
    void ChatStyleTakeColor(TextRenderAttributes& Attrs, unsigned long Rgba)
    {
        Attrs.Packed = (Attrs.Packed & 0xffu) | ((Rgba & 0x00ffffffu) << 8);
    }
}

void ChatRegisterTextStyles()
{
    // FUN_00468c20 (no arguments; sole caller InitializeEngine 0x005b1ca0).
    StyleCollection::set_style(0xd5ec, G_ChatStyle_66da14);
    StyleCollection::set_style(0x73f7, G_ChatStyle_66da0c);
    StyleCollection::set_style(0xd681, G_ChatStyle_66da04);
    StyleCollection::set_style(0xd801, G_ChatStyle_66d9fc);
    StyleCollection::set_style(0x1d9d, G_ChatStyle_66d9f4);
    StyleCollection::set_style(0x8283, G_ChatStyle_66d9ec);
    StyleCollection::set_style(0xe784, G_ChatStyle_66d9e4);
    StyleCollection::set_style(0x3ea8, G_ChatStyle_66d9dc);
    StyleCollection::set_style(0xba28, G_ChatStyle_66d9d4);
    StyleCollection::set_style(0x66b7, G_ChatStyle_66d9cc);
    StyleCollection::set_style(0x2b34, G_ChatStyle_66d9c4);
    StyleCollection::set_style(0x66ac, G_ChatStyle_66d9bc);
    StyleCollection::set_style(0x828b, G_ChatStyle_66d9b4);
    StyleCollection::set_style(0x1df5, G_ChatStyle_66d9ac);
    StyleCollection::set_style(0x43e0, G_ChatStyle_66d9a4);
    StyleCollection::set_style(0xd824, G_ChatStyle_66d99c);
    StyleCollection::set_style(0xd190, G_ChatStyle_66d994);
    StyleCollection::set_style(0x69f5, G_ChatStyle_66d98c);
    StyleCollection::set_style(0x3343, G_ChatStyle_66d984);
    StyleCollection::set_style(0xbb80, G_ChatStyle_66d97c);
    StyleCollection::set_style(0x5203, G_ChatStyle_66d974);
    StyleCollection::set_style(0x3dbc, G_ChatStyle_66d96c);
    StyleCollection::set_style(0x867f, G_ChatStyle_66d964);

    // 0x00468d5b..0x00468e04: four styles take the HUD chrome colour (DAT_00679bb4), and one
    // more reuses the +0x66d95c record with the pulse amber (DAT_00679ba4).
    unsigned long const Chrome = static_cast<unsigned long>(G_HudChromeColorRgba[0]) |
                                 (static_cast<unsigned long>(G_HudChromeColorRgba[1]) << 8) |
                                 (static_cast<unsigned long>(G_HudChromeColorRgba[2]) << 16);
    ChatStyleTakeColor(G_ChatStyle_66d95c, Chrome);
    StyleCollection::set_style(0x9ca4, G_ChatStyle_66d95c);
    StyleCollection::set_style(0x76ec, G_ChatStyle_66d95c);
    ChatStyleTakeColor(G_ChatStyle_66d954, Chrome);
    StyleCollection::set_style(0x686c, G_ChatStyle_66d954);
    ChatStyleTakeColor(G_ChatStyle_66d94c, Chrome);
    StyleCollection::set_style(0xf64, G_ChatStyle_66d94c);
    ChatStyleTakeColor(G_ChatStyle_66d95c, G_NnPaletteArgb_679ba4);
    StyleCollection::set_style(0x3d0a, G_ChatStyle_66d95c);

    // 0x00468e06..0x00468f9e: style 0x4433 is a list -- TRANode({flags 8, RGB e0c387}, -1, 0)
    // and JustifyNode(1), each cloned into the stack list (FUN_0046ea00) with the first clone
    // deleted; StyleCollection::set_style(0x4433, list); the list erased node by node
    // (FUN_0046d680) and its head freed -- the RenderDisplayList's own destructor here.
    RenderDisplayList Style;
    {
        TextRenderAttributes Attrs;
        Attrs.Packed = 0xe0c38708u;
        TRANode Tra(Attrs, 0xffffffffu, 0);
        RDLNode* const TraClone = Tra.Clone();
        RdlAppendNode(Style, TraClone->Clone());
        delete TraClone;
    }
    {
        JustifyNode Justify(static_cast<TextJustify>(1));
        RDLNode* const JustifyClone = Justify.Clone();
        RdlAppendNode(Style, JustifyClone->Clone());
        delete JustifyClone;
    }
    StyleCollection::set_style(0x4433, Style);
}

FChatMessageStore* G_ChatMessageStore = nullptr;
FChatDisplaySnapshot* G_ChatAllMessages = nullptr;
FChatDisplaySnapshot* G_ChatRecentMessages = nullptr;
std::map<unsigned, RenderDisplayList*>* G_ChatNameCache70 = nullptr;
std::map<unsigned, RenderDisplayList*>* G_ChatNameCache74 = nullptr;
std::map<unsigned, RenderDisplayList*>* G_ChatNameCache78 = nullptr;
FChatChannelTable* G_ChatChannelTable = nullptr;
MsgWin* G_ChatMsgWin = nullptr;
NnGroupControl* G_ChatGroupControl = nullptr;
bool G_bChatMsgWinWanted = false;

RenderDisplayList G_ChatPrefixNewPlayer;
RenderDisplayList G_ChatPrefixDepartingPlayer;
RenderDisplayList G_ChatPrefixNewAffiliation;
RenderDisplayList G_ChatPrefixLostAffiliation;

// std::string DAT_0066d8e4 -- assigned first thing in FUN_004690c0; ChatTextInputDisplay's
// Draw (0x0045f521) builds "chat.txm" under it.
std::string G_ChatImageRoot;

namespace
{
    const char kNnChatCpp[] = "E:\\FL\\Scratch\\Source\\Client\\nn\\NN_Chat.cpp";

    // DAT_0066d430 / DAT_0066d43c
    RenderDisplayList G_ChatServerMotd;
    int G_ChatServerMotdVersion = 0;

    // DAT_0066da30 -- static initializer LAB_0045ded0: DAT_005ceedc (0.46) * DAT_005cef60 (-0.5),
    // the input line's X. (LAB_0045def0 copies it into DAT_0066da2c, which nothing reads.)
    float const G_ChatInputX = 0.46f * -0.5f;

    // The prefix style (0x3ea8) every system notice opens with.
    const unsigned short kChatNoticeStyle = 0x3ea8;

    // The clock()/1000*1000 stamp every chat path computes (0x10624dd3 multiply = signed /1000).
    int ChatNowMsRounded()
    {
        int const Ticks = static_cast<int>(clock());
        return (Ticks / 1000) * 1000;
    }

    void ChatNotice(int Line, char const* Message)
    {
        FDUMP(0x100003, "%s(%d) : NOTICE:General:%s", kNnChatCpp, Line, Message);
    }

    // FUN_004690c0's prefix idiom (0x004691fa..0x00469344 and three more): StyleNode 0x3ea8, then
    // the IDS text with its trailing paragraph stripped, or JustifyNode(1) + the debug text.
    void BuildPrefix(RenderDisplayList& Prefix, unsigned Ids, wchar_t const* DebugText)
    {
        StyleNode Style(kChatNoticeStyle);
        RdlAppendNode(Prefix, Style.Clone());
        LoadIdsTextIntoRdl(Ids, Prefix, DebugText);
    }

    // FUN_00469d20 -- every cached name list deleted, then the map itself.
    void DestroyNameCache(std::map<unsigned, RenderDisplayList*>*& Cache)
    {
        if (Cache == nullptr)
            return;
        for (std::map<unsigned, RenderDisplayList*>::iterator It = Cache->begin();
             It != Cache->end(); ++It)
        {
            delete It->second;
            It->second = nullptr;
        }
        delete Cache;
        Cache = nullptr;
    }

    // The first step of FUN_00466770 / FUN_0046b6e0 / FUN_0046b8b0: FUN_00432240 find; a hit
    // returns the node's list.
    RenderDisplayList* CacheFindName(std::map<unsigned, RenderDisplayList*>& Cache, unsigned Id)
    {
        std::map<unsigned, RenderDisplayList*>::iterator const Found = Cache.find(Id);
        if (Found != Cache.end())
            return Found->second;
        return nullptr;
    }

    // The miss path of the same three: a new list filled from Ids (bHaveIds false = no object)
    // with the "????" fallback, inserted under Id. The object lookup happens after the find and
    // the allocation in retail, and the callers keep that order.
    RenderDisplayList* CacheNewName(std::map<unsigned, RenderDisplayList*>& Cache, unsigned Id,
                                    RenderDisplayList* Name, bool bHaveIds, unsigned Ids)
    {
        if (!bHaveIds || !LoadIdsIntoRdl(Ids, *Name))
        {
            // 0x0046b783..0x0046b835: every node erased, then TextPtrNode(u"????", -1).
            RdlClear(*Name);
            TextPtrNode Unknown(L"????", -1);
            RdlAppendNode(*Name, Unknown.Clone());
        }
        // FUN_0046d6d0 insert {Id, 0}, then the node's value = the list.
        Cache[Id] = Name;
        return Name;
    }

    // The RDLRef + ParagraphNode pair both rebuilds append per message.
    void AppendMessageLine(RenderDisplayList& Display, RenderDisplayList& Body)
    {
        RDLRefNode Ref(Body);
        RdlAppendNode(Display, Ref.Clone());
        ParagraphNode Para;
        RdlAppendNode(Display, Para.Clone());
    }

    // FUN_0046a4d0 / FUN_0046a770 body: prefix ref + TextNode(Name), then commit.
    void PostNotice(RenderDisplayList& Prefix, wchar_t const* Name)
    {
        FChatMessageEntry* const Entry = ChatAcquireEntry();
        RDLRefNode Ref(Prefix);
        RdlAppendNode(Entry->Body, Ref.Clone());
        TextNode Text(Name, -1);
        RdlAppendNode(Entry->Body, Text.Clone());
        ChatCommitEntries();
    }
}

// ---------------------------------------------------------------------------------------------
// Shared idioms

FChatMessageEntry* ChatScratchEntry()
{
    // DAT_0066d440 under the DAT_0066d44c bit-0 guard; atexit LAB_00468bf0 destroys it.
    static FChatMessageEntry Scratch;
    return &Scratch;
}

FChatMessageEntry* ChatAcquireEntry()
{
    // FUN_00468a80, and the same code inlined at every site (e.g. 0x00463683..0x004636c7):
    // first the DAT_0066d44c bit-0 guard constructs the scratch DAT_0066d440 (and registers
    // LAB_00468bf0) whether or not it is returned; then DAT_0066da60 ? FUN_004683b0 : scratch.
    FChatMessageEntry* const Scratch = ChatScratchEntry();
    if (G_ChatMessageStore != nullptr)
        return G_ChatMessageStore->Acquire();
    return Scratch;
}

void ChatCommitEntries()
{
    // e.g. 0x0046b04b..0x0046b05e: the store's FUN_004686e0 then FUN_004684e0.
    if (G_ChatMessageStore == nullptr)
        return;
    G_ChatMessageStore->RebuildAll();
    G_ChatMessageStore->RebuildRecent();
}

void ChatAppendRdlCopy(RenderDisplayList& Dst, RenderDisplayList const& Source)
{
    // The FUN_0046c800 loop: one list element per source element, each copy-constructed by
    // FUN_0046ea00 (a clone of the node).
    if (Source.Count == 0)
        return;
    void** const Sentinel = (void**)Source.NodeList;
    for (void** Cell = static_cast<void**>(Sentinel[0]); Cell != Sentinel;
         Cell = static_cast<void**>(Cell[0]))
    {
        RdlAppendNode(Dst, static_cast<RDLNode*>(Cell[2])->Clone());
    }
}

// ---------------------------------------------------------------------------------------------
// FChatChannelTable

FChatChannelTable::FChatChannelTable()
    : Generation(0)
{
}

FChatChannelTable::~FChatChannelTable()
{
    // FUN_00469e60: every name list deleted and its node erased, the info map emptied,
    // generation 0, both maps released.
    for (FNameMap::iterator It = Names.begin(); It != Names.end(); ++It)
    {
        delete It->second;
    }
    Names.clear();
    PlayerInfo.clear();
    Generation = 0;
}

void FChatChannelTable::AddChannel(CHAT_ID Id, RenderDisplayList const& Name)
{
    FNameMap::iterator It = Names.find(Id.Id);
    if (It == Names.end())
    {
        // 0x0045f89c..0x0045f9cf
        RenderDisplayList* const Copy = new RenderDisplayList();
        ChatAppendRdlCopy(*Copy, Name);
        Names[Id.Id] = Copy;
        FChatPlayerInfo& Info = PlayerInfo[Id.Id];
        Info.Field_0x0 = 0;
        Info.Rank = 0;
        Info.Field_0x8 = 0;
        ++Generation;
        ChatNotice(0x552, "Channel add successful.\n");
        return;
    }

    // 0x0045f9d4..0x0045fb08
    ChatNotice(0x558, "Channel add attempted on existing channel. Modifying instead.\n");
    RenderDisplayList* const Existing = Names[Id.Id];
    RdlClear(*Existing);
    ChatAppendRdlCopy(*Existing, Name);
    FChatPlayerInfo& Info = PlayerInfo[Id.Id];
    Info.Field_0x0 = 0;
    Info.Rank = 0;
    Info.Field_0x8 = 0;
    ++Generation;
}

void FChatChannelTable::RemovePlayerChannels()
{
    // FUN_0045fb40: channel ids (byte 2 set) and G_ChatIdNone stay; every player id loses its
    // info (equal_range erase on +0x18), its name list and its node.
    FNameMap::iterator It = Names.begin();
    while (It != Names.end())
    {
        unsigned const Key = It->first;
        if (((Key >> 16) & 0xffu) != 0 || Key == G_ChatIdNone.Id)
        {
            ++It;
            continue;
        }
        PlayerInfo.erase(Key);
        delete It->second;
        It = Names.erase(It);
    }
    ++Generation;
}

RenderDisplayList* FChatChannelTable::FindName(CHAT_ID Id) const
{
    FNameMap::const_iterator It = Names.find(Id.Id);
    if (It == Names.end())
        return nullptr;
    return It->second;
}

FChatPlayerInfo* FChatChannelTable::FindPlayerInfo(CHAT_ID Id)
{
    std::map<unsigned, FChatPlayerInfo>::iterator It = PlayerInfo.find(Id.Id);
    if (It == PlayerInfo.end())
        return nullptr;
    return &It->second;
}

// ---------------------------------------------------------------------------------------------
// FChatMessageStore

FChatMessageStore::FChatMessageStore(int Capacity, int InMaxCount, int InMaxAgeMs)
    : LastClockMs(0)
    , MaxAgeMs(InMaxAgeMs)
    , MaxCount(InMaxCount)
{
    // FUN_004699d0: four empty lists, then Capacity fresh 0x10 entries pushed onto the pool
    // (0x00469a80..0x00469afc); +0x40 = arg 1, +0x18 = +0x28 = +0x38 = 0, +0x3c = arg 2.
    for (int Index = 0; Index < Capacity; ++Index)
        Free.push_back(new FChatMessageEntry());
}

FChatMessageStore::~FChatMessageStore()
{
    // FUN_004688b0: every active entry cleared and pooled, the active list emptied and both
    // snapshots rebuilt (FUN_00535980, FUN_004686e0, FUN_004684e0); then every pooled entry
    // destroyed.
    for (std::list<FChatMessageEntry*>::iterator It = Active.begin(); It != Active.end(); ++It)
    {
        FChatMessageEntry* const Entry = *It;
        RdlClear(Entry->Body);
        Entry->TimestampMs = 0;
        Free.push_back(Entry);
    }
    Active.clear();
    RebuildAll();
    RebuildRecent();
    for (std::list<FChatMessageEntry*>::iterator It = Free.begin(); It != Free.end(); ++It)
        delete *It;
    Free.clear();
}

void FChatMessageStore::RecycleOldest()
{
    // FUN_0046a300: with any active entry, the oldest one is emptied, stamped 0 and pooled, then
    // both snapshots are rebuilt.
    if (Active.empty())
        return;
    FChatMessageEntry* const Oldest = Active.front();
    RdlClear(Oldest->Body);
    Oldest->TimestampMs = 0;
    Free.push_back(Oldest);
    Active.pop_front();
    RebuildAll();
    RebuildRecent();
}

FChatMessageEntry* FChatMessageStore::Acquire()
{
    // FUN_004683b0: an empty pool first takes back the oldest active entry (the same steps as
    // FUN_0046a300, inlined), then the pool's front is emptied, stamped with the rounded clock
    // and moved to the back of the active list. The pool is preloaded with Capacity entries and
    // an entry only ever moves between the two lists, so the front always exists.
    if (Free.empty() && !Active.empty())
        RecycleOldest();
    FChatMessageEntry* const Entry = Free.front();
    RdlClear(Entry->Body);
    Entry->TimestampMs = ChatNowMsRounded();
    Free.pop_front();
    Active.push_back(Entry);
    return Entry;
}

void FChatMessageStore::RebuildRecent()
{
    // FUN_004684e0: walk newest -> oldest while the message is within MaxAgeMs and no more than
    // MaxCount have been kept; LastClockMs ends on the oldest kept stamp (or now).
    int const NowMs = ChatNowMsRounded();
    LastClockMs = NowMs;

    int Kept = 0;
    std::list<FChatMessageEntry*>::reverse_iterator It = Active.rbegin();
    for (; It != Active.rend(); ++It)
    {
        FChatMessageEntry* const Entry = *It;
        unsigned const Age = static_cast<unsigned>(NowMs - Entry->TimestampMs);
        if (static_cast<unsigned>(MaxAgeMs) < Age || MaxCount < Kept)
            break;
        LastClockMs = Entry->TimestampMs;
        ++Kept;
    }

    // Rebuilt only when something is kept or the snapshot was non-empty.
    if (Kept < 1 && Recent.Display.Count == 0)
        return;
    RdlClear(Recent.Display);
    ++Recent.Generation;
    for (std::list<FChatMessageEntry*>::iterator Fwd = It.base(); Fwd != Active.end(); ++Fwd)
        AppendMessageLine(Recent.Display, (*Fwd)->Body);
}

void FChatMessageStore::RebuildAll()
{
    // FUN_004686e0: the snapshot emptied, one RDLRef + ParagraphNode per active message,
    // generation + 1.
    RdlClear(All.Display);
    for (std::list<FChatMessageEntry*>::iterator It = Active.begin(); It != Active.end(); ++It)
        AppendMessageLine(All.Display, (*It)->Body);
    ++All.Generation;
}

bool FChatMessageStore::Discard(FChatMessageEntry* Entry)
{
    // 0x00463706..0x00463736 + 0x00464c83..0x00464cd2: find the entry on the active list; when
    // it is there, empty it, stamp 0, pool it and unlink it.
    for (std::list<FChatMessageEntry*>::iterator It = Active.begin(); It != Active.end(); ++It)
    {
        if (*It != Entry)
            continue;
        RdlClear(Entry->Body);
        Entry->TimestampMs = 0;
        Free.push_back(Entry);
        Active.erase(It);
        return true;
    }
    return false;
}

void FChatMessageStore::DiscardAll()
{
    // 0x0046b1ff..0x0046b264: every active entry emptied, stamped 0 and pooled, in order; the
    // active list emptied (FUN_004a2f80); FUN_004686e0 + FUN_004684e0.
    for (std::list<FChatMessageEntry*>::iterator It = Active.begin(); It != Active.end(); ++It)
    {
        FChatMessageEntry* const Entry = *It;
        RdlClear(Entry->Body);
        Entry->TimestampMs = 0;
        Free.push_back(Entry);
    }
    Active.clear();
    RebuildAll();
    RebuildRecent();
}

// ---------------------------------------------------------------------------------------------
// Init / shutdown

void ChatInit()
{
    // 0x004690dc..0x004690f8
    G_ChatImageRoot.assign("Interface\\NeuroNet\\Chat\\");

    // 0x004690fe..0x004691ab: the three name caches (FUN_004c5a10).
    G_ChatNameCache70 = new std::map<unsigned, RenderDisplayList*>();
    G_ChatNameCache74 = new std::map<unsigned, RenderDisplayList*>();
    G_ChatNameCache78 = new std::map<unsigned, RenderDisplayList*>();

    // 0x004691a5..0x00469207
    G_ChatMessageStore = new FChatMessageStore(kChatMessageStoreCapacity,
                                               kChatMessageStoreMaxCount,
                                               kChatMessageStoreMaxAgeMs);
    G_ChatAllMessages = &G_ChatMessageStore->All;
    G_ChatRecentMessages = &G_ChatMessageStore->Recent;
    G_ChatInputTarget = G_ChatIdChannel1;   // 0x0046920c

    // 0x004691fa..0x00469760: the four notice prefixes.
    BuildPrefix(G_ChatPrefixNewPlayer, 0x60052u, L"DEBUG - New player:");
    BuildPrefix(G_ChatPrefixDepartingPlayer, 0x60053u, L"DEBUG - Departing player:");
    BuildPrefix(G_ChatPrefixNewAffiliation, 0x6004fu, L"DEBUG - New affiliation:");
    BuildPrefix(G_ChatPrefixLostAffiliation, 0x60050u, L"DEBUG - Lost affiliation:");

    // 0x00469760..0x004697c6
    G_ChatChannelTable = new FChatChannelTable();

    // 0x004697b3..0x004698a8: three channel names. The IDS text goes in as loaded (no
    // FUN_0057eb40) and only when it loaded -- no debug fallback here.
    {
        RenderDisplayList Label;
        if (LoadIdsIntoRdl(0x6003du, Label))
            G_ChatChannelTable->AddChannel(G_ChatIdChannel1, Label);
    }
    {
        RenderDisplayList Label;
        if (LoadIdsIntoRdl(0x60036u, Label))
            G_ChatChannelTable->AddChannel(G_ChatIdChannel3, Label);
    }
    {
        RenderDisplayList Label;
        if (LoadIdsIntoRdl(0x60035u, Label))
            G_ChatChannelTable->AddChannel(G_ChatIdNone, Label);
    }

    // 0x004698ad..0x004698f9
    G_ChatGroupControl = static_cast<NnGroupControl*>(
        WinFactoryCreate("NN_GroupControl", "NN_GroupControl", nullptr, nullptr));
    if (G_ChatGroupControl != nullptr)
    {
        float const Position[3] = {0.0f, 0.0f, -1.0f};
        G_ChatGroupControl->SetPosition(Position);
        G_ChatGroupControl->Flags = static_cast<unsigned char>(G_ChatGroupControl->Flags & 0xfc);
    }
}

void ChatShutdown()
{
    // FUN_00469b30
    if (G_ChatGroupControl != nullptr)
    {
        // 0x00469b3f..0x00469b67 is FUN_0043d2a0 inlined on the root.
        Win3DDeleteTree(G_ChatGroupControl);
        G_ChatGroupControl = nullptr;
    }
    if (G_ChatMsgWin != nullptr)
    {
        Win3DDeleteTree(G_ChatMsgWin);
        G_ChatMsgWin = nullptr;
    }
    if (G_ChatChannelTable != nullptr)
    {
        delete G_ChatChannelTable;
        G_ChatChannelTable = nullptr;
    }
    if (G_ChatMessageStore != nullptr)
    {
        delete G_ChatMessageStore;
        G_ChatMessageStore = nullptr;
        G_ChatAllMessages = nullptr;
        G_ChatRecentMessages = nullptr;
    }
    DestroyNameCache(G_ChatNameCache70);
    DestroyNameCache(G_ChatNameCache74);
    DestroyNameCache(G_ChatNameCache78);
}

// ---------------------------------------------------------------------------------------------
// Incoming chat

void ChatDispatchIncoming(CHAT_ID Sender, void const* Buffer, unsigned SizeBytes)
{
    // FUN_0046a150
    if (Sender.Id == G_ChatIdGroupInvite.Id)
    {
        if (G_ChatGroupControl != nullptr)
            G_ChatGroupControl->HandleInviteMessage(Buffer, SizeBytes);
        return;
    }

    // 0x0046a180..0x0046a228: FUN_004683b0 inlined (with FUN_0046a300 for the empty pool).
    FChatMessageEntry* const Entry = ChatAcquireEntry();

    BinaryRDLReader Reader;
    if (!Reader.read_buffer(Entry->Body, static_cast<char const*>(Buffer), SizeBytes))
    {
        // 0x0046a253..0x0046a2cb
        if (G_ChatMessageStore != nullptr)
            G_ChatMessageStore->Discard(Entry);
        ChatNotice(0x1249, "Failed to decode a chat message!\n");
        return;
    }
    ChatCommitEntries();
}

// ---------------------------------------------------------------------------------------------
// Players

void ChatPlayerJoined(wchar_t const* Name, unsigned PlayerId, bool bNotify)
{
    // FUN_0046aa10
    if (G_ChatChannelTable != nullptr)
    {
        RenderDisplayList Label;
        TextNode Text(Name, -1);
        RdlAppendNode(Label, Text.Clone());
        G_ChatChannelTable->AddChannel(ChatIdForPlayer(PlayerId), Label);
    }
    if (!bNotify)
        return;

    // 0x0046aafe..0x0046ad1b
    FChatMessageEntry* const Entry = ChatAcquireEntry();
    RDLRefNode Ref(G_ChatPrefixNewPlayer);
    RdlAppendNode(Entry->Body, Ref.Clone());
    TextNode Text(Name, -1);
    RdlAppendNode(Entry->Body, Text.Clone());
    ChatCommitEntries();
}

void ChatPlayerLeft(unsigned PlayerId, bool bNotify)
{
    // FUN_0046aeb0
    CHAT_ID const Key = ChatIdForPlayer(PlayerId);
    FChatChannelTable* Table = G_ChatChannelTable;
    if (Table == nullptr)
        return;
    RenderDisplayList* const Name = Table->FindName(Key);
    if (Name == nullptr)
        return;

    if (bNotify)
    {
        // 0x0046af24..0x0046b05e
        FChatMessageEntry* const Entry = ChatAcquireEntry();
        RDLRefNode Ref(G_ChatPrefixDepartingPlayer);
        RdlAppendNode(Entry->Body, Ref.Clone());
        ChatAppendRdlCopy(Entry->Body, *Name);
        ChatCommitEntries();
    }

    // 0x0046b063..0x0046b0bb: DAT_0066da7c reloaded, the key looked up again.
    Table = G_ChatChannelTable;
    FChatChannelTable::FNameMap::iterator It = Table->Names.find(Key.Id);
    if (It == Table->Names.end())
        return;
    Table->PlayerInfo.erase(Key.Id);
    delete It->second;
    Table->Names.erase(It);
    ++Table->Generation;
}

void ChatSetPlayerInfoField0(unsigned PlayerId, unsigned Value)
{
    // FUN_0046ad60
    if (G_ChatChannelTable == nullptr)
        return;
    FChatPlayerInfo* const Info = G_ChatChannelTable->FindPlayerInfo(ChatIdForPlayer(PlayerId));
    if (Info == nullptr)
        return;
    Info->Field_0x0 = Value;
    ++G_ChatChannelTable->Generation;
}

void ChatSetPlayerRank(unsigned PlayerId, unsigned Rank)
{
    // FUN_0046add0
    if (G_ChatChannelTable == nullptr)
        return;
    FChatPlayerInfo* const Info = G_ChatChannelTable->FindPlayerInfo(ChatIdForPlayer(PlayerId));
    if (Info == nullptr)
        return;
    Info->Rank = Rank;
    ++G_ChatChannelTable->Generation;
}

void ChatSetPlayerInfoField8(unsigned PlayerId, unsigned Value)
{
    // FUN_0046ae40
    if (G_ChatChannelTable == nullptr)
        return;
    FChatPlayerInfo* const Info = G_ChatChannelTable->FindPlayerInfo(ChatIdForPlayer(PlayerId));
    if (Info == nullptr)
        return;
    Info->Field_0x8 = Value;
    ++G_ChatChannelTable->Generation;
}

RenderDisplayList* ChatFindPlayerName(unsigned PlayerId)
{
    // FUN_0046b400: the argument dword is the key as passed (no FUN_0046ad40 here).
    return G_ChatChannelTable->FindName(CHAT_ID(PlayerId));
}

void ChatPostLostAffiliation(wchar_t const* Name)
{
    // FUN_0046a4d0 -- prefix DAT_0066d400.
    PostNotice(G_ChatPrefixLostAffiliation, Name);
}

void ChatPostNewAffiliation(wchar_t const* Name)
{
    // FUN_0046a770 -- prefix DAT_0066d40c.
    PostNotice(G_ChatPrefixNewAffiliation, Name);
}

// ---------------------------------------------------------------------------------------------
// Windows

bool ChatIsInputOpen()
{
    // FUN_0046a480: FUN_0059da10("CTID") != 0. The name is the dword string at 0x005cf04c
    // (43 54 49 44 00); Ghidra shows only its first byte as "C".
    return Win3DFindByName("CTID") != nullptr;
}

void ChatSetInputOpen(bool bOpen)
{
    // FUN_0046a3c0
    Win3D* Input = Win3DFindByName("CTID");   // FUN_0059da10, name at 0x005cf04c
    if (bOpen)
    {
        if (Input == nullptr)
        {
            // 0x0046a3e3..0x0046a422
            Input = WinFactoryCreate("CTID", "ChatTextInputDisplay", nullptr, nullptr);
            if (Input == nullptr)
                return;
            float const Position[3] = {G_ChatInputX, -0.265f, -1.0f};   // DAT_005ceee4
            Input->SetPosition(Position);
        }
        // 0x0046a425..0x0046a455: +0x539 = 1 (close after one message), FUN_0045eaf0(&DAT_0066d3fc)
        // (show the target), FUN_0057c750 (activate), FUN_0057bdb0(0x50), FUN_0057b520(1),
        // FUN_00579a50(1).
        ChatTextInputDisplay* const Line = static_cast<ChatTextInputDisplay*>(Input);
        Line->bCloseAfterSend = 1;
        Line->SetTarget(G_ChatInputTarget);
        Line->Activate();
        Line->SetMaxLength(0x50);
        Line->SetLayoutFlag(true);
        Line->SetKeepScrolledToEnd(true);
        return;
    }
    if (Input != nullptr)
    {
        // 0x0046a463..0x0046a46e: FUN_0057bff0 (clear the edit buffer), FUN_0057c7d0(1).
        ChatTextInputDisplay* const Line = static_cast<ChatTextInputDisplay*>(Input);
        Line->ClearText();
        Line->Deactivate(true);
    }
}

namespace
{
    // DAT_0066d3f8: the chat key-command handler, one vptr (005cfe7c, single slot
    // LAB_0046a110) -- the FHudCmdHandler shape FUN_00576410 calls through. Built by the static
    // initializer LAB_0046a0a0 (FUN_0046a0c0), destroyed by its atexit LAB_0046a140.
    unsigned const kChatOpenCommand = 0x5d; // the key command id FUN_0046a0c0 registers

    class FChatCommandHandler : public EventHandler
    {
    public:
        FChatCommandHandler();
        ~FChatCommandHandler();

        // LAB_0046a110 (thiscall, RET 4).
        bool OnEvent(unsigned MsgId) override;

    };

    FChatCommandHandler::FChatCommandHandler()
    {
        // FUN_0046a0c0: vptr 005cfe7c, then FUN_005760d0(0x5d).
        RegisterEvent(kChatOpenCommand);
    }

    FChatCommandHandler::~FChatCommandHandler()
    {
        // LAB_0046a140 -> FUN_00576010 runs through the EventHandler base destructor.
    }

    bool FChatCommandHandler::OnEvent(unsigned MsgId)
    {
        // LAB_0046a110: command 0x5d, not single player, and the chat window wanted
        // (DAT_0066da88) -> FUN_0046a3c0(1) and consume; anything else is not handled.
        if (MsgId != kChatOpenCommand)
            return false;
        if (SinglePlayer())
            return false;
        if (!G_bChatMsgWinWanted)
            return false;
        ChatSetInputOpen(true);
        return true;
    }

    FChatCommandHandler G_ChatCommandHandler; // DAT_0066d3f8
}

namespace
{
    // The body FUN_0046b170 and FUN_0046b2b0 share (0x0046b182..0x0046b29a /
    // 0x0046b2c2..0x0046b3f4). Neither runs in single player.
    void ChatResetMultiplayerState()
    {
        if (SinglePlayer())
            return;

        NnGroupControl* const Group = G_ChatGroupControl;
        if (Group != nullptr)
        {
            // FUN_0046d380 set erase(begin, end), FUN_004a2f80 list erase(begin, end), then
            // FUN_00462e30.
            Group->GroupMembers.clear();
            Group->Requests.clear();
            Group->RefreshRequestDisplay();
        }
        if (G_ChatChannelTable != nullptr)
            G_ChatChannelTable->RemovePlayerChannels();   // FUN_0045fb40

        G_bChatMsgWinWanted = false;
        if (G_ChatMsgWin != nullptr)
            G_ChatMsgWin->Flags = static_cast<unsigned char>(G_ChatMsgWin->Flags & 0xfc);

        ChatSetInputOpen(false);   // FUN_0046a3c0(0)

        // Every active message cleared, stamped 0 and pooled; the active list emptied;
        // FUN_004686e0 + FUN_004684e0.
        if (G_ChatMessageStore != nullptr)
            G_ChatMessageStore->DiscardAll();

        G_ChatInputTarget = G_ChatIdChannel1;   // DAT_0066d3fc = DAT_0066da40
        Win3D* const Input = Win3DFindByName("CTID");   // FUN_0059da10
        if (Input != nullptr)
        {
            // FUN_0045eaf0(&target) + FUN_0057d330 on the input line.
            ChatTextInputDisplay* const Line = static_cast<ChatTextInputDisplay*>(Input);
            Line->SetTarget(G_ChatInputTarget);
            Line->RebuildDisplay();
        }
    }
}

void ChatResetForDisconnect()
{
    // FUN_0046b170
    ChatResetMultiplayerState();
}

void ChatResetForConnect()
{
    // FUN_0046b2b0
    ChatResetMultiplayerState();
}

void ChatShowMsgWin(bool bShow)
{
    // FUN_0046a4a0
    G_bChatMsgWinWanted = bShow;
    if (bShow)
    {
        if (G_ChatMsgWin != nullptr)
            G_ChatMsgWin->Flags = static_cast<unsigned char>(G_ChatMsgWin->Flags | 0x03);
    }
    else
    {
        if (G_ChatMsgWin != nullptr)
            G_ChatMsgWin->Flags = static_cast<unsigned char>(G_ChatMsgWin->Flags & 0xfc);
    }
}

void ChatDestroyMsgWin()
{
    // FUN_0046b650: FUN_0043d2a0 inlined on DAT_0066da80.
    if (G_ChatMsgWin == nullptr)
        return;
    Win3DDeleteTree(G_ChatMsgWin);
    G_ChatMsgWin = nullptr;
}

void ChatPlaceMsgWinForTrade(int Mode)
{
    // FUN_0046b450
    if (G_ChatMsgWin == nullptr)
        return;
    if (Mode == 0)
    {
        // 0x0046b4b3: the default place, {-0.51, 0.25, -1}, 1.0 x 0.39.
        float const Position[3] = {-0.51f, 0.25f, -1.0f};
        G_ChatMsgWin->SetPosition(Position);
        G_ChatMsgWin->SetNormSize(1.0f, 0.39f);
    }
    else if (Mode == 1)
    {
        // 0x0046b471: beside the trade window, {-0.275, 0.045, -1}, 0.56 x 0.39.
        float const Position[3] = {-0.275f, 0.045f, -1.0f};
        G_ChatMsgWin->SetPosition(Position);
        G_ChatMsgWin->SetNormSize(0.56f, 0.39f);
    }
}

// ---------------------------------------------------------------------------------------------
// MsgWin

// DAT_0066d47c -- LAB_00464d60 runs FUN_0046c0a0(&DAT_0066d47c, "MsgWin") and registers the
// atexit unlink LAB_00464d80.
WINFACTORY<MsgWin> G_MsgWinFactory("MsgWin");

MsgWin::MsgWin()
    : Snapshot(nullptr)
    , SnapshotGeneration(0)
{
    // Inlined in FUN_0046c110 after RichTextWin's ctor FUN_0057b1b0: vftables, +0x49c = 0,
    // +0x498 = 0.
}

MsgWin::~MsgWin()
{
    // FUN_0046c4c0: +0x49c = +0x498 = 0, then RichTextWin's dtor FUN_0057b260.
    SnapshotGeneration = 0;
    Snapshot = nullptr;
}

bool MsgWin::HitTest(float const* /*Ray*/)
{
    // Slot 4 of vftable 0x005cffc4 (entry 0x005cffd4) is FUN_0053f330 -- XOR AL,AL / RET 4 --
    // not BaseRichTextWin's LAB_0057aa80: the chat message window is never picked, so the mouse
    // goes through it to space.
    return false;
}

void MsgWin::Update()
{
    // LAB_0046c1d0 -- 0x0046c1df..0x0046c2fb: every changed-affiliation entry of this client.
    std::map<unsigned int, unsigned int>* const Changed =
        Reputation::GetChangedAffiliationClientMap();
    if (Changed != nullptr)
    {
        for (std::map<unsigned int, unsigned int>::iterator It = Changed->begin();
             It != Changed->end(); ++It)
        {
            if (It->first != GetPlayerConnectionId())
                continue;
            // The map holds the affiliation the client had; Vibe::GetAffiliation the one it has.
            unsigned const Lost = It->second;
            unsigned Current = 0xffffffffu;
            int const PlayerVibe = GetPlayerSpawnSystemIdRetail();   // FUN_004c4670
            Reputation::Vibe::GetAffiliation(PlayerVibe, Current, false);
            if (Lost != 0xffffffffu)
            {
                // 0x0046c240..0x0046c26b
                wchar_t Name[0x80];
                if (GetIdsStringWWithRawFallback(Reputation::get_name(Lost), Name, 0x80) == 0)
                    Name[0] = 0;
                ChatPostLostAffiliation(Name);   // FUN_0046a4d0
            }
            if (Current != 0xffffffffu)
            {
                // 0x0046c27c..0x0046c2b0
                wchar_t Name[0x80];
                if (GetIdsStringWWithRawFallback(Reputation::get_name(Current), Name, 0x80) == 0)
                    Name[0] = 0;
                ChatPostNewAffiliation(Name);    // FUN_0046a770
            }
        }
        // 0x0046c306..0x0046c393: the whole map erased (Common never clears it).
        Changed->clear();
    }

    // 0x0046c395..0x0046c3ed
    if (Snapshot != nullptr)
    {
        FChatMessageStore* const Store = G_ChatMessageStore;
        if (Store != nullptr)
        {
            int const Ticks = static_cast<int>(clock());
            int const NowMs = (Ticks / 1000) * 1000;
            if (static_cast<unsigned>(NowMs - Store->LastClockMs) >
                static_cast<unsigned>(Store->MaxAgeMs))
                Store->RebuildRecent();
        }
        if (SnapshotGeneration != Snapshot->Generation)
            RefreshFromSnapshot();
    }
    RichTextWin::Update();   // FUN_0057b530
}

void MsgWin::RefreshFromSnapshot()
{
    // FUN_0046c410
    if (Snapshot == nullptr)
        return;
    ClearContent();
    RDLRefNode Ref(Snapshot->Display);
    AppendNode(Ref);
    SnapshotGeneration = Snapshot->Generation;
}

void ChatCreateMsgWin()
{
    // FUN_0046b580
    if (G_ChatMsgWin != nullptr || G_ChatRecentMessages == nullptr)
        return;
    G_ChatMsgWin = static_cast<MsgWin*>(WinFactoryCreate("MsgWin", "MsgWin", nullptr, nullptr));
    if (G_ChatMsgWin != nullptr)
    {
        // 0x0046b5bc..0x0046b5f7: DAT_005ceec4 / DAT_005ceec8, then DAT_005ceebc / DAT_005ceec0.
        float const Position[3] = {-0.51f, 0.25f, -1.0f};
        G_ChatMsgWin->SetPosition(Position);
        G_ChatMsgWin->SetNormSize(1.0f, 0.39f);
    }
    // 0x0046b601..0x0046b642: retail does not test the window again here.
    G_ChatMsgWin->Snapshot = G_ChatRecentMessages;
    G_ChatMsgWin->SnapshotGeneration = 0;
    G_ChatMsgWin->Flags = static_cast<unsigned char>(G_ChatMsgWin->Flags | 0x03);
    G_ChatMsgWin->SetDrawFrame(false);
    G_ChatMsgWin->SetAlignMode(3);
    G_ChatMsgWin->SetAutoScrollbar(false);
}

void ChatGroupTradeRequestReceived(unsigned ShipId)
{
    // FUN_0046b500
    if (G_ChatGroupControl != nullptr)
        G_ChatGroupControl->AddTradeRequest(ShipId);
}

void ChatGroupTradeRequestRemoved(unsigned ShipId)
{
    // FUN_0046b520: the first request whose +0x10 is ShipId (non-zero) is unlinked, then
    // FUN_00462e30 (tail jump). Nothing found: no refresh.
    NnGroupControl* const Group = G_ChatGroupControl;
    if (Group == nullptr)
        return;
    for (std::list<FNnGroupRequest>::iterator It = Group->Requests.begin();
         It != Group->Requests.end(); ++It)
    {
        if (It->ShipId != 0 && It->ShipId == ShipId)
        {
            Group->Requests.erase(It);
            Group->RefreshRequestDisplay();
            return;
        }
    }
}

// ---------------------------------------------------------------------------------------------
// MOTD

void ChatSetServerMotd(const wchar_t* Text)
{
    // FUN_0046b0f0
    RdlClear(G_ChatServerMotd);
    AppendTextAsRdl(Text, static_cast<unsigned int>(wcslen(Text)), G_ChatServerMotd, false);
    ++G_ChatServerMotdVersion;
}

// ---------------------------------------------------------------------------------------------
// Name caches

RenderDisplayList* ChatGetSystemName(unsigned SystemId)
{
    // FUN_0046b6a0 -> FUN_00466770 on DAT_0066da70.
    RenderDisplayList* const Cached = CacheFindName(*G_ChatNameCache70, SystemId);
    if (Cached != nullptr)
        return Cached;
    RenderDisplayList* const Name = new RenderDisplayList();
    Universe::ISystem const* const System = Universe::get_system(SystemId);
    return CacheNewName(*G_ChatNameCache70, SystemId, Name, System != nullptr,
                        System != nullptr ? (unsigned)System->GetStridName() : 0);
}

RenderDisplayList* ChatGetShipArchName(unsigned ShipArchId)
{
    // FUN_0046b6c0 -> FUN_0046b6e0 on DAT_0066da74.
    RenderDisplayList* const Cached = CacheFindName(*G_ChatNameCache74, ShipArchId);
    if (Cached != nullptr)
        return Cached;
    RenderDisplayList* const Name = new RenderDisplayList();
    Archetype::Ship* const Ship = Archetype::GetShip(ShipArchId);
    return CacheNewName(*G_ChatNameCache74, ShipArchId, Name, Ship != nullptr,
                        Ship != nullptr ? static_cast<unsigned>(Ship->IdsName) : 0);
}

RenderDisplayList* ChatGetBaseName(unsigned BaseId)
{
    // FUN_0046b890 -> FUN_0046b8b0 on DAT_0066da78.
    RenderDisplayList* const Cached = CacheFindName(*G_ChatNameCache78, BaseId);
    if (Cached != nullptr)
        return Cached;
    RenderDisplayList* const Name = new RenderDisplayList();
    Universe::IBase* const Base = Universe::get_base(BaseId);
    return CacheNewName(*G_ChatNameCache78, BaseId, Name, Base != nullptr,
                        Base != nullptr ? static_cast<unsigned>(Base->StridName) : 0);
}

RenderDisplayList* ChatGetServerMotdRdl()
{
    return &G_ChatServerMotd;
}

int ChatGetServerMotdVersion()
{
    return G_ChatServerMotdVersion;
}
