// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "cli_nebula.h"

#include "cli_projectile_fx.h" // CliEffectCompanionArmStop / CliEffectCompanionClear (+0xe4)
#include "client_mesh_load.h"
#include "common_csys.h"
#include "common_id.h"
#include "common_ini.h"
#include "common_misc.h"
#include "common_net.h"
#include "common_universe.h"
#include "effect_base.h"    // FEffectBase (the effect at Nebula +0xec)
#include "engine_light.h"
#ifdef FREELANCER_RP9
#include "nebula_volume.h" // port-only Direct3D 9 volumetric nebula gates
#endif
#include "pixel_format.h"   // PixelFormatFromD3D / PixelFormatGetDesc (FUN_005b7010 / 005b7030)
#include "space_camera.h"
#include "star_sys.h"
#include "texture_panels.h"
#include "texture_shapes.h"
#include "win3d_immediate.h"
#include "win3d_render.h"

#include "DALib.h"
#include "engine/engbase.h"
#include "render/IRenderPipeline.h"
#include "render/rendcomp.h"
#include "render/shading.h"
#include "x86math.h"

#include <list>
#include <map>
#include <math.h>
#include <new>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

I3DMathEngine* G_CloudsMath = nullptr; // DAT_00679860 (cli_nebula.h)

// The zone_shell mesh an [Exclusion Zones] section loads (0x00505c78: operator new(0x38)).
// +0x00 the ObjectHandle (FUN_00420d40); 0x00505c9b sets the seven floats +0x10..+0x28 to
// 1.0f; +0x10 is handed to the engine as the instance's user data (0x00505d1f), and the
// exclusion-shell draw copies the tint into +0x10..+0x18 and the faded alpha into +0x20
// (0x00508ff6..0x00509017). +0x24 shell_scalar (raised to 1), +0x28 max_alpha (capped at 1),
// +0x2c..+0x34 exclusion_tint (each capped at 1) -- the tint is never initialised when the
// key is absent, exactly as retail leaves it.
struct FNebulaZoneShell
{
    FMeshHolder Handle;   // +0x00
    float UserData[5];    // +0x10 colour r, g, b; +0x1c; +0x20 alpha
    float ShellScalar;    // +0x24
    float MaxAlpha;       // +0x28
    float Tint[3];        // +0x2c
};
static_assert(sizeof(FNebulaZoneShell) == 0x38, "zone_shell mesh is 0x38");
static_assert(offsetof(FNebulaZoneShell, ShellScalar) == 0x24, "zone_shell +0x24");
static_assert(offsetof(FNebulaZoneShell, Tint) == 0x2c, "zone_shell +0x2c");


namespace
{
    // DAT_006132dc / +0xe0/+0xe4/+0xe8 — PE .data defaults (exe RVA 0x2132dc…).
    float G_NebulaDetail = 1.0f;
    float G_NebulaDetailEase = 0.707f;   // DAT_006132e0
    float G_NebulaDetailMix = 0.35f;     // DAT_006132e4
    float G_NebulaDetailFog = 0.5f;      // DAT_006132e8

    float BitsToFloat(unsigned Bits)
    {
        float V = 0.0f;
        memcpy(&V, &Bits, sizeof(float));
        return V;
    }

    // The "color" keys of FUN_00504e60 and FUN_00550240: three values, each * 1/255 (float
    // 0x005c7f68) and stored as float, into Out[0..2].
    void ReadColor255(INI_Reader& Reader, float* Out)
    {
        float const R = Reader.get_value_float(0);
        float const G = Reader.get_value_float(1);
        float const Bl = Reader.get_value_float(2);
        Out[0] = R * (1.0f / 255.0f);
        Out[1] = G * (1.0f / 255.0f);
        Out[2] = Bl * (1.0f / 255.0f);
    }

    I3DMathEngine* AcquireNebulaMath()
    {
        // DAT_00675000 — lazy 3DMathEngine (FUN_005b73e0).
        static I3DMathEngine* G_NebulaMath = nullptr;
        if (G_NebulaMath == nullptr)
            G_NebulaMath = Acquire3DMathEngine();
        return G_NebulaMath;
    }

    // Nebula +0x38: std::map<unsigned (CreateID of the mesh name), zone_shell*>. Retail's VC6
    // map (FUN_004e7f60 ctor, lower_bound FUN_0052c7a0 / FUN_0041bd10, insert FUN_004b68b0,
    // erase FUN_00509d60); the port keeps a real std::map in the same bytes.
    typedef std::map<unsigned, FNebulaZoneShell*> FNebulaZoneShellMap;
    static_assert(sizeof(FNebulaZoneShellMap) < 0x14,
                  "std::map must fit in the retail tree's 0x14 bytes");

    FNebulaZoneShellMap& NebulaZoneShells(FNebulaEffect* Nebula)
    {
        return Nebula->ZoneShells;
    }

    // The two-step find every reader inlines (0x00505c38..0x00505c65, 0x005071f6..0x0050721b):
    // lower_bound, then end unless !(Key < node key).
    FNebulaZoneShellMap::iterator NebulaFindZoneShell(FNebulaZoneShellMap& Map, unsigned Key)
    {
        FNebulaZoneShellMap::iterator It = Map.lower_bound(Key);
        if (It == Map.end() || Key < It->first)
            return Map.end();
        return It;
    }

    // The nebula's ShapeDB lives in the retail bytes at +0x04 (FUN_00509c30 builds it there).
    FShapeDB* NebulaShapes(FNebulaEffect* Nebula)
    {
        return &Nebula->Shapes;
    }

    // FUN_00506eb0 (thiscall) -- the zone_shell's destructor: FUN_00420f70 (instance and both
    // material slots released), then ~ObjectHandle FUN_00420d10 (vptr reset only). The caller
    // frees the memory.
    void NebulaZoneShellDestroy(FNebulaZoneShell* Shell)
    {
        ClientMeshHolderRelease(&Shell->Handle);
    }

    // DAT_0067505c/60/64 is not a nebula listener list: it is the TexturePanels instance list
    // (FUN_0050bfa0 links into it, FUN_00509f20 hands it to the StarSys tick). Its walks
    // FUN_00509f30 / FUN_00509f60 live with the panels in texture_panels.cpp.

    int ClampByte255(int V)
    {
        if (V < 0)
            return 0;
        if (V > 255)
            return 255;
        return V;
    }

} // namespace

// FUN_00422440 (cdecl, 3 args) -- the one port of it; space_scene_obj_draw.cpp had a second
// copy, now removed. Each colour channel: FLD / FMUL 255.0f (DAT_005c75d8) / ftol, clamped
// to [0,0xff] (00422449..004224c6). The float*float product is exact in double, as it is in
// the x87 register. No null tests: retail dereferences Rgb3 and DALib::Pipeline directly.
void FogApplyColorAndRange(float const* Rgb3, float FogStart, float FogEnd)
{
    int R = static_cast<int>(static_cast<double>(Rgb3[0]) * 255.0);
    if (R < 0)
        R = 0;
    else if (R > 0xff)
        R = 0xff;
    int G = static_cast<int>(static_cast<double>(Rgb3[1]) * 255.0);
    if (G < 0)
        G = 0;
    else if (G > 0xff)
        G = 0xff;
    int B = static_cast<int>(static_cast<double>(Rgb3[2]) * 255.0);
    if (B < 0)
        B = 0;
    else if (B > 0xff)
        B = 0xff;

    // 004224cb..004224ed: Pipeline vt+0x20 queries device ability 0xd (W-fog),
    // preset to 0. GetPipelineState is vt+0x18 and reads an unrelated setting.
    unsigned long WFogAbility = 0;
    DALib::Pipeline->QueryDeviceAbility(0xd, &WFogAbility);
    if (WFogAbility == 0)
    {
        DALib::Pipeline->SetRenderState(D3DRS_FOGTABLEMODE, 0); // D3DRS_FOGTABLEMODE
        DALib::Pipeline->SetRenderState(D3DRS_FOGVERTEXMODE, 3); // D3DRS_FOGVERTEXMODE
    }
    else
    {
        DALib::Pipeline->SetRenderState(D3DRS_FOGTABLEMODE, 3);
        DALib::Pipeline->SetRenderState(D3DRS_FOGVERTEXMODE, 0);
        DALib::Pipeline->SetRenderState(D3DRS_RANGEFOGENABLE, 0); // D3DRS_RANGEFOGENABLE
    }

    // 00422556..00422579: ((R|0xffffff00)<<8|G)<<8|B -> D3DRS_FOGCOLOR.
    unsigned Packed = static_cast<unsigned>(R) | 0xffffff00u;
    Packed = (Packed << 8) | static_cast<unsigned>(G);
    Packed = (Packed << 8) | static_cast<unsigned>(B);
    DALib::Pipeline->SetRenderState(D3DRS_FOGCOLOR, static_cast<int>(Packed));

    // 0042257f..004225c8: the raw float bits of arg 2 / arg 3, then D3DRS_FOGENABLE = 1.
    int FogStartBits = 0;
    int FogEndBits = 0;
    memcpy(&FogStartBits, &FogStart, sizeof(float));
    memcpy(&FogEndBits, &FogEnd, sizeof(float));
    DALib::Pipeline->SetRenderState(D3DRS_FOGSTART, FogStartBits); // D3DRS_FOGSTART
    DALib::Pipeline->SetRenderState(D3DRS_FOGEND, FogEndBits);   // D3DRS_FOGEND
    DALib::Pipeline->SetRenderState(D3DRS_FOGENABLE, 1);
}

namespace
{

    // DALib::MaterialBatcher slot +0x20. Retail reads the import and calls through it with no
    // null test at every site (e.g. 0x00508e67, 0x00508ed1).
    void SetMaterialBatchBinOrder(unsigned long Index, long Key)
    {
        DALib::MaterialBatcher->SetBinOrder(Index, Key);
    }

    // FUN_005097c0 — ambient GameLight at +0x270: SetPosition, flicker accum, colour/enable.
    // Retail FUN_005097c0 is thiscall (RET 4) with the camera position as the stack argument;
    // caller 0x00507309 (NebulaUpdate). No test of `this` or of the position pointer.
    void NebulaAmbientTick(FNebulaEffect* Nebula, float const* CamPos3)
    {
        unsigned char* const B = reinterpret_cast<unsigned char*>(Nebula);
        // 0x005097c6..0x005097cf: the light at +0x270 is the one pointer retail tests.
        FGameLight* const GLight = *reinterpret_cast<FGameLight**>(B + 0x270);
        if (GLight == nullptr)
            return;
#ifdef FREELANCER_RP9
        // Port-only: while the Direct3D 9 volumetric nebula's lightning strikes drive this light
        // (direction from the strike, its flicker; nebula_volume.cpp), the random flash below
        // stays out. Never true on RP8 or with the volume off.
        if (NebulaVolume::DrivesNebulaLight(Nebula))
            return;
#endif
        FEngineLight* const Light = (FEngineLight*)GLight;
        // 0x005097d5..0x005097e6: +0x278 FCOMP 0.0f, TEST AH,0x41 / JNP -- return when <= 0
        // (a NaN period goes on).
        float const Period = *reinterpret_cast<float*>(B + 0x278);
        if (Period <= 0.0f)
            return;

        // 0x005097f1: FUN_00410ef0 on the light.
        EngineLightSetPosition(Light, CamPos3);

        // 0x005097f6..0x0050980d: the old +0x27c vs 0.0f; +0x27c = dt (+0x1c) + old, FST float,
        // with the unrounded sum kept on the FPU.
        float const Dt = *reinterpret_cast<float*>(B + 0x1c);
        float const AccPrev = *reinterpret_cast<float*>(B + 0x27c);
        double const Acc = static_cast<double>(Dt) + AccPrev;
        *reinterpret_cast<float*>(B + 0x27c) = static_cast<float>(Acc);

        // 0x00509813 TEST AH,0x41 / JNZ: only an old value strictly above 0 falls through;
        // LAB_00509921 then returns unless the new sum is strictly above 0.
        if (!(AccPrev > 0.0f))
        {
            if (!(Acc > 0.0))
                return;
        }
        // 0x0050981c..0x00509827: FCOMP [+0x278], TEST AH,0x41 / JNZ -- only a sum strictly
        // above the period re-arms; anything else goes to the colour path (LAB_00509932).
        else if (Acc > Period)
        {
            // 0x0050982d..0x00509855: +0x27c = float((rand * 3.0517578125e-05f * double(0.7f)
            // - 1.5) * +0x274).
            int const RArm = rand();
            *reinterpret_cast<float*>(B + 0x27c) = static_cast<float>(
                (static_cast<double>(RArm) * 3.0517578125e-05f * static_cast<double>(0.7f) - 1.5) *
                *reinterpret_cast<float*>(B + 0x274));

            // 0x0050985b..0x005098b8: three rands, each r * 3.0517578125e-05f * 2 - 1.0 (exact).
            double const First = static_cast<double>(rand()) * 3.0517578125e-05f;
            double const Second = static_cast<double>(rand()) * 3.0517578125e-05f;
            double const Third = static_cast<double>(rand()) * 3.0517578125e-05f;
            double const Z = (Third + Third) - 1.0;
            double const Y = (Second + Second) - 1.0;
            double const X = (First + First) - 1.0;
            // 0x005098be..0x005098da: Inv = float(1.0f / sqrt(X^2 + Y^2 + Z^2)); no zero test.
            float const Inv = static_cast<float>(1.0f / sqrt(X * X + Y * Y + Z * Z));
            // 0x005098de..0x005098f8: the out vector is {Z, Y, X} * Inv -- the THIRD rand lands
            // in [0], the first in [2].
            float Dir[3];
            Dir[0] = static_cast<float>(Z * Inv);
            Dir[1] = static_cast<float>(Y * Inv);
            Dir[2] = static_cast<float>(static_cast<float>(X) * static_cast<double>(Inv));
            // 0x005098fc: FUN_00411470 on the light.
            EngineLightSetDirection(Light, Dir);
            // 0x00509901..0x00509916: lit (+0xb4 bit 3) -> GameLight vt+8 (disable).
            if ((Light->Flags & 0x08) != 0)
                GameLightVirtualDisable(GLight);
            return;
        }

        // LAB_00509932: intensity = +0x28c, plus +0x290 (stored as float) when +0x26c is set.
        float Intensity = *reinterpret_cast<float*>(B + 0x28c);
        if (B[0x26c] != 0)
            Intensity = Intensity + *reinterpret_cast<float*>(B + 0x290);

        // 0x00509954..0x00509980: U0 = float(rand * c); Flick = rand * U0 * Intensity * c, kept
        // on the FPU.
        float const U0 = static_cast<float>(static_cast<double>(rand()) * 3.0517578125e-05f);
        int const RFlick = rand();
        double const Flick =
            static_cast<double>(RFlick) * U0 * Intensity * 3.0517578125e-05f;
        // 0x00509986..0x0050999e: red and green are stored as float, blue stays on the FPU.
        float Cr = static_cast<float>(Flick * *reinterpret_cast<float*>(B + 0x280));
        float Cg = static_cast<float>(Flick * *reinterpret_cast<float*>(B + 0x284));
        double Cb = Flick * *reinterpret_cast<float*>(B + 0x288);
        // 0x005099a4..0x005099e5: FCOMP / FCOM 1.0f, TEST AH,5 / JNP -- keep only a value below
        // 1.0; anything else (>=, NaN) becomes 1.0.
        if (!(Cr < 1.0f))
            Cr = 1.0f;
        if (!(Cg < 1.0f))
            Cg = 1.0f;
        if (!(Cb < 1.0))
            Cb = 1.0;

        // 0x005099eb..0x00509a6c: red and green * 255.0f stored as float, then truncated
        // (ftol); blue * 255.0f is truncated from the FPU. Each is clamped to 0..255.
        float const Red255 = Cr * 255.0f;
        float const Green255 = Cg * 255.0f;
        int const Blue = ClampByte255(static_cast<int>(Cb * 255.0f));
        int const Green = ClampByte255(static_cast<int>(Green255));
        int const Red = ClampByte255(static_cast<int>(Red255));

        // 0x00509a71..0x00509a91: {R, G, B, 0xff} built in the argument slot, then FUN_004114f0.
        unsigned char Rgba[4];
        Rgba[0] = static_cast<unsigned char>(Red);
        Rgba[1] = static_cast<unsigned char>(Green);
        Rgba[2] = static_cast<unsigned char>(Blue);
        Rgba[3] = 0xff;
        EngineLightSetColorBytes(Light, Rgba);
        // 0x00509a96..0x00509aa8: not lit -> GameLight vt+4 (enable).
        if ((Light->Flags & 0x08) == 0)
            GameLightEnable(Light);
    }

    // FUN_00550180
    float const kCloudsRandScale = 3.0517578125e-05f; // DAT_005caeec = 1/32768

    void LocalNebulaCloudsConstruct(FNebulaClouds* Clouds)
    {
        // FUN_00550180 (thiscall): only these fields; the rest of the 0x12c block keeps what
        // operator new left there until the [Clouds] parse. The zeroed local vector at
        // [ESP+4..0xc] is an inlined empty ctor that nothing reads.
        unsigned char* const R = Clouds->Raw;
        *reinterpret_cast<float*>(R + 0x8) = 1.0f;
        *reinterpret_cast<float*>(R + 0xac) = 1.0f;
        *reinterpret_cast<int*>(R + 0xf4) = 0;
        *reinterpret_cast<int*>(R + 0xf8) = 0;
        *reinterpret_cast<int*>(R + 0xfc) = 0;
        *reinterpret_cast<int*>(R + 0x0) = 0;
        *reinterpret_cast<int*>(R + 0x4) = 0;
        *reinterpret_cast<int*>(R + 0xc) = 0;
        *reinterpret_cast<int*>(R + 0xc4) = 0;
        R[0xec] = 0;
        *reinterpret_cast<int*>(R + 0x128) = 0;
    }

    // The detail pair both FUN_00552530 and FUN_005524a0 compute, on the FPU: a = 1.0f - d;
    // +0x08 = float(0.5f * a + d); +0xac = float((1.0f - e) * 0.707f + e), e = 1.0f - a * a
    // (0.707f is DAT_005e0e10).
    void CloudsDetailFactors(FNebulaClouds* Clouds, float Detail)
    {
        double const A = 1.0f - static_cast<double>(Detail);
        *reinterpret_cast<float*>(Clouds->Raw + 0x8) = static_cast<float>(0.5f * A + Detail);
        double const Ease = 1.0f - A * A;
        *reinterpret_cast<float*>(Clouds->Raw + 0xac) =
            static_cast<float>((1.0f - Ease) * 0.707f + Ease);
    }

    // FUN_00552530 (thiscall, RET 4). No test of `this`.
    void LocalNebulaCloudsSetDetail(FNebulaClouds* Clouds, float Detail)
    {
        CloudsDetailFactors(Clouds, Detail);
    }

    // FUN_00550cf0 (thiscall): free (FUN_005b7e1d) every non-null puff of the table (+0x00,
    // count +0x0c re-read each step) and null its slot, then free the table itself and null
    // +0x00. No test of the table pointer; +0x04 / +0xec are left alone.
    void LocalNebulaCloudsFreePuffTable(FNebulaClouds* Clouds)
    {
        unsigned char* const R = Clouds->Raw;
        for (int I = 0; I < *reinterpret_cast<int*>(R + 0xc); ++I)
        {
            void** const Table = *reinterpret_cast<void***>(R);
            if (Table[I] != nullptr)
                operator delete(Table[I]);
            (*reinterpret_cast<void***>(R))[I] = nullptr;
        }
        operator delete(*reinterpret_cast<void**>(R));
        *reinterpret_cast<void**>(R) = nullptr;
    }

    // FUN_005501e0 (thiscall): with +0xec set, the FUN_00550cf0 body, then +0x04 = 0 and
    // +0xec = 0.
    void LocalNebulaCloudsReleaseBits(FNebulaClouds* Clouds)
    {
        if (Clouds->Raw[0xec] == 0)
            return;
        LocalNebulaCloudsFreePuffTable(Clouds);
        *reinterpret_cast<int*>(Clouds->Raw + 4) = 0;
        Clouds->Raw[0xec] = 0;
    }

    // FUN_00550040 (thiscall on the puff, RET 0x18): Sample, Scale, Min, Max, Weights, Count.
    void LocalNebulaCloudsFillPuff(unsigned char* Puff, Vector const& Sample, float Scale,
                                   Vector const& MinCorner, Vector const& MaxCorner,
                                   float const* Weights, int WeightCount)
    {
        // 0x00550047..0x0055005f: the position.
        *reinterpret_cast<float*>(Puff + 0x0) = Sample.x;
        *reinterpret_cast<float*>(Puff + 0x4) = Sample.y;
        *reinterpret_cast<float*>(Puff + 0x8) = Sample.z;

        // 0x00550062..0x0055008a: +0x18 = float((rand * c * 0.40000003576278687 (double
        // 0x005e0bd8) + double(0.8f) (0x005e0bd0)) * Scale).
        int const RSize = rand();
        *reinterpret_cast<float*>(Puff + 0x18) = static_cast<float>(
            (static_cast<double>(RSize) * kCloudsRandScale * 0.40000003576278687 +
             0.800000011920929) *
            Scale);

        // 0x0055008d..0x00550105: D = Max - Min (floats); T = rand * c on the FPU; x = float(
        // D.x * T + Min.x) in one go, y and z add Min to the float product.
        float const Dx = MaxCorner.x - MinCorner.x;
        float const Dy = MaxCorner.y - MinCorner.y;
        float const Dz = MaxCorner.z - MinCorner.z;
        int const RTint = rand();
        double const T = static_cast<double>(RTint) * kCloudsRandScale;
        float const Py = static_cast<float>(Dy * T);
        float const Pz = static_cast<float>(Dz * T);
        *reinterpret_cast<float*>(Puff + 0xc) = static_cast<float>(Dx * T + MinCorner.x);
        *reinterpret_cast<float*>(Puff + 0x10) = Py + MinCorner.y;
        *reinterpret_cast<float*>(Puff + 0x14) = Pz + MinCorner.z;

        // 0x00550108..0x0055011d: +0x1c = (rand * count) / 32768, truncated toward zero.
        int const RIndex = rand();
        *reinterpret_cast<int*>(Puff + 0x1c) = (RIndex * WeightCount) / 32768;

        // 0x00550120..0x0055015d: pick = float(rand * c); the weights accumulate on the FPU and
        // the first running sum strictly above the pick (TEST AH,5 / JNP) replaces +0x1c.
        int const RPick = rand();
        float const Pick = static_cast<float>(static_cast<double>(RPick) * kCloudsRandScale);
        double Running = 0.0f;
        for (int I = 0; I < WeightCount; ++I)
        {
            Running += Weights[I];
            if (Pick < Running)
            {
                *reinterpret_cast<int*>(Puff + 0x1c) = I;
                break;
            }
        }

        // 0x00550163..0x00550166: +0x54 = 0, +0x38 = -1.0f.
        *reinterpret_cast<int*>(Puff + 0x54) = 0;
        *reinterpret_cast<float*>(Puff + 0x38) = -1.0f;
    }

