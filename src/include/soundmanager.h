// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "dacom.h"
#include "x86math.h"            // FVector -- SoundManager's FVec3 was a third copy of it
#include "audio/dsound8.h"

// soundmanager.dll public ABI. Source: EXE/Disasm/soundmanager.dll, docs/re/audio.md.
// Object size 0x14C; IIDs are DACOM strings, not COM GUIDs. Everything private to the port
// (the DirectSound GUIDs, the instance/attached node layouts, the factory) stays in
// src/SoundManager/SoundManagerInternal.h.
//
// DALib holds the live manager in DALib::Sound and freelancer drives it from
// menu_sound_arch.cpp; until 2026-09-16 both did it by raw slot number
// (docs/re/cross_dll_protocol.md).

// The listener/source vectors this interface takes are plain 3-float records -- x86math.h's
// FVector, which SoundManager and Thorn each used to redeclare as FVec3.
typedef FVector FVec3;

struct ISoundManagerStatus : IDAComponent
{
    virtual GENRESULT DACOM_API GetInstanceStatus(int Index, void* Out) = 0; // +0x0C
};

struct ISoundArchetype : IDAComponent
{
    virtual GENRESULT DACOM_API GetFormat(void* Out) = 0;          // +0x0C LAB_06ee1530
    virtual GENRESULT DACOM_API GetSamples(void* Out) = 0;         // +0x10 stub FDUMP
    virtual unsigned long DACOM_API GetNumFrames() = 0;            // +0x14
    virtual float DACOM_API GetGain() = 0;                         // +0x18
    virtual unsigned long DACOM_API GetChannelCount() = 0;         // +0x1C
    virtual unsigned long DACOM_API GetDurationMs() = 0;           // +0x20
    virtual GENRESULT DACOM_API SetSamples(void* A, void* B) = 0;  // +0x24 stub FDUMP
    virtual GENRESULT DACOM_API SetGain(float Gain) = 0;           // +0x28
    virtual unsigned long DACOM_API IsReady() = 0;                 // +0x2C AL=1
    virtual GENRESULT DACOM_API GetLoopRange(unsigned long* Start,
                                             unsigned long* End) = 0; // +0x30
};

