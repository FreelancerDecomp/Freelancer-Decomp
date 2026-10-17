// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Content.dll TU25 (06ed5a70-06eeea30): GCS, the radio chatter of NPCs in space.
// Lane CNT-GCS (docs/re/content.md, TU map). Listing: DLLS\BIN\Content.dll.txt.
//
// Three parts are ported here:
//  - the handler set (DAT_06fd1bf0, SpaceSupport forwards its messages to it) and the
//    handler registry GetGcsHandler (FUN_06ee93f0, map DAT_06fcf0e0);
//  - the conversation manager (DAT_06fd1c08) that turns GCS events into conversations
//    and sends delayed controller messages;
//  - the admission tables (DAT_06fcf0fc/110/128/140/158, DAT_06fce360) that decide whether
//    a comm of a category may be spoken (FUN_06eebd10) and remember where one was
//    (FUN_06eeb920).
// Conversations 1 (06ee2000), 2 (06ee6e60), 3 (06ee8880), 5 (06ee4cd0), 6..0x10 (06ee2c20),
// 0x11..0x18 (06ee55f0) and 0x19 (06ee7b00) and their lines are ported; see
// QueueConversation.

#include "common_ini.h"    // Vector
#include "common_server_controller.h" // pub::Controller::EMessage
#include "common_tstring.h" // TString

#include <list>
#include <map>
#include <vector>

struct FSystemInfo;
struct FContentPlayerState;
class FContentSpaceObj;
class FContentShipBase;
class FRmConversationBase;  // RandomMissionOffer.h (TU61)

// ---------------------------------------------------------------------------------------
// Voice line names.
// ---------------------------------------------------------------------------------------

// FUN_06eec2b0 (cdecl, varargs): vsprintf into a 0x40-byte local, then the TString copy
// clamped to 0x3f. The port bounds the local (retail's vsprintf would overrun it).
void FormatTString64(TString<0x40>* Out, char const* Format, ...);

// FUN_06eeb010 (cdecl): pick a voice line "<Name>_NN<Suffix>" of VoiceId. The index is
// floor(n - cos(rand() * 2^-15 * pi/2) * (n - 1) + 0.5) for the voice's permutation count n
// of GetNicknameId(Name). 0 on success; -2 (no voice) / -3 (n <= 0) empty *Out.
int __cdecl PickVoiceLine(char const* Name, char const* Suffix, unsigned int VoiceId,
                          TString<0x40>* Out);

// FUN_06eeaf10 (cdecl, the result through a hidden pointer): the PickVoiceLine name
// "<Name>_NN<Suffix>" of VoiceId, empty when the voice or the name's permutations are
// missing (71 callers).
TString<0x40> __cdecl MakeVoiceLineName(char const* Name, char const* Suffix,
                                        unsigned int VoiceId);

// FUN_06eec250 (thiscall on the TString, RET 4): Tail appended, clamped so the string keeps
// at most 0x3f chars; a null Tail appends nothing.
void AppendTString64(TString<0x40>* Str, char const* Tail);
// FUN_06eec1f0 (thiscall ctor, RET 4): the TString copy of Str (null -> empty, clamp 0x3f).
TString<0x40> MakeTString64(char const* Str);

// FUN_06eeb110 (cdecl): *Out = the msg_id_prefix of the base (FindContentBaseInfo, then
// CommReferrable slot 1), 0; -4 without the base (Out untouched).
int __cdecl GetBaseMsgIdPrefix(unsigned int BaseId, TString<0x40>* Out);
// FUN_06eeb190 (cdecl): the same for the faction (GetFactionProps).
int __cdecl GetFactionMsgIdPrefix(unsigned int FactionId, TString<0x40>* Out);

// FUN_06eeb210 (cdecl, the result through a hidden pointer): the voice line naming Object
// (a ship of the system): Kind 0 the ship's line of business for a dock hail ("gcs_dhail_mbid_
// trader(s)" / "patrol(lers)" + "_leg" / "_ill"), 1 its formation designation, 2 its number
// (Suffix appended when given), 3 its number with a dash. Empty for anything else.
TString<0x40> __cdecl MakeObjectReferenceName(int Kind, FContentSpaceObj* Object,
                                              char const* Suffix);
// FUN_06eeb730 (cdecl, the result through a hidden pointer): the msg_id_prefix of the place
// SpaceObjId stands for -- its docking target's base, or for a jump gate / hole (type &
// 0xc40) the target system -- followed by "-"; empty when it is neither.
TString<0x40> __cdecl MakeObjectLocationName(unsigned int SpaceObjId);
// FUN_06eeb870 (cdecl): the ship's formation group (+0x154) has more than one member.
bool __cdecl HasFormationWingmen(FContentSpaceObj* Ship);
// FUN_06eeb590 (cdecl): one "gcs_misc_number_<digit>" name per decimal digit of Number,
// most significant first (each digit is inserted at the front); the last digit gets Suffix
// appended (when non-null). 0 appends "gcs_misc_number_0<Suffix>"; a negative Number adds
// nothing. Out gets room for three first.
void __cdecl MakeNumberNames(int Number, char const* Suffix, std::vector<TString<0x40>>* Out);
// FUN_06eec030 (cdecl, the result through a hidden pointer): the voice line (with "-") of a
// flee reason: 0 "gcs_combat_fleereason_tough", 2 "..._damaged", 3 "..._noweapons", any other
// "..._other".
TString<0x40> __cdecl MakeFleeReasonName(int Reason, unsigned int VoiceId);
// FUN_06eeb900 (cdecl): Object's space object id is a multiple of 5 (unsigned DIV, NEG / SBB /
// INC). The combat conversation (06ee2c20) lets formation leaders with such ids give orders.
bool __cdecl IsSpaceObjIdMultipleOf5(FContentSpaceObj* Object);
// FUN_06eec0c0 (cdecl, the result through a hidden pointer): "gcs_dockrequest_todock%s"
// (dock type 1), "..._tomoor%s" (3, 4, 5), "..._toland%s" (6) formatted with Suffix; empty
// for any other type.
TString<0x40> __cdecl MakeDockApproachName(unsigned int DockType, char const* Suffix);

// ---------------------------------------------------------------------------------------
// Conversation lines and conversations.
// ---------------------------------------------------------------------------------------

// One line of a conversation (0x30 bytes, base vtable 06fb6254: FUN_06ed72c0 deleting dtor,
// FUN_06ed6780, LAB_06ed6a40, LAB_06ed67c0, LAB_06ed69c0). Every line class of TU25 derives
// from it with its own ctor and a vtable holding the same five slots; the base is never
// created on its own (its vptr is only stored by the dtor FUN_06ed61d0).
class FGcsLine
{
public:
    // FUN_06ed72c0 (scalar deleting) -> FUN_06ed61d0: the text ids are freed.
    virtual ~FGcsLine();
    // Slot 1, FUN_06ed6780: +0x28 set, a system, the speaker found in it (FindShipOrSolar)
    // and at least one text. FGcsConversation::OnMessage and the conversation ctors read it.
    virtual bool CanPlay();
    // Slot 2, LAB_06ed6a40: sends the comm (SendSpaceComm); outside single player the
    // conversation manager also gets the delayed "line done" message 0xb.
    virtual void Play();
    // Slot 3, LAB_06ed67c0 (RET 4): whether Parms (message 0xb {listener, system, speaker,
    // last text}) is this line's.
    virtual bool IsDone(void const* Parms);
    // Slot 4, LAB_06ed69c0: the summed voice durations of the texts, 5.0 when one is unknown.
    // QueueConversation returns it for event type 7.
    virtual float GetDuration();

    // FUN_06ed6800 (fastcall, the name by value on the stack): push_back
    // GetNicknameId(Name.Data).
    void AddText(TString<0x40> Name);

    unsigned int Speaker;               // +0x04
    unsigned int Listener;              // +0x08
    unsigned int VoiceId;               // +0x0c 0 = the speaker's (GetVoiceID)
    int CostumeId;                      // +0x10 0 = no costume (GetCostumeByID)
    std::vector<unsigned int> TextIds;  // +0x18 (allocator byte +0x14)
    FSystemInfo* System;                // +0x24 null makes the line unplayable
    // +0x28: SendSpaceComm's ninth argument (the port calls it Mood); the ctors store
    // 0x4a39 / 0x4a41 ... TODO: meaning of the values. CanPlay needs it non-zero.
    unsigned int Field_0x28;
    // +0x2c: SendSpaceComm's seventh argument (the port calls it CommType). TODO: meaning.
    unsigned int Field_0x2c;

protected:
    // The inline base part of every line ctor (e.g. 06ed9363-06ed938f): +0x0c / +0x10 /
    // +0x28 / +0x2c zero, no texts.
    FGcsLine(FSystemInfo* InSystem, unsigned int InSpeaker, unsigned int InListener);

