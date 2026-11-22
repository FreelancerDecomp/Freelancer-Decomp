// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// RPGPU's state calls (docs/re/rpgpu.md): the [RenderPipeline] settings and RP8's defaults, the
// pipeline, render and texture-stage state, the viewport, the transforms and the lights.

#include "RpGpu.h"

#include "luaprofile.h" // IProfileParser
#include "render/rp8_semantics.h"

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace RpGpuDetail;

namespace
{
    // RP8's FUN_06d16930: the key's text when GetString finds it, converted as RP8 does
    // (Rp8IniNumber).
    bool ReadNumber(IProfileParser* Parser, void* Section, const char* Key, unsigned long* Out)
    {
        char Text[0x400] = {};
        if (Parser->GetString(Section, Key, Text, sizeof(Text)) == 0)
            return false;
        Text[sizeof(Text) - 1] = '\0';
        *Out = Rp8IniNumber(Text);
        return true;
    }

    void Transpose(const float* In, float* Out)
    {
        for (int Row = 0; Row < 4; ++Row)
            for (int Col = 0; Col < 4; ++Col)
                Out[Col * 4 + Row] = In[Row * 4 + Col];
    }
}

// RP8's keys and defaults (render/rp8_semantics.h), each overridden by its key in the ini
// section when present, as RP8's Startup reads them: the pipeline states, the abilities and the
// render states. RP8 does not read the texture-stage keys; every stage starts with its defaults,
// COLOROP and ALPHAOP 1 (disabled) included.
void FRpGpu::ReadSettings()
{
    for (const FRp8StateKey& Row : kRp8PipelineProperties)
        PipelineState_[Row.Id] = static_cast<int>(Row.Default);
    std::memset(DefaultRenderStates_, 0, sizeof(DefaultRenderStates_));
    for (const FRp8StateKey& Row : kRp8RenderStateProperties)
        DefaultRenderStates_[Row.Id] = static_cast<int>(Row.Default);
    std::memset(DefaultStageStates_, 0, sizeof(DefaultStageStates_));
    for (const FRp8StateKey& Row : kRp8StageOpsOtherStage)
        DefaultStageStates_[Row.Id] = static_cast<int>(Row.Default);
    for (const FRp8StateKey& Row : kRp8StageStateProperties)
        DefaultStageStates_[Row.Id] = static_cast<int>(Row.Default);
    std::memset(AbilityOverrides_, 0, sizeof(AbilityOverrides_));
    std::memset(bAbilityOverridden_, 0, sizeof(bAbilityOverridden_));
    bNullDevice_ = false;

    IProfileParser* const Parser = AcquireProfileParser();
    if (Parser != nullptr)
    {
        void* const Section = Parser->OpenSection(Section_, 3);
        if (Section != nullptr)
        {
            unsigned long Value = 0;
            for (const FRp8StateKey& Row : kRp8PipelineProperties)
                if (ReadNumber(Parser, Section, Row.Name, &Value))
                    PipelineState_[Row.Id] = static_cast<int>(Value);
            for (const FRp8StateKey& Row : kRp8AbilityProperties)
            {
                if (ReadNumber(Parser, Section, Row.Name, &Value))
                {
                    AbilityOverrides_[Row.Id] = Value;
                    bAbilityOverridden_[Row.Id] = true;
                }
            }
            for (const FRp8StateKey& Row : kRp8RenderStateProperties)
                if (ReadNumber(Parser, Section, Row.Name, &Value))
                    DefaultRenderStates_[Row.Id] = static_cast<int>(Value);
            // RPGPU's own key: no device, for runs without a GPU (RpGpuBackend.h).
            if (ReadNumber(Parser, Section, "NULL_DEVICE", &Value))
                bNullDevice_ = Value != 0;
            Parser->CloseSection(Section);
        }
        Parser->Release();
    }

    // BROKEN_MULTITEXTURE is read, then overwritten with the device's blend-stage count, capped
    // at 7 (RP8's FUN_06d020ab). RPGPU's "device" has the stages its shader draws.
    PipelineState_[kPsMaxBlendStages] = kRpGpuStages;
}

void FRpGpu::ResetStateToDefaults()
{
    std::memcpy(RenderStates_, DefaultRenderStates_, sizeof(RenderStates_));
    for (int Stage = 0; Stage < 8; ++Stage)
        std::memcpy(StageStates_[Stage], DefaultStageStates_, sizeof(StageStates_[Stage]));
}

