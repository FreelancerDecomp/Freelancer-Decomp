// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Material.h"
#include "MaterialAnimation.h"
#include "MaterialBatcher.h"
#include "MaterialLibrary.h"
#include "ShadingInternal.h"
#include "TextureLibrary.h"

namespace
{
    enum EComponentKind
    {
        KindTextureLibrary = 0,
        KindMaterialLibrary,
        KindMaterialBatcher,
        KindMaterialAnimation,
        KindBatchedMaterial,
        KindMaterialType
    };

    struct FComponentSpec
    {
        const char* Name;
        EComponentKind Kind;
    };

    class FComponentFactory final : public IComponentFactory
    {
    public:
        explicit FComponentFactory(const FComponentSpec& Spec) : RefCount(1), Spec(Spec) {}

        GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override
        {
            // LAB_06eb1050: mandatory output, exact IID including terminator.
            *Out = nullptr;
            if (std::strncmp(Iid, Iids::ComponentFactory, sizeof(Iids::ComponentFactory)) != 0)
                return GR_INTERFACE_UNSUPPORTED;
            *Out = (IComponentFactory*)this;
            AddRef();
            return GR_OK;
        }

        unsigned long DACOM_API AddRef() override { return ++RefCount; }

        unsigned long DACOM_API Release() override
        {
            if (RefCount > 0)
                --RefCount;
            const unsigned long Next = RefCount;
            if (Next == 0)
            {
                RefCount = 1;
                delete this;
            }
            return Next;
        }

