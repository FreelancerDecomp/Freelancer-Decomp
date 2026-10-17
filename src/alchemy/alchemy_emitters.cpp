// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "alchemy_emitters.h"

#include "alchemy_common.h"
#include "alchemy_math.h"
#include "alchemy_ramp.h"

#include <cmath>
#include <cstring>

// alchemy.dll -- the shared emitter body and the three generic emitters.
// See alchemy_emitters.h for the retail vtable map. Everything here was
// re-traced against alchemy.dll.txt on 2026-09-26 (ALC-1) and again, term by term with
// the x87 precision, on 2026-09-30 (ALC-5): values that stay on the FPU stack are
// double here, every FSTP float is a cast to float, and the properties are the ramps of
// the emitter's property set (ALC-6: the set objects themselves, shared with clones).
namespace AlchemyImpl
{
    namespace
    {
        I3DMathEngine* G_ConeEmitter3DMath = nullptr; // DAT_06257a30.
        I3DMathEngine* G_CubeEmitter3DMath = nullptr; // DAT_06257a34.
        I3DMathEngine* G_SphereEmitter3DMath = nullptr; // DAT_06257ba4.

        // Emitter property hashes (descriptor table 0x06257700, records of
        // {name, hash, kind, min, max}); descriptor-inner slot in parentheses.
        const unsigned long kEmitterLodCurveHash = 0xFE077E05;          // +0x68, kind 0x200
        const unsigned long kEmitterInitialParticlesHash = 0x0F9A9D52;  // +0x64, kind 2 (int)
        const unsigned long kEmitterFrequencyHash = 0x023C350C;         // +0x4c
        const unsigned long kEmitterMaxParticlesHash = 0xF5042852;      // +0x50
        const unsigned long kEmitterEmitCountHash = 0xE7221F95;         // +0x5c (keyed bursts)
        const unsigned long kEmitterInitLifeSpanHash = 0x0A635880;      // +0x54
        const unsigned long kEmitterPressureHash = 0x0AB180C5;          // +0x58
        const unsigned long kEmitterVelocityApproachHash = 0x0681233E;  // +0x60

        // Shape properties (per-type property sets, slots +0x6c..).
        const unsigned long kConeMinRadiusHash = 0x1C73D16B;   // +0x6c
        const unsigned long kConeMaxRadiusHash = 0xF636F07F;   // +0x70
        const unsigned long kConeMinSpreadHash = 0x100E7F55;   // +0x74 (degrees)
        const unsigned long kConeMaxSpreadHash = 0xFA4B5E41;   // +0x78 (degrees)
        const unsigned long kCubeWidthHash = 0xFD22A11C;       // +0x6c
        const unsigned long kCubeHeightHash = 0xEFB0F8CF;      // +0x70
        const unsigned long kCubeDepthHash = 0x14B74EB2;       // +0x74
        const unsigned long kCubeMinSpreadHash = 0xE282006B;   // +0x78 (degrees)
        const unsigned long kCubeMaxSpreadHash = 0x08C7217F;   // +0x7c (degrees)
        const unsigned long kSphereMinRadiusHash = 0xF231FF60; // +0x6c
        const unsigned long kSphereMaxRadiusHash = 0x1874DE74; // +0x70

        // The property tables the sets enumerate (.data 0x06257700 emitter, 0x06255e28
        // cone, 0x06255f30 cube, 0x06256c4c sphere; {name, hash, kind, min, max}).
        const FNodePropertyRow kEmitterPropertyRows[] = {
            {"Emitter_LODCurve", 0xFE077E05U, 0x200, 0.0f, 1.0f},
            {"Emitter_InitialParticles", 0x0F9A9D52U, 0x2, 0.0f, 1000.0f},
            {"Emitter_Frequency", 0x023C350CU, 0x202, 0.0f, kNodePropertyRowMax},
            {"Emitter_MaxParticles", 0xF5042852U, 0x202, 0.0f, kNodePropertyRowMax},
            {"Emitter_EmitCount", 0xE7221F95U, 0x202, 0.0f, 5000.0f},
            {"Emitter_InitLifeSpan", 0x0A635880U, 0x202, 0.0f, kNodePropertyRowMax},
            {"Emitter_Pressure", 0x0AB180C5U, 0x202, -kNodePropertyRowMax, kNodePropertyRowMax},
            {"Emitter_VelocityApproach", 0x0681233EU, 0x202, 0.0f, 1.0f},
            {nullptr, 0, 0, 0.0f, 1.0f},
        };
        const FNodePropertyRow kConePropertyRows[] = {
            {"ConeEmitter_MinRadius", 0x1C73D16BU, 0x202, 0.0f, kNodePropertyRowMax},
            {"ConeEmitter_MaxRadius", 0xF636F07FU, 0x202, 0.0f, kNodePropertyRowMax},
            {"ConeEmitter_MinSpread", 0x100E7F55U, 0x202, 0.0f, 90.0f},
            {"ConeEmitter_MaxSpread", 0xFA4B5E41U, 0x202, 0.0f, 90.0f},
            {nullptr, 0, 0, 0.0f, 1.0f},
        };
        const FNodePropertyRow kCubePropertyRows[] = {
            {"CubeEmitter_Width", 0xFD22A11CU, 0x202, 0.0f, kNodePropertyRowMax},
            {"CubeEmitter_Height", 0xEFB0F8CFU, 0x202, 0.0f, kNodePropertyRowMax},
            {"CubeEmitter_Depth", 0x14B74EB2U, 0x202, 0.0f, kNodePropertyRowMax},
            {"CubeEmitter_MinSpread", 0xE282006BU, 0x202, 0.0f, 90.0f},
            {"CubeEmitter_MaxSpread", 0x08C7217FU, 0x202, 0.0f, 90.0f},
            {nullptr, 0, 0, 0.0f, 1.0f},
        };
        const FNodePropertyRow kSpherePropertyRows[] = {
            {"SphereEmitter_MinRadius", 0xF231FF60U, 0x202, 0.0f, kNodePropertyRowMax},
            {"SphereEmitter_MaxRadius", 0x1874DE74U, 0x202, 0.0f, kNodePropertyRowMax},
            {nullptr, 0, 0, 0.0f, 1.0f},
        };