// ISoundSource -- the "play key". SoundManager never creates one: freelancer.exe hands it in
// through AttachPlayKey and the mixer then calls back into it every tick to ask where the
// sound is, how loud, how far, whether it is still alive. The port had no declaration for it
// at all -- SoundManager reached it through 44 hand-written `reinterpret_cast<Fn>(Vtable[N])`
// wrappers, and menu_sound_arch.cpp answered with a hand-built `static void* [42]` table that
// it memcpy'd onto two more tables to fake derived classes.
//
// Slot widths below are read off retail's own Source3D vtable, PTR_LAB_005c9efc in
// freelancer.exe. All three source vtables -- PTR_LAB_005c9fac (Sound::Source),
// PTR_LAB_005ca05c (Sound::StreamedSource) and PTR_LAB_005c9efc (Sound::Source3D) -- are
// exactly 0xAC bytes, i.e. 43 entries; the port's hand-built table had 42 and was one short.
//
// THE CALLING CONVENTION IS MIXED, exactly like IMaterial, and the split is clean: slots
// 3..21 read `this` off the stack (DACOM_API); slots 22..42 take it in ECX (__thiscall).
// Verified per slot against the listing -- 0x00429910 (slot 5) opens `MOV ECX,[ESP+4]`, while
// 0x004291c0 (slot 22) opens `PUSH ESI; MOV ESI,ECX` and returns `RET 0x8` for two arguments,
// and slot 39 at 0x00428810 is `MOV EAX,[ECX+0x60]; RET` -- a bare RET, which no explicit-this
// reading allows. Everything from slot 22 down is called only from inside freelancer.exe;
// SoundManager only ever touches 3..21, which is why the port got away with declaring the
// whole tail DACOM_API until 2026-09-16.
//
// THE BASE CLASS DOES NOT IMPLEMENT THE 3D SLOTS. In Sound::Source and Sound::StreamedSource
// slots 12 and 15..18 (position, cone angles, cone orientation, cone outside volume,
// velocity) are the shared "unsupported" stubs -- LAB_0044fce0 (`MOV EAX,0xFFFFFFF9; RET 0xC`)
// for the three-dword slot 15, LAB_00454190 (`MOV EAX,0xFFFFFFF9; RET 0x8`) for the rest.
// Only Sound::Source3D has real bodies. The mixer guards every one of those calls with
// Is3d(), so this is not a behavioural difference, but the port now mirrors it: FSoundSource
// answers "unsupported" and FSpatialSource overrides.
struct ISoundSource : IDAComponent
{
    // 3 +0x0C RET 0x4 -- FUN_004298b0. Refresh pass slot 4 runs first: on flag 0x4 without
    // 0x10 it asks DALib::Sound for the tick (slot 44) and restamps this+0x14. The mixer
    // never calls this one directly; it is slot 4's own first instruction.
    virtual unsigned long DACOM_API Slot3() = 0;
    // 4 +0x10 RET 0x4 -- FUN_004298f0: unless flag 0x10 is set, call slot 3, then return
    // this+0x14. That field is the tick the sound started on.
    virtual unsigned long DACOM_API GetStartTick() = 0;
    // 5 +0x14 RET 0x4 -- FUN_00429910: `this+0x2C != 0`.
    virtual unsigned char DACOM_API IsActive() = 0;
    // 6 +0x18 RET 0x4 -- LAB_0043d8e0. True only for the spatial (3D) source class; retail
    // answers it by which vtable the object carries, not by a flag.
    virtual unsigned char DACOM_API Is3d() = 0;
    // 7 +0x1C RET 0x4 -- LAB_00429930: bit 0 of the flag byte at this+0x0C.
    virtual unsigned char DACOM_API IsLooping() = 0;
    // 8 +0x20 -- LAB_00429940 is a three-instruction thunk with no RET of its own:
    // `MOV ECX,[ECX+0x18]; MOV EAX,[ECX]; JMP [EAX]` -- forward to the archetype at +0x18,
    // slot 0. Arity is therefore whatever the caller pushes, and every caller pushes nothing
    // but the object.
    virtual int DACOM_API GetArchId() = 0;
    // 9 +0x24 RET 0x8 -- FUN_004299e0, the per-source attenuation in dB.
    virtual GENRESULT DACOM_API GetAttenuation(float* Out) = 0;
    // 10 +0x28 RET 0x8 -- FUN_00429b80.
    virtual GENRESULT DACOM_API GetPitch(float* Out) = 0;
    // 11 +0x2C RET 0x8 -- LAB_00429bd0: `ftol(this+0x20)` into the out-param. The mixer
    // clamps it to +/-10000 and hands it to IDirectSoundBuffer::SetPan, so it is a pan.
    virtual GENRESULT DACOM_API GetPan(long* Out) = 0;
    // 12 +0x30 RET 0x8 -- LAB_00429d30: world position, resolved through DALib::Engine when
    // the source is attached (this+0x60 != -1).
    virtual GENRESULT DACOM_API GetPosition(FVec3* Out) = 0;
    // 13 +0x34 RET 0x8 -- LAB_00429bf0. The mixer defaults it to 10.0f before asking.
    virtual GENRESULT DACOM_API GetMinDistance(float* Out) = 0;
    // 14 +0x38 RET 0x8 -- FUN_00429c20. Default 500.0f; squared into the mixer's range cull.
    virtual GENRESULT DACOM_API GetMaxDistance(float* Out) = 0;
    // 15 +0x3C RET 0xC -- FUN_00429ec0, the two DS3D cone angles.
    virtual GENRESULT DACOM_API GetConeAngles(unsigned long* Inside,
                                              unsigned long* Outside) = 0;
    // 16 +0x40 RET 0x8 -- LAB_00429f60.
    virtual GENRESULT DACOM_API GetConeOrientation(FVec3* Out) = 0;
    // 17 +0x44 RET 0x8 -- LAB_00429f20.
    virtual GENRESULT DACOM_API GetConeOutsideVolume(float* Out) = 0;
    // 18 +0x48 RET 0x8 -- LAB_00429de0.
    virtual GENRESULT DACOM_API GetVelocity(FVec3* Out) = 0;
    // 19 +0x4C RET 0x8 -- the mixer forwards the answer straight into EAX 2.0 listener
    // property 1 via IKsPropertySet, so it is an environment id.
    virtual GENRESULT DACOM_API GetEaxEnvironment(unsigned long* Out) = 0;
    // 20 +0x50 RET 0x8 -- 0 => DS3DMODE_NORMAL, 1 => DS3DMODE_HEADRELATIVE, anything else is
    // a warning and NORMAL.
    virtual GENRESULT DACOM_API GetSoundMode(unsigned long* Out) = 0;
    // 21 +0x54 RET 0x8 -- the SoundManager mixer (FUN_06ee64c0) maps 0 => dwApply 1
    // (DS3D_DEFERRED, committed once per tick by CommitDeferredSettings at 06ee524e) and
    // 1 => dwApply 0 (DS3D_IMMEDIATE); anything else is a warning and 1.
    virtual GENRESULT DACOM_API GetApplyMode(unsigned long* Out) = 0;
    // Slots 19/20/21 all share one retail implementation, LAB_00454190:
    // `MOV EAX,0xFFFFFFF9; RET 0x8` -- "unsupported", one argument. The port used to install a
    // zero-argument stub here, which is what made these three a live stack bug (see below).

