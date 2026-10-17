// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "flmaterials.h"
#include "common_misc.h" // ICamera
#include "engine/engbase.h" // IEngine
#include "render/rendcomp.h" // IRenderer2
#include "render/shading.h"  // IMaterialLibrary, ITextureLibrary2
#include "x86math.h"         // I3DMathEngine

#include <cmath>
#include <cstddef>
#include <cstring>
#include <map>
#include <new>

namespace
{
    const char* const kFlmFile = "E:\\FL\\Scratch\\Source\\FLMaterials\\FLMaterials.cpp";

    const char* const kOverrideNames[] = {
        "ColorizeMaterial",        // 0
        "WireMaterial",            // 1
        "WireMaterial2",           // 2
        "CloakMaterial",           // 3
        "EnvMapMaterial",          // 4
        "BackdropMaterial",        // 5
        "StarsphereMaterial",      // 6
        "ShadowMaterial",          // 7
        "VisibleCounterMaterial",  // 8
    };
    const int kOverrideCount = 9;

    IMaterial* G_Overrides[kOverrideCount] = {};

    // Slot 6 writes these; no readers in this DLL. CRT defaults from §11.2.
    float G_Slot6First[3] = {0.0f, -1.0f, 0.0f};
    float G_Slot6Second[3] = {1.0f, 1.0f, 1.0f};
    unsigned char G_Slot6Flag = 0;

    // LAB_066fc980: native private unique-key map owns construction and CRT teardown.
    std::map<int, void*> G_IdValues;

    // Slot 11/12 flags: initialized DATA bytes are 1; BSS flag bytes stay zero.
    unsigned char G_Slot11FlagA = 1; // DAT_0670406C
    unsigned char G_Slot11FlagB = 1; // DAT_06704068
    unsigned char G_Slot11FlagC = 0; // DAT_0670463C
    unsigned char G_Slot11FlagD = 1; // DAT_06704064
    unsigned char G_Slot12FlagA = 0; // DAT_0670464C
    unsigned char G_Slot12FlagB = 0; // DAT_06704648
    unsigned char G_Slot12FlagC = 1; // DAT_06704094
    unsigned char G_Slot12FlagD = 0; // DAT_06704644

    const unsigned long kShadowDrawRs[] = {0x89, 0, 0x1D, 0, 0x1B, 1, 0x13, 5, 0x14, 6, 0};
    const unsigned long kShadowDrawTss0[] = {1, 4, 2, 2, 3, 1, 4, 4, 5, 2, 6, 1, 0};
    const unsigned long kShadowDrawTss1[] = {1, 1, 2, 2, 3, 1, 4, 1, 5, 2, 6, 1, 0};

    const char* const G_Name3DMathEngine = "3DMathEngine";
    const float kCameraNearConst = 1.0f;       // DAT_06701394
    const float kCameraFarConst = 200000.0f;   // DAT_06702A8C
    const float kHalf = 0.5f;                  // DAT_06702FB0
    const float kShadowExtentX = 0.28f;        // DAT_06704090
    const float kShadowExtentZ = 0.24f;        // DAT_0670408C
    const float kShadowExtentAdd = 0.05f;      // DAT_06704088
    const float kShadowZBias = -0.1f;          // DAT_06704084
    const float kShadowFadeRange = 50.0f;      // DAT_06704080
    const float kShadowFadeQuad = 4.0f;        // DAT_0670407C
    const float kShadowAlphaSub = 0.8f;        // DAT_06704074
    const float kShadowAlphaConst = 0.6f;      // DAT_06704070
    const float kShadowColorScale = 255.0f;    // DAT_067022C4
    const float kShadowFadeForce = 10.0f;      // DAT_06702FC0
    const double kShadowScaleBias = 0.8;       // DAT_06702FB8
    const unsigned long kShadowFvf = 0x142;    // XYZ|DIFFUSE|TEX1
    float G_ShadowQuadScale = 1.0f;            // DAT_06704078
    unsigned char G_ShadowFadeForce = 0;       // DAT_06704640
    I3DMathEngine* G_MathEngine = nullptr;

    void IdentityTransform(float* M12)
    {
        M12[0] = 1.0f;
        M12[1] = 0.0f;
        M12[2] = 0.0f;
        M12[3] = 0.0f;
        M12[4] = 1.0f;
        M12[5] = 0.0f;
        M12[6] = 0.0f;
        M12[7] = 0.0f;
        M12[8] = 1.0f;
        M12[9] = 0.0f;
        M12[10] = 0.0f;
        M12[11] = 0.0f;
    }

    // Camera wrapper borrows its inner camera and adjusts the projected scale.
    class FCameraBlob : public IDAComponent
    {
    public:
        FCameraBlob()
            : Inner_(nullptr), Byte8_(0), Byte9_(0), Pad0_(0), Pad1_(0), RefCount_(1)
        {
            IdentityTransform(TransformA_);
            IdentityTransform(TransformB_);
            Vec6C_[0] = 1.0f;
            Vec6C_[1] = 0.0f;
            Vec6C_[2] = 0.0f;
            Vec78_[0] = 0.0f;
            Vec78_[1] = 1.0f;
            Vec78_[2] = 0.0f;
        }

        GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override
        {
            // LAB_066ff700: immutable map contains only Camera at offset zero.
            if (std::strcmp(Iid, Iids::Camera) != 0)
            {
                *Out = nullptr;
                return GR_INTERFACE_UNSUPPORTED;
            }
            AddRef();
            *Out = (IDAComponent*)this;
            return GR_OK;
        }

        unsigned long DACOM_API AddRef() override
        {
            RefCount_ += 1;
            return RefCount_;
        }

        unsigned long DACOM_API Release() override
        {
            if (RefCount_ != 0)
                RefCount_ -= 1;
            const unsigned long Next = RefCount_;
            if (Next == 0)
            {
                RefCount_ = 1;
                delete this;
            }
            return Next;
        }

        // +0x0C LAB_066fdc60: byte+9 → this+0x30, else inner GetPosition.
        virtual float const* DACOM_API GetPosition()
        {
            if (Byte9_ != 0)
                return TransformA_ + 9;
            return Inner_->GetPosition();
        }