        // The shared ramp-getter hit (e.g. FUN_06245a20 0x06245a82..0x06245a92): *Out =
        // the ramp, AddRef, 0.
        GENRESULT HandOutRamp(INestedSingleRamp* Ramp, INestedSingleRamp** Out)
        {
            *Out = Ramp;
            Ramp->AddRef();
            return GR_OK;
        }

        const float kTwoPi = 6.2831855f;     // DAT_0624d4c8
        const float kDegToRad = 0.017453292f; // DAT_0624d4c4

        // FUN_0623bf90 (thiscall on the vector): L = (x*x + y*y) + z*z FSTP float; one
        // Newton step of the 0x5f3759df inverse square root, s = (1.5f - ((y*y)*L)*0.5f)
        // * y kept in the FPU (DAT_0624bab8 = 0.5, DAT_0624bac0 = 1.5); v[i] = (float)(s *
        // v[i]).
        __declspec(noinline) void FastNormalize3(float* Vec)
        {
            float LengthSq;
            StoreAlchemyFpuFloat(LengthSq, static_cast<double>(Vec[0]) * Vec[0] +
                                              static_cast<double>(Vec[1]) * Vec[1] +
                                              static_cast<double>(Vec[2]) * Vec[2]);
            long Bits = 0;
            std::memcpy(&Bits, &LengthSq, sizeof(Bits));
            Bits = 0x5F3759DF - (Bits >> 1);
            float Guess = 0.0f;
            std::memcpy(&Guess, &Bits, sizeof(Guess));
            const double Scale =
                (1.5 - static_cast<double>(Guess) * Guess * LengthSq * 0.5) * Guess;
            StoreAlchemyFpuFloat(Vec[0], Scale * Vec[0]);
            StoreAlchemyFpuFloat(Vec[1], Scale * Vec[1]);
            StoreAlchemyFpuFloat(Vec[2], Scale * Vec[2]);
        }

        // The cone / cube direction (0621bab7..0621badd, cube 0621d467..0621d491): the
        // spread stays in the FPU; FSIN of it is reused for x and z.
        //   x = (float)(cos(Az) * sin(Sp)); y = (float)cos(Sp); z = (float)-(sin(Az) * sin(Sp)).
        __declspec(noinline) void SpreadDirection(float Azimuth, double Spread, float* Dir)
        {
            const double SinSpread = ComputeAlchemyFpuSine(Spread);
            const double AzimuthExt = Azimuth;
            StoreAlchemyFpuFloat(Dir[0], ComputeAlchemyFpuCosine(AzimuthExt) * SinSpread);
            StoreAlchemyFpuFloat(Dir[1], ComputeAlchemyFpuCosine(Spread));
            StoreAlchemyFpuFloat(Dir[2], -(ComputeAlchemyFpuSine(AzimuthExt) * SinSpread));
        }
    } // namespace

    // ---- FFxEmitterBase ------------------------------------------------------

    // ---- Emitter property sets ------------------------------------------------

    // FUN_062455b0 (after FUN_0623f7b0): +0x4c FxAnimatedSingleRamp (FUN_06212db0) whose
    // key 0 is FUN_06209ad0(100.0); +0x50 (FUN_06206ce0) 10000.0; +0x54 2.0; +0x58 10.0;
    // +0x5c 0; +0x60 0; +0x64 = 0; +0x68 FUN_06217c20(1.0) (FxRampSingleRamp with an
    // FxRampSingle child in mode 4 keyed 1.0).
    FFxEmitterPropertySet::FFxEmitterPropertySet()
        : FrequencyRamp(nullptr), MaxParticlesRamp(nullptr), InitLifeSpanRamp(nullptr),
          PressureRamp(nullptr), EmitCountRamp(nullptr), VelocityApproachRamp(nullptr),
          InitialParticles(0), LodCurveRamp(nullptr)
    {
        MakeAnimatedSingleRamp(100.0f, &FrequencyRamp);
        MakeAnimatedSingleRamp(10000.0f, &MaxParticlesRamp);
        MakeAnimatedSingleRamp(2.0f, &InitLifeSpanRamp);
        MakeAnimatedSingleRamp(10.0f, &PressureRamp);
        MakeAnimatedSingleRamp(0.0f, &EmitCountRamp);
        MakeAnimatedSingleRamp(0.0f, &VelocityApproachRamp);
        MakeRampSingleRamp(1.0f, &LodCurveRamp);
    }

    FFxEmitterPropertySet::~FFxEmitterPropertySet()
    {
        ReleaseDescriptorSlot(FrequencyRamp);
        ReleaseDescriptorSlot(MaxParticlesRamp);
        ReleaseDescriptorSlot(InitLifeSpanRamp);
        ReleaseDescriptorSlot(PressureRamp);
        ReleaseDescriptorSlot(EmitCountRamp);
        ReleaseDescriptorSlot(VelocityApproachRamp);
        ReleaseDescriptorSlot(LodCurveRamp);
    }

    // FUN_0621b490: four FxAnimatedSingleRamps keyed 0, 1, 0, 0.
    FFxConeEmitterPropertySet::FFxConeEmitterPropertySet()
        : MinRadiusRamp(nullptr), MaxRadiusRamp(nullptr), MinSpreadRamp(nullptr),
          MaxSpreadRamp(nullptr)
    {
        MakeAnimatedSingleRamp(0.0f, &MinRadiusRamp);
        MakeAnimatedSingleRamp(1.0f, &MaxRadiusRamp);
        MakeAnimatedSingleRamp(0.0f, &MinSpreadRamp);
        MakeAnimatedSingleRamp(0.0f, &MaxSpreadRamp);
    }

    FFxConeEmitterPropertySet::~FFxConeEmitterPropertySet()
    {
        ReleaseDescriptorSlot(MinRadiusRamp);
        ReleaseDescriptorSlot(MaxRadiusRamp);
        ReleaseDescriptorSlot(MinSpreadRamp);
        ReleaseDescriptorSlot(MaxSpreadRamp);
    }

