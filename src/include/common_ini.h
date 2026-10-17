// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"

#include <cmath>
#include <string>

// Common.dll INI_Reader (text + BINI) — Common.dll.c:146749–148523, Ini.cpp.
// Decorated exports match retail Common.dll (linked into freelancer.exe).
// Object layout is NOT retail-compatible (no MSVCP60 std::string). See docs/re/common.md (INI).

class Vector
{
public:
    float x;
    float y;
    float z;
};

// Common.dll.c 0x0628b030 -- trivial Vector assign (this->x=a, this->y=b, this->z=c). Not
// exported; one of the highest call-count leaf helpers in Common.dll (273 call sites, mostly
// steering/physics math building a scratch Vector from three floats). Kept here as a real port
// of the primitive for tracker completeness; callers elsewhere in this port should normally
// just assign the three fields directly rather than route through this wrapper.
inline void Vec3Assign(Vector* Dst, float X, float Y, float Z)
{
    Dst->x = X;
    Dst->y = Y;
    Dst->z = Z;
}

// Common.dll.c 0x062c5a70 -- Vector unary negate (Dst = -*Src). Thiscall leaf helper (Src in
// ECX, hidden struct-return pointer passed on the stack, EAX = that pointer on return per the
// usual by-value-return convention). Not exported; 21 call sites across steering/pathfinding
// math in the 0x062cxxxx/0x062exxxx/0x0633xxxx ranges. Ported as a free function like
// Vec3Assign rather than a real operator- to match this file's existing convention.
inline Vector* VecNegate3(Vector const* Src, Vector* Dst)
{
    Dst->x = -Src->x;
    Dst->y = -Src->y;
    Dst->z = -Src->z;
    return Dst;
}

// Common.dll.c 0x062e2590 (FUN_062e2590). Thiscall leaf: `this -= *Src` in place
// (This->x/y/z each FSUB the matching field of *Src, FSTP'd straight back). RET 0x4
// confirms exactly one stack arg past ECX=this. Sole caller (2 sites) is the still-
// unported FUN_062e1f40 orientation/basis builder in the AISTEER/TurnHelper cluster
// (0x062dexxxx-0x062e6xxx); ported standalone since the operation itself is
// unambiguous. Named to match VecNegate3's free-function convention rather than a
// real operator-=.
inline void VecSubInPlace3(Vector* This, Vector const* Src)
{
    This->x -= Src->x;
    This->y -= Src->y;
    This->z -= Src->z;
}

// Common.dll.c 0x062e25c0 (FUN_062e25c0). Thiscall leaf: `this *= Scale` in place
// (each field FMUL'd by the single stack-passed scalar). RET 0x4, one stack arg past
// ECX=this. Called from the same FUN_062e1f40 cluster as VecSubInPlace3 above (4
// sites) and, independently, from ParseGUID (0x0631b980) -- almost certainly a COMDAT
// fold of identical machine code rather than a real semantic link between the two.
inline void VecScaleInPlace3(Vector* This, float Scale)
{
    This->x *= Scale;
    This->y *= Scale;
    This->z *= Scale;
}

// Common.dll.c 0x062e25f0 (FUN_062e25f0). __cdecl(Out, A, B): standard 3D cross product,
// Out = A x B. Verified both from the raw x87 disasm (Common.dll.txt:236296-236316, each
// of the 3 output components built from exactly the expected FLD/FMUL/FSUBP pair) and the
// decompiled body agrees on operand order. 3 call sites, all from the still-unported
// FUN_062e1f40 orientation/basis-builder (same AISTEER/TurnHelper cluster as
// VecSubInPlace3/VecScaleInPlace3 above). Plain cdecl (RET with no stack-adjust immediate),
// returns Out for chaining like the other free helpers in this file.
// Precision (062e25f8-062e2627): each product pair and its FSUBP stay on the x87 stack and
// all three components are computed before the first FSTP float, so each component rounds
// once and Out may alias A or B.
inline Vector* VecCross3(Vector* Out, Vector const* A, Vector const* B)
{
    double const X = (double)A->y * B->z - (double)A->z * B->y;
    double const Y = (double)A->z * B->x - (double)A->x * B->z;
    double const Z = (double)A->x * B->y - (double)A->y * B->x;
    Out->x = (float)X;
    Out->y = (float)Y;
    Out->z = (float)Z;
    return Out;
}

