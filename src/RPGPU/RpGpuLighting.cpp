// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// RPGPU's lights, material and fog (docs/re/rpgpu.md, "Lighting and fog"). The light calls keep
// RP8's light manager (FRp8LightManager in RP8.cpp): records by handle, a queue sent at the next
// draw's flush, and a pool of light indices. What the flush leaves in DeviceLights_ is what
// Direct3D's light table would hold; the draws light with its enabled entries.

#include "RpGpu.h"

#include "RpGpuCommon.h"

#include <cmath>
#include <cstring>

using namespace RpGpuDetail;

namespace
{
    // Direct3D 8 render-state numbers this file reads.
    enum : int
    {
        kRsFogEnable = 28,
        kRsSpecularEnable = 29,
        kRsFogColor = 34,
        kRsFogTableMode = 35,
        kRsFogStart = 36,
        kRsFogEnd = 37,
        kRsFogDensity = 38,
        kRsRangeFogEnable = 48,
        kRsLighting = 137,
        kRsAmbient = 139,
        kRsFogVertexMode = 140,
        kRsColorVertex = 141,
        kRsLocalViewer = 142,
        kRsNormalizeNormals = 143,
        kRsDiffuseMaterialSource = 145,
        kRsSpecularMaterialSource = 146,
        kRsAmbientMaterialSource = 147,
        kRsEmissiveMaterialSource = 148,
    };

    // D3DERR_INVALIDCALL: what Direct3D's SetLight and GetLight answer a bad call with.
    constexpr GENRESULT kInvalidCall = (GENRESULT)0x8876086C;

    float StateFloat(int Bits)
    {
        float Value;
        std::memcpy(&Value, &Bits, sizeof(Value));
        return Value;
    }

    void ColourOf(std::uint32_t Argb, float* Out)
    {
        Out[0] = static_cast<float>((Argb >> 16) & 0xFF) / 255.0f;
        Out[1] = static_cast<float>((Argb >> 8) & 0xFF) / 255.0f;
        Out[2] = static_cast<float>(Argb & 0xFF) / 255.0f;
        Out[3] = static_cast<float>((Argb >> 24) & 0xFF) / 255.0f;
    }

    void Copy4(const FRpColorValue& Colour, float* Out)
    {
        Out[0] = Colour.r;
        Out[1] = Colour.g;
        Out[2] = Colour.b;
        Out[3] = Colour.a;
    }

    // The inverse of the upper 3x3 of a Direct3D 4x4 matrix, into Out (3x3, row-major). False
    // when it is singular.
    bool Inverse3(const float* M, float* Out)
    {
        const double A = M[0], B = M[1], C = M[2];
        const double D = M[4], E = M[5], F = M[6];
        const double G = M[8], H = M[9], I = M[10];
        const double Co0 = E * I - F * H;
        const double Co1 = F * G - D * I;
        const double Co2 = D * H - E * G;
        const double Det = A * Co0 + B * Co1 + C * Co2;
        if (Det == 0.0 || !std::isfinite(Det))
            return false;
        const double R = 1.0 / Det;
        Out[0] = static_cast<float>(Co0 * R);
        Out[1] = static_cast<float>((C * H - B * I) * R);
        Out[2] = static_cast<float>((B * F - C * E) * R);
        Out[3] = static_cast<float>(Co1 * R);
        Out[4] = static_cast<float>((A * I - C * G) * R);
        Out[5] = static_cast<float>((C * D - A * F) * R);
        Out[6] = static_cast<float>(Co2 * R);
        Out[7] = static_cast<float>((B * G - A * H) * R);
        Out[8] = static_cast<float>((A * E - B * D) * R);
        return true;
    }
}

// --- RP8's light manager -----------------------------------------------------------------------

FRpGpu::FLightRecord* FRpGpu::FindLight(void* Handle)
{
    auto Found = LightRecords_.find(Handle);
    return Found != LightRecords_.end() ? &Found->second : nullptr;
}

// FRp8LightManager::FindOrCreate: a new record takes an index from the pool (RP8's
// LightIndexAlloc: freed indices first, then the next unused one) and, with data, is dirty.
int FRpGpu::FindOrCreateLight(void* Handle, const RpLight* Data)
{
    auto Inserted = LightRecords_.emplace(Handle, FLightRecord());
    FLightRecord& Record = Inserted.first->second;
    if (Inserted.second)
    {
        if (LightIndicesUsed_ >= LightIndexPool_.size())
            LightIndexPool_.resize(LightIndexPool_.size() + 0x10, 0);
        if (LightIndicesUsed_ >= LightIndexNext_)
            LightIndexPool_[LightIndicesUsed_] = static_cast<int>(LightIndexNext_++);
        Record.Index = LightIndexPool_[LightIndicesUsed_++];
        Record.Data = Data;
        Record.bDataDirty = Data != nullptr;
    }
    return Record.Index;
}