    // FUN_0621cdd0: five FxAnimatedSingleRamps keyed 5, 5, 5, 0, 15.
    FFxCubeEmitterPropertySet::FFxCubeEmitterPropertySet()
        : WidthRamp(nullptr), HeightRamp(nullptr), DepthRamp(nullptr), MinSpreadRamp(nullptr),
          MaxSpreadRamp(nullptr)
    {
        MakeAnimatedSingleRamp(5.0f, &WidthRamp);
        MakeAnimatedSingleRamp(5.0f, &HeightRamp);
        MakeAnimatedSingleRamp(5.0f, &DepthRamp);
        MakeAnimatedSingleRamp(0.0f, &MinSpreadRamp);
        MakeAnimatedSingleRamp(15.0f, &MaxSpreadRamp);
    }

    FFxCubeEmitterPropertySet::~FFxCubeEmitterPropertySet()
    {
        ReleaseDescriptorSlot(WidthRamp);
        ReleaseDescriptorSlot(HeightRamp);
        ReleaseDescriptorSlot(DepthRamp);
        ReleaseDescriptorSlot(MinSpreadRamp);
        ReleaseDescriptorSlot(MaxSpreadRamp);
    }

    // FUN_0623bb80 0x0623bbd4..0x0623bc68: +0x6c an FxAnimatedSingleRamp (FUN_06206ce0)
    // with AddKey(0, FUN_06209ad0(0)); +0x70 FUN_06210900(1.0).
    FFxSphereEmitterPropertySet::FFxSphereEmitterPropertySet()
        : MinRadiusRamp(nullptr), MaxRadiusRamp(nullptr)
    {
        MakeAnimatedSingleRamp(0.0f, &MinRadiusRamp);
        MakeAnimatedSingleRamp(1.0f, &MaxRadiusRamp);
    }

    FFxSphereEmitterPropertySet::~FFxSphereEmitterPropertySet()
    {
        ReleaseDescriptorSlot(MinRadiusRamp);
        ReleaseDescriptorSlot(MaxRadiusRamp);
    }

    // ---- FFxEmitterBase ------------------------------------------------------

    FFxEmitterBase::FFxEmitterBase(const char* PrimaryIid, const char* FactoryName)
        : FGenericFxObjectBase(PrimaryIid, FactoryName),
          LastEmitTime_(0.0f),
          Pressure_(0.0f),
          InitLifeSpan_(0.0f),
          bResetEmitHistory_(1),
          EmitFraction_(0.0f)
    {
        PrevNodePos_[0] = 0.0f;
        PrevNodePos_[1] = 0.0f;
        PrevNodePos_[2] = 0.0f;
        // FUN_06206c70 on +0xbc (identity rotation), then +0xe0..+0xe8 = 0.
        FillIdentity3x3(EmitterXform_);
        EmitterXform_[9] = 0.0f;
        EmitterXform_[10] = 0.0f;
        EmitterXform_[11] = 0.0f;
        bIsEmitterNode_ = 1; // byte +0x95
    }

    FFxEmitterBase::~FFxEmitterBase()
    {
    }

    // FUN_06245ad0.
    GENRESULT DACOM_API FFxEmitterPropertySet::GetPropInt(unsigned long Hash, unsigned long* Out)
    {
        if (Hash != kEmitterInitialParticlesHash)
            return GR_ALE_CREATE_FAIL;
        *Out = InitialParticles;
        return GR_OK;
    }

    // FUN_06245b00.
    GENRESULT DACOM_API FFxEmitterPropertySet::SetPropInt(unsigned long Hash, unsigned long Value)
    {
        if (Hash != kEmitterInitialParticlesHash)
            return GR_ALE_CREATE_FAIL;
        InitialParticles = Value;
        return GR_OK;
    }

    // FUN_06245aa0 (0x06245aa0..0x06245ac7).
    GENRESULT DACOM_API FFxEmitterPropertySet::GetPropSingleRampRamp(unsigned long Hash,
                                                                     INestedSingleRamp** Out)
    {
        if (Hash != kEmitterLodCurveHash)
            return GR_ALE_CREATE_FAIL;
        return HandOutRamp(LodCurveRamp, Out);
    }

    // FUN_06245a20 (0x06245a20..0x06245a9a): Frequency +0x4c, MaxParticles +0x50,
    // EmitCount +0x5c, Pressure +0x58, InitLifeSpan +0x54, VelocityApproach +0x60, in
    // that test order; else -8.
    GENRESULT DACOM_API FFxEmitterPropertySet::GetPropAnimatedSingleRamp(unsigned long Hash,
                                                                         INestedSingleRamp** Out)
    {
        if (Hash == kEmitterFrequencyHash)
            return HandOutRamp(FrequencyRamp, Out);
        if (Hash == kEmitterMaxParticlesHash)
            return HandOutRamp(MaxParticlesRamp, Out);
        if (Hash == kEmitterEmitCountHash)
            return HandOutRamp(EmitCountRamp, Out);
        if (Hash == kEmitterPressureHash)
            return HandOutRamp(PressureRamp, Out);
        if (Hash == kEmitterInitLifeSpanHash)
            return HandOutRamp(InitLifeSpanRamp, Out);
        if (Hash == kEmitterVelocityApproachHash)
            return HandOutRamp(VelocityApproachRamp, Out);
        return GR_ALE_CREATE_FAIL;
    }

    // FUN_062458f0: the base rows (FUN_0623f960); a non-zero result is returned, else the
    // emitter rows (FUN_0623f920 over .data 0x06257700).
    int DACOM_API FFxEmitterPropertySet::EnumerateProps(FNodePropertyRowCallback Callback,
                                                        void* Context)
    {
        const int Result = FFxNodePropertySet::EnumerateProps(Callback, Context);
        if (Result != 0)
            return Result;
        return WalkPropertyRows(kEmitterPropertyRows, Callback, Context);
    }

