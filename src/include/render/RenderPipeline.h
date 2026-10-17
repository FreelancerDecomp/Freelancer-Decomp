// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "dacom.h"
#include "render/IRenderPipeline.h"

// Pipeline / library slot numbers from docs/re/render.md and docs/re/shading.md.
// Call through the live vtable. Do not invent C++ method names for unnamed slots.

static const int kPipelineQueryDeviceAbility = 8;     // +0x20
static const int kPipelineSetModelview = 31;          // +0x7C
static const int kPipelineSetPerspective = 37;        // +0x94
static const int kPipelineSetMaterial = 43;           // +0xAC
static const int kPipelineGetMaterial = 44;           // +0xB0
static const int kPipelineGetRenderState = 61;        // +0xF4
static const int kPipelineSetRenderState = 60;        // +0xF0
static const int kPipelineSetTextureStageState = 62;  // +0xF8
static const int kPipelineGetTextureStageState = 63;  // +0xFC (EnvMap: unnamed counterpart to
                                                       // SetTextureStageState +0xF8, used by
                                                       // LAB_066F5AF0 to probe which of stage
                                                       // 0/1 already carries TSS type 1 == 1
                                                       // before picking the stage to apply the
                                                       // dynamic envmap TSS pair at)
static const int kPipelineSetTextureStageTransform = 64; // +0x100
static const int kPipelineSetTextureStageTexture = 66;   // +0x108
static const int kPipelineVerifyState = 68;           // +0x110
static const int kPipelineDrawPrimitiveVb = 71;       // +0x11C
static const int kPipelineDrawIndexedPrimitiveVb = 72; // +0x120
// The two 3DMathEngine slot numbers that used to live here are gone: that component is not
// part of the render pipeline and its interface is declared in src/include/x86math.h.
static const int kVbMgrLockVertexMemory = 5;          // +0x14
static const int kVbMgrUnlockVertexMemory = 6;        // +0x18
static const int kVbMgrFillVertexMemory = 8;          // +0x20 (VertexBufferManager's
                                                       // OWN slot 8 -- a DIFFERENT interface than
                                                       // Pipeline's slot 8/kPipelineQueryDeviceAbility
                                                       // despite the same slot number. Confirmed via
                                                       // a full manual push-by-push trace of
                                                       // FlmFillMeshAndDraw (flmaterials.dll.txt
                                                       // 14766-14816) and the byte-identical
                                                       // Exclusion/Glass Slot10 fallbacks: takes
                                                       // (VbMgr, DestBase, Fvf, Mesh, Start, Count)
                                                       // and has the VB manager itself walk the
                                                       // Mesh's own vertex arrays into the locked
                                                       // buffer -- replaces the earlier NomadCopyMesh
                                                       // CPU-fill stand-in these call sites used.
static const int kEngineGetInstanceTransform = 40;    // +0xA0
static const int kEngineGetInstanceHandle = 25;       // +0x64
static const int kEngineReleaseHandle = 15;           // +0x3C
// CORRECTED 2026-09-17 (cross-DLL ABI pass 40). Slot 49 is IEngine::GetInstanceRelative, not
// a parent getter -- IEngine's own published table (src/include/engine/engbase.h) puts
// GetInstanceParent at slot 53, and this constant's own comment below already describes the
// relative getter's body: EngBase's EngineGetInstanceRelative is exactly
// `parent == nullptr ? nullptr : Instance->Relative`. The old name said "GetParent".
static const int kEngineGetInstanceRelative = 49;     // +0xC4 (parent ? inst+0x5C : 0)
static const int kEngineGetRoot = 55;                 // +0xDC (root via +0x10)
static const int kEngineQueryInstance = 30;           // +0x78 (src/include/engine/engbase.h:66;
                                                       // IEngine->QueryInstance(
                                                       // inst, iid, &out); flmaterials Wire's own Slot10
                                                       // calls this to fetch IVMeshWire/IMesh off
                                                       // G_FlmEngineOuter)
static const int kRendererGetExtents = 4;             // +0x10

static const int kMaterialLibraryFindOrCreate = 15; // +0x3C
static const int kMaterialLibrarySlot18 = 18;       // +0x48
static const int kMaterialLibrarySlot23 = 23;       // +0x5C
static const int kMaterialLibrarySlot25 = 25;       // +0x64

static const int kMatAnimLibrarySlot5 = 5; // +0x14

static const int kBatchedSlot6 = 6;  // +0x18
static const int kBatchedSlot7 = 7;  // +0x1C (Shadow's
                                      // own Slot9, LAB_066fa530 — called on the QI'd
                                      // IBatchedMaterial on success; a different slot than
                                      // kBatchedSlot6, used by Backdrop/Cloak's own Candidate
                                      // dance — confirms per-caller slot choice, not a fixed one)
static const int kBatchedSlot8 = 8;  // +0x20
static const int kBatchedSlot17 = 17; // +0x44
static const int kBatchedSlot18 = 18; // +0x48

// The pipeline is IRenderPipeline (rp8.dll), so these are ordinary virtual calls now --
// the kPipeline* slot numbers above are kept only as the documented offset map, and the
// arity of each one is whatever src/include/render/IRenderPipeline.h declares.
inline GENRESULT PipelineQueryDeviceAbility(IRenderPipeline* Pipeline, unsigned long Index,
                                            unsigned long* Out)
{
    return Pipeline->QueryDeviceAbility(static_cast<unsigned int>(Index), Out);
}

inline GENRESULT PipelineSetModelview(IRenderPipeline* Pipeline, const float* Matrix12)
{
    return Pipeline->SetModelview(Matrix12);
}

