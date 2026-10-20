// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "portable_float_load.h"
#include "room_camera.h"

#include "common_csys.h"
#include "common_text.h"
#include "dacom.h"
#include "globals.h"
#include "widescreen.h"   // port-only Hor+ (FREELANCER_WIDESCREEN_SUPPORT)
#include "x86math.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

// Layout: every field sits where FUN_00556280 / FUN_005564b0 write it.
static_assert(offsetof(FRoomCamera, Viewport) == 0x04, "room camera ViewRect +0x04");
static_assert(offsetof(FRoomCamera, HalfFovXDeg) == 0x14, "room camera +0x14");
static_assert(offsetof(FRoomCamera, ZNear) == 0x1c, "room camera +0x1c");
static_assert(offsetof(FRoomCamera, TanHalfFovX) == 0x24, "room camera +0x24");
static_assert(offsetof(FRoomCamera, HalfWidth) == 0x30, "room camera +0x30");
static_assert(offsetof(FRoomCamera, PlaneX) == 0x40, "room camera +0x40");
static_assert(offsetof(FRoomCamera, PlaneY) == 0x4c, "room camera +0x4c");
static_assert(offsetof(FRoomCamera, RefCount) == 0x58, "room camera +0x58");
static_assert(offsetof(FRoomCamera, Orient) == 0x5c, "room camera Orient +0x5c");
static_assert(offsetof(FRoomCamera, Pos) == 0x80, "room camera Pos +0x80");
static_assert(offsetof(FRoomCamera, View) == 0x8c, "room camera View +0x8c");
static_assert(sizeof(FRoomCamera) == 0xbc, "room camera operator_new(0xbc)");

namespace
{
    // .rdata 0x005c901c (0x3c0efa35) -- pi / 360, used on a DOUBLED half-angle.
    const float kPiOver360 = 0.008726646192371845f;
    // .rdata 0x005c77f4 (0x3c8efa35) -- pi / 180.
    const float kPiOver180 = 0.01745329238474369f;
    // .rdata 0x005c8928 (double) -- 180 / pi.
    const double kRadToDeg = 57.29577791868205;
    // .rdata 0x005e139c (0xbfaaaaab) -- -4/3, FUN_005564b0's provisional Y scale.
    const float kMinusFourThirds = -1.3333333730697632f;
    // .rdata 0x005dbc98 (double) -- ClassifySphere's "centre on the eye plane" epsilon.
    const double kClassifyEpsilon = 0.0001;
    // .rdata 0x005df814 (0xc0490fdb) -- -pi.
    const float kMinusPi = -3.1415927410125732f;


    // FUN_0041a390 -- normalise in place. The x87 sequence loads z, y, x and sums
    // (x*x + y*y) + z*z before the FSQRT; 1/len is kept on the stack and each component is
    // multiplied by it.
    void NormalizeXyz(float* V)
    {
        const double Len = sqrt((static_cast<double>(V[0]) * V[0] +
                                 static_cast<double>(V[1]) * V[1]) +
                                static_cast<double>(V[2]) * V[2]);
        const double Inv = 1.0 / Len;
        V[0] = static_cast<float>(Inv * V[0]);
        V[1] = static_cast<float>(Inv * V[1]);
        V[2] = static_cast<float>(Inv * V[2]);
    }

    // FUN_00556e00's second normalise loads the same three floats but sums
    // (y*y + z*z) + x*x (0x00556ec0..0x00556ece).
    void NormalizeYzx(float* V)
    {
        const double Len = sqrt((static_cast<double>(V[1]) * V[1] +
                                 static_cast<double>(V[2]) * V[2]) +
                                static_cast<double>(V[0]) * V[0]);
        const double Inv = 1.0 / Len;
        V[0] = static_cast<float>(Inv * V[0]);
        V[1] = static_cast<float>(Inv * V[1]);
        V[2] = static_cast<float>(Inv * V[2]);
    }

#ifdef FREELANCER_WIDESCREEN_SUPPORT
    // Port-only Hor+ (widescreen.h): +0x14 keeps the horizontal half-angle on a 4:3 surface,
    // +0x24 / the X plane / XScale take its tangent times this factor.
    double HorPlusScale(FRoomCamera const* Camera)
    {
        return WidescreenHorPlusScale(static_cast<long long>(Camera->Viewport[2]) -
                                      Camera->Viewport[0] + 1);
    }
#endif
}

