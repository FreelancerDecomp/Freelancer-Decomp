// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include <cmath>
#include <cstring>

struct IVP_U_Matrix3;
struct IVP_U_Matrix;

// Precision convention for this file (retail is VC6 x87 code): a value retail keeps on the
// x87 stack is `double` here, a value retail spills with FSTP dword is `float`. Sums are
// written in retail's evaluation order (it changes the rounding). Every function below
// names the retail address it ports.
// Trigonometry uses standard C++ math. Instruction comments describe retail;
// extended-precision trigonometric rounding is no longer reproduced.

// 16-byte 3D point/vector (4th float used for hesse distance or padding)
struct IVP_U_Point
{
    union
    {
        struct
        {
            float x;
            float y;
            float z;
            float hesse;
        };
        float k[4];
    };

    void set(float _x, float _y, float _z)
    {
        k[0] = _x;
        k[1] = _y;
        k[2] = _z;
    }

    // FUN_0628bdf0: copies k[0..2].
    void set(const IVP_U_Point *p)
    {
        k[0] = p->k[0];
        k[1] = p->k[1];
        k[2] = p->k[2];
    }

    void mult(const IVP_U_Point *p, float f)
    {
        k[0] = p->k[0] * f;
        k[1] = p->k[1] * f;
        k[2] = p->k[2] * f;
    }

    float dot_product(const IVP_U_Point *p) const
    {
        return k[0] * p->k[0] + k[1] * p->k[1] + k[2] * p->k[2];
    }

    void add_mult(const IVP_U_Point *v1, const IVP_U_Point *v2, float f)
    {
        k[0] = v1->k[0] + v2->k[0] * f;
        k[1] = v1->k[1] + v2->k[1] * f;
        k[2] = v1->k[2] + v2->k[2] * f;
    }

    void subtract(const IVP_U_Point *v1, const IVP_U_Point *v2)
    {
        k[0] = v1->k[0] - v2->k[0];
        k[1] = v1->k[1] - v2->k[1];
        k[2] = v1->k[2] - v2->k[2];
    }

    // No float cross product / quad_length members here (cmn-ivp3g
    // 2026-10-09): the float-per-step versions that lived here were port-only
    // lookalikes. Retail's cross product is FUN_0628bed0 =
    // IVP_CalcCrossProduct below (x87 products, one rounding per component).

    // FUN_06347e60 (.text:06347e60-06347e80), thiscall, no arguments.
    // sqrt((x*x + y*y) + z*z); the result is returned on the x87 stack unrounded.
    double real_length() const
    {
        const double X = k[0];
        const double Y = k[1];
        const double Z = k[2];
        return sqrt((X * X + Y * Y) + Z * Z);
    }

    // FUN_06347e90 (.text:06347e90-06347ee6), thiscall, no arguments.
    // q = (x*x + y*y) + z*z stays on the stack; FCOM 1e-10f (DAT_063a53d8); TEST AH,0x1;
    // JE -> q >= eps normalises. Otherwise (less, or unordered) the vector is left alone and
    // 0.0 (DAT_063a53d4) is returned. inv = 1.0 / sqrt(q) is extended, every component is
    // float(inv * c), and the return value is q * inv (FMULP), unrounded.
    double real_length_plus_normize()
    {
        const double X = k[0];
        const double Y = k[1];
        const double Z = k[2];
        const double QuadLength = (X * X + Y * Y) + Z * Z;
        if (!(QuadLength >= (double)(1e-10f)))
            return 0.0;
        const double InvLength = 1.0 / sqrt(QuadLength);
        k[0] = (float)(InvLength * k[0]);
        k[1] = (float)(InvLength * k[1]);
        k[2] = (float)(InvLength * k[2]);
        return QuadLength * InvLength;
    }

    // FUN_06347ef0 (.text:06347ef0-06347f41). |v|^2 = (x*x + y*y) + z*z stays on the x87
    // stack (double here); FCOM DAT_063a53d8 (1e-10f); TEST AH,0x1; JZ -> normalise when
    // |v|^2 >= 1e-10, otherwise (smaller, or unordered) leave v untouched and return 0.
    // inv = 1.0 / sqrt(|v|^2) is kept extended, each component is float(inv * c). Returns 1.
    int normize()
    {
        const double QuadLength = ((double)(k[0]) * k[0] + (double)(k[1]) * k[1]) +
                                  (double)(k[2]) * k[2];
        if (!(QuadLength >= (double)(1e-10f)))
            return 0;
        const double InvLength = 1.0 / sqrt(QuadLength);
        k[0] = (float)(InvLength * k[0]);
        k[1] = (float)(InvLength * k[1]);
        k[2] = (float)(InvLength * k[2]);
        return 1;
    }

    // FUN_06347e00 (.text:06347e00-06347e52), thiscall, RET 0xc: this = p1 * (1 - t) + p2 * t.
    // s = 1.0 - t is extended. x and y are summed on the stack (float only at the FSTP), z is
    // float(float(s * p1.z) + float(t * p2.z)) -- both z products are spilled as floats first.
    void set_interpolate(const IVP_U_Point *p1, const IVP_U_Point *p2, float t)
    {
        const double S = 1.0 - (double)(t);
        const float Z1 = (float)(S * p1->k[2]);
        const float Z2 = (float)((double)(t) * p2->k[2]);
        const double Y = S * p1->k[1] + (double)(t) * p2->k[1];
        const double X = S * p1->k[0] + (double)(t) * p2->k[0];
        k[0] = (float)(X);
        k[1] = (float)(Y);
        k[2] = (float)((double)(Z2) + Z1);
    }