// Common.dll.c 0x062e1680 (FUN_062e1680, decompiled cleanly -- Common.dll.c:113891-113918).
// __cdecl(Out, Src): standard Vector normalize by magnitude. Below the 0.01f "near-zero"
// epsilon on sqrt(x^2+y^2+z^2) (retail's `<=`, matched here), Out is zeroed rather than
// left with a divide-by-near-zero result; otherwise Out = Src * (1/|Src|). The 0.01f
// constant is DAT_0639e6b8, confirmed from its raw IEEE-754 bytes in .rdata (0a d7 23 3c
// little-endian == 0x3c23d70a == 0.01f) -- the same constant is reused at 21 call sites
// across this module's steering/AISTEER family, not unique to this function. 8 call sites,
// all from still-unported functions in the AISTEER/TurnHelper cluster (0x062c2430/blocked,
// 0x062c50e0, 0x062cdfa0, 0x062e1f40, 0x062e6d40, 0x062e8ce0/blocked, 0x062e8e60,
// 0x062ebf50); ported standalone as a foundational primitive. Returns Out for chaining,
// matching retail's EAX=Out-on-exit convention and this file's other free helpers.
// PRECISION CORRECTED 2026-10-08 (claude-code:cmn-ai2) from the listing (.text:062e1680-
// 062e1723): Src is copied to three float locals first, then (z*z + y*y) + x*x, FSQRT and
// the reciprocal (FDIVR 1.0f) all stay on the x87 stack (double here), the gate is
// `FCOM 0.01f; TEST AH,0x41; JNZ` (less, equal or unordered -> zero), and each component
// is `local * inv` FSTP'd to float. The previous body rounded to float at every step.
inline Vector* VecNormalize3(Vector const* Src, Vector* Out)
{
    float X = Src->x;
    float Y = Src->y;
    float Z = Src->z;
    double const Len = std::sqrt(((double)Z * Z + (double)Y * Y) + (double)X * X);
    if (Len > 0.01f)
    {
        double const Inv = 1.0 / Len;
        X = (float)(X * Inv);
        Y = (float)(Y * Inv);
        Z = (float)(Z * Inv);
    }
    else
    {
        X = 0.0f;
        Y = 0.0f;
        Z = 0.0f;
    }
    Out->x = X;
    Out->y = Y;
    Out->z = Z;
    return Out;
}

// Common.dll.c 0x062e4f20 (FUN_062e4f20), raw disasm Common.dll.txt:239939-239965.
// __cdecl(A, B): Euclidean distance between two points. 062e4f28-062e4f4e: the three
// FSUBs stay on the x87 stack, the sum is (dx*dx + dz*dz) + dy*dy, FSQRT, and the
// result is returned in ST0 UNROUNDED -- hence double. Callers: 0x062cf724
// (GuideBehavior, FSTP float at once) and 0x062e83ac (IUStateGraph, which FCOMs the
// unrounded value before storing it).
inline double Distance3(Vector const* A, Vector const* B)
{
    double const DX = (double)A->x - B->x;
    double const DY = (double)A->y - B->y;
    double const DZ = (double)A->z - B->z;
    return std::sqrt((DX * DX + DZ * DZ) + DY * DY);
}

