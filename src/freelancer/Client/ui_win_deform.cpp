// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "ui_win_deform.h"

#include "globals.h"          // GetClientFrameTimeCapped (FUN_0042d680)
#include "hud_element_list.h" // HudMatrixRotationY / RotateRows12 / RotateRows01 (FUN_004c8b40..)
#include "space_scene_obj_draw.h" // SpaceCameraSetNearFar (FUN_0040fb60)
#include "ui_page.h"          // UiWidgetMathEngine (DAT_0067dbc0)
#include "ui_page_desc.h"     // FUiPayloadBounds
#include "win3d_immediate.h"  // Win3DSetViewport (FUN_00422610)
#include "win3d_render.h"     // Win3DApplyCamera (FUN_00422950)
#include "win_factory.h"

#include "DALib.h"
#include "common_reputation.h" // GetCostumeDescriptions, CostumeDescriptions, Costume
#include "dacom.h"             // FDUMP
#include "engine/engbase.h"    // IDeformable
#include "x86math.h"

#include <stdio.h>
#include <string.h>

namespace
{
// .rdata 0x005e5874 -- the FDUMP / Win3DSetViewport source name of every UIWin.cpp call here.
char const kUiWinCpp[] = "E:\\FL\\Scratch\\Source\\Client\\Interface\\UIWin.cpp";
} // namespace

UiWinDeform::UiWinDeform()
    : Width(0.0f)
    , Height(0.0f)
    , PoseZ(-5.0f)
    , PoseHeight(-1.0f)
{
    // FUN_0059aca0. Win3D's ctor is inlined (0x0059acbf..0x0059ad04); the string at +0x7c
    // (0x0059ad0e..0x0059ad18), the body (0x0059ad1e..0x0059ad9c: FEngineCharacterBody's ctor
    // inlined) and the camera base ctor FUN_0040f150 (0x0059adb3) follow in member order.
    // 0x0059adca..0x0059ade0: +0x3e8 / +0x3e4 = 0, +0x3ec = -5.0f (0xc0a00000),
    // +0x3f0 = -1.0f (0xbf800000) -- the initializer list above.
    SpaceCameraConstruct(&Camera);

    // 0x0059adb8..0x0059ae18: the preview camera takes the Win3D scene camera's (DAT_0067dbf8)
    // viewport (+0xac = DAT_0067dca4, FUN_0040fd60), near / far (+0xa4 DAT_0067dc9c, +0xa8
    // DAT_0067dca0, FUN_0040fb60) and half horizontal fov (+0x98 DAT_0067dc90, FUN_0040f8c0).
    FSpaceCamera* const Scene = Win3DGetCamera();
    SpaceCameraSetViewport(&Camera, Scene->Viewport);
    SpaceCameraSetNearFar(&Camera, Scene->ZNear, Scene->ZFar);
    SpaceCameraSetHalfFovX(&Camera, Scene->HalfFovXDeg);
}

UiWinDeform::~UiWinDeform()
{
    // FUN_0059ae60. 0x0059ae88..0x0059aed1: with a deformable, FUN_004ca4b0's body inlined
    // (StopAnimations, the four holders released, IDeformable slot 6, +0x8c = 0).
    if (Body.Deformable != nullptr)
        Body.Reset();
    // 0x0059aed7..0x0059aee8: the camera's base dtor (vftable 0x005c8944, an empty body);
    // 0x0059aeed..0x0059af88: FEngineCharacterBody's dtor (FUN_004ca530 inlined);
    // 0x0059af8d..0x0059afbf: the MSVCP60 string at +0x7c released; 0x0059afc5..0x0059b004:
    // Win3D's dtor inlined -- all of them the members' and the base's own destructors.
}

void UiWinDeform::SetPosition(float const* NewPosition)
{
    // LAB_0059bb40 (RET 4). 0x0059bb46..0x0059bb5a: three dwords to +0x60 (Win3D's slot 5).
    Position[0] = NewPosition[0];
    Position[1] = NewPosition[1];
    Position[2] = NewPosition[2];

    // 0x0059bb5d..0x0059bb65: only with a deformable.
    if (Body.Deformable == nullptr)
        return;
    PoseBody();
}