// FRp8LightManager::Remove: out of the queue, its index back to the pool.
void FRpGpu::EraseLight(FLightRecord* Record, void* Handle)
{
    for (FLightRecord*& Queued : DirtyLights_)
    {
        if (Queued == Record)
        {
            Queued = nullptr;
            break;
        }
    }
    if (Record->Index != -1)
        LightIndexPool_[--LightIndicesUsed_] = Record->Index;
    LightRecords_.erase(Handle);
}

// RP8's LightQueueDirty: once per flush generation, and only when the enable state changes or
// the light is on with data to send.
void FRpGpu::QueueLight(FLightRecord* Record)
{
    if (Record->Generation == LightGeneration_)
        return;
    if (Record->bEnabled == Record->bWantEnabled && (!Record->bWantEnabled || !Record->bDataDirty))
        return;
    DirtyLights_.push_back(Record);
    Record->Generation = LightGeneration_;
}

// FRp8LightManager::SetEnable: disabling an unknown light is a no-op; enabling one makes a
// record without data.
void FRpGpu::SetLightWanted(void* Handle, int Enable)
{
    FLightRecord* Record = FindLight(Handle);
    if (Record == nullptr)
    {
        if (Enable == 0)
            return;
        FindOrCreateLight(Handle, nullptr);
        Record = FindLight(Handle);
    }
    Record->bWantEnabled = Enable != 0;
    QueueLight(Record);
}

FRpGpu::FDeviceLight& FRpGpu::DeviceLight(int Index)
{
    if (static_cast<std::size_t>(Index) >= DeviceLights_.size())
        DeviceLights_.resize(static_cast<std::size_t>(Index) + 1);
    return DeviceLights_[static_cast<std::size_t>(Index)];
}

// FRp8LightManager::Flush: the queued records' data (when on) and enable states go to the light
// table. A record with nothing to send (SetLight's, which has no data pointer) fails as
// Direct3D's SetLight(NULL) does, and that ends the flush, as in RP8.
void FRpGpu::FlushLights()
{
    for (FLightRecord* Record : DirtyLights_)
    {
        if (Record == nullptr)
            continue;
        bool bOk = true;
        if (Record->bDataDirty && Record->bWantEnabled)
        {
            Record->bDataDirty = false;
            if (Record->Data != nullptr)
            {
                FDeviceLight& Light = DeviceLight(Record->Index);
                Light.Data = *Record->Data;
                Light.bSet = true;
            }
            else
            {
                bOk = false;
            }
        }
        if (Record->bWantEnabled != Record->bEnabled && bOk)
        {
            DeviceLight(Record->Index).bEnabled = Record->bWantEnabled;
            Record->bEnabled = Record->bWantEnabled;
        }
        if (!bOk)
            break;
    }
    DirtyLights_.clear();
    ++LightGeneration_;
}

// FRp8LightManager::Reset, on a new device: its light table is empty, so every light with data
// is resent and every light asked on is enabled again, at the next draw.
void FRpGpu::ResetLights()
{
    DeviceLights_.clear();
    DirtyLights_.clear();
    LightGeneration_ = 0;
    for (auto& Entry : LightRecords_)
    {
        FLightRecord& Record = Entry.second;
        if (Record.Data != nullptr)
            Record.bDataDirty = true;
        if (Record.bWantEnabled)
        {
            Record.bEnabled = false;
            QueueLight(&Record);
        }
    }
}

