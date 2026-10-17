// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "hud_damage_indicator.h"

#include "common_csys.h"
#include "common_iface.h"
#include "common_ini.h"
#include "game_options.h"
#include "globals.h"
#include "input_manager.h"
#include "nn_ui_pulse.h"
#include "player_behavior.h"
#include "win3d.h"

#include "DALib.h"
#include "dacom.h"
#include "engine/engbase.h"
#include "render/rendcomp.h"
#include "x86math.h"

#include <math.h>
#include <string.h>

static_assert(sizeof(FHudDamageIndicatorPart) == 0x2c, "retail damage-indicator part is 0x2c");
static_assert(sizeof(HUD_DamageIndicator) == 0x414, "retail HUD_DamageIndicator is 0x414 bytes");

namespace
{
    // DAT_00674a10 -- the live indicator (ctor 0x004d63c9, dtor 0x004d6507).
    HUD_DamageIndicator* G_HudDamageIndicator = nullptr;

    // DAT_00674a0c -- HUD_DamageIndicator.cpp's 3DMathEngine, FUN_005b73e0 on first use (every
    // use site repeats the test: 0x004d6d5d, 0x004d6d99, 0x004d6f68, 0x004d6fc4, 0x004d710a).
    I3DMathEngine* G_DamageIndicatorMath = nullptr;

    I3DMathEngine* DamageIndicatorMath()
    {
        if (G_DamageIndicatorMath == nullptr)
        {
            G_DamageIndicatorMath = Acquire3DMathEngine();
        }
        return G_DamageIndicatorMath;
    }

    char const kDamageIndicatorCpp[] =
        "E:\\FL\\Scratch\\Source\\Client\\Space\\HUD_DamageIndicator.cpp"; // .rdata 0x005d8198
    char const kDamageBottomMesh[] = "interface\\hud\\HUD_damagebottom.3db"; // .rdata 0x005d8130

    float const kFlashSeconds = 0.7f;   // DAT_005d800c -- the hit amount and Draw's divisor
    float const kFlashAhead = 0.35f;    // DAT_005d8008 -- FlashTowards' straight-ahead bonus
    float const kFlashMax = 1.0f;       // DAT_005d8004
    float const kAlphaMax = 0.8f;       // DAT_005d8010
    double const kQuadrantTurn = 0.39269909262657166; // DAT_005d8158, pi/8
    double const kInv255 = 0.00392156862745098;      // DAT_005c89c8
    double const kByteScale = 255.0;                 // DAT_005c89c0

    // FUN_004104c0 (cdecl) -- clamp to 0..255.
    int ClampByte(int Value)
    {
        if (Value < 0)
        {
            return 0;
        }
        if (Value > 0xff)
        {
            return 0xff;
        }
        return Value;
    }

    // FLD K / FADD Flash / FST Flash / FCOM 1.0 / TEST AH,5 / JNP: the unrounded sum is kept
    // when it is below 1.0, anything else (NaN included) stores 1.0.
    void AddFlash(float& Flash, float Amount)
    {
        double const Sum = static_cast<double>(Amount) + static_cast<double>(Flash);
        Flash = Sum < static_cast<double>(kFlashMax) ? static_cast<float>(Sum) : kFlashMax;
    }

    // The fade step of FUN_004d6af0 / FUN_004d6be0: above zero it loses the frame time,
    // otherwise (NaN included, TEST AH,0x41) it is set to 0.
    void FadeFlash(float& Flash, double FrameTime)
    {
        if (Flash > 0.0f)
        {
            Flash = static_cast<float>(static_cast<double>(Flash) - FrameTime);
        }
        else
        {
            Flash = 0.0f;
        }
    }

