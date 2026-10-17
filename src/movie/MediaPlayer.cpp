// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "MediaPlayer.h"
#include "MoviePlay.h"
#include "MovieObject.h"
#include <dshow.h>
#include <stdio.h>
#include <cstring>
#include <new>

// 0x06acf3db. movie.dll operator new: __nh_malloc(size, 1). Null on failure, does not throw.
void* __cdecl MovieOperatorNew(unsigned int Size);

// FUN_06acf3d0. movie.dll operator delete: push arg, call the heap free, pop, ret.
void __cdecl MovieOperatorDelete(void* Block);

// Global state variables initialization
int g_stateFlag = 0;
FApproach2Player* g_playerObject = nullptr; // DAT_06ae092c
IGraphBuilder* g_graphBuilder = nullptr; // DAT_06ae0940
IMediaEventEx* g_mediaEventEx = nullptr; // DAT_06ae0934
IMediaControl* g_mediaControl = nullptr; // DAT_06ae0938
IBasicVideo* g_basicVideo = nullptr; // DAT_06ae0944
IVideoWindow* g_videoWindow = nullptr; // DAT_06ae093c
int g_initSuccess = 0;
unsigned char g_playing = 0;
HWND g_hwndParent = nullptr;
WNDPROC g_originalWndProc = nullptr;
IUnknown* g_serviceProvider = nullptr; // DAT_06ae0948
HMODULE g_movieModule = nullptr; // DAT_06ae0904

// 0x06ac16a0
bool __cdecl Movie_LogNegativeCode(long Code, const char* File, int Line)
{
    char Buffer[0x400];
    if (Code < 0)
    {
        wsprintfA(Buffer, "Error code = %08X, file=%s, line=%d", Code, File, Line);
        char* End = Buffer;
        while (*End != 0)
        {
            End++;
        }
        *reinterpret_cast<unsigned short*>(End) = 0x000A;
        OutputDebugStringA(Buffer);
    }
    return Code < 0;
}

// 0x06ac1bd0
bool __cdecl Movie_IsPlaying()
{
    return g_playing != 0;
}

// 0x06ac1ce0
void __cdecl Movie_Nop()
{
}

static void ReleaseApproach2Object()
{
    FApproach2Player* Object = g_playerObject;
    if (Object == nullptr)
    {
        return;
    }

    Object->MciClose();

    Object = g_playerObject;
    if (Object == nullptr)
    {
        return;
    }

    Object->DestroyWindows();

    Object = g_playerObject;
    if (Object != nullptr)
    {
        Object->ScalarDelete(1);
    }
    g_playerObject = nullptr;
}

// 0x06ac1be0
void __cdecl Movie_ReleaseComAndRestoreWndProc()
{
    IMediaEventEx* Video = g_mediaEventEx;
    if (Video != nullptr)
    {
        Video->SetNotifyWindow(0, 0, 0);
        Video = g_mediaEventEx;
        Video->Release();
        g_mediaEventEx = nullptr;
    }

    IMediaControl* Media = g_mediaControl;
    if (Media != nullptr)
    {
        Media->Stop();
        Media = g_mediaControl;
    }

    IVideoWindow* Window = g_videoWindow;
    if (Window != nullptr)
    {
        Window->put_Visible(0);
        Window = g_videoWindow;
        Window->put_Owner(0);
        Media = g_mediaControl;
        Window = g_videoWindow;
    }

    if (Media != nullptr)
    {
        Media->Release();
        Window = g_videoWindow;
        g_mediaControl = nullptr;
    }

    IBasicVideo* Basic = g_basicVideo;
    if (Basic != nullptr)
    {
        Basic->Release();
        Window = g_videoWindow;
        g_basicVideo = nullptr;
    }

    if (Window != nullptr)
    {
        Window->Release();
        g_videoWindow = nullptr;
    }

    IGraphBuilder* Graph = g_graphBuilder;
    if (Graph != nullptr)
    {
        Graph->Release();
        g_graphBuilder = nullptr;
    }

    IUnknown* Callback = g_serviceProvider;
    if (Callback != nullptr)
    {
        Callback->Release();
        g_serviceProvider = nullptr;
    }

    if (g_originalWndProc != nullptr)
    {
        SetWindowLongA(g_hwndParent, -4, static_cast<LONG>(reinterpret_cast<LONG_PTR>(g_originalWndProc)));
        g_originalWndProc = nullptr;
    }

    g_playing = 0;
}

// Exported function implementations

