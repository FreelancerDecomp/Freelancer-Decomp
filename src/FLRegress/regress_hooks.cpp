// Freelancer Decomp: FLRegress, the headless regression harness -- PORT(flregress), not part of retail.

#include "regress_hooks.h"

#include <windows.h>
#include <mmsystem.h>
#include <psapi.h>
#include <shlobj.h>

#include <cmath>
#include <cstring>

namespace
{
    // The virtual clock starts at the same fixed time in every run (1000 s after "boot"), so
    // values derived from an absolute reading (a timeGetTime srand seed) repeat as well.
    const long long StartSeconds = 1000;

    bool GVirtualClock = false;
    long long GFrequency = 0;     // the real counter frequency (Timing::init caches it too)
    long long GOffsetTicks = 0;   // virtual time since the start, in counter ticks

    char GDocumentsFolder[MAX_PATH] = "";

    typedef HRESULT(WINAPI* FSHGetFolderPathA)(HWND, int, HANDLE, DWORD, LPSTR);
    FSHGetFolderPathA GRealSHGetFolderPathA = nullptr;

    long long NowTicks()
    {
        return StartSeconds * GFrequency + GOffsetTicks;
    }

    DWORD NowMs()
    {
        return (DWORD)(NowTicks() * 1000 / GFrequency);
    }

    BOOL WINAPI HookQueryPerformanceCounter(LARGE_INTEGER* Count)
    {
        if (!GVirtualClock)
        {
            return QueryPerformanceCounter(Count);
        }
        if (Count == nullptr)
        {
            return FALSE;
        }
        Count->QuadPart = NowTicks();
        return TRUE;
    }

    DWORD WINAPI HookTimeGetTime()
    {
        return GVirtualClock ? NowMs() : timeGetTime();
    }

    DWORD WINAPI HookGetTickCount()
    {
        return GVirtualClock ? NowMs() : GetTickCount();
    }

    HRESULT WINAPI HookSHGetFolderPathA(HWND Owner, int Folder, HANDLE Token, DWORD Flags, LPSTR Path)
    {
        // The low byte is the CSIDL; the high byte holds flags such as CSIDL_FLAG_CREATE.
        if ((Folder & 0xff) == CSIDL_PERSONAL && GDocumentsFolder[0] != '\0' && Path != nullptr)
        {
            if ((Folder & CSIDL_FLAG_CREATE) != 0)
            {
                CreateDirectoryA(GDocumentsFolder, nullptr);
            }
            strcpy(Path, GDocumentsFolder);
            return S_OK;
        }
        if (GRealSHGetFolderPathA == nullptr)
        {
            return E_FAIL;
        }
        return GRealSHGetFolderPathA(Owner, Folder, Token, Flags, Path);
    }

    void* ReplacementFor(const char* Name)
    {
        if (strcmp(Name, "QueryPerformanceCounter") == 0)
        {
            return (void*)&HookQueryPerformanceCounter;
        }
        if (strcmp(Name, "timeGetTime") == 0)
        {
            return (void*)&HookTimeGetTime;
        }
        if (strcmp(Name, "GetTickCount") == 0)
        {
            return (void*)&HookGetTickCount;
        }
        if (strcmp(Name, "SHGetFolderPathA") == 0)
        {
            return (void*)&HookSHGetFolderPathA;
        }
        return nullptr;
    }

