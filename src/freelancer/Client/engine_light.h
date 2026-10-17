// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Client light object — E:\FL\Scratch\Source\Client\cmn\EngineLight.cpp.
//
// Retail allocates it with `operator new(200)` in FUN_00556fd0 and drives it through
// FUN_00410ca0 (ctor), FUN_00411120 (set the D3DLIGHT8), FUN_00410ef0 (set position),
// FUN_00410f60 (set orientation) and FUN_00410dd0 (hand it to the render pipeline). The
// vtable is PTR_FUN_005c89d4.
//
// The whole object is 0xC8 bytes and its D3DLIGHT8 sits at +0x0C, because both pipeline calls
// — AddLight (slot 73, +0x124) and UpdateLight (slot 75, +0x12C) — are handed `this + 0x0C`
// directly. FEngineLight below keeps those offsets.

// The three curve fields below are GraphLibrary curves (graph_library.h): [LightSource]'s
// ambient_curve / atten_curve / color_curve name them, GraphLibraryFind (FUN_00419760)
// AddRefs the one it returns, and the light Releases (slot 9) the old one before overwriting.
#include "graph_library.h"
#include "common_metalist.h"

struct ICamera;

// D3DLIGHT8 as the exe lays it out: 0x68 bytes at light +0x0C.
struct FD3dLight8
{
    unsigned long Type;   // +0x00 (light +0x0C) 1 point, 2 spot, 3 directional
    float Diffuse[4];     // +0x04
    float Specular[4];    // +0x14
    float Ambient[4];     // +0x24
    float Position[3];    // +0x34 (light +0x40)
    float Direction[3];   // +0x40 (light +0x4C)
    float Range;          // +0x4C (light +0x58)
    float Falloff;        // +0x50 (light +0x5C)
    float Attenuation0;   // +0x54 (light +0x60)
    float Attenuation1;   // +0x58 (light +0x64)
    float Attenuation2;   // +0x5C (light +0x68)
    float Theta;          // +0x60 (light +0x6C)
    float Phi;            // +0x64 (light +0x70)
};

struct FEngineLight
{
    explicit FEngineLight(int LightType = -1);
    virtual ~FEngineLight();
    virtual bool Enable();
    virtual void Disable();

    unsigned Field04;     // +0x04 intrusive light-list Next (retail DAT_006164e0 = list head)
    unsigned Field08;     // +0x08 intrusive light-list Prev (retail DAT_006164e4 = list tail,
                           // DAT_006164e8 = count). The list-insert routine that actually links a
                           // light in (setting Flags bit 4) is a different, not yet ported
                           // function; this batch only ports the unlink side (dtor / Unregister),
                           // so these fields and G_LightListHead/Tail below stay unused until it
                           // lands, exactly like retail's dead branches would be for an
                           // unregistered light.
    FD3dLight8 D3d;       // +0x0C
    float Orient[9];      // +0x74 3x3, identity from the ctor
    float Position[3];    // +0x98
    float Color[3];       // +0xA4 mirror of D3d.Diffuse rgb, 1.0 from the ctor
    int Type;             // +0xB0 1/2/3 kept, -2 kept, anything else becomes -1
    unsigned char Flags;  // +0xB4 bit 0 = unknown caller flag (FUN_004110c0/EngineLightSetType
                           // sets it verbatim, retail meaning not recovered), bit 2 = registered,
                           // bit 3 = enabled, bit 4 = linked into the global light list,
                           // bit 5 = has a D3DLIGHT8
    unsigned char PadB5[3];
    IGraph* AmbientCurve; // +0xB8 "ambient_curve" (FUN_00411690 @ 00411a8c); an ambient light
                           // (Type -2) scales its colour by it over [0, Range]
    IGraph* ZoneCurve;    // +0xBC "atten_curve" (FUN_00411d90): scales the colour over [0, Range]
    IGraph* ColorCurve;   // +0xC0 "color_curve" (FUN_00411e90): tints the colour over time
    float ColorCurvePeriod; // +0xC4 FUN_00411e90's second argument: the Max of the wrapping
                           // range ColorCurve is evaluated with (FUN_00410850)
};

#include <stddef.h>

