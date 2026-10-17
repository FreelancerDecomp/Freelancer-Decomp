// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "cli_asteroid.h"

#include "asteroid_billboard.h"
#include "cli_asteroid_dynamic.h"
#include "cli_asteroid_med.h"
#include "cli_nebula.h" // G_CloudsMath (DAT_00679860), read by the band rings (FUN_0054c8b0)
#include "DALib.h"
#include "common_archetype.h"
#include "common_csys.h"
#include "common_id.h"
#include "common_misc.h"
#include "common_net.h"
#include "common_universe.h"
#include "dacom_iids.h"
#include "engine_light.h"
#include "game_options.h"
#ifdef FREELANCER_RP9
#include "nebula_volume.h" // NebulaVolume::AsteroidSpawnReach (port-only Direct3D 9 nebula)
#endif
#include "render/RenderPipeline.h"
#include "render/mesh_iface.h"
#include "render/rendcomp.h"
#include "render/shading.h"
#include "space_camera.h"
#include "space_scene_obj_draw.h"
#include "star_sys.h"
#include "win3d_immediate.h"
#include "win3d_render.h"
#include "x86math.h"

#include <list>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <windows.h>

// FUN_00524880's inlined AsteroidExclusionZoneMgr::set_max_cliasteroid_exclusion_distance
// (0x00524932..0x00524936): FLD ST0 / FMUL ST1 squares the distance still on the x87 stack,
// FSTP float [mgr+0x14] rounds once. Befriended by the class (common_net.h).
struct FCliAsteroidExclusionInline
{
    static void SetMaxCliDist(Exclusion::AsteroidExclusionZoneMgr* Mgr, double Dist)
    {
        Mgr->MaxCliDist = static_cast<float>(Dist * Dist);
    }

#ifdef FREELANCER_RP9
    // Port-only (CliAsteroidTickOne's extended spawn distance under the volumetric nebula):
    // the squared value as stored, read and put back around one near-field draw.
    static float GetMaxCliDistSq(Exclusion::AsteroidExclusionZoneMgr const* Mgr)
    {
        return Mgr->MaxCliDist;
    }

    static void SetMaxCliDistSq(Exclusion::AsteroidExclusionZoneMgr* Mgr, float DistSq)
    {
        Mgr->MaxCliDist = DistSq;
    }
#endif
};

namespace
{
    // Retail DAT_00613844 — graphics/detail scalar (default 1.0f in .data).
    float& AsteroidDetailScalar()
    {
        static float s_Detail = 1.0f;
        return s_Detail;
    }

    // DAT_006753c0 / c4 / c8 — a VC6 std::list<CliAsteroid*> (allocator byte, head node, size):
    // XC LAB_0051fe10 (initializer slot .data:0060fa74) builds the self-linked head
    // (operator new(0xc)) with size 0 and registers LAB_0051fe50 with atexit to free the nodes.
    // Fields are appended at the tail and walked head->next forward (FUN_00520130 /
    // FUN_005200b0), as this std::list is.
    //
    // LIFETIME. The list is a namespace-scope static on purpose, not a function-local one: it
    // is constructed during CRT start-up, like the XC initializer, so its destructor is
    // registered with atexit before WinMain registers ClientAtExitTeardown (LAB_005b2190,
    // 0x005b3101). atexit runs in reverse order, so the teardown's ShutdownSpaceState ->
    // StarSysLeave -> CliAsteroidDestroyAll (0x00554b7e) still sees a live list. A function-local
    // static was first constructed in game, after that registration, and was therefore already
    // destroyed when the teardown walked it.
    std::list<FCliAsteroid*> GCliAsteroidFields;

    std::list<FCliAsteroid*>& Registry()
    {
        return GCliAsteroidFields;
    }

    void CliAsteroidFillMeshDesc(FRpVertexSrcDesc* Desc, void* Verts, unsigned short VertCount);
    void CliAsteroidMergeCubeMesh(FCliAsteroid* Field, int Lod, char const* FieldIniRel);

    // The tint clamp FUN_0054d970 / FUN_0054fee0 inline six times: FCOMP 1.0f (DAT_005c75dc),
    // TEST AH,5 / JP. Only "less" (C0 alone, odd parity) keeps V; greater, equal and unordered
    // (even parity) store 1.0. There is no lower bound.
    float ClampTintUpper(float V)
    {
        if (V < 1.0f)
            return V;
        return 1.0f;
    }

    int FloatToIntTrunc(float V)
    {
        return static_cast<int>(V);
    }

    // ---- Band blob dword accessors (FUN_0054c580 / 0054c630 / 0054d970 / 0054dab0) ----

    float* BandF(unsigned char* Blob, int DwordIndex)
    {
        return reinterpret_cast<float*>(Blob) + DwordIndex;
    }

    unsigned* BandU(unsigned char* Blob, int DwordIndex)
    {
        return reinterpret_cast<unsigned*>(Blob) + DwordIndex;
    }

    // FUN_0054c580 (__thiscall on the 0x84 blob), in listing order. It writes nothing else:
    // +0x08, +0x7c, +0x80 and the pad bytes +0x75..+0x77 keep whatever the storage held --
    // BandParseIni (FUN_0054c630) sets +0x08 / +0x58 / +0x7c / +0x80 when a section is read.
    void InitBandBlob(unsigned char* Blob)
    {
        *BandF(Blob, 3) = 1.0f;         // +0x0c..+0x2c = 0x3f800000
        *BandF(Blob, 4) = 1.0f;
        *BandF(Blob, 5) = 1.0f;
        *BandF(Blob, 6) = 1.0f;
        *BandF(Blob, 7) = 1.0f;
        *BandF(Blob, 8) = 1.0f;
        *BandF(Blob, 9) = 1.0f;
        *BandF(Blob, 10) = 1.0f;
        *BandF(Blob, 11) = 1.0f;
        *BandU(Blob, 0xc) = 0;          // +0x30
        *BandU(Blob, 0xd) = 0;          // +0x34
        *BandU(Blob, 0xe) = 0;          // +0x38
        *BandU(Blob, 0) = 0;            // +0x00
        *BandU(Blob, 1) = 0;            // +0x04
        *BandU(Blob, 0xf) = 0;          // +0x3c
        *BandU(Blob, 0x11) = 0;         // +0x44
        *BandF(Blob, 0x12) = 1000.0f;   // +0x48 0x447a0000
        *BandF(Blob, 0x19) = 1500.0f;   // +0x64 0x44bb8000
        *BandF(Blob, 0x1a) = 2500.0f;   // +0x68 0x451c4000
        *BandF(Blob, 0x1b) = 100000.0f; // +0x6c 0x47c35000
        *BandF(Blob, 0x1c) = 130000.0f; // +0x70 0x47fde800
        *BandU(Blob, 0x1e) = 0;         // +0x78
        Blob[0x1d * 4] = 0;             // byte +0x74
        *BandF(Blob, 0x10) = 1.0f;      // +0x40
        *BandU(Blob, 0x13) = 1;         // +0x4c
        *BandU(Blob, 0x18) = 0;         // +0x60
        *BandU(Blob, 0x16) = 0;         // +0x58
        *BandU(Blob, 0x14) = 0;         // +0x50
        *BandU(Blob, 0x17) = 0;         // +0x5c
        *BandF(Blob, 0x15) = 1.0f;      // +0x54
    }

    // FUN_0054d970(band, diffuse_tint, ambient_tint)
    void BandApplyTints(unsigned char* Blob, float const* Diffuse, float const* Ambient)
    {
        float* Cs0 = BandF(Blob, 9);
        float* OutD = BandF(Blob, 3);
        float* OutA = BandF(Blob, 6);
        OutD[0] = ClampTintUpper(Cs0[0] * Diffuse[0]);
        OutD[1] = ClampTintUpper(Cs0[1] * Diffuse[1]);
        OutD[2] = ClampTintUpper(Cs0[2] * Diffuse[2]);
        OutA[0] = ClampTintUpper(Cs0[0] * Ambient[0]);
        OutA[1] = ClampTintUpper(Cs0[1] * Ambient[1]);
        OutA[2] = ClampTintUpper(Cs0[2] * Ambient[2]);
    }

    // FUN_0054dab0
    void BandSetAmbientIncrease(unsigned char* Blob, float const* Rgb)
    {
        *BandF(Blob, 0xc) = Rgb[0];
        *BandF(Blob, 0xd) = Rgb[1];
        *BandF(Blob, 0xe) = Rgb[2];
    }

    // FUN_0054c8b0 — ExclusionBand / [Band] ring mesh (thiscall on 0x84 blob).
    void BandBuildExclusionRings(unsigned char* Blob);

    // FUN_0054c630 — [Band] / ExclusionBand value keys into 0x84 blob; `shape` is looked up in
    // the field's own ShapeDB through FUN_00512660 (cmn\ShapeDB.h:0xf2 on a miss).
    void BandParseIni(unsigned char* Blob, INI_Reader& Reader, FShapeDB* Shapes)
    {
        Blob[0x7c] = 0;                 // 0x0054c63d: BYTE +0x7c (the bytes after it untouched)
        *BandU(Blob, 0x20) = 0;         // +0x80
        *BandF(Blob, 2) = 1.0f;         // +0x08
        *BandU(Blob, 0x16) = 1;         // +0x58

        if (!Reader.read_value())
            return;

        do
        {
            if (Reader.is_value("shape"))
            {
                char const* Name = Reader.get_value_string();
                FTexShape const* Shape = Shapes->Find(Name);
                *reinterpret_cast<FTexShape const**>(Blob) = Shape;
            }
            else if (Reader.is_value("texture_aspect"))
            {
                // 0x0054c6fc: FMUL qword DAT_005e0ac0 (1.15 as a double), FSTP float +0x40.
                *BandF(Blob, 0x10) =
                    static_cast<float>(static_cast<double>(Reader.get_value_float(0)) * 1.15);
            }
            else if (Reader.is_value("height"))
            {
                *BandF(Blob, 0x12) = Reader.get_value_float(0);
            }
            else if (Reader.is_value("offset_dist"))
            {
                Blob[0x1d * 4] = 0;
                *BandF(Blob, 0x1e) = Reader.get_value_float(0);
            }
            else if (Reader.is_value("offset_percent"))
            {
                Blob[0x1d * 4] = 1;
                *BandF(Blob, 0x1e) = Reader.get_value_float(0);
            }
            else if (Reader.is_value("color_shift"))
            {
                *BandF(Blob, 9) = Reader.get_value_float(0);
                *BandF(Blob, 10) = Reader.get_value_float(1);
                *BandF(Blob, 11) = Reader.get_value_float(2);
            }
            else if (Reader.is_value("fade"))
            {
                *BandF(Blob, 0x19) = Reader.get_value_float(0);
                *BandF(Blob, 0x1a) = Reader.get_value_float(1);
                *BandF(Blob, 0x1b) = Reader.get_value_float(2);
                *BandF(Blob, 0x1c) = Reader.get_value_float(3);
            }
            else if (Reader.is_value("vert_increase"))
            {
                int V = FloatToIntTrunc(Reader.get_value_float(0));
                if (V < 1)
                    V = 1;
                *BandU(Blob, 0x13) = static_cast<unsigned>(V);
            }
            else if (Reader.is_value("render_parts"))
            {
                int V = FloatToIntTrunc(Reader.get_value_float(0));
                if (V < 1)
                    V = 1;
                *BandU(Blob, 0x16) = static_cast<unsigned>(V);
            }
            else if (Reader.is_value("cull_mode"))
            {
                *BandU(Blob, 0x14) = static_cast<unsigned>(FloatToIntTrunc(Reader.get_value_float(0)));
            }
            else if (Reader.is_value("ambient_intensity"))
            {
                *BandF(Blob, 2) = Reader.get_value_float(0);
            }
        } while (Reader.read_value());
    }

    // FUN_0054c600 (thiscall) -- free the ring particle (+0x3c) and spoke (+0x60) buffers of a
    // band blob; +0x3c/+0x44 and +0x60/+0x58 = 0. No test of the blob: its callers
    // (0x0052067a / 0x00520938 after a null test, 0x005207b0 on the embedded +0xfc band) never
    // pass null. operator delete (FUN_005b7e1d) takes null.
    void BandFreeRingMesh(unsigned char* Blob)
    {
        operator delete(*reinterpret_cast<void**>(Blob + 0x3c));
        *reinterpret_cast<void**>(Blob + 0x3c) = nullptr;
        *BandU(Blob, 0x11) = 0;
        operator delete(*reinterpret_cast<void**>(Blob + 0x60));
        *reinterpret_cast<void**>(Blob + 0x60) = nullptr;
        *BandU(Blob, 0x16) = 0;
    }

    // FUN_00520850 0x00520904..0x00520978: with a non-empty exclusion band vector (+0x184),
    // every non-null band gets FUN_0054c600 and operator delete, then the vector is erased
    // (FUN_00527820 copy + IDPMsgHandler::OnReceive range destroy, end = begin). The storage
    // is kept.
    void BandVectorClear(FCliAsteroid* This)
    {
        if (This->BandBegin != nullptr && This->BandEnd - This->BandBegin > 0)
        {
            for (unsigned char** It = This->BandBegin; It != This->BandEnd; ++It)
            {
                unsigned char* const Blob = *It;
                if (Blob != nullptr)
                {
                    BandFreeRingMesh(Blob);
                    operator delete(Blob);
                }
            }
            This->BandEnd = This->BandBegin;
        }
    }

    void BandVectorPush(FCliAsteroid* This, unsigned char* Blob)
    {
        size_t Count = 0;
        size_t Cap = 0;
        if (This->BandBegin != nullptr)
        {
            Count = static_cast<size_t>(This->BandEnd - This->BandBegin);
            Cap = static_cast<size_t>(This->BandCap - This->BandBegin);
        }
        if (Count + 1 > Cap)
        {
            size_t NewCap = Cap == 0 ? 4 : Cap * 2;
            unsigned char** NewBuf =
                static_cast<unsigned char**>(operator new(sizeof(unsigned char*) * NewCap));
            if (This->BandBegin != nullptr && Count != 0)
                memcpy(NewBuf, This->BandBegin, Count * sizeof(unsigned char*));
            operator delete(This->BandBegin);
            This->BandBegin = NewBuf;
            This->BandEnd = NewBuf + Count;
            This->BandCap = NewBuf + NewCap;
        }
        *This->BandEnd = Blob;
        ++This->BandEnd;
    }

    // ---- FCliDynamicAsteroid 0xE8 (FUN_005256a0 / 00525760 / 00525800 keys) ----

    void DynVectorPush(FCliAsteroid* This, FCliDynamicAsteroid* Dyn)
    {
        size_t Count = 0;
        size_t Cap = 0;
        if (This->DynBegin != nullptr)
        {
            Count = static_cast<size_t>(This->DynEnd - This->DynBegin);
            Cap = static_cast<size_t>(This->DynCap - This->DynBegin);
        }
        if (Count + 1 > Cap)
        {
            size_t NewCap = Cap == 0 ? 4 : Cap * 2;
            void** NewBuf = static_cast<void**>(operator new(sizeof(void*) * NewCap));
            if (This->DynBegin != nullptr && Count != 0)
                memcpy(NewBuf, This->DynBegin, Count * sizeof(void*));
            operator delete(This->DynBegin);
            This->DynBegin = NewBuf;
            This->DynEnd = NewBuf + Count;
            This->DynCap = NewBuf + NewCap;
        }
        *This->DynEnd = Dyn;
        ++This->DynEnd;
    }

    // The manager functions (FUN_005256a0 / 00525760 / 00525800 / 005267b0) live in
    // cli_asteroid_dynamic.cpp.

    // FUN_00520850 0x0052097b..0x005209e8: with a non-empty dynamic asteroid vector (+0x220),
    // every non-null manager gets FUN_00525760 and operator delete, then the vector is erased
    // (end = begin). The storage is kept.
    void DynVectorClear(FCliAsteroid* This)
    {
        if (This->DynBegin != nullptr && This->DynEnd - This->DynBegin > 0)
        {
            for (void** It = This->DynBegin; It != This->DynEnd; ++It)
            {
                void* const Dyn = *It;
                if (Dyn != nullptr)
                {
                    CliDynamicAsteroidDestroy(static_cast<FCliDynamicAsteroid*>(Dyn));
                    operator delete(Dyn);
                }
            }
            This->DynEnd = This->DynBegin;
        }
    }

    // FUN_00520850 0x005211e1..0x0052126d -- a new manager's tints from the field's:
    // diffuse (+0xbc) = color_shift * the field's diffuse (+0xb4) through FUN_00422b80,
    // ambient (+0xc8) = FUN_00522000(color_shift, the field's ambient +0xc0), then
    // FUN_00521fa0 on the diffuse and on the ambient (clamped to 1.0 from above only).
    void CliDynamicAsteroidApplyParentTints(FCliDynamicAsteroid* Dyn, FCliAsteroid* Parent)
    {
        float Diffuse[3];
        Diffuse[0] = Parent->TintB4 * Dyn->ColorShift0;
        Diffuse[1] = Dyn->ColorShift1 * Parent->TintB8;
        Diffuse[2] = Dyn->ColorShift2 * Parent->TintBC;
        Dyn->TintedDiffuse0 = Diffuse[0];
        Dyn->TintedDiffuse1 = Diffuse[1];
        Dyn->TintedDiffuse2 = Diffuse[2];

        float Ambient[3];
        CliAsteroidMulColor(Ambient, &Dyn->ColorShift0, &Parent->TintC0);
        Dyn->TintedAmbient0 = Ambient[0];
        Dyn->TintedAmbient1 = Ambient[1];
        Dyn->TintedAmbient2 = Ambient[2];

        CliAsteroidClampColorToOne(&Dyn->TintedDiffuse0);
        CliAsteroidClampColorToOne(&Dyn->TintedAmbient0);
    }

    // ---- Billboard block (FUN_00550000 / 0054dbe0 / 0054fee0 / 0054de10) ----

    void BillboardEnsureRocks(FAsteroidBillboardBlock* Bb);

    // FUN_00550000 (thiscall, RET 4) — the two quality scalars, then FUN_0054de10 (0x00550034),
    // which rebuilds the rock array at the new QualityA / QualityB. From the [AsteroidBillboards]
    // header (before FUN_0054dbe0, Flag7C still 0, so the rebuild only frees) and from the detail
    // slider FUN_00524880.
    void BillboardSetQuality(FAsteroidBillboardBlock* Bb, float Detail)
    {
        // 0x00550000..00550014: (1 - d) * 0.25 (DAT_005c94f8) + d.
        Bb->QualityA = (1.0f - Detail) * 0.25f + Detail;
        // 0x00550017..0055002f: d*d kept on the FPU, (1 - d*d) * 0.75 (DAT_005c8930) + d*d.
        double const DetailSq = static_cast<double>(Detail) * Detail;
        Bb->QualityB = static_cast<float>((1.0 - DetailSq) * 0.75 + DetailSq);
        BillboardEnsureRocks(Bb);
    }

    // FUN_0054feb0(this=Billboard, a, b) — order (min,max) into Float44/Float40.
    // Call sites 005214e2 / 00524908: a = FillDist*Scale230,
    // b = CubeSize*1.25 + FillDist*Scale230. Listing: FCOM; if a < b then
    // +0x44=a, +0x40=b else +0x40=a, +0x44=b → Float40=max, Float44=min.
    void BillboardSetFadePlanes(FAsteroidBillboardBlock* Bb, float A, float B)
    {
        if (A < B)
        {
            Bb->Float44 = A;
            Bb->Float40 = B;
        }
        else
        {
            Bb->Float40 = A;
            Bb->Float44 = B;
        }
    }

    // FUN_0054fee0(bb, &TintB4, &TintC0)
    void BillboardApplyTints(FAsteroidBillboardBlock* Bb, float const* Diffuse, float const* Ambient)
    {
        Bb->TintedDiffuse0 = ClampTintUpper(Bb->ColorShift0 * Diffuse[0]);
        Bb->TintedDiffuse1 = ClampTintUpper(Bb->ColorShift1 * Diffuse[1]);
        Bb->TintedDiffuse2 = ClampTintUpper(Bb->ColorShift2 * Diffuse[2]);
        Bb->TintedAmbient0 = ClampTintUpper(Ambient[0]);
        Bb->TintedAmbient1 = ClampTintUpper(Ambient[1]);
        Bb->TintedAmbient2 = ClampTintUpper(Ambient[2]);
    }