// SetLight sends the light to the table at once; the record it makes keeps no data.
GENRESULT DACOM_API FRpGpu::SetLight(void* Handle, const RpLight* Light)
{
    const int Index = FindOrCreateLight(Handle, nullptr);
    if (Light == nullptr)
        return kInvalidCall;
    FDeviceLight& Entry = DeviceLight(Index);
    Entry.Data = *Light;
    Entry.bSet = true;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::DestroyLight(void* Handle)
{
    SetLightWanted(Handle, 0);
    FlushLights();
    if (FLightRecord* const Record = FindLight(Handle))
        EraseLight(Record, Handle);
    return GR_OK;
}

// RP8 passes the handle to Direct3D's GetLight as the light index, unchanged.
GENRESULT DACOM_API FRpGpu::GetLight(void* Handle, RpLight* OutLight)
{
    const std::uintptr_t Index = reinterpret_cast<std::uintptr_t>(Handle);
    if (OutLight == nullptr || Index >= DeviceLights_.size() || !DeviceLights_[Index].bSet)
        return kInvalidCall;
    *OutLight = DeviceLights_[Index].Data;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::SetLightEnable(void* Handle, int Enable)
{
    SetLightWanted(Handle, Enable);
    return GR_OK;
}

// The state asked for, not the one committed; 0 for an unknown light.
GENRESULT DACOM_API FRpGpu::GetLightEnable(void* Handle, int* OutEnable)
{
    if (OutEnable == nullptr)
        return GR_INVALID_PARMS;
    const FLightRecord* const Record = FindLight(Handle);
    *OutEnable = Record != nullptr && Record->bWantEnabled ? 1 : 0;
    return GR_OK;
}

// The handle is the caller's own light, read again at every flush that finds it dirty.
GENRESULT DACOM_API FRpGpu::AddLight(void* Handle)
{
    FindOrCreateLight(Handle, static_cast<const RpLight*>(Handle));
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::RemoveLight(void* Handle)
{
    SetLightEnable(Handle, 0);
    FlushLights();
    if (FLightRecord* const Record = FindLight(Handle))
        EraseLight(Record, Handle);
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::UpdateLight(void* Handle)
{
    FLightRecord* const Record = FindLight(Handle);
    if (Record != nullptr)
    {
        Record->bDataDirty = true;
        QueueLight(Record);
    }
    return GR_OK;
}

// RP8 sends the material to the device at once; only within a buffer session.
GENRESULT DACOM_API FRpGpu::SetMaterial(const RpMaterial* Material)
{
    if (!bBuffersActive_)
        return GR_GENERIC;
    if (Material == nullptr)
        return kInvalidCall;
    Material_ = *Material;
    return GR_OK;
}

GENRESULT DACOM_API FRpGpu::GetMaterial(RpMaterial* Material)
{
    if (!bBuffersActive_)
        return GR_GENERIC;
    if (Material == nullptr)
        return kInvalidCall;
    *Material = Material_;
    return GR_OK;
}

// --- a draw's lighting and fog -----------------------------------------------------------------

// Fills Command's transform extras, its lighting block and its fog. FvfColours: 1 the vertex has
// a normal, 2 a diffuse colour, 4 a specular colour.
void FRpGpu::FillLightingAndFog(FRpGpuCommand* Command, bool bPretransformed, unsigned int FvfColours)
{
    FRpGpuTransformUniforms& Transform = Command->Transform;
    Transform.Mode[1] = bPretransformed ? 1.0f : 0.0f;
    Transform.Flags[1] = FvfColours;

    const bool bFog = RenderStates_[kRsFogEnable] != 0;
    const int TableMode = RenderStates_[kRsFogTableMode];
    const int VertexMode = RenderStates_[kRsFogVertexMode];
    FRpGpuStageUniforms& Stage = Command->Stage;
    Stage.FogFlags[0] = !bFog ? 0u : TableMode != 0 ? 2u : 1u;
    // Table fog runs on w unless the projection is affine (Direct3D's rule, Wine's test).
    const bool bAffine = Projection_[3] == 0.0f && Projection_[7] == 0.0f && Projection_[11] == 0.0f &&
                         Projection_[15] == 1.0f;
    Stage.FogFlags[1] = bAffine ? 0u : 1u;
    Stage.FogFlags[2] = RenderStates_[kRsSpecularEnable] != 0 ? 1u : 0u;
    ColourOf(static_cast<std::uint32_t>(RenderStates_[kRsFogColor]), Stage.FogColor);
    Stage.FogTable[0] = static_cast<float>(TableMode);
    Stage.FogTable[1] = StateFloat(RenderStates_[kRsFogStart]);
    Stage.FogTable[2] = StateFloat(RenderStates_[kRsFogEnd]);
    Stage.FogTable[3] = StateFloat(RenderStates_[kRsFogDensity]);

    if (bPretransformed)
        return; // no transform, lighting or vertex fog

    std::memcpy(Transform.World, World_, sizeof(Transform.World));
    std::memcpy(Transform.View, View_, sizeof(Transform.View));
    // Normals go through the inverse transpose of the world's 3x3 (the world itself when that
    // has no inverse).
    float Inverse[9];
    Identity(Transform.NormalMatrix);
    if (Inverse3(World_, Inverse))
    {
        for (int Row = 0; Row < 3; ++Row)
        {
            for (int Col = 0; Col < 3; ++Col)
                Transform.NormalMatrix[Row * 4 + Col] = Inverse[Col * 3 + Row];
        }
    }
    else
    {
        for (int Row = 0; Row < 3; ++Row)
        {
            for (int Col = 0; Col < 3; ++Col)
                Transform.NormalMatrix[Row * 4 + Col] = World_[Row * 4 + Col];
        }
    }
    // The camera in world space: the view's inverse applied to its origin, and to the direction
    // towards a non-local viewer (-z in Direct3D's camera space).
    float ViewInverse[9];
    if (Inverse3(View_, ViewInverse))
    {
        for (int Col = 0; Col < 3; ++Col)
        {
            Transform.Eye[Col] = -(View_[12] * ViewInverse[Col] + View_[13] * ViewInverse[3 + Col] +
                                   View_[14] * ViewInverse[6 + Col]);
            Transform.Viewer[Col] = -ViewInverse[6 + Col];
        }
    }
    else
    {
        Transform.Viewer[2] = -1.0f;
    }
    const bool bLighting = RenderStates_[kRsLighting] != 0;
    Transform.Flags[0] = bLighting ? 1u : 0u;
    Transform.Flags[2] = RenderStates_[kRsNormalizeNormals] != 0 ? 1u : 0u;
    Transform.Flags[3] = RenderStates_[kRsLocalViewer] != 0 ? 1u : 0u;
    Transform.Fog[0] = static_cast<float>(VertexMode);
    Transform.Fog[1] = Stage.FogTable[1];
    Transform.Fog[2] = Stage.FogTable[2];
    Transform.Fog[3] = Stage.FogTable[3];
    Transform.FogFlags[0] = bFog && TableMode == 0 && VertexMode != 0 ? 1u : 0u;
    Transform.FogFlags[1] = RenderStates_[kRsRangeFogEnable] != 0 ? 1u : 0u;

    if (!bLighting)
        return;
    FRpGpuLightingUniforms Block = {};
    ColourOf(static_cast<std::uint32_t>(RenderStates_[kRsAmbient]), Block.GlobalAmbient);
    Copy4(Material_.Diffuse, Block.MaterialDiffuse);
    Copy4(Material_.Ambient, Block.MaterialAmbient);
    Copy4(Material_.Specular, Block.MaterialSpecular);
    Copy4(Material_.Emissive, Block.MaterialEmissive);
    Block.Power[0] = Material_.Power;
    if (RenderStates_[kRsColorVertex] != 0)
    {
        Block.Sources[0] = static_cast<std::uint32_t>(RenderStates_[kRsDiffuseMaterialSource]);
        Block.Sources[1] = static_cast<std::uint32_t>(RenderStates_[kRsAmbientMaterialSource]);
        Block.Sources[2] = static_cast<std::uint32_t>(RenderStates_[kRsSpecularMaterialSource]);
        Block.Sources[3] = static_cast<std::uint32_t>(RenderStates_[kRsEmissiveMaterialSource]);
    }
    // The enabled lights in index order, as many as the shader takes. A light enabled without
    // SetLight is Direct3D's default: white, directional, along +z.
    std::uint32_t Count = 0;
    for (const FDeviceLight& Entry : DeviceLights_)
    {
        if (!Entry.bEnabled || Count >= static_cast<std::uint32_t>(kRpGpuMaxLights))
            continue;
        RpLight Light = Entry.Data;
        if (!Entry.bSet)
        {
            Light = RpLight{};
            Light.Type = 3;
            Light.Diffuse = {1.0f, 1.0f, 1.0f, 0.0f};
            Light.Direction = {0.0f, 0.0f, 1.0f};
        }
        FRpGpuLight& Out = Block.Lights[Count++];
        Copy4(Light.Diffuse, Out.Diffuse);
        Copy4(Light.Specular, Out.Specular);
        Copy4(Light.Ambient, Out.Ambient);
        Out.Position[0] = Light.Position.x;
        Out.Position[1] = Light.Position.y;
        Out.Position[2] = Light.Position.z;
        Out.Position[3] = static_cast<float>(Light.Type);
        Out.Direction[0] = Light.Direction.x;
        Out.Direction[1] = Light.Direction.y;
        Out.Direction[2] = Light.Direction.z;
        Out.Direction[3] = Light.Range;
        Out.Attenuation[0] = Light.Attenuation0;
        Out.Attenuation[1] = Light.Attenuation1;
        Out.Attenuation[2] = Light.Attenuation2;
        Out.Attenuation[3] = Light.Falloff;
        Out.Spot[0] = std::cos(Light.Theta * 0.5f);
        Out.Spot[1] = std::cos(Light.Phi * 0.5f);
    }
    Block.Count[0] = Count;
    // Draws in a row usually share their lighting: one block for all of them.
    if (Frame_.Lighting.empty() || std::memcmp(&Frame_.Lighting.back(), &Block, sizeof(Block)) != 0)
        Frame_.Lighting.push_back(Block);
    Command->Lighting = static_cast<std::int32_t>(Frame_.Lighting.size() - 1);
}
