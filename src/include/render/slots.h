// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Evidenced vtable slot indices (byte offset / 4). __stdcall, this on the stack.
// Names only from FDUMP / IMaterialImpl.h / domain docs. Unknown stays SlotN.
// Do not guess D3DFMT / D3DRS names. Sources: docs/re/{rp8,shading,rendcomp,flmaterials,ximage,render}.md.

namespace Render
{
    enum EComponentFactorySlot
    {
        ComponentFactoryQueryInterface = 0,
        ComponentFactoryAddRef = 1,
        ComponentFactoryRelease = 2,
        CreateInstance = 3,
        ComponentFactorySlotCount = 4,
    };

    enum EAggregateComponentSlot
    {
        AggregateQueryInterface = 0,
        AggregateAddRef = 1,
        AggregateRelease = 2,
        Initialize = 3,
    };

    enum EEngineComponentSlot
    {
        EngineComponentQueryInterface = 0,
        EngineComponentAddRef = 1,
        EngineComponentRelease = 2,
        EngineComponentInitialize = 3,
        EngineComponentSlot4 = 4,
        EngineComponentSlot5 = 5,
        EngineComponentSlot6 = 6,
        EngineComponentSlot7 = 7,
        EngineComponentSlot8 = 8,
        EngineComponentSlot9 = 9,
        EngineComponentSlot10 = 10,
        EngineComponentSlot11 = 11,
        EngineComponentSlot12 = 12,
    };

    // Consumed by RENDER; implemented in dacom / luaprofile, not here.
    enum EProfileParserSlot
    {
        ProfileParserQueryInterface = 0,
        ProfileParserAddRef = 1,
        ProfileParserRelease = 2,
        ProfileParserSlot3 = 3,
        ProfileParserSlot4 = 4,
        OpenSection = 5, // +0x14; flags 3 ("RenderPipeline", "MaterialMap", …)
        CloseSection = 6, // +0x18
        EnumLine = 7,     // +0x1C
        GetKey = 8,       // +0x20
    };

    // IRenderPipeline — 77 slots. FDUMP names (docs/re/render.md).
    enum ERenderPipeline8BSlot
    {
        PipelineQueryInterface = 0,
        PipelineAddRef = 1,
        PipelineRelease = 2,
        Startup = 3,
        Slot4 = 4, // DALib Shutdown (Pipeline+0x10); no FDUMP name
        Slot5 = 5,
        GetPipelineState = 6,
        GetDeviceInfo = 7,
        QueryDeviceAbility = 8,
        Slot9 = 9,   // FDUMP get_display_mode (adapter mode count)
        Slot10 = 10, // FDUMP get_display_mode (EnumAdapterModes)
        SelectMode = 11,
        CreateBuffers = 12,
        Slot13 = 13,
        Slot14 = 14, // GPU resource teardown; device stays alive
        Clear = 15,  // FDUMP clear / clear_buffers
        SwapBuffers = 16,
        LockBuffer = 17,
        UnlockBuffer = 18,
        GetBufferInterface = 19,
        GetDeviceStats = 20,
        SetViewport = 21,
        GetViewport = 22,
        SetDepthRange = 23,
        GetDepthRange = 24,
        SetWindow = 25,
        GetWindow = 26,
        SetWorld = 27,
        GetWorld = 28,
        SetView = 29,
        Slot30 = 30, // FDUMP get_modelview
        SetModelview = 31,
        Slot32 = 32, // FDUMP get_modelview (second)
        SetProjection = 33,
        GetProjection = 34,
        SetLookat = 35,
        SetOrtho = 36,
        SetPerspective = 37,
        SetLight = 38,
        DestroyLight = 39,
        GetLight = 40,
        SetLightEnable = 41,
        GetLightEnable = 42,
        SetMaterial = 43,
        GetMaterial = 44,
        CreateTexture = 45,
        DestroyTexture = 46,
        IsTexture = 47,
        LockTexture = 48,
        Slot49 = 49, // second lock/unlock; string get_subsurface
        GetTextureFormat = 50,
        GetTextureDim = 51,
        GetTextureInterface = 52,
        SetTextureLevelData = 53,
        BlitTexture = 54,
        SetRenderTarget = 55,
        GetRenderTarget = 56,
        BeginScene = 57,
        EndScene = 58,
        ResetRenderStatesToDefaults = 59,
        SetRenderState = 60,
        GetRenderState = 61,
        SetTextureStageState = 62,
        GetTextureStageState = 63,
        SetTextureStageTransform = 64,
        GetTextureStageTransform = 65,
        SetTextureStageTexture = 66,
        GetTextureStageTexture = 67,
        VerifyState = 68,
        DrawPrimitive = 69,
        DrawIndexedPrimitive = 70,
        DrawPrimitiveVb = 71,
        DrawIndexedPrimitiveVb = 72,
        AddLight = 73,
        RemoveLight = 74,
        UpdateLight = 75,
        SetWorldN = 76,
        PipelineSlotCount = 77,
    };