static_assert(sizeof(FD3dLight8) == 0x68, "D3DLIGHT8 is 0x68 bytes");
static_assert(offsetof(FD3dLight8, Position) == 0x34, "D3DLIGHT8 Position at +0x34");
static_assert(offsetof(FD3dLight8, Phi) == 0x64, "D3DLIGHT8 Phi at +0x64");
static_assert(offsetof(FEngineLight, D3d) == 0x0C, "light D3DLIGHT8 at +0x0C");
static_assert(offsetof(FEngineLight, Orient) == 0x74, "light orientation at +0x74");
static_assert(offsetof(FEngineLight, Position) == 0x98, "light position at +0x98");
static_assert(offsetof(FEngineLight, Color) == 0xA4, "light colour at +0xA4");
static_assert(offsetof(FEngineLight, Type) == 0xB0, "light type at +0xB0");
static_assert(offsetof(FEngineLight, Flags) == 0xB4, "light flags at +0xB4");
static_assert(sizeof(FEngineLight) == 0xC8, "retail operator new(200)");

// FUN_00410ca0 — construct in place. LightType 1/2/3 and -2 are kept, anything else is -1.
void EngineLightInit(FEngineLight* Light, int LightType);
// FUN_00411120 — copy a D3DLIGHT8 in. Position and Direction are deliberately not copied:
// retail skips src[0x0D..0x12], because FUN_00410ef0 / FUN_00410f60 own those.
void EngineLightSetLight(FEngineLight* Light, FD3dLight8 const* Source);
// FUN_00410ef0 — world position. Also writes the D3DLIGHT8 Position with a negated Z for
// every type but directional.
void EngineLightSetPosition(FEngineLight* Light, float const* Pos3);
// FUN_00410f60 — 3x3 orientation.
void EngineLightSetOrientation(FEngineLight* Light, float const* Orient9);
// FUN_00410dd0 — register with DALib::Pipeline (slot 73) and raise the enabled bit.
bool EngineLightRegister(FEngineLight* Light);

// FUN_00410e20 — vtable slot 2 ("Unregister"): drop the enabled bit and, if linked into the
// global light list, unlink it and tell the pipeline to drop the light (slot 41). Distinct from
// EngineLightDestroy: this clears Flags bit 3 (enabled), the destructor clears bit 2
// (registered). Nothing currently sets bit 4 (see FEngineLight::Field04 above), so the unlink
// branch is presently dead, same as in retail before the light is ever list-inserted.
void EngineLightUnregister(FEngineLight* Light);
// FUN_00410ec0 — the PushLightUpdate tail every setter below already runs inline; exposed
// standalone because retail calls this exact 4-line sequence as its own out-of-line function
// from elsewhere (e.g. the not-yet-ported [LightSource] INI loader around FUN_00411690).
void EngineLightPushUpdate(FEngineLight* Light);
// FUN_00410fe0 — combined "set orientation + position" from one {Orient[9], Pos[3]} block, in
// that order (position first, then orientation) with the pipeline pushed after each half,
// exactly like calling EngineLightSetPosition then EngineLightSetOrientation back to back.
void EngineLightSetTransform(FEngineLight* Light, float const* Orient9, float const* Pos3);
// FUN_004110c0 — set Flags bit 0 to Bit0 (retail meaning not recovered) and the light type via
// the same switch as EngineLightInit/EngineLightSetLight (StoreType, always touching D3d.Type).
void EngineLightSetType(FEngineLight* Light, int LightType, bool Bit0);
// FUN_004112f0 — raw D3d.Range write, no attenuation recompute and no pipeline push (unlike
// EngineLightSetRadius below). Retail callers: 005011af (nebula directional light,
// cli_nebula.cpp) and 005060e5 (gate tunnel light, gate_tunnel.cpp), both with 500000.
void EngineLightSetRangeRaw(FEngineLight* Light, float Range);
// FUN_00411300 — "SetRadius": Range=Radius, Attenuation0=1, Attenuation1=0,
// Attenuation2=16/Radius^2, then pushes the update.
void EngineLightSetRadius(FEngineLight* Light, float Radius);
// FUN_00411470 — set the light from a direction vector instead of a full orientation matrix:
// derives D3d.Direction (non-point lights) and back-fills Orient's look column (Orient[2],
// Orient[5], Orient[8]) from it, then pushes the update.
void EngineLightSetDirection(FEngineLight* Light, float const* Dir3);
// FUN_004114f0 and FUN_004115b0 — byte-triplet colour setters. Retail's two functions have
// byte-identical bodies (Color + D3d.Diffuse.rgb from RGB/255, Diffuse.a=1, then push); kept as
// two entry points since they're genuinely separate retail symbols/call sites.
void EngineLightSetColorBytes(FEngineLight* Light, unsigned char const RGB[3]);
void EngineLightSetColorBytesAlt(FEngineLight* Light, unsigned char const RGB[3]);
// FUN_00411670 — "SetConeAngleDegrees": Falloff=1, Phi=degrees in radians, Theta=Phi/2. Notably
// does NOT push the update (retail has no pipeline call here, unlike every other setter).
void EngineLightSetConeAngle(FEngineLight* Light, float DegreesFullAngle);

