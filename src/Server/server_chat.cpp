// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// server_chat.cpp -- Server.dll's chat router.
//
// Retail source file: "E:\FL\Scratch\Source\Server\FLChatServer.cpp", the __FILE__
// every FDUMP notice below carries. Everything here sits in the contiguous block
// .text:06d18930 .. .text:06d1b6f0 of Server.dll plus its .data (06d8e5a8 ..
// 06d8f610) and is reached from exactly one place: IServerImpl::SubmitChat
// (0x06d191b0, the IChatServer slot), which IServerImpl.cpp forwards here.
//
// What SubmitChat does, in retail order:
//   1. builds the slash-command table once (flag DAT_06d8f610);
//   2. group-control messages (Target == 0x00010004) go straight to the
//      invite / leave / join verbs and nothing else happens;
//   3. decodes the BinaryRDL text; a leading '/' is matched against the command
//      table, and a matched command either swallows the message or rewrites the
//      target/payload and lets it continue;
//   4. prefixes "<sender name>: " as rich text, appends the original payload (or
//      a red "<message too big>"), re-encodes;
//   5. routes on the target CHAT_ID: private, universe, system, local, group.
#include "../include/common_server.h"
#include "../include/common_server_playerdb.h"
#include "../include/common_text.h"
#include "../include/common_reputation.h"   // Reputation::Vibe::GetName
#include "../include/common_net.h"          // IsMPServer
#include "../include/server_sysdb.h"
#include "../include/rpc_iclient.h"
#include <windows.h>
#include <cstring>
#include <cwchar>
#include <list>
#include <new>
#include <regex>
#include <set>
#include <string>
#include <vector>

// Retail .data:DAT_06d8fb18, defined in IServerImpl.cpp.
extern IChatClient* g_pChatClientInterface;

// FUN_06d1b1b0's port, defined in server_classes.cpp (shared with the cheat report).
void Server_RdlAppend(RenderDisplayList& Rdl, const RDLNode& Node);

// The exported hook slots g_pPrivateChatHook / g_pSystemChatHook /
// g_pUniverseChatHook are declared in common_server.h and defined with the other
// Server.dll data exports in server_classes.cpp.

// ---------------------------------------------------------------------------
// Chat ids. Each is a CHAT_ID global set by its own CRT initializer, which
// writes the word and byte 2 and leaves byte 3 at its .data zero:
//   LAB_06d18930  DAT_06d8f5f0  word 0, byte2 0   console / "nobody"
//   LAB_06d18940  DAT_06d8f5ec  word 0, byte2 1   universe
//   LAB_06d18960  DAT_06d8f5e8  word 1, byte2 1   system
//   LAB_06d18980  DAT_06d8f5e4  word 2, byte2 1   local
//   LAB_06d189a0  DAT_06d8f5e0  word 3, byte2 1   group
//   LAB_06d189c0  DAT_06d8f5dc  word 4, byte2 1   group control
// Byte 2 is the "channel" flag: a CHAT_ID whose byte 2 is zero names a player.
// Every comparison in SubmitChat is a full-dword CMP against these.
// ---------------------------------------------------------------------------
static const unsigned int GChatIdConsole  = 0x00000000u;
static const unsigned int GChatIdUniverse = 0x00010000u;
static const unsigned int GChatIdSystem   = 0x00010001u;
static const unsigned int GChatIdLocal    = 0x00010002u;
static const unsigned int GChatIdGroup    = 0x00010003u;
// server_classes.cpp's CPlayerGroup senders use the same global, hence extern.
extern const unsigned int GGroupChatSourceId = 0x00010004u;

// ---------------------------------------------------------------------------
// Buffers and codec objects.
// ---------------------------------------------------------------------------
static wchar_t GChatText[0x400];        // .data:06d8e9b0, decoded text (0x400 wchar)
static char GChatSendBuffer[0x400];     // .data:06d8e5a8, the encoded outgoing message
static char GChatCommandBuffer[0x400];  // .data:06d8f1b8, echo / channel re-encodes

// Retail keeps the reader (.data:06d8f1b0) and the first writer (.data:06d8e9a8)
// as function-local statics of SubmitChat, guarded by bits 0/1 of DAT_06d8f1b4
// (.text:06d193a3 / .text:06d194f0); their "construction" is a single vptr store
// and their atexit hooks FUN_06d1ad20 / FUN_06d1ad10 are bare RETs. The second
// writer (.data:06d8f5b8) is built by the CRT initializer LAB_06d18e90 and used
// only by the command handlers. All three are plain file-scope objects here --
// no observable difference, the constructors do nothing else.
static BinaryRDLReader GChatReader;
static BinaryRDLWriter GChatWriter;
static BinaryRDLWriter GChatCommandWriter;

static const char* const GChatServerFile = "E:\\FL\\Scratch\\Source\\Server\\FLChatServer.cpp";

// Every notice in this file is the same call shape (e.g. .text:06d198ff):
//   FDUMP(0x100003, "%s(%d) : NOTICE:General:%s", __FILE__, line, msg)
// through the DACOM import, unconditionally.
static void ChatNotice(int Line, const char* Message)
{
    FDUMP(0x100003, "%s(%d) : NOTICE:General:%s", GChatServerFile, Line, Message);
}