void UiWinDeform::SetTransform(float const* Transform)
{
    // LAB_0059b8f0 (RET 4).
    // 0x0059b90f..0x0059b91d: REP MOVSD of the 12 incoming floats into a local transform.
    float Local[12];
    memcpy(Local, Transform, sizeof(Local));

    // 0x0059b8f9..0x0059b94e: the widget's top-right corner offset {Width * 0.5f (DAT_005c75e0),
    // Height * -0.5f (DAT_005cef60), 0}.
    FVector HalfExtent;
    HalfExtent.X = Width * 0.5f;
    HalfExtent.Y = Height * -0.5f;
    HalfExtent.Z = 0.0f;

    // 0x0059b962..0x0059b970: I3DMathEngine slot 11 TransformMatrix(Out, the incoming 3x3,
    // HalfExtent) through the shared cache DAT_0067dbc0.
    FVector Rotated;
    UiWidgetMathEngine()->TransformMatrix(Rotated,
                                          *reinterpret_cast<FMatrix const*>(Transform),
                                          HalfExtent);

    // 0x0059b973..0x0059b9ba: the local translation = Rotated + the incoming translation.
    Local[9] = Rotated.X + Transform[9];
    Local[10] = Rotated.Y + Transform[10];
    Local[11] = Rotated.Z + Transform[11];

    // 0x0059b9ab..0x0059b9d0: slot 7 SetOrientation(Local), slot 5 SetPosition(Local + 9) --
    // the virtual calls, so this class's SetPosition poses the body.
    SetOrientation(Local);
    SetPosition(Local + 9);

    // 0x0059b9d3..0x0059ba5d: the top-left corner {Width * -0.5f, Height * 0.5f, 0} through
    // I3DMathEngine slot 10 TransformXform(Out, this + 0x3c (orientation + position), corner).
    FTransform const& Xform = *reinterpret_cast<FTransform const*>(Orientation);
    FVector Corner;
    Corner.X = Width * -0.5f;
    Corner.Y = Height * 0.5f;
    Corner.Z = 0.0f;
    FVector TopLeft;
    UiWidgetMathEngine()->TransformXform(TopLeft, Xform, Corner);

    // 0x0059ba22..0x0059ba96: the bottom-right corner {Width * 0.5f, Height * -0.5f, 0}.
    Corner.X = Width * 0.5f;
    Corner.Y = Height * -0.5f;
    Corner.Z = 0.0f;
    FVector BottomRight;
    UiWidgetMathEngine()->TransformXform(BottomRight, Xform, Corner);

    // 0x0059ba9a..0x0059bade: both corners projected by the Win3D scene camera (DAT_0067dbf8,
    // ICamera slot 13); the results are not tested.
    ICamera* const Scene = AsCamera(Win3DGetCamera());
    float X1;
    float Y1;
    float Z1;
    Scene->Project(&X1, &Y1, &Z1, &TopLeft.X);
    float X2;
    float Y2;
    float Z2;
    Scene->Project(&X2, &Y2, &Z2, &BottomRight.X);

    // 0x0059bae1..0x0059bb22: ftol (truncation) of each coordinate into {left, top, right,
    // bottom}, then FUN_0040fb80(&rect, 1) on the preview camera.
    int Rect[4];
    Rect[0] = static_cast<int>(X1);
    Rect[1] = static_cast<int>(Y1);
    Rect[2] = static_cast<int>(X2);
    Rect[3] = static_cast<int>(Y2);
    SpaceCameraSetViewportEx(&Camera, Rect, 1);
}

bool UiWinDeform::Create(Win3D* NewParent, void* Payload)
{
    // LAB_0059b8a0 (RET 8). 0x0059b8a0..0x0059b8c6: Win3D's slot 9 body inlined (reparent,
    // register, flags = (flags & 0xfb) | 3).
    Win3D::Create(NewParent, Payload);

    // 0x0059b8c4..0x0059b8d6: a payload carries {width, height} into +0x3e4 / +0x3e8.
    if (Payload != nullptr)
    {
        FUiPayloadBounds const* const Bounds = static_cast<FUiPayloadBounds const*>(Payload);
        Width = Bounds->Right;
        Height = Bounds->Bottom;
    }
    // 0x0059b8dc: MOV AL,1.
    return true;
}