// FUN_00411350 — generic 3-float negate. Lives in EngineLight.cpp in retail (13 call sites, most
// outside this subsystem) but isn't otherwise identified/ported, so it's exposed here rather than
// re-declared per caller.
void Vec3Negate(float* Out, float const* In);
// FUN_00410190 (thiscall on a 3-float vector) — sqrt((x*x + y*y) + z*z), left on the FPU
// (so returned as double).
double Vec3Length(float const* V);
// FUN_00411d60 — generic Out = Scalar * V.
void ScaleVec3(float* Out, float const* V, float Scalar);
// FUN_00411c50 — pack two nibbles into a byte-ish uint: *Out = (Lo & 0xF) | (Hi << 4). Purpose
// beyond the raw bit-op not recovered.
void PackNibbleByte(unsigned int* Out, int Hi, unsigned int Lo);
// FUN_00411bc0 — _ftol each of Rgb3 (0..255 scale), clamp to [0, 0xFF], store in the same
// order (Out4[i] from Rgb3[i]) and set Out4[3] = 0xFF. Returns Out4.
unsigned char* EngineLightPackColorBytes(unsigned char* Out4, float const* Rgb3);
// FUN_00411c80 — Bytes3[i] / 255 into OutRgb3[i]. Returns OutRgb3.
float* EngineLightUnpackColorBytes(unsigned char const* Bytes3, float* OutRgb3);
// FUN_00411cd0 — parse a [LightSource] type string. "AMBIENT"->-2, "POINT"->1, "CONE"->2,
// "DIRECTIONAL"->3, anything else logs an FDUMP error and returns -1.
int EngineLightParseType(char const* Text);
// FUN_00411f70 — write column 2 of a row-major 3x3 (elements 2, 5, 8). Shared: also called from
// 005143f8, 0051673d and 00519c72.
void MatrixSetColumn2(float* Matrix9, float const* Vec3);

// The {length, text} buffer FUN_00411f00 fills from a C string (at most 0x7F characters).
struct FLightCurveName
{
    unsigned Len;
    char Text[0x80];
};
// FUN_00411f00.
FLightCurveName* LightCurveNameInit(FLightCurveName* Name, char const* Source);

// FUN_00411d90 — "atten_curve": an ambient light (Type -2) takes the graph as AmbientCurve,
// any other light as ZoneCurve (FDUMP "Invalid dynamic light type" unless the type is 1..3).
// Releases the previous curve first; MessageBox "Error: unknown graph name" when the graph is
// missing.
void EngineLightSetAttenCurve(FEngineLight* Light, char const* GraphName);
// FUN_00411e90 — "color_curve": ColorCurve = the graph, ColorCurvePeriod = Period.
void EngineLightSetColorCurve(FEngineLight* Light, char const* GraphName, float Period);
// FUN_00411690 — one [LightSource] key: pos, range, color, type, attenuation, direction,
// ambient_curve, atten_curve, color_curve. Returns false for any other key.
bool EngineLightReadValue(FEngineLight* Light, char const* Name, char const* Value);

// FUN_00556fd0(Csys const* Frame, D3DLIGHT8 const* Src, Vector const* Direction, byte Group),
// the light part. Frame12 is a position followed by a 3x3 orientation (a Csys: RoomLightInfo
// +0x00, or the snap's Pos/Orient pair). Allocates the 0xC8 light, constructs it with type -1
// (retail passes -1, not the source type — FUN_00411120 sets the real one), copies the source,
// forces theta/phi to pi for a point light, sets the position, then the orientation
// LookMatrixYup(Orient * Direction3) through DAT_006799bc, then registers it (slot 1).
FEngineLight* EngineLightCreate(float const* Frame12, FD3dLight8 const* Source,
                                float const* Direction3);