    // The rotation of kQuadrantTurn about Z both FlashFromPosition (0x004d6d81..0x004d6deb)
    // and FlashTowards (0x004d7104..0x004d7155) build on the stack.
    FMatrix QuadrantMatrix()
    {
        float const Cos = static_cast<float>(cos(kQuadrantTurn));
        float const Sin = static_cast<float>(sin(kQuadrantTurn));
        FMatrix M;
        M.D[0][0] = Cos;
        M.D[0][1] = -Sin;
        M.D[0][2] = 0.0f;
        M.D[1][0] = Sin;
        M.D[1][1] = Cos;
        M.D[1][2] = 0.0f;
        M.D[2][0] = 0.0f;
        M.D[2][1] = 0.0f;
        M.D[2][2] = 1.0f;
        return M;
    }

    // Draw's alpha for one arrow: Flash / 0.7 when above zero, capped at 0.8. False when the
    // arrow is not drawn (TEST AH,0x41 / JNZ: zero, negative or NaN).
    bool FlashAlpha(float Flash, float* OutAlpha)
    {
        double const Ratio = static_cast<double>(Flash) / static_cast<double>(kFlashSeconds);
        if (!(Ratio > 0.0))
        {
            return false;
        }
        // TEST AH,5 / JP: 0.8 or more takes the cap.
        *OutAlpha = Ratio < static_cast<double>(kAlphaMax) ? static_cast<float>(Ratio) : kAlphaMax;
        return true;
    }

    // The tint and draw tail shared by all four arrows: FUN_004a1af0 with the byte quad, then
    // IRenderer2 slot 7 (&DAT_0067dbf8, instance, 1.0f, 0, 0, 0).
    void DrawPart(FHudDamageIndicatorPart& Part, unsigned char const* Colour)
    {
        ClientMeshHolderSetColor(&Part.Holder, Colour);
        DALib::Renderer->RenderInstance(Win3DGetCamera(), Part.Holder.EngineInstance, 1.0f, 0, nullptr,
                               nullptr);
    }

    // The colour the first two arrows build inline (0x004d666c..0x004d6727 and
    // 0x004d6794..0x004d684f): from DAT_00679ba8's bytes 0, 2, 1 and the alpha. Bytes 1 and 2
    // pass through a float (FSTP float) before the x255; byte 0 stays on the FPU stack, so its
    // /255*255 round trip is taken at the FPU's precision (under 53-bit precision it lands one
    // below for 24 byte values -- the same truncation the double arithmetic here gives).
    void InlineColour(float Alpha, unsigned char* OutColour)
    {
        unsigned long const Palette = G_NnPaletteArgb_679ba8;
        int const Byte0 = static_cast<int>(Palette & 0xffu);
        int const Byte1 = static_cast<int>((Palette >> 8) & 0xffu);
        int const Byte2 = static_cast<int>((Palette >> 16) & 0xffu);
        float const Byte1F = static_cast<float>(static_cast<double>(Byte1) * kInv255);
        float const Byte2F = static_cast<float>(static_cast<double>(Byte2) * kInv255);
        OutColour[0] = 0;
        OutColour[1] = 0;
        OutColour[2] = 0;
        OutColour[3] = 0;
        OutColour[0] = static_cast<unsigned char>(
            ClampByte(static_cast<int>(static_cast<double>(Byte0) * kInv255 * kByteScale)));
        OutColour[1] = static_cast<unsigned char>(
            ClampByte(static_cast<int>(static_cast<double>(Byte2F) * kByteScale)));
        OutColour[2] = static_cast<unsigned char>(
            ClampByte(static_cast<int>(static_cast<double>(Byte1F) * kByteScale)));
        OutColour[3] = static_cast<unsigned char>(
            ClampByte(static_cast<int>(static_cast<double>(Alpha) * kByteScale)));
    }

    // FUN_004d6a40 (thiscall on a byte quad, RET 0x10) -- each float x255.0, ftol, clamped to
    // 0..255 (inline, not through FUN_004104c0), into bytes 0..3.
    void ColourFromFloats(unsigned char* OutColour, float R, float G, float B, float A)
    {
        OutColour[0] = static_cast<unsigned char>(
            ClampByte(static_cast<int>(static_cast<double>(R) * kByteScale)));
        OutColour[1] = static_cast<unsigned char>(
            ClampByte(static_cast<int>(static_cast<double>(G) * kByteScale)));
        OutColour[2] = static_cast<unsigned char>(
            ClampByte(static_cast<int>(static_cast<double>(B) * kByteScale)));
        OutColour[3] = static_cast<unsigned char>(
            ClampByte(static_cast<int>(static_cast<double>(A) * kByteScale)));
    }