namespace MediaPlayer
{

void __cdecl Startup()
{
    g_movieModule = GetModuleHandleA(nullptr);
    if (g_stateFlag == 2)
    {
        Approach2RegisterParentClass();
    }
}

bool __cdecl UpdateMovie()
{
    Sleep(0x12c);
    if (g_stateFlag == 0)
    {
        return Movie_IsPlaying();
    }

    FApproach2Player* Object = g_playerObject;
    if (Object != nullptr)
    {
        unsigned char Status = Object->GetError();
        if (Status == 0)
        {
            Object = g_playerObject;
            return Object->IsPlaying();
        }
    }
    return false;
}

void __cdecl StopMovie()
{
    if (g_stateFlag == 0)
    {
        Movie_ReleaseComAndRestoreWndProc();
        return;
    }

    FApproach2Player* Object = g_playerObject;
    if (Object != nullptr)
    {
        Object->MciClose();
    }
}

long __cdecl PlayMovie(HWND hwnd, bool fullscreen, const char* path)
{
    if (g_stateFlag == 0)
    {
        return BuildAndRunWmAsfGraph(hwnd, static_cast<int>(fullscreen), path);
    }

    const char* PathForPlay = path;
    char PathBuf[0x208];
    if (path[1] != ':')
    {
        GetCurrentDirectoryA(0x104, PathBuf);
        char* Term = PathBuf;
        while (*Term != 0)
        {
            Term++;
        }
        *reinterpret_cast<unsigned short*>(Term) = 0x005C;

        char* Dest = PathBuf;
        const char* Src = path;
        while (*Dest != 0)
        {
            Dest++;
        }
        while (*Src != 0)
        {
            *Dest = *Src;
            Dest++;
            Src++;
        }
        *Dest = 0;

        PathForPlay = PathBuf;
        if (PathBuf[0] != 0)
        {
            char* Cursor = PathBuf;
            for (;;)
            {
                bool bAdvance = true;
                if (Cursor[0] == '\\' && Cursor[1] == '.' && Cursor[2] == '.')
                {
                    char* Previous = Cursor - 1;
                    if (Previous != PathBuf)
                    {
                        bool bCollapsed = false;
                        for (;;)
                        {
                            if (*Previous == '\\')
                            {
                                const char* Tail = Cursor + 3;
                                size_t Count = 0;
                                const char* Scan = Tail;
                                while (*Scan != 0)
                                {
                                    Scan++;
                                    Count++;
                                }
                                Count = Count + 1;
                                memmove(Previous, Tail, Count);
                                Cursor = Previous;
                                bCollapsed = true;
                                break;
                            }
                            Previous--;
                            if (Previous == PathBuf)
                            {
                                break;
                            }
                        }
                        if (bCollapsed)
                        {
                            if (Cursor == PathBuf)
                            {
                                Cursor++;
                            }
                            bAdvance = false;
                        }
                    }
                }
                if (bAdvance)
                {
                    Cursor++;
                }
                if (*Cursor == 0)
                {
                    break;
                }
            }
        }
    }

    if (g_stateFlag == 0)
    {
        Movie_Nop();
    }
    else
    {
        ReleaseApproach2Object();
    }

    RECT ClientRect;
    GetClientRect(hwnd, &ClientRect);
    char DebugBuf[0x400];
    sprintf(DebugBuf, "Client rect: %d, %d, %d, %d\n",
            ClientRect.left, ClientRect.top, ClientRect.right, ClientRect.bottom);
    OutputDebugStringA(DebugBuf);

    int Flag = g_stateFlag;
    Flag = Flag - 1;
    if (Flag == 0)
    {
        return 1;
    }
    Flag = Flag - 1;
    if (Flag != 0)
    {
        return 1;
    }

    FApproach2Player* Object = nullptr;
    void* Allocated = MovieOperatorNew(0x20);
    if (Allocated != nullptr)
    {
        try
        {
            Object = new (Allocated) FApproach2Player();
        }
        catch (...)
        {
            MovieOperatorDelete(Allocated);
            throw;
        }
    }

    Object->Open(hwnd, reinterpret_cast<LONG*>(&ClientRect), PathForPlay);
    g_playerObject = Object;
    Object->Play(static_cast<int>(fullscreen));
    return 0;
}

void __cdecl FreeMovie()
{
    if (g_stateFlag == 0)
    {
        Movie_Nop();
        return;
    }
    ReleaseApproach2Object();
}

void __cdecl Shutdown()
{
    if (g_stateFlag == 0)
    {
        Movie_Nop();
    }
    else
    {
        ReleaseApproach2Object();
    }

    if (g_stateFlag == 0)
    {
        Movie_Nop();
        return;
    }
    if (g_stateFlag == 2)
    {
        Approach2UnregisterParentClass();
    }
}

void __cdecl SetApproach(Approach)
{
    g_stateFlag = 0;
}

} // namespace MediaPlayer