// The Thorn light-props block: 0x90 bytes at snap+0x44. Offsets recovered from the two
// FUN_00556fd0 call sites (00453075 / 0044975c): +0x0C is the light's local direction (turned
// by the entity orientation in FUN_00556fd0) and +0x28 is the D3DLIGHT8 itself, which fills the
// block exactly (0x28 + 0x68 = 0x90).
FD3dLight8 const* ThornLightPropsD3d(float const* Props36);
float const* ThornLightPropsTarget(float const* Props36);
// FUN_00557130 — release slot 0 and free.
void EngineLightDestroy(FEngineLight* Light);
// FUN_00410ba0 — vtable slot 0 itself, called with 1: the FUN_00410ab0 body (inlined in retail)
// and operator delete. For a light from operator new(200) + EngineLightInit, e.g.
// ShipViewControl's three (ctor 0x00583a50, dtor 0x00583cee).
void EngineLightDelete(FEngineLight* Light);

// FUN_00410a40 — light range/distance test ("does this light reach that point"). A
// directional light (Type 3) without flag bit 0x2 always affects every point; everything
// else is a plain squared-distance check against D3d.Range padded by ExtraRadius. Requires
// the enabled bit (Flags & 8); returns false otherwise.
bool EngineLightAffectsPoint(FEngineLight const* Light, float const* Point3, float ExtraRadius);

// FUN_00410850 (__thiscall, RET 8) — effective RGB colour: starts from Color, then optionally
// modulates it through two GraphLibrary curves: ColorCurve (+0xC0, slot 7, at the client
// elapsed time over the wrapping range [0, ColorCurvePeriod], packed 0x00RRGGBB) and ZoneCurve
// (+0xBC, slot 6, at Dist over the clamped range [0, D3d.Range], a scalar that dims all three
// channels). Dist is passed by value -- both retail callers (004105c0, 004109a7) push the
// distance FUN_00411370 wrote. Returns true if either modifier applied.
bool EngineLightGetEffectiveColor(FEngineLight* Light, float Dist, float* OutColor3);

// FUN_00411370 — unit direction toward Point (point/spot with flag bit1), else −Orient col2.
// Optional OutDist receives |Point − Position| when Point != null.
float* EngineLightComputeDirection(FEngineLight* Light, float* OutDir3, float const* Point,
                                   float* OutDist);

// FUN_00410960 (__thiscall, RET 0x10) -- sample the light at Point. False, with nothing written,
// when the light is not enabled (Flags bit 3). Otherwise OutDir3 = the FUN_00411370 direction,
// OutColor3 = the FUN_00410850 colour at the distance FUN_00411370 wrote, and for an ambient
// light (Type -2) InOutAmbient3 += Scale * OutColor3, Scale being the AmbientCurve at that
// distance over {clamp, 0, D3d.Range}, or 1.0 without a curve. Returns true. Caller:
// SpaceTerrainRenderComp's billboard lighting FUN_0054e130 (asteroid_billboard.cpp).
bool EngineLightSample(FEngineLight* Light, float const* Point, float* OutDir3,
                       float* InOutAmbient3, float* OutColor3);

// FUN_00410280 (cdecl) -- D3DRS_AMBIENT (0x8b) read back into OutRgb: [0] = bits 16..23,
// [1] = bits 8..15, [2] = bits 0..7. Nothing when OutRgb is null.
void EngineLightGetAmbientRgbBytes(unsigned char* OutRgb);

// FUN_004104e0 — per-object dynamic light for the nearby-collect path. Camera is used
// when DAT_00610588 != 0 (soft path: camera slot +0x40 → TransformXform/Rotate via
// Acquire3DMathEngine / DAT_006164ec, then SetLight). HW path (state 0) skips that arm.
// Returns whether the light stayed lit.
bool EngineLightApplyNearby(FEngineLight* Light, ICamera* Camera, float const* Pos3,
                            float* InOutAmbient);

// FUN_004101e0 — walk DAT_006164e0, SetLightEnable(D3d,0), clear Flags bit4, zero head/tail/count.
void EngineLightClearRegisteredList();