    // FUN_00550b60 (thiscall): rebuild the puff table while +0xec is set.
    void LocalNebulaCloudsRebuildBits(FNebulaClouds* Clouds)
    {
        unsigned char* const R = Clouds->Raw;
        if (R[0xec] == 0)
            return;

        // 0x00550b75..0x00550bb1: an existing table (the one pointer tested here) is freed as
        // in FUN_00550cf0.
        if (*reinterpret_cast<void**>(R) != nullptr)
            LocalNebulaCloudsFreePuffTable(Clouds);

        // 0x00550bb7..0x00550bd4: +0x0c = ftol(+0x04 * +0x08 + 0.5f); +0x00 = operator new
        // (count << 2), not tested.
        int const Count = static_cast<int>(
            static_cast<double>(*reinterpret_cast<int*>(R + 4)) * *reinterpret_cast<float*>(R + 8) +
            0.5f);
        *reinterpret_cast<int*>(R + 0xc) = Count;
        void** const Table =
            static_cast<void**>(operator new(static_cast<unsigned>(Count) << 2));
        *reinterpret_cast<void***>(R) = Table;

        // 0x00550be8..0x00550cdf: per puff (the count re-read every step).
        for (int I = 0; I < *reinterpret_cast<int*>(R + 0xc); ++I)
        {
            // 0x00550c00..0x00550c1a: a 0x58 block, +0x50 / +0x51 = 0 only on a non-null one.
            unsigned char* Puff = static_cast<unsigned char*>(operator new(0x58));
            if (Puff != nullptr)
            {
                Puff[0x50] = 0;
                Puff[0x51] = 0;
            }
            Table[I] = Puff;

            // 0x00550c1c..0x00550c54: Common random_inside_sphere(+0x100), then FUN_00550040
            // with float(+0xa8 * +0xac), the box +0x78 / +0x84, the weights +0x48 and +0x68.
            Vector const Sample = random_inside_sphere(*reinterpret_cast<float*>(R + 0x100));
            float const Scale = *reinterpret_cast<float*>(R + 0xa8) * *reinterpret_cast<float*>(R + 0xac);
            LocalNebulaCloudsFillPuff(Puff, Sample, Scale, *reinterpret_cast<Vector*>(R + 0x78),
                                      *reinterpret_cast<Vector*>(R + 0x84),
                                      reinterpret_cast<float*>(R + 0x48),
                                      *reinterpret_cast<int*>(R + 0x68));

            // 0x00550c59..0x00550c79: the drift, random_inside_sphere(+0xb0), at +0x44.
            Vector const Drift = random_inside_sphere(*reinterpret_cast<float*>(R + 0xb0));
            *reinterpret_cast<float*>(Puff + 0x44) = Drift.x;
            *reinterpret_cast<float*>(Puff + 0x48) = Drift.y;
            *reinterpret_cast<float*>(Puff + 0x4c) = Drift.z;

            // 0x00550c7c..0x00550cc1: +0xa0 FCOMP 0.0f, TEST AH,0x41 / JNZ -- <= 0 or unordered
            // gives +0x38 = -1.0f; otherwise float((double(-0.8f) (0x005e0df8) - rand * c *
            // double(0.7f) (0x005db0b8)) * +0x9c).
            if (!(*reinterpret_cast<float*>(R + 0xa0) > 0.0f))
            {
                *reinterpret_cast<float*>(Puff + 0x38) = -1.0f;
            }
            else
            {
                int const RPhase = rand();
                *reinterpret_cast<float*>(Puff + 0x38) = static_cast<float>(
                    (-0.800000011920929 -
                     static_cast<double>(RPhase) * kCloudsRandScale * 0.699999988079071) *
                    *reinterpret_cast<float*>(R + 0x9c));
            }
            // 0x00550cc8: +0x50 = 1.
            Puff[0x50] = 1;
        }
    }

    // FUN_005524a0 (thiscall, RET 4): the FUN_00550cf0 body (no +0xec test), the FUN_00552530
    // factors inline, then FUN_00550b60 (which tests +0xec itself). No test of `this`.
    void LocalNebulaCloudsApplyDetail(FNebulaClouds* Clouds, float Detail)
    {
        LocalNebulaCloudsFreePuffTable(Clouds);
        CloudsDetailFactors(Clouds, Detail);
        LocalNebulaCloudsRebuildBits(Clouds);
    }

    // DAT_006140b0 (.data, initialised 1, only reader 00550f51): puff sort direction.
    // Non-zero sorts the table by descending +0x54, zero by ascending.
    int G_CloudsSortDescending = 1;

    // DAT_00679860 (G_CloudsMath, cli_nebula.h) -- this TU's lazy 3DMathEngine (FUN_005b73e0),
    // shared with 0054cb06, 0055156e, 005519f3 and 00551b94.
    I3DMathEngine* AcquireCloudsMath()
    {
        if (G_CloudsMath == nullptr)
            G_CloudsMath = Acquire3DMathEngine();
        return G_CloudsMath;
    }

    // .rdata constants read by FUN_00550d40 (values from the PE).
    float const kCloudsCullSlack = 1.0199999809265137f;    // DAT_005e0e04
    float const kCloudsExclusionDepth = 0.8999999761581421f; // DAT_005e0e00
    double const kCloudsDriftSpan = 0.699999988079071;     // DAT_005db0b8
    double const kCloudsDriftBase = -0.800000011920929;    // DAT_005e0df8
    double const kCloudsTwoPi = 6.2831854820251465;        // DAT_005c97f8
    float const kCloudsDriftSpeed = 5.0f;                  // DAT_005d8464

    // FUN_00550d40 (__thiscall, RET 0x10): (camera, dt, visibility, exclusion manager).
    // Callers: the nebula update 005072e4 (visibility = nebula +0x294, manager = nebula +0x34)
    // and the TexturePanels update 0050b2b1 (visibility = panels +0x70, manager = null).
    // Puff (0x58): +0x00 position, +0x20 drift anchor, +0x2c drift direction, +0x38 drift
    // timer, +0x3c/+0x40 drift randoms, +0x44 velocity, +0x50 bActive, +0x51 bHidden,
    // +0x54 sort key.
    void LocalNebulaCloudsUpdate(FNebulaClouds* Clouds, void* Camera, float Dt, float Visibility,
                                 Exclusion::NebulaExclusionZoneMgr* ExclusionMgr)
    {
        unsigned char* const C = Clouds->Raw;
        // 00550d49: nothing to do until the puff table was built.
        if (C[0xec] == 0)
            return;

        // 00550d57..00550d89: budget = ftol(visibility / +0x108 * count), clamped to [0, count].
        int const PuffCount = *reinterpret_cast<int*>(C + 0xc);
        int Budget = static_cast<int>(static_cast<double>(Visibility) /
                                      static_cast<double>(*reinterpret_cast<float*>(C + 0x108)) *
                                      static_cast<double>(PuffCount));
        *reinterpret_cast<int*>(C + 0x10) = Budget;
        if (Budget < 0)
            *reinterpret_cast<int*>(C + 0x10) = 0;
        else if (Budget > PuffCount)
            *reinterpret_cast<int*>(C + 0x10) = PuffCount;

        // 00550d8c..00550ddb: fade. FCOMP +0x104 / TEST AH,0x41 / JNZ: only visibility above
        // +0x104 gives 1.0; then FCOMP 0.0 / TEST AH,0x41 / JP: an ordered visibility <= 0
        // gives 0.0; everything else (including unordered) divides.
        float const FullVisibility = *reinterpret_cast<float*>(C + 0x104);
        if (Visibility > FullVisibility)
            *reinterpret_cast<float*>(C + 0xe0) = 1.0f;
        else if (Visibility <= 0.0f)
            *reinterpret_cast<float*>(C + 0xe0) = 0.0f;
        else
            *reinterpret_cast<float*>(C + 0xe0) = Visibility / FullVisibility;

        // 00550de1..00550e24: +0xb8 = +0xbc + R (R = +0x100), the cull threshold R*R*1.02,
        // and the local vector (0, 0, -(+0xbc + R)).
        ICamera* const Cam = static_cast<ICamera*>(Camera);
        float const Radius = *reinterpret_cast<float*>(C + 0x100);
        float const Ahead = *reinterpret_cast<float*>(C + 0xbc) + Radius;
        *reinterpret_cast<float*>(C + 0xb8) = Ahead;
        float CullDistSq = static_cast<float>(static_cast<double>(Radius) * Radius *
                                              static_cast<double>(kCloudsCullSlack));
        FVector AheadLocal;
        AheadLocal.X = 0.0f;
        AheadLocal.Y = 0.0f;
        AheadLocal.Z = -Ahead;

        // 00550e2a..00550e6c: the cloud centre +0xf4 = camera transform (vt+0x10) applied to
        // that vector through 3DMathEngine vt+0x28 (TransformXform).
        FTransform const* const CamXf = reinterpret_cast<FTransform const*>(Cam->GetOrientation());
        FVector Centre;
        AcquireCloudsMath()->TransformXform(Centre, *CamXf, AheadLocal);
        *reinterpret_cast<float*>(C + 0xf4) = Centre.X;
        *reinterpret_cast<float*>(C + 0xf8) = Centre.Y;
        *reinterpret_cast<float*>(C + 0xfc) = Centre.Z;

        unsigned char** const Table = *reinterpret_cast<unsigned char***>(C);

        // 00550e6f..00550f16: pass 1 over every puff.
        int Active = 0;
        for (int I = 0; I < *reinterpret_cast<int*>(C + 0xc); ++I)
        {
            unsigned char* const Puff = Table[I];
            // 00550e80..00550e9b: FCOMP 0.0 / TEST AH,0x41 / JP -- an ordered fade <= 0
            // deactivates every puff.
            if (*reinterpret_cast<float*>(C + 0xe0) <= 0.0f)
            {
                Puff[0x50] = 0;
                Puff[0x51] = 1;
            }
            if (Puff[0x50] == 0)
                continue;
            // 00550ea8..00550f09: an active puff further than the cull radius from the centre
            // is deactivated (TEST AH,0x41 / JNZ keeps <= and unordered), the rest are counted.
            double const Dx = static_cast<double>(*reinterpret_cast<float*>(C + 0xf4)) -
                              *reinterpret_cast<float*>(Puff + 0x0);
            double const Dy = static_cast<double>(*reinterpret_cast<float*>(C + 0xf8)) -
                              *reinterpret_cast<float*>(Puff + 0x4);
            double const Dz = static_cast<double>(*reinterpret_cast<float*>(C + 0xfc)) -
                              *reinterpret_cast<float*>(Puff + 0x8);
            double const DistSq = Dz * Dz + Dy * Dy + Dx * Dx;
            if (DistSq > CullDistSq)
            {
                Puff[0x50] = 0;
                Puff[0x51] = 1;
            }
            else
                ++Active;
        }

        // 00550f1c..00550f2a: the same local now holds +0xc8 squared; the drift pass below
        // lowers it to the nearest anchor distance. Nothing reads the result.
        float NearestDistSq = static_cast<float>(
            static_cast<double>(*reinterpret_cast<float*>(C + 0xc8)) *
            *reinterpret_cast<float*>(C + 0xc8));

        // 00550f30..00550f8b: bubble sort of the table by the float at +0x54, repeated until a
        // pass makes no swap. DAT_006140b0 != 0: swap when left < right (TEST AH,0x5 / JP
        // skips >=, == and unordered); == 0: swap when left > right (TEST AH,0x41 / JNZ).
        for (;;)
        {
            if (*reinterpret_cast<int*>(C + 0xc) <= 1)
                break;
            int Swaps = 0;
            for (int J = 1; J < *reinterpret_cast<int*>(C + 0xc); ++J)
            {
                unsigned char* const Left = Table[J - 1];
                unsigned char* const Right = Table[J];
                float const L = *reinterpret_cast<float*>(Left + 0x54);
                float const R = *reinterpret_cast<float*>(Right + 0x54);
                bool const bSwap = G_CloudsSortDescending != 0 ? (L < R) : (L > R);
                if (bSwap)
                {
                    Table[J - 1] = Right;
                    ++Swaps;
                    Table[J] = Left;
                }
            }
            if (Swaps <= 0)
                break;
        }

        // 00550f8d..00551396: pass 2 -- respawn, drift, move.
        for (int I = 0; I < *reinterpret_cast<int*>(C + 0xc); ++I)
        {
            // 00550fa8..00550fbc: an inactive puff respawns while fewer than the budget are
            // active (signed JGE).
            if (Table[I][0x50] == 0 && Active < *reinterpret_cast<int*>(C + 0x10))
            {
                // 00550fc2..00551026: on the sphere of radius R around the centre.
                float const SpawnRadius = *reinterpret_cast<float*>(C + 0x100);
                Vector const OnSphere = random_on_unit_sphere();
                float const Sz = static_cast<float>(static_cast<double>(SpawnRadius) * OnSphere.z);
                float* const Pos = reinterpret_cast<float*>(Table[I]);
                Pos[0] = static_cast<float>(static_cast<double>(SpawnRadius) * OnSphere.x +
                                            *reinterpret_cast<float*>(C + 0xf4));
                Pos[1] = static_cast<float>(static_cast<double>(SpawnRadius) * OnSphere.y +
                                            *reinterpret_cast<float*>(C + 0xf8));
                Pos[2] = static_cast<float>(static_cast<double>(Sz) +
                                            *reinterpret_cast<float*>(C + 0xfc));

                // 00551029..00551057: velocity random_inside_sphere(+0xb0) -> +0x44.
                Vector const Vel = random_inside_sphere(*reinterpret_cast<float*>(C + 0xb0));
                *reinterpret_cast<float*>(Table[I] + 0x44) = Vel.x;
                *reinterpret_cast<float*>(Table[I] + 0x48) = Vel.y;
                *reinterpret_cast<float*>(Table[I] + 0x4c) = Vel.z;

                // 0055103e..00551086: FCOMP +0x10c / TEST AH,0x1 / JZ -- visibility not below
                // +0x10c accepts outright; otherwise the owner (+0xc4, null rejects) is asked
                // for the density at the new position (vt[0], __thiscall), and only a density
                // above 0.0 (TEST AH,0x41 / JNZ) accepts.
                bool bAccept = true;
                if (!(Visibility >= *reinterpret_cast<float*>(C + 0x10c)))
                {
                    Universe::IZoneImmersion const* const Owner = *(Universe::IZoneImmersion const**)(C + 0xc4);
                    if (Owner == nullptr)
                        bAccept = false;
                    else
                    {
                        using FDensity = float(__thiscall*)(void*, float const*);
                        float const Density =
                            (float)Owner->GetImmersion(*(Vector const*)Pos);
                        if (!(static_cast<double>(Density) > 0.0))
                            bAccept = false;
                    }
                }

                if (bAccept)
                {
                    // 00551088..005510d7: activate; inside an exclusion zone deeper than 0.9
                    // (TEST AH,0x41 / JNZ keeps <= and unordered) it stays hidden.
                    ++Active;
                    Table[I][0x50] = 1;
                    Table[I][0x51] = 0;
                    if (ExclusionMgr != nullptr)
                    {
                        Exclusion::NebulaExclusionZone* HitZone;
                        float Depth = -1.0f;
                        if (ExclusionMgr->is_in_exclusion_zone(
                                *reinterpret_cast<Vector const*>(Table[I]), &HitZone, &Depth,
                                true) &&
                            Depth > kCloudsExclusionDepth)
                            Table[I][0x51] = 1;
                    }
                }
            }

            // 005510e2..005510f4: only active, visible puffs drift and move.
            unsigned char* const Puff = Table[I];
            if (Puff[0x50] == 0 || Puff[0x51] != 0)
                continue;

            float* const Timer = reinterpret_cast<float*>(Puff + 0x38);
            float const Period = *reinterpret_cast<float*>(C + 0xa0);
            // 005510fa..0055110b: TEST AH,0x41 / JNZ -- no drift unless the period is above 0.
            if (Period > 0.0f)
            {
                // 00551111..0055112c: the timer advances by dt; the branch is taken on the OLD
                // value (FCOMP before the FADD; TEST AH,0x1 / JNZ sends < 0 and unordered to
                // 005511cd).
                float const OldTimer = *Timer;
                *Timer = static_cast<float>(static_cast<double>(Dt) + OldTimer);
                if (OldTimer >= 0.0f)
                {
                    // 00551132..00551189: with +0xc8 above 0, the squared distance of the drift
                    // anchor (+0x20) from the camera lowers NearestDistSq (TEST AH,0x5 / JP
                    // keeps >= and unordered).
                    if (*reinterpret_cast<float*>(C + 0xc8) > 0.0f)
                    {
                        float const* const CamPos = Cam->GetPosition();
                        double const Ax = static_cast<double>(*reinterpret_cast<float*>(Puff + 0x20)) - CamPos[0];
                        double const Ay = static_cast<double>(*reinterpret_cast<float*>(Puff + 0x24)) - CamPos[1];
                        double const Az = static_cast<double>(*reinterpret_cast<float*>(Puff + 0x28)) - CamPos[2];
                        double const AnchorDistSq = Az * Az + Ay * Ay + Ax * Ax;
                        if (AnchorDistSq < NearestDistSq)
                            NearestDistSq = static_cast<float>(AnchorDistSq);
                    }
                    // 0055118b..005511c5: past the period (TEST AH,0x41 / JNZ keeps <=), the
                    // timer restarts negative: (-0.8 - rand/32768 * 0.7) * +0x9c.
                    if (*Timer > Period)
                    {
                        int const R = rand();
                        *Timer = static_cast<float>(
                            (kCloudsDriftBase -
                             static_cast<double>(R) * kCloudsRandScale * kCloudsDriftSpan) *
                            *reinterpret_cast<float*>(C + 0x9c));
                    }
                }
                else if (*Timer >= 0.0f)
                {
                    // 005511cd..00551309: the timer just crossed zero. Anchor = position; the
                    // drift direction is a random in-plane direction (cos, sin, 0) of
                    // LookMatrix(anchor - camera), via 3DMathEngine vt+0x2c, scaled to length
                    // 5.0; +0x40 = rand/32768.
                    *reinterpret_cast<float*>(Puff + 0x20) = *reinterpret_cast<float*>(Puff + 0x0);
                    *reinterpret_cast<float*>(Puff + 0x24) = *reinterpret_cast<float*>(Puff + 0x4);
                    *reinterpret_cast<float*>(Puff + 0x28) = *reinterpret_cast<float*>(Puff + 0x8);
                    float const* const CamPos = Cam->GetPosition();
                    Vector ToAnchor;
                    ToAnchor.x = *reinterpret_cast<float*>(Puff + 0x20) - CamPos[0];
                    ToAnchor.y = *reinterpret_cast<float*>(Puff + 0x24) - CamPos[1];
                    ToAnchor.z = *reinterpret_cast<float*>(Puff + 0x28) - CamPos[2];
                    Matrix const Look = LookMatrix(ToAnchor);

                    int const RAngle = rand();
                    double const Angle = static_cast<double>(RAngle) * kCloudsRandScale * kCloudsTwoPi;
                    FVector InPlane;
                    InPlane.X = static_cast<float>(cos(Angle));
                    InPlane.Y = static_cast<float>(sin(Angle));
                    InPlane.Z = 0.0f;
                    FVector Dir;
                    AcquireCloudsMath()->TransformMatrix(
                        Dir, *reinterpret_cast<FMatrix const*>(&Look), InPlane);
                    float* const DriftDir = reinterpret_cast<float*>(Puff + 0x2c);
                    DriftDir[0] = Dir.X;
                    DriftDir[1] = Dir.Y;
                    DriftDir[2] = Dir.Z;
                    double const Len = sqrt(static_cast<double>(DriftDir[0]) * DriftDir[0] +
                                            static_cast<double>(DriftDir[1]) * DriftDir[1] +
                                            static_cast<double>(DriftDir[2]) * DriftDir[2]);
                    double const Scale = static_cast<double>(kCloudsDriftSpeed) / Len;
                    DriftDir[0] = static_cast<float>(Scale * DriftDir[0]);
                    DriftDir[1] = static_cast<float>(Scale * DriftDir[1]);
                    DriftDir[2] = static_cast<float>(Scale * DriftDir[2]);

                    int const RPhase = rand();
                    *reinterpret_cast<float*>(Puff + 0x40) =
                        static_cast<float>(static_cast<double>(RPhase) * kCloudsRandScale);
                }

                // 0055130c..00551348: while the timer is not negative (TEST AH,0x1 / JNZ skips
                // < and unordered), +0x3c = rand * (rand/32768) / 32768.
                if (*Timer >= 0.0f)
                {
                    int const RA = rand();
                    float const A = static_cast<float>(static_cast<double>(RA) * kCloudsRandScale);
                    int const RB = rand();
                    *reinterpret_cast<float*>(Puff + 0x3c) = static_cast<float>(
                        static_cast<double>(RB) * A * kCloudsRandScale);
                }
            }

            // 0055134b..00551382: position += dt * velocity.
            float* const Pos = reinterpret_cast<float*>(Puff);
            float const StepZ = static_cast<float>(
                static_cast<double>(Dt) * *reinterpret_cast<float*>(Puff + 0x4c));
            Pos[0] = static_cast<float>(
                static_cast<double>(Dt) * *reinterpret_cast<float*>(Puff + 0x44) + Pos[0]);
            Pos[1] = static_cast<float>(
                static_cast<double>(Dt) * *reinterpret_cast<float*>(Puff + 0x48) + Pos[1]);
            Pos[2] = static_cast<float>(static_cast<double>(StepZ) + Pos[2]);
        }

        // 0055139c..005513a2.
        *reinterpret_cast<int*>(C + 0x14) = Active;
    }

    // FUN_005513b0 (thiscall, RET 4, arg the nebula's exclusion manager): re-seed every puff of
    // the table around the clouds centre (+0xf4..+0xfc). No test of `this`, the table or a puff.
    void LocalNebulaCloudsRegenExcluded(FNebulaClouds* Clouds, void* ExclusionCtx)
    {
        unsigned char* const R = Clouds->Raw;
        Exclusion::NebulaExclusionZoneMgr* const ExclMgr =
            static_cast<Exclusion::NebulaExclusionZoneMgr*>(ExclusionCtx);
        // 0x005513b6..0x005514ee: for i < +0x0c (re-read each step).
        for (int I = 0; I < *reinterpret_cast<int*>(R + 0xc); ++I)
        {
            unsigned char* const Puff = static_cast<unsigned char*>((*reinterpret_cast<void***>(R))[I]);
            // 0x005513d7 / LAB_005514cb: past the active count (+0x10) -> hidden, flagged.
            if (I >= *reinterpret_cast<int*>(R + 0x10))
            {
                Puff[0x50] = 0;
                Puff[0x51] = 1;
                continue;
            }

            // 0x005513e0..0x0055144a: position = centre + random_inside_sphere(+0x100) (float
            // sums), drift = random_inside_sphere(+0xb0).
            Vector const Sample = random_inside_sphere(*reinterpret_cast<float*>(R + 0x100));
            *reinterpret_cast<float*>(Puff + 0x0) = *reinterpret_cast<float*>(R + 0xf4) + Sample.x;
            *reinterpret_cast<float*>(Puff + 0x4) = *reinterpret_cast<float*>(R + 0xf8) + Sample.y;
            *reinterpret_cast<float*>(Puff + 0x8) = *reinterpret_cast<float*>(R + 0xfc) + Sample.z;
            Vector const Drift = random_inside_sphere(*reinterpret_cast<float*>(R + 0xb0));
            *reinterpret_cast<float*>(Puff + 0x44) = Drift.x;
            *reinterpret_cast<float*>(Puff + 0x48) = Drift.y;
            *reinterpret_cast<float*>(Puff + 0x4c) = Drift.z;

            // 0x0055144d..0x0055146c: the zone (+0xc4; null counts as outside) slot 0 on the
            // position, FCOMP 0.0, TEST AH,0x41 / JNZ -- only strictly above 0 is inside.
            Universe::IZone const* const Zone = *reinterpret_cast<Universe::IZone**>(R + 0xc4);
            if (Zone == nullptr ||
                !(Zone->GetImmersion(*reinterpret_cast<Vector const*>(Puff)) > 0.0))
            {
                // LAB_005514be.
                Puff[0x50] = 0;
                Puff[0x51] = 1;
                continue;
            }

            // 0x0055146e..0x005514b9: shown and not flagged; with a manager, a hit whose distance
            // is strictly above 0.9f (0x005e0e00; TEST AH,0x41 / JNZ) flags it again.
            Puff[0x50] = 1;
            Puff[0x51] = 0;
            if (ExclMgr != nullptr)
            {
                Exclusion::NebulaExclusionZone* HitZone = nullptr;
                float HitDist = -1.0f;
                if (ExclMgr->is_in_exclusion_zone(*reinterpret_cast<Vector*>(Puff), &HitZone,
                                                  &HitDist, true) &&
                    HitDist > 0.9f)
                {
                    Puff[0x51] = 1;
                }
            }
        }
    }

    // FUN_00506f00 (thiscall, RET 4): FUN_005501e0, then free when bit 0 of the flags is set.
    // No test of `this`.
    void LocalNebulaCloudsDestroy(FNebulaClouds* Clouds, unsigned char Flags)
    {
        LocalNebulaCloudsReleaseBits(Clouds);
        if ((Flags & 1u) != 0)
            operator delete(Clouds);
    }