// FUN_06d19180 (7 insns, thiscall, `RET 0x4`): CHAT_ID{word} -- stores the
// ushort, zeroes bytes 2 and 3. FUN_06d19ef0 (6 insns) is the same with word 0.
static unsigned int ChatIdFromWord(unsigned int Id)
{
    return Id & 0xffffu;
}

// ---------------------------------------------------------------------------
// PlayerData helpers.
// ---------------------------------------------------------------------------

// FUN_06d4a1e0 (14 insns, thiscall on the slot):
//   CALL IsMPServer / TEST AL,AL / JZ -> return 1
//   MOV CL,[ESI+0x64] / SETNZ -> return selected_character[0] != 0
static bool PlayerData_HasSelectedCharacter(const PlayerData& Slot)
{
    if (!IsMPServer())
        return true;
    return Slot.selected_character.Buffer[0] != '\0';
}

// FUN_06d4eda0 (75 insns, thiscall on the slot): two default FmtStr locals,
// DisplayName = u">>>BAD   NAME<<<" (.data:06d8d40c),
// r = Reputation::Vibe::GetName(slot+0x318, fmt1, fmt2, DisplayName),
// DisplayName re-forced to the fallback when r == -2, FmtStrs destroyed,
// DisplayName returned (it aliases the vibe's own name buffer).
static const wchar_t GChatBadName[] = L">>>BAD   NAME<<<";

static const wchar_t* PlayerData_GetCharacterName(const PlayerData& Slot)
{
    const wchar_t* DisplayName = GChatBadName;
    FmtStr Name1;
    FmtStr Name2;
    if (::Reputation::Vibe::GetName(Slot.rep_vibe, Name1, Name2, DisplayName) == -2)
        DisplayName = GChatBadName;
    return DisplayName;
}

// ---------------------------------------------------------------------------
// The group verbs. Every reply goes to the client as an 8-byte control block
// on the group-control channel: IChatClient::SendCommonChatMsg(recipient, 0x00010004, 8,
// &{Type, Param}).
// ---------------------------------------------------------------------------
struct FChatGroupControl {
    unsigned int Type;    // +0x00
    unsigned int Param;   // +0x04, a ushort zero-extended (MOVZX / XOR+MOV reg16)
};

static void ChatGroup_Send(unsigned int Recipient, unsigned int Type, unsigned int Param)
{
    FChatGroupControl Payload;
    Payload.Type = Type;
    Payload.Param = Param & 0xffffu;
    g_pChatClientInterface->SendCommonChatMsg(CHAT_ID(ChatIdFromWord(Recipient)),
                                              CHAT_ID(GGroupChatSourceId), sizeof(Payload), &Payload);
}

// FUN_06d191a0 (2 insns): `MOV EAX,[ECX+0x348] / RET` -- the slot's client id.

// FUN_06d19f20 (180 insns, cdecl): Inviter asks Invitee into his group.
// Name, when given, must match the invitee's character name (_wcsicmp).
static void ChatGroup_Invite(unsigned int Inviter, unsigned int Invitee, const wchar_t* Name)
{
    // .text:06d19f42 .. 06d19f4e -- the inviter's slot is indexed unchecked.
    PlayerData& InviterSlot = Players[Inviter];

    // .text:06d19f55 .. 06d19f81 -- invitee id > 0, id-1 < MaxPlayers, slot live.
    if (!Players.is_valid(Invitee))
        return;
    PlayerData& InviteeSlot = Players[Invitee];
    if (!PlayerData_HasSelectedCharacter(InviteeSlot))      // .text:06d19f96
        return;
    if (Name != nullptr &&                                     // .text:06d19fa7
        _wcsicmp(Name, PlayerData_GetCharacterName(InviteeSlot)) != 0)
        return;

    // .text:06d19fc5 .. 06d1a00a -- a groupless inviter founds one, joining it first.
    CPlayerGroup* Group = static_cast<CPlayerGroup*>(InviterSlot.group_ptr);
    if (Group == nullptr) {
        Group = new CPlayerGroup();                            // operator new(0x30)
        Group->AddMember(Inviter);
    }

    if (InviterSlot.client_id == InviteeSlot.client_id) {     // .text:06d1a010
        ChatGroup_Send(Inviter, 0x9, InviterSlot.client_id);  // inviting yourself
        return;
    }
    if (InviteeSlot.group_mission_id != 0) {                   // .text:06d1a036
        ChatGroup_Send(InviterSlot.client_id, 0xf, InviteeSlot.client_id);
        return;
    }
    if (Group->GetMemberCount() >= 8) {                        // .text:06d1a065 [EBP+0x18]
        ChatGroup_Send(Inviter, 0xa, InviterSlot.client_id);  // group full
        return;
    }
    if (Group->IsMember(InviteeSlot.client_id)) {              // .text:06d1a0a9
        ChatGroup_Send(Inviter, 0xb, InviteeSlot.client_id);  // already a member
        return;
    }
    // .text:06d1a0b6 -- retail re-reads the inviter's group_ptr here rather than
    // reusing the local (AddMember above has set it for a fresh group).
    static_cast<CPlayerGroup*>(InviterSlot.group_ptr)->AddInvite(InviteeSlot.client_id);
    ChatGroup_Send(Inviter, 0x3, InviteeSlot.client_id);          // .text:06d1a108
    ChatGroup_Send(InviteeSlot.client_id, 0x4, Inviter);          // .text:06d1a17d
}