// FUN_004102c0 (cdecl) — apply a selected light set. LIGHTING option (0x18) off: disable and
// unlink every registered light, D3DRS_LIGHTING (0x89) = 0. On: read D3DRS_AMBIENT (0x8b) back
// as three floats, disable and unlink every registered light, run FUN_004104e0 on each of the
// Count lights (which re-links the lit ones and adds ambient-type lights into the floats), then
// write the floats back to D3DRS_AMBIENT as 0xFFRRGGBB. Pos3 may be null (FUN_005572b0 passes 0).
void EngineLightApplyList(FEngineLight* const* Lights, unsigned Count, ICamera* Camera,
                          float const* Pos3);

// DAT_00675570 — gamelit enable list head (FUN_00537690 links, FUN_005376f0 unlinks), chained
// through FGameLight::ListNext (+0xC8). LightGridRebuild (FUN_00537c30) walks Head → Next.
// Only GameLights (0x12C, or the 0x130 CliEquipLight light) are ever on it.
FEngineLight* EngineLightGameListHead();
FEngineLight* EngineLightGameListNext(FEngineLight* Light);
// FUN_00537690 — GameLight vtable +0x4, thiscall on a GameLight: already lit (Flags bit 3) ->
// true; else FUN_00410dd0 (register), and on success append to DAT_00675570/74 (+0xCC = old
// tail, +0xC8 = 0), ++DAT_0067556c, ++RefCount (+0xD0). Light must be a GameLight's Eng.
bool GameLightEnable(FEngineLight* Light);
// FUN_005376f0 — GameLight vtable +0x8: lit only -> FUN_00410e20 (unregister), unlink from
// DAT_00675570/74, clear +0xC8/+0xCC, --DAT_0067556c, --RefCount and vtable slot 0 (1) at <= 0.
void GameLightDisable(FEngineLight* Light);

// FUN_00410240 — pack RGB bytes to D3DCOLOR-ish dword and Pipeline SetRenderState(0x8b, packed).
// Memory order: R at [0], G at [1], B at [2] (listing MOVZX chain → OR into ESI).
void EngineLightSetAmbientRgbBytes(unsigned char const Rgb[3]);

// ---------------------------------------------------------------------------
// GameLight (gamelit.cpp) — extends EngineLight to 300 bytes (0x12C).
// Retail Heap DAT_006789a4 block size is 300 (FUN_00537a90).
// ---------------------------------------------------------------------------

// Layout beyond FEngineLight (0xC8):
//   +0xC8 / +0xCC  DAT_00675570 gamelit list Next/Prev (FUN_00537690 / FUN_005376f0)
//   +0xD0          refcount (FUN_00537ac0 / FUN_00537ad0)
//   +0xD4..+0xE8   FGameLightAnimSlot (FUN_005388e0 this)
//   +0xEC          engine instance (-1 = none)
//   … through +0x128
// fx\lightanim.ini -- [LightAnim] sections, loaded by LightAnimationLoad (FUN_00538b20) into the
// global OwnerList<Light::AnimationArch> DAT_00675558 (static constructor LAB_00538a40, atexit
// FUN_00539000). Names are retail's (RTTI .?AUAnimationArch@Light@@, .?AUAnimFrame@Light@@,
// .?AV?$MetaList@X@@ for the key-buffer list).
namespace Light
{
    // One "frame = time, r, g, b, intensity" line: operator new(0x10) in FUN_00538b20.
    struct AnimFrame
    {
        float Time;              // +0x00 get_value_float(0); scaled by the bind's time scale
        unsigned char Rgb[3];    // +0x04 ftol(get_value_float(1..3))
        unsigned char Pad07;     // +0x07 never written
        float Intensity;         // +0x08 get_value_float(4); <= 0 becomes 0.01; scaled at bind
        int EndMarker;           // +0x0C 0; -1 on the last frame of a section (one-shot)
    };

    static_assert(sizeof(AnimFrame) == 0x10, "Light::AnimFrame is 0x10 bytes");