// --- 2026-09-10 re-verification pass, DO-NOT-TOUCH-list correction --------------------------
// The 0x062e0000-0x06301280 sub-range was previously merged into a "forbidden" megazone under
// the assumption it was entangled with qhull/PhyArch::Part. That assumption was independently
// re-checked this pass (git log -- src/qhull/ shows only the one-time bulk vendoring commit;
// decomp-tracker items 1909/1910/1927/1962 in this exact range already carry their own D40/D43
// XREF-level re-analysis dated 2026-09-09 proving it) and found stale for this sub-range: real
// vendored qhull 2002.1 lives entirely under src/qhull/vendor/ (qh_initflags etc., already
// ported per item 3775), and the functions actually sitting at 0x062e0000-0x06301280 are the
// AISTEER/TurnHelper math cluster (see VecSubInPlace3/VecScaleInPlace3/VecCross3/VecNormalize3
// above) plus unrelated Archetype/ArchDB ini-loading code -- nothing here touches qhull. Ported
// below as the first real functions out of this now-reopened sub-range.

// OrientedBox: the 15-float box (Center, the three axes as columns of the owning rotation,
// HalfExtent) RayOrientedBoxIntersect (FUN_062dfd80) takes and FUN_062e04c0 (OBBIntersect,
// file-local in src/Common/common_airaycast.cpp) tests pairwise.
struct OrientedBox
{
    Vector Center;
    Vector AxisX;
    Vector AxisY;
    Vector AxisZ;
    Vector HalfExtent;
};

// Common.dll.c 0x062dc410 (FUN_062dc410), raw disasm Common.dll.txt:062dc410-062dc430 (14
// insns, decomp matches exactly, Common.dll.c:110718-110726). __thiscall(Vector* This, float
// Length): in-place normalize using an already-computed magnitude -- Inv = 1/Length; This->{x,
// y,z} *= Inv. RET 0x4 confirms exactly one stack float past ECX=This. 14 call sites across
// the module (Common.dll.txt:062dc0b2/062dc115/062e1fcb/062e213d/062e235d/062e350c/062e3663/
// 062e3ae9/062e3d43/062e3db3/062e3eea/062e530c/06330efd/06331361), most inside the still-
// unported FUN_062e1f40 orientation/basis-builder (0x062dexxxx-0x062e6xxx AISTEER/TurnHelper
// cluster, same family as VecNormalize3/VecCross3 above) which always precedes this call with
// FUN_06347e60 (a plain sqrt(x^2+y^2+z^2) magnitude, already ported elsewhere as VectorLength
// per decomp-tracker item 2879) guarded by the same 0.01f epsilon as VecNormalize3 -- i.e. this
// is VecNormalize3's in-place counterpart, split into a separate helper in retail because the
// length is computed once and reused by the caller. Ported standalone as a foundational
// primitive; not yet wired into FUN_062e1f40 itself (left for a future pass, see that item's
// notes) since that caller also needs FUN_06347e60's in-place figure of an unported local.
inline void VecNormalizeInPlaceByLength3(Vector* This, float Length)
{
    // 062dc410-062dc42d: `FLD 1.0f; FDIV Length` stays on the x87 stack for all
    // three multiplies; only the FSTP float stores round.
    double const Inv = 1.0 / (double)Length;
    This->x = (float)(This->x * Inv);
    This->y = (float)(This->y * Inv);
    This->z = (float)(This->z * Inv);
}