// DAT_006799bc (room_camera.h).
I3DMathEngine* G_LightsCameraMath = nullptr;

I3DMathEngine* LightsCameraMath()
{
    if (G_LightsCameraMath == nullptr)
        G_LightsCameraMath = Acquire3DMathEngine();
    return G_LightsCameraMath;
}

FRoomCamera::FRoomCamera(ViewRect const& Viewport)
    : RefCount(1)
{
    // 0x005562f5 RefCount = 1; FUN_004215b0 on +0x5c (identity 3x3); +0x80..+0x88 = 0;
    // FUN_004b5150 on +0x8c (identity 3x3 and a zero translation).
    int Index = 0;
    for (Index = 0; Index < 9; ++Index)
        Orient[Index] = 0.0f;
    Orient[0] = 1.0f;
    Orient[4] = 1.0f;
    Orient[8] = 1.0f;
    Pos[0] = 0.0f;
    Pos[1] = 0.0f;
    Pos[2] = 0.0f;
    for (Index = 0; Index < 12; ++Index)
        View[Index] = 0.0f;
    View[0] = 1.0f;
    View[4] = 1.0f;
    View[8] = 1.0f;

    // 0x00556327: FUN_005564b0(&ViewRect).
    InitFromViewRect(&Viewport.Left);
}

FRoomCamera::~FRoomCamera()
{
    // FUN_00556bd0: restamps the abstract base vtable (.rdata 0x005e1354) and returns.
}

void FRoomCamera::InitFromViewRect(int const* ViewRect4)
{
    // FUN_005564b0 (RET 4).
    YScale = 1.0f;
    XScale = -1.0f;
    ZNear = 1.0f;
    ZFar = 100000.0f;                   // 0x47c35000
    Aspect = 1.3333333730697632f;       // 0x3faaaaab
    TanHalfFovX = 1.0f;
    TanHalfFovY = 0.75f;                // 0x3f400000
    if (ViewRect4 != nullptr)
    {
        Viewport[0] = ViewRect4[0];
        Viewport[1] = ViewRect4[1];
        Viewport[2] = ViewRect4[2];
        Viewport[3] = ViewRect4[3];
    }
    else
    {
        Viewport[0] = 0;
        Viewport[1] = 0;
        Viewport[2] = 0x27f;
        Viewport[3] = 0x1df;
    }

    // 0x0055651e..0x0055655a: the extents are right-left and bottom-top, no +1 here.
    HalfWidth = static_cast<float>(Viewport[2] - Viewport[0]) * 0.5f;
    HalfHeight = static_cast<float>(Viewport[3] - Viewport[1]) * 0.5f;
    XScale = HalfWidth;
    YScale = HalfHeight * kMinusFourThirds;

    // 0x0055655d: atan(1.0 / 1) in degrees.
    HalfFovXDeg = static_cast<float>(atan2(1.0, 1.0) * kRadToDeg);
    PlaneX[0] = 1.0f;
    PlaneX[1] = 0.0f;
    PlaneX[2] = 1.0f;
    NormalizeXyz(PlaneX);
    XScale = HalfWidth / TanHalfFovX;

    HalfFovYDeg = static_cast<float>(atan2(static_cast<double>(TanHalfFovY), 1.0) * kRadToDeg);
    PlaneY[0] = 0.0f;
    PlaneY[2] = TanHalfFovY;
    PlaneY[1] = 1.0f;
    NormalizeXyz(PlaneY);
    YScale = -(HalfHeight / TanHalfFovY);
}