    // FUN_0054dbe0 — [AsteroidBillboards] values into +0x19C block; `shape` is found in the
    // field's ShapeDB passed as the second argument (00521041, inline find at 0054dc38).
    void BillboardParseIni(FAsteroidBillboardBlock* Bb, INI_Reader& Reader, FShapeDB* Shapes)
    {
        Bb->FadeDistPercent = 0.3f;
        Bb->Flag7C = 1;

        if (!Reader.read_value())
            return;

        do
        {
            if (Reader.is_value("shape"))
            {
                char const* Name = Reader.get_value_string();
                FTexShape const* Shape = Shapes->Find(Name);
                Bb->Shape = Shape;
            }
            else if (Reader.is_value("count"))
            {
                Bb->Count = FloatToIntTrunc(Reader.get_value_float(0));
            }
            else if (Reader.is_value("size"))
            {
                float A = Reader.get_value_float(0);
                float B = Reader.get_value_float(1);
                if (B < A)
                {
                    float T = A;
                    A = B;
                    B = T;
                }
                Bb->SizeMin = A;
                Bb->SizeMax = B;
            }
            else if (Reader.is_value("start_dist"))
            {
                Bb->StartDist = Reader.get_value_float(0);
            }
            else if (Reader.is_value("fade_dist_percent"))
            {
                float V = Reader.get_value_float(0);
                if (V > 1.0f)
                    V = 1.0f;
                else if (V < 0.0f)
                    V = 0.0f;
                Bb->FadeDistPercent = V;
            }
            else if (Reader.is_value("ambient_intensity"))
            {
                Bb->AmbientIntensity = Reader.get_value_float(0);
            }
            else if (Reader.is_value("color_shift"))
            {
                Bb->ColorShift0 = Reader.get_value_float(0);
                Bb->ColorShift1 = Reader.get_value_float(1);
                Bb->ColorShift2 = Reader.get_value_float(2);
            }
        } while (Reader.read_value());
    }

    // Retail rand() returns 0..0x7fff; DAT_005caeec is 1/32768.
    unsigned char BillboardRandomBool()
    {
        // FILD rand / FMUL DAT_005caeec / FCOMP 0.5 / TEST AH,0x41 / JNZ -> 0; above 0.5 -> 1.
        double const Unit = static_cast<double>(rand()) * 3.0517578125e-05;
        return Unit > 0.5 ? 1 : 0;
    }

    // FUN_0054de10 (thiscall on the billboard block, no arguments) — rebuild the rock array.
    void BillboardEnsureRocks(FAsteroidBillboardBlock* Bb)
    {
        // 0x0054de16..0054de24: the old array freed; the pointer is not cleared (it is
        // overwritten below, or left alone when the block is not initialised).
        if (Bb->Rocks != nullptr)
            operator delete(Bb->Rocks);

        // 0x0054de27: Flag7C clear -> nothing else.
        if (Bb->Flag7C == 0)
            return;

        // 0x0054de32..0054de3f: FILD Count / FMUL QualityA / ftol.
        Bb->RockCount = static_cast<int>(static_cast<double>(Bb->Count) * Bb->QualityA);
        if (Bb->RockCount <= 0)
        {
            // 0x0054de44..0054de65: an error, not a return -- the allocation still happens.
            FDUMP(0x100001, "%s(%d) : *** ERROR: %s",
                  "E:\\FL\\Scratch\\Source\\Client\\Space\\SpaceTerrainRenderComp.cpp", 0x297,
                  "Billboard rock count is not > 0  If we have no rocks to draw, this instance "
                  "should not exist");
        }

        // 0x0054de68..0054de9e: operator new(RockCount << 5); Range = SizeMax - SizeMin and
        // InvCount = (float)(1.0 / RockCount) (DAT_005c89b8, a double).
        Bb->Rocks = static_cast<FAsteroidBillboardRock*>(
            operator new(static_cast<unsigned>(Bb->RockCount) << 5));
        float const Range = Bb->SizeMax - Bb->SizeMin;
        float const InvCount = static_cast<float>(1.0 / static_cast<double>(Bb->RockCount));

        int Index = 0;
        for (Index = 0; Index < Bb->RockCount; ++Index)
        {
            FAsteroidBillboardRock* const Rock = &Bb->Rocks[Index];
            Rock->Pos[0] = 0.0f;
            Rock->Pos[1] = 0.0f;
            Rock->Pos[2] = 0.0f;
            // 0x0054dec0..0054dee0: (rand * Range * 1/32768 + SizeMin) * 0.5, one FPU chain.
            Rock->HalfSize = static_cast<float>(
                (static_cast<double>(rand()) * Range * 3.0517578125e-05 + Bb->SizeMin) * 0.5);
            Rock->Alpha = 1.0f;
            Rock->bVisible = 1;
            // 0x0054def7..0054df9f: four rand() > 0.5 tests, in field order.
            Rock->bFlipU = BillboardRandomBool();
            Rock->bFlipV = BillboardRandomBool();
            Rock->bSwapAxes = BillboardRandomBool();
            Rock->bNegateApex = BillboardRandomBool();
            // 0x0054df98..0054dfb2: FILD Index / FMUL InvCount.
            Rock->Phase = static_cast<float>(static_cast<double>(Index) * InvCount);
        }

        // 0x0054dfc2..0054e002: only for start_dist > 0 (TEST AH,0x41 / JNZ skips <= 0 and
        // unordered). A = QualityB * start_dist, T = tan(DAT_005e0b00 = 35 degrees),
        // R = A*T / (1 - T): Float00 = R, Float04 = R + A, Float2C = R * FadeDistPercent.
        if (Bb->StartDist > 0.0f)
        {
            double const A = static_cast<double>(Bb->QualityB) * Bb->StartDist;
            double const T = tan(0.6108652353286743);
            double const R = A * T / (1.0 - T);
            Bb->Float00 = static_cast<float>(R);
            Bb->Float04 = static_cast<float>(R + A);
            Bb->Float2C = static_cast<float>(R * Bb->FadeDistPercent);
        }

        // 0x0054e004..0054e030: a shape whose FUN_00425640 frame-0 handle the pipeline does not
        // recognise (IsTexture, slot 47, non-zero) is dropped.
        if (Bb->Shape != nullptr)
        {
            void* const Handle = TexShapeGetGpuHandle(Bb->Shape, 0);
            if (DALib::Pipeline->IsTexture(Handle) != 0)
                Bb->Shape = nullptr;
        }
    }

    void ScaleRgb255(float* Out, float R, float G, float B)
    {
        Out[0] = R * (1.0f / 255.0f);
        Out[1] = G * (1.0f / 255.0f);
        Out[2] = B * (1.0f / 255.0f);
    }

    // FUN_00524880(this, detail) — always writes Scale230; Flag2C body is detail-slider recall.
    // No test of `this` (ECX is used as is).
    void CliAsteroidApplyDetail(FCliAsteroid* This, float Detail)
    {
        // 0x00524883..0x005248a7: FLD 1.0 / FSUB d; FST float keeps (1 - d) for the dyn block
        // below while the stack value goes on: * 0.65f (DAT_005db53c) + d, FSTP float.
        double const OneMinusDetail = 1.0 - static_cast<double>(Detail);
        float const OneMinusDetailF = static_cast<float>(OneMinusDetail);
        This->Scale230 = static_cast<float>(
            OneMinusDetail * static_cast<double>(0.65f) + static_cast<double>(Detail));
        if (This->Flag2C == 0 || This->CmnField == nullptr)
            return;

        // 0x005248c4..0x005248d0: FILD +0x24, FMUL float Scale230, ftol -> field +0x48.
        int const SyncedFill = static_cast<int>(
            static_cast<double>(This->FillDistAt24) * static_cast<double>(This->Scale230));
        This->CmnField->set_fill_dist(SyncedFill);

        if (This->Flag198 != 0)
        {
            // 0x005248dd..0x00524914: FUN_0054feb0(FillScaled, FadeFar) then FUN_00550000(d).
            // FillScaled is FSTP float; FadeFar = CubeSize * 1.25f (DAT_005c9804) + FillScaled
            // on the stack, FSTP float.
            float const FillScaled = static_cast<float>(
                static_cast<double>(This->FillDistAt24) * static_cast<double>(This->Scale230));
            float const FadeFar = static_cast<float>(
                static_cast<double>(This->CubeSize) * static_cast<double>(1.25f) +
                static_cast<double>(FillScaled));
            BillboardSetFadePlanes(&This->Billboard, FillScaled, FadeFar);
            BillboardSetQuality(&This->Billboard, Detail);
        }

        if (This->ExclusionMgr != nullptr)
        {
            // 0x00524919..0x00524939: retail inlines the setter -- FILD +0x24 * Scale230 * 1.25f,
            // squared on the stack (FLD ST0 / FMUL ST1), FSTP float into the manager's +0x14,
            // FSTP ST0 drops the unsquared value.
            double const CliDist = static_cast<double>(This->FillDistAt24) *
                                   static_cast<double>(This->Scale230) *
                                   static_cast<double>(1.25f);
            FCliAsteroidExclusionInline::SetMaxCliDist(This->ExclusionMgr, CliDist);
        }

        // 0x0052493b..0x00524a1b: every dynamic asteroid manager -- DetailScale =
        // (1 - d) * 0.5 + d and 1 / DetailScale (no guard), computed once for a non-empty
        // vector; then per manager the slots released (FUN_005267b0's loop, inlined), the
        // array freed, SlotCount = ftol(CountIni * DetailScale + 0.5) and a new zeroed array
        // (operator new even for 0 slots).
        if (This->DynBegin == This->DynEnd)
            return;
        // 0x00524953..0x0052496f: the stored float (1 - d) * 0.5f + d, FSTP float; then
        // 1.0 / that float, FSTP float.
        float const DynDetail = static_cast<float>(
            static_cast<double>(OneMinusDetailF) * static_cast<double>(0.5f) +
            static_cast<double>(Detail));
        float const InvDyn = static_cast<float>(1.0 / static_cast<double>(DynDetail));
        for (void** It = This->DynBegin; It != This->DynEnd; ++It)
        {
            FCliDynamicAsteroid* const Dyn = static_cast<FCliDynamicAsteroid*>(*It);
            CliDynamicAsteroidReleaseSlots(Dyn);
            operator delete(Dyn->SlotPtrs);
            Dyn->DetailScale = DynDetail;
            Dyn->SlotPtrs = nullptr;
            Dyn->InvDetailScale = InvDyn;
            Dyn->SlotCount =
                static_cast<int>(static_cast<double>(Dyn->CountIni) * DynDetail + 0.5f);
            Dyn->SlotPtrs = static_cast<FCliDynamicAsteroidGameobj**>(operator new(
                static_cast<unsigned>(Dyn->SlotCount) * sizeof(FCliDynamicAsteroidGameobj*)));
            for (int I = 0; I < Dyn->SlotCount; ++I)
                Dyn->SlotPtrs[I] = nullptr;
        }
    }

    // FUN_005201e0 (thiscall, no arguments). No test of `this`: its one caller (FUN_0051ff50)
    // tests operator new's result.
    // Stores in listing order. Retail writes no byte of +0x00 (Flag00), +0x30..+0x9f
    // (MeshDesc), +0xb0 (FieldB0) or the pads here; each is written before it is read:
    // Flag00 by FUN_00520850 (the field-ini load, = 0 before the section loop), MeshDesc by
    // FUN_0051feb0 and FieldB0 by the mesh build, both inside the same load.
    FCliAsteroid* CliAsteroidConstructRetail(FCliAsteroid* This)
    {
        // 0x005201f8..0x0052021a: the allocator bytes (+0x08, +0x09), byte +0x14 and
        // FUN_005274a0 -- the cube map at +0x08.
        CliAsteroidCubeMapConstruct(This);

        // 0x0052021f..0x00520256: +0xb4..+0xc8 = 1.0.
        This->TintB4 = 1.0f;
        This->TintB8 = 1.0f;
        This->TintBC = 1.0f;
        This->TintC0 = 1.0f;
        This->TintC4 = 1.0f;
        This->TintC8 = 1.0f;

        // 0x0052025c..0x00520267: FUN_004a2f10 -- the field's ShapeDB at +0xE0 (its tail store
        // +0xf4 = +0xe4 is part of the container's own construction).
        ShapeDBConstructAt(This->ShapeDbE0);

        // 0x00520278: FUN_0054c580 on +0xfc.
        InitBandBlob(This->BandBlob);

        // 0x0052027d..0x00520293: the band vector at +0x184 (allocator byte, begin/end/cap = 0).
        This->BandBegin = nullptr;
        This->BandEnd = nullptr;
        This->BandCap = nullptr;

        // 0x0052029b..0x005202b9: Up = (0, 1, 0), MaxLights (+0x1e4) = 4.
        This->Billboard.Up[0] = 0.0f;
        This->Billboard.Up[1] = 1.0f;
        This->Billboard.Up[2] = 0.0f;
        This->Billboard.MaxLights = 4;

        // 0x005202ae..0x005202df: FUN_00422b80(1, 1, 1) on +0x1e8, +0x1f4, +0x200.
        This->Billboard.TintedDiffuse0 = 1.0f;
        This->Billboard.TintedDiffuse1 = 1.0f;
        This->Billboard.TintedDiffuse2 = 1.0f;
        This->Billboard.TintedAmbient0 = 1.0f;
        This->Billboard.TintedAmbient1 = 1.0f;
        This->Billboard.TintedAmbient2 = 1.0f;
        This->Billboard.ColorShift0 = 1.0f;
        This->Billboard.ColorShift1 = 1.0f;
        This->Billboard.ColorShift2 = 1.0f;

        // 0x005202e4..0x00520376: the billboard block's scalars.
        This->Billboard.Float00 = 2500.0f;   // +0x19c 0x451c4000
        This->Billboard.StartDist = 1500.0f; // +0x1a4 0x44bb8000
        This->Billboard.Float04 = 4000.0f;   // +0x1a0 0x457a0000
        This->Billboard.Rocks = nullptr;     // +0x1a8
        This->Billboard.Count = 100;         // +0x1ac
        This->Billboard.RockCount = 100;     // +0x1b0
        This->Billboard.ZoneRef = nullptr;   // +0x1b4
        This->Billboard.DensityScale = 1.0f; // +0x1b8
        This->Billboard.Shape = nullptr;     // +0x1bc
        This->Billboard.SizeMin = 500.0f;    // +0x1c0 0x43fa0000
        This->Billboard.SizeMax = 100.0f;    // +0x1c4 0x42c80000
        This->Billboard.Float2C = 700.0f;    // +0x1c8 0x442f0000
        This->Billboard.FadeDistPercent = 0.3f; // +0x1cc 0x3e99999a
        This->Billboard.Float40 = 2000.0f;   // +0x1dc 0x44fa0000
        This->Billboard.Float44 = 1500.0f;   // +0x1e0
        This->Billboard.AmbientIntensity = 1.0f; // +0x20c
        This->Billboard.QualityA = 1.0f;     // +0x210
        This->Billboard.QualityB = 1.0f;     // +0x214
        This->Billboard.Flag7C = 0;          // +0x218

        // 0x0052037c..0x0052038e: the dynamic-asteroid vector at +0x220 (begin/end/cap = 0).
        This->DynBegin = nullptr;
        This->DynEnd = nullptr;
        This->DynCap = nullptr;

        // 0x00520396..0x005203d7: CreateAsteroidField(FUN_00554e90()) into +4; AddRef when
        // made, otherwise FDUMP 0x100001 (line 0x11e).
        CmnAsteroid::CAsteroidField* Field =
            CmnAsteroid::CreateAsteroidField(StarSysGetActiveSystemId());
        This->CmnField = Field;
        if (Field != nullptr)
        {
            Field->AddRef();
        }
        else
        {
            FDUMP(0x100001, "%s(%d) : *** ERROR: %s",
                  "E:\\FL\\Scratch\\Source\\Client\\Space\\CliAsteroid.cpp", 0x11e,
                  "NO cmn field created for client asteroid field");
        }

        // 0x005203da..0x00520411.
        This->CubeSize = 0x190;
        This->FillDistAt24 = 0x4b0;
        This->ScaleCC = 1.0f;
        This->Scale230 = 1.0f;
        This->Flag2C = 0;
        This->FlagD3 = 0;
        This->EmptyCubeFreq = 0.0f;
        This->FlagD0 = 0;
        This->FlagD1 = 0;
        This->FlagD2 = 0;

        // 0x00520417..0x00520439: FUN_00527310 erase(begin, end) on the just-built cube map --
        // nothing to erase.
        // 0x00520444..0x00520468.
        This->ExclusionMgr = nullptr;
        This->Material = nullptr;
        This->MeshVerts = nullptr;
        This->MeshIndices = nullptr;
        This->MeshIndexCount = 0;
        This->ZeroD4 = 0;
        This->FloatD8 = 100.0f;

        // 0x0052047a: FUN_005995c0 destroys the (empty) band range; 0x0052047f: +0x194 = 0.
        This->BandVertSum = 0;
        // 0x00520485..0x005204a8: FUN_00527820 + the range destroy on the (empty) dynamic
        // vector, end = begin.

        // 0x005204ac..0x005204c8.
        This->Zone = nullptr;
        This->FlagF8 = 0;
        This->Flag180 = 0;
        This->Flag198 = 0;
        This->Flag234 = 1;
        This->Flag21C = 1;

        return This;
    }

    // FUN_00520850 [Field] (0x00520ad2..0x00520e3e). Lod is the caller's local [ESP+0x30].
    void ParseFieldSection(FCliAsteroid* This, INI_Reader& Reader, int& Lod)
    {
        if (!Reader.read_value())
            return;

        float const KInv255 = 1.0f / 255.0f;

        do
        {
            if (Reader.is_value("cube_size"))
            {
                This->CubeSize = FloatToIntTrunc(Reader.get_value_float(0));
            }
            else if (Reader.is_value("fill_dist"))
            {
                This->FillDistAt24 = FloatToIntTrunc(Reader.get_value_float(0));
            }
            else if (Reader.is_value("LOD"))
            {
                // 0x00520b49..0x00520b56: ftol into the local [ESP+0x30], which the mesh merge
                // clamps and hands to the renderer (0x00521641..0x00521670).
                Lod = FloatToIntTrunc(Reader.get_value_float(0));
            }
            else if (Reader.is_value("tint_field"))
            {
                float R = Reader.get_value_float(0) * KInv255;
                float G = Reader.get_value_float(1) * KInv255;
                float B = Reader.get_value_float(2) * KInv255;
                This->TintB4 = R;
                This->TintB8 = G;
                This->TintBC = B;
                This->TintC0 = R;
                This->TintC4 = G;
                This->TintC8 = B;
            }
            else if (Reader.is_value("diffuse_color"))
            {
                This->TintB4 = Reader.get_value_float(0) * KInv255;
                This->TintB8 = Reader.get_value_float(1) * KInv255;
                This->TintBC = Reader.get_value_float(2) * KInv255;
            }
            else if (Reader.is_value("ambient_color"))
            {
                This->TintC0 = Reader.get_value_float(0) * KInv255;
                This->TintC4 = Reader.get_value_float(1) * KInv255;
                This->TintC8 = Reader.get_value_float(2) * KInv255;
            }
            else if (Reader.is_value("ambient_increase"))
            {
                This->FlagD0 = static_cast<unsigned char>(FloatToIntTrunc(Reader.get_value_float(0)));
                This->FlagD1 = static_cast<unsigned char>(FloatToIntTrunc(Reader.get_value_float(1)));
                This->FlagD2 = static_cast<unsigned char>(FloatToIntTrunc(Reader.get_value_float(2)));
            }
            else if (Reader.is_value("empty_cube_frequency"))
            {
                This->EmptyCubeFreq = Reader.get_value_float(0);
            }
            else if (Reader.is_value("max_alpha"))
            {
                This->ScaleCC = Reader.get_value_float(0);
            }
            else if (Reader.is_value("test_asteroid_placement"))
            {
                This->Flag00 = 1;
            }
            else if (Reader.is_value("contains_fog_zone"))
            {
                This->FlagD3 = 1;
            }
        } while (Reader.read_value());
    }
}

FCliAsteroid* CliAsteroidConstruct(FCliAsteroid* This)
{
    // FUN_005201e0.
    return CliAsteroidConstructRetail(This);
}

