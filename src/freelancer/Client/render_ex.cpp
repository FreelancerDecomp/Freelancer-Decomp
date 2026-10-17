// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Port-only (no retail counterpart): the client side of the Direct3D 9 render extensions.
// See render_ex.h and docs/re/rp9.md, "Ex extensions".
// Compiled only with FREELANCER_RP9 (src/Directory.Build.props).
#ifdef FREELANCER_RP9
#include "render_ex.h"

#include "nebula_volume.h"

#include "DALib.h"
#include "common_ini.h"  // INI_Reader
#include "common_misc.h" // GetFileSysPath
#include "dacom.h"
#include "dacom_iids.h"
#include "globals.h"     // g_display.data_path ([Freelancer] data path)
#include "readfile.h"    // IFileSystem
#include "render/IRenderPipeline.h"
#include "render/IRenderPipelineEx.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <windows.h>

namespace
{
    const char kRenderExSource[] = "render_ex.cpp";
    // The port's own data lives outside the retail DATA tree, in DATA_EXTRA next to it
    // ([Freelancer] data path "..\DATA" -> "..\DATA_EXTRA"), so an install only adds the folder.
    const char kExtraDataDir[] = "DATA_EXTRA";
    // DATA_EXTRA-relative (GExtraData is the FileSystem rooted at that directory).
    const char kVolumetricIni[] = "VOLUMETRIC\\volumetric.ini";
    const char kDebugDepthShader[] = "SHADERS\\debug_depth.hlsl";
    // Any file of the shader directory: resolves the directory's real path for hot reload.
    const char kShaderDirProbe[] = "SHADERS\\common.hlsli";
    // Hot reload: volumetric.ini and DATA_EXTRA/SHADERS/*.hlsl* are polled this often (ms).
    const DWORD kHotReloadPollMs = 1000;

    // DATA_EXTRA/VOLUMETRIC/volumetric.ini: [Debug] here; [Nebula] / [NebulaOverride] are read by
    // NebulaVolume::LoadSettings (nebula_volume.cpp).
    struct FRenderExSettings
    {
        bool bSceneDepthView;     // [Debug] scene_depth_view
        float SceneDepthViewMax;  // [Debug] scene_depth_view_max: 0 = log near..far, else metres
    };

    IRenderPipelineEx* GPipelineEx = nullptr;
    IRPDrawEx* GDrawEx = nullptr;
    IFileSystem* GExtraData = nullptr;
    FRenderExSettings GSettings = {false, 0.0f};
    FRPExProgram* GDepthProgram = nullptr;
    bool GbDepthProgramTried = false;
    bool GbNoSceneDepthReported = false;
    // GetExCaps answers only once the device exists (after RenderEx::Startup): queried lazily,
    // cached and logged once.
    FRPExCaps GCaps;
    bool GbCapsValid = false;

    // Hot reload. The real paths are resolved once at Startup (GetFileSysPath through
    // GExtraData, as LoadSettings resolves volumetric.ini); the poll then only asks the file
    // system for write times: one GetFileAttributesEx for the ini, one directory enumeration
    // (FindFirstFile / FindNextFile, which return each file's write time) for the shaders.
    char GIniPath[MAX_PATH] = {};
    char GShaderPattern[MAX_PATH] = {}; // <DATA_EXTRA>\SHADERS\*.hlsl* (.hlsl and .hlsli)
    DWORD GLastPollTick = 0;
    unsigned long long GIniStamp = 0;
    unsigned long long GShaderStamp = 0;

