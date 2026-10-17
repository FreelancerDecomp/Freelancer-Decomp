// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail Client/Space/CliAsteroid.cpp — client asteroid-field object (operator_new 0x238).
// Primary type constructed by FUN_005201e0; registered in DAT_006753c4; loaded by FUN_0051ff50
// / FUN_00520850. Distinct from Archetype::CliAsteroid (ICliObj companion) and from
// CmnAsteroid::CAsteroidField (Common.dll field sim at this+0x04).
//
// No vtable at +0 (Flag00). static_assert sizeof == 0x238 is mandatory.
//
// Subobjects:
//   +0xFC  Band blob 0x84 (FUN_0054c580 / [Band] FUN_0054c630)
//   +0x19C FAsteroidBillboardBlock ([AsteroidBillboards] FUN_0054dbe0 / 00550000 / 0054fee0)

#include "common_casteroid.h"
#include "common_ini.h"
#include "common_universe.h"
#include "render/IRenderPipeline.h"  // FRpVertexSrcDesc
#include "texture_shapes.h"

#include <cstddef>

struct IMaterial;

// One billboard rock, 0x20 bytes: the array FUN_0054de10 allocates at block +0x0C (operator new
// of RockCount << 5) and FUN_0054e900 / FUN_0054f9f0 walk.
#pragma pack(push, 1)
struct FAsteroidBillboardRock
{
    float Pos[3];                     // +0x00 zeroed by FUN_0054de10, placed by FUN_0054e900
    float HalfSize;                   // +0x0C (rand * (SizeMax - SizeMin) / 32768 + SizeMin) / 2
    float Alpha;                      // +0x10 1.0 at creation, recomputed every draw
    unsigned char bVisible;           // +0x14
    unsigned char bFlipU;             // +0x15 corner-UV / corner-colour index, bit 0
    unsigned char bFlipV;             // +0x16 corner-UV / corner-colour index, bit 1
    unsigned char bSwapAxes;          // +0x17 FUN_0054f9f0: which of right/up spans the base
    unsigned char bNegateApex;        // +0x18 FUN_0054f9f0: apex offset negated
    unsigned char Pad19[3];
    float Phase;                      // +0x1C Index / RockCount, the density-cull threshold
};
#pragma pack(pop)

static_assert(sizeof(FAsteroidBillboardRock) == 0x20, "billboard rock stride 0x20");

// Billboard subobject at FCliAsteroid+0x19C (FUN_0054dbe0 this). Size through Flag7C+pad = 0x80,
// so Flag21C of the parent sits immediately after at +0x21C. Retail source:
// Client\Space\SpaceTerrainRenderComp.cpp (the FDUMP path at .rdata:005e0b08).
#pragma pack(push, 1)
struct FAsteroidBillboardBlock
{
    float Float00;                    // +0x00 (+0x19C) ctor 2500; FUN_0054de10: the fade radius
                                      //        R = Q*start_dist*tan(35deg) / (1 - tan(35deg))
    float Float04;                    // +0x04 (+0x1A0) ctor 4000; FUN_0054de10: R + Q*start_dist,
                                      //        the distance of the rock sphere's centre ahead
    float StartDist;                  // +0x08 (+0x1A4) ctor 1500 / start_dist
    FAsteroidBillboardRock* Rocks;    // +0x0C (+0x1A8) FUN_0054de10 operator new
    int Count;                        // +0x10 (+0x1AC) count
    int RockCount;                    // +0x14 (+0x1B0) ftol(Count * QualityA), ctor 100
    Universe::IZone* ZoneRef;         // +0x18 (+0x1B4)
    float DensityScale;               // +0x1C (+0x1B8) rocks with Phase above it are culled
    FTexShape const* Shape;           // +0x20 (+0x1BC) shape
    float SizeMin;                    // +0x24 (+0x1C0) size[0]
    float SizeMax;                    // +0x28 (+0x1C4) size[1]
    float Float2C;                    // +0x2C (+0x1C8) ctor 700; FUN_0054de10: R * fade pct
    float FadeDistPercent;            // +0x30 (+0x1CC) fade_dist_percent (def 0.3)
    float Up[3];                      // +0x34 (+0x1D0) ctor (0, 1, 0); FUN_0054e900 rewrites it
                                      //        every draw as normalise(right x forward)
    float Float40;                    // +0x40 (+0x1DC) FUN_0054feb0 max(fade planes)
    float Float44;                    // +0x44 (+0x1E0) FUN_0054feb0 min(fade planes)
    int MaxLights;                    // +0x48 (+0x1E4) ctor 4, FUN_0054e130's light cap
    float TintedDiffuse0;             // +0x4C (+0x1E8) FUN_0054fee0 out
    float TintedDiffuse1;
    float TintedDiffuse2;
    float TintedAmbient0;             // +0x58 (+0x1F4)
    float TintedAmbient1;
    float TintedAmbient2;
    float ColorShift0;                // +0x64 (+0x200) color_shift
    float ColorShift1;
    float ColorShift2;
    float AmbientIntensity;           // +0x70 (+0x20C)
    float QualityA;                   // +0x74 (+0x210) FUN_00550000
    float QualityB;                   // +0x78 (+0x214)
    unsigned char Flag7C;             // +0x7C (+0x218)
    unsigned char Pad7D[3];
};
#pragma pack(pop)

