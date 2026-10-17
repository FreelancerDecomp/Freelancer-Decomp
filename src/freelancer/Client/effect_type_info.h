// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// EffectTypes.cpp — PTR_s_EFT_NONE_00612a68 (stride 0x28), FUN_004fd500/560/5a0.
// EffectLOD screen-size table — DAT_00667d90 (FUN_0042dae0 / LAB_0042dac0).

constexpr int kEffectTypeCount = 0x2f; // 0..0x2e; 0x2f = unknown
constexpr int kEffectLodTypeCount = 7; // 0=none .. 6=small_distant (names FUN_0042dae0 matches)
// DAT_00667d90 holds 8 rows: the static initializer LAB_0042dac0 seeds 8 x (-1,-1,-1); only
// rows 1..6 are ever filled by FUN_0042dae0.
constexpr int kEffectLodTableRows = 8;

struct FEffectTypeInfo
{
    char const* Nickname;     // +0x00  "EFT_*"
    float Priority;           // +0x04
    float GenericPriority;    // +0x08
    int LodType;              // +0x0C  index into EFT_LOD_* / EffectLOD row
    float Radius;             // +0x10
    unsigned char VisibilityOffscreen; // +0x14  EXIST_OFFSCREEN
    unsigned char UpdateOffscreen;     // +0x15  UPDATE_OFFSCREEN
    unsigned char Pad16[2];
    float RunTime;            // +0x18  <0 → EffectArch flags bit0
    float Fps;                // +0x1C
    float PbubbleNear;        // +0x20
    float PbubbleFar;         // +0x24
};
static_assert(sizeof(FEffectTypeInfo) == 0x28, "EffectTypeInfo 0x28");

// One EffectLOD row: max_lod_screen_size / min_lod_screen_size / min_screen_size.
struct FEffectLodSizes
{
    float MaxLodScreenSize;
    float MinLodScreenSize;
    float MinScreenSize;
};

extern FEffectTypeInfo G_EffectTypeTable[kEffectTypeCount];
extern FEffectLodSizes G_EffectLodTable[kEffectLodTableRows];

// FUN_004fd500 — name → index; returns kEffectTypeCount (0x2f) if unknown.
int EffectTypeIndexFromName(char const* Nickname);

// FUN_004fd560 — index → row (0x00612a68 + Index * 0x28), no range test. It never returns null;
// the null tests at its call sites (0x0040e3b8, 0x004f81f3, 0x004f820b, 0x004f9629) are retail's.
FEffectTypeInfo* EffectTypeInfoByIndex(int Index);

// FUN_004fd570 -- the perf level (DAT_006129c8) is at or below the type's Priority. No range
// test on Index, as retail. Caller: the splash-hit one-shot FUN_0053a230 (0x0053a268).
bool EffectTypeWithinPerf(int Index);

// FUN_004fd5a0 — parse one [EffectType] section into G_EffectTypeTable.
bool LoadEffectTypeSection(class INI_Reader* Reader);

// FUN_0042dae0 — parse one [EffectLOD] section into G_EffectLodTable.
bool LoadEffectLodSection(class INI_Reader* Reader);

struct ICamera;

// FUN_0042da00 (__thiscall on the row, RET 0x10) — screen size = Radius / Distance * ZNear *
// XScaleOverNear (camera slots +0x20 / +0x2c); at or below min_screen_size -> *Out = 0, false;
// else the blend (size - min_lod) / (max_lod - min_lod) clamped to [0, 1], true. A distance
// below 1e-5 (double DAT_005c75f0) gives 1.0 without asking the camera.
bool EffectLodRowBlend(FEffectLodSizes const* Row, ICamera* Cam, float Distance, float Radius,
                       float* OutBlend);

// FUN_0042dcd0 (cdecl) — the type's lod_type 1..6 selects the G_EffectLodTable row for
// FUN_0042da00 with the type's radius; any other type (or no row) gives *Out = 1.0, true.
// Caller: the particle visibility test FUN_004f86c0 (0x004f897f) under ALCHEMY_LOD.
bool EffectTypeLodBlend(int TypeIndex, ICamera* Cam, float Distance, float* OutBlend);