    // 22..25 +0x58..+0x64: start / alternate start / stop / offset. All __thiscall.
    // 22 FUN_004291c0 RET 0x8 -- `PUSH ESI; MOV ESI,ECX`, two stack arguments.
    virtual unsigned char __thiscall Start(void* Arch, int Extra) = 0;
    // 23 FUN_0053f330 RET 0x4 -- `XOR AL,AL; RET 0x4`: one argument, always fails.
    virtual unsigned char __thiscall AltStart(void* Arch) = 0;
    // 24 FUN_004293e0 bare RET -- NO arguments. The port declared `Stop(int)` and threw the
    // argument away, which is four bytes wider than retail.
    virtual void __thiscall Stop() = 0;
    // 25 FUN_004293b0 RET 0x4 -- stores Offset, or clock - Offset when flag 0x10 is set.
    virtual void __thiscall SetStartOffset(int Offset) = 0;

    // 26..35 +0x68..+0x8C: all __thiscall; unnamed entries retain listing-derived shapes.
    // 26 LAB_004296b0: stores the pause tick, then stops the instance buffer.
    virtual void __thiscall Pause() = 0;
    // 27 LAB_004296e0: adjusts a latched start tick, plays the buffer, then clears the pause tick.
    virtual void __thiscall Resume() = 0;
    // 28 LAB_004287e0: pause timestamp != 0; pausing at clock zero still reports false.
    virtual unsigned long __thiscall IsPaused() = 0;
    // 29 FUN_00429840 bare RET, 0 args. an earlier note read this one as
    // *is_finished*: it returns false when the looping bit at +0x0C is set, which squares with
    // SoundManager handing looping buffers DSBPLAY_LOOPING. Slot 42 calls it.
    virtual unsigned char __thiscall IsFinished() = 0;
    // 30 LAB_00429730 bare RET, 0 args -- sets up an SEH frame, so it owns a C++ object.
    virtual unsigned long __thiscall Slot30() = 0;
    // 31 LAB_00429450 RET 0x4: write mixer score, preserving x87 rounding and unordered branches.
    virtual void __thiscall ComputeMixerScore(const float* Listener) = 0;
    // 32 LAB_00429330: clamps source attenuation to [-96, 0] dB, preserving NaN bits.
    virtual void __thiscall SetAttenuation(float Attenuation) = 0;
    // 33 LAB_00429370 RET 0x4 -- clamps pan to [-10000, 10000]; NaN is stored unchanged.
    virtual void __thiscall SetPan(float Pan) = 0;
    // 34 FUN_004287f0: replaces flag bit 0 with the argument's low bit.
    virtual void __thiscall SetLooping(unsigned char bLooping) = 0;
    // 35 LAB_00429ca0 RET 0x4, one argument. In the two non-3D vtables this slot is folded
    // onto IDPMsgHandler::OnAddHost (`RET 0x4`), an unrelated do-nothing of the same shape.
    virtual unsigned long __thiscall Slot35(void* Arg) = 0;