        // +0x10 LAB_066fdc80: byte+9 → this+0x0C, else inner GetOrientation.
        virtual float const* DACOM_API GetOrientation()
        {
            if (Byte9_ != 0)
                return TransformA_;
            return Inner_->GetOrientation();
        }

        // +0x14 LAB_066fdcc0: tail-call inner GetViewport.
        virtual int const* DACOM_API GetViewport() { return Inner_->GetViewport(); }

        // +0x18 LAB_066fdcd0: tail-call inner GetHalfFovXDeg. Float in ST0, not EAX.
        virtual float DACOM_API GetHalfFovXDeg() { return Inner_->GetHalfFovXDeg(); }

        // +0x1C LAB_066fdce0: tail-call inner GetFovYDeg.
        virtual float DACOM_API GetFovYDeg() { return Inner_->GetFovYDeg(); }

        // +0x20 LAB_066fdcf0: byte+8 → 1.0, else inner GetZNear.
        virtual float DACOM_API GetZNear()
        {
            if (Byte8_ != 0)
                return kCameraNearConst;
            return Inner_->GetZNear();
        }

        // +0x24 LAB_066fdd10: byte+8 → 200000, else inner GetZFar.
        virtual float DACOM_API GetZFar()
        {
            if (Byte8_ != 0)
                return kCameraFarConst;
            return Inner_->GetZFar();
        }

        // +0x28 LAB_066fdd30: tail-call inner GetAspect.
        virtual float DACOM_API GetAspect() { return Inner_->GetAspect(); }

        // Round each inner scale result to float before dividing.
        virtual double DACOM_API GetXScaleOverNear()
        {
            const float First = (float)(Inner_->GetXScaleOverNear());
            if (Byte8_ == 0)
                return First;
            return (double)(Inner_->GetZNear()) * First;
        }

        // +0x30 FUN_066fdd80: same shape as slot 11, on GetYScaleOverNear.
        virtual double DACOM_API GetYScaleOverNear()
        {
            const float First = (float)(Inner_->GetYScaleOverNear());
            if (Byte8_ == 0)
                return First;
            return (double)(Inner_->GetZNear()) * First;
        }

        // +0x34 LAB_066fddc0 RET 0x14: tail-call inner Project.
        virtual int DACOM_API Project(float* OutX, float* OutY, float* OutZ, float const* World)
        {
            return Inner_->Project(OutX, OutY, OutZ, World);
        }

        // +0x38 LAB_066fddd0 RET 0x10: tail-call inner Unproject.
        virtual void DACOM_API Unproject(float* OutWorld3, float ScreenX, float ScreenY)
        {
            Inner_->Unproject(OutWorld3, ScreenX, ScreenY);
        }

        // +0x3C LAB_066fddf0 RET 0xC. Byte+8 clear forwards ClassifySphere. Byte+8 set
        // classifies against this object's near/far and the axes at +0x6C/+0x78.
        virtual int DACOM_API ClassifySphere(float const* Center, float Radius)
        {
            if (Byte8_ == 0)
                return Inner_->ClassifySphere(Center, Radius);

            const float ZNear = GetZNear();
            const float ZFar = GetZFar();
            const float Cx = Center[0];
            const float Cy = Center[1];
            const float Cz = Center[2];

            if (((double)Radius - ZNear) < Cz)
                return 1;

            int Rank = 4;
            if (!(((double)Radius + Cz) < -(double)ZNear))
                Rank = 3;
            if (((double)Radius + Cz) < -(double)ZFar)
                return 1;
            if (!(((double)Radius - ZFar) < Cz))
                Rank = 3;

            const double SumX = std::fabs((double)Cx) * Vec6C_[0]
                + (double)Cz * Vec6C_[2];
            if (SumX > Radius)
                return 1;
            if (!(SumX < -Radius))
                Rank = 3;

            const double SumY = std::fabs((double)Cy) * Vec78_[1]
                + (double)Cz * Vec78_[2];
            if (SumY > Radius)
                return 1;
            if (SumY < -Radius)
                return Rank;
            return 3;
        }

        // +0x40 LAB_066fdca0: byte+9 → this+0x3C, else inner GetViewTransform.
        virtual float const* DACOM_API GetViewTransform()
        {
            if (Byte9_ != 0)
                return TransformB_;
            return Inner_->GetViewTransform();
        }

        void BindArg(void* Arg, unsigned char Flag8, unsigned char Flag9)
        {
            Inner_ = (ICamera*)Arg;
            Byte8_ = Flag8;
            Byte9_ = Flag9;
        }

        void ClearArg()
        {
            Inner_ = nullptr;
            Byte9_ = 0;
        }

        void SetByte8(unsigned char Value) { Byte8_ = Value; }
        unsigned char Byte8() const { return Byte8_; }
        unsigned char Byte9() const { return Byte9_; }
        float* TransformA() { return TransformA_; }
        float* TransformB() { return TransformB_; }
        float* Vec6C() { return Vec6C_; }
        float* Vec78() { return Vec78_; }
        ICamera* Inner() const { return Inner_; }

        // FUN_066fe700: 9 floats from the orientation, then 3 from the second pointer, at +0x24.
        void SetOrientationAndTranslation(const float* Matrix9, const float* Translation)
        {
            for (int Index = 0; Index < 9; ++Index)
                TransformA_[Index] = Matrix9[Index];
            TransformA_[9] = Translation[0];
            TransformA_[10] = Translation[1];
            TransformA_[11] = Translation[2];
        }

        // FUN_066fe4b0: preserve source; write inverse to the separate output transform.
        void BuildInverseTransform(float* OutTransform)
        {
            if (G_MathEngine == nullptr)
                G_MathEngine = FlmCreateMathEngine();
            I3DMathEngine* Math = G_MathEngine;
            float Dest[3];
            // These references view float storage without a numeric conversion.
            Math->TransposeTransform(reinterpret_cast<FVector&>(Dest[0]),
                                     reinterpret_cast<const FMatrix&>(TransformA_[0]),
                                     reinterpret_cast<const FVector&>(TransformA_[9]));
            float Transposed[12];
            Transposed[0] = TransformA_[0];
            Transposed[1] = TransformA_[3];
            Transposed[2] = TransformA_[6];
            Transposed[3] = TransformA_[1];
            Transposed[4] = TransformA_[4];
            Transposed[5] = TransformA_[7];
            Transposed[6] = TransformA_[2];
            Transposed[7] = TransformA_[5];
            Transposed[8] = TransformA_[8];
            Transposed[9] = -Dest[0];
            Transposed[10] = -Dest[1];
            Transposed[11] = -Dest[2];
            for (int Index = 0; Index < 12; ++Index)
                OutTransform[Index] = Transposed[Index];
        }