    // FUN_06347f50 (.text:06347f50-06347f94), thiscall (v, dir), RET 8: this = v - dot(v, dir) * dir.
    // n = -((v.x*d.x + d.z*v.z) + v.y*d.y) stays on the stack (FCHS);
    // y = n*d.y + v.y (kept), z = n*d.z + v.z (FSTP float to a temp), x = n*d.x + v.x.
    // All reads precede the first store; the stores go z, x, y.
    void set_perpendicular_component(const IVP_U_Point *v, const IVP_U_Point *dir)
    {
        const double NegDot = -(((double)(v->k[0]) * dir->k[0] + (double)(dir->k[2]) * v->k[2]) +
                                (double)(v->k[1]) * dir->k[1]);
        const double Y = NegDot * dir->k[1] + v->k[1];
        const float Z = (float)(NegDot * dir->k[2] + v->k[2]);
        const double X = NegDot * dir->k[0] + v->k[0];
        k[2] = Z;
        k[0] = (float)(X);
        k[1] = (float)(Y);
    }

    // FUN_06347fa0 (.text:06347fa0-06348038), thiscall (coeffs = {a, b, c}), RET 4.
    // Output {disc, root0, root1} of a*x^2 + b*x + c = 0.
    //   FLD a; FABS; FCOMP 1e-10f; TEST AH,0x1; JZ quadratic -> |a| >= eps goes quadratic,
    //   smaller or unordered goes linear. Linear: k[0] = 0; |b| < eps (or unordered) ->
    //   k[0] = -1.0f; else r = -(c / b) extended (FDIV; FCHS), FST k[2], FSTP k[1].
    //   Quadratic: disc = b*b - (c*a)*4 extended; k[0] = float(disc); the sign test
    //   (FCOMP 0.0; TEST AH,0x1; JNZ return) uses the EXTENDED disc. sqrt is taken of the
    //   float-rounded disc, inv = 0.5 / a stays extended, s = sqrt(...) extended and also
    //   spilled as float: k[2] = float((s - b) * inv), k[1] = float((-b - float(s)) * inv).
    void set_quadratic_roots(const IVP_U_Point *coeffs)
    {
        const double Epsilon = (double)(1e-10f); // DAT_063a53d8
        const float a = coeffs->k[0];
        const float b = coeffs->k[1];
        const float c = coeffs->k[2];

        if (fabs((double)(a)) >= Epsilon)
        {
            const double Disc = (double)(b) * b - ((double)(c) * a) * 4.0;
            const float DiscF = (float)(Disc);
            k[0] = DiscF;
            if (!(Disc >= 0.0))
                return;
            const double HalfInvA = 0.5 / (double)(a);
            const double Root = sqrt((double)(DiscF));
            const float RootF = (float)(Root);
            const double Upper = (Root - b) * HalfInvA;
            const double Lower = (-(double)(b) - RootF) * HalfInvA;
            k[1] = (float)(Lower);
            k[2] = (float)(Upper);
            return;
        }

        k[0] = 0.0f;
        if (!(fabs((double)(b)) >= Epsilon))
        {
            k[0] = -1.0f;
            return;
        }
        const double Root = -((double)(c) / b);
        k[2] = (float)(Root);
        k[1] = (float)(Root);
    }

    // FUN_063480a0 (.text:063480a0-06348143), thiscall (p0, p1, p2), RET 0xc: the
    // (unnormalised) Hesse plane through three points.
    //   a = p1 - p0 stays on the stack, b = p2 - p0 is spilled as floats.
    //   n = b x a (NOT a x b): n.x = float(b.y*a.z - b.z*a.y), n.y = float(b.z*a.x - b.x*a.z)
    //   are stored and re-read as floats, n.z = b.x*a.y - b.y*a.x is stored (FST) but kept
    //   extended; hesse = -((n.z*p0.z + n.y*p0.y) + n.x*p0.x). p0 stays on the stack.
    //   Every input is read before the first store; the stores go x, y, z, hesse.
    void set_plane_from_3_points(const IVP_U_Point *p0, const IVP_U_Point *p1, const IVP_U_Point *p2)
    {
        const double X0 = p0->k[0];
        const double Y0 = p0->k[1];
        const double Z0 = p0->k[2];
        const double Ax = p1->k[0] - X0;
        const double Ay = p1->k[1] - Y0;
        const double Az = p1->k[2] - Z0;
        const float Bx = (float)(p2->k[0] - X0);
        const float By = (float)(p2->k[1] - Y0);
        const float Bz = (float)(p2->k[2] - Z0);

        const float Nx = (float)(By * Az - Bz * Ay);
        k[0] = Nx;
        const float Ny = (float)(Bz * Ax - Bx * Az);
        k[1] = Ny;
        const double Nz = Bx * Ay - By * Ax;
        k[2] = (float)(Nz);
        k[3] = (float)(-((Nz * Z0 + (double)(Ny) * Y0) + (double)(Nx) * X0));
    }

    // FUN_06348150 (.text:06348150-0634817b), thiscall: inv = 1.0 / real_length() (FUN_06347e60,
    // FDIVR on the unrounded return), kept extended; all four components become float(inv * c).
    // No zero-length guard in retail.
    void normize_plane()
    {
        const double InvLength = 1.0 / real_length();
        k[0] = (float)(InvLength * k[0]);
        k[1] = (float)(InvLength * k[1]);
        k[2] = (float)(InvLength * k[2]);
        k[3] = (float)(InvLength * k[3]);
    }

    // FUN_06348180 (.text:06348180-063481a2), thiscall (v), RET 4: this = -v. All three
    // components of v are read before the first store; the stores go x, z, y.
    void set_negated(const IVP_U_Point *v)
    {
        const float y = v->k[1];
        const float z = v->k[2];
        k[0] = -v->k[0];
        k[2] = -z;
        k[1] = -y;
    }

    // FUN_063485e0, defined after IVP_CalcCrossProduct below.
    void set_arbitrary_perpendicular_to(const IVP_U_Point *v);
};

// FUN_06347df0 (.text:06347df0-06347dfc), cdecl (float x): FLD x; FSQRT; FDIVR 1.0f
// (DAT_063a53d0); RET -- 1 / sqrt(x) returned unrounded on the x87 stack. 18 retail
// callers (minimize solver, ledge sweep, limiter contact frames).
inline double IVP_InvSqrtFloat(float Value)
{
    return 1.0 / sqrt((double)(Value));
}

