// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "hud_button.h"

#include "cockpit_load.h"
#include "common_csys.h"
#include "common_ini.h"
#include "common_misc.h"
#include "common_text.h"
#include "game_options.h"
#include "globals.h"
#include "hud_command_dispatch.h"
#include "input_manager.h"
#include "space_camera.h"
#include "win3d_engine_submit.h"
#include "win3d_immediate.h"

#include "DALib.h"
#include "engine/engbase.h"

#include <string.h>

namespace
{
    // FUN_00421050 + FUN_00421070 on the holder: IEngine::SetInstanceMatrix(+0x04, Transform)
    // then UpdateInstance(+0x04, 0, 0).
    void HolderSetMatrixAndUpdate(FClientMeshHolder* Holder, float* Transform12)
    {
        DALib::Engine->SetInstanceMatrix(Holder->EngineInstance, Transform12);
        ClientMeshHolderUpdate(Holder);
    }
}

// ============================================================================================
// WinButton
// ============================================================================================

WinButton::WinButton()
{
    // 0x004c8d84: FUN_004b5150 on +0xac (identity rotation, zero translation), the vptr, then
    // the fields.
    memset(&RestPose, 0, sizeof(RestPose));
    RestPose.Rotation.D[2][2] = 1.0f;
    RestPose.Rotation.D[1][1] = 1.0f;
    RestPose.Rotation.D[0][0] = 1.0f;
    bHover = 0;
    SpinAngle = 0.0f;
    SpinSpeed = 180.0f;
    FieldE8 = 0.85f;
    FieldEC = 0.75f;
    ByteF0 = 0;
}

bool WinButtonProjectedRect(FClientMeshHolder* Holder, float* OutRect4)
{
    // FUN_0059f200.
    if (!ClientMeshHolderHasInstance(Holder))
        return false;

    float Radius = 0.0f;
    float Center[3];
    DALib::Engine->GetInstanceExtent(Holder->EngineInstance, 0, &Radius, Center);
    float const* const Pos = DALib::Engine->GetInstancePosition(Holder->EngineInstance);
    Vector World;
    World.x = Center[0] + Pos[0];
    World.y = Center[1] + Pos[1];
    World.z = Center[2] + Pos[2];

    ViewRect Rect;
    float Depth = 0.0f;
    if (get_projected_bounding_box(AsCamera(Win3DGetCamera()), World, Radius, &Rect, Depth) == 0)
        return false;

    OutRect4[0] = static_cast<float>(Rect.Left);
    OutRect4[1] = static_cast<float>(Rect.Top);
    OutRect4[2] = static_cast<float>(Rect.Right);
    OutRect4[3] = static_cast<float>(Rect.Bottom);
    return true;
}

bool WinButton::HitTest(float const* Ray)
{
    // FUN_0059f2e0 (RET 4).
    (void)Ray;
    float Rect[4];
    if (!WinButtonProjectedRect(&MeshHolder, Rect))
        return false;

    int CursorX = 0;
    int CursorY = 0;
    int CursorZ = 0;
    GetClientSoftwareCursor(&CursorX, &CursorY, &CursorZ);
    float const Y = static_cast<float>(CursorY);
    float const X = static_cast<float>(CursorX);
    // TEST AH,1 / JNZ rejects "below or unordered", TEST AH,0x41 / JP "above or unordered".
    if (!(X >= Rect[0]))
        return false;
    if (!(X <= Rect[2]))
        return false;
    if (!(Y >= Rect[1]))
        return false;
    if (!(Y <= Rect[3]))
        return false;
    return true;
}

void WinButton::SetPosition(float const* NewPosition)
{
    // LAB_004c9320.
    Position[0] = NewPosition[0];
    Position[1] = NewPosition[1];
    Position[2] = NewPosition[2];
    RestPose.Translation.X = NewPosition[0];
    RestPose.Translation.Y = NewPosition[1];
    RestPose.Translation.Z = NewPosition[2];
    memcpy(Orientation, &RestPose, 12 * sizeof(float));
    HolderSetMatrixAndUpdate(&MeshHolder, Orientation);
}

void WinButton::SetOrientation(float const* NewOrientation)
{
    // LAB_004c92e0: nine dwords into +0x3c, then +0x3c .. +0x6b (orientation and position) into
    // the rest pose.
    memcpy(Orientation, NewOrientation, 9 * sizeof(float));
    memcpy(&RestPose, Orientation, 12 * sizeof(float));
    HolderSetMatrixAndUpdate(&MeshHolder, Orientation);
}