    private:
        ICamera* Inner_;
        unsigned char Byte8_;
        unsigned char Byte9_;
        unsigned char Pad0_;
        unsigned char Pad1_;
        float TransformA_[12];
        float TransformB_[12];
        float Vec6C_[3];
        float Vec78_[3];
        unsigned long RefCount_;
    };

    static_assert(sizeof(FCameraBlob) == 0x88, "retail camera object is 0x88");

    void CopyVec3(float* Dest, const float* Source)
    {
        Dest[0] = Source[0];
        Dest[1] = Source[1];
        Dest[2] = Source[2];
    }

    void ReleaseOverrides()
    {
        for (int Index = 0; Index < kOverrideCount; ++Index)
        {
            if (G_Overrides[Index] != nullptr)
            {
                G_Overrides[Index]->Release();
                G_Overrides[Index] = nullptr;
            }
        }
    }

    void CreateOneOverride(IDAComponent* Library, int Index)
    {
        const char* Name = kOverrideNames[Index];
        G_Overrides[Index] = nullptr;
        IMaterialLibrary* Materials = (IMaterialLibrary*)Library;
        const GENRESULT Found = Materials->FindOrCreate(Name, Name, 0, &G_Overrides[Index]);
        if (Found != GR_OK)
        {
            FDUMP(kFdumpError, "%s(%d) : ERROR:General:FLMaterials: couldnt create override material %s",
                  kFlmFile, 0x49 + Index, Name);
            return;
        }

        unsigned long CrcStruct[2];
        CrcStruct[0] = (unsigned long)(Name);
        CrcStruct[1] = DACOM_CRC::GetCRC32(Name);
        Materials->RemoveMaterial(0, CrcStruct);
    }

    void CreateOverrides(IDAComponent* Library)
    {
        for (int Index = 0; Index < kOverrideCount; ++Index)
            CreateOneOverride(Library, Index);

        unsigned long Reserve[2];
        Reserve[0] = 1;
        Reserve[1] = 0;
        ((IMaterialLibrary*)Library)->SetGlobalRecord(4, Reserve);
    }

    class FFLAppearanceBase : public IFLAppearance
    {
    public:
        class FEngineFace final : public IEngineComponent
        {
        public:
            GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
            unsigned long DACOM_API AddRef() override;
            unsigned long DACOM_API Release() override;
            int DACOM_API Initialize() override;
            int DACOM_API CreateArchetype(void* A, void* B) override { (void)A; (void)B; return GR_OK; }
            int DACOM_API DuplicateArchetype(void* A, void* B) override { (void)A; (void)B; return GR_OK; }
            int DACOM_API DestroyArchetype(void* A) override { (void)A; return GR_OK; }
            int DACOM_API QueryArchetype(void* A, const char* B, void** C) override
            {
                (void)A;
                (void)B;
                (void)C;
                return GR_GENERIC;
            }
            int DACOM_API CreateInstance(void* A, void* B) override { (void)A; (void)B; return GR_OK; }
            int DACOM_API DestroyInstance(void* A) override;
            int DACOM_API UpdateInstance(void* A, void* B) override { (void)A; (void)B; return GR_OK; }
            int DACOM_API QueryInstance(void* A, const char* B, void** C) override
            {
                (void)A;
                (void)B;
                (void)C;
                return GR_GENERIC;
            }
            int DACOM_API Notify(void* A) override { (void)A; return GR_OK; }
        };

        class FInner final : public IDAComponent
        {
        public:
            GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
            unsigned long DACOM_API AddRef() override;
            unsigned long DACOM_API Release() override;
            unsigned long RefCount;
            FFLAppearanceBase* Owner;
        };

        FFLAppearanceBase();
        virtual ~FFLAppearanceBase() noexcept(false);

        GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override = 0;
        unsigned long DACOM_API AddRef() override = 0;
        unsigned long DACOM_API Release() override = 0;
        GENRESULT DACOM_API SetInstanceValue(int Id, void* Value) override;
        GENRESULT DACOM_API GetInstanceValue(int Id, void** Out) override;
        GENRESULT DACOM_API RemoveInstanceValue(int Key) override;
        GENRESULT DACOM_API SetDirectionalLight(const float* First, const float* Second) override;
        GENRESULT DACOM_API SetDirectionalLightEnabled(unsigned char Flag) override;
        GENRESULT DACOM_API GetOverrideMaterial(unsigned long Index, void** Out) override;
        GENRESULT DACOM_API DrawBackdrop(void* A, void* Extra) override;
        GENRESULT DACOM_API DrawStarsphere(void* A, void* Extra) override;
        GENRESULT DACOM_API DrawFadingShadows(void* A, void* B, void* C, void* D) override;
        GENRESULT DACOM_API DrawFixedShadows(void* A, void* B, void* C, void* D) override;
        GENRESULT DACOM_API CountVisibleGeometry(void* A, void* B, void* C) override;

        GENRESULT FinishCreate(FAggDesc18* Desc, void** Instance);
        GENRESULT EngineInitialize();

        FEngineFace EngineFace_;
        IDAComponent* Engine_;
        IDAComponent* System_;
        IRenderer2* Renderer_;
        IRenderPipeline* Pipeline_;
        IVertexBufferManager* VertexBuffers_;
        IDAComponent* TextureLibrary_;
        void* Scratch88_;
        IDAComponent* Outer_;
        FInner Inner_;
    };

    // Native C++ installs the retail pure QI/refcount base phase before callbacks.
    class FFLAppearance final : public FFLAppearanceBase
    {
    public:
        GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override
        {
            return Outer_->QueryInterface(Iid, Out);
        }
        unsigned long DACOM_API AddRef() override { return Outer_->AddRef(); }
        unsigned long DACOM_API Release() override { return Outer_->Release(); }
    };

    FFLAppearanceBase* OwnerFromEngineFace(FFLAppearanceBase::FEngineFace* Face)
    {
        return (FFLAppearanceBase*)((char*)Face -
                                                offsetof(FFLAppearanceBase, EngineFace_));
    }