void CliAsteroidLoadFieldIni(FCliAsteroid* This, char const* FieldIniRel)
{
    // FUN_00520850. No entry test of `This` or the file name.
    // 0x00520875..0x0052087c: the INI_Reader is constructed first.
    INI_Reader Reader;

    // 0x00520882..0x005208a3: GetFileSysPath(buf, file, DALib::Data).
    char FieldIniPath[MAX_PATH] = {};
    GetFileSysPath(FieldIniPath, FieldIniRel, DALib::Data);

    // 0x005208a9..0x005208de; [ESP+0x30] is the LOD local ([Field] "LOD", read by the merge).
    This->CubeSize = 0xfa;
    This->EmptyCubeFreq = 0.0f;
    int Lod = 0;
    This->ScaleCC = 1.0f;
    This->FlagF8 = 0;
    This->Flag198 = 0;
    This->Flag180 = 0;
    This->Flag00 = 0;

    // 0x005208e2..0x00520901: a field with no zone (+0x1c) is reported and loading goes on.
    if (This->Zone == nullptr)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: Asteroid field (%s) must be hooked up to a zone",
              "E:\\FL\\Scratch\\Source\\Client\\Space\\CliAsteroid.cpp", 0x187, FieldIniRel);
    }

    // 0x00520904..0x005209e8: the exclusion bands and the dynamic asteroid managers dropped.
    BandVectorClear(This);
    DynVectorClear(This);

    // 0x005209eb..0x00520a04: INI_Reader::open(path, false); a failure goes to the FDUMP at
    // 0x00521371 and then on to the band/billboard/mesh tail at 0x00521399.
    if (!Reader.open(FieldIniPath, false))
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: asteroid field ini file (%s) could not be opened",
              "E:\\FL\\Scratch\\Source\\Client\\Space\\CliAsteroid.cpp", 0x247, FieldIniRel);
    }
    else
    {
        // 0x00520a0a..0x00520a1d: +0x2c = 1, the reader's state saved for the common field.
        This->Flag2C = 1;

        INI_Reader::State Saved;
        Reader.get_state(Saved);

        // 0x00520a23..0x00521285: every header.
        while (Reader.read_header())
        {
            if (Reader.is_header("TexturePanels"))
            {
                // 0x00520a5f..0x00520ab7: every "file" -> FUN_00425b30(GetFullFilename(value),
                // no base filesystem) on the ShapeDB (+0xe0).
                while (Reader.read_value())
                {
                    if (!Reader.is_value("file"))
                        continue;
                    char const* Rel = Reader.get_value_string();
                    char const* Full = GetFullFilename(Rel);
                    ShapeDBAt(This->ShapeDbE0)->Load(Full, nullptr);
                }
            }
            else if (Reader.is_header("Field"))
            {
                ParseFieldSection(This, Reader, Lod);
            }
            else if (Reader.is_header("Band"))
            {
                // 0x00520e52..0x00520ee1: +0xf8 = 1, FUN_0054c630 / FUN_0054d970 /
                // FUN_0054dab0 on the field's own band (+0xfc).
                This->FlagF8 = 1;
                BandParseIni(This->BandBlob, Reader, ShapeDBAt(This->ShapeDbE0));
                float Diffuse[3] = {This->TintB4, This->TintB8, This->TintBC};
                float Ambient[3] = {This->TintC0, This->TintC4, This->TintC8};
                BandApplyTints(This->BandBlob, Diffuse, Ambient);
                float Inc[3];
                ScaleRgb255(Inc, static_cast<float>(This->FlagD0),
                            static_cast<float>(This->FlagD1), static_cast<float>(This->FlagD2));
                BandSetAmbientIncrease(This->BandBlob, Inc);
            }
            else if (Reader.is_header("ExclusionBand"))
            {
                // 0x00520efc..0x00520f0b: the section's state saved.
                INI_Reader::State ExState;
                Reader.get_state(ExState);

                // 0x00520f11..0x00520f56: operator new(0x84) + FUN_0054c580 (only on a
                // non-null block), pushed onto the +0x184 vector (FUN_00537260).
                unsigned char* Blob = static_cast<unsigned char*>(operator new(0x84));
                if (Blob != nullptr)
                    InitBandBlob(Blob);
                BandVectorPush(This, Blob);

                // 0x00520f5b..0x00520f75: the vector's last element (no test) gets
                // FUN_0054c630.
                unsigned char* const Last = This->BandEnd[-1];
                BandParseIni(Last, Reader, ShapeDBAt(This->ShapeDbE0));

                // 0x00520f7a..0x0052100f: the section again; every "zone" resolved with
                // Universe::get_zone on FUN_00554e90's system into +4, FDUMP when missing.
                Reader.set_state(ExState);
                while (Reader.read_value())
                {
                    if (!Reader.is_value("zone"))
                        continue;
                    char const* Nick = Reader.get_value_string();
                    Universe::IZone const* Z =
                        Universe::get_zone(StarSysGetActiveSystemId(), Nick);
                    if (Z == nullptr)
                    {
                        FDUMP(0x100001,
                              "%s(%d) : *** ERROR: Exband (exclusion zone band) linked to "
                              "missing Zone (%s)",
                              "E:\\FL\\Scratch\\Source\\Client\\Space\\CliAsteroid.cpp", 0x1ea,
                              Nick);
                    }
                    else
                    {
                        *reinterpret_cast<Universe::IZone const**>(Last + 4) = Z;
                    }
                }
            }
            else if (Reader.is_header("AsteroidBillboards"))
            {
                // 0x00521026..0x00521067: +0x198 = 1, FUN_00550000(DAT_00613844),
                // FUN_0054dbe0, FUN_0054fee0 on the billboards (+0x19c).
                This->Flag198 = 1;
                BillboardSetQuality(&This->Billboard, AsteroidDetailScalar());
                BillboardParseIni(&This->Billboard, Reader, ShapeDBAt(This->ShapeDbE0));
                float Diffuse[3] = {This->TintB4, This->TintB8, This->TintBC};
                float Ambient[3] = {This->TintC0, This->TintC4, This->TintC8};
                BillboardApplyTints(&This->Billboard, Diffuse, Ambient);
            }
            else if (Reader.is_header("DynamicAsteroids"))
            {
                // FUN_00520850 [DynamicAsteroids] (0x00521087..0x0052126d).
                INI_Reader::State SavedDyn;
                Reader.get_state(SavedDyn);

                // 0x0052109c..0x005210e5: operator new(0xe8), FUN_005256a0(CmnField), pushed
                // onto the +0x220 vector (FUN_00537260); the record is read back as the
                // vector's last.
                FCliDynamicAsteroid* const Dyn = CliDynamicAsteroidConstruct(
                    static_cast<FCliDynamicAsteroid*>(operator new(sizeof(FCliDynamicAsteroid))),
                    This->CmnField);
                DynVectorPush(This, Dyn);

                // 0x005210ea..0x00521129: DetailScale = (1 - d) * 0.5 + d (d = DAT_00613844),
                // stored with FST; InvDetailScale = 1 / the unrounded x87 value (FDIV ST0,ST1),
                // no guard.
                double const Detail = AsteroidDetailScalar();
                double const DetailScale = (1.0 - Detail) * 0.5 + Detail;
                Dyn->DetailScale = static_cast<float>(DetailScale);
                Dyn->InvDetailScale = static_cast<float>(1.0 / DetailScale);

                // 0x0052112b: FUN_00525800.
                CliDynamicAsteroidParseIni(Dyn, Reader);

                // 0x00521130..0x005211df: the section again; every "zone" resolved with
                // Universe::get_zone on FUN_00554e90's system into +0 (FDUMP ERROR 0x212 when
                // missing); no zone found -> the field's zone, when it has one.
                Reader.set_state(SavedDyn);
                unsigned char bZoneSet = 0;
                if (Reader.read_value())
                {
                    do
                    {
                        if (Reader.is_value("zone"))
                        {
                            char const* Nick = Reader.get_value_string();
                            Universe::IZone* Z = const_cast<Universe::IZone*>(
                                Universe::get_zone(StarSysGetActiveSystemId(), Nick));
                            if (Z == nullptr)
                            {
                                FDUMP(0x100001,
                                      "%s(%d) : *** ERROR: DynamicAsteroids, linked to missing "
                                      "Zone (%s)",
                                      "E:\\FL\\Scratch\\Source\\Client\\Space\\CliAsteroid.cpp",
                                      0x212, Nick);
                            }
                            else
                            {
                                Dyn->Zone = Z;
                                bZoneSet = 1;
                            }
                        }
                    } while (Reader.read_value());
                }
                if (bZoneSet == 0 && This->Zone != nullptr)
                    Dyn->Zone = This->Zone;

                // 0x005211e1..0x0052126d.
                CliDynamicAsteroidApplyParentTints(Dyn, This);
            }
        }

        // 0x0052128b..0x0052130b: the common field (+4) loads from the saved state, then gets
        // its fill distance (+0x48 = ftol(fill * +0x230), x87 product) and empty frequency
        // (+0x4c); +0xdc = its exclusion manager (+0x30). Without one: FDUMP.
        if (This->CmnField != nullptr)
        {
            Reader.set_state(Saved);
            This->CmnField->load(Reader, This->CubeSize);
            This->CmnField->set_fill_dist(static_cast<int>(
                static_cast<double>(This->FillDistAt24) * static_cast<double>(This->Scale230)));
            This->CmnField->set_empty_freq(This->EmptyCubeFreq);
            This->ExclusionMgr = This->CmnField->ExclusionMgr;
        }
        else
        {
            FDUMP(0x100001,
                  "%s(%d) : *** ERROR: client asteroid field (%s) must be connected to common "
                  "asteroid field before calling load",
                  "E:\\FL\\Scratch\\Source\\Client\\Space\\CliAsteroid.cpp", 0x22f, FieldIniRel);
        }

        // 0x0052130e..0x0052136f: with billboards, the count (+0x1ac) raised to the exclusion
        // manager's maximum, DensityScale (+0x1b8) = old count / new count (FIDIV, no zero
        // test) and the manager normalized; without a manager DensityScale = 1.
        if (This->Flag198 != 0)
        {
            int Use = This->Billboard.Count;
            if (This->ExclusionMgr != nullptr)
            {
                int const MaxBb = This->ExclusionMgr->get_max_billboard_count();
                if (Use < MaxBb)
                    Use = MaxBb;
                int const OldCount = This->Billboard.Count;
                This->Billboard.Count = Use;
                This->Billboard.DensityScale = static_cast<float>(
                    static_cast<double>(OldCount) / static_cast<double>(Use));
                This->ExclusionMgr->normalize_billboard_density(Use);
            }
            else
            {
                This->Billboard.DensityScale = 1.0f;
            }
        }
    }

    // 0x00521399..0x005213b9: the field's own band gets the zone (+0x100) and its rings
    // (FUN_0054c8b0); the spoke cursor starts at its spoke count (+0x154).
    int BandSpokeCursor = 0;
    if (This->FlagF8 != 0)
    {
        *reinterpret_cast<Universe::IZone**>(This->BandBlob + 4) = This->Zone;
        BandBuildExclusionRings(This->BandBlob);
        BandSpokeCursor = static_cast<int>(*BandU(This->BandBlob, 0x16));
    }
    // 0x005213bf..0x00521497: +0x194 = 0; with exclusion bands, +0x180 = 1 and every band gets
    // the tints, the ambient increase and its rings, its spoke base (+0x5c) and adds its spoke
    // count (+0x58) to +0x194. No test of the band pointer.
    This->BandVertSum = 0;
    if (This->BandBegin != nullptr && This->BandEnd != This->BandBegin)
    {
        This->Flag180 = 1;
        for (unsigned char** It = This->BandBegin; It != This->BandEnd; ++It)
        {
            unsigned char* const Blob = *It;
            float Diffuse[3] = {This->TintB4, This->TintB8, This->TintBC};
            float Ambient[3] = {This->TintC0, This->TintC4, This->TintC8};
            BandApplyTints(Blob, Diffuse, Ambient);
            float Inc[3];
            ScaleRgb255(Inc, static_cast<float>(This->FlagD0), static_cast<float>(This->FlagD1),
                        static_cast<float>(This->FlagD2));
            BandSetAmbientIncrease(Blob, Inc);
            BandBuildExclusionRings(Blob);
            *BandU(Blob, 0x17) = static_cast<unsigned>(BandSpokeCursor);
            int const SpokeN = static_cast<int>(*BandU(Blob, 0x16));
            BandSpokeCursor += SpokeN;
            This->BandVertSum += static_cast<unsigned>(SpokeN);
        }
    }

    // 0x0052149d..0x005214e2: with billboards, ZoneRef (+0x1b4) = the zone, FUN_0054de10, then
    // FUN_0054feb0(fill * +0x230 stored as float, cube * 1.25 + that float).
    if (This->Flag198 != 0)
    {
        This->Billboard.ZoneRef = This->Zone;
        BillboardEnsureRocks(&This->Billboard);
        float const FillScaled = static_cast<float>(
            static_cast<double>(This->FillDistAt24) * static_cast<double>(This->Scale230));
        float const FadeFar = static_cast<float>(
            static_cast<double>(This->CubeSize) * 1.25 + static_cast<double>(FillScaled));
        BillboardSetFadePlanes(&This->Billboard, FillScaled, FadeFar);
    }

    // 0x005214e7..0x00521516: the exclusion manager's CliAsteroid distance (fill * +0x230 *
    // 1.25, squared into +0x14) and its DynamicAsteroid distance (+0x18 = 10000 = 100^2) --
    // the two setters inlined.
    if (This->ExclusionMgr != nullptr)
    {
        This->ExclusionMgr->set_max_cliasteroid_exclusion_distance(static_cast<float>(
            static_cast<double>(This->FillDistAt24) * static_cast<double>(This->Scale230) * 1.25));
        This->ExclusionMgr->set_max_dynamicasteroid_exclusion_distance(100.0f);
    }

    // 0x00521518..0x00521b01: the archetype mesh merge into +0xa0..+0xac and the vertex source.
    CliAsteroidMergeCubeMesh(This, Lod, FieldIniRel);

    // 0x00521b0c..0x00521f96: the INI_Reader's destructor (Reader goes out of scope).
}

bool CliAsteroidCreateAndLoad(char const* FieldIniRel, char const* ZoneNickname)
{
    // FUN_0051ff50(file, zone) (cdecl).
    // 0x0051ff68..0x0051ffdf: operator new(0x238) + FUN_005201e0 (only on a non-null block),
    // pushed onto DAT_006753c0 (FUN_0042a7e0 / FUN_005999b0, count DAT_006753c8) before
    // anything can fail -- a field whose zone is missing stays on the list.
    FCliAsteroid* Obj = static_cast<FCliAsteroid*>(operator new(sizeof(FCliAsteroid)));
    if (Obj != nullptr)
        CliAsteroidConstruct(Obj);
    Registry().push_back(Obj);

    // 0x0051ffeb..0x0051fff2: Universe::get_non_const_zone(FUN_00554e90(), zone).
    Universe::IZone* Zone = Universe::get_non_const_zone(StarSysGetActiveSystemId(), ZoneNickname);
    if (Zone == nullptr)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: AsteroidField (%s) linked to missing Zone (%s)",
              "E:\\FL\\Scratch\\Source\\Client\\Space\\CliAsteroid.cpp", 0xc8, FieldIniRel,
              ZoneNickname);
        return false;
    }

    // 00520040: zone vt+0x18 (shape) must be 1 (sphere) or 2 (ellipsoid); anything else only
    // warns.
    unsigned const Shape = Zone->GetShape();
    if (Shape != Universe::ZoneShapeSphere && Shape != Universe::ZoneShapeEllipsoid)
    {
        FDUMP(0x100002,
              "%s(%d) : *** WARNING: Asteroid field (%s) hooked up to zone (%s) which is not a "
              "valid shape",
              "E:\\FL\\Scratch\\Source\\Client\\Space\\CliAsteroid.cpp", 0xd1, FieldIniRel,
              ZoneNickname);
    }

    // 0x00520073..0x0052007e: +0x1c = zone; the common field (+4), when there is one, gets
    // CmnAsteroid::CAsteroidField::set_zone.
    Obj->Zone = Zone;
    if (Obj->CmnField != nullptr)
        Obj->CmnField->set_zone(Zone);

    // 0x00520084..0x00520095: FUN_00524880(DAT_00613844), then FUN_00520850(file); AL = 1.
    CliAsteroidApplyDetail(Obj, AsteroidDetailScalar());
    CliAsteroidLoadFieldIni(Obj, FieldIniRel);
    return true;
}

unsigned CliAsteroidRegistryCount()
{
    // FUN_005201c0: MOV EAX,[DAT_006753c8] (the list count).
    return static_cast<unsigned>(Registry().size());
}

void CliAsteroidUpdateFields(void* Camera, float Dt)
{
    // FUN_00520130(cam, dt): with ASTEROIDS (option 0x21) on, every field on DAT_006753c4 gets
    // FUN_00522070(cam, dt) (0x0052015b), in list order. No null test of the field.
    if (GetGameOption(kGameOptAsteroids) == 0)
        return;
    std::list<FCliAsteroid*>& List = Registry();
    for (std::list<FCliAsteroid*>::iterator It = List.begin(); It != List.end(); ++It)
        CliAsteroidUpdate(*It, static_cast<FSpaceCamera*>(Camera), Dt);
}

void CliAsteroidDestroyAll()
{
    // FUN_005200b0. 0x005200c0..0x005200e0: every non-null field gets FUN_005205d0 and
    // operator delete; 0x005200e2..0x0052011b: every node unlinked and freed (the count at
    // DAT_006753c8 decremented per node); AL = 1 (no caller reads it). An empty list (first node
    // == sentinel, 0x005200b7..0x005200be) goes straight to the node loop at 0x005200e2, which
    // has nothing to free -- no early return.
    std::list<FCliAsteroid*>& List = Registry();
    for (std::list<FCliAsteroid*>::iterator It = List.begin(); It != List.end(); ++It)
    {
        FCliAsteroid* const Field = *It;
        if (Field != nullptr)
        {
            CliAsteroidDestruct(Field);
            operator delete(Field);
        }
    }
    List.clear();
}

void CliAsteroidDestruct(FCliAsteroid* This)
{
    // FUN_005205d0 (thiscall).
    // 0x005205f2..0x00520609: the common field released (import), +4 = 0.
    if (This->CmnField != nullptr)
    {
        This->CmnField->Release();
        This->CmnField = nullptr;
    }
    // 0x0052060c..0x0052061c: the material (+0xa0) released through vt+8, +0xa0 = 0.
    if (This->Material != nullptr)
    {
        static_cast<IDAComponent*>(This->Material)->Release();
        This->Material = nullptr;
    }
    // 0x00520622..0x0052064b: the merged vertices (+0xa4) and indices (+0xa8) freed and zeroed.
    operator delete(This->MeshVerts);
    This->MeshVerts = nullptr;
    operator delete(This->MeshIndices);
    This->MeshIndices = nullptr;
    // 0x00520651: FUN_00425a20 -- the ShapeDB (+0xe0) cleared.
    ShapeDBAt(This->ShapeDbE0)->Clear();
    // 0x00520656..0x00520695: every non-null exclusion band gets FUN_0054c600 and operator
    // delete; 0x00520697..0x005206c8: the band vector erased (FUN_00527820 / FUN_0051a5a0).
    for (unsigned char** It = This->BandBegin; It != This->BandEnd; ++It)
    {
        unsigned char* const Blob = *It;
        if (Blob != nullptr)
        {
            BandFreeRingMesh(Blob);
            operator delete(Blob);
        }
    }
    This->BandEnd = This->BandBegin;
    // 0x005206cb..0x00520751: every non-null dynamic asteroid manager gets FUN_00525760 and
    // operator delete, then the vector is erased.
    for (void** It = This->DynBegin; It != This->DynEnd; ++It)
    {
        FCliDynamicAsteroid* const Dyn = static_cast<FCliDynamicAsteroid*>(*It);
        if (Dyn != nullptr)
        {
            CliDynamicAsteroidDestroy(Dyn);
            operator delete(Dyn);
        }
    }
    This->DynEnd = This->DynBegin;
    // 0x00520754: the exclusion manager pointer (+0xdc) dropped.
    This->ExclusionMgr = nullptr;
    // 0x0052075a..0x00520769: the dynamic asteroid vector's storage freed, its three words 0.
    operator delete(This->DynBegin);
    This->DynBegin = nullptr;
    This->DynEnd = nullptr;
    This->DynCap = nullptr;
    // 0x0052076c..0x0052077e: the billboard rocks (+0x1a8) freed; Count (+0x1ac) and ZoneRef
    // (+0x1b4) 0. The Rocks pointer itself is left as it was.
    operator delete(This->Billboard.Rocks);
    This->Billboard.Count = 0;
    This->Billboard.ZoneRef = nullptr;
    // 0x00520784..0x005207a5: the band vector's storage freed, its three words 0.
    operator delete(This->BandBegin);
    This->BandBegin = nullptr;
    This->BandEnd = nullptr;
    This->BandCap = nullptr;
    // 0x005207b0: FUN_0054c600 on the field's own band (+0xfc).
    BandFreeRingMesh(This->BandBlob);
    // 0x005207b5..0x005207f3: the ShapeDB's destructor (erase all, head and storage freed).
    ShapeDBDestructAt(This->ShapeDbE0);
    // 0x005207f6..0x00520834: the cube map's destructor (FUN_00527310 and the head freed).
    CliAsteroidCubeMapDestruct(This);
}

void CliAsteroidClampColorToOne(float* Rgb)
{
    // FUN_00521fa0 (cdecl). Each component: FCOMP 1.0, TEST AH,5 / JP -- taken when the value
    // is >= 1.0 or unordered, which stores 1.0; otherwise the value is stored back unchanged.
    // Nothing is clamped from below.
    for (int Index = 0; Index < 3; ++Index)
    {
        if (!(Rgb[Index] < 1.0f))
            Rgb[Index] = 1.0f;
    }
}

float* CliAsteroidMulColor(float* Out, float const* A, float const* B)
{
    // FUN_00522000 (cdecl): the products on the FPU (z, y, x), stored x, y, z; returns Out.
    float const Z = A[2] * B[2];
    float const Y = A[1] * B[1];
    float const X = A[0] * B[0];
    Out[0] = X;
    Out[1] = Y;
    Out[2] = Z;
    return Out;
}

I3DMathEngine* CliAsteroidMathEngine()
{
    // DAT_006753d8: tested and, when null, filled from FUN_005b73e0 at each use (0x0052183a,
    // 0x00521881, 0x00523fab, 0x005244f7, 0x0052643c).
    static I3DMathEngine* s_Math = nullptr;
    if (s_Math == nullptr)
        s_Math = Acquire3DMathEngine();
    return s_Math;
}