void FRoomCamera::RebuildPlaneX()
{
    // FUN_005566a0.
#ifdef FREELANCER_WIDESCREEN_SUPPORT
    // Port-only Hor+: +0x14 is the 4:3 angle the widened +0x24 came from.
    HalfFovXDeg = static_cast<float>(
        atan2(static_cast<double>(TanHalfFovX) / HorPlusScale(this), 1.0) * kRadToDeg);
#else
    HalfFovXDeg = static_cast<float>(atan2(static_cast<double>(TanHalfFovX), 1.0) * kRadToDeg);
#endif
    PlaneX[0] = 1.0f;
    PlaneX[2] = TanHalfFovX;
    PlaneX[1] = 0.0f;
    NormalizeXyz(PlaneX);
    XScale = HalfWidth / TanHalfFovX;
}

void FRoomCamera::RebuildPlaneY()
{
    // FUN_00556710.
    HalfFovYDeg = static_cast<float>(atan2(static_cast<double>(TanHalfFovY), 1.0) * kRadToDeg);
    PlaneY[0] = 0.0f;
    PlaneY[2] = TanHalfFovY;
    PlaneY[1] = 1.0f;
    NormalizeXyz(PlaneY);
    YScale = -(HalfHeight / TanHalfFovY);
}

GENRESULT DACOM_API FRoomCamera::QueryInterface(char const* Iid, void** Out)
{
    // LAB_00558600: one-entry table at .rdata 0x005e143c {"1.11_ICamera", offset 0}. A hit
    // hands out `this` and AddRefs through slot 1; a miss nulls *Out and returns 0xfffffffd.
    if (strcmp(Iids::Camera, Iid) == 0)
    {
        AddRef();
        *Out = this;
        return GR_OK;
    }
    *Out = nullptr;
    return GR_INTERFACE_UNSUPPORTED;
}

unsigned long DACOM_API FRoomCamera::AddRef()
{
    // LAB_005586b0.
    RefCount += 1;
    return RefCount;
}

unsigned long DACOM_API FRoomCamera::Release()
{
    // LAB_005586c0: unsigned decrement that stops at zero; at zero the count is set back to 1
    // and slot 17 runs with 1 (FUN_005585e0 -> FUN_00556bd0 + free), returning 0.
    if (RefCount > 0)
        RefCount -= 1;
    if (RefCount == 0)
    {
        RefCount = 1;
        delete this;
        return 0;
    }
    return RefCount;
}

float const* DACOM_API FRoomCamera::GetPosition()
{
    return Pos;
}

float const* DACOM_API FRoomCamera::GetOrientation()
{
    return Orient;
}

int const* DACOM_API FRoomCamera::GetViewport()
{
    return Viewport;
}

float DACOM_API FRoomCamera::GetHalfFovXDeg()
{
    return HalfFovXDeg;
}

float DACOM_API FRoomCamera::GetFovYDeg()
{
    return HalfFovYDeg;
}

float DACOM_API FRoomCamera::GetZNear()
{
    return ZNear;
}

float DACOM_API FRoomCamera::GetZFar()
{
    return ZFar;
}

float DACOM_API FRoomCamera::GetAspect()
{
    return Aspect;
}

// LAB_00556680 / LAB_00556690: FLD float, FDIV float, RET 4 -- the quotient stays unrounded.
double DACOM_API FRoomCamera::GetXScaleOverNear()
{
    return static_cast<double>(XScale) / ZNear;
}

double DACOM_API FRoomCamera::GetYScaleOverNear()
{
    return static_cast<double>(YScale) / ZNear;
}

