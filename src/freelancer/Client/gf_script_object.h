// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Gf\Script (E:\FL\Scratch\Source\Client\Gf\Script.cpp), Gf\ScriptSequence
// (ScriptSequence.cpp), the sequence list at DAT_0066d2d0 and Gf\MirrorDatabase
// (MirrorDatabase.cpp) -- what plays a Thorn script in a room.
//
//   * FGfScript (operator new(0xb4), ctor FUN_0044eab0) is one loaded .thn bound to its room:
//     it spawns one Thorn object per entity (FUN_0044ed90, gf_script_spawn.cpp), keeps the
//     script's streamed / preloaded sound events, and runs the IScriptEngine clock.
//   * FGfScriptSequence (operator new(0x13c), ctor FUN_00454920) runs up to 0x20 scripts one
//     after another (entries of 0x40 bytes) and owns what their spawns create: the props of
//     COMPOUND entities (+0x130), the characters of DEFORMABLE entities (+0x124) and the
//     character bindings (+0x114).
//   * FGfScriptManager (the object at DAT_0066d2d0, vtable 0x005ce114) holds every live
//     sequence: RoomControl creates them (FUN_00456540), the Gameflow tick advances them
//     (FUN_00456650), the room draw draws their effects (FUN_00456740), the reference
//     entities find the entity they name through it (FUN_00456790). Retail keeps the Gf audio
//     fade in the same object (+0x10..+0x20); the port keeps that part in sound_config.cpp
//     (GetClientAudioFade / SnapClientAudioFade / TickClientAudioFade), so FGfScriptManager
//     itself does not carry those fields.
//
// Field offsets are retail's, in comments; the port's layout is not ABI (retail's VC6 lists and
// FmtStr pairs are held as current-STL members).

#include "common_csys.h"
#include "common_text.h" // FmtStr
#include "event_handler.h"

#include <list>
#include <string>

class FObjectProp;
class NnOfferDialog;
struct FCharacterObject;
struct FRoomObject;
struct FRoomRuntime;
struct FThornComponent;
struct FThornEntitySnap;
struct FThornSound;
struct ICamera;
struct IScriptEngine;
struct FGfScriptSequence;
class FGfMirrorDatabase;

// One sound event a script plays or preloads (the 0x20-byte payload of a +0xa8 list node,
// built by FUN_0044f310 from FUN_0044f270's five words).
struct FGfScriptSoundEvent
{
    void* Arch;              // +0x00 Archetype::ISound of the sound entity (Thorn_Sound +0x7c)
    FThornSound* Sound;      // +0x04 the Thorn_Sound that plays it
    int StartMs;             // +0x08 the event's start
    int EndMs;               // +0x0c start + duration
    unsigned char bLoop;     // +0x10 event Flags bit 3
    unsigned char bSpatial;  // +0x11 entity Flags bit 1
    unsigned char Pad12[2];
    int EventId;             // +0x14 the event's unique id
    void* Handle;            // +0x18 ISoundSource of a streamed sound, or null
    unsigned char bStarted;  // +0x1c
    unsigned char Pad1D[3];
};

struct FGfScript
{
    // FUN_0044eab0 -- the empty event list, then FUN_0044ebf0.
    FGfScript();
    // FUN_0044eb20 -- every spawned object deleted (identity slot 13), the table freed, the event
    // list freed.
    ~FGfScript();

    // FUN_0044ebf0 -- every field back to its default (+0x34 is not written).
    void Init();
    // FUN_0044ece0 (thiscall, RET 8) -- +0x30 / +0x34.
    void SetRoom(FRoomRuntime* InRoom, FGfScriptSequence* InSequence);
    // FUN_0044ed00 -- once: the script's sounds stopped, every spawned object told the scene
    // is gone (slot 19), stopped (slot 14) and detached from the engine (slot 13), the
    // sequence's props dropped (FUN_00455ca0), both names freed.
    void Teardown();
    // FUN_0044ed90 -- one Thorn object per entity of the engine (gf_script_spawn.cpp).
    void Spawn();