void CliAsteroidApplyGlobalDetail(float Detail)
{
    // LAB_005267f0 -- the Asteroids perf option (IDS 0x57c): DAT_00613844 = Detail,
    // FUN_0054bd50 (the active camera's cut flag, +0xbc), then FUN_00524880(Detail) on every
    // field.
    AsteroidDetailScalar() = Detail;
    RaiseActiveSpaceCameraCut(); // 0x005267fb
    for (std::list<FCliAsteroid*>::iterator It = Registry().begin(); It != Registry().end(); ++It)
        CliAsteroidApplyDetail(*It, Detail);
}

int CliAsteroidPieceCount(FCliAsteroid* Field)
{
    // FUN_00524730: *(this+0x194) + *(this+0x154), no null test.
    int const Primary =
        static_cast<int>(*reinterpret_cast<unsigned*>(Field->BandBlob + 0x58)); // dword 0x16
    return Primary + static_cast<int>(Field->BandVertSum);
}

namespace
{
    // FUN_0054d920 (thiscall on the band, RET 8: out, piece) — piece XYZ from the band's spoke
    // table (+0x60, stride 0x18, XYZ at +8). Index - base (+0x5c), a negative result (JNS)
    // clamped to 0; nothing is tested for null. Returns the out pointer.
    float* BandGetPiecePos(unsigned char* Blob, int PieceIndex, float* OutXYZ)
    {
        int Rel = PieceIndex - static_cast<int>(*reinterpret_cast<unsigned*>(Blob + 0x5c));
        if (Rel < 0)
            Rel = 0;
        unsigned char* const Table = *reinterpret_cast<unsigned char**>(Blob + 0x60);
        float const* const Entry = reinterpret_cast<float const*>(Table + Rel * 0x18 + 8);
        OutXYZ[0] = Entry[0];
        OutXYZ[1] = Entry[1];
        OutXYZ[2] = Entry[2];
        return OutXYZ;
    }

    // FUN_0054d950 (thiscall on the band, RET 4) — the radius float at spoke entry +0x14, same
    // index clamp, no null test.
    float BandGetPieceRadius(unsigned char* Blob, int PieceIndex)
    {
        int Rel = PieceIndex - static_cast<int>(*reinterpret_cast<unsigned*>(Blob + 0x5c));
        if (Rel < 0)
            Rel = 0;
        unsigned char* const Table = *reinterpret_cast<unsigned char**>(Blob + 0x60);
        return *reinterpret_cast<float const*>(Table + Rel * 0x18 + 0x14);
    }
} // namespace

namespace
{
    // DAT_006140a4 — two-sided band light soft threshold.
    constexpr float kBandLightSoft = 0.75f;

    // FUN_00410960 lives in engine_light.cpp (EngineLightSample); this keeps the band bake's
    // call site readable.
    int BandSampleDirectionalLight(FEngineLight* Light, float const* ZonePos, float* OutDir3,
                                   float* InOutAmbient3, float* OutColor3)
    {
        return EngineLightSample(Light, ZonePos, OutDir3, InOutAmbient3, OutColor3) ? 1 : 0;
    }

    // FUN_0054cfb0 — bake per-particle lit RGB into +0x1c / +0x28 from nearby lights.
    void BandBakeLighting(unsigned char* Blob)
    {
        if (Blob == nullptr)
            return;

        Universe::IZone* Zone = *reinterpret_cast<Universe::IZone**>(Blob + 4);
        if (Zone == nullptr)
            return;

        float ZonePos[3] = {Zone->Pos.x, Zone->Pos.y, Zone->Pos.z};
        void* LightSlots[16] = {};
        int const LightCount =
            SpaceCollectNearbyLights(LightSlots, 16, ZonePos, 100.0f, 0);
        if (LightCount <= 0)
            return;

        unsigned char const* AmbientBytes = StarSysAmbientColor(); // FUN_00555470
        float const AmbScale = *reinterpret_cast<float*>(Blob + 0x08);
        float AmbR =
            (static_cast<float>(AmbientBytes[0]) * 0.003921569f +
             *reinterpret_cast<float*>(Blob + 0x30)) *
            *reinterpret_cast<float*>(Blob + 0x18);
        float AmbG =
            (static_cast<float>(AmbientBytes[1]) * 0.003921569f +
             *reinterpret_cast<float*>(Blob + 0x34)) *
            *reinterpret_cast<float*>(Blob + 0x1c);
        float AmbB =
            (static_cast<float>(AmbientBytes[2]) * 0.003921569f +
             *reinterpret_cast<float*>(Blob + 0x38)) *
            *reinterpret_cast<float*>(Blob + 0x20);

        float LightPack[4 * 6] = {};
        int DirCount = 0;
        float AmbWork[3] = {AmbR, AmbG, AmbB};
        for (int I = 0; I < LightCount && DirCount < 4; ++I)
        {
            FEngineLight* Light = static_cast<FEngineLight*>(LightSlots[I]);
            if (Light == nullptr || Light->Type != 3)
                continue;
            float* Slot = LightPack + DirCount * 6;
            float Color[3] = {};
            if (BandSampleDirectionalLight(Light, ZonePos, Slot, AmbWork, Color) == 0)
                continue;
            float LenSq = Slot[0] * Slot[0] + Slot[1] * Slot[1] + Slot[2] * Slot[2];
            if (LenSq > 0.0f)
            {
                float const Inv = 1.0f / sqrtf(LenSq);
                Slot[0] *= Inv;
                Slot[1] *= Inv;
                Slot[2] *= Inv;
            }
            Slot[3] = Color[0];
            Slot[4] = Color[1];
            Slot[5] = Color[2];
            ++DirCount;
        }

        AmbR = AmbWork[0] * AmbScale;
        AmbG = AmbWork[1] * AmbScale;
        AmbB = AmbWork[2] * AmbScale;

        int const PointCount = static_cast<int>(*reinterpret_cast<unsigned*>(Blob + 0x44));
        float* Particles = *reinterpret_cast<float**>(Blob + 0x3c);
        if (Particles == nullptr || PointCount <= 0)
        {
            Blob[0x7c] = 1;
            return;
        }

        float const DiffR = *reinterpret_cast<float*>(Blob + 0x0c);
        float const DiffG = *reinterpret_cast<float*>(Blob + 0x10);
        float const DiffB = *reinterpret_cast<float*>(Blob + 0x14);

        for (int P = 0; P < PointCount; ++P)
        {
            float* Nrm = Particles + P * 15 + 5;
            float SideA[3] = {AmbR, AmbG, AmbB};
            float SideB[3] = {AmbR, AmbG, AmbB};
            for (int L = 0; L < DirCount; ++L)
            {
                float const* Pack = LightPack + L * 6;
                float const Dot =
                    Pack[0] * Nrm[0] + Pack[1] * Nrm[1] + Pack[2] * Nrm[2];
                if (-kBandLightSoft < Dot)
                {
                    float const T = (kBandLightSoft + Dot) / (kBandLightSoft + 1.0f);
                    SideB[0] += Pack[3] * DiffR * T;
                    SideB[1] += Pack[4] * DiffG * T;
                    SideB[2] += Pack[5] * DiffB * T;
                }
                if (-kBandLightSoft < -Dot)
                {
                    float const T = (-Dot + kBandLightSoft) / (kBandLightSoft + 1.0f);
                    SideA[0] += Pack[3] * DiffR * T;
                    SideA[1] += Pack[4] * DiffG * T;
                    SideA[2] += Pack[5] * DiffB * T;
                }
            }
            for (int C = 0; C < 3; ++C)
            {
                if (SideA[C] > 1.0f)
                    SideA[C] = 1.0f;
                if (SideB[C] > 1.0f)
                    SideB[C] = 1.0f;
            }
            float* Dst = Particles + P * 15;
            Dst[7] = SideA[0];
            Dst[8] = SideA[1];
            Dst[9] = SideA[2];
            Dst[10] = SideB[0];
            Dst[11] = SideB[1];
            Dst[12] = SideB[2];
        }
        Blob[0x7c] = 1;
    }

    // FUN_0054c420 (thiscall on one 0x3c-byte spoke, RET 0x14: camera position and the band's
    // four squared distances) -- the spoke's fade (+0x34) and facing flag (+0x38). Spoke:
    // position +0x00, axis +0x10. No null tests.
    void BandUpdateSpokeFade(unsigned char* Spoke, Vector const* CamPos, float InnerSq,
                             float NearSq, float FarSq, float OuterSq)
    {
        float* const Pos = reinterpret_cast<float*>(Spoke);
        float* const Axis = reinterpret_cast<float*>(Spoke + 0x10);
        float* const Fade = reinterpret_cast<float*>(Spoke + 0x34);

        // 0x0054c427..0x0054c45f: the offset to the camera (stored as floats), its squared
        // length summed x*x + z*z, then + y*y.
        float const Dx = CamPos->x - Pos[0];
        float const Dy = CamPos->y - Pos[1];
        float const Dz = CamPos->z - Pos[2];
        // The products and sums stay on the FPU (double here), only the offsets are floats.
        double const DistSq = (static_cast<double>(Dx) * Dx + static_cast<double>(Dz) * Dz) +
                              static_cast<double>(Dy) * Dy;

        // 0x0054c461..0x0054c4ca. DL (bInside) is set only by the inner branch.
        bool bInside = false;
        if (DistSq > NearSq) // FCOM p3, TEST AH,0x41 / JNZ: not greater -> 0x0054c4a4
        {
            if (DistSq < FarSq) // FCOM p4, TEST AH,5 / JP: greater, equal or unordered -> 480
            {
                *Fade = 1.0f;
            }
            else if (DistSq > OuterSq) // FCOM p5, TEST AH,0x41 / JNZ -> 494
            {
                *Fade = 0.0f;
            }
            else
            {
                *Fade = static_cast<float>((OuterSq - DistSq) / (OuterSq - FarSq));
            }
        }
        else if (DistSq < InnerSq) // FCOM p2, TEST AH,5 / JP -> 0x0054c4ba
        {
            *Fade = 0.0f;
            bInside = true;
        }
        else
        {
            *Fade = static_cast<float>((DistSq - InnerSq) / (NearSq - InnerSq));
        }

        // 0x0054c4cd..0x0054c4df: FCOMP 0.0, TEST AH,0x41 / JZ -- a fade above 0, or the inner
        // case, goes on; otherwise nothing more.
        if (!(*Fade > 0.0f) && !bInside)
            return;

        // 0x0054c4e5..0x0054c50e: the unit offset (1.0 / sqrt) dotted with the axis, summed
        // z, y, then x.
        double const Inv = 1.0 / sqrt(DistSq);
        double const Dot = (Dz * Inv * Axis[2] + Dy * Inv * Axis[1]) + Dx * Inv * Axis[0];
        // 0x0054c510..0x0054c528: +0x38 = Dot > 0 (TEST AH,0x41 / JNZ -> 0).
        Spoke[0x38] = (Dot > 0.0) ? 1 : 0;
        // 0x0054c526..0x0054c573: |Dot| stored as a float; below 0.3 (DAT_005d3e08) the spoke
        // is hidden, below 0.5 (DAT_005c75e0) it fades by (|Dot| - 0.3) * 5 (DAT_005e0a60).
        float const AbsDot = static_cast<float>(fabs(Dot));
        if (AbsDot < 0.3f)
        {
            *Fade = 0.0f;
            return;
        }
        if (AbsDot < 0.5f)
            *Fade = (AbsDot - 0.3f) * *Fade * 5.0f;
    }

    // FUN_0054d410 (thiscall on the band, RET 4: camera position) -- the band's per-frame
    // update from FUN_00522070: the lighting baked on the second call (+0x80 counts up while
    // +0x7c is clear), then, unless the fraction (+0x54) is below 0.001 (DAT_005c75f8, TEST
    // AH,5 / JNP), every spoke's fade against the squares of the four distances +0x64..+0x70.
    void BandUpdate(unsigned char* Blob, Vector const* CamPos)
    {
        if (Blob[0x7c] == 0)
        {
            int* const Calls = reinterpret_cast<int*>(Blob + 0x80);
            *Calls = *Calls + 1;
            if (*Calls > 1) // JLE skips
                BandBakeLighting(Blob);
        }

        if (*BandF(Blob, 0x54 / 4) < 0.001)
            return;

        float const A = *BandF(Blob, 0x64 / 4);
        float const B = *BandF(Blob, 0x68 / 4);
        float const C = *BandF(Blob, 0x6c / 4);
        float const D = *BandF(Blob, 0x70 / 4);
        float const ASq = A * A;
        float const BSq = B * B;
        float const CSq = C * C;
        float const DSq = D * D;
        int const SpokeCount = *reinterpret_cast<int*>(Blob + 0x44);
        unsigned char* const Spokes = *reinterpret_cast<unsigned char**>(Blob + 0x3c);
        for (int Index = 0; Index < SpokeCount; ++Index)
            BandUpdateSpokeFade(Spokes + Index * 0x3c, CamPos, ASq, BSq, CSq, DSq);
    }

    // FUN_0054d4e0 (thiscall on the band, RET 8) — Win3D triangle-pair quads for one spoke
    // piece index. Retail's first stack argument (the camera position FUN_00522850 passes) is
    // never read, so the port drops it. Nothing is tested for null.
    void BandDrawPiece(unsigned char* Blob, int PieceIndex)
    {
        // 0x0054d4ea..0x0054d4f5: bake the lighting once (+0x7c).
        if (Blob[0x7c] == 0)
            BandBakeLighting(Blob);

        // 0x0054d4fa..0x0054d511: Rel = piece - base (+0x5c); >= count (+0x58) or < 0 -> 0.
        int Rel = PieceIndex - static_cast<int>(*reinterpret_cast<unsigned*>(Blob + 0x5c));
        int const RenderParts = static_cast<int>(*reinterpret_cast<unsigned*>(Blob + 0x58));
        if (Rel >= RenderParts || Rel < 0)
            Rel = 0;

        // 0x0054d513..0x0054d521: FCOMP 0.01f (DAT_005c99c8), TEST AH,5 / JNP -- strictly below
        // leaves; >= and unordered go on.
        float const MaxAlpha = *reinterpret_cast<float*>(Blob + 0x54);
        if (MaxAlpha < 0.01f)
            return;

        // 0x0054d527..0x0054d538: 1.0f / float(+0x4c), stored float.
        int const VertInc = static_cast<int>(*reinterpret_cast<unsigned*>(Blob + 0x4c));
        float const InvVertInc = static_cast<float>(1.0 / static_cast<double>(VertInc));
        int const PointCount = static_cast<int>(*reinterpret_cast<unsigned*>(Blob + 0x44));
        int const CullMode = static_cast<int>(*reinterpret_cast<unsigned*>(Blob + 0x50));
        float* Particles = *reinterpret_cast<float**>(Blob + 0x3c);
        unsigned char* Spokes = *reinterpret_cast<unsigned char**>(Blob + 0x60);

        // 0x0054d53c..0x0054d54a: a shape at +0 binds FUN_00425640(shape, 0) -- whatever that
        // returns, null included.
        FTexShape const* Shape = *reinterpret_cast<FTexShape const**>(Blob);
        if (Shape != nullptr)
            Win3DBindTexture(TexShapeGetGpuHandle(Shape, 0));

        // 0x0054d54d..0x0054d578: spoke end (+4), texture cursor 0 / 1, Begin(4).
        int const EndIdx = *reinterpret_cast<int*>(Spokes + Rel * 0x18 + 4);
        int const StartIdx = *reinterpret_cast<int*>(Spokes + Rel * 0x18);
        int UCursor = 0;
        int UNext = 1;
        Win3DBegin(kWin3DTriangles);

        for (int Pass = 0; Pass < 2; ++Pass)
        {
            // 0x0054d580..0x0054d59e: the pass whose number + 1 equals the cull mode (+0x50) is
            // skipped (mode 1 drops pass 0, mode 2 drops pass 1); an empty spoke skips too.
            if (Pass + 1 == CullMode)
                continue;
            if (StartIdx >= EndIdx)
                continue;

            int Idx = StartIdx;
            int Remaining = EndIdx - StartIdx;
            int PartOff = Idx * 0x3c;
            do
            {
                ++Idx;
                if (UNext > VertInc)
                {
                    UCursor -= VertInc;
                    UNext -= VertInc;
                }
                int WrapIdx = Idx;
                if (WrapIdx >= PointCount)
                    WrapIdx -= PointCount;

                unsigned char* PartBase = reinterpret_cast<unsigned char*>(Particles);
                unsigned char const FlagA = PartBase[PartOff + 0x38];
                unsigned char const FlagB = PartBase[WrapIdx * 0x3c + 0x38];

                bool bDraw = false;
                if (Pass == 0)
                {
                    if (FlagA == 0 || FlagB == 0)
                        bDraw = true;
                }
                else if (Pass != 1 || (FlagA != 0 && FlagB != 0))
                {
                    bDraw = true;
                }

                if (bDraw)
                {
                    float* PartA = reinterpret_cast<float*>(PartBase + PartOff);
                    float* PartB = Particles + WrapIdx * 15;
                    float AlphaA = PartA[13];
                    float AlphaB = PartB[13];
                    float* ColorA = nullptr;
                    float* ColorB = nullptr;
                    if (FlagA == 0)
                    {
                        if (CullMode == 1)
                            AlphaA = 0.0f;
                        ColorA = PartA + 10;
                    }
                    else
                    {
                        if (CullMode == 2)
                            AlphaA = 0.0f;
                        ColorA = PartA + 7;
                    }
                    if (FlagB == 0)
                    {
                        if (CullMode == 1)
                            AlphaB = 0.0f;
                        ColorB = PartB + 10;
                    }
                    else
                    {
                        if (CullMode == 2)
                            AlphaB = 0.0f;
                        ColorB = PartB + 7;
                    }

                    // 0x0054d6ad..0x0054d6de: TEST AH,5 / JP on each -- the quad is skipped only
                    // when both alphas are strictly below 0.01f (unordered draws).
                    if (!(AlphaA < 0.01f) || !(AlphaB < 0.01f))
                    {
                        // 0x0054d6e4..0x0054d70e: TEST AH,0x41 / JNZ -- only an alpha strictly
                        // above +0x54 is clamped to it.
                        if (MaxAlpha < AlphaA)
                            AlphaA = MaxAlpha;
                        if (MaxAlpha < AlphaB)
                            AlphaB = MaxAlpha;

                        // 0x0054d712..0x0054d72a: FILD cursor * the stored reciprocal.
                        float const U0 = static_cast<float>(UCursor) * InvVertInc;
                        float const U1 = static_cast<float>(UNext) * InvVertInc;
                        float const HalfH = PartA[3];
                        float const Ax = PartA[0];
                        float const Ay = PartA[1];
                        float const Az = PartA[2];
                        float const Bx = PartB[0];
                        float const By = PartB[1];
                        float const Bz = PartB[2];

                        Win3DColor4f(ColorA[0], ColorA[1], ColorA[2], AlphaA);
                        Win3DTexCoord2f(U0, 1.0f);
                        Win3DVertex3f(Ax, Ay + HalfH, Az);
                        Win3DTexCoord2f(U0, 0.0f);
                        Win3DVertex3f(Ax, Ay - HalfH, Az);

                        Win3DColor4f(ColorB[0], ColorB[1], ColorB[2], AlphaB);
                        Win3DTexCoord2f(U1, 0.0f);
                        Win3DVertex3f(Bx, By - HalfH, Bz);
                        Win3DTexCoord2f(U1, 0.0f);
                        Win3DVertex3f(Bx, By - HalfH, Bz);
                        Win3DTexCoord2f(U1, 1.0f);
                        Win3DVertex3f(Bx, By + HalfH, Bz);

                        Win3DColor4f(ColorA[0], ColorA[1], ColorA[2], AlphaA);
                        Win3DTexCoord2f(U0, 1.0f);
                        Win3DVertex3f(Ax, Ay + HalfH, Az);
                    }
                }

                PartOff += 0x3c;
                ++UCursor;
                ++UNext;
                --Remaining;
            } while (Remaining != 0);
        }
        Win3DEnd();
    }
} // namespace

