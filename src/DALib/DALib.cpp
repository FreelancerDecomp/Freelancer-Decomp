// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "DALib.h"
#include "dacom.h"
#include "alchemy.h"
#include "soundmanager.h"
#include "soundstreamer.h"
#include "system.h"

#include <io.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unknwn.h>

namespace
{
    // Retail .text:065c15d0 (FUN_065c15d0) -- cdecl debug print: a NULL format does nothing;
    // otherwise vsprintf into a 0x100-byte stack buffer (unbounded, as retail) and
    // OutputDebugStringA. Only caller: DALib::Startup's DACOM/INI failure path.
    void DebugPrint(const char* Format, ...)
    {
        if (Format == nullptr)
            return;
        char Buffer[0x100];
        va_list Args;
        va_start(Args, Format);
        vsprintf(Buffer, Format, Args);
        va_end(Args);
        OutputDebugStringA(Buffer);
    }

    // SystemContainer uses the extended descriptor; other factories consume its prefix.
    struct FNamedAggDesc
    {
        unsigned long Size;
        const char* Name;
        void* Outer;
        void* InnerOut;
        const char* Extra;
    };

    // Kept across calls for Shutdown's trailing-reference checks.
    unsigned long GLastReleaseCount = 0;
}

// Retail .data:065ca2ac -- DALib's own module handle, stored by DllMain (.text:065c5d4c) and
// passed to GetModuleFileNameA by Startup (.text:065c11b0).
static HMODULE GModule = nullptr;

// Retail .data:065ca0b0 / 065ca0b4 -- both start NULL; the host installs its handlers before
// DALib::Startup (freelancer.exe WinMain .text:005b30e8 / 005b30f4).
FatalErrorFnType FatalErrorFn = nullptr;
CriticalWarningFnType CriticalWarningFn = nullptr;

ICOManager* DALib::Dacom = nullptr;
IEngine* DALib::Engine = nullptr;
IEngine2* DALib::Engine2 = nullptr;
IRenderer2* DALib::Renderer = nullptr;
IAnimation2* DALib::Anim = nullptr;
IHardpoint* DALib::Hardpoint = nullptr;
IDeformable* DALib::Deform = nullptr;
IChannel* DALib::Channel = nullptr;
IRenderPipeline* DALib::Pipeline = nullptr;
IRPDraw* DALib::RPDraw = nullptr;
IRPIndexBuffer* DALib::RPIndexBuffer = nullptr;
IRPVertexBuffer* DALib::RPVertexBuffer = nullptr;
IFileSystem* DALib::Data = nullptr;
ISoundManager* DALib::Sound = nullptr;
IStreamer2* DALib::Streamer = nullptr;
ITextureLibrary2* DALib::TextureLib = nullptr;
IMaterialLibrary* DALib::MaterialLib = nullptr;
IMaterialBatcher2* DALib::MaterialBatcher = nullptr;
IVertexBufferManager* DALib::VertexBuffer = nullptr;
IAlchemy* DALib::Alchemy = nullptr;
IFxRuntime* DALib::FxRuntime = nullptr;
IFxEffectLibrary* DALib::FxEffectLibrary = nullptr;
IMatAnimLibrary* DALib::MatAnimLibrary = nullptr;
IVMeshLibrary* DALib::VMeshLibrary = nullptr;

namespace
{
    // One `if (p) { LastRelease = p->Release(); p = 0; }` block of DALib's teardown
    // (.text:065c16d5.., e.g. Channel .text:065c16f1-1705). The count is stored even when 0.
    template <typename T>
    void ReleaseGlobal(T*& Global)
    {
        if (Global != nullptr)
        {
            GLastReleaseCount = ((IDAComponent*)Global)->Release();
            Global = nullptr;
        }
    }
}

