// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "alchemy_material.h"

#include "alchemy_alchemy.h"
#include "alchemy_common.h"

// alchemy.dll — DACOM particle FX scaffolding.
// Out-of-line method bodies for FALMaterial / FALBatchedMaterialFacet.
// See alchemy_material.h for the full byte-verified retail trace.
namespace AlchemyImpl
{
    FALMaterial G_DefaultALMaterial;

    // ---- FALBatchedMaterialFacet ("1.11_IBatchedMaterial", object+4) -----

    GENRESULT DACOM_API FALBatchedMaterialFacet::QueryInterface(const char* Iid, void** Out)
    {
        // LAB_06203440: `this -= 4` then tail-jump into LAB_06203350, i.e.
        // the outer object's own QueryInterface handles both IIDs.
        return Outer_->QueryInterface(Iid, Out);
    }

    unsigned long DACOM_API FALBatchedMaterialFacet::AddRef()
    {
        // LAB_06203450: `this -= 4` then tail-jump into LAB_06203400.
        return Outer_->AddRef();
    }

    unsigned long DACOM_API FALBatchedMaterialFacet::Release()
    {
        // LAB_06203460: `this -= 4` then tail-jump into LAB_06203410.
        return Outer_->Release();
    }

    GENRESULT DACOM_API FALBatchedMaterialFacet::Slot3(void* /*A*/)
    {
        // LAB_062032a0: OR EAX,0xFFFFFFFF; RET 8.
        return -1;
    }

    GENRESULT DACOM_API FALBatchedMaterialFacet::Slot4(void* /*A*/)
    {
        // LAB_062032b0: OR EAX,0xFFFFFFFF; RET 8.
        return -1;
    }

    GENRESULT DACOM_API FALBatchedMaterialFacet::Slot5(unsigned long* Out)
    {
        // LAB_062032c0: MOV EAX,[ESP+8]; MOV dword ptr [EAX],1; XOR EAX,EAX; RET 8.
        *Out = 1;
        return GR_OK;
    }

    GENRESULT DACOM_API FALBatchedMaterialFacet::Slot6(void* /*A*/)
    {
        // LAB_06203300: CALL FUN_0623f450 (see
        // FALMaterial::ApplyDefaultRenderPipelineState); XOR EAX,EAX; RET 8.
        return FALMaterial::ApplyDefaultRenderPipelineState();
    }

    GENRESULT DACOM_API FALBatchedMaterialFacet::Slot7(void* /*A*/)
    {
        // LAB_06203310: XOR EAX,EAX; RET 8.
        return GR_OK;
    }

    GENRESULT DACOM_API FALBatchedMaterialFacet::Slot8(void* /*A*/, void* /*B*/, void* /*C*/,
                                                       void* /*D*/, void* /*E*/, void* /*F*/,
                                                       void* /*G*/)
    {
        // LAB_062032f0: XOR EAX,EAX; RET 0x20 (7 args).
        return GR_OK;
    }

    // ---- FALMaterial ("1.11_IMaterial", object+0) -------------------------

    GENRESULT DACOM_API FALMaterial::QueryInterface(const char* Iid, void** Out)
    {
        // LAB_06203350: retail's real QI is a generic byte-compared 2-entry
        // IID table ("1.11_IBatchedMaterial" -> offset 4, "1.11_IMaterial"
        // -> offset 0); expressed here the same way this port expresses
        // every other multi-IID QueryInterface (see FFxRuntime::
        // QueryInterface). Falls through to GR_INTERFACE_UNSUPPORTED
        // (-3, *Out=0) exactly like retail's table-exhausted path.
        if (IidEqual(Iid, Iids::BatchedMaterial))
        {
            Facet_.AddRef();
            *Out = static_cast<IALBatchedMaterial*>(&Facet_);
            return GR_OK;
        }
        if (IidEqual(Iid, Iids::Material))
        {
            AddRef();
            *Out = static_cast<IALMaterial*>(this);
            return GR_OK;
        }
        *Out = nullptr;
        return GR_INTERFACE_UNSUPPORTED;
    }

    unsigned long DACOM_API FALMaterial::Release()
    {
        // LAB_06203410: guards the decrement against underflow, then on
        // hitting 0 re-arms the refcount to 1 (retail's re-entrancy guard,
        // already fixed the same way across a dozen other alchemy classes)
        // before freeing via FUN_062460e0 (this port's `delete this`).
        if (RefCount_ != 0)
            --RefCount_;
        const unsigned long Next = RefCount_;
        if (Next == 0)
        {
            RefCount_ = 1;
            delete this;
            return 0;
        }
        return Next;
    }

    GENRESULT DACOM_API FALMaterial::Slot3(void* /*A*/)
    {
        // LAB_062031d0: XOR EAX,EAX; RET 8.
        return GR_OK;
    }