void CliAsteroidUpdate(FCliAsteroid* This, FSpaceCamera* Cam, float Dt)
{
    // FUN_00522070 (thiscall, RET 8: camera, dt).

    // 0x0052208e..0x005220c3: once (+0x21c, set by the constructor), with a zone: the nebula
    // at the zone's position (IZone vt+0x10, FUN_005553b0(pos, 0, 0)) into +0xd4. The flag is
    // cleared either way.
    if (This->Flag21C != 0 && This->Zone != nullptr)
    {
        This->ZeroD4 = static_cast<unsigned>(reinterpret_cast<uintptr_t>(
            StarSysFindNebula(&This->Zone->GetPos(), nullptr, nullptr)));
    }
    This->Flag21C = 0;

    // 0x005220ca..0x005220e2: the camera position (vt+0xc) copied.
    float const* const Pos3 = AsCamera(Cam)->GetPosition();
    Vector CamPos;
    CamPos.x = Pos3[0];
    CamPos.y = Pos3[1];
    CamPos.z = Pos3[2];

    // 0x005220e0..0x005220f0: nothing more without ASTEROIDS.
    if (GetGameOption(kGameOptAsteroids) == 0)
        return;

    // 0x005220f6..0x0052213b: the exclusion timer (+0xd8) advanced by dt. Above 0.5
    // (DAT_00613848; FCOMP, TEST AH,0x41 / JZ taken when greater) or on a camera cut (the
    // camera's +0xbc byte), the timer is reset and the exclusion manager, when there is one,
    // re-sorted around the camera (vt+8).
    This->FloatD8 += Dt;
    if (This->FloatD8 > 0.5f || Cam->FlagBC != 0)
    {
        This->FloatD8 = 0.0f;
        if (This->ExclusionMgr != nullptr)
            This->ExclusionMgr->sort(&CamPos);
    }

    // 0x00522140..0x00522157: [OBJECTS] and the common field.
    if (GetGameOption(kGameOptObjectsGroup) != 0 && This->CmnField != nullptr)
    {
        FCliAsteroidCubeMap& Cubes = CliAsteroidCubeMap(This);

        // 0x0052215d..0x00522305: every change event of the common field, in list order.
        for (CmnAsteroid::asteroid_marker* Event = This->CmnField->get_changes();
             Event != nullptr; Event = Event->Next)
        {
            // 0x00522177..0x00522195: FUN_00432240 -- find the event's cube (+0).
            FCliAsteroidCubeMap::iterator It = Cubes.find(Event->CubeId);
            if (It == Cubes.end())
            {
                // 0x0052219a..0x00522273: a default value inserted under the cube id
                // (FUN_00526a40 / FUN_00526d20), the temporaries destroyed.
                It = Cubes.insert(std::pair<unsigned long const, FCliAsteroidCube>(
                                      Event->CubeId, FCliAsteroidCube()))
                         .first;
                FCliAsteroidCube& Value = It->second;
                // 0x00522273..0x005222f1: the common cube (find_cube, by value) copied in --
                // the pointer, get_id, its +0x0c orientation and +0x30 position, and the
                // field's cube size (+0x20) -- or FDUMP warning 0x31d when it is missing.
                CmnAsteroid::CAsteroidCube* const Cube = This->CmnField->find_cube(Event->CubeId);
                if (Cube != nullptr)
                {
                    Value.Cube = Cube;
                    Value.CubeId = Cube->get_id();
                    Value.Orient = Cube->Orient;
                    Value.Pos = Cube->Pos;
                    Value.CubeSize = This->CubeSize;
                }
                else
                {
                    FDUMP(0x100002, "%s(%d) : *** WARNING: %s",
                          "E:\\FL\\Scratch\\Source\\Client\\Space\\CliAsteroid.cpp", 0x31d,
                          "no cmn cube found");
                }
            }
            // 0x005222f4..0x005222f7: FUN_00524cb0(event) on the value.
            It->second.ApplyEvent(Event);
        }

        // 0x0052230b..0x0052237b: every cube with no asteroid left (the inner map's size,
        // node +0x5c) erased (FUN_00526f30); the others kept.
        FCliAsteroidCubeMap::iterator CubeIt = Cubes.begin();
        while (CubeIt != Cubes.end())
        {
            if (CubeIt->second.Asteroids.size() == 0)
                CubeIt = Cubes.erase(CubeIt);
            else
                ++CubeIt;
        }

        // 0x0052237d..0x005223b0: every dynamic asteroid manager gets FUN_00525c80(camera, dt,
        // the exclusion manager, re-read per manager).
        for (void** DynIt = This->DynBegin; DynIt != This->DynEnd; ++DynIt)
        {
            CliDynamicAsteroidUpdate(static_cast<FCliDynamicAsteroid*>(*DynIt), Cam, Dt,
                                     This->ExclusionMgr);
        }
    }

    // 0x005223b2..0x005223ec: [EFFECTS], the common field, ASTEROID_EXT in (0, 3) (signed:
    // JLE / JGE).
    if (GetGameOption(kGameOptEffectsGroup) == 0 || This->CmnField == nullptr)
        return;
    if (GetGameOption(kGameOptAsteroidExt) <= 0)
        return;
    if (GetGameOption(kGameOptAsteroidExt) >= 3)
        return;

    // 0x005223ee..0x00522403: the field's own band (+0xfc) when [Band] was read (+0xf8).
    if (This->FlagF8 != 0)
        BandUpdate(This->BandBlob, &CamPos);

    // 0x00522408..0x0052245a: with exclusion bands (+0x180): near_field_fraction(camera,
    // 1000.0f) once, then per band +0x54 = that fraction and FUN_0054d410.
    if (This->Flag180 == 0)
        return;
    float const Fraction = This->CmnField->near_field_fraction(CamPos, 1000.0f);
    for (unsigned char** It = This->BandBegin; It != This->BandEnd; ++It)
    {
        *BandF(*It, 0x15) = Fraction;
        BandUpdate(*It, &CamPos);
    }
}

float* CliAsteroidGetPiecePos(FCliAsteroid* Field, int PieceIndex, float* OutXYZ)
{
    // FUN_00524740 (thiscall, RET 8: out, piece; returns out). 0x00524740..0x00524767: a
    // piece in [0, band count +0x154) comes from the field's own band (+0xfc, FUN_0054d920).
    int const Primary = static_cast<int>(*reinterpret_cast<unsigned*>(Field->BandBlob + 0x58));
    if (PieceIndex < Primary && PieceIndex >= 0)
        return BandGetPiecePos(Field->BandBlob, PieceIndex, OutXYZ);

    // 0x0052476a..0x00524795: otherwise the exclusion-band vector [+0x188, +0x18c) -- the first
    // band with base (+0x5c) <= piece < base + count (+0x58); entries are read unchecked.
    for (unsigned char** It = Field->BandBegin; It != Field->BandEnd; ++It)
    {
        unsigned char* const Blob = *It;
        int const Base = static_cast<int>(*reinterpret_cast<unsigned*>(Blob + 0x5c));
        if (PieceIndex >= Base &&
            PieceIndex < Base + static_cast<int>(*reinterpret_cast<unsigned*>(Blob + 0x58)))
            return BandGetPiecePos(Blob, PieceIndex, OutXYZ);
    }

    // 0x00524797..0x005247d5: no band holds it -- warn (CliAsteroid.cpp line 0x699) and zero
    // the out vector.
    FDUMP(0x100002, "%s(%d) : *** WARNING: %s",
          "E:\\FL\\Scratch\\Source\\Client\\Space\\CliAsteroid.cpp", 0x699,
          "bad piece number supplied to get_piece_pos in asteroid field");
    OutXYZ[0] = 0.0f;
    OutXYZ[1] = 0.0f;
    OutXYZ[2] = 0.0f;
    return OutXYZ;
}

float CliAsteroidGetPieceRadius(FCliAsteroid* Field, int PieceIndex)
{
    // FUN_005247f0 (thiscall, RET 4). 0x005247f0..0x0052480e: own band (+0xfc) tail-jumps
    // into FUN_0054d950.
    int const Primary = static_cast<int>(*reinterpret_cast<unsigned*>(Field->BandBlob + 0x58));
    if (PieceIndex < Primary && PieceIndex >= 0)
        return BandGetPieceRadius(Field->BandBlob, PieceIndex);

    // 0x00524813..0x00524838 / 0x0052486f: the exclusion-band vector, read unchecked.
    for (unsigned char** It = Field->BandBegin; It != Field->BandEnd; ++It)
    {
        unsigned char* const Blob = *It;
        int const Base = static_cast<int>(*reinterpret_cast<unsigned*>(Blob + 0x5c));
        if (PieceIndex >= Base &&
            PieceIndex < Base + static_cast<int>(*reinterpret_cast<unsigned*>(Blob + 0x58)))
            return BandGetPieceRadius(Blob, PieceIndex);
    }

    // 0x00524840..0x0052486c: warn (line 0x6ad) and return 1.0f (DAT_005c75dc).
    FDUMP(0x100002, "%s(%d) : *** WARNING: %s",
          "E:\\FL\\Scratch\\Source\\Client\\Space\\CliAsteroid.cpp", 0x6ad,
          "bad piece number supplied to get_piece_radius in asteroid field");
    return 1.0f;
}

void CliAsteroidForEach(FCliAsteroidVisitFn Fn, void* Ctx)
{
    // The head->next walk of FUN_005201d0's list that FUN_0051ba20 (0x0051ba79..0x0051ba93)
    // and FUN_00511b90 (0x00511c52..0x00511c90) inline; both read [node+8] unchecked. Entries
    // come only from FUN_0051ff50's operator new (CliAsteroidCreateAndLoad), never null.
    std::list<FCliAsteroid*>& List = Registry();
    for (std::list<FCliAsteroid*>::iterator It = List.begin(); It != List.end(); ++It)
        Fn(*It, Ctx);
}

void CliAsteroidDrawFarPiece(FCliAsteroid* Field, FSpaceCamera* Cam, int PieceIndex)
{
    // FUN_00522850 (thiscall on the field, RET 8: cam, piece) — FAR band draw for one piece
    // index. 0x00522856..0x005228ad: the gates; nothing is tested for null.
    if (Field->FlagF8 == 0 && Field->Flag180 == 0)
        return;
    if (GetGameOption(kGameOptAsteroidExt) == 0)
        return;
    if (GetGameOption(kGameOptEffectsGroup) == 0)
        return;
    if (GetGameOption(kGameOptAsteroidExt) >= 3)
        return;
    if (Field->Flag234 == 0)
        return;

    // 0x005228b4..0x005228f0: the camera's orientation (vt+0x10) and position (vt+0x0c) are
    // copied to locals and the zone's position (+0x1c, vt+0x10) fetched; all three are const
    // getters whose results nothing reads -- the position copy is only handed to FUN_0054d4e0 as
    // its unread first argument -- so the port does not make the calls.
    // 0x005228f3..0x0052291a: near / far (vt+0x20 / +0x24), then FUN_0040fb60(near, 250000.0f
    // DAT_00610534) -- the two stores at camera +0xa4 / +0xa8.
    ICamera* const Camera = AsCamera(Cam);
    float const SavedNear = Camera->GetZNear();
    float const SavedFar = Camera->GetZFar();
    Cam->ZNear = SavedNear;
    Cam->ZFar = 250000.0f;

    // DAT_00613850: .data 1, read here only (XREF[1] 0x0052291a), no writer.
    static unsigned long s_BandDrawEnable = 1;
    if (s_BandDrawEnable != 0)
    {
        // 0x00522927..0x00522960: FUN_00421ed0 disables, FUN_00421dd0 enables.
        Win3DDisable(kWin3DLightingCap); // 0xb50
        Win3DDisable(kWin3DCullFace);    // 0xb44
        Win3DSetDepthWrite(0);           // FUN_00422190(0)
        Win3DDisable(kWin3DDepthTest);   // 0xb71
        Win3DEnable(kWin3DBlend);        // 0xbe2
        Win3DApplyCamera(Camera);        // FUN_00422950(cam)
        Win3DSetBlendFunc(5, 6);         // FUN_004220c0(5, 6)

        // 0x00522965..0x0052299b: pipeline state 0x18 saved (slot +0xf4, zero-initialised
        // out), then set to 5 (slot +0xf0).
        int SavedRs18 = 0;
        DALib::Pipeline->GetRenderState(D3DRS_ALPHAREF, &SavedRs18);
        DALib::Pipeline->SetRenderState(D3DRS_ALPHAREF, 5);

        // 0x0052299b..0x005229f1: the own band (+0xfc) when piece < count (+0x154) + base
        // (+0x158); otherwise the first exclusion band (vector +0x188..+0x18c, read unchecked)
        // with base <= piece < base + count. No band -- nothing drawn.
        int const PrimaryCount =
            static_cast<int>(*reinterpret_cast<unsigned*>(Field->BandBlob + 0x58));
        int const PrimaryBase =
            static_cast<int>(*reinterpret_cast<unsigned*>(Field->BandBlob + 0x5c));
        if (PieceIndex < PrimaryCount + PrimaryBase)
        {
            BandDrawPiece(Field->BandBlob, PieceIndex);
        }
        else
        {
            for (unsigned char** It = Field->BandBegin; It != Field->BandEnd; ++It)
            {
                unsigned char* const ExBlob = *It;
                int const ExBase =
                    static_cast<int>(*reinterpret_cast<unsigned*>(ExBlob + 0x5c));
                if (PieceIndex >= ExBase &&
                    PieceIndex < ExBase + static_cast<int>(*reinterpret_cast<unsigned*>(ExBlob + 0x58)))
                {
                    BandDrawPiece(ExBlob, PieceIndex);
                    break;
                }
            }
        }

        // 0x005229f6..0x00522a30: states back, then state 0x18.
        Win3DDisable(kWin3DBlend);     // 0xbe2
        Win3DEnable(kWin3DDepthTest);  // 0xb71
        Win3DSetDepthWrite(1);         // FUN_00422190(1)
        Win3DEnable(kWin3DCullFace);   // 0xb44
        DALib::Pipeline->SetRenderState(D3DRS_ALPHAREF, SavedRs18);
    }

    // 0x00522a36..0x00522a44: FUN_0040fb60(near, far) and the camera applied again.
    Cam->ZNear = SavedNear;
    Cam->ZFar = SavedFar;
    Win3DApplyCamera(Camera);
}

namespace
{
    // .rdata DAT_005c9804 — Ghidra (fVar1+fVar2)*1.25; DAT_005c75dc = 1.0f.
    constexpr float kBillboardMarginScale = 1.25f;
    constexpr float kAmbientScaleArg = 100.0f; // 0x42c80000 pushed into FUN_00522700

    // FUN_0054e040(Billboard*, CamPos) — zone-proximity gate for billboard draw.
    bool BillboardNearCamera(FAsteroidBillboardBlock* Bb, Vector const& CamPos)
    {
        // .text:0054e046-0054e053: no zone -> true. (ESI = the billboard block, never tested.)
        Universe::IZone* Zone = Bb->ZoneRef;
        if (Zone == nullptr)
            return true;

        // .text:0054e056-0054e063: (F04 + F00) * 1.25f, stored as a float.
        float const Margin = (Bb->Float04 + Bb->Float00) * kBillboardMarginScale;

        // .text:0054e067-0054e089: the offset from the zone's vt+0x10 position, as floats.
        Vector const& ZonePos = Zone->GetPos();
        Vector Dir;
        Dir.x = CamPos.x - ZonePos.x;
        Dir.y = CamPos.y - ZonePos.y;
        Dir.z = CamPos.z - ZonePos.z;

        // .text:0054e08f-0054e0c5: vt+0x30 + Margin on the stack; (dx^2 + dz^2) + dy^2 is
        // stored as a float and compared on the stack with Reach^2: TEST 0x41 / JZ -> false
        // when the distance is greater.
        double const Reach = static_cast<double>(Zone->GetRadius()) + Margin;
        double const DistSq = (static_cast<double>(Dir.x) * Dir.x + static_cast<double>(Dir.z) * Dir.z) +
                              static_cast<double>(Dir.y) * Dir.y;
        float const DistSqF = static_cast<float>(DistSq);
        if (DistSq > Reach * Reach)
            return false;

        // .text:0054e0c7-0054e105: the float distance, the direction scaled by 1.0f / it (no
        // zero test), then vt+0x40 on it.
        float const Dist = static_cast<float>(sqrt(static_cast<double>(DistSqF)));
        double const Inv = 1.0f / static_cast<double>(Dist);
        Dir.x = static_cast<float>(Dir.x * Inv);
        Dir.y = static_cast<float>(Dir.y * Inv);
        Dir.z = static_cast<float>(Dir.z * Inv);

        // .text:0054e108-0054e115: Dist - vt+0x40 vs Margin, TEST 0x41 / JP -> false when
        // greater or unordered.
        return (static_cast<double>(Dist) - Zone->GetRadiusAlong(Dir)) <= Margin;
    }

    // FUN_00522700 (thiscall on the field, RET 8) -- light the field around a point Scale
    // along the camera's view axis: StarSys object lighting (FUN_005554d0) at that point with
    // radius 250, then the ambient read back (FUN_00410280), raised by ambient_increase
    // (+0xd0..+0xd2, each channel capped at 0xff) and written again (FUN_00410240).
    void CliAsteroidAmbientPass(FCliAsteroid* Field, FSpaceCamera* Cam, float Scale)
    {
        ICamera* const Camera = AsCamera(Cam);

        // 0x0052270b..0x00522748: the orientation's third column (+0x8 / +0x14 / +0x20) times
        // Scale, each product stored as a float.
        float const* const Orient = Camera->GetOrientation();
        float const AxisX = Orient[2] * Scale;
        float const AxisY = Orient[5] * Scale;
        float const AxisZ = Orient[8] * Scale;

        // 0x00522740..0x00522778: the fog factor starts at 1.0f. With contains_fog_zone (+0xd3):
        // a field controller (DAT_00675244) with the camera inside it (DAT_0067524c) becomes
        // +0xd4 and its vt+0x24 value (FSTP float) the factor; otherwise +0xd4 = 0. Without
        // the flag +0xd4 is left as it is.
        float FogFactor = 1.0f;
        if (Field->FlagD3 != 0)
        {
            void* const Controller = GetSpaceFieldController();
            if (Controller != nullptr && GetSpaceFieldEnclosure() != 0)
            {
                Field->ZeroD4 = static_cast<unsigned>(reinterpret_cast<uintptr_t>(Controller));
                using FControllerFloatFn = float(__thiscall*)(void*);
                FogFactor = ((FNebulaEffect*)Controller)->GetFloat294();
            }
            else
            {
                Field->ZeroD4 = 0;
            }
        }

        // 0x00522782..0x005227c1: that offset plus the camera position (vt+0xc), then
        // FUN_005554d0(point, 250.0f, camera, +0xd4, factor bits, 1). (Port-only, FREELANCER_RP9:
        // while the Direct3D 9 volumetric nebula draws the controller, StarSysApplyObjectLighting
        // skips its fog block -- NebulaVolume::ReplacesObjectFog -- the ambient lerp stays.)
        float const* const CamPos = Camera->GetPosition();
        float Point[3];
        Point[0] = AxisX + CamPos[0];
        Point[1] = AxisY + CamPos[1];
        Point[2] = AxisZ + CamPos[2];
        unsigned FogBits = 0;
        memcpy(&FogBits, &FogFactor, sizeof(FogBits));
        SpaceApplyObjectLighting(Point, 250.0f, Cam, Field->ZeroD4, FogBits, 1);

        // 0x005227c6..0x00522836: D3DRS_AMBIENT back as bytes, plus ambient_increase, capped.
        unsigned char Ambient[3];
        EngineLightGetAmbientRgbBytes(Ambient);
        int R = static_cast<int>(Field->FlagD0) + static_cast<int>(Ambient[0]);
        int G = static_cast<int>(Field->FlagD1) + static_cast<int>(Ambient[1]);
        int B = static_cast<int>(Field->FlagD2) + static_cast<int>(Ambient[2]);
        if (R > 0xff)
            R = 0xff;
        if (G > 0xff)
            G = 0xff;
        if (B > 0xff)
            B = 0xff;
        unsigned char Raised[3];
        Raised[0] = static_cast<unsigned char>(R);
        Raised[1] = static_cast<unsigned char>(G);
        Raised[2] = static_cast<unsigned char>(B);
        EngineLightSetAmbientRgbBytes(Raised);
    }

    // FUN_0051feb0 — fill the FRpVertexSrcDesc / Dyn MeshRegion (listing 444776-444830).
    // cdecl (desc*, verts*, ushort vertCount). Stride 0x2C attribute table for VB upload.
    // Listing 0051feb0..0051ff41 is straight-line: no test of Desc or Verts, the attribute
    // bases are Verts + 0xc / 0x18 / 0x1c / 0x24 unconditionally.
    void CliAsteroidFillMeshDesc(FRpVertexSrcDesc* Desc, void* Verts, unsigned short VertCount)
    {
        unsigned char* const Base = static_cast<unsigned char*>(Verts);
        unsigned int const Count = static_cast<unsigned int>(VertCount);
        Desc->Flags = 2;
        Desc->Position.Base = Base;
        Desc->Position.ByteSize = 0xC;
        Desc->Position.Stride = 0x2C;
        Desc->Position.IndexArray = nullptr;
        Desc->Position.Count = Count;
        Desc->Normal.Base = Base + 0xC;
        Desc->Normal.ByteSize = 0xC;
        Desc->Normal.Stride = 0x2C;
        Desc->Normal.IndexArray = nullptr;
        Desc->Normal.Count = Count;
        Desc->Diffuse.Base = Base + 0x18;
        Desc->Diffuse.ByteSize = 4;
        Desc->Diffuse.Stride = 0x2C;
        Desc->Diffuse.IndexArray = nullptr;
        Desc->Diffuse.Count = Count;
        Desc->Tex[0].Base = Base + 0x1C;
        Desc->Tex[0].ByteSize = 8;
        Desc->Tex[0].Stride = 0x2C;
        Desc->Tex[0].IndexArray = nullptr;
        Desc->Tex[0].Count = Count;
        Desc->Tex[1].Base = Base + 0x24;
        Desc->Tex[1].ByteSize = 8;
        Desc->Tex[1].Stride = 0x2C;
        Desc->Tex[1].IndexArray = nullptr;
        Desc->Tex[1].Count = Count;
        Desc->SrcFvf = 0x252;
        Desc->SrcCount = Count;
    }