    GENRESULT DACOM_API FFxEmitterBase::AssociateAppearance(IDAComponent* Appearance)
    {
        if (AssociatedAppearance_ != Appearance)
        {
            if (AssociatedAppearance_ != nullptr)
            {
                AssociatedAppearance_->Release();
                AssociatedAppearance_ = nullptr;
            }
            AssociatedAppearance_ = Appearance;
            if (Appearance != nullptr)
                Appearance->AddRef();
        }
        bResetEmitHistory_ = 1;
        if (AssociatedAppearance_ != nullptr)
            AsFxNode(AssociatedAppearance_)->AddEmitterRef();
        return GR_OK;
    }

    GENRESULT DACOM_API FFxEmitterBase::SetNodeFlags(unsigned long Flags)
    {
        if (AssociatedAppearance_ != nullptr && static_cast<long>(Flags) < 0)
            AsFxNode(AssociatedAppearance_)->ReleaseEmitterRef();
        NodeFlags_ = Flags;
        return GR_OK;
    }

    // FUN_06245bf0 (RET 8: node, Dt). Descriptor D = [node+0x9c]; every ramp is evaluated
    // with +0x1C Eval(node+0x8c, InnerTime).
    GENRESULT FFxEmitterBase::UpdateEmitter(float Dt)
    {
        // 06245c14: node vt+0x44.
        TickLifetime(Dt);

        // 06245c1c..06245c3f, 062460a9: no appearance -> done; a dead appearance
        // or a dead emitter drops the association.
        if (AssociatedAppearance_ == nullptr)
            return GR_OK;
        IGenericFx* const Appearance = AsFxNode(AssociatedAppearance_);
        if (static_cast<unsigned char>(Appearance->IsNodeDead()) != 0 ||
            static_cast<long>(NodeFlags_) < 0)
        {
            AssociatedAppearance_->Release();
            AssociatedAppearance_ = nullptr;
            return GR_OK;
        }

        // 06245c45: node slot 7 (FUN_0623ffb0) -> +0xbc.
        ComputeRelativeTransform(AssociatedAppearance_, EmitterXform_);

        // 06245c59..06245cbc: D+0x58 -> +0xb0, D+0x54 -> +0xb4, D+0x4c -> the local
        // frequency, each FSTP float, at (+0x8c, Age).
        FFxEmitterPropertySet* const Set = EmitterProps(); // node+0x9c
        Pressure_ = static_cast<float>(Set->PressureRamp->Eval(AppearanceOuterTime_, Age_));
        InitLifeSpan_ =
            static_cast<float>(Set->InitLifeSpanRamp->Eval(AppearanceOuterTime_, Age_));
        const float Frequency =
            static_cast<float>(Set->FrequencyRamp->Eval(AppearanceOuterTime_, Age_));

        // 06245cc0..06245cd4: Age - LastEmit is FST'd (Elapsed, float) but the FMUL by
        // the frequency uses the unrounded difference; ftol truncates.
        const double ElapsedExt = static_cast<double>(Age_) - LastEmitTime_;
        const float Elapsed = static_cast<float>(ElapsedExt);
        long EmitCount = static_cast<long>(ElapsedExt * Frequency);

        // 06245cd9..06245d12: first frame after construction / re-association:
        // + D+0x64 (InitialParticles, ADD), PrevNodePos = node+0x70.. (translation).
        if (bResetEmitHistory_ != 0)
        {
            bResetEmitHistory_ = 0;
            EmitCount =
                static_cast<long>(static_cast<unsigned long>(EmitCount) + Set->InitialParticles);
            PrevNodePos_[0] = NodeWorldXform_[9];
            PrevNodePos_[1] = NodeWorldXform_[10];
            PrevNodePos_[2] = NodeWorldXform_[11];
        }

        // 06245d15..06245dd1: keyed bursts. D+0x5c's +0x24 GetKeyValue(0, &Keys) (AddRefs);
        // Keys->GetKeyCount (+0x14) with no null check; every key with
        // LastEmitTime_ <= time <= Age adds its count.
        {
            IDAComponent* KeyObject = nullptr;
            Set->EmitCountRamp->GetKeyValue(0, &KeyObject);
            IFxAnimatedSingle* Keys = static_cast<IFxAnimatedSingle*>(KeyObject);
            const long KeyCount = static_cast<long>(Keys->GetKeyCount());
            long KeyIndex = 0;
            for (KeyIndex = 0; KeyIndex < KeyCount; ++KeyIndex)
            {
                // 06245d51: GetKeyTime (+0x2C); a non-zero result skips the key.
                float KeyTime = 0.0f;
                if (Keys->GetKeyTime(static_cast<int>(KeyIndex), &KeyTime) != GR_OK)
                    continue;
                // FCOMP LastEmit; TEST AH,5; JNP skips when only C0 is set (time < last).
                if (KeyTime < LastEmitTime_)
                    continue;
                // FCOMP Age; AND 0x4100; JZ stops when neither C0 nor C3 (time > Age).
                if (KeyTime > Age_)
                    break;
                // 06245da1: GetKeyValue (+0x30) into a zeroed triple.
                float Value[3] = { 0.0f, 0.0f, 0.0f };
                if (Keys->GetKeyValue(static_cast<int>(KeyIndex), Value) != GR_OK)
                    continue;
                // `FILD count; FADD Value[0]; CALL ftol` -- the sum stays on the x87 stack.
                EmitCount = static_cast<long>(static_cast<double>(EmitCount) + Value[0]);
            }
            // 06245dbe..06245dd1: the holder releases a non-null object.
            if (Keys != nullptr)
                Keys->Release();
        }

        // 06245dd4..06245e20: D+0x68 at (+0x8c, +0x90 LodInput) FIMUL count, FADD
        // +0xec, FST float +0xec; ftol of the unrounded sum; FILD the result, FSTP float
        // (EmitNowF), +0xec -= EmitNowF.
        const double FractionExt =
            Set->LodCurveRamp->Eval(AppearanceOuterTime_, LodInput_) * EmitCount + EmitFraction_;
        EmitFraction_ = static_cast<float>(FractionExt);
        long EmitNow = static_cast<long>(FractionExt);
        const float EmitNowF = static_cast<float>(EmitNow);
        EmitFraction_ = EmitFraction_ - EmitNowF;

        // 06245e26..06245e49: nothing to emit. FCOMP 0.0; TEST AH,0x44; JP returns
        // unless exactly C3 (equal): a zero frequency settles the history at Age.
        if (EmitNow == 0)
        {
            if (Frequency == 0.0f)
                LastEmitTime_ = Age_;
            return GR_OK;
        }

        // 06245e4e..06245ee6: how far the emitter moved since the last emit, in this
        // frame's local space (math +0x3C InverseTransform); 1.0f / EmitNowF stays in the
        // FPU for the per-particle step and is FST'd to float for the time step.
        float Delta[3] = { 0.0f, 0.0f, 0.0f };
        Acquire3DMath()->InverseTransform(*reinterpret_cast<FVector*>(Delta),
                                          *reinterpret_cast<const FTransform*>(NodeWorldXform_),
                                          *reinterpret_cast<const FVector*>(PrevNodePos_)); // +0x3C
        const double InvCountExt = 1.0 / static_cast<double>(EmitNowF);
        float FullDelta[3] = { Delta[0], Delta[1], Delta[2] };
        float Velocity[3] = { 0.0f, 0.0f, 0.0f };
        PrevNodePos_[0] = NodeWorldXform_[9];
        PrevNodePos_[1] = NodeWorldXform_[10];
        PrevNodePos_[2] = NodeWorldXform_[11];
        const float InvCount = static_cast<float>(InvCountExt);
        float Step[3] = { static_cast<float>(Delta[0] * InvCountExt),
                          static_cast<float>(Delta[1] * InvCountExt),
                          static_cast<float>(Delta[2] * InvCountExt) };

        // 06245ee8..06245f7c: FCOMP 0.0; AND 0x4100; JNZ skips Elapsed <= 0 (and
        // unordered). N = 1.0f / -Elapsed (FCHS, FDIVR) stays in the FPU; the scaled
        // delta is FSTP float; D+0x60 Eval stays in the FPU for the product.
        if (Elapsed > 0.0f)
        {
            const double NegInvElapsed = 1.0 / -static_cast<double>(Elapsed);
            const float Scaled[3] = { static_cast<float>(Delta[0] * NegInvElapsed),
                                      static_cast<float>(Delta[1] * NegInvElapsed),
                                      static_cast<float>(Delta[2] * NegInvElapsed) };
            const double Approach = Set->VelocityApproachRamp->Eval(AppearanceOuterTime_, Age_);
            Velocity[0] = static_cast<float>(Scaled[0] * Approach);
            Velocity[1] = static_cast<float>(Scaled[1] * Approach);
            Velocity[2] = static_cast<float>(Scaled[2] * Approach);
        }

        // 06245f80..06245fcd: sub-frame spacing (the float InvCount times Elapsed); no
        // motion compensation when the appearance shares this emitter's parent
        // (outer +0x28 == node+8).
        float AgeOffset = Elapsed;
        const float TimeStep = InvCount * Elapsed;
        if (Appearance->GetAttachParent() == AttachParent_)
        {
            Step[0] = 0.0f;
            Step[1] = 0.0f;
            Step[2] = 0.0f;
            FullDelta[0] = 0.0f;
            FullDelta[1] = 0.0f;
            FullDelta[2] = 0.0f;
        }

        // 06245fd5..0624602a: D+0x50 FSTP float; FILD the appearance's live count
        // (outer +0xCC), FSUBR (kept in the FPU); FCOM EmitNowF; TEST AH,5; JP keeps N
        // unless only C0 is set (room < N), then N = ftol(room).
        const float MaxParticles =
            static_cast<float>(Set->MaxParticlesRamp->Eval(AppearanceOuterTime_, Age_));
        unsigned long LiveCount = 0;
        Appearance->GetLiveInstanceCount(&LiveCount);
        const double Room =
            static_cast<double>(MaxParticles) - static_cast<double>(static_cast<long>(LiveCount));
        if (Room < EmitNowF)
            EmitNow = static_cast<long>(Room);

        // 0624602c..06246091: outer +0xB4, N x node slot 20, outer +0xC0.
        Appearance->Pad45();
        if (EmitNow > 0)
        {
            do
            {
                AgeOffset = AgeOffset - TimeStep;
                FullDelta[0] = FullDelta[0] - Step[0];
                FullDelta[1] = FullDelta[1] - Step[1];
                FullDelta[2] = FullDelta[2] - Step[2];
                EmitParticle(AgeOffset, FullDelta, Velocity);
                --EmitNow;
            } while (EmitNow != 0);
        }
        Appearance->Pad48();

        // 06246097..062460a1.
        LastEmitTime_ = Elapsed + LastEmitTime_;
        return GR_OK;
    }

