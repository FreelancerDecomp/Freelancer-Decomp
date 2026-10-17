// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "alchemy_types.h"

// alchemy.dll — DACOM particle FX scaffolding.
//
// FALMaterial: a previously-undocumented component (sonnet:alchemy-wave73-lane2,
// 2026-09-11) discovered by a broad-range survey of retail's 0x062031b0-
// 0x06203410 byte range. Full retail evidence:
//
//   - LAB_06203340 returns the literal string "ALMaterial" (a GetClassName-
//     style getter). The "ALMaterial" string itself has exactly one XREF in
//     the whole module (that getter) -- it is NOT one of the 33 doc-
//     documented RegisterComponent names, so this component is never handed
//     to ICOManager::RegisterComponent the way the rest of alchemy's classes
//     are.
//   - LAB_06203350 is a real byte-verified QueryInterface: a generic 2-entry
//     IID table (string-ptr + offset pairs at 0x0624b3ac/0x0624b3b4) that
//     recognizes "1.11_IBatchedMaterial" (offset 4 -> the facet below) and
//     "1.11_IMaterial" (offset 0 -> the object itself), matching the
//     standard MSVC multiple-inheritance QI-table codegen (adjustor thunks
//     LAB_06203440/50/60 do `this -= 4` then tail-jump into the very same
//     QueryInterface/AddRef/Release used for offset 0).
//   - LAB_06203400/LAB_06203410 are a completely ordinary AddRef/Release
//     pair: refcount lives at this+8, Release frees via FUN_062460e0 (this
//     port's usual `delete this`) once it hits 0, with the same
//     re-entrancy guard (refcount re-armed to 1 immediately before the free
//     call) already fixed across a dozen other alchemy classes this
//     marathon.
//   - Both "1.11_IMaterial" and "1.11_IBatchedMaterial" are real, widely
//     used DACOM interface names -- flmaterials.dll implements the exact
//     same two IID strings dozens of times over for its own FL rendering
//     materials (src/flmaterials/flmaterials.h, render/IMaterial.h). That
//     header is explicitly a flmaterials-local, 29-slot shape though (see
//     its own top-of-file comment: "this fix is local to flmaterials.dll
//     and cannot affect Shading/RendComp's own, differently-shaped IMaterial
//     interfaces"). Alchemy's own vtable1 here is a *smaller*, 21-slot
//     shape (QI/AddRef/Release + 17 unnamed slots + GetClassName) and
//     vtable2 a 9-slot shape (QI/AddRef/Release + 6 unnamed slots) -- a
//     third, alchemy-local shape of the same conceptual interface. No
//     semantic slot names can be derived without a real header for this
//     exact shape (not present anywhere in DALib.h/dacom.h/render/
//     IMaterial.h), so every unnamed slot below keeps its retail byte
//     behavior with a plain SlotN name, matching this port's existing
//     convention for unresolved DACOM interfaces (see e.g. IFxRuntime).
//
// Retail wires this object up unconditionally at DLL load: LAB_062031b0 is
// one of ~20 entries in the CRT's C++ static-object-constructor table
// (DAT_06255000..0625504c, iterated by MSVCRT's `_initterm` from the DLL
// entry point) -- NOT a lazily-triggered singleton the way a prior pass's
// notes described it. It unconditionally stores this object's two vtables
// into the two static fields below and leaves refcount at 1. No other code
// anywhere in alchemy.dll ever reads those two vtable fields again (XREF-
// confirmed -- each has exactly one XREF, the constructor's own write), and
// "ALMaterial" is absent from the 33 documented RegisterComponent names, so
// the real call site that ever queries this instance for "1.11_IMaterial"/
// "1.11_IBatchedMaterial" was not found this wave either. Ported here as a
// real, byte-faithful global singleton (matching retail's unconditional
// construction) so a future wave that does locate the consumer (most likely
// something in Freelancer.exe's renderer or the Alchemy.useMaterialBatcher
// material-batcher path -- G_Defaults.bUseMaterialBatcher in
// alchemy_alchemy.h/.cpp) has a working target to wire in immediately,
// rather than leaving the gap undiscoverable.
namespace AlchemyImpl
{
    class FALMaterial;