static_assert(sizeof(FAsteroidBillboardBlock) == 0x80, "Billboard block 0x80");
static_assert(offsetof(FAsteroidBillboardBlock, Rocks) == 0x0C, "billboard Rocks +0x0C");
static_assert(offsetof(FAsteroidBillboardBlock, Up) == 0x34, "billboard Up +0x34");
static_assert(offsetof(FAsteroidBillboardBlock, MaxLights) == 0x48, "billboard MaxLights +0x48");
static_assert(offsetof(FAsteroidBillboardBlock, TintedDiffuse0) == 0x4C, "billboard diffuse +0x4C");
static_assert(offsetof(FAsteroidBillboardBlock, AmbientIntensity) == 0x70, "billboard amb +0x70");

// FUN_0051feb0 fills the vertex-source descriptor at FCliAsteroid+0x30 (0x70 bytes): the
// published FRpVertexSrcDesc (render/IRenderPipeline.h), flags 2, five streams into the
// merged 0x2C-byte vertices -- position +0 (0xc), normal +0xc (0xc), diffuse +0x18 (4),
// texcoord 0 +0x1c (8), texcoord 1 +0x24 (8) -- FVF 0x252 (XYZ | NORMAL | DIFFUSE | TEX2).
static_assert(sizeof(FRpVertexSrcDesc) == 0x70, "MeshDesc 0x70 at +0x30");

