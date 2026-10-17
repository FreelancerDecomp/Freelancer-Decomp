// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "alchemy_factory.h"

#include "alchemy_common.h"
#include "alchemy_alchemy.h"
#include "alchemy_appearances.h"
#include "alchemy_effectarchetype.h"
#include "alchemy_effectlibrary.h"
#include "alchemy_effectobject.h"
#include "alchemy_emitters.h"
#include "alchemy_fields.h"
#include "alchemy_fxruntime.h"
#include "alchemy_genericfx.h"
#include "alchemy_nodelibrary.h"
#include "alchemy_ramp.h"

#include <new>

// alchemy.dll — DACOM particle FX scaffolding.
// Out-of-line method bodies for FComponentFactory, the G_Specs[] table, and
// the DllMain registration helper.
namespace AlchemyImpl
{
    static IDAComponent* CreateGenericFxLeaf(const char* PrimaryIid, const char* Name);

    IDAComponent* CreateComponent(const FComponentSpec& Spec)
    {
        switch (Spec.Kind)
        {
        case KindAlchemy:
            return new (std::nothrow) FAlchemy();
        case KindFxRuntime:
            return new (std::nothrow) FFxRuntime();
        case KindFxEffectLibrary:
            return new (std::nothrow) FFxEffectLibrary();
        case KindFxEffect:
            return new (std::nothrow) FFxEffectObject();
        case KindFxEffectArchetype:
            return new (std::nothrow) FFxEffectArchetype();
        // The five keyed ramps: 06242c90 (0x2C, vtable 0x0624bbf0), 06242630 (0x2C,
        // 0x0624bc40), 062414e0 (0x20, 0x0624bb18), 062416f0 (0x20, 0x0624bb68),
        // 06241900 (0x20, 0x0624bac8). Each runs its Initialize slot right after
        // construction (FComponentFactory::CreateInstance below).
        case KindFxRampSingle:
            return static_cast<IRampSingle*>(new (std::nothrow) FRampSingle());
        case KindFxRampColor:
            return static_cast<IRampColor*>(new (std::nothrow) FRampColor());
        case KindFxRampSingleRamp:
            return static_cast<INestedSingleRamp*>(new (std::nothrow) FRampSingleRamp());
        case KindFxRampColorRamp:
            return static_cast<IRampColorRamp*>(new (std::nothrow) FRampColorRamp());
        case KindFxAnimatedSingleRamp:
            return static_cast<INestedSingleRamp*>(new (std::nothrow) FAnimatedSingleRamp());
        case KindFxAnimatedSingle:
            // FFxAnimatedSingle backs exactly one interface (IAnimatedSingle).
            return new (std::nothrow) FFxAnimatedSingle();
        case KindFxAnimatedTransform:
            // wave120 (sonnet:wave120:animatedtransform): FFxAnimatedTransform
            // backs exactly one interface (IAnimatedTransform, vtable
            // 0x0624bbb8).
            return new (std::nothrow) FFxAnimatedTransform();
        case KindFxNodeLibrary:
            return new (std::nothrow) FFxNodeLibrary();
        case KindFxNodePropertySet:
            // FUN_06240ad0 0x06240b3a..0x06240b75: operator_new(0x54), FUN_0623f7b0, inner
            // vtable 0x0624f9e0, outer 0x0624f980, count 1.
            return static_cast<IFxNodePropertySet*>(new (std::nothrow) FFxNodeOwnPropertySet());
        case KindGeneric:
            return CreateGenericFxLeaf(Spec.PrimaryIid, Spec.Name);
        default:
            return nullptr;
        }
    }