    // query_device_ability indices (RP8 FUN_06d19530). 0–3, 19, 20 have no strings.
    enum EDeviceAbility
    {
        AbilitySlot0 = 0,
        AbilitySlot1 = 1,
        AbilitySlot2 = 2,
        AbilitySlot3 = 3,
        DeviceGeometry = 4,
        TextureSquareOnly = 5,
        TextureMaxWidth = 6,
        TextureMaxHeight = 7,
        TextureCubemaps = 8,
        DepthBias = 9,
        FogVertex = 10,
        FogPixel = 11,
        FogRange = 12,
        FogW = 13,
        RasterAntialiasEdges = 14,
        DeviceSupportLevel = 15,
        TextureTrilinear = 16,
        DeviceBadMode = 17,
        DeviceBad4444 = 18,
        AbilitySlot19 = 19,
        AbilitySlot20 = 20,
    };

    // IRPDraw — 4 slots. Slot 3 FDUMP draw_indexed_primitive (RET 0x18).
    enum ERpDrawSlot
    {
        RpDrawQueryInterface = 0,
        RpDrawAddRef = 1,
        RpDrawRelease = 2,
        RpDrawIndexedPrimitive = 3,
        RpDrawSlotCount = 4,
    };

    // IVertexBufferManager, IRPVertexBuffer and IRPIndexBuffer used to carry slot-number
    // enums here. They are gone: all three are declared as real interfaces in
    // render/IRenderPipeline.h and every caller in the tree invokes methods, so a second,
    // hand-maintained numbering could only drift away from the first.

    // IRPTexture's slot enum is gone for the same reason: the interface is declared in
    // render/IRenderPipeline.h (slot 6 is GetImageInfo, not a second load_texture).

    // IGammaControl — 8 slots. Strings set_gamma_function / set_gamma_ramp /
    // get_gamma_ramp exist; slot numbers are not evidenced.

    // IMaterial — 29 slots (Shading + flmaterials). C++ name only for initialize / bind.
    enum EMaterialSlot
    {
        MaterialQueryInterface = 0,
        MaterialAddRef = 1,
        MaterialRelease = 2,
        MaterialInitialize = 3, // IMaterialImpl::initialize(outer)
        MaterialSlot4 = 4,      // load properties from IFileSystem
        MaterialSlot5 = 5,
        MaterialSlot6 = 6, // apply/verify wrapper
        MaterialSlot7 = 7, // texture-handle resolve; not unapply
        Bind = 8,
        MaterialSlot9 = 9,   // post-verify / teardown
        MaterialSlot10 = 10, // submit primitives, 7 args, RET 0x20; no C++ name
        MaterialSlot11 = 11,
        MaterialSlot12 = 12,
        MaterialSlot13 = 13, // copy name via slot 20
        MaterialSlot14 = 14,
        MaterialSlot15 = 15,
        MaterialSlot16 = 16, // +0x40 LAB_06eb1b50 writes slot-27 property count
        MaterialSlot17 = 17,
        MaterialSlot18 = 18,
        MaterialSlot19 = 19,
        MaterialSlot20 = 20, // class-name getter
        MaterialSlot21 = 21,
        MaterialSlot22 = 22, // Atmosphere FUN_06eb2b10: Field74 byte = 1
        MaterialSlot23 = 23,
        MaterialSlot24 = 24, // setup before bind
        MaterialSlot25 = 25,
        MaterialSlot26 = 26, // property block
        MaterialSlot27 = 27, // property count
        MaterialSlot28 = 28, // property by index
        MaterialSlotCount = 29,
    };

    // IMaterialLibrary — named roles only; full table size not evidenced.
    enum EMaterialLibrarySlot
    {
        MaterialLibraryQueryInterface = 0,
        MaterialLibraryAddRef = 1,
        MaterialLibraryRelease = 2,
        MaterialLibrarySlot3 = 3,  // +0x0C bind pattern→type RET 0x10 (Deformable2 EyeMaterial)
        MaterialLibrarySlot7 = 7,  // unnamed (CRC map; not boot load)
        MaterialLibrarySlot8 = 8,  // load "Material library" + "Type" + CreateInstance
        MaterialLibrarySlot15 = 15, // find-or-create (flmaterials override)
        MaterialLibrarySlot16 = 16, // +0x40 FindByCrc (query+4), no create
        MaterialLibrarySlot18 = 18, // unnamed (CRC insert-or-replace)
        MaterialLibrarySlot23 = 23, // unnamed (reserve stride-8)
        MaterialLibrarySlot25 = 25, // unnamed (HUDAnim getter)
        MaterialLibrarySlot26 = 26, // CreateInstance after MaterialMap
    };

