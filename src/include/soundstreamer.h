// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "dacom.h"
#include "audio/dsound8.h"

// soundstreamer.dll public ABI. Source: EXE/Disasm/soundstreamer.dll,
// docs/re/audio.md. DALib holds the live streamer and drives the device through it;
// everything private to the port stays in src/SoundStreamer/SoundStreamerInternal.h.

struct FStreamerInitDesc
{
    unsigned long Size;        // +0x00 must be 0x18
    IDirectSound* DirectSound; // +0x04
    HWND Window;               // +0x08
    unsigned long Extra;       // +0x0C never read after store
    unsigned long Unused10;    // +0x10
    float Gain;                // +0x14; 0 → 1.0f
};

struct IStreamer2 : IDAComponent
{
    virtual int DACOM_API InitializeDevice(FStreamerInitDesc* Desc) = 0; // +0x0C BOOL
    virtual void* DACOM_API CreateStream(const char* Path, IDAComponent* FileSystem,
                                         unsigned long Flags) = 0; // +0x10
    virtual GENRESULT DACOM_API DestroyStream(void* Handle) = 0;   // +0x14
    virtual GENRESULT DACOM_API StopStream(void* Handle) = 0;      // +0x18
    virtual GENRESULT DACOM_API StartStream(void* Handle) = 0;     // +0x1C
    virtual GENRESULT DACOM_API SetVolume(void* Handle, long Volume) = 0; // +0x20
    virtual GENRESULT DACOM_API GetVolume(void* Handle, long* Out) = 0;   // +0x24
    virtual unsigned long DACOM_API QueryStatus(void* Handle) = 0;        // +0x28
    virtual unsigned long DACOM_API GetReserved58() = 0;                  // +0x2C
    virtual unsigned long DACOM_API GetReserved5C() = 0;                  // +0x30
    virtual GENRESULT DACOM_API SetPan(void* Handle, long Pan) = 0;       // +0x34
    virtual GENRESULT DACOM_API GetPan(void* Handle, long* Out) = 0;      // +0x38
};