    void FFxEmitterBase::SubmitParticle(const float* Pos3, const float* Vel3, const float& AgeOffset,
                                       const float* PosOffset3, const float* VelIn3,
                                       I3DMathEngine*& MathSlot)
    {
        // Q = W * AgeOffset (FUN_06206ab0, FSTP float each).
        float Q[3];
        StoreAlchemyFpuFloat(Q[2], static_cast<double>(Vel3[2]) * AgeOffset);
        StoreAlchemyFpuFloat(Q[1], static_cast<double>(Vel3[1]) * AgeOffset);
        StoreAlchemyFpuFloat(Q[0], static_cast<double>(Vel3[0]) * AgeOffset);
        // 0621bb88..0621bbbe (cone; cube 0621d541.., sphere 0623be5c..): x and y sums stay
        // in the FPU until + PosOffset; the z sum is FSTP'd first.
        float Pos[3];
        const double SumX = static_cast<double>(Q[0]) + Pos3[0];
        const double SumY = static_cast<double>(Pos3[1]) + Q[1];
        float SumZ;
        StoreAlchemyFpuFloat(SumZ, static_cast<double>(Pos3[2]) + Q[2]);
        StoreAlchemyFpuFloat(Pos[0], SumX + PosOffset3[0]);
        StoreAlchemyFpuFloat(Pos[1], SumY + PosOffset3[1]);
        StoreAlchemyFpuFloat(Pos[2], static_cast<double>(SumZ) + PosOffset3[2]);
        float Vel[3];
        StoreAlchemyFpuFloat(Vel[0], static_cast<double>(Vel3[0]) + VelIn3[0]);
        StoreAlchemyFpuFloat(Vel[1], static_cast<double>(Vel3[1]) + VelIn3[1]);
        StoreAlchemyFpuFloat(Vel[2], static_cast<double>(Vel3[2]) + VelIn3[2]);

        I3DMathEngine* Math = MathSlot;
        if (Math == nullptr)
        {
            Math = Create3DMath();
            MathSlot = Math;
        }
        float Rotated[3] = { 0.0f, 0.0f, 0.0f };
        Math->Rotate(*reinterpret_cast<FVector*>(Rotated),
                     *reinterpret_cast<const FTransform*>(EmitterXform_),
                     *reinterpret_cast<const FVector*>(Vel)); // +0x30
        float Six[6];
        std::memcpy(Six + 3, Rotated, sizeof(Rotated));
        Math = MathSlot;
        if (Math == nullptr)
        {
            Math = Create3DMath();
            MathSlot = Math;
        }
        float Placed[3] = { 0.0f, 0.0f, 0.0f };
        Math->TransformXform(*reinterpret_cast<FVector*>(Placed),
                             *reinterpret_cast<const FTransform*>(EmitterXform_),
                             *reinterpret_cast<const FVector*>(Pos)); // +0x28

        std::memcpy(Six, Placed, sizeof(Placed));
        IGenericFx* const Appearance = AsFxNode(AssociatedAppearance_);
        if (ForwardEmitterPad46(Appearance, Six, AgeOffset, InitLifeSpan_) >= 0)
            ForwardEmitterPad47(AsFxNode(AssociatedAppearance_), Six, AgeOffset, InitLifeSpan_);
    }