    // The 20 RegisterComponent names still marked KindGeneric (2 node +
    // 3 emitter + 7 field + 8 appearance — re-counted this wave; a prior
    // wave's comment undercounted this as "19") each get their own concrete
    // leaf class now (see alchemy_genericfx.h's file-header comment for the
    // class-hierarchy-split rationale) instead of one parameterized
    // FGenericFxObject. Matched by exact retail name (the same string
    // G_Specs[] already carries), not by PrimaryIid, since two different
    // leaves can share an IID string (all 3 emitters are "1.11_IFxEmitter",
    // all 7 fields are "1.11_IFxField", all 8 appearances are
    // "1.11_IFxAppearance").
    static IDAComponent* CreateGenericFxLeaf(const char* PrimaryIid, const char* Name)
    {
        if (NameEqual(Name, "FxNode"))
            return new (std::nothrow) FxNode(PrimaryIid, Name);

        if (NameEqual(Name, "FxSphereEmitter"))
            return new (std::nothrow) FxSphereEmitter(PrimaryIid, Name);
        if (NameEqual(Name, "FxConeEmitter"))
            return new (std::nothrow) FxConeEmitter(PrimaryIid, Name);
        if (NameEqual(Name, "FxCubeEmitter"))
            return new (std::nothrow) FxCubeEmitter(PrimaryIid, Name);

        if (NameEqual(Name, "FxAirField"))
            return new (std::nothrow) FxAirField(PrimaryIid, Name);
        if (NameEqual(Name, "FxGravityField"))
            return new (std::nothrow) FxGravityField(PrimaryIid, Name);
        if (NameEqual(Name, "FxCollideField"))
            return new (std::nothrow) FxCollideField(PrimaryIid, Name);
        if (NameEqual(Name, "FxRadialField"))
            return new (std::nothrow) FxRadialField(PrimaryIid, Name);
        if (NameEqual(Name, "FxTurbulenceField"))
            return new (std::nothrow) FxTurbulenceField(PrimaryIid, Name);
        if (NameEqual(Name, "FLBeamField"))
            return new (std::nothrow) FLBeamField(PrimaryIid, Name);
        if (NameEqual(Name, "FLDustField"))
            return new (std::nothrow) FLDustField(PrimaryIid, Name);

        if (NameEqual(Name, "FxBasicAppearance"))
            return new (std::nothrow) FxBasicAppearance(PrimaryIid, Name);
        if (NameEqual(Name, "FxParticleAppearance"))
            return new (std::nothrow) FxParticleAppearance(PrimaryIid, Name);
        if (NameEqual(Name, "FxRectAppearance"))
            return new (std::nothrow) FxRectAppearance(PrimaryIid, Name);
        if (NameEqual(Name, "FxPerpAppearance"))
            return new (std::nothrow) FxPerpAppearance(PrimaryIid, Name);
        if (NameEqual(Name, "FxMeshAppearance"))
            return new (std::nothrow) FxMeshAppearance(PrimaryIid, Name);
        if (NameEqual(Name, "FxOrientedAppearance"))
            return new (std::nothrow) FxOrientedAppearance(PrimaryIid, Name);
        if (NameEqual(Name, "FLDustAppearance"))
            return new (std::nothrow) FLDustAppearance(PrimaryIid, Name);
        if (NameEqual(Name, "FLBeamAppearance"))
            return new (std::nothrow) FLBeamAppearance(PrimaryIid, Name);

        return nullptr;
    }

    // Every per-name factory QI (06243310, 06243d50 and their twins): *Out = 0, a REPE
    // CMPSB against "1.11_IComponentFactory" (0x17 bytes) -- mismatch -3; match *Out =
    // this and AddRef (+4), 0. No null test on Out.
    GENRESULT DACOM_API FComponentFactory::QueryInterface(const char* Iid, void** Out)
    {
        *Out = nullptr;
        if (!IidEqual(Iid, Iids::ComponentFactory))
            return GR_INTERFACE_UNSUPPORTED;
        *Out = static_cast<IComponentFactory*>(this);
        AddRef();
        return GR_OK;
    }