    // operator new(0x4C). vtable PTR_FUN_005de730 { FUN_00538690 deleting dtor }.
    struct AnimationArch : public OwnerList<AnimFrame>
    {
        // Inlined at 00538bb8..00538bde: OwnerList<AnimFrame>(false) (FUN_00538ed0 + vptr),
        // NameLen = 0, Name[0] = 0, KeyBuffers(true) (FUN_00538fe0).
        AnimationArch();
        // FUN_00538560: free() every spare key buffer, then ~MetaList<void>, then
        // ~OwnerList<AnimFrame>.
        virtual ~AnimationArch();

        unsigned NameLen;          // +0x14 FUN_00417c50's return
        char Name[0x20];           // +0x18 "name"; FUN_00538e80 stricmps it
        // +0x38 malloc'd copies of the frame list that bound lights have handed back
        // (FUN_005388e0 / FUN_005379b0 / FUN_005375d0 append, FUN_005388e0 reuses the head).
        MetaList<void> KeyBuffers;
    };
}

// OwnerList<Light::AnimationArch>::free (FUN_00538ab0) and its destructor (FUN_00539000) release
// each element through its virtual deleting destructor (`PUSH 1; CALL [EAX]`), not a bare
// operator delete.
template <>
inline void OwnerListDeleteData<Light::AnimationArch>(Light::AnimationArch* Data)
{
    delete Data;
}

static_assert(offsetof(Light::AnimationArch, NameLen) == 0x14, "AnimationArch NameLen +0x14");
static_assert(offsetof(Light::AnimationArch, Name) == 0x18, "AnimationArch Name +0x18");

// FUN_00538b20 -- __cdecl(char const* Path), called from FUN_0054aa80 @ 0054ac70 with
// GetFileSysPath("fx\lightanim.ini"). Appends one AnimationArch per [LightAnim] section.
void LightAnimationLoad(char const* Path);
// FUN_00538ab0 -- OwnerList<Light::AnimationArch>::free on the global list; retail calls it
// from the space teardown FUN_0054aec0 @ 0054afd4.
void LightAnimationFreeAll();

struct FGameLightAnimSlot
{
    Light::AnimFrame* Keys;      // +0x00 / Light+0xD4 private copy of Def's frames
    int KeyCount;                // +0x04 / +0xD8
    Light::AnimationArch* Def;   // +0x08 / +0xDC
    int CurrentIndex;            // +0x0C / +0xE0  (-1 = past end)
    float Elapsed;               // +0x10 / +0xE4
    float RangeScale;            // +0x14 / +0xE8  light Range at bind
};

static_assert(sizeof(FGameLightAnimSlot) == 0x18, "anim slot 0x18");

struct FGameLight : FEngineLight
{
    FGameLight();
    ~FGameLight() override;
    bool Enable() override;
    void Disable() override;
    virtual void Tick(float Dt);
    static void operator delete(void* Memory);

    FGameLight* ListNext;       // +0xC8 DAT_00675570 list
    FGameLight* ListPrev;       // +0xCC
    int RefCount;               // +0xD0
    FGameLightAnimSlot Anim;    // +0xD4
    unsigned EngineIndex;       // +0xEC  -1 none
    float LocalXform[12];       // +0xF0
    float Field120;             // +0x120 velocity/offset
    float Field124;             // +0x124
    float Field128;             // +0x128
};

static_assert(offsetof(FGameLight, RefCount) == 0xD0, "GameLight RefCount +0xD0");
static_assert(offsetof(FGameLight, Anim) == 0xD4, "GameLight Anim +0xD4");
static_assert(offsetof(FGameLight, Anim.Def) == 0xDC, "Anim.Def +0xDC");
static_assert(sizeof(FGameLight) == 0x12C, "GameLight 300 bytes");

// A GameLight's vtable slot 2 (+8), the shape of every retail disable site
// (`MOV EAX,[ECX]; CALL [EAX+8]` -- a raw scan finds no direct CALL FUN_005376f0).
inline void GameLightVirtualDisable(FGameLight* Light)
{
    Light->Disable();
}

// DAT_006789c0 -- the game light unit's lazily filled I3DMathEngine (FUN_005b73e0 on first
// use). Its only readers are FUN_005377c0 (0x005377f9, CliExplosionAttachLocalXform) and
// FUN_00537860 (0x00537893, GameLightTick); raw PE scan finds no other reference.
struct I3DMathEngine;
extern I3DMathEngine* G_GameLightMath;

