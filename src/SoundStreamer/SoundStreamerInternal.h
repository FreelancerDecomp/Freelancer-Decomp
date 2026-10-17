// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "dacom.h"
#include "soundstreamer.h"
#include "audio/dsound8.h"
#include <mmreg.h>
#include <msacm.h>
#include <stddef.h>

static const unsigned long kAggDescSize = 0x14;
static const unsigned long kRegisterPriority = 0x40000000;

static const char kComponentName[] = "SoundStreamer";

static const unsigned int kFdumpError = 0x100001;
static const unsigned int kFdumpWarning = 0x100002;
static const unsigned int kFdumpNotice = 0x100003;

// .data:06efe070: __FILE__ of the StreamingSound handle code (FDUMP sites 06ef101d, 17ec, 18f6,
// 1b0c, 1b3d, 1bc7, 1c7f, 206d).
static const char kRetailSource[] =
    "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\SoundStreamer\\NewStream.cpp";
// .data:06efe320: __FILE__ of the streamer object code (device init 06ef2e00 and CreateStream
// 06ef3060: FDUMP sites 06ef2e80/2e9a, 2efe/2f18, 2fc5, 2ff6, 303c, 31ae).
static const char kStreamerSource[] =
    "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\SoundStreamer\\SoundStreamer.cpp";
static const char kFdumpErrorFmt[] = "%s(%d) : ERROR:General:%s";
static const char kFdumpWarningFmt[] = "%s(%d) : WARNING:General:%s";
static const char kFdumpNoticeFmt[] = "%s(%d) : NOTICE:General:%s";