// FUN_0628bed0 (thiscall on Out, RET 8): Out = A x B. All six products stay on the x87 stack;
// x = (a.y*b.z) - (a.z*b.y), y = (b.x*a.z) - (a.x*b.z), z = (a.x*b.y) - (a.y*b.x), each rounded
// to float at its store; every operand is read before the first store (alias-safe). The
// PhySys copy of the same retail function is PhySys_Vec3Cross (common_physys.cpp, on Vector).
inline void IVP_CalcCrossProduct(IVP_U_Point* Out, const IVP_U_Point* A, const IVP_U_Point* B)
{
    const double Ax = A->k[0], Ay = A->k[1], Az = A->k[2];
    const double Bx = B->k[0], By = B->k[1], Bz = B->k[2];
    const double Y = Bx * Az - Ax * Bz;
    const double X = Ay * Bz - Az * By;
    const double Z = Ax * By - Ay * Bx;
    Out->k[0] = (float)(X);
    Out->k[1] = (float)(Y);
    Out->k[2] = (float)(Z);
}

// FUN_063485e0 (.text:063485e0-0634865a), thiscall (v), RET 4: a vector perpendicular to v.
//   this.xyz = v.xyz. Largest |component|: max starts at 0.0f, index at 0, and the scan runs
//   from index 2 down to 0 with FCOM; TEST AH,0x41; JNZ skip -> only a strictly greater |c|
//   replaces it (ties keep the higher index), the new max is spilled as float.
//   prev = index - 1, wrapping -1 to 2. this[index] = this[prev], this[prev] = -this[index]
//   (FMUL -1.0f). Then FUN_0628bed0(this; this, v): this = this x v.
inline void IVP_U_Point::set_arbitrary_perpendicular_to(const IVP_U_Point *v)
{
    k[0] = v->k[0];
    k[1] = v->k[1];
    k[2] = v->k[2];

    int MaxAxis = 0;
    float MaxAbs = 0.0f;
    for (int Axis = 2; Axis >= 0; --Axis)
    {
        const double Abs = fabs((double)(k[Axis]));
        if (Abs > MaxAbs)
        {
            MaxAbs = (float)(Abs);
            MaxAxis = Axis;
        }
    }

    const float Negated = (float)((double)(k[MaxAxis]) * -1.0);
    int PrevAxis = MaxAxis - 1;
    if (PrevAxis < 0)
        PrevAxis = 2;
    k[MaxAxis] = k[PrevAxis];
    k[PrevAxis] = Negated;

    IVP_CalcCrossProduct(this, this, v);
}

// 48-byte 3x3 rotation matrix (3 rows of 16-byte IVP_U_Point)
struct IVP_U_Matrix3
{
    IVP_U_Point rows[3];

    IVP_U_Point *get_row(int index) { return &rows[index]; }
    const IVP_U_Point *get_row(int index) const { return &rows[index]; }

    // FUN_06348580 (.text:06348580-063485d6), thiscall (in, out), RET 8: out = M * in.
    //   y = (r1.x*in.x + r1.z*in.z) + r1.y*in.y stays on the stack,
    //   z = (r2.x*in.x + r2.z*in.z) + r2.y*in.y is spilled as float,
    //   x = (r0.z*in.z + r0.y*in.y) + r0.x*in.x.
    // Every read precedes the first store; the stores go z, x, y.
    void vmult(const IVP_U_Point *p_in, IVP_U_Point *p_out) const
    {
        const double X = p_in->k[0];
        const double Y = p_in->k[1];
        const double Z = p_in->k[2];
        const double Ry = (rows[1].k[0] * X + rows[1].k[2] * Z) + rows[1].k[1] * Y;
        const float Rz = (float)((rows[2].k[0] * X + rows[2].k[2] * Z) + rows[2].k[1] * Y);
        const double Rx = (rows[0].k[2] * Z + rows[0].k[1] * Y) + rows[0].k[0] * X;
        p_out->k[2] = Rz;
        p_out->k[0] = (float)(Rx);
        p_out->k[1] = (float)(Ry);
    }

    // FUN_06366010 (.text:06366010-06366066), thiscall (in, out), RET 8: out = M^T * in.
    //   y = (r0.y*in.x + r2.y*in.z) + r1.y*in.y stays on the stack,
    //   z = (r0.z*in.x + r2.z*in.z) + r1.z*in.y is spilled as float,
    //   x = (r2.x*in.z + r1.x*in.y) + r0.x*in.x.
    // Every read precedes the first store; the stores go z, x, y.
    void vimult(const IVP_U_Point *p_in, IVP_U_Point *p_out) const
    {
        const double X = p_in->k[0];
        const double Y = p_in->k[1];
        const double Z = p_in->k[2];
        const double Ry = (rows[0].k[1] * X + rows[2].k[1] * Z) + rows[1].k[1] * Y;
        const float Rz = (float)((rows[0].k[2] * X + rows[2].k[2] * Z) + rows[1].k[2] * Y);
        const double Rx = (rows[2].k[0] * Z + rows[1].k[0] * Y) + rows[0].k[0] * X;
        p_out->k[2] = Rz;
        p_out->k[0] = (float)(Rx);
        p_out->k[1] = (float)(Ry);
    }
};

// 64-byte 3x4 transformation matrix
struct IVP_U_Matrix : public IVP_U_Matrix3
{
    IVP_U_Point vv; // translation offset at +0x30

    // FUN_063481b0 (.text:063481b0-063481cc), thiscall: REP STOSD zeroes all 0x10 dwords
    // (the rows' 4th floats and vv.hesse included), then 1.0f at +0x00 / +0x14 / +0x28.
    void init()
    {
        memset(this, 0, sizeof(IVP_U_Matrix));
        rows[0].k[0] = 1.0f;
        rows[1].k[1] = 1.0f;
        rows[2].k[2] = 1.0f;
    }