#pragma pack(push, 1)
struct FCliAsteroid
{
    unsigned char Flag00;                              // +0x00
    unsigned char Pad01[3];
    CmnAsteroid::CAsteroidField* CmnField;             // +0x04
    unsigned char MapAt08[0x14];                       // +0x08
    Universe::IZone* Zone;                             // +0x1C
    int CubeSize;                                      // +0x20
    int FillDistAt24;                                  // +0x24
    float EmptyCubeFreq;                               // +0x28  empty_cube_frequency
    unsigned char Flag2C;                              // +0x2C
    unsigned char Pad2D[3];                            // +0x2D
    FRpVertexSrcDesc MeshDesc;                         // +0x30  FUN_0051feb0
    IMaterial* Material;                               // +0xA0 first IMesh material
    void* MeshVerts;                                   // +0xA4  merged stride-0x2C verts
    void* MeshIndices;                                 // +0xA8  ushort indices
    unsigned MeshIndexCount;                           // +0xAC
    unsigned FieldB0;                                  // +0xB0  IMesh slot write
    float TintB4;                                      // +0xB4
    float TintB8;
    float TintBC;
    float TintC0;                                      // +0xC0
    float TintC4;
    float TintC8;
    float ScaleCC;                                     // +0xCC  max_alpha
    unsigned char FlagD0;                              // +0xD0  ambient_increase
    unsigned char FlagD1;
    unsigned char FlagD2;
    unsigned char FlagD3;                              // +0xD3  contains_fog_zone
    unsigned ZeroD4;
    float FloatD8;
    Exclusion::AsteroidExclusionZoneMgr* ExclusionMgr; // +0xDC
    unsigned char ShapeDbE0[0x18];                     // +0xE0  the field's ShapeDB (FShapeDB in place)
    unsigned char FlagF8;                              // +0xF8  [Band]
    unsigned char PadF9[3];
    unsigned char BandBlob[0x84];                      // +0xFC  FUN_0054c580 / Band
    unsigned char Flag180;
    unsigned char Pad181[3];
    unsigned char VecAlloc184;
    unsigned char Pad185[3];
    unsigned char** BandBegin;                         // +0x188  ExclusionBand* vector
    unsigned char** BandEnd;                           // +0x18C
    unsigned char** BandCap;                           // +0x190
    unsigned BandVertSum;                              // +0x194
    unsigned char Flag198;                             // +0x198  [AsteroidBillboards]
    unsigned char Pad199[3];
    FAsteroidBillboardBlock Billboard;                 // +0x19C
    unsigned char Flag21C;
    unsigned char Pad21D[3];
    unsigned char VecAlloc220;
    unsigned char Pad221[3];
    void** DynBegin;                                   // +0x224
    void** DynEnd;
    void** DynCap;
    float Scale230;
    unsigned char Flag234;
    unsigned char Pad235[3];
};
#pragma pack(pop)

static_assert(sizeof(FCliAsteroid) == 0x238, "FCliAsteroid must match retail operator_new(0x238)");
static_assert(offsetof(FCliAsteroid, CmnField) == 0x04, "CmnField +0x04");
static_assert(offsetof(FCliAsteroid, Zone) == 0x1C, "Zone +0x1C");
static_assert(offsetof(FCliAsteroid, CubeSize) == 0x20, "CubeSize +0x20");
static_assert(offsetof(FCliAsteroid, FillDistAt24) == 0x24, "FillDistAt24 +0x24");
static_assert(offsetof(FCliAsteroid, EmptyCubeFreq) == 0x28, "EmptyCubeFreq +0x28");
static_assert(offsetof(FCliAsteroid, Flag2C) == 0x2C, "Flag2C +0x2C");
static_assert(offsetof(FCliAsteroid, MeshDesc) == 0x30, "MeshDesc +0x30");
static_assert(offsetof(FCliAsteroid, Material) == 0xA0, "Material +0xA0");
static_assert(offsetof(FCliAsteroid, MeshVerts) == 0xA4, "MeshVerts +0xA4");
static_assert(offsetof(FCliAsteroid, MeshIndices) == 0xA8, "MeshIndices +0xA8");
static_assert(offsetof(FCliAsteroid, MeshIndexCount) == 0xAC, "MeshIndexCount +0xAC");
static_assert(offsetof(FCliAsteroid, TintB4) == 0xB4, "TintB4 +0xB4");
static_assert(offsetof(FCliAsteroid, ScaleCC) == 0xCC, "ScaleCC +0xCC");
static_assert(offsetof(FCliAsteroid, FlagF8) == 0xF8, "FlagF8 +0xF8");
static_assert(offsetof(FCliAsteroid, BandBlob) == 0xFC, "BandBlob +0xFC");
static_assert(offsetof(FCliAsteroid, Flag180) == 0x180, "Flag180 +0x180");
static_assert(offsetof(FCliAsteroid, BandBegin) == 0x188, "BandBegin +0x188");
static_assert(offsetof(FCliAsteroid, Flag198) == 0x198, "Flag198 +0x198");
static_assert(offsetof(FCliAsteroid, Billboard) == 0x19C, "Billboard +0x19C");
static_assert(offsetof(FCliAsteroid, Billboard.StartDist) == 0x1A4, "StartDist +0x1A4");
static_assert(offsetof(FCliAsteroid, Billboard.Count) == 0x1AC, "Count +0x1AC");
static_assert(offsetof(FCliAsteroid, Billboard.ColorShift0) == 0x200, "ColorShift +0x200");
static_assert(offsetof(FCliAsteroid, Flag21C) == 0x21C, "Flag21C +0x21C");
static_assert(offsetof(FCliAsteroid, DynBegin) == 0x224, "DynBegin +0x224");
static_assert(offsetof(FCliAsteroid, Scale230) == 0x230, "Scale230 +0x230");
static_assert(offsetof(FCliAsteroid, Flag234) == 0x234, "Flag234 +0x234");