void FRpGpu::ResetDeviceState()
{
    ResetStateToDefaults();
    Identity(Projection_);
    Viewport_[0] = 0;
    Viewport_[1] = 0;
    Viewport_[2] = static_cast<int>(Backend().Width());
    Viewport_[3] = static_cast<int>(Backend().Height());
    DepthRange_[0] = 0.0f;
    DepthRange_[1] = 1.0f;
    Material_ = RpMaterial{}; // a new device's material is all zeros
    // RP8's ApplyDefaultTextureStages: no textures bound, identity texture transforms.
    for (int Stage = 0; Stage < 8; ++Stage)
    {
        StageTextures_[Stage] = nullptr;
        Identity(StageTransforms_[Stage]);
    }
}

GENRESULT DACOM_API FRpGpu::SetPipelineState(int Id, int Value)
{
    if (Id >= 0 && Id < kPsCount)
        PipelineState_[Id] = Value;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::GetPipelineState(int Id, int* Out)
{
    if (!bStarted_)
        return GR_GENERIC;
    if (Out != nullptr)
    {
        if (Id >= 0 && Id < kPsCount)
            *Out = PipelineState_[Id];
        else if (Id == kPsCount)
            *Out = 0;
    }
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::SetViewportRect(int X, int Y, int W, int H)
{
    Viewport_[0] = X;
    Viewport_[1] = Y;
    Viewport_[2] = W;
    Viewport_[3] = H;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::GetViewportRect(int* OutX, int* OutY, int* OutW, int* OutH)
{
    if (OutX != nullptr)
        *OutX = Viewport_[0];
    if (OutY != nullptr)
        *OutY = Viewport_[1];
    if (OutW != nullptr)
        *OutW = Viewport_[2];
    if (OutH != nullptr)
        *OutH = Viewport_[3];
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::SetDepthRange(float MinZ, float MaxZ)
{
    DepthRange_[0] = MinZ;
    DepthRange_[1] = MaxZ;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::GetDepthRange(float* OutMinZ, float* OutMaxZ)
{
    if (OutMinZ != nullptr)
        *OutMinZ = DepthRange_[0];
    if (OutMaxZ != nullptr)
        *OutMaxZ = DepthRange_[1];
    return GR_OK;
}

// --- transforms -------------------------------------------------------------------------------
//
// RP8's stores, call for call (RP8.cpp, SetWorld to SetLookat): the 12-float DA transforms
// (rotation rows, then translation) are kept as Direct3D's 4x4, and the view (or, with
// VIEWSPACE_LIGHTS, the world) carries RP8's z flip, which turns the game's right-handed space
// into Direct3D's left-handed one. RP8 multiplies through the 3DMathEngine; these products keep
// its double sums.

namespace
{
    // FRp8TransformCache::StoreAffine / LoadAffine.
    void StoreAffine(float* Values, const float* M12)
    {
        Values[0] = M12[0];
        Values[4] = M12[1];
        Values[8] = M12[2];
        Values[1] = M12[3];
        Values[5] = M12[4];
        Values[9] = M12[5];
        Values[2] = M12[6];
        Values[6] = M12[7];
        Values[10] = M12[8];
        Values[12] = M12[9];
        Values[13] = M12[10];
        Values[14] = M12[11];
        Values[3] = 0.0f;
        Values[7] = 0.0f;
        Values[11] = 0.0f;
        Values[15] = 1.0f;
    }

    void LoadAffine(const float* Values, float* Out12)
    {
        Out12[0] = Values[0];
        Out12[1] = Values[4];
        Out12[2] = Values[8];
        Out12[3] = Values[1];
        Out12[4] = Values[5];
        Out12[5] = Values[9];
        Out12[6] = Values[2];
        Out12[7] = Values[6];
        Out12[8] = Values[10];
        Out12[9] = Values[12];
        Out12[10] = Values[13];
        Out12[11] = Values[14];
    }

    // The 3DMathEngine's MulTransform: rotation A * B, translation A * Tb + Ta. Out may alias.
    void MulTransform12(float* Out12, const float* A, const float* B)
    {
        float R[12];
        for (int Row = 0; Row < 3; ++Row)
        {
            for (int Col = 0; Col < 3; ++Col)
            {
                R[Row * 3 + Col] = static_cast<float>(static_cast<double>(A[Row * 3]) * B[Col] +
                                                      static_cast<double>(A[Row * 3 + 1]) * B[3 + Col] +
                                                      static_cast<double>(A[Row * 3 + 2]) * B[6 + Col]);
            }
            R[9 + Row] = static_cast<float>(static_cast<double>(A[Row * 3]) * B[9] +
                                            static_cast<double>(A[Row * 3 + 1]) * B[10] +
                                            static_cast<double>(A[Row * 3 + 2]) * B[11] + A[9 + Row]);
        }
        std::memcpy(Out12, R, sizeof(R));
    }

    // RP8's MulZFlip12: diag(1, 1, -1) * M, which negates the rotation's third row and Tz.
    void ZFlip12(float* Out12, const float* In12)
    {
        float R[12];
        std::memcpy(R, In12, sizeof(R));
        R[6] = -R[6];
        R[7] = -R[7];
        R[8] = -R[8];
        R[11] = -R[11];
        std::memcpy(Out12, R, sizeof(R));
    }

    constexpr float kIdentity12[12] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f};

    // RP8's Rp8NormalizeInPlace: double length, float reciprocal, no zero check.
    void Normalize(float* V)
    {
        const double Length = std::sqrt(static_cast<double>(V[0]) * V[0] + static_cast<double>(V[1]) * V[1] +
                                        static_cast<double>(V[2]) * V[2]);
        const float Inverse = static_cast<float>(1.0 / Length);
        V[0] = static_cast<float>(static_cast<double>(V[0]) * Inverse);
        V[1] = static_cast<float>(static_cast<double>(V[1]) * Inverse);
        V[2] = static_cast<float>(static_cast<double>(V[2]) * Inverse);
    }
}

void FRpGpu::StoreViewIdentity(bool bFlip)
{
    float M12[12];
    if (bFlip)
        ZFlip12(M12, kIdentity12);
    else
        std::memcpy(M12, kIdentity12, sizeof(M12));
    StoreAffine(View_, M12);
    bViewCommitted_ = false;
}

// RP8's ApplyIdentityTransforms (CreateBuffers): identity view, world and projection, the z flip
// on the view without VIEWSPACE_LIGHTS and on the world with it; all three committed and marked.
void FRpGpu::ApplyIdentityTransforms()
{
    const bool bViewspaceLightsOff = PipelineState_[kPsViewspaceLights] == 0;
    StoreViewIdentity(bViewspaceLightsOff);
    float World12[12];
    if (!bViewspaceLightsOff)
        ZFlip12(World12, kIdentity12);
    else
        std::memcpy(World12, kIdentity12, sizeof(World12));
    StoreAffine(World_, World12);
    Identity(Projection_);
    bViewCommitted_ = true;
    bViewMarker_ = true;
}

// What RP8's FlushDrawState does at every draw that RPGPU has to copy: FRp8TransformCache::Commit
// on the view (a changed view is sent, and that clears the marker), then the light manager's
// flush.
void FRpGpu::FlushDrawState()
{
    if (!bViewCommitted_)
    {
        bViewMarker_ = false;
        bViewCommitted_ = true;
    }
    FlushLights();
}

void FRpGpu::ResetStreams()
{
    SelectedVb_ = nullptr;
    SelectedIb_ = nullptr;
    IbBaseVertex_ = 0xFFFFFFFFu;
}

GENRESULT DACOM_API FRpGpu::SetWorld(const float* M12)
{
    if (M12 == nullptr)
        return GR_INVALID_PARMS;
    StoreAffine(World_, M12);
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::GetWorld(float* Out12)
{
    if (Out12 == nullptr)
        return GR_INVALID_PARMS;
    LoadAffine(World_, Out12);
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::SetView(const float* M12)
{
    if (M12 == nullptr)
        return GR_INVALID_PARMS;
    float Flipped[12];
    ZFlip12(Flipped, M12);
    StoreAffine(View_, Flipped);
    bViewCommitted_ = false;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::GetView(float* Out12)
{
    if (Out12 == nullptr)
        return GR_INVALID_PARMS;
    float Stored[12];
    LoadAffine(View_, Stored);
    ZFlip12(Out12, Stored);
    return GR_OK;
}

// The model-view goes into the world; the view becomes the identity (flipped without
// VIEWSPACE_LIGHTS) unless SetModelview or SetLookat already put it there since the last draw
// that committed a view.
GENRESULT DACOM_API FRpGpu::SetModelview(const float* M12)
{
    if (M12 == nullptr)
        return GR_INVALID_PARMS;
    const bool bViewspaceLightsOff = PipelineState_[kPsViewspaceLights] == 0;
    float World12[12];
    if (!bViewspaceLightsOff)
        ZFlip12(World12, M12);
    else
        std::memcpy(World12, M12, sizeof(World12));
    StoreAffine(World_, World12);
    if (!bViewMarker_)
    {
        StoreViewIdentity(bViewspaceLightsOff);
        bViewMarker_ = true;
    }
    return GR_OK;
}

// View * world, each with its stored flip undone.
GENRESULT DACOM_API FRpGpu::GetModelview(float* Out12)
{
    if (Out12 == nullptr)
        return GR_INVALID_PARMS;
    const bool bViewspaceLightsOff = PipelineState_[kPsViewspaceLights] == 0;
    float World12[12];
    LoadAffine(World_, World12);
    if (!bViewspaceLightsOff)
        ZFlip12(World12, World12);
    float View12[12];
    LoadAffine(View_, View12);
    if (bViewspaceLightsOff)
        ZFlip12(View12, View12);
    MulTransform12(Out12, View12, World12);
    return GR_OK;
}

// The projection is kept as D3D's row-major matrix; callers hand it over and read it back
// transposed (RP8's convention).
GENRESULT DACOM_API FRpGpu::SetProjection(const float* M16)
{
    if (M16 == nullptr)
        return GR_INVALID_PARMS;
    Transpose(M16, Projection_);
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::GetProjection(float* Out16)
{
    if (Out16 == nullptr)
        return GR_INVALID_PARMS;
    Transpose(Projection_, Out16);
    return GR_OK;
}

// RP8's set_lookat: a camera transform from eye, target and up (rotation rows X, Y, Z with Z
// pointing away from the target), into the world with VIEWSPACE_LIGHTS (and the identity view,
// as SetModelview puts it) or into the view without. Nothing is checked, as in RP8.
GENRESULT DACOM_API FRpGpu::SetLookat(float EyeX, float EyeY, float EyeZ, float AtX, float AtY,
                                      float AtZ, float UpX, float UpY, float UpZ)
{
    float Forward[3] = {AtX - EyeX, AtY - EyeY, AtZ - EyeZ};
    Normalize(Forward);
    const float AxisZ[3] = {-Forward[0], -Forward[1], -Forward[2]};
    float AxisX[3] = {
        static_cast<float>(static_cast<double>(UpY) * AxisZ[2] - static_cast<double>(UpZ) * AxisZ[1]),
        static_cast<float>(static_cast<double>(UpZ) * AxisZ[0] - static_cast<double>(UpX) * AxisZ[2]),
        static_cast<float>(static_cast<double>(UpX) * AxisZ[1] - static_cast<double>(UpY) * AxisZ[0]),
    };
    Normalize(AxisX);
    float AxisY[3] = {
        static_cast<float>(static_cast<double>(AxisZ[1]) * AxisX[2] - static_cast<double>(AxisZ[2]) * AxisX[1]),
        static_cast<float>(static_cast<double>(AxisZ[2]) * AxisX[0] - static_cast<double>(AxisZ[0]) * AxisX[2]),
        static_cast<float>(static_cast<double>(AxisZ[0]) * AxisX[1] - static_cast<double>(AxisZ[1]) * AxisX[0]),
    };
    Normalize(AxisY);

    float Xform[12] = {AxisX[0], AxisX[1], AxisX[2], AxisY[0], AxisY[1], AxisY[2], AxisZ[0], AxisZ[1], AxisZ[2]};
    const float Eye[3] = {EyeX, EyeY, EyeZ};
    for (int Row = 0; Row < 3; ++Row)
    {
        // The 3DMathEngine's TransformMatrix (rotation * eye), negated into the translation.
        const float Rotated = static_cast<float>(static_cast<double>(Xform[Row * 3 + 2]) * Eye[2] +
                                                 static_cast<double>(Xform[Row * 3 + 1]) * Eye[1] +
                                                 static_cast<double>(Xform[Row * 3]) * Eye[0]);
        Xform[9 + Row] = -Rotated;
    }

    float Flipped[12];
    ZFlip12(Flipped, Xform);
    if (PipelineState_[kPsViewspaceLights] != 0)
    {
        StoreAffine(World_, Flipped);
        if (!bViewMarker_)
        {
            StoreViewIdentity(false);
            bViewMarker_ = true;
        }
    }
    else
    {
        StoreAffine(View_, Flipped);
        bViewCommitted_ = false;
    }
    return GR_OK;
}

// RP8's set_ortho and set_perspective: the differences in double, each element stored as float.
GENRESULT DACOM_API FRpGpu::SetOrtho(float Left, float Right, float Bottom, float Top, float NearPlane,
                                     float FarPlane)
{
    const double Width = static_cast<double>(Right) - Left;
    const double Height = static_cast<double>(Top) - Bottom;
    const double Depth = static_cast<double>(FarPlane) - NearPlane;
    std::memset(Projection_, 0, sizeof(Projection_));
    Projection_[0] = static_cast<float>(2.0 / Width);
    Projection_[5] = static_cast<float>(2.0 / Height);
    Projection_[10] = static_cast<float>(2.0 / Depth);
    Projection_[12] = static_cast<float>(-(static_cast<double>(Right) + Left) / Width);
    Projection_[13] = static_cast<float>(-(static_cast<double>(Bottom) + Top) / Height);
    Projection_[14] = static_cast<float>(-static_cast<double>(NearPlane) / Depth);
    Projection_[15] = 1.0f;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::SetPerspective(float FovDeg, float Aspect, float NearPlane, float FarPlane)
{
    if (NearPlane <= 0.0f || FarPlane <= 0.0f || FarPlane == NearPlane)
        return GR_GENERIC;
    const float Cot = static_cast<float>(1.0 / std::tan(3.141592653589793 / 180.0 * FovDeg));
    const float InvDepth = static_cast<float>(1.0 / (static_cast<double>(FarPlane) - NearPlane));
    std::memset(Projection_, 0, sizeof(Projection_));
    Projection_[0] = static_cast<float>(static_cast<double>(Cot) / Aspect);
    Projection_[5] = Cot;
    Projection_[10] = static_cast<float>(static_cast<double>(FarPlane) * InvDepth);
    Projection_[11] = 1.0f;
    Projection_[14] = static_cast<float>(-static_cast<double>(FarPlane) * NearPlane * InvDepth);
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::ResetRenderStatesToDefaults()
{
    ResetStateToDefaults();
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::SetRenderState(RpRenderState State, int Value)
{
    const unsigned int Index = static_cast<unsigned int>(State);
    if (Index < 256)
        RenderStates_[Index] = Value;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::GetRenderState(RpRenderState State, int* Out)
{
    const unsigned int Index = static_cast<unsigned int>(State);
    if (Out != nullptr)
        *Out = Index < 256 ? RenderStates_[Index] : 0;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::SetTextureStageState(int Stage, int Type, int Value)
{
    if (Stage >= 0 && Stage < 8 && Type >= 0 && Type < 32)
        StageStates_[Stage][Type] = Value;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::GetTextureStageState(int Stage, int Type, int* Out)
{
    if (Out != nullptr)
        *Out = (Stage >= 0 && Stage < 8 && Type >= 0 && Type < 32) ? StageStates_[Stage][Type] : 0;
    return GR_OK;
}

// RP8 stores the caller's matrix transposed (StoreTransposed) and hands it to Direct3D as the
// stage's texture transform; reading it back transposes again.
GENRESULT DACOM_API FRpGpu::SetTextureStageTransform(int Stage, const float* Matrix16)
{
    if (!bBuffersActive_)
        return GR_GENERIC;
    if (Stage < 0 || Stage >= 8 || Matrix16 == nullptr)
        return GR_INVALID_PARMS;
    Transpose(Matrix16, StageTransforms_[Stage]);
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::GetTextureStageTransform(int Stage, float* OutMatrix16)
{
    if (!bBuffersActive_)
        return GR_GENERIC;
    if (Stage < 0 || Stage >= 8 || OutMatrix16 == nullptr)
        return GR_INVALID_PARMS;
    Transpose(StageTransforms_[Stage], OutMatrix16);
    return GR_OK;
}

// With pipeline state TEXTURE off, RP8 ignores the call and GetTextureStageTexture answers null.
GENRESULT DACOM_API FRpGpu::SetTextureStageTexture(int Stage, void* Handle)
{
    if (PipelineState_[kPsTexture] == 0)
        return GR_OK;
    if (Stage >= 0 && Stage < 8)
        StageTextures_[Stage] = Handle;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::GetTextureStageTexture(int Stage, void** OutHandle)
{
    if (OutHandle != nullptr)
        *OutHandle = (PipelineState_[kPsTexture] != 0 && Stage >= 0 && Stage < 8) ? StageTextures_[Stage] : nullptr;
    return GR_OK;
}

// RP8's verify_state: outside a buffer session it fails; otherwise it flushes the draw state
// (committing the view), fails when the stage past the last blend stage is not disabled, and
// resets the stream selection. RP8 also asks the device to validate, which SDL_GPU has no
// counterpart for, and refuses MODULATE2X under a flag not yet identified.
GENRESULT DACOM_API FRpGpu::VerifyState()
{
    if (!bBuffersActive_)
        return GR_GENERIC;
    FlushDrawState();
    const int Stage = std::clamp(PipelineState_[kPsMaxBlendStages], 0, 7);
    if (StageStates_[Stage][1] != 1 || StageStates_[Stage][4] != 1) // COLOROP, ALPHAOP
        return GR_GENERIC;
    ResetStreams();
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::SetWorldN(int WorldIndex, const float* M12)
{
    (void)WorldIndex;
    (void)M12;
    return GR_OK;
}
