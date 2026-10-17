// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "MovieObject.h"
#include "MediaPlayer.h"
#include "MoviePlay.h"

#include <new>
#include <string.h>
#include <vfw.h>

#pragma comment(lib, "vfw32.lib")

static_assert(sizeof(WNDCLASSEXA) == 0x30, "RegisterClassExA cbSize");
static_assert((WS_CHILD | WS_VISIBLE) == 0x50000000u, "CreateWindowEx style");
static_assert(SWP_NOZORDER == 4, "SetWindowPos flags");
static_assert(WM_CREATE == 1 && WM_SIZE == 5 && WM_PAINT == 0x0F && WM_ERASEBKGND == 0x14, "window messages");
static_assert(GWL_USERDATA == -21, "SetWindowLong index");

// FUN_06acf3d0. movie.dll operator delete: push arg, call FUN_06ad07b3, pop, ret.
void __cdecl MovieOperatorDelete(void* Block);

// 0x06acf3db. movie.dll operator new: __nh_malloc(size, 1). Returns the pointer; does not throw itself.
void* __cdecl MovieOperatorNew(unsigned int Size);

// FUN_06ad970e. Throws the "string too long" C++ exception. No return on the retail path.
void __cdecl MovieThrowStringTooLong();

// FUN_06acf876. movie.dll atexit registration (not the CRT atexit import).
int __cdecl MovieRegisterAtexit(void (__cdecl* Callback)());

// LAB_06acf7ef. __amsg_exit(0x19). Slots +0x04..+0x28 of PTR_LAB_06ada20c.
void __cdecl MovieAbortRuntime();

uint8_t g_movieAtexitGuard;

// DAT_06ada238 is four zero bytes. MCIWndCreateA reads it as an empty C string.
static const char GApproach2EmptyFile[4] = { 0, 0, 0, 0 };

// PTR_LAB_06ada20c slots after the scalar delete. Retail stores one __amsg_exit address in every slot.
static void Approach2BaseAbort()
{
    MovieAbortRuntime();
}

FApproach2Base* FApproach2Base::ScalarDelete(uint8_t Flags)
{
    new (this) FApproach2Base;
    if ((Flags & 1u) != 0)
    {
        MovieOperatorDelete(this);
    }
    return this;
}

void FApproach2Base::DestroyWindows() { Approach2BaseAbort(); }
bool FApproach2Base::OpenFile(const char* /*File*/) { Approach2BaseAbort(); return false; }
void FApproach2Base::Play(int /*Unused*/) { Approach2BaseAbort(); }
LRESULT FApproach2Base::MciStop() { Approach2BaseAbort(); return 0; }
LRESULT FApproach2Base::MciClose() { Approach2BaseAbort(); return 0; }
bool FApproach2Base::IsPlaying() { Approach2BaseAbort(); return false; }
bool FApproach2Base::ReturnFalse() { Approach2BaseAbort(); return false; }
uint8_t FApproach2Base::GetError() { Approach2BaseAbort(); return 0; }
void FApproach2Base::Nop24() { Approach2BaseAbort(); }
void FApproach2Base::Nop28() { Approach2BaseAbort(); }

void FApproach2Player::Nop24() {}
void FApproach2Player::Nop28() {}

// 0x06ac15a9. Catch of the string grow. pType of HandlerType_06adb3a8 is null (catch-all).
// Replaces the rounded capacity with the requested size and allocates that size plus 2.
static uint8_t* Approach2StringGrowOnAllocFailure(uint32_t Size, uint32_t* Capacity)
{
    *Capacity = Size;
    uint32_t AllocBytes = Size + 2u;
    if ((int32_t)AllocBytes < 0)
    {
        AllocBytes = 0;
    }
    return (uint8_t*)MovieOperatorNew(AllocBytes);
}

