// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "dacom.h"
#include "render/IRenderPipeline.h"

// Thin wrappers over IRenderPipeline's typed methods, kept for their callers' int / unsigned long
// bridging and the table-walk helpers. Slot numbers live only in the typed vtable
// (render/IRenderPipeline.h); the kPipeline* / kEngine* / kVbMgr* slot constants that used to
// sit here had no remaining users.

inline GENRESULT PipelineSetModelview(IRenderPipeline* Pipeline, const float* Matrix12)
{
    return Pipeline->SetModelview(Matrix12);
}

inline GENRESULT PipelineDrawPrimitiveVb(IRenderPipeline* Pipeline, FlULong Prim, void* Vb,
                                          FlULong StartVertex, FlULong Count,
                                          void* Unused)
{
    return Pipeline->DrawPrimitiveVb(Prim, Vb, static_cast<unsigned int>(StartVertex),
                                     static_cast<unsigned int>(Count), Unused);
}

inline GENRESULT PipelineSetPerspective(IRenderPipeline* Pipeline, float FovDeg, float Aspect,
                                         float NearPlane, float FarPlane)
{
    return Pipeline->SetPerspective(FovDeg, Aspect, NearPlane, FarPlane);
}

inline GENRESULT PipelineSetRenderState(IRenderPipeline* Pipeline, D3DRENDERSTATETYPE State,
                                         FlULong Value)
{
    return Pipeline->SetRenderState(State, (int)Value);
}

// The int / unsigned long bridge is the port's (the declared slot takes int*); retail passes
// the caller's out pointer straight through.
inline GENRESULT PipelineGetRenderState(IRenderPipeline* Pipeline, D3DRENDERSTATETYPE State,
                                         FlULong* Out)
{
    int Value = 0;
    const GENRESULT Result = Pipeline->GetRenderState(State, &Value);
    *Out = static_cast<FlULong>(Value);
    return Result;
}

// No null test: every retail SetTextureStageState site calls [vtable+0xF8] straight through
// the pipeline pointer (Freelancer.exe 0x0050b31c/0x0050b358/0x0050b37a, flmaterials.dll's
// bind paths). Callers own the pipeline's validity, exactly as in retail.
inline GENRESULT PipelineSetTextureStageState(IRenderPipeline* Pipeline, FlULong Stage,
                                               FlULong Type, FlULong Value)
{
    return Pipeline->SetTextureStageState(static_cast<int>(Stage), static_cast<int>(Type),
                                         static_cast<int>(Value));
}

inline GENRESULT PipelineGetTextureStageState(IRenderPipeline* Pipeline, FlULong Stage,
                                               FlULong Type, FlULong* Out)
{
    int Value = 0;
    const GENRESULT Result =
        Pipeline->GetTextureStageState(static_cast<int>(Stage), static_cast<int>(Type), &Value);
    *Out = static_cast<FlULong>(Value);
    return Result;
}

inline GENRESULT PipelineSetTextureStageTexture(IRenderPipeline* Pipeline, FlULong Stage,
                                                 FlULong Handle)
{
    return Pipeline->SetTextureStageTexture(static_cast<int>(Stage),
                                           reinterpret_cast<void*>(Handle));
}

// Slot 64's second argument is the 4x4 UV matrix, not a flags dword: flmaterials.dll
// 0x066f9e3e and 0x067001e3 both push the address of a 0x40-byte stack buffer that the
// preceding MatAnim slot 6 call just filled.
inline GENRESULT PipelineSetTextureStageTransform(IRenderPipeline* Pipeline, FlULong Stage,
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
inline void PipelineApplyRsTable(IRenderPipeline* Pipeline, const FlULong* Table)
{
    FlULong State = Table[0];
    const FlULong* Cursor = Table;
    while (State != 0)
    {
        PipelineSetRenderState(Pipeline, (D3DRENDERSTATETYPE)State, Cursor[1]);
        State = Cursor[2];
        Cursor += 2;
    }
}

inline void PipelineApplyTssTable(IRenderPipeline* Pipeline, FlULong Stage,
                                  const FlULong* Table)
{
    FlULong Type = Table[0];
    const FlULong* Cursor = Table;
    while (Type != 0)
    {
        PipelineSetTextureStageState(Pipeline, Stage, Type, Cursor[1]);
        Type = Cursor[2];
        Cursor += 2;
    }
}
