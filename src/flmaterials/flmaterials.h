// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "dacom.h"
#include "render/IBatchedMaterial.h"
#include "render/IMaterial.h"
#include "render/IFLAppearance.h"
#include "render/RenderPipeline.h"
#include "render/mesh_iface.h"
#include "render/shading.h"


// Registration uses this opaque token; callers remove const explicitly.
static const void* kRegisterPriority = (void*)0x80000000;

static constexpr int kFdumpError = 0x100001;
static constexpr int kFdumpNotice = 0x100003; // "%s(%d) : NOTICE:General:%s" (Slot24's own use)
static constexpr int kFdumpTrace1 = 0x100004;


static const char* const G_NameGlassMaterial = "GlassMaterial";
static const char* const G_NameHighGlassMaterial = "HighGlassMaterial";
static const char* const G_NameGfGlassMaterial = "GFGlassMaterial";
static const char* const G_NameNomadMaterial = "NomadMaterial";
static const char* const G_NameNomadNoBendy = "NomadMaterialNoBendy";
static const char* const G_NameBackdropMaterial = "BackdropMaterial";
static const char* const G_NameStarsphereMaterial = "StarsphereMaterial";
static const char* const G_NameHudIconMaterial = "HUDIconMaterial";
static const char* const G_NameHudAnimMaterial = "HUDAnimMaterial";
static const char* const G_NameColorizeMaterial = "ColorizeMaterial";
static const char* const G_NameCloakMaterial = "CloakMaterial";
static const char* const G_NameWireMaterial = "WireMaterial";
static const char* const G_NameWireMaterial2 = "WireMaterial2";
static const char* const G_NameExclusionZoneMaterial = "ExclusionZoneMaterial";
static const char* const G_NameEnvMapMaterial = "EnvMapMaterial";
static const char* const G_NameShadowMaterial = "ShadowMaterial";
static const char* const G_NameEngineGlowMaterial = "EngineGlowMaterial";
static const char* const G_NameVisibleCounterMaterial = "VisibleCounterMaterial";
static const char* const G_NamePlanetWaterMaterial = "PlanetWaterMaterial";
static const char* const G_NameFlAppearance = "FLAppearance";

enum EMaterialKind
{
    KindGlass = 0,
    KindNomad,
    KindBackdrop,
    KindHudIcon,
    KindHudAnim,
    KindColorize,
    KindCloak,
    KindWire,
    KindWire2,
    KindExclusion,
    KindEnvMap,
    KindShadow,
    KindEngineGlow,
    KindVisibleCounter,
    KindPlanetWater
};

struct FMaterialSpec
{
    const char* Name;
    EMaterialKind Kind;
};

bool FlmIidEqual(const char* Left, const char* Right);
bool FlmNameEqual(const char* Left, const char* Right);
void FlmFdump(int Flags, const char* File, int Line, const char* Kind, const char* Message);

IMaterial* FlmCreateMaterial(const FMaterialSpec& Spec);
GENRESULT FlmCreateAppearanceInstance(FAGGDESC18* Desc, void** Instance);

// VisibleCounter slot 10 / IFLAppearance slot 13 (DAT_067045B4 / B8 / BC).
extern FlULong G_VisibleCounterA;
extern FlULong G_VisibleCounterB;
extern FlULong G_VisibleCounterC;
void FlmVisibleCounterReset();
void FlmVisibleCounterCopy(FlULong* Out3);

// Borrowed engine outer component cached by FLAppearance initialization.
extern IDAComponent* G_FlmEngineOuter;

// Look up the root-instance fade value; leave Out untouched on a miss.
bool FlmLookupIdValue(int Id, void** Out);

// FUN_067006d0: uncached factory; the Nomad and camera callers own separate lazy caches.
struct I3DMathEngine;
I3DMathEngine* FlmCreateMathEngine();