// FUN_06d1a2e0 (191 insns, cdecl): Joiner accepts Inviter's invitation.
static void ChatGroup_Join(unsigned int Joiner, unsigned int Inviter, const wchar_t* Name)
{
    PlayerData& JoinerSlot = Players[Joiner];                  // unchecked (.text:06d1a2ff)

    if (!Players.is_valid(Inviter))                            // .text:06d1a2fd .. 06d1a32c
        return;
    PlayerData& InviterSlot = Players[Inviter];
    if (!PlayerData_HasSelectedCharacter(InviterSlot))        // .text:06d1a341
        return;
    if (Name != nullptr &&                                     // .text:06d1a352
        _wcsicmp(Name, PlayerData_GetCharacterName(InviterSlot)) != 0)
        return;

    CPlayerGroup* const OldGroup = static_cast<CPlayerGroup*>(JoinerSlot.group_ptr);
    CPlayerGroup* const Group = static_cast<CPlayerGroup*>(InviterSlot.group_ptr);

    if (JoinerSlot.client_id == InviterSlot.client_id) {      // .text:06d1a386
        ChatGroup_Send(Joiner, 0xc, JoinerSlot.client_id);
        return;
    }
    if (Group == nullptr || !Group->IsInvited(JoinerSlot.client_id)) {   // .text:06d1a3d8
        ChatGroup_Send(Joiner, 0xd, JoinerSlot.client_id);
        return;
    }
    if (Group->IsMember(JoinerSlot.client_id)) {               // .text:06d1a3f9
        ChatGroup_Send(Joiner, 0xe, InviterSlot.client_id);
        return;
    }
    if (JoinerSlot.group_mission_id != 0) {                    // .text:06d1a430
        ChatGroup_Send(JoinerSlot.client_id, 0xf, JoinerSlot.client_id);
        return;
    }
    if (Group->GetMemberCount() >= 8) {                        // .text:06d1a46b [EBX+0x18]
        ChatGroup_Send(Joiner, 0x10, InviterSlot.client_id);
        return;
    }
    if (OldGroup != nullptr)                                   // .text:06d1a496
        OldGroup->DelMember(JoinerSlot.client_id);
    ChatGroup_Send(JoinerSlot.client_id, 0x5, InviterSlot.client_id);    // .text:06d1a4f0
    Group->AddMember(JoinerSlot.client_id);                    // .text:06d1a4fb
    Group->DelInvite(JoinerSlot.client_id);                    // .text:06d1a509
}

// FUN_06d1a6b0 (42 insns, cdecl): Player leaves his group; a groupless player
// is told so with type 0x12.
static void ChatGroup_Leave(unsigned int Player)
{
    PlayerData& Slot = Players[Player];                        // unchecked (.text:06d1a6c4)
    CPlayerGroup* const Group = static_cast<CPlayerGroup*>(Slot.group_ptr);
    if (Group != nullptr) {
        Group->DelMember(Slot.client_id);
        return;
    }
    ChatGroup_Send(Slot.client_id, 0x12, Slot.client_id);
}

// ---------------------------------------------------------------------------
// The slash-command table.
// ---------------------------------------------------------------------------

// SubmitChat's on-stack context, handed to every command callback. Layout off
// .text:06d19410 .. 06d1944b and FUN_06d1aa70's stores (+4, +8, +0xc, +0x10).
struct FChatCommandContext {
    CHAT_ID Sender;         // +0x00
    CHAT_ID Target;         // +0x04
    const void* Data;       // +0x08
    unsigned long Size;     // +0x0C
    bool bPassThrough;      // +0x10  set: continue with the rewritten message
};

// Callbacks are cdecl (the matcher cleans up with ADD ESP,8 at .text:06d18df2)
// and return AL, which the matcher ignores.
typedef bool (__cdecl* FChatCommandFn)(FChatCommandContext* Ctx, std::vector<std::wstring>* Args);