inline GENRESULT PipelineDrawPrimitiveVb(IRenderPipeline* Pipeline, unsigned long Prim, void* Vb,
                                          unsigned long StartVertex, unsigned long Count,
                                          void* Unused)
{
    return Pipeline->DrawPrimitiveVb(Prim, Vb, static_cast<unsigned int>(StartVertex),
                                     static_cast<unsigned int>(Count), Unused);
}

// Slot 72 takes SEVEN arguments, not six: FUN_06d10e4b ends in RET 0x20, so the callee pops
// eight dwords, and every retail caller pushes eight (shading.dll 0x06eb328a, 0x06eb3c0f,
// 0x06eb5915, 0x06eba8ff all open with PUSH 0x0). This wrapper used to push seven, leaving
// the __stdcall stack four bytes short on every indexed draw that went through it.
inline GENRESULT PipelineDrawIndexedPrimitiveVb(IRenderPipeline* Pipeline, unsigned long Prim,
                                                 void* Vb, unsigned long StartVertex,
                                                 unsigned long VertCount, void* Indices,
                                                 unsigned long IndexCount)
{
    return Pipeline->DrawIndexedPrimitiveVb(Prim, Vb, static_cast<unsigned int>(StartVertex),
                                            static_cast<unsigned int>(VertCount), Indices,
                                            static_cast<unsigned int>(IndexCount), 0);
}

inline GENRESULT PipelineSetPerspective(IRenderPipeline* Pipeline, float FovDeg, float Aspect,
                                         float NearPlane, float FarPlane)
{
    return Pipeline->SetPerspective(FovDeg, Aspect, NearPlane, FarPlane);
}

inline GENRESULT PipelineSetMaterial(IRenderPipeline* Pipeline, const D3DMATERIAL8* Block)
{
    return Pipeline->SetMaterial(Block);
}

inline GENRESULT PipelineGetMaterial(IRenderPipeline* Pipeline, D3DMATERIAL8* Block)
{
    return Pipeline->GetMaterial(Block);
}

inline GENRESULT PipelineSetRenderState(IRenderPipeline* Pipeline, D3DRENDERSTATETYPE State,
                                         unsigned long Value)
{
    return Pipeline->SetRenderState(State, (int)Value);
}

// The int / unsigned long bridge is the port's (the declared slot takes int*); retail passes
// the caller's out pointer straight through.
inline GENRESULT PipelineGetRenderState(IRenderPipeline* Pipeline, D3DRENDERSTATETYPE State,
                                         unsigned long* Out)
{
    int Value = 0;
    const GENRESULT Result = Pipeline->GetRenderState(State, &Value);
    *Out = static_cast<unsigned long>(Value);
    return Result;
}

// No null test: every retail SetTextureStageState site calls [vtable+0xF8] straight through
// the pipeline pointer (Freelancer.exe 0x0050b31c/0x0050b358/0x0050b37a, flmaterials.dll's
// bind paths). Callers own the pipeline's validity, exactly as in retail.
inline GENRESULT PipelineSetTextureStageState(IRenderPipeline* Pipeline, unsigned long Stage,
                                               unsigned long Type, unsigned long Value)
{
    return Pipeline->SetTextureStageState(static_cast<int>(Stage), static_cast<int>(Type),
                                         static_cast<int>(Value));
}

inline GENRESULT PipelineGetTextureStageState(IRenderPipeline* Pipeline, unsigned long Stage,
                                               unsigned long Type, unsigned long* Out)
{
    int Value = 0;
    const GENRESULT Result =
        Pipeline->GetTextureStageState(static_cast<int>(Stage), static_cast<int>(Type), &Value);
    *Out = static_cast<unsigned long>(Value);
    return Result;
}

inline GENRESULT PipelineSetTextureStageTexture(IRenderPipeline* Pipeline, unsigned long Stage,
                                                 unsigned long Handle)
{
    return Pipeline->SetTextureStageTexture(static_cast<int>(Stage),
                                           reinterpret_cast<void*>(Handle));
}

// Slot 64's second argument is the 4x4 UV matrix, not a flags dword: flmaterials.dll
// 0x066f9e3e and 0x067001e3 both push the address of a 0x40-byte stack buffer that the
// preceding MatAnim slot 6 call just filled.
inline GENRESULT PipelineSetTextureStageTransform(IRenderPipeline* Pipeline, unsigned long Stage,
                                                   const float* Matrix16)
{
    return Pipeline->SetTextureStageTransform(static_cast<int>(Stage), Matrix16);
}

inline GENRESULT PipelineVerifyState(IRenderPipeline* Pipeline)
{
    return Pipeline->VerifyState();
}

// RS/TSS tables: first dword is the start state/type (not consumed as a value).
// Then [+4]=value, [+8]=next, stride 8, stop when next==0.
// Retail inlines this loop at every site (Freelancer.exe 0x0050b366..0x0050b388) with no test
// of the pipeline or the table pointer; the tables are constants in .rdata.
inline void PipelineApplyRsTable(IRenderPipeline* Pipeline, const unsigned long* Table)
{
    unsigned long State = Table[0];
    const unsigned long* Cursor = Table;
    while (State != 0)
    {
        PipelineSetRenderState(Pipeline, (D3DRENDERSTATETYPE)State, Cursor[1]);
        State = Cursor[2];
        Cursor += 2;
    }
}

inline void PipelineApplyTssTable(IRenderPipeline* Pipeline, unsigned long Stage,
                                  const unsigned long* Table)
{
    unsigned long Type = Table[0];
    const unsigned long* Cursor = Table;
    while (Type != 0)
    {
        PipelineSetTextureStageState(Pipeline, Stage, Type, Cursor[1]);
        Type = Cursor[2];
        Cursor += 2;
    }
}