    // "<parent of the data path>DATA_EXTRA": "..\DATA" -> "..\DATA_EXTRA",
    // "D:\Games\FL\DATA\" -> "D:\Games\FL\DATA_EXTRA", "DATA" -> "DATA_EXTRA".
    void BuildExtraDataPath(char* Out, size_t OutSize)
    {
        const char* const DataPath = g_display.data_path.c_str();
        size_t Length = strlen(DataPath);
        while (Length > 0 && (DataPath[Length - 1] == '\\' || DataPath[Length - 1] == '/'))
            --Length;
        size_t ParentLength = Length;
        while (ParentLength > 0 && DataPath[ParentLength - 1] != '\\' &&
               DataPath[ParentLength - 1] != '/' && DataPath[ParentLength - 1] != ':')
            --ParentLength;
        _snprintf(Out, OutSize, "%.*s%s", (int)(ParentLength), DataPath, kExtraDataDir);
        Out[OutSize - 1] = '\0';
    }

    // The DOS FileSystem over DATA_EXTRA, described like DALib::OpenData's over DATA.
    IFileSystem* OpenExtraData(const char* Path)
    {
        FFileSystemAggDesc Desc = {};
        Desc.Size = 0x34;
        Desc.ComponentName = "FileSystem";
        Desc.BackendName = "DOS";
        Desc.DataPath = Path;
        Desc.Field10 = 0x80000000; // GENERIC_READ
        Desc.Field14 = 1;          // FILE_SHARE_READ
        Desc.Field1C = 3;          // OPEN_EXISTING
        Desc.Field20 = 0x08000080;
        Desc.Field30 = 0xFFFFFFFF;
        IFileSystem* FileSystem = nullptr;
        if (DALib::Dacom == nullptr ||
            DALib::Dacom->CreateInstance((AGGDESC*)(&Desc), (void**)(&FileSystem)) != GR_OK)
            return nullptr;
        return FileSystem;
    }

    void LoadSettings()
    {
        GSettings.bSceneDepthView = false;
        GSettings.SceneDepthViewMax = 0.0f;
        char Path[MAX_PATH] = {};
        GetFileSysPath(Path, kVolumetricIni, GExtraData);
        INI_Reader Reader;
        if (!Reader.open(Path, false))
        {
            FDUMP(0x100002, "%s(%d) : WARNING:General:%s not found, Ex defaults used",
                  kRenderExSource, __LINE__, kVolumetricIni);
            NebulaVolume::LoadSettings(nullptr);
            return;
        }
        while (Reader.read_header())
        {
            if (!Reader.is_header("Debug"))
                continue;
            while (Reader.read_value())
            {
                if (Reader.is_value("scene_depth_view"))
                    GSettings.bSceneDepthView = Reader.get_value_bool(0);
                else if (Reader.is_value("scene_depth_view_max"))
                    GSettings.SceneDepthViewMax = (float)(Reader.get_value_float(0));
            }
        }
        Reader.close();
        NebulaVolume::LoadSettings(Path);
    }

    unsigned long long FileTimeValue(FILETIME const& Time)
    {
        return ((unsigned long long)(Time.dwHighDateTime) << 32) | Time.dwLowDateTime;
    }

    void ResolveHotReloadPaths()
    {
        GetFileSysPath(GIniPath, kVolumetricIni, GExtraData);
        GShaderPattern[0] = '\0';
        char Probe[MAX_PATH] = {};
        GetFileSysPath(Probe, kShaderDirProbe, GExtraData);
        char* Slash = strrchr(Probe, '\\');
        char* const ForwardSlash = strrchr(Probe, '/');
        if (ForwardSlash != nullptr && (Slash == nullptr || ForwardSlash > Slash))
            Slash = ForwardSlash;
        const char kMask[] = "*.hlsl*";
        if (Slash != nullptr && (size_t)(Slash + 1 - Probe) + sizeof(kMask) <= sizeof(Probe))
        {
            memcpy(Slash + 1, kMask, sizeof(kMask));
            memcpy(GShaderPattern, Probe, sizeof(GShaderPattern));
        }
    }

    // volumetric.ini's last-write time (0 when missing).
    unsigned long long ComputeIniStamp()
    {
        WIN32_FILE_ATTRIBUTE_DATA Attributes;
        if (GIniPath[0] == '\0' ||
            !GetFileAttributesExA(GIniPath, GetFileExInfoStandard, &Attributes))
            return 0;
        return FileTimeValue(Attributes.ftLastWriteTime);
    }