    // FUN_00550240 (thiscall, RET 8) -- the [Clouds] section: (reader, the owner's ShapeDB --
    // nebula +0x04 at 0x0050545c, TexturePanels +0x04 at 0x0050a75d). No test of `this`.
    void LocalNebulaCloudsParseIni(FNebulaClouds* Clouds, INI_Reader& Reader, FShapeDB* Shapes)
    {
        unsigned char* const R = Clouds->Raw;

        // 0x0055024a..0x0055025e: an existing puff table is freed (FUN_00550cf0), +0x04 and
        // +0xec cleared.
        if (R[0xec] != 0)
        {
            LocalNebulaCloudsFreePuffTable(Clouds);
            *reinterpret_cast<int*>(R + 4) = 0;
            R[0xec] = 0;
        }

        // 0x00550264..0x00550350: the defaults (+0x11c, +0x128, +0x90..+0x98 and +0x18..+0x44
        // are left as they are).
        *reinterpret_cast<int*>(R + 0x114) = 0;
        *reinterpret_cast<int*>(R + 0x110) = 0;
        *reinterpret_cast<int*>(R + 0x124) = 0;
        *reinterpret_cast<int*>(R + 0x120) = 0;
        *reinterpret_cast<int*>(R + 0x118) = 0;
        *reinterpret_cast<int*>(R + 0x74) = 0;
        *reinterpret_cast<float*>(R + 0x6c) = 1.0f;
        *reinterpret_cast<float*>(R + 0x70) = 1.0f;
        {
            // 0x00550290..0x005502dc: (+0x6c, +0x70, +0x74) normalised -- 1.0f / sqrt((x^2 + y^2)
            // + z^2) on the FPU, no zero test.
            float* const N = reinterpret_cast<float*>(R + 0x6c);
            double const Inv = 1.0f / sqrt((static_cast<double>(N[0]) * N[0] +
                                            static_cast<double>(N[1]) * N[1]) +
                                           static_cast<double>(N[2]) * N[2]);
            N[0] = static_cast<float>(Inv * N[0]);
            N[1] = static_cast<float>(Inv * N[1]);
            N[2] = static_cast<float>(Inv * N[2]);
        }
        *reinterpret_cast<float*>(R + 0xa4) = 1.0f;
        *reinterpret_cast<float*>(R + 0xdc) = 1.0f;
        *reinterpret_cast<float*>(R + 0xa8) = 50.0f;
        *reinterpret_cast<float*>(R + 0xb4) = 0.75f;
        *reinterpret_cast<int*>(R + 0xa0) = 0;
        *reinterpret_cast<int*>(R + 0xb0) = 0;
        *reinterpret_cast<float*>(R + 0xc0) = 1500.0f;
        *reinterpret_cast<int*>(R + 0xc8) = 0;
        *reinterpret_cast<int*>(R + 0x68) = 0;
        for (int I = 0; I < 8; ++I)
            reinterpret_cast<float*>(R + 0x48)[I] = 1.0f; // REP STOSD with 0x3f800000
        *reinterpret_cast<float*>(R + 0xe4) = 50.0f;
        *reinterpret_cast<float*>(R + 0xe8) = 100.0f;
        *reinterpret_cast<float*>(R + 0x104) = BitsToFloat(0x3dcccccd);
        *reinterpret_cast<float*>(R + 0x108) = BitsToFloat(0x3e99999a);
        *reinterpret_cast<float*>(R + 0x10c) = BitsToFloat(0x3e99999a);
        *reinterpret_cast<int*>(R + 0xf0) = 0;

        // [ESP+0x14]: how many puff_colora / puff_colorb keys were taken (at most two).
        int ColorCount = 0;
        while (Reader.read_value())
        {
            if (Reader.is_value("puff_count"))
            {
                *reinterpret_cast<int*>(R + 4) = static_cast<int>(Reader.get_value_float(0));
            }
            else if (Reader.is_value("puff_radius"))
            {
                *reinterpret_cast<float*>(R + 0xa8) = Reader.get_value_float(0);
            }
            else if (Reader.is_value("puff_colora") || Reader.is_value("puff_colorb"))
            {
                // LAB_00550973: either name fills the next of the two colour slots (+0x78,
                // +0x84) in the order the keys come, as v / 255; the first one also seeds the
                // second slot. A third is ignored.
                if (ColorCount < 2)
                {
                    float Color[3];
                    ReadColor255(Reader, Color);
                    float* const Dest = reinterpret_cast<float*>(R + 0x78 + ColorCount * 0xc);
                    Dest[0] = Color[0];
                    Dest[1] = Color[1];
                    Dest[2] = Color[2];
                    if (ColorCount == 0)
                    {
                        float* const Second = reinterpret_cast<float*>(R + 0x84);
                        Second[0] = Dest[0];
                        Second[1] = Dest[1];
                        Second[2] = Dest[2];
                    }
                    ++ColorCount;
                }
            }
            else if (Reader.is_value("puff_shape"))
            {
                // 0x005503e5..0x005504a2: at most eight (+0x68). A miss is the ShapeDB warning
                // and then this file's own error (SpaceTerrainRenderComp.cpp:0x523, the value
                // string re-read and the reader's file name); the null shape is stored anyway.
                if (*reinterpret_cast<int*>(R + 0x68) < 8)
                {
                    char const* const Name = Reader.get_value_string();
                    FTexShape* const Shape = Shapes->Find(Name);
                    if (Shape == nullptr)
                    {
                        char const* const File = Reader.get_file_name();
                        FDUMP(0x100001,
                              "%s(%d) : *** ERROR: Texture Shape (%s) not found for [Clouds] in "
                              "file (%s)",
                              "E:\\FL\\Scratch\\Source\\Client\\Space\\SpaceTerrainRenderComp.cpp",
                              0x523, Reader.get_value_string(), File);
                    }
                    reinterpret_cast<FTexShape**>(R + 0x28)[*reinterpret_cast<int*>(R + 0x68)] =
                        Shape;
                    ++*reinterpret_cast<int*>(R + 0x68);
                }
            }
            else if (Reader.is_value("puff_weights"))
            {
                for (int I = 0; I < *reinterpret_cast<int*>(R + 0x68); ++I)
                    reinterpret_cast<float*>(R + 0x48)[I] = Reader.get_value_float(I);
            }
            else if (Reader.is_value("puff_opacity"))
            {
                // 0x005504e5..0x005504f0: recognised and ignored.
            }
            else if (Reader.is_value("puff_max_alpha"))
            {
                *reinterpret_cast<float*>(R + 0xb4) = Reader.get_value_float(0);
            }
            else if (Reader.is_value("puff_drift"))
            {
                *reinterpret_cast<float*>(R + 0xb0) = Reader.get_value_float(0);
            }
            else if (Reader.is_value("lightning_shape"))
            {
                // 0x00550547..0x0055063b: four shapes (+0x18..+0x24), value i copied into a
                // 32-byte buffer (FUN_00417c50, at most 31 characters; a null value is empty)
                // and found through the cached ShapeDB find; a miss stores null.
                for (int I = 0; I < 4; ++I)
                {
                    char Buf[0x20];
                    Buf[0] = '\0';
                    char const* const Src = Reader.get_value_string(static_cast<unsigned>(I));
                    if (Src != nullptr)
                    {
                        size_t Len = strlen(Src);
                        if (Len >= sizeof(Buf))
                            Len = sizeof(Buf) - 1;
                        memcpy(Buf, Src, Len);
                        Buf[Len] = '\0';
                    }
                    reinterpret_cast<FTexShape**>(R + 0x18)[I] = Shapes->Find(Buf);
                }
            }
            else if (Reader.is_value("lightning_color"))
            {
                ReadColor255(Reader, reinterpret_cast<float*>(R + 0x90));
            }
            else if (Reader.is_value("lightning_gap"))
            {
                *reinterpret_cast<float*>(R + 0x9c) = Reader.get_value_float(0);
            }
            else if (Reader.is_value("lightning_duration"))
            {
                *reinterpret_cast<float*>(R + 0xa0) = Reader.get_value_float(0);
            }
            else if (Reader.is_value("lightning_size"))
            {
                *reinterpret_cast<float*>(R + 0xa4) = Reader.get_value_float(0);
            }
            else if (Reader.is_value("llight_range"))
            {
                *reinterpret_cast<float*>(R + 0xc8) = Reader.get_value_float(0);
            }
            else if (Reader.is_value("llight_color"))
            {
                ReadColor255(Reader, reinterpret_cast<float*>(R + 0xcc));
            }
            else if (Reader.is_value("lightning_intensity"))
            {
                *reinterpret_cast<float*>(R + 0xdc) = Reader.get_value_float(0);
            }
            else if (Reader.is_value("max_distance"))
            {
                *reinterpret_cast<float*>(R + 0xc0) = Reader.get_value_float(0);
            }
            else if (Reader.is_value("near_fade_distance"))
            {
                // 0x005507fb..0x00550867: +0xe4 / +0xe8; a second value strictly below the first
                // (FCOMP, TEST AH,5 / JP) warns and swaps the two.
                *reinterpret_cast<float*>(R + 0xe4) = Reader.get_value_float(0);
                *reinterpret_cast<float*>(R + 0xe8) = Reader.get_value_float(1);
                if (*reinterpret_cast<float*>(R + 0xe8) < *reinterpret_cast<float*>(R + 0xe4))
                {
                    FDUMP(0x100002, "%s(%d) : *** WARNING: %s",
                          "E:\\FL\\Scratch\\Source\\Client\\Space\\SpaceTerrainRenderComp.cpp",
                          0x555, "near_fade_distances are flipped");
                    float const Far = *reinterpret_cast<float*>(R + 0xe8);
                    *reinterpret_cast<float*>(R + 0xe8) = *reinterpret_cast<float*>(R + 0xe4);
                    *reinterpret_cast<float*>(R + 0xe4) = Far;
                }
            }
            else if (Reader.is_value("no_cloud_blend"))
            {
                *reinterpret_cast<int*>(R + 0x110) = static_cast<int>(Reader.get_value_float(0));
            }
            else if (Reader.is_value("zwrite_clouds"))
            {
                *reinterpret_cast<int*>(R + 0x114) = static_cast<int>(Reader.get_value_float(0));
            }
            else if (Reader.is_value("disable_clouds"))
            {
                *reinterpret_cast<int*>(R + 0x118) = static_cast<int>(Reader.get_value_float(0));
            }
            else if (Reader.is_value("disable_wisps"))
            {
                *reinterpret_cast<int*>(R + 0x11c) = static_cast<int>(Reader.get_value_float(0));
            }
            else if (Reader.is_value("disable_lightning_flash"))
            {
                *reinterpret_cast<int*>(R + 0x124) = static_cast<int>(Reader.get_value_float(0));
            }
            else if (Reader.is_value("disable_lightning_bolts"))
            {
                *reinterpret_cast<int*>(R + 0x120) = static_cast<int>(Reader.get_value_float(0));
            }
            else if (Reader.is_value("disable_batcher"))
            {
                *reinterpret_cast<int*>(R + 0x128) = static_cast<int>(Reader.get_value_float(0));
            }
        }

        // 0x00550a27..0x00550a6d: +0xec = 1; the weights normalised -- the sum on the FPU, each
        // = float((1.0f / sum) * weight), no zero test, the count re-read each step.
        R[0xec] = 1;
        {
            float* const Weights = reinterpret_cast<float*>(R + 0x48);
            int const WeightCount = *reinterpret_cast<int*>(R + 0x68);
            double Sum = 0.0f;
            for (int I = 0; I < WeightCount; ++I)
                Sum += Weights[I];
            if (WeightCount > 0)
            {
                double const InvSum = 1.0f / Sum;
                for (int I = 0; I < *reinterpret_cast<int*>(R + 0x68); ++I)
                    Weights[I] = static_cast<float>(InvSum * Weights[I]);
            }
        }

        // 0x00550a6a..0x00550a8f: +0x0c = +0x10 = +0x04; +0x100 = float(+0xc0 * 0.5f + +0xa8);
        // FUN_00550b60.
        *reinterpret_cast<int*>(R + 0xc) = *reinterpret_cast<int*>(R + 4);
        *reinterpret_cast<int*>(R + 0x10) = *reinterpret_cast<int*>(R + 4);
        *reinterpret_cast<float*>(R + 0x100) = static_cast<float>(
            static_cast<double>(*reinterpret_cast<float*>(R + 0xc0)) * 0.5f +
            *reinterpret_cast<float*>(R + 0xa8));
        LocalNebulaCloudsRebuildBits(Clouds);

        // 0x00550a94..0x00550b42: +0xbc, the near-fade reach. Only a +0xe4 strictly above 0
        // (TEST AH,0x41 / JNZ) computes it; otherwise +0xbc = 0.
        float const NearFade = *reinterpret_cast<float*>(R + 0xe4);
        if (!(NearFade > 0.0f))
        {
            *reinterpret_cast<int*>(R + 0xbc) = 0;
            return;
        }
        // T = tan(double 0.6108652551968893 (0x005e0be0)); A = float(+0x100 * 0.8f / T -
        // +0xe4); B = (T * +0xe4 + +0xa8 * 1.2f) * 1.25f on the FPU.
        double const Tan = tan(0.6108652551968893);
        double const Reach = *reinterpret_cast<float*>(R + 0x100);
        float const A = static_cast<float>(Reach * 0.8f / Tan - NearFade);
        double const BReach =
            (Tan * NearFade + static_cast<double>(*reinterpret_cast<float*>(R + 0xa8)) * 1.2f) *
            1.25f;
        // FCOM +0x100, TEST AH,0x41 / JNZ: B strictly above the reach takes A; otherwise
        // sqrt(reach^2 - B^2).
        double X;
        if (BReach > Reach)
            X = A;
        else
            X = sqrt(Reach * Reach - BReach * BReach);
        // FCOM A, TEST AH,0x41 / JNZ: X strictly above A becomes A.
        if (X > A)
            X = A;
        // +0xbc = float(X + +0xe4 - +0x100); FCOMP 0.0, TEST AH,5 / JP: an ordered "less"
        // becomes 0.
        double const Result = (X + NearFade) - Reach;
        *reinterpret_cast<float*>(R + 0xbc) = static_cast<float>(Result);
        if (Result < 0.0)
            *reinterpret_cast<int*>(R + 0xbc) = 0;
    }

    // FUN_00504e60's clouds-list insert, 0x00505477..0x005054ac: FUN_0042a7e0 buys a node linked
    // in front of the sentinel (+0xf8), FUN_005999b0 stores the value and the size (+0xfc) goes
    // up. No tests.
    void CloudsListPush(FNebulaEffect* Nebula, FNebulaClouds* Clouds)
    {
        FNebulaCloudsListNode* const Sentinel = Nebula->Clouds.Sentinel;
        FNebulaCloudsListNode* const Node =
            static_cast<FNebulaCloudsListNode*>(operator new(sizeof(FNebulaCloudsListNode)));
        Node->Next = Sentinel;
        Node->Prev = Sentinel->Prev;
        Sentinel->Prev = Node;
        Node->Prev->Next = Node;
        Node->Clouds = Clouds;
        ++Nebula->Clouds.Count;
    }

    // FUN_00504e60's [Exterior] section, 0x00504fd3..0x005053ff (LAB_00505342 is a label inside
    // the bit_radius_random_variation clamp).
    void ParseExteriorSection(FNebulaEffect* Nebula, INI_Reader& Reader)
    {
        unsigned char* const B = reinterpret_cast<unsigned char*>(Nebula);
        while (Reader.read_value())
        {
            if (Reader.is_value("bit_radius"))
            {
                *reinterpret_cast<float*>(B + 0xa4) = Reader.get_value_float(0);
            }
            else if (Reader.is_value("color"))
            {
                // 0x00505029..0x005050b2: +0xa8..+0xb0.
                ReadColor255(Reader, reinterpret_cast<float*>(B + 0xa8));
            }
            else if (Reader.is_value("shape"))
            {
                // 0x005050c9..0x0050518a: at most eight shapes (+0x60[+0xa0]); the ShapeDB (+0x04)
                // find warns and yields null on a miss; the count goes up either way.
                if (*reinterpret_cast<int*>(B + 0xa0) < 8)
                {
                    char const* const Name = Reader.get_value_string();
                    FTexShape* const Shape = NebulaShapes(Nebula)->Find(Name);
                    reinterpret_cast<FTexShape**>(B + 0x60)[*reinterpret_cast<int*>(B + 0xa0)] =
                        Shape;
                    ++*reinterpret_cast<int*>(B + 0xa0);
                }
            }
            else if (Reader.is_value("shape_weights"))
            {
                // 0x005051a1..0x005051d3: one weight per shape read so far (+0x80[i]).
                for (int I = 0; I < *reinterpret_cast<int*>(B + 0xa0); ++I)
                    reinterpret_cast<float*>(B + 0x80)[I] = Reader.get_value_float(I);
            }
            else if (Reader.is_value("fill_shape"))
            {
                // 0x005051f7..0x00505294: the fill shape at +0x5c, the same find.
                char const* const Name = Reader.get_value_string();
                *reinterpret_cast<FTexShape**>(B + 0x5c) = NebulaShapes(Nebula)->Find(Name);
            }
            else if (Reader.is_value("plane_slices"))
            {
                *reinterpret_cast<int*>(B + 0xbc) = static_cast<int>(Reader.get_value_float(0));
            }
            else if (Reader.is_value("min_bits"))
            {
                *reinterpret_cast<int*>(B + 0xc0) = static_cast<int>(Reader.get_value_float(0));
            }
            else if (Reader.is_value("max_bits"))
            {
                *reinterpret_cast<int*>(B + 0xc4) = static_cast<int>(Reader.get_value_float(0));
            }
            else if (Reader.is_value("bit_radius_random_variation"))
            {
                // 0x0050532c..0x00505372: FCOM double 0.9999 (0x005dafa8), TEST AH,0x41 / JNZ --
                // only a value strictly above it becomes 0.9999f (0x005dafa4); then FCOM 0.0,
                // TEST AH,5 / JP -- only an ordered "less" becomes 0.0f. +0xc8 = 1 - v,
                // +0xcc = v + 1.
                float V = Reader.get_value_float(0);
                if (static_cast<double>(V) > 0.9999)
                    V = 0.9999f;
                if (static_cast<double>(V) < 0.0)
                    V = 0.0f;
                *reinterpret_cast<float*>(B + 0xc8) = 1.0f - V;
                *reinterpret_cast<float*>(B + 0xcc) = V + 1.0f;
            }
            else if (Reader.is_value("equator_bias"))
            {
                // 0x0050538c..0x0050539a: FSUBR double 1.0.
                *reinterpret_cast<float*>(B + 0xd0) =
                    static_cast<float>(1.0 - static_cast<double>(Reader.get_value_float(0)));
            }
            else if (Reader.is_value("move_bit_percent"))
            {
                *reinterpret_cast<float*>(B + 0xd4) = Reader.get_value_float(0);
            }
            else if (Reader.is_value("opacity"))
            {
                float const V = Reader.get_value_float(0);
                *reinterpret_cast<float*>(B + 0xb4) = V;
                *reinterpret_cast<float*>(B + 0xb8) = V;
            }
        }
    }

    // FUN_00504e60's [Fog] section, 0x005054ed..0x00505652 ([ObjectFog] is recognised and skipped
    // before it).
    void ParseFogSection(FNebulaEffect* Nebula, INI_Reader& Reader)
    {
        unsigned char* const B = reinterpret_cast<unsigned char*>(Nebula);
        while (Reader.read_value())
        {
            if (Reader.is_value("distance"))
            {
                Nebula->FogDistance = Reader.get_value_float(0);
            }
            else if (Reader.is_value("near"))
            {
                Nebula->FogNear = Reader.get_value_float(0);
            }
            else if (Reader.is_value("color"))
            {
                // 0x00505567..0x0050563e: +0x2a0..+0x2a8, then each FCOMP 1.0f, TEST AH,5 / JP --
                // only a value below 1.0 is kept, anything else (>=, NaN) becomes 1.0f.
                float* const Color = reinterpret_cast<float*>(B + 0x2a0);
                ReadColor255(Reader, Color);
                for (int I = 0; I < 3; ++I)
                    Color[I] = (Color[I] < 1.0f) ? Color[I] : 1.0f;
            }
        }
    }

    // FUN_00504e60's [BackgroundLightning] section, 0x00505712..0x0050585e.
    void ParseBackgroundLightningSection(FNebulaEffect* Nebula, INI_Reader& Reader)
    {
        unsigned char* const B = reinterpret_cast<unsigned char*>(Nebula);
        while (Reader.read_value())
        {
            if (Reader.is_value("gap"))
            {
                // 0x00505749..0x00505759: +0x250 = gap, +0x258 = -gap.
                float const Gap = Reader.get_value_float(0);
                *reinterpret_cast<float*>(B + 0x250) = Gap;
                *reinterpret_cast<float*>(B + 0x258) = -Gap;
            }
            else if (Reader.is_value("duration"))
            {
                *reinterpret_cast<float*>(B + 0x254) = Reader.get_value_float(0);
            }
            else if (Reader.is_value("color"))
            {
                ReadColor255(Reader, reinterpret_cast<float*>(B + 0x25c));
            }
            else if (Reader.is_value("intensity"))
            {
                *reinterpret_cast<float*>(B + 0x268) = Reader.get_value_float(0);
            }
        }
    }

    // FUN_00504e60's [DynamicLightning] section, 0x00505879..0x005059c9.
    void ParseDynamicLightningSection(FNebulaEffect* Nebula, INI_Reader& Reader)
    {
        unsigned char* const B = reinterpret_cast<unsigned char*>(Nebula);
        while (Reader.read_value())
        {
            if (Reader.is_value("gap"))
                *reinterpret_cast<float*>(B + 0x274) = Reader.get_value_float(0);
            else if (Reader.is_value("duration"))
                *reinterpret_cast<float*>(B + 0x278) = Reader.get_value_float(0);
            else if (Reader.is_value("color"))
                ReadColor255(Reader, reinterpret_cast<float*>(B + 0x280));
            else if (Reader.is_value("ambient_intensity"))
                *reinterpret_cast<float*>(B + 0x28c) = Reader.get_value_float(0);
            else if (Reader.is_value("intensity_increase"))
                *reinterpret_cast<float*>(B + 0x290) = Reader.get_value_float(0);
        }
    }

    // FUN_00504e60's [NebulaLight] section, 0x005059e4..0x00505abc.
    void ParseNebulaLightSection(FNebulaEffect* Nebula, INI_Reader& Reader)
    {
        unsigned char* const B = reinterpret_cast<unsigned char*>(Nebula);
        while (Reader.read_value())
        {
            if (Reader.is_value("ambient"))
            {
                // 0x00505a19..0x00505a61: +0x2cc = 1, then the three values truncated (ftol)
                // into the bytes +0x2cd..+0x2cf.
                B[0x2cc] = 1;
                B[0x2cd] = static_cast<unsigned char>(static_cast<int>(Reader.get_value_float(0)));
                B[0x2ce] = static_cast<unsigned char>(static_cast<int>(Reader.get_value_float(1)));
                B[0x2cf] = static_cast<unsigned char>(static_cast<int>(Reader.get_value_float(2)));
            }
            else if (Reader.is_value("sun_burnthrough_intensity"))
            {
                *reinterpret_cast<float*>(B + 0x2c4) = Reader.get_value_float(0);
            }
            else if (Reader.is_value("sun_burnthrough_scaler"))
            {
                *reinterpret_cast<float*>(B + 0x2c8) = Reader.get_value_float(0);
            }
        }
    }

