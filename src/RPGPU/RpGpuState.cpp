// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// RPGPU's state calls (docs/re/rpgpu.md): the [RenderPipeline] settings and RP8's defaults, the
// pipeline, render and texture-stage state, the viewport, the transforms and the lights.

#include "RpGpu.h"

#include "luaprofile.h" // IProfileParser
#include "render/rp8_semantics.h"

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
    // at 7 (RP8's FUN_06d020ab).
    PipelineState_[kPsMaxBlendStages] = 7;
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

// The world, view and model-view transforms have no reader yet; the skeleton draws nothing.
GENRESULT DACOM_API FRpGpu::SetWorld(const float* M12)
{
    (void)M12;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::GetWorld(float* Out12)
{
    if (Out12 != nullptr)
        std::memset(Out12, 0, 12 * sizeof(float));
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::SetView(const float* M12)
{
    (void)M12;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::GetView(float* Out12)
{
    if (Out12 != nullptr)
        std::memset(Out12, 0, 12 * sizeof(float));
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::SetModelview(const float* M12)
{
    (void)M12;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::GetModelview(float* Out12)
{
    if (Out12 != nullptr)
        std::memset(Out12, 0, 12 * sizeof(float));
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

GENRESULT DACOM_API FRpGpu::SetLookat(float EyeX, float EyeY, float EyeZ, float AtX, float AtY,
                                      float AtZ, float UpX, float UpY, float UpZ)
{
    (void)EyeX;
    (void)EyeY;
    (void)EyeZ;
    (void)AtX;
    (void)AtY;
    (void)AtZ;
    (void)UpX;
    (void)UpY;
    (void)UpZ;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::SetOrtho(float Left, float Right, float Bottom, float Top, float NearPlane,
                                     float FarPlane)
{
    std::memset(Projection_, 0, sizeof(Projection_));
    Projection_[0] = 2.0f / (Right - Left);
    Projection_[5] = 2.0f / (Top - Bottom);
    Projection_[10] = 2.0f / (FarPlane - NearPlane);
    Projection_[12] = -(Right + Left) / (Right - Left);
    Projection_[13] = -(Bottom + Top) / (Top - Bottom);
    Projection_[14] = -NearPlane / (FarPlane - NearPlane);
    Projection_[15] = 1.0f;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::SetPerspective(float FovDeg, float Aspect, float NearPlane, float FarPlane)
{
    if (NearPlane <= 0.0f || FarPlane <= 0.0f || FarPlane == NearPlane)
        return GR_GENERIC;
    const float C = 1.0f / std::tan(FovDeg * 3.14159265358979f / 180.0f);
    std::memset(Projection_, 0, sizeof(Projection_));
    Projection_[0] = C / Aspect;
    Projection_[5] = C;
    Projection_[10] = FarPlane / (FarPlane - NearPlane);
    Projection_[11] = 1.0f;
    Projection_[14] = -FarPlane * NearPlane / (FarPlane - NearPlane);
    return GR_OK;
}

// Lights: the handle is the caller's own light record. Nothing is lit until the skeleton draws.
GENRESULT DACOM_API FRpGpu::SetLight(void* Handle, const RpLight* Light)
{
    (void)Handle;
    (void)Light;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::DestroyLight(void* Handle)
{
    (void)Handle;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::GetLight(void* Handle, RpLight* OutLight)
{
    (void)Handle;
    if (OutLight != nullptr)
        std::memset(OutLight, 0, sizeof(*OutLight));
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::SetLightEnable(void* Handle, int Enable)
{
    (void)Handle;
    (void)Enable;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::GetLightEnable(void* Handle, int* OutEnable)
{
    (void)Handle;
    if (OutEnable != nullptr)
        *OutEnable = 0;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::SetMaterial(const RpMaterial* Material)
{
    (void)Material;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::GetMaterial(RpMaterial* Material)
{
    if (Material != nullptr)
        std::memset(Material, 0, sizeof(*Material));
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

GENRESULT DACOM_API FRpGpu::SetTextureStageTransform(int Stage, const float* Matrix16)
{
    (void)Stage;
    (void)Matrix16;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::GetTextureStageTransform(int Stage, float* OutMatrix16)
{
    (void)Stage;
    if (OutMatrix16 != nullptr)
        Identity(OutMatrix16);
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::SetTextureStageTexture(int Stage, void* Handle)
{
    (void)Stage;
    (void)Handle;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::GetTextureStageTexture(int Stage, void** OutHandle)
{
    (void)Stage;
    if (OutHandle != nullptr)
        *OutHandle = nullptr;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::VerifyState()
{
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::AddLight(void* Handle)
{
    (void)Handle;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::RemoveLight(void* Handle)
{
    (void)Handle;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::UpdateLight(void* Handle)
{
    (void)Handle;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::SetWorldN(int WorldIndex, const float* M12)
{
    (void)WorldIndex;
    (void)M12;
    return GR_OK;
}