    // The inline voice test of the line ctors (e.g. 06ed93a2-06ed93ca / 06ed97a3):
    // GetVoiceID(speaker) into +0x0c; without voice properties (FUN_06fa6e50) or without
    // every bit of Roles (FUN_06fa6e30) System = null and false.
    bool HasVoiceRoles(unsigned int Roles);
    // The inline text appends of the line ctors (FUN_06eb5770 / FUN_06f937c0 insert(end, 1,
    // GetNicknameId(Name))).
    void PushText(char const* Name);
    // The one-name ctor body shared by twelve lines (06ee11c0, 06edc180, ...; their listings
    // differ only in the vtable, the role mask and the name): HasVoiceRoles(Roles);
    // rand() / 32768 == 0; +0x28 = 0x4a39; room for one text; the voice line of Name (copied
    // into a TString clamped to 0x3f) with "-" (DAT_06fb6064).
    void BuildSingleVoiceLine(unsigned int Roles, char const* Name);
};

// Abstract line with a clone slot, vtable 06fb6268 (FGcsLine's five slots + slot 5 purecall
// 06fa9030). Only seen as the vptr the inlined copy ctors write (06ed667b, TU61 06f848f0);
// its ctor/dtor are trivial. The retail name is unknown (no RTTI).
class FGcsCloneableLine : public FGcsLine
{
public:
    // Slot 5: a heap copy of the line.
    virtual FGcsCloneableLine* Clone() = 0;

protected:
    using FGcsLine::FGcsLine;
};

// A line spoken to a list of players (0x40 bytes, vtable 06fb623c): every player with a
// ship gets the comm in its own system. Built by TU61's mission conversations (the 0x90
// class of vtable 06fbc648 derives from it, ctor 06f84790). The dtor (06f847e0, deleting
// 06f847c0) is emitted in TU61 and only frees PlayerIds.
class FGcsPlayerListLine : public FGcsCloneableLine
{
public:
    // FUN_06ed6180 (RET 4): no speaker / listener, the system, no players.
    explicit FGcsPlayerListLine(FSystemInfo* InSystem);

    // Slot 1, FUN_06ed6710 (also called directly by TU61 06f849c3): the first player with
    // a ship decides; a live speaker refuses, otherwise playable when there are texts.
    bool CanPlay() override;
    // Slot 2, LAB_06ed6200: the comm to every player's ship in the player's system; outside
    // single player the delayed "line done" message 0xb for the last such player.
    void Play() override;
    // Slot 3, FUN_06ed6560 (RET 4): Parms' listener (word 0) is the ship of one of the
    // players, and the speaker and last text match (the system is not compared).
    bool IsDone(void const* Parms) override;
    // Slot 5, FUN_06ed65c0: new 0x40 copy.
    FGcsCloneableLine* Clone() override;

    std::vector<unsigned int> PlayerIds;    // +0x34 (allocator byte +0x30)
};

// vtable 06fb66ec, ctor FUN_06ed9330 (RET 0x10): the dock hails a launching ship (speaker:
// the dock, listener: the ship); needs voice role 0x40 (dockrequest_dock). Texts: the
// listener's faction prefix + "_short", its formation designation, number, "gcs_misc_dash",
// number-, "gcs_misc_thisis+", the dock's own prefix-, the clearance for TerminalType and
// "gcs_misc_wellwish". Caller FGcsLaunchConversation.
class FGcsLineLaunchClearance : public FGcsLine
{
public:
    FGcsLineLaunchClearance(FSystemInfo* InSystem, unsigned int InSpeaker,
                            unsigned int InListener, unsigned int TerminalType);
};

// vtable 06fb6fa0, ctor FUN_06edf7a0 (RET 0xc): "gcs_misc_ack"; needs voice role 0x200
// (misc_chatter). Callers 06ee3419, 06ee3a75, 06ee3f37, 06ee471e, FGcsLaunchConversation.
class FGcsLineAck : public FGcsLine
{
public:
    FGcsLineAck(FSystemInfo* InSystem, unsigned int InSpeaker, unsigned int InListener);
};

// The dock conversation's lines (FGcsDockConversation). Voice role 0x80 (dockrequest_docker)
// for the ship's lines, 0x40 (dockrequest_dock) for the dock's.

// vtable 06fb635c, ctor FUN_06ed6dd0 (RET 0x10): the ship (speaker) asks the dock
// (listener) to dock: "gcs_misc_thisis+", its faction prefix + "_short", designation,
// number, dash, number, "gcs_misc_to", the dock's place-, "gcs_dockrequest_request+" and
// the approach for DockType ("-").
class FGcsLineDockRequest : public FGcsLine
{
public:
    FGcsLineDockRequest(FSystemInfo* InSystem, unsigned int InSpeaker, unsigned int InListener,
                        unsigned int DockType);
};

// vtable 06fb6390, ctor FUN_06ed72e0 (RET 0xc): the dock refuses a hostile ship
// ("gcs_dockrequest_responsehostile"). Response 0.
class FGcsLineDockHostile : public FGcsLine
{
public:
    FGcsLineDockHostile(FSystemInfo* InSystem, unsigned int InSpeaker, unsigned int InListener);
};

// vtable 06fb6454, ctor FUN_06ed74d0 (RET 0x14): the dock grants the request (response 4);
// three shapes: landing (dock type 6), a terminal up to 20 and one above 20 (its number
// spoken digit by digit).
class FGcsLineDockGranted : public FGcsLine
{
public:
    FGcsLineDockGranted(FSystemInfo* InSystem, unsigned int InSpeaker, unsigned int InListener,
                        unsigned int DockType, int DockIndex);
};

// vtable 06fb64c0, ctor FUN_06ed7dc0 (RET 0xc): "stand by", response 3.
class FGcsLineDockStandby : public FGcsLine
{
public:
    FGcsLineDockStandby(FSystemInfo* InSystem, unsigned int InSpeaker, unsigned int InListener);
};

// vtable 06fb6504, ctor FUN_06ed80d0 (RET 0x10): "denied, no fit", response 2.
class FGcsLineDockDenied : public FGcsLine
{
public:
    FGcsLineDockDenied(FSystemInfo* InSystem, unsigned int InSpeaker, unsigned int InListener,
                       unsigned int DockType);
};

// vtable 06fb6534, ctor FUN_06ed8450 (RET 0x10): the dock calls a ship that waited in the
// queue ("gcs_dockrequest_nowcleared").
class FGcsLineDockNowCleared : public FGcsLine
{
public:
    FGcsLineDockNowCleared(FSystemInfo* InSystem, unsigned int InSpeaker,
                           unsigned int InListener, unsigned int DockType);
};

// vtable 06fb6580, ctor FUN_06ed8810 (RET 0x10): a formation leader at a jump gate / hole:
// "jumpgateopen", "goingto", the target system's prefix.
class FGcsLineJumpGateOpen : public FGcsLine
{
public:
    FGcsLineJumpGateOpen(FSystemInfo* InSystem, unsigned int InSpeaker, unsigned int InListener,
                         unsigned int TargetSystem);
};

// vtable 06fb65b4, ctor FUN_06ed8ac0 (RET 0x10): "jumpgatedelayed", the target system.
class FGcsLineJumpGateDelayed : public FGcsLine
{
public:
    FGcsLineJumpGateDelayed(FSystemInfo* InSystem, unsigned int InSpeaker,
                            unsigned int InListener, unsigned int TargetSystem);
};

// vtable 06fb65f0, ctor FUN_06ed8d20 (RET 0xc): "jumpgateopenafterdelay".
class FGcsLineJumpGateOpenAfterDelay : public FGcsLine
{
public:
    FGcsLineJumpGateOpenAfterDelay(FSystemInfo* InSystem, unsigned int InSpeaker,
                                   unsigned int InListener);
};