    FFLAppearanceBase* OwnerFromInner(FFLAppearanceBase::FInner* Inner)
    {
        return Inner->Owner;
    }

    FFLAppearanceBase::FFLAppearanceBase()
        : Engine_(nullptr),
          System_(nullptr),
          Renderer_(nullptr),
          Pipeline_(nullptr),
          VertexBuffers_(nullptr),
          TextureLibrary_(nullptr),
          Scratch88_(nullptr),
          Outer_(nullptr)
    {
        Inner_.RefCount = 1;
        Inner_.Owner = this;
    }

    FFLAppearanceBase::~FFLAppearanceBase() noexcept(false)
    {
        // LAB_066ff8d0: base phase, overrides, direct camera deletion, owner free.
        ReleaseOverrides();
        delete (FCameraBlob*)Scratch88_;
    }

    GENRESULT DACOM_API FFLAppearanceBase::FEngineFace::QueryInterface(const char* Iid, void** Out)
    {
        return OwnerFromEngineFace(this)->QueryInterface(Iid, Out);
    }

    unsigned long DACOM_API FFLAppearanceBase::FEngineFace::AddRef()
    {
        return OwnerFromEngineFace(this)->AddRef();
    }

    unsigned long DACOM_API FFLAppearanceBase::FEngineFace::Release()
    {
        return OwnerFromEngineFace(this)->Release();
    }

    int DACOM_API FFLAppearanceBase::FEngineFace::Initialize()
    {
        return OwnerFromEngineFace(this)->EngineInitialize();
    }

    int DACOM_API FFLAppearanceBase::FEngineFace::DestroyInstance(void* Instance)
    {
        // LAB_066fda20: adjust the receiver and forward to IFLAppearance slot 5.
        return OwnerFromEngineFace(this)->RemoveInstanceValue(
            (int)((unsigned long)(Instance)));
    }

    GENRESULT DACOM_API FFLAppearanceBase::FInner::QueryInterface(const char* Iid, void** Out)
    {
        // LAB_066ff810: the immutable map contains these three owner faces only.
        IDAComponent* Face;
        if (std::strcmp(Iid, Iids::FLAppearance) == 0)
            Face = (IFLAppearance*)Owner;
        else if (std::strcmp(Iid, Iids::AggregateComponent) == 0 ||
                 std::strcmp(Iid, Iids::EngineComponent) == 0)
            Face = (IEngineComponent*)(&Owner->EngineFace_);
        else
        {
            *Out = nullptr;
            return GR_INTERFACE_UNSUPPORTED;
        }
        Face->AddRef();
        *Out = Face;
        return GR_OK;
    }

    unsigned long DACOM_API FFLAppearanceBase::FInner::AddRef()
    {
        RefCount += 1;
        return RefCount;
    }

    unsigned long DACOM_API FFLAppearanceBase::FInner::Release()
    {
        // LAB_066ff8d0: zero skips decrement, then still enters destruction.
        if (RefCount != 0)
            RefCount -= 1;
        const unsigned long Next = RefCount;
        if (Next == 0)
        {
            RefCount = 1;
            if (Owner != nullptr)
            {
                delete Owner;
            }
        }
        return Next;
    }

    GENRESULT FFLAppearanceBase::FinishCreate(FAggDesc18* Desc, void** Instance)
    {
        if (Desc->OuterAt8 != nullptr)
        {
            Outer_ = (IDAComponent*)Desc->OuterAt8;
            *Desc->InnerOutAtC = (IDAComponent*)(&Inner_);
        }
        else
        {
            Outer_ = (IDAComponent*)(&Inner_);
        }

        const GENRESULT EngineQi =
            QueryInterface(Iids::Engine, (void**)(&Engine_));
        if (EngineQi < 0)
        {
            FlmFdump(kFdumpError, kFlmFile, 0xCA, "ERROR",
                     "FLAppearance: Initialize: Unable to acquire IID_IEngine, unable to continue.");
            return GR_GENERIC;
        }
        Engine_->Release();

        System_ = Desc->SystemAt14;
        if (System_ == nullptr)
        {
            FlmFdump(kFdumpError, kFlmFile, 0xD1, "ERROR",
                     "FLAppearance: Initialize: Unable to acquire IID_ISystem, unable to continue.");
            return GR_GENERIC;
        }

        *Instance = this;
        return GR_OK;
    }

    GENRESULT FFLAppearanceBase::EngineInitialize()
    {
        if (System_ == nullptr)
            return GR_GENERIC;

        // Unwind@06700da0: retain the local library across every QI and allocation.
        struct FMaterialLibraryReference
        {
            IDAComponent* Library = nullptr;
            ~FMaterialLibraryReference() noexcept(false)
            {
                if (Library != nullptr)
                {
                    Library->Release();
                    Library = nullptr;
                }
            }
        } Reference;
        GENRESULT Result =
            System_->QueryInterface(Iids::MaterialLibrary, (void**)(&Reference.Library));
        if (Result < 0)
        {
            FlmFdump(kFdumpError, kFlmFile, 0x118, "ERROR",
                     "FLAppearance: Initialize: Unable to acquire IID_IMaterialLibrary, unable to continue.");
            return GR_GENERIC;
        }

        Result = System_->QueryInterface(Iids::RenderPipeline8B, (void**)(&Pipeline_));
        if (Result < 0)
        {
            FlmFdump(kFdumpError, kFlmFile, 0x11E, "ERROR",
                     "FLAppearance: Initialize: Unable to acquire IID_IRenderPipeline, unable to continue.");
            return GR_GENERIC;
        }
        // 0x066fdacc..0x066fdad1: released unconditionally after the successful QI.
        Pipeline_->Release();

        Result =
            System_->QueryInterface(Iids::VertexBufferManager, (void**)&VertexBuffers_);
        if (Result < 0)
        {
            FlmFdump(kFdumpError, kFlmFile, 0x125, "ERROR",
                     "FLAppearance: Initialize: Unable to acquire IID_IVertexBufferManager, unable to continue.");
            return GR_GENERIC;
        }
        VertexBuffers_->Release();

        Result =
            System_->QueryInterface(Iids::TextureLibraryA, (void**)(&TextureLibrary_));
        if (Result < 0)
        {
            FlmFdump(kFdumpError, kFlmFile, 300, "ERROR",
                     "FLAppearance: Initialize: Unable to acquire IID_ITextureLibrary, unable to continue.");
            return GR_GENERIC;
        }
        TextureLibrary_->Release();

        Result = Engine_->QueryInterface(Iids::Renderer, (void**)&Renderer_);
        if (Result < 0)
        {
            FlmFdump(kFdumpError, kFlmFile, 0x133, "ERROR",
                     "FLAppearance: Initialize: Unable to acquire IID_IRenderer, unable to continue.");
            return GR_GENERIC;
        }
        Renderer_->Release();

        // FUN_066FCA80's first statement (DAT_06704630 = arg). See flmaterials.h.
        G_FlmEngineOuter = Engine_;
        CreateOverrides(Reference.Library);

        FCameraBlob* Camera = new (std::nothrow) FCameraBlob();
        Scratch88_ = Camera;
        return GR_OK;
    }