void UiWinDeform::Update()
{
    // LAB_0059bc30. 0x0059bc37..0x0059bc5a: Win3D's slot 11 body inlined (per child: bit0 ?
    // slot 11 : slot 12).
    Win3D::Update();

    // 0x0059bc5c..0x0059bc62: nothing more without a deformable.
    if (Body.Deformable == nullptr)
        return;

    // 0x0059bc68..0x0059bc7c: the frame time (FUN_0042d680) stored as a float (FST);
    // FCOMP double 0.0 (DAT_005c8ba8), TEST AH,0x41 / JNZ -> out when <= 0 or unordered, the
    // fall-through is > 0.
    double const FrameTime = GetClientFrameTimeCapped();
    float Remaining = static_cast<float>(FrameTime);
    if (!(FrameTime > 0.0))
        return;

    for (;;)
    {
        // 0x0059bc86..0x0059bca1: FCOMP 0.2f (DAT_005d043c), TEST AH,5 / JP -> the step is
        // 0.2f (0x3e4ccccd) when Remaining >= 0.2f or unordered; the fall-through (below) takes
        // Remaining itself.
        float const Step = (Remaining < 0.2f) ? Remaining : 0.2f;

        // 0x0059bca9..0x0059bd71: FUN_004cba60's body inlined, with its own deformable test
        // (0x0059bcaf) -- MixerWalk / UpdateInstance with Step * AnimRate, the playback slots
        // emptied when IDeformable slot 26 reports none running.
        Body.Advance(Step);

        // 0x0059bd73..0x0059bd8a: Remaining -= Step (FST to the float local), FCOMP double 0.0,
        // TEST AH,0x41 / JZ -> loop while the (unrounded) difference is > 0.
        double const Left = static_cast<double>(Remaining) - static_cast<double>(Step);
        Remaining = static_cast<float>(Left);
        if (!(Left > 0.0))
            break;
    }
}

void UiWinDeform::Draw()
{
    // LAB_0059bda0. 0x0059bda0..0x0059be7a: a debug outline gated on DAT_0067dbc4 (FUN_00422690,
    // FUN_00421ed0 x4, FUN_00421a70, FUN_00421670(7), four FUN_00421cc0, FUN_00421690). The
    // byte has four readers (0x00599ce0, 0x0059a310, 0x0059ab6c, here) and no writer in the PE,
    // so the branch never runs; it is left out, as in ui_static_text / ui_list_box.

    // 0x0059be80..0x0059be88: the body only with a deformable.
    if (Body.Deformable != nullptr)
    {
        // 0x0059be8e..0x0059bea3: four zero LOD ids.
        int Zeros[4] = {0, 0, 0, 0};
        // [ESP+0x14]: Draw's out-parameter; retail does not initialize it.
        int OutLod = 0;

        // 0x0059bea2..0x0059beb7: the preview camera applied (FUN_00422950) and its viewport set
        // (FUN_00422610, UIWin.cpp line 0x29f).
        Win3DApplyCamera(AsCamera(&Camera));
        Win3DSetViewport(AsCamera(&Camera), kUiWinCpp, 0x29f);

        // 0x0059bebc..0x0059bee3: IRenderPipeline slot 60 (+0xf0) SetRenderState(0x16, 2) and
        // SetRenderState(0x17, 4).
        DALib::Pipeline->SetRenderState(D3DRS_CULLMODE, 2);
        DALib::Pipeline->SetRenderState(D3DRS_ZFUNC, 4);

        // 0x0059bee9..0x0059bf25: IDeformable slot 15 SetLod(inst, 0.0f, Zeros), slot 16
        // Draw(inst, &Camera, Zeros, 0, &OutLod, 0).
        DALib::Deform->SetLod(Body.Deformable, 0.0f, Zeros);
        DALib::Deform->Draw(Body.Deformable, &Camera, Zeros, nullptr, &OutLod, 0);

        // 0x0059bf28..0x0059bf41: the scene camera back (FUN_00422950, FUN_00422610 line 0x2ad).
        Win3DApplyCamera(AsCamera(Win3DGetCamera()));
        Win3DSetViewport(AsCamera(Win3DGetCamera()), kUiWinCpp, 0x2ad);
    }

    // 0x0059bf49..0x0059bf4b: FUN_0059e5c0 -- Win3D's slot 13 (the sorted children).
    Win3D::Draw();
}