    // ITextureLibrary2 — slot 6 loads "Texture library"; slot 28 is +0x70. No C++ names.
    enum ETextureLibrarySlot
    {
        TextureLibraryQueryInterface = 0,
        TextureLibraryAddRef = 1,
        TextureLibraryRelease = 2,
        TextureLibrarySlot6 = 6,
        TextureLibrarySlot28 = 28,
    };

    // IMaterialBatcher2 — flush-adjacent slots 9/10; "Flush" is not an FDUMP name.
    enum EMaterialBatcherSlot
    {
        MaterialBatcherQueryInterface = 0,
        MaterialBatcherAddRef = 1,
        MaterialBatcherRelease = 2,
        MaterialBatcherSlot9 = 9,
        MaterialBatcherSlot10 = 10,
    };

    // IRenderer / IRenderer2 — one vtable @ RenderManager+4, 23 slots. Only 0–2 named.
    enum ERendererSlot
    {
        RendererQueryInterface = 0,
        RendererAddRef = 1,
        RendererRelease = 2,
        RendererSlot3 = 3,
        RendererSlot4 = 4,
        RendererSlot5 = 5,
        RendererSlot6 = 6,
        RendererSlot7 = 7, // mesh submit used by exe; no FDUMP name
        RendererSlot8 = 8,
        RendererSlot9 = 9,
        RendererSlot10 = 10,
        RendererSlot11 = 11,
        RendererSlot12 = 12,
        RendererSlot13 = 13,
        RendererSlot14 = 14,
        RendererSlot15 = 15,
        RendererSlot16 = 16,
        RendererSlot17 = 17,
        RendererSlot18 = 18,
        RendererSlot19 = 19,
        RendererSlot20 = 20,
        RendererSlot21 = 21,
        RendererSlot22 = 22,
        RendererSlotCount = 23,
    };

    // IRenderComponent — 18 slots. No official method strings.
    enum ERenderComponentSlot
    {
        RenderComponentQueryInterface = 0,
        RenderComponentAddRef = 1,
        RenderComponentRelease = 2,
        RenderComponentSlot3 = 3, // claim/create from IFileSystem
        RenderComponentSlot4 = 4,
        RenderComponentSlot5 = 5, // destroy instance
        RenderComponentSlot6 = 6,
        RenderComponentSlot7 = 7,
        RenderComponentSlot8 = 8,
        RenderComponentSlot9 = 9,
        RenderComponentSlot10 = 10,
        RenderComponentSlot11 = 11,
        RenderComponentSlot12 = 12,
        RenderComponentSlot13 = 13,
        RenderComponentSlot14 = 14,
        RenderComponentSlot15 = 15,
        RenderComponentSlot16 = 16, // submit/draw-adjacent; name not in binary
        RenderComponentSlot17 = 17,
        RenderComponentSlotCount = 18,
    };

    // IBigImage2 / IBigImage — 12 slots, same this. Names from docs/re/render.md / client.
    enum EBigImage2Slot
    {
        BigImageQueryInterface = 0,
        BigImageAddRef = 1,
        BigImageRelease = 2,
        BigImageSlot3 = 3, // reserved RET 0xC, no xrefs
        Draw = 4,
        BigImageSlot5 = 5, // width/height out
        SetAlpha = 6,
        SetColor = 7,
        BigImageSlot8 = 8, // tile count
        GetTile = 9,
        SetFromBuffer = 10,
        BigImageSlot11 = 11, // UV recalc
        BigImageSlotCount = 12,
    };

    // IFLAppearance — slots 3–13 have no method strings. Exe hits 6–8.
    enum EFlAppearanceSlot
    {
        FlAppearanceQueryInterface = 0,
        FlAppearanceAddRef = 1,
        FlAppearanceRelease = 2,
        FlAppearanceSlot3 = 3,
        FlAppearanceSlot4 = 4,
        FlAppearanceSlot5 = 5,
        FlAppearanceSlot6 = 6,
        FlAppearanceSlot7 = 7,
        FlAppearanceSlot8 = 8,
        FlAppearanceSlot9 = 9,
        FlAppearanceSlot10 = 10,
        FlAppearanceSlot11 = 11,
        FlAppearanceSlot12 = 12,
        FlAppearanceSlot13 = 13,
    };
}