class FCliDynamicAsteroidGameobj; // cli_asteroid_dynamic.h
struct IMaterial;                 // render/IMaterial.h

// The dynamic asteroid mesh copy FUN_00525800 builds (operator new(0x10), zeroed): the
// IRenderableMesh vertices (stride 0x2c, FVF 0x252) and ushort indices.
struct FCliDynamicAsteroidMesh
{
    unsigned char* Verts;      // +0x00  operator new(VertCount * 0x2c); IRenderableMesh slot 4
    int VertCount;             // +0x04  IRenderableMesh slot 3
    unsigned short* Indices;   // +0x08  operator new(IndexCount * 2); IRenderableMesh slot 6
    int IndexCount;            // +0x0c  IRenderableMesh slot 5
};
static_assert(sizeof(FCliDynamicAsteroidMesh) == 0x10, "dynamic asteroid mesh block 0x10");

// Client DynamicAsteroids manager — operator_new(0xE8), ctor FUN_005256a0.
// Distinct from Archetype::DynamicAsteroid (0x5C Root) and CDynamicAsteroid (CObject).
// Stored in FCliAsteroid DynBegin/DynEnd vector (+0x224). Its functions live in
// cli_asteroid_dynamic.{h,cpp}.
#pragma pack(push, 1)
struct FCliDynamicAsteroid
{
    Universe::IZone* Zone;                       // +0x00
    CmnAsteroid::CAsteroidField* CmnField;       // +0x04
    Archetype::DynamicAsteroid* Arch;            // +0x08
    FCliDynamicAsteroidGameobj** SlotPtrs;       // +0x0C  SlotCount gameobjs (null = free)
    int CountIni;                                // +0x10  "count" (default 10)
    int SlotCount;                               // +0x14  ftol(CountIni * DetailScale + 0.5)
    float MaxVelocity;                           // +0x18  default 50
    float MaxAngularVelocity;                    // +0x1C  default 5
    float PlacementRadius;                       // +0x20  default 150
    float PlacementRadiusInner;                  // +0x24  default 75 / radius*0.7
    float CameraPos[3];                          // +0x28  FUN_005261b0: the camera's position
    unsigned char bSpawnAllowed;                 // +0x34  FUN_00525c80 (zone, tradelane, exclusion)
    unsigned char bAnyVisible;                   // +0x35  FUN_00525c80: some slot above alpha 0.005
    unsigned char Pad36[2];
    FRpVertexSrcDesc MeshDesc;                   // +0x38  FUN_0051feb0 (same fill as field)
    IMaterial* Material;                         // +0xA8  IMesh slot 14; released (vt+8)
    unsigned PrimitiveType;                      // +0xAC  IMesh slot 13 out
    FCliDynamicAsteroidMesh* Mesh;               // +0xB0  FUN_00525800
    unsigned MeshVertTotal;                      // +0xB4
    unsigned MeshIndexTotal;                     // +0xB8
    float TintedDiffuse0;                        // +0xBC
    float TintedDiffuse1;                        // +0xC0
    float TintedDiffuse2;                        // +0xC4
    float TintedAmbient0;                        // +0xC8
    float TintedAmbient1;                        // +0xCC
    float TintedAmbient2;                        // +0xD0
    float ColorShift0;                           // +0xD4
    float ColorShift1;                           // +0xD8
    float ColorShift2;                           // +0xDC
    float DetailScale;                           // +0xE0
    float InvDetailScale;                        // +0xE4
};
#pragma pack(pop)