// 0x06ac15d0. Continuation after the allocation. EBX = string, ESI = capacity, [EBP+8] = block.
static void Approach2StringGrowInstall(FApproach2String* This, uint8_t* Block, uint32_t Capacity)
{
    uint32_t OldLength = This->Length;
    if (OldLength != 0)
    {
        uint32_t CopyLength = OldLength;
        if (OldLength > Capacity)
        {
            CopyLength = Capacity;
        }
        memcpy(Block + 1, This->Ptr, CopyLength);
    }

    char* OldPtr = This->Ptr;
    if (OldPtr != nullptr)
    {
        uint8_t Ref = (uint8_t)OldPtr[-1];
        if (Ref == 0 || Ref == 0xFFu)
        {
            MovieOperatorDelete(OldPtr - 1);
        }
        else
        {
            OldPtr[-1] = (char)(Ref - 1u);
        }
    }

    This->Length = 0;
    char* NewPtr = (char*)(Block + 1);
    This->Ptr = NewPtr;
    NewPtr[-1] = 0;
    This->Capacity = Capacity;
    uint32_t NewLength = Capacity;
    if (OldLength <= Capacity)
    {
        NewLength = OldLength;
    }
    This->Length = NewLength;
    NewPtr[NewLength] = 0;
}

// 0x06ac1000. WNDPROC of MPMCIParent.
LRESULT __stdcall Approach2ParentWindowProc(HWND Hwnd, UINT Message, WPARAM WParam, LPARAM LParam)
{
    FApproach2Player* Player;
    if (Message == WM_CREATE)
    {
        Player = *(FApproach2Player**)LParam;
        SetWindowLongA(Hwnd, GWL_USERDATA, (LONG)(uintptr_t)Player);
        Player->ParentWindow = Hwnd;
    }
    else
    {
        Player = (FApproach2Player*)(uintptr_t)GetWindowLongA(Hwnd, GWL_USERDATA);
    }

    if (Player != nullptr)
    {
        return Player->HandleParentMessage(Message, WParam, LParam);
    }
    return DefWindowProcA(Hwnd, Message, WParam, LParam);
}

// 0x06ac1060. RegisterClassExA("MPMCIParent").
void __cdecl Approach2RegisterParentClass()
{
    WNDCLASSEXA WindowClass;
    memset(&WindowClass, 0, sizeof(WindowClass));
    WindowClass.cbSize = 0x30;
    WindowClass.style = 0xA0;
    WindowClass.lpfnWndProc = &Approach2ParentWindowProc;
    WindowClass.hInstance = GetModuleHandleA(nullptr);
    WindowClass.lpszClassName = "MPMCIParent";
    RegisterClassExA(&WindowClass);
}

// 0x06ac10b0. UnregisterClassA("MPMCIParent").
void __cdecl Approach2UnregisterParentClass()
{
    HMODULE Instance = GetModuleHandleA(nullptr);
    UnregisterClassA("MPMCIParent", Instance);
}

// 0x06ac10d0. Constructor of the 0x20-byte approach-2 player.
FApproach2Player::FApproach2Player()
{
    // Listing pushes ECX and reads [ESP+3], the high byte of This, into the allocator byte.
    uint8_t Allocator = (uint8_t)((uintptr_t)this >> 24);
    Path.Allocator = Allocator;
    Path.Ptr = nullptr;
    Path.Length = 0;
    Path.Capacity = 0;
    MciWindow = nullptr;
    bStopped = 1;
    bError = 0;
    ParentWindow = nullptr;
}

// 0x06ac1100. Scalar deleting destructor.
FApproach2Base* FApproach2Player::ScalarDelete(uint8_t Flags)
{
    Destroy();
    if ((Flags & 1u) != 0)
    {
        MovieOperatorDelete(this);
    }
    return this;
}

// 0x06ac1120. Destroy the MCI child and release the VC6 path.
void FApproach2Player::Destroy()
{
    FApproach2Player* This = this;
    new (This) FApproach2Player(FApproach2Player::VtableOnly);
    if (This->ParentWindow != nullptr)
    {
        DestroyWindow(This->MciWindow);
        This->ParentWindow = nullptr;
        This->MciWindow = nullptr;
    }

    char* Text = This->Path.Ptr;
    if (Text != nullptr)
    {
        uint8_t Ref = (uint8_t)Text[-1];
        if (Ref != 0 && Ref != 0xFFu)
        {
            Text[-1] = (char)(Ref - 1u);
            This->Path.Ptr = nullptr;
            This->Path.Length = 0;
            This->Path.Capacity = 0;
            new (This) FApproach2Base;
            return;
        }
        MovieOperatorDelete(Text - 1);
    }

    This->Path.Ptr = nullptr;
    This->Path.Length = 0;
    This->Path.Capacity = 0;
    new (This) FApproach2Base;
}

