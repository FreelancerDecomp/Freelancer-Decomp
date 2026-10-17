// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// -------------------------------------------------------------------------------------------
// HUD_Comm -- retail Client\Space\HUD_Comm.cpp (FDUMP path .rdata 0x005d7220), freelancer.exe
// 0x004ca640..0x004cefe0 (the EngineCharacter half of the range, 0x004c98a0..0x004ca610, is
// engine_character.cpp), plus Client\space\radio.h's comm inbox (FDUMP path .rdata 0x005decec)
// that IClient::SendComm LAB_0053bfb0 fills.
//
// The one element is HUD_Receiver (RTTI .?AUHUD_Receiver@@ : Win3D @0, HUDItem @0x7c; 0x5b8
// bytes). Its factory is the static HUDFACTORY<HUD_Receiver> DAT_00673578 ("Receiver"; XC entry
// LAB_004cc850, ctor FUN_004cd270, atexit LAB_004cc870 -> FUN_004cf010), vftable 0x005d74d0
// whose Create FUN_004cd2e0 is operator new(0x5b8) + FUN_004cd350 and the HUDItem face.
//
// vftables:
//   Win3D face 0x005d764c (28 slots) -- overrides 0 FUN_004ce8d0 (scalar deleting dtor around
//     FUN_004ce8f0), 11 FUN_004cd9d0 Update, 12 FUN_00406d10 (`JMP [EAX+0x2c]`: UpdateInactive
//     is Update), 13 FUN_004cd8a0 Draw; every other slot is Win3D's 0x005d5544 entry.
//   HUDItem face 0x005d761c (11 slots) -- overrides 0 LAB_004cee70 (ECX -= 0x7c, JMP
//     FUN_004ce8d0) and 3 FUN_004cdf30 Init (both inbox counts zeroed, true).
//
// It owns two comm channels. Channel A (+0xb4) is the one with a window: the talking head
// (EngineCharacter body, a HUD_genericcomm.3db corner frame, the static noise) and the caption
// TextControl. Channel B (+0x284) only speaks. Slot A (+0x454) feeds channel A from the player's
// inbox DAT_00673f48 (messages the player's ship sends or receives), slot B (+0x464) feeds
// channel B from DAT_006735c0 (everything else).
//
// Holes, not scaffolds (see the decomp tracker, epic:hud-comm):
//   * (closed 2026-09-25) Voice lines: FVoiceMsg is now an Archetype::ISound (sound_archetype.h)
//     and PlayLine hands it to FUN_00428680 (PlayClientSoundArch) as retail does.
//   * (closed 2026-09-26) The talking head: Appearance (FUN_004cb250) resolves each voice script
//     through gf\CharacterInfo.cpp's script database (character_info.h, FUN_004356c0 /
//     FUN_00434e90) and plays it with IDeformable slot 7. The database is filled by
//     LoadCharacterInfo / PreloadCharacterScripts (FUN_004354e0 / FUN_004355e0).
// -------------------------------------------------------------------------------------------

#include "client_mesh_load.h"   // FClientMeshHolder
#include "common_reputation.h"  // Costume
#include "common_server_types.h" // CommResult
#include "hud_element_list.h"   // HUDItem
#include "space_camera_game.h"  // FGameCamera
#include "texture_shapes.h"     // FTextureRef
#include "win3d.h"
#include "x86math.h"            // FMatrix, FVector, FTransform

#include <stddef.h>
#include <vector>

class INI_Reader;
class NnTextControl;
struct CObject;
struct FEngineCharacterBody;
struct FEngineLight;
struct FVoiceMsg;
struct FVoiceProfile;
struct ICamera;
struct ISoundSource;

// ============================================================================================
// Radio inbox (radio.h)
// ============================================================================================

// One comm message (0x98 bytes). Field names follow what reads them; the server calls +0x40 the
// comm type and +0x88 the mood.
struct FCommMessage
{
    // FUN_004cdc30 -- the four costume parts and the accessory count 0, TextCount 1, +0x88 and
    // +0x90 zero; nothing else is written (FUN_004cdc10 is the costume-only half). The port
    // zeroes the rest too: retail leaves it as whatever the stack or .bss held.
    FCommMessage();

    unsigned Sender;          // +0x00 object id
    unsigned Receiver;        // +0x04 object id
    Costume SenderCostume;    // +0x08 (0x34 bytes)
    unsigned VoiceId;         // +0x3c
    unsigned CaptionIds;      // +0x40 -- the name line of the caption (Appearance's NameIds)
    unsigned TextIds[16];     // +0x44 -- voice message ids, played in permutation order
    int TextCount;            // +0x84
    unsigned PermutationIds;  // +0x88 -- ids of a "2, 0, 1" order string; 0 = in order
    float Attenuation;        // +0x8c dB, set by Dispatch from the distance
    float Linger;             // +0x90 seconds the channel stays busy after the last line
    CommResult Result;        // +0x94 -- what CommComplete reports
};