int DACOM_API FRoomCamera::Project(float* OutX, float* OutY, float* OutZ, float const* World)
{
    // LAB_005568b0 (RET 0x14). Slot 4 hands back +0x5c, which retail passes to the math
    // engine's InverseTransform (+0x3c) as a whole FTransform: the orientation and the
    // position that follows it at +0x80.
    FTransform const* Xform = reinterpret_cast<FTransform const*>(GetOrientation());
    I3DMathEngine* Math = LightsCameraMath();
    FVector Local;
    Math->InverseTransform(Local, *Xform, *reinterpret_cast<FVector const*>(World));

    // 0x005568e5: FLD near; FCHS; FCOMP local.z; TEST AH,1; JNZ -> fail when -near < z or
    // the compare is unordered.
    if (!(-ZNear >= Local.Z))
        return 0;

    // DAT_005c7500 = -1.0f.
    const double K = -1.0 / static_cast<double>(Local.Z);
    *OutX = static_cast<float>((static_cast<double>(Local.X) * XScale * K + Viewport[0]) +
                               HalfWidth);
    *OutY = static_cast<float>((static_cast<double>(Local.Y) * YScale * K + Viewport[1]) +
                               HalfHeight);
    *OutZ = -Local.Z;
    return 1;
}

void DACOM_API FRoomCamera::Unproject(float* OutWorld3, float ScreenX, float ScreenY)
{
    // LAB_00556950 (RET 0x10). Both screen offsets are rounded to float in their argument
    // slots before the orientation is fetched through slot 4.
    const float A =
        static_cast<float>((ScreenX - (static_cast<double>(Viewport[0]) + HalfWidth)) / XScale);
    const float B =
        static_cast<float>((ScreenY - (static_cast<double>(Viewport[1]) + HalfHeight)) / YScale);
    float const* M = GetOrientation();

    // out = M * (A, B, -1), row by row, with retail's temporaries (0x0055697d..0x00556a31):
    // M1*B, M0*A and M6*A are FSTP'd to floats; M4*B, M3*A and M7*B stay on the x87 stack.
    const float M1B = static_cast<float>(static_cast<double>(M[1]) * static_cast<double>(B));
    const double M4B = static_cast<double>(M[4]) * static_cast<double>(B);
    const double M7B = static_cast<double>(M[7]) * static_cast<double>(B);
    const float M0A = static_cast<float>(static_cast<double>(M[0]) * static_cast<double>(A));
    const double M3A = static_cast<double>(M[3]) * static_cast<double>(A);
    const float M6A = static_cast<float>(static_cast<double>(M[6]) * static_cast<double>(A));
    const float R0 = static_cast<float>(static_cast<double>(M0A) + static_cast<double>(M1B));
    const float R1 = static_cast<float>(M3A + M4B);
    const float R2 = static_cast<float>(static_cast<double>(M6A) + M7B);
    OutWorld3[0] = static_cast<float>(static_cast<double>(R0) - static_cast<double>(M[2]));
    OutWorld3[1] = static_cast<float>(static_cast<double>(R1) - static_cast<double>(M[5]));
    OutWorld3[2] = static_cast<float>(static_cast<double>(R2) - static_cast<double>(M[8]));

    // 0x00556a34: scale by ZNear.
    OutWorld3[0] = ZNear * OutWorld3[0];
    OutWorld3[1] = ZNear * OutWorld3[1];
    OutWorld3[2] = ZNear * OutWorld3[2];
}