// Retail .text:065c1160 DALib::Startup(HWND, const char*).
bool __cdecl DALib::Startup(HWND Hwnd, const char* IniPath)
{
    Dacom = DACOM_Acquire();
    if (Dacom == nullptr)
    {
        DebugPrint("ERROR: creating DACOM with INI file\n"); // .text:065c15aa
        return false;
    }

    // .text:065c117c-1218: an explicit path goes straight to SetINIConfig; else "DACOM.INI" if
    // it is readable from the cwd; else DACOM.INI next to DALib.dll itself (GModule).
    GENRESULT IniResult;
    if (IniPath != nullptr)
    {
        IniResult = Dacom->SetINIConfig(IniPath, 0);
    }
    else if (_access("DACOM.INI", 4) == 0)
    {
        IniResult = Dacom->SetINIConfig("DACOM.INI", 0);
    }
    else
    {
        char ModulePath[0x104];
        char Drive[0x44];
        char Dir[0x100];
        char IniFile[0x100];
        GetModuleFileNameA(GModule, ModulePath, 0x104);
        _splitpath(ModulePath, Drive, Dir, nullptr, nullptr);
        _makepath(IniFile, Drive, Dir, "DACOM.INI", nullptr);
        IniResult = Dacom->SetINIConfig(IniFile, 0);
    }
    if (IniResult != GR_OK)
    {
        DebugPrint("ERROR: creating DACOM with INI file\n");
        return false;
    }

    // .text:065c1220-12ae. The SystemContainer instance is released only once ISystemContainer
    // was obtained from it; retail leaks it on every earlier failure.
    bool bResult = true; // .text:065c1253 byte [ESP+0x17] = 1
    ISystemContainer* Container = nullptr;
    {
        FNamedAggDesc Desc = {0x14, "SystemContainer", nullptr, nullptr, nullptr};
        IDAComponent* System = nullptr;
        if (Dacom->CreateInstance((AGGDESC*)(&Desc), (void**)(&System)) ==
            GR_OK)
        {
            IAggregateComponent* Aggregate = nullptr;
            if (System->QueryInterface(Iids::AggregateComponent, (void**)(&Aggregate)) ==
                GR_OK)
            {
                const GENRESULT InitResult = Aggregate->Initialize();
                Aggregate->Release();
                if (InitResult == GR_OK &&
                    System->QueryInterface(Iids::SystemContainer, (void**)(&Container)) ==
                        GR_OK)
                {
                    System->Release();
                }
            }
        }
    }

    if (Container != nullptr)
    {
        // .text:065c12bd-13a8: every result lands directly in the exported global.
        Container->LoadSystemComponents();
        Container->QueryInterface(Iids::SoundManager, (void**)(&Sound));
        Container->QueryInterface(Iids::Streamer2, (void**)(&Streamer));
        Container->QueryInterface(Iids::RenderPipeline8B, (void**)(&Pipeline));
        if (Pipeline != nullptr)
        {
            Pipeline->QueryInterface(Iids::RPDraw, (void**)(&RPDraw));
            Pipeline->QueryInterface(Iids::RPIndexBuffer, (void**)(&RPIndexBuffer));
            Pipeline->QueryInterface(Iids::RPVertexBuffer, (void**)(&RPVertexBuffer));
        }
        Container->QueryInterface(Iids::TextureLibrary2, (void**)(&TextureLib));
        Container->QueryInterface(Iids::MaterialLibrary, (void**)(&MaterialLib));
        Container->QueryInterface(Iids::MaterialBatcher2, (void**)(&MaterialBatcher));
        Container->QueryInterface(Iids::VertexBufferManager, (void**)(&VertexBuffer));
        Container->QueryInterface(Iids::Alchemy, (void**)(&Alchemy));
        Container->QueryInterface(Iids::FxRuntime, (void**)(&FxRuntime));
        Container->QueryInterface(Iids::MatAnimLibrary, (void**)(&MatAnimLibrary));
        Container->QueryInterface(Iids::VMeshLibrary, (void**)(&VMeshLibrary));

        // .text:065c13aa-13f5: a sound device that fails to start is shut down and dropped.
        bool bAudioFailed = false;
        if (Sound != nullptr && Sound->StartupDevice(Hwnd, 0x113) != GR_OK)
        {
            bAudioFailed = true;
            Sound->ShutdownDevice();
            ReleaseGlobal(Sound);
        }

        // .text:065c13fa-1489: the streamer gets the sound manager's DirectSound; a streamer
        // whose InitializeDevice returns 0 is dropped. The DirectSound reference is released
        // last, and its count too lands in GLastReleaseCount.
        if (Streamer != nullptr)
        {
            IDirectSound* DirectSound = nullptr;
            if (Sound != nullptr)
                Sound->GetDirectSound(&DirectSound);

            FStreamerInitDesc Desc = {};
            Desc.Size = 0x18;
            Desc.DirectSound = DirectSound;
            Desc.Window = Hwnd;
            if (Streamer->InitializeDevice(&Desc) == 0)
            {
                bAudioFailed = true;
                ReleaseGlobal(Streamer);
            }
            if (DirectSound != nullptr)
                GLastReleaseCount = DirectSound->Release();
        }

        // .text:065c148c-14a0: the host's verdict becomes Startup's return value.
        if (bAudioFailed)
            bResult = CriticalWarningFn(0xd3, nullptr);
    }

    // .text:065c14a4-1553: CreateInstance writes DALib::Engine itself; on success the engine
    // loads its components from the container (NULL when the container failed) and is queried.
    {
        FNamedAggDesc Desc = {8, "Engine", nullptr, nullptr, nullptr};
        if (Dacom->CreateInstance((AGGDESC*)(&Desc), (void**)(&Engine)) ==
            GR_OK)
        {
            Engine->LoadEngineComponents(Container);
            Engine->QueryInterface(Iids::Animation2, (void**)(&Anim));
            Engine->QueryInterface(Iids::Renderer2, (void**)(&Renderer));
            Engine->QueryInterface(Iids::Hardpoint, (void**)(&Hardpoint));
            Engine->QueryInterface(Iids::Deformable, (void**)(&Deform));
            Engine->QueryInterface(Iids::Channel, (void**)(&Channel));
            Engine->QueryInterface(Iids::Engine2, (void**)(&Engine2));
        }
    }

    // .text:065c1555-1590: no Alchemy check -- a NULL Alchemy is passed through.
    {
        FNamedAggDesc Desc = {8, "FxEffectLibrary", nullptr, nullptr, nullptr};
        if (Dacom->CreateInstance((AGGDESC*)(&Desc),
                                  (void**)(&FxEffectLibrary)) == GR_OK)
        {
            FxEffectLibrary->BindAlchemy(Alchemy, Alchemy);
        }
    }

    if (Container != nullptr)
        Container->Release();

    return bResult;
}

