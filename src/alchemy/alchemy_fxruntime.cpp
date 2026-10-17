// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "alchemy_fxruntime.h"

#include "alchemy_common.h"

// alchemy.dll — DACOM particle FX scaffolding.
// Out-of-line method bodies for FFxRuntime / FFxRuntimeAggregate.
namespace AlchemyImpl
{
    GENRESULT DACOM_API FFxRuntimeAggregate::QueryInterface(const char* Iid, void** Out)
    {
        return Outer_->QueryInterface(Iid, Out);
    }

    unsigned long DACOM_API FFxRuntimeAggregate::AddRef() { return Outer_->AddRef(); }

    unsigned long DACOM_API FFxRuntimeAggregate::Release() { return Outer_->Release(); }

    GENRESULT DACOM_API FFxRuntime::FInnerUnknown::QueryInterface(const char* Iid, void** Out)
    {
        // LAB_062043a0: only IFxRuntime and IAggregateComponent are listed.
        if (IidEqual(Iid, Iids::FxRuntime))
        {
            IFxRuntime* const Interface = static_cast<IFxRuntime*>(Owner_);
            Interface->AddRef();
            *Out = Interface;
            return GR_OK;
        }
        if (IidEqual(Iid, Iids::AggregateComponent))
        {
            IAggregateComponent* const Interface = &Owner_->Aggregate_;
            Interface->AddRef();
            *Out = Interface;
            return GR_OK;
        }
        *Out = nullptr;
        return GR_INTERFACE_UNSUPPORTED;
    }

    unsigned long DACOM_API FFxRuntime::FInnerUnknown::AddRef()
    {
        return ++RefCount_;
    }

    unsigned long DACOM_API FFxRuntime::FInnerUnknown::Release()
    {
        if (RefCount_ != 0)
            --RefCount_;
        const unsigned long Next = RefCount_;
        if (Next == 0)
        {
            RefCount_ = 1;
            if (Owner_ != nullptr)
                delete Owner_;
            return 0;
        }
        return Next;
    }

    GENRESULT DACOM_API FFxRuntime::QueryInterface(const char* Iid, void** Out)
    {
        return Controller_->QueryInterface(Iid, Out);
    }

    unsigned long DACOM_API FFxRuntime::AddRef()
    {
        return Controller_->AddRef();
    }

    unsigned long DACOM_API FFxRuntime::Release()
    {
        return Controller_->Release();
    }

    GENRESULT DACOM_API FFxRuntime::BindAlchemy(void* Alchemy)
    {
        // LAB_0623b670 (DAT_06257b98): raw store, no AddRef / no Release of the
        // previous value — same pattern as BindMeshFactory below.
        G_RuntimeAlchemy = Alchemy;
        return GR_OK;
    }
    GENRESULT DACOM_API FFxRuntime::BindUnknown(void* Value)
    {
        // LAB_0623b680 (DAT_06257b94): raw store, no AddRef / no Release.
        G_RuntimeUnknown = Value;
        return GR_OK;
    }
    GENRESULT DACOM_API FFxRuntime::BindEffectLibrary(void* Library)
    {
        // LAB_0623b690 (DAT_06257b90): raw store, no AddRef / no Release.
        G_RuntimeEffectLibrary = Library;
        return GR_OK;
    }
    GENRESULT DACOM_API FFxRuntime::GetAlchemy(void** Out)
    {
        // LAB_0623b6b0: a miss leaves Out untouched. Reload the global after
        // AddRef, since the bound object's callback may change the binding.
        if (G_RuntimeAlchemy == nullptr)
            return GR_GENERIC;
        static_cast<IDAComponent*>(G_RuntimeAlchemy)->AddRef();
        *Out = G_RuntimeAlchemy;
        return GR_OK;
    }
    GENRESULT DACOM_API FFxRuntime::GetUnknown(void** Out)
    {
        // LAB_0623b6e0: miss does not write Out; hit reloads after AddRef.
        if (G_RuntimeUnknown == nullptr)
            return GR_GENERIC;
        static_cast<IDAComponent*>(G_RuntimeUnknown)->AddRef();
        *Out = G_RuntimeUnknown;
        return GR_OK;
    }
    GENRESULT DACOM_API FFxRuntime::GetEffectLibrary(void** Out)
    {
        // LAB_0623b710: miss does not write Out; hit reloads after AddRef.
        if (G_RuntimeEffectLibrary == nullptr)
            return GR_GENERIC;
        static_cast<IDAComponent*>(G_RuntimeEffectLibrary)->AddRef();
        *Out = G_RuntimeEffectLibrary;
        return GR_OK;
    }
    GENRESULT DACOM_API FFxRuntime::BindMeshFactory(void* Factory)
    {
        // LAB_0623b6a0: raw store, no AddRef / no Release of the previous.
        G_FxMeshFactory = static_cast<IDAComponent*>(Factory);
        return GR_OK;
    }
    GENRESULT DACOM_API FFxRuntime::GetMeshFactory(void** Out)
    {
        // LAB_0623b740: miss does not write Out; hit reloads after AddRef.
        if (G_FxMeshFactory == nullptr)
            return GR_GENERIC;
        G_FxMeshFactory->AddRef();
        *Out = G_FxMeshFactory;
        return GR_OK;
    }

} // namespace AlchemyImpl