int DACOM_API FRoomCamera::ClassifySphere(float const* Center, float Radius)
{
    // LAB_00556780 (RET 0xc). Centre is already in camera space (-Z forward).
    // 1 = outside, 2 = too small on screen, 3 = straddling a plane, 4 = inside. Every sum and
    // product below stays on the x87 stack in retail (double here); only the -R temporary is
    // stored as a float.
    const double Z = Center[2];
    const double R = Radius;

    // 0x00556784..0x0055679a: R - near FCOMP z, TEST AH,5 / JP -- less returns 1.
    if (R - static_cast<double>(ZNear) < Z)
        return 1;

    // 0x005567a3..0x005567bf: S = R + z kept; S FCOMPP -near, TEST AH,5 / JNP keeps 4 only when
    // S < -near.
    int Result = 4;
    const double S = R + Z;
    if (!(S < -static_cast<double>(ZNear)))
        Result = 3;
    // 0x005567c4..0x005567d4: S FCOMPP -far, TEST AH,5 / JP -- less returns 1.
    if (S < -static_cast<double>(ZFar))
        return 1;
    // 0x005567dd..0x005567ee: R - far FCOMP z, TEST AH,5 / JNP -- not less makes it 3.
    if (!(R - static_cast<double>(ZFar) < Z))
        Result = 3;

    // 0x005567f3..0x0055682d: side planes. FCOM R, TEST AH,0x41 / JNZ keeps going on <=, == or
    // unordered (greater returns 1); then FCOMP the float -R, TEST AH,5 / JNP: not less is 3.
    const float NegRadius = -Radius;
    double Dist = fabs(static_cast<double>(Center[0])) * static_cast<double>(PlaneX[0]) +
                  static_cast<double>(PlaneX[2]) * Z;
    if (Dist > R)
        return 1;
    if (!(Dist < static_cast<double>(NegRadius)))
        Result = 3;

    // 0x00556832..0x00556863: the same on y with PlaneY.
    Dist = fabs(static_cast<double>(Center[1])) * static_cast<double>(PlaneY[1]) +
           static_cast<double>(PlaneY[2]) * Z;
    if (Dist > R)
        return 1;
    if (!(Dist < static_cast<double>(NegRadius)))
        Result = 3;

    // 0x00556868: TEST AH,1 on |z| vs 1e-4 -- below it (or unordered) skips the size test.
    if (!(fabs(Z) >= kClassifyEpsilon))
        return Result;

    // 0x0055687a..0x005568a5: Q = -(R / z), ((YScale*XScale)*Q)*Q * -pi, FCOMP 1.0f,
    // TEST AH,5 / JNP -- less returns 2.
    const double Q = -(R / Z);
    const double Area = static_cast<double>(YScale) * static_cast<double>(XScale) * Q * Q *
                        static_cast<double>(kMinusPi);
    if (Area < 1.0)
        return 2;
    return Result;
}

float const* DACOM_API FRoomCamera::GetViewTransform()
{
    // LAB_00556aa0 (RET 4): TransposeTransform(+0x5c, +0x80) through the shared math engine,
    // then the transposed orientation and the negated result go to +0x8c.
    I3DMathEngine* Math = LightsCameraMath();
    FVector Rotated;
    Math->TransposeTransform(Rotated, *reinterpret_cast<FMatrix const*>(Orient),
                             *reinterpret_cast<FVector const*>(Pos));

    View[0] = Orient[0];
    View[1] = Orient[3];
    View[2] = Orient[6];
    View[3] = Orient[1];
    View[4] = Orient[4];
    View[5] = Orient[7];
    View[6] = Orient[2];
    View[7] = Orient[5];
    View[8] = Orient[8];
    View[9] = -Rotated.X;
    View[10] = -Rotated.Y;
    View[11] = -Rotated.Z;
    return View;
}

int DACOM_API FRoomCamera::ProjectVec(float* /*Out3*/, float const* /*World*/)
{
    // This class's slot 17 is its scalar deleting destructor (FUN_005585e0), not a
    // ProjectVec; see room_camera.h. Destruction goes through Release / RoomCameraDestroy.
    // The int return only follows the shared header (space camera slot 17 returns Project's
    // result); nothing in the port reaches this override.
    return 0;
}

void FRoomCamera::GetVelocity(float* /*Out3*/)
{
    // This class's slot 18 is LAB_00556a90 (GetOrientation again); see room_camera.h.
}