// vtable 06fb6624, ctor FUN_06ed8f10 (RET 0xc): a formation leader at a trade lane ring.
class FGcsLineTradeLaneStart : public FGcsLine
{
public:
    FGcsLineTradeLaneStart(FSystemInfo* InSystem, unsigned int InSpeaker,
                           unsigned int InListener);
};

// vtable 06fb6670, ctor FUN_06ed9100 (RET 0xc): a formation leader at its dock: "this our
// stop", "thanks for the help".
class FGcsLineThisOurStop : public FGcsLine
{
public:
    FGcsLineThisOurStop(FSystemInfo* InSystem, unsigned int InSpeaker, unsigned int InListener);
};

// The dock hail conversation's lines (FGcsDockHailConversation). The station (voice role 0x20,
// hail_dock) hails the ship; the ship answers with role 0x10 (hail_hailee).

// vtable 06fb676c, ctor FUN_06ed97e0 (RET 0x10): the hail. Attitude (the station's
// reputation towards the ship) at or below -0.6 refuses ("gcs_dhail_nopermission"); otherwise
// one of two identification requests (rand).
class FGcsLineDockHail : public FGcsLine
{
public:
    FGcsLineDockHail(FSystemInfo* InSystem, unsigned int InSpeaker, unsigned int InListener,
                     float Attitude);
};

// vtable 06fb67c0, ctor FUN_06ed9f70 (RET 0xc): the station acknowledges the ship's data and
// asks where it is headed.
class FGcsLineDockHailReceived : public FGcsLine
{
public:
    FGcsLineDockHailReceived(FSystemInfo* InSystem, unsigned int InSpeaker,
                             unsigned int InListener);
};

// vtable 06fb67f4, ctor FUN_06eda290 (RET 0xc): the ship identifies itself and submits its
// data.
class FGcsLineHaileeDataSubmit : public FGcsLine
{
public:
    FGcsLineHaileeDataSubmit(FSystemInfo* InSystem, unsigned int InSpeaker,
                             unsigned int InListener);
};

// vtable 06fb6824, ctor FUN_06eda5a0 (RET 0xc): the refused ship's angry answer.
class FGcsLineHaileeAngry : public FGcsLine
{
public:
    FGcsLineHaileeAngry(FSystemInfo* InSystem, unsigned int InSpeaker, unsigned int InListener);
};

// vtable 06fb685c, ctor FUN_06eda790 (RET 0xc; callers types 1 and 5): "gcs_misc_goingto"
// (_p with formation wingmen, else _s) and the place of the guide's second target.
class FGcsLineGoingTo : public FGcsLine
{
public:
    FGcsLineGoingTo(FSystemInfo* InSystem, unsigned int InSpeaker, unsigned int InListener);
};

// vtable 06fb6884, ctor FUN_06edab00 (RET 0xc; callers types 1 and 5): "gcs_patrol_idhome"
// (_p / _s) and the place of the guide's first target.
class FGcsLinePatrolIdHome : public FGcsLine
{
public:
    FGcsLinePatrolIdHome(FSystemInfo* InSystem, unsigned int InSpeaker,
                         unsigned int InListener);
};

// vtable 06fb6934, ctor FUN_06edae70 (RET 0xc; callers types 1 and 5): the patrol's progress
// read from slot +0x4c of the type-3 guide (FPatrolGuide::GetPathPointsToGo, LAB_06f0b2d0):
// 0 -> "gcs_patrol_finished" (_p / _s); n -> "gcs_patrol_(i|we)have(only)+", the number n,
// "gcs_patrol_morewp(s)togo-".
class FGcsLinePatrolStatus : public FGcsLine
{
public:
    FGcsLinePatrolStatus(FSystemInfo* InSystem, unsigned int InSpeaker,
                         unsigned int InListener);
};

// vtable 06fb6980, ctor FUN_06edb260 (RET 0xc): a trader tells what it carries (the trade
// guide's trade good, "gcs_misc_to") or wants to get (its return good, "gcs_misc_from") and
// the place of the guide's second target.
class FGcsLineTradeStatus : public FGcsLine
{
public:
    FGcsLineTradeStatus(FSystemInfo* InSystem, unsigned int InSpeaker, unsigned int InListener);
};

// vtable 06fb69a8, ctor FUN_06edb720 (RET 0xc): "gcs_trade_idhome" (_p / _s) and the place
// of the guide's first target.
class FGcsLineTradeIdHome : public FGcsLine
{
public:
    FGcsLineTradeIdHome(FSystemInfo* InSystem, unsigned int InSpeaker, unsigned int InListener);
};

// The player hail conversation's lines (FGcsPlayerHailConversation): the hailed NPC (voice
// role 0x10, hail_hailee) answers the player's ship.

// vtable 06fb71f0, ctor FUN_06ee11c0 (RET 0xc): "gcs_phail_blowoff" -- the same player hailed
// again within 15 s.
class FGcsLinePlayerHailBlowOff : public FGcsLine
{
public:
    FGcsLinePlayerHailBlowOff(FSystemInfo* InSystem, unsigned int InSpeaker,
                              unsigned int InListener);
};

// vtable 06fb721c, ctor FUN_06ee13b0 (RET 0xc): "gcs_phail_combatblowoff" -- the hailed ship
// is busy (FContentShipBase slot 2, byte +0x11c).
class FGcsLinePlayerHailCombatBlowOff : public FGcsLine
{
public:
    FGcsLinePlayerHailCombatBlowOff(FSystemInfo* InSystem, unsigned int InSpeaker,
                                    unsigned int InListener);
};

// vtable 06fb7230, ctor FUN_06ee15a0 (RET 0xc): the hailed ship names itself:
// "gcs_misc_thisis+", its faction prefix + "_short", its designation and its number-.
class FGcsLinePlayerHailIdentify : public FGcsLine
{
public:
    FGcsLinePlayerHailIdentify(FSystemInfo* InSystem, unsigned int InSpeaker,
                               unsigned int InListener);
};

// The combat conversation's lines (FGcsCombatConversation). Every ctor takes (system, speaker,
// listener) and, where noted, a fourth value; the callers pass listener 0 unless noted. Voice
// roles: 0x4000 combat_chatter, 0x4 combat_order (0xc004 with the wingman roles), 0x800 the
// station's.

// vtable 06fb69d8, ctor FUN_06edba90 (RET 0xc): "gcs_combat_deathsympathy" (the listener
// argument is ignored: +0x08 = 0).
class FGcsLineCombatDeathSympathy : public FGcsLine
{
public:
    FGcsLineCombatDeathSympathy(FSystemInfo* InSystem, unsigned int InSpeaker,
                                unsigned int Unused);
};

// vtable 06fb6a04, ctor FUN_06edbc80 (RET 0xc): role 0x4; "gcs_misc_thisis+", designation,
// number, dash, number-, "gcs_combat_takinglead".
class FGcsLineCombatTakingLead : public FGcsLine
{
public:
    FGcsLineCombatTakingLead(FSystemInfo* InSystem, unsigned int InSpeaker,
                             unsigned int InListener);
};

// vtable 06fb6a2c, ctor FUN_06edbf60 (RET 0xc): "gcs_combat_scream"; role 0x4000 for a ship
// (type & 0x7f0000), 0x800 otherwise.
class FGcsLineCombatScream : public FGcsLine
{
public:
    FGcsLineCombatScream(FSystemInfo* InSystem, unsigned int InSpeaker, unsigned int InListener);
};

// vtable 06fb6a50, ctor FUN_06edc180 (RET 0xc): "gcs_combat_brag".
class FGcsLineCombatBrag : public FGcsLine
{
public:
    FGcsLineCombatBrag(FSystemInfo* InSystem, unsigned int InSpeaker, unsigned int InListener);
};

// vtable 06fb6aa8, ctor FUN_06edc370 (RET 0x10): "gcs_combat_taking_damage" ("_worst" below
// 0.49 relative health); role as FGcsLineCombatScream.
class FGcsLineCombatTakingDamage : public FGcsLine
{
public:
    FGcsLineCombatTakingDamage(FSystemInfo* InSystem, unsigned int InSpeaker,
                               unsigned int InListener, float Health);
};

// vtable 06fb6b08, ctor FUN_06edc5d0 (RET 0x10): "gcs_combat_inflicting_damage" ("_worst"
// below 0.33 relative health of the victim).
class FGcsLineCombatInflictingDamage : public FGcsLine
{
public:
    FGcsLineCombatInflictingDamage(FSystemInfo* InSystem, unsigned int InSpeaker,
                                   unsigned int InListener, float Health);
};