    // Stride-0x2C merged cube vertex (FVF 0x252): pos/nrm/diff/spec/uv.
    static unsigned const kCubeVertStride = 0x2C;

    // FUN_00520850's local std::list element (node+0x08..+0x14, node 0x18 bytes): the
    // vertices and indices of one asteroid archetype. The temporary pushed at 0x005216c7 is
    // all zero and copied with FUN_0045a080 (four dwords); both the temporary (0x0052171f /
    // 0x00521731) and every node at the end (0x00521ac7 / 0x00521ad6) free the two buffers,
    // so the element owns them. Built in place here, never copied.
    struct FAsteroidMergeChunk
    {
        unsigned char* Verts;    // node+0x08
        int VertCount;           // node+0x0c
        unsigned short* Indices; // node+0x10
        int IndexCount;          // node+0x14

        FAsteroidMergeChunk()
            : Verts(nullptr)
            , VertCount(0)
            , Indices(nullptr)
            , IndexCount(0)
        {
        }
        FAsteroidMergeChunk(FAsteroidMergeChunk const&) = delete;
        FAsteroidMergeChunk& operator=(FAsteroidMergeChunk const&) = delete;
        ~FAsteroidMergeChunk()
        {
            operator delete(Verts);
            operator delete(Indices);
        }
    };

    // FUN_00520850 0x00521518..0x00521a90 (inline in retail; split out here as a helper of the
    // same function). Lod is FUN_00520850's local [ESP+0x30] (the [Field] "LOD" key, 0 when
    // absent); FieldIniRel is its file argument ([ESP+0x3b4c]), named by the WARNING FDUMP.
    void CliAsteroidMergeCubeMesh(FCliAsteroid* Field, int Lod, char const* FieldIniRel)
    {
        // 0x00521518..0x00521548: the chunk list (head node operator new(0x18)); the two
        // totals [ESP+0x34] / [ESP+0x18].
        std::list<FAsteroidMergeChunk> Chunks;
        int TotalVerts = 0;
        int TotalIdx = 0;

        // 0x00521537..0x0052154c: nothing is walked without the common field.
        if (Field->CmnField != nullptr)
        {
            std::list<CmnAsteroid::AsteroidCubeArchEntry> const& ArchList =
                Field->CmnField->ArchList;

            // 0x00521555..0x0052156e: a material from an earlier load is released through
            // vt+8 and the pointer is left as it was (so it is not queried again below).
            if (Field->Material != nullptr)
                static_cast<IDAComponent*>(Field->Material)->Release();

            // 0x00521571 / 0x00521580..0x0052194b: every archetype node of the common field.
            for (std::list<CmnAsteroid::AsteroidCubeArchEntry>::const_iterator EntryIt = ArchList.begin();
                 EntryIt != ArchList.end(); ++EntryIt)
            {
                CmnAsteroid::AsteroidCubeArchEntry const& Entry = *EntryIt;
                Archetype::Asteroid* const Arch = Entry.Arch;
                IMesh* Mesh = nullptr;
                IRenderableMesh* RMesh = nullptr;

                // 0x00521588..0x005215e0: IMesh for the archetype's engine index (+0x40).
                int const EngineIndex = static_cast<int>(Arch->EngineIndex);
                if (DALib::Engine->QueryEngineComponent(EngineIndex, const_cast<char*>(Iids::Mesh),
                                                        reinterpret_cast<void**>(&Mesh)) < 0)
                {
                    FDUMP(0x100001, "%s(%d) : *** ERROR: IMesh query failed, on asteroid arch (%s)",
                          "E:\\FL\\Scratch\\Source\\Client\\Space\\CliAsteroid.cpp", 0x28e,
                          Arch->Nickname.get_string());
                }

                // 0x005215e2..0x005215e8: no mesh -> straight to the releases.
                if (Mesh != nullptr)
                {
                    // 0x005215ee..0x0052162a: slot 9 (0, 1); slot 14 into +0xa0 while it is
                    // null; slot 13 into +0xb0; slot 10.
                    Mesh->OpenFaceGroup(0, 1);
                    if (Field->Material == nullptr)
                        Mesh->GetFaceGroupMaterial(&Field->Material);
                    Mesh->GetFaceGroupPrimitiveType(&Field->FieldB0);
                    Mesh->CloseFaceGroup();

                    // 0x0052162d..0x00521670: the LOD count (renderer vt+0x2c), the LOD clamped
                    // to [0, count - 1] and kept for the next archetype, then set (vt+0x34).
                    int LodCount = 0;
                    DALib::Renderer->GetArchetypeLodCount(
                        reinterpret_cast<void*>(static_cast<intptr_t>(EngineIndex)), &LodCount);
                    if (Lod + 1 > LodCount)
                        Lod = LodCount - 1;
                    if (Lod < 0)
                        Lod = 0;
                    DALib::Renderer->SetArchetypeLod(
                        reinterpret_cast<void*>(static_cast<intptr_t>(EngineIndex)), Lod);

                    // 0x00521673..0x005216b8: IRenderableMesh for the same index.
                    if (DALib::Engine->QueryEngineComponent(
                            EngineIndex, const_cast<char*>(Iids::RenderableMesh),
                            reinterpret_cast<void**>(&RMesh)) < 0)
                    {
                        FDUMP(0x100001,
                              "%s(%d) : *** ERROR: IRenderableMesh query failed, on asteroid arch "
                              "(%s)",
                              "E:\\FL\\Scratch\\Source\\Client\\Space\\CliAsteroid.cpp", 0x2a7,
                              Arch->Nickname.get_string());
                    }

                    // 0x005216bb..0x00521907: with a renderable mesh, a new chunk at the back.
                    if (RMesh != nullptr)
                    {
                        Chunks.emplace_back();
                        FAsteroidMergeChunk& Chunk = Chunks.back();

                        // 0x00521746..0x005217dc: vertex count (slot 3), operator new(count *
                        // 0x2c) -- the per-vertex texcoord constructors FUN_0043d950 write
                        // nothing --, the total, then the vertices in FVF 0x252 (slot 4).
                        RMesh->GetVertexCount(&Chunk.VertCount);
                        Chunk.Verts = static_cast<unsigned char*>(
                            operator new(static_cast<unsigned>(Chunk.VertCount) * kCubeVertStride));
                        TotalVerts += Chunk.VertCount;
                        RMesh->CopyVertexData(Chunk.Verts, 0x252u, 0, Chunk.VertCount);

                        // 0x005217df..0x0052181d: index count (slot 5), operator new(count * 2),
                        // the indices (slot 6), the total.
                        RMesh->GetIndexCount(&Chunk.IndexCount);
                        Chunk.Indices = static_cast<unsigned short*>(
                            operator new(static_cast<unsigned>(Chunk.IndexCount) * 2));
                        RMesh->CopyIndices(&Chunk.Indices, Chunk.IndexCount);
                        TotalIdx += Chunk.IndexCount;

                        // 0x00521821..0x00521901: every vertex's position and normal through the
                        // math engine's TransformMatrix (vt+0x2c) with the node's orientation
                        // (node+0x18), then the node's offset (node+0x0c) added to the position.
                        for (unsigned Vi = 0; Vi < static_cast<unsigned>(Chunk.VertCount); ++Vi)
                        {
                            float* const Vert =
                                reinterpret_cast<float*>(Chunk.Verts + Vi * kCubeVertStride);
                            FVector OutPos;
                            CliAsteroidMathEngine()->TransformMatrix(
                                OutPos, reinterpret_cast<FMatrix const&>(Entry.Orient),
                                *reinterpret_cast<FVector const*>(Vert));
                            Vert[0] = OutPos.X;
                            Vert[1] = OutPos.Y;
                            Vert[2] = OutPos.Z;
                            FVector OutNrm;
                            CliAsteroidMathEngine()->TransformMatrix(
                                OutNrm, reinterpret_cast<FMatrix const&>(Entry.Orient),
                                *reinterpret_cast<FVector const*>(Vert + 3));
                            Vert[3] = OutNrm.X;
                            Vert[4] = OutNrm.Y;
                            Vert[5] = OutNrm.Z;
                            Vert[0] = Entry.Extent.x + Vert[0];
                            Vert[1] = Vert[1] + Entry.Extent.y;
                            Vert[2] = Vert[2] + Entry.Extent.z;
                        }
                    }
                }

                // 0x0052190d..0x0052193e: the renderable mesh, then the mesh, released.
                if (RMesh != nullptr)
                    RMesh->Release();
                if (Mesh != nullptr)
                    Mesh->Release();
            }
        }

        // 0x00521951..0x0052197d: no material at all is only a warning.
        if (Field->Material == nullptr)
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: no material found for asteroids in field (%s)",
                  "E:\\FL\\Scratch\\Source\\Client\\Space\\CliAsteroid.cpp", 0x2ca, FieldIniRel);
        }

        // 0x00521980..0x005219ed: the merged vertices (+0xa4, texcoord constructors again
        // empty) and indices (+0xa8). Whatever the two pointers held before is not freed.
        Field->MeshVerts = operator new(static_cast<unsigned>(TotalVerts) * kCubeVertStride);
        Field->MeshIndices = operator new(static_cast<unsigned>(TotalIdx) * 2);

        // 0x005219f3..0x00521a79: every chunk copied in list order; its indices rebased by the
        // vertices before it (16-bit add).
        int VertBase = 0;
        int IdxBase = 0;
        for (std::list<FAsteroidMergeChunk>::const_iterator It = Chunks.begin(); It != Chunks.end();
             ++It)
        {
            FAsteroidMergeChunk const& Chunk = *It;
            memcpy(static_cast<unsigned char*>(Field->MeshVerts) +
                       static_cast<unsigned>(VertBase) * kCubeVertStride,
                   Chunk.Verts, static_cast<unsigned>(Chunk.VertCount) * kCubeVertStride);
            unsigned short* const Dst = static_cast<unsigned short*>(Field->MeshIndices);
            for (unsigned Ii = 0; Ii < static_cast<unsigned>(Chunk.IndexCount); ++Ii)
            {
                Dst[static_cast<unsigned>(IdxBase) + Ii] = static_cast<unsigned short>(
                    Chunk.Indices[Ii] + static_cast<unsigned short>(VertBase));
            }
            IdxBase += Chunk.IndexCount;
            VertBase += Chunk.VertCount;
        }

        // 0x00521a7b..0x00521a98: FUN_0051feb0(&+0x30, +0xa4, total vertices), +0xac = total
        // indices. The chunk list is destroyed on return (0x00521aa3..0x00521b01).
        CliAsteroidFillMeshDesc(&Field->MeshDesc, Field->MeshVerts,
                                static_cast<unsigned short>(TotalVerts));
        Field->MeshIndexCount = static_cast<unsigned>(TotalIdx);
    }

    // DAT_00613854 / DAT_00613855 -- .data bytes, initial 1 (PE bytes), read only by FUN_00522a60
    // (0x00523a93; 0x005240b8 and 0x0052458b) and written nowhere in the image: the per-cube
    // plane test and the per-cube blend switch both always run.
    unsigned char G_AsteroidPlaneTest = 1;
    unsigned char G_AsteroidAlphaSwitch = 1;
    // DAT_006753e0 / DAT_006753e4 -- .bss dwords read only by FUN_00522a60 (0x00522b83; 0x0052418a
    // and 0x00524645) and written nowhere: the orientation cache is filled but never read back, and
    // no draw is skipped.
    int G_AsteroidUseOrientCache = 0;
    int G_AsteroidSkipDraw = 0;
    // DAT_006753b4 / DAT_006753b8 / DAT_006753bc -- zeroed by FUN_00520170 (0x0052017f..0x0052018b)
    // and accumulated at the end of FUN_00522a60 (0x005246c4..0x005246fd): triangles drawn, cubes
    // visited, cubes drawn. Those are their only references; nothing reads the totals.
    unsigned G_AsteroidTrianglesDrawn = 0;
    unsigned G_AsteroidCubesVisited = 0;
    unsigned G_AsteroidCubesDrawn = 0;

    // DAT_005dc3c4 / DAT_005dc394 / DAT_005dc374 -- {state, value} tables, copied from the PE.
    unsigned long const kRsAsteroidCull[] = {0x8d, 0, 0};
    unsigned long const kRsAsteroidBlend[] = {0x89, 1, 0x1d, 0, 0x1b, 1, 0x13, 5, 0x14, 6, 0};
    unsigned long const kRsAsteroidOpaque[] = {0x89, 1, 0x1d, 0, 0x1b, 0, 0};

    // The inline {state, value} walk (first state loaded as a constant, next state tested for 0).
    void AsteroidApplyRs(IRenderPipeline* Pipe, unsigned long const* Table)
    {
        for (unsigned long const* Pair = Table; Pair[0] != 0; Pair += 2)
            Pipe->SetRenderState((D3DRENDERSTATETYPE)Pair[0], static_cast<int>(Pair[1]));
    }

    // MSVCRT _ftol (FUN_005b7ec0): truncation toward zero; the callers keep EAX.
    int AsteroidFtol(double X)
    {
        return static_cast<int>(static_cast<long long>(X));
    }

    // 0x00523064..0x0052308d (and the fourteen copies after it): a snapped coordinate strictly
    // greater than 0 (TEST AH,0x41 / JNE) gets + 0.1f (DAT_005d3ee0), anything else - 0.1f, on the
    // FPU, then _ftol.
    int AsteroidCubeCoord(float V)
    {
        if (V > 0.0f)
            return AsteroidFtol(static_cast<double>(V) + 0.1f);
        return AsteroidFtol(static_cast<double>(V) - 0.1f);
    }

    // The bound update after every later seed: below the minimum (JGE not taken) replaces it,
    // otherwise above the maximum (JLE not taken) replaces that.
    void AsteroidExtend(int V, int* Min, int* Max)
    {
        if (V < *Min)
            *Min = V;
        else if (V > *Max)
            *Max = V;
    }

    Vector AsteroidVec(float X, float Y, float Z)
    {
        Vector V;
        V.x = X;
        V.y = Y;
        V.z = Z;
        return V;
    }

    // FUN_00422b80 fed with three FSTP'd products: each component float(component * S).
    Vector AsteroidScale(Vector const& V, double S)
    {
        return AsteroidVec(static_cast<float>(V.x * S), static_cast<float>(V.y * S),
                           static_cast<float>(V.z * S));
    }

    // FUN_00423b30 (Out = A + B) and FUN_00423b60 (Out = A - B), per component.
    Vector AsteroidAdd(Vector const& A, Vector const& B)
    {
        return AsteroidVec(A.x + B.x, A.y + B.y, A.z + B.z);
    }

    Vector AsteroidSub(Vector const& A, Vector const& B)
    {
        return AsteroidVec(A.x - B.x, A.y - B.y, A.z - B.z);
    }

    // The side-plane dot product in the listing's order: (d.x*v.x + d.z*v.z) + d.y*v.y.
    double AsteroidPlaneDot(Vector const& D, Vector const& V)
    {
        return (static_cast<double>(D.x) * V.x + static_cast<double>(D.z) * V.z) +
               static_cast<double>(D.y) * V.y;
    }

    // 0x00523e08..0x00523f38 (first cube) and 0x0052434b..0x00524496 (the camera's own cube): the
    // one upload of the field mesh. VertexBuffer +0x14 locks +0x98 (FVF) x +0x9c vertices into
    // Desc; a negative result FDUMPs the warning with the caller's line (0x524 / 0x580) and the
    // caller returns out of FUN_00522a60 without the render-state restore. Otherwise +0x20 copies
    // the field's source descriptor (+0x30) into the lock at the lock's FVF, +0x18 unlocks, the
    // material (+0xa0) gets its slot 8 (+0x20) with 0, DAT_005dc3c4 is applied, and max_alpha
    // (+0xcc) below 1.0 (TEST AH,5 / JP) picks DAT_005dc394 + RS 0x18 = 5, else DAT_005dc374 +
    // RS 0x18 = 0x7f.
    bool AsteroidUploadMesh(FCliAsteroid* Field, FVertexMemoryLock* Desc, int Line)
    {
        IVertexBufferManager* const VbMgr = DALib::VertexBuffer;
        if (VbMgr->LockVertexMemory(Field->MeshDesc.SrcFvf, Field->MeshDesc.SrcCount, Desc) < 0)
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: %s",
                  "E:\\FL\\Scratch\\Source\\Client\\Space\\CliAsteroid.cpp", Line,
                  "Vertex buffer copy in asteroid field failed");
            return false;
        }
        VbMgr->CopyVertexData(Desc->Vertices, Desc->Listing.Fvf, &Field->MeshDesc, 0, Field->MeshDesc.SrcCount);
        VbMgr->UnlockVertexMemory(Desc);
        if (Field->Material != nullptr)
        {
            Field->Material->Bind(nullptr);
        }
        IRenderPipeline* const Pipe = DALib::Pipeline;
        AsteroidApplyRs(Pipe, kRsAsteroidCull);
        if (Field->ScaleCC < 1.0f)
        {
            AsteroidApplyRs(Pipe, kRsAsteroidBlend);
            Pipe->SetRenderState(D3DRS_ALPHAREF, 5);
        }
        else
        {
            AsteroidApplyRs(Pipe, kRsAsteroidOpaque);
            Pipe->SetRenderState(D3DRS_ALPHAREF, 0x7f);
        }
        return true;
    }

    // 0x00523f3e..0x0052402f / 0x0052449c..0x0052457b: pipeline state 7, read into a local seeded
    // with 1 (GetPipelineState, +0x18), picks the combined transform: MulTransform (DAT_006753d8
    // slot 7, +0x1c) of the view and the cube, copied, through SetModelview (+0x7c). Otherwise
    // SetWorld (+0x6c) with the cube, then SetView (+0x74) with the view.
    void AsteroidBindCubeTransform(FTransform const& CubeXform, FTransform const& ViewXform)
    {
        IRenderPipeline* const Pipe = DALib::Pipeline;
        int Combined = 1;
        Pipe->GetPipelineState(7, &Combined);
        if (Combined != 0)
        {
            FTransform Product;
            CliAsteroidMathEngine()->MulTransform(Product, ViewXform, CubeXform);
            FTransform const Copy = Product;
            Pipe->SetModelview(reinterpret_cast<float const*>(&Copy));
        }
        else
        {
            Pipe->SetWorld(reinterpret_cast<float const*>(&CubeXform));
            Pipe->SetView(reinterpret_cast<float const*>(&ViewXform));
        }
    }

    // 0x005240b8..0x005241cb / 0x0052458b..0x00524686: with DAT_00613855 set, a material alpha
    // below 1.0 (TEST AH,5 / JP) applies DAT_005dc394 + RS 0x18 = 5 once (the state byte), anything
    // else applies DAT_005dc374 + RS 0x18 = 0x7f every time and clears the byte. Then SetMaterial
    // (+0xac) and, unless DAT_006753e4, DrawIndexedPrimitiveVb (+0x120): the primitive +0xb0, the
    // lock's buffer and start vertex, +0x9c vertices, the indices +0xa8, +0xac of them, 0.
    void AsteroidDrawCube(FCliAsteroid* Field, float const* Material, unsigned char* bBlendState,
                          const FVertexMemoryLock* Desc)
    {
        IRenderPipeline* const Pipe = DALib::Pipeline;
        if (G_AsteroidAlphaSwitch != 0)
        {
            if (Material[3] < 1.0)
            {
                if (*bBlendState == 0)
                {
                    AsteroidApplyRs(Pipe, kRsAsteroidBlend);
                    Pipe->SetRenderState(D3DRS_ALPHAREF, 5);
                    *bBlendState = 1;
                }
            }
            else
            {
                AsteroidApplyRs(Pipe, kRsAsteroidOpaque);
                Pipe->SetRenderState(D3DRS_ALPHAREF, 0x7f);
                *bBlendState = 0;
            }
        }
        Pipe->SetMaterial((const D3DMATERIAL8*)Material);
        if (G_AsteroidSkipDraw == 0)
        {
            Pipe->DrawIndexedPrimitiveVb(Field->FieldB0, Desc->Listing.VertexBuffer,
                                         static_cast<unsigned int>(Desc->Listing.BaseVertex),
                                         Field->MeshDesc.SrcCount, Field->MeshIndices,
                                         Field->MeshIndexCount, 0);
        }
    }

    // .text:00522a60..0052470b (__thiscall on the field, RET 4: the camera). Two tests only -- the
    // common field (+0x04) and the byte +0x00; nothing on the camera, the zone, the pipeline or
    // the vertex-buffer manager.
#ifdef FREELANCER_RP9
    // Port-only parameters (CliAsteroidTickOne, the volumetric nebula's extended spawn
    // distance): FillOverride > 0 replaces the fill distance (fill_dist * detail) of the cube
    // walk, FadeBandOverride > 0 the width of the alpha fade before the reach (retail: half a
    // cube). Both 0: retail exactly.
    void CliAsteroidDrawNearField(FCliAsteroid* Field, FSpaceCamera* Cam, double FillOverride,
                                  double FadeBandOverride)
#else
    void CliAsteroidDrawNearField(FCliAsteroid* Field, FSpaceCamera* Cam)
