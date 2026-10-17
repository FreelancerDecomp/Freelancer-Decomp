// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Port-only: the client side of the Direct3D 9 render extensions (render/IRenderPipelineEx.h,
// RP9.dll). Retail has no such code. With the DirectX8 pipeline (RP8.dll, the retail one) the
// QueryInterface fails, IsAvailable() stays false and every hook below returns at once, so the
// retail algorithms run unchanged. Settings: DATA_EXTRA/VOLUMETRIC/volumetric.ini; shaders:
// DATA_EXTRA/SHADERS/ (DATA_EXTRA sits next to the [Freelancer] data path directory). See
// docs/re/rp9.md, "Ex extensions".

struct FRPExCaps;
struct IFileSystem;
struct IRenderPipelineEx;
struct IRPDrawEx;

namespace RenderEx
{
    // After DALib::Startup and the pipeline's Startup: QueryInterface the Ex interfaces and read
    // DATA_EXTRA/VOLUMETRIC/volumetric.ini.
    void Startup();
    // Before DALib::Shutdown: destroys the client's Ex objects (NebulaVolume's too) and
    // releases the interfaces.
    void Shutdown();

    // True when the pipeline answered both Ex interfaces (Direct3D 9) and DATA_EXTRA was found.
    bool IsAvailable();
    // The FileSystem rooted at DATA_EXTRA (the port's own data next to the retail DATA:
    // SHADERS\, VOLUMETRIC\); nullptr unless IsAvailable().
    IFileSystem* GetExtraData();
    IRenderPipelineEx* GetPipelineEx();
    IRPDrawEx* GetDrawEx();
    // The device's Ex caps (full current FRPExCaps). False until a device exists; the first
    // successful query is cached and logged once (adapter, shader versions and slot budgets,
    // INTZ, FP16 targets / blending).
    bool GetCaps(FRPExCaps* Out);

    // The space frame, after the 3D scene and before the HUD clears the depth buffer and draws
    // the 2D interface (SpaceCameraRenderSlot78). Hot reload: once a second (wall clock) checks
    // volumetric.ini (changed -> [Debug] and the nebula volume settings re-read) and
    // DATA_EXTRA/SHADERS/*.hlsl* (changed -> every Ex program recompiled); then runs the [Debug]
    // scene_depth_view pass. (The volumetric nebula is drawn earlier, inside SpaceObjDrawPass --
    // nebula_volume.h.)
    void AfterSpaceScene();
}