// Common.dll.c 0x062e1730 (FUN_062e1730), raw disasm Common.dll.txt:234905-235061 (132 insns,
// decomp matches raw disasm exactly, Common.dll.c:113922-113977). __cdecl(Out, A, B). NOTE:
// list_references reports 0 used_by/uses for this item -- confirmed stale per the module's
// standing XREF-under-report warning; the raw .txt XREF header (Common.dll.txt:234905) plus a
// direct grep for "CALL FUN_062e1730" show exactly 2 real callers, both still-unported: 0x062cd287
// (Common.dll.txt:202410) and 0x062d7abd (Common.dll.txt:219854), in the AISTEER/TurnHelper
// family (0x062cxxxx/0x062dxxxx ranges).
//
// Computes NormA = VecNormalize3(A) and NormB = VecNormalize3(B) (identical 0.01f epsilon /
// zero-fallback as VecNormalize3 above -- same FCOM-against-DAT_0639e6b8 pattern, confirmed
// against the raw disasm for both halves), then Scale = clamp(dot(NormA, NormB), -1.0f, 1.0f)
// times |A| -- the ORIGINAL, un-normalized magnitude of A (retail re-reads and re-sums *param_2
// directly for this second SQRT rather than reusing the normalized copy, since the normalized
// values live in separate stack locals). Out = NormB * Scale: a vector along B's direction whose
// length is A's own length scaled by the (clamped) cosine of the angle between A and B -- i.e.
// the projection of A's magnitude onto B's direction, expressed as a vector along B. Ported as a
// free function matching this file's Out/A/B convention (see VecCross3 above); no confirmed
// higher-level/domain name for this operation was established this pass (both real call sites
// are themselves still unported).
// PRECISION RE-TRACED 2026-10-08 (cmn-ai9) from .text:062e1730-062e18ec. A is normalised with a
// double inverse (both lengths are sqrt((y*y + z*z) + x*x), gate `> 0.01f`, else zero); B's
// inverse is FSTP float, B.z * inv FSTP float, while B.x * inv and B.y * inv stay on the FPU;
// Dot = (A.y*b.y + b.z*A.z) + b.x*A.x on the FPU, clamped (above 1.0f -> 1.0f, below -1.0f ->
// -1.0f, NaN kept); Mag = sqrt((x*x + y*y) + z*z) of the original A FSTP float; Scale =
// float(Mag * Dot); Out = (b.x * Scale, b.y * Scale, float(b.z) * Scale), each FSTP float.
inline Vector* VecCosineProject3(Vector* Out, Vector const* A, Vector const* B)
{
    Vector NormA;
    VecNormalize3(A, &NormA);

    float const Bx = B->x;
    float const By = B->y;
    float const Bz = B->z;
    double const LenB = std::sqrt(((double)By * By + (double)Bz * Bz) + (double)Bx * Bx);
    double DirBx = 0.0;
    double DirBy = 0.0;
    float DirBz = 0.0f;
    if (LenB > 0.01f)
    {
        float const InvB = (float)(1.0 / LenB);
        DirBx = (double)InvB * Bx;
        DirBy = (double)By * InvB;
        DirBz = (float)((double)Bz * InvB);
    }

    double Dot = ((double)NormA.y * DirBy + (double)DirBz * NormA.z) + DirBx * NormA.x;
    if (Dot > 1.0)
        Dot = 1.0;
    else if (Dot < -1.0)
        Dot = -1.0;

    float const MagA =
        (float)std::sqrt(((double)A->x * A->x + (double)A->y * A->y) + (double)A->z * A->z);
    float const Scale = (float)((double)MagA * Dot);
    Out->x = (float)(DirBx * Scale);
    Out->y = (float)(DirBy * Scale);
    Out->z = (float)((double)DirBz * Scale);
    return Out;
}

