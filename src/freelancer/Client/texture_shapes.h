// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Texture-shape library -- E:\FL\Scratch\Source\Client\cmn\ShapeDB.cpp and cmn\ShapeDB.h.
//
// A "shape" is a named sub-rectangle of a texture: the UV rect plus a centred scale/offset pair
// that the FX and UI code multiplies into its quads. Retail keeps shapes in ShapeDB objects --
// a std::map<TString<24>, shape> compared with _stricmp, plus a last-found cache -- and there
// are several of them, each filled by FUN_00425b30 from its own INI:
//
//   DAT_00616728  mouse.ini                        (FUN_0041e5c0; cleared 0041e727)
//   DAT_00673520  [HUD] shapes =                   (FUN_004c73d0; cleared by FUN_004c77a0)
//   DAT_00674f24  every [Data] effect_shapes =     (FUN_004f70d0; cleared by FUN_004f7140)
//   DAT_006750a0  solar\StarArch.ini               (FUN_00510f90; cleared 00511010)
//   DAT_006798ac  the system's [TexturePanels]     (00553e49; cleared 00554c63)
//   DAT_00679a2c  Interface\ButtonTextures.ini     (00559df6; cleared 00559d9c)
//   DAT_0067d9c8  Interface\UI\UITextures.ini      (FUN_00595420; cleared by LAB_00595400)
//   nebula +0x04, TexturePanels +0x04, asteroid field +0xe0, nav map +0x880 (per object)
//
// The INI understands two section types:
//
//   [Texture]                       [Shape]
//   file        = fx\efx.txm        name       = <shape>
//   name        = <texture>         shape_name = <shape>
//   texture_name= <texture>         x / y / w / h = <float>
//   shape_name  = <shape>           upper_left  = u, v
//   tex_shape   = <texture+shape>   lower_right = u, v   (each +1)
//   dim         = x,y,w,h[,ul,lr]   dim = x,y,w,h,ulu,ulv,lru,lrv

#include "common_tstring.h"

#include <map>
#include <stddef.h>

struct IFileSystem;

// Retail's texture reference, 8 bytes: the ITextureLibrary2 record Slot12 hands out plus the
// alpha-probe cache FUN_00552420 fills. Its default constructor is the static initializer
// LAB_004cc950 (`[+4] = -1`, `[+0] = 0`), its destructor FUN_00426de0 / LAB_004cc970 (release
// through slot +0x38), and its copy constructor FUN_00426f80 (copy the record, take a
// reference with slot +0x34, copy +4). Retail declares no assignment.
struct FTextureRef
{
    void* Texture; // +0x00 ITextureLibrary2 texture record* (Slot12 OutTex)
    int Field04;   // +0x04 FUN_00552420 cache (-1 = unprobed; 0/1 = has-alpha)

    FTextureRef();
    FTextureRef(FTextureRef const& Other);
    ~FTextureRef();
    FTextureRef& operator=(FTextureRef const& Other) = delete;
};

// The shape record, 0x30 bytes: the texture reference at +0, then what FUN_004256c0 stores
// (`ESI + 0x08 ... 0x2c`). Its copy is FTextureRef's copy plus a member-wise copy of the rest
// (the MOV chain at 00426e76).
struct FTexShape : FTextureRef
{
    float U0;      // +0x08
    float V0;      // +0x0c
    float U1;      // +0x10
    float V1;      // +0x14
    int PixelW;    // +0x18 ftol(w)
    int PixelH;    // +0x1c ftol(h)
    float ScaleU;  // +0x20
    float ScaleV;  // +0x24
    float OffsetU; // +0x28
    float OffsetV; // +0x2c
};

static_assert(sizeof(FTextureRef) == 0x08, "retail texture reference is 8 bytes");
static_assert(offsetof(FTexShape, U0) == 0x08, "shape U0 at +0x08");
static_assert(offsetof(FTexShape, PixelW) == 0x18, "shape pixel width at +0x18");
static_assert(offsetof(FTexShape, ScaleU) == 0x20, "shape scale at +0x20");
static_assert(sizeof(FTexShape) == 0x30, "retail shape record is 0x30 bytes");

// FUN_004255c0 -- release the current record, then look Name up through Slot12 with its CRC32;
// FDUMP an error when it is not defined. Returns whether a record is now held.
bool TextureRefLoad(FTextureRef* Ref, char const* Name);