void WinButton::SetTransform(FTransform const* NewTransform)
{
    // FUN_004c92b0.
    memcpy(Orientation, NewTransform, 12 * sizeof(float));
    HolderSetMatrixAndUpdate(&MeshHolder, Orientation);
}

void WinButton::Update()
{
    // FUN_0059f360.
    double const Dt = GetClientFrameTimeCapped();

    // 0x0059f36d: only a positive speed moves the angle.
    if (SpinSpeed > 0.0f)
    {
        double const Delta = Dt * SpinSpeed;
        if (bHover != 0)
        {
            SpinAngle = static_cast<float>(Delta + SpinAngle);
        }
        else
        {
            // Off the button the angle runs on at twice the speed to the nearer rest: past 180
            // it continues to 359 and snaps to 0, otherwise it winds back below 1 and snaps.
            double const Delta2 = Delta + Delta;
            if (SpinAngle > 180.0f)
            {
                double const Next = Delta2 + SpinAngle;
                SpinAngle = static_cast<float>(Next);
                if (Next > 359.0)
                    SpinAngle = 0.0f;
            }
            else
            {
                double const Next = SpinAngle - Delta2;
                SpinAngle = static_cast<float>(Next);
                if (Next < 1.0)
                    SpinAngle = 0.0f;
            }
        }
    }

    // 0x0059f3ff: wrap into (-360, 360). TEST AH,1 / JNZ sends "below 360 or unordered" to the
    // second test, whose TEST AH,0x41 / JP skips "above -360 or unordered".
    if (SpinAngle >= 360.0f)
        SpinAngle = static_cast<float>(static_cast<double>(SpinAngle) - 360.0);
    else if (SpinAngle <= -360.0f)
        SpinAngle = static_cast<float>(static_cast<double>(SpinAngle) + 360.0);

    // 0x0059f43f .. 0x0059f4da: the rest orientation times RotateMatrix((0, angle, 0)) through
    // the 3DMathEngine (slot +0x20), with the rest translation, into +0x3c and the instance.
    Vector Angles;
    Angles.x = 0.0f;
    Angles.y = SpinAngle;
    Angles.z = 0.0f;
    Matrix const Spin = RotateMatrix(Angles);
    // DAT_0067dd10 (G_Win3DObjectMath, shared with FUN_0059efa0), FUN_005b73e0 when null.
    if (G_Win3DObjectMath == nullptr)
        G_Win3DObjectMath = Acquire3DMathEngine();
    FTransform Live;
    G_Win3DObjectMath->MulMatrix(Live.Rotation, RestPose.Rotation,
                                 *reinterpret_cast<FMatrix const*>(&Spin));
    Live.Translation = RestPose.Translation;
    memcpy(Orientation, &Live, 12 * sizeof(float));
    HolderSetMatrixAndUpdate(&MeshHolder, Orientation);

    // 0x0059f4df: the Win3D child walk (FUN_005a17b0 inlined).
    Win3D* Child = ChildHead;
    while (Child != nullptr)
    {
        Win3D* const NextChild = Child->NextSibling;
        if ((Child->Flags & kWin3DActive) != 0)
            Child->Update();
        else
            Child->UpdateInactive();
        Child = NextChild;
    }
}

void WinButton::Draw()
{
    // FUN_0059f510: "[INTERFACE]" (option 6) and "NEURONET" (option 9) must both be on.
    if (GetGameOption(kGameOptInterfaceGroup) == 0)
        return;
    if (GetGameOption(kGameOptNeuronet) == 0)
        return;

    if ((Flags & kWin3DVisible) != 0)
    {
        // 0x0059f546: FUN_0059efa0(camera, 0, 1.0f, 0) on the holder.
        SubmitWin3DEngineInstance(&MeshHolder, Win3DGetCamera(), 0, 1.0f, 0);
        // 0x0059f54b: with bit 0 of +0xa4 the tinted pass, FUN_00434630(camera, instance, 1,
        // &tint, 1, 1.0f, 0).
        if ((DrawFlags & 1) != 0)
        {
            SubmitWin3DTintedEngineInstanceWrapped(Win3DGetCamera(), MeshHolder.EngineInstance, 1,
                                                   TintRgba, 1, 1.0f, 0);
        }
    }

    // 0x0059f57a: JMP FUN_0059e5c0 when there are children.
    if (ChildHead != nullptr)
        Win3DDrawChildren(this);
}

void WinButton::MouseOver(float const* Ray)
{
    // FUN_004c9380: vt+0x10 (slot 4) with the ray, the result into +0xdc.
    bHover = HitTest(Ray) ? 1 : 0;
}

void WinButton::MouseLeave(float const* Ray)
{
    // LAB_004c93a0 (RET 4).
    (void)Ray;
    bHover = 0;
}