int FRoomCamera::SetProperty(char const* /*Key*/, char const* /*Value*/)
{
    // This class's slot 19 is SetPosition (FUN_00556b70); see room_camera.h.
    return 0;
}

void DACOM_API FRoomCamera::Slot20()
{
    // This class's slot 20 is SetOrientation (FUN_00556b90); see room_camera.h.
}

void DACOM_API FRoomCamera::Slot21()
{
    // This class's slot 21 is SetTransform (LAB_00556bb0); see room_camera.h.
}

void FRoomCamera::SetPosition(float const* Pos3)
{
    // FUN_00556b70.
    Pos[0] = Pos3[0];
    Pos[1] = Pos3[1];
    Pos[2] = Pos3[2];
}

void FRoomCamera::SetOrientation(float const* Orient9)
{
    // FUN_00556b90.
    memcpy(Orient, Orient9, sizeof(float) * 9);
}

void FRoomCamera::SetTransform(float const* Transform12)
{
    // LAB_00556bb0: MOVSD x 0xc into +0x5c -- the orientation and then the position.
    memcpy(Orient, Transform12, sizeof(float) * 12);
}

FRoomCamera* RoomCameraConstruct(float const* PosOrient12, Rect const& Viewport,
                                 float HalfFovXDeg, float ZNear, float ZFar)
{
    // FUN_00556280.
    const ViewRect Edges = Rect_to_ViewRect(Viewport);
    FRoomCamera* Cam = new FRoomCamera(Edges);

    // 0x00556330: tan of the doubled angle times pi/360, rounded to float in the argument slot.
    const float Tan = static_cast<float>(
        tan((static_cast<double>(HalfFovXDeg) + HalfFovXDeg) * kPiOver360));
#ifdef FREELANCER_WIDESCREEN_SUPPORT
    // Port-only Hor+: +0x14 keeps the 4:3 angle, +0x24 and the X plane the widened tangent.
    Cam->TanHalfFovX = static_cast<float>(static_cast<double>(Tan) * HorPlusScale(Cam));
#else
    Cam->TanHalfFovX = Tan;
#endif
    Cam->HalfFovXDeg = static_cast<float>(atan2(static_cast<double>(Tan), 1.0) * kRadToDeg);
    // FUN_00556480(+0x40, 1.0, 0, tan) then FUN_0041a390.
    Cam->PlaneX[0] = 1.0f;
    Cam->PlaneX[1] = 0.0f;
    Cam->PlaneX[2] = Cam->TanHalfFovX;
    NormalizeXyz(Cam->PlaneX);
    Cam->XScale = Cam->HalfWidth / Cam->TanHalfFovX;
    Cam->Aspect = Cam->TanHalfFovX / Cam->TanHalfFovY;

    // 0x00556395: FILD Width / FIDIV Height of the Rect argument (extents, not edges).
    const double RectAspect =
        static_cast<double>(Viewport.Width) / static_cast<double>(Viewport.Height);
    Cam->Aspect = static_cast<float>(RectAspect);
    const double TanY = static_cast<double>(Cam->TanHalfFovX) / RectAspect;
    Cam->TanHalfFovY = static_cast<float>(TanY);
    Cam->HalfFovYDeg = static_cast<float>(atan2(TanY, 1.0) * kRadToDeg);
    Cam->PlaneY[0] = 0.0f;
    Cam->PlaneY[2] = Cam->TanHalfFovY;
    Cam->PlaneY[1] = 1.0f;
    // FUN_00410190 returns the length; the ctor divides and scales inline.
    NormalizeXyz(Cam->PlaneY);
    Cam->ZNear = ZNear;
    Cam->YScale = -(Cam->HalfHeight / Cam->TanHalfFovY);

    // 0x005563f5: (x + x) * 0.5 is stored back, then tan(x * pi/180) and FUN_005566a0.
    const double HalfX = (static_cast<double>(Cam->HalfFovXDeg) + Cam->HalfFovXDeg) * 0.5;
    Cam->HalfFovXDeg = static_cast<float>(HalfX);
#ifdef FREELANCER_WIDESCREEN_SUPPORT
    Cam->TanHalfFovX = static_cast<float>(tan(HalfX * kPiOver180) * HorPlusScale(Cam));
#else
    Cam->TanHalfFovX = static_cast<float>(tan(HalfX * kPiOver180));
#endif
    Cam->RebuildPlaneX();
    Cam->Aspect = Cam->TanHalfFovX / Cam->TanHalfFovY;

    const double HalfY = (static_cast<double>(Cam->HalfFovYDeg) + Cam->HalfFovYDeg) * 0.5;
    Cam->HalfFovYDeg = static_cast<float>(HalfY);
    Cam->TanHalfFovY = static_cast<float>(tan(HalfY * kPiOver180));
    Cam->RebuildPlaneY();
    Cam->Aspect = Cam->TanHalfFovX / Cam->TanHalfFovY;
    Cam->ZFar = ZFar;

    // 0x00556458 / 0x00556462: slots 19 and 20 with the position and the orientation.
    Cam->SetPosition(PosOrient12);
    Cam->SetOrientation(PosOrient12 + 3);
    return Cam;
}

