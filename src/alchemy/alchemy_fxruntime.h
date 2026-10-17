// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "alchemy_types.h"
#include "alchemy_common.h"

// alchemy.dll — DACOM particle FX scaffolding.
// FFxRuntime / FFxRuntimeAggregate: the IFxRuntime component and its
// IAggregateComponent inner-object facet.
namespace AlchemyImpl
{
    class FFxRuntime;

    class FFxRuntimeAggregate final : public IAggregateComponent
    {
    public:
        explicit FFxRuntimeAggregate(FFxRuntime* Outer) : Outer_(Outer) {}

        GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
        unsigned long DACOM_API AddRef() override;
        unsigned long DACOM_API Release() override;
        GENRESULT DACOM_API Initialize() override
        {
            return GR_OK;
        }

    private:
        FFxRuntime* Outer_;
    };

    class FFxRuntime final : public IFxRuntime
    {
    public:
        FFxRuntime()
            : Aggregate_(this), Inner_(this), Controller_(&Inner_)
        {
        }

        FFxRuntimeAggregate* Aggregate() { return &Aggregate_; }

        GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
        unsigned long DACOM_API AddRef() override;
        unsigned long DACOM_API Release() override;

        void SetController(IDAComponent* OuterUnknown)
        {
            Controller_ = OuterUnknown != nullptr ? OuterUnknown : &Inner_;
        }
        IDAComponent* InnerUnknown() { return &Inner_; }
        GENRESULT InitializeFromFactory(AGGDESC*) { return GR_OK; }

        GENRESULT DACOM_API BindAlchemy(void* Alchemy) override;
        GENRESULT DACOM_API BindUnknown(void* Value) override;
        GENRESULT DACOM_API BindEffectLibrary(void* Library) override;
        GENRESULT DACOM_API GetAlchemy(void** Out) override;
        GENRESULT DACOM_API GetUnknown(void** Out) override;
        GENRESULT DACOM_API GetEffectLibrary(void** Out) override;
        GENRESULT DACOM_API BindMeshFactory(void* Factory) override;
        GENRESULT DACOM_API GetMeshFactory(void** Out) override;

    private:
        class FInnerUnknown final : public IDAComponent
        {
        public:
            explicit FInnerUnknown(FFxRuntime* Owner) : RefCount_(1), Owner_(Owner) {}

            GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
            unsigned long DACOM_API AddRef() override;
            unsigned long DACOM_API Release() override;

        private:
            unsigned long RefCount_;
            FFxRuntime* Owner_;
        };

        FFxRuntimeAggregate Aggregate_;
        FInnerUnknown Inner_;
        IDAComponent* Controller_;
        // The three bound pointers are module globals in retail (DAT_06257b98 / b94 /
        // b90), read directly by the particle appearance; see G_RuntimeAlchemy.
    };

} // namespace AlchemyImpl