static_assert(offsetof(FCommMessage, VoiceId) == 0x3c, "comm message +0x3c voice");
static_assert(offsetof(FCommMessage, TextIds) == 0x44, "comm message +0x44 text ids");
static_assert(offsetof(FCommMessage, TextCount) == 0x84, "comm message +0x84 count");
static_assert(offsetof(FCommMessage, Result) == 0x94, "comm message +0x94 result");
static_assert(sizeof(FCommMessage) == 0x98, "retail comm message is 0x98 bytes");

// A fixed 16-record queue (0x984 bytes).
struct FCommInbox
{
    // LAB_004c9820 / LAB_004c9860 -- FUN_004cdc30 on every record, Count = 0.
    FCommInbox();

    // FUN_004cde50 (thiscall, RET 4) -- false when empty, else Out = the head (FUN_004ce810's
    // copy, inlined).
    bool Peek(FCommMessage* Out) const;
    // FUN_004cdc60 (thiscall, RET 4) -- false when empty, else Out = the head, every other
    // record moved down one and Count - 1.
    bool Pop(FCommMessage* Out);

    unsigned Count;               // +0x00
    FCommMessage Records[16];     // +0x04
};

// DAT_00673f48 -- the player's inbox; DAT_006735c0 -- everyone else's.
extern FCommInbox G_CommInboxPlayer;
extern FCommInbox G_CommInboxOther;

// FUN_004ce810 (thiscall on To, RET 4) -- the record copy every queue move uses: the costume's
// accessory ids only up to its count (FUN_0053c250's rule), every other field whole.
void CommCopyMessage(FCommMessage* To, FCommMessage const* From);
// FUN_0053c250 (thiscall on To, RET 4) -- the four parts, the count, and only the first Count
// accessory ids; the rest of To keeps what it had.
void CommCopyCostume(Costume* To, Costume const* From);

// LAB_0053bfb0 -- IClient::SendServerSendComm (slot 68, +0x110, thiscall, RET 0x5c): the
// server's SendComm. PlayerId (the client id) and bUnused (the trailing byte argument) are not
// read. SenderCostume is the costume IClient passes by value (13 of the 23 dwords). TextIds /
// TextCount are copied, the rest go straight into the record. The record is queued in the
// player's inbox when the player's ship (FUN_0054baf0, slot 8) is the sender or the receiver,
// else in the other inbox; a full inbox FDUMPs radio.h line 0x7c.
void ClientSendComm(unsigned PlayerId, unsigned Sender, unsigned Receiver, unsigned VoiceId,
                    Costume const& SenderCostume, unsigned CaptionIds, unsigned const* TextIds,
                    int TextCount, unsigned PermutationIds, float Linger, bool bUnused);

// ============================================================================================
// Spaceflight ducking shared by both channels (DAT_006735a4..DAT_006735b0).
// ============================================================================================
struct FCommDuck
{
    float TargetDb;       // +0x00 DAT_006735a4
    float CurrentDb;      // +0x04 DAT_006735a8 -- channel B's +0x1c follows it every frame
    float FadeRemaining;  // +0x08 DAT_006735ac, -1 = no fade (ClientSoundFadeDb)
    int RefCount;         // +0x0c DAT_006735b0

    // FUN_004ca670 (thiscall) -- --RefCount; at zero the fade back to 0 dB over
    // DAT_006108f8 (SoundCFG.ini ducking_comm_up_time, 0.25 s) starts. Every caller runs
    // PopClientSoundDucking first.
    void Release();
};

extern FCommDuck G_CommDuck;

// ============================================================================================
// The channel (0x1d0 bytes): a voice part (+0x00..+0x37), a window (+0x38..+0x16f), the rest.
// ============================================================================================

// One queued voice line (0x10 bytes).
struct FCommLine
{
    unsigned MsgId;         // +0x00
    FVoiceMsg* Msg;         // +0x04
    unsigned char bAcquired; // +0x08 -- Msg slot 4 (Release) is owed; Play never sets it
    ISoundSource* Source;   // +0x0c
};