    // FUN_0044f270 (thiscall, RET 8) -- a START_SOUND event of a created, non-player sound
    // entity: its archetype, object, span and flags into Out. False otherwise.
    bool CollectSoundEvent(int EventId, FGfScriptSoundEvent* Out);
    // FUN_0044f310 -- one list entry per event FUN_0044f270 accepts.
    void CollectSoundEvents();
    // FUN_0044f410 -- every entry not started yet: a plain archetype acquired (slot 3), a
    // streamer started through FUN_00428680 (bAltStart 1); started either way.
    void StartScriptSounds();
    // FUN_0044f500 -- the longest time a held voice line still runs past the clock, 0 for none.
    int GetSoundTailMs();
    // FUN_0044f550 -- every started entry: an archetype released (slot 4), a streamed handle
    // stopped and released; not started any more.
    void StopScriptSounds();
    // FUN_0044f5c0 (thiscall, RET 4) -- the first entry holding Handle leaves the list. Callers:
    // Thorn_Sound's dtor and ISoundControl slot 4 (gf_thorn_sound.cpp).
    void RemoveSoundEvent(void* Handle);
    // FUN_0044f610 (thiscall, RET 8) -- the streamed handle of EventId restarted at Offset
    // (entry found, handle held), or a new source for it (FUN_00428680, bAltStart 0). Caller:
    // Thorn_Sound's ISoundControl slot 3.
    void* PlayStreamedSoundEvent(int EventId, int Offset);

    // FUN_0044f720 (thiscall, RET 0x2c) -- bind the engine and play: spawn, the sound events,
    // IScriptEngine slots 20 (loop), 22 (start) and 26 (clock 0).
    void Start(IScriptEngine* InEngine, char const* InScriptFile, int InBindMode,
               unsigned char bInLoop, unsigned char bInField55, unsigned char bInApplySpatial,
               int InPriority, unsigned char bInFade, char const* InName,
               unsigned char bInPropsToRoom, unsigned char bInSoundsUseFade);
    // FUN_0044f7f0 (thiscall, RET 4) -- advance the clock by Ms (or run out a paused script);
    // true once finished.
    bool Update(int Ms);
    // FUN_0044f950 -- IScriptEngine slots 23 (stop) and 22 (start 0).
    void Restart();
    // FUN_0044f970 (thiscall, RET 4) -- identity slot 18 on every spawned object.
    void DrawEffects(ICamera* Camera);
    // FUN_0044f9a0 (thiscall, RET 8) -- the prop behind entity Index is (un)detached: its
    // engine instance (Thorn object slot 17) goes to the sequence's FUN_00455d20. Callers: the
    // hardpoint connect / disconnect slots of Thorn_Compound (0x00451e7a / 0x00451eba) and
    // Thorn_Deformable (0x00452d9a / 0x00452dda), with 1 / 0.
    void SetEntityPropDetached(int Index, unsigned char bDetached);
    // FUN_0044f9d0 -- +0x44 = 1: the next Update runs the script out.
    void Pause();
    // FUN_0044f9e0 (thiscall, RET 4) -- Update by the remaining time scaled by Scale
    // (rounded; one more millisecond unless Scale < 1).
    void Advance(float Scale);