    // FUN_00504e60's [Exclusion Zones] section, 0x00505ac1..0x00505ff0. Two locals carry across
    // the keys: the number of zones this section added (EBX, reset at 0x00505b08) gates every
    // key but "exclusion", and the current zone_shell (ESI, reset there too) receives
    // shell_scalar / max_alpha / exclusion_tint.
    void ParseExclusionZonesSection(FNebulaEffect* Nebula, INI_Reader& Reader, unsigned SystemId)
    {
        // 0x00505ad0..0x00505b05: the manager is created once per nebula.
        if (Nebula->ExclusionMgr == nullptr)
            Nebula->ExclusionMgr = new Exclusion::NebulaExclusionZoneMgr();

        int ZonesAdded = 0;
        FNebulaZoneShell* Shell = nullptr;
        while (Reader.read_value())
        {
            if (Reader.is_value("exclusion"))
            {
                // 0x00505b42..0x00505b97: an unknown zone is an FDUMP and nothing else.
                char const* const Nick = Reader.get_value_string();
                Universe::IZone const* const Z = Universe::get_zone(SystemId, Nick);
                if (Z == nullptr)
                {
                    FDUMP(0x100001,
                          "%s(%d) : *** ERROR: AsteroidField linked to missing Exclusion Zone (%s)",
                          "E:\\FL\\Scratch\\Source\\Client\\Space\\Nebula.cpp", 0x2b2, Nick);
                    continue;
                }
                Nebula->ExclusionMgr->push_back(Z, Nebula->Zone);
                ++ZonesAdded;
            }
            else if (Reader.is_value("fog_far"))
            {
                // 0x00505bab..0x00505bcd: FSTP [back + 0x8].
                if (ZonesAdded == 0)
                    continue;
                Exclusion::NebulaExclusionZone* const Back = Nebula->ExclusionMgr->back();
                Back->FogFar = Reader.get_value_float(0);
            }
            else if (Reader.is_value("zone_shell"))
            {
                // 0x00505bef: the current shell is dropped first, whatever follows.
                Shell = nullptr;
                if (ZonesAdded == 0)
                    continue;
                Exclusion::NebulaExclusionZone* const Back = Nebula->ExclusionMgr->back();
                char const* const Name = Reader.get_value_string();
                Back->set_zone_shell_name(Name);
                unsigned const Key = CreateID(Name);

                // 0x00505c38..0x00505c73: a mesh this nebula already loaded is reused.
                FNebulaZoneShellMap& Shells = NebulaZoneShells(Nebula);
                FNebulaZoneShellMap::iterator const Found = NebulaFindZoneShell(Shells, Key);
                if (Found != Shells.end())
                {
                    Shell = Found->second;
                    continue;
                }

                // 0x00505c78..0x00505cb5: a new 0x38 shell, the ObjectHandle ctor and seven
                // 1.0f.
                FNebulaZoneShell* const NewShell = new FNebulaZoneShell;
                for (int I = 0; I < 5; ++I)
                    NewShell->UserData[I] = 1.0f;
                NewShell->ShellScalar = 1.0f;
                NewShell->MaxAlpha = 1.0f;

                // 0x00505cb9..0x00505d08: FUN_00420dd0(Name, 0); on failure the shell is
                // destroyed (FUN_00420f70, FUN_00420d10, free) and there is no current shell.
                if (!ClientMeshHolderLoad(&NewShell->Handle, Name, nullptr))
                {
                    NebulaZoneShellDestroy(NewShell);
                    delete NewShell;
                    continue;
                }

                // 0x00505d0d..0x00505d2c: +0x10 becomes the instance's user data, and its first
                // three floats are set to 1.0f again.
                DALib::Engine->SetInstanceUserData(NewShell->Handle.EngineInstance,
                                                   &NewShell->UserData[0]);
                NewShell->UserData[0] = 1.0f;
                NewShell->UserData[1] = 1.0f;
                NewShell->UserData[2] = 1.0f;

                // 0x00505d2f..0x00505e35: a unique insert keyed by the zone's shell id (the
                // same CreateID), result discarded.
                Shells.insert(FNebulaZoneShellMap::value_type(Back->ZoneShellName.Id, NewShell));
                Shell = NewShell;
            }
            else if (Reader.is_value("shell_scalar"))
            {
                // 0x00505e5d..0x00505e90: FST +0x24; TEST AH,5 / JP -- raised to 1.0 only on an
                // ordered "less".
                if (ZonesAdded == 0 || Shell == nullptr)
                    continue;
                Shell->ShellScalar = Reader.get_value_float(0);
                if (Shell->ShellScalar < 1.0f)
                    Shell->ShellScalar = 1.0f;
            }
            else if (Reader.is_value("max_alpha"))
            {
                // 0x00505eb2..0x00505ee5: FST +0x28; TEST AH,0x41 / JNZ -- capped only when
                // strictly greater.
                if (ZonesAdded == 0 || Shell == nullptr)
                    continue;
                Shell->MaxAlpha = Reader.get_value_float(0);
                if (Shell->MaxAlpha > 1.0f)
                    Shell->MaxAlpha = 1.0f;
            }
            else if (Reader.is_value("exclusion_tint"))
            {
                // 0x00505f0b..0x00505fed: three bytes / 255 (DAT_005c7f68); each is kept only
                // on an ordered "less" than 1.0 (TEST AH,5 / JNP), anything else is 1.0.
                if (ZonesAdded == 0 || Shell == nullptr)
                    continue;
                float const R = Reader.get_value_float(0);
                float const G = Reader.get_value_float(1);
                float const Bl = Reader.get_value_float(2);
                float const ScaledB = static_cast<float>(Bl * (1.0f / 255.0f));
                float const ScaledG = static_cast<float>(G * (1.0f / 255.0f));
                float const ScaledR = static_cast<float>(R * (1.0f / 255.0f));
                Shell->Tint[0] = (ScaledR < 1.0f) ? ScaledR : 1.0f;
                Shell->Tint[1] = (ScaledG < 1.0f) ? ScaledG : 1.0f;
                Shell->Tint[2] = (ScaledB < 1.0f) ? ScaledB : 1.0f;
            }
        }
    }

    // FUN_00504880 (MOV EAX,[ECX+0x20]; RET), shared with the JumpShipEffect colour getter
    // (JumpShipEffectColor, space_scene_obj_draw.cpp). No test of `this`.


    // LAB_00504bf0 / LAB_00504c00 / LAB_00509470 / LAB_00509660 / LAB_00509670: each is
    // FLD float [ECX+off]; RET -- no test of `this`.


    // LAB_00509480 (thiscall, RET 4) — copy fog RGB float3 from +0x2a0; no tests.
    // 0x00509480 loads EAX = [ESP+4] first and never changes it, so the slot returns Out; the
    // space scene's lighting pass (0x0055564c CALL [EDX+0x30]; PUSH EAX) uses that result.


    // LAB_005094b0 (thiscall, RET 4) — fog RGB flicker into Out (or copy +0x2a0 when idle).
    // No test of `this` or of the out pointer.
    // Both RET 4 exits (0x0050963a / 0x00509637) leave EAX = Out (MOV EAX,[ESP+0x2c]).


    // LAB_00509770 — the [NebulaLight] ambient bytes (+0x2cd..+0x2cf, written by Load's
    // 0x00505a19..0x00505a61) when their flag +0x2cc is set, else null; no test of `this`.
    // Reader: the space scene's ambient lerp (0x00555588).


    // FUN_00509790 (thiscall, RET 4) — Out = Zone(+0x20)->GetPos() (IZone vt+0x10), then
    // return Zone->GetRadius() (vt+0x30). No tests of `this`, the out pointer or the zone.


    // LAB_00509340 — exterior distance factor vs Zone@+0x20 (not +0x24).


    // FUN_00509680 — fog factor; +0x24 = NebulaExclusionZone*.


    // LAB_00509710 (thiscall, RET 4). The one stack argument is never read; the draw pass
    // (0x0051c3c0) pushes DAT_006164d4 there. No test of `this` in retail.


    // LAB_005091a0 — CloudsList@+0xf8 → FUN_00551500 (TexturePanelDrawClouds).


    // LAB_00508f20 — exclusion-shell mesh draw when flag@+0x2c and holder@+0x30 set.


    // FUN_00552570 (thiscall, RET 4) — [Lightning] INI into the member at Nebula+0x100.
    // 0x00552570..0x005526c8; no test of `this`.
    void LightningLoadIni(float* Lightning, INI_Reader& Reader)
    {
        unsigned char* const Flag = reinterpret_cast<unsigned char*>(Lightning) + 0x14c;
        if (*Flag != 0)
            *Flag = 0;
        Lightning[0x52] = 0.0f;
        while (Reader.read_value())
        {
            if (Reader.is_value("gap"))
                Lightning[10] = Reader.get_value_float(0);
            else if (Reader.is_value("color"))
            {
                Lightning[7] = Reader.get_value_float(0) * (1.0f / 255.0f);
                Lightning[8] = Reader.get_value_float(1) * (1.0f / 255.0f);
                Lightning[9] = Reader.get_value_float(2) * (1.0f / 255.0f);
            }
            else if (Reader.is_value("size"))
                Lightning[11] = Reader.get_value_float(0);
            else if (Reader.is_value("duration"))
                Lightning[12] = Reader.get_value_float(0);
        }
        Lightning[6] = 0.0f;
        Lightning[14] = 0.0f;
        Lightning[15] = 0.0f;
        Lightning[16] = 0.0f;
        Lightning[17] = 0.0f;
        // 0x00552697..0x005526c1: the first flash is a negative countdown,
        // (-0.8 - rand/32768 * 0.7) * gap, on the FPU with the two double constants
        // (DAT_005e0df8 = double(-0.8f), DAT_005db0b8 = double(0.7f)). This is the only write
        // of +0x00 in Freelancer.exe, so the draw's "+0x00 >= 0" test keeps a lightning with a
        // positive gap dark for good.
        int const R = rand();
        *Flag = 1;
        Lightning[0] = static_cast<float>(
            (static_cast<double>(-0.8f) -
             static_cast<double>(R) * 3.0517578125e-05f * static_cast<double>(0.7f)) *
            Lightning[10]);
    }

    // FUN_005526d0 (thiscall) — clear the lightning's active flag (+0x14c) when it is set.
    void LightningDisable(float* Lightning)
    {
        unsigned char* const Flag = reinterpret_cast<unsigned char*>(Lightning) + 0x14c;
        if (*Flag != 0)
            *Flag = 0;
    }

    // FUN_005526f0 (stdcall, RET 8) — one lightning bolt segment as a camera-facing quad.
    // Seg layout (0x1c): float3 A, float3 B, float* UVs (+8/+c/+10/+14). Eye = the camera
    // position (ICamera vt+0xc, read by the caller). The vector helpers are FUN_00423b60
    // (out = a - b), FUN_00423b30 (out = a + b), FUN_00411d60 (out = v * s), each storing float.
    void LightningDrawSegment(float* Seg, float const* Eye)
    {
        float const* const A = Seg;
        float const* const B = Seg + 3;

        // 0x00552704 / 0x00552713: Eye - A, B - A.
        float EyeMinusA[3];
        EyeMinusA[0] = Eye[0] - A[0];
        EyeMinusA[1] = Eye[1] - A[1];
        EyeMinusA[2] = Eye[2] - A[2];

        float BA[3];
        BA[0] = B[0] - A[0];
        BA[1] = B[1] - A[1];
        BA[2] = B[2] - A[2];

        // 0x00552718..0x0055275d: Cross = (Eye-A) x (B-A); both products and the difference stay
        // on the FPU, one float store (a float*float product is exact in double).
        float Cross[3];
        Cross[0] = static_cast<float>(static_cast<double>(EyeMinusA[2]) * BA[1] -
                                      static_cast<double>(BA[2]) * EyeMinusA[1]);
        Cross[1] = static_cast<float>(static_cast<double>(BA[2]) * EyeMinusA[0] -
                                      static_cast<double>(EyeMinusA[2]) * BA[0]);
        Cross[2] = static_cast<float>(static_cast<double>(EyeMinusA[1]) * BA[0] -
                                      static_cast<double>(BA[1]) * EyeMinusA[0]);

        // 0x00552761 / 0x00552775: Mid = (A + B) * 0.5 (sum stored to float first).
        float Mid[3];
        Mid[0] = (A[0] + B[0]) * 0.5f;
        Mid[1] = (A[1] + B[1]) * 0.5f;
        Mid[2] = (A[2] + B[2]) * 0.5f;

        // 0x00552785: HalfChord = Mid - A.
        float HalfChord[3];
        HalfChord[0] = Mid[0] - A[0];
        HalfChord[1] = Mid[1] - A[1];
        HalfChord[2] = Mid[2] - A[2];

        // 0x0055278a..0x005527e6: Cross *= |HalfChord| / |Cross|. Both lengths and the quotient
        // stay on the FPU (no zero test in retail); each component is stored back as float.
        double const LenHalf = sqrt(static_cast<double>(HalfChord[0]) * HalfChord[0] +
                                    static_cast<double>(HalfChord[1]) * HalfChord[1] +
                                    static_cast<double>(HalfChord[2]) * HalfChord[2]);
        double const LenCross = sqrt(static_cast<double>(Cross[0]) * Cross[0] +
                                     static_cast<double>(Cross[1]) * Cross[1] +
                                     static_cast<double>(Cross[2]) * Cross[2]);
        double const Scale = LenHalf / LenCross;
        Cross[0] = static_cast<float>(Cross[0] * Scale);
        Cross[1] = static_cast<float>(Cross[1] * Scale);
        Cross[2] = static_cast<float>(Cross[2] * Scale);

        // 0x005527f3: Edge = A - Mid; 0x005527f8..0x00552860: Edge and Cross times 1.15
        // (DAT_005db09c), stored through FUN_00422b80.
        float const K = 1.15f;
        float Edge[3];
        Edge[0] = A[0] - Mid[0];
        Edge[1] = A[1] - Mid[1];
        Edge[2] = A[2] - Mid[2];
        Edge[0] *= K;
        Edge[1] *= K;
        Edge[2] *= K;
        Cross[0] *= K;
        Cross[1] *= K;
        Cross[2] *= K;

        // 0x00552865: the UV block (+0x18) is read without a null test.
        float const* const UvBase = *reinterpret_cast<float* const*>(Seg + 6);

        // Quad: Mid+Edge, Mid+Cross, Mid-Edge, Mid-Cross with UV corners from shape.
        Win3DTexCoord2f(UvBase[2], UvBase[3]); // +8, +0xc
        Win3DVertex3f(Mid[0] + Edge[0], Mid[1] + Edge[1], Mid[2] + Edge[2]);
        Win3DTexCoord2f(UvBase[4], UvBase[3]); // +0x10, +0xc
        Win3DVertex3f(Mid[0] + Cross[0], Mid[1] + Cross[1], Mid[2] + Cross[2]);
        Win3DTexCoord2f(UvBase[4], UvBase[5]); // +0x10, +0x14
        Win3DVertex3f(Mid[0] - Edge[0], Mid[1] - Edge[1], Mid[2] - Edge[2]);
        Win3DTexCoord2f(UvBase[2], UvBase[5]); // +8, +0x14
        Win3DVertex3f(Mid[0] - Cross[0], Mid[1] - Cross[1], Mid[2] - Cross[2]);
    }

    // FUN_005529b0 (thiscall, RET 4) — the lightning flash draw, 0x005529b0..0x00552b00.
    void LightningDraw(float* Lightning, void* Camera)
    {
        // 0x005529b6..0x005529d1: nothing unless active (+0x14c) and the countdown +0x00 is not
        // below zero (TEST AH,5 / JNP -- only an ordered "less" returns).
        unsigned char const Flag =
            *reinterpret_cast<unsigned char*>(reinterpret_cast<unsigned char*>(Lightning) + 0x14c);
        if (Flag == 0 || Lightning[0] < 0.0f)
            return;

        // 0x005529da..0x005529f9: culling off, blending on, blend (2, 2), depth write off.
        Win3DDisable(kWin3DCullFace);
        Win3DEnable(kWin3DBlend);
        Win3DSetBlendFunc(2, 2);
        Win3DSetDepthWrite(0);

        // 0x005529fe..0x00552a6d: colour (+0x1c..+0x24) * rand/32768 * intensity (+0x14). The
        // red product stays on the FPU between the two multiplies; green and blue are rounded to
        // float after the first.
        double const Jitter = static_cast<double>(rand()) * 3.0517578125e-05f;
        float const Intensity = Lightning[5];
        float const Red =
            static_cast<float>(static_cast<double>(Lightning[7]) * Jitter * Intensity);
        float const Green = static_cast<float>(
            static_cast<double>(static_cast<float>(Lightning[8] * Jitter)) * Intensity);
        float const Blue = static_cast<float>(
            static_cast<double>(static_cast<float>(Lightning[9] * Jitter)) * Intensity);

        // 0x00552a71: FUN_00422950 -- the camera's projection and view.
        ICamera* const Cam = static_cast<ICamera*>(Camera);
        Win3DApplyCamera(Cam);

        // 0x00552a76..0x00552a84: the texture of the reference at +0x38 (FUN_00425640, frame 0).
        FTextureRef const* const TexRef = *reinterpret_cast<FTextureRef* const*>(Lightning + 14);
        Win3DBindTexture(TexShapeGetGpuHandle(TexRef, 0));

        // 0x00552a89..0x00552a9f: Color3f, then quads (FUN_00421670(8)).
        Win3DColor3f(Red, Green, Blue);
        Win3DBegin(kWin3DQuads);

        // 0x00552aa7..0x00552ad4: eight 0x1c-byte segments from +0x48, each drawn while its
        // counter (+0x128 + 4*i) is below 3; the eye is re-read (ICamera vt+0x0c) per segment.
        int* const Counters = reinterpret_cast<int*>(Lightning + 0x4a);
        float* Seg = Lightning + 0x12;
        for (int I = 0; I < 8; ++I)
        {
            if (Counters[I] < 3)
                LightningDrawSegment(Seg, Cam->GetPosition());
            Seg += 7;
        }

        // 0x00552ad6..0x00552af1: End, depth write on, blending off, culling on.
        Win3DEnd();
        Win3DSetDepthWrite(1);
        Win3DDisable(kWin3DBlend);
        Win3DEnable(kWin3DCullFace);
    }

    // LAB_00508b00 — screen fog fade overlay (+ Lightning@+0x100).


    // LAB_00508cf0 — fog shell + NebulaUpdateBoltCamera under MaterialBatcher/depth gate.


    // FUN_00508e00 (.text:00508e00..00508f19) — fog RS + NebulaDrawBolts under MaterialBatcher/
    // depth gate. No null tests on This, the Pipeline or the MaterialBatcher, and no guard on a
    // zero +0x298 (retail FDIVs straight through).


    // FUN_00504c10 (thiscall, RET 4) -- vt+0x58: FUN_00504cf0, then free (FUN_005b7e1d) when
    // bit 0 of the flags is set; returns this. No test of `this`.


    // FUN_00504e60 (thiscall, RET 8) -- vt+0x00 Load(file, zone), 0x00504e60..0x0050629x. Caller:
    // StarSysLoadIni's [Nebula] block. No test of `this` or of either string.

} // anonymous namespace

void NebulaCloudsConstruct(FNebulaClouds* Clouds)
{
    LocalNebulaCloudsConstruct(Clouds);
}

void NebulaCloudsSetDetail(FNebulaClouds* Clouds, float Detail)
{
    LocalNebulaCloudsSetDetail(Clouds, Detail);
}

void NebulaCloudsApplyDetail(FNebulaClouds* Clouds, float Detail)
{
    LocalNebulaCloudsApplyDetail(Clouds, Detail);
}

void NebulaCloudsReleaseBits(FNebulaClouds* Clouds)
{
    LocalNebulaCloudsReleaseBits(Clouds);
}

void NebulaCloudsRebuildBits(FNebulaClouds* Clouds)
{
    LocalNebulaCloudsRebuildBits(Clouds);
}

void NebulaCloudsRegenExcluded(FNebulaClouds* Clouds, void* ExclusionCtx)
{
    LocalNebulaCloudsRegenExcluded(Clouds, ExclusionCtx);
}

void NebulaCloudsUpdate(FNebulaClouds* Clouds, void* Camera, float Dt, float Visibility,
                        Exclusion::NebulaExclusionZoneMgr* ExclusionMgr)
{
    LocalNebulaCloudsUpdate(Clouds, Camera, Dt, Visibility, ExclusionMgr);
}

void NebulaCloudsDestroy(FNebulaClouds* Clouds, unsigned char Flags)
{
    LocalNebulaCloudsDestroy(Clouds, Flags);
}

void NebulaCloudsParseIni(FNebulaClouds* Clouds, INI_Reader& Reader, FShapeDB* Shapes)
{
    LocalNebulaCloudsParseIni(Clouds, Reader, Shapes);
}

void FNebulaEffect::Load(char const* FilePath, char const* ZoneName)
{
    FNebulaEffect* const This = this;
    unsigned char* const B = reinterpret_cast<unsigned char*>(This);

    // 0x00504e83..0x00504e94: a loaded nebula is shut down first (vt+0x04); +0x4c = 1.
    if (This->FlagLoaded != 0)
        This->Shutdown();
    B[0x4c] = 1;

    // 0x00504e98..0x00504ee6: the zone from the active system (FUN_00554e90); a miss is an
    // FDUMP error and nothing else -- the zone's GetShape (vt+0x18) is called regardless.
    unsigned const SystemId = StarSysGetActiveSystemId();
    This->Zone = const_cast<Universe::IZone*>(Universe::get_zone(SystemId, ZoneName));
    if (This->Zone == nullptr)
        FDUMP(0x100001, "%s(%d) : *** ERROR: Nebula linked to missing Zone (%s)",
              "E:\\FL\\Scratch\\Source\\Client\\Space\\Nebula.cpp", 0x1ee, ZoneName);
    (void)This->Zone->GetShape();

    // 0x00504ee9..0x00504f40: the defaults.
    *reinterpret_cast<float*>(B + 0xb8) = 0.75f;
    *reinterpret_cast<float*>(B + 0xb4) = 0.75f;
    *reinterpret_cast<int*>(B + 0xa0) = 0;
    for (int I = 0; I < 8; ++I)
        reinterpret_cast<float*>(B + 0x80)[I] = 1.0f;
    This->FogDistance = 10000.0f;
    *reinterpret_cast<int*>(B + 0x29c) = 0;
    *reinterpret_cast<int*>(B + 0x24) = 0;
    *reinterpret_cast<int*>(B + 0x28) = 0;
    B[0x2c] = 0;
    *reinterpret_cast<int*>(B + 0x30) = 0;
    *reinterpret_cast<float*>(B + 0x258) = -1.0f;
    *reinterpret_cast<int*>(B + 0xbc) = 1;

    // 0x00504f2f..0x00504f90: the reader, and the file resolved against DALib::Data.
    INI_Reader Reader;
    char FullPath[MAX_PATH];
    GetFileSysPath(FullPath, FilePath, DALib::Data);
    if (!Reader.open(FullPath, false))
    {
        // 0x00506020..0x00506045: an error, then the rebuild and the light as usual.
        FDUMP(0x100001, "%s(%d) : *** ERROR: unable to open nebula file %s",
              "E:\\FL\\Scratch\\Source\\Client\\Space\\Nebula.cpp", 0x312, FullPath);
    }
    else
    {
        // 0x00504f96..0x00506014: the section loop; a header nobody names is skipped.
        while (Reader.read_header())
        {
            if (Reader.is_header("Exterior"))
            {
                ParseExteriorSection(This, Reader);
            }
            else if (Reader.is_header("Clouds"))
            {
                // 0x00505413..0x005054af: a 0x12c clouds object (the ctor FUN_00550180 only
                // on a non-null block), FUN_00552530(detail), FUN_00550240(reader, ShapeDB
                // +0x04), +0xc4 = the zone, then the list insert. Nothing else is tested.
                void* const Mem = operator new(sizeof(FNebulaClouds));
                FNebulaClouds* Clouds = nullptr;
                if (Mem != nullptr)
                {
                    Clouds = static_cast<FNebulaClouds*>(Mem);
                    LocalNebulaCloudsConstruct(Clouds);
                }
                LocalNebulaCloudsSetDetail(Clouds, G_NebulaDetail);
                LocalNebulaCloudsParseIni(Clouds, Reader, NebulaShapes(This));
                *reinterpret_cast<Universe::IZone**>(Clouds->Raw + 0xc4) = This->Zone;
                CloudsListPush(This, Clouds);
            }
            else if (Reader.is_header("ObjectFog"))
            {
                // 0x005054b4..0x005054c4: recognised and skipped -- its keys are never read.
            }
            else if (Reader.is_header("Fog"))
            {
                ParseFogSection(This, Reader);
            }
            else if (Reader.is_header("TexturePanels"))
            {
                // 0x00505669..0x005056c4: every file= goes through GetFullFilename into the
                // ShapeDB (+0x04, FUN_00425b30 with no base directory).
                while (Reader.read_value())
                {
                    if (Reader.is_value("file"))
                        NebulaShapes(This)->Load(GetFullFilename(Reader.get_value_string()),
                                                 nullptr);
                }
            }
            else if (Reader.is_header("Lightning"))
            {
                // 0x005056d4..0x005056ea: FUN_00552570 on +0x100, then +0x248 = the zone.
                LightningLoadIni(reinterpret_cast<float*>(B + 0x100), Reader);
                *reinterpret_cast<Universe::IZone**>(B + 0x248) = This->Zone;
            }
            else if (Reader.is_header("BackgroundLightning"))
            {
                ParseBackgroundLightningSection(This, Reader);
            }
            else if (Reader.is_header("DynamicLightning"))
            {
                ParseDynamicLightningSection(This, Reader);
            }
            else if (Reader.is_header("NebulaLight"))
            {
                ParseNebulaLightSection(This, Reader);
            }
            else if (Reader.is_header("Exclusion Zones"))
            {
                // 0x00505ac1: the only spelling; the system id is the one read at
                // 0x00504e98.
                ParseExclusionZonesSection(This, Reader, SystemId);
            }
        }
    }

    // 0x00506048: FUN_00506600.
    NebulaRebuild(This);

    // LAB_0050604f..00506144 — ambient GameLight when period@+0x278 > 0.
    {
        unsigned char* const LB = reinterpret_cast<unsigned char*>(This);
        // 0x0050604f: FCOMP 0.0 / TEST AH,0x41 / JNZ -- only a strictly positive period
        // builds the light. There is no else: +0x270 keeps whatever it held (null from the
        // ctor, or the pointer the last Shutdown released -- retail never clears it there).
        float const Period = *reinterpret_cast<float*>(LB + 0x278);
        if (Period > 0.0f)
        {
            // 0x00506066..0x00506088: FUN_00537a90(0x12c), the ctor only on a non-null
            // block; the result is stored and used with no null test.
            void* Mem = GameLightHeapAlloc(sizeof(FGameLight));
            FGameLight* Light = nullptr;
            if (Mem != nullptr)
                Light = GameLightConstruct(static_cast<FGameLight*>(Mem));
            *reinterpret_cast<FGameLight**>(LB + 0x270) = Light;
            // FUN_004110c0(type=3, bit0=0) — directional.
            EngineLightSetType((FEngineLight*)Light, 3, false);
            float Zero[3] = {0.0f, 0.0f, 0.0f};
            EngineLightSetPosition((FEngineLight*)Light, Zero);
            unsigned char White[3] = {0xff, 0xff, 0xff};
            EngineLightSetColorBytesAlt((FEngineLight*)Light, White);
            // 0x48f42400 = 500000.0f — FUN_004112f0 raw Range.
            EngineLightSetRangeRaw((FEngineLight*)Light, 500000.0f);
            // 0x005060ea..0x00506141: vt+4 (enable); failure releases the light, warns and
            // clears +0x270; success turns it straight off again when its flag bit 3 is set
            // (FUN_005097c0's flicker enables it).
            if (!GameLightEnable((FEngineLight*)Light))
            {
                GameLightRelease(Light);
                // 0x00506104..0x0050612f: retail passes the double 500000.0 to a "%d".
                FDUMP(0x100002,
                      "%s(%d) : *** WARNING: Could not create nebula light of range %d",
                      "E:\\FL\\Scratch\\Source\\Client\\Space\\Nebula.cpp", 0x32a, 500000.0);
                *reinterpret_cast<FGameLight**>(LB + 0x270) = nullptr;
            }
            else if ((Light->Flags & 0x08) != 0)
            {
                GameLightVirtualDisable(Light);
            }
        }
    }

    // 0x00506144: loaded.
    This->FlagLoaded = 1;
}