// One list element, 8 bytes in retail: {rpattern*, callback}.
//
// DEVIATION NOTE (library substitution, owner's decision 2026-09-24; see the
// GRETA / std::wregex entry in docs/re/deviations.md): retail's pattern
// is a GRETA rpattern (statically linked: ctor FUN_06d1be80, match FUN_06d1f300,
// object size 0x68), constructed with flags 0x8000 and mode 0. 0x8000 is GRETA's
// C-string mode (the subject ends at the first NUL) and carries none of NOCASE
// (1) / MULTILINE (4) / SINGLELINE (8), so matching is case-sensitive and `^`
// anchors at the start of the text only.
//
// `.` is spelled `[^\n]` in the patterns below because that is exactly GRETA's
// `.` here: FUN_06d26220 picks the dot node from flags & 0x8008 -- 0x8000 gives
// vtable PTR_06d6906c, whose matchers LAB_06d2b590 / LAB_06d2b5e0 / LAB_06d2b610
// fail only on the terminating NUL or '\n' (CMP ..,0xa) and accept everything
// else, '\r' included. ECMAScript's own `.` would also refuse '\r', U+2028 and
// U+2029. The subject is a NUL-terminated wchar_t string on both sides, so the
// NUL half of the test is the end of the input for std::regex_search as well.
//
// `^` is GRETA's beginning-of-string node here, not beginning-of-line: token 4
// (BEGIN_LINE) is built at .text:06d1df1b by testing flags & 4 (MULTILINE); with
// 0x8000 it takes FUN_06d284c0 (vtable PTR_FUN_06d6929c), whose matcher
// LAB_06d29390 fails unless the position equals the subject start (+4). MSVC's
// std::regex has no single-line `^` -- its _N_bol also matches after every '\n'
// -- so ChatCommand_Match passes match_continuous instead: every pattern below
// starts with `^`, and match_continuous confines regex_search to the one
// position where GRETA's `^` can hold, the start of the text.
//
// `\s` is spelled out by ChatCommand_SpaceClass because the two libraries
// classify differently. GRETA's `\s` (escape 's' -> token 0x15 at .text:06d28eca
// -> caseD_15 .text:06d1e4fd -> static charset FUN_06d225d0, built by FUN_06d23c70
// with mask _SPACE (8), no extra characters, not negated) tests code units
// <= 0xff against a bitset filled at .text:06d23d98 with msvcrt
// _isctype(c, _SPACE) -- the narrow "C" LC_CTYPE table, since no retail module
// sets LC_CTYPE (freelancer.exe only sets LC_NUMERIC) -- and code units > 0xff
// with msvcrt iswctype(c, _SPACE) (.text:06d2997d and siblings), which for
// c >= 0x100 is GetStringTypeW(CT_CTYPE1) & C1_SPACE. UCRT's ctype<wchar_t>
// (what std::wregex's `\s` uses) is GetStringTypeW for every code unit, so it
// would also accept U+0085 and U+00A0, which retail rejects.
//
// So the class is built the way retail classifies: the "C" table's spaces for
// the low half (0x09-0x0D and 0x20; measured with msvcrt _isctype, which in the
// "C" locale marks nothing else in 0x00-0xff), and GetStringTypeW for every code
// unit from 0x100 up, asked of the running OS just as retail's iswctype asks it
// per character. On Windows 10 19045 that adds U+1680, U+180E, U+2000-U+200A,
// U+2028, U+2029, U+202F, U+205F and U+3000 -- the same list msvcrt iswctype
// returns over 0x100-0xffff there. None of them is ']', '^', '-' or the escape
// character, so they are plain members of the bracket expression.
static std::wstring ChatCommand_SpaceClass()
{
    std::wstring Class;
    Class.push_back(L'[');
    Class.push_back(static_cast<wchar_t>(0x09));   // 0x09 .. 0x0D
    Class.push_back(L'-');
    Class.push_back(static_cast<wchar_t>(0x0d));
    Class.push_back(static_cast<wchar_t>(0x20));
    for (unsigned int Code = 0x100; Code <= 0xffff; ++Code) {
        const wchar_t Ch = static_cast<wchar_t>(Code);
        WORD Type = 0;
        if (GetStringTypeW(CT_CTYPE1, &Ch, 1, &Type) != 0 && (Type & C1_SPACE) != 0)
            Class.push_back(Ch);
    }
    Class.push_back(L']');
    return Class;
}

struct FChatCommand {
    std::wregex* Pattern;
    FChatCommandFn Callback;
};

// .data:DAT_06d8f5bc: a VC6 std::list built by LAB_06d18a20 (sentinel of 0x10
// bytes, size 0) whose atexit FUN_06d18a60 frees the nodes but never the
// patterns -- they are deliberately leaked here too.
static std::list<FChatCommand> GChatCommands;
static bool GChatCommandsBuilt = false;   // .data:DAT_06d8f610

// FUN_06d18ac0 (93 insns, thiscall, `RET 0x8`): entry {null, Callback}; a null
// pattern string leaves Pattern null, otherwise new rpattern(wstring(Text), 0x8000, 0).
// A failed allocation also leaves it null (.text:06d18b0c CMP EDI,0 / JZ 06d18b8e:
// VC6 operator new returns null), hence the nothrow new.
static FChatCommand ChatCommand_Make(const wchar_t* Text, FChatCommandFn Callback)
{
    FChatCommand Entry;
    Entry.Pattern = nullptr;
    Entry.Callback = Callback;
    if (Text != nullptr)
        Entry.Pattern = new (std::nothrow) std::wregex(Text, std::regex_constants::ECMAScript);
    return Entry;
}

// FUN_06d18be0 (203 insns, thiscall on the entry, `RET 0x8`).
//   null pattern                -> false (.text:06d18c06)
//   no match                    -> false (.text:06d18e46)
//   match, null callback        -> true  (.text:06d18c60)
//   match                       -> build a vector<wstring> with one string per
//                                  backref (0 = whole match), call the callback,
//                                  destroy the vector, true -- whatever it returned.
static bool ChatCommand_Match(const FChatCommand& Entry, FChatCommandContext* Ctx, const wchar_t* Text)
{
    if (Entry.Pattern == nullptr)
        return false;
    std::wcmatch Match;
    // match_continuous = GRETA's beginning-of-string `^` (see FChatCommand).
    if (!std::regex_search(Text, Match, *Entry.Pattern, std::regex_constants::match_continuous))
        return false;
    if (Entry.Callback == nullptr)
        return true;
    // .text:06d18c66 .. 06d18dd0 -- count = backrefs, each string assigned from
    // [first, second) (FUN_06d276f0 = distance).
    std::vector<std::wstring> Args(Match.size());
    for (size_t Index = 0; Index < Match.size(); ++Index)
        Args[Index].assign(Match[Index].first, Match[Index].second);
    Entry.Callback(Ctx, &Args);
    return true;
}