FRoomCamera* RoomCameraCreate()
{
    // FUN_00556be0.
    float PosOrient[12];
    int Index = 0;
    for (Index = 0; Index < 12; ++Index)
        PosOrient[Index] = 0.0f;
    PosOrient[3] = 1.0f;
    PosOrient[7] = 1.0f;
    PosOrient[11] = 1.0f;
    const Rect Viewport(0, 0, static_cast<int>(G_RenderSurfaceWidth),
                        static_cast<int>(G_RenderSurfaceHeight));
    return RoomCameraConstruct(PosOrient, Viewport, 35.0f, 0.25f, 1000000.0f);
}

void RoomCameraDestroy(FRoomCamera* Camera)
{
    // FUN_00556ca0.
    if (Camera != nullptr)
        delete Camera;
}

void RoomCameraSetPosition(FRoomCamera* Camera, float const* Pos3)
{
    // FUN_00556cb0.
    Camera->SetPosition(Pos3);
}

void RoomCameraSetOrientation(FRoomCamera* Camera, float const* Orient9)
{
    // FUN_00556cc0.
    Camera->SetOrientation(Orient9);
}

void RoomCameraSetTransform(FRoomCamera* Camera, float const* Transform12)
{
    // FUN_00556cd0.
    Camera->SetTransform(Transform12);
}

void RoomCameraSetHalfFovX(FRoomCamera* Camera, float HalfFovXDeg)
{
    RoomCameraSetHalfFovXValue(Camera, &HalfFovXDeg);
}

void RoomCameraSetHalfFovXValue(FRoomCamera* Camera, const float* HalfFovXDeg)
{
    // FUN_00556ce0.
    int const* Edges = Camera->GetViewport();
    const double Tan = tan((static_cast<double>(*HalfFovXDeg) + *HalfFovXDeg) * kPiOver360);
#ifdef FREELANCER_WIDESCREEN_SUPPORT
    // Port-only Hor+: +0x14 keeps the 4:3 angle, +0x24 and the X plane the widened tangent.
    const double TanX = Tan * HorPlusScale(Camera);
#else
    const double TanX = Tan;
#endif
    Camera->TanHalfFovX = static_cast<float>(TanX);
    Camera->HalfFovXDeg = static_cast<float>(atan2(Tan, 1.0) * kRadToDeg);
    Camera->PlaneX[0] = 1.0f;
    Camera->PlaneX[1] = 0.0f;
    Camera->PlaneX[2] = static_cast<float>(TanX);
    NormalizeXyz(Camera->PlaneX);
    Camera->XScale = Camera->HalfWidth / Camera->TanHalfFovX;

    // 0x00556d7d: inclusive edges, so both extents get +1 here.
    const int Width = Edges[2] - Edges[0] + 1;
    const int Height = Edges[3] - Edges[1] + 1;
    const double EdgeAspect = static_cast<double>(Width) / static_cast<double>(Height);
    Camera->Aspect = static_cast<float>(EdgeAspect);
    const double TanY = static_cast<double>(Camera->TanHalfFovX) / EdgeAspect;
    Camera->TanHalfFovY = static_cast<float>(TanY);
    Camera->HalfFovYDeg = static_cast<float>(atan2(TanY, 1.0) * kRadToDeg);
    Camera->PlaneY[0] = 0.0f;
    Camera->PlaneY[1] = 1.0f;
    Camera->PlaneY[2] = static_cast<float>(TanY);
    NormalizeXyz(Camera->PlaneY);
    Camera->YScale = -(Camera->HalfHeight / Camera->TanHalfFovY);
}