    // The colour the last two arrows build through FUN_004d6a40 (0x004d68bc..0x004d6926 and
    // 0x004d6997..0x004d69f9): the same bytes 0, 2, 1, each rounded to a float first.
    void CalledColour(float Alpha, unsigned char* OutColour)
    {
        unsigned long const Palette = G_NnPaletteArgb_679ba8;
        int const Byte0 = static_cast<int>(Palette & 0xffu);
        int const Byte1 = static_cast<int>((Palette >> 8) & 0xffu);
        int const Byte2 = static_cast<int>((Palette >> 16) & 0xffu);
        ColourFromFloats(OutColour, static_cast<float>(static_cast<double>(Byte0) * kInv255),
                         static_cast<float>(static_cast<double>(Byte2) * kInv255),
                         static_cast<float>(static_cast<double>(Byte1) * kInv255), Alpha);
    }
}

HUD_DamageIndicator* GetHudDamageIndicator()
{
    return G_HudDamageIndicator;
}

// ============================================================================================
// FHudDamageIndicatorPart
// ============================================================================================

FHudDamageIndicatorPart::FHudDamageIndicatorPart(char const* Path, float Degrees)
    : Flash(0.0f)
{
    // FUN_004d72a0.
    ClientMeshHolderConstruct(&Holder);
    Holder.UserFloats[3] = 1.0f;          // +0x20
    Holder.UserFloats[2] = 1.0f;          // +0x1c
    Holder.UserFloats[1] = 1.0f;          // +0x18
    Holder.UserFloats[0] = 1.0f;          // +0x14
    Holder.UserFloats[4] = 1.0f;          // +0x24
    Holder.AnimOrArchIndex = -1;          // +0x10
    ClientMeshLoadFromPath(&Holder, Path, 0, true); // FUN_0059ec80(Path, 0, 1)
    if (!ClientMeshHolderHasInstance(&Holder))
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: HUD_DamageIndicator is missing: %s.",
              kDamageIndicatorCpp, 0xf7, Path);
    }

    // 0x004d732b..0x004d73b1: the angle is FST'd as a float before FSIN reloads it; FCOS
    // takes the unrounded product.
    double const Angle = static_cast<double>(Degrees) * static_cast<double>(0.017453292f);
    float const AngleF = static_cast<float>(Angle);
    float const Cos = static_cast<float>(cos(Angle));
    float const Sin = static_cast<float>(sin(static_cast<double>(AngleF)));
    FTransform Xform;
    Xform.Rotation.D[0][0] = Cos;
    Xform.Rotation.D[0][1] = -Sin;
    Xform.Rotation.D[0][2] = 0.0f;
    Xform.Rotation.D[1][0] = Sin;
    Xform.Rotation.D[1][1] = Cos;
    Xform.Rotation.D[1][2] = 0.0f;
    Xform.Rotation.D[2][0] = 0.0f;
    Xform.Rotation.D[2][1] = 0.0f;
    Xform.Rotation.D[2][2] = 1.0f;
    Xform.Translation.X = 0.0f;
    Xform.Translation.Y = 0.0f;
    Xform.Translation.Z = -1.0f;
    // The instance is used as it is, -1 included, as in retail.
    DALib::Engine->SetInstanceMatrix(Holder.EngineInstance, &Xform);   // +0x9c
    DALib::Engine->UpdateInstance(Holder.EngineInstance, 0, nullptr);  // +0x6c
}