// vtable 06fb6b30, ctor FUN_06edc800 (RET 0xc): "gcs_combat_insights".
class FGcsLineCombatInSights : public FGcsLine
{
public:
    FGcsLineCombatInSights(FSystemInfo* InSystem, unsigned int InSpeaker,
                           unsigned int InListener);
};

// vtable 06fb6b5c, ctor FUN_06edc9f0 (RET 0xc): "gcs_combat_hasinsights".
class FGcsLineCombatHasInSights : public FGcsLine
{
public:
    FGcsLineCombatHasInSights(FSystemInfo* InSystem, unsigned int InSpeaker,
                              unsigned int InListener);
};

// vtable 06fb6b8c, ctor FUN_06edcbe0 (RET 0xc): "gcs_combat_rejoiningform".
class FGcsLineCombatRejoiningForm : public FGcsLine
{
public:
    FGcsLineCombatRejoiningForm(FSystemInfo* InSystem, unsigned int InSpeaker,
                                unsigned int InListener);
};

// vtable 06fb6bdc, ctor FUN_06edcdd0 (RET 0xc): "gcs_combat_order_disengage",
// "gcs_combat_announce_allclear".
class FGcsLineCombatOrderDisengage : public FGcsLine
{
public:
    FGcsLineCombatOrderDisengage(FSystemInfo* InSystem, unsigned int InSpeaker,
                                 unsigned int InListener);
};

// vtable 06fb6bf0, ctor FUN_06edd000 (RET 0xc): "gcs_combat_announce_allclear".
class FGcsLineCombatAllClear : public FGcsLine
{
public:
    FGcsLineCombatAllClear(FSystemInfo* InSystem, unsigned int InSpeaker,
                           unsigned int InListener);
};

// vtable 06fb6c1c, ctor FUN_06edd1f0 (RET 0xc): role 0xc004; "gcs_combat_resume_trade+",
// "gcs_misc_to", the place of the guide's first target.
class FGcsLineCombatResumeTrade : public FGcsLine
{
public:
    FGcsLineCombatResumeTrade(FSystemInfo* InSystem, unsigned int InSpeaker,
                              unsigned int InListener);
};

// vtable 06fb6c4c, ctor FUN_06edd500 (RET 0xc): role 0x4; "gcs_combat_resume_patrol".
class FGcsLineCombatResumePatrol : public FGcsLine
{
public:
    FGcsLineCombatResumePatrol(FSystemInfo* InSystem, unsigned int InSpeaker,
                               unsigned int InListener);
};

// vtable 06fb6c74, ctor FUN_06edd6f0 (RET 0xc): role 0x4; "gcs_combat_engaging+" and the
// listener's (the target's) faction + "_short", designation and number-; a text id of 0 makes
// the line unplayable.
class FGcsLineCombatEngaging : public FGcsLine
{
public:
    FGcsLineCombatEngaging(FSystemInfo* InSystem, unsigned int InSpeaker,
                           unsigned int InListener);
};

// vtable 06fb6cf0, ctor FUN_06edda20 (RET 0x10): role 0x4; "gcs_combat_noengaging+" and
// "gcs_combat_noengagereason_tootough_01-" (Reason 2) / "..._toofar_01-".
class FGcsLineCombatNoEngaging : public FGcsLine
{
public:
    FGcsLineCombatNoEngaging(FSystemInfo* InSystem, unsigned int InSpeaker,
                             unsigned int InListener, int Reason);
};

// vtable 06fb6d1c, ctor FUN_06eddc10 (RET 0xc): role 0x4; "gcs_combat_askengage".
class FGcsLineCombatAskEngage : public FGcsLine
{
public:
    FGcsLineCombatAskEngage(FSystemInfo* InSystem, unsigned int InSpeaker,
                            unsigned int InListener);
};

// vtable 06fb6d48, ctor FUN_06edde00 (RET 0xc): "gcs_combat_order_engage".
class FGcsLineCombatOrderEngage : public FGcsLine
{
public:
    FGcsLineCombatOrderEngage(FSystemInfo* InSystem, unsigned int InSpeaker,
                              unsigned int InListener);
};

// vtable 06fb6d78, ctor FUN_06eddff0 (RET 0x10): role 0x4; "gcs_combat_order_noengage-" and
// the reason as FGcsLineCombatNoEngaging.
class FGcsLineCombatOrderNoEngage : public FGcsLine
{
public:
    FGcsLineCombatOrderNoEngage(FSystemInfo* InSystem, unsigned int InSpeaker,
                                unsigned int InListener, int Reason);
};

// vtable 06fb6db0, ctor FUN_06ede1e0 (RET 0xc): "gcs_combat_announce_enemysighted".
class FGcsLineCombatEnemySighted : public FGcsLine
{
public:
    FGcsLineCombatEnemySighted(FSystemInfo* InSystem, unsigned int InSpeaker,
                               unsigned int InListener);
};

// vtable 06fb6ddc, ctor FUN_06ede3d0 (RET 0xc): role 0x4; the listener's faction + "_short",
// designation, number-, "gcs_combat_askforhelp".
class FGcsLineCombatAskForHelp : public FGcsLine
{
public:
    FGcsLineCombatAskForHelp(FSystemInfo* InSystem, unsigned int InSpeaker,
                             unsigned int InListener);
};

// vtable 06fb6e04, ctor FUN_06ede6c0 (RET 0xc): role 0x4; "gcs_misc_ack",
// "gcs_combat_comingin" (_p / _s).
class FGcsLineCombatComingIn : public FGcsLine
{
public:
    FGcsLineCombatComingIn(FSystemInfo* InSystem, unsigned int InSpeaker,
                           unsigned int InListener);
};

// vtable 06fb6e30, ctor FUN_06ede9b0 (RET 0xc): role 0x4; "this is" the speaker (faction,
// designation, number) "to" the listener (faction, designation, number-),
// "gcs_combat_offerhelp".
class FGcsLineCombatOfferHelp : public FGcsLine
{
public:
    FGcsLineCombatOfferHelp(FSystemInfo* InSystem, unsigned int InSpeaker,
                            unsigned int InListener);
};

// vtable 06fb6e84, ctor FUN_06ededd0 (RET 0x10): role 0xc004;
// "gcs_misc_attentionallwingmen+", "gcs_combat_order_retreattowards+", the place of the
// guide's first target, the flee reason.
class FGcsLineCombatRetreatTowards : public FGcsLine
{
public:
    FGcsLineCombatRetreatTowards(FSystemInfo* InSystem, unsigned int InSpeaker,
                                 unsigned int InListener, int Reason);
};

// vtable 06fb6f2c, ctor FUN_06edf160 (RET 0x10): role 0x4; "gcs_combat_announce_flee", the
// flee reason.
class FGcsLineCombatAnnounceFlee : public FGcsLine
{
public:
    FGcsLineCombatAnnounceFlee(FSystemInfo* InSystem, unsigned int InSpeaker,
                               unsigned int InListener, int Reason);
};

// vtable 06fb6f58, ctor FUN_06edf3c0 (RET 0xc): "gcs_combat_announce_ff".
class FGcsLineCombatFriendlyFire : public FGcsLine
{
public:
    FGcsLineCombatFriendlyFire(FSystemInfo* InSystem, unsigned int InSpeaker,
                               unsigned int InListener);
};

// vtable 06fb6f8c, ctor FUN_06edf5b0 (RET 0xc): "gcs_combat_announce_combatdrift".
class FGcsLineCombatDrift : public FGcsLine
{
public:
    FGcsLineCombatDrift(FSystemInfo* InSystem, unsigned int InSpeaker, unsigned int InListener);
};

// vtable 06fb7264, ctor FUN_06ee1830 (RET 0xc): a station (role 0x800):
// "gcs_combat_attentionallunits", "gcs_misc_thisis+", its place, "gcs_combat_announce_allclear".
class FGcsLineStationAllClear : public FGcsLine
{
public:
    FGcsLineStationAllClear(FSystemInfo* InSystem, unsigned int InSpeaker,
                            unsigned int InListener);
};

// vtable 06fb72ac, ctor FUN_06ee1b50 (RET 0xc): a station (role 0x800) under attack: without
// a place name nothing; else (rand) "gcs_combat_attentionallunits", "gcs_misc_thisis+", its
// place, "gcs_combat_announce_engage" -- or "gcs_combat_manturrets".
class FGcsLineStationEngage : public FGcsLine
{
public:
    FGcsLineStationEngage(FSystemInfo* InSystem, unsigned int InSpeaker, unsigned int InListener);
};

