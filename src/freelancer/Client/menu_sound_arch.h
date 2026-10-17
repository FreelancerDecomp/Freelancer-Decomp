// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "dacom.h"
#include "sound_archetype.h"

// Client SoundArch table (SoundArch.cpp). FUN_0042b030 loads [Sound] from
// freelancer.ini Data sounds=; FUN_0042ae40 looks up by CreateID.
// FUN_0042b5e0 creates the ISoundManager wav archetype into SmArchId.
// Heap play: FUN_00428680 / FUN_00428790 / FUN_00428850 / FUN_00429040.
// Not SoundManager.dll internals.

// FUN_0042b030 -- read every [Sound] of the ini into the registry (DAT_00667cc0); returns how many
// SoundArchs it added. bForce = retail's second argument (0 from the one caller, 0x005b0d6f),
// handed on to the redefine FUN_0042bc70.
int LoadClientSoundArchIni(char const* FullPath, bool bForce);
// FUN_0042ae40 -- the registered SoundArch of Id; null (with the 0x168 "unknown Sound archetype"
// WARNING, which names Id->get_string()) for an empty or unknown id.
void* LookupClientSoundArch(struct ID_String const* Id);
// FUN_0042acb0 -- every registered SoundArch whose persistence bits ((flags >> 20) & Mask & 0xf)
// match and whose SoundManager id is still -1 is acquired once. Mask 1 from the space entry
// (0x0054ac99), 2 from the base entry (0x005588e9).
void PreloadClientSoundArchesByCategory(unsigned Mask);
// FUN_0042ad30 -- the same walk and test (SoundManager id -1 included), releasing once. Mask 1
// from the space exit (0x0054afde), 2 from the base exit (0x00558a80).
void UnloadClientSoundArchesByCategory(unsigned Mask);

// FUN_0042b4d0 — app-shutdown teardown: force-release every archetype's wav references and
// free the whole registry. Retail calls this from WinMain's teardown chain (0x5b22af), right
// after the voice registry drop and before the rest of shutdown.
void ShutdownClientSoundArch();

// FUN_0042b840 — slot 3: create wav if needed, bump UseCount.
unsigned char AcquireClientSoundArch(void* Arch);
// FUN_0042b8a0 — slot 4.
unsigned char ReleaseClientSoundArch(void* Arch);

// FUN_00427a10 — look the archetype up by ID_String (FUN_0042ae40) and Acquire it (slot 3).
// Returns 0 for an empty id or an unknown archetype. Caller: StarSys [Archetype] snd =.
unsigned char PreloadClientSoundArch(struct ID_String const* Id);
// FUN_00427a40 — the matching Release (slot 4). Caller: the StarSys leave FUN_00554ce0.
void UnloadClientSoundArch(struct ID_String const* Id);

// LAB_00503420 / FUN_00428790 — alloc Sound::Source3D (ISoundSource*). Caller owns one
// ref; Release when done. createeffect factory vt+4.
void* CreateClientSoundSource3D();

// LAB_00513460 -- alloc a plain Sound::Source (0x30, FUN_00429040(1)). Slot 1 of the effect
// factories DAT_00674c5c and DAT_006751cc: the tradelane camera flash plays unpositioned.
void* CreateClientSoundSource();

class FClientEquippedObj;
struct ISoundSource;

// FUN_00428f80(0x50) + FUN_0052efb0 -- a CliThrusterSound (vtable 005ddbfc) for the ship
// (0x00545535): exterior archetype always, interior too for this client's own ship, looping.
// Null when the allocation fails. The ship holds it at +0xf0.
ISoundSource* CreateClientThrusterSound(FClientEquippedObj* Ship);
// FUN_0052f110 (RET 4) -- the per-update tick (0x00545567): the working thrusters' summed
// thrust sets the level and their mean position the 3D position; the player's first-person
// view switches to the interior sound. Dt is not read.
void TickClientThrusterSound(ISoundSource* Sound, float Dt);

// FUN_00527a70 — Source3D + Acquire(Arch). Engine equip emitters (retail sizeof 0x68, vtable
// 005dd084, arch reference held at +0x64 and released by its destructor FUN_00527b20);
// gun/InternalFX use CreateClientSoundSource3D + Start separately. Returns ISoundSource*
// (null only when the allocation fails); caller Release when done. Arch must not be null:
// retail calls its Acquire unconditionally.
void* CreateClientEquipSoundEmitter(void* Arch);
// That emitter's +0x64 archetype (FUN_00528240 restarts the emitter with it through
// FUN_004291c0, 0x005282e2 / 0x005283f1).
void* GetClientEquipSoundEmitterArch(void* Emitter);

