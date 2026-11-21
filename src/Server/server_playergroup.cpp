// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// server_playergroup.cpp -- CPlayerGroup.
// Split out of server_classes.cpp unchanged; the declarations the split files share are in
// server_classes_internal.h.

#include "server_pub.h"
#include "common_net.h"
#include "common_physys.h"
#include "ivp_physics/ivp_real_object.h" // IVP_Real_Object +0x1c/+0xa4 (StarSystem_MpCollisionFilter)
#include "common_cequip.h" // TractorArm
#include "common_casteroid.h" // CmnAsteroid::CAsteroidSystem (SrvAsteroidSystem AddRef/Release/get_sys_id)
#include "common_id.h"
#include "common_server_playerdb.h"
#include "server_savegame.h" // PlayerData_CreateNewCharacter (retail FUN_06d4b790)
#include "server_savefile.h" // Server_AccountDirName / Server_GetAccountsRoot (FUN_06d49f60)
#include "common_ini.h"
#include "common_misc.h"
#include "x86math.h" // I3DMathEngine
#include "common_universe.h"
#include "common_reputation.h"
#include "server_sysdb.h"
#include "server_gameobject.h" // GameObject's +0x154..+0x160 slots (StarSystem::remove_player/unload)
#include "common_cobjdb.h"  // IObjDB::GetZoneDb (SrvAsteroid_UpdateSystem, FUN_06d03480)
#include "DALib.h"          // DALib::Data (SrvAsteroid's GetFileSysPath calls)
#include "readfile.h"       // IFileSystem::GetFileSysPath (+0x84), create_restart_file
#include "server_createinfo.h"
#include "server_characterset.h" // CCharacterSet (FUN_06d499c0)
#include "common_goods.h"    // AmbientScriptProperties (server_basedb.h uses it by value)
#include "server_basedb.h"
#include "rpc_iclient.h"
#include "rpc_iclient_args.h" // FServerSetReputationInfo / FPayloadServerActivateObject (add_player MP walk)
#include "md5hash.h"      // MD5HASH_API: Server exports its implicit members like retail
#include <windows.h>
#include <mmsystem.h>
#include "../include/platform.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <string>
#include <vector>
#include <iterator>
#include <list>
#include <map>
#include <set>
#include <new>          // placement new: Observer is constructed into a raw
                        // player-list node, as std::list's _Construct does
#include <io.h>
#include <direct.h>
#include <wchar.h>
#include <stdarg.h>
#include <algorithm>
#include <unordered_map> // PORT(perf-index): the ObjectList blocks' id index (find_by_id)
#include <cassert>       // PORT(perf-index): the id index's _DEBUG cross-check
#include "server_classes_internal.h"

// ===========================================================================
// CPlayerGroup class implementation
// ===========================================================================

// (`Players` is declared in server_pub.h and defined in server_classes.cpp;
// CPlayerGroup's member add/remove paths route the group-id notification
// through it, as retail does on .data:Players.)


// Retail .data:06d90450, zero-initialised: the ctor pre-increments and the dtor
// gives the id back when it was the last one handed out.
unsigned int CPlayerGroup::s_uiGroupID = 0;

// ---------------------------------------------------------------------------
// CPlayerGroup::CPlayerGroup — retail 0x7b320 (0x06d5b320), ordinal 7
// Two empty std::set<uint> trees, then
//     m_GroupID = ++s_uiGroupID;                       (0x06d5b3f3-0x06d5b400)
//     s_GroupIDToGroupPtrMap[m_GroupID] = this;        (0x06d5b40d-0x06d5b42c)
// Note the PRE-increment: retail reads s_uiGroupID, INCs, stores it back and
// only then copies it into [EDI+4].
// ---------------------------------------------------------------------------
CPlayerGroup::CPlayerGroup() {
    m_GroupID = ++s_uiGroupID;
    s_GroupIDToGroupPtrMap[m_GroupID] = this;
}

// ---------------------------------------------------------------------------
// CPlayerGroup::CPlayerGroup(const CPlayerGroup&) — retail 0x1100 (0x06ce1100),
// ordinal 6, and CPlayerGroup::operator= — retail 0x1280 (0x06ce1280), ordinal
// 26. Both are exported and neither has a single call site inside Server.dll
// (their only XREFs are the entry-point table and the export thunks at
// 0x06d8004c / 0x06d808cc): the familiar exported-but-uncalled special-member
// pattern. Retail copies m_GroupID (0x06ce111e) and both sets, and does NOT
// register the copy in s_GroupIDToGroupPtrMap.
// ---------------------------------------------------------------------------
CPlayerGroup::CPlayerGroup(const CPlayerGroup& Other) {
    m_GroupID = Other.m_GroupID;
    m_Members = Other.m_Members;
    m_Invites = Other.m_Invites;
}