#endif
    {
        if (Field->CmnField == nullptr)
            return;
        if (Field->Flag00 != 0)
            return;
        ICamera* const Camera = AsCamera(Cam);
        CmnAsteroid::CAsteroidField* const Cmn = Field->CmnField;

        // 0x00522a89..0x00522aa7: camera slot 16 (+0x40), the view transform, twelve dwords.
        FTransform ViewXform;
        memcpy(&ViewXform, Camera->GetViewTransform(), sizeof(ViewXform));

        // 0x00522a94..0x00522ae3: DAT_006753b0 bits 0 and 1 guard two function-local statics
        // whose atexit destructors (LAB_00524720 / LAB_00524710) are bare RETs: the cached eye
        // position DAT_00675380 and orientation DAT_0067538c.
        static float s_CachedPos[3];
        static float s_CachedOrient[9];

        // 0x00522ae6..0x00522b40: slots 6 and 7 (+0x18, +0x1c) in radians (DAT_005c77f4), stored
        // as floats; sine and cosine of pi/2 (DAT_005dc88c) minus each.
        float const FovY =
            static_cast<float>(static_cast<double>(Camera->GetFovYDeg()) * 0.017453292f);
#ifdef FREELANCER_WIDESCREEN_SUPPORT
        // Port-only Hor+: slot 6 keeps the 4:3 angle, but the side planes of this cull cone must
        // match the frustum, whose horizontal tangent is tan(fovy) * aspect.
        float const HalfFovX = static_cast<float>(
            atan(tan(static_cast<double>(FovY)) * static_cast<double>(Camera->GetAspect())));
#else
        float const HalfFovX =
            static_cast<float>(static_cast<double>(Camera->GetHalfFovXDeg()) * 0.017453292f);
#endif
        double const AngleX = static_cast<double>(1.5707964f) - HalfFovX;
        double const AngleY = static_cast<double>(1.5707964f) - FovY;
        float const SinX = static_cast<float>(sin(AngleX));
        float const CosX = static_cast<float>(cos(AngleX));
        float const SinY = static_cast<float>(sin(AngleY));
        float const CosY = static_cast<float>(cos(AngleY));

        // 0x00522b44..0x00522b88: slot 4 (+0x10) nine floats, slot 3 (+0x0c) the position, then
        // slot 6 once more with its result popped.
        float Orient9[9];
        memcpy(Orient9, Camera->GetOrientation(), sizeof(Orient9));
        float const* const Pos3 = Camera->GetPosition();
        Vector const CamPos = AsteroidVec(Pos3[0], Pos3[1], Pos3[2]);
        Camera->GetHalfFovXDeg();

        // 0x00522b83..0x00522d63: the orientation's three columns and the eye position, from the
        // cache while DAT_006753e0 is set, otherwise from the camera, refilling the cache.
        Vector Col0;
        Vector Col1;
        Vector Col2;
        Vector Eye;
        if (G_AsteroidUseOrientCache != 0)
        {
            Col0 = AsteroidVec(s_CachedOrient[0], s_CachedOrient[3], s_CachedOrient[6]);
            Col1 = AsteroidVec(s_CachedOrient[1], s_CachedOrient[4], s_CachedOrient[7]);
            Col2 = AsteroidVec(s_CachedOrient[2], s_CachedOrient[5], s_CachedOrient[8]);
            Eye = AsteroidVec(s_CachedPos[0], s_CachedPos[1], s_CachedPos[2]);
        }
        else
        {
            Col0 = AsteroidVec(Orient9[0], Orient9[3], Orient9[6]);
            Col1 = AsteroidVec(Orient9[1], Orient9[4], Orient9[7]);
            Col2 = AsteroidVec(Orient9[2], Orient9[5], Orient9[8]);
            memcpy(s_CachedOrient, Orient9, sizeof(s_CachedOrient));
            s_CachedPos[0] = CamPos.x;
            s_CachedPos[1] = CamPos.y;
            s_CachedPos[2] = CamPos.z;
            Eye = CamPos;
        }

        // 0x00522d6a..0x00522f3d: the four side-plane directions --
        //   D0 = Col0 * -SinX - Col2 * CosX, D1 = Col0 * SinX - Col2 * CosX,
        //   D2 = Col1 * -SinY - Col2 * CosY, D3 = Col1 * SinY - Col2 * CosY.
        Vector const BackX = AsteroidScale(Col2, CosX);
        Vector const D0 = AsteroidSub(AsteroidScale(Col0, -SinX), BackX);
        Vector const D1 = AsteroidSub(AsteroidScale(Col0, SinX), BackX);
        Vector const BackY = AsteroidScale(Col2, CosY);
        Vector const D2 = AsteroidSub(AsteroidScale(Col1, -SinY), BackY);
        Vector const D3 = AsteroidSub(AsteroidScale(Col1, SinY), BackY);

        // 0x00522f42..0x00522f93: margin = float(cube size * 1.25f) (DAT_005c9804); the near point
        // is the eye plus Col2 * margin.
        float const Margin = static_cast<float>(static_cast<double>(Field->CubeSize) * 1.25f);
        Vector const NearPt = AsteroidAdd(Eye, AsteroidScale(Col2, Margin));

        // 0x00522f98..0x00522fe2: the FOV tangents as floats; fill = FILD +0x24 * +0x230 on the
        // FPU; the two lateral extents _ftol(tan * fill).
        float const TanX = static_cast<float>(tan(static_cast<double>(HalfFovX)));
        float const TanY = static_cast<float>(tan(static_cast<double>(FovY)));
#ifdef FREELANCER_RP9
        double const Fill = FillOverride > 0.0
                                ? FillOverride
                                : static_cast<double>(Field->FillDistAt24) * Field->Scale230;
#else
        double const Fill = static_cast<double>(Field->FillDistAt24) * Field->Scale230;
#endif
        int const ExtX = AsteroidFtol(TanX * Fill);
        int const ExtY = AsteroidFtol(TanY * Fill);

        // 0x00522fe4..0x005230f9: the first seed, eye + Col2 * cube size, snapped
        // (closest_cube_pos) and set as both bounds on every axis.
        int MinX = 0;
        int MaxX = 0;
        int MinY = 0;
        int MaxY = 0;
        int MinZ = 0;
        int MaxZ = 0;
        {
            Vector const Seed =
                Cmn->closest_cube_pos(AsteroidAdd(AsteroidScale(Col2, Field->CubeSize), Eye));
            MinX = MaxX = AsteroidCubeCoord(Seed.x);
            MinY = MaxY = AsteroidCubeCoord(Seed.y);
            MinZ = MaxZ = AsteroidCubeCoord(Seed.z);
        }

        // 0x005230fe..0x005232a9: NegZ = -Col2 (kept for the depth test); the far centre
        // eye + NegZ * fill; the lateral offsets Col0 * ExtX and Col1 * ExtY.
        Vector const NegZ = AsteroidVec(-Col2.x, -Col2.y, -Col2.z);
        Vector const FarCentre = AsteroidAdd(AsteroidScale(NegZ, Fill), Eye);
        Vector const LatX = AsteroidScale(Col0, ExtX);
        Vector const LatY = AsteroidScale(Col1, ExtY);

        // 0x005232ae..0x0052378a: four more seeds, (far + X) + Y, (far + X) - Y, (far - X) - Y and
        // (far - X) + Y, each snapped and folded into the bounds.
        {
            Vector const FarXp = AsteroidAdd(FarCentre, LatX);
            Vector const FarXm = AsteroidSub(FarCentre, LatX);
            Vector Seeds[4];
            Seeds[0] = AsteroidAdd(FarXp, LatY);
            Seeds[1] = AsteroidSub(FarXp, LatY);
            Seeds[2] = AsteroidSub(FarXm, LatY);
            Seeds[3] = AsteroidAdd(FarXm, LatY);
            for (int I = 0; I < 4; ++I)
            {
                Vector const Snapped = Cmn->closest_cube_pos(Seeds[I]);
                AsteroidExtend(AsteroidCubeCoord(Snapped.x), &MinX, &MaxX);
                AsteroidExtend(AsteroidCubeCoord(Snapped.y), &MinY, &MaxY);
                AsteroidExtend(AsteroidCubeCoord(Snapped.z), &MinZ, &MaxZ);
            }
        }

        // 0x0052378e..0x005237b4: RS 0x18 saved (zeroed local); the drawn and visited counters.
        IRenderPipeline* const Pipe = DALib::Pipeline;
        int Saved18 = 0;
        Pipe->GetRenderState(D3DRS_ALPHAREF, &Saved18);
        unsigned Drawn = 0;
        unsigned Visited = 0;

        // 0x005237ba..0x005237ef: max_alpha (+0xcc) below 1.0f (TEST AH,5 / JNP keeps the 1) sets
        // both the blend-state byte and the fade byte.
        unsigned char bBlendState = Field->ScaleCC < 1.0f ? 1 : 0;
        unsigned char bFading = Field->ScaleCC < 1.0f ? 1 : 0;

        // 0x005237f4..0x0052385a: RS 0xe saved; a fading field turns RS 0xe off, clears the
        // registered lights (FUN_004101e0) and sets a white ambient (FUN_00410240).
        int SavedE = 0;
        Pipe->GetRenderState(D3DRS_ZWRITEENABLE, &SavedE);
        if (bFading != 0)
        {
            Pipe->SetRenderState(D3DRS_ZWRITEENABLE, 0);
            EngineLightClearRegisteredList();
            unsigned char const White[3] = {0xff, 0xff, 0xff};
            EngineLightSetAmbientRgbBytes(White);
        }

        // 0x0052385d..0x00523934: the 0x44-byte material, zeroed. Diffuse rgb = +0xb4..+0xbc.
        // With LIGHTING (option 0x18) on and no fade, ambient rgb = +0xc0..+0xc8; otherwise the
        // ambient is the diffuse, alpha included (still 0 here).
        float Material[17];
        memset(Material, 0, sizeof(Material));
        Material[0] = Field->TintB4;
        Material[1] = Field->TintB8;
        Material[2] = Field->TintBC;
        if (GetGameOption(kGameOptLighting) != 0 && bFading == 0)
        {
            Material[4] = Field->TintC0;
            Material[5] = Field->TintC4;
            Material[6] = Field->TintC8;
        }
        else
        {
            Material[4] = Material[0];
            Material[5] = Material[1];
            Material[6] = Material[2];
            Material[7] = Material[3];
        }

        // 0x0052393b..0x00523970: half a cube (DAT_005c75e0); the reach = fill + margin on the FPU,
        // stored as a float; the fade start float(reach - half cube).
        float const HalfCube = static_cast<float>(static_cast<double>(Field->CubeSize) * 0.5f);
#ifdef FREELANCER_RP9
        double const ReachD = Fill + Margin; // retail: FILD +0x24 * +0x230 + margin, = Fill
        float const Reach = static_cast<float>(ReachD);
        // Port-only: the fade band of the extended spawn distance (retail: half a cube).
        double const FadeWidth =
            FadeBandOverride > 0.0 ? FadeBandOverride : static_cast<double>(HalfCube);
        float const FadeStart = static_cast<float>(ReachD - FadeWidth);
#else
        double const ReachD =
            static_cast<double>(Field->FillDistAt24) * Field->Scale230 + Margin;
        float const Reach = static_cast<float>(ReachD);
        float const FadeStart = static_cast<float>(ReachD - HalfCube);
#endif

        // 0x00523977..0x0052399a: the camera's own cube id (from the camera position, not the
        // cached eye); the seen byte cleared; the first-draw byte set.
        unsigned long const PlayerCubeId =
            CmnAsteroid::compute_cube_id(Cmn->closest_cube_pos(CamPos));
        unsigned char bPlayerCubeSeen = 0;
        unsigned char bFirst = 1;
        FVertexMemoryLock Desc = {}; // [ESP+0x154]: buffer, FVF, start vertex, bits
        // [ESP+0x60]: the plane test's depth, read by the fade. DAT_00613854 is never cleared, so
        // the test always writes it first.
        double Depth = 0.0;

        // 0x0052399f..0x00523bdd: X, Y, Z from the minimum to the maximum in steps of +0x20 (re-read
        // at every step), signed compares.
        for (int X = MinX; X <= MaxX; X += Field->CubeSize)
        {
            for (int Y = MinY; Y <= MaxY; Y += Field->CubeSize)
            {
                for (int Z = MinZ; Z <= MaxZ; Z += Field->CubeSize)
                {
                    // 0x005239f3..0x00523a9b: the snapped cube position; its offset from the near
                    // point; visited += 1.
                    Vector const CubePos = Cmn->closest_cube_pos(AsteroidVec(
                        static_cast<float>(X), static_cast<float>(Y), static_cast<float>(Z)));
                    Vector const Delta = AsteroidSub(CubePos, NearPt);
                    ++Visited;

                    if (G_AsteroidPlaneTest != 0)
                    {
                        // 0x00523aa5..0x00523aea: depth = |(n.x*d.x + n.z*d.z) + n.y*d.y| with
                        // n = NegZ, kept as a double; beyond the reach (strictly) -> next cube.
                        Depth = fabs(AsteroidPlaneDot(NegZ, Delta));
                        if (Depth > Reach)
                            continue;
                        double const Inv = 1.0 / Depth;

                        // 0x00523af6..0x00523c18: sideways along Col0. Only a slope not within
                        // TanX (TEST AH,1 / JE passes TanX >= |slope|) tests a side plane: D0 for a
                        // positive slope, D1 otherwise; a dot ordered-less than 0 -> next cube.
                        double const SlopeX = AsteroidPlaneDot(Delta, Col0) * Inv;
                        if (!(TanX >= fabs(SlopeX)))
                        {
                            if (SlopeX > 0.0)
                            {
                                if (AsteroidPlaneDot(D0, Delta) < 0.0)
                                    continue;
                            }
                            else if (AsteroidPlaneDot(D1, Delta) < 0.0)
                                continue;
                        }

                        // 0x00523c1c..0x00523cca: the same along Col1 with TanY, D2 and D3.
                        double const SlopeY = AsteroidPlaneDot(Delta, Col1) * Inv;
                        if (!(TanY >= fabs(SlopeY)))
                        {
                            if (SlopeY > 0.0)
                            {
                                if (AsteroidPlaneDot(D2, Delta) < 0.0)
                                    continue;
                            }
                            else if (AsteroidPlaneDot(D3, Delta) < 0.0)
                                continue;
                        }
                    }

                    // 0x00523ccc..0x00523ce6: zone slot 0 (+0x00) strictly above 0.001f
                    // (DAT_005d1494), otherwise next cube.
                    if (!(Field->Zone->GetImmersion(CubePos) > 0.001f))
                        continue;

                    // 0x00523cec..0x00523d44: the frequency +0x28; with an exclusion manager
                    // (+0xdc), is_excluded (+0x04, type 0) with a -1.0f distance out; a distance
                    // not below 0 (TEST AH,1 / JNE) replaces the frequency when not excluded.
                    float Freq = Field->EmptyCubeFreq;
                    unsigned char bNotExcluded = 1;
                    if (Field->ExclusionMgr != nullptr)
                    {
                        float Dist = -1.0f;
                        bNotExcluded = Field->ExclusionMgr->is_excluded(
                                           CubePos, Exclusion::EOT_CliAsteroid, &Dist)
                                           ? 0
                                           : 1;
                        if (bNotExcluded != 0 && Dist >= 0.0f)
                            Freq = Dist;
                    }

                    // 0x00523d44..0x00523d6b: compute_cube_id; id 0 or excluded -> next cube; the
                    // camera's own id marks it seen.
                    unsigned long const CubeId = CmnAsteroid::compute_cube_id(CubePos);
                    if (CubeId == 0)
                        continue;
                    if (bNotExcluded == 0)
                        continue;
                    if (CubeId == PlayerCubeId)
                        bPlayerCubeSeen = 1;

                    // 0x00523d70..0x00523df6: an identity orientation through
                    // compute_cube_orientation(id, &m, common field +0x50, frequency); false ->
                    // next cube.
                    Matrix Orient(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
                    if (!CmnAsteroid::compute_cube_orientation(CubeId, &Orient, Cmn->Rotations,
                                                               Freq))
                        continue;

                    // 0x00523dfc..0x00523e37: the first cube uploads the mesh (byte cleared before
                    // the lock); a failed lock returns (0x005241e0..0x0052420b).
                    if (bFirst != 0)
                    {
                        bFirst = 0;
                        if (!AsteroidUploadMesh(Field, &Desc, 0x524))
                            return;
                    }

                    FTransform CubeXform;
                    memcpy(&CubeXform.Rotation, &Orient, sizeof(CubeXform.Rotation));
                    CubeXform.Translation.X = CubePos.x;
                    CubeXform.Translation.Y = CubePos.y;
                    CubeXform.Translation.Z = CubePos.z;
                    AsteroidBindCubeTransform(CubeXform, ViewXform);

                    // 0x00524032..0x005240b1: past the fade start (ordered less, TEST AH,5 / JP)
                    // the alpha is ((reach - depth) / half cube) * max_alpha, clamped to max_alpha
                    // above and to 0.0f below, stored in the material; at or under 0.001f (TEST
                    // AH,0x41 / JNP) -> next cube. Otherwise the alpha is max_alpha.
                    if (FadeStart < Depth)
                    {
#ifdef FREELANCER_RP9
                        double Alpha =
                            ((static_cast<double>(Reach) - Depth) / FadeWidth) * Field->ScaleCC;
#else
                        double Alpha =
                            ((static_cast<double>(Reach) - Depth) / HalfCube) * Field->ScaleCC;
#endif
                        if (Alpha > Field->ScaleCC)
                            Alpha = Field->ScaleCC;
                        else if (Alpha < 0.0)
                            Alpha = 0.0f;
                        Material[3] = static_cast<float>(Alpha);
                        if (Alpha <= 0.001f)
                            continue;
                    }
                    else
                    {
                        Material[3] = Field->ScaleCC;
                    }

                    // 0x005240b8..0x005241d1: blend switch, material, draw; drawn += 1.
                    AsteroidDrawCube(Field, Material, &bBlendState, &Desc);
                    ++Drawn;
                }
            }
        }

        // 0x0052420e..0x0052468c: when the walk never met the camera's own (non-zero) cube, that
        // cube is drawn on its own: snapped camera position, zone slot 0 above 0.001f, the same
        // exclusion and frequency rule, compute_cube_orientation, the upload if nothing was drawn
        // yet (line 0x580), the transform, the full max_alpha (no fade), blend switch, material,
        // draw.
        if (bPlayerCubeSeen == 0 && PlayerCubeId != 0)
        {
            Vector const PlayerPos = Cmn->closest_cube_pos(CamPos);
            if (Field->Zone->GetImmersion(PlayerPos) > 0.001f)
            {
                float Freq = Field->EmptyCubeFreq;
                unsigned char bNotExcluded = 1;
                if (Field->ExclusionMgr != nullptr)
                {
                    float Dist = -1.0f;
                    bNotExcluded = Field->ExclusionMgr->is_excluded(
                                       PlayerPos, Exclusion::EOT_CliAsteroid, &Dist)
                                       ? 0
                                       : 1;
                    if (bNotExcluded != 0 && Dist >= 0.0f)
                        Freq = Dist;
                }
                Matrix Orient(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
                if (bNotExcluded != 0 &&
                    CmnAsteroid::compute_cube_orientation(PlayerCubeId, &Orient, Cmn->Rotations,
                                                          Freq))
                {
                    if (bFirst != 0 && !AsteroidUploadMesh(Field, &Desc, 0x580))
                        return;
                    FTransform CubeXform;
                    memcpy(&CubeXform.Rotation, &Orient, sizeof(CubeXform.Rotation));
                    CubeXform.Translation.X = PlayerPos.x;
                    CubeXform.Translation.Y = PlayerPos.y;
                    CubeXform.Translation.Z = PlayerPos.z;
                    AsteroidBindCubeTransform(CubeXform, ViewXform);
                    Material[3] = Field->ScaleCC;
                    AsteroidDrawCube(Field, Material, &bBlendState, &Desc);
                    ++Drawn;
                }
            }
        }

        // 0x00524690..0x005246fd: RS 0x18 and RS 0xe restored; the frame counters accumulate
        // (triangles = (+0xac / 3) * drawn, unsigned).
        Pipe->SetRenderState(D3DRS_ALPHAREF, Saved18);
        Pipe->SetRenderState(D3DRS_ZWRITEENABLE, SavedE);
        G_AsteroidCubesDrawn += Drawn;
        G_AsteroidCubesVisited += Visited;
        G_AsteroidTrianglesDrawn += (Field->MeshIndexCount / 3) * Drawn;
    }

#ifdef FREELANCER_RP9
    // Port-only: CAsteroidField::near_field (Common .text:062a53c0) with an extended fill
    // distance -- the margin (cube + fill) * 1.74f around the zone, the zone's extent along the
    // line to its centre. The common field's own FillDist is left alone (it also drives the
    // server-side asteroid population).
    bool CliAsteroidNearFieldExtended(FCliAsteroid const* Field, Vector const& Pos, double Fill)
    {
        Universe::IZone const* const Zone = Field->Zone;
        if (Zone == nullptr)
            return true;
        double const Margin = (static_cast<double>(Field->CubeSize) + Fill) * 1.74;
        Vector const& Center = Zone->GetPos();
        double const Dx = static_cast<double>(Pos.x) - Center.x;
        double const Dy = static_cast<double>(Pos.y) - Center.y;
        double const Dz = static_cast<double>(Pos.z) - Center.z;
        double const Reach = static_cast<double>(Zone->GetRadius()) + Margin;
        double const DistSq = Dx * Dx + Dy * Dy + Dz * Dz;
        if (DistSq > Reach * Reach)
            return false;
        double const Dist = sqrt(DistSq);
        if (!(Dist > 0.0001))
            return true;
        Vector Dir;
        Dir.x = static_cast<float>(Dx / Dist);
        Dir.y = static_cast<float>(Dy / Dist);
        Dir.z = static_cast<float>(Dz / Dist);
        return Dist - Zone->GetRadiusAlong(Dir) <= Margin;
    }
#endif

    // FUN_005225d0 (thiscall on the field, RET 4: camera). No test of the field or the camera.
    void CliAsteroidTickOne(FCliAsteroid* Field, FSpaceCamera* Cam)
    {
        // 0x005225d9..0x005225f9: the ambient-done byte cleared; the camera position (vt+0xc)
        // copied.
        unsigned char bAmbientDone = 0;
        float const* const Pos3 = AsCamera(Cam)->GetPosition();
        Vector CamPos;
        CamPos.x = Pos3[0];
        CamPos.y = Pos3[1];
        CamPos.z = Pos3[2];

#ifdef FREELANCER_RP9
        // Port-only: under the Direct3D 9 volumetric nebula (nebula_volume.cpp) the haze is far
        // thinner than retail's linear fog, which hid the end of the cube walk (fill_dist *
        // detail + 1.25 cubes, faded over its last half cube) and the billboards' hand-over
        // there. While a drawn nebula overlaps this field the cube walk goes out to where the
        // haze leaves asteroid_spawn_transmittance (at most asteroid_max_spawn_distance), fades
        // over asteroid_fade_fraction of that depth, the billboards' near fade planes and the
        // exclusion zones' cut-off follow it, and the near-field test widens with it. 0 = retail.
        float SpawnFadeFraction = 0.0f;
        float const SpawnReach = NebulaVolume::AsteroidSpawnReach(Field->Zone, &SpawnFadeFraction);
        double ExtFill = 0.0;
        double ExtFadeBand = 0.0;
        if (SpawnReach > 0.0f)
        {
            double const RetailFill = static_cast<double>(Field->FillDistAt24) * Field->Scale230;
            double const Margin =
                static_cast<double>(static_cast<float>(static_cast<double>(Field->CubeSize) * 1.25f));
            double const HalfCube = static_cast<double>(Field->CubeSize) * 0.5;
            if (static_cast<double>(SpawnReach) - Margin > RetailFill)
            {
                ExtFill = static_cast<double>(SpawnReach) - Margin;
                ExtFadeBand = static_cast<double>(SpawnReach) * SpawnFadeFraction;
                if (ExtFadeBand < HalfCube)
                    ExtFadeBand = HalfCube;
            }
        }
#endif

        // 0x005225fd..0x00522658: [AsteroidBillboards] (+0x198), ASTEROID_EXT above 1 (signed
        // JLE), [EFFECTS], each read only when the one before passed, and FUN_0054e040 on the
        // billboard block: FUN_00522700(camera, 100.0f), the byte set, then
        // FUN_0054e900(camera, +0xdc, 2).
        if (Field->Flag198 != 0 && GetGameOption(kGameOptAsteroidExt) > 1 &&
            GetGameOption(kGameOptEffectsGroup) != 0 &&
            BillboardNearCamera(&Field->Billboard, CamPos))
        {
            CliAsteroidAmbientPass(Field, Cam, kAmbientScaleArg);
            bAmbientDone = 1;
#ifdef FREELANCER_RP9
            if (ExtFill > 0.0)
            {
                // Port-only: the billboards fade in over the cubes' extended fade band (retail:
                // from the fill distance to the reach, FUN_0054feb0), then the planes go back.
                float const SavedFar = Field->Billboard.Float40;
                float const SavedNear = Field->Billboard.Float44;
                Field->Billboard.Float40 = SpawnReach;
                Field->Billboard.Float44 = static_cast<float>(SpawnReach - ExtFadeBand);
                BillboardDraw(&Field->Billboard, Cam, Field->ExclusionMgr, 2);
                Field->Billboard.Float40 = SavedFar;
                Field->Billboard.Float44 = SavedNear;
            }
            else
#endif
                BillboardDraw(&Field->Billboard, Cam, Field->ExclusionMgr, 2);
        }

        // 0x0052265d..0x005226c4: ASTEROIDS, [OBJECTS], the common field (+4) and its
        // near_field(camera position): the ambient pass unless already done (the byte set
        // after it), FUN_00522a60, then the byte cleared again when max_alpha (+0xcc) is below
        // 1.0f (FCOMP DAT_005c75dc, TEST AH,5 / JP: only strictly less clears).
        if (GetGameOption(kGameOptAsteroids) != 0 && GetGameOption(kGameOptObjectsGroup) != 0 &&
            Field->CmnField != nullptr &&
#ifdef FREELANCER_RP9
            (ExtFill > 0.0 ? CliAsteroidNearFieldExtended(Field, CamPos, ExtFill)
                           : Field->CmnField->near_field(CamPos)))
#else
            Field->CmnField->near_field(CamPos))
#endif
        {
            if (bAmbientDone == 0)
            {
                CliAsteroidAmbientPass(Field, Cam, kAmbientScaleArg);
                bAmbientDone = 1;
            }
#ifdef FREELANCER_RP9
            // Port-only: the fading cubes write depth while translucent; the volume adds the
            // medium behind them for the part they let through (their alpha reaches 0 at the
            // view depth ExtFill, see CliAsteroidDrawNearField).
            if (ExtFill > 0.0)
                NebulaVolume::NoteAsteroidFade(static_cast<float>(ExtFill),
                                               static_cast<float>(ExtFadeBand),
                                               static_cast<float>(Field->CubeSize));
            if (ExtFill > 0.0 && Field->ExclusionMgr != nullptr)
            {
                // Port-only: the exclusion zones are cut off at fill * 1.25 from the camera
                // (FUN_00524880, MaxCliDist); the extended walk needs them as far as it goes.
                float const SavedDistSq =
                    FCliAsteroidExclusionInline::GetMaxCliDistSq(Field->ExclusionMgr);
                double const ExtDist = ExtFill * 1.25;
                float const ExtDistSq = static_cast<float>(ExtDist * ExtDist);
                if (ExtDistSq > SavedDistSq)
                    FCliAsteroidExclusionInline::SetMaxCliDistSq(Field->ExclusionMgr, ExtDistSq);
                CliAsteroidDrawNearField(Field, Cam, ExtFill, ExtFadeBand);
                FCliAsteroidExclusionInline::SetMaxCliDistSq(Field->ExclusionMgr, SavedDistSq);
            }
            else
                CliAsteroidDrawNearField(Field, Cam, ExtFill, ExtFadeBand);
#else
            CliAsteroidDrawNearField(Field, Cam);
#endif
            if (Field->ScaleCC < 1.0f)
                bAmbientDone = 0;
        }

        // 0x005226c9..0x005226f3: every DynamicAsteroids record (+0x224..+0x228) gets
        // FUN_005261b0(camera, &byte, field) (cli_asteroid_dynamic.cpp).
        for (void** It = Field->DynBegin; It != Field->DynEnd; ++It)
            CliDynamicAsteroidDraw(static_cast<FCliDynamicAsteroid*>(*It), Cam, &bAmbientDone,
                                   Field);
    }
} // namespace