FHudDamageIndicatorPart::~FHudDamageIndicatorPart()
{
    // FUN_004d7400: FUN_0059eee0 with an instance bound, then the holder's destructor
    // FUN_0059ef20 (~FClientMeshHolder: the same release again, which finds nothing).
    if (ClientMeshHolderHasInstance(&Holder))
    {
        ClientUiMeshHolderRelease(&Holder);
    }
}

// ============================================================================================
// HUD_DamageIndicator
// ============================================================================================

HUD_DamageIndicator::HUD_DamageIndicator(INI_Reader& /*Reader*/)
    : PartDeg180(kDamageBottomMesh, 180.0f)     // 0x004d634a, 0x43340000
    , PartDeg0(kDamageBottomMesh, 0.0f)         // 0x004d6360
    , PartDegMinus90(kDamageBottomMesh, -90.0f) // 0x004d637a, 0xc2b40000
    , PartDeg90(kDamageBottomMesh, 90.0f)       // 0x004d6394, 0x42b40000
{
    // FUN_004d62d0: HUDItem inlined, WinControl FUN_0059f860, the four arrows, the vptrs,
    // DAT_00674a10 = this, HUDItem's name, then Create's body inlined (SetOffset slot 31,
    // SetPosition slot 5, Win3D::Create FUN_004b5060(0, 0) called directly), the Win3D name and
    // the class name. Flags are not touched.
    G_HudDamageIndicator = this;
    HUDItem::Name = "DamageIndicator";
    SetOffset(0.0f, 0.0f, -1.0f);
    SetPosition(Position);
    Win3D::Create(nullptr, nullptr);
    Win3D::SetName("HUD_DamageIndicator");
    ClassName = "HUD_DamageIndicator";
}

HUD_DamageIndicator::~HUD_DamageIndicator()
{
    // FUN_004d64c0: DAT_00674a10 = 0, then the arrows in reverse (+0x3e8, +0x3bc, +0x390,
    // +0x364), ~WinControl, HUDItem's watcher -- the member and base destructors.
    G_HudDamageIndicator = nullptr;
}

bool HUD_DamageIndicator::Create(Win3D* NewParent, void* Data)
{
    // LAB_004f3d70 (this = the WinControl face).
    SetOffset(0.0f, 0.0f, -1.0f);   // slot 31 (+0x7c)
    SetPosition(Position);          // slot 5 (+0x14)
    return Win3D::Create(NewParent, Data);
}

void HUD_DamageIndicator::Update()
{
    // FUN_004d6af0 (this = the WinControl face).
    if (GetClientMouseCursorDrawFlag() != 0 && GetPlayerObjRW() != nullptr)
    {
        SetHiddenWithChildren(false);             // FUN_005a0c30(0)
        Flags = static_cast<unsigned char>(Flags | kWin3DActive);
    }
    else
    {
        SetHiddenWithChildren(true);              // FUN_005a0c30(1)
    }
    double const FrameTime = GetClientFrameTimeCapped(); // FUN_0042d680
    FadeFlash(PartDeg0.Flash, FrameTime);         // +0x3b8
    FadeFlash(PartDeg180.Flash, FrameTime);       // +0x38c
    FadeFlash(PartDegMinus90.Flash, FrameTime);   // +0x3e4
    FadeFlash(PartDeg90.Flash, FrameTime);        // +0x410
    Win3D::Update();                              // tail jump FUN_005a17b0
}

void HUD_DamageIndicator::UpdateInactive()
{
    // FUN_004d6be0 (this = the WinControl face).
    if (GetClientMouseCursorDrawFlag() != 0 && GetPlayerObjRW() != nullptr)
    {
        SetHiddenWithChildren(false);             // FUN_005a0c30(0)
    }
    double const FrameTime = GetClientFrameTimeCapped(); // FUN_0042d680
    FadeFlash(PartDeg0.Flash, FrameTime);
    FadeFlash(PartDeg180.Flash, FrameTime);
    FadeFlash(PartDegMinus90.Flash, FrameTime);
    FadeFlash(PartDeg90.Flash, FrameTime);
    // 0x004d6ca9..0x004d6cc4: LAB_004682f0 inlined.
    Win3D::UpdateInactive();
}