    // --- the spawn's factories (gf_script_spawn.cpp) -----------------------------------
    FThornComponent* SpawnScene(FThornEntitySnap const* Snap);                  // FUN_0044d630
    void ApplySpecialMarker(FThornEntitySnap const* Snap, FThornComponent* Obj); // FUN_0044d6a0
    FThornComponent* SpawnMarker(FThornEntitySnap const* Snap);                 // FUN_0044d810
    FThornComponent* SpawnMonitor(FThornEntitySnap const* Snap);                // FUN_0044d9b0
    void ApplySpecialCamera(FThornEntitySnap const* Snap, FThornComponent* Obj,
                            float const* CameraProps4);                          // FUN_0044da40
    FThornComponent* SpawnCamera(FThornEntitySnap const* Snap);                 // FUN_0044dba0
    FThornComponent* SpawnCameraReference(FThornEntitySnap const* Snap);        // FUN_0044dc20
    FThornComponent* SpawnCompound(int Index, FThornEntitySnap const* Snap);    // FUN_0044dd80
    FThornComponent* SpawnDeformable(int Index, FThornEntitySnap const* Snap);  // FUN_0044ded0
    FThornComponent* SpawnSound(int Index, FThornEntitySnap const* Snap);       // FUN_0044e030
    FThornComponent* SpawnPlayerEngineSound(FThornEntitySnap const* Snap);      // FUN_0044e180
    FThornComponent* SpawnSoundReference(FThornEntitySnap const* Snap);         // FUN_0044e1f0
    FThornComponent* SpawnLight(FThornEntitySnap const* Snap);                  // FUN_0044e260
    FThornComponent* SpawnLightReference(FThornEntitySnap const* Snap);         // FUN_0044e2d0
    FThornComponent* SpawnPsys(FThornEntitySnap const* Snap);                   // FUN_0044e340
    FThornComponent* SpawnPlayerEnginePsys(FThornEntitySnap const* Snap);       // FUN_0044e3c0
    // FUN_0044e430 (thiscall, RET 0x1c) -- what a COMPOUND entity drives.
    void ResolveCompoundTarget(int Index, FRoomObject** OutObject, long* OutInstance,
                               void** OutAnimScript, Csys* OutOffset, int* OutCategory,
                               unsigned char* bOutUseScene);
    // FUN_0044e870 (thiscall, RET 8) -- the character a DEFORMABLE entity drives.
    FCharacterObject* ResolveCharacter(FThornEntitySnap const* Snap, char const* Actor);

    Csys Frame;                       // +0x00 identity; every Thorn object's base ctor copies it
    FRoomRuntime* Room;               // +0x30 the room this script plays in
    FGfScriptSequence* Sequence;      // +0x34 the sequence that runs it
    FGfMirrorDatabase* Mirror;        // +0x38 the sequence's +0x114 (FUN_00454fb0 0x00454ff6)
    IScriptEngine* Engine;            // +0x3c
    int Duration;                     // +0x40 IScriptEngine slot 17
    unsigned char bPaused;            // +0x44 FUN_0044f9d0; Thorn_Sound slot 3 skips while set
    unsigned char bTornDown;          // +0x45 FUN_0044ed00 ran
    unsigned char Pad46[2];
    FThornComponent** Spawned;        // +0x48 one Thorn object per entity, or null
    int SpawnedCount;                 // +0x4c
    int BindMode;                     // +0x50 how a DEFORMABLE's actor is found (FUN_0044e870)
    unsigned char bLoop;              // +0x54 IScriptEngine slot 20
    unsigned char Field_0x55;         // +0x55 TODO: meaning not recovered; handed to
                                      //       CharacterObject FUN_004454e0 by the spawn and to
                                      //       FUN_00445540 by Thorn_Deformable's slot 14
    unsigned char bApplySpatial;      // +0x56 1: the spawn writes each entity's initial pose
    unsigned char Pad57;
    int Priority;                     // +0x58 camera pool priority ("priority" of the SCENE)
    unsigned char bPropsToRoom;       // +0x5c a spawned prop goes to the room, not the sequence
    unsigned char bFade;              // +0x5d fade to black at the end
    unsigned char bFadeStarted;       // +0x5e
    unsigned char bFinished;          // +0x5f
    unsigned char bSoundsUseFade;     // +0x60 handed to every Thorn_Sound's +0x84
    unsigned char bPlayerActor;       // +0x61 a DEFORMABLE bound the player
    unsigned char Pad62[2];
    Csys PlayerCsys;                  // +0x64 that DEFORMABLE's character frame (FUN_00452e40)
    float PlayerField94;              // +0x94 TODO: the character's slot 4 (+0x10) float
    void* PlayerDeformInstance;       // +0x98 FUN_00452eb0
    FCharacterObject* PlayerCharacter; // +0x9c
    char* Name;                       // +0xa0 malloc'd (FUN_0044f720)
    char* ScriptFile;                 // +0xa4 malloc'd (FUN_0044f720)
    std::list<FGfScriptSoundEvent> SoundEvents; // +0xa8 (head +0xac, size +0xb0)
};