static_assert(sizeof(FCliDynamicAsteroid) == 0xE8, "FCliDynamicAsteroid must be 0xE8");
static_assert(offsetof(FCliDynamicAsteroid, CmnField) == 0x04, "Dyn CmnField +0x04");
static_assert(offsetof(FCliDynamicAsteroid, Arch) == 0x08, "Dyn Arch +0x08");
static_assert(offsetof(FCliDynamicAsteroid, SlotPtrs) == 0x0C, "Dyn SlotPtrs +0x0C");
static_assert(offsetof(FCliDynamicAsteroid, CountIni) == 0x10, "Dyn CountIni +0x10");
static_assert(offsetof(FCliDynamicAsteroid, SlotCount) == 0x14, "Dyn SlotCount +0x14");
static_assert(offsetof(FCliDynamicAsteroid, MaxVelocity) == 0x18, "Dyn MaxVelocity +0x18");
static_assert(offsetof(FCliDynamicAsteroid, PlacementRadius) == 0x20, "Dyn PlacementRadius +0x20");
static_assert(offsetof(FCliDynamicAsteroid, CameraPos) == 0x28, "Dyn CameraPos +0x28");
static_assert(offsetof(FCliDynamicAsteroid, bSpawnAllowed) == 0x34, "Dyn +0x34");
static_assert(offsetof(FCliDynamicAsteroid, bAnyVisible) == 0x35, "Dyn +0x35");
static_assert(offsetof(FCliDynamicAsteroid, MeshDesc) == 0x38, "Dyn MeshDesc +0x38");
static_assert(offsetof(FCliDynamicAsteroid, Material) == 0xA8, "Dyn Material +0xA8");
static_assert(offsetof(FCliDynamicAsteroid, PrimitiveType) == 0xAC, "Dyn PrimitiveType +0xAC");
static_assert(offsetof(FCliDynamicAsteroid, Mesh) == 0xB0, "Dyn Mesh +0xB0");
static_assert(offsetof(FCliDynamicAsteroid, MeshVertTotal) == 0xB4, "Dyn +0xB4");
static_assert(offsetof(FCliDynamicAsteroid, MeshIndexTotal) == 0xB8, "Dyn +0xB8");
static_assert(offsetof(FCliDynamicAsteroid, TintedDiffuse0) == 0xBC, "Dyn TintedDiffuse +0xBC");
static_assert(offsetof(FCliDynamicAsteroid, ColorShift0) == 0xD4, "Dyn ColorShift +0xD4");
static_assert(offsetof(FCliDynamicAsteroid, DetailScale) == 0xE0, "Dyn DetailScale +0xE0");
static_assert(offsetof(FCliDynamicAsteroid, InvDetailScale) == 0xE4, "Dyn InvDetailScale +0xE4");