// ============================================================================================
// HUD_Button
// ============================================================================================

HUD_Button::HUD_Button(INI_Reader& Reader)
{
    // FUN_004c8d40 after the WinButton part: the HUDItem part at +0xf4 (inlined), the action
    // string at +0x12c emptied, both vptrs, HUDItem's name "Button" (FUN_00420750), the position
    // (0, 0, -5) straight into +0x60, +0x150 = 0, then the ini keys.
    Action.Length = 0;
    Action.Data[0] = '\0';
    HUDItem::Name = "Button";   // HUDItem's TString, not Win3D's +0x18 name
    Position[0] = 0.0f;
    Position[1] = 0.0f;
    Position[2] = -5.0f;
    CommandId = 0;

    while (Reader.read_value())
    {
        if (Reader.is_value("action"))
        {
            char const* const Value = Reader.get_value_string();
            Action = Value;
            CommandId = 0;
            if (Action == "radio")
                CommandId = 0x5c;
            else if (Action == "status")
                CommandId = 0x5e;
            else if (Action == "zoom")
                CommandId = 0x55;
            else if (Action == "navmap")
                CommandId = 0x63;
            else if (Action == "toggle_manuevers")
                CommandId = 0x73;
            else
                DebugPrint("Warning: unknown HUD_Button action %s\n", Value);
        }
        else if (Reader.is_value("object"))
        {
            // 0x004c8f6b: FUN_0059f1c0(0, path, 0).
            LoadMeshAndCreate(nullptr, Reader.get_value_string(), 0);
        }
        else if (Reader.is_value("object_pos"))
        {
            // 0x004c8f93: values 1, 0, 2 read in that order; the point goes to +0x60 and to the
            // rest translation, then FUN_004c92b0 with the rest pose.
            float const Y = Reader.get_value_float(1);
            float const X = Reader.get_value_float(0);
            float const Z = Reader.get_value_float(2);
            Position[0] = X;
            Position[1] = Y;
            Position[2] = Z;
            RestPose.Translation.X = X;
            RestPose.Translation.Y = Y;
            RestPose.Translation.Z = Z;
            SetTransform(&RestPose);
        }
        else if (Reader.is_value("tool_tip_id"))
        {
            ToolTipIds = static_cast<int>(Reader.get_value_float(0));
        }
    }
}

HUD_Button::~HUD_Button()
{
    // FUN_004c94b0: WinObject::Destroy inlined (release a bound instance, clear bits 0 and 1,
    // slot 10 on every child, FUN_0059dc00); then ~HUDItem, the holder's destructor and ~Win3D
    // run as the bases unwind.
    WinObject::Destroy();
}

void HUD_Button::Update()
{
    // LAB_004c93d0: visible only with DAT_00673560 set and options 6 and 0xa on.
    bool const bVisible = IsCockpitResourceStringLoading() &&
                          GetGameOption(kGameOptInterfaceGroup) != 0 &&
                          GetGameOption(kGameOptHud) != 0;
    Flags = static_cast<unsigned char>((Flags & ~kWin3DVisible) | (bVisible ? kWin3DVisible : 0));
    if (bHover != 0)
        HUDItem::ShowToolTip(); // FUN_004c81d0 on +0xf4
    WinButton::Update();        // JMP FUN_0059f360
}

void HUD_Button::Draw()
{
    // LAB_004c93b0: FUN_00421ed0(0xb71), then JMP FUN_0059f510.
    Win3DDisable(kWin3DDepthTest);
    WinButton::Draw();
}

bool HUD_Button::MouseEvent(FWin3DMouseEvent const* Event)
{
    // LAB_004c9430 (RET 4): a click (-1, 2) posts the command record through FUN_0054bcb0.
    if (Event->Field0 != -1)
        return false;
    if (Event->Field1 != 2)
        return false;
    FSpaceCommand Command;
    memset(&Command, 0, sizeof(Command));
    Command.Byte26 = 1;
    Command.Id = CommandId;
    if (CommandId == 0)
        return false;
    return SpaceSendCommand(&Command);
}

HUDItem* HUD_ButtonFactory::Create(INI_Reader& Reader)
{
    // FUN_004c8d40: operator new(0x154), WinObject's ctor FUN_004c9060, the rest inlined.
    HUD_Button* const Button = new HUD_Button(Reader);
    return Button;
}

namespace
{
    // DAT_006733b0 -- XC entry LAB_004c8320: FUN_004c8ca0(this, "Button"); atexit LAB_004c8340.
    HUD_ButtonFactory G_HudButtonFactory("Button");
}
