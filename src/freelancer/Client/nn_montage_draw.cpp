// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "nn_montage_draw.h"

#include "screen_quad_state.h"

#include "DALib.h"
#include "common_ini.h"
#include "common_misc.h"   // GetFileSysPath
#include "dacom.h"
#include "render/IRenderPipeline.h"
#include "render/shading.h"      // ITextureLibrary2 (DALib::TextureLib slot 22)
#include "space_camera.h"        // AsCamera
#include "win3d.h"               // Win3DGetCamera (DAT_0067dbf8)

#include <cstring>

// Static initialisers in retail XC order: LAB_00559830 (0x0060fdb8), LAB_005598b0 (0x0060fdbc),
// LAB_005598e0 (0x0060fdc0).
std::vector<FButtonMontageTile> G_ButtonMontageTiles; // DAT_00679a44
FShapeDB G_ButtonShapeDB;                             // DAT_00679a2c
unsigned long G_ButtonsMaterialKey = DACOM_CRC::GetCRC32("buttons"); // DAT_00679a28
bool G_bButtonMontageLoaded = false;                  // DAT_00679af0

namespace
{
    FTexShape* G_ButtonShapeMontage32 = nullptr;        // DAT_00679af4
    FTexShape* G_ButtonShapeMontage64 = nullptr;        // DAT_00679af8
    FTexShape* G_ButtonShapeTargetingElement = nullptr; // DAT_00679afc
    FTexShape* G_ButtonShapeTalkElement = nullptr;      // DAT_00679b00

}

void LoadButtonMontage()
{
    // FUN_00559db0.
    if (G_bButtonMontageLoaded)
        return;
    G_bButtonMontageLoaded = true;

    // 0x00559ddc..0x00559df6: FUN_00425b30 on DAT_00679a2c.
    G_ButtonShapeDB.Load("Interface\\ButtonTextures.ini", DALib::Data);

    // 0x00559dfb..0x0055a003: four inlined ShapeDB finds (cmn\shapedb.h:0xf2 on a miss; the
    // last one through FUN_00512660, the same cached find).
    G_ButtonShapeMontage32 = G_ButtonShapeDB.Find("Montage32", kShapeDbHeaderLower);
    G_ButtonShapeMontage64 = G_ButtonShapeDB.Find("Montage64", kShapeDbHeaderLower);
    G_ButtonShapeTargetingElement =
        G_ButtonShapeDB.Find("HUD_targetingelement", kShapeDbHeaderLower);
    G_ButtonShapeTalkElement = G_ButtonShapeDB.Find("HUD_talkelement", kShapeDbHeaderLower);

    // 0x0055a003..0x0055a062.
    char Path[0x100];
    GetFileSysPath(Path, "interface\\ButtonMontage.ini", DALib::Data);
    INI_Reader Reader;
    if (!Reader.open(Path, false) || !Reader.read_header())
        return;

    do
    {
        // 0x0055a070..0x0055a098: per section.
        float Step = 0.0f;
        float U = 0.0f;
        float V = 0.0f;
        int TextureIndex = 0;
        int Size = 0;
        if (!Reader.is_header("Montage") || !Reader.read_value())
            continue;

        do
        {
            if (Reader.is_value("size"))
            {
                // 0x0055a0e4..0x0055a11c: 1 / (256 / size), 1 / 4 for a non-positive size.
                Size = Reader.get_value_int(0);
                int TilesPerRow = 4;
                if (Size > 0)
                    TilesPerRow = 0x100 / Size;
                Step = static_cast<float>(1.0 / static_cast<double>(TilesPerRow));
                U = 0.0f;
                V = 0.0f;
            }
            else if (Reader.is_value("texture"))
            {
                TextureIndex = Reader.get_value_int(0);
            }
            else if (Reader.is_value("num_pages"))
            {
                Reader.get_value_int(0);
            }
            else
            {
                // 0x0055a163..0x0055a19f: the shape the tile sits on.
                FTexShape* Shape = nullptr;
                if (TextureIndex == 2)
                {
                    Shape = G_ButtonShapeTargetingElement;
                }
                else if (TextureIndex == 3)
                {
                    Shape = G_ButtonShapeTalkElement;
                    Step = 0.5f;
                }
                else if (Size == 0x20)
                {
                    Shape = G_ButtonShapeMontage32;
                }
                else if (Size == 0x40)
                {
                    Shape = G_ButtonShapeMontage64;
                }

                // 0x0055a19f..0x0055a1c9.
                float Scale = 0.0f;
                if (!Reader.is_value_empty(1))
                    Scale = Reader.get_value_float(1);

                // 0x0055a1cd..0x0055a261: FUN_0055d870 -- insert one element at end().
                FButtonMontageTile Tile;
                Tile.Name = Reader.get_name_ptr();
                Tile.U = U;
                Tile.V = V;
                Tile.Step = Step;
                Tile.Shape = Shape;
                Tile.Scale = Scale;
                G_ButtonMontageTiles.push_back(Tile);

                // 0x0055a267..0x0055a2a1: FST stores the rounded sum, FCOMP tests the unrounded
                // one against 0.99f (DAT_005e1518); only an ordered > moves to the next row.
                double const NextU = static_cast<double>(Step) + static_cast<double>(U);
                U = static_cast<float>(NextU);
                if (NextU > static_cast<double>(0.99f))
                {
                    U = 0.0f;
                    double const NextV = static_cast<double>(Step) + static_cast<double>(V);
                    V = static_cast<float>(NextV);
                    if (NextV > static_cast<double>(0.99f))
                        V = 0.0f;
                }
            }
        } while (Reader.read_value());
    } while (Reader.read_header());
}