void FNebulaEffect::DrawFogOverlay(void* Camera)
{
    FNebulaEffect* const This = this;
    // LAB_00508b00 (thiscall, RET 4), 0x00508b00..0x00508ce6. No test of `this` or Camera.
    if (This->FlagLoaded == 0)
        return;
    unsigned char* const B = reinterpret_cast<unsigned char*>(This);
    ICamera* const Cam = static_cast<ICamera*>(Camera);

    // 0x00508b12 / 0x00508b21: the zone's GetPos (IZone vt+0x10) and the camera's
    // GetOrientation (vt+0x10) are called and their results dropped.
    (void)This->Zone->GetPos();
    (void)Cam->GetOrientation();

    float const* const FadePtr = reinterpret_cast<float*>(B + 0x294);
    bool bDrawOverlay = true;
    // 0x00508b24: FCOMP 1.0f, TEST AH,5 / JP -- only an ordered "less" clears +0x2b8.
    if (*FadePtr < 1.0f)
        B[0x2b8] = 0;
    // 0x00508b3e: JNP skips on "less"; otherwise a set +0x2b8 goes straight to the lightning.
    if (!(*FadePtr < 1.0f) && B[0x2b8] != 0)
        bDrawOverlay = false;
    // 0x00508b5f: FCOMP 0.0f, TEST AH,0x41 / JNZ -- <= 0 or unordered skips to the lightning.
    else if (!(*FadePtr > 0.0f))
        bDrawOverlay = false;

    if (bDrawOverlay)
    {
        // 0x00508b76: FCOMP 1.0f, TEST AH,1 / JNZ -- only >= 1.0 sets +0x2b8.
        if (*FadePtr >= 1.0f)
            B[0x2b8] = 1;
    }

#ifdef FREELANCER_RP9
    // Port-only: while the Direct3D 9 volumetric nebula draws this nebula's inside
    // (nebula_volume.cpp), it replaces the fullscreen fog quad; the flag above and the
    // lightning below stay as retail.
    if (bDrawOverlay && !NebulaVolume::ReplacesFogOverlay(This))
#else
    if (bDrawOverlay)
#endif
    {
        // 0x00508b90: the alpha is the raw +0x294 dword, copied before the state calls.
        float const Alpha = *FadePtr;
        // 0x00508b9c..0x00508be5: FUN_004227d0(Camera); Disable 0xb50; DepthWrite 0;
        // Disable 0xb71; Disable 0xde1; Enable 0xbe2; Disable 0xb44; BlendFunc(5, 6);
        // Begin(8). FUN_00421dd0 is Enable and FUN_00421ed0 is Disable.
        Win3DSetOrtho2DForCamera(static_cast<FSpaceCamera*>(Camera));
        Win3DDisable(kWin3DLightingCap);
        Win3DSetDepthWrite(0);
        Win3DDisable(kWin3DDepthTest);
        Win3DDisable(0xde1); // GL_TEXTURE_2D
        Win3DEnable(kWin3DBlend);
        Win3DDisable(kWin3DCullFace);
        Win3DSetBlendFunc(5, 6);
        Win3DBegin(kWin3DQuads);
        // 0x00508c04: Color4f(+0x2a0, +0x2a4, +0x2a8, alpha).
        float const* Rgb = reinterpret_cast<float*>(B + 0x2a0);
        Win3DColor4f(Rgb[0], Rgb[1], Rgb[2], Alpha);
        // 0x00508c0f: the camera's viewport rect (vt+0x14), no null test; the four corners
        // go (0,0), (W,0), (W,H), (0,H), each width / height an int difference FILD'd to float.
        int const* const Vp = Cam->GetViewport();
        Win3DVertex3f(0.0f, 0.0f, 0.0f);
        Win3DVertex3f(static_cast<float>(Vp[2] - Vp[0]), 0.0f, 0.0f);
        Win3DVertex3f(static_cast<float>(Vp[2] - Vp[0]), static_cast<float>(Vp[3] - Vp[1]),
                      0.0f);
        Win3DVertex3f(0.0f, static_cast<float>(Vp[3] - Vp[1]), 0.0f);
        // 0x00508c91..0x00508cbc: End; DepthWrite 1; Enable 0xb71; Disable 0xbe2;
        // Enable 0xb44; FUN_00422910(Camera).
        Win3DEnd();
        Win3DSetDepthWrite(1);
        Win3DEnable(kWin3DDepthTest);
        Win3DDisable(kWin3DBlend);
        Win3DEnable(kWin3DCullFace);
        Win3DApplyCameraPerspective(Cam);
    }

    // LAB_00508cc5: +0x294 re-read, FCOMP 0.0f, TEST AH,0x41 / JNZ -- only a value strictly
    // above 0 draws the lightning (FUN_005529b0(this+0x100, Camera)).
    if (*FadePtr > 0.0f)
        LightningDraw(reinterpret_cast<float*>(B + 0x100), Camera);
}

void FNebulaEffect::DrawFogShell(void* Camera)
{
    FNebulaEffect* const This = this;
    // LAB_00508cf0 (thiscall, RET 4), 0x00508cf0..0x00508dfa. No test of `this`, Camera,
    // the MaterialBatcher or the Pipeline.
    if (This->FlagLoaded == 0)
        return;
    unsigned char* const B = reinterpret_cast<unsigned char*>(This);
    // 0x00508d07: FCOMP 1.0f, TEST AH,5 / JP -- only an ordered "less" goes on.
    float const Fade = *reinterpret_cast<float*>(B + 0x294);
    if (!(Fade < 1.0f))
        return;

    // 0x00508d1e..0x00508d72: batcher slot8(1, -1); RS 0x89 = 0; DepthWrite 0; Disable 0xb71;
    // FUN_004101e0; RS 0x8b = -1.
    SetMaterialBatchBinOrder(1u, -1);
    DALib::Pipeline->SetRenderState(D3DRS_LIGHTING, 0);
    Win3DSetDepthWrite(0);
    Win3DDisable(kWin3DDepthTest);
    EngineLightClearRegisteredList();
    DALib::Pipeline->SetRenderState(D3DRS_AMBIENT, -1);

    // 0x00508d78..0x00508d8e: the camera's GetOrientation (vt+0x10) block, 12 dwords (the
    // 3x3 axes and, at +0x24, the position) copied to the stack.
    ICamera* const Cam = static_cast<ICamera*>(Camera);
    float Xform[12];
    memcpy(Xform, Cam->GetOrientation(), sizeof(Xform));
    // 0x00508d90: FUN_00422950(Camera) -- the passed camera, not the active space camera.
    Win3DApplyCamera(Cam);
    // 0x00508d95..0x00508db0: FUN_005074e0(this, copy+0x24, copy+0x28, copy+0x2c).
#ifdef FREELANCER_RP9
    // Port-only: while the Direct3D 9 volumetric nebula draws this nebula at full strength
    // (nebula_volume.cpp) the two fill sheets are left out -- the path FUN_005074e0 itself
    // takes without a fill shape; the states around it stay as retail.
    if (!NebulaVolume::ReplacesFillSheets(This))
#endif
        NebulaUpdateBoltCamera(This, Xform[9], Xform[10], Xform[11]);

    // 0x00508db5..0x00508deb: batcher slot8(1, 1); RS 0x89 = 1; DepthWrite 1; Enable 0xb71.
    SetMaterialBatchBinOrder(1u, 1);
    DALib::Pipeline->SetRenderState(D3DRS_LIGHTING, 1);
    Win3DSetDepthWrite(1);
    Win3DEnable(kWin3DDepthTest);
}

void FNebulaEffect::DrawBoltsPass(void* Camera)
{
    FNebulaEffect* const This = this;
    if (This->FlagLoaded == 0)
        return;
    unsigned char* const B = reinterpret_cast<unsigned char*>(This);
    float const FarZ = *reinterpret_cast<float*>(B + 0x2ac);
    float const Near = *reinterpret_cast<float*>(B + 0x29c);
    float const FogDist = *reinterpret_cast<float*>(B + 0x298);
    // 0x00508e11..0x00508e5d: ((FarZ - Near) * 20.0 / FogDist), clamped at 0 when below it,
    // then (T + 1.0) * FogDist + FarZ, stored as float.
    double T = (static_cast<double>(FarZ) - Near) * 20.0 / FogDist;
    if (T < 0.0)
        T = 0.0;
    float const FogEnd = static_cast<float>((T + 1.0) * FogDist + FarZ);
    FogApplyColorAndRange(reinterpret_cast<float*>(B + 0x2a0), FarZ, FogEnd);

    SetMaterialBatchBinOrder(1u, -1);
    DALib::Pipeline->SetRenderState(D3DRS_LIGHTING, 0);
    Win3DSetDepthWrite(0);
    Win3DDisable(kWin3DDepthTest);
    EngineLightClearRegisteredList();
    DALib::Pipeline->SetRenderState(D3DRS_AMBIENT, -1);

#ifdef FREELANCER_RP9
    // Port-only: while the Direct3D 9 volumetric nebula draws this nebula at full strength
    // (nebula_volume.cpp) the exterior billboards are left out -- the path FUN_00507ed0 itself
    // takes with the zone out of view; the states around it stay as retail.
    if (!NebulaVolume::ReplacesExteriorBits(This))
#endif
        NebulaDrawBolts(This, Camera);

    SetMaterialBatchBinOrder(1u, 1);
    DALib::Pipeline->SetRenderState(D3DRS_LIGHTING, 1);
    Win3DSetDepthWrite(1);
    Win3DEnable(kWin3DDepthTest);
    Win3DDisable(0xb60); // GL_FOG — Win3D may warn; listing keeps the call
}

void FNebulaEffect::DrawExclusionShell(void* Camera)
{
    FNebulaEffect* const This = this;
    // LAB_00508f20 (thiscall, RET 4), 0x00508f20..0x00509192. Drawn only while loaded and
    // while Update found a shell for the current exclusion zone (+0x2c / +0x30).
    if (This->FlagLoaded == 0)
        return;
    unsigned char* const B = reinterpret_cast<unsigned char*>(This);
    if (B[0x2c] == 0)
        return;

    // 0x00508f3f..0x00508fb3: batcher slot 8 (1, -1); RS 0x89 = 0; every registered light
    // off (FUN_004101e0); RS 0x8b saved into a zeroed local, then -1; depth write off and
    // fog off (FUN_00421ed0(0xb60)) -- depth test and culling are left alone.
    SetMaterialBatchBinOrder(1u, -1);
    DALib::Pipeline->SetRenderState(D3DRS_LIGHTING, 0);
    EngineLightClearRegisteredList();
    int SavedRs8b = 0;
    DALib::Pipeline->GetRenderState(D3DRS_AMBIENT, &SavedRs8b);
    DALib::Pipeline->SetRenderState(D3DRS_AMBIENT, -1);
    Win3DSetDepthWrite(0);
    Win3DDisable(kWin3DFog);

    // 0x00508fb8..0x00508ff0: (immersion +0x28 - 0.2f) * 1.25f, kept on the FPU; below
    // 0.0 (TEST AH,5 / JP) -> 0.0f, else above 1.0 (TEST AH,0x41 / JNZ) -> 1.0f.
    double Alpha = (static_cast<double>(*reinterpret_cast<float*>(B + 0x28)) - 0.2f) * 1.25f;
    if (Alpha < 0.0)
        Alpha = 0.0f;
    else if (Alpha > 1.0)
        Alpha = 1.0f;

    // 0x00508ff6..0x00509017: the shell's user data gets the tint and alpha * max_alpha.
    FNebulaZoneShell* const Shell = *reinterpret_cast<FNebulaZoneShell**>(B + 0x30);
    Shell->UserData[0] = Shell->Tint[0];
    Shell->UserData[1] = Shell->Tint[1];
    Shell->UserData[2] = Shell->Tint[2];
    Shell->UserData[4] = static_cast<float>(Alpha * Shell->MaxAlpha);

    // 0x0050901a..0x0050909d: a scale transform, identity rotation times
    // zone radius (vt+0x30) * shell_scalar * 0.001f on the diagonal, zero translation.
    Exclusion::NebulaExclusionZone* const Excl =
        *reinterpret_cast<Exclusion::NebulaExclusionZone**>(B + 0x24);
    Universe::IZone const& Zone = *Excl->ZonePtr;
    float const Scale = static_cast<float>(
        static_cast<double>(Zone.GetRadius()) * Shell->ShellScalar * 0.001f);
    FTransform ScaleXf;
    memset(&ScaleXf, 0, sizeof(ScaleXf));
    ScaleXf.Rotation.D[0][0] = Scale;
    ScaleXf.Rotation.D[1][1] = Scale;
    ScaleXf.Rotation.D[2][2] = Scale;

    // 0x005090a1..0x005090f3: the zone's transform (vt+0x0c) times the scale through
    // DAT_00675000 MulTransform (+0x1c), copied out and set as the instance matrix
    // (IEngine +0x9c).
    FTransform const& ZoneXf = reinterpret_cast<FTransform const&>(Zone.GetTransform());
    FTransform Scaled;
    AcquireNebulaMath()->MulTransform(Scaled, ZoneXf, ScaleXf);
    DALib::Engine->SetInstanceMatrix(Shell->Handle.EngineInstance, &Scaled);

    // 0x005090f9..0x00509128: rendered (IRenderer +0x1c) only with a live instance.
    if (Shell->Handle.EngineInstance != reinterpret_cast<void*>(-1))
    {
        DALib::Renderer->RenderInstance(Camera, Shell->Handle.EngineInstance, 1.0f, 0x108u, nullptr,
                              nullptr);
    }

    // 0x0050912b..0x0050918b: batcher slot 8 (1, 1); RS 0x16 = 2, 0x89 = 1, 0x8b restored;
    // depth write on.
    SetMaterialBatchBinOrder(1u, 1);
    DALib::Pipeline->SetRenderState(D3DRS_CULLMODE, 2);
    DALib::Pipeline->SetRenderState(D3DRS_LIGHTING, 1);
    DALib::Pipeline->SetRenderState(D3DRS_AMBIENT, SavedRs8b);
    Win3DSetDepthWrite(1);
}

void FNebulaEffect::DrawCloudsPanels(void* Camera)
{
    FNebulaEffect* const This = this;
    NebulaDrawCloudsPanels(This, Camera);
}

float FNebulaEffect::ZoneExteriorFactor(float const* CamPos3)
{
    FNebulaEffect* const This = this;
    // .text:00509346-0050936d: the offset from the zone's vt+0x10 position, as floats.
    Universe::IZone const& Zone = *This->Zone;
    Vector const& ZonePos = Zone.GetPos();
    Vector Dir;
    Dir.x = CamPos3[0] - ZonePos.x;
    Dir.y = CamPos3[1] - ZonePos.y;
    Dir.z = CamPos3[2] - ZonePos.z;

    // .text:00509373-005093b3: vt+0x30 squared on the stack vs the float (dx^2 + dz^2) +
    // dy^2; TEST 0x41 / JNZ -> go on when the distance is <= or unordered, else 0.0f.
    double const Radius = Zone.GetRadius();
    float const DistSq = static_cast<float>(
        (static_cast<double>(Dir.x) * Dir.x + static_cast<double>(Dir.z) * Dir.z) +
        static_cast<double>(Dir.y) * Dir.y);
    if (DistSq > Radius * Radius)
        return 0.0f;

    // .text:005093b6-005093d1: vs 0.1 (double .rdata:005c7720), TEST 5 / JP -> go on when
    // >= or unordered, else 1.0f.
    if (DistSq < 0.1)
        return 1.0f;

    // .text:005093d4-0050940e: the direction scaled by 1.0 / sqrt (double, unrounded),
    // stored as floats, then vt+0x40.
    double const Dist = sqrt(static_cast<double>(DistSq));
    float const DistF = static_cast<float>(Dist);
    double const Inv = 1.0 / Dist;
    Dir.x = static_cast<float>(Dir.x * Inv);
    Dir.y = static_cast<float>(Dir.y * Inv);
    Dir.z = static_cast<float>(Dir.z * Inv);

    // .text:00509411-00509430: Edge = vt+0x40 - float distance, FST float, the stack
    // value vs 0.0f with TEST 5 / JP -> go on when >= or unordered, else 0.0f.
    double const Edge = static_cast<double>(Zone.GetRadiusAlong(Dir)) - DistF;
    float const EdgeF = static_cast<float>(Edge);
    if (Edge < 0.0)
        return 0.0f;

    // .text:00509433-0050945f: the stored edge vs FogDistance (+0x298), TEST 0x41 / JNZ ->
    // divide when <= or unordered, else 1.0f.
    float const FogDist = This->FogDistance;
    if (EdgeF > FogDist)
        return 1.0f;
    return EdgeF / FogDist;
}

float FNebulaEffect::GetFloat294()
{
    FNebulaEffect* const This = this;
    return *reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(This) + 0x294);
}

float FNebulaEffect::GetFloat2C4()
{
    FNebulaEffect* const This = this;
    return *reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(This) + 0x2c4);
}

float FNebulaEffect::GetFloat2C8()
{
    FNebulaEffect* const This = this;
    return *reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(This) + 0x2c8);
}

float* FNebulaEffect::CopyFogRgb(float* OutRgb3)
{
    FNebulaEffect* const This = this;
    float const* Src = reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(This) + 0x2a0);
    OutRgb3[0] = Src[0];
    OutRgb3[1] = Src[1];
    OutRgb3[2] = Src[2];
    return OutRgb3;
}

float* FNebulaEffect::FillFogRgb(float* OutRgb3)
{
    FNebulaEffect* const This = this;
    unsigned char* const B = reinterpret_cast<unsigned char*>(This);
    // 0x005094b6..0x005094c8: +0x254 FCOMP 0.0f, TEST AH,0x41 / JNP -> copy when the period
    // is <= 0 (a NaN period goes on).
    float const Period = *reinterpret_cast<float*>(B + 0x254);
    if (Period <= 0.0f)
    {
        float const* Src = reinterpret_cast<float*>(B + 0x2a0);
        OutRgb3[0] = Src[0];
        OutRgb3[1] = Src[1];
        OutRgb3[2] = Src[2];
        return OutRgb3;
    }

    // 0x005094ce..0x005094f2: the old +0x258 vs 0.0f, +0x26c = 0, then +0x258 = dt (+0x1c) +
    // old, FST float with the unrounded sum kept on the FPU for the compares below.
    float const AccPrev = *reinterpret_cast<float*>(B + 0x258);
    B[0x26c] = 0;
    float const Dt = *reinterpret_cast<float*>(B + 0x1c);
    double const Acc = static_cast<double>(Dt) + AccPrev;
    *reinterpret_cast<float*>(B + 0x258) = static_cast<float>(Acc);

    // 0x005094f8 TEST AH,0x41 / JNZ: the old value strictly greater than 0 falls through.
    if (AccPrev > 0.0f)
    {
        // 0x005094fd..0x00509539: +0x26c = 1; the sum strictly above the period re-arms
        // +0x258 = (rand * 3.0517578125e-05f * double(0.7f) - 1.5) * +0x250.
        B[0x26c] = 1;
        if (Acc > Period)
        {
            int const R = rand();
            *reinterpret_cast<float*>(B + 0x258) = static_cast<float>(
                (static_cast<double>(R) * 3.0517578125e-05f * static_cast<double>(0.7f) - 1.5) *
                *reinterpret_cast<float*>(B + 0x250));
        }
    }
    else if (Acc > 0.0)
    {
        // LAB_0050953b: the sum strictly above 0 sets +0x26c.
        B[0x26c] = 1;
    }

    if (B[0x26c] == 0)
    {
        float const* Src = reinterpret_cast<float*>(B + 0x2a0);
        OutRgb3[0] = Src[0];
        OutRgb3[1] = Src[1];
        OutRgb3[2] = Src[2];
        return OutRgb3;
    }

    // 0x0050955d..0x00509585: U0 = float(rand * 3.0517578125e-05f); Scale = rand * +0x268 *
    // U0 * 3.0517578125e-05f, kept on the FPU.
    float const U0 = static_cast<float>(static_cast<double>(rand()) * 3.0517578125e-05f);
    int const Rand2 = rand();
    double const Scale = static_cast<double>(Rand2) * *reinterpret_cast<float*>(B + 0x268) * U0 *
                         3.0517578125e-05f;
    // 0x0050958b..0x005095cd: the green and blue products are stored as float before the
    // base is added; the red product stays on the FPU. Each sum is stored as float (green's
    // unrounded sum is the one compared below, which clamps to the same value).
    float const GreenProduct = static_cast<float>(Scale * *reinterpret_cast<float*>(B + 0x260));
    float const BlueProduct = static_cast<float>(Scale * *reinterpret_cast<float*>(B + 0x264));
    float R = static_cast<float>(Scale * *reinterpret_cast<float*>(B + 0x25c) +
                                 *reinterpret_cast<float*>(B + 0x2a0));
    float G = GreenProduct + *reinterpret_cast<float*>(B + 0x2a4);
    float Bv = BlueProduct + *reinterpret_cast<float*>(B + 0x2a8);
    // 0x005095d1..0x00509610: FCOMP 1.0f, TEST AH,5 / JNP -- keep only a value below 1.0,
    // anything else (>=, NaN) becomes 1.0f.
    if (!(R < 1.0f))
        R = 1.0f;
    if (!(G < 1.0f))
        G = 1.0f;
    if (!(Bv < 1.0f))
        Bv = 1.0f;
    OutRgb3[0] = R;
    OutRgb3[1] = G;
    OutRgb3[2] = Bv;
    return OutRgb3;
}

float FNebulaEffect::GetFloat2AC()
{
    FNebulaEffect* const This = this;
    return *reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(This) + 0x2ac);
}

