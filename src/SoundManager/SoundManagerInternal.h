// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "dacom.h"
#include "soundmanager.h"
#include "luaprofile.h"
#include "audio/dsound8.h"
#include <mmreg.h>
#include <stddef.h>

// SoundManager.dll internals. The ABI other modules see is src/include/soundmanager.h.

static constexpr FlULong kAggDescSize = 0x14;
static constexpr FlULong kRegisterPriority = 0x40000000;

static constexpr char kComponentName[] = "SoundManager";

static constexpr unsigned int kFdumpError = 0x100001;
static constexpr unsigned int kFdumpWarning = 0x100002;
static constexpr unsigned int kFdumpNotice = 0x100003;

// s_C:\work\... at .data:06eea070 (21 FDUMP sites in SoundMan.cpp code).
static constexpr char kRetailSource[] =
    "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\SoundManager\\SoundMan.cpp";
// .data:06eeadd4 - the statically linked WavLib (FDUMP sites 06ee867d, 8812, 883f, 88f8).
static constexpr char kWavLibSource[] =
    "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\WavLib\\wavlib.cpp";
static constexpr char kFdumpErrorFmt[] = "%s(%d) : ERROR:General:%s";
static constexpr char kFdumpWarningFmt[] = "%s(%d) : WARNING:General:%s";
static constexpr char kFdumpNoticeFmt[] = "%s(%d) : NOTICE:General:%s";