// Gf\MirrorDatabase (0xc bytes at ScriptSequence +0x114): character objects by name -- the
// characters a sequence plays with (RoomControl FUN_0044a540 adds the room's, 0x0044aaf7).
// A list of {object, VC6 std::string}.
class FGfMirrorDatabase
{
public:
    // FUN_004539b0 -- the empty list.
    FGfMirrorDatabase();
    // FUN_004539e0 -- every node freed, the list gone.
    ~FGfMirrorDatabase();

    // FUN_00453a40 (thiscall, RET 8) -- append {Object, Name}; a null Name is "".
    void Add(FCharacterObject* Object, char const* ObjName);
    // FUN_00453b50 -- the first object, or null.
    FCharacterObject* First() const;
    // FUN_00453b60 (thiscall, RET 4) -- the first object whose name matches (stricmp), or null.
    FCharacterObject* Find(char const* ObjName) const;

    struct FEntry
    {
        FCharacterObject* Object; // node +0x08
        std::string Name;         // node +0x0c
    };

    std::list<FEntry> Entries;    // +0x04 head, +0x08 size
};

// One script slot of a sequence (0x40 bytes; ctor LAB_00454e20, dtor LAB_00454d70).
struct FGfScriptEntry
{
    FGfScriptEntry();
    ~FGfScriptEntry();

    // FUN_00454820 (thiscall, RET 8) -- a new Script bound to Room / Seq, framed by Frame. A
    // second run reloads the engine from the file first.
    FGfScript* Create(FRoomRuntime* InRoom, FGfScriptSequence* InSeq);

    FGfScript* Script;       // +0x00
    IScriptEngine* Engine;   // +0x04
    char* ScriptFile;        // +0x08 malloc'd
    Csys Frame;              // +0x0c
    int Kind;                // +0x3c 0 plain, 1 / 4 / 6 dialog kinds, 5 traffic (FUN_00455fc0)
};

struct FGfScriptSequence
{
    // FUN_00454920.
    FGfScriptSequence();
    // FUN_00454bb0 -- stopped first unless already (FUN_004554c0(1)); entries, names, the
    // pending object, props and characters deleted.
    ~FGfScriptSequence();