    // Retail vtable2 (PTR_LAB_0624b334, "1.11_IBatchedMaterial"): a facet
    // embedded at object+4, exactly the FFxRuntimeAggregate /
    // IAggregateComponent composition pattern already used elsewhere in
    // this port (see alchemy_fxruntime.h) -- QueryInterface/AddRef/Release
    // all delegate to the outer object, matching retail's adjustor thunks
    // (LAB_06203440/50/60: `this -= 4` then jump into the outer's own
    // QI/AddRef/Release).
    struct IALBatchedMaterial : IDAComponent
    {
        virtual GENRESULT DACOM_API Slot3(void* A) = 0;                    // +0x0C LAB_062032a0 RET 8, returns -1
        virtual GENRESULT DACOM_API Slot4(void* A) = 0;                    // +0x10 LAB_062032b0 RET 8, returns -1
        virtual GENRESULT DACOM_API Slot5(unsigned long* Out) = 0;         // +0x14 LAB_062032c0 RET 8, *Out=1, returns 0
        virtual GENRESULT DACOM_API Slot6(void* A) = 0;                    // +0x18 LAB_06203300 RET 8, returns 0
        virtual GENRESULT DACOM_API Slot7(void* A) = 0;                    // +0x1C LAB_06203310 RET 8, returns 0
        virtual GENRESULT DACOM_API Slot8(void* A, void* B, void* C, void* D,
                                          void* E, void* F, void* G) = 0;  // +0x20 LAB_062032f0 RET 0x20, returns 0
    };

    class FALBatchedMaterialFacet final : public IALBatchedMaterial
    {
    public:
        explicit FALBatchedMaterialFacet(FALMaterial* Outer) : Outer_(Outer) {}

        GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
        unsigned long DACOM_API AddRef() override;
        unsigned long DACOM_API Release() override;

        GENRESULT DACOM_API Slot3(void* A) override;
        GENRESULT DACOM_API Slot4(void* A) override;
        GENRESULT DACOM_API Slot5(unsigned long* Out) override;
        GENRESULT DACOM_API Slot6(void* A) override;
        GENRESULT DACOM_API Slot7(void* A) override;
        GENRESULT DACOM_API Slot8(void* A, void* B, void* C, void* D, void* E, void* F,
                                  void* G) override;

    private:
        FALMaterial* Outer_;
    };

    // Retail vtable1 (PTR_LAB_0624b358, "1.11_IMaterial").
    struct IALMaterial : IDAComponent
    {
        virtual GENRESULT DACOM_API Slot3(void* A) = 0;                    // +0x0C LAB_062031d0 RET 8, returns 0
        virtual GENRESULT DACOM_API Slot4(void* A) = 0;                    // +0x10 LAB_062031e0 RET 8, returns 0
        virtual GENRESULT DACOM_API Slot5(void* A) = 0;                    // +0x14 LAB_062031f0 RET 8, returns 0
        virtual GENRESULT DACOM_API Slot6(void* A, void* B) = 0;           // +0x18 LAB_06203200 RET 0xC, returns 0
        virtual GENRESULT DACOM_API Slot7(void* A) = 0;                    // +0x1C LAB_06203210 RET 8, returns 0
        virtual GENRESULT DACOM_API Slot8(void* A) = 0;                    // +0x20 LAB_06203320 RET 8, returns 0
        virtual GENRESULT DACOM_API Slot9(void* A) = 0;                    // +0x24 LAB_06203330 RET 8, returns 0
        virtual GENRESULT DACOM_API Slot10(void* A, void* B, void* C, void* D,
                                           void* E, void* F, void* G) = 0; // +0x28 LAB_062032e0 RET 0x20, returns -1
        virtual GENRESULT DACOM_API Slot11(void* A) = 0;                   // +0x2C LAB_06203220 RET 8, returns -1
        virtual GENRESULT DACOM_API Slot12(void* A, void* B) = 0;          // +0x30 LAB_06203230 RET 0xC, returns -1
        virtual GENRESULT DACOM_API Slot13(void* A, void* B) = 0;          // +0x34 LAB_06203240 RET 0xC, returns -1
        virtual GENRESULT DACOM_API Slot14(unsigned long* Out) = 0;        // +0x38 LAB_06203250 RET 8, *Out=1, returns 0
        virtual GENRESULT DACOM_API Slot15(unsigned long* Out) = 0;        // +0x3C LAB_062032d0 RET 8, *Out=2, returns 0
        virtual GENRESULT DACOM_API Slot16(unsigned long* Out) = 0;        // +0x40 LAB_06203260 RET 8, *Out=0, returns 0
        virtual GENRESULT DACOM_API Slot17(void* A, void* B) = 0;          // +0x44 LAB_06203270 RET 0xC, returns -1
        virtual GENRESULT DACOM_API Slot18(void* A, void* B) = 0;          // +0x48 LAB_06203280 RET 0xC, returns -1
        virtual GENRESULT DACOM_API Slot19(void* A, void* B) = 0;          // +0x4C LAB_06203290 RET 0xC, returns -1
        // +0x50 LAB_06203340 -> "ALMaterial": `MOV EAX,str; RET` -- no stack argument popped,
        // so not a DACOM_API (stdcall, `this` on the stack, RET 4) slot. __thiscall, as the
        // shared IMaterial::ClassName (render/IMaterial.h) declares the same slot.
        virtual const char* __thiscall GetClassName() = 0;
    };