void RoomCameraSetZNear(FRoomCamera* Camera, float ZNear)
{
    RoomCameraSetZNearValue(Camera, &ZNear);
}

void RoomCameraSetZNearValue(FRoomCamera* Camera, const float* ZNear)
{
    // FUN_00556e00.
    // Retail consumes the raw argument with FLD/FSTP at this callee boundary.
    float* Destination = &Camera->ZNear;
    PortableFloatLoad::Copy(Destination, ZNear);

    const double TanX =
        tan((static_cast<double>(Camera->HalfFovXDeg) + Camera->HalfFovXDeg) * kPiOver360);
#ifdef FREELANCER_WIDESCREEN_SUPPORT
    // Port-only Hor+: +0x14 keeps the 4:3 angle, +0x24 (and the X plane built from it) widens.
    Camera->TanHalfFovX = static_cast<float>(TanX * HorPlusScale(Camera));
#else
    Camera->TanHalfFovX = static_cast<float>(TanX);
#endif
    Camera->HalfFovXDeg = static_cast<float>(atan2(TanX, 1.0) * kRadToDeg);
    Camera->PlaneX[0] = 1.0f;
    Camera->PlaneX[2] = Camera->TanHalfFovX;
    Camera->PlaneX[1] = 0.0f;
    NormalizeXyz(Camera->PlaneX);
    Camera->XScale = Camera->HalfWidth / Camera->TanHalfFovX;
    Camera->Aspect = Camera->TanHalfFovX / Camera->TanHalfFovY;

    const double TanY =
        tan((static_cast<double>(Camera->HalfFovYDeg) + Camera->HalfFovYDeg) * kPiOver360);
    Camera->TanHalfFovY = static_cast<float>(TanY);
    Camera->HalfFovYDeg = static_cast<float>(atan2(TanY, 1.0) * kRadToDeg);
    Camera->PlaneY[0] = 0.0f;
    Camera->PlaneY[2] = Camera->TanHalfFovY;
    Camera->PlaneY[1] = 1.0f;
    NormalizeYzx(Camera->PlaneY);

    // 0x00556ef4: 1 / TanHalfFovY is kept on the stack for both products.
    const double InvTanY = 1.0 / static_cast<double>(Camera->TanHalfFovY);
    Camera->YScale = static_cast<float>(-(InvTanY * Camera->HalfHeight));
    Camera->Aspect = static_cast<float>(InvTanY * Camera->TanHalfFovX);
}

void RoomCameraSetZFar(FRoomCamera* Camera, float ZFar)
{
    RoomCameraSetZFarValue(Camera, &ZFar);
}

void RoomCameraSetZFarValue(FRoomCamera* Camera, const float* ZFar)
{
    // FUN_00556f10: ordinary floating assignment at the raw input boundary.
    Camera->ZFar = *ZFar;
}

void RoomVec3Normalize(float* V)
{
    // FUN_0041a390.
    NormalizeXyz(V);
}