FCliAsteroid* CliAsteroidConstruct(FCliAsteroid* This);
// FUN_0051ff50(file, zone) — caller: the [Asteroids] arm of the system INI load FUN_00553740.
bool CliAsteroidCreateAndLoad(char const* FieldIniRel, char const* ZoneNickname);
// FUN_00520130(cam, dt) — StarSys tick: with ASTEROIDS on, FUN_00522070 (CliAsteroidUpdate)
// on every field. Camera is the FSpaceCamera FUN_0040f130 hands out.
void CliAsteroidUpdateFields(void* Camera, float Dt);
// FUN_005200b0 — every registered field destroyed (FUN_005205d0 + free), then the list
// emptied. Caller: the StarSys system unload (0x00554b7e).
void CliAsteroidDestroyAll();
// FUN_005205d0 (thiscall) — the field destructor (retail's only caller is FUN_005200b0).
void CliAsteroidDestruct(FCliAsteroid* This);
// FUN_00521fa0 (cdecl) — each of the three floats above or equal to 1.0 (or unordered) becomes
// 1.0 (FCOMP 1.0, TEST AH,5 / JP). Callers 0x00521262 / 0x00521268 (the dynamic asteroid
// tints in FUN_00520850) and 0x0054e8f1 (the billboard centre colour).
void CliAsteroidClampColorToOne(float* Rgb);
// FUN_00522000 (cdecl) — Out = A * B per component (z, y, x order). Caller 0x00521244.
float* CliAsteroidMulColor(float* Out, float const* A, float const* B);
// FUN_0051feb0 (cdecl) — the vertex source descriptor over stride-0x2c FVF 0x252 vertices.
// Callers: the field mesh merge (0x00521a8b) and the dynamic asteroid parse (0x00525c54).
void CliAsteroidFillVertexSource(FRpVertexSrcDesc* Desc, void* Verts, unsigned short VertCount);
// DAT_006753d8 — CliAsteroid.cpp's 3DMathEngine, acquired (FUN_005b73e0) on first use.
struct I3DMathEngine;
I3DMathEngine* CliAsteroidMathEngine();
// LAB_005267f0 — Asteroids perf option (IDS 0x57c).
void CliAsteroidApplyGlobalDetail(float Detail);
// FUN_00520850 (thiscall, RET 4). Resolves zones against FUN_00554e90's system itself.
void CliAsteroidLoadFieldIni(FCliAsteroid* This, char const* FieldIniRel);
unsigned CliAsteroidRegistryCount();

// FUN_00524730 — BandBlob[0x16] (+0x154) + BandVertSum (+0x194).
int CliAsteroidPieceCount(FCliAsteroid* Field);
// FUN_00524740 / FUN_0054d920 — piece world position; returns OutXYZ (zeroed, with an FDUMP
// warning, for a piece no band holds).
float* CliAsteroidGetPiecePos(FCliAsteroid* Field, int PieceIndex, float* OutXYZ);
// FUN_005247f0 / FUN_0054d950 — piece radius.
float CliAsteroidGetPieceRadius(FCliAsteroid* Field, int PieceIndex);

typedef void (*FCliAsteroidVisitFn)(FCliAsteroid* Field, void* Ctx);
// Walk DAT_006753c4 registry (FUN_005201d0 list).
void CliAsteroidForEach(FCliAsteroidVisitFn Fn, void* Ctx);

struct FSpaceCamera;

// FUN_00522700 (thiscall on the field, RET 8) — light the field around the point Scale along
// the camera's view axis (the dynamic asteroid draw FUN_005261b0 calls it with 100.0f).
void CliAsteroidLightAround(FCliAsteroid* Field, FSpaceCamera* Cam, float Scale);

// FUN_00522070 (thiscall on the field, RET 8) -- the per-frame update: the nebula lookup once,
// the exclusion re-sort, the CmnAsteroid change events into the cube map (MedAsteroid
// gameobjs), empty cubes dropped, the dynamic asteroid managers, then the band fades.
// Caller: FUN_00520130 (0x0052015b).
void CliAsteroidUpdate(FCliAsteroid* This, FSpaceCamera* Cam, float Dt);

// FUN_00520170 — per-frame walk of DAT_006753c4, gated on ASTEROIDS (0x21).
// FUN_00522a60 Pass2: cube envelope + selection filters; GPU draw deferred (see cpp).
void CliAsteroidTickFields(FSpaceCamera* Cam);
// LAB_0051bdf0 → FUN_00522850 — FAR band piece draw from deferred node.
void CliAsteroidDrawFarPiece(FCliAsteroid* Field, FSpaceCamera* Cam, int PieceIndex);