    int PatchModule(HMODULE Module)
    {
        BYTE* const Base = (BYTE*)Module;
        const IMAGE_DOS_HEADER* Dos = (const IMAGE_DOS_HEADER*)Base;
        if (Dos->e_magic != IMAGE_DOS_SIGNATURE)
        {
            return 0;
        }
        const IMAGE_NT_HEADERS* Nt = (const IMAGE_NT_HEADERS*)(Base + Dos->e_lfanew);
        const IMAGE_DATA_DIRECTORY& Imports = Nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (Imports.VirtualAddress == 0)
        {
            return 0;
        }

        int Patched = 0;
        for (const IMAGE_IMPORT_DESCRIPTOR* Desc = (const IMAGE_IMPORT_DESCRIPTOR*)(Base + Imports.VirtualAddress);
             Desc->Name != 0; ++Desc)
        {
            if (Desc->OriginalFirstThunk == 0)
            {
                continue;
            }
            const IMAGE_THUNK_DATA* Names = (const IMAGE_THUNK_DATA*)(Base + Desc->OriginalFirstThunk);
            IMAGE_THUNK_DATA* Addresses = (IMAGE_THUNK_DATA*)(Base + Desc->FirstThunk);
            for (; Names->u1.AddressOfData != 0; ++Names, ++Addresses)
            {
                if (IMAGE_SNAP_BY_ORDINAL(Names->u1.Ordinal))
                {
                    continue;
                }
                const IMAGE_IMPORT_BY_NAME* Import = (const IMAGE_IMPORT_BY_NAME*)(Base + Names->u1.AddressOfData);
                void* const Replacement = ReplacementFor((const char*)Import->Name);
                if (Replacement == nullptr || Addresses->u1.Function == (ULONG_PTR)Replacement)
                {
                    continue;
                }
                DWORD OldProtect = 0;
                if (!VirtualProtect(&Addresses->u1.Function, sizeof(Addresses->u1.Function), PAGE_READWRITE,
                        &OldProtect))
                {
                    continue;
                }
                Addresses->u1.Function = (ULONG_PTR)Replacement;
                VirtualProtect(&Addresses->u1.Function, sizeof(Addresses->u1.Function), OldProtect, &OldProtect);
                ++Patched;
            }
        }
        return Patched;
    }

    // The directory part of a module path, lower-cased, without the trailing backslash.
    bool ModuleDirectory(HMODULE Module, char (&Out)[MAX_PATH])
    {
        if (GetModuleFileNameA(Module, Out, MAX_PATH) == 0)
        {
            return false;
        }
        char* Slash = strrchr(Out, '\\');
        if (Slash == nullptr)
        {
            return false;
        }
        *Slash = '\0';
        CharLowerA(Out);
        return true;
    }
}

namespace RegressHooks
{
    void SetDocumentsFolder(const char* Path)
    {
        strncpy(GDocumentsFolder, Path, MAX_PATH - 1);
        GDocumentsFolder[MAX_PATH - 1] = '\0';
    }

    int Install()
    {
        if (GFrequency == 0)
        {
            LARGE_INTEGER Frequency;
            QueryPerformanceFrequency(&Frequency);
            GFrequency = Frequency.QuadPart > 0 ? Frequency.QuadPart : 10000000;
        }
        if (GRealSHGetFolderPathA == nullptr)
        {
            HMODULE Shell = LoadLibraryA("shell32.dll");
            if (Shell != nullptr)
            {
                GRealSHGetFolderPathA = (FSHGetFolderPathA)GetProcAddress(Shell, "SHGetFolderPathA");
            }
        }

        // Only the game modules: the DLLs beside this executable.
        char OwnDirectory[MAX_PATH];
        if (!ModuleDirectory(nullptr, OwnDirectory))
        {
            return 0;
        }
        HMODULE Modules[1024];
        DWORD Needed = 0;
        if (!EnumProcessModules(GetCurrentProcess(), Modules, sizeof(Modules), &Needed))
        {
            return 0;
        }
        DWORD Count = Needed / sizeof(HMODULE);
        if (Count > 1024)
        {
            Count = 1024;
        }
        const HMODULE Self = GetModuleHandleA(nullptr);
        int Patched = 0;
        for (DWORD i = 0; i < Count; ++i)
        {
            if (Modules[i] == Self)
            {
                continue;
            }
            char Directory[MAX_PATH];
            if (ModuleDirectory(Modules[i], Directory) && strcmp(Directory, OwnDirectory) == 0)
            {
                Patched += PatchModule(Modules[i]);
            }
        }
        return Patched;
    }

    void EnableVirtualClock()
    {
        GVirtualClock = true;
    }

    void Advance(double Seconds)
    {
        GOffsetTicks += (long long)std::llround(Seconds * (double)GFrequency);
    }
}