// FUN_00425680 -- swap the record a reference holds: release the old one, store the new one and
// take a reference on it (slot +0x34).
void TexShapeSetTexture(FTextureRef* Ref, void* Texture);

// FUN_00425640 -- TextureLib Slot22(Ref->Texture, Frame, out5); returns out5[0], the GPU
// handle, or null when no record is held.
void* TexShapeGetGpuHandle(FTextureRef const* Ref, int Frame = 0);

// FUN_004256c0 -- recompute the UV rect and the centred scale/offset from a pixel (or
// normalised) rect plus an optional explicit upper-left / lower-right pair. Negative
// upper-left / lower-right components mean "derive from the rect".
void TexShapeSetRect(FTexShape* Shape, float X, float Y, float W, float H, float UpperLeftU,
                     float UpperLeftV, float LowerRightU, float LowerRightV);

// The three spellings of the header path the inlined ShapeDB::Find FDUMPs with.
extern char const kShapeDbHeaderFull[];  // s_E:\FL\...\cmn\ShapeDB.h (005c95c8)
extern char const kShapeDbHeaderLower[]; // s_cmn\shapedb.h (005d3c00)
extern char const kShapeDbHeader[];      // s_cmn\ShapeDB.h (005da4a0)

// The map comparator: retail's lower_bound (FUN_004206c0) and key test (FUN_00420690) both call
// MSVCRT _stricmp on the key text.
struct FShapeKeyLess
{
    bool operator()(TString<24> const& A, TString<24> const& B) const;
};

class FShapeDB
{
public:
    typedef std::map<TString<24>, FTexShape, FShapeKeyLess> FShapeMap;

    FShapeDB();
    FShapeDB(FShapeDB const& Other) = delete;
    FShapeDB& operator=(FShapeDB const& Other) = delete;

    // FUN_00425b30 -- __thiscall, RET 8. Loads one shapes INI (resolved against BaseDir when it
    // is given) and returns the number of shapes the DB now holds.
    int Load(char const* Path, IFileSystem* BaseDir);

    // FUN_00425a20 -- release every shape's record, empty the map, reset the last-found cache.
    void Clear();

    // FUN_00426e20 -- the shape under Key, inserted with a null record when it is missing.
    // Does not touch the last-found cache.
    FTexShape* FindOrCreate(TString<24> const& Key);

    // The ShapeDB.h:0xf2 find every caller inlines: TString<24> key (FUN_004205c0), cache test
    // (FUN_00420690), else lower_bound (FUN_004206c0) and store the cache; a miss FDUMPs a
    // warning with SourceFile and returns null.
    FTexShape* Find(char const* Name, char const* SourceFile = kShapeDbHeader);

    // Retail reads `[this + 0x10]`, the map size.
    unsigned Count() const;

private:
    FShapeMap Shapes;
    // Retail +0x14, an iterator whose end() means "no cache"; null plays end() here.
    FShapeMap::value_type* LastFound;
};

// Owners whose layout is still a raw byte image keep the DB in the bytes retail gives it
// (nebula +0x04, TexturePanels +0x04, asteroid field +0xe0; retail's ShapeDB is 0x18 bytes) and
// construct / destroy it in place, where retail's own constructor and destructor do.
int const kShapeDbStorageBytes = 0x18;
FShapeDB* ShapeDBConstructAt(void* Storage);
void ShapeDBDestructAt(void* Storage);
FShapeDB* ShapeDBAt(void* Storage);

// The global ShapeDB instances (see the table at the top).
extern FShapeDB G_MouseShapeDB;      // DAT_00616728
extern FShapeDB G_HudShapeDB;        // DAT_00673520
extern FShapeDB G_EffectShapeDB;     // DAT_00674f24
extern FShapeDB G_StarSphereShapeDB; // DAT_006750a0
extern FShapeDB G_UiShapeDB;         // DAT_0067d9c8

// FUN_004f7230 -- queue a `[Data] effect_shapes =` path (Common StringAlloc'd) for the drain.
void AddEffectShapesFile(char const* Path);
// FUN_004f73b0 -- empty that queue (client teardown 0x005b2290), strings not freed.
void ClearEffectShapesFiles();
// FUN_004f70d0 -- load every queued file into G_EffectShapeDB, then create the FLFxMeshFactory
// (fl_fx_mesh.h, DAT_00674cdc) and hand it to IFxRuntime::BindMeshFactory. Returns the last
// Load result.
int WarmUpEffectShapes();