// The scan conversation's lines (FGcsScanConversation), voice role 0x8. Except
// FGcsLineScanAnnounce they are the one-name template (FGcsLine::BuildSingleVoiceLine).

// vtable 06fb7324, ctor FUN_06ee5c20 (RET 0xc): "this is" the patrol (faction, designation,
// number) "to" the scanned ship (faction, designation, number, dash, number-),
// "gcs_scan_announce".
class FGcsLineScanAnnounce : public FGcsLine
{
public:
    FGcsLineScanAnnounce(FSystemInfo* InSystem, unsigned int InSpeaker, unsigned int InListener);
};

// vtable 06fb7350, ctor FUN_06ee6090 (RET 0xc): "gcs_scan_nothingfound".
class FGcsLineScanNothingFound : public FGcsLine
{
public:
    FGcsLineScanNothingFound(FSystemInfo* InSystem, unsigned int InSpeaker,
                             unsigned int InListener);
};

// vtable 06fb7378, ctor FUN_06ee6280 (RET 0xc): "gcs_scan_orderdrop".
class FGcsLineScanOrderDrop : public FGcsLine
{
public:
    FGcsLineScanOrderDrop(FSystemInfo* InSystem, unsigned int InSpeaker, unsigned int InListener);
};

// vtable 06fb739c, ctor FUN_06ee6470 (RET 0xc): "gcs_scan_refuse".
class FGcsLineScanRefuse : public FGcsLine
{
public:
    FGcsLineScanRefuse(FSystemInfo* InSystem, unsigned int InSpeaker, unsigned int InListener);
};

// vtable 06fb73c0, ctor FUN_06ee6660 (RET 0xc): "gcs_scan_comply".
class FGcsLineScanComply : public FGcsLine
{
public:
    FGcsLineScanComply(FSystemInfo* InSystem, unsigned int InSpeaker, unsigned int InListener);
};

// vtable 06fb73e4, ctor FUN_06ee6850 (RET 0xc): "gcs_scan_angry".
class FGcsLineScanAngry : public FGcsLine
{
public:
    FGcsLineScanAngry(FSystemInfo* InSystem, unsigned int InSpeaker, unsigned int InListener);
};

// vtable 06fb7408, ctor FUN_06ee6a40 (RET 0xc): "gcs_scan_happy".
class FGcsLineScanHappy : public FGcsLine
{
public:
    FGcsLineScanHappy(FSystemInfo* InSystem, unsigned int InSpeaker, unsigned int InListener);
};

// vtable 06fb7434, ctor FUN_06ee6c30 (RET 0xc): "gcs_scan_lootdestroyed" (the third argument
// is the loot taker, not the scanned ship).
class FGcsLineScanLootDestroyed : public FGcsLine
{
public:
    FGcsLineScanLootDestroyed(FSystemInfo* InSystem, unsigned int InSpeaker,
                              unsigned int InListener);
};

// The fidget conversation's lines (FGcsFidgetConversation), voice role 0x400 (fidget). Every
// ctor takes (system, speaker, listener); the conversation passes listener 0 unless noted.

// vtable 06fb6fcc, ctor FUN_06edf950 (RET 0xc): "gcs_fidget_traderumor" (one-name template).
class FGcsLineFidgetTradeRumor : public FGcsLine
{
public:
    FGcsLineFidgetTradeRumor(FSystemInfo* InSystem, unsigned int InSpeaker,
                             unsigned int InListener);
};

// vtable 06fb7010, ctor FUN_06edfb40 (RET 0x10): a trader tells what it heard about Good:
// "gcs_fidget_trade_ihear+", the good's prefix, "gcs_fidget_talkcargo-".
class FGcsLineFidgetTradeIHear : public FGcsLine
{
public:
    FGcsLineFidgetTradeIHear(FSystemInfo* InSystem, unsigned int InSpeaker,
                             unsigned int InListener, unsigned int Good);
};

// vtable 06fb703c, ctor FUN_06edfde0 (RET 0xc): "gcs_fidget_trade_simple" (one-name template).
class FGcsLineFidgetTradeSimple : public FGcsLine
{
public:
    FGcsLineFidgetTradeSimple(FSystemInfo* InSystem, unsigned int InSpeaker,
                              unsigned int InListener);
};

// vtable 06fb7068, ctor FUN_06edffd0 (RET 0xc): the formation leader asks its wingmen for
// their status: "gcs_misc_attentionallwingmen+", "gcs_misc_thisis+", its faction prefix +
// "_short", designation, number, dash, number-, "gcs_fidget_whatstatus-".
class FGcsLineFidgetStatusQuery : public FGcsLine
{
public:
    FGcsLineFidgetStatusQuery(FSystemInfo* InSystem, unsigned int InSpeaker,
                              unsigned int InListener);
};

// vtable 06fb7128, ctor FUN_06ee0390 (RET 0xc; the listener is the leader): a wingman
// answers: "gcs_misc_thisis+", designation, number, dash, number-, and its damage by relative
// health (below 0.3 large, 0.75 medium, 1.0 small, else none).
class FGcsLineFidgetStatusReport : public FGcsLine
{
public:
    FGcsLineFidgetStatusReport(FSystemInfo* InSystem, unsigned int InSpeaker,
                               unsigned int InListener);
};

// vtable 06fb7154, ctor FUN_06ee06d0 (RET 0xc): "gcs_fidget_patrolrumor" (one-name template).
class FGcsLineFidgetPatrolRumor : public FGcsLine
{
public:
    FGcsLineFidgetPatrolRumor(FSystemInfo* InSystem, unsigned int InSpeaker,
                              unsigned int InListener);
};

// vtable 06fb7180, ctor FUN_06ee08c0 (RET 0xc): "gcs_fidget_patrolsimple" (one-name template).
class FGcsLineFidgetPatrolSimple : public FGcsLine
{
public:
    FGcsLineFidgetPatrolSimple(FSystemInfo* InSystem, unsigned int InSpeaker,
                               unsigned int InListener);
};

// vtable 06fb71b4, ctor FUN_06ee0ab0 (RET 0xc; speaker a wingman, listener its leader): "gcs_
// misc_thisis+", its faction prefix + "_short", designation, number, dash, number-,
// "gcs_fidget_patrol_wpremainquery-".
class FGcsLineFidgetWaypointQuery : public FGcsLine
{
public:
    FGcsLineFidgetWaypointQuery(FSystemInfo* InSystem, unsigned int InSpeaker,
                                unsigned int InListener);
};

// vtable 06fb71c8, ctor FUN_06ee0e10 (RET 0xc; speaker the leader, listener the wingman): the
// patrol guide's points to go (slot +0x4c): 0 -> "gcs_patrol_finished" (_p / _s); n ->
// "gcs_patrol_wehave(only)+", the number n, "gcs_patrol_morewp(s)togo-". A twin of
// FGcsLinePatrolStatus (role 0x400, the ship looked up before the voice test, a rand() test
// in the n branch, always "we").
class FGcsLineFidgetPatrolStatus : public FGcsLine
{
public:
    FGcsLineFidgetPatrolStatus(FSystemInfo* InSystem, unsigned int InSpeaker,
                               unsigned int InListener);
};

// Base conversation (vtable 06fb6284: FUN_06ed6db0 deleting dtor, LAB_06ee1f40,
// purecall, FUN_06ee1f60, LAB_06ed6da0). +0x04 allocator byte, +0x08 the lines, +0x14 the
// finished flag. TU61 (06f84be0, 06f84d20) derives from it too.
class FGcsConversation
{
public:
    FGcsConversation() = default;
    // FUN_06ee2b70: every line is deleted, front first.
    virtual ~FGcsConversation();
    // LAB_06ee1f40: plays the first line (tail call of its +0x08), nothing without lines.
    virtual void Start();
    // Slot 2: whether the conversation can take place (e.g. LAB_06ee8870: +0x18 != 0).
    virtual bool IsValid() = 0;
    // FUN_06ee1f60 (RET 4): the first line that takes Parms is deleted and removed; the
    // next one plays when it CanPlay, otherwise the conversation is finished.
    // False when no line took Parms.
    virtual bool OnMessage(void const* Parms);
    // LAB_06ed6da0: +0x14.
    virtual bool IsFinished();