void CliAsteroidLightAround(FCliAsteroid* Field, FSpaceCamera* Cam, float Scale)
{
    // FUN_00522700 -- the body is CliAsteroidAmbientPass above.
    CliAsteroidAmbientPass(Field, Cam, Scale);
}

void CliAsteroidFillVertexSource(FRpVertexSrcDesc* Desc, void* Verts, unsigned short VertCount)
{
    // FUN_0051feb0 -- the body is CliAsteroidFillMeshDesc above.
    CliAsteroidFillMeshDesc(Desc, Verts, VertCount);
}

void CliAsteroidTickFields(FSpaceCamera* Cam)
{
    // .text:00520170..005201b5 (cdecl, the camera). ASTEROIDS (option 0x21) off -> return; the
    // three frame counters DAT_006753b4/b8/bc zeroed; every registered field (DAT_006753c4 list)
    // -> FUN_005225d0(camera). No test of the camera.
    if (GetGameOption(kGameOptAsteroids) == 0)
        return;
    G_AsteroidTrianglesDrawn = 0;
    G_AsteroidCubesVisited = 0;
    G_AsteroidCubesDrawn = 0;

    std::list<FCliAsteroid*>& List = Registry();
    for (std::list<FCliAsteroid*>::iterator It = List.begin(); It != List.end(); ++It)
        CliAsteroidTickOne(*It, Cam);
}

namespace
{
    // FUN_00422b80 — write three floats at This.
    void BandSetVec3(float* Dst, float X, float Y, float Z)
    {
        Dst[0] = X;
        Dst[1] = Y;
        Dst[2] = Z;
    }

    // MSVC `n & 1` / even test used at 0054ce17..0054ce24 (0x80000001 idiom).
    bool BandDiffIsEven(int Diff)
    {
        unsigned U = static_cast<unsigned>(Diff) & 0x80000001u;
        if (static_cast<int>(U) < 0)
            U = (U - 1u) | 0xfffffffeu;
        return U == 0;
    }

    // FUN_0054c8b0 — thiscall on Band/ExclusionBand 0x84 blob.
    // Builds an elliptical particle ring around IZone (+4): count from zone size × π /
    // (texture_aspect × height), positions via cos/sin in zone XZ, TransformMatrix by
    // zone orientation + Pos, per-particle XZ normals, then render_parts spokes (+0x60).
    void BandBuildExclusionRings(unsigned char* Blob)
    {
        if (Blob == nullptr)
            return;

        void* OldParticles = *reinterpret_cast<void**>(Blob + 0x3c);
        if (OldParticles != nullptr)
            operator delete(OldParticles);

        Blob[0x7c] = 0;
        *BandU(Blob, 0x20) = 0;

        Universe::IZone* Zone = *reinterpret_cast<Universe::IZone**>(Blob + 4);
        if (Zone == nullptr)
            return;

        // Retail IZone vtbl +0x28 GetSize → three floats (Size.x/y/z).
        float SizeX = Zone->Size.x;
        float SizeY = Zone->Size.y;
        float SizeZ = Zone->Size.z;
        if (SizeZ < 0.1f)
            SizeZ = SizeX;

        float const Aspect = *BandF(Blob, 0x10); // +0x40 texture_aspect
        float Height = *BandF(Blob, 0x12);       // +0x48 height
        int VertInc = static_cast<int>(*BandU(Blob, 0x13)); // +0x4c vert_increase

        // count = ftol(((sx+sz)*π) / (aspect*height) + 0.5) * vert_increase
        float const CountF =
            ((SizeX + SizeZ) * 3.14159274f) / (Aspect * Height) + 0.5f;
        int PointCount = FloatToIntTrunc(CountF) * VertInc;
        *BandU(Blob, 0x11) = static_cast<unsigned>(PointCount); // +0x44

        float* Particles = nullptr;
        if (PointCount > 0)
        {
            Particles = static_cast<float*>(operator new(static_cast<size_t>(PointCount) * 0x3c));
            if (Particles != nullptr)
            {
                float* Cursor = Particles;
                for (int I = 0; I < PointCount; ++I)
                {
                    BandSetVec3(Cursor + 7, 1.0f, 1.0f, 1.0f);           // +0x1C
                    BandSetVec3(Cursor + 10, 0.2f, 0.2f, 0.2f);          // +0x28
                    Cursor[13] = 1.0f;                                   // +0x34
                    Cursor[3] = 1000.0f;                                 // +0x0C
                    *reinterpret_cast<unsigned char*>(Cursor + 14) = 1; // +0x38
                    Cursor += 15; // 0x3C / 4
                }
            }
        }
        *reinterpret_cast<float**>(Blob + 0x3c) = Particles;

        // Fade distances × height, then height *= 0.5.
        *BandF(Blob, 0x19) = *BandF(Blob, 0x19) * Height;
        *BandF(Blob, 0x1a) = *BandF(Blob, 0x1a) * Height;
        *BandF(Blob, 0x1b) = *BandF(Blob, 0x1b) * Height;
        *BandF(Blob, 0x1c) = *BandF(Blob, 0x1c) * Height;
        Height = Height * 0.5f;
        *BandF(Blob, 0x12) = Height;

        // Second GetSize + GetOrientation (+0x14) + GetPosition (+0x10).
        SizeX = Zone->Size.x;
        SizeY = Zone->Size.y;
        SizeZ = Zone->Size.z;
        if (SizeZ < 0.1f)
            SizeZ = SizeX;

        Matrix const& Orient = Zone->GetOrientation();
        FMatrix MathOrient;
        for (int R = 0; R < 3; ++R)
        {
            for (int C = 0; C < 3; ++C)
                MathOrient.D[R][C] = Orient.E[R][C];
        }
        float const PosX = Zone->Pos.x;
        float const PosY = Zone->Pos.y;
        float const PosZ = Zone->Pos.z;

        float const Offset = *BandF(Blob, 0x1e); // +0x78
        float RadiusX = 0.0f;
        float RadiusY = 0.0f;
        float RadiusZ = 0.0f;
        if (Blob[0x74] == 0)
        {
            // offset_dist: radii = size - offset
            RadiusX = SizeX - Offset;
            RadiusY = SizeY - Offset;
            RadiusZ = SizeZ - Offset;
        }
        else
        {
            // offset_percent: radii = size * offset
            RadiusX = SizeX * Offset;
            RadiusY = SizeY * Offset;
            RadiusZ = SizeZ * Offset;
        }
        (void)RadiusY;

        float* P = Particles;
        if (P != nullptr && PointCount > 0)
        {
            for (int I = 0; I < PointCount; ++I)
            {
                float const Angle =
                    (static_cast<float>(I) / static_cast<float>(PointCount)) * 6.28318548f;
                float const LocalX = cosf(Angle) * RadiusX;
                float const LocalZ = sinf(Angle) * RadiusZ;
                FVector Local;
                Local.X = LocalX;
                Local.Y = 0.0f;
                Local.Z = LocalZ;
                FVector World;
                // 0x0054cb06..0x0054cb27: DAT_00679860 tested inside the loop, FUN_005b73e0
                // when still null, then vt+0x2c TransformMatrix. No fallback in retail.
                if (G_CloudsMath == nullptr)
                    G_CloudsMath = Acquire3DMathEngine();
                G_CloudsMath->TransformMatrix(World, MathOrient, Local);
                P[0] = World.X + PosX;
                P[1] = World.Y + PosY;
                P[2] = World.Z + PosZ;
                P[3] = Height;
                P += 15;
            }
        }

        // Per-particle normals from neighbour tangents (0054cb81..0054cd43).
        // Retail multiplies several terms by DAT_005c7474 (0.0); kept verbatim.
        PointCount = static_cast<int>(*BandU(Blob, 0x11));
        unsigned char* Base = *reinterpret_cast<unsigned char**>(Blob + 0x3c);
        if (Base != nullptr && PointCount > 0)
        {
            float* NrmAt = reinterpret_cast<float*>(Base + 8);
            for (int I = 1; I <= PointCount; ++I)
            {
                int Prev = I - 2;
                if (Prev < 0)
                    Prev += PointCount;
                int Next = I;
                if (Next >= PointCount)
                    Next -= PointCount;

                float* Cur = NrmAt - 2; // particle pos at -8 bytes from +8
                float* NextP = reinterpret_cast<float*>(Base + Next * 0x3c);
                float* PrevP = reinterpret_cast<float*>(Base + Prev * 0x3c);

                float DxNext = NextP[0] - Cur[0];
                float DyNext = NextP[1] - Cur[1];
                float DzNext = NextP[2] - Cur[2];
                float DxPrev = Cur[0] - PrevP[0];
                float DyPrev = Cur[1] - PrevP[1];
                float DzPrev = Cur[2] - PrevP[2];

                float N1y0 = DyNext * 0.0f;
                float N1z = DzNext - N1y0;
                float N1x = DxNext * 0.0f - DzNext * 0.0f;
                float N1y = N1y0 - DxNext;
                float Inv1 = 1.0f / sqrtf(N1x * N1x + N1y * N1y + N1z * N1z);
                N1z *= Inv1;
                N1x *= Inv1;
                N1y *= Inv1;

                float N2z = DzPrev - DyPrev * 0.0f;
                float N2x = DxPrev * 0.0f - DzPrev * 0.0f;
                float N2y = DyPrev * 0.0f - DxPrev;
                float Inv2 = 1.0f / sqrtf(N2x * N2x + N2y * N2y + N2z * N2z);
                N2z *= Inv2;
                N2x *= Inv2;
                N2y *= Inv2;

                float Nx = N2z + N1z;
                float Ny = N2x + N1x;
                float Nz = N2y + N1y;
                float InvN = 1.0f / sqrtf(Nx * Nx + Ny * Ny + Nz * Nz);
                NrmAt[2] = Nx * InvN; // +0x10
                NrmAt[3] = Ny * InvN; // +0x14
                NrmAt[4] = Nz * InvN; // +0x18

                NrmAt += 15;
            }
        }

        // Clamp render_parts to pointCount/2 when larger (0054cd43).
        int RenderParts = static_cast<int>(*BandU(Blob, 0x16)); // +0x58
        float const HalfCount = static_cast<float>(PointCount) * 0.5f;
        if (static_cast<float>(RenderParts) > HalfCount)
        {
            RenderParts = FloatToIntTrunc(HalfCount);
            *BandU(Blob, 0x16) = static_cast<unsigned>(RenderParts);
        }

        void* Spokes = operator new(static_cast<size_t>(RenderParts) * 0x18);
        *reinterpret_cast<void**>(Blob + 0x60) = Spokes;

        if (Spokes != nullptr && RenderParts > 0 && PointCount > 0)
        {
            float const Step = static_cast<float>(PointCount) / static_cast<float>(RenderParts);
            float Acc = 0.0f;
            int StartIdx = 0;
            unsigned char* SpokeBytes = static_cast<unsigned char*>(Spokes);
            for (int I = 0; I < RenderParts; ++I)
            {
                Acc += Step;
                *reinterpret_cast<int*>(SpokeBytes) = StartIdx;
                int const EndIdx = FloatToIntTrunc(Acc + 0.0001f);
                *reinterpret_cast<int*>(SpokeBytes + 4) = EndIdx;
                StartIdx = EndIdx;
                SpokeBytes += 0x18;
            }
        }

        if (Spokes != nullptr && RenderParts > 0)
        {
            unsigned char* SpokeBytes = static_cast<unsigned char*>(Spokes);
            float* PartBase = *reinterpret_cast<float**>(Blob + 0x3c);
            for (int I = 0; I < RenderParts; ++I)
            {
                int I0 = *reinterpret_cast<int*>(SpokeBytes);
                int I1 = *reinterpret_cast<int*>(SpokeBytes + 4);
                int Diff = I1 - I0;
                int MidOff = FloatToIntTrunc(static_cast<float>(Diff) * -0.5f);
                int Idx = I0 - MidOff;
                if (Idx >= PointCount)
                    Idx -= PointCount;

                float* OutPos = reinterpret_cast<float*>(SpokeBytes + 8);
                if (BandDiffIsEven(Diff))
                {
                    float* A = PartBase + Idx * 15;
                    int Idx2 = Idx + 1;
                    if (Idx2 >= PointCount)
                        Idx2 -= PointCount;
                    float* B = PartBase + Idx2 * 15;
                    OutPos[0] = (A[0] + B[0]) * 0.5f;
                    OutPos[1] = (A[1] + B[1]) * 0.5f;
                    OutPos[2] = (A[2] + B[2]) * 0.5f;
                }
                else
                {
                    float* A = PartBase + Idx * 15;
                    OutPos[0] = A[0];
                    OutPos[1] = A[1];
                    OutPos[2] = A[2];
                }

                if (I0 >= PointCount)
                    I0 -= PointCount;
                if (I1 >= PointCount)
                    I1 -= PointCount;

                float* P0 = PartBase + I0 * 15;
                float* P1 = PartBase + I1 * 15;
                float D0x = OutPos[0] - P0[0];
                float D0y = OutPos[1] - P0[1];
                float D0z = OutPos[2] - P0[2];
                float Dist0 = sqrtf(D0x * D0x + D0y * D0y + D0z * D0z);
                float D1x = OutPos[0] - P1[0];
                float D1y = OutPos[1] - P1[1];
                float D1z = OutPos[2] - P1[2];
                float Dist1 = sqrtf(D1x * D1x + D1y * D1y + D1z * D1z);
                if (Dist0 < Dist1)
                    Dist0 = Dist1;
                *reinterpret_cast<float*>(SpokeBytes + 0x14) = Dist0;

                SpokeBytes += 0x18;
            }
        }
    }
}