// Common.dll.c 0x062e1e20 (FUN_062e1e20), raw disasm Common.dll.txt:235559-235662 (85 insns).
// Ghidra's own pseudocode (Common.dll.c:114172-114203) already renders this cleanly EXCEPT for
// the acos-domain clamp, which it splits into two duplicated tail-call copies (one per branch)
// with the actual clamp assignments dropped from the C rendering -- verified against the raw x87
// sequence at Common.dll.txt:235613-235662 by hand: MOV [slot],0x3f800000 (clamp to +1.0f) before
// the first _CIacos call (Common.dll.txt:235700 area) when dot > 1.0, and a second FCOMP against
// DAT_06399404 clamping to -1.0f before the fallback _CIacos call when dot < -1.0. Confirmed the
// three referenced .rdata constants from their raw bytes: DAT_06399404 = -1.0f, DAT_06399408 =
// 0.0f (Common.dll.txt:555837-555850), DAT_0639a1d0 = 1.0f (Common.dll.txt:556960-556964).
//
// __cdecl(Basis, A, B): Dir = VecNormalize3(A - B) (0.01f epsilon / zero fallback, same pattern
// as VecNormalize3 above); Dot = clamp(dot(BasisZColumn, Dir), -1.0f, 1.0f); returns
// fabsf(acosf(Dot)) (the fabsf is a no-op for any real acos() result over this domain, kept only
// for exact retail parity). "BasisZColumn" is confirmed purely from raw byte offsets (Common.dll.
// txt:235637/235653/235657, `FLD [EAX+8]` / `[EAX+0x14]` / `[EAX+0x20]`, each exactly 0xC/12
// bytes apart) to be the .z field of 3 consecutive Vectors starting at Basis[0] -- i.e. the "z
// column" of a row-major 3x3 basis (Basis[0].z, Basis[1].z, Basis[2].z). No confirmed struct/
// class owns this exact layout at this address in the port; per the placeholder-field policy the
// parameter is kept as a raw 3-Vector array (offset + primitive type confirmed, semantic owner
// not) rather than guessing a Matrix/Csys type. list_references reports 0 used_by/uses for this
// item too (same stale-index caveat as VecCosineProject3 above); no caller was traced this pass.
//
// RE-TRACED 2026-10-08 (cmn-ai7), 85/85 insns, Common.dll.txt 062e1e20-062e1f3e. The
// normalization is inlined, not VecNormalize3: D = A - B (each FSTP float); Len =
// sqrt((D.z*D.z + D.x*D.x) + D.y*D.y) unrounded; above 0.01f (TEST AH,0x41 + JNZ) Inv =
// (float)(1.0 / Len) and N = Inv * D stays on the x87 stack, else N = 0. Dot =
// (float)((N.z*Basis[2].z + N.y*Basis[1].z) + N.x*Basis[0].z) (the FXCH chain at
// 062e1ec6-062e1ee5); > 1.0f -> 1.0f, else < -1.0f (TEST AH,0x5 + JP keeps >=) -> -1.0f;
// returns FABS(_CIacos(Dot)) in ST0, i.e. a double the callers FCOMP unrounded.
inline double AngleToBasisZColumn3(Vector const* Basis, Vector const* A, Vector const* B)
{
    float const Dx = A->x - B->x;
    float const Dy = A->y - B->y;
    float const Dz = A->z - B->z;
    double const Len = std::sqrt(((double)Dz * Dz + (double)Dx * Dx) + (double)Dy * Dy);
    double Nx = 0.0;
    double Ny = 0.0;
    double Nz = 0.0;
    if (Len > (double)0.01f)
    {
        float const Inv = (float)(1.0 / Len);
        Nx = (double)Inv * Dx;
        Ny = (double)Dy * Inv;
        Nz = (double)Dz * Inv;
    }

    float Dot = (float)((Nz * Basis[2].z + Ny * Basis[1].z) + Nx * Basis[0].z);
    if (Dot > 1.0f)
        Dot = 1.0f;
    else if (Dot < -1.0f)
        Dot = -1.0f;

    return std::fabs(std::acos((double)Dot));
}

COMMON_API char const* __cdecl StringIndex(char const* Text, unsigned int Index);
COMMON_API double __cdecl ParseNumber(char const* Text);
// Common.dll.txt:06333ca0. Returns a pointer into Common's static StringCache pool: never null,
// never freed or moved (null / "" -> the shared empty string; bTemporary == false interns
// case-insensitively, so equal names share one pointer).
COMMON_API char* __cdecl StringAlloc(char const* Text, bool bTemporary);

// Common.dll.txt:0631bee0, ?ValueNum@@YANPBDI@Z. Field Index of a comma-separated value,
// run through the CRT atof -- NOT through ParseNumber, so no 0x / base-N / char-literal
// handling here.
COMMON_API double __cdecl ValueNum(char const* Text, unsigned int Index);

// Common.dll.txt:06333c70, ?StringShutdown@@YAXXZ. Retail tears down the StringCache pool.
COMMON_API void __cdecl StringShutdown();