    // 36..39: __thiscall, object in ECX -- see the header note above.
    // 36 +0x90 RET 0x4 -- LAB_00429dc0 copies three floats into this+0x48.
    virtual void __thiscall SetVelocity(const float* Velocity) = 0;
    // 37 +0x94 RET 0x8 -- FUN_00429e70: this+0x60 = handle, then three floats from Pose+0x24
    // into this+0x30.
    virtual void __thiscall SetPose(int AttachHandle, const float* Transform) = 0;
    // 38 +0x98 RET 0x4 -- LAB_004299a0 clamps the pitch into +/-100.0f at this+0x10.
    virtual void __thiscall SetPitch(float Pitch) = 0;
    // 39 +0x9C bare RET -- LAB_00428810 is `MOV EAX,[ECX+0x60]; RET`. Zero arguments, and the
    // bare RET is the proof: an explicit this would have to pop 4.
    virtual int __thiscall GetAttachHandle() = 0;

    // 40 +0xA0 FUN_0044fd00 -- `XOR AL,AL; RET`. Bare RET, no arguments, always false; the
    // same folded stub sits in 20 other vtables.
    virtual unsigned char __thiscall Slot40() = 0;
    // 41 +0xA4 FUN_00428820 RET 0x4 -- MSVC's scalar deleting destructor (the base class uses
    // its own, FUN_004290a0, which reinstalls PTR_LAB_005c9fac first).
    virtual void* __thiscall ScalarDeletingDestructor(unsigned char bDelete) = 0;
    // 42 +0xA8 RET 0x8 -- two arguments, and the second is a float. Source3D's LAB_0042a020
    // integrates the velocity triple at +0x48 into the position at +0x30 by that delta, then
    // calls its own slot 31 with the first argument and slot 29 (IsFinished), returning the
    // logical NOT of that. The base class body FUN_00429690 is the same minus the integration.
    // The port's hand-built table stopped at 41 and never had this slot at all.
    virtual unsigned char __thiscall Update(const float* Listener, float DeltaSeconds) = 0;
};

// ISoundListenerPose -- the camera pose ISoundManager::MixerTick takes. FUN_06ee6e20 copies
// orientation, position, velocity and the three DirectSound scale factors out of it into the
// manager's own listener state. Every getter answers >= 0 when it has a value, and the mixer
// keeps its current setting otherwise.
//
// NOT IMPLEMENTED ANYWHERE IN THE PORT YET: both MixerTick call sites in
// menu_sound_arch.cpp pass nullptr, so the listener never moves. Declaring the interface is
// what makes that gap visible instead of hiding it behind a void*.
struct ISoundListenerPose : IDAComponent
{
    virtual int DACOM_API GetOrientation(FVec3* OutFront, FVec3* OutUp) = 0; // +0x0C
    virtual int DACOM_API GetPosition(FVec3* Out) = 0;                       // +0x10
    virtual int DACOM_API GetVelocity(FVec3* Out) = 0;                       // +0x14
    virtual int DACOM_API GetDistanceFactor(float* Out) = 0;                 // +0x18
    virtual int DACOM_API GetDopplerFactor(float* Out) = 0;                  // +0x1C
    virtual int DACOM_API GetRolloffFactor(float* Out) = 0;                  // +0x20
};