class FMaterialImpl : public IMaterial
{
public:
    explicit FMaterialImpl(const FMaterialSpec& Spec);
    ~FMaterialImpl() noexcept(false);
    void ConfigureAggregation(FAGGDESC14* Desc);

    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
    FlULong DACOM_API AddRef() override;
    FlULong DACOM_API Release() override;

    GENRESULT DACOM_API Initialize(IDAComponent* Outer) override;
    GENRESULT DACOM_API LoadFromFileSystem(void* FileSystem) override;
    GENRESULT DACOM_API SaveProperties(void* FileSystem) override;
    GENRESULT DACOM_API ApplyVerify(void* Context, void* Extra) override;
    GENRESULT DACOM_API ResolveTextures(float Delta) override;
    GENRESULT DACOM_API Bind(void* Context) override;
    GENRESULT DACOM_API Teardown(void* Context) override;
    GENRESULT DACOM_API Submit(void* Context, FlULong PrimType, FlULong Start,
                               FlULong Count, FlULong IbStart, FlULong IndexSpan,
                               FlULong Flags) override;
    GENRESULT DACOM_API SetName(const char* Name) override;
    GENRESULT DACOM_API GetName(char* Out, FlULong Size) override;
    GENRESULT DACOM_API CopyClassName(char* Out, FlULong Size) override;
    GENRESULT DACOM_API GetPassCount(int* Out) override;
    GENRESULT DACOM_API GetRenderFlags(FlULong* Out) override;
    GENRESULT DACOM_API GetParameterCount(FlULong* OutCount) override;
    GENRESULT DACOM_API GetParameterDescriptor(FlULong Index, void* OutDesc) override;
    GENRESULT DACOM_API GetParameterValue(FlULong Index, void* InOutDesc) override;
    GENRESULT DACOM_API SetParameterValue(FlULong Index, void* Value) override;
    const char* __thiscall ClassName() override;
    void* __thiscall ScalarDeletingDestructor(unsigned char bDelete) override;
    GENRESULT __thiscall InitializeFromDescriptor(void* Arg) override;
    GENRESULT __thiscall ApplyInitialRenderStates(void* Arg) override;
    GENRESULT __thiscall Setup() override;
    bool __thiscall ValueWithinDeclaredRange(FlULong Index, void* Entry, void* Value) override;
    GENRESULT __thiscall StoreParameterValue(FlULong Index, void* Entry, void* Value) override;
    FlULong __thiscall GetParameterTable(void** OutBase) override;
    void* __thiscall GetParameterAddress(FlULong Index) override;
    GENRESULT DACOM_API InnerQueryInterface(const char* Iid, void** Out) override;
    FlULong DACOM_API InnerAddRef() override;
    FlULong DACOM_API InnerRelease() override;

private:
    void BindUnknown(const char* Why);
    void ReleaseMaterialResources();
    void BindGlass(void* Context);
    void BindNomad();
    void BindHudIcon(void* Context);
    void BindHudAnim(void* Context);
    GENRESULT BindColorize(void* Context);
    void RecomputeOpacityFlags();
    void BindCloak(void* Context);
    void BindWire();
    void BindWire2(void* Context);
    void BindExclusion();
    void BindEnvMap(void* Context);
    GENRESULT BindShadow(void* Context);
    GENRESULT DrawShadow(void* Context, FlULong Prim, FlULong Start, FlULong Count,
                         FlULong IbStart, FlULong IndexSpan, FlULong Flags);
    void BindEngineGlow(void* Context);
    void BindVisibleCounter();
    void BindBackdrop(void* Context);
    GENRESULT TeardownBackdrop(void* Context);
    GENRESULT DrawBackdrop(void* Context, FlULong Prim, FlULong Start, FlULong Count,
                           FlULong IbStart, FlULong IndexSpan, FlULong Flags);
    void BindPlanetWater(void* Context);
    GENRESULT ShadowCandidateDispatch(void* Context);
    void TeardownCandidateIgnoringResult(void* Context);
    GENRESULT SubmitCandidateGeometry(void* Context, FlULong PrimType,
                                     FlULong Start, FlULong Count,
                                     FlULong IbStart, FlULong IndexSpan,
                                     FlULong Flags);