    // FUN_00454e80 (thiscall, RET 4) -- +0x120 = malloc copy of InName.
    void SetName(char const* InName);
    // FUN_00454eb0 (thiscall, RET 0x10) -- append a script: file, kind, frame; the engine loaded
    // from the file when none is given. No capacity check (retail).
    void AddScript(char const* File, int InKind, Csys const* InFrame, IScriptEngine* InEngine);
    // FUN_00454f30 (thiscall, RET 0x14) -- +0xc8..+0xd0 and the two text blocks.
    void SetDialog(int A, int B, int C, FmtStr const& TextA, FmtStr const& TextB);
    // FUN_00454f80 -- play: the setup (FUN_00455290), the first script, active.
    void Play();
    // FUN_00454fb0 (thiscall, RET 4) -- start entry Index.
    void StartEntry(int Index);
    // FUN_00455290 -- the room camera saved, the scripts' animations / audio flags gathered.
    void Setup();
    // FUN_004554c0 (thiscall, RET 4) -- once: location change, player / camera / audio restored,
    // animations released, callback.
    void StopPlayback(unsigned char bArg);
    // FUN_004557d0 (thiscall) -- the multiplayer group offer was answered (IClient slot 114):
    // DAT_00673350 cleared; with the offer dialog up it is closed and the script resumes.
    void OnMissionOfferAnswered();
    // FUN_00455850 (thiscall, RET 8) -- open the kind-1 offer: NN_OfferDialog
    // "ScriptSequence_OfferDialog" into +0x108 with the type / text of +0xc8, a single button
    // unless a Decline branch (kind 3) follows. bHasAcceptBranch is not read.
    void ShowOfferDialog(unsigned char bHasAcceptBranch, unsigned char bHasDeclineBranch);
    // LAB_004559b0 (cdecl) -- the offer's answer: IServer::MissionResponse, then the dialog
    // closed and the script resumed (in multiplayer group offers only once the server answered).
    static void __cdecl OnOfferAnswer(void* Context, int Answer);
    // LAB_00455b10 (cdecl) -- the info dialog (kind 4) closed: +0x10d = 1, the dialog deleted,
    // the script resumed.
    static void __cdecl OnInfoDialogClosed(void* Context, int Answer);
    // FUN_00455b80 (thiscall, RET 8) -- append {Prop, Name}. Caller: FUN_0044e430 (0x0044e82e).
    void AddProp(FObjectProp* Prop, char const* PropName);
    // FUN_00455ca0 -- every prop deleted, the list emptied. Caller: FUN_0044ed00 (0x0044ed58).
    void ClearProps();
    // FUN_00455d20 (thiscall, RET 8) -- the first prop of Props whose engine instance (RoomObject
    // slot 6) is Instance gets bDetached (ObjectProp +0x48); none, nothing.
    void SetPropDetached(void* Instance, unsigned char bDetached);
    // FUN_00455d70 (thiscall, RET 8) -- append {Character, Name}. Caller: FUN_0044e870.
    void AddCharacter(FCharacterObject* Character, char const* CharName);
    // FUN_00455ed0 (thiscall, RET 4) -- the first character named CharName (stricmp), or null
    // (null for a null name).
    FCharacterObject* FindCharacter(char const* CharName) const;
    // FUN_00455f50 (thiscall, RET 8) -- how many entries ahead the next one of Kinds is.
    int FindNextOfKinds(int const* Kinds, int KindCount) const;
    // FUN_00455fc0 -- the entry that follows the current one.
    int NextEntryIndex() const;
    // FUN_00456150 (thiscall, RET 4) -- advance by Ms; true once the sequence is done.
    bool Update(int Ms);
    // FUN_004562e0 -- the current script restarted (FUN_0044f950), playing again.
    void RestartCurrent();
    // FUN_00456300 (thiscall, RET 4) -- while playing, FUN_0044f9e0 on the current script.
    void AdvanceCurrent(float Scale);

    struct FProp
    {
        FObjectProp* Prop;   // node +0x08
        std::string Name;    // node +0x0c
    };
    struct FCharacter
    {
        FCharacterObject* Character; // node +0x08
        std::string Name;            // node +0x0c
    };

    typedef void(__cdecl* FStopCallback)(void* A, void* B, unsigned char bArg);

