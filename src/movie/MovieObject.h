// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include <windows.h>
#include <stddef.h>
#include <stdint.h>

// VC6 basic_string<char> embedded at FApproach2Player+0x10. 0x10 bytes.
// Ptr addresses the characters; the allocation byte immediately before Ptr is the refcount
// (0 or 0xFF: unique/frozen, operator delete the block; otherwise decrement and detach).
struct FApproach2String
{
    uint8_t Allocator; // +0x00
    char* Ptr;         // +0x04
    uint32_t Length;   // +0x08
    uint32_t Capacity; // +0x0C
};

// PTR_LAB_06ada20c. Eleven thiscall slots. Slots after the scalar delete call __amsg_exit.
// Destroy writes this vtable over the derived one; the compiler emits it for this base.
struct FApproach2Base
{
    virtual FApproach2Base* ScalarDelete(uint8_t Flags);
    virtual void DestroyWindows();
    virtual bool OpenFile(const char* File);
    virtual void Play(int Unused);
    virtual LRESULT MciStop();
    virtual LRESULT MciClose();
    virtual bool IsPlaying();
    virtual bool ReturnFalse();
    virtual uint8_t GetError();
    virtual void Nop24(); // formerly Slot24
    virtual void Nop28(); // formerly Slot28

    FApproach2Base() {}
};

// PTR_FUN_06ada1e0. Same eleven slots. Retail operator new(0x20), then this constructor.
// Padding at +0x0E and string +0x01..+0x03 is not written.
struct FApproach2Player : FApproach2Base
{
    enum FVtableOnly { VtableOnly };

    FApproach2Player();
    // Body is empty. The prologue only stores this class's vtable, which is what
    // 0x06ac1120 does with `mov [this], PTR_FUN_06ada1e0` before it touches the fields.
    FApproach2Player(FVtableOnly) {}

    FApproach2Base* ScalarDelete(uint8_t Flags) override;
    void DestroyWindows() override;
    bool OpenFile(const char* File) override;
    void Play(int Unused) override;
    LRESULT MciStop() override;
    LRESULT MciClose() override;
    bool IsPlaying() override;
    bool ReturnFalse() override;
    uint8_t GetError() override;
    void Nop24() override;
    void Nop28() override;

    void Destroy();
    bool Open(HWND Hwnd, LONG* ClientRect, const char* Path);
    LRESULT HandleParentMessage(UINT Message, WPARAM WParam, LPARAM LParam);

    HWND ParentWindow; // +0x04 MPMCIParent
    HWND MciWindow;    // +0x08 MCIWndCreateA child
    uint8_t bStopped;  // +0x0C 1 until MCI_PLAY returns 0; MCI_MODE_STOP sets it back to 1
    uint8_t bError;    // +0x0D
    FApproach2String Path; // +0x10
};

static_assert(sizeof(FApproach2String) == 0x10, "VC6 string");
static_assert(offsetof(FApproach2String, Ptr) == 0x4, "string ptr");
static_assert(offsetof(FApproach2String, Length) == 0x8, "string length");
static_assert(offsetof(FApproach2String, Capacity) == 0xC, "string capacity");
static_assert(sizeof(FApproach2Base) == 4, "vfptr only");
static_assert(sizeof(FApproach2Player) == 0x20, "operator new(0x20)");
static_assert(offsetof(FApproach2Player, ParentWindow) == 0x4, "hwnd");
static_assert(offsetof(FApproach2Player, MciWindow) == 0x8, "mci wnd");
static_assert(offsetof(FApproach2Player, bStopped) == 0xC, "stopped");
static_assert(offsetof(FApproach2Player, bError) == 0xD, "error");
static_assert(offsetof(FApproach2Player, Path) == 0x10, "string");

// DAT_06ae1208. Bit 0 is set by Approach2RegisterEmptyAtexit and Approach2RepeatEmptyAtexit.
extern uint8_t g_movieAtexitGuard;

// stdcall WNDPROC of class MPMCIParent, RET 0x10.
LRESULT __stdcall Approach2ParentWindowProc(HWND Hwnd, UINT Message, WPARAM WParam, LPARAM LParam);

// Called from MediaPlayer::Startup only when DAT_06ae0930 == 2. Registers MPMCIParent.
void __cdecl Approach2RegisterParentClass();

// Called from MediaPlayer::Shutdown when DAT_06ae0930 == 2. UnregisterClass MPMCIParent.
void __cdecl Approach2UnregisterParentClass();

// RET 4. VC6 string tidy. FreeBuffers == 0 only zeros the fields.
void  Approach2StringRelease(FApproach2String* This, char FreeBuffers);

// RET 4. VC6 basic_string grow.
void  Approach2StringGrow(FApproach2String* This, uint32_t Size);

// CRT initializer: guard bit 0, then atexit of the empty Movie_Nop.
void __cdecl Approach2RegisterEmptyAtexit();

// 0x06ac1a80. Second _initterm slot (06add008). Same body as Approach2RegisterEmptyAtexit.
void __cdecl Approach2RepeatEmptyAtexit();