    std::vector<FGcsLine*> Lines;       // +0x08
    bool bFinished = false;             // +0x14

protected:
    // The conversation ctors' common step: a line that CanPlay is appended (FUN_06f937c0),
    // otherwise it is deleted through its vtable and false is returned (the caller then
    // clears its system, which makes IsValid false).
    bool AddLine(FGcsLine* Line);
};

class FGcsLaunchEvent;      // DockQueues.h (type 3)
class FGcsDockGrantedEvent; // DockQueues.h (type 2)
class FGcsObjectEvent;      // below (types 1..3)
class FGcsPlayerObjectEvent; // below (type 5)

// Type 1 (0x24 bytes, vtable 06fb72c0: IsValid FUN_06ee6e20), ctor FUN_06ee2000 (RET 4): the
// station hails a SpacePop ship (FGcsDockHailEvent). The hail first; a hostile attitude
// (<= -0.6) gets the ship's angry answer, otherwise the ship submits its data, the station
// acknowledges, and the ship's guide type adds what it is doing (2 trade, 3 patrol, 4 wander).
class FGcsDockHailConversation : public FGcsConversation
{
public:
    // Reads the event's +0x04..+0x0c only (any type-1 event).
    explicit FGcsDockHailConversation(FGcsObjectEvent const* Event);
    // FUN_06ee6e20 (shared with type 2).
    bool IsValid() override;

    FSystemInfo* System;                // +0x18 event +0x04
    unsigned int Speaker;               // +0x1c event +0x08 (the station)
    unsigned int Listener;              // +0x20 event +0x0c (the ship)
};

// Type 2 (0x34 bytes, vtable 06fb7448: IsValid FUN_06ee6e20), ctor FUN_06ee6e60 (RET 4): a
// ship at a dock target (the event's speaker; listener the ship). By the target's type:
// 0x400 no line; 0x40 / 0x800 (jump gate / hole) and 0x80 (trade lane ring) a line of the
// formation leader of a SpacePop formation; anything else the dock request (unless the ship
// waited in the queue) and the dock's answer by Response. A line that cannot play clears
// System.
class FGcsDockConversation : public FGcsConversation
{
public:
    explicit FGcsDockConversation(FGcsDockGrantedEvent const* Event);
    // FUN_06ee6e20: System, the listener's ship (FindShip) and the speaker (FindShipOrSolar).
    bool IsValid() override;

    FSystemInfo* System;                // +0x18 event +0x04
    unsigned int Speaker;               // +0x1c event +0x08 (the dock target)
    unsigned int Listener;              // +0x20 event +0x0c (the ship)
    int DockIndex;                      // +0x24 event +0x10
    unsigned int DockType;              // +0x28 event +0x14
    int Response;                       // +0x2c event +0x18 (0 hostile, 2 denied, 3 stand by,
                                        //        4 granted; 1 and others no answer)
    // +0x30 event +0x1c (byte): the ship waited in the dock queue -- only the dock's
    // "now cleared" line is spoken.
    bool bWaitedInQueue;
};

// Type 3 (0x2c bytes, vtable 06fb7470: IsValid LAB_06ee8870), ctor FUN_06ee8880 (RET 4): a
// ship leaves a terminal. The dock clears it (FGcsLineLaunchClearance); a ship that is no
// player's answers with FGcsLineAck. Either line failing to play clears System.
class FGcsLaunchConversation : public FGcsConversation
{
public:
    explicit FGcsLaunchConversation(FGcsLaunchEvent const* Event);
    // LAB_06ee8870: System != null.
    bool IsValid() override;

    FSystemInfo* System;                // +0x18 event +0x04
    unsigned int Speaker;               // +0x1c event +0x08 (the dock)
    unsigned int Listener;              // +0x20 event +0x0c (the ship)
    int DockIndex;                      // +0x24 event +0x10
    unsigned int TerminalType;          // +0x28 event +0x14
};

// Type 5 (0x24 bytes, vtable 06fb72e8: IsValid LAB_06ee8870), ctor FUN_06ee4cd0 (RET 4): a
// player hails an NPC (FGcsHandlerSet message 0x1f). The object's formation leader (the
// object itself outside a formation) answers when its attitude towards the player is at
// least -0.6 and it is a SpacePop ship: busy -> combat blow-off; the same player within 15 s
// -> blow-off; otherwise it names itself and its guide type adds what it is doing (2 trade,
// 3 patrol, 4 wander).
class FGcsPlayerHailConversation : public FGcsConversation
{
public:
    explicit FGcsPlayerHailConversation(FGcsPlayerObjectEvent const* Event);
    // LAB_06ee8870 (shared with type 3): System != null.
    bool IsValid() override;

    FSystemInfo* System;                // +0x18 event +0x04
    FContentPlayerState* Player;        // +0x1c event +0x08
    FContentSpaceObj* Object;           // +0x20 event +0x0c
};

class FGcsCombatEvent;      // below (types 6..0x10)
class FGcsScanEvent;        // below (types 0x11..0x18)
class FGcsFidgetEvent;      // below (type 0x19)

// Types 0x11..0x18 (0x24 bytes, vtable 06fb72fc: IsValid LAB_06ee8870), ctor FUN_06ee55f0
// (RET 4): a patrol scans a ship. One line per type (0x14: a scanned NPC complies first; 0x15
// the scanned NPC refuses); a line that cannot play is deleted and clears System. Type 0x17
// (and 0x15 with a player listener) reaches the common tail with no line: retail calls
// through a null pointer there (the scan worker never sends them).
class FGcsScanConversation : public FGcsConversation
{
public:
    explicit FGcsScanConversation(FGcsScanEvent const* Event);
    // LAB_06ee8870: System != null.
    bool IsValid() override;

    FSystemInfo* System;                // +0x18 event +0x04
    unsigned int Speaker;               // +0x1c event +0x0c (the patrol)
    unsigned int Listener;              // +0x20 event +0x10 (the scanned ship)
};

// Types 6..0x10 (0x1c bytes, vtable 06fb72d4: IsValid LAB_06ee8870), ctor FUN_06ee2c20 (RET
// 4): combat chatter, one case per event type (see the ctor). Only the event's system is
// kept; lines that cannot play are deleted and clear it.
class FGcsCombatConversation : public FGcsConversation
{
public:
    explicit FGcsCombatConversation(FGcsCombatEvent const* Event);
    // LAB_06ee8870: System != null.
    bool IsValid() override;

    FSystemInfo* System;                // +0x18 event +0x04

private:
    // The ctor's common tail (e.g. 06ee2d40-06ee2d64 / 06ee4356 / 06ee4538): a line that can
    // play is appended (true); otherwise it is deleted and System = null (false).
    bool AddLineOrClear(FGcsLine* Line);
    // The "the formation acknowledges" loop of cases 9, 0xa, 0xb, 0xe (e.g.
    // 06ee3370-06ee347f): the formation of Ship is collected again (FUN_06f66220) and every
    // member after the first that is no player's (FUN_06f49bc0) acknowledges Ship with
    // FGcsLineAck; the first line that cannot play stops it (deleted, System = null).
    void AddFormationAcks(FContentShipBase* Ship);
    // Cases 9 / 0xb, leader with an id divisible by 5 (e.g. 06ee3103-06ee32b0): on the rand
    // pick the first member after the first that is no player's asks to engage, else (or
    // with none) Ship announces the enemy. False when that line could not play.
    bool AddEnemySightedLine(FContentShipBase* Ship);
};

// Type 0x19 (0x24 bytes, vtable 06fb745c: the base's deleting dtor 06ed6db0, IsValid
// LAB_06ee8870), ctor FUN_06ee7b00 (RET 4): an NPC near a player chatters (FGcsHandler2). By
// the ship's guide type (slot +0x28: 2 trade, 3 patrol, 4 wander) and the event's Kind (0
// simple, 1 formation status, 2 rumour) one line, or the leader's status query and the
// wingmen's answers. A line that cannot play is deleted and clears System.
class FGcsFidgetConversation : public FGcsConversation
{
public:
    explicit FGcsFidgetConversation(FGcsFidgetEvent const* Event);
    // LAB_06ee8870: System != null.
    bool IsValid() override;