// Retail DAT_06ee91cc / 06ee91bc / 06ee91ac — last word E5 60, not SDK A7 70.
static const GUID kClsidDirectSound = {
    0x47D4D946, 0x62E8, 0x11CF, {0x93, 0xBC, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
static const GUID kIidDirectSoundRetail = {
    0x279AFA83, 0x4981, 0x11CE, {0xA5, 0x21, 0x00, 0x20, 0xAF, 0x0B, 0xE5, 0x60}};
static const GUID kIidDirectSound3DListenerRetail = {
    0x279AFA84, 0x4981, 0x11CE, {0xA5, 0x21, 0x00, 0x20, 0xAF, 0x0B, 0xE5, 0x60}};
static const GUID kIidDirectSound3DBufferRetail = {
    0x279AFA86, 0x4981, 0x11CE, {0xA5, 0x21, 0x00, 0x20, 0xAF, 0x0B, 0xE5, 0x60}};
// DAT_06ee918c — IID_IDirectSoundNotify (FUN_06ee1a40 QI).
static const GUID kIidDirectSoundNotify = {
    0xB0210783, 0x89CD, 0x11D0, {0xAF, 0x08, 0x00, 0xA0, 0xC9, 0x25, 0xCD, 0x16}};

// IKsPropertySet — SDK tail differs; QI fail only disables EAX.
static const GUID kIidKsPropertySet = {
    0x31EFAC30, 0x515C, 0x11D0, {0xA9, 0xAA, 0x00, 0xAA, 0x00, 0x61, 0xBE, 0x93}};
static const GUID kDsPropSetEax20Listener = {
    0x4A4E6FC0, 0xC341, 0x11D1, {0xB7, 0x3A, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
static const GUID kDsPropSetEax20Buffer = {
    0x4A4E6FC1, 0xC341, 0x11D1, {0xB7, 0x3A, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};

// This factory takes dacom.h's FAGGDESC14, as SystemContainer children do.

// Nested vtbl @ object +0x08 — not in the aggregator QI table. Slot 3 binds IProfileParser.
struct ISoundManagerParserBind : IDAComponent
{
    virtual GENRESULT DACOM_API BindParser() = 0; // +0x0C LAB_06ee56f0
};

class FSoundArchetype final : public ISoundArchetype
{
public:
    FSoundArchetype();
    ~FSoundArchetype();

    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
    FlULong DACOM_API AddRef() override;
    FlULong DACOM_API Release() override;
    GENRESULT DACOM_API GetFormat(void* Out) override;
    GENRESULT DACOM_API GetSamples(void* Out) override;
    FlULong DACOM_API GetNumFrames() override;
    float DACOM_API GetGain() override;
    FlULong DACOM_API GetChannelCount() override;
    FlULong DACOM_API GetDurationMs() override;
    GENRESULT DACOM_API SetSamples(void* A, void* B) override;
    GENRESULT DACOM_API SetGain(float InGain) override;
    FlULong DACOM_API IsReady() override;
    GENRESULT DACOM_API GetLoopRange(FlULong* Start, FlULong* End) override;

    GENRESULT FillFromPcm(const unsigned short* Fmt8, FlULong ByteCount,
                          FlULong LoopStart, FlULong LoopEnd, const void* Samples,
                          IDirectSound* DirectSound, FlULong InFlags);
    // Retail FUN_06ee1240: the caller opens the file as an IFileSystem and this reads the
    // RIFF straight out of its mapping (WavLib FUN_06ee8960). No path is involved.
    GENRESULT FillFromWav(IDAComponent* FileSystem, IDirectSound* DirectSound,
                          FlULong InFlags);
    GENRESULT DuplicateBuffer(IDirectSound* DirectSound, IDirectSoundBuffer** Out) const;

    unsigned short nChannels;      // +0x04
    unsigned short Field06;        // +0x06
    unsigned short nSamplesPerSec; // +0x08
    unsigned short nBlockAlign;    // +0x0A
    FlULong LoopStart;       // +0x0C
    FlULong LoopEnd;         // +0x10
    FlULong NumFrames;       // +0x14
    float Gain;                    // +0x18
    FlULong DurationMs;      // +0x1C
    FlULong Flags;           // +0x20
    IDirectSoundBuffer* Buffer;    // +0x24
    unsigned char bSamplesReady;   // +0x28
    unsigned char Pad29[3];
    FlULong RefCount;        // +0x2C
};

struct FArchMapEntry
{
    int Id;
    FSoundArchetype* Archetype;
};

// Instance list node 0x60 (FUN_06ee7d50). Payload starts at +8.
struct FSoundInstanceNode
{
    FSoundInstanceNode* Next;      // +0x00
    FSoundInstanceNode* Prev;      // +0x04
    ISoundSource* PlayKey;         // +0x08
    IDirectSoundBuffer* Buffer;    // +0x0C
    IDirectSound3DBuffer* Buffer3d;// +0x10
    HANDLE NotifyEvent;            // +0x14 payload+0x0C (FUN_06ee1a40)
    FlULong Pad18;           // +0x18 payload+0x10
    FlULong FeatureFlags;    // +0x1C payload+0x14 notify offset / loop-end bytes
    FlULong CapsFlags;       // +0x20 → Status +8 via node+0x28
    FlULong StatusFlags;     // +0x24
    FlULong DsCapsFlags;     // +0x28
    float DistSq;                  // +0x2C payload +0x24
    float MaxDistance;             // +0x30
    float MaxDistSq;               // +0x34
    float MinDistance;             // +0x38
    float MinDistSq;               // +0x3C
    FVec3 Position;                // +0x40
    FVec3 Direction;               // +0x4C listener→source (payload +0x44)
    unsigned char bIs3dCache;      // +0x58 payload+0x50
    unsigned char Pad59[3];
    FlULong LastStartTick;   // +0x5C payload+0x54
};

struct FAttachedNode
{
    FAttachedNode* Next;
    FAttachedNode* Prev;
    ISoundSource* PlayKey;
};

class FSoundManager;

class FSoundInner final : public IDAComponent
{
public:
    FSoundInner();

    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
    FlULong DACOM_API AddRef() override;
    FlULong DACOM_API Release() override;

    FlULong RefCount;
    FSoundManager* Self;
};

class FSoundManagerFactory final : public IComponentFactory
{
public:
    FSoundManagerFactory();

    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
    FlULong DACOM_API AddRef() override;
    FlULong DACOM_API Release() override;
    GENRESULT DACOM_API CreateInstance(AGGDESC* Desc, void** Instance) override;

    FlULong RefCount;
    const char* Name;
};

class FSoundManager final : public ISoundManager,
                            public ISoundManagerStatus,
                            public ISoundManagerParserBind
{
public:
    FSoundManager();
    ~FSoundManager();

    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
    FlULong DACOM_API AddRef() override;
    FlULong DACOM_API Release() override;

    GENRESULT DACOM_API SetPcmFormat(unsigned short* Fmt) override;
    GENRESULT DACOM_API GetPcmFormat(unsigned short* Fmt) override;
    GENRESULT DACOM_API SetMasterGain(float Gain) override;
    GENRESULT DACOM_API SetMaxChannels(FlULong Count) override;
    GENRESULT DACOM_API SetSpeakerConfig(FlULong Index) override;
    float DACOM_API GetMasterGain() override;
    FlULong DACOM_API GetMaxChannels() override;
    FlULong DACOM_API GetSpeakerConfig() override;
    GENRESULT DACOM_API SetEaxPreview(int Env, float Vol, float Decay, float Damp) override;
    GENRESULT DACOM_API GetEaxPreview(FlULong* Env, FlULong* Vol, FlULong* Decay,
                                      FlULong* Damp) override;
    GENRESULT DACOM_API SetOrientation(FVec3* Front, FVec3* Up) override;
    GENRESULT DACOM_API SetOrientationFromMatrix(float* Matrix) override;
    GENRESULT DACOM_API SetListenerPosition(FVec3* Pos) override;
    GENRESULT DACOM_API SetListenerVelocity(FVec3* Vel) override;
    GENRESULT DACOM_API SetDistanceFactor(float Value) override;
    GENRESULT DACOM_API SetDopplerFactor(float Value) override;
    GENRESULT DACOM_API SetRolloffFactor(float Value) override;
    GENRESULT DACOM_API GetOrientationSpace(FVec3* Pos, FVec3* Vel) override;
    GENRESULT DACOM_API GetOrientationMatrix(float* Out9) override;
    GENRESULT DACOM_API GetListenerPosition(FVec3* Out) override;
    GENRESULT DACOM_API GetListenerVelocity(FVec3* Out) override;
    float DACOM_API GetDistanceFactor() override;
    float DACOM_API GetDopplerFactor() override;
    float DACOM_API GetRolloffFactor() override;
    GENRESULT DACOM_API EaxGetInstance(void* Id, GUID* Set, FlULong Prop, void* Buf,
                                       FlULong Cb, FlULong* Pcb) override;
    GENRESULT DACOM_API EaxSetInstance(void* Id, GUID* Set, FlULong Prop, void* Data,
                                       FlULong Cb) override;
    GENRESULT DACOM_API EaxGetPrimary(GUID* Set, FlULong Prop, void* Buf, FlULong Cb,
                                      FlULong* Pcb) override;
    GENRESULT DACOM_API EaxSetPrimary(GUID* Set, FlULong Prop, void* Data,
                                      FlULong Cb) override;
    int DACOM_API CreateArchetypePcm(void* Fmt, FlULong ByteCount, FlULong LoopStart,
                                     FlULong LoopEnd, void* Samples,
                                     FlULong ArchFlags) override;
    int DACOM_API CreateArchetypeFromSource(void* Source, FlULong ArchFlags) override;
    int DACOM_API CreateArchetypeFromWav(IDAComponent* FileSystem, FlULong ArchFlags) override;
    GENRESULT DACOM_API DestroyArchetype(int Id) override;
    GENRESULT DACOM_API QueryArchetype(int Id, void** Out) override;
    GENRESULT DACOM_API AttachPlayKey(ISoundSource* PlayKey, FlULong Index) override;
    GENRESULT DACOM_API DetachPlayKey(ISoundSource* PlayKey) override;
    GENRESULT DACOM_API ReplaceAttachedList(void** Keys, FlULong Count) override;
    FlULong DACOM_API GetAttachedCount() override;
    FlULong DACOM_API GetPlayingCount() override;
    GENRESULT DACOM_API StartupDevice(HWND Hwnd, FlULong Flags) override;
    GENRESULT DACOM_API ShutdownDevice() override;
    GENRESULT DACOM_API GetDirectSound(IDirectSound** Out) override;
    FlULong DACOM_API GetTickCountSlot() override;
    GENRESULT GetDeviceSnapshot(void* Out) override;
    GENRESULT DACOM_API MixerTick(ISoundListenerPose* ListenerPose) override;
    GENRESULT DACOM_API StopInstanceBuffer(ISoundSource* PlayKey) override;
    GENRESULT DACOM_API PlayInstanceBuffer(ISoundSource* PlayKey) override;
    GENRESULT DACOM_API GetArchetypeFormat(void* Id, void* Out) override;
    GENRESULT DACOM_API GetArchetypeSamples(void* Id, void* Out) override;
    FlULong DACOM_API GetArchetypeFrames(void* Id) override;
    float DACOM_API GetArchetypeGain(void* Id) override;
    FlULong DACOM_API GetArchetypeChannels(void* Id) override;
    GENRESULT DACOM_API SetArchetypeSamples(void* A, void* B, void* C) override;
    FlULong DACOM_API GetArchetypeReady(void* Id) override;

    GENRESULT DACOM_API GetInstanceStatus(int Index, void* Out) override;
    GENRESULT DACOM_API BindParser() override;

    void BindOuter(FAGGDESC14* Desc);
    GENRESULT CreateDirectSound(); // FUN_06ee5730
    void DumpDeviceNotices();      // FUN_06ee5b20
    void ApplyListener();          // FUN_06ee6d50
    void ReleaseCom();
    FSoundArchetype* LookupArchetype(int Id);
    int InsertArchetype(FSoundArchetype* Archetype);
    void DestroyAllArchetypes();
    void StopInstancesForArch(int ArchId); // FUN_06ee4740
    void ClearAttached();
    FSoundInstanceNode* FindInstanceByKey(ISoundSource* PlayKey);
    FSoundInstanceNode* CreateInstanceFromKey(ISoundSource* PlayKey);
    FAttachedNode* FindAttached(ISoundSource* PlayKey);
    void UnlinkInstance(FSoundInstanceNode* Node);
    unsigned char MixerApplyInstance(FSoundInstanceNode* Node);  // FUN_06ee64c0
    void MixerApplyEax(FSoundInstanceNode* Node);
    void RefreshInstancePose(FSoundInstanceNode* Node);
    unsigned char MixerAllowCreate(ISoundSource* PlayKey);
    void LinkPendingInstance(FSoundInstanceNode* Node);          // FUN_06ee7c00
    void SplicePendingInstances();                               // FUN_06ee7c70
    unsigned char MixerSetStartOffset(FSoundInstanceNode* Node); // FUN_06ee6b70
    void ApplyListenerFromSource(ISoundListenerPose* Pose);      // FUN_06ee6e20
    unsigned char OneShotAlreadyExpired(ISoundSource* PlayKey);  // FUN_06ee6070
    unsigned char SetupInstanceNotify(FSoundInstanceNode* Node); // FUN_06ee1a40

    unsigned char bParserBound;     // +0x0C
    char DeviceIdString[0x28];      // +0x0D
    GUID DeviceGuid;                // +0x38
    float MasterGain;               // +0x48
    FlULong Field4C;
    FlULong Field50;
    FlULong MaxChannels;      // +0x54
    int EaxEnv;                     // +0x58
    float EaxVol;                   // +0x5C
    float EaxDecay;                 // +0x60
    float EaxDamp;                  // +0x64
    FlULong Field68;
    FlULong Field6C;
    FlULong Field70;
    FlULong Field74;
    float ReverbMix;                // +0x78
    FlULong Field7C;          // +0x7C DS3DALG GUID dword0 (ctor DAT_06ee95e4 = 0)
    FlULong Field80;          // +0x80 DAT_06ee95e8
    FlULong Field84;          // +0x84 DAT_06ee95ec
    FlULong Field88;          // +0x88 DAT_06ee95f0
    FlULong Flags;            // +0x8C
    FlULong CapsFlags;        // +0x90
    FlULong Field94;
    FlULong SampleRate;       // +0x98
    FlULong Channels;         // +0x9C
    FlULong BitsPerSample;    // +0xA0
    int CoordScale;                 // +0xA4
    FVec3 Front;                    // +0xA8
    FVec3 Up;                       // +0xB4
    FlULong PadC0[3];         // +0xC0 ctor zeros
    FVec3 Position;                 // +0xCC
    FVec3 Velocity;                 // +0xD8
    float DistanceFactor;           // +0xE4
    float DopplerFactor;            // +0xE8
    float RolloffFactor;            // +0xEC
    float Max3dPan;                 // +0xF0
    HWND Window;                    // +0xF4 not written by FUN_06ee1b40
    FArchMapEntry* ArchEntries;     // +0xF8 retail FUN_06ee7620 std::map
    FlULong ArchCount;        // +0xFC
    FlULong ArchCap;          // +0x100
    unsigned char bArchEmpty;       // +0x104
    unsigned char Pad105[3];
    FlULong Pad108;
    FAttachedNode* AttachedUnused;  // +0x10C list this (FUN_06ee7d80 size 0xC)
    FAttachedNode* AttachedSentinel;// +0x110
    FlULong AttachedCount;    // +0x114
    FSoundInstanceNode* InstanceUnused;   // +0x118 list this (FUN_06ee7d50 size 0x60)
    FSoundInstanceNode* InstanceSentinel; // +0x11C
    FlULong InstanceEmbeddedCount;  // +0x120 (play count is DAT_06eeaf08)
    FlULong NextArchId;       // +0x124
    IDirectSound3DListener* Listener; // +0x128
    IDirectSound* DirectSound;      // +0x12C
    IDirectSoundBuffer* Primary;    // +0x130
    IProfileParser* Parser;         // +0x134
    unsigned char bCoInitialized;   // +0x138
    unsigned char Pad139[3];
    IDAComponent* Controlling;      // +0x13C
    FSoundInner Inner;              // +0x140
};

FL_LAYOUT_ASSERT(sizeof(FSoundArchetype) == 0x30, "archetype is 0x30");
FL_LAYOUT_ASSERT(offsetof(FSoundArchetype, nChannels) == 0x04, "arch nChannels +4");
FL_LAYOUT_ASSERT(offsetof(FSoundArchetype, Buffer) == 0x24, "arch Buffer +0x24");
FL_LAYOUT_ASSERT(offsetof(FSoundArchetype, RefCount) == 0x2C, "arch ref +0x2C");
FL_LAYOUT_ASSERT(sizeof(FSoundInstanceNode) == 0x60, "instance node is 0x60");
FL_LAYOUT_ASSERT(offsetof(FSoundInstanceNode, DistSq) == 0x2C, "DistSq +0x2C");
FL_LAYOUT_ASSERT(offsetof(FSoundInstanceNode, MaxDistance) == 0x30, "MaxDistance +0x30");
FL_LAYOUT_ASSERT(offsetof(FSoundInstanceNode, MinDistance) == 0x38, "MinDistance +0x38");
FL_LAYOUT_ASSERT(offsetof(FSoundInstanceNode, Position) == 0x40, "Position +0x40");
FL_LAYOUT_ASSERT(offsetof(FSoundInstanceNode, Direction) == 0x4C, "Direction +0x4C");
FL_LAYOUT_ASSERT(sizeof(FSoundManagerFactory) == 0x0C, "factory is 0xC");
FL_LAYOUT_ASSERT(offsetof(FSoundManager, bParserBound) == 0x0C, "bParserBound +0x0C");
FL_LAYOUT_ASSERT(offsetof(FSoundManager, DeviceGuid) == 0x38, "DeviceGuid +0x38");
FL_LAYOUT_ASSERT(offsetof(FSoundManager, MasterGain) == 0x48, "MasterGain +0x48");
FL_LAYOUT_ASSERT(offsetof(FSoundManager, MaxChannels) == 0x54, "MaxChannels +0x54");
FL_LAYOUT_ASSERT(offsetof(FSoundManager, Field7C) == 0x7C, "DS3DALG +0x7C");
FL_LAYOUT_ASSERT(offsetof(FSoundManager, Flags) == 0x8C, "Flags +0x8C");
FL_LAYOUT_ASSERT(offsetof(FSoundManager, SampleRate) == 0x98, "SampleRate +0x98");
FL_LAYOUT_ASSERT(offsetof(FSoundManager, Front) == 0xA8, "Front +0xA8");
FL_LAYOUT_ASSERT(offsetof(FSoundManager, Up) == 0xB4, "Up +0xB4");
FL_LAYOUT_ASSERT(offsetof(FSoundManager, Position) == 0xCC, "Position +0xCC");
FL_LAYOUT_ASSERT(offsetof(FSoundManager, Window) == 0xF4, "Window +0xF4");
FL_LAYOUT_ASSERT(offsetof(FSoundManager, AttachedUnused) == 0x10C, "attached list +0x10C");
FL_LAYOUT_ASSERT(offsetof(FSoundManager, InstanceUnused) == 0x118, "instance list +0x118");
FL_LAYOUT_ASSERT(offsetof(FSoundManager, NextArchId) == 0x124, "NextArchId +0x124");
FL_LAYOUT_ASSERT(offsetof(FSoundManager, DirectSound) == 0x12C, "DirectSound +0x12C");
FL_LAYOUT_ASSERT(offsetof(FSoundManager, Parser) == 0x134, "Parser +0x134");
FL_LAYOUT_ASSERT(offsetof(FSoundManager, bCoInitialized) == 0x138, "bCoInitialized +0x138");
FL_LAYOUT_ASSERT(offsetof(FSoundManager, Controlling) == 0x13C, "Controlling +0x13C");
FL_LAYOUT_ASSERT(offsetof(FSoundManager, Inner) == 0x140, "Inner +0x140");
FL_LAYOUT_ASSERT(sizeof(FSoundManager) == 0x14C, "object is 0x14C");
FL_LAYOUT_ASSERT(sizeof(FSoundInner) == 0x0C, "inner is 0x0C");