    void ApplyTssFromTextureBlock(FlULong Stage, const FlULong* Block);
    FlULong TextureHandleAt(const FlULong* Block) const;
    bool ContextWantsColorVertex(void* Context) const;
    float CompareField2C() const;

    class FInner final : public IDAComponent
    {
    public:
        explicit FInner(FMaterialImpl* Owner) : RefCount(1), Owner(Owner) {}
        GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
        FlULong DACOM_API AddRef() override;
        FlULong DACOM_API Release() override;
        FlULong RefCount;
        FMaterialImpl* Owner;
    };

    FInner Inner_;
    IDAComponent* ControllingOuter_;
    EMaterialKind Kind_;
    const char* FactoryName_;
    IDAComponent* Outer_;
    IRenderPipeline* Pipeline_;
    ITextureLibrary2* TextureLibrary_; // Borrowed from the outer component.
    IVertexBufferManager* VertexBufferManager_;
    IRPDraw* RpDraw_;
    IMaterialLibrary* MaterialLibrary_;
    FlULong* TextureBlock_;
    float MaterialBlock_[17];
    unsigned char Flag64_;
    unsigned char Flag65_;
    unsigned char bBackdrop_;
    unsigned char bBackdropNameMatchCopy_; // Retail +69: duplicate stored Backdrop name-equality result; no render purpose inferred.
    unsigned char bNomadRegular_;
    unsigned char bSecondTexture_;
    unsigned char Pad71_;
    float NomadDead68_;
    float NomadLive6C_;
    // Nomad Slot28 index 0 returns a three-float Diffuse Color span at retail +0x74.
    // Keep it separate from the Wire-only color and packed-state members.
    float NomadDiffuseColor_[3];
    FlULong HudAnimFlag_;
    IImaInstance* MatAnim_;
    FlULong EnvMapFlag_;
    // Stage texture handle resolved by BindEnvMap and reused by Submit.
    FlULong EnvMapResolvedHandle_;
    // EnvMap record remains separate from Shadow layer count / record / resolved texture.
    FlULong ShadowHandle_; // EnvMap retained texture record (retail +0x68).
    ITextureLibrary2* ShadowTextureLibrary_; // borrowed Shadow +0x6c
    void* ShadowTextureRecord_;              // retained Shadow +0x74
    FlULong ShadowResolvedHandle_;    // Shadow +0x70 pipeline texture
    FlULong ShadowLayerCount_;        // Shadow +0x68 candidate layer count
    bool bShadowTextureInitialized_;         // Shadow +0x78 one-shot lookup
    // CRC of the texture name currently retained in ShadowHandle_.
    FlULong EnvMapTexNameCrc_;
    FlULong StageCounter_;
    // Wire and Wire2 store RGBA here, initially white.
    float WireColorR_;
    float WireColorG_;
    float WireColorB_;
    float WireField74_;
    // Wire ARGB color; Wire2 additionally keeps a reduced RGB color for shifted points.
    FlULong WireField78_;
    FlULong WireField7C_;
    // Cached facegroup cursor; retry group zero once when selection fails.
    FlULong WireGroupIndex_;
    FlULong Exclusion68_;
    // Colorize alpha component used for render flags and base opacity.
    float ColorizeExtraW_;
    // Colorize Options is a raw flags word, not a float.
    FlULong ColorizeOptionsFlags_;

    // Owned lowercase name; replaced on SetName and destroyed after textures.
    char* InstanceName_; // Owned, capped at 64 bytes including terminator.
    bool bInstanceNameSet_;

    // Number of entries in the per-instance texture array shared by bind, resolve and setup.
    FlULong TextureVerifyCount_;
};