    FSystemInfo* System;                // +0x18 event +0x04
    FContentSpaceObj* Ship;             // +0x1c event +0x08 (an NPC ship, not null-tested)
    int Kind;                           // +0x20 event +0x0c

private:
    // The status query of Kind 1 with more than one member (e.g. 06ee7c72-06ee7d44): the
    // leader's FGcsLineFidgetStatusQuery, then every member after the first answers
    // (FGcsLineFidgetStatusReport, listener the leader). The first line that cannot play is
    // deleted, System = null, and false is returned.
    bool AddStatusQuery(std::vector<FContentShipBase*> const& Members);
};

// ---------------------------------------------------------------------------------------
// Events handed to the conversation manager. Every event class has a one-slot vtable
// whose slot 0 returns its type (ICF-shared `MOV EAX,n; RET` bodies).
// ---------------------------------------------------------------------------------------

class FGcsEvent
{
public:
    virtual int GetType() const = 0;

    FSystemInfo* System = nullptr;      // +0x04
};

// Types 1..3: two space objects (+0x08 the speaker, +0x0c the listener; the admission
// test FUN_06eebd10 and the record FUN_06eeb920 take them in this order).
class FGcsObjectEvent : public FGcsEvent
{
public:
    unsigned int Speaker = 0;           // +0x08
    unsigned int Listener = 0;          // +0x0c
};

// Type 1 (vtable 06fb74c0 = {LAB_06ee9e10}), built by the dock hail handler: the solar of
// the touched phantom speaks to the ship.
class FGcsDockHailEvent : public FGcsObjectEvent
{
public:
    int GetType() const override;
};

// Type 5 (vtable 06fb74bc = {LAB_06ee99a0}), built by FGcsHandlerSet for message 0x1f.
class FGcsPlayerObjectEvent : public FGcsEvent
{
public:
    int GetType() const override;

    FContentPlayerState* Player = nullptr;  // +0x08 FindContentPlayerStateByShip(&Parms[0])
    FContentSpaceObj* Object = nullptr;     // +0x0c System->FindSpaceObj(Parms[1])
};

// Types 6..0x10 (vtable 06fb74d4 = {FUN_06eea860}: the ICF getter returns +0x10), built on
// the stack by FGcsHandler1 for the combat messages. The four derived classes (own vtables
// holding the same getter) add a value at +0x14.
class FGcsCombatEvent : public FGcsEvent
{
public:
    int GetType() const override;

    unsigned int ObjectA = 0;           // +0x08 (e.g. the attacker / the ship)
    unsigned int ObjectB = 0;           // +0x0c (e.g. the victim / the target)
    int Type = 0;                       // +0x10
};

// Type 6 (vtable 06fb74d0): +0x14 the victim's relative health.
class FGcsCombatDamageEvent : public FGcsCombatEvent
{
public:
    float Health = 0.0f;                // +0x14
};

// Type 0xa (vtable 06fb74c8): +0x14 message 0x1001's word 0 (3 or 4).
class FGcsCombatOverEvent : public FGcsCombatEvent
{
public:
    int Reason = 0;                     // +0x14
};

// Type 0xb (vtable 06fb74cc): +0x14 message 0x1001's word 0 (1 or 2; 2 = too tough).
class FGcsCombatNoEngageEvent : public FGcsCombatEvent
{
public:
    int Reason = 0;                     // +0x14
};

// Type 0xe (vtable 06fb74c4): +0x14 message 0x1004's word 1, the flee reason.
class FGcsCombatFleeEvent : public FGcsCombatEvent
{
public:
    int Reason = 0;                     // +0x14
};

// Types 0x11..0x18 (vtable 06fb857c = {FUN_06f33f80}: the ICF getter returns +0x08), built by
// the patrol_path guide's scan worker (TU37 06f0cbc0): 0x11 stand by for the scan, 0x12
// nothing found, 0x13 contraband found, 0x14 the player jettisoned, 0x15 an NPC answered,
// 0x16 turn hostile, 0x18 someone else took the loot.
class FGcsScanEvent : public FGcsEvent
{
public:
    int GetType() const override;

    int Type = 0;                       // +0x08
    unsigned int Speaker = 0;           // +0x0c the patrol ship
    unsigned int Listener = 0;          // +0x10 the scanned ship
    unsigned int LootTakerId = 0;       // +0x14 type 0x18: the ship that took the loot
    unsigned int CargoGoodId = 0;       // +0x18 type 0x13: the scanned good
    unsigned int ScanResponse = 0;      // +0x1c 1; type 0x15: get_scan_response (1 / 2)
};

// Type 0x19 (vtable 06fb74d8 = {LAB_06eea870}: 0x19), built on the stack by FGcsHandler2.
class FGcsFidgetEvent : public FGcsEvent
{
public:
    int GetType() const override;

    FContentSpaceObj* Ship = nullptr;   // +0x08 the chattering NPC (a formation leader)
    int Kind = 0;                       // +0x0c 0 simple, 1 formation status, 2 rumour
};

// ---------------------------------------------------------------------------------------
// Conversation manager (DAT_06fd1c08, 0x24 bytes, vtable 06fb7484 = {LAB_06ee91b0}).
// Built by TU53's static initializer 06f5c980 (FUN_06ee8a70, atexit 06f5c9a0 ->
// FUN_06ee8af0).
// ---------------------------------------------------------------------------------------

// A delayed controller message (0x18 bytes): four words, the time waited and the delay.
struct FGcsDelayedMessage
{
    unsigned int Field_0x00;            // +0x00 sent as message word 1
    unsigned int Field_0x04;            // +0x04 sent as message word 2
    unsigned int Field_0x08;            // +0x08 sent as message word 0
    unsigned int Field_0x0c;            // +0x0c sent as message word 3
    float Elapsed;                      // +0x10 0 when queued
    float Delay;                        // +0x14
};

// Message 0xb as FUN_06ee8c50 sends it (0x14 bytes). TODO: meaning of the words; retail
// leaves the last one uninitialised.
struct FGcsSentMessage
{
    unsigned int Words[5];
};

// The empty base of FGcsConversationManager at +0x04 (sharing the address with bEnabled).
// Its ctor is inlined at 06ee8a8b-06ee8a92 (DAT_06fcf1b0 = the derived object) before any
// member is built; its dtor FUN_06eec4d0 (DAT_06fcf1b0 = 0) is inlined at 06ee8bbe after the
// members are gone and is the unwind action of state 0 in both FUN_06ee8a70
// (Unwind@06fabbe0, ECX = this+4) and FUN_06ee8af0 (Unwind@06fabc00). No RTTI in Content.dll,
// so the retail name is unknown.
class FGcsConversationManagerInstance
{
public:
    FGcsConversationManagerInstance();
    ~FGcsConversationManagerInstance();     // FUN_06eec4d0
};

class FGcsConversationManager : public FGcsConversationManagerInstance
{
public:
    // FUN_06ee8a70: the base (DAT_06fcf1b0 = this), then enabled and empty.
    FGcsConversationManager();
    // FUN_06ee8af0: every conversation deleted, then the members, then the base
    // (DAT_06fcf1b0 cleared). Not virtual: the
    // vtable 06fb7484 has the single slot below (06fb7488 starts the next vtable) and the dtor
    // is only called directly (atexit 06f5c9a0).
    ~FGcsConversationManager();

    // Slot 0, LAB_06ee91b0 (RET 0x14): {*A, *B, *C, *D, 0, *Delay} appended to the delayed
    // messages. Callers outside TU25 reach it through the vtable.
    virtual void QueueMessage(unsigned int const* A, unsigned int const* B,
                              unsigned int const* C, unsigned int const* D,
                              float const* Delay);

    // FUN_06ee8bd0 (RET 4): while enabled, the first conversation whose OnMessage takes
    // Parms is deleted once it is finished. SpaceSupport 06f5dc4b.
    void OnMessage(void const* Parms);
    // FUN_06ee8c50 (RET 4): outside single player, every delayed message whose time is up
    // is sent (message 0xb to the SpaceSupport controller). SpaceSupport heartbeat 06f5d722.
    void Update(float DeltaTime);
    // FUN_06ee8d70 (RET 4): starts the conversation of Event; 1.0 when one was queued (for
    // type 7 the first line's +0x10), 0.0 otherwise.
    float QueueConversation(FGcsEvent const* Event);
    // FUN_06ee9140 (RET 4, caller TU61 06f8c56a): while enabled, a clone of Conversation
    // (slot 5) is started and queued when valid, deleted otherwise; true when queued.
    bool Add(FRmConversationBase* Conversation);