        GENRESULT DACOM_API CreateInstance(AGGDESC* Desc, void** Instance) override
        {
            // LAB_06ec6d40..6e2e has its own aggregate descriptor contract.
            if (Spec.Kind == KindMaterialAnimation)
            {
                FMaterialAggregateDesc* AnimationDesc = (FMaterialAggregateDesc*)Desc;
                if (AnimationDesc->Size != sizeof(FMaterialAggregateDesc) ||
                    std::strcmp(AnimationDesc->NameAt4, Spec.Name) != 0)
                {
                    *Instance = nullptr;
                    return GR_INTERFACE_UNSUPPORTED;
                }
                FMaterialAnimation* Animation = new (std::nothrow) FMaterialAnimation();
                if (Animation == nullptr)
                {
                    *Instance = nullptr;
                    return GR_OUT_OF_MEMORY;
                }
                Animation->Outer = AnimationDesc->OuterAt8;
                if (Animation->Outer != nullptr)
                    *AnimationDesc->InnerOutAtC = (IDAComponent*)(&Animation->Controller);
                *Instance = (IMatAnimLibrary*)Animation;
                return GR_OK;
            }

            // FUN_06ecb940: controller publication precedes descriptor init.
            if (Spec.Kind == KindMaterialBatcher)
            {
                FMaterialAggregateDesc* BatcherDesc = (FMaterialAggregateDesc*)Desc;
                if (BatcherDesc->Size != sizeof(FMaterialAggregateDesc) ||
                    std::strcmp(BatcherDesc->NameAt4, Spec.Name) != 0)
                {
                    *Instance = nullptr;
                    return GR_INTERFACE_UNSUPPORTED;
                }
                FMaterialBatcher* Batcher = new (std::nothrow) FMaterialBatcher();
                if (Batcher == nullptr)
                {
                    *Instance = nullptr;
                    return GR_OUT_OF_MEMORY;
                }
                Batcher->ControllingUnknown = BatcherDesc->OuterAt8 != nullptr
                    ? BatcherDesc->OuterAt8 : (IDAComponent*)(&Batcher->Controller);
                if (BatcherDesc->OuterAt8 != nullptr)
                    *BatcherDesc->InnerOutAtC = (IDAComponent*)(&Batcher->Controller);
                const GENRESULT Result = Batcher->InitializeFromDescriptor(BatcherDesc);
                if (Result != GR_OK)
                {
                    if (BatcherDesc->InnerOutAtC != nullptr)
                        *BatcherDesc->InnerOutAtC = nullptr;
                    delete Batcher;
                    *Instance = nullptr;
                    return Result;
                }
                *Instance = (IMaterialBatcher2*)Batcher;
                return Result;
            }

            // Raw PE 06eceff0..06ecf0fd: publish the nondelegating face only
            // for aggregation, then run descriptor initialization on both paths.
            if (Spec.Kind == KindMaterialLibrary)
            {
                FMaterialAggregateDesc* LibraryDesc = (FMaterialAggregateDesc*)Desc;
                if (LibraryDesc->Size != sizeof(FMaterialAggregateDesc) ||
                    std::strcmp(LibraryDesc->NameAt4, Spec.Name) != 0)
                {
                    *Instance = nullptr;
                    return GR_INTERFACE_UNSUPPORTED;
                }
                FMaterialLibrary* Library = new (std::nothrow) FMaterialLibrary();
                if (Library == nullptr)
                {
                    *Instance = nullptr;
                    return GR_OUT_OF_MEMORY;
                }
                Library->ControllingUnknown = LibraryDesc->OuterAt8 != nullptr
                    ? LibraryDesc->OuterAt8 : (IDAComponent*)(&Library->Controller);
                if (LibraryDesc->OuterAt8 != nullptr)
                    *LibraryDesc->InnerOutAtC = (IDAComponent*)(&Library->Controller);
                const GENRESULT Result = Library->InitializeFromDescriptor(LibraryDesc);
                if (Result != GR_OK)
                {
                    if (LibraryDesc->InnerOutAtC != nullptr)
                        *LibraryDesc->InnerOutAtC = nullptr;
                    delete Library;
                    *Instance = nullptr;
                    return Result;
                }
                *Instance = (IMaterialLibrary*)Library;
                return Result;
            }

            // FUN_06ec6760: descriptor initialization is distinct from aggregate Initialize.
            if (Spec.Kind == KindTextureLibrary)
            {
                FSystemAggDesc* TextureDesc = (FSystemAggDesc*)Desc;
                if (TextureDesc->Size != sizeof(FSystemAggDesc) ||
                    std::strcmp(TextureDesc->Name, Spec.Name) != 0)
                {
                    *Instance = nullptr;
                    return GR_INTERFACE_UNSUPPORTED;
                }
                FTextureLibrary* Texture = new (std::nothrow) FTextureLibrary();
                if (Texture == nullptr)
                {
                    *Instance = nullptr;
                    return GR_OUT_OF_MEMORY;
                }
                Texture->Outer = TextureDesc->Outer;
                if (Texture->Outer != nullptr)
                    *TextureDesc->InnerOut = (IDAComponent*)(&Texture->Controller);
                const GENRESULT Result = Texture->InitializeFromDescriptor(TextureDesc);
                if (Result != GR_OK)
                {
                    if (TextureDesc->InnerOut != nullptr)
                        *TextureDesc->InnerOut = nullptr;
                    delete Texture;
                    *Instance = nullptr;
                    return Result;
                }
                *Instance = (ITextureLibrary2*)Texture;
                return Result;
            }

            // Each material callback independently has the exact extended descriptor
            // contract: 10c0, 4840, 5430, 5c70, 6670, 7b90, 8630, 8c40,
            // 9260, a400, aca0, c2b0, c800, ce10, d2d0 and de50.
            if (Spec.Kind == KindMaterialType)
            {
                FMaterialAggregateDesc* MaterialDesc = (FMaterialAggregateDesc*)Desc;
                if (MaterialDesc->Size != sizeof(FMaterialAggregateDesc) ||
                    std::strcmp(MaterialDesc->NameAt4, Spec.Name) != 0)
                {
                    *Instance = nullptr;
                    return GR_INTERFACE_UNSUPPORTED;
                }
                FMaterial* Material = CreateMaterialInstance(Spec.Name);
                if (Material == nullptr)
                {
                    *Instance = nullptr;
                    return GR_OUT_OF_MEMORY;
                }
                Material->ConfigureAggregation(MaterialDesc);
                const GENRESULT Result = Material->InitializeFromDescriptor(MaterialDesc);
                if (Result != GR_OK)
                {
                    if (MaterialDesc->InnerOutAtC != nullptr)
                        *MaterialDesc->InnerOutAtC = nullptr;
                    Material->ScalarDeletingDestructor(1);
                    *Instance = nullptr;
                    return Result;
                }
                *Instance = (IMaterial*)Material;
                return Result;
            }

            // Raw PE 4530..463b: BatchedMaterial calls its concrete descriptor
            // initializer (3e80), after publishing the aggregation controller.
            if (Spec.Kind == KindBatchedMaterial)
            {
                FMaterialAggregateDesc* BatchedDesc = (FMaterialAggregateDesc*)Desc;
                if (BatchedDesc->Size != sizeof(FMaterialAggregateDesc) ||
                    std::strcmp(BatchedDesc->NameAt4, Spec.Name) != 0)
                {
                    *Instance = nullptr;
                    return GR_INTERFACE_UNSUPPORTED;
                }
                FBatchedMaterial* Batched = new (std::nothrow) FBatchedMaterial();
                if (Batched == nullptr)
                {
                    *Instance = nullptr;
                    return GR_OUT_OF_MEMORY;
                }
                Batched->ConfigureAggregation(BatchedDesc);
                const GENRESULT Result = Batched->InitializeFromDescriptor(BatchedDesc);
                if (Result != GR_OK)
                {
                    if (BatchedDesc->InnerOutAtC != nullptr)
                        *BatchedDesc->InnerOutAtC = nullptr;
                    delete Batched;
                    *Instance = nullptr;
                    return Result;
                }
                *Instance = (IMaterial*)Batched;
                return Result;
            }

            *Instance = nullptr;
            return GR_INTERFACE_UNSUPPORTED;
        }