    // ---- FxConeEmitter -------------------------------------------------------

    FxConeEmitter::FxConeEmitter(const char* PrimaryIid, const char* FactoryName)
        : FFxEmitterBase(PrimaryIid, FactoryName),
          MinSpreadRad_(0.0f),
          SpreadRangeRad_(0.0f),
          MinRadius_(0.0f),
          RadiusRange_(0.0f)
    {
    }

    FxConeEmitter::~FxConeEmitter()
    {
    }

    // FUN_0621b7b0: operator_new(0x84), FUN_0621b490 on +4, the set's vtables, count 1;
    // node +0x7c / +0x80 / +0x9c / +0xf0.
    void FxConeEmitter::CreatePropertySet()
    {
        AdoptPropertySet(new FFxConeEmitterPropertySet());
    }

    // FUN_0621b890 (0x0621b890..0x0621b9c1, RET 8; this = the prototype's node):
    // operator_new(0x10c) and the cone's constructor inline (0x0621b8cc..0x0621b976, the
    // same stores as the factory's 0x0621c546.. body), then FUN_06245b30 (FUN_0623fc30 and
    // the +0x9c set cache) and the +0xf0 set cache (0x0621b99c..0x0621b9a7); *Out = the new
    // outer, 0. Both caches are PropertySet_ here.
    GENRESULT DACOM_API FxConeEmitter::CloneNode(IDAComponent** Out)
    {
        FxConeEmitter* const Node = new FxConeEmitter(PrimaryIid_, FactoryName_);
        Node->InitNodeFromArchetype(this);
        *Out = Node;
        return GR_OK;
    }

    // LAB_0621c030 (RET 0xc, through the outer thunk): 0x1C73D16B -> +0x6c, 0xF636F07F ->
    // +0x70, 0x100E7F55 -> +0x74, 0xFA4B5E41 -> +0x78 (AddRef'd, 0); otherwise JMP
    // FUN_06245a20.
    GENRESULT DACOM_API FFxConeEmitterPropertySet::GetPropAnimatedSingleRamp(
        unsigned long Hash, INestedSingleRamp** Out)
    {
        if (Hash == kConeMinRadiusHash)
            return HandOutRamp(MinRadiusRamp, Out);
        if (Hash == kConeMaxRadiusHash)
            return HandOutRamp(MaxRadiusRamp, Out);
        if (Hash == kConeMinSpreadHash)
            return HandOutRamp(MinSpreadRamp, Out);
        if (Hash == kConeMaxSpreadHash)
            return HandOutRamp(MaxSpreadRamp, Out);
        return FFxEmitterPropertySet::GetPropAnimatedSingleRamp(Hash, Out);
    }

    // LAB_0621c0e0: FUN_062458f0, then .data 0x06255e28.
    int DACOM_API FFxConeEmitterPropertySet::EnumerateProps(FNodePropertyRowCallback Callback,
                                                            void* Context)
    {
        const int Result = FFxEmitterPropertySet::EnumerateProps(Callback, Context);
        if (Result != 0)
            return Result;
        return WalkPropertyRows(kConePropertyRows, Callback, Context);
    }

    // LAB_0621b9d0 (RET 8: node, Dt): t = (float)(Dt + Age); D = [node+0xf0];
    // +0xfc = D+0x6c Eval; +0x100 = D+0x70 Eval - [+0xfc]; +0xf4 = D+0x74 Eval * 0.017453292f;
    // +0xf8 = D+0x78 Eval * 0.017453292f - [+0xf4] (each FSTP float, Evals at (+0x8c, t));
    // then FUN_06245bf0(node, Dt).
    GENRESULT DACOM_API FxConeEmitter::Pad5(float Dt)
    {
        FFxConeEmitterPropertySet* const Set = ConeProps();
        const float Time = Dt + Age_;
        MinRadius_ = static_cast<float>(Set->MinRadiusRamp->Eval(AppearanceOuterTime_, Time));
        RadiusRange_ = static_cast<float>(Set->MaxRadiusRamp->Eval(AppearanceOuterTime_, Time) -
                                          MinRadius_);
        MinSpreadRad_ = static_cast<float>(Set->MinSpreadRamp->Eval(AppearanceOuterTime_, Time) *
                                           kDegToRad);
        SpreadRangeRad_ = static_cast<float>(
            Set->MaxSpreadRamp->Eval(AppearanceOuterTime_, Time) * kDegToRad - MinSpreadRad_);
        return UpdateEmitter(Dt);
    }