float FNebulaEffect::FogFactor(float const* CamPos3)
{
    FNebulaEffect* const This = this;
    unsigned char* const B = reinterpret_cast<unsigned char*>(This);
    float const FarZ = *reinterpret_cast<float*>(B + 0x2b0);
    Exclusion::NebulaExclusionZone* const Excl =
        *reinterpret_cast<Exclusion::NebulaExclusionZone**>(B + 0x24);
    if (Excl != nullptr)
    {
        // .text:0050968a-005096b6: the exclusion zone's slot +0x00 on the camera position
        // (kept on the stack), blended between FarZ and FogNear + FogDistance.
        double const Inside = Excl->ZonePtr->GetImmersion(*reinterpret_cast<Vector const*>(CamPos3));
        float const Span = This->FogNear + This->FogDistance;
        return static_cast<float>((1.0f - Inside) * Span + Inside * FarZ);
    }

    // LAB_005096b9: vt+0x20 (ZoneExteriorFactor). FCOM 1.0f / TEST 5 / JP -> FarZ when
    // >= 1 or unordered; else FarZ / max(F^2, 1e-4f) (FCOM / TEST 5 / JP keeps F^2 when
    // >= the floor).
    double F = This->ZoneExteriorFactor(CamPos3);
    if (!(F < 1.0f))
        return FarZ;
    F = F * F;
    if (F < 0.0001f)
        F = 0.0001f;
    return static_cast<float>(FarZ / F);
}

float FNebulaEffect::GetFloat2B0()
{
    FNebulaEffect* const This = this;
    return *reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(This) + 0x2b0);
}

float FNebulaEffect::FogFarClamp(float Unused)
{
    FNebulaEffect* const This = this;
    (void)Unused;
    unsigned char* const B = reinterpret_cast<unsigned char*>(This);
    // 0x00509714..0x0050971f: vt+0x40 (GetFloat2B0) + 500.0f (.rdata 0x005d9668), stored
    // rounded to [ESP+4]; the compare below uses the unrounded sum.
    typedef float(__thiscall* FGetFloatFn)(FNebulaEffect* Self);
    const double Sum =
        static_cast<double>(This->GetFloat2B0()) +
        500.0f;
    float V = static_cast<float>(Sum);
    // 0x00509723..0x00509736: FCOMP [+0x2bc], TEST AH,5 / JP -- keep the sum when it is
    // >= +0x2bc or unordered; only a strictly smaller sum is replaced by +0x2bc.
    const float Floor = *reinterpret_cast<float*>(B + 0x2bc);
    if (Sum < Floor)
        V = Floor;
    // 0x0050973a..0x0050975f: with an exclusion zone at +0x24 (only that pointer is tested),
    // R = its zone's GetRadius (IZone vt+0x30) * 2 + 500.0 (double .rdata 0x005db0c0).
    // FCOMP V against R, TEST AH,0x41 / JNZ: V <= R or unordered returns R, else V.
    Exclusion::NebulaExclusionZone* const Excl =
        *reinterpret_cast<Exclusion::NebulaExclusionZone**>(B + 0x24);
    if (Excl != nullptr)
    {
        const double Radius = static_cast<double>(Excl->ZonePtr->GetRadius());
        const double R = (Radius + Radius) + 500.0;
        if (!(V > R))
            return static_cast<float>(R);
    }
    return V;
}

unsigned char const* FNebulaEffect::GetAmbientBytes()
{
    FNebulaEffect* const This = this;
    unsigned char* const B = reinterpret_cast<unsigned char*>(This);
    if (B[0x2cc] == 0)
        return nullptr;
    return B + 0x2cd;
}

Universe::IZone* FNebulaEffect::GetZone()
{
    FNebulaEffect* const This = this;
    return This->Zone;
}

float FNebulaEffect::GetZoneCenterRadius(float* OutPos3)
{
    FNebulaEffect* const This = this;
    Vector const& Pos = This->Zone->GetPos();
    OutPos3[0] = Pos.x;
    OutPos3[1] = Pos.y;
    OutPos3[2] = Pos.z;
    return This->Zone->GetRadius();
}

FNebulaEffectCompanion::FNebulaEffectCompanion()
{
    Values[0] = nullptr;
    Values[1] = nullptr;
    Values[2] = nullptr;
    Values[3] = nullptr;
}

FNebulaEffectCompanion::~FNebulaEffectCompanion()
{
    CliEffectCompanionClear(Values);
}

FNebulaCloudsListStorage::FNebulaCloudsListStorage()
{
    void* ListSent = operator new(0xc);
    *static_cast<void**>(ListSent) = ListSent;
    *reinterpret_cast<void**>(static_cast<unsigned char*>(ListSent) + 4) = ListSent;
    Sentinel = static_cast<FNebulaCloudsListNode*>(ListSent);
    Count = 0;
}

FNebulaCloudsListStorage::~FNebulaCloudsListStorage()
{
    FNebulaCloudsListNode* Node = Sentinel->Next;
    while (Node != Sentinel)
    {
        FNebulaCloudsListNode* const Next = Node->Next;
        operator delete(Node);
        Node = Next;
    }
    operator delete(Sentinel);
    Sentinel = nullptr;
    Count = 0;
}

FNebulaEffect::FNebulaEffect()
    : Field24(0), Field28(0), Flag2C(0), Field30(0)
{
    FNebulaEffect* const This = this;
    // FUN_00504970: member construction registers the native cleanup order for exceptions.
    // Remaining stores deliberately leave every other byte untouched.
    unsigned char* const B = reinterpret_cast<unsigned char*>(This);

    // The compiler installs the derived vptr; +0x24c retains its explicit zero store.
    B[0x24c] = 0;

    // 0x00504a56..0x00504ade.
    *reinterpret_cast<unsigned*>(B + 0xa0) = 0;
    *reinterpret_cast<unsigned*>(B + 0xdc) = 0;
    *reinterpret_cast<unsigned*>(B + 0xe0) = 0;
    This->Zone = nullptr;
    This->FlagLoaded = 0;
    B[0x2cc] = 0;
    *reinterpret_cast<unsigned*>(B + 0xd0) = 0;
    *reinterpret_cast<unsigned*>(B + 0x294) = 0;
    *reinterpret_cast<unsigned*>(B + 0x2b4) = 0;
    *reinterpret_cast<unsigned*>(B + 0x5c) = 0;
    This->ExclusionMgr = nullptr;

    // 0x00504ae1..0x00504af4: FUN_00509d60, erase(begin, end) of the (empty) zone_shell map.
    NebulaZoneShells(This).clear();

    // 0x00504b48..0x00504b4e, 0x00504b87.
    B[0x26c] = 0;
    *reinterpret_cast<unsigned*>(B + 0x270) = 0;
    *reinterpret_cast<unsigned*>(B + 0x1c) = 0;

    // Constants of 0x00504a77..0x00504b8a.
    *reinterpret_cast<unsigned*>(reinterpret_cast<unsigned char*>(This) + 0xc0) = 4;
    *reinterpret_cast<unsigned*>(reinterpret_cast<unsigned char*>(This) + 0xc4) = 8;
    *reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(This) + 0xc8) = 1.0f;
    *reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(This) + 0xcc) = 1.0f;
    *reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(This) + 0xd4) =
        BitsToFloat(0x3f400000);
    *reinterpret_cast<unsigned*>(reinterpret_cast<unsigned char*>(This) + 0xd8) = 1;

    *reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(This) + 0x50) =
        BitsToFloat(0x3dcccccd);
    *reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(This) + 0x54) =
        BitsToFloat(0x41200000);
    *reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(This) + 0x58) =
        BitsToFloat(0x3e4ccccd);

    *reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(This) + 0x250) =
        BitsToFloat(0x41200000);
    *reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(This) + 0x254) =
        BitsToFloat(0x3f000000);
    *reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(This) + 0x258) =
        BitsToFloat(0xbf800000);
    *reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(This) + 0x25c) =
        BitsToFloat(0x3e800000);
    *reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(This) + 0x260) =
        BitsToFloat(0x3e800000);
    *reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(This) + 0x264) =
        BitsToFloat(0x3e800000);
    *reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(This) + 0x268) = 1.0f;

    *reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(This) + 0x274) =
        BitsToFloat(0x40400000);
    *reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(This) + 0x278) =
        BitsToFloat(0x3f000000);
    *reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(This) + 0x27c) =
        BitsToFloat(0xbf800000);
    *reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(This) + 0x280) = 1.0f;
    *reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(This) + 0x284) = 1.0f;
    *reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(This) + 0x288) = 1.0f;
    *reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(This) + 0x28c) =
        BitsToFloat(0x3e800000);
    *reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(This) + 0x290) =
        BitsToFloat(0x3f400000);

    *reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(This) + 0x2c4) =
        BitsToFloat(0x3f000000);
    *reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(This) + 0x2c8) =
        BitsToFloat(0x3fe00000);
    *reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(This) + 0x2bc) =
        BitsToFloat(0x459c4000);

}

FNebulaEffect* NebulaConstruct(FNebulaEffect* This)
{
    return ::new (This) FNebulaEffect();
}

FNebulaEffect* NebulaCreate()
{
    FNebulaEffect* const Nebula = new FNebulaEffect;
    Nebula->Detail(G_NebulaDetail);
    return Nebula;
}


void NebulaDestroy(FNebulaEffect* Nebula)
{
    // FUN_00509b70 (cdecl): a non-null nebula gets vt+0x58 (the deleting dtor) with 1.
    if (Nebula == nullptr)
        return;
#ifdef FREELANCER_RP9
    NebulaVolume::UnregisterNebula(Nebula); // port-only bookkeeping (nebula_volume.cpp)
#endif
    using FDel = void*(__thiscall*)(FNebulaEffect*, unsigned char);
    delete Nebula;
}

void FNebulaEffect::Detail(float Detail)
{
    FNebulaEffect* const Nebula = this;
    // LAB_00509ac0 (thiscall, RET 4) -- vt+0x54: FUN_005524a0(detail) on every clouds object of
    // the list (+0xf8), then FUN_00506600. The global detail is not written here (the options
    // callback FUN_00509b80 does that); no test of `this` or of a node.
    FNebulaCloudsListNode* const Sentinel = Nebula->Clouds.Sentinel;
    for (FNebulaCloudsListNode* N = Sentinel->Next; N != Sentinel; N = N->Next)
        LocalNebulaCloudsApplyDetail(N->Clouds, Detail);
    NebulaRebuild(Nebula);
}

void NebulaSetDetail(FNebulaEffect* Nebula, float Detail)
{
    Nebula->FNebulaEffect::Detail(Detail);
}

void NebulaApplyGlobalDetail(float Detail)
{
    // FUN_00509b80 — PerfOptions Nebula callback.
    RaiseActiveSpaceCameraCut(); // 0x00509b83 FUN_0054bd50, before the store
    G_NebulaDetail = Detail; // DAT_006132dc
    // 00509b91..00509bb0: every nebula on the StarSys list gets slot 21 (+0x54) Detail.
    std::list<FNebulaEffect*>& Nebulae = StarSysNebulaList();
    for (std::list<FNebulaEffect*>::iterator It = Nebulae.begin(); It != Nebulae.end(); ++It)
    {
        FNebulaEffect* const Nebula = *It;
        using FDetail = void(__thiscall*)(FNebulaEffect*, float);
        Nebula->Detail(Detail);
    }
    // 00509bb3: FUN_00509f60, the TexturePanels detail walk.
    TexturePanelsApplyDetailAll(Detail);
}

void NebulaOrientBolts(FNebulaEffect* Nebula, float Tx, float Ty, float Tz)
{
    // FUN_00507340 (thiscall, RET 0xc), 0x00507340..0x005074d9: re-aim each bolt anchor's
    // axis (+0x14..+0x1c) square to the direction from the anchor to the target. No test of
    // `this`, the zone or the buffer; no zero-length test.
    unsigned char* const B = reinterpret_cast<unsigned char*>(Nebula);

    // 0x00507346..0x00507393: the target relative to the zone position (GetPos, vt+0x10), as
    // floats.
    Vector const ZonePos = Nebula->Zone->GetPos();
    float const RelX = Tx - ZonePos.x;
    float const RelY = Ty - ZonePos.y;
    float const RelZ = Tz - ZonePos.z;

    float* const Segs = *reinterpret_cast<float**>(B + 0xe0);
    for (int I = 0; I < *reinterpret_cast<int*>(B + 0xdc); ++I)
    {
        float* const Seg = Segs + I * 8;
        // 0x005073a0..0x005073ea: D = normalize(Rel - anchor). The z difference is stored as a
        // float and squared as unrounded * float; Inv = float(1.0f / sqrt((z2 + y2) + x2)).
        // Dx and Dz are stored as floats, Dy stays on the FPU; the dot below uses the
        // unrounded Dz, the cross the stored one.
        double const A = static_cast<double>(RelX) - Seg[0];
        double const Bv = static_cast<double>(RelY) - Seg[1];
        double const C = static_cast<double>(RelZ) - Seg[2];
        float const CF = static_cast<float>(C);
        float const Inv = static_cast<float>(1.0f / sqrt((C * CF + Bv * Bv) + A * A));
        float const Dx = static_cast<float>(A * Inv);
        double const Dy = Bv * Inv;
        double const DzExt = static_cast<double>(CF) * Inv;
        float const DzF = static_cast<float>(DzExt);

        // 0x005073ee..0x0050740d: |(Dz * o7 + Dy * o6) + Dx * o5| vs double 0.25; TEST AH,0x41 /
        // JNZ -- only strictly above re-aims.
        double const Dot =
            fabs((DzExt * Seg[7] + Dy * Seg[6]) + static_cast<double>(Dx) * Seg[5]);
        if (Dot > 0.25)
        {
            // 0x00507413..0x0050743b: N = D x O, on the FPU (Dz the stored float).
            double const Cx = Dy * Seg[7] - static_cast<double>(DzF) * Seg[6];
            double const Cy = static_cast<double>(DzF) * Seg[5] - static_cast<double>(Dx) * Seg[7];
            double const Cz = static_cast<double>(Dx) * Seg[6] - Dy * Seg[5];
            // 0x0050743d..0x0050746f: the same normalisation (Cz stored as float first).
            float const CzF = static_cast<float>(Cz);
            float const CInv = static_cast<float>(1.0f / sqrt((Cz * CzF + Cy * Cy) + Cx * Cx));
            float const Ncx = static_cast<float>(Cx * CInv);
            double const Ncy = Cy * CInv;
            double const Ncz = static_cast<double>(CzF) * CInv;
            // 0x00507473..0x005074b8: O = N x D, each component stored as float.
            Seg[5] = static_cast<float>(Ncy * DzF - Ncz * Dy);
            Seg[6] = static_cast<float>(Ncz * Dx - static_cast<double>(DzF) * Ncx);
            Seg[7] = static_cast<float>(Dy * Ncx - Ncy * Dx);
        }
    }
}

namespace
{
    // FUN_00506600's projection step (0x00506a84..0x00506ad7, and the two caps): Seg xyz =
    // Zone->GetSurfacePoint(Point) (IZone vt+0x44) minus the zone position, each difference a float.
    void NebulaBoltProject(Universe::IZone* Zone, Vector const& Point, Vector const& ZonePos,
                           float* Seg)
    {
        Vector const Surface = Zone->GetSurfacePoint(Point);
        Seg[0] = Surface.x - ZonePos.x;
        Seg[1] = Surface.y - ZonePos.y;
        Seg[2] = Surface.z - ZonePos.z;
    }

    // FUN_00506600's pull-in step (0x00506afa..0x00506b21 and the caps): Seg xyz *=
    // (Len - +0xd4 * +0xa4) / Len, the factor on the FPU and no zero test. Len is the caller's
    // sqrt; the sites differ in their summation order.
    void NebulaBoltShrink(FNebulaEffect* Nebula, float* Seg, double Len)
    {
        unsigned char* const B = reinterpret_cast<unsigned char*>(Nebula);
        double const Margin = static_cast<double>(*reinterpret_cast<float*>(B + 0xd4)) *
                              *reinterpret_cast<float*>(B + 0xa4);
        double const Shrink = (Len - Margin) / Len;
        Seg[0] = static_cast<float>(Shrink * Seg[0]);
        Seg[1] = static_cast<float>(Shrink * Seg[1]);
        Seg[2] = static_cast<float>(Shrink * Seg[2]);
    }
}

void NebulaRebuild(FNebulaEffect* Nebula)
{
    // FUN_00506600 (thiscall, no arguments), 0x00506600..0x00506cb2: the bolt-anchor buffer
    // (+0xe0, count +0xdc, 0x20 bytes each) rebuilt on the zone's surface. No test of `this`.
    unsigned char* const B = reinterpret_cast<unsigned char*>(Nebula);

    // 0x0050660f..0x00506623: FUN_005b7e1d (operator delete -> free) on the old buffer, then
    // +0xe0 = +0xdc = 0.
    operator delete(*reinterpret_cast<void**>(B + 0xe0));
    *reinterpret_cast<void**>(B + 0xe0) = nullptr;
    *reinterpret_cast<int*>(B + 0xdc) = 0;

    // 0x00506629..0x00506648: the detail DAT_006132dc FCOMP double 0.05 (0x005d5b58), TEST AH,5
    // / JNP -- only an ordered "less" returns; then the zone (+0x20) is the one pointer tested.
    if (static_cast<double>(G_NebulaDetail) < 0.05 || Nebula->Zone == nullptr)
        return;

    // 0x0050664e..0x00506673: the zone's GetPos (vt+0x10), copied to the stack.
    Vector const ZonePos = Nebula->Zone->GetPos();

    // 0x00506653..0x005066c9, on the FPU: Ease = 1.0f - (1.0 - detail)^2;
    // Scale = float((DAT_006132e0 * (1 - Ease) + Ease) * +0xa4);
    // Density = float((1.0f - detail) * DAT_006132e4 + detail);
    // +0xb8 = float(((1 - Ease) * DAT_006132e8 + Ease) * +0xb4).
    double const OneMinusDetail = 1.0 - static_cast<double>(G_NebulaDetail);
    double const Ease = 1.0f - OneMinusDetail * OneMinusDetail;
    double const OneMinusEase = 1.0f - Ease;
    float const Scale = static_cast<float>((G_NebulaDetailEase * OneMinusEase + Ease) *
                                           *reinterpret_cast<float*>(B + 0xa4));
    float const Density = static_cast<float>(
        (1.0 - static_cast<double>(G_NebulaDetail)) * G_NebulaDetailMix + G_NebulaDetail);
    *reinterpret_cast<float*>(B + 0xb8) = static_cast<float>(
        (OneMinusEase * G_NebulaDetailFog + Ease) * *reinterpret_cast<float*>(B + 0xb4));

    // 0x005066cb..0x005066fb: the two per-slice bit counts scaled by Density, + 0.5f, truncated
    // (ftol): +0xc4 is the count at the middle slice ([ESP+0x1c]), +0xc0 the one at the ends
    // ([ESP+0x24]).
    int const MidCount = static_cast<int>(
        static_cast<double>(*reinterpret_cast<int*>(B + 0xc4)) * Density + 0.5f);
    int const EndCount = static_cast<int>(
        static_cast<double>(*reinterpret_cast<int*>(B + 0xc0)) * Density + 0.5f);

    // 0x005066ff: no slices (+0xbc) -> nothing more.
    int const PlaneSlices = *reinterpret_cast<int*>(B + 0xbc);
    if (PlaneSlices <= 0)
        return;

    // 0x0050670b..0x0050675b: the shape weights (+0x80, count +0xa0) normalised -- the sum on
    // the FPU, then each weight = float(weight * (1.0f / sum)) with no zero test; the count is
    // re-read every step.
    {
        float* const Weights = reinterpret_cast<float*>(B + 0x80);
        int const WeightCount = *reinterpret_cast<int*>(B + 0xa0);
        double Sum = 0.0f;
        for (int I = 0; I < WeightCount; ++I)
            Sum += Weights[I];
        if (WeightCount > 0)
        {
            double const InvSum = 1.0f / Sum;
            for (int I = 0; I < *reinterpret_cast<int*>(B + 0xa0); ++I)
                Weights[I] = static_cast<float>(InvSum * Weights[I]);
        }
    }

    // 0x00506755..0x00506812: Mid = float((slices - 1) * 0.5). One slice takes MidCount;
    // otherwise each slice adds floor((1 - W) * EndCount + MidCount * W + 0.5), W rising
    // T / Mid to the middle and falling 1 - (T - Mid) / Mid after it (here through a float
    // 1.0f / Mid; FCOM / TEST AH,0x41 / JNZ -- only T strictly above Mid falls).
    float const Mid = static_cast<float>(static_cast<double>(PlaneSlices - 1) * 0.5);
    int SegTotal = 0;
    if (PlaneSlices == 1)
    {
        SegTotal = MidCount;
    }
    else
    {
        float const InvMid = static_cast<float>(1.0f / static_cast<double>(Mid));
        double const EndCountD = EndCount;
        float const MidCountF = static_cast<float>(MidCount);
        for (int I = 0; I < PlaneSlices; ++I)
        {
            double const T = I;
            double W;
            if (T > Mid)
                W = 1.0f - (T - Mid) * InvMid;
            else
                W = T * InvMid;
            SegTotal += static_cast<int>(floor((1.0 - W) * EndCountD + MidCountF * W + 0.5));
        }
    }

    // 0x00506814..0x00506831: +0xdc = total + 2 (the two polar caps); +0xe0 = operator new
    // (count << 5), neither tested nor cleared.
    int const SegCount = SegTotal + 2;
    *reinterpret_cast<int*>(B + 0xdc) = SegCount;
    float* const Segs =
        static_cast<float*>(operator new(static_cast<unsigned>(SegCount) << 5));
    *reinterpret_cast<float**>(B + 0xe0) = Segs;

    // 0x00506837..0x0050692d: per segment (the count re-read every step): +0xc radius,
    // +0x14/+0x18 sin/cos of a random angle, +0x1c = 0, +0x10 a weighted random shape.
    for (int I = 0; I < *reinterpret_cast<int*>(B + 0xdc); ++I)
    {
        float* const S = Segs + I * 8;
        // 0x00506852..0x00506888: +0xc = float((rand * c * (+0xcc - +0xc8) + +0xc8) * Scale).
        float const RadiusLo = *reinterpret_cast<float*>(B + 0xc8);
        float const RadiusHi = *reinterpret_cast<float*>(B + 0xcc);
        int const RRadius = rand();
        S[3] = static_cast<float>(
            (static_cast<double>(RRadius) * 3.0517578125e-05f *
                 (static_cast<double>(RadiusHi) - RadiusLo) +
             RadiusLo) *
            Scale);
        // 0x0050688b..0x005068c2: angle = rand * c * 2pi (double 0x005c97f8) on the FPU.
        int const RAngle = rand();
        double const Angle =
            static_cast<double>(RAngle) * 3.0517578125e-05f * 6.2831854820251465;
        S[5] = static_cast<float>(sin(Angle));
        S[6] = static_cast<float>(cos(Angle));
        S[7] = 0.0f;
        // 0x005068c5..0x0050690e: pick = float(rand * c); the weights accumulate on the FPU and
        // the first running sum strictly above the pick (FCOMP, TEST AH,5 / JNP) stores that
        // index's shape (+0x60[j]) at +0x10. No match leaves +0x10 as it was.
        int const RPick = rand();
        float const Pick = static_cast<float>(static_cast<double>(RPick) * 3.0517578125e-05f);
        int const WeightCount = *reinterpret_cast<int*>(B + 0xa0);
        double Running = 0.0f;
        for (int J = 0; J < WeightCount; ++J)
        {
            Running += reinterpret_cast<float*>(B + 0x80)[J];
            if (Pick < Running)
            {
                memcpy(&S[4], B + 0x60 + J * 4, sizeof(float));
                break;
            }
        }
    }

    // 0x0050692f..0x00506b43: the latitude rings (the slice count re-read every step). The
    // phase ([ESP+0x28], 0 at the start) carries over between slices.
    float* Seg = Segs;
    float Phase = 0.0f;
    for (int Slice = 0; Slice < *reinterpret_cast<int*>(B + 0xbc); ++Slice)
    {
        int const Slices = *reinterpret_cast<int*>(B + 0xbc);
        int Bits;
        if (Slices == 1)
        {
            // 0x00506955: one slice takes MidCount.
            Bits = MidCount;
        }
        else
        {
            // 0x00506962..0x005069dc: W as above but with a true division by Mid; then past the
            // middle (FCOMP Mid, TEST AH,0x41 / JNZ -- strictly above only) the phase becomes
            // float(pi (0x005c8a5c) / Bits), no zero test.
            float const T = static_cast<float>(Slice);
            double W;
            if (T > Mid)
                W = 1.0f - (static_cast<double>(T) - Mid) / Mid;
            else
                W = static_cast<double>(T) / Mid;
            Bits = static_cast<int>(
                floor((1.0 - W) * EndCount + static_cast<double>(MidCount) * W + 0.5));
            if (T > Mid)
                Phase = static_cast<float>(3.1415927f / static_cast<double>(Bits));
        }

        // 0x005069e0..0x00506a06: Lat = acos((((slice + 1.0f) / (slices + 1.0f)) * 2 - 1.0f)
        // * +0xd0), on the FPU.
        double const Lat =
            acos((((static_cast<double>(Slice) + 1.0f) / (static_cast<double>(Slices) + 1.0f)) *
                      2.0 -
                  1.0f) *
                 *reinterpret_cast<float*>(B + 0xd0));

        if (Bits > 0)
        {
            // 0x00506a19..0x00506a37: Bits as a double; cos(Lat) stored as float, sin(Lat) as a
            // double; the point's y = float(zone y + that float cos).
            double const BitsD = Bits;
            float const CosLat = static_cast<float>(cos(Lat));
            double const SinLat = sin(Lat);
            float const PointY = ZonePos.y + CosLat;
            for (int Bit = 0; Bit < Bits; ++Bit)
            {
                // 0x00506a40..0x00506a9b: angle = bit * 2pi / Bits + phase; x = cos * sinLat
                // (stored, and kept unrounded for the point), z = float(sin * sinLat); y is the
                // float cos (overwritten by the projection below).
                Seg[1] = CosLat;
                double const Angle =
                    static_cast<double>(Bit) * 6.2831854820251465 / BitsD + Phase;
                double const DirX = cos(Angle) * SinLat;
                Seg[0] = static_cast<float>(DirX);
                float const DirZ = static_cast<float>(sin(Angle) * SinLat);
                Seg[2] = DirZ;
                Vector Point;
                Point.x = static_cast<float>(ZonePos.x + DirX);
                Point.y = PointY;
                Point.z = ZonePos.z + DirZ;
                // 0x00506aa1..0x00506ad7: the surface point minus the zone position.
                NebulaBoltProject(Nebula->Zone, Point, ZonePos, Seg);
                // 0x00506ada..0x00506b21: Len = sqrt((z^2 + y^2) + x^2), then the pull-in.
                double const Len = sqrt((static_cast<double>(Seg[2]) * Seg[2] +
                                         static_cast<double>(Seg[1]) * Seg[1]) +
                                        static_cast<double>(Seg[0]) * Seg[0]);
                NebulaBoltShrink(Nebula, Seg, Len);
                Seg += 8;
            }
        }
    }

    // 0x00506b49..0x00506c18: the +Y cap, zone position + (0, 1.0f, 0); same length order.
    Vector Point;
    Point.x = ZonePos.x;
    Point.y = ZonePos.y + 1.0f;
    Point.z = ZonePos.z;
    NebulaBoltProject(Nebula->Zone, Point, ZonePos, Seg);
    double const TopLen = sqrt((static_cast<double>(Seg[2]) * Seg[2] +
                                static_cast<double>(Seg[1]) * Seg[1]) +
                               static_cast<double>(Seg[0]) * Seg[0]);
    NebulaBoltShrink(Nebula, Seg, TopLen);
    Seg += 8;

    // 0x00506c1b..0x00506ca8: the -Y cap; this length sums (x^2 + y^2) + z^2.
    Point.y = ZonePos.y - 1.0f;
    NebulaBoltProject(Nebula->Zone, Point, ZonePos, Seg);
    double const BottomLen = sqrt((static_cast<double>(Seg[0]) * Seg[0] +
                                   static_cast<double>(Seg[1]) * Seg[1]) +
                                  static_cast<double>(Seg[2]) * Seg[2]);
    NebulaBoltShrink(Nebula, Seg, BottomLen);
}