void HUD_DamageIndicator::Draw()
{
    // LAB_004d65f0 (this = the WinControl face).
    if (GetGameOption(kGameOptInterfaceGroup) == 0)     // FUN_005792b0(6)
    {
        return;
    }
    if (GetGameOption(kGameOptHud) == 0)                // FUN_005792b0(0xa)
    {
        return;
    }
    if (GetPlayerObjRW() == nullptr)                    // FUN_0054baf0
    {
        return;
    }

    unsigned char Colour[4];
    float Alpha = 0.0f;
    if (FlashAlpha(PartDeg0.Flash, &Alpha))             // +0x3b8, 0x004d6629
    {
        InlineColour(Alpha, Colour);
        DrawPart(PartDeg0, Colour);
    }
    if (FlashAlpha(PartDeg180.Flash, &Alpha))           // +0x38c, 0x004d6753
    {
        InlineColour(Alpha, Colour);
        DrawPart(PartDeg180, Colour);
    }
    if (FlashAlpha(PartDegMinus90.Flash, &Alpha))       // +0x3e4, 0x004d687b
    {
        CalledColour(Alpha, Colour);
        DrawPart(PartDegMinus90, Colour);
    }
    if (FlashAlpha(PartDeg90.Flash, &Alpha))            // +0x410, 0x004d6955
    {
        // 0x004d6997: FUN_004b5140 zeroes the quad before FUN_004d6a40 fills it.
        memset(Colour, 0, sizeof(Colour));
        CalledColour(Alpha, Colour);
        DrawPart(PartDeg90, Colour);
    }
}

void HUD_DamageIndicator::FlashFromPosition(Vector const* WorldPos)
{
    // FUN_004d6d40.
    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW()); // FUN_0054baf0
    if (Player == nullptr)
    {
        return;
    }
    // Slot 4 (+0x10); its body is the one of slot 3 (0x5F1F0 in Common, ICF), so the pointer
    // is the whole transform the math engine reads.
    FTransform const& ShipXform = reinterpret_cast<FTransform const&>(Player->get_transform());
    FVector Local;
    DamageIndicatorMath()->InverseTransform(Local, ShipXform,
                                            reinterpret_cast<FVector const&>(*WorldPos));
    FVector const Source = Local;
    FMatrix const Turn = QuadrantMatrix();
    FVector Turned;
    DamageIndicatorMath()->TransposeTransform(Turned, Turn, Source);

    // 0x004d6e10..0x004d6ee8: TEST AH,0x41 on y, then on x (<= 0 and NaN go the "no" way).
    if (Turned.Y > 0.0f)
    {
        if (Turned.X > 0.0f)
        {
            AddFlash(PartDeg180.Flash, kFlashSeconds);     // +0x38c
        }
        else
        {
            AddFlash(PartDegMinus90.Flash, kFlashSeconds); // +0x3e4
        }
    }
    else
    {
        if (Turned.X > 0.0f)
        {
            AddFlash(PartDeg90.Flash, kFlashSeconds);      // +0x410
        }
        else
        {
            AddFlash(PartDeg0.Flash, kFlashSeconds);       // +0x3b8
        }
    }
}

void HUD_DamageIndicator::FlashAlongRay(Vector const* Origin, Vector const* Direction)
{
    // FUN_004d6cd0: each Direction component x -1000.0f (DAT_005d8154), plus Origin's. The z
    // product is parked in a float local (0x004d6d12) before its add; x and y stay on the FPU.
    float const K = -1000.0f;
    float const ZScaled = static_cast<float>(static_cast<double>(Direction->z) * K);
    Vector Point;
    Point.x = static_cast<float>(static_cast<double>(Direction->x) * K + Origin->x);
    Point.y = static_cast<float>(static_cast<double>(Direction->y) * K + Origin->y);
    Point.z = static_cast<float>(static_cast<double>(ZScaled) + Origin->z);
    FlashTowards(&Point);
}