class FCommVoice
{
public:
    // The first half of FUN_004ca690: the watcher built and set(0), the line vector, then
    // FUN_004ca910.
    FCommVoice();
    // FUN_004ca800 (the unwind form; HUD_Receiver's dtor inlines the same steps): the static
    // stopped and released, every line's source stopped and released (FUN_00489dc0), the
    // ducking dropped, FUN_004ca910, the vector freed, the watcher cleared.
    ~FCommVoice();

    // FUN_004ca910 -- the watcher cleared (set_pointer(0) when set), +0x0c..+0x1c zero,
    // +0x08 = 1, +0x20 = 1.
    void ResetState();
    // FUN_004caae0 -- per line: Msg slot 4 when bAcquired == 1, the source stopped and
    // released; then the vector emptied.
    void StopLines();
    // FUN_004cbb40 -- the static stopped and released, StopLines, the ducking dropped, then
    // ResetState's body inline (with the watcher's set_pointer(0) first).
    void StopVoice();
    // The static-source release every stop path inlines: vt+0x60 Stop, vt+0x08 Release, null.
    void StopStatic();
    // The ducking-drop every stop path inlines (0x004ca894..0x004ca8ba and its copies).
    void DropDucking();

    // FUN_004cae10 (thiscall, RET 0x1c) -- queue Msg's lines (in the permutation order read
    // from +0x88's resource string) and start the first. bStaticSound adds "comm_static_1",
    // bDuck ducks the mix, bPositional pans by the speaker's position. Out* get the sender,
    // the receiver and the last queued message id when anything was queued.
    void Play(FCommMessage const* Msg, bool bStaticSound, bool bDuck, bool bPositionalArg,
              unsigned* OutSender, unsigned* OutReceiver, unsigned* OutLastMsgId);
    // FUN_004cbfc0 (thiscall, RET 4) -- start Line (FUN_00428680 with the channel's play key);
    // on failure the 3 s timer runs instead. Returns true.
    bool PlayLine(FCommLine* Line);
    // FUN_004cc060 (thiscall, RET 0xc; `this` is not read) -- the pan of a listener-space
    // offset: 0 when the speaker is more than ~10 degrees above/below or within 0.1 of the
    // listener, else F * 100 * -sin(a) / max(cos(a), F * 0.01) with a = atan2(-x, |y|) * R / 180,
    // F / R being SoundCFG.ini's spaceflight_dialogue_pan_factor / _range (20 / 90).
    static float Pan(float X, float Y, float Z);

    BaseWatcher Speaker;          // +0x00 -- the sender's CSimple::SelfWatchable
    unsigned char bWatchSpeaker;  // +0x08 -- 1: busy only while Speaker lives
    unsigned char Pad09[3];
    ISoundSource* Static;         // +0x0c -- "comm_static_1"
    float Timer;                  // +0x10 -- 3 s stand-in when a line fails to start
    unsigned char bDucking;       // +0x14
    unsigned char bKeyBit2;       // +0x15 -- bit 2 of the play key's flags; only Play writes it (0)
    unsigned char Pad16[2];
    float Attenuation;            // +0x18 -- the message's +0x8c
    float DuckDb;                 // +0x1c -- channel B: G_CommDuck.CurrentDb every frame
    unsigned char bPositional;    // +0x20
    unsigned char Pad21[3];
    // +0x24 retail VC6 vector<FCommLine> (allocator byte, first, last, end) and +0x34 the
    // iterator of the line playing; the port keeps a std::vector and an index.
    std::vector<FCommLine> Lines;
    size_t Cursor;

private:
    FCommVoice(FCommVoice const&) = delete;
    FCommVoice& operator=(FCommVoice const&) = delete;
};

class FCommWindow
{
public:
    // The window half of FUN_004ca690.
    FCommWindow();
    // FUN_004cab70 -- FUN_0059eee0 on the corner holder when it has an instance, then its
    // destructor FUN_0059ef20 (the same release again).
    ~FCommWindow();

    // FUN_004ccbd0 (thiscall, RET 4) -- corner / btn_pos / win_pos / win_size / fly_time /
    // static; false for any other key.
    bool ReadIni(INI_Reader& Reader);
    // FUN_004ccde0 (thiscall, RET 4) -- the four corner transforms from the open fraction and
    // Rect from Cam's projection of corners 0 and 3.
    void Layout(ICamera* Cam);
    // FUN_004cd160 -- ambient white, then the corner mesh drawn at each of the four transforms.
    void DrawCorners();

