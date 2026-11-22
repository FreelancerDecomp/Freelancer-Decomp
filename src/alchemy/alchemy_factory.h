// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "alchemy_types.h"

// alchemy.dll — DACOM particle FX scaffolding.
// FComponentFactory: the IComponentFactory shim shared by every registered
// component name, plus the G_Specs[] registration table and DllMain's
// InitializeAlchemyModule helper.
namespace AlchemyImpl
{
    enum EComponentKind
    {
        KindAlchemy,
        KindFxRuntime,
        KindFxEffectLibrary,
        KindFxEffect,
        KindFxEffectArchetype,
        // One kind per keyed-ramp class (ALC-2): each retail name has its own factory
        // CreateInstance and its own object vtable (alchemy_ramp.h).
        KindFxRampSingle,
        KindFxRampColor,
        KindFxRampSingleRamp,
        KindFxRampColorRamp,
        KindFxAnimatedSingleRamp,
        KindFxAnimatedSingle,
        KindFxAnimatedTransform,
        KindFxNodeLibrary,
        // "FxNodePropertySet" (RegisterComponent FUN_0623f750, CreateInstance
        // FUN_06240ad0): FxNode's own property set, not a node.
        KindFxNodePropertySet,
        KindGeneric,
    };

    struct FComponentSpec
    {
        const char* Name;
        FlULong RequiredSize;
        EComponentKind Kind;
        const char* PrimaryIid;
    };

    IDAComponent* CreateComponent(const FComponentSpec& Spec);

    class FComponentFactory final : public IComponentFactory
    {
    public:
        explicit FComponentFactory(const FComponentSpec& Spec) : RefCount_(1), Spec_(Spec) {}

        GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
        FlULong DACOM_API AddRef() override { return ++RefCount_; }
        FlULong DACOM_API Release() override;

        GENRESULT DACOM_API CreateInstance(AGGDESC* Desc, void** Instance) override;

        const char* Name() const { return Spec_.Name; }

    private:
        FlULong RefCount_;
        FComponentSpec Spec_;
    };

    extern const FComponentSpec G_Specs[];

    void InitializeAlchemyModule();

} // namespace AlchemyImpl