// Retail ILoadingScreen (PAVILoadingScreen). Slot 0 = thiscall, no extra args
// (Common.dll.txt:06311430 JMP [vtable+0]). Both retail implementers have exactly two slots
// (FLServer.exe vtable .rdata:0041d030, Freelancer.exe .rdata:005e448c), so slot 1 belongs to
// the interface. Its name is unrecoverable: no symbol names it and no retail module calls it
// through the vtable (Common's only reader of the registered pointer, DAT_063fd2a4, is the
// slot-0 call; Freelancer.exe runs the same body non-virtually as FUN_00579290).
class ILoadingScreen
{
public:
    virtual void Refresh() = 0;
    virtual void Slot01() = 0;   // +0x04
};

COMMON_API void __cdecl RegisterLoadingScreen(ILoadingScreen* Screen);
COMMON_API void __cdecl RefreshLoadingProgress();

#pragma pack(push, 1)
// Retail names from FileMap::read_ptr's mangling (BINI_Header/BINI_Block/BINI_Value); the
// sizes 0x0c/4/3 are the byte counts each read_ptr requests (.text:0626c017/0626c067/0626c0b7).
struct BINI_Header
{
    unsigned int Magic;
    unsigned int Version;
    unsigned int StringTableOffset;
};

struct BINI_Block
{
    unsigned short NameOffset;
    unsigned short ValueCount;
};

struct BINI_Value
{
    unsigned short NameOffset;
    unsigned char ParamCount;
};

// One BINI parameter: a type byte and a 4-byte payload read according to it (the getters
// .text:0630ff40 / 063105b0 / 063107c0 / 063108f0 switch on Type 0..3).
enum EBiniParamType : unsigned char
{
    BiniParamByte = 0,   // MOVZX of the payload's first byte
    BiniParamInt = 1,    // the payload dword as int
    BiniParamFloat = 2,  // the payload dword as float
    BiniParamString = 3, // the payload dword: an offset into the string table
};

struct FBiniParam
{
    EBiniParamType Type;
    union
    {
        unsigned char Byte;
        int Int;
        float Float;
        unsigned int StringOffset;
    };
};
#pragma pack(pop)

// winnt.h's own definition, repeated so FileMap can hold its OS handles by type without
// pulling <windows.h> into every module that includes this header.
typedef void* HANDLE;

// FileMap -- a read-only view of a file (CreateFileMapping/MapViewOfFile) or of a malloc'd
// copy of a memory buffer. Retail layout (ctor .text:06263040): +0x00 std::string FileName,
// +0x10 FileHandle (INVALID_HANDLE_VALUE when none), +0x14 MappingHandle, +0x18 View,
// +0x1c Size, +0x20 Cursor. There is no "heap view" flag: close() frees View whenever it is
// still set after the file branch, and the file branch only runs while FileHandle is valid.
struct COMMON_API FileMap
{
    FileMap();
    // .text:06263310: close() inlined, then the string dtor.
    ~FileMap();

    // .text:06262ac0 / .text:06262c00: copy (or assign) FileName, then copy the five
    // dwords +0x10..+0x20 verbatim. The OS handles are aliased, not duplicated.
    FileMap(FileMap const& Other);
    FileMap& operator=(FileMap const& Other);

    bool open(char const* Path);
    bool open_memory(void const* Buffer, unsigned int Bytes);
    void close();
    void seek(unsigned int Pos);
    unsigned int tell() const;
    bool eof() const;
    unsigned int get_file_length() const;
    char const* get_file_name() const;
    void* request_pointer(unsigned int& Bytes);
    unsigned int read(void* Dest, unsigned int Bytes);
    bool read_ptr(BINI_Header*& Out);
    bool read_ptr(BINI_Block*& Out);
    bool read_ptr(BINI_Value*& Out);
    void handoff_to(FileMap& Other);