void UiWinDeform::SetCostume(char const* Name)
{
    // FUN_0059b020 (thiscall, RET 4).
    // 0x0059b02e..0x0059b076: with a deformable, FUN_004ca4b0's body inlined.
    if (Body.Deformable != nullptr)
        Body.Reset();

    // 0x0059b07c..0x0059b0a8: the costume table, the named costume and a nonzero id, else out.
    CostumeDescriptions const* const Descs = GetCostumeDescriptions();
    if (Descs == nullptr)
        return;
    costume const* const Found = Descs->find_costume(Name);
    if (Found == nullptr || Found->Id == 0)
        return;

    // 0x0059b0ae..0x0059b0ca: a local Costume with the four part ids and the accessory count
    // zeroed, filled by CostumeDescriptions::get_costume(int id, Costume&).
    Costume Local;
    Local.PartIds[0] = 0;
    Local.PartIds[1] = 0;
    Local.PartIds[2] = 0;
    Local.PartIds[3] = 0;
    Local.AccessoryCount = 0;
    Descs->get_costume(static_cast<int>(Found->Id), Local);

    // 0x0059b0d0..0x0059b122: FUN_0059b220; on failure FUN_00402180 (_vsnprintf into the
    // 0x2000-byte stack buffer) and FDUMP(0x100003) with UIWin.cpp line 0x210.
    if (!Body.Build(Local))
    {
        char Message[0x2000];
        _snprintf(Message, sizeof(Message), "Failed to create costume \"%s\".\n", Name);
        Message[sizeof(Message) - 1] = '\0';
        FDUMP(0x100003, "%s(%d) : NOTICE:General:%s", kUiWinCpp, 0x210, Message);
        return;
    }

    // 0x0059b125..0x0059b127: only with a deformable (Build's result already says so).
    if (Body.Deformable == nullptr)
        return;
    PoseBody();
}

void UiWinDeform::PoseBody()
{
    // The pose SetCostume (0x0059b12d..0x0059b20e) and SetPosition (0x0059bb6b..0x0059bc1c)
    // spell out inline: the 3x3 identity (FUN_004215b0 / the explicit 1s and 0s), then
    // FUN_004c8b40(pi = 0x40490fdb), FUN_004c8b80(0), FUN_004c8bc0(0).
    float Rotation[9] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    HudMatrixRotationY(Rotation, 3.14159274f);
    HudMatrixRotateRows12(Rotation, 0.0f);
    HudMatrixRotateRows01(Rotation, 0.0f);

    // 0x0059b1af..0x0059b1ca / 0x0059bbb0..0x0059bbd0: IDeformable slot 24 with +0x3f0, only
    // with a deformable.
    if (Body.Deformable != nullptr)
        DALib::Deform->SetHeight(Body.Deformable, PoseHeight);

    // 0x0059b1cd..0x0059b1f9 / 0x0059bbd3..0x0059bc03: slot 19 with {0, 0, +0x3ec}.
    float Pose[3] = {0.0f, 0.0f, PoseZ};
    DALib::Deform->SetPosePos(Body.Deformable, Pose);

    // 0x0059b1fc..0x0059b20e / 0x0059bc06..0x0059bc1c: slot 20 with the rotation.
    DALib::Deform->SetPoseOri(Body.Deformable, Rotation);
}

// Retail DAT_0067da94 -- WINFACTORY<UIWinDeform> (static init LAB_0059c2d0 -> FUN_0059ca50
// ("UIWinDeform"), atexit LAB_0059c2f0 -> FUN_00579380; Create FUN_0059cac0: operator_new(0x3f4),
// FUN_0059aca0, +0x10 = the factory name, slot 9, the scalar deleting dtor on failure).
WINFACTORY<UiWinDeform> G_UIWinDeformFactory("UIWinDeform");