// ---------------------------------------------------------------------------
// Server_SendStyledChat -- retail FUN_06d18ea0 (.text:06d18ea0 .. .text:06d190c0,
// cdecl, 161 insns). Only caller: CPlayerGroup::SendChat (.text:06d5bec0).
// Ported 1:1 2026-09-24 (claude-code:w5-server-06d4).
//   .text:06d18ed3  vswprintf(local[0x400 wchar], Fmt, varargs) -- unbounded
//   .text:06d18f0c  StyleNode(Style) -> cloned (vt+0x1c) and appended to a local
//                   RenderDisplayList, clone released, temporary destroyed
//   .text:06d18f9a  TextNode(local, -1) (the wchar_t const* overload) -> same
//   .text:06d19042  DAT_06d8f5b8.write_buffer(rdl, DAT_06d8f1b8, 0x400, &size)
//   .text:06d1906c  on success only: DAT_06d8fb18->SendCommonChatMsg(Target, DAT_06d8f5f0
//                   (console id), size, DAT_06d8f1b8) -- no null test on the
//                   interface, no notice on failure
//   .text:06d1906e .. 06d190a1  the list nodes and head freed
// The two append sequences are FUN_06d1b1b0's body inlined, i.e. Server_RdlAppend.
// ---------------------------------------------------------------------------
void __cdecl Server_SendStyledChat(unsigned short Style, unsigned int Target,
                                   const wchar_t* Fmt, ...)
{
    wchar_t Text[0x400];
    va_list Args;
    va_start(Args, Fmt);
    _vswprintf(Text, Fmt, Args);                               // .text:06d18ed3
    va_end(Args);

    RenderDisplayList Rdl;
    {
        StyleNode Node(Style);                                 // .text:06d18f0c
        Server_RdlAppend(Rdl, Node);
    }
    {
        TextNode Node(static_cast<const wchar_t*>(Text), -1);  // .text:06d18f9a
        Server_RdlAppend(Rdl, Node);
    }
    unsigned int Size = 0;
    if (GChatCommandWriter.write_buffer(Rdl, GChatCommandBuffer, 0x400, Size)) {   // .text:06d19042
        g_pChatClientInterface->SendCommonChatMsg(CHAT_ID(Target), CHAT_ID(GChatIdConsole), Size,
                                                  GChatCommandBuffer);              // .text:06d1906c
    }
}

// FUN_06d1aa70 (145 insns, cdecl): rewrite the message for a channel command --
// target := Channel, payload := StyleNode(Style) + TextNode(Message) encoded into
// DAT_06d8f1b8, and ask SubmitChat to carry on with it.
static void ChatCommand_Redirect(FChatCommandContext* Ctx, const std::wstring& Message,
                                 unsigned int Channel, unsigned short Style)
{
    Ctx->Target = CHAT_ID(Channel);                            // .text:06d1aa9c
    RenderDisplayList Rdl;
    {
        StyleNode Node(Style);
        Server_RdlAppend(Rdl, Node);
    }
    {
        TextNode Node(Message, -1);                            // basic_string overload, .text:06d1ab25
        Server_RdlAppend(Rdl, Node);
    }
    unsigned int Size = 0;
    if (GChatCommandWriter.write_buffer(Rdl, GChatCommandBuffer, 0x400, Size)) {
        Ctx->Data = GChatCommandBuffer;                        // .text:06d1abc2
        Ctx->Size = Size;
        Ctx->bPassThrough = true;
    } else {
        ChatNotice(0x21e, "Failed to re-encode chat message.\n");
    }
}

// FUN_06d1a800 (173 insns): "/echo <text>" -- send the text back to the sender
// alone, styled 0x3ea8. Swallows the message.
static bool __cdecl ChatCommand_Echo(FChatCommandContext* Ctx, std::vector<std::wstring>* Args)
{
    RenderDisplayList Rdl;
    {
        StyleNode Node(static_cast<unsigned short>(0x3ea8));
        Server_RdlAppend(Rdl, Node);
    }
    if (Args->size() == 2) {                                   // .text:06d1a8b0
        TextNode Node((*Args)[1], -1);                         // basic_string overload, .text:06d1a8bf
        Server_RdlAppend(Rdl, Node);
    } else {
        TextNode Node(L"invalid argument count", -1);
        Server_RdlAppend(Rdl, Node);
    }
    unsigned int Size = 0;
    if (GChatCommandWriter.write_buffer(Rdl, GChatCommandBuffer, 0x400, Size)) {
        g_pChatClientInterface->SendCommonChatMsg(Ctx->Sender, Ctx->Sender, Size, GChatCommandBuffer);
    } else {
        ChatNotice(0x1ff, "Failed to encode echo message.\n");
    }
    return true;
}

// LAB_06d1a1a0 (103 insns): "/invite <name>", "/i <name>". Only players (byte 2
// of the sender clear); the name is looked up among account characters.
static bool __cdecl ChatCommand_Invite(FChatCommandContext* Ctx, std::vector<std::wstring>* Args)
{
    if (((Ctx->Sender.Id >> 16) & 0xffu) != 0)
        return true;
    if (Args->size() < 3)
        return false;
    CiWString Name((*Args)[2].c_str());
    CAccount* const Account = Players.FindAccountFromCharacterName(Name);
    if (Account != nullptr && Account->m_ClientID != 0)       // CAccount+0x34
        ChatGroup_Invite(Ctx->Sender.GetPlayerId(), Account->m_ClientID, (*Args)[2].c_str());
    return true;
}