    // LAB_0621ba90 (thiscall, RET 0xc): Az = (float)(rand * 2pi); Sp = rand * [+0xf8] +
    // [+0xf4] (FPU); Dir from SpreadDirection; R = rand * [+0x100] + [+0xfc] (FPU);
    // P = (float)(Dir * R); W = (float)(Dir * [+0xb0]); the common tail.
    void FxConeEmitter::EmitParticle(float AgeOffset, const float* PosOffset3, const float* Vel3)
    {
        const float Azimuth = static_cast<float>(AlchemyRand01() * kTwoPi);
        const double Spread = AlchemyRand01() * SpreadRangeRad_ + MinSpreadRad_;
        float Dir[3];
        SpreadDirection(Azimuth, Spread, Dir);
        const double Radius = AlchemyRand01() * RadiusRange_ + MinRadius_;
        float Pos[3];
        StoreAlchemyFpuFloat(Pos[2], Dir[2] * Radius);
        StoreAlchemyFpuFloat(Pos[1], Dir[1] * Radius);
        StoreAlchemyFpuFloat(Pos[0], Dir[0] * Radius);
        const float Vel[3] = { Dir[0] * Pressure_, Dir[1] * Pressure_, Dir[2] * Pressure_ };
        SubmitParticle(Pos, Vel, AgeOffset, PosOffset3, Vel3, G_ConeEmitter3DMath);
    }

    // ---- FxCubeEmitter -------------------------------------------------------

    FxCubeEmitter::FxCubeEmitter(const char* PrimaryIid, const char* FactoryName)
        : FFxEmitterBase(PrimaryIid, FactoryName),
          MinSpreadRad_(0.0f),
          SpreadRangeRad_(0.0f),
          Width_(0.0f),
          Height_(0.0f),
          Depth_(0.0f)
    {
    }

    FxCubeEmitter::~FxCubeEmitter()
    {
    }

    // FUN_0621d140: operator_new(0x88), FUN_0621cdd0 on +4, the set's vtables, count 1.
    void FxCubeEmitter::CreatePropertySet()
    {
        AdoptPropertySet(new FFxCubeEmitterPropertySet());
    }

    // FUN_0621d220 (RET 8): the cube's constructor inline (as its factory 0x0621dd26..),
    // FUN_06245b30 (0x0621d32d), the +0xf0 set cache (0x0621d332..0x0621d33d); *Out, 0.
    GENRESULT DACOM_API FxCubeEmitter::CloneNode(IDAComponent** Out)
    {
        FxCubeEmitter* const Node = new FxCubeEmitter(PrimaryIid_, FactoryName_);
        Node->InitNodeFromArchetype(this);
        *Out = Node;
        return GR_OK;
    }

    // LAB_0621d080 (RET 0xc): 0xFD22A11C -> +0x6c, 0xEFB0F8CF -> +0x70, 0x14B74EB2 ->
    // +0x74, 0xE282006B -> +0x78, 0x08C7217F -> +0x7c (AddRef'd, 0); otherwise JMP
    // FUN_06245a20.
    GENRESULT DACOM_API FFxCubeEmitterPropertySet::GetPropAnimatedSingleRamp(
        unsigned long Hash, INestedSingleRamp** Out)
    {
        if (Hash == kCubeWidthHash)
            return HandOutRamp(WidthRamp, Out);
        if (Hash == kCubeHeightHash)
            return HandOutRamp(HeightRamp, Out);
        if (Hash == kCubeDepthHash)
            return HandOutRamp(DepthRamp, Out);
        if (Hash == kCubeMinSpreadHash)
            return HandOutRamp(MinSpreadRamp, Out);
        if (Hash == kCubeMaxSpreadHash)
            return HandOutRamp(MaxSpreadRamp, Out);
        return FFxEmitterPropertySet::GetPropAnimatedSingleRamp(Hash, Out);
    }

    // LAB_0621d8c0: FUN_062458f0, then .data 0x06255f30.
    int DACOM_API FFxCubeEmitterPropertySet::EnumerateProps(FNodePropertyRowCallback Callback,
                                                            void* Context)
    {
        const int Result = FFxEmitterPropertySet::EnumerateProps(Callback, Context);
        if (Result != 0)
            return Result;
        return WalkPropertyRows(kCubePropertyRows, Callback, Context);
    }

    // LAB_0621d360 (RET 8): t = (float)(Dt + Age); +0xfc / +0x100 / +0x104 = D+0x6c /
    // +0x70 / +0x74 Eval; +0xf4 = D+0x78 Eval * 0.017453292f; +0xf8 = D+0x7c Eval *
    // 0.017453292f - [+0xf4] (FSTP float each); FUN_06245bf0(node, Dt).
    GENRESULT DACOM_API FxCubeEmitter::Pad5(float Dt)
    {
        FFxCubeEmitterPropertySet* const Set = CubeProps();
        const float Time = Dt + Age_;
        Width_ = static_cast<float>(Set->WidthRamp->Eval(AppearanceOuterTime_, Time));
        Height_ = static_cast<float>(Set->HeightRamp->Eval(AppearanceOuterTime_, Time));
        Depth_ = static_cast<float>(Set->DepthRamp->Eval(AppearanceOuterTime_, Time));
        MinSpreadRad_ = static_cast<float>(Set->MinSpreadRamp->Eval(AppearanceOuterTime_, Time) *
                                           kDegToRad);
        SpreadRangeRad_ = static_cast<float>(
            Set->MaxSpreadRamp->Eval(AppearanceOuterTime_, Time) * kDegToRad - MinSpreadRad_);
        return UpdateEmitter(Dt);
    }