    GENRESULT DACOM_API FFLAppearanceBase::SetInstanceValue(int Id, void* Value)
    {
        if (Id == -1)
            return (GENRESULT)-1;
        // LAB_066fdf20: unique insert with initial null value, then assign caller value.
        G_IdValues[Id] = Value;
        return GR_OK;
    }

    GENRESULT DACOM_API FFLAppearanceBase::GetInstanceValue(int Id, void** Out)
    {
        if (Id == -1)
            return (GENRESULT)-1;
        std::map<int, void*>::const_iterator Found = G_IdValues.find(Id);
        if (Found == G_IdValues.end())
            return (GENRESULT)-1;
        *Out = Found->second;
        return GR_OK;
    }

    GENRESULT DACOM_API FFLAppearanceBase::RemoveInstanceValue(int Key)
    {
        if (Key == -1)
            return (GENRESULT)-1;
        G_IdValues.erase(Key);
        return GR_OK;
    }

    GENRESULT DACOM_API FFLAppearanceBase::SetDirectionalLight(const float* First, const float* Second)
    {
        // LAB_066fe1f0: flag precedes both mandatory vector reads.
        G_Slot6Flag = 1;
        CopyVec3(G_Slot6First, First);
        CopyVec3(G_Slot6Second, Second);
        return GR_OK;
    }

    GENRESULT DACOM_API FFLAppearanceBase::SetDirectionalLightEnabled(unsigned char Flag)
    {
        G_Slot6Flag = Flag;
        return GR_OK;
    }

    GENRESULT DACOM_API FFLAppearanceBase::GetOverrideMaterial(unsigned long Index, void** Out)
    {
        *Out = G_Overrides[Index];
        if (*Out != nullptr)
            return GR_OK;
        return (GENRESULT)-1;
    }

    void CallSetPerspective(IRenderPipeline* Pipeline, FCameraBlob* Camera)
    {
        // Retail evaluates these virtual calls in push order, rounding each to float.
        const float FarPlane = Camera->GetZFar();
        const float NearPlane = Camera->GetZNear();
        const float Aspect = Camera->GetAspect();
        const float Fov = Camera->GetFovYDeg();
        PipelineSetPerspective(Pipeline, Fov, Aspect, NearPlane, FarPlane);
    }

    double CameraAxisLength(const float* Axis) // FUN_066fe480
    {
        const double X = Axis[0];
        const double Y = Axis[1];
        const double Z = Axis[2];
        return std::sqrt((X * X + Y * Y) + Z * Z);
    }

    void FillLocalAxes(FCameraBlob* Camera)
    {
        const int* Viewport = Camera->Inner()->GetViewport();
        const long Width = (long)Viewport[2] - (long)Viewport[0];
        const float HalfWidth = (float)((double)Width * kHalf);
        const float Horizontal = (float)((double)HalfWidth / Camera->GetXScaleOverNear());
        const float Vertical = (float)((double)Horizontal / Camera->GetAspect());
        float* Right = Camera->Vec6C();
        Right[0] = 1.0f;
        Right[1] = 0.0f;
        Right[2] = Horizontal;
        const double RightScale = (double)kCameraNearConst / CameraAxisLength(Right);
        Right[0] = (float)(RightScale * Right[0]);
        Right[1] = (float)(RightScale * Right[1]);
        Right[2] = (float)(RightScale * Right[2]);
        float* Up = Camera->Vec78();
        Up[0] = 0.0f;
        Up[1] = 1.0f;
        Up[2] = Vertical;
        const double UpScale = (double)kCameraNearConst / CameraAxisLength(Up);
        Up[0] = (float)(UpScale * Up[0]);
        Up[1] = (float)(UpScale * Up[1]);
        Up[2] = (float)(UpScale * Up[2]);
    }

    class FAppearanceRenderStateScope
    {
    public:
        explicit FAppearanceRenderStateScope(IRenderPipeline* Pipeline)
            : Pipeline(Pipeline)
        {
            PipelineGetRenderState(Pipeline, D3DRS_AMBIENT, &Saved);
            PipelineSetRenderState(Pipeline, D3DRS_AMBIENT, (unsigned long)-1);
        }
        ~FAppearanceRenderStateScope() noexcept(false) { PipelineSetRenderState(Pipeline, D3DRS_AMBIENT, Saved); }
    private:
        IRenderPipeline* Pipeline;
        unsigned long Saved;
    };

    struct FShadowTextureScope // FUN_066ff260
    {
        explicit FShadowTextureScope(ITextureLibrary2* Library) : Library(Library), Record(nullptr) {}
        ~FShadowTextureScope() noexcept(false)
        {
            if (Record != nullptr)
            {
                Library->ReleaseTextureRecord(Record);
                Record = nullptr;
            }
        }
        ITextureLibrary2* Library;
        void* Record;
    };

    struct FShadowRenderStateScope // FUN_066f3080
    {
        FShadowRenderStateScope(IRenderPipeline* Pipeline, D3DRENDERSTATETYPE State, unsigned long Value)
            : Pipeline(Pipeline), State(State)
        {
            PipelineGetRenderState(Pipeline, State, &Saved);
            PipelineSetRenderState(Pipeline, State, Value);
        }
        ~FShadowRenderStateScope() noexcept(false) { PipelineSetRenderState(Pipeline, State, Saved); }
        IRenderPipeline* Pipeline;
        D3DRENDERSTATETYPE State;
        unsigned long Saved;
    };