    // FUN_063484e0 (.text:063484e0-06348573), thiscall (in, out), RET 8: out = M * in + vv.
    // The compiler spills the x and y partial sums as floats after every term:
    //   tx = float(r00*x); ty = float(r10*x); ez = x*r20 (kept);
    //   tx = float(r01*y + tx); ty = float(r11*y + ty); ez = ez + r21*y;
    //   tx = float(r02*z + tx); out.y = (r12*z + ty) + vv.y (kept);
    //   out.z = float((r22*z + ez) + vv.z) (temp); out.x = tx + vv.x.
    // in.z is copied to a local before any store; the stores go z, x, y.
    void vmult4(const IVP_U_Point *p_in, IVP_U_Point *p_out) const
    {
        const double X = p_in->k[0];
        float Tx = (float)(rows[0].k[0] * X);
        float Ty = (float)(rows[1].k[0] * X);
        double Ez = X * rows[2].k[0];
        const double Y = p_in->k[1];
        const float Z = p_in->k[2];
        Tx = (float)(rows[0].k[1] * Y + Tx);
        Ty = (float)(rows[1].k[1] * Y + Ty);
        Ez = Ez + rows[2].k[1] * Y;
        Tx = (float)((double)(rows[0].k[2]) * Z + Tx);
        const double Ry = ((double)(rows[1].k[2]) * Z + Ty) + vv.k[1];
        const float Rz = (float)(((double)(rows[2].k[2]) * Z + Ez) + vv.k[2]);
        const double Rx = (double)(Tx) + vv.k[0];
        p_out->k[2] = Rz;
        p_out->k[0] = (float)(Rx);
        p_out->k[1] = (float)(Ry);
    }

    // FUN_06348470 (.text:06348470-063484cf), thiscall (in, out), RET 8: out = M^T * (in - vv).
    //   d = in - vv stays on the stack (dx, dy, dz);
    //   out.x = (r2.x*dz + r1.x*dy) + r0.x*dx, likewise y and z, each stored as float in
    //   the order x, y, z after d is complete.
    void vimult4(const IVP_U_Point *p_in, IVP_U_Point *p_out) const
    {
        const double Dx = (double)(p_in->k[0]) - vv.k[0];
        const double Dy = (double)(p_in->k[1]) - vv.k[1];
        const double Dz = (double)(p_in->k[2]) - vv.k[2];
        p_out->k[0] = (float)((rows[2].k[0] * Dz + rows[1].k[0] * Dy) + rows[0].k[0] * Dx);
        p_out->k[1] = (float)((rows[2].k[1] * Dz + rows[1].k[1] * Dy) + rows[0].k[1] * Dx);
        p_out->k[2] = (float)((rows[2].k[2] * Dz + rows[1].k[2] * Dy) + rows[0].k[2] * Dx);
    }

    // FUN_063481d0 (.text:063481d0-063482b9), thiscall (mb, result), RET 8:
    // result = this * mb (rotation), result.vv = this.rot * mb.vv + this.vv.
    //   t = FUN_06348580(this; &mb.vv) into a float local, first.
    //   Per row r of this (a), into a float local c:
    //     c[r][0] = (b00*a.x + b20*a.z) + b10*a.y
    //     c[r][1] = (b21*a.z + b11*a.y) + b01*a.x
    //     c[r][2] = (b22*a.z + b12*a.y) + b02*a.x
    //   result.rows = c (k[0..2] only: FLD/FSTP for row 0, FUN_0628bdf0 for rows 1 and 2).
    //   Then (reading this.vv only now): y = t.y + vv.y (kept), z = float(t.z + vv.z),
    //   x = t.x + vv.x; stored z, x, y.
    // Retail callers pass result == this (06347503) or a separate local (06343da4).
    void mmult4(const IVP_U_Matrix *mb, IVP_U_Matrix *result) const
    {
        IVP_U_Point Translation;
        vmult(&mb->vv, &Translation);

        float C[3][3];
        for (int Row = 0; Row < 3; ++Row)
        {
            const double Ax = rows[Row].k[0];
            const double Ay = rows[Row].k[1];
            const double Az = rows[Row].k[2];
            C[Row][0] = (float)((mb->rows[0].k[0] * Ax + mb->rows[2].k[0] * Az) + mb->rows[1].k[0] * Ay);
            C[Row][1] = (float)((mb->rows[2].k[1] * Az + mb->rows[1].k[1] * Ay) + mb->rows[0].k[1] * Ax);
            C[Row][2] = (float)((mb->rows[2].k[2] * Az + mb->rows[1].k[2] * Ay) + mb->rows[0].k[2] * Ax);
        }
        for (int Row = 0; Row < 3; ++Row)
        {
            result->rows[Row].k[0] = C[Row][0];
            result->rows[Row].k[1] = C[Row][1];
            result->rows[Row].k[2] = C[Row][2];
        }

        const double Ty = (double)(Translation.k[1]) + vv.k[1];
        const float Tz = (float)((double)(Translation.k[2]) + vv.k[2]);
        const double Tx = (double)(Translation.k[0]) + vv.k[0];
        result->vv.k[2] = Tz;
        result->vv.k[0] = (float)(Tx);
        result->vv.k[1] = (float)(Ty);
    }

    // FUN_063482c0 (.text:063482c0-063483c9), thiscall (mb, result), RET 8:
    // result = this^-1 * mb for a rigid this: rotation this^T * mb.rot, translation
    // this^T * (mb.vv - this.vv).
    //   d = float(mb.vv - vv) (x, y, z); FUN_06366010(this; &d, &result.vv) -- written first.
    //   t = this^T copied into a float local (t[r][c] = rows[c][r]).
    //   Per row r (m = t[r]):
    //     c1 = (b21*m.z + b01*m.x) + b11*m.y            (kept)
    //     c2 = float((b12*m.y + b02*m.x) + b22*m.z)      (temp)
    //     c0 = (b00*m.x + b20*m.z) + b10*m.y
    //   written back into t[r] as 2, 0, 1; then result.rows = t via FUN_0628bdf0.
    void mimult4(const IVP_U_Matrix *mb, IVP_U_Matrix *result) const
    {
        IVP_U_Point Delta;
        Delta.k[0] = (float)((double)(mb->vv.k[0]) - vv.k[0]);
        Delta.k[1] = (float)((double)(mb->vv.k[1]) - vv.k[1]);
        Delta.k[2] = (float)((double)(mb->vv.k[2]) - vv.k[2]);
        vimult(&Delta, &result->vv);

        float T[3][3];
        for (int Row = 0; Row < 3; ++Row)
        {
            T[Row][0] = rows[0].k[Row];
            T[Row][1] = rows[1].k[Row];
            T[Row][2] = rows[2].k[Row];
        }
        for (int Row = 0; Row < 3; ++Row)
        {
            const double Mx = T[Row][0];
            const double My = T[Row][1];
            const double Mz = T[Row][2];
            const double C1 = (mb->rows[2].k[1] * Mz + mb->rows[0].k[1] * Mx) + mb->rows[1].k[1] * My;
            const float C2 = (float)((mb->rows[1].k[2] * My + mb->rows[0].k[2] * Mx) + mb->rows[2].k[2] * Mz);
            const double C0 = (mb->rows[0].k[0] * Mx + mb->rows[2].k[0] * Mz) + mb->rows[1].k[0] * My;
            T[Row][2] = C2;
            T[Row][0] = (float)(C0);
            T[Row][1] = (float)(C1);
        }
        for (int Row = 0; Row < 3; ++Row)
        {
            result->rows[Row].k[0] = T[Row][0];
            result->rows[Row].k[1] = T[Row][1];
            result->rows[Row].k[2] = T[Row][2];
        }
    }