    unsigned char bOpen;          // +0x00
    unsigned char Pad01[3];
    int Rect[4];                  // +0x04 -- screen rect of corners 0 and 3 (ctor 0, 0, 1, 1)
    unsigned char Unknown14[0x10]; // +0x14..+0x23 never written
    FVector WinPos;               // +0x24 "win_pos" (ctor 0.3, 0.3, -1)
    FVector BtnPos;               // +0x30 "btn_pos" (ctor 0, 0, -2)
    float WinSizeX;               // +0x3c "win_size" (ctor 0.2)
    float WinSizeY;               // +0x40 (ctor 0.2)
    float OpenFraction;           // +0x44 -- 1 open, 0 closed; nothing animates it
    float FlyTime;                // +0x48 "fly_time" (ctor 0.5); read by nothing
    unsigned char bOpenRequest;   // +0x4c
    unsigned char bCloseRequest;  // +0x4d
    unsigned char bStatic;        // +0x4e "static" (ctor 1)
    unsigned char Pad4f;
    FClientMeshHolder Corner;     // +0x50 "corner" mesh
    FTransform CornerXform[4];    // +0x78 / +0xa8 / +0xd8 / +0x108

private:
    FCommWindow(FCommWindow const&) = delete;
    FCommWindow& operator=(FCommWindow const&) = delete;
};

class FCommChannel : public FCommVoice
{
public:
    // FUN_004ca690.
    FCommChannel();
    // FUN_004ca940 (the unwind form; HUD_Receiver's dtor inlines it): Reset's body, then the
    // window's and the voice's destructors.
    ~FCommChannel();

    // FUN_004cabd0 -- CommText's tree deleted, StopVoice's body, body = 0 and +0x174 / +0x1c0..
    // +0x1cc zeroed. The window stays as it is.
    void Reset();
    // FUN_004cad10 (thiscall, RET 8) -- Reset without deleting the window state, then
    // Play(Msg, 0, 0, bPositional) and the message's linger.
    void PlayNoPortrait(FCommMessage const* Msg, bool bPositionalArg);
    // FUN_004cb250 (thiscall, RET 0x48) -- open the window on Speaker: the talking head from
    // SpeakerCostume (perf option slot 5 above 0), the caption text control under Parent with
    // the pilot name / group name / object name and NameIds.
    void Appearance(CObject* SpeakerObject, CObject* ReceiverUnused, Costume SpeakerCostume,
                    unsigned InNameIds, FVoiceProfile* Profile, Win3D* Parent);
    // FUN_004cbc40 (thiscall, RET 4) -- advance the lines; true while the channel is idle (the
    // window closed or closing, the speaker gone, the linger run out). On the busy-to-idle edge
    // single player gets CommComplete SUCCESS.
    bool TickVoice(float Dt);
    // FUN_004cc140 (thiscall, RET 8) -- open/close requests, Layout on the Win3D camera, Cam's
    // viewport from corners 0 and 3, the head advanced by Dt in 0.2 s steps, and the window
    // closed once the speaker is gone.
    void WindowTick(float Dt, FGameCamera* Cam);
    // FUN_004cc2c0 -- close the window, CommComplete CANCELLED in single player, StopVoice.
    void Cancel();
    // The window close every path inlines (0x004cc28c..0x004cc2ab): requests, body = 0 and the
    // caption hidden.
    void CloseWindow();
    // FUN_004cc440 -- the chrome rectangle around WinPos (line list, additive).
    void DrawFrame();
    // FUN_004cc660 (thiscall, RET 0xc) -- the backdrop quad and the head through Cam with Light.
    void DrawPortrait(FGameCamera* Cam, FEngineLight* Light, void* BackdropTexture);

    FCommWindow Window;               // +0x38
    FEngineCharacterBody* Body;       // +0x170
    unsigned char bNoPortrait;        // +0x174
    unsigned char Pad175[3];
    FMatrix SpeakerRotate;            // +0x178 "speaker_rotate"
    FVector MaleOffset;               // +0x19c "male_speaker_offset"
    FVector FemaleOffset;             // +0x1a8 "female_speaker_offset"
    unsigned NameIds;                 // +0x1b4
    float Linger;                     // +0x1b8
    NnTextControl* CommText;          // +0x1bc "CommText"
    int bWasBusy;                     // +0x1c0
    unsigned CompleteSender;          // +0x1c4 -- CommComplete's three ids
    unsigned CompleteReceiver;        // +0x1c8
    unsigned CompleteMsgId;           // +0x1cc

private:
    FCommChannel(FCommChannel const&) = delete;
    FCommChannel& operator=(FCommChannel const&) = delete;
};

