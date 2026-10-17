// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail ButtonMontage -- the tile atlas behind every 2D HUD / NeuroNet icon ("montage" tiles):
// interface\ButtonMontage.ini names square tiles on the ButtonTextures.ini shapes, and
// FUN_00559900 draws one tile through the screen-quad batcher under the "buttons" material key.
//
// Globals (.data):
//   DAT_00679a28 -- GetCRC32("buttons"), static initialiser LAB_005598e0
//   DAT_00679a2c -- the ButtonTextures ShapeDB (ctor FUN_0041dd20 from LAB_005598b0, atexit
//                   LAB_005598d0 -> FUN_0041dc50)
//   DAT_00679a44 -- std::vector<tile> (allocator byte, then begin / end / capacity at
//                   DAT_00679a48 / DAT_00679a4c / DAT_00679a50); LAB_00559830 zeroes it,
//                   LAB_00559860 (atexit) destroys every element and frees the buffer
//   DAT_00679af0 -- loaded flag (FUN_00559db0 sets it; nothing clears it)
//   DAT_00679af4 / DAT_00679af8 / DAT_00679afc / DAT_00679b00 -- the "Montage32", "Montage64",
//                   "HUD_targetingelement" and "HUD_talkelement" shapes

#include "texture_shapes.h"

#include <string>
#include <vector>

// The 0x24-byte vector element FUN_00559db0 appends (the VC6 std::string is its first 0x10 bytes;
// FUN_004dd9a0 is that string's destructor).
struct FButtonMontageTile
{
    std::string Name;   // +0x00 the ini key
    float U;            // +0x10 upper-left u on the shape
    float V;            // +0x14 upper-left v
    float Step;         // +0x18 tile size in uv (1 / (256 / size), or 0.5 for texture = 3)
    FTexShape* Shape;   // +0x1c the ButtonTextures shape the tile sits on (may be null)
    float Scale;        // +0x20 the key's optional second value (FUN_00559aa0 reads it), else 0
};

extern unsigned long G_ButtonsMaterialKey;                  // DAT_00679a28
extern FShapeDB G_ButtonShapeDB;                            // DAT_00679a2c
extern std::vector<FButtonMontageTile> G_ButtonMontageTiles; // DAT_00679a44
extern bool G_bButtonMontageLoaded;                         // DAT_00679af0

// FUN_00559db0 (cdecl) -- once (DAT_00679af0): load Interface\ButtonTextures.ini into
// DAT_00679a2c, find the four shapes, then read every [Montage] section of
// interface\ButtonMontage.ini ("size", "texture", "num_pages" and one key per tile).
void LoadButtonMontage();

// FUN_0055a360 (cdecl, the string by value) -- load on first use, then the first tile whose name
// _stricmp-equals Name, or null.
FButtonMontageTile* FindButtonMontageTile(std::string Name);

// FUN_00559d20 (cdecl) -- empty the tile vector, then the ShapeDB (FUN_00425a20). The loaded
// flag stays set. Caller: the client teardown 0x005b2237.
void ClearButtonMontage();

// FUN_00559900 (cdecl) in its retail form -- false without a tile or without its shape; else
// the shape's texture frame (DALib::TextureLib slot 22, frame 0), its UV rect replaced by the
// tile's square inset by 1/256 (DAT_005d748c) and mirrored per Mode (0 plain, 1 horizontal,
// 2 vertical, 3 both; other values keep the frame's rect), the "buttons" material key
// (FUN_00402d30), the batch node for the frame (FUN_00402e30, blend 2, find), and one icon of
// Size at Position through the Win3D camera (FUN_004034a0(&DAT_0067dbf8, ..., Angle, 0)) in
// the colour of the RGBA bytes. True then. Callers: the 21 HUD_Targetable_Objects sites the
// XREF header lists (FUN_004ef030, FUN_004f0620, FUN_004f0900) and ButtonControl's body draw
// FUN_00559aa0 (0x00559b86, missing from that header; Mode 0, Angle 0).
bool NnMontageDrawTileAt(FButtonMontageTile const* Tile, float const* Position,
                         unsigned char const* Rgba, float Size, int Mode, float Angle);

// FUN_00559c20 — flush the "buttons" material batch (ScreenQuadFlush).
void NnMontageFlushButtons();