    // The newest last-write time among DATA_EXTRA/SHADERS/*.hlsl*, mixed with the file count so an
    // added or removed file counts as a change too.
    unsigned long long ComputeShaderStamp()
    {
        if (GShaderPattern[0] == '\0')
            return 0;
        unsigned long long Newest = 0;
        unsigned long long Count = 0;
        WIN32_FIND_DATAA Found;
        HANDLE const Find = FindFirstFileA(GShaderPattern, &Found);
        if (Find != INVALID_HANDLE_VALUE)
        {
            do
            {
                const unsigned long long Written = FileTimeValue(Found.ftLastWriteTime);
                if (Written > Newest)
                    Newest = Written;
                ++Count;
            } while (FindNextFileA(Find, &Found));
            FindClose(Find);
        }
        return Newest ^ (Count << 56);
    }

    // Once a second (wall clock). volumetric.ini changed: [Debug] and the nebula volume
    // settings are re-read and apply from the next frame. A shader changed: every Ex program is
    // recompiled (RP9 keeps the old code of one that fails and logs the compiler errors).
    void PollHotReload()
    {
        const DWORD Now = GetTickCount();
        if (Now - GLastPollTick < kHotReloadPollMs)
            return;
        GLastPollTick = Now;

        const unsigned long long IniStamp = ComputeIniStamp();
        if (IniStamp != GIniStamp)
        {
            GIniStamp = IniStamp;
            LoadSettings();
            FDUMP(0x100003, "%s(%d) : NOTICE:General:hot reload: %s re-read (scene_depth_view %d)",
                  kRenderExSource, __LINE__, kVolumetricIni, GSettings.bSceneDepthView ? 1 : 0);
        }

        const unsigned long long ShaderStamp = ComputeShaderStamp();
        if (ShaderStamp != GShaderStamp)
        {
            GShaderStamp = ShaderStamp;
            const GENRESULT Result = GPipelineEx->ReloadExPrograms();
            NebulaVolume::OnProgramsReloaded();
            FDUMP(0x100003, "%s(%d) : NOTICE:General:hot reload: SHADERS changed, Ex programs "
                  "recompiled (%s)", kRenderExSource, __LINE__,
                  Result == GR_OK ? "all compiled" : "errors above, old code kept");
        }
    }

    void LogCaps(FRPExCaps const& Caps)
    {
        FDUMP(0x100003, "%s(%d) : NOTICE:General:Ex caps: adapter \"%s\", vs %lu.%lu (%lu slots), "
              "ps %lu.%lu (%lu slots, %lu executed), INTZ scene depth %d, volume textures %d "
              "(max %lu), FP16 target %d, FP16 blending %d, R32F target %d",
              kRenderExSource, __LINE__, Caps.AdapterDescription, Caps.VertexShaderVersion >> 8,
              Caps.VertexShaderVersion & 0xff, Caps.MaxVertexShaderSlots,
              Caps.PixelShaderVersion >> 8, Caps.PixelShaderVersion & 0xff,
              Caps.MaxPixelShaderSlots, Caps.MaxPixelShaderInstructionsExecuted,
              (int)(Caps.bSceneDepth), (int)(Caps.bVolumeTextures), Caps.MaxVolumeExtent,
              (int)(Caps.bFloat16Target), (int)(Caps.bFloat16Blending),
              (int)(Caps.bFloat32Target));
    }

    // View-space distance of a hardware depth value through the projection's z row: for an
    // on-axis point d = (A z + B) / (C z + D), so z = (B - d D) / (d C - A). Works for any
    // perspective or orthographic projection whose z/w rows ignore x and y.
    float LinearDepth(const float* ProjZ, float DeviceDepth)
    {
        const float Denominator = DeviceDepth * ProjZ[2] - ProjZ[0];
        if (Denominator == 0.0f)
            return 0.0f;
        return std::fabs((ProjZ[1] - DeviceDepth * ProjZ[3]) / Denominator);
    }