// LAB_06d1a570 (103 insns): "/join <name>", "/j <name>" -- the invite handler's
// twin, calling the join verb.
static bool __cdecl ChatCommand_Join(FChatCommandContext* Ctx, std::vector<std::wstring>* Args)
{
    if (((Ctx->Sender.Id >> 16) & 0xffu) != 0)
        return true;
    if (Args->size() < 3)
        return false;
    CiWString Name((*Args)[2].c_str());
    CAccount* const Account = Players.FindAccountFromCharacterName(Name);
    if (Account != nullptr && Account->m_ClientID != 0)
        ChatGroup_Join(Ctx->Sender.GetPlayerId(), Account->m_ClientID, (*Args)[2].c_str());
    return true;
}

// LAB_06d1a740 (60 insns): "/leave", "/lv" -- FUN_06d1a6b0 inlined on the sender.
static bool __cdecl ChatCommand_Leave(FChatCommandContext* Ctx, std::vector<std::wstring>* Args)
{
    if (((Ctx->Sender.Id >> 16) & 0xffu) != 0)
        return true;
    if (Args->size() < 2)
        return false;
    ChatGroup_Leave(Ctx->Sender.GetPlayerId());
    return true;
}

// LAB_06d1aa30 / LAB_06d1ac50 / LAB_06d1ac90 / LAB_06d1acd0 (19 insns each):
// exactly three backrefs, else nothing; always true.
static bool __cdecl ChatCommand_Universe(FChatCommandContext* Ctx, std::vector<std::wstring>* Args)
{
    if (Args->size() == 3)
        ChatCommand_Redirect(Ctx, (*Args)[2], GChatIdUniverse, 0xd681);
    return true;
}

static bool __cdecl ChatCommand_System(FChatCommandContext* Ctx, std::vector<std::wstring>* Args)
{
    if (Args->size() == 3)
        ChatCommand_Redirect(Ctx, (*Args)[2], GChatIdSystem, 0xd801);
    return true;
}

static bool __cdecl ChatCommand_Local(FChatCommandContext* Ctx, std::vector<std::wstring>* Args)
{
    if (Args->size() == 3)
        ChatCommand_Redirect(Ctx, (*Args)[2], GChatIdLocal, 0x1d9d);
    return true;
}

static bool __cdecl ChatCommand_Group(FChatCommandContext* Ctx, std::vector<std::wstring>* Args)
{
    if (Args->size() == 3)
        ChatCommand_Redirect(Ctx, (*Args)[2], GChatIdGroup, 0xba28);
    return true;
}