// 56 stdcall slots unless noted. Unnamed slots keep documented RET sizes; do not invent names.
struct ISoundManager : IDAComponent
{
    virtual GENRESULT DACOM_API SetPcmFormat(unsigned short* Fmt) = 0;                 // 3  +0x0C
    virtual GENRESULT DACOM_API GetPcmFormat(unsigned short* Fmt) = 0;                 // 4
    virtual GENRESULT DACOM_API SetMasterGain(float Gain) = 0;                         // 5
    virtual GENRESULT DACOM_API SetMaxChannels(unsigned long Count) = 0;               // 6
    virtual GENRESULT DACOM_API SetSpeakerConfig(unsigned long Index) = 0;             // 7
    virtual float DACOM_API GetMasterGain() = 0;                                       // 8
    virtual unsigned long DACOM_API GetMaxChannels() = 0;                              // 9
    virtual unsigned long DACOM_API GetSpeakerConfig() = 0;                            // 10
    virtual GENRESULT DACOM_API SetEaxPreview(int Env, float Vol, float Decay,
                                              float Damp) = 0;                         // 11
    virtual GENRESULT DACOM_API GetEaxPreview(unsigned long* Env, unsigned long* Vol,
                                              unsigned long* Decay,
                                              unsigned long* Damp) = 0;                // 12
    virtual GENRESULT DACOM_API SetOrientation(FVector* Front, FVector* Up) = 0;           // 13
    // 14: Front = column 2 (m[2], m[5], m[8]), Up = column 1 (m[1], m[4], m[7]) - 06ee2380.
    virtual GENRESULT DACOM_API SetOrientationFromMatrix(float* Matrix) = 0;           // 14
    virtual GENRESULT DACOM_API SetListenerPosition(FVector* Pos) = 0;                   // 15
    virtual GENRESULT DACOM_API SetListenerVelocity(FVector* Vel) = 0;                   // 16
    virtual GENRESULT DACOM_API SetDistanceFactor(float Value) = 0;                    // 17
    virtual GENRESULT DACOM_API SetDopplerFactor(float Value) = 0;                     // 18
    virtual GENRESULT DACOM_API SetRolloffFactor(float Value) = 0;                     // 19
    // 20..23 return no status: retail leaves the (last) out pointer in EAX (06ee2730, 25f0, 27c0,
    // 2810). Slot 20 writes (front, up) as slot 13 took them (stored +0xA8 negated back); slot 21
    // writes columns (right, up, front), the inverse of slot 14.
    virtual GENRESULT DACOM_API GetOrientationSpace(FVector* Pos, FVector* Vel) = 0;       // 20
    virtual GENRESULT DACOM_API GetOrientationMatrix(float* Out9) = 0;                 // 21
    virtual GENRESULT DACOM_API GetListenerPosition(FVector* Out) = 0;                   // 22
    virtual GENRESULT DACOM_API GetListenerVelocity(FVector* Out) = 0;                   // 23
    virtual float DACOM_API GetDistanceFactor() = 0;                                   // 24
    virtual float DACOM_API GetDopplerFactor() = 0;                                    // 25
    virtual float DACOM_API GetRolloffFactor() = 0;                                    // 26
    virtual GENRESULT DACOM_API EaxGetInstance(void* Id, GUID* Set, unsigned long Prop,
                                               void* Buf, unsigned long Cb,
                                               unsigned long* Pcb) = 0;                // 27 RET 0x1C
    virtual GENRESULT DACOM_API EaxSetInstance(void* Id, GUID* Set, unsigned long Prop,
                                               void* Data, unsigned long Cb) = 0;      // 28 RET 0x18
    virtual GENRESULT DACOM_API EaxGetPrimary(GUID* Set, unsigned long Prop, void* Buf,
                                              unsigned long Cb, unsigned long* Pcb) = 0; // 29
    virtual GENRESULT DACOM_API EaxSetPrimary(GUID* Set, unsigned long Prop, void* Data,
                                              unsigned long Cb) = 0;                   // 30
    virtual int DACOM_API CreateArchetypePcm(void* Fmt, unsigned long ByteCount, unsigned long LoopStart,
                                             unsigned long LoopEnd, void* Samples,
                                             unsigned long ArchFlags) = 0;             // 31 +0x7C RET 0x1C
    // 32 +0x80 RET 0xC. LAB_06ee2dc0: Source is a plain PCM descriptor, not a COM object --
    // format 8 bytes at +0, loop start +0x8, loop end +0xC, byte count +0x14, samples +0x18,
    // fed to the same FUN_06ee1330 fill as slot 31.
    virtual int DACOM_API CreateArchetypeFromSource(void* Source, unsigned long ArchFlags) = 0;
    // 33 +0x84 RET 0xC. Retail 0042b72f pushes the caller's IFileSystem, not a path.
    virtual int DACOM_API CreateArchetypeFromWav(IDAComponent* FileSystem,
                                                 unsigned long ArchFlags) = 0;
    virtual GENRESULT DACOM_API DestroyArchetype(int Id) = 0;                          // 34 +0x88 RET 0x8
    virtual GENRESULT DACOM_API QueryArchetype(int Id, void** Out) = 0;                // 35 +0x8C RET 0xC
    virtual GENRESULT DACOM_API AttachPlayKey(ISoundSource* PlayKey,
                                              unsigned long Index) = 0;                // 36 +0x90 RET 0xC
    virtual GENRESULT DACOM_API DetachPlayKey(ISoundSource* PlayKey) = 0;              // 37 +0x94 RET 0x8
    virtual GENRESULT DACOM_API ReplaceAttachedList(void** Keys, unsigned long Count) = 0; // 38 +0x98 RET 0xC
    virtual unsigned long DACOM_API GetAttachedCount() = 0;                            // 39 RET 0x4
    virtual unsigned long DACOM_API GetPlayingCount() = 0;                             // 40 RET 0x4
    virtual GENRESULT DACOM_API StartupDevice(HWND Hwnd, unsigned long Flags) = 0;     // 41 +0xA4
    virtual GENRESULT DACOM_API ShutdownDevice() = 0;                                  // 42 +0xA8
    virtual GENRESULT DACOM_API GetDirectSound(IDirectSound** Out) = 0;                // 43 +0xAC
    virtual unsigned long DACOM_API GetTickCountSlot() = 0;                            // 44 +0xB0
    virtual GENRESULT GetDeviceSnapshot(void* Out) = 0;                                // 45 +0xB4 thiscall
    virtual GENRESULT DACOM_API MixerTick(ISoundListenerPose* Listener) = 0;           // 46
    virtual GENRESULT DACOM_API StopInstanceBuffer(ISoundSource* PlayKey) = 0;       // 47
    virtual GENRESULT DACOM_API PlayInstanceBuffer(ISoundSource* PlayKey) = 0;         // 48
    virtual GENRESULT DACOM_API GetArchetypeFormat(void* Id, void* Out) = 0;           // 49
    virtual GENRESULT DACOM_API GetArchetypeSamples(void* Id, void* Out) = 0;          // 50
    virtual unsigned long DACOM_API GetArchetypeFrames(void* Id) = 0;                  // 51
    virtual float DACOM_API GetArchetypeGain(void* Id) = 0;                            // 52
    virtual unsigned long DACOM_API GetArchetypeChannels(void* Id) = 0;                // 53
    virtual GENRESULT DACOM_API SetArchetypeSamples(void* A, void* B, void* C) = 0;    // 54
    virtual unsigned long DACOM_API GetArchetypeReady(void* Id) = 0;                   // 55
};