    // [Debug] scene_depth_view: the INTZ scene depth, linearised, as greyscale over the back
    // buffer (DATA_EXTRA/SHADERS/debug_depth.hlsl).
    void DrawSceneDepthView()
    {
        IRenderPipelineEx* const Ex = GPipelineEx;
        FRPExTexture* SceneDepth = nullptr;
        if (Ex->GetSceneDepth(&SceneDepth) != GR_OK)
        {
            if (!GbNoSceneDepthReported)
            {
                GbNoSceneDepthReported = true;
                FDUMP(0x100002, "%s(%d) : WARNING:General:scene_depth_view: the device has no "
                      "INTZ scene depth", kRenderExSource, __LINE__);
            }
            return;
        }
        if (!GbDepthProgramTried)
        {
            // A failed compile still returns a handle (errors logged by RP9); it binds once a
            // ReloadExPrograms compiles it.
            GbDepthProgramTried = true;
            Ex->CreateExProgram(GExtraData, kDebugDepthShader, "VSMain", "PSMain",
                                &GDepthProgram);
        }
        if (GDepthProgram == nullptr)
            return;

        // The projection the scene was drawn with. GetProjection returns the transpose of the
        // device matrix: row 2 is the z_clip row, row 3 the w row.
        float Projection[16];
        if (DALib::Pipeline->GetProjection(Projection) != GR_OK)
            return;
        const float ProjZ[4] = {Projection[10], Projection[11], Projection[14], Projection[15]};
        const float DepthView[4] = {LinearDepth(ProjZ, 0.0f), LinearDepth(ProjZ, 1.0f),
                                    GSettings.SceneDepthViewMax > 0.0f ? 1.0f : 0.0f,
                                    GSettings.SceneDepthViewMax};

        FRPExPassDesc Pass;
        Pass.ColorTarget = nullptr; // the back buffer
        Pass.Flags = 0;             // no depth surface: the scene depth is sampled
        Pass.ClearColor = 0;
        if (Ex->BeginExPass(&Pass) != GR_OK)
            return;
        if (Ex->BindExProgram(GDepthProgram) == GR_OK)
        {
            FRPExSamplerState Point;
            Point.MinMagFilter = RPExFilterPoint;
            Point.MipFilter = RPExMipNone;
            Point.AddressU = RPExAddressClamp;
            Point.AddressV = RPExAddressClamp;
            Point.AddressW = RPExAddressClamp;
            if (Ex->BindExTexture(0, SceneDepth, &Point) == GR_OK)
            {
                Ex->SetExPixelConstants(0, ProjZ, 1);
                Ex->SetExPixelConstants(1, DepthView, 1);
                GDrawEx->DrawFullscreenQuad();
            }
        }
        Ex->EndExPass();
    }
}