// FUN_00428680. PlayKey layout matches listing (flags @ +4). OutHandle = param_3.
unsigned char PlayClientSoundArch(void* Arch, unsigned* PlayKey, void** OutHandle,
                                  unsigned char bAltStart);
// FUN_0042a2a0 (Sound::StreamedSource) -- restart a streamed source on Arch at Offset and kick
// its stream. Caller: the Gf script's streamed sound events (FGfScript::PlayStreamedSoundEvent,
// FUN_0044f610 0x0044f707). The answer is only whether the source holds a stream handle.
unsigned char RestartClientStreamedSound(ISoundSource* Handle, void* Arch, int Offset);
// FUN_004285f0(Arch, PlayKey, OutHandle): if any live source already plays Arch, return 0;
// otherwise FUN_00428680(Arch, PlayKey, OutHandle, 0).
unsigned char PlayClientSoundArchIfIdle(void* Arch, unsigned* PlayKey, void** OutHandle);
// FUN_00428640(Arch, PlayKey, OutHandle): the first live source playing Arch is rewound through
// vt+0x64 SetStartOffset(0); with none, FUN_00428680(Arch, PlayKey, OutHandle, 0).
void PlayOrRewindClientSoundArch(void* Arch, unsigned* PlayKey, void** OutHandle);
// FUN_004285c0(Arch): every live source whose +0x18 is Arch gets vt+0x60 (Stop); the next
// node is read before the call.
void StopClientSoundArch(void* Arch);
// FUN_00428430(KeepArch) (cdecl) -- stop and drop the background music / ambience sources, Stop
// every live source whose archetype is not KeepArch, SoundManager MixerTick(0), reset the
// ducking state. Only caller: FUN_004284f0.
void StopAllClientSoundsExceptArch(void* KeepArch);
// FUN_004284f0 (cdecl) -- FUN_00428430(0), then the "sound reference count leaks" warning when
// Sound::Source instances are still alive (DAT_00667cbc), then Heap::Shrink on the three source
// pools (DAT_00667a44 / a34 / a24). Callers: 0x0054afd9 (space exit), 0x00558a7b (base exit),
// 0x005b227b (shutdown).
void StopAllClientSounds();
// FUN_00428550 / FUN_00428580 (cdecl) -- every playing source (DAT_00667c70, from the front)
// paused (slot 26), resp. every paused one (slot 28) resumed (slot 27). Only caller:
// FUN_0042d6f0 (the single-player server-tick pause) on its 0 -> 1 and 1 -> 0 edges.
void PauseAllClientSounds();
void ResumePausedClientSounds();
// FUN_00428000(Arch) -- how many live sources play Arch. Caller: FUN_00564760 (the UI sound
// table's "anything still playing" test).
int CountClientSoundArchSources(void* Arch);

// FUN_00428ba0 / FUN_00428bc0: copy global BGM/ambient source.
unsigned char BindClientBackgroundMusic(void** OutSource);
unsigned char BindClientBackgroundAmbient(void** OutSource);
// FUN_004288e0 / FUN_00428a40 — DAT_00667cb0 / DAT_00667cb4 writers.
unsigned char SetClientBackgroundMusic(unsigned NicknameId, unsigned Flags, float Fade);
unsigned char SetClientBackgroundAmbient(unsigned NicknameId, unsigned Flags, float Fade);
// FUN_00428c50 / FUN_00428d00 (cdecl) -- fade the background music / ambience source to
// TargetDb at Rate dB per second (+0x34 target, +0x38 = |target - current| / Rate). A target
// above 1e8 means the ducking down_by, a rate above 1e8 the ducking down_time as the time.
// Unused is never read. Callers: the Gf script sequence setup / stop (FUN_00455290 /
// FUN_004554c0).
void FadeClientBackgroundMusic(int Unused, float TargetDb, float Rate);
void FadeClientBackgroundAmbient(int Unused, float TargetDb, float Rate);

// FUN_00428040 — per-frame client sound mixer: global ducking fade, listener update,
// audible-source collection, ranking by score (+0x28) and ISoundManager 38/46.
// Camera is retail's listener source (vtable +0xC/+0x10); null falls back to SM slot 22.
void TickClientSoundMixer(void* Camera, float DeltaSeconds);