static const GUID kClsidDirectSound = {
    0x47D4D946, 0x62E8, 0x11CF, {0x93, 0xBC, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
static const GUID kIidDirectSoundRetail = {
    0x279AFA83, 0x4981, 0x11CE, {0xA5, 0x21, 0x00, 0x20, 0xAF, 0x0B, 0xE5, 0x60}};

struct FStreamerAggDesc
{
    unsigned long Size;
    const char* Name;
    IDAComponent* Outer;
    IDAComponent** InnerOut;
    const char* Extra;
};

class FSoundStreamer;

class FStreamerInner final : public IDAComponent
{
public:
    FStreamerInner();

    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
    unsigned long DACOM_API AddRef() override;
    unsigned long DACOM_API Release() override;

    unsigned long RefCount;
    FSoundStreamer* Self;
};

class FSoundStreamerFactory final : public IComponentFactory
{
public:
    FSoundStreamerFactory();

    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
    unsigned long DACOM_API AddRef() override;
    unsigned long DACOM_API Release() override;
    GENRESULT DACOM_API CreateInstance(AGGDESC* Desc, void** Instance) override;

    unsigned long RefCount;
    const char* Name;
};

// Heap handle — retail StreamingSound 0x110 (FUN_06ef1200 / FUN_06ef13d0).
struct FStreamingSound
{
    char* Path;                      // +0x00
    unsigned long PathLen;           // +0x04
    unsigned long PathCap;           // +0x08
    unsigned long PathPad;           // +0x0C
    IDAComponent* FileSystem;        // +0x10
    unsigned long FileHandle;        // +0x14
    unsigned long MapHandle;         // +0x18
    unsigned char* MappedView;       // +0x1C
    unsigned long MappedSize;        // +0x20
    unsigned long ReadCursor;        // +0x24
    HACMSTREAM AcmStream;            // +0x28
    ACMSTREAMHEADER AcmHeader;       // +0x2C size 0x54 → +0x80
    unsigned long State;             // +0x80
    unsigned char bBufferReady;      // +0x84
    unsigned char bLoop;             // +0x85
    unsigned char bWantPlay;         // +0x86
    unsigned char bOwnedMap;         // +0x87 retail owned-map flag; port uses FS view
    long Volume;                     // +0x88
    long Pan;                        // +0x8C
    unsigned long FactSamples;       // +0x90
    unsigned long DecodeOutSize;     // +0x94
    unsigned long SourceBlock;       // +0x98
    unsigned char* DataPtr;          // +0x9C
    unsigned long DataSize;          // +0xA0
    unsigned long DataCursor;        // +0xA4
    unsigned char* DecodeHeap;       // +0xA8
    unsigned long DecodeCap;         // +0xAC
    unsigned long DecodeRead;        // +0xB0
    unsigned long DecodeFilled;      // +0xB4
    IDirectSoundBuffer* Buffer;      // +0xB8
    unsigned long BufferBytes;       // +0xBC
    unsigned long WriteCursor;       // +0xC0
    float Threshold;                 // +0xC4
    unsigned long EmittedPcm;        // +0xC8
    unsigned long TrimSamples;       // +0xCC
    long EncoderDelayBytes;          // +0xD0
    unsigned char* FmtBlob;          // +0xD4
    unsigned short PcmTag;           // +0xD8
    unsigned short PcmChannels;      // +0xDA
    unsigned long PcmRate;           // +0xDC
    unsigned long PcmAvg;            // +0xE0
    unsigned short PcmBlock;         // +0xE4
    unsigned short PcmBits;          // +0xE6
    unsigned short PcmCbSize;        // +0xE8
    unsigned short FmtPad;           // +0xEA
    unsigned long AlignedSize;       // +0xEC
    unsigned long BytesPerSec;       // +0xF0
    unsigned long EndMarkF4;         // +0xF4 drain start (play cursor at EOF)
    unsigned long EndMarkF8;         // +0xF8 drain end (write cursor); -1 = not armed
    unsigned long SilenceBytes;      // +0xFC FUN_06ef1ee0 remaining silence
    void* LockPtr1;                  // +0x100
    unsigned long LockBytes1;        // +0x104
    void* LockPtr2;                  // +0x108
    unsigned long LockBytes2;        // +0x10C
};

class FSoundStreamer final : public IStreamer2, public IAggregateComponent
{
public:
    FSoundStreamer();
    ~FSoundStreamer();

    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
    unsigned long DACOM_API AddRef() override;
    unsigned long DACOM_API Release() override;

    int DACOM_API InitializeDevice(FStreamerInitDesc* Desc) override;
    void* DACOM_API CreateStream(const char* Path, IDAComponent* FileSystem,
                                 unsigned long Flags) override;
    GENRESULT DACOM_API DestroyStream(void* Handle) override;
    GENRESULT DACOM_API StopStream(void* Handle) override;
    GENRESULT DACOM_API StartStream(void* Handle) override;
    GENRESULT DACOM_API SetVolume(void* Handle, long Volume) override;
    GENRESULT DACOM_API GetVolume(void* Handle, long* Out) override;
    unsigned long DACOM_API QueryStatus(void* Handle) override;
    unsigned long DACOM_API GetReserved58() override;
    unsigned long DACOM_API GetReserved5C() override;
    GENRESULT DACOM_API SetPan(void* Handle, long Pan) override;
    GENRESULT DACOM_API GetPan(void* Handle, long* Out) override;

    GENRESULT DACOM_API Initialize() override;

    void BindOuter(FStreamerAggDesc* Desc);
    void ShutdownDevice();
    unsigned long PumpStreams();
    void PumpOneStream(FStreamingSound* Stream);
    void MovePendingActive();
    bool OpenOwnDirectSound();
    bool LoadWav(FStreamingSound* Stream, const char* Path, IDAComponent* FileSystem); // FUN_06ef1560
    bool OpenAcmStream(FStreamingSound* Stream);                                      // FUN_06ef13d0
    void FillStreamBlock(FStreamingSound* Stream, unsigned long Want);                // FUN_06ef27a0
    void ReleaseStreamResources(FStreamingSound* Stream); // FUN_06ef1120 + 3910 / 3ea0 / 3ec0

    IDirectSound* DirectSound;    // +0x08
    HWND Window;                  // +0x0C
    unsigned long Extra;          // +0x10
    unsigned long Pad14;
    float Gain;                   // +0x18
    unsigned char bCoInitialized; // +0x1C
    unsigned char Pad1D[3];
    CRITICAL_SECTION Lock;        // +0x20
    unsigned long ActiveCookie;   // +0x38 VC6 vector cookie
    FStreamingSound** ActiveBegin;// +0x3C
    FStreamingSound** ActiveEnd;  // +0x40
    FStreamingSound** ActiveCap;  // +0x44
    unsigned long PendingCookie;  // +0x48
    FStreamingSound** PendingBegin;
    FStreamingSound** PendingEnd;
    FStreamingSound** PendingCap;
    unsigned long Reserved58;     // +0x58
    unsigned long Reserved5C;     // +0x5C
    HANDLE WorkerThread;          // +0x60
    unsigned long ThreadFlags;    // +0x64 bit0 started, bit1 exited, bit7 stop
    HANDLE WakeEvent;             // +0x68
    IDAComponent* Controlling;    // +0x6C
    FStreamerInner Inner;         // +0x70
};

static_assert(sizeof(FSoundStreamerFactory) == 0x0C, "factory is 0xC");
static_assert(offsetof(FSoundStreamer, DirectSound) == 0x08, "DS +0x08");
static_assert(offsetof(FSoundStreamer, Window) == 0x0C, "HWND +0x0C");
static_assert(offsetof(FSoundStreamer, Gain) == 0x18, "Gain +0x18");
static_assert(offsetof(FSoundStreamer, bCoInitialized) == 0x1C, "CoInit +0x1C");
static_assert(offsetof(FSoundStreamer, Lock) == 0x20, "CS +0x20");
static_assert(offsetof(FSoundStreamer, ActiveCookie) == 0x38, "active cookie +0x38");
static_assert(offsetof(FSoundStreamer, Reserved58) == 0x58, "reserved +0x58");
static_assert(offsetof(FSoundStreamer, WorkerThread) == 0x60, "thread +0x60");
static_assert(offsetof(FSoundStreamer, ThreadFlags) == 0x64, "flags +0x64");
static_assert(offsetof(FSoundStreamer, Controlling) == 0x6C, "controlling +0x6C");
static_assert(offsetof(FSoundStreamer, Inner) == 0x70, "inner +0x70");
static_assert(sizeof(FStreamerInner) == 0x0C, "inner is 0x0C");
static_assert(sizeof(FSoundStreamer) == 0x7C, "object is 0x7C");
static_assert(sizeof(ACMSTREAMHEADER) == 0x54, "ACMSTREAMHEADER is 0x54");
static_assert(sizeof(FStreamingSound) == 0x110, "StreamingSound is 0x110");
static_assert(offsetof(FStreamingSound, FileSystem) == 0x10, "FS +0x10");
static_assert(offsetof(FStreamingSound, AcmStream) == 0x28, "HACMSTREAM +0x28");
static_assert(offsetof(FStreamingSound, AcmHeader) == 0x2C, "ACM header +0x2C");
static_assert(offsetof(FStreamingSound, State) == 0x80, "state +0x80");
static_assert(offsetof(FStreamingSound, DataPtr) == 0x9C, "data +0x9C");
static_assert(offsetof(FStreamingSound, DecodeHeap) == 0xA8, "decode heap +0xA8");
static_assert(offsetof(FStreamingSound, Buffer) == 0xB8, "DS buffer +0xB8");
static_assert(offsetof(FStreamingSound, TrimSamples) == 0xCC, "trim +0xCC");
static_assert(offsetof(FStreamingSound, EncoderDelayBytes) == 0xD0, "delay +0xD0");
static_assert(offsetof(FStreamingSound, FmtBlob) == 0xD4, "fmt +0xD4");
static_assert(offsetof(FStreamingSound, PcmTag) == 0xD8, "pcm tag +0xD8");
static_assert(offsetof(FStreamingSound, AlignedSize) == 0xEC, "aligned +0xEC");
static_assert(offsetof(FStreamingSound, BytesPerSec) == 0xF0, "bytes/sec +0xF0");
static_assert(offsetof(FStreamingSound, EndMarkF4) == 0xF4, "drain start +0xF4");
static_assert(offsetof(FStreamingSound, EndMarkF8) == 0xF8, "drain end +0xF8");
static_assert(offsetof(FStreamingSound, SilenceBytes) == 0xFC, "silence +0xFC");