    // FUN_063483d0 (.text:063483d0-06348466), thiscall (source), RET 4: this = source^-1 for
    // a rigid source. The diagonal is copied (00, 11, 22), then each off-diagonal pair
    // (1,0)/(0,1), (2,0)/(0,2), (2,1)/(1,2) is loaded from source and stored swapped (both
    // loads before both stores, so this == source works). Only k[0..2] of each row are
    // written. Then FUN_06348580(this; &source.vv) into a float local t and vv = -t.
    void set_inverse(const IVP_U_Matrix *Source)
    {
        rows[0].k[0] = Source->rows[0].k[0];
        rows[1].k[1] = Source->rows[1].k[1];
        rows[2].k[2] = Source->rows[2].k[2];
        for (int Row = 1; Row < 3; ++Row)
        {
            for (int Col = 0; Col < Row; ++Col)
            {
                const float Lower = Source->rows[Row].k[Col];
                const float Upper = Source->rows[Col].k[Row];
                rows[Row].k[Col] = Upper;
                rows[Col].k[Row] = Lower;
            }
        }

        IVP_U_Point RotatedTranslation;
        vmult(&Source->vv, &RotatedTranslation);
        vv.k[0] = -RotatedTranslation.k[0];
        vv.k[1] = -RotatedTranslation.k[1];
        vv.k[2] = -RotatedTranslation.k[2];
    }
};

// 16-byte Quaternion
struct IVP_U_Quat
{
    float x;
    float y;
    float z;
    float w;

    void set_matrix(IVP_U_Matrix3 *out_matrix) const;
    void set_from_matrix(const IVP_U_Matrix3 *m);
    void set_interpolate_quat(const IVP_U_Quat *q0, const IVP_U_Quat *q1, float alpha);
    void normize();
    void fast_normize();
    void set_invert_unit(const IVP_U_Quat *q);
    void set_mult_quat(const IVP_U_Quat *q1, const IVP_U_Quat *q2);
    void set_mult_inv_quat(const IVP_U_Quat *q1, const IVP_U_Quat *q2);
    void set_inv_mult_quat(const IVP_U_Quat *q1, const IVP_U_Quat *q2);
    void set_fast_rot_vector(const IVP_U_Point *v, float dt);
    void set_rot_vector(const IVP_U_Point *v, float dt);
};

// FUN_06350bc0 (.text:06350bc0-06350c94), thiscall (out), RET 4: rotation matrix of the quat.
//   x2 = x + x stays on the stack; y2 = y + y and z2 = z + z are spilled as floats.
//   xx, xy, xz, yy, yz, zz are float products; wx = x2*w, wy = y2*w, wz = z2*w stay extended.
//   Stores in address order: 00 = 1-(zz+yy), 01 = xy-wz, 02 = wy+xz, 10 = wz+xy,
//   11 = 1-(zz+xx), 12 = yz-wx, 20 = xz-wy, 21 = wx+yz, 22 = 1-(yy+xx). Rows' k[3] untouched.
inline void IVP_U_Quat::set_matrix(IVP_U_Matrix3 *out_matrix) const
{
    const double X2 = (double)(x) + x;
    const float Y2 = (float)((double)(y) + y);
    const float Z2 = (float)((double)(z) + z);

    const float XX = (float)(X2 * x);
    const float XY = (float)((double)(Y2) * x);
    const float XZ = (float)((double)(Z2) * x);
    const float YY = (float)((double)(Y2) * y);
    const float YZ = (float)((double)(Z2) * y);
    const float ZZ = (float)((double)(Z2) * z);

    const double WX = X2 * w;
    const double WY = (double)(Y2) * w;
    const double WZ = (double)(Z2) * w;

    out_matrix->rows[0].k[0] = (float)(1.0 - ((double)(ZZ) + YY));
    out_matrix->rows[0].k[1] = (float)((double)(XY) - WZ);
    out_matrix->rows[0].k[2] = (float)(WY + XZ);
    out_matrix->rows[1].k[0] = (float)(WZ + XY);
    out_matrix->rows[1].k[1] = (float)(1.0 - ((double)(ZZ) + XX));
    out_matrix->rows[1].k[2] = (float)((double)(YZ) - WX);
    out_matrix->rows[2].k[0] = (float)((double)(XZ) - WY);
    out_matrix->rows[2].k[1] = (float)(WX + YZ);
    out_matrix->rows[2].k[2] = (float)(1.0 - ((double)(YY) + XX));
}