    unsigned long DACOM_API FComponentFactory::Release()
    {
        // Retail (FUN_06203ea0 / FUN_062040f0 / FUN_06222150 — byte-identical
        // across every one of the 33 per-name factory Release slots, sampled
        // and re-verified this wave) guards the decrement: "if (refcount != 0)
        // refcount--", not an unconditional --RefCount_. A prior wave's port
        // dropped that guard, so a stray extra Release() call (refcount
        // already 0) would underflow to 0xFFFFFFFF here and never free the
        // object, instead of retail's no-op. Retail also sets refcount back to
        // 1 before deleting (defends its two-vtable object against a
        // reentrant Release() from within its own teardown); harmless to
        // mirror even though FComponentFactory's own destruction path has no
        // such reentrancy risk.
        if (RefCount_ != 0)
            --RefCount_;
        if (RefCount_ == 0)
        {
            RefCount_ = 1;
            delete this;
            return 0;
        }
        return RefCount_;
    }

    // The per-name CreateInstance bodies (06243390 and twins): no null test on Desc or
    // Instance; a size or name mismatch stores 0 and returns -3, and every failure path
    // leaves *Instance = 0.
    GENRESULT DACOM_API FComponentFactory::CreateInstance(AGGDESC* Desc, void** Instance)
    {
        const bool bAggregation = Spec_.Kind == KindAlchemy || Spec_.Kind == KindFxRuntime;
        if (!bAggregation && Spec_.Kind != KindFxNodeLibrary)
            *Instance = nullptr;

        const char* RequestedName = nullptr;
        if (Desc->size == Spec_.RequiredSize)
        {
            RequestedName = reinterpret_cast<FAggDesc8*>(Desc)->NameAt4;
        }
        else
        {
            *Instance = nullptr;
            return GR_INTERFACE_UNSUPPORTED;
        }

        if (!NameEqual(RequestedName, Spec_.Name))
        {
            *Instance = nullptr;
            return GR_INTERFACE_UNSUPPORTED;
        }

        IDAComponent* Object = CreateComponent(Spec_);
        if (Object == nullptr)
        {
            *Instance = nullptr;
            return GR_OUT_OF_MEMORY;
        }

        if (Spec_.Kind == KindAlchemy)
        {
            FAlchemy* Alchemy = static_cast<FAlchemy*>(Object);
            FAGGDESC14* Aggregate = reinterpret_cast<FAGGDESC14*>(Desc);
            Alchemy->SetController(Aggregate->OuterUnknown);
            if (Aggregate->OuterUnknown != nullptr)
                *Aggregate->InnerUnknown = Alchemy->InnerUnknown();
            const GENRESULT InitResult = Alchemy->Initialize(Desc);
            if (InitResult != GR_OK)
            {
                if (Aggregate->InnerUnknown != nullptr)
                    *Aggregate->InnerUnknown = nullptr;
                delete Alchemy;
                *Instance = nullptr;
                return InitResult;
            }
        }
        else if (Spec_.Kind == KindFxRuntime)
        {
            FFxRuntime* Runtime = static_cast<FFxRuntime*>(Object);
            FAGGDESC14* Aggregate = reinterpret_cast<FAGGDESC14*>(Desc);
            Runtime->SetController(Aggregate->OuterUnknown);
            if (Aggregate->OuterUnknown != nullptr)
                *Aggregate->InnerUnknown = Runtime->InnerUnknown();
            const GENRESULT InitResult = Runtime->InitializeFromFactory(Desc);
            if (InitResult != GR_OK)
            {
                if (Aggregate->InnerUnknown != nullptr)
                    *Aggregate->InnerUnknown = nullptr;
                delete Runtime;
                *Instance = nullptr;
                return InitResult;
            }
        }
        else if (Spec_.Kind == KindFxEffect)
        {
            FFxEffectObject* Effect = static_cast<FFxEffectObject*>(Object);
            const GENRESULT InitResult = Effect->Initialize(Desc);
            if (InitResult != GR_OK)
            {
                Effect->Release();
                return InitResult;
            }
        }
        else if (Spec_.Kind == KindFxRampSingle || Spec_.Kind == KindFxRampColor ||
                 Spec_.Kind == KindFxRampSingleRamp || Spec_.Kind == KindFxRampColorRamp ||
                 Spec_.Kind == KindFxAnimatedSingleRamp)
        {
            // The ramp factories (06242c90, 06242630, 062414e0, 062416f0, 06241900) call
            // the new object's Initialize slot (+0x40 / +0x50 / +0x40 / +0x44 / +0x40)
            // with the descriptor; a failure destroys the object, *Instance = 0 and the
            // code is returned (06242d6e -> 06242dfb).
            GENRESULT InitResult = GR_OK;
            if (Spec_.Kind == KindFxRampSingle)
                InitResult = static_cast<IRampSingle*>(Object)->Initialize(Desc);
            else if (Spec_.Kind == KindFxRampColor)
                InitResult = static_cast<IRampColor*>(Object)->Initialize(Desc);
            else if (Spec_.Kind == KindFxRampColorRamp)
                InitResult = static_cast<IRampColorRamp*>(Object)->Initialize(Desc);
            else
                InitResult = static_cast<INestedSingleRamp*>(Object)->Initialize(Desc);
            if (InitResult != GR_OK)
            {
                Object->Release();
                return InitResult;
            }
        }
        else if (Spec_.Kind == KindGeneric)
        {
            // Every node factory (e.g. the sphere emitter's 0x0623cb8d) calls the new node's
            // slot 1, FUN_0623fde0, with the creation descriptor: the class's property set
            // is created there. A non-zero result destroys the node (0x0623cb96..) and is
            // returned with *Instance = 0.
            FGenericFxObjectBase* const Node = static_cast<FGenericFxObjectBase*>(Object);
            const GENRESULT InitResult =
                Node->InitializeFromFactory(reinterpret_cast<const FAggDesc8*>(Desc));
            if (InitResult != GR_OK)
            {
                Object->Release();
                return InitResult;
            }
        }
        else if (Spec_.Kind == KindFxNodePropertySet)
        {
            // FUN_06240ad0 0x06240b93..0x06240c37: the set's Initialize (inner +0x00,
            // FUN_0623f8d0) with the descriptor; a non-zero result destroys the set (the
            // inlined FUN_0623f7b0 destructor and operator_delete), *Instance = 0 and the
            // result is returned; otherwise *Instance = the set, 0.
            FFxNodePropertySet* const Set =
                static_cast<FFxNodePropertySet*>(static_cast<IFxNodePropertySet*>(Object));
            const GENRESULT InitResult = Set->Initialize(Desc);
            if (InitResult != GR_OK)
            {
                Object->Release();
                return InitResult;
            }
        }
        else if (Spec_.Kind == KindFxAnimatedSingle)
        {
            // 06243390 (bytes 0624341f..06243448, left undisassembled by Ghidra): the new
            // FxAnimatedSingle's Initialize (+0x4C, 06242f20 -> Reset); a non-zero result
            // runs the scalar deleting destructor (+0x48 with 1), *Instance = 0 and
            // returns that result.
            const GENRESULT InitResult = static_cast<IFxAnimatedSingle*>(Object)->Initialize(Desc);
            if (InitResult != GR_OK)
            {
                Object->Release();
                return InitResult;
            }
        }

        if (Desc->size == 0x14 && !bAggregation)
        {
            // Non-aggregation private descriptors retain their separate
            // initialization contract; Alchemy/Runtime inner outputs are
            // handled before Initialize above.
            FAggDesc14* Extended = reinterpret_cast<FAggDesc14*>(Desc);
            if (Extended->WritebackAtC != nullptr)
                *Extended->WritebackAtC = Object;
        }

        *Instance = Object;
        return GR_OK;
    }