void HUD_DamageIndicator::FlashTowardsPoint(Vector const* Point, int /*Unused*/)
{
    // FUN_004d6f00.
    FlashTowards(Point);
}

void HUD_DamageIndicator::FlashTowards(Vector const* Point)
{
    // FUN_004d6f10.
    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW()); // FUN_0054baf0
    if (Player == nullptr)
    {
        return;
    }
    float Radius = 0.0f;
    Vector Center;
    Player->get_physical_radius(Radius, Center);        // slot 11 (+0x2c)
    FTransform const& ShipXform = reinterpret_cast<FTransform const&>(Player->get_transform());

    // 0x004d6f42..0x004d6f89: the ship's rotation at its physical centre; D = (0, 0, -Radius).
    FTransform Frame;
    Frame.Rotation = ShipXform.Rotation;
    Frame.Translation.X = Center.x;
    Frame.Translation.Y = Center.y;
    Frame.Translation.Z = Center.z;
    FVector Back;
    Back.X = 0.0f;
    Back.Y = 0.0f;
    Back.Z = -Radius;
    FVector Rotated;
    DamageIndicatorMath()->Rotate(Rotated, Frame, Back); // +0x30
    // FUN_004d7270 (Vector::operator+=): this[i] = other[i] + this[i].
    Frame.Translation.X = Rotated.X + Frame.Translation.X;
    Frame.Translation.Y = Rotated.Y + Frame.Translation.Y;
    Frame.Translation.Z = Rotated.Z + Frame.Translation.Z;

    FVector Local;
    DamageIndicatorMath()->InverseTransform(Local, Frame,
                                            reinterpret_cast<FVector const&>(*Point)); // +0x3c
    FVector const Source = Local;

    // 0x004d6fef..0x004d7100: a hit from straight ahead -- |z| above |x| and above |y| (NaN
    // fails both) and z not below zero -- adds 0.35 to all four arrows; z below zero leaves
    // without touching anything.
    float const AbsX = fabsf(Local.X);
    float const AbsY = fabsf(Local.Y);
    float const AbsZ = fabsf(Local.Z);
    if (AbsZ > AbsX && AbsZ > AbsY)
    {
        if (Local.Z < 0.0f)
        {
            return;
        }
        AddFlash(PartDeg180.Flash, kFlashAhead);      // +0x38c
        AddFlash(PartDeg0.Flash, kFlashAhead);        // +0x3b8
        AddFlash(PartDegMinus90.Flash, kFlashAhead);  // +0x3e4
        AddFlash(PartDeg90.Flash, kFlashAhead);       // +0x410
    }

    // 0x004d7104..0x004d725e: the same quadrant pick as FlashFromPosition.
    FMatrix const Turn = QuadrantMatrix();
    FVector Turned;
    DamageIndicatorMath()->TransposeTransform(Turned, Turn, Source); // +0x38
    if (Turned.Y > 0.0f)
    {
        if (Turned.X > 0.0f)
        {
            AddFlash(PartDeg180.Flash, kFlashSeconds);
        }
        else
        {
            AddFlash(PartDegMinus90.Flash, kFlashSeconds);
        }
    }
    else
    {
        if (Turned.X > 0.0f)
        {
            AddFlash(PartDeg90.Flash, kFlashSeconds);
        }
        else
        {
            AddFlash(PartDeg0.Flash, kFlashSeconds);
        }
    }
}

namespace
{
    // The static HUDFACTORY<HUD_DamageIndicator> DAT_006749d4: XC entry LAB_004d62a0 runs the
    // ctor FUN_004d7450 ("DamageIndicator") and registers LAB_004d62c0 with atexit (->
    // FUN_004cf010). Its Create FUN_004d74c0 is operator new(0x414) + FUN_004d62d0.
    HUDFACTORY<HUD_DamageIndicator> G_HudDamageIndicatorFactory("DamageIndicator");
}