// Retail .text:065c1610 DALib::OpenData -- 0x34-byte "FileSystem"/"DOS" descriptor, result
// written straight into DALib::Data; neither Dacom nor the result is checked.
void __cdecl DALib::OpenData(const char* DataPath)
{
    FFileSystemAggDesc Desc = {};
    Desc.Size = 0x34;
    Desc.ComponentName = "FileSystem";
    Desc.BackendName = "DOS";
    Desc.DataPath = DataPath;
    Desc.Field10 = 0x80000000; // GENERIC_READ
    Desc.Field14 = 1;          // FILE_SHARE_READ
    Desc.Field1C = 3;          // OPEN_EXISTING
    Desc.Field20 = 0x08000080;
    Desc.Field30 = 0xFFFFFFFF;
    Dacom->CreateInstance((AGGDESC*)(&Desc), (void**)(&Data));
}

// Retail .text:065c1690 DALib::CloseData.
void __cdecl DALib::CloseData()
{
    ReleaseGlobal(Data);
}

// Retail .text:065c16b0 DALib::Shutdown. DALib::Data is not touched (CloseData is the host's
// job); MatAnimLibrary and VMeshLibrary are neither released nor cleared.
void __cdecl DALib::Shutdown(HWND /*Hwnd*/)
{
    OutputDebugStringA("DALib::Shutdown () ENTER\n");

    if (FxEffectLibrary != nullptr)
        FxEffectLibrary->ClearEffects(); // slot +0x24
    IRenderPipeline* const SavedPipeline = Pipeline; // EBX, .text:065c16d8
    ReleaseGlobal(FxEffectLibrary);
    ReleaseGlobal(Channel);
    ReleaseGlobal(Anim);
    ReleaseGlobal(Renderer);
    ReleaseGlobal(Hardpoint);
    ReleaseGlobal(Deform);
    ReleaseGlobal(Engine2);
    ReleaseGlobal(Engine);

    if (GLastReleaseCount != 0)
    {
        FDUMP(0x100002, "%s(%d) : WARNING:General:%s", "E:\\FL\\Scratch\\Source\\DALib\\DALib.cpp",
              0x14e, "DA ENGINE: trailing references");
    }

    ReleaseGlobal(TextureLib);
    ReleaseGlobal(MaterialBatcher);
    ReleaseGlobal(MaterialLib);

    if (Alchemy != nullptr)
    {
        Alchemy->ClearNodeLibrary();    // slot +0x24
        Alchemy->ReleaseVertexBuffer(); // slot +0x38
        ReleaseGlobal(Alchemy);
    }

    ReleaseGlobal(FxRuntime);

    if (VertexBuffer != nullptr)
    {
        VertexBuffer->VbMgrReleaseAll(); // slot +0x10
        ReleaseGlobal(VertexBuffer);
    }

    ReleaseGlobal(Streamer);

    if (Sound != nullptr)
    {
        Sound->ShutdownDevice(); // slot +0xa8
        ReleaseGlobal(Sound);
    }

    ReleaseGlobal(RPDraw);
    ReleaseGlobal(RPIndexBuffer);
    ReleaseGlobal(RPVertexBuffer);

    if (Pipeline != nullptr)
    {
        Pipeline->ShutdownPipeline(); // slot +0x10
        ReleaseGlobal(Pipeline);
    }

    // .text:065c1956-1991: drain the saved pipeline pointer until a Release returns 0.
    while (GLastReleaseCount != 0)
    {
        FDUMP(0x100002, "%s(%d) : WARNING:General:%s", "E:\\FL\\Scratch\\Source\\DALib\\DALib.cpp",
              0x178, "******* DA SYSTEM: trailing references *******");
        GLastReleaseCount = SavedPipeline->Release();
    }

    if (Dacom != nullptr)
    {
        Dacom->Shutdown(); // slot +0x24
        Dacom = nullptr;
    }

    OutputDebugStringA("DALib::Shutdown () EXIT\n");
}

// Retail .text:065c5d40 DllMain: on DLL_PROCESS_ATTACH (reason 1) stash the module handle in
// .data:065ca2ac and opt out of thread notifications; every reason returns 1.
BOOL WINAPI DllMain(HMODULE Module, DWORD Reason, LPVOID)
{
    if (Reason == DLL_PROCESS_ATTACH)
    {
        GModule = Module;
        DisableThreadLibraryCalls(Module);
    }
    return TRUE;
}