    const FComponentSpec G_Specs[] = {
        {"Alchemy", 0x14, KindAlchemy, Iids::Alchemy},
        {"FxRuntime", 0x14, KindFxRuntime, Iids::FxRuntime},
        {"FxNodeLibrary", 8, KindFxNodeLibrary, Iids::FxNodeLibrary},
        // The animated transform and the six ramp-family names: each is registered by
        // its own RegisterComponent thunk with its own factory and object vtable
        // (FxAnimatedTransform 0x0624bbb8, FxAnimatedSingle 0x0624bca0 via FUN_06242e20,
        // FxAnimatedSingleRamp 0x0624bac8 via FUN_062412d0, and the four keyed ramps
        // listed in CreateComponent above).
        {"FxAnimatedTransform", 8, KindFxAnimatedTransform, Iids::AnimatedTransform},
        {"FxAnimatedSingle", 8, KindFxAnimatedSingle, Iids::AnimatedSingle},
        {"FxRampSingle", 8, KindFxRampSingle, Iids::RampSingle},
        {"FxRampColor", 8, KindFxRampColor, Iids::RampColor},
        {"FxAnimatedSingleRamp", 8, KindFxAnimatedSingleRamp, Iids::AnimatedSingleRamp},
        {"FxRampSingleRamp", 8, KindFxRampSingleRamp, Iids::RampSingleRamp},
        {"FxRampColorRamp", 8, KindFxRampColorRamp, Iids::RampColorRamp},
        {"FxEffectArchetype", 8, KindFxEffectArchetype, Iids::FxEffectArchetype},
        {"FxEffect", 8, KindFxEffect, Iids::FxEffect},
        {"FxEffectLibrary", 8, KindFxEffectLibrary, Iids::FxEffectLibrary},
        {"FxNodePropertySet", 8, KindFxNodePropertySet, Iids::FxNodePropertySet},
        {"FxNode", 8, KindGeneric, Iids::FxNode},
        {"FxSphereEmitter", 8, KindGeneric, Iids::FxEmitter},
        {"FxConeEmitter", 8, KindGeneric, Iids::FxEmitter},
        {"FxCubeEmitter", 8, KindGeneric, Iids::FxEmitter},
        {"FxAirField", 8, KindGeneric, Iids::FxField},
        {"FxGravityField", 8, KindGeneric, Iids::FxField},
        {"FxCollideField", 8, KindGeneric, Iids::FxField},
        {"FxRadialField", 8, KindGeneric, Iids::FxField},
        {"FxTurbulenceField", 8, KindGeneric, Iids::FxField},
        {"FLDustField", 8, KindGeneric, Iids::FxField},
        {"FLBeamField", 8, KindGeneric, Iids::FxField},
        {"FxBasicAppearance", 8, KindGeneric, Iids::FxAppearance},
        {"FxParticleAppearance", 8, KindGeneric, Iids::FxAppearance},
        {"FxRectAppearance", 8, KindGeneric, Iids::FxAppearance},
        {"FxPerpAppearance", 8, KindGeneric, Iids::FxAppearance},
        {"FxMeshAppearance", 8, KindGeneric, Iids::FxAppearance},
        {"FxOrientedAppearance", 8, KindGeneric, Iids::FxAppearance},
        {"FLDustAppearance", 8, KindGeneric, Iids::FxAppearance},
        {"FLBeamAppearance", 8, KindGeneric, Iids::FxAppearance},
    };