// FUN_06350df0 (.text:06350df0-06350fa3), thiscall (q0, q1, alpha), RET 0xc.
//   dot = ((q0.z*q1.z + q0.y*q1.y) + q1.w*q0.w) + q0.x*q1.x stays on the stack and is also
//   spilled as float d. FCOM 0.0; TEST AH,0x41; JNZ negate -> dot <= 0 or unordered:
//   dot = -dot, sign = -1.0f, d = float(dot); otherwise sign = 1.0f.
//   FCOMP 0.999f (DAT_063a5630); TEST AH,0x1; JE lerp -> the extended dot >= 0.999 lerps,
//   less or unordered slerps:
//     omega = _CIacos(d); inv = float(1 / sqrt(1 - d*d));
//     s0 = float(sin((1 - alpha) * omega) * inv); s1 = sin(omega * alpha) * inv * sign (kept);
//     x = float(s0*q0.x + s1*q1.x), y, z likewise, w = float(s1*q1.w + s0*q0.w).
//   lerp: c = float((sign*q1.c - q0.c) * alpha + q0.c) for x, y, z; w the same but kept
//     extended after its FST; f = (((x*x + y*y) + z*z) + w*w) * 0.5 with the float x, y, z;
//     g = 1.5 - f; g = g + (0.5 - g*g*f); g = (0.5 - f*(g*g)) + g; every component = float(g*c).
// Each component reads only its own q0/q1 component before its store, so this == q1 is safe.
inline void IVP_U_Quat::set_interpolate_quat(const IVP_U_Quat *q0, const IVP_U_Quat *q1, float alpha)
{
    double Dot = (((double)(q0->z) * q1->z + (double)(q0->y) * q1->y) +
                  (double)(q1->w) * q0->w) +
                 (double)(q0->x) * q1->x;
    float DotF = (float)(Dot);
    float Sign;
    if (Dot > 0.0)
    {
        Sign = 1.0f;
    }
    else
    {
        Dot = -Dot;
        Sign = -1.0f;
        DotF = (float)(Dot);
    }

    if (!(Dot >= (double)(0.999f)))
    {
        const double Omega = acos((double)(DotF));
        const float InvSin = (float)(1.0 / sqrt(1.0 - (double)(DotF) * DotF));
        const float Scale0 = (float)(std::sin((1.0 - (double)(alpha)) * Omega) * InvSin);
        const double Scale1 = (std::sin(static_cast<double>(Omega * alpha)) * InvSin) * Sign;

        x = (float)((double)(Scale0) * q0->x + Scale1 * q1->x);
        y = (float)((double)(Scale0) * q0->y + Scale1 * q1->y);
        z = (float)((double)(Scale0) * q0->z + Scale1 * q1->z);
        w = (float)(Scale1 * q1->w + (double)(Scale0) * q0->w);
        return;
    }

    x = (float)(((double)(Sign) * q1->x - q0->x) * alpha + q0->x);
    y = (float)(((double)(Sign) * q1->y - q0->y) * alpha + q0->y);
    z = (float)(((double)(Sign) * q1->z - q0->z) * alpha + q0->z);
    const double W = ((double)(Sign) * q1->w - q0->w) * alpha + q0->w;
    w = (float)(W);

    const double Half = ((((double)(x) * x + (double)(y) * y) + (double)(z) * z) + W * W) * 0.5;
    double G = 1.5 - Half;
    G = G + (0.5 - (G * G) * Half);
    G = (0.5 - Half * (G * G)) + G;

    x = (float)(G * x);
    y = (float)(G * y);
    z = (float)(G * z);
    w = (float)(G * w);
}

// FUN_06350ca0 (.text:06350ca0-06350de8), thiscall (m), RET 4: quat from a rotation matrix.
//   tr = (m11 + m00) + m22 extended; FCOM 0.0; TEST AH,0x41; JNZ -> tr <= 0 or unordered
//   takes the diagonal branch.
//   tr > 0: s = sqrt(tr + 1.0); w = float(s*0.5); s = 0.5 / s (extended);
//     x = float((m21 - m12)*s), y = float((m02 - m20)*s), z = float((m10 - m01)*s).
//   else: next = {1, 2, 0}; i = 0; FCOMPP m00 vs m11, TEST AH,0x1 -> i = 1 when m00 < m11 or
//     unordered; FCOMPP m[i][i] vs m22 the same way -> i = 2. j = next[i], k = next[j].
//     s = sqrt((m[i][i] - (m[k][k] + m[j][j])) + 1.0); q[i] = float(s*0.5);
//     FCOM 0.0; TEST AH,0x40; JNZ -> s == 0 or unordered keeps s, otherwise s = 0.5 / s;
//     q[3] = float((m[k][j] - m[j][k])*s), q[j] = float((m[j][i] + m[i][j])*s),
//     q[k] = float((m[k][i] + m[i][k])*s); this = q.
//   Both branches end in FUN_06350fb0 (normize).
inline void IVP_U_Quat::set_from_matrix(const IVP_U_Matrix3 *m)
{
    const double Trace = ((double)(m->rows[1].k[1]) + m->rows[0].k[0]) + m->rows[2].k[2];
    if (Trace > 0.0)
    {
        double S = sqrt(Trace + 1.0);
        w = (float)(S * 0.5);
        S = 0.5 / S;
        x = (float)(((double)(m->rows[2].k[1]) - m->rows[1].k[2]) * S);
        y = (float)(((double)(m->rows[0].k[2]) - m->rows[2].k[0]) * S);
        z = (float)(((double)(m->rows[1].k[0]) - m->rows[0].k[1]) * S);
        normize();
        return;
    }

    const int Next[3] = { 1, 2, 0 };
    int I = 0;
    if (!(m->rows[0].k[0] >= m->rows[1].k[1]))
        I = 1;
    if (!(m->rows[I].k[I] >= m->rows[2].k[2]))
        I = 2;
    const int J = Next[I];
    const int K = Next[J];

    double S = sqrt(((double)(m->rows[I].k[I]) - ((double)(m->rows[K].k[K]) + m->rows[J].k[J])) + 1.0);
    float Q[4];
    Q[I] = (float)(S * 0.5);
    // FCOM 0.0; TEST AH,0x40 (C3): equal or unordered skips the division.
    if (S > 0.0 || S < 0.0)
        S = 0.5 / S;

    Q[3] = (float)(((double)(m->rows[K].k[J]) - m->rows[J].k[K]) * S);
    Q[J] = (float)(((double)(m->rows[J].k[I]) + m->rows[I].k[J]) * S);
    Q[K] = (float)(((double)(m->rows[K].k[I]) + m->rows[I].k[K]) * S);

    x = Q[0];
    y = Q[1];
    z = Q[2];
    w = Q[3];
    normize();
}

