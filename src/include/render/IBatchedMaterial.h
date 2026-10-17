// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "dacom.h"

// IBatchedMaterial ("1.11_IBatchedMaterial") -- the batched face a material can be asked for.
//
// It lives in its own header rather than inside render/shading.h because flmaterials queries
// for it too: every Candidate dance in flmaterials/MaterialImpl.cpp QIs the material out of
// Context+0x1C for this IID and, on success, binds/tears down/draws through slots 6/7/8
// instead of the material's own 8/9/10. Those sites used to call the slots through raw
// vtable indices, which is how they came to pass one argument too few.

struct IBatchedMaterial : IDAComponent
{
    // +0x0C. LAB_06eb4380 RET 8: face at object byte 4, inner IMaterial at byte 0x10.
    virtual GENRESULT DACOM_API CreateInnerMaterial(void* Name) = 0;
    virtual GENRESULT DACOM_API SetBatchBin(void* Value) = 0; // +0x10 [this+4]=arg (object+8)
    virtual GENRESULT DACOM_API GetBatchBin(void** Out) = 0;  // +0x14 *out=[this+4]
    virtual GENRESULT DACOM_API Bind(void* Context) = 0; // +0x18: JMP inner slot 8 (bind)
    virtual GENRESULT DACOM_API Teardown(void* Context) = 0; // +0x1C: JMP inner slot 9 (teardown)
    virtual GENRESULT DACOM_API Submit(void* Context, unsigned long PrimType,
                                      unsigned long Start, unsigned long Count,
                                      unsigned long IbStart, unsigned long IndexSpan,
                                      unsigned long Flags) = 0; // +0x20: JMP inner slot 10 (submit)
};