void RenderEx::Startup()
{
    if (DALib::Pipeline == nullptr)
        return;
    IRenderPipelineEx* PipelineEx = nullptr;
    if (DALib::Pipeline->QueryInterface(Iids::RenderPipelineEx, (void**)(&PipelineEx)) != GR_OK ||
        PipelineEx == nullptr)
    {
        // DirectX8 (RP8.dll): the retail rendering path, nothing else to do.
        FDUMP(0x100003, "%s(%d) : NOTICE:General:render pipeline has no Direct3D 9 extensions",
              kRenderExSource, __LINE__);
        return;
    }
    IRPDrawEx* DrawEx = nullptr;
    if (DALib::Pipeline->QueryInterface(Iids::RPDrawEx, (void**)(&DrawEx)) != GR_OK ||
        DrawEx == nullptr)
    {
        PipelineEx->Release();
        FDUMP(0x100002, "%s(%d) : WARNING:General:IRenderPipelineEx without IRPDrawEx",
              kRenderExSource, __LINE__);
        return;
    }
    // Shaders and volumetric.ini come from DATA_EXTRA; without it there is nothing to draw the
    // extensions with, so they stay off and every retail path runs.
    char ExtraPath[MAX_PATH] = {};
    BuildExtraDataPath(ExtraPath, sizeof(ExtraPath));
    const DWORD ExtraAttributes = GetFileAttributesA(ExtraPath);
    IFileSystem* const ExtraData =
        (ExtraAttributes != INVALID_FILE_ATTRIBUTES &&
         (ExtraAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
            ? OpenExtraData(ExtraPath)
            : nullptr;
    if (ExtraData == nullptr)
    {
        DrawEx->Release();
        PipelineEx->Release();
        FDUMP(0x100001, "%s(%d) : *** ERROR: could not find the %s directory (%s); Direct3D 9 "
              "extensions off", kRenderExSource, __LINE__, kExtraDataDir, ExtraPath);
        return;
    }
    GExtraData = ExtraData;
    GPipelineEx = PipelineEx;
    GDrawEx = DrawEx;
    LoadSettings();
    ResolveHotReloadPaths();
    GLastPollTick = GetTickCount();
    GIniStamp = ComputeIniStamp();
    GShaderStamp = ComputeShaderStamp();
    FDUMP(0x100003, "%s(%d) : NOTICE:General:Direct3D 9 extensions available "
          "(scene_depth_view %d)", kRenderExSource, __LINE__, GSettings.bSceneDepthView ? 1 : 0);
    // Usually there is no device yet; then the caps are logged by the first GetCaps that
    // finds one (AfterSpaceScene, NebulaVolume::BeginFrame).
    FRPExCaps Caps;
    GetCaps(&Caps);
}

void RenderEx::Shutdown()
{
    NebulaVolume::Shutdown();
    if (GPipelineEx != nullptr && GDepthProgram != nullptr)
        GPipelineEx->DestroyExProgram(GDepthProgram);
    GDepthProgram = nullptr;
    GbDepthProgramTried = false;
    GbCapsValid = false;
    if (GDrawEx != nullptr)
    {
        GDrawEx->Release();
        GDrawEx = nullptr;
    }
    if (GPipelineEx != nullptr)
    {
        GPipelineEx->Release();
        GPipelineEx = nullptr;
    }
    // After the programs: each one holds a reference to the FileSystem it was compiled from.
    if (GExtraData != nullptr)
    {
        GExtraData->Release();
        GExtraData = nullptr;
    }
}

IFileSystem* RenderEx::GetExtraData()
{
    return GExtraData;
}

bool RenderEx::IsAvailable()
{
    return GPipelineEx != nullptr && GDrawEx != nullptr;
}

IRenderPipelineEx* RenderEx::GetPipelineEx()
{
    return GPipelineEx;
}

IRPDrawEx* RenderEx::GetDrawEx()
{
    return GDrawEx;
}

bool RenderEx::GetCaps(FRPExCaps* Out)
{
    if (Out == nullptr || GPipelineEx == nullptr)
        return false;
    if (!GbCapsValid)
    {
        memset(&GCaps, 0, sizeof(GCaps));
        GCaps.Size = sizeof(FRPExCaps);
        if (GPipelineEx->GetExCaps(&GCaps) != GR_OK)
            return false; // no device yet: asked again next time
        GbCapsValid = true;
        LogCaps(GCaps);
    }
    *Out = GCaps;
    return true;
}

void RenderEx::AfterSpaceScene()
{
    if (!IsAvailable())
        return;
    if (!GbCapsValid)
    {
        FRPExCaps Caps;
        GetCaps(&Caps);
    }
    PollHotReload();
    if (GSettings.bSceneDepthView)
        DrawSceneDepthView();
}

#endif // FREELANCER_RP9