        const char* Name() const { return Spec.Name; }

        unsigned long RefCount;
        FComponentSpec Spec;
    };

    const FComponentSpec G_Specs[] = {
        {"TextureLibrary", KindTextureLibrary},
        {"MaterialLibrary", KindMaterialLibrary},
        {"MaterialBatcher", KindMaterialBatcher},
        {"MaterialAnimation", KindMaterialAnimation},
        {"BatchedMaterial", KindBatchedMaterial},
        {"DcDt", KindMaterialType},
        {"DcDtEc", KindMaterialType},
        {"DcDtOcOt", KindMaterialType},
        {"DcDtEcOcOt", KindMaterialType},
        {"DcDtTwo", KindMaterialType},
        {"DcDtEcTwo", KindMaterialType},
        {"DcDtOcOtTwo", KindMaterialType},
        {"DcDtEcOcOtTwo", KindMaterialType},
        {"DcDtEt", KindMaterialType},
        {"EcEt", KindMaterialType},
        {"EcEtTwo", KindMaterialType},
        {"AtmosphereMaterial", KindMaterialType},
        {"Nebula", KindMaterialType},
        {"NebulaTwo", KindMaterialType},
        {"NullMaterial", KindMaterialType},
        {"LightMap", KindMaterialType},
        {"DetailMapMaterial", KindMaterialType},
        {"DetailMap2Dm1Msk2PassMaterial", KindMaterialType},
        {"IllumDetailMapMaterial", KindMaterialType},
        {"Masked2DetailMapMaterial", KindMaterialType},
        {"BtDetailMapMaterial", KindMaterialType},
        {"BtDetailMapTwoMaterial", KindMaterialType},
    };

    // 06ec3810: only the texture registration acquires before allocation.
    void RegisterTextureLibraryComponent()
    {
        ICOManager* Manager = DACOM_Acquire();
        if (Manager == nullptr)
            return;
        FComponentFactory* Factory = new (std::nothrow) FComponentFactory(G_Specs[0]);
        if (Factory == nullptr)
            return;
        // Opaque priority token: keep const removal explicit.
        Manager->RegisterComponent(Factory, Factory->Name(), const_cast<void*>(kRegisterPriority));
        Factory->Release();
    }

    // Each of the 26 f440 registration callees allocates before acquiring.
    // BatchedMaterial's private name pointer is immutable (06ed4278).
    bool RegisterMaterialComponent(const FComponentSpec& Spec)
    {
        FComponentFactory* Factory = new (std::nothrow) FComponentFactory(Spec);
        if (Factory == nullptr)
            return false;
        ICOManager* Manager = DACOM_Acquire();
        // Opaque priority token: keep const removal explicit.
        Manager->RegisterComponent(Factory, Factory->Name(), const_cast<void*>(kRegisterPriority));
        Factory->Release();
        return true;
    }

    void RegisterMaterialComponents()
    {
        // 06ecf446..f48c: bd20,83f0,6cf0,3dc0,d140,d280,d0a0,d1e0,
        // 5c20,d050,d190,d0f0,d230,8590,85e0.
        static const unsigned long First[] = {1, 2, 3, 4, 8, 12, 6, 10, 13, 5, 9, 7, 11, 14, 15};
        for (unsigned long Index = 0; Index < sizeof(First) / sizeof(First[0]); ++Index)
            RegisterMaterialComponent(G_Specs[First[Index]]);
        ActivateMaterialGlobalRecord(2); // 06ecf491..f52b
        ActivateMaterialGlobalRecord(1); // 06ecf52d..f580

        // 06ecf582..f596: 1000,c210,c260,cdc0,a3b0.
        static const unsigned long Second[] = {16, 17, 18, 19, 20};
        for (unsigned long Index = 0; Index < sizeof(Second) / sizeof(Second[0]); ++Index)
            RegisterMaterialComponent(G_Specs[Second[Index]]);
        ActivateMaterialGlobalRecord(3); // 06ecf59b..f5f0

        // 06ecf5f2..f601: 7b40,6620,ac50,9210.
        static const unsigned long Third[] = {21, 22, 24, 23};
        for (unsigned long Index = 0; Index < sizeof(Third) / sizeof(Third[0]); ++Index)
            RegisterMaterialComponent(G_Specs[Third[Index]]);
        ActivateMaterialGlobalRecord(0); // 06ecf606..f650
        RegisterMaterialComponent(G_Specs[25]); // 06ecf652 -> 47a0
        RegisterMaterialComponent(G_Specs[26]); // 06ecf65d -> 47f0
    }
}

void RegisterShadingComponents()
{
    RegisterTextureLibraryComponent();
    RegisterMaterialComponents();
}

BOOL WINAPI DllMain(HINSTANCE Instance, DWORD Reason, LPVOID /*Reserved*/)
{
    if (Reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(Instance);
        RegisterShadingComponents();
    }
    return TRUE;
}