// FUN_06350fb0 (.text:06350fb0-0635100d), thiscall.
//   len = ((x*x + y*y) + z*z) + w*w extended; FCOM 1e-10f (DAT_063a53d8); TEST AH,0x41;
//   JNZ return -> len <= eps or unordered leaves the quat alone. inv = 1.0 / sqrt(len)
//   extended; every component = float(inv * c).
inline void IVP_U_Quat::normize()
{
    const double LenSq = (((double)(x) * x + (double)(y) * y) + (double)(z) * z) +
                         (double)(w) * w;
    if (!(LenSq > (double)(1e-10f)))
        return;
    const double InvLen = 1.0 / sqrt(LenSq);
    x = (float)(InvLen * x);
    y = (float)(InvLen * y);
    z = (float)(InvLen * z);
    w = (float)(InvLen * w);
}

// FUN_06351010 (.text:06351010-063510b7), thiscall.
//   len = float(((x*x + y*y) + z*z) + w*w) (FSTP dword to the PUSH ECX slot).
//   |1.0 - len| FCOMP 1e-6f (DAT_063a5610); TEST AH,0x41; JNZ return -> not greater or
//   unordered leaves the quat alone.
//   f = 1.5 - len*0.5 extended; do f = f + (1.0 - f*f*len)*0.5 while |1.0 - f*f*len| > 1e-6
//   (TEST AH,0x41; JE loop); every component = float(f * c).
inline void IVP_U_Quat::fast_normize()
{
    const float LenSq = (float)((((double)(x) * x + (double)(y) * y) +
                                            (double)(z) * z) +
                                           (double)(w) * w);
    const double Epsilon = (double)(1e-6f);
    if (!(fabs(1.0 - LenSq) > Epsilon))
        return;

    double F = 1.5 - (double)(LenSq) * 0.5;
    do
    {
        F = F + (1.0 - (F * F) * LenSq) * 0.5;
    } while (fabs(1.0 - (F * F) * LenSq) > Epsilon);

    x = (float)(F * x);
    y = (float)(F * y);
    z = (float)(F * z);
    w = (float)(F * w);
}

// FUN_063510c0 (.text:063510c0-063510e0), thiscall (q), RET 4: (-x, -y, -z, w).
inline void IVP_U_Quat::set_invert_unit(const IVP_U_Quat *q)
{
    x = -q->x;
    y = -q->y;
    z = -q->z;
    w = q->w;
}

// FUN_063510f0 (.text:063510f0-06351183), thiscall (q1, q2), RET 8: this = q1 * q2.
//   y = ((q1.w*q2.y + q2.w*q1.y) + q1.z*q2.x) - q2.z*q1.x            (kept)
//   z = float(((q1.w*q2.z + q1.x*q2.y) + q1.z*q2.w) - q2.x*q1.y)      (temp)
//   w = float(((q1.w*q2.w - q1.x*q2.x) - q1.y*q2.y) - q1.z*q2.z)      (temp)
//   x = ((q1.x*q2.w + q2.z*q1.y) + q1.w*q2.x) - q1.z*q2.y
// All reads precede the first store; stores go z, w, x, y.
inline void IVP_U_Quat::set_mult_quat(const IVP_U_Quat *q1, const IVP_U_Quat *q2)
{
    const double Ry = (((double)(q1->w) * q2->y + (double)(q2->w) * q1->y) +
                       (double)(q1->z) * q2->x) -
                      (double)(q2->z) * q1->x;
    const float Rz = (float)((((double)(q1->w) * q2->z + (double)(q1->x) * q2->y) +
                                         (double)(q1->z) * q2->w) -
                                        (double)(q2->x) * q1->y);
    const float Rw = (float)((((double)(q1->w) * q2->w - (double)(q1->x) * q2->x) -
                                         (double)(q1->y) * q2->y) -
                                        (double)(q1->z) * q2->z);
    const double Rx = (((double)(q1->x) * q2->w + (double)(q2->z) * q1->y) +
                       (double)(q1->w) * q2->x) -
                      (double)(q1->z) * q2->y;
    z = Rz;
    w = Rw;
    x = (float)(Rx);
    y = (float)(Ry);
}

// FUN_06351190 (.text:06351190-0635124b), thiscall (q1, q2), RET 8: this = q1 * q2^-1.
// l = FUN_063510c0(q2) in a float local first, then (its own summation order):
//   y = ((l.x*q1.z + l.y*q1.w) + l.w*q1.y) - l.z*q1.x                (kept)
//   z = float(((l.y*q1.x + l.w*q1.z) + l.z*q1.w) - l.x*q1.y)          (temp)
//   w = float(((l.w*q1.w - l.x*q1.x) - l.y*q1.y) - l.z*q1.z)          (temp)
//   x = ((l.w*q1.x + l.z*q1.y) + l.x*q1.w) - l.y*q1.z
// Stores go z, w, x, y.
inline void IVP_U_Quat::set_mult_inv_quat(const IVP_U_Quat *q1, const IVP_U_Quat *q2)
{
    IVP_U_Quat L;
    L.set_invert_unit(q2);
    const double Ry = (((double)(L.x) * q1->z + (double)(L.y) * q1->w) +
                       (double)(L.w) * q1->y) -
                      (double)(L.z) * q1->x;
    const float Rz = (float)((((double)(L.y) * q1->x + (double)(L.w) * q1->z) +
                                         (double)(L.z) * q1->w) -
                                        (double)(L.x) * q1->y);
    const float Rw = (float)((((double)(L.w) * q1->w - (double)(L.x) * q1->x) -
                                         (double)(L.y) * q1->y) -
                                        (double)(L.z) * q1->z);
    const double Rx = (((double)(L.w) * q1->x + (double)(L.z) * q1->y) +
                       (double)(L.x) * q1->w) -
                      (double)(L.y) * q1->z;
    z = Rz;
    w = Rw;
    x = (float)(Rx);
    y = (float)(Ry);
}