    std::string FileName;
    HANDLE FileHandle;
    HANDLE MappingHandle;
    char* View;
    unsigned int Size;
    unsigned int Cursor;
};

// LineParser32 -- one text line plus a read position (retail size 0x408: Buffer[0x400],
// Length +0x400, Position +0x404). Every method is exported by retail (whole struct).
// Methods .text:06262e30..06263010 and 0630f170..0630f250.
struct COMMON_API LineParser32
{
    LineParser32();

    void clear();
    void set(char const* Text, unsigned int Count);
    bool whitespace(char Ch) const;
    void skip_white();
    char get_char() const;
    bool skip_char(char Ch);
    int find_char(char Ch) const;
    unsigned int read_string(char* Dest, char const* Terminators);
    bool terminate_line(char Ch);
    unsigned int fetch_string(char* Dest, unsigned int EndPos);
    unsigned int read_terminator(char* Dest, char Term);
    unsigned int read_terminator2(char* Dest, char TermA, char TermB);

    LineParser32& operator=(LineParser32 const& Other);

    char Buffer[0x400];
    unsigned int Length;
    unsigned int Position;
};

// TextFile32 -- a FileMap read line by line. Retail size 0x28: the FileMap (0x24) and the
// line counter at +0x24. Its default ctor and dtor are FileMap's own bodies (ICF-folded
// .text:06263040 / 06263310), so the counter is NOT initialised by the ctor.
struct COMMON_API TextFile32 : public FileMap
{
    TextFile32();
    TextFile32(TextFile32 const& Other);
    ~TextFile32();
    TextFile32& operator=(TextFile32 const& Other);

    unsigned int get_line_num() const;
    bool read_line(LineParser32& Line);

    unsigned int LineNum;
};

// Retail exports only the public API below, member by member (no private helper and
// no static constant of this port leaves the DLL), so the class itself is not dllexport.
//
// Retail layout (ctor .text:0630f660), kept in the same member order here:
//   +0x0000 TextFile32 Text           the text-mode file and its line counter
//   +0x0028 char Header[0x400]        text [section] name
//   +0x0428 char ValueName[0x400]     text value name
//   +0x0828 char ValueParams[0x400]   text value parameters
//   +0x0c28 wchar ValueWide[0x400]    text get_value_wstring buffer
//   +0x1428 BINI half (FBiniFile)     its own FileMap + header/string-table/block/
//                                     value-index/value/params + 0x100 join buffer
//   +0x1564 bool bBini
class INI_Reader
{
public:
    // Text snapshot is cursor+line+3x0x400 = 0xC08 (get_state .text:06310f6b). BINI writes
    // only the first 0x14 bytes: cursor, block-rel, word value index, value-rel, params-rel
    // (.text:06310f0e..06310f64); the rest of the caller's State is left as it was, the two
    // bytes after the word index included.
    struct State
    {
        struct FText
        {
            int LineNum;                // +0x004
            char Header[0x400];         // +0x008
            char ValueName[0x400];      // +0x408
            char ValueParams[0x400];    // +0x808
        };
        // Offsets from the BINI view; a null pointer is saved as a negative offset.
        struct FBini
        {
            int BlockOffset;            // +0x004
            short ValueIndex;           // +0x008
            int ValueOffset;            // +0x00c
            int ParamsOffset;           // +0x010
        };

        unsigned int Cursor;            // +0x000, both modes
        union
        {
            FText Text;
            FBini Bini;
        };
    };

    COMMON_API INI_Reader();
    COMMON_API ~INI_Reader();

    // Common.dll.txt 0x062634f0/0x06263650: memberwise copy / assignment of every member in
    // declaration order -- both FileMaps included, so the copy aliases the source's open
    // OS mappings (see FileMap's own copy note).
    COMMON_API INI_Reader(INI_Reader const& Other);
    COMMON_API INI_Reader& operator=(INI_Reader const& Other);