// FUN_0040f130 / FUN_0040f140 — DAT_006164dc: ICamera* the mixer reads for listener
// velocity (slot 16 / GetVelocity @ +0x48). Space camera mode switch writes it.
void* GetClientSoundListenerObject();
void SetClientSoundListenerObject(void* CameraOrNull);

// Sound::Source3D spatial setters. The cone pair is FUN_00429ea0 / FUN_00429f10 (both
// non-virtual in retail); the pose is vtable slot 37 (FUN_00429e70), whose Transform argument
// is a 3x3 orientation followed by the world position at +0x24.
void SetClientSoundSourceCone(void* SourcePtr, float InnerAngle, float OuterAngle);
void SetClientSoundSourceConeOutsideVolume(void* SourcePtr, float Volume);
void SetClientSoundSourcePose(void* SourcePtr, int AttachHandle, float const* Transform);
// The source's +0x04 category (0 sfx, 1 interface, 2/5 voice, 3 music, 4 ambient), written
// directly by callers after FUN_00428680 (HUD_Comm 0x004cb0f6 / 0x004cb1f4 store 2).
void SetClientSoundSourceType(void* SourcePtr, int Type);

// FUN_00427940 (cdecl) -- one step of a dB fade. False when *Remaining < 0 (no fade running).
// Otherwise *Remaining -= Dt; at or below zero *Remaining = -1 and *OutDb = ToDb; else
// *OutDb = FromDb..ToDb interpolated Dt / (old remaining) of the way in power-ratio space,
// -96 dB at or below 10^-9.6. Returns true. Caller: HUD_Receiver::Update (0x004cdf76).
bool ClientSoundFadeDb(float FromDb, float ToDb, float Dt, float* Remaining, float* OutDb);

// FUN_00428db0 / FUN_00428de0 — refcounted spaceflight ducking of the whole mix.
void PushClientSoundDucking();
void PopClientSoundDucking();
// Current global ducking attenuation in dB (retail `_DAT_00667ca4`).
float GetClientSoundDuckingDb();

// LAB_00428e00 / e30 / e60 / e90 / ec0 — the audio_* user-option callbacks. Each takes a
// 0..1 slider and stores 10*log2(x) dB (floored at -96) in the matching category master
// (DAT_00667c88 / c84 / c80 / c7c / c78). Category ← source type: sfx 0, interface 1,
// voice 2 and 5, music 3, ambient 4.
void SetClientAudioMusicVolume(float Linear);
void SetClientAudioAmbientVolume(float Linear);
void SetClientAudioSfxVolume(float Linear);
void SetClientAudioInterfaceVolume(float Linear);
void SetClientAudioVoiceVolume(float Linear);

// FUN_0042ac30 — the `sound_level_of_detail` option (DAT_00667ce4). Changing it re-resolves
// the `lod1=` / `lod2=` link of every archetype (FUN_0042bf90); a lod naming NONE unlinks the
// archetype entirely, so it stops playing.
void SetClientSoundLod(int Lod);
int GetClientSoundLod();

// FUN_004299e0 (ISoundSource slot 9) and the mixer score at +0x28 — exposed for tests.
GENRESULT GetClientSoundSourceVolumeDb(void* Source, float* OutDb);
float GetClientSoundSourceMixerScore(void* Source);

// Direct reads a caller makes on a source or archetype: the source's +0x18 archetype (what
// Start stored), the archetype's slot 12 (+0x30) lod link and slot 11 (+0x2c) random pitch, and
// its +0x04 nickname id. Caller: GameObject slot 109 (FUN_0053a4c0).
void* GetClientSoundSourceArch(void* Source);
void* GetClientSoundArchLinked(void* Arch);
float GetClientSoundArchRandomPitch(void* Arch);
unsigned GetClientSoundArchId(void* Arch);

// FUN_0042b5e0 (cdecl): create Arch's ISoundManager wav archetype. The name comes from Arch
// slot 1, the flags from slots 10/13 and the [AUDIO]/PITCH_BEND_SOUNDS options; ParentFs is
// the IFileSystem the name lives in (a voice profile's .utf), null for DALib::Data.
// Returns the SM archetype id or -1. Callers: SoundImpl Acquire (FUN_0042b840) and
// VoiceMsg::Acquire (FUN_00431220).
int CreateSoundArchWav(Archetype::ISound* Arch, void* ParentFs);
// ISoundManager slot 34 (+0x88) — used by VoiceMsg::Release (FUN_00431260).
void DestroyClientSoundWav(int SmArchId);