// 0x06ac1190. Parent window procedure body. WM_CREATE creates the MCI child.
LRESULT FApproach2Player::HandleParentMessage(UINT Message, WPARAM WParam, LPARAM LParam)
{
    FApproach2Player* This = this;
    if (Message > WM_PAINT)
    {
        if (Message == WM_ERASEBKGND)
        {
            return 1;
        }
        if (Message == 0x4C8u)
        {
            if (LParam == 0x20D)
            {
                This->bStopped = 1;
            }
            return 0;
        }
    }
    else if (Message == WM_PAINT)
    {
        if (GetUpdateRect(This->ParentWindow, nullptr, FALSE) != 0)
        {
            PAINTSTRUCT Paint;
            BeginPaint(This->ParentWindow, &Paint);
            EndPaint(This->ParentWindow, &Paint);
        }
        return 0;
    }
    else if (Message == WM_CREATE)
    {
        const char* FileName = This->Path.Ptr;
        if (FileName == nullptr)
        {
            FileName = GApproach2EmptyFile;
        }
        HMODULE Instance = GetModuleHandleA(nullptr);
        HWND Mci = MCIWndCreateA(This->ParentWindow, Instance, 0x5200810Au, FileName);
        This->MciWindow = Mci;
        if (Mci != nullptr)
        {
            RECT Client;
            GetClientRect(This->ParentWindow, &Client);
            SetWindowPos(This->MciWindow, nullptr, 0, 0, Client.right, Client.bottom, SWP_NOZORDER);
            return 0;
        }
        This->bError = 1;
        return 0;
    }
    else if (Message == WM_SIZE && This->MciWindow != nullptr)
    {
        RECT Client;
        GetClientRect(This->ParentWindow, &Client);
        SetWindowPos(This->MciWindow, nullptr, 0, 0, Client.right, Client.bottom, SWP_NOZORDER);
    }

    return DefWindowProcA(This->MciWindow, Message, WParam, LParam);
}

// 0x06ac12f0. Assign the VC6 path and create the MPMCIParent window.
bool FApproach2Player::Open(HWND Hwnd, LONG* ClientRect, const char* Path)
{
    FApproach2Player* This = this;
    DestroyWindows();
    This->bStopped = 1;
    This->bError = 0;

    uint32_t Length = 0;
    const char* Scan = Path;
    while (*Scan != 0)
    {
        Scan += 1;
        Length += 1;
    }
    if (Length > 0xFFFFFFFDu)
    {
        MovieThrowStringTooLong();
    }

    FApproach2String* Text = &This->Path;
    char* Existing = Text->Ptr;
    uint8_t Ref = 0;
    bool bShared = false;
    if (Existing != nullptr)
    {
        Ref = (uint8_t)Existing[-1];
        if (Ref != 0 && Ref != 0xFFu)
        {
            bShared = true;
        }
    }

    if (bShared)
    {
        if (Length == 0)
        {
            Existing[-1] = (char)(Ref - 1u);
            Text->Ptr = nullptr;
            Text->Length = 0;
            Text->Capacity = 0;
        }
        else
        {
            Approach2StringGrow(Text, Length);
            char* Dest = Text->Ptr;
            memcpy(Dest, Path, Length);
            Text->Length = Length;
            Dest[Length] = 0;
        }
    }
    else if (Length == 0)
    {
        Approach2StringRelease(Text, 1);
    }
    else
    {
        if (Text->Capacity > 0x1Fu || Text->Capacity < Length)
        {
            Approach2StringRelease(Text, 1);
            Approach2StringGrow(Text, Length);
        }
        char* Dest = Text->Ptr;
        memcpy(Dest, Path, Length);
        Text->Length = Length;
        Dest[Length] = 0;
    }

    LONG Left = ClientRect[0];
    LONG Top = ClientRect[1];
    LONG Width = ClientRect[2] - Left;
    LONG Height = ClientRect[3] - Top;
    HWND Created = CreateWindowExA(
        0,
        "MPMCIParent",
        "TNB",
        WS_CHILD | WS_VISIBLE,
        Left,
        Top,
        Width,
        Height,
        Hwnd,
        nullptr,
        GetModuleHandleA(nullptr),
        This);
    This->ParentWindow = Created;
    if (Created == nullptr)
    {
        This->bError = 1;
    }
    return Created != nullptr;
}

// 0x06ac1400. Destroy the MCI child if the parent window exists.
void FApproach2Player::DestroyWindows()
{
    FApproach2Player* This = this;
    if (This->ParentWindow != nullptr)
    {
        DestroyWindow(This->MciWindow);
        This->ParentWindow = nullptr;
        This->MciWindow = nullptr;
    }
}