    COMMON_API bool open(char const* Path, bool bFatal);
    COMMON_API bool open_memory(char const* Buffer, unsigned int Bytes);
    COMMON_API void close();
    COMMON_API void reset();
    COMMON_API void seek(unsigned int Pos);
    COMMON_API unsigned int tell() const;
    COMMON_API int get_line_num() const;

    COMMON_API bool read_header();
    COMMON_API bool read_value();
    COMMON_API bool find_header(char const* Name);
    COMMON_API bool is_header(char const* Name);
    COMMON_API bool is_value(char const* Name);
    COMMON_API bool is_end() const;

    COMMON_API char const* get_header_ptr();
    COMMON_API char const* get_name_ptr();
    COMMON_API char const* get_file_name() const;
    COMMON_API char const* get_name() const;
    COMMON_API char const* get_line_ptr();

    COMMON_API unsigned int get_num_parameters() const;
    COMMON_API bool is_value_empty(unsigned int Index);
    COMMON_API bool get_value_bool(unsigned int Index);
    COMMON_API int get_value_int(unsigned int Index);
    // Retail ?get_value_float@INI_Reader@@QAEMI@Z leaves the result unrounded in ST0 and
    // importers consume it as such (FSTP double, _ftol, FCOMP double), so the port returns double.
    COMMON_API double get_value_float(unsigned int Index);
    COMMON_API char const* get_value_string(unsigned int Index);
    COMMON_API char const* get_value_string();
    // Retail ?get_value_wstring@INI_Reader@@QAEPBGXZ (G = VC6 wchar_t).
    COMMON_API wchar_t const* get_value_wstring();
    COMMON_API char const* get_value_ptr();
    COMMON_API char const* get_indexed_value(unsigned int Index);
    COMMON_API bool get_bool(unsigned int Index);
    COMMON_API double value_num(unsigned int Index);
    COMMON_API Vector get_vector();

    COMMON_API void get_state(State& Out) const;
    COMMON_API void set_state(State const& In);
    COMMON_API bool is_number(char const* Candidate) const;
    // .text:062ed730: `RET 4` -- the retail build compiled the link logging out.
    COMMON_API void log_link(char const* Link) const;

private:
    static unsigned int const NameChars = 0x400;
    // Retail's join buffer is char[0x100] (+0x1464) and is filled with unbounded strcat:
    // a value whose joined parameters reach 0x100 chars overruns into bBini and past the
    // object (undefined behaviour). The port keeps the bytes in a larger buffer and
    // truncates only beyond it.
    static unsigned int const JoinChars = 0x1000;

    // The BINI half at +0x1428. Retail calls FUN_0630fbd0 and FUN_0630ff40 with this
    // sub-object as `this`; nothing about it is exported.
    struct FBiniFile : public FileMap
    {
        FBiniFile();

        bool Init();                                        // FUN_0630fbd0
        char const* FormatParam(unsigned int Index);        // FUN_0630ff40
        void ReportMissingParam(unsigned int Index) const;  // inlined in every getter
        FBiniParam const* ParamAt(unsigned int Index) const;
        char const* String(unsigned int Offset) const;
        char const* JoinParams();                           // inlined join loop

        BINI_Header const* FileHeader;  // +0x24
        char const* StringBase;         // +0x28
        BINI_Block const* Block;        // +0x2c
        short ValueIndex;               // +0x30 (MOVSX / INC word)
        BINI_Value const* Value;        // +0x34
        FBiniParam const* Params;       // +0x38, Value->ParamCount entries
        char Joined[JoinChars];         // +0x3c (retail 0x100)
    };

    bool ReadTextHeader();   // FUN_0630f3d0
    bool ReadTextValue();    // LAB_0630f4e0
    bool ReadBiniHeader();   // read_header's BINI branch (.text:06310082-06310166)
    double ValueAsNumber(unsigned int Index); // get_value_float's x87 result

    TextFile32 Text;
    char Header[NameChars];
    char ValueName[NameChars];
    char ValueParams[NameChars];
    wchar_t ValueWide[NameChars];
    FBiniFile Bini;
    bool bBini;
};