    struct FShadowInterfacesScope
    {
        FShadowInterfacesScope(IRenderPipeline* Pipeline, IVertexBufferManager* Manager)
            : Pipeline(Pipeline), Manager(Manager)
        {
            if (Pipeline != nullptr)
                Pipeline->AddRef();
            if (Manager != nullptr)
                Manager->AddRef();
        }
        ~FShadowInterfacesScope() noexcept(false)
        {
            if (Pipeline != nullptr)
            {
                Pipeline->Release();
                Pipeline = nullptr;
            }
            if (Manager != nullptr)
            {
                Manager->Release();
                Manager = nullptr;
            }
        }
        IRenderPipeline* Pipeline;
        IVertexBufferManager* Manager;
    };

    struct FShadowInstance
    {
        void* Id;
        float Radius;
    };


    struct FShadowCorner
    {
        float X;
        float Y;
        float Z;
        unsigned long Color;
        float U;
        float V;
    };

    struct FShadowVertWriter
    {
        char* Xyz;
        char* Color;
        char* Uv;
        int Stride;
        int Written;
        int MaxVerts;
        unsigned long ColorValue;
        float U;
        float V;
        unsigned long ExpanderCount;
        FShadowCorner Saved[2];
    };

    void ShadowWriterReset(FShadowVertWriter* Writer, char* Dest, int MaxVerts, unsigned long Fvf)
    {
        Writer->Xyz = Dest;
        const unsigned long PositionBytes = (Fvf & 0xC) == 0xC ? 28 :
            ((Fvf & 4) != 0 ? 16 : ((Fvf & 2) != 0 ? 12 : 0));
        const unsigned long NormalBytes = (Fvf & 0x10) != 0 ? 12 : 0;
        const unsigned long ColorBytes = ((Fvf & 0x40) != 0 ? 4 : 0) + ((Fvf & 0x80) != 0 ? 4 : 0);
        unsigned long TextureBytes = 0;
        for (unsigned long Index = 0; Index < ((Fvf >> 8) & 15); ++Index)
        {
            const unsigned long Lane = (Fvf >> ((16 + Index * 2) & 31)) & 3;
            TextureBytes += Lane == 0 ? 8 : (Lane == 1 ? 12 : (Lane == 2 ? 16 : 4));
        }
        Writer->Color = Dest + PositionBytes + NormalBytes;
        Writer->Uv = Dest + PositionBytes + NormalBytes + ColorBytes;
        Writer->Stride = (int)(PositionBytes + NormalBytes + ColorBytes + TextureBytes);
        Writer->Written = 0;
        Writer->MaxVerts = MaxVerts;
        Writer->ColorValue = 0xFFFFFFFF;
        Writer->U = 0.0f;
        Writer->V = 0.0f;
        Writer->ExpanderCount = 0;
    }

    void ShadowWriterRaw(FShadowVertWriter* Writer, const FShadowCorner* Corner)
    {
        float* Xyz = (float*)Writer->Xyz;
        Xyz[0] = Corner->X;
        Xyz[1] = Corner->Y;
        Xyz[2] = Corner->Z;
        Writer->Xyz += Writer->Stride;
        *(unsigned long*)Writer->Color = Corner->Color;
        Writer->Color += Writer->Stride;
        float* Uv = (float*)Writer->Uv;
        Uv[0] = Corner->U;
        Uv[1] = Corner->V;
        Writer->Uv += Writer->Stride;
        Writer->Written += 1;
    }

    // FUN_066FF460 + bit4 expander: 4 corners → 6-vert triangle list (0,1,2)+(3,0,2).
    void ShadowWriterEmit(FShadowVertWriter* Writer, float X, float Y, float Z)
    {
        FShadowCorner Corner;
        Corner.X = X;
        Corner.Y = Y;
        Corner.Z = Z;
        Corner.Color = Writer->ColorValue;
        Corner.U = Writer->U;
        Corner.V = Writer->V;
        ShadowWriterRaw(Writer, &Corner);
        const unsigned long Index = Writer->ExpanderCount;
        if ((Index & 1) == 0)
            Writer->Saved[Index >> 1] = Corner;
        Writer->ExpanderCount = Index + 1;
        if (Writer->ExpanderCount > 3)
        {
            Writer->ExpanderCount = 0;
            ShadowWriterRaw(Writer, &Writer->Saved[0]);
            ShadowWriterRaw(Writer, &Writer->Saved[1]);
        }
    }