FButtonMontageTile* FindButtonMontageTile(std::string Name)
{
    // FUN_0055a360.
    if (!G_bButtonMontageLoaded)
        LoadButtonMontage();

    int const Count = static_cast<int>(G_ButtonMontageTiles.size());
    for (int Index = 0; Index < Count; ++Index)
    {
        // DACOM stricmp(entry, Name); a null string buffer reads as "".
        if (_stricmp(G_ButtonMontageTiles[Index].Name.c_str(), Name.c_str()) == 0)
            return &G_ButtonMontageTiles[Index];
    }
    return nullptr;
}

void ClearButtonMontage()
{
    // FUN_00559d20: erase(begin, end), then JMP FUN_00425a20 on DAT_00679a2c.
    G_ButtonMontageTiles.clear();
    G_ButtonShapeDB.Clear();
}

bool NnMontageDrawTileAt(FButtonMontageTile const* Tile, float const* Position,
                         unsigned char const* Rgba, float Size, int Mode, float Angle)
{
    // FUN_00559900. 0x00559904..0x0055991a: nothing without a tile or its shape.
    if (Tile == nullptr)
        return false;
    if (Tile->Shape == nullptr)
        return false;

    // 0x00559921..0x0055994e: frame 0 of the shape's texture record (handle, u0, v0, u1, v1).
    unsigned long Frame[5];
    DALib::TextureLib->GetTextureFrame(Tile->Shape->Texture, 0, Frame);

    // 0x00559952..0x00559a20: the tile's square, inset by 1/256, mirrored per Mode.
    double const Inset = static_cast<double>(0.00390625f);   // DAT_005d748c
    float const Low0 = static_cast<float>(static_cast<double>(Tile->U) + Inset);
    float const Low1 = static_cast<float>(static_cast<double>(Tile->V) + Inset);
    float const High0 = static_cast<float>(
        static_cast<double>(Tile->Step) + static_cast<double>(Tile->U) - Inset);
    float const High1 = static_cast<float>(
        static_cast<double>(Tile->Step) + static_cast<double>(Tile->V) - Inset);
    float Rect[4];
    bool bRect = true;
    switch (Mode)
    {
    case 0:
        Rect[0] = Low0;
        Rect[1] = Low1;
        Rect[2] = High0;
        Rect[3] = High1;
        break;
    case 1:
        Rect[0] = High0;
        Rect[1] = Low1;
        Rect[2] = Low0;
        Rect[3] = High1;
        break;
    case 2:
        Rect[0] = Low0;
        Rect[1] = High1;
        Rect[2] = High0;
        Rect[3] = Low1;
        break;
    case 3:
        Rect[0] = High0;
        Rect[1] = High1;
        Rect[2] = Low0;
        Rect[3] = Low1;
        break;
    default:
        bRect = false;   // JA caseD_4: the frame's own rect stays
        break;
    }
    if (bRect)
        memcpy(&Frame[1], Rect, sizeof(Rect));

    // 0x00559a24..0x00559a72: the "buttons" key, the batch node, the icon (the RGBA bytes as a
    // D3DCOLOR A:R:G:B).
    ScreenQuadSetMaterialKey(G_ButtonsMaterialKey);   // FUN_00402d30(DAT_00679a28)
    ScreenQuadBeginBatchFrame(Frame, 2, true);        // FUN_00402e30(&frame, 2, 1)
    unsigned long const Argb = (static_cast<unsigned long>(Rgba[3]) << 24) |
                               (static_cast<unsigned long>(Rgba[0]) << 16) |
                               (static_cast<unsigned long>(Rgba[1]) << 8) |
                               static_cast<unsigned long>(Rgba[2]);
    ScreenQuadEnqueueIcon(AsCamera(Win3DGetCamera()), Position, Argb, Size, Angle, 0);
    return true;
}

void NnMontageFlushButtons()
{
    // FUN_00559c20, listing-traced 0x00559c20..0x00559d13. Two stack savers on DALib::Pipeline
    // (slot 61 read, slot 60 write; EH states 0 / 1): 0x17 -> 8, 0xe -> 0. FUN_00403ea0 on the
    // "buttons" list DAT_00679a28 with DALib::Pipeline read again. Then 0xe restored, then 0x17
    // (0x00559cc7..0x00559cff). No null check anywhere; no material-key call -- FUN_00403ea0
    // finds the "buttons" container by its key.
    IRenderPipeline* const Pipe17 = DALib::Pipeline;
    int Saved17 = 0;
    Pipe17->GetRenderState(D3DRS_ZFUNC, &Saved17);
    Pipe17->SetRenderState(D3DRS_ZFUNC, 8);
    IRenderPipeline* const Pipe0E = DALib::Pipeline;
    int Saved0E = 0;
    Pipe0E->GetRenderState(D3DRS_ZWRITEENABLE, &Saved0E);
    Pipe0E->SetRenderState(D3DRS_ZWRITEENABLE, 0);
    ScreenQuadFlush(DALib::Pipeline, G_ButtonsMaterialKey);
    Pipe0E->SetRenderState(D3DRS_ZWRITEENABLE, Saved0E);
    Pipe17->SetRenderState(D3DRS_ZFUNC, Saved17);
}