CPlayerGroup& CPlayerGroup::operator=(const CPlayerGroup& Other) {
    if (this != &Other) {
        m_GroupID = Other.m_GroupID;
        m_Members = Other.m_Members;
        m_Invites = Other.m_Invites;
    }
    return *this;
}

// ---------------------------------------------------------------------------
// CPlayerGroup::~CPlayerGroup — retail 0x7b440 (0x06d5b440), ordinal 20
// 0x06d5b467-0x06d5b482:  if (m_GroupID == s_uiGroupID) --s_uiGroupID;
// 0x06d5b487-0x06d5b4cb:  GGroupIDToGroupPtrMap.erase(m_GroupID)
// then both trees are torn down. The id give-back was missing from the port.
// ---------------------------------------------------------------------------
CPlayerGroup::~CPlayerGroup() {
    if (m_GroupID == s_uiGroupID) {
        --s_uiGroupID;
    }
    s_GroupIDToGroupPtrMap.erase(m_GroupID);
}

// ---------------------------------------------------------------------------
// CPlayerGroup::FromGroupID — retail 0x7b5c0 (0x06d5b5c0), ordinal 138
// lower_bound + the usual !(key < it->first) recheck (0x06d5b5e4), then
// it->second from node+0x10 (0x06d5b603).
// ---------------------------------------------------------------------------
CPlayerGroup* CPlayerGroup::FromGroupID(unsigned int GroupId) {
    GroupMapType::iterator It = s_GroupIDToGroupPtrMap.find(GroupId);
    if (It != s_GroupIDToGroupPtrMap.end()) {
        return It->second;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// CPlayerGroup::AddMember -- retail .text:06d5b610 .. .text:06d5b90c, ordinal 53,
// thiscall (PlayerId), RET 4. Rewritten 2026-09-24 (claude-code:w5-server-06d4)
// from the listing; every branch below is retail's.
//   06d5b61d..06d5b63a  Joiner = &Players[PlayerId] (raw IMUL 0x418, no guard)
//   06d5b63e..06d5b690  for each member M: FUN_06d190d0(CHAT_ID{M}, 6,
//                       CHAT_ID{PlayerId}) -- the dword pushed at 06d5b657 is the
//                       joiner's id as a word plus two zero bytes
//   06d5b692..06d5b6df  for each member M: FUN_06d190d0(CHAT_ID{PlayerId}, 6,
//                       CHAT_ID{M})
//   06d5b6e1..06d5b726  GroupMission = the first member's non-zero +0x408, else 0
//   06d5b72a..06d5b784  GroupSetBy   = the first member's non-zero +0x40c, else 0
//                       (a separate scan)
//   06d5b786..06d5b796  if Joiner+0x408 == 0 || GroupMission != 0:
//     06d5b8e7..06d5b906  Joiner+0x408 = GroupMission, Joiner+0x40c = GroupSetBy,
//                         IClient +0x1c8 (SendServerMiscObjUpdateMission)(PlayerId,
//                         GroupMission, GroupSetBy)
//                       else (the joiner brings a mission into a group without one):
//     06d5b79c..06d5b808  every member gets the joiner's +0x408/+0x40c and a
//                         SendServerMiscObjUpdateMission(member, those two)
//   06d5b81b            m_Members.insert(PlayerId)
//   06d5b841            PlayerDB::SendGroupID(PlayerId, m_GroupID)
//   06d5b84a            Joiner+0x404 (group_ptr) = this
//   06d5b85b            FUN_06d43650(0xa, &{PlayerId}) -- controller broadcast
//   06d5b871..06d5b8d6  FUN_06d006c0(&Joiner+0x3e4 ship id, &obj, &owner); on a hit
//                       with obj != 0, obj->cobject() (vt +0x150) non-null and a
//                       CShip ((class & 0x503) == 0x503): ship+0x1b4 = m_GroupID,
//                       then SysDB::SysMap.find(Joiner+0x3e0); when found,
//                       FUN_06d0f1c0(&system, ship id) -- StarSystem_SendGroupChange
//   06d5b8de            return true -- no IsFull test anywhere (the callers do it)
// No IChatClient / IClient null test anywhere. FUN_06d006c0 is
// Server_LookupGameObject in this port (null on a miss covers both early-outs).
// ---------------------------------------------------------------------------
bool CPlayerGroup::AddMember(unsigned int PlayerId) {
    PlayerData& Joiner = Players[PlayerId];                                 // .text:06d5b62b

    std::set<unsigned int>::const_iterator It;
    for (It = m_Members.begin(); It != m_Members.end(); ++It) {            // .text:06d5b650
        FGroupMsgPayload Payload;
        Payload.MsgType = (unsigned int)BaseGroupMessage::MemberJoined;    // 6
        Payload.Param = MakeChatId(PlayerId);                              // .text:06d5b657
        g_pChatClientInterface->SendCommonChatMsg(CHAT_ID(MakeChatId(*It)), CHAT_ID(GGroupChatSourceId),
                                                  sizeof(Payload), &Payload);
    }
    for (It = m_Members.begin(); It != m_Members.end(); ++It) {            // .text:06d5b6a0
        FGroupMsgPayload Payload;
        Payload.MsgType = (unsigned int)BaseGroupMessage::MemberJoined;    // 6
        Payload.Param = MakeChatId(*It);                                   // .text:06d5b6b2
        g_pChatClientInterface->SendCommonChatMsg(CHAT_ID(MakeChatId(PlayerId)), CHAT_ID(GGroupChatSourceId),
                                                  sizeof(Payload), &Payload);
    }

    unsigned int GroupMission = 0;                                         // .text:06d5b728
    for (It = m_Members.begin(); It != m_Members.end(); ++It) {
        if (Players[*It].group_mission_id != 0) {                          // .text:06d5b70e
            GroupMission = Players[*It].group_mission_id;                  // .text:06d5b774
            break;
        }
    }
    unsigned int GroupSetBy = 0;                                           // .text:06d5b784
    for (It = m_Members.begin(); It != m_Members.end(); ++It) {
        if (Players[*It].group_mission_set_by != 0) {                      // .text:06d5b752
            GroupSetBy = Players[*It].group_mission_set_by;                // .text:06d5b77c
            break;
        }
    }

    unsigned int const JoinerMission = Joiner.group_mission_id;            // .text:06d5b786
    if (JoinerMission == 0 || GroupMission != 0) {
        Joiner.group_mission_id = GroupMission;                            // .text:06d5b8ec
        Joiner.group_mission_set_by = GroupSetBy;                          // .text:06d5b8f2
        g_pClientInterface->SendServerMiscObjUpdateMission(PlayerId, GroupMission, GroupSetBy); // .text:06d5b906
    } else {
        unsigned int const JoinerSetBy = Joiner.group_mission_set_by;      // .text:06d5b7a0
        for (It = m_Members.begin(); It != m_Members.end(); ++It) {
            PlayerData& Member = Players[*It];
            Member.group_mission_id = JoinerMission;                       // .text:06d5b7ce
            Member.group_mission_set_by = JoinerSetBy;                     // .text:06d5b7d4
            g_pClientInterface->SendServerMiscObjUpdateMission(*It, JoinerMission, JoinerSetBy); // .text:06d5b7ec
        }
    }

    m_Members.insert(PlayerId);                                            // .text:06d5b81b
    Players.SendGroupID(PlayerId, m_GroupID);                              // .text:06d5b841
    Joiner.group_ptr = this;                                               // .text:06d5b84a
    unsigned int JoinEvent = PlayerId;                                     // .text:06d5b857
    Server_ControllerDispatch(0, pub::Controller::EMessage::GroupMemberJoined, &JoinEvent); // .text:06d5b85b

    // .text:06d5b860 .. .text:06d5b8d6 -- stamp the group on the joiner's ship.
    GameObject* const ShipObj = Server_LookupGameObject(Joiner.ship_object_id);
    if (ShipObj == nullptr)
        return true;
    CObject const* const Cobj = ShipObj->cobject();                        // .text:06d5b887
    if (Cobj == nullptr)
        return true;
    if ((static_cast<unsigned int>(Cobj->ObjectClass) & 0x503u) != 0x503u)
        return true;
    const_cast<CShip*>(static_cast<CShip const*>(Cobj))->GroupId = m_GroupID; // .text:06d5b8a5
    SysDB::SysMapType::iterator System = SysDB::SysMap.find(Joiner.system_id); // .text:06d5b8bf
    if (System != SysDB::SysMap.end())
        StarSystem_SendGroupChange(System->second, Joiner.ship_object_id); // .text:06d5b8d6
    return true;
}

// ---------------------------------------------------------------------------
// CPlayerGroup::DelMember -- retail .text:06d5b920 .. .text:06d5bb79, ordinal 96,
// thiscall (PlayerId), RET 4. Rewritten 2026-09-24 (claude-code:w5-server-06d4)
// from the listing; every branch below is retail's.
//   06d5b94d..06d5b95d  m_Members.find(PlayerId); == end() -> return false
//   06d5b962..06d5b971  FUN_06d43650(9, &{PlayerId}) -- controller broadcast,
//                       BEFORE the erase
//   06d5b985            m_Members.erase(it)
//   06d5b98a..06d5b9a0  Leaver = &Players[PlayerId] (raw IMUL 0x418, no guard)
//   06d5b9c0..06d5ba00  for each remaining member M: FUN_06d190d0(CHAT_ID{M}, 8,
//                       CHAT_ID{PlayerId}) -- the dword pushed at 06d5b9c0 is the
//                       leaver's id as a word plus two zero bytes
//   06d5ba02..06d5ba2d  FUN_06d190d0(CHAT_ID{PlayerId}, 7, CHAT_ID{PlayerId})
//   06d5ba35..06d5ba50  Leaver+0x408 = Leaver+0x40c = 0; IClient +0x1c8
//                       (SendServerMiscObjUpdateMission)(PlayerId, 0, 0)
//   06d5ba56..06d5ba96  a 0x40 record at [ESP+0x24]: +0x00, +0x04, +0x08 = 0,
//                       +0x3c byte = 0, two zeroed FmtStrs at +0x0c / +0x24
//                       (FUN_06d55bc0 is the inlined FmtStr() body); IClient
//                       +0x120 (SendServerSetMissionObjectives)(&PlayerId, &record) -- the id goes BY
//                       ADDRESS (LEA EAX,[ESP+0x78] = &param_1)
//   06d5baa7            PlayerDB::SendGroupID(PlayerId, 0)
//   06d5babd            Leaver+0x404 (group_ptr) = 0
//   06d5bac3..06d5baf2  FUN_06d006c0(&Leaver+0x3e4 ship id, &obj, &owner); on a hit
//                       with obj != 0, obj->cobject() (vt +0x150) non-null and a
//                       CShip ((class & 0x503) == 0x503):
//   06d5bb05              ship+0x1b4 = 0
//   06d5bb0b..06d5bb3e    SysDB::SysMap.find(Leaver+0x3e0) (FUN_06d5c810 is the
//                         lower_bound, 06d5bb1f re-tests the key); when found,
//                         FUN_06d0f1c0(&system, ship id) -- StarSystem_SendGroupChange
//   06d5bb65            return true
// No IChatClient / IClient null test anywhere. FUN_06d006c0 is
// Server_LookupGameObject in this port (null on a miss covers both early-outs).
// ---------------------------------------------------------------------------
bool CPlayerGroup::DelMember(unsigned int PlayerId) {
    std::set<unsigned int>::iterator Found = m_Members.find(PlayerId);    // .text:06d5b94d
    if (Found == m_Members.end())
        return false;                                                      // .text:06d5b95b

    unsigned int LeaveEvent = PlayerId;                                    // .text:06d5b96d
    Server_ControllerDispatch(0, pub::Controller::EMessage::GroupMemberLeft, &LeaveEvent); // .text:06d5b971
    m_Members.erase(Found);                                                // .text:06d5b985

    PlayerData& Leaver = Players[PlayerId];                                // .text:06d5b9a0

    std::set<unsigned int>::const_iterator It;
    for (It = m_Members.begin(); It != m_Members.end(); ++It) {            // .text:06d5b9c0
        FGroupMsgPayload Payload;
        Payload.MsgType = (unsigned int)BaseGroupMessage::MemberLeft;      // 8
        Payload.Param = MakeChatId(PlayerId);                              // .text:06d5b9c7
        g_pChatClientInterface->SendCommonChatMsg(CHAT_ID(MakeChatId(*It)), CHAT_ID(GGroupChatSourceId),
                                                  sizeof(Payload), &Payload);
    }
    {
        FGroupMsgPayload Payload;
        Payload.MsgType = (unsigned int)BaseGroupMessage::RemovedFromGroup; // 7
        Payload.Param = MakeChatId(PlayerId);                              // .text:06d5ba11
        g_pChatClientInterface->SendCommonChatMsg(CHAT_ID(MakeChatId(PlayerId)), CHAT_ID(GGroupChatSourceId),
                                                  sizeof(Payload), &Payload);
    }

    Leaver.group_mission_id = 0;                                           // .text:06d5ba35
    Leaver.group_mission_set_by = 0;                                       // .text:06d5ba3b
    g_pClientInterface->SendServerMiscObjUpdateMission(PlayerId, 0, 0);    // .text:06d5ba50

    FServerSetMissionObjectivesInfo NoObjectives;                          // .text:06d5ba56
    NoObjectives.Field_0x00 = 0;                                           // .text:06d5ba7b
    NoObjectives.Entries = nullptr;                                        // .text:06d5ba87
    NoObjectives.Count = 0;                                                // .text:06d5ba7f
    NoObjectives.Field_0x3c = 0;                                           // .text:06d5ba83
    g_pClientInterface->SendServerSetMissionObjectives(PlayerId, NoObjectives); // .text:06d5ba96

    Players.SendGroupID(PlayerId, 0);                                      // .text:06d5baa7
    Leaver.group_ptr = nullptr;                                            // .text:06d5babd

    // .text:06d5bac3 .. .text:06d5bb3e -- clear the group on the leaver's ship.
    GameObject* const ShipObj = Server_LookupGameObject(Leaver.ship_object_id);
    if (ShipObj == nullptr)
        return true;
    CObject const* const Cobj = ShipObj->cobject();                        // .text:06d5bad9
    if (Cobj == nullptr)
        return true;
    if ((static_cast<unsigned int>(Cobj->ObjectClass) & 0x503u) != 0x503u)
        return true;
    const_cast<CShip*>(static_cast<CShip const*>(Cobj))->GroupId = 0;     // .text:06d5bb05
    SysDB::SysMapType::iterator System = SysDB::SysMap.find(Leaver.system_id); // .text:06d5bb0b
    if (System != SysDB::SysMap.end())
        StarSystem_SendGroupChange(System->second, Leaver.ship_object_id); // .text:06d5bb3e
    return true;
}

// ---------------------------------------------------------------------------
// CPlayerGroup::IsMember — retail 0x7bb90 (0x06d5bb90), ordinal 316
// lower_bound on m_Members, then SETNZ against end() (0x06d5bbc5).
// ---------------------------------------------------------------------------
bool CPlayerGroup::IsMember(unsigned int PlayerId) {
    return m_Members.contains(PlayerId);
}

// ---------------------------------------------------------------------------
// CPlayerGroup::AddInvite — retail 0x7bbf0 (0x06d5bbf0), ordinal 52
// A plain m_Invites.insert with the hint machinery inlined; MOV AL,1 on every
// exit (0x06d5bc44 / 0x06d5bc85), so it can never fail -- there is no
// eight-invite cap, which the old port invented.
// ---------------------------------------------------------------------------
bool CPlayerGroup::AddInvite(unsigned int PlayerId) {
    m_Invites.insert(PlayerId);
    return true;
}

// ---------------------------------------------------------------------------
// CPlayerGroup::DelInvite — retail 0x7bc90 (0x06d5bc90), ordinal 95
// lower_bound, false when absent (0x06d5bcd0), otherwise erase and true.
// ---------------------------------------------------------------------------
bool CPlayerGroup::DelInvite(unsigned int PlayerId) {
    std::set<unsigned int>::iterator It = m_Invites.find(PlayerId);
    if (It == m_Invites.end()) {
        return false;
    }
    m_Invites.erase(It);
    return true;
}

// ---------------------------------------------------------------------------
// CPlayerGroup::IsInvited — retail 0x7bd00 (0x06d5bd00), ordinal 315
// ---------------------------------------------------------------------------
bool CPlayerGroup::IsInvited(unsigned int PlayerId) {
    return m_Invites.contains(PlayerId);
}

// ---------------------------------------------------------------------------
// Counters. Retail reads the two trees' _Size words directly:
//   GetMemberCount 0x7bb80 -> MOV EAX,[this+0x18]   (m_Members.size())
//   GetInviteCount 0x7bcf0 -> MOV EAX,[this+0x2c]   (m_Invites.size())
//   GetID          0x10f0  -> MOV EAX,[this+0x04]
//   IsFull         0x10e0  -> CMP [this+0x18],8 / SBB EAX,EAX / INC EAX
// All four are QAE, i.e. NOT const -- the port used to declare them const,
// which changes the exported mangled name.
// ---------------------------------------------------------------------------
unsigned int CPlayerGroup::GetMemberCount() { return (unsigned int)m_Members.size(); }
unsigned int CPlayerGroup::GetInviteCount() { return (unsigned int)m_Invites.size(); }
unsigned int CPlayerGroup::GetID() { return m_GroupID; }
bool CPlayerGroup::IsFull() { return m_Members.size() >= 8; }

// ---------------------------------------------------------------------------
// CPlayerGroup::HandleClientLogout — retail 0x7bd60 (0x06d5bd60), ordinal 299
//   m_Invites.erase(PlayerId) when present  (0x06d5bd7f-0x06d5bd95)
//   DelMember(PlayerId)                     (0x06d5bd9d)
//   if (m_Members.size() == 0) vftable[0](1) -- the scalar deleting destructor
//   with the delete flag, i.e. `delete this` (0x06d5bda2-0x06d5bdaf).
// That self-destruct was missing from the port, so empty groups leaked and
// stayed reachable through FromGroupID.
// ---------------------------------------------------------------------------
void CPlayerGroup::HandleClientLogout(unsigned int PlayerId) {
    std::set<unsigned int>::iterator It = m_Invites.find(PlayerId);
    if (It != m_Invites.end()) {
        m_Invites.erase(It);
    }
    DelMember(PlayerId);
    if (m_Members.empty()) {
        delete this;
    }
}

// ---------------------------------------------------------------------------
// CPlayerGroup::SendGroup — retail 0x7bdc0 (0x06d5bdc0), ordinal 402
// For every member: FUN_06d19180 (0x06d5bdfc) makes CHAT_ID{member} and
// FUN_06d190d0 (0x06d5be01) -- whose entire body at 0x06d190d0 is
//     IChatClient::SendCommonChatMsg(CHAT_ID, DAT_06d8f5dc, 8, &{ MsgType, (uint)Param })
// -- ships it. The 8 is sizeof(payload), not a message id.
// ---------------------------------------------------------------------------
void CPlayerGroup::SendGroup(BaseGroupMessage::Type MsgType, unsigned int Param) {
    // No null test on DAT_06d8fb18: FUN_06d190d0 loads it and calls slot 0
    // directly (.text:06d190d0..06d190f5); the guard the port had was removed
    // 2026-09-24 (s2).
    std::set<unsigned int>::const_iterator It = m_Members.begin();
    for (; It != m_Members.end(); ++It) {
        FGroupMsgPayload Payload;
        Payload.MsgType = (unsigned int)MsgType;
        Payload.Param = Param;
        g_pChatClientInterface->SendCommonChatMsg(CHAT_ID(MakeChatId(*It)), CHAT_ID(GGroupChatSourceId),
                                                  sizeof(Payload), &Payload);
    }
}

// CPlayerGroup::SendChat -- retail .text:06d5be60 .. .text:06d5bf1c (cdecl, `this`
// on the stack, ?SendChat@CPlayerGroup@@QAAXHPBGZZ). Ported 1:1 2026-09-24
// (claude-code:w5-server-06d4).
//   .text:06d5be7d  vswprintf(local[0x400 wchar], Msg, varargs) -- unbounded
//   .text:06d5bea1 .. 06d5bf12  for each member of the set at this+0xc:
//                   FUN_06d18ea0(Style, CHAT_ID{member} (FUN_06d19180 at 06d5beba),
//                   L"%s" (.rdata:06d6b9b8, PE bytes 25 00 73 00 00 00), local)
// The first int is the StyleNode style FUN_06d18ea0 renders the text with (it is
// pushed from [ESP+0x814] straight into FUN_06d18ea0's ushort slot), not a
// sender id. It is NOT a pub::Controller message: the id 0x3d an earlier port used
// was invented for this site.
void CPlayerGroup::SendChat(int Style, const wchar_t* Msg, ...) {
    wchar_t FormattedMsg[0x400];
    va_list Args;
    va_start(Args, Msg);
    _vswprintf(FormattedMsg, Msg, Args);                                  // .text:06d5be7d
    va_end(Args);

    std::set<unsigned int>::const_iterator It = m_Members.begin();
    for (; It != m_Members.end(); ++It) {
        Server_SendStyledChat(static_cast<unsigned short>(Style), MakeChatId(*It),
                              L"%s", FormattedMsg);                         // .text:06d5bec0
    }
}

// CPlayerGroup::DeliverChat — retail 0x7bf20 (0x06d5bf20). Per member:
//     IChatClient::SendCommonChatMsg(CHAT_ID{member}, DAT_06d9042c, MsgType, MsgData)
// (the vtable is loaded from DAT_06d8fb18 at 0x06d5bf40 and slot 0 is called at
// 0x06d5bf6a; FUN_06d19180 at 0x06d5bf5f overwrites the first of the four pushed
// dwords with CHAT_ID{member}). The id 0x3e the port sent here was invented for this
// site -- 0x3e is a real id, but it belongs to the timer-expiry path (0x06d5e011).
// The first argument is the BYTE SIZE and the second the RDL buffer: the one caller,
// IServerImpl::SubmitChat's group branch (.text:06d19d79), pushes the encoded chat
// buffer DAT_06d8e5a8 and its size. Retail has no null check on DAT_06d8fb18 --
// it reloads it per member (0x06d5bf40) -- so neither does this.
void CPlayerGroup::DeliverChat(unsigned long MsgType, const void* MsgData) {
    std::set<unsigned int>::const_iterator It = m_Members.begin();
    for (; It != m_Members.end(); ++It) {
        g_pChatClientInterface->SendCommonChatMsg(CHAT_ID(MakeChatId(*It)), CHAT_ID(GDeliverChatSourceId),
                                                  MsgType, MsgData);
    }
}

// CPlayerGroup::SetMissionObjectives — retail 0x7bfd0 (0x06d5bfd0). 0x120 is a vtable
// BYTE offset, not a message id: 0x120 / 4 = IClient slot 72
// (FLPACKET_SERVER_SETMISSIONOBJECTIVES). Retail 0x06d5bfea-0x06d5bfef pushes
// &node._Myval (LEA EDX,[ESI+0xc]) and the CMissionObjectives*, i.e. the slot's first
// parameter is `unsigned int const&` -- rpclocal's own body dereferences it
// (0x06d8b1f3: MOV EDX,[ESP+0x28] / MOV EDX,[EDX]) -- SendServerSetMissionObjectives
// takes it as a reference, bound here to the set node's own value.
// (claude-code:w5-server-06d4, 2026-09-24) No IClient null test -- DAT_06d8fb14 is
// loaded and called per member (.text:06d5bfe2..06d5bfef) -- and the address pushed is
// the set node's own value (LEA EDX,[ESI+0xc]), not a copy: both now as retail.
// The record retail calls CMissionObjectives IS the 0x40-byte SETMISSIONOBJECTIVES
// argument (rpc_iclient_args.h's FServerSetMissionObjectivesInfo); server_pub.h still
// declares CMissionObjectives as a one-pointer placeholder, so the reference is
// reinterpreted here, as the old dword cast did.
void CPlayerGroup::SetMissionObjectives(CMissionObjectives& Objs) {
    std::set<unsigned int>::const_iterator It = m_Members.begin();
    for (; It != m_Members.end(); ++It) {
        g_pClientInterface->SendServerSetMissionObjectives(
            *It, reinterpret_cast<const FServerSetMissionObjectivesInfo&>(Objs));
    }
}

// CPlayerGroup::SetMissionMessage — retail 0x7c050 (0x06d5c050). 0x118 / 4 = IClient
// slot 70 (FLPACKET_SERVER_SET_MISSION_MESSAGE); same &member / &Msg shape as
// SetMissionObjectives above (0x06d5c06a-0x06d5c06f). The id 0x41 the port sent was
// invented for this site. No IClient null test (.text:06d5c062..06d5c06f loads
// DAT_06d8fb14 and calls per member); the node's value is pushed by address
// (LEA EDX,[ESI+0xc]). Guard and copy removed 2026-09-24 (claude-code:w5-server-06d4).
void CPlayerGroup::SetMissionMessage(CSetMissionMessage& Msg) {
    std::set<unsigned int>::const_iterator It = m_Members.begin();
    for (; It != m_Members.end(); ++It) {
        g_pClientInterface->SendServerSetMissionMessage(*It, Msg);
    }
}

// ---------------------------------------------------------------------------
// CPlayerGroup::StoreMemberList — retail 0x7c0d0 (0x06d5c0d0), ordinal 455
// One inlined push_back per member (the whole 0x06d5c0f0-0x06d5c294 block is
// std::vector<uint>::insert(end(), *it) expanded). It does NOT clear the
// vector first -- the port's OutMembers.clear() had no counterpart.
// ---------------------------------------------------------------------------
void CPlayerGroup::StoreMemberList(std::vector<unsigned int>& OutMembers) {
    std::set<unsigned int>::const_iterator It = m_Members.begin();
    for (; It != m_Members.end(); ++It) {
        OutMembers.push_back(*It);
    }
}

// ---------------------------------------------------------------------------
// CPlayerGroup::RewardMembers -- retail .text:06d5c2b0 .. .text:06d5c32a,
// ordinal 389, thiscall (Amount), RET 4. Per member (in-order walk):
//   .text:06d5c2c2 .. 06d5c2d4  this = &Players[member] (raw IMUL 0x418, no guard)
//   .text:06d5c2db              FUN_06d4f4e0(Amount, 1) -- PlayerData_AdjustCash
//                               with bCountEarned = true (earned counter, MP gate,
//                               clamp, IClient slot 77, controller message 0)
// Rewritten 2026-09-24 (claude-code:w5-server-06d4): the old body inlined a copy
// of the clamp without the MP gate, the slot 77 notification or the controller
// message, and skipped members PlayerDB_GetPlayer rejected.
// ---------------------------------------------------------------------------
void CPlayerGroup::RewardMembers(int Amount) {
    std::set<unsigned int>::const_iterator It = m_Members.begin();
    for (; It != m_Members.end(); ++It) {
        PlayerData_AdjustCash(Players[*It], Amount, true);                // .text:06d5c2db
    }
}

// ---------------------------------------------------------------------------
// CPlayerGroup::GetMissionID — retail 0x7c330 (0x06d5c330), ordinal 216
// CPlayerGroup::GetMissionSetBy — retail 0x7c3b0 (0x06d5c3b0), ordinal 217
// The mission is not stored on the group at all: both walk m_Members and
// return the first non-zero PlayerData field (+0x408 resp. +0x40c), zero when
// nobody has one. The port used to `return 0;` unconditionally.
// ---------------------------------------------------------------------------
// Both index Players raw (.text:06d5c342..06d5c34b: IMUL 0x418, disp -0x10 /
// -0xc) -- no is_valid, no null test.
unsigned int CPlayerGroup::GetMissionID() {
    std::set<unsigned int>::const_iterator It = m_Members.begin();
    for (; It != m_Members.end(); ++It) {
        if (Players[*It].group_mission_id != 0) {
            return Players[*It].group_mission_id;
        }
    }
    return 0;
}

unsigned int CPlayerGroup::GetMissionSetBy() {
    std::set<unsigned int>::const_iterator It = m_Members.begin();
    for (; It != m_Members.end(); ++It) {
        if (Players[*It].group_mission_set_by != 0) {
            return Players[*It].group_mission_set_by;
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// CPlayerGroup::SetMissionID — retail 0x7c430 (0x06d5c430), ordinal 423
// For every member: +0x408 = MissionId, +0x40c = SetBy (0x06d5c45d/0x06d5c463)
// then IClient slot 114 (+0x1c8, 0x06d5c477) with (member, MissionId, SetBy).
// The port used to have an empty body.
// ---------------------------------------------------------------------------
void CPlayerGroup::SetMissionID(unsigned int MissionId, unsigned int SetBy) {
    std::set<unsigned int>::const_iterator It = m_Members.begin();
    // Raw Players index (.text:06d5c450/06d5c456) and no interface null test
    // (.text:06d5c469 loads DAT_06d8fb14 and calls).
    for (; It != m_Members.end(); ++It) {
        PlayerData& Player = Players[*It];
        Player.group_mission_id = MissionId;
        Player.group_mission_set_by = SetBy;
        g_pClientInterface->SendServerMiscObjUpdateMission(*It, MissionId, SetBy);
    }
}

// ---------------------------------------------------------------------------
// CPlayerGroup::GetMember — retail 0x7c4c0 (0x06d5c4c0), ordinal 213
// Index < 0 or Index >= m_Members.size() gives 0 (0x06d5c4c8 / 0x06d5c4cd),
// otherwise it advances the set iterator Index times and returns *it. This is
// the one accessor retail declares const (QBE).
// ---------------------------------------------------------------------------
unsigned int CPlayerGroup::GetMember(int Index) const {
    if (Index < 0 || (unsigned int)Index >= m_Members.size()) {
        return 0;
    }
    std::set<unsigned int>::const_iterator It = m_Members.begin();
    for (int i = 0; i < Index; ++i) {
        ++It;
    }
    return *It;
}