// ---------------------------------------------------------------------------
// IServerImpl::SubmitChat (0x06d191b0, 906 insns, `RET 0x14`).
// ---------------------------------------------------------------------------
void Server_SubmitChat(CHAT_ID Sender, unsigned long Size, const void* Data, CHAT_ID Target, int SystemId)
{
    // 1. .text:06d191be .. 06d19328 -- eight push_backs through FUN_06d1b2d0, once.
    if (!GChatCommandsBuilt) {
        // Retail .rdata strings (06d6898c .. 06d6888c), read from the PE:
        // u"^echo\s+(.*)", u"^(invite|i)\s+(.*)", u"^(join|j)\s+(.*)", u"^(leave|lv)",
        // u"^(universe|u)\s+(.*)", u"^(system|s)\s+(.*)", u"^(local|l)\s+(.*)",
        // u"^(group|g)\s+(.*)" -- here with `\s` = ChatCommand_SpaceClass() and
        // `.` = [^\n] (see FChatCommand). ChatCommand_Make copies the pattern into
        // the regex, so the temporaries may die at the end of each statement.
        const std::wstring Space = ChatCommand_SpaceClass();
        GChatCommands.push_back(ChatCommand_Make((L"^echo" + Space + L"+([^\n]*)").c_str(), ChatCommand_Echo));
        GChatCommands.push_back(ChatCommand_Make((L"^(invite|i)" + Space + L"+([^\n]*)").c_str(), ChatCommand_Invite));
        GChatCommands.push_back(ChatCommand_Make((L"^(join|j)" + Space + L"+([^\n]*)").c_str(), ChatCommand_Join));
        GChatCommands.push_back(ChatCommand_Make(L"^(leave|lv)", ChatCommand_Leave));
        GChatCommands.push_back(ChatCommand_Make((L"^(universe|u)" + Space + L"+([^\n]*)").c_str(), ChatCommand_Universe));
        GChatCommands.push_back(ChatCommand_Make((L"^(system|s)" + Space + L"+([^\n]*)").c_str(), ChatCommand_System));
        GChatCommands.push_back(ChatCommand_Make((L"^(local|l)" + Space + L"+([^\n]*)").c_str(), ChatCommand_Local));
        GChatCommands.push_back(ChatCommand_Make((L"^(group|g)" + Space + L"+([^\n]*)").c_str(), ChatCommand_Group));
        GChatCommandsBuilt = true;
    }

    // 2. .text:06d19336 .. 06d1939e -- group control: {verb, player id}.
    if (Target.Id == GGroupChatSourceId) {
        if (Size != 8)
            return;
        const unsigned int* const Control = static_cast<const unsigned int*>(Data);
        switch (Control[0]) {
        case 0:
            ChatGroup_Invite(Sender.GetPlayerId(), Control[1], nullptr);
            return;
        case 1:
            ChatGroup_Leave(Sender.GetPlayerId());
            return;
        case 2:
            ChatGroup_Join(Sender.GetPlayerId(), Control[1], nullptr);
            return;
        default:
            return;
        }
    }

    // 3. .text:06d193ce .. 06d193f2 -- decode.
    //
    // RETAIL READS AN UNINITIALISED LOCAL HERE. The test that gates the command
    // pass is `CMP [ESP+0x24],0` (.text:06d193f8) on the out-parameter
    // extract_text_from_buffer was handed (local_54), but that function never
    // writes it -- Common.dll 0x0632a8a0 reads only params 1, 2, 4 and 5 -- and
    // nothing in SubmitChat's frame writes [B+0x24] before the read (B = ESP
    // after the four register pushes): the table build stores its list::insert
    // iterator at B+0x20 (LEA [ESP+0x28] with ESP = B-8, FUN_06d1b2d0 writes 4
    // bytes) and its entry temporary at B+0x30 (LEA [ESP+0x38], ESP = B-8), and
    // callees run below B. Retail therefore tests stale stack left by whatever
    // ran at that depth before the call -- in practice a pointer or return
    // address, i.e. non-zero; a zero leftover would silently skip the command
    // pass. The port seeds it non-zero, so the gate reduces to the other half,
    // `Text[0] == '/'` (.text:06d19402). Ledger: the GRETA /
    // std::wregex entry in docs/re/deviations.md.
    unsigned int DecodedSize = 1;
    GChatReader.extract_text_from_buffer(GChatText, 0x400, DecodedSize,
                                         static_cast<const char*>(Data), Size);

    // 4. .text:06d19410 .. 06d194e9 -- the command pass, walking the table
    // BACKWARDS (FUN_06d5c540 is begin(); the loop steps through node->prev),
    // matching against the text after the '/'.
    if (DecodedSize != 0 && GChatText[0] == L'/') {
        FChatCommandContext Ctx;
        Ctx.Sender = Sender;
        Ctx.Target = Target;
        Ctx.Data = Data;
        Ctx.Size = Size;
        Ctx.bPassThrough = false;
        std::list<FChatCommand>::reverse_iterator It = GChatCommands.rbegin();
        for (; It != GChatCommands.rend(); ++It) {
            if (ChatCommand_Match(*It, &Ctx, GChatText + 1)) {
                if (!Ctx.bPassThrough)
                    return;                                    // .text:06d194c1
                Sender = Ctx.Sender;                           // .text:06d194c7 .. 06d194e9
                Target = Ctx.Target;
                Data = Ctx.Data;
                Size = Ctx.Size;
                break;
            }
        }
    }

    // 5. .text:06d1951e .. 06d197ad -- "<name>: " header.
    RenderDisplayList Rdl;                                     // FUN_06d1ad30 / FUN_06d1ad60
    const unsigned int SenderWord = Sender.GetPlayerId();
    // .text:06d19530 .. 06d19575 -- id > 0, id-1 < MaxPlayers, client_id != 0.
    PlayerData* const SenderSlot = Players.is_valid(SenderWord) ? &Players[SenderWord] : nullptr;
    if (SenderSlot != nullptr) {
        {
            StyleNode Node(static_cast<unsigned short>(0xd5ec));
            Server_RdlAppend(Rdl, Node);
        }
        if (PlayerData_HasSelectedCharacter(*SenderSlot)) {
            TextNode Node(PlayerData_GetCharacterName(*SenderSlot), -1);
            Server_RdlAppend(Rdl, Node);
        } else {
            TextNode Node(L"<unnamed>", -1);
            Server_RdlAppend(Rdl, Node);
        }
    } else {
        {
            StyleNode Node(static_cast<unsigned short>(0x6c0e));
            Server_RdlAppend(Rdl, Node);
        }
        {
            TextNode Node(L"CONSOLE", -1);
            Server_RdlAppend(Rdl, Node);
        }
    }
    {
        TextNode Node(L": ", -1);
        Server_RdlAppend(Rdl, Node);
    }
    unsigned int EncodedSize = 0;
    if (!GChatWriter.write_buffer(Rdl, GChatSendBuffer, 0x400, EncodedSize)) {
        ChatNotice(0x2d6, "Failed to encode chat sender name.\n");
        return;
    }

    // 6. .text:06d197b2 .. 06d19954 -- append the caller's payload after the
    // header when it fits (32-bit sum, JBE), else append a red notice to the
    // header list and re-encode the whole thing.
    if (EncodedSize + Size <= 0x400) {
        memcpy(GChatSendBuffer + EncodedSize, Data, Size);     // REP MOVSD / MOVSB
        EncodedSize = EncodedSize + Size;
    } else {
        // .text:06d197cb .. 06d197ec -- the attribute dword is assembled on the
        // stack: bytes 1..3 = 0xbf,0x1d,0x1d (AND 0xff1d1dbf / OR 0x1d1dbf on the
        // unaligned dword at +1), byte 0 = 0x11; TRANode(attrs, 0xffffffff, 0).
        TextRenderAttributes Attrs;
        Attrs.Packed = 0x1d1dbf11u;
        {
            TRANode Node(Attrs, 0xffffffffu, 0u);
            Server_RdlAppend(Rdl, Node);
        }
        {
            TextNode Node(L"<message too big>", -1);
            Server_RdlAppend(Rdl, Node);
        }
        if (!GChatWriter.write_buffer(Rdl, GChatSendBuffer, 0x400, EncodedSize)) {
            ChatNotice(0x2e7, "Failed to encode \"too big chat\" error message.\n");
            return;
        }
    }

    // 7. .text:06d19958 .. 06d19974 -- decode the final message for the hooks.
    GChatReader.extract_text_from_buffer(GChatText, 0x400, DecodedSize,
                                         GChatSendBuffer, EncodedSize);
    const void* const SendBuffer = GChatSendBuffer;

    // 8. .text:06d1997a / .text:06d19df8 -- byte 2 clear: a private message.
    if (((Target.Id >> 16) & 0xffu) == 0) {
        if (g_pPrivateChatHook != nullptr) {
            if (Sender.Id == GChatIdConsole)                   // .text:06d19e12
                g_pPrivateChatHook(GChatText, static_cast<int>(Target.Id & 0xffffu));
            else if (Target.Id == GChatIdConsole)              // .text:06d19e1a
                g_pPrivateChatHook(GChatText, static_cast<int>(SenderWord));
        }
        // .text:06d19e2d .. 06d19e68 -- to the target, when it is a live player ...
        if (Players.is_valid(Target.Id & 0xffffu))
            g_pChatClientInterface->SendCommonChatMsg(Target, Sender, EncodedSize, SendBuffer);
        // .text:06d19e6a -- ... and always back to the sender.
        g_pChatClientInterface->SendCommonChatMsg(Sender, Target, EncodedSize, SendBuffer);
        return;
    }

    // 9. .text:06d19990 -- universe: admins and the console only.
    if (Target.Id == GChatIdUniverse) {
        if (SenderSlot != nullptr &&
            !static_cast<CAccount*>(SenderSlot->account_ptr)->m_bAdmin) {   // [+0x400]->+0x32
            ChatNotice(0x31e, "Universe chat attempted from an account without the proper rights.\n");
            return;
        }
        if (g_pUniverseChatHook != nullptr)
            g_pUniverseChatHook(GChatText, static_cast<int>(SenderWord));
        for (PlayerData* Slot = Players.traverse_active(nullptr); Slot != nullptr;
             Slot = Players.traverse_active(Slot)) {
            g_pChatClientInterface->SendCommonChatMsg(CHAT_ID(ChatIdFromWord(Slot->client_id)), Target,
                                                      EncodedSize, SendBuffer);
        }
        return;
    }

    // 10. .text:06d19a6d -- system: the sender's system and/or SystemId.
    if (Target.Id == GChatIdSystem) {
        if (SenderSlot == nullptr && SystemId == -1) {
            ChatNotice(0x340, "System chat attempted without a player or console system.\n");
            return;
        }
        // Retail fetches the first active slot before calling the hook (.text:06d19ae1).
        PlayerData* Slot = Players.traverse_active(nullptr);
        if (g_pSystemChatHook != nullptr)
            g_pSystemChatHook(GChatText, SenderSlot != nullptr
                                             ? static_cast<int>(SenderSlot->system_id) : SystemId);
        for (; Slot != nullptr; Slot = Players.traverse_active(Slot)) {
            if ((SenderSlot != nullptr && Slot->system_id == SenderSlot->system_id) ||
                static_cast<int>(Slot->system_id) == SystemId) {
                g_pChatClientInterface->SendCommonChatMsg(CHAT_ID(ChatIdFromWord(Slot->client_id)), Target,
                                                          EncodedSize, SendBuffer);
            }
        }
        return;
    }

    // 11. .text:06d19b80 -- local: the players whose objects the sender's own
    // view has already created on his client, then the sender himself.
    if (Target.Id == GChatIdLocal) {
        if (SenderSlot == nullptr) {
            ChatNotice(0x36c, "Local chat attempted from console.\n");
            return;
        }
        if (g_pSystemChatHook != nullptr)
            g_pSystemChatHook(GChatText, static_cast<int>(SenderSlot->system_id));
        SysDB::SysMapType::iterator Found = SysDB::SysMap.find(SenderSlot->system_id);   // FUN_06d15d10
        if (Found != SysDB::SysMap.end()) {
            std::set<unsigned int> Owners;                     // FUN_06d1b150
            StarSystem_CollectCreatedOwners(&Found->second, SenderWord, &Owners);
            std::set<unsigned int>::const_iterator It = Owners.begin();
            for (; It != Owners.end(); ++It) {
                g_pChatClientInterface->SendCommonChatMsg(CHAT_ID(ChatIdFromWord(*It)), Target,
                                                          EncodedSize, SendBuffer);
            }
        }
        g_pChatClientInterface->SendCommonChatMsg(Sender, Target, EncodedSize, SendBuffer);
        return;
    }

    // 12. .text:06d19d5d -- group.
    if (Target.Id == GChatIdGroup) {
        if (SenderSlot != nullptr && SenderSlot->group_ptr != nullptr) {
            static_cast<CPlayerGroup*>(SenderSlot->group_ptr)->DeliverChat(EncodedSize, GChatSendBuffer);
        } else {
            ChatNotice(0x37a, "Console group chat attempted. Feature is not implemented.\n");
        }
        return;
    }

    // 13. .text:06d19dce
    ChatNotice(0x381, "Received message to unknown chat list. Ignoring.\n");
}
