// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"

// Common.dll Csys — listing RVA 0x1170 / 0xa7490 (Common.dll.txt).
// Layout: Vector @ +0, Matrix @ +0xC, sizeof 0x30. Transform is Matrix then Vector.
// trans_from_frame / trans_to_frame / get_inverse / operator* go through I3DMathEngine
// (Acquire3DMathEngine), as retail does.

#include "common_ini.h"

class Matrix
{
public:
    Matrix() {}

    // Not exported by Common.dll, Server.dll or freelancer.exe: every module compiles its own
    // copy of this header body (Common .text:06284f70, freelancer.exe .text:00447b60 -- the
    // same 20 instructions, arguments stored row-major to +0x00..+0x20, RET 0x24).
    Matrix(float m00, float m01, float m02,
           float m10, float m11, float m12,
           float m20, float m21, float m22)
    {
        E[0][0] = m00;
        E[0][1] = m01;
        E[0][2] = m02;
        E[1][0] = m10;
        E[1][1] = m11;
        E[1][2] = m12;
        E[2][0] = m20;
        E[2][1] = m21;
        E[2][2] = m22;
    }

    float E[3][3];
};

class Transform
{
public:
    Matrix Rotation;    // +0x00
    Vector Translation; // +0x24
};

class COMMON_API Csys
{
public:
    // User-declared so that no implicit move members are generated and exported:
    // VC6 had none, and retail exports exactly the copy assignment.
    Csys& operator=(Csys const&) = default;

    // Retail ??0Csys@@QAE@XZ is empty (no identity). Call set_identity / Csys_identity.
    Csys();

    void set_identity();
    void init(Vector const& NewPos, Matrix const& NewOrient);
    Transform get_transform() const;
    void set_transform(Transform const& Xform);

    Vector trans_from_frame(Vector const& Local) const;
    Matrix trans_from_frame(Matrix const& Local) const;
    Csys trans_from_frame(Csys const& Local) const;

    Vector trans_to_frame(Vector const& World) const;
    Matrix trans_to_frame(Matrix const& World) const;
    Csys trans_to_frame(Csys const& World) const;

    Csys get_inverse() const;

    Vector Pos;     // +0x00
    Matrix Orient;  // +0x0C
};

static_assert(sizeof(Vector) == 0x0C, "Vector 3 float");
static_assert(sizeof(Matrix) == 0x24, "Matrix 3x3");
static_assert(sizeof(Transform) == 0x30, "Transform matrix+pos");
static_assert(sizeof(Csys) == 0x30, "Csys pos+matrix");

COMMON_API Csys __cdecl Csys_identity();
COMMON_API Csys __cdecl Csys_init(Vector const& Pos, Matrix const& Orient);
COMMON_API Csys __cdecl operator*(Csys const& Left, Csys const& Right);

// Port helpers, not retail exports. MatrixSetIdentity is header-inline (every module that
// needs an identity 3x3 compiles its own, as retail's inline Matrix code did);
// CsysFromPosOrient is Common.dll-internal (common_roomdata.cpp only).
inline void MatrixSetIdentity(Matrix& Out)
{
    unsigned Row = 0;
    for (; Row < 3; ++Row)
    {
        unsigned Col = 0;
        for (; Col < 3; ++Col)
            Out.E[Row][Col] = (Row == Col) ? 1.0f : 0.0f;
    }
}
void CsysFromPosOrient(Csys& Out, float const* Pos3, float const* Orient9);

// Common.dll.c 0xa38b0. Orthonormal 3x3: column 2 = -normalize(Dir).
COMMON_API Matrix __cdecl LookMatrix(Vector const& Direction);

// Common.dll.c 0xa3a60, ?LookMatrixYup@@YA?AVMatrix@@ABVVector@@@Z. The Y-up sibling of
// LookMatrix: column 2 is -normalize(Direction), column 1 is the perpendicular that keeps Y
// up, column 0 closes the right-handed basis. Retail picks its reference axis from X and Z
// (LookMatrix uses X and Y) and compares the SQUARED length against 1e-4 before the sqrt.
COMMON_API Matrix __cdecl LookMatrixYup(Vector const& Direction);

// Common.dll.txt:06307900, ?DALib_Engine_get_csys@@YA?AVCsys@@J@Z. The engine instance's
// transform (slot 40, +0xa0) repackaged as a Csys. Retail dereferences the returned
// transform without a null check.
COMMON_API Csys __cdecl DALib_Engine_get_csys(long Handle);

// Common.dll.txt:06303c10, ?GetDirectionMatrix@@YA?AVMatrix@@ABVTransform@@ABVVector@@@Z.
// Orthonormal basis whose column 2 points from Target back to the transform. Column 1 is
// normally the transform's own column 1 orthogonalised against that direction; when the
// two are (exactly) parallel retail falls back to column 0 -- see docs/re/deviations.md
// for the abs()-on-a-float that decides which. Returns the transform's rotation unchanged
// when the two positions coincide.
COMMON_API Matrix __cdecl GetDirectionMatrix(Transform const& Xform, Vector const& Target);