// FUN_005374e0 — construct GameLight in place (EngineLightInit(-1) + GameLight fields).
FGameLight* GameLightConstruct(FGameLight* Light);

// FUN_00537af0 — __cdecl
FGameLight* GameLightCreatePoint(float const* Pos3, unsigned char const Rgb[3], float Range);

// FUN_00538e80 — __cdecl; the first AnimationArch whose Name stricmps equal, else null.
Light::AnimationArch* GameLightFindAnimByName(char const* Name);

// FUN_005388e0 — __thiscall on Anim slot at Light+0xD4, RET 0x14
//   Anim, RangeScale, IntensityScale, TimeScale, StartTime
// Hands any previous key copy back to its AnimationArch, then takes Anim's first spare
// KeyBuffers entry (or mallocs count * 0x10), copies the frames in with Time *= TimeScale and
// Intensity *= IntensityScale, and seeds CurrentIndex from StartTime.
void GameLightBindAnimKeys(FGameLightAnimSlot* Slot, Light::AnimationArch* Anim, float RangeScale,
                           float IntensityScale, float TimeScale, float StartTime);

// FUN_00537960 — __thiscall on GameLight, RET 0x10
// GameLightBindAnimKeys(slot, Anim, Light->D3d.Range, IntensityScale, TimeScale, StartTime);
// then RefCount++ and true when the slot holds keys.
bool GameLightAttachAnim(FGameLight* Light, Light::AnimationArch* Anim, float IntensityScale,
                         float TimeScale, float StartTime);

// FUN_005386d0 — advance Slot by Dt and push colour/radius onto Light.
void GameLightTickAnim(FGameLightAnimSlot* Slot, float Dt, FGameLight* Light);

// FUN_00537860 — per-frame: free-motion (EngineIndex==-1) or MulTransform attach
// (GetInstanceTransform × LocalXform@+0xf0 → SetTransform); then TickAnim when Keys != null.
void GameLightTick(FGameLight* Light, float Dt);

// FUN_00538390 — walk DAT_00675570, vtable+0xc(Dt) = GameLightTick. Retail call site
// LAB_0054b000 (space ClientObj Update) immediately before LightGridRebuild (00537c30),
// gated by GetGameOption(LIGHTING=0x18).
void GameLightTickAll(float Dt);

// FUN_005383c0 (cdecl, caller StarSysLeave 00554bc2): every GameLight on DAT_00675570 gets
// ++RefCount, its animation detached (the inlined FUN_005379b0), vtable slot 2 (disable) and
// --RefCount with slot 0 (1) at <= 0; the ones still referenced are chained on a local list
// and, in a second pass, unlinked from it and re-enabled through vtable slot 1.
void GameLightLeaveSystem();

// FUN_00538520 (cdecl, caller the space teardown 0054afca): --RefCount on every GameLight of
// DAT_00675570 (next read first), vtable slot 0 (1) at <= 0. The list itself is left as is.
void GameLightReleaseAll();

// FUN_00537a90 — GameLight::operator new: size==0x12c → Heap::Malloc on the GameLight pool
// (DAT_006789a4); any other size → malloc. Every GameLight goes back through FUN_005375a0's
// Heap::Free, whichever way it was allocated.
void* GameLightHeapAlloc(size_t Size);

// FUN_00537ab0 — Heap::Shrink on the GameLight pool. Retail calls it from the space teardown
// FUN_0054aec0 @ 0054afcf (DestroySpaceState), right before FUN_00538ab0.
void GameLightHeapShrink();

// FUN_00537ac0 — RefCount++ at +0xD0.
void GameLightAddRef(FGameLight* Light);

// FUN_00537ad0 — drop one ref; destroy at 0.
int GameLightRelease(FGameLight* Light);

class INI_Reader;
// FUN_00537a40 — __thiscall(INI_Reader*): feed every value of the current section to
// EngineLightReadValue, then vtable slot 1 (enable). Retail caller: the [LightSource] branch of
// the system loader at 00553db4.
void GameLightLoad(FGameLight* Light, INI_Reader* Reader);

// FUN_005379b0 — if Anim.Keys is set: hand the keys back to Def->KeyBuffers (when Def is set),
// then vtable slot 2 (disable), RefCount--, and the deleting destructor (slot 0) at <= 0.
void GameLightAnimDetach(FGameLight* Light);