    static void RegisterComponent(ICOManager* Manager, const FComponentSpec& Spec)
    {
        FComponentFactory* Factory = new (std::nothrow) FComponentFactory(Spec);
        if (Factory == nullptr)
            return;
        Manager->RegisterComponent(Factory, Factory->Name(), const_cast<void*>(kRegisterPriority));
        Factory->Release();
    }

    static void RegisterComponent(const FComponentSpec& Spec)
    {
        FComponentFactory* Factory = new (std::nothrow) FComponentFactory(Spec);
        if (Factory == nullptr)
            return;
        // The 31 standalone registration functions allocate before acquiring.
        ICOManager* Manager = DACOM_Acquire();
        Manager->RegisterComponent(Factory, Factory->Name(), const_cast<void*>(kRegisterPriority));
        Factory->Release();
    }

    void InitializeAlchemyModule()
    {
        ICOManager* Manager = DACOM_Acquire();
        if (Manager != nullptr)
        {
            RegisterComponent(Manager, G_Specs[0]);
            RegisterComponent(Manager, G_Specs[1]);
        }

        ApplyAlchemyIniAndPools(Manager);

        const int Count = static_cast<int>(sizeof(G_Specs) / sizeof(G_Specs[0]));
        for (int Index = 2; Index < Count; ++Index)
        {
            RegisterComponent(G_Specs[Index]);
        }
    }

} // namespace AlchemyImpl