// FUN_06351250 (.text:06351250-0635130b), thiscall (q1, q2), RET 8: this = q1^-1 * q2.
// l = FUN_063510c0(q1) in a float local first, then:
//   y = ((l.z*q2.x + l.y*q2.w) + l.w*q2.y) - l.x*q2.z                (kept)
//   z = float(((l.w*q2.z + l.z*q2.w) + l.x*q2.y) - l.y*q2.x)          (temp)
//   w = float(((l.w*q2.w - l.x*q2.x) - l.y*q2.y) - l.z*q2.z)          (temp)
//   x = ((l.y*q2.z + l.w*q2.x) + l.x*q2.w) - l.z*q2.y
// Stores go z, w, x, y.
inline void IVP_U_Quat::set_inv_mult_quat(const IVP_U_Quat *q1, const IVP_U_Quat *q2)
{
    IVP_U_Quat L;
    L.set_invert_unit(q1);
    const double Ry = (((double)(L.z) * q2->x + (double)(L.y) * q2->w) +
                       (double)(L.w) * q2->y) -
                      (double)(L.x) * q2->z;
    const float Rz = (float)((((double)(L.w) * q2->z + (double)(L.z) * q2->w) +
                                         (double)(L.x) * q2->y) -
                                        (double)(L.y) * q2->x);
    const float Rw = (float)((((double)(L.w) * q2->w - (double)(L.x) * q2->x) -
                                         (double)(L.y) * q2->y) -
                                        (double)(L.z) * q2->z);
    const double Rx = (((double)(L.y) * q2->z + (double)(L.w) * q2->x) +
                       (double)(L.x) * q2->w) -
                      (double)(L.z) * q2->y;
    z = Rz;
    w = Rw;
    x = (float)(Rx);
    y = (float)(Ry);
}

// FUN_06350aa0 (.text:06350aa0-06350b17), thiscall (v, dt), RET 8.
//   h = dt*0.5 extended; per axis s = h*v.c, s = s - ((s*s)*s)*(1/6f) (DAT_063a5624).
//   x and y are stored as floats (and re-read as floats); z is stored (FST) but its
//   extended value is squared: w = float(sqrt(1.0 - ((z*z + y*y) + x*x))).
inline void IVP_U_Quat::set_fast_rot_vector(const IVP_U_Point *v, float dt)
{
    const double SixthF = (double)(0.16666667f);
    const double Half = (double)(dt) * 0.5;

    double S = Half * v->k[0];
    S = S - ((S * S) * S) * SixthF;
    const float Sx = (float)(S);
    x = Sx;

    S = Half * v->k[1];
    S = S - ((S * S) * S) * SixthF;
    const float Sy = (float)(S);
    y = Sy;

    S = Half * v->k[2];
    S = S - ((S * S) * S) * SixthF;
    z = (float)(S);

    w = (float)(sqrt(1.0 - ((S * S + (double)(Sy) * Sy) + (double)(Sx) * Sx)));
}

// FUN_06350b20 (.text:06350b20-06350bb7), thiscall (v, dt), RET 8.
//   h = dt*0.5 extended; x = float(FSIN(h*v.x)), y = float(FSIN(h*v.y)); z = FSIN(h*v.z) is
//   stored (FST) and kept extended. len = (x*x + y*y) + z*z with the float x, y.
//   FCOM 1.0; TEST AH,0x41; JNZ -> len <= 1 or unordered keeps len. Otherwise
//   f = 0.997f (DAT_063a5628) / sqrt(len); x, y = float(f*c) (also spilled), z = f*z_float
//   stored and kept, len = (z*z + y*y) + x*x. w = float(sqrt(1.0 - len)).
inline void IVP_U_Quat::set_rot_vector(const IVP_U_Point *v, float dt)
{
    const double Half = (double)(dt) * 0.5;
    x = static_cast<float>(std::sin(static_cast<double>(Half * v->k[0])));
    y = static_cast<float>(std::sin(static_cast<double>(Half * v->k[1])));
    // FSIN; FST dword (one rounding of the 80-bit result) and the value kept on the stack.
    const double Sz = std::sin(static_cast<double>(Half * v->k[2]));
    z = static_cast<float>(std::sin(static_cast<double>(Half * v->k[2])));

    double LenSq = ((double)(x) * x + (double)(y) * y) + Sz * Sz;
    if (LenSq > 1.0)
    {
        const double F = (double)(0.997f) / sqrt(LenSq);
        const float Rx = (float)(F * x);
        x = Rx;
        const float Ry = (float)(F * y);
        y = Ry;
        const double Rz = F * z;
        z = (float)(Rz);
        LenSq = (Rz * Rz + (double)(Ry) * Ry) + (double)(Rx) * Rx;
    }
    w = (float)(sqrt(1.0 - LenSq));
}

static_assert(sizeof(IVP_U_Point) == 0x10, "IVP_U_Point size mismatch");
static_assert(sizeof(IVP_U_Matrix3) == 0x30, "IVP_U_Matrix3 size mismatch");
static_assert(sizeof(IVP_U_Matrix) == 0x40, "IVP_U_Matrix size mismatch");
static_assert(sizeof(IVP_U_Quat) == 0x10, "IVP_U_Quat size mismatch");

// FUN_06350a60 (.text:06350a60-06350a8f), thiscall (this = In, out), RET 4: per component
// out = float(a + a) with a = _CIasin(in) (FADD ST0,ST0 on the unrounded result), stored
// x, y, z, each before the next input component is read.
// CRT asin is the retail result: system msvcrt _CIasin and ucrtbase asin dispatch to the
// same SSE2 core on the reference Windows system under default MXCSR / masked
// x87 exceptions. Other platforms use their own standard math implementations.
inline void IVP_DoubleAsinVector(const IVP_U_Point *In, IVP_U_Point *Out)
{
    double A = asin((double)(In->k[0]));
    Out->k[0] = (float)(A + A);
    A = asin((double)(In->k[1]));
    Out->k[1] = (float)(A + A);
    A = asin((double)(In->k[2]));
    Out->k[2] = (float)(A + A);
}