    int BindMode;                  // +0x00 -> FGfScript +0x50
    unsigned char bLoop;           // +0x04 -> FGfScript +0x54
    unsigned char Field_0x05;      // +0x05 -> FGfScript +0x55
    unsigned char bLetterbox;      // +0x06 1: letterbox / cursor / hotspot lock around playback
    unsigned char bFade;           // +0x07 -> FGfScript +0x5d
    unsigned char bApplySpatial;   // +0x08 1 -> FGfScript +0x56
    unsigned char bRestoreCamera;  // +0x09 1: the room camera restored at the stop
    unsigned char bDuckAmbient;    // +0x0a the room ambience faded out while playing
    unsigned char bDuckMusic;      // +0x0b the room music faded out while playing
    unsigned char bDuckSlow;       // +0x0c the slow (config) fade instead of an instant one
    unsigned char bSoundsUseFade;  // +0x0d -> FGfScript +0x60; FUN_00456650's refresh gate
    unsigned char bRestorePlayer;  // +0x0e 1: the player character's pose restored at the stop
    unsigned char bHoldTime;       // +0x0f 1: FUN_00558bf0 at each script start
    int Priority;                  // +0x10 -> FGfScript +0x58
    unsigned char bPropsToRoom;    // +0x14 -> FGfScript +0x5c
    unsigned char Field_0x15;      // +0x15 1 TODO: gates +0x2e with +0x16 (0x00455085, 0x00456866)
    unsigned char Field_0x16;      // +0x16 TODO: see +0x15
    unsigned char Pad17;
    FStopCallback Callback;        // +0x18 called at the stop with (+0x1c, +0x20, bArg)
    void* CallbackA;               // +0x1c
    void* CallbackB;               // +0x20
    unsigned RtcId;                // +0x24 non-zero: IServer::RTCDone(session, it) at the stop
                                   //       (slot 69, +0x114); FUN_00435dd0 sets ScriptBehavior
                                   //       +0x25c
    unsigned Location;             // +0x28 the location to move to at the stop, 0 = none
    unsigned char bStopped;        // +0x2c 1 until Setup, 1 again after StopPlayback
    unsigned char bPlaying;        // +0x2d Play / RestartCurrent
    unsigned char bSkip;           // +0x2e run the current script out
    unsigned char bFindable;       // +0x2f FUN_00456540's flag; FUN_00456790 searches only these
    unsigned char bPlayerSaved;    // +0x30 the player character's pose below was saved
    unsigned char Pad31[3];
    Csys PlayerCsys;               // +0x34 FGfScript +0x64
    float PlayerField64;           // +0x64 FGfScript +0x94
    void* PlayerDeformInstance;    // +0x68 FGfScript +0x98
    FCharacterObject* PlayerCharacter; // +0x6c FGfScript +0x9c
    float CameraPos[3];            // +0x70 the room camera at Setup
    float CameraOrient[9];         // +0x7c
    float CameraHalfFovX;          // +0xa0
    float CameraZNear;             // +0xa4
    float CameraZFar;              // +0xa8
    unsigned char bHasAmbient;     // +0xac a script plays BackgroundAmbient
    unsigned char bHasMusic;       // +0xad a script plays BackgroundMusic
    unsigned char PadAE[2];
    FRoomRuntime* Room;            // +0xb0
    FGfScriptEntry* Entries;       // +0xb4 new[0x20]
    int Capacity;                  // +0xb8 0x20
    int Count;                     // +0xbc
    int Current;                   // +0xc0 -1 = none
    unsigned char bActive;         // +0xc4
    unsigned char PadC5[3];
    int OfferType;                 // +0xc8 FUN_00454f30's first argument: 1..4 pick the
                                   //       NN_OfferDialog type and text (FUN_00455850); 3 is
                                   //       the multiplayer group offer (FUN_004559b0 /
                                   //       FUN_00455fc0)
    int OfferArg;                  // +0xcc IServer::MissionResponse argument 0 (FUN_004559b0)
    int OfferMissionId;            // +0xd0 IServer::MissionResponse MissionId (FUN_004559b0)
    FmtStr DialogTextA;            // +0xd4 the offer text of types 1, 3, 4
    FmtStr DialogTextB;            // +0xec the offer text of type 2; the info text (kind 4)
    unsigned char bOfferShown;     // +0x104 the kind-1 NN_OfferDialog is up
    unsigned char bOfferAnswered;  // +0x105 FUN_004559b0 sent the answer
    unsigned char bOfferAccepted;  // +0x106 the answer was Accept
    unsigned char Pad107;
    NnOfferDialog* OfferDialog;    // +0x108 FUN_00455850's "ScriptSequence_OfferDialog"
    unsigned char bInfoShown;      // +0x10c the kind-4 NN_OfferDialog is up
    unsigned char bInfoClosed;     // +0x10d FUN_00455b10 ran
    unsigned char Pad10E[2];
    NnOfferDialog* InfoDialog;     // +0x110 "ScriptSequence_InfoDialog" (StartEntry kind 4)
    FGfMirrorDatabase Mirror;      // +0x114
    char* Name;                    // +0x120
    std::list<FCharacter> Characters; // +0x124 (head +0x128, size +0x12c)
    std::list<FProp> Props;        // +0x130 (head +0x134, size +0x138)

private:
    FGfScriptSequence(FGfScriptSequence const&) = delete;
    FGfScriptSequence& operator=(FGfScriptSequence const&) = delete;
};