// One inbox feeding one channel (0x10 bytes).
struct FCommSlot
{
    FCommInbox* Inbox;            // +0x00
    FCommChannel* Channel;        // +0x04
    int Priority;                 // +0x08 -- of the message playing
    unsigned char bActive;        // +0x0c
    unsigned char bDeferred;      // +0x0d -- a conflicting message is waiting
    unsigned char Pad0e[2];
};

// FUN_004cc9a0 (cdecl) -- the [HUD] static_txm noise over Rect, blended by static_blend.
void CommDrawStatic(int const* Rect);

// ============================================================================================
// HUD_Receiver
// ============================================================================================
class HUD_Receiver : public Win3D, public HUDItem
{
public:
    // FUN_004cd350 (thiscall, RET 4).
    explicit HUD_Receiver(INI_Reader& Reader);
    // FUN_004ce8f0 (scalar deleting FUN_004ce8d0; HUDItem thunk LAB_004cee70).
    ~HUD_Receiver() override;

    // Win3D slot 11 FUN_004cd9d0.
    void Update() override;
    // Win3D slot 12 FUN_00406d10 -- `JMP [EAX+0x2c]`, i.e. Update.
    void UpdateInactive() override;
    // Win3D slot 13 FUN_004cd8a0.
    void Draw() override;
    // HUDItem slot 3 FUN_004cdf30 -- both inbox counts zeroed; true.
    bool Init() override;

    // FUN_004ce000 (thiscall, RET 8) -- start Msg on Slot's channel.
    void Dispatch(FCommSlot* Slot, FCommMessage* Msg);
    // FUN_004ce4c0 (thiscall, RET 8) -- the per-frame slot logic; channel A's copy is inlined
    // into Update (0x004cda5d..0x004cdbad) with FUN_004cdf40 called instead of inlined.
    void UpdateSlot(FCommSlot* Slot, float Dt);
    // FUN_004cdf40 (thiscall, RET 4; `this` is not read) -- the highest voice-message priority
    // of Msg's lines, -100 without one (or without the voice).
    static int MaxVoicePriority(FCommMessage const* Msg);
    // FUN_004cdfc0 (thiscall, RET 4; `this` is not read) -- in single player,
    // CommComplete(sender, receiver, last text id, Msg->Result).
    static void CommCancelled(FCommMessage const* Msg);

    FCommChannel ChannelA;            // +0x0b4
    FCommChannel ChannelB;            // +0x284
    FCommSlot SlotA;                  // +0x454
    FCommSlot SlotB;                  // +0x464
    FTextureRef Backdrop;             // +0x474 "backdrop"
    FMatrix SpeakerRotate;            // +0x47c "speaker_rotate"
    FVector MaleOffset;               // +0x4a0
    FVector FemaleOffset;             // +0x4ac
    FGameCamera Camera;               // +0x4b8 -- "video_fovx", near 1.4 / far 50
    FEngineLight* Light;              // +0x5b0
    Win3D* LowEndCommButton;          // +0x5b4

private:
    HUD_Receiver(HUD_Receiver const&) = delete;
    HUD_Receiver& operator=(HUD_Receiver const&) = delete;
};

// DAT_006748f8 -- the live HUD_Receiver, or null (ctor 0x004cd813, dtor 0x004ce9d9).
void* GetHudCommInstance();
void SetHudCommInstance(void* Instance);

// FUN_004cc880 (cdecl) -- with a live HUD_Receiver: true when the CSimple channel B watches,
// else the one channel A watches, has id ObjectId. Callers: 0x004d27af, 0x004d39ca
// (HUD_ContactList), 0x004ec17a (HUD_Targetable_Objects FUN_004ebf70), 0x004ef0e8, 0x004f09a9.
bool HudCommIsWatchedObjectId(unsigned ObjectId);
// FUN_004cc8d0 (cdecl) -- channel A's window is open and the letterbox is down. Callers:
// 0x0045ad7b, 0x0045add4.
bool HudCommIsWindowOpen();
// FUN_004cc900 (cdecl) -- with a live HUD_Receiver, build (or find) the body of Costume in the
// EngineCharacter cache; FDUMP line 0x119 when it cannot be built. Caller: 0x0053de92.
void HudCommPreloadCostume(Costume const* SpeakerCostume);

// FUN_004a2c50 (retail NavMap; cdecl) -- a registered Win3D named "NeuroNetNavMap" exists. Kept
// here until NavMap.cpp is ported; its one traced caller is HUD_Receiver::Draw.
bool IsNeuroNetNavMapOpen();
// FUN_0054bc90 (retail space.cpp) GetSpaceScriptCameraLatch now lives in space_camera.{h,cpp}
// next to its writer FUN_0054bb10 (SetupSpaceScriptCamera).