    GENRESULT DACOM_API FALMaterial::Slot4(void* /*A*/)
    {
        // LAB_062031e0: XOR EAX,EAX; RET 8.
        return GR_OK;
    }

    GENRESULT DACOM_API FALMaterial::Slot5(void* /*A*/)
    {
        // LAB_062031f0: XOR EAX,EAX; RET 8.
        return GR_OK;
    }

    GENRESULT DACOM_API FALMaterial::Slot6(void* /*A*/, void* /*B*/)
    {
        // LAB_06203200: XOR EAX,EAX; RET 0xC.
        return GR_OK;
    }

    GENRESULT DACOM_API FALMaterial::Slot7(void* /*A*/)
    {
        // LAB_06203210: XOR EAX,EAX; RET 8.
        return GR_OK;
    }

    GENRESULT DACOM_API FALMaterial::Slot8(void* /*A*/)
    {
        // LAB_06203320: CALL FUN_0623f450; XOR EAX,EAX; RET 8.
        return ApplyDefaultRenderPipelineState();
    }

    GENRESULT DACOM_API FALMaterial::Slot9(void* /*A*/)
    {
        // LAB_06203330: XOR EAX,EAX; RET 8.
        return GR_OK;
    }

    GENRESULT DACOM_API FALMaterial::Slot10(void* /*A*/, void* /*B*/, void* /*C*/, void* /*D*/,
                                            void* /*E*/, void* /*F*/, void* /*G*/)
    {
        // LAB_062032e0: OR EAX,0xFFFFFFFF; RET 0x20 (7 args).
        return -1;
    }

    GENRESULT DACOM_API FALMaterial::Slot11(void* /*A*/)
    {
        // LAB_06203220: OR EAX,0xFFFFFFFF; RET 8.
        return -1;
    }

    GENRESULT DACOM_API FALMaterial::Slot12(void* /*A*/, void* /*B*/)
    {
        // LAB_06203230: OR EAX,0xFFFFFFFF; RET 0xC.
        return -1;
    }

    GENRESULT DACOM_API FALMaterial::Slot13(void* /*A*/, void* /*B*/)
    {
        // LAB_06203240: OR EAX,0xFFFFFFFF; RET 0xC.
        return -1;
    }

    GENRESULT DACOM_API FALMaterial::Slot14(unsigned long* Out)
    {
        // LAB_06203250: MOV EAX,[ESP+8]; MOV dword ptr [EAX],1; XOR EAX,EAX; RET 8.
        *Out = 1;
        return GR_OK;
    }

    GENRESULT DACOM_API FALMaterial::Slot15(unsigned long* Out)
    {
        // LAB_062032d0: MOV EAX,[ESP+8]; MOV dword ptr [EAX],2; XOR EAX,EAX; RET 8.
        *Out = 2;
        return GR_OK;
    }

    GENRESULT DACOM_API FALMaterial::Slot16(unsigned long* Out)
    {
        // LAB_06203260: MOV EAX,[ESP+8]; MOV dword ptr [EAX],0; XOR EAX,EAX; RET 8.
        *Out = 0;
        return GR_OK;
    }

    GENRESULT DACOM_API FALMaterial::Slot17(void* /*A*/, void* /*B*/)
    {
        // LAB_06203270: OR EAX,0xFFFFFFFF; RET 0xC.
        return -1;
    }

    GENRESULT DACOM_API FALMaterial::Slot18(void* /*A*/, void* /*B*/)
    {
        // LAB_06203280: OR EAX,0xFFFFFFFF; RET 0xC.
        return -1;
    }

    GENRESULT DACOM_API FALMaterial::Slot19(void* /*A*/, void* /*B*/)
    {
        // LAB_06203290: OR EAX,0xFFFFFFFF; RET 0xC.
        return -1;
    }

    const char* __thiscall FALMaterial::GetClassName()
    {
        // LAB_06203340 returns the class string without accessing the receiver.
        return "ALMaterial";
    }

    GENRESULT FALMaterial::ApplyDefaultRenderPipelineState()
    {
        // FUN_0623f450 == AlchemyImpl::ApplyParticleDrawRenderState, already
        // ported (alchemy_alchemy.cpp) against G_RenderPipeline (this
        // port's name for DAT_06257bc0, see alchemy_common.h/.cpp) -- the
        // same global FAlchemy::BeginDraw drives at its own call site
        // (0x06201b5e). Both retail call sites of THIS slot (LAB_06203300,
        // LAB_06203320) discard the return value and unconditionally return
        // 0 regardless of what the callee did, so this wrapper does the
        // same after invoking the real routine.
        ApplyParticleDrawRenderState();
        return GR_OK;
    }

} // namespace AlchemyImpl