    // LAB_0621d430 (thiscall, RET 0xc): Az, Sp and Dir as the cone; then three draws,
    // each ((rand - 0.5f) * extent) FADD ST0,ST0 FSTP float: height (y), width (x),
    // depth (z); P = (x, y, z); W = (float)(Dir * [+0xb0]); the common tail.
    void FxCubeEmitter::EmitParticle(float AgeOffset, const float* PosOffset3, const float* Vel3)
    {
        const float Azimuth = static_cast<float>(AlchemyRand01() * kTwoPi);
        const double Spread = AlchemyRand01() * SpreadRangeRad_ + MinSpreadRad_;
        float Dir[3];
        SpreadDirection(Azimuth, Spread, Dir);
        float Pos[3];
        StoreAlchemyFpuFloat(Pos[1], (AlchemyRand01() - 0.5) * Height_ * 2.0);
        StoreAlchemyFpuFloat(Pos[0], (AlchemyRand01() - 0.5) * Width_ * 2.0);
        StoreAlchemyFpuFloat(Pos[2], (AlchemyRand01() - 0.5) * Depth_ * 2.0);
        const float Vel[3] = { Dir[0] * Pressure_, Dir[1] * Pressure_, Dir[2] * Pressure_ };
        SubmitParticle(Pos, Vel, AgeOffset, PosOffset3, Vel3, G_CubeEmitter3DMath);
    }

    // ---- FxSphereEmitter -----------------------------------------------------

    FxSphereEmitter::FxSphereEmitter(const char* PrimaryIid, const char* FactoryName)
        : FFxEmitterBase(PrimaryIid, FactoryName), MinRadius_(0.0f), RadiusRange_(0.0f)
    {
    }

    FxSphereEmitter::~FxSphereEmitter()
    {
    }

    // FUN_0623bb80: operator_new(0x7c), the emitter set (FUN_062455b0) on +4 plus the two
    // sphere ramps, the set's vtables, count 1 (0x0623bc81).
    void FxSphereEmitter::CreatePropertySet()
    {
        AdoptPropertySet(new FFxSphereEmitterPropertySet());
    }

    // FUN_0623ba50 (RET 8): the sphere's constructor inline (as its factory 0x0623ca46..),
    // FUN_06245b30 (0x0623bb4b), the +0xf0 set cache (0x0623bb50..0x0623bb5b); *Out, 0.
    GENRESULT DACOM_API FxSphereEmitter::CloneNode(IDAComponent** Out)
    {
        FxSphereEmitter* const Node = new FxSphereEmitter(PrimaryIid_, FactoryName_);
        Node->InitNodeFromArchetype(this);
        *Out = Node;
        return GR_OK;
    }

    // LAB_0623c210 (RET 0xc): 0xF231FF60 -> +0x6c, 0x1874DE74 -> +0x70 (AddRef'd, 0);
    // otherwise JMP FUN_06245a20.
    GENRESULT DACOM_API FFxSphereEmitterPropertySet::GetPropAnimatedSingleRamp(
        unsigned long Hash, INestedSingleRamp** Out)
    {
        if (Hash == kSphereMinRadiusHash)
            return HandOutRamp(MinRadiusRamp, Out);
        if (Hash == kSphereMaxRadiusHash)
            return HandOutRamp(MaxRadiusRamp, Out);
        return FFxEmitterPropertySet::GetPropAnimatedSingleRamp(Hash, Out);
    }

    // LAB_0623c290: FUN_062458f0, then .data 0x06256c4c.
    int DACOM_API FFxSphereEmitterPropertySet::EnumerateProps(FNodePropertyRowCallback Callback,
                                                              void* Context)
    {
        const int Result = FFxEmitterPropertySet::EnumerateProps(Callback, Context);
        if (Result != 0)
            return Result;
        return WalkPropertyRows(kSpherePropertyRows, Callback, Context);
    }

    // LAB_0623bcf0 (RET 8): t = (float)(Dt + Age); +0xf4 = D+0x6c Eval; +0xf8 = D+0x70
    // Eval - [+0xf4] (FSTP float); FUN_06245bf0(node, Dt).
    GENRESULT DACOM_API FxSphereEmitter::Pad5(float Dt)
    {
        FFxSphereEmitterPropertySet* const Set = SphereProps();
        const float Time = Dt + Age_;
        MinRadius_ = static_cast<float>(Set->MinRadiusRamp->Eval(AppearanceOuterTime_, Time));
        RadiusRange_ = static_cast<float>(Set->MaxRadiusRamp->Eval(AppearanceOuterTime_, Time) -
                                          MinRadius_);
        return UpdateEmitter(Dt);
    }

    // LAB_0623bd60 (thiscall, RET 0xc): three draws (rand - 0.5f) FADD ST0,ST0 FSTP
    // float -- the first is y, the second x, the third z; FUN_0623bf90 normalises;
    // R = rand * [+0xf8] + [+0xf4] (FPU); P = (float)(Dir * R); W = (float)(Dir *
    // [+0xb0]); the common tail.
    void FxSphereEmitter::EmitParticle(float AgeOffset, const float* PosOffset3, const float* Vel3)
    {
        float Dir[3];
        StoreAlchemyFpuFloat(Dir[1], (AlchemyRand01() - 0.5) * 2.0);
        StoreAlchemyFpuFloat(Dir[0], (AlchemyRand01() - 0.5) * 2.0);
        StoreAlchemyFpuFloat(Dir[2], (AlchemyRand01() - 0.5) * 2.0);
        FastNormalize3(Dir);
        const double Radius = AlchemyRand01() * RadiusRange_ + MinRadius_;
        float Pos[3];
        StoreAlchemyFpuFloat(Pos[2], Dir[2] * Radius);
        StoreAlchemyFpuFloat(Pos[1], Dir[1] * Radius);
        StoreAlchemyFpuFloat(Pos[0], Dir[0] * Radius);
        const float Vel[3] = { Dir[0] * Pressure_, Dir[1] * Pressure_, Dir[2] * Pressure_ };
        SubmitParticle(Pos, Vel, AgeOffset, PosOffset3, Vel3, G_SphereEmitter3DMath);
    }

} // namespace AlchemyImpl