    GENRESULT DrawShadowLike(FFLAppearanceBase* Appearance, unsigned char FlagA, unsigned char FlagB,
                             unsigned char FlagC, unsigned char FlagD, void* A, void* B, void* C,
                             void* D)
    {
        (void)D;
        unsigned long Ability = 0;
        PipelineQueryDeviceAbility(Appearance->Pipeline_, 9, &Ability);
        if (Ability == 0)
            return (GENRESULT)-1;
        const int InstanceCount = (int)((unsigned long)(C));
        if (InstanceCount == 0)
            return GR_OK;

        FShadowTextureScope Texture((ITextureLibrary2*)Appearance->TextureLibrary_);
        unsigned long Handle = 0;
        unsigned long Crc[2] = {(unsigned long)("efxshadow"), DACOM_CRC::GetCRC32("efxshadow")};
        const GENRESULT Found = Texture.Library->FindTextureByCrc(Crc, &Texture.Record);
        if (Found < 0)
        {
            FDUMP(0x100002, "%s(%d) : WARNING:General:%s",
                  "E:\\FL\\Scratch\\Source\\FLMaterials\\shadows_inl.h", 0x24,
                  "couldnt find texture 'efxshadow' (from efx.txm?)");
        }
        else
        {
            unsigned long Frame[5];
            if (Texture.Library->GetTextureFrame(Texture.Record, 0, Frame) >= 0)
                Handle = Frame[0];
        }
        if (Handle == 0)
            return (GENRESULT)-1;

        PipelineApplyRsTable(Appearance->Pipeline_, kShadowDrawRs);
        PipelineSetModelview(Appearance->Pipeline_, ((ICamera*)A)->GetViewTransform());
        FShadowRenderStateScope DepthFunction(Appearance->Pipeline_, D3DRS_ZFUNC, 4); // formerly Cull
        FShadowRenderStateScope CullMode(Appearance->Pipeline_, D3DRS_CULLMODE, 1); // formerly ZWrite
        FShadowRenderStateScope DepthBias(Appearance->Pipeline_, D3DRS_ZBIAS, 4); // formerly Fog
        FShadowRenderStateScope DepthWrite(Appearance->Pipeline_, D3DRS_ZWRITEENABLE, 0); // formerly Lighting
        PipelineSetTextureStageState(Appearance->Pipeline_, 0, D3DTSS_TEXCOORDINDEX, 0);
        PipelineSetTextureStageTexture(Appearance->Pipeline_, 0, Handle);
        PipelineApplyTssTable(Appearance->Pipeline_, 0, kShadowDrawTss0);
        PipelineApplyTssTable(Appearance->Pipeline_, 1, kShadowDrawTss1);

        FShadowInterfacesScope Interfaces(Appearance->Pipeline_, Appearance->VertexBuffers_);
        const unsigned long QuadVertsBits = (unsigned long)InstanceCount * 4;
        const int QuadVerts = (int)QuadVertsBits;
        const unsigned long LockCount = QuadVertsBits + (unsigned long)(QuadVerts / 4) * 2;
        if (QuadVerts != 0)
        {
            FVertexMemoryLock LockOut = {{nullptr, kShadowFvf, 0}, nullptr};
            if (Interfaces.Manager->LockVertexMemory(kShadowFvf, LockCount, &LockOut) == GR_OK)
            {
                FShadowVertWriter Writer;
                ShadowWriterReset(&Writer, (char*)LockOut.Vertices, (int)LockCount, LockOut.Listing.Fvf);
                const FShadowInstance* Instances = (const FShadowInstance*)B;
                for (int Index = 0; Index < InstanceCount; ++Index)
                {
                    IEngine* Engine = (IEngine*)Appearance->Engine_;
                    float* Transform = Engine->GetInstanceTransform(Instances[Index].Id);
                    float PosX = Transform[9];
                    float PosY = Transform[10];
                    float PosZ = Transform[11];
                    void* Archetype = Engine->GetInstanceArchetype(Instances[Index].Id);
                    Engine->ReleaseHandle(Archetype);
                    float Extents[6];
                    bool bUsedFallback = true;
                    if (FlagD != 0 && Archetype != (void*)((unsigned long)-1))
                        bUsedFallback = Appearance->Renderer_->GetBounds(Archetype, 1.0f, Extents) < 0;
                    if (bUsedFallback)
                    {
                        Extents[0] = kShadowExtentX;
                        Extents[1] = -kShadowExtentX;
                        Extents[2] = 0.0f;
                        Extents[3] = kShadowExtentAdd;
                        Extents[4] = kShadowExtentZ;
                        Extents[5] = -kShadowExtentZ;
                        const float OffsetX = (float)((double)kShadowZBias * Transform[2]);
                        const float OffsetY = (float)((double)kShadowZBias * Transform[5]);
                        const float OffsetZ = (float)((double)kShadowZBias * Transform[8]);
                        PosX = (float)((double)OffsetX + PosX);
                        PosY = (float)((double)OffsetY + PosY);
                        PosZ = (float)((double)OffsetZ + PosZ);
                    }
                    float Radius = Instances[Index].Radius;
                    if (FlagC != 0)
                        Radius = (float)((double)Radius + Extents[3]);
                    double Fade = (double)PosY - Radius;
                    // TEST AH,05 + JP preserves positive/equal/unordered; negative becomes zero.
                    if (Fade < 0.0)
                        Fade = 0.0;
                    if (G_ShadowFadeForce != 0)
                        Fade = kShadowFadeForce;
                    // TEST AH,41 + JZ skips only ordered-greater values.
                    if (Fade > kShadowFadeRange)
                        continue;
                    Fade /= kShadowFadeRange;
                    if (FlagB != 0)
                        G_ShadowQuadScale = (float)((Fade * Fade) * kShadowFadeQuad + kShadowScaleBias);
                    double Alpha = kShadowAlphaConst;
                    if (FlagA != 0)
                    {
                        Alpha = (double)kShadowAlphaSub - Fade;
                        // TEST AH,05 + JNP skips negative; TEST AH,41 + JNZ retains <=1/unordered.
                        if (Alpha < 0.0)
                            continue;
                        if (Alpha > 1.0)
                            Alpha = 1.0;
                    }
                    // TEST AH,41 + JNZ skips <=0 and unordered.
                    if (!(Alpha > 0.0))
                        continue;
                    const int ColorByte = (int)((Alpha * Alpha) * kShadowColorScale);
                    Writer.ColorValue = ((unsigned long)ColorByte << 24) | 0x00FFFFFF;
                    const double Angle = std::atan2((double)Transform[2], (double)Transform[8]);
                    const double SinA = std::sin(Angle) * G_ShadowQuadScale;
                    const double CosA = std::cos(Angle) * G_ShadowQuadScale;
                    const double WidthX = (double)Extents[0] - Extents[1];
                    const float WidthZ = (float)((double)Extents[4] - Extents[5]);
                    const float CenterX = (float)((0.5 * WidthX + Extents[1]) + PosX);
                    const float CenterZ = (float)(((double)WidthZ * 0.5 + Extents[5]) + PosZ);
                    const float Hx = (float)(WidthX * CosA);
                    const float Hy = (float)(WidthX * SinA);
                    const float Zx = (float)((double)WidthZ * CosA);
                    const float Zy = (float)((double)WidthZ * SinA);
                    const float Left = (float)((double)CenterX - Hx);
                    Writer.U = 0.0f;
                    Writer.V = 0.0f;
                    ShadowWriterEmit(&Writer, (float)((double)Left + Zy), Radius,
                        (float)(((double)Zx + Hy) + CenterZ));
                    Writer.U = 0.0f;
                    Writer.V = 1.0f;
                    ShadowWriterEmit(&Writer, (float)((double)Left - Zy), Radius,
                        (float)(((double)Hy + CenterZ) - Zx));
                    const double Back = (double)CenterZ - Hy;
                    const float BackStored = (float)Back;
                    Writer.U = 1.0f;
                    Writer.V = 1.0f;
                    ShadowWriterEmit(&Writer, (float)(((double)Hx + CenterX) - Zy), Radius,
                        (float)(Back - Zx));
                    Writer.U = 1.0f;
                    Writer.V = 0.0f;
                    ShadowWriterEmit(&Writer, (float)(((double)Zy + Hx) + CenterX), Radius,
                        (float)((double)BackStored + Zx));
                }
                Interfaces.Manager->UnlockVertexMemory(&LockOut);
                if (Writer.Written != 0)
                    PipelineDrawPrimitiveVb(Interfaces.Pipeline, 4, LockOut.Listing.VertexBuffer, LockOut.Listing.BaseVertex,
                        (unsigned long)Writer.Written, nullptr);
            }
        }
        return GR_OK;
    }