    bool bEnabled = true;                               // +0x04
    std::list<FGcsConversation*> Conversations;         // +0x08 (head +0x0c, size +0x10)
    std::vector<FGcsDelayedMessage> DelayedMessages;    // +0x18 (allocator byte +0x14)
};

// DAT_06fd1c08.
extern FGcsConversationManager GGcsConversations;
// DAT_06fcf1b0: the live manager (set by its ctor, cleared by its dtor).
extern FGcsConversationManager* GGcsConversationManager;

// ---------------------------------------------------------------------------------------
// Handlers (base vtable 06fb7494: LAB_06ee9640 deleting dtor, purecall, purecall).
// ---------------------------------------------------------------------------------------

class FGcsHandler
{
public:
    // FUN_06ee9630: vptr reset only.
    virtual ~FGcsHandler() = default;
    // Slot 1: a SpaceSupport message (RET 8).
    virtual void HandleMessage(pub::Controller::EMessage MessageId, void const* Parms) = 0;
    // Slot 2.
    virtual int GetType() const = 0;
};

// Parms of message 0x2d as the dock hail handler reads them.
struct FGcsPhantomTouch
{
    unsigned int Ship;                  // +0x00
    void* Phantom;                      // +0x04
    unsigned int System;                // +0x08
};

// Handler 0 (0x24 bytes, vtable 06fb7488: FUN_06ee99b0, LAB_06ee9c10, LAB_06ea1450):
// a 2500 m phantom around every dockable solar with voice data; a ship touching one is
// hailed by the solar (event type 1) unless the pair is in the recent-hail list.
class FGcsDockHailHandler : public FGcsHandler
{
public:
    FGcsDockHailHandler() = default;
    // FUN_06ee9aa0: OnSystemUnloaded(&0), then the members.
    ~FGcsDockHailHandler() override;
    // LAB_06ee9c10.
    void HandleMessage(pub::Controller::EMessage MessageId, void const* Parms) override;
    // LAB_06ea1450: 0.
    int GetType() const override;

    // FUN_06ee9b70 (thiscall, RET 0xc): {*Phantom, *Ship} appended to the recent hails; a
    // SpaceSupport timer {DAT_06fcf0f4, &entry} removes the oldest after Delay seconds.
    // Phantom 0 matches every phantom. Callers TU29 06ef58d8, TU53 06f5e1de, 06f60be0.
    void SuppressHail(unsigned int const* Ship, void* const* Phantom, int Delay);

    // FUN_06ee9e20 (RET 4): message 0x3c, *SystemId loaded.
    void OnSystemLoaded(unsigned int const* SystemId);
    // FUN_06eea080 (RET 4): message 0x3d and the dtor; *SystemId 0 = every system.
    void OnSystemUnloaded(unsigned int const* SystemId);

    // +0x04: system -> phantom -> solar.
    std::map<unsigned int, std::map<void*, unsigned int>> Phantoms;
    // +0x18: recent hails {phantom (0 = any), ship}.
    std::list<std::pair<void*, unsigned int>> RecentHails;
};

// Handler 1 (4 bytes, vtable 06fb74a0: FUN_06ee9670, LAB_06eea160, LAB_06ee9e10).
class FGcsHandler1 : public FGcsHandler
{
public:
    // LAB_06eea160: the combat messages 0xd (damage), 0x38 / 0x39 (a ship died), 0x1001
    // (engage / no engage / over), 0x1002 (drift), 0x1003 (help), 0x1004 (flight), 0x1005
    // (friendly fire), 0x1009 (taking the lead), 0x100e (in sights) become combat events (types
    // 6..0x10) once IsGcsCommAllowed passes; each is recorded and queued. Message 0x38 gets
    // the queued conversation's result in its word 3.
    void HandleMessage(pub::Controller::EMessage MessageId, void const* Parms) override;
    // LAB_06ee9e10: 1.
    int GetType() const override;
};

// DAT_06fcf0dc: 2500^2, the squared range of the fidget chatter (static initializer
// 06eea880 stores 0x4abebc20; read by FGcsHandler2 06eeab9c / 06eeaccf).
extern float GGcsFidgetRangeSquared;

// Handler 2 (4 bytes, vtable 06fb74ac: FUN_06ee9670, LAB_06eea890, LAB_06ee9660): the fidget
// chatter of NPCs near players.
class FGcsHandler2 : public FGcsHandler
{
public:
    // LAB_06eea890 (06eea890-06eeaedb): on message 0x20 every player not yet handled this
    // tick may (rand) get a chattering formation leader within 2500 m (event type 0x19);
    // the players near that leader count as handled.
    void HandleMessage(pub::Controller::EMessage MessageId, void const* Parms) override;
    // LAB_06ee9660: 2.
    int GetType() const override;
};

// FUN_06ee93f0 (cdecl): the handler of *Type from DAT_06fcf0e0, created on first use
// (0, 1, 2); null for any other type. The map does not own the handlers.
FGcsHandler* __cdecl GetGcsHandler(int const* Type);

// The handler set (DAT_06fd1bf0, 0x14 bytes, vtable 06fb74b8 = {FUN_06ee9900}); TU53's
// static initializer 06f5c9c0 builds it (FUN_06f5ca00, atexit 06f5c9e0 -> FUN_06ee9690).
class FGcsHandlerSet
{
public:
    FGcsHandlerSet() = default;
    // FUN_06ee9690: every handler deleted, front first. Not virtual: the vtable 06fb74b8 has
    // the single slot below (06fb74bc starts the type-5 event's vtable) and the dtor is only
    // called directly (atexit 06f5c9e0).
    ~FGcsHandlerSet();
    // FUN_06ee9900 (RET 8): message 0x1f starts a type 5 conversation; every other
    // message goes to each handler in order. SpaceSupport 06f5d5d0 / 06f5d833.
    virtual void HandleMessage(pub::Controller::EMessage MessageId, void const* Parms);

    // FUN_06ee9740 (RET 4): GetGcsHandler(Type) appended (no null test). SpaceSupport
    // FUN_06f5cda0 adds 0, 1, 2.
    void AddHandler(int const* Type);

    std::vector<FGcsHandler*> Handlers;  // +0x08 (allocator byte +0x04)
};

// DAT_06fd1bf0.
extern FGcsHandlerSet GGcsHandlers;

// ---------------------------------------------------------------------------------------
// Admission (the gcs_clamp flag DAT_06fce368 lives in Content.cpp).
// ---------------------------------------------------------------------------------------

// A spoken comm as FUN_06eeb920 remembers it (list node payload, 0x20 bytes).
struct FGcsRecentComm
{
    FSystemInfo* System;                // +0x00
    float Time;                         // +0x04 the game time
    Vector Position;                    // +0x08 GetLocation of the speaker (else listener)
    unsigned int Speaker;               // +0x14
    unsigned int Listener;              // +0x18
    int Field_0x1c;                     // +0x1c -1. TODO: no reader found yet.
};

// FUN_06eeba30 (cdecl): DAT_06fce360, the interval of a category without its own.
void __cdecl SetGcsDefaultInterval(double Interval);
// FUN_06eeba50 (cdecl): DAT_06fcf140.insert({Category, Interval}) (an existing entry wins).
void __cdecl SetGcsCategoryInterval(int Category, float Interval);
// FUN_06eebb00 (cdecl): DAT_06fcf128.insert({Category, Chance}).
void __cdecl SetGcsCategoryChance(int Category, float Chance);
// FUN_06eebbb0 (cdecl): DAT_06fcf0fc.insert({Category, true}): categories a speaker that
// is not a SpacePop ship may use.
void __cdecl EnableGcsSpeakerCategory(int Category);
// FUN_06eebc60 (cdecl): DAT_06fcf110.insert({Category, true}): the same for listeners.
void __cdecl EnableGcsListenerCategory(int Category);

// FUN_06eebd10 (cdecl): whether a comm of Category between Speaker and Listener may be
// spoken now.
bool __cdecl IsGcsCommAllowed(int Category, FSystemInfo* System, unsigned int Speaker,
                              unsigned int Listener);
// FUN_06eeb920 (cdecl): remembers a spoken comm (at most 10 per category, newest first).
void __cdecl RecordGcsComm(int Category, FSystemInfo* System, unsigned int Speaker,
                           unsigned int Listener);