// 0x06ac1430. MCIWNDM_OPENA (0x499).
bool FApproach2Player::OpenFile(const char* File)
{
    FApproach2Player* This = this;
    LRESULT Result = SendMessageA(This->MciWindow, 0x499, 0, (LPARAM)File);
    if (Result == 0)
    {
        return true;
    }
    This->bError = 1;
    return false;
}

// 0x06ac1460. MCI_PLAY (0x806). The stack dword is not read.
void FApproach2Player::Play(int /*Unused*/)
{
    FApproach2Player* This = this;
    LRESULT Result = SendMessageA(This->MciWindow, 0x806, 0, 0);
    if (Result == 0)
    {
        This->bStopped = 0;
        return;
    }
    This->bStopped = 1;
}

// 0x06ac1490. MCI_STOP (0x808).
LRESULT FApproach2Player::MciStop()
{
    FApproach2Player* This = this;
    return SendMessageA(This->MciWindow, 0x808, 0, 0);
}

// 0x06ac14b0. MCI_CLOSE (0x804).
LRESULT FApproach2Player::MciClose()
{
    FApproach2Player* This = this;
    return SendMessageA(This->MciWindow, 0x804, 0, 0);
}

// 0x06ac14d0. True when bStopped is 0.
bool FApproach2Player::IsPlaying()
{
    FApproach2Player* This = this;
    uint8_t Flag = This->bStopped;
    return Flag == 0;
}

// 0x06ac14e0. XOR AL, AL. No caller in movie.dll reads EAX: the only CALL [reg+0x1C]
// is the COM object at 06ac236a. Slot +0x1C of PTR_FUN_06ada1e0 has no call site.
bool FApproach2Player::ReturnFalse()
{
    return false;
}

// 0x06ac14f0. AL = byte [this+0x0D]. UpdateMovie 06ac1755 is the only call of this
// slot and it tests AL (06ac1758).
uint8_t FApproach2Player::GetError()
{
    FApproach2Player* This = this;
    return This->bError;
}

// 0x06ac1500. VC6 string tidy. FreeBuffers == 0 only zeros ptr/len/cap.
void  Approach2StringRelease(FApproach2String* This, char FreeBuffers)
{
    if (FreeBuffers != 0 && This->Ptr != nullptr)
    {
        uint8_t Ref = (uint8_t)This->Ptr[-1];
        if (Ref == 0 || Ref == 0xFFu)
        {
            MovieOperatorDelete(This->Ptr - 1);
        }
        else
        {
            This->Ptr[-1] = (char)(Ref - 1u);
        }
    }
    This->Ptr = nullptr;
    This->Length = 0;
    This->Capacity = 0;
}

// 0x06ac1550. VC6 basic_string grow.
void  Approach2StringGrow(FApproach2String* This, uint32_t Size)
{
    uint32_t Capacity = Size | 0x1Fu;
    if (Capacity > 0xFFFFFFFDu)
    {
        Capacity = Size;
    }
    uint32_t AllocBytes = Capacity + 2u;
    if ((int32_t)AllocBytes < 0)
    {
        AllocBytes = 0;
    }

    uint8_t* Block;
    try
    {
        Block = (uint8_t*)MovieOperatorNew(AllocBytes);
    }
    catch (...)
    {
        Block = Approach2StringGrowOnAllocFailure(Size, &Capacity);
    }
    Approach2StringGrowInstall(This, Block, Capacity);
}

// 0x06ac1660. Set the atexit guard bit, then register the empty terminator.
void __cdecl Approach2RegisterEmptyAtexit()
{
    uint8_t Guard = g_movieAtexitGuard;
    if ((Guard & 1u) == 0)
    {
        g_movieAtexitGuard = (uint8_t)(Guard | 1u);
    }
    MovieRegisterAtexit(&Movie_Nop);
}

// 0x06ac1a80. Identical to 0x06ac1660, including the JNZ that skips only the
// OR/store (06ac1a8a -> 06ac1a94). Both always register FUN_06ac1ce0 (Movie_Nop).
void __cdecl Approach2RepeatEmptyAtexit()
{
    uint8_t Guard = g_movieAtexitGuard;
    if ((Guard & 1u) == 0)
    {
        g_movieAtexitGuard = (uint8_t)(Guard | 1u);
    }
    MovieRegisterAtexit(&Movie_Nop);
}