    GENRESULT DACOM_API FFLAppearanceBase::DrawBackdrop(void* A, void* Extra)
    {
        if (Renderer_ == nullptr)
            return GR_OK;
        FCameraBlob* Camera = (FCameraBlob*)Scratch88_;
        IDAComponent* const Override = G_Overrides[5];
        Camera->BindArg(A, 1, 0);
        CallSetPerspective(Pipeline_, Camera);
        FillLocalAxes(Camera);
        if (Camera->Byte9() != 0)
        {
            const float Translation[3] = {};
            Camera->SetOrientationAndTranslation(((ICamera*)A)->GetOrientation(), Translation);
            float Inverse[12];
            Camera->BuildInverseTransform(Inverse);
            float* Dest = Camera->TransformB();
            for (int Index = 0; Index < 12; ++Index)
                Dest[Index] = Inverse[Index];
        }
        // LAB_066fe41c: Inst is the second argument, Extra is the Backdrop override.
        IRenderer2* const Renderer = Renderer_;
        Camera = (FCameraBlob*)Scratch88_;
        Renderer->RenderInstance(Camera, Extra, 1.0f, 0, nullptr, Override);
        Camera = (FCameraBlob*)Scratch88_;
        if (Camera->Byte8() != 0)
        {
            Camera->SetByte8(0);
            CallSetPerspective(Pipeline_, Camera);
        }
        Camera->ClearArg();
        return GR_OK;
    }

    GENRESULT DACOM_API FFLAppearanceBase::DrawStarsphere(void* A, void* Extra)
    {
        if (Renderer_ == nullptr)
            return GR_OK;
        // 0x066fe598..0x066fe5cb: the pipeline (+0x14) is used with no null test.
        FAppearanceRenderStateScope SavedState(Pipeline_);
        IDAComponent* const Override = G_Overrides[6];
        FCameraBlob* Camera = (FCameraBlob*)Scratch88_;
        Camera->BindArg(A, 0, 1);
        {
            const float* Orientation = ((ICamera*)A)->GetOrientation();
            float Translation[3];
            Translation[0] = 0.0f;
            Translation[1] = 0.0f;
            Translation[2] = 0.0f;
            Camera->SetOrientationAndTranslation(Orientation, Translation);
            float Inverse[12];
            Camera->BuildInverseTransform(Inverse);
            float* Dest = Camera->TransformB();
            for (int Index = 0; Index < 12; ++Index)
                Dest[Index] = Inverse[Index];
        }
        // LAB_066fe667: Inst is the second argument, Extra is the Starsphere override.
        IRenderer2* const Renderer = Renderer_;
        Camera = (FCameraBlob*)Scratch88_;
        Renderer->RenderInstance(Camera, Extra, 1.0f, 0x80, nullptr, Override);
        Camera = (FCameraBlob*)Scratch88_;
        if (Camera->Byte8() != 0)
        {
            Camera->SetByte8(0);
            CallSetPerspective(Pipeline_, Camera);
        }
        Camera->ClearArg();
        return GR_OK;
    }

    GENRESULT DACOM_API FFLAppearanceBase::DrawFadingShadows(void* A, void* B, void* C, void* D)
    {
        DrawShadowLike(this, G_Slot11FlagA, G_Slot11FlagB, G_Slot11FlagC, G_Slot11FlagD, A, B, C, D);
        return GR_OK;
    }

    GENRESULT DACOM_API FFLAppearanceBase::DrawFixedShadows(void* A, void* B, void* C, void* D)
    {
        DrawShadowLike(this, G_Slot12FlagA, G_Slot12FlagB, G_Slot12FlagC, G_Slot12FlagD, A, B, C, D);
        return GR_OK;
    }

    GENRESULT DACOM_API FFLAppearanceBase::CountVisibleGeometry(void* A, void* B, void* C)
    {
        void* Visible = nullptr;
        const GENRESULT Got = GetOverrideMaterial(8, &Visible);
        if (Got < 0)
            return GR_OK;
        FlmVisibleCounterReset();
        // FUN_066ff620: both renderer and result destination are mandatory.
        Renderer_->RenderInstance(A, B, 1.0f, 0x80, nullptr, Visible);
        FlmVisibleCounterCopy((unsigned long*)C);
        return GR_OK;
    }
}

I3DMathEngine* FlmCreateMathEngine() // FUN_067006d0, shared by all three retail callers
{
    FMathDesc Desc = {0x0C, G_Name3DMathEngine, nullptr};
    ICOManager* Manager = DACOM_Acquire();
    void* Created = nullptr;
    Manager->CreateInstance((AGGDESC*)(&Desc), &Created);
    return (I3DMathEngine*)Created;
}

IDAComponent* G_FlmEngineOuter = nullptr;

bool FlmLookupIdValue(int Id, void** Out)
{
    if (Id == -1)
        return false;
    std::map<int, void*>::const_iterator Found = G_IdValues.find(Id);
    if (Found == G_IdValues.end())
        return false;
    *Out = Found->second;
    return true;
}

GENRESULT FlmCreateAppearanceInstance(FAggDesc18* Desc, void** Instance)
{
    FFLAppearance* Appearance = new (std::nothrow) FFLAppearance();
    if (Appearance == nullptr)
    {
        *Instance = nullptr;
        return GR_OUT_OF_MEMORY;
    }
    const GENRESULT Result = Appearance->FinishCreate(Desc, Instance);
    if (Result < 0)
    {
        // LAB_066fd92a: clear published inner before base destruction callbacks.
        if (Desc->InnerOutAtC != nullptr)
            *Desc->InnerOutAtC = nullptr;
        delete Appearance;
        *Instance = nullptr;
    }
    return Result;
}
