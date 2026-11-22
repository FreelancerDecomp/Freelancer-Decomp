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

static constexpr FlULong kAggDescSize = 0x14;
static constexpr FlULong kRegisterPriority = 0x40000000;

static constexpr char kComponentName[] = "SoundStreamer";

static constexpr unsigned int kFdumpError = 0x100001;
static constexpr unsigned int kFdumpWarning = 0x100002;
static constexpr unsigned int kFdumpNotice = 0x100003;

// .data:06efe070: __FILE__ of the StreamingSound handle code (FDUMP sites 06ef101d, 17ec, 18f6,
// 1b0c, 1b3d, 1bc7, 1c7f, 206d).
static constexpr char kRetailSource[] =
    "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\SoundStreamer\\NewStream.cpp";
// .data:06efe320: __FILE__ of the streamer object code (device init 06ef2e00 and CreateStream
// 06ef3060: FDUMP sites 06ef2e80/2e9a, 2efe/2f18, 2fc5, 2ff6, 303c, 31ae).
static constexpr char kStreamerSource[] =
    "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\SoundStreamer\\SoundStreamer.cpp";
static constexpr char kFdumpErrorFmt[] = "%s(%d) : ERROR:General:%s";
static constexpr char kFdumpWarningFmt[] = "%s(%d) : WARNING:General:%s";
static constexpr char kFdumpNoticeFmt[] = "%s(%d) : NOTICE:General:%s";