// Common.dll.c 0x62d3af0 (unnamed static helper, no decorated export -- Ghidra
// recovered no symbol; called from 12 sites across IBehaviorManager and other
// steering/physics code, e.g. IBehaviorManager::update_current_behavior_direction).
// Retail signature is thiscall `void(Matrix const* this, Vector* Out)`, writing
// Out->x/y/z = this->E[0][2]/E[1][2]/E[2][2] -- the RAW column 2 of an orthonormal
// 3x3, with NO sign flip. Contrast with the many already-inlined "Forward =
// -E[*][2]" sites elsewhere in this port (common_cobject.cpp, common_cequip.cpp,
// common_aisteer.cpp), which negate column 2 for a forward-facing convention;
// this helper does not negate. Ported as a plain by-value accessor.
// Unexported in retail (FUN_*): Common.dll-internal, no caller outside Common.dll.
Vector __cdecl MatrixColumn2(Matrix const& M);

// Common.dll.c 0x62e0480 (FUN_062e0480, another unnamed static helper of the same
// family as MatrixColumn2, distinct compiled instance -- 6 call sites near
// FUN_062dffd0/IBehaviorManager steering code). Retail: thiscall
// `void(Matrix const* this, Vector* Out)`, MSVC hidden-return-pointer convention
// (EAX = Out on exit). Writes Out->x/y/z = this->E[0][0]/E[1][0]/E[2][0] -- raw
// column 0, no sign flip. Ported as a plain by-value accessor, same style as
// MatrixColumn2.
// Unexported in retail (FUN_*): Common.dll-internal, no caller outside Common.dll.
Vector __cdecl MatrixColumn0(Matrix const& M);

// Common.dll.c 0x62e04a0 (FUN_062e04a0), raw disasm Common.dll.txt:232875-232888.
// Third member of the MatrixColumn0/1/2 accessor family -- same thiscall/hidden-
// return-pointer convention, this one reading raw column 1 (E[0][1]/E[1][1]/
// E[2][1], no sign flip). Previously left `simplified` (decomp-tracker item
// 1717) for lack of a concrete caller to attach it to; resolved while porting
// FUN_062e1f40/FUN_062e0a60 (src/Common/common_aisteer.cpp), which both call it
// via the already-ported MatrixColumn0/MatrixColumn2 pattern.
// Unexported in retail (FUN_*): Common.dll-internal, no caller outside Common.dll.
Vector __cdecl MatrixColumn1(Matrix const& M);

// Common.dll.c 0x62e3710 (FUN_062e3710). Tiny __thiscall thunk (8 instructions,
// verified directly against the raw disassembly -- no decompiler corruption):
//   MOV EAX,[ESP+4]; MOV EDX,[EAX]; MOV [ECX+8],EDX;
//   MOV EDX,[EAX+4]; MOV [ECX+0x14],EDX; MOV EAX,[EAX+8]; MOV [ECX+0x20],EAX; RET 4
// i.e. `this` (ECX) = Matrix*, stack arg = a 3-float source vector; writes
// Src.x/y/z into E[0][2]/E[1][2]/E[2][2] -- the setter counterpart of
// MatrixColumn2 above (offsets +8/+0x14/+0x20 are exactly that column, row
// stride 0xC). Sole caller is FUN_062e3460, an unported "build a heading-
// constrained orientation matrix" helper in the AISTEER cluster
// (src/include/common_aisteer.h, 0x062d6000-0x062e3000 header note) that
// fills columns 0/1 of the same Matrix from two other basis vectors and then
// calls this to fill in column 2; also reachable transitively from
// FUN_062e3730 and the large, currently-blocked FUN_062e37e0 steering
// function. Ported standalone since its own behavior is unambiguous
// regardless of the caller's still-unresolved surrounding math.
// Unexported in retail (FUN_*): Common.dll-internal, no caller outside Common.dll.
void __cdecl MatrixSetColumn2(Matrix& M, Vector const& Src);

// Common.dll.txt:06303fe0, ?RotateMatrix@@YA?AVMatrix@@ABVVector@@@Z. Euler angles in
// DEGREES, but composed in a different order from EulerMatrix: Ry * Rx * Rz, and each
// axis whose angle is within 0.01 degrees of zero is skipped entirely (its multiply does
// not run at all, so no rounding creeps in).
COMMON_API Matrix __cdecl RotateMatrix(Vector const& AnglesDegrees);

// Common.dll.c 0xa4230, ?EulerMatrix@@YA?AVMatrix@@ABVVector@@@Z. Euler angles in DEGREES
// (pitch = x, yaw = y, roll = z), converted with 0.017453292f.
COMMON_API Matrix __cdecl EulerMatrix(Vector const& AnglesDegrees);


COMMON_API float __cdecl Csys_error(Csys const& A, Csys const& B);
// Retail takes both frames by value: ?Csys_interpolate@@YA?AVCsys@@V1@0M@Z.
COMMON_API Csys __cdecl Csys_interpolate(Csys A, Csys B, float T);
COMMON_API void __cdecl DALib_Engine_set_csys(long Handle, Csys const& InCsys);

// Returns x86math.dll's 3DMathEngine singleton (DACOM CreateInstance by component name).
// The return type used to be void*, so all 113 call sites in the tree had to cast it
// back; the interface itself has been public in src/include/x86math.h since the x86math
// port closed. Forward-declared rather than included so this header keeps its own
// include set.
//
// Not exported: retail Common.dll's copy is the unexported FUN_06391cf0, and freelancer.exe
// carries its own (FUN_005b73e0, defined in src/freelancer/Client/engine_init.cpp). This
// declaration is shared; each module that calls it defines it.
struct I3DMathEngine;
I3DMathEngine* __cdecl Acquire3DMathEngine();