    class FALMaterial final : public IALMaterial
    {
    public:
        FALMaterial() : Facet_(this), RefCount_(1) {}

        FALBatchedMaterialFacet* Facet() { return &Facet_; }

        GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
        unsigned long DACOM_API AddRef() override { return ++RefCount_; }
        unsigned long DACOM_API Release() override;

        GENRESULT DACOM_API Slot3(void* A) override;
        GENRESULT DACOM_API Slot4(void* A) override;
        GENRESULT DACOM_API Slot5(void* A) override;
        GENRESULT DACOM_API Slot6(void* A, void* B) override;
        GENRESULT DACOM_API Slot7(void* A) override;
        GENRESULT DACOM_API Slot8(void* A) override;
        GENRESULT DACOM_API Slot9(void* A) override;
        GENRESULT DACOM_API Slot10(void* A, void* B, void* C, void* D, void* E, void* F,
                                   void* G) override;
        GENRESULT DACOM_API Slot11(void* A) override;
        GENRESULT DACOM_API Slot12(void* A, void* B) override;
        GENRESULT DACOM_API Slot13(void* A, void* B) override;
        GENRESULT DACOM_API Slot14(unsigned long* Out) override;
        GENRESULT DACOM_API Slot15(unsigned long* Out) override;
        GENRESULT DACOM_API Slot16(unsigned long* Out) override;
        GENRESULT DACOM_API Slot17(void* A, void* B) override;
        GENRESULT DACOM_API Slot18(void* A, void* B) override;
        GENRESULT DACOM_API Slot19(void* A, void* B) override;
        const char* __thiscall GetClassName() override;

        // LAB_06203300 / LAB_06203320 call the same render/texture-stage
        // state block as BeginDraw (FUN_0623f450), then return success.
        static GENRESULT ApplyDefaultRenderPipelineState();

    private:
        // Both interface identities share RefCount_; the modern facet keeps
        // its outer pointer instead of requiring the retail binary layout.
        FALBatchedMaterialFacet Facet_;
        unsigned long RefCount_;
    };

    // The one global instance retail's CRT static-initializer table
    // (LAB_062031b0) unconditionally constructs at DLL load. Never queried
    // by any other code in this module (byte-verified: DAT_06257920/24/28
    // each carry exactly one XREF, the constructor's own write) -- kept
    // here, constructed the same way, so a future wave that finds the real
    // consumer can wire it in directly instead of re-deriving this class
    // from scratch.
    extern FALMaterial G_DefaultALMaterial;

} // namespace AlchemyImpl