static const GUID kClsidDirectSound = {
    0x47D4D946, 0x62E8, 0x11CF, {0x93, 0xBC, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
static const GUID kIidDirectSoundRetail = {
    0x279AFA83, 0x4981, 0x11CE, {0xA5, 0x21, 0x00, 0x20, 0xAF, 0x0B, 0xE5, 0x60}};


class FSoundStreamer;

class FStreamerInner final : public IDAComponent
{
public:
    FStreamerInner();

    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
    FlULong DACOM_API AddRef() override;
    FlULong DACOM_API Release() override;

    FlULong RefCount;
    FSoundStreamer* Self;
};

class FSoundStreamerFactory final : public IComponentFactory
{
public:
    FSoundStreamerFactory();

    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
    FlULong DACOM_API AddRef() override;
    FlULong DACOM_API Release() override;
    GENRESULT DACOM_API CreateInstance(AGGDESC* Desc, void** Instance) override;

    FlULong RefCount;
    const char* Name;
};

// Heap handle — retail StreamingSound 0x110 (FUN_06ef1200 / FUN_06ef13d0).
struct FStreamingSound
{
    char* Path;                      // +0x00
    FlULong PathLen;           // +0x04
    FlULong PathCap;           // +0x08
    FlULong PathPad;           // +0x0C
    IDAComponent* FileSystem;        // +0x10
    FlULong FileHandle;        // +0x14
    FlULong MapHandle;         // +0x18
    unsigned char* MappedView;       // +0x1C
    FlULong MappedSize;        // +0x20
    FlULong ReadCursor;        // +0x24
    HACMSTREAM AcmStream;            // +0x28
    ACMSTREAMHEADER AcmHeader;       // +0x2C size 0x54 → +0x80
    FlULong State;             // +0x80
    unsigned char bBufferReady;      // +0x84
    unsigned char bLoop;             // +0x85
    unsigned char bWantPlay;         // +0x86
    unsigned char bOwnedMap;         // +0x87 retail owned-map flag; port uses FS view
    FlLong Volume;                     // +0x88
    FlLong Pan;                        // +0x8C
    FlULong FactSamples;       // +0x90
    FlULong DecodeOutSize;     // +0x94
    FlULong SourceBlock;       // +0x98
    unsigned char* DataPtr;          // +0x9C
    FlULong DataSize;          // +0xA0
    FlULong DataCursor;        // +0xA4
    unsigned char* DecodeHeap;       // +0xA8
    FlULong DecodeCap;         // +0xAC
    FlULong DecodeRead;        // +0xB0
    FlULong DecodeFilled;      // +0xB4
    IDirectSoundBuffer* Buffer;      // +0xB8
    FlULong BufferBytes;       // +0xBC
    FlULong WriteCursor;       // +0xC0
    float Threshold;                 // +0xC4
    FlULong EmittedPcm;        // +0xC8
    FlULong TrimSamples;       // +0xCC
    FlLong EncoderDelayBytes;          // +0xD0
    unsigned char* FmtBlob;          // +0xD4
    unsigned short PcmTag;           // +0xD8
    unsigned short PcmChannels;      // +0xDA
    FlULong PcmRate;           // +0xDC
    FlULong PcmAvg;            // +0xE0
    unsigned short PcmBlock;         // +0xE4
    unsigned short PcmBits;          // +0xE6
    unsigned short PcmCbSize;        // +0xE8
    unsigned short FmtPad;           // +0xEA
    FlULong AlignedSize;       // +0xEC
    FlULong BytesPerSec;       // +0xF0
    FlULong EndMarkF4;         // +0xF4 drain start (play cursor at EOF)
    FlULong EndMarkF8;         // +0xF8 drain end (write cursor); -1 = not armed
    FlULong SilenceBytes;      // +0xFC FUN_06ef1ee0 remaining silence
    void* LockPtr1;                  // +0x100
    FlULong LockBytes1;        // +0x104
    void* LockPtr2;                  // +0x108
    FlULong LockBytes2;        // +0x10C
};

class FSoundStreamer final : public IStreamer2, public IAggregateComponent
{
public:
    FSoundStreamer();
    ~FSoundStreamer();

    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
    FlULong DACOM_API AddRef() override;
    FlULong DACOM_API Release() override;

    int DACOM_API InitializeDevice(FStreamerInitDesc* Desc) override;
    void* DACOM_API CreateStream(const char* Path, IDAComponent* FileSystem,
                                 FlULong Flags) override;
    GENRESULT DACOM_API DestroyStream(void* Handle) override;
    GENRESULT DACOM_API StopStream(void* Handle) override;
    GENRESULT DACOM_API StartStream(void* Handle) override;
    GENRESULT DACOM_API SetVolume(void* Handle, FlLong Volume) override;
    GENRESULT DACOM_API GetVolume(void* Handle, FlLong* Out) override;
    FlULong DACOM_API QueryStatus(void* Handle) override;
    FlULong DACOM_API GetReserved58() override;
    FlULong DACOM_API GetReserved5C() override;
    GENRESULT DACOM_API SetPan(void* Handle, FlLong Pan) override;
    GENRESULT DACOM_API GetPan(void* Handle, FlLong* Out) override;

    GENRESULT DACOM_API Initialize() override;

    void BindOuter(FAGGDESC14* Desc);
    void ShutdownDevice();
    FlULong PumpStreams();
    void PumpOneStream(FStreamingSound* Stream);
    void MovePendingActive();
    bool OpenOwnDirectSound();
    bool LoadWav(FStreamingSound* Stream, const char* Path, IDAComponent* FileSystem); // FUN_06ef1560
    bool OpenAcmStream(FStreamingSound* Stream);                                      // FUN_06ef13d0
    void FillStreamBlock(FStreamingSound* Stream, FlULong Want);                // FUN_06ef27a0
    void ReleaseStreamResources(FStreamingSound* Stream); // FUN_06ef1120 + 3910 / 3ea0 / 3ec0

    IDirectSound* DirectSound;    // +0x08
    HWND Window;                  // +0x0C
    FlULong Extra;          // +0x10
    FlULong Pad14;
    float Gain;                   // +0x18
    unsigned char bCoInitialized; // +0x1C
    unsigned char Pad1D[3];
    CRITICAL_SECTION Lock;        // +0x20
    FlULong ActiveCookie;   // +0x38 VC6 vector cookie
    FStreamingSound** ActiveBegin;// +0x3C
    FStreamingSound** ActiveEnd;  // +0x40
    FStreamingSound** ActiveCap;  // +0x44
    FlULong PendingCookie;  // +0x48
    FStreamingSound** PendingBegin;
    FStreamingSound** PendingEnd;
    FStreamingSound** PendingCap;
    FlULong Reserved58;     // +0x58
    FlULong Reserved5C;     // +0x5C
    HANDLE WorkerThread;          // +0x60
    FlULong ThreadFlags;    // +0x64 bit0 started, bit1 exited, bit7 stop
    HANDLE WakeEvent;             // +0x68
    IDAComponent* Controlling;    // +0x6C
    FStreamerInner Inner;         // +0x70
};

FL_LAYOUT_ASSERT(sizeof(FSoundStreamerFactory) == 0x0C, "factory is 0xC");
FL_LAYOUT_ASSERT(offsetof(FSoundStreamer, DirectSound) == 0x08, "DS +0x08");
FL_LAYOUT_ASSERT(offsetof(FSoundStreamer, Window) == 0x0C, "HWND +0x0C");
FL_LAYOUT_ASSERT(offsetof(FSoundStreamer, Gain) == 0x18, "Gain +0x18");
FL_LAYOUT_ASSERT(offsetof(FSoundStreamer, bCoInitialized) == 0x1C, "CoInit +0x1C");
FL_LAYOUT_ASSERT(offsetof(FSoundStreamer, Lock) == 0x20, "CS +0x20");
FL_LAYOUT_ASSERT(offsetof(FSoundStreamer, ActiveCookie) == 0x38, "active cookie +0x38");
FL_LAYOUT_ASSERT(offsetof(FSoundStreamer, Reserved58) == 0x58, "reserved +0x58");
FL_LAYOUT_ASSERT(offsetof(FSoundStreamer, WorkerThread) == 0x60, "thread +0x60");
FL_LAYOUT_ASSERT(offsetof(FSoundStreamer, ThreadFlags) == 0x64, "flags +0x64");
FL_LAYOUT_ASSERT(offsetof(FSoundStreamer, Controlling) == 0x6C, "controlling +0x6C");
FL_LAYOUT_ASSERT(offsetof(FSoundStreamer, Inner) == 0x70, "inner +0x70");
FL_LAYOUT_ASSERT(sizeof(FStreamerInner) == 0x0C, "inner is 0x0C");
FL_LAYOUT_ASSERT(sizeof(FSoundStreamer) == 0x7C, "object is 0x7C");
FL_LAYOUT_ASSERT(sizeof(ACMSTREAMHEADER) == 0x54, "ACMSTREAMHEADER is 0x54");
FL_LAYOUT_ASSERT(sizeof(FStreamingSound) == 0x110, "StreamingSound is 0x110");
FL_LAYOUT_ASSERT(offsetof(FStreamingSound, FileSystem) == 0x10, "FS +0x10");
FL_LAYOUT_ASSERT(offsetof(FStreamingSound, AcmStream) == 0x28, "HACMSTREAM +0x28");
FL_LAYOUT_ASSERT(offsetof(FStreamingSound, AcmHeader) == 0x2C, "ACM header +0x2C");
FL_LAYOUT_ASSERT(offsetof(FStreamingSound, State) == 0x80, "state +0x80");
FL_LAYOUT_ASSERT(offsetof(FStreamingSound, DataPtr) == 0x9C, "data +0x9C");
FL_LAYOUT_ASSERT(offsetof(FStreamingSound, DecodeHeap) == 0xA8, "decode heap +0xA8");
FL_LAYOUT_ASSERT(offsetof(FStreamingSound, Buffer) == 0xB8, "DS buffer +0xB8");
FL_LAYOUT_ASSERT(offsetof(FStreamingSound, TrimSamples) == 0xCC, "trim +0xCC");
FL_LAYOUT_ASSERT(offsetof(FStreamingSound, EncoderDelayBytes) == 0xD0, "delay +0xD0");
FL_LAYOUT_ASSERT(offsetof(FStreamingSound, FmtBlob) == 0xD4, "fmt +0xD4");
FL_LAYOUT_ASSERT(offsetof(FStreamingSound, PcmTag) == 0xD8, "pcm tag +0xD8");
FL_LAYOUT_ASSERT(offsetof(FStreamingSound, AlignedSize) == 0xEC, "aligned +0xEC");
FL_LAYOUT_ASSERT(offsetof(FStreamingSound, BytesPerSec) == 0xF0, "bytes/sec +0xF0");
FL_LAYOUT_ASSERT(offsetof(FStreamingSound, EndMarkF4) == 0xF4, "drain start +0xF4");
FL_LAYOUT_ASSERT(offsetof(FStreamingSound, EndMarkF8) == 0xF8, "drain end +0xF8");
FL_LAYOUT_ASSERT(offsetof(FStreamingSound, SilenceBytes) == 0xFC, "silence +0xFC");