void FNebulaEffect::Update(void* Camera, float Dt)
{
    FNebulaEffect* const Nebula = this;
    // LAB_00506f20 — vtable +0x08 (__thiscall, RET 8). 506f20 MOV EAX,[ESP+8] /
    // MOV [ESI+0x1c],EAX: the SECOND argument (frame dt) lands at +0x1c; the first is the
    // camera (506f9c).
    unsigned char* const B = reinterpret_cast<unsigned char*>(Nebula);
    *reinterpret_cast<float*>(B + 0x1c) = Dt;

    // 00506f2f..00506f98: after a (re)load (+0x4c), +0x2b4 = the largest clouds reach
    // (+0xc0 * 0.5 + +0xa8; FCOM / TEST AH,0x41 / JNZ keeps the old value on <= and
    // unordered). The exclusion manager gets its square at +0x14 -- the listing inlines what
    // NebulaExclusionZoneMgr::set_max_distance does (Common .text:06242b70, the same store).
    if (B[0x4c] != 0)
    {
        *reinterpret_cast<float*>(B + 0x2b4) = 0.0f;
        FNebulaCloudsListNode* const Sentinel = Nebula->Clouds.Sentinel;
        for (FNebulaCloudsListNode* N = Sentinel->Next; N != Sentinel; N = N->Next)
        {
            unsigned char const* const C = N->Clouds->Raw;
            double const Reach =
                static_cast<double>(*reinterpret_cast<float const*>(C + 0xc0)) * 0.5f +
                *reinterpret_cast<float const*>(C + 0xa8);
            if (Reach > *reinterpret_cast<float*>(B + 0x2b4))
                *reinterpret_cast<float*>(B + 0x2b4) = static_cast<float>(Reach);
        }
        if (Nebula->ExclusionMgr != nullptr)
            Nebula->ExclusionMgr->set_max_distance(*reinterpret_cast<float*>(B + 0x2b4));
        B[0x4c] = 0;
    }

    // 00506f9c..00506fcb: camera vt+0x0c (__stdcall), copied to a local.
    ICamera* const Cam = static_cast<ICamera*>(Camera);
    float const* const CamPosPtr = Cam->GetPosition();
    Vector CamPos;
    CamPos.x = CamPosPtr[0];
    CamPos.y = CamPosPtr[1];
    CamPos.z = CamPosPtr[2];
    // The camera's cut flag, byte +0xbc of the camera object (FSpaceCamera::FlagBC, raised by
    // FUN_0054bd50 when the view jumps). Read raw at 00506fe1 and 0050701d.
    unsigned char const CameraCut = static_cast<unsigned char const*>(Camera)[0xbc];

    // 00506fa6..00507007: not yet fully inside (+0x294 < 1.0; TEST AH,0x5 / JP skips >= and
    // unordered): re-orient the bolts (FUN_00507340) when the +0x58 timer is above 1.0 or the
    // camera cut, then restart that timer.
    if (*reinterpret_cast<float*>(B + 0x294) < 1.0)
    {
        if (*reinterpret_cast<float*>(B + 0x58) > 1.0f || CameraCut != 0)
        {
            NebulaOrientBolts(Nebula, CamPos.x, CamPos.y, CamPos.z);
            *reinterpret_cast<float*>(B + 0x58) = 0.0f;
        }
    }

    // 0050700a..00507068: partly inside (+0x294 > 0), the exclusion manager is sorted around
    // the camera (vt+0x08, sort) on a camera cut, or when +0x50 is above 0.25 and +0x58 is not
    // zero (TEST AH,0x44 / JNP skips only an ordered equal).
    if (*reinterpret_cast<float*>(B + 0x294) > 0.0)
    {
        bool bSort = CameraCut != 0;
        if (!bSort)
            bSort = *reinterpret_cast<float*>(B + 0x294) > 0.0 &&
                    *reinterpret_cast<float*>(B + 0x50) > 0.25f &&
                    !(*reinterpret_cast<float*>(B + 0x58) == 0.0f);
        if (bSort && Nebula->ExclusionMgr != nullptr)
            Nebula->ExclusionMgr->sort(&CamPos);
    }

    // 0050706b..00507151: depth into the zone. Dir = camera - zone position (FUN_00422b80, the
    // Vector(x, y, z) ctor); zone vt+0x30 is called and its result dropped; Len = |Dir|; Dir is
    // scaled by 1 / Len and zone vt+0x40 gives the surface distance along it. D = Len - that:
    // positive outside. +0x294 = 0 for D > 0 (TEST AH,0x41 / JNZ), else -D / (+0x298 + +0x29c)
    // capped at 1.0 (-D above the span gives 1.0; <= and unordered divide).
    Universe::IZone const* const Zone = Nebula->Zone;
    Vector const& ZonePos = Zone->GetPos();
    Vector Dir;
    Dir.x = CamPos.x - ZonePos.x;
    Dir.y = CamPos.y - ZonePos.y;
    Dir.z = CamPos.z - ZonePos.z;
    (void)Zone->GetRadius();
    float const Len = static_cast<float>(sqrt(static_cast<double>(Dir.y) * Dir.y +
                                              static_cast<double>(Dir.z) * Dir.z +
                                              static_cast<double>(Dir.x) * Dir.x));
    float const FogSpan = Nebula->FogDistance + Nebula->FogNear;
    double const InvLen = 1.0 / Len;
    Dir.x = static_cast<float>(Dir.x * InvLen);
    Dir.y = static_cast<float>(Dir.y * InvLen);
    Dir.z = static_cast<float>(Dir.z * InvLen);
    float const Depth = static_cast<float>(static_cast<double>(Len) - Zone->GetRadiusAlong(Dir));
    if (Depth > 0.0f)
        *reinterpret_cast<float*>(B + 0x294) = 0.0f;
    else
    {
        double const Inside = -static_cast<double>(Depth);
        if (Inside > FogSpan)
            *reinterpret_cast<float*>(B + 0x294) = 1.0f;
        else
            *reinterpret_cast<float*>(B + 0x294) = static_cast<float>(Inside / FogSpan);
    }

    if (*reinterpret_cast<float*>(B + 0x294) > 0.0)
    {
        // 0050716e..0050718e: fog far = +0x298 + +0x29c, fog near = +0x29c.
        *reinterpret_cast<float*>(B + 0x2b0) = Nebula->FogDistance + Nebula->FogNear;
        *reinterpret_cast<float*>(B + 0x2ac) = Nebula->FogNear;

        // 00507194..005071bc: the current exclusion zone (+0x24) is dropped when the camera's
        // immersion in its zone (IZone vt+0x00) falls below 1e-4 (TEST AH,0x5 / JP keeps >=
        // and unordered); +0x28 keeps that immersion.
        Exclusion::NebulaExclusionZone** const ExclSlot =
            reinterpret_cast<Exclusion::NebulaExclusionZone**>(B + 0x24);
        if (*ExclSlot != nullptr)
        {
            float const Immersion = (*ExclSlot)->ZonePtr->GetImmersion(CamPos);
            *reinterpret_cast<float*>(B + 0x28) = Immersion;
            if (Immersion < 9.999999747378752e-05f) // DAT_005db098
            {
                *ExclSlot = nullptr;
                *reinterpret_cast<unsigned*>(B + 0x28) = 0;
                B[0x2c] = 0;
                *reinterpret_cast<void**>(B + 0x30) = nullptr;
            }
        }

        // 005071bf..0050722d: with none, ask the manager straight into +0x24/+0x28 (its bool
        // is ignored); a zone found there looks its shell id (+0x0c) up in the +0x38 map
        // (FUN_0041bd10 lower_bound, then !(key < node key)) and, when present, raises +0x2c
        // and stores the mapped value at +0x30.
        if (*ExclSlot == nullptr && Nebula->ExclusionMgr != nullptr)
        {
            Nebula->ExclusionMgr->is_in_exclusion_zone(
                CamPos, ExclSlot, reinterpret_cast<float*>(B + 0x28), true);
            if (*ExclSlot != nullptr)
            {
                unsigned const Key = (*ExclSlot)->ZoneShellName.Id;
                FNebulaZoneShellMap& Shells = NebulaZoneShells(Nebula);
                FNebulaZoneShellMap::iterator const Found = NebulaFindZoneShell(Shells, Key);
                if (Found != Shells.end())
                {
                    B[0x2c] = 1;
                    *reinterpret_cast<FNebulaZoneShell**>(B + 0x30) = Found->second;
                }
            }
        }

        // 00507230..00507272: inside a zone with immersion above 0 (TEST AH,0x41 / JNZ) whose
        // fog_far (+0x08) is beyond the near value, the far distance blends toward it:
        // +0x2b0 = (1 - f) * +0x2b0 + fog_far * f.
        Exclusion::NebulaExclusionZone* const Excl = *ExclSlot;
        if (Excl != nullptr && *reinterpret_cast<float*>(B + 0x28) > 0.0f)
        {
            float const FogFar = Excl->FogFar;
            if (FogFar > *reinterpret_cast<float*>(B + 0x2ac))
            {
                float const F = *reinterpret_cast<float*>(B + 0x28);
                *reinterpret_cast<float*>(B + 0x2b0) = static_cast<float>(
                    (1.0f - static_cast<double>(F)) * *reinterpret_cast<float*>(B + 0x2b0) +
                    static_cast<double>(FogFar) * F);
            }
        }
    }
    else
    {
        // 00507274..005072a8: outside, the fog range follows the depth: near = D + +0x29c,
        // far = near + +0x298; an enabled ambient light (+0x270, Flags bit 3) is switched off
        // through its vt+0x08 (GameLight disable, FUN_005376f0).
        double const Near = static_cast<double>(Depth) + Nebula->FogNear;
        *reinterpret_cast<float*>(B + 0x2ac) = static_cast<float>(Near);
        *reinterpret_cast<float*>(B + 0x2b0) = static_cast<float>(Near + Nebula->FogDistance);
        FGameLight* const Light = *reinterpret_cast<FGameLight**>(B + 0x270);
        if (Light != nullptr && (Light->Flags & 0x08) != 0)
            GameLightVirtualDisable(Light);
    }

    // 005072ac..005072ed: within 300 of the surface (TEST AH,0x5 / JP skips >= and
    // unordered), every Clouds gets FUN_00550d40(camera, dt, +0x294, exclusion manager).
    if (Depth < 300.0)
    {
        FNebulaCloudsListNode* const CloudSent = Nebula->Clouds.Sentinel;
        for (FNebulaCloudsListNode* N = CloudSent->Next; N != CloudSent; N = N->Next)
            NebulaCloudsUpdate(N->Clouds, Camera, Dt, *reinterpret_cast<float*>(B + 0x294),
                               Nebula->ExclusionMgr);
    }

    // 005072ef..00507309: fully inside (TEST AH,0x1 / JNZ skips < and unordered), the ambient
    // light tick FUN_005097c0 with the camera position.
    if (*reinterpret_cast<float*>(B + 0x294) >= 1.0f)
    {
        float const CamCopy[3] = {CamPos.x, CamPos.y, CamPos.z};
        NebulaAmbientTick(Nebula, CamCopy);
    }

    // 0050730e..0050732a: the three timers advance by dt.
    *reinterpret_cast<float*>(B + 0x58) = Dt + *reinterpret_cast<float*>(B + 0x58);
    *reinterpret_cast<float*>(B + 0x50) = Dt + *reinterpret_cast<float*>(B + 0x50);
    *reinterpret_cast<float*>(B + 0x54) = Dt + *reinterpret_cast<float*>(B + 0x54);
}

void NebulaUpdate(FNebulaEffect* Nebula, void* Camera, float Dt)
{
    Nebula->FNebulaEffect::Update(Camera, Dt);
}

namespace
{
    // FUN_00507e70 (cdecl, one float): the estimate Y = 0x5f3759df - (bits >> 1) and one Newton
    // step (1.5f - Y * Y * X * 0.5f) * Y, returned on the FPU. FUN_005074e0 also inlines the
    // same sequence twice.
    double NebulaInvSqrt(float X)
    {
        int XBits = 0;
        memcpy(&XBits, &X, sizeof(XBits));
        int const YBits = 0x5f3759df - (XBits >> 1);
        float Y = 0.0f;
        memcpy(&Y, &YBits, sizeof(Y));
        return (1.5f - static_cast<double>(Y) * Y * X * 0.5f) * Y;
    }

    // FUN_00507ea0 (thiscall on the vector, RET 4): each component = float(S * component).
    void NebulaScaleVec3(float* V, float S)
    {
        V[0] = S * V[0];
        V[1] = S * V[1];
        V[2] = S * V[2];
    }

    // The FUN_005074e0 fog-sheet edge: normalize (the FUN_00507e70 sequence on the float
    // (x^2 + z^2) + y^2), then scale to float(Zone->GetRadiusAlong(edge) * 1.15f) / Vec3Length(edge)
    // (FUN_00410190), both factors on the FPU.
    void NebulaSheetEdge(Universe::IZone* Zone, float* Edge)
    {
        float const LenSq = static_cast<float>(
            (static_cast<double>(Edge[0]) * Edge[0] + static_cast<double>(Edge[2]) * Edge[2]) +
            static_cast<double>(Edge[1]) * Edge[1]);
        double const Inv = NebulaInvSqrt(LenSq);
        Edge[0] = static_cast<float>(Edge[0] * Inv);
        Edge[1] = static_cast<float>(Edge[1] * Inv);
        Edge[2] = static_cast<float>(Edge[2] * Inv);
        Vector Dir;
        Dir.x = Edge[0];
        Dir.y = Edge[1];
        Dir.z = Edge[2];
        float const Reach = Zone->GetRadiusAlong(Dir) * 1.15f;
        double const Scale = Reach / Vec3Length(Edge);
        Edge[0] = static_cast<float>(Edge[0] * Scale);
        Edge[1] = static_cast<float>(Edge[1] * Scale);
        Edge[2] = static_cast<float>(Edge[2] * Scale);
    }
}

void NebulaUpdateBoltCamera(FNebulaEffect* Nebula, float CamX, float CamY, float CamZ)
{
    // FUN_005074e0 (thiscall, RET 0xc), 0x005074e0..0x00507e63: two crossing fog sheets through
    // the zone, textured with the fill shape (+0x5c). Only +0x5c is tested; the arguments are the
    // camera position.
    unsigned char* const B = reinterpret_cast<unsigned char*>(Nebula);
    FTexShape* const Shape = *reinterpret_cast<FTexShape**>(B + 0x5c);
    if (Shape == nullptr)
        return;

    // 0x005074f4..0x0050759e: GetSize (vt+0x28), GetPos (vt+0x10) and the GetOrientation block
    // (vt+0x14, nine dwords), split into its columns.
    Universe::IZone* const Zone = Nebula->Zone;
    Vector const Size = Zone->GetSize();
    Vector const ZonePos = Zone->GetPos();
    Matrix const Orient = Zone->GetOrientation();
    float Col0[3] = {Orient.E[0][0], Orient.E[1][0], Orient.E[2][0]};
    float Col1[3] = {Orient.E[0][1], Orient.E[1][1], Orient.E[2][1]};
    float Col2[3] = {Orient.E[0][2], Orient.E[1][2], Orient.E[2][2]};

    // 0x005075a3..0x00507640: FCOMP Size.z, TEST AH,0x41 / JNZ -- only Size.x strictly above
    // Size.z makes column 0 the long axis (A) with Size.x; otherwise column 2 with Size.z. The
    // other of the two is B, used only for the second sheet's fade.
    float AxisA[3];
    float AxisB[3];
    float ExtA;
    if (Size.x > Size.z)
    {
        AxisA[0] = Col0[0];
        AxisA[1] = Col0[1];
        AxisA[2] = Col0[2];
        ExtA = Size.x;
        AxisB[0] = Col2[0];
        AxisB[1] = Col2[1];
        AxisB[2] = Col2[2];
    }
    else
    {
        AxisA[0] = Col2[0];
        AxisA[1] = Col2[1];
        AxisA[2] = Col2[2];
        ExtA = Size.z;
        AxisB[0] = Col0[0];
        AxisB[1] = Col0[1];
        AxisB[2] = Col0[2];
    }

    // 0x00507644..0x005076c9: Look = normalize(camera - zone position) (FUN_00423b60, then the
    // inline FUN_00507e70 sequence on the float (y^2 + x^2) + z^2).
    float Look[3];
    Look[0] = CamX - ZonePos.x;
    Look[1] = CamY - ZonePos.y;
    Look[2] = CamZ - ZonePos.z;
    float const LookLenSq = static_cast<float>(
        (static_cast<double>(Look[1]) * Look[1] + static_cast<double>(Look[0]) * Look[0]) +
        static_cast<double>(Look[2]) * Look[2]);
    double const LookInv = NebulaInvSqrt(LookLenSq);
    Look[0] = static_cast<float>(Look[0] * LookInv);
    Look[1] = static_cast<float>(Look[1] * LookInv);
    Look[2] = static_cast<float>(Look[2] * LookInv);

    // 0x005076cb..0x00507727: FaceA = ((1.0 - |(ay*ly + ax*lx) + az*lz|) - 0.0001f) * 204.08163f,
    // stored as float. FCOMP double 1.0 on the unrounded value, TEST AH,0x41 / JNZ: strictly
    // above -> 1.0f; otherwise the stored float FCOMP 0.0, TEST AH,5 / JP: an ordered "less"
    // -> 0.0f.
    double const DotA = fabs((static_cast<double>(AxisA[1]) * Look[1] +
                              static_cast<double>(AxisA[0]) * Look[0]) +
                             static_cast<double>(AxisA[2]) * Look[2]);
    double const FaceARaw = ((1.0 - DotA) - static_cast<double>(0.0001f)) * 204.08163f;
    float FaceA = static_cast<float>(FaceARaw);
    if (FaceARaw > 1.0)
        FaceA = 1.0f;
    else if (FaceA < 0.0)
        FaceA = 0.0f;

    // 0x0050772f..0x00507777: EdgeA = Look x AxisA (each component a float store).
    float EdgeA[3];
    EdgeA[0] = static_cast<float>(static_cast<double>(AxisA[2]) * Look[1] -
                                  static_cast<double>(Look[2]) * AxisA[1]);
    EdgeA[1] = static_cast<float>(static_cast<double>(Look[2]) * AxisA[0] -
                                  static_cast<double>(AxisA[2]) * Look[0]);
    EdgeA[2] = static_cast<float>(static_cast<double>(AxisA[1]) * Look[0] -
                                  static_cast<double>(Look[1]) * AxisA[0]);
    // 0x0050777c..0x0050782b: normalised and scaled to the zone's reach.
    NebulaSheetEdge(Zone, EdgeA);

    // 0x0050782d..0x00507855: AxisA *= ExtA * 1.15f (the factor on the FPU).
    double const AxisScale = static_cast<double>(ExtA) * 1.15f;
    AxisA[0] = static_cast<float>(AxisA[0] * AxisScale);
    AxisA[1] = static_cast<float>(AxisA[1] * AxisScale);
    AxisA[2] = static_cast<float>(AxisA[2] * AxisScale);

    // 0x00507857..0x005078b3: FaceB from column 1 with 0.2f / 5.0f, the same two clamps.
    double const DotY = fabs((static_cast<double>(Col1[1]) * Look[1] +
                              static_cast<double>(Col1[0]) * Look[0]) +
                             static_cast<double>(Col1[2]) * Look[2]);
    double const FaceBRaw = ((1.0 - DotY) - static_cast<double>(0.2f)) * 5.0f;
    float FaceB = static_cast<float>(FaceBRaw);
    if (FaceBRaw > 1.0)
        FaceB = 1.0f;
    else if (FaceB < 0.0)
        FaceB = 0.0f;
    // 0x005078bb..0x00507911: only a FaceB strictly above 0 (TEST AH,0x41 / JNZ) is refined by
    // axis B: FaceB = float(((1.0 - |(by*ly + bx*lx) + bz*lz|) - 0.2f) * FaceB * 5.0f), the
    // unrounded value below 0 (TEST AH,5 / JP) -> 0.0f.
    if (FaceB > 0.0)
    {
        double const DotB = fabs((static_cast<double>(AxisB[1]) * Look[1] +
                                  static_cast<double>(AxisB[0]) * Look[0]) +
                                 static_cast<double>(AxisB[2]) * Look[2]);
        double const Refined = ((1.0 - DotB) - static_cast<double>(0.2f)) * FaceB * 5.0f;
        FaceB = static_cast<float>(Refined);
        if (Refined < 0.0)
            FaceB = 0.0f;
    }

    // 0x00507919..0x00507961: EdgeB = Look x column 1.
    float EdgeB[3];
    EdgeB[0] = static_cast<float>(static_cast<double>(Col1[2]) * Look[1] -
                                  static_cast<double>(Col1[1]) * Look[2]);
    EdgeB[1] = static_cast<float>(static_cast<double>(Look[2]) * Col1[0] -
                                  static_cast<double>(Col1[2]) * Look[0]);
    EdgeB[2] = static_cast<float>(static_cast<double>(Col1[1]) * Look[0] -
                                  static_cast<double>(Look[1]) * Col1[0]);
    // 0x00507966..0x005079f5: normalised (FUN_00507e70 proper) and scaled to the zone's reach.
    NebulaSheetEdge(Zone, EdgeB);
    // 0x005079f7..0x00507a07: column 1 *= float(Size.y * 1.15f) (FUN_00507ea0).
    NebulaScaleVec3(Col1, Size.y * 1.15f);

    // 0x00507a0c..0x00507a59: lighting off, culling off, depth write off, depth test off,
    // blending on, the fill shape's texture (FUN_00425640, frame 0), blend (5, 6), quads.
    Win3DDisable(kWin3DLightingCap);
    Win3DDisable(kWin3DCullFace);
    Win3DSetDepthWrite(0);
    Win3DDisable(kWin3DDepthTest);
    Win3DEnable(kWin3DBlend);
    Win3DBindTexture(TexShapeGetGpuHandle(Shape, 0));
    Win3DSetBlendFunc(5, 6);
    Win3DBegin(kWin3DQuads);

    float const* const Fog = reinterpret_cast<float*>(B + 0x2a0);
    float V[3];
    float T[3];

    // 0x00507a5e..0x00507c10: sheet A, only when FaceA is strictly above 0 (TEST AH,0x41 / JNZ).
    // Corners (zone +/- EdgeA) +/- AxisA through FUN_00423b30 / FUN_00423b60 (float stores).
    if (FaceA > 0.0)
    {
        Win3DColor4f(Fog[0], Fog[1], Fog[2], FaceA);
        Win3DTexCoord2f(Shape->U0, Shape->V0);
        T[0] = ZonePos.x + EdgeA[0];
        T[1] = ZonePos.y + EdgeA[1];
        T[2] = ZonePos.z + EdgeA[2];
        Win3DVertex3f(T[0] - AxisA[0], T[1] - AxisA[1], T[2] - AxisA[2]);
        Win3DTexCoord2f(Shape->U1, Shape->V0);
        Win3DVertex3f(T[0] + AxisA[0], T[1] + AxisA[1], T[2] + AxisA[2]);
        Win3DTexCoord2f(Shape->U1, Shape->V1);
        T[0] = ZonePos.x - EdgeA[0];
        T[1] = ZonePos.y - EdgeA[1];
        T[2] = ZonePos.z - EdgeA[2];
        Win3DVertex3f(T[0] + AxisA[0], T[1] + AxisA[1], T[2] + AxisA[2]);
        Win3DTexCoord2f(Shape->U0, Shape->V1);
        Win3DVertex3f(T[0] - AxisA[0], T[1] - AxisA[1], T[2] - AxisA[2]);
    }

    // 0x00507c13..0x00507e2c: sheet B, only when FaceB is strictly above 0. Corners
    // (zone +/- EdgeB) +/- scaled column 1. The last corner keeps x and y on the FPU across both
    // subtractions; its z is rounded in between.
    if (FaceB > 0.0)
    {
        Win3DColor4f(Fog[0], Fog[1], Fog[2], FaceB);
        Win3DTexCoord2f(Shape->U0, Shape->V0);
        T[0] = ZonePos.x + EdgeB[0];
        T[1] = EdgeB[1] + ZonePos.y;
        T[2] = EdgeB[2] + ZonePos.z;
        Win3DVertex3f(T[0] - Col1[0], T[1] - Col1[1], T[2] - Col1[2]);
        Win3DTexCoord2f(Shape->U1, Shape->V0);
        Win3DVertex3f(T[0] + Col1[0], T[1] + Col1[1], T[2] + Col1[2]);
        Win3DTexCoord2f(Shape->U1, Shape->V1);
        T[0] = ZonePos.x - EdgeB[0];
        T[1] = ZonePos.y - EdgeB[1];
        T[2] = ZonePos.z - EdgeB[2];
        V[0] = T[0] + Col1[0];
        V[1] = T[1] + Col1[1];
        V[2] = T[2] + Col1[2];
        Win3DVertex3f(V[0], V[1], V[2]);
        Win3DTexCoord2f(Shape->U0, Shape->V1);
        V[0] = static_cast<float>(
            (static_cast<double>(ZonePos.x) - EdgeB[0]) - static_cast<double>(Col1[0]));
        V[1] = static_cast<float>(
            (static_cast<double>(ZonePos.y) - EdgeB[1]) - static_cast<double>(Col1[1]));
        V[2] = (ZonePos.z - EdgeB[2]) - Col1[2];
        Win3DVertex3f(V[0], V[1], V[2]);
    }

    // 0x00507e2f..0x00507e54: End, depth test on, depth write on, blending off, culling on
    // (lighting is left off).
    Win3DEnd();
    Win3DEnable(kWin3DDepthTest);
    Win3DSetDepthWrite(1);
    Win3DDisable(kWin3DBlend);
    Win3DEnable(kWin3DCullFace);
}