// The object at DAT_0066d2d0 (sequence half; the fade half is sound_config.cpp). Its Event::
// Handler face (vtable 0x005ce114, one slot LAB_00456840) takes KeyMap id 0xf.
class FGfScriptManager : public EventHandler
{
public:
    // FUN_00456360 (static init LAB_00456330) is the implicit constructor: the EventHandler base,
    // the empty list; the fade half it zeroes (+0x10..+0x20) is sound_config.cpp's GAudioFade.
    FGfScriptManager() = default;
    // FUN_004563d0 (the atexit LAB_00456350) -- every sequence still listed deleted (FUN_00454bb0
    // + operator delete), then the list and the EventHandler base (FUN_00576010).
    ~FGfScriptManager();

    // FUN_00456540 (thiscall, RET 8) -- a new sequence for Room with 0x20 empty entries,
    // appended.
    FGfScriptSequence* CreateSequence(FRoomRuntime* Room, unsigned char bFindable);
    // FUN_00456650 (thiscall, RET 4) -- every sequence advanced (the finished ones deleted),
    // then the Gf fade advanced by Ms * 0.001 and, when it moved, identity slot 21 on every
    // spawned object of the current script of each sequence with +0x0d.
    void Update(int Ms);
    // FUN_00456740 (thiscall, RET 8) -- FUN_0044f970 on the current script of every playing
    // sequence of Room. Caller: the room draw FUN_0044b570 (0x0044b8c8).
    void DrawEffects(FRoomRuntime* Room, ICamera* Camera);
    // FUN_00456790 (thiscall, RET 8) -- the first created (not reference) entity of Type named
    // EntityName among the current scripts of the findable sequences, or null.
    FThornComponent* FindCreatedEntity(int Type, char const* EntityName);
    // LAB_00456840 (vtable slot 0) -- event 0xf: every letterboxed sequence is marked to skip
    // (when playing and +0x15), and FUN_0045b3c0 runs when there was one; true then.
    bool OnEvent(unsigned MsgId) override;
    // FUN_004568a0 -- false while a letterboxed sequence without +0x15 exists.
    bool CanSkipAll() const;
    // FUN_004568d0 (thiscall, RET 4) -- Sequence deleted and taken out, if listed.
    void DestroySequence(FGfScriptSequence* Sequence);
    // FUN_00456930 -- every sequence deleted. Caller: RoomControl FUN_00449420 (0x0044945c).
    void DestroyAll();
    // FUN_004564d0 -- register id 0xf. Callers: the base enter FUN_005588a0 (0x0055893d) and
    // the intro 0x00573990.
    void RegisterEvents();
    // FUN_004564e0 -- every sequence deleted, id 0xf unregistered. Callers: the base leave
    // FUN_00558a50 (0x00558a6c) and the menu 0x0057405d / 0x005740ee / 0x00574a9a.
    void Shutdown();

    std::list<FGfScriptSequence*> Sequences; // +0x08 head, +0x0c size
};

extern FGfScriptManager G_GfScriptManager;

// FUN_00454910 -- DAT_0066d304, set by the option callback LAB_004548f0 (value < 0.5). Read by
// FUN_00454fb0 (0x00455075) for the skip latch.
bool GfScriptSequenceSkipLatch();
// LAB_004548f0 (cdecl, float) -- DAT_0066d304 = Value < 0.5 (`TEST AH,1` + JZ: not less keeps 0).
void GfScriptSequenceSetSkipOption(float Value);

// FUN_0044fa60 (cdecl) -- Levels times DALib::Engine slot 53 (the parent instance) up from
// Instance; Levels not positive returns Instance itself. Caller: Thorn_Deformable's IK start
// (0x0045248f) with the descriptor's count_to_root.
void* GfEngineInstanceAncestor(void* Instance, int Levels);