void NebulaDrawBolts(FNebulaEffect* Nebula, void* Camera)
{
    // FUN_00507ed0 (__thiscall, RET 4), traced end to end against .text:00507ed0..00508aef.
    unsigned char* const B = reinterpret_cast<unsigned char*>(Nebula);

    // 0x00507edf..0x00507f00: nothing without segments (+0xdc, signed JLE), nothing once +0x294
    // is not below 1.0 (`TEST AH,0x5; JP` also exits when unordered).
    if (*reinterpret_cast<int*>(B + 0xdc) <= 0)
        return;
    if (!(*reinterpret_cast<float*>(B + 0x294) < 1.0))
        return;

    // 0x00507f06..0x00507f5e: the camera's slot +0x10 (orientation followed by the position:
    // twelve dwords copied with REP MOVSD) and the zone's slot +0x10 position.
    ICamera* const Cam = static_cast<ICamera*>(Camera);
    FTransform View;
    memcpy(&View, Cam->GetOrientation(), sizeof(View));
    Vector const CamPos = { View.Translation.X, View.Translation.Y, View.Translation.Z };
    Vector const ZonePos = Nebula->Zone->GetPos();

    // 0x00507f62..0x00507fdb: zone centre into view space (DAT_00675000 slot +0x3c), zone radius
    // (slot +0x30), camera ClassifySphere (slot +0x3c); `CMP AX,3; JC` -- below 3 draws nothing.
    {
        FVector ZoneWorld;
        ZoneWorld.X = ZonePos.x;
        ZoneWorld.Y = ZonePos.y;
        ZoneWorld.Z = ZonePos.z;
        FVector ZoneView = {};
        AcquireNebulaMath()->InverseTransform(ZoneView, View, ZoneWorld);
        float const Center[3] = { ZoneView.X, ZoneView.Y, ZoneView.Z };
        float const ZoneRadius = Nebula->Zone->GetRadius();
        if (static_cast<unsigned short>(Cam->ClassifySphere(Center, ZoneRadius)) < 3)
            return;
    }

    // 0x00507fe1..0x005082cb: back-to-front bubble sort while +0x54 is above 1.15f, by squared
    // distance to the camera (world point minus camera position). A pair swaps only when the
    // later one is farther by more than 10. The previous distance is cached (byte [ESP+0xf])
    // only right after a swap -- the swapped-down record keeps its value; a pair left alone
    // inside the 10 band keeps the flag as it was (retail quirk, kept). +0x54 is zeroed after
    // the sort, including when there is only one segment.
    if (*reinterpret_cast<float*>(B + 0x54) > 1.15f)
    {
        int Swapped = 0;
        do
        {
            Swapped = 0;
            if (*reinterpret_cast<int*>(B + 0xdc) <= 1)
                break;
            bool bPrevCached = false;
            double PrevDistSq = 0.0;
            for (int I = 1; I < *reinterpret_cast<int*>(B + 0xdc); ++I)
            {
                float* const Segs = *reinterpret_cast<float**>(B + 0xe0);
                float* const Prev = Segs + (I - 1) * 8;
                float* const Cur = Segs + I * 8;
                if (!bPrevCached)
                {
                    float const Dx = (ZonePos.x + Prev[0]) - CamPos.x;
                    float const Dy = (ZonePos.y + Prev[1]) - CamPos.y;
                    float const Dz = (ZonePos.z + Prev[2]) - CamPos.z;
                    PrevDistSq = static_cast<double>(Dx) * Dx + static_cast<double>(Dy) * Dy +
                                 static_cast<double>(Dz) * Dz;
                }
                float const Cx = (ZonePos.x + Cur[0]) - CamPos.x;
                float const Cy = (ZonePos.y + Cur[1]) - CamPos.y;
                float const Cz = (ZonePos.z + Cur[2]) - CamPos.z;
                double const CurDistSq = static_cast<double>(Cx) * Cx +
                                         static_cast<double>(Cy) * Cy +
                                         static_cast<double>(Cz) * Cz;
                if (!(PrevDistSq < CurDistSq))
                {
                    bPrevCached = false;
                    continue;
                }
                if (!(fabs(PrevDistSq - CurDistSq) > 10.0))
                    continue;
                ++Swapped;
                float Tmp[8];
                memcpy(Tmp, Prev, sizeof(Tmp));
                memcpy(Prev, Cur, sizeof(Tmp));
                memcpy(Cur, Tmp, sizeof(Tmp));
                bPrevCached = true;
            }
        } while (Swapped > 0);
        *reinterpret_cast<float*>(B + 0x54) = 0.0f;
    }

    // 0x005082d2..0x00508303: lighting and culling off, no depth write, depth test OFF
    // (FUN_00421ed0 0xb71), blending on, then FUN_00422950 with the camera.
    Win3DDisable(kWin3DLightingCap);
    Win3DDisable(kWin3DCullFace);
    Win3DSetDepthWrite(0);
    Win3DDisable(kWin3DDepthTest);
    Win3DEnable(kWin3DBlend);
    Win3DApplyCamera(Cam);
    // 0x00508308..0x00508315: FUN_00425640(+0x60 shape, frame 0) bound through FUN_004220a0.
    FTexShape* const BoundShape = *reinterpret_cast<FTexShape**>(B + 0x60);
    Win3DBindTexture(TexShapeGetGpuHandle(BoundShape, 0));
    // 0x0050831a..0x0050836c: the shape's alpha cache (+0x04), probed while it is negative:
    // FUN_005b7010(0) seeds the format id, TextureLib slot 23 (+0x5c, GetGroupPixelFormat) reads
    // the shape record's, then FUN_00552420's body inline -- ids 10..14 have alpha, any other id
    // takes its descriptor's +0x48 byte (FUN_005b7030).
    if (BoundShape->Field04 < 0)
    {
        int Format = PixelFormatFromD3D(0);
        DALib::TextureLib->GetGroupPixelFormat(BoundShape->Texture, nullptr, &Format);
        unsigned char bHasAlpha = 1;
        if (Format < 10 || Format > 14)
            bHasAlpha = static_cast<unsigned char>(PixelFormatGetDesc(Format)->HasAlpha & 0xffu);
        BoundShape->Field04 = bHasAlpha;
    }
    // 0x0050836f..0x00508387: FUN_004220c0(5, 6) with alpha, (2, 2) without. The SETNZ result
    // is kept ([ESP+0xf]) and picks the colour call per bolt below.
    bool const bHasAlpha = BoundShape->Field04 != 0;
    if (bHasAlpha)
        Win3DSetBlendFunc(kWin3DBlendSrcAlpha, kWin3DBlendInvSrcAlpha);
    else
        Win3DSetBlendFunc(kWin3DBlendOne, kWin3DBlendOne);

    // 0x0050838c..0x005083c0: Pipeline render state 0x18 saved into a zeroed local (slot +0xf4),
    // then set to 4 (slot +0xf0). No null test on the Pipeline.
    int SavedRs18 = 0;
    DALib::Pipeline->GetRenderState(D3DRS_ALPHAREF, &SavedRs18);
    DALib::Pipeline->SetRenderState(D3DRS_ALPHAREF, 4);

    // 0x005083c6..0x005083fb: base alpha +0xb8, faded out over the second half of +0x294.
    float const Fade = *reinterpret_cast<float*>(B + 0x294);
    float BaseAlpha = *reinterpret_cast<float*>(B + 0xb8);
    if (Fade > 0.5f)
        BaseAlpha = static_cast<float>((1.0 - ((static_cast<double>(Fade) - 0.5) +
                                               (static_cast<double>(Fade) - 0.5))) * BaseAlpha);

    // 0x005083ff..0x0050846c: edge fade. `TEST AH,0x41; JP` against 0.0f takes the zero branch
    // when +0x294 is above zero (or unordered): only a non-positive +0x294 gets the span ramp.
    // Span = +0x2ac - +0x29c; below 7000 -> 0, from 10000 up -> 1, linear in between.
    float EdgeFade = 1.0f;
    if (!(Fade <= 0.0f))
        EdgeFade = 0.0f;
    else
    {
        float const Span = *reinterpret_cast<float*>(B + 0x2ac) - Nebula->FogNear;
        if (Span < 7000.0f)
            EdgeFade = 0.0f;
        else if (Span < 10000.0f)
            EdgeFade = static_cast<float>(1.0 - (10000.0f - Span) * 0.00033333333f);
    }

    // 0x0050846c: FUN_00421670(8).
    Win3DBegin(kWin3DQuads);

    // 0x00508473..0x00508509: the unit vector from the zone centre to the camera.
    float ToCam[3] = { CamPos.x - ZonePos.x, CamPos.y - ZonePos.y, CamPos.z - ZonePos.z };
    {
        float const InvLen =
            1.0f / sqrtf(ToCam[0] * ToCam[0] + ToCam[2] * ToCam[2] + ToCam[1] * ToCam[1]);
        ToCam[0] *= InvLen;
        ToCam[1] *= InvLen;
        ToCam[2] *= InvLen;
    }

    // 0x0050847e: the segment array is read once; the count (+0xdc) is re-read per iteration.
    // Segment record (0x20): +0x00 position relative to the zone, +0x0c radius, +0x10 FTexShape*,
    // +0x14 bolt axis.
    float* Seg = *reinterpret_cast<float**>(B + 0xe0);
    for (int I = 0; I < *reinterpret_cast<int*>(B + 0xdc); ++I, Seg += 8)
    {
        float A = BaseAlpha;

        // 0x00508514..0x00508582: world point P and the camera-to-point vector V.
        float const P[3] = { ZonePos.x + Seg[0], ZonePos.y + Seg[1], ZonePos.z + Seg[2] };
        float const V[3] = { P[0] - CamPos.x, P[1] - CamPos.y, P[2] - CamPos.z };

        // 0x00508582..0x005085c8: view depth through InverseTransform (slot +0x3c); a point on
        // or behind the eye plane is skipped (`TEST AH,0x41; JNP`: less or equal).
        FVector PWorld;
        PWorld.X = P[0];
        PWorld.Y = P[1];
        PWorld.Z = P[2];
        FVector PView = {};
        AcquireNebulaMath()->InverseTransform(PView, View, PWorld);
        float const Depth = -PView.Z;
        if (Depth <= 0.0f)
            continue;

        // 0x005085ce..0x005086a4: facing fade, entered when +0xd8 is set or EdgeFade < 1.
        // Dot = ToCam . normalize(segment position) -- the segment's offset from the zone
        // centre, not its axis.
        int const Mode = *reinterpret_cast<int*>(B + 0xd8);
        if (Mode != 0 || EdgeFade < 1.0f)
        {
            float const Sx = Seg[0];
            float const Sy = Seg[1];
            float const Sz = Seg[2];
            float const Inv = 1.0f / sqrtf(Sx * Sx + Sz * Sz + Sy * Sy);
            float const Dot = ToCam[0] * (Sx * Inv) + ToCam[2] * (Sz * Inv) + ToCam[1] * (Sy * Inv);
            if (Mode == 0)
            {
                if (Dot < 0.1f)
                    A = EdgeFade * BaseAlpha;
            }
            else
            {
                if (Dot < 0.0f)
                    continue;
                if (Dot < 0.05)
                    A = static_cast<float>(Dot * BaseAlpha * 20.0);
            }
        }

        // 0x005086a6..0x00508781: Q = V x axis and H = Q x V, each scaled to the radius +0x0c.
        float const R = Seg[3];
        float const* const Axis = Seg + 5;
        float Qx = V[1] * Axis[2] - V[2] * Axis[1];
        float Qy = V[2] * Axis[0] - V[0] * Axis[2];
        float Qz = V[0] * Axis[1] - V[1] * Axis[0];
        float const QScale = R / sqrtf(Qx * Qx + Qz * Qz + Qy * Qy);
        Qx *= QScale;
        Qy *= QScale;
        Qz *= QScale;
        float Hx = Qy * V[2] - Qz * V[1];
        float Hy = Qz * V[0] - V[2] * Qx;
        float Hz = V[1] * Qx - Qy * V[0];
        float const HScale = R / sqrtf(Hz * Hz + Hy * Hy + Hx * Hx);
        Hx *= HScale;
        Hy *= HScale;
        Hz *= HScale;

        // 0x00508785..0x005087b1: nearer than the radius -> alpha scaled by Depth / R; below
        // 0.01 the bolt is skipped.
        if (Depth < R)
            A = (Depth / R) * A;
        if (A < 0.01)
            continue;

        // 0x005087b7..0x0050884d: colour +0xa8..+0xb0. With an alpha texture FUN_00421bd0
        // (Color4f); without, the additive path premultiplies through FUN_00421ad0 (Color3f).
        float const Cr = *reinterpret_cast<float*>(B + 0xa8);
        float const Cg = *reinterpret_cast<float*>(B + 0xac);
        float const Cb = *reinterpret_cast<float*>(B + 0xb0);
        if (bHasAlpha)
            Win3DColor4f(Cr, Cg, Cb, A);
        else
            Win3DColor3f(Cr * A, Cg * A, Cb * A);

        // 0x00508850..0x00508a4b: four corners, P+Q+H, P+Q-H, P-Q-H, P-Q+H.
        FTexShape const* const Shape = *reinterpret_cast<FTexShape* const*>(Seg + 4);
        Win3DTexCoord2f(Shape->U0, Shape->V0);
        Win3DVertex3f((P[0] + Qx) + Hx, (P[1] + Qy) + Hy, (P[2] + Qz) + Hz);
        Win3DTexCoord2f(Shape->U1, Shape->V0);
        Win3DVertex3f((P[0] + Qx) - Hx, (P[1] + Qy) - Hy, (P[2] + Qz) - Hz);
        Win3DTexCoord2f(Shape->U1, Shape->V1);
        Win3DVertex3f((P[0] - Qx) - Hx, (P[1] - Qy) - Hy, (P[2] - Qz) - Hz);
        Win3DTexCoord2f(Shape->U0, Shape->V1);
        Win3DVertex3f((P[0] - Qx) + Hx, (P[1] - Qy) + Hy, (P[2] - Qz) + Hz);
    }

    // 0x00508a82..0x00508ac2: End, render state 0x18 restored, then depth test ON, depth write
    // on, blending off, culling on.
    Win3DEnd();
    DALib::Pipeline->SetRenderState(D3DRS_ALPHAREF, SavedRs18);
    Win3DEnable(kWin3DDepthTest);
    Win3DSetDepthWrite(1);
    Win3DDisable(kWin3DBlend);
    Win3DEnable(kWin3DCullFace);

    // 0x00508ac7..0x00508ae6: the effect at +0xec (released by NebulaShutdown's FUN_004f7a90 at
    // 0x00506e35), its vtable slot +0x0c with 1 - +0x294.
    FEffectBase* const Effect = *reinterpret_cast<FEffectBase**>(B + 0xec);
    if (Effect != nullptr)
    {
        using FEffectSlot0C = void(__thiscall*)(FEffectBase*, float);
        Effect->SetIntensity(static_cast<float>(1.0 - *reinterpret_cast<float*>(B + 0x294)));
    }
}


void FNebulaEffect::Shutdown()
{
    FNebulaEffect* const Nebula = this;
    // FUN_00506cc0 (thiscall), 0x00506cc0..0x00506eaa. Nothing unless loaded (+0x2c0).
    if (Nebula->FlagLoaded == 0)
        return;

    unsigned char* const B = reinterpret_cast<unsigned char*>(Nebula);
    *reinterpret_cast<unsigned*>(B + 0xa0) = 0;

    void* Bolt = *reinterpret_cast<void**>(B + 0xe0);
    if (Bolt != nullptr)
        operator delete(Bolt);
    *reinterpret_cast<void**>(B + 0xe0) = nullptr;
    *reinterpret_cast<unsigned*>(B + 0xdc) = 0;

    // 0x00506ceb..0x00506d3d: every Clouds of the list is destroyed and freed (FUN_00506f00(1),
    // skipped for a null entry), then the list is cleared (FUN_00535980: each node unlinked,
    // freed, count decremented).
    FNebulaCloudsListNode* const Sentinel = Nebula->Clouds.Sentinel;
    FNebulaCloudsListNode* N = Sentinel->Next;
    while (N != Sentinel)
    {
        if (N->Clouds != nullptr)
            LocalNebulaCloudsDestroy(N->Clouds, 1);
        N = N->Next;
    }
    N = Sentinel->Next;
    while (N != Sentinel)
    {
        FNebulaCloudsListNode* const Next = N->Next;
        N->Prev->Next = N->Next;
        N->Next->Prev = N->Prev;
        operator delete(N);
        --Nebula->Clouds.Count;
        N = Next;
    }

    if (Nebula->ExclusionMgr != nullptr)
    {
        delete Nebula->ExclusionMgr;
        Nebula->ExclusionMgr = nullptr;
    }

    // 0x00506d5a..0x00506da7: every zone_shell of the +0x38 map is destroyed (FUN_00506eb0)
    // and freed, then the whole map is erased (FUN_00509d60 over [begin, end)).
    {
        FNebulaZoneShellMap& Shells = NebulaZoneShells(Nebula);
        for (FNebulaZoneShellMap::iterator It = Shells.begin(); It != Shells.end(); ++It)
        {
            FNebulaZoneShell* const Shell = It->second;
            if (Shell != nullptr)
            {
                NebulaZoneShellDestroy(Shell);
                delete Shell;
            }
        }
        Shells.clear();
    }

    // 0x00506dac: FUN_005526d0 on the lightning member at +0x100.
    LightningDisable(reinterpret_cast<float*>(B + 0x100));

    // 0x00506db7..0x00506e6b: the effect / sound / light companion at +0xe4 is stopped (the
    // inlined FUN_004fcef0 prelude: effect vt+4, sound pose (-1, identity) + vt+0x60, light
    // FUN_005379b0 + vt+8) and released (the inlined FUN_00502d90). Nothing in Nebula.cpp ever
    // fills it (only the ctor, this function and the dtor touch +0xe4..+0xf0).
    CliEffectCompanionArmStop(B + 0xe4);
    CliEffectCompanionClear(B + 0xe4);

    // 0x00506e71..0x00506e8f: the ambient light is switched off when bit 3 is set and
    // released. +0x270 is NOT cleared -- only a later Load with a positive period rewrites it.
    FGameLight* const Ambient = *reinterpret_cast<FGameLight**>(B + 0x270);
    if (Ambient != nullptr)
    {
        if ((Ambient->Flags & 0x08) != 0)
            GameLightVirtualDisable(Ambient);
        GameLightRelease(*reinterpret_cast<FGameLight**>(B + 0x270));
    }

    // 0x00506e94..0x00506e9f: FUN_00425a20 on the ShapeDB (+0x04), zone and loaded flag
    // cleared.
    NebulaShapes(Nebula)->Clear();
    Nebula->Zone = nullptr;
    Nebula->FlagLoaded = 0;
}

void NebulaShutdown(FNebulaEffect* Nebula)
{
    Nebula->FNebulaEffect::Shutdown();
}

FNebulaEffect::~FNebulaEffect()
{
    FNebulaEffect* const Nebula = this;
    // FUN_00504cf0 (thiscall), 0x00504cf0..0x00504e5d: the member destructors in reverse order
    // after Shutdown.
    NebulaShutdown(Nebula);

    unsigned char* const B = reinterpret_cast<unsigned char*>(Nebula);

    // 0x00504d25..0x00504d30: ~lightning (+0x100) is FUN_005526d0.
    LightningDisable(reinterpret_cast<float*>(B + 0x100));

    // Clouds, companion, zone-shell map and ShapeDB unwind in reverse member order.
}

void NebulaDestruct(FNebulaEffect* Nebula)
{
    Nebula->FNebulaEffect::~FNebulaEffect();
}
