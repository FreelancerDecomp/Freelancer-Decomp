// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "nn_static_control.h"
#include "hud_widescreen.h"   // port-only (FREELANCER_WIDESCREEN_SUPPORT)

#include "engine_light.h"         // EngineLightClearRegisteredList (FUN_004101e0)
#include "game_options.h"         // GetGameOption (FUN_005792b0)
#include "globals.h"              // GetClientFrameTimeCapped, G_RenderSurfaceWidth / Height
#include "nn_ui_pulse.h"          // G_NnPaletteArgb_679bb0 / _679b94
#include "tooltip_hover.h"        // FToolTipHoverInfo / NnToolTipFeedHover (FUN_004be370)
#include "win3d.h"
#include "win3d_engine_submit.h"  // FUN_0059efa0 / FUN_00434630 / FUN_00434670
#include "win3d_immediate.h"      // Win3DEnable / Disable, viewport and projection helpers
#include "win3d_render.h"         // Win3DApplyCamera (FUN_00422950)
#include "win_factory.h"

#include "common_csys.h"          // Acquire3DMathEngine (FUN_005b73e0)
#include "common_ini.h"           // INI_Reader
#include "x86math.h"

#include "DALib.h"
#include "engine/engbase.h"
#include "render/IRenderPipeline.h"
#include "render/rendcomp.h"

#include <math.h>
#include <string.h>

// Retail DAT_0067c504 -- WINFACTORY<StaticControl> (static init LAB_00587e10: FUN_005899b0
// "StaticControl" + atexit LAB_00587e30 -> FUN_00579380); Create FUN_00589a20.
WINFACTORY<NnStaticControl> G_StaticControlFactory("StaticControl");

namespace
{
    // s_E:\FL\Scratch\Source\Client\Interface\StaticControl.cpp_005e4ec0 (the viewport calls'
    // source-file argument, lines 0x13d and 0x185).
    char const kStaticControlCpp[] = "E:\\FL\\Scratch\\Source\\Client\\Interface\\StaticControl.cpp";

    // DAT_005db098 -- 0x38d1b717, 1e-4: Update's "already at the start angle" threshold.
    float const kAngleEpsilon = 9.99999975e-05f;
    // DAT_005c7f68 -- 1/255 (0x3b808081)
    float const kInv255 = 1.0f / 255.0f;
    // 0x420c0000 -- the render-to-window camera's vertical field of view.
    float const kWindowFovY = 35.0f;
    // 0xc2ae0000 -- where a render-to-window instance sits while it is drawn.
    float const kWindowDepth = -87.0f;

    // DAT_0067c53c -- the 3DMathEngine this class acquires on first use (FUN_005b73e0), shared
    // by Draw (0x00588bc5) and SetOrientation (0x00588d19).
    I3DMathEngine* G_StaticMathEngine = nullptr;

    I3DMathEngine* StaticMathEngine()
    {
        if (G_StaticMathEngine == nullptr)
            G_StaticMathEngine = Acquire3DMathEngine();
        return G_StaticMathEngine;
    }

    bool HasInstance(WinControl const* Control)
    {
        // The `CMP [ESI+0x84],-1` every slot below inlines.
        return Control->MeshHolder.EngineInstance !=
               reinterpret_cast<void*>(static_cast<intptr_t>(-1));
    }

    // FUN_004215b0 on a 3x4 local with its position cleared: identity orientation, origin.
    void SetIdentityTransform(float* Transform12)
    {
        Transform12[0] = 1.0f;
        Transform12[1] = 0.0f;
        Transform12[2] = 0.0f;
        Transform12[3] = 0.0f;
        Transform12[4] = 1.0f;
        Transform12[5] = 0.0f;
        Transform12[6] = 0.0f;
        Transform12[7] = 0.0f;
        Transform12[8] = 1.0f;
        Transform12[9] = 0.0f;
        Transform12[10] = 0.0f;
        Transform12[11] = 0.0f;
    }

    // The instance placement Init (0x005885a2..0x0058864a), Create's "offset"
    // (0x005880fb..0x005881b4) and property 0x17 (0x005891b2..0x00589229) spell out: identity
    // orientation at (offset + position) through Engine SetInstanceMatrix (+0x9c), then
    // UpdateInstance (+0x6c). The three sums are +0xa8.. + +0x60.. (FUN_00422b80).
    void PlaceInstanceAtOffset(NnStaticControl* Self)
    {
        float Transform[12];
        SetIdentityTransform(Transform);
        Transform[9] = Self->Offset[0] + Self->Position[0];
        Transform[10] = Self->Offset[1] + Self->Position[1];
        Transform[11] = Self->Offset[2] + Self->Position[2];
        DALib::Engine->SetInstanceMatrix(Self->MeshHolder.EngineInstance, Transform);
        DALib::Engine->UpdateInstance(Self->MeshHolder.EngineInstance, 0, nullptr);
    }

    // One plane rotation of the three rows of a 3x3, as SetOrientation and the angle
    // properties unroll it: per row, a = M[r][ColA], b = M[r][ColB] (both read before either
    // is written), then M[r][ColA] = C*a + S*b and M[r][ColB] = C*b - S*a. The products are
    // kept on the FPU stack; SetOrientation and the first turn of each property store C as a
    // float first (FSTP dword), the second turn of each property keeps both on the stack.
    void RotateColumns(float* M, int ColA, int ColB, double C, double S)
    {
        for (int Row = 0; Row < 3; ++Row)
        {
            double const A = M[Row * 3 + ColA];
            double const B = M[Row * 3 + ColB];
            M[Row * 3 + ColA] = static_cast<float>(C * A + S * B);
            M[Row * 3 + ColB] = static_cast<float>(C * B - S * A);
        }
    }

    // The holder tint Draw writes before FUN_0059efa0 (0x00588aab..0x00588b10): bytes 0..2
    // into +0x14 / +0x18 / +0x1c, byte 3 into +0x24, each times 1/255.
    void SetHolderTint(FClientMeshHolder* Holder, unsigned char const* Rgba)
    {
        Holder->UserFloats[0] = static_cast<float>(static_cast<double>(Rgba[0]) * kInv255);
        Holder->UserFloats[1] = static_cast<float>(static_cast<double>(Rgba[1]) * kInv255);
        Holder->UserFloats[2] = static_cast<float>(static_cast<double>(Rgba[2]) * kInv255);
        Holder->UserFloats[4] = static_cast<float>(static_cast<double>(Rgba[3]) * kInv255);
    }

    // SetProperty 0xd / 0xf / 0x11 (0x00589261.., 0x00589379.., 0x0058948b..): the bound
    // instance's transform (Engine +0xa0) turned back by the old angle, then by the new one,
    // written back (+0x9c) and updated (+0x6c).
    void TurnInstanceAngle(NnStaticControl* Self, int ColA, int ColB, float OldAngle,
                           float NewAngle)
    {
        float Transform[12];
        memcpy(Transform, DALib::Engine->GetInstanceTransform(Self->MeshHolder.EngineInstance),
               sizeof(Transform));
        double const Back = -static_cast<double>(OldAngle);
        RotateColumns(Transform, ColA, ColB, static_cast<float>(cos(Back)), sin(Back));
        double const Forward = static_cast<double>(NewAngle);
        RotateColumns(Transform, ColA, ColB, cos(Forward), sin(Forward));
        DALib::Engine->SetInstanceMatrix(Self->MeshHolder.EngineInstance, Transform);
        DALib::Engine->UpdateInstance(Self->MeshHolder.EngineInstance, 0, nullptr);
    }

    float FloatFromBits(uintptr_t Bits)
    {
        unsigned const Value = static_cast<unsigned>(Bits);
        float Result;
        memcpy(&Result, &Value, sizeof(Result));
        return Result;
    }

    intptr_t BitsFromFloat(float Value)
    {
        unsigned Bits;
        memcpy(&Bits, &Value, sizeof(Bits));
        return static_cast<intptr_t>(static_cast<int>(Bits));
    }

    // The "boolean" INI keys: get_value_float(0) against 0.0 (DAT_005c8ba8; FCOMP / TEST
    // AH,0x44 / JNP: only an equal value gives 0).
    unsigned char IniFlag(INI_Reader* Ini)
    {
        return (Ini->get_value_float(0) != 0.0) ? 1 : 0;
    }
}

NnStaticControl::NnStaticControl()
    : CurRoll(0.0f)
    , CurPitch(0.0f)
    , CurYaw(0.0f)
    , bNoMeshRender(0)
    , bNoWireRender(0)
    , bNoColoring(0)
    , Field_0x347(0)
    , bUseColours(0)
    , bEnableRotation(0)
    , bNoClearRender(1)
    , bRenderToWindow(0)
    , bZClear(0)
    , ForceRadius(0.0f)
    , RotationSpeed(0.0f)
{
    // FUN_00587e40 after FUN_0059f860: the camera (FUN_0040f150 on +0x350), both colours and
    // every flag, angle and float zeroed, +0x34a = 1.
    SpaceCameraConstruct(&Camera);
    StartYpr[0] = 0.0f;
    StartYpr[1] = 0.0f;
    StartYpr[2] = 0.0f;
    Pad34d[0] = 0;
    Pad34d[1] = 0;
    Pad34d[2] = 0;
    memset(MeshColour, 0, sizeof(MeshColour));
    memset(WireColour, 0, sizeof(WireColour));
}

NnStaticControl::~NnStaticControl()
{
    // FUN_00587f60: the camera's vftable back to 0x005c8944 and its empty dtor
    // (IDPMsgHandler::OnDisconnect, a RET), then WinControl's FUN_0059fa50. Nothing to free.
}

void NnStaticControl::SetPosition(float const* NewPosition)
{
    // LAB_005896b0 (RET 4).
    Field2e0[0] = NewPosition[0];
    Field2e0[1] = NewPosition[1];
    Field2e0[2] = NewPosition[2];

    float Placed[3];
    if ((PositionFlags & 1) != 0)
    {
        float Attach[3];
        GetAttachPosition(Attach); // slot 41
        Placed[0] = Attach[0] + NewPosition[0];
        Placed[1] = Attach[1] + NewPosition[1];
        Placed[2] = Attach[2] + NewPosition[2];
    }
    else
    {
        Placed[0] = NewPosition[0];
        Placed[1] = NewPosition[1];
        Placed[2] = NewPosition[2];
    }
    Position[0] = Placed[0];
    Position[1] = Placed[1];
    Position[2] = Placed[2];

    // 0x00589728: the instance at +0x60 (+ +0xa8 under PositionFlags bit 0).
    if (HasInstance(this))
    {
        float Instance[3];
        if ((PositionFlags & 1) != 0)
        {
            Instance[0] = Position[0] + Offset[0];
            Instance[1] = Position[1] + Offset[1];
            Instance[2] = Position[2] + Offset[2];
        }
        else
        {
            Instance[0] = Position[0];
            Instance[1] = Position[1];
            Instance[2] = Position[2];
        }
        DALib::Engine->SetInstancePosition(MeshHolder.EngineInstance, Instance);
        DALib::Engine->UpdateInstance(MeshHolder.EngineInstance, 0, nullptr);
    }

    // 0x005897ac: every child placed at this control's position (slot 5).
    for (Win3D* Child = ChildHead; Child != nullptr; Child = Child->NextSibling)
        Child->SetPosition(Position);
}

void NnStaticControl::SetOrientation(float const* NewOrientation)
{
    // LAB_00588d00 (RET 4): the new orientation times the attach orientation (slot 40) through
    // the 3DMathEngine's MulMatrix (+0x20) into Win3D's +0x3c; the instance gets that turned by
    // the pitch (+0x330, columns 1/2), the yaw (+0x334, columns 2/0) and the roll (+0x32c,
    // columns 0/1), in that order.
    FMatrix Attach;
    GetAttachOrientation(&Attach.D[0][0]);
    FMatrix Result;
    StaticMathEngine()->MulMatrix(Result, *reinterpret_cast<FMatrix const*>(NewOrientation),
                                  Attach);
    memcpy(Orientation, &Result, sizeof(Orientation));
    if (!HasInstance(this))
        return;

    float Turned[9];
    memcpy(Turned, &Result, sizeof(Turned));
    double const Pitch = static_cast<double>(CurPitch);
    RotateColumns(Turned, 1, 2, static_cast<float>(cos(Pitch)), sin(Pitch));
    double const Yaw = static_cast<double>(CurYaw);
    RotateColumns(Turned, 2, 0, static_cast<float>(cos(Yaw)), sin(Yaw));
    double const Roll = static_cast<double>(CurRoll);
    RotateColumns(Turned, 0, 1, static_cast<float>(cos(Roll)), sin(Roll));
    DALib::Engine->SetInstanceOrientation(MeshHolder.EngineInstance, Turned); // +0x94
    DALib::Engine->UpdateInstance(MeshHolder.EngineInstance, 0, nullptr);
}

bool NnStaticControl::Create(Win3D* NewParent, void* Data)
{
    // LAB_00587fc0 (RET 8).
    Win3DReparent(this, NewParent); // FUN_0059dd00
    Win3DRegister(this);            // FUN_0059dbf0
    Flags = static_cast<unsigned char>((Flags & 0xfb) | 0x03);

    INI_Reader* const Ini = static_cast<INI_Reader*>(Data);
    if (Ini != nullptr)
    {
        while (Ini->read_value())
        {
            if (Ini->is_value("mesh")) // DAT_005dba88
            {
                SetMesh(Ini->get_value_string(), 0); // slot 38
                continue;
            }
            if (Ini->is_value("mouse_size"))
            {
                float const Y = static_cast<float>(Ini->get_value_float(1));
                float const X = static_cast<float>(Ini->get_value_float(0));
                float const Z = static_cast<float>(Ini->get_value_float(2));
                HalfExtents[0] = X;
                HalfExtents[1] = Y;
                HalfExtents[2] = Z;
                continue;
            }
            if (Ini->is_value("offset"))
            {
                float const Y = static_cast<float>(Ini->get_value_float(1));
                float const X = static_cast<float>(Ini->get_value_float(0));
                float const Z = static_cast<float>(Ini->get_value_float(2));
                Offset[0] = X;
                Offset[1] = Y;
                Offset[2] = Z;
                if (HasInstance(this))
                    PlaceInstanceAtOffset(this);
                continue;
            }
            if (Ini->is_value("hardpoint"))
            {
                // 0x005881cd: the name is kept at +0x1d0, the attach transform reset, then
                // filled from the NEW parent's hardpoint of that name (slot 36 of the parent
                // as a WinControl); its answer is +0x328.
                strcpy(HardpointName, Ini->get_value_string());
                AttachOrientation[0] = 1.0f;
                AttachOrientation[1] = 0.0f;
                AttachOrientation[2] = 0.0f;
                AttachOrientation[3] = 0.0f;
                AttachOrientation[4] = 1.0f;
                AttachOrientation[5] = 0.0f;
                AttachOrientation[6] = 0.0f;
                AttachOrientation[7] = 0.0f;
                AttachOrientation[8] = 1.0f;
                AttachPosition[0] = 0.0f;
                AttachPosition[1] = 0.0f;
                AttachPosition[2] = 0.0f;
                if (NewParent == nullptr)
                    continue;
                WinControl* const ParentControl = NewParent->AsWinControl();
                if (ParentControl == nullptr)
                    continue;
                bAttached = ParentControl->GetHardpointTransform(Ini->get_value_string(),
                                                                 AttachOrientation)
                                ? 1
                                : 0;
                continue;
            }
            if (Ini->is_value("no_z_enable"))
            {
                bNoZEnable = IniFlag(Ini);
                continue;
            }
            if (Ini->is_value("zclear"))
            {
                bZClear = IniFlag(Ini);
                continue;
            }
            if (Ini->is_value("no_mesh_render"))
            {
                bNoMeshRender = IniFlag(Ini);
                continue;
            }
            if (Ini->is_value("no_wire_render"))
            {
                bNoWireRender = IniFlag(Ini);
                continue;
            }
            if (Ini->is_value("no_coloring"))
            {
                bNoColoring = IniFlag(Ini);
                continue;
            }
            if (Ini->is_value("render_to_window"))
            {
                bRenderToWindow = IniFlag(Ini);
                continue;
            }
            if (Ini->is_value("no_clear_render"))
            {
                bNoClearRender = IniFlag(Ini);
                continue;
            }
            if (Ini->is_value("force_radius"))
            {
                ForceRadius = static_cast<float>(Ini->get_value_float(0));
                continue;
            }
            if (Ini->is_value("rotation_speed"))
            {
                RotationSpeed = static_cast<float>(Ini->get_value_float(0));
                continue;
            }
            if (Ini->is_value("enable_rotation"))
            {
                bEnableRotation = IniFlag(Ini);
                continue;
            }
            if (Ini->is_value("starting_yaw_pitch_roll"))
            {
                float const Pitch = static_cast<float>(Ini->get_value_float(1));
                float const Yaw = static_cast<float>(Ini->get_value_float(0));
                float const Roll = static_cast<float>(Ini->get_value_float(2));
                StartYpr[0] = Yaw;
                StartYpr[1] = Pitch;
                StartYpr[2] = Roll;
                CurYaw = StartYpr[0];
                CurPitch = StartYpr[1];
                CurRoll = StartYpr[2];
            }
        }
    }

    // 0x00588523: the parent is re-placed at its own position (slot 5), which positions this
    // control with it.
    if (Parent != nullptr)
        Parent->SetPosition(Parent->Position);
    return true;
}

void NnStaticControl::Update()
{
    // LAB_00588e80: Win3D's child walk (FUN_005a17b0) at the head and again on every way out.
    Win3D::Update();
    if (!HasInstance(this) || RotationSpeed == 0.0f) // FCOMP 0 / TEST AH,0x44 / JNP
    {
        Win3D::Update();
        return;
    }

    float const Identity[9] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    if (bEnableRotation != 0)
    {
        // 0x00588f01: the yaw runs on with the frame time.
        CurYaw = static_cast<float>(GetClientFrameTimeCapped() *
                                        static_cast<double>(RotationSpeed) +
                                    static_cast<double>(CurYaw)); // FUN_0042d680
        SetOrientation(Identity); // slot 7
        Win3D::Update();
        return;
    }

    // 0x00588f30: rotation off -- if any angle is more than sqrt(1e-4) away from its start
    // (FCOMP / TEST AH,0x41: JP on greater or unordered), all three snap back.
    float const Pitch = CurPitch;
    float const Roll = CurRoll;
    double const DYaw = static_cast<double>(StartYpr[0]) - static_cast<double>(CurYaw);
    double const DPitch = static_cast<double>(StartYpr[1]) - static_cast<double>(Pitch);
    double const DRoll = static_cast<double>(StartYpr[2]) - static_cast<double>(Roll);
    double const Eps = static_cast<double>(kAngleEpsilon);
    if (!(DYaw * DYaw <= Eps) || !(DPitch * DPitch <= Eps) || !(DRoll * DRoll <= Eps))
    {
        CurPitch = StartYpr[1];
        CurYaw = StartYpr[0];
        CurRoll = StartYpr[2];
        SetOrientation(Identity); // slot 7
    }
    Win3D::Update();
}

void NnStaticControl::Draw()
{
#ifdef FREELANCER_WIDESCREEN_SUPPORT
    // Port-only: owners that walk their children themselves (HudTargetBase, NN_MainButton)
    // reach this without the Win3D draw walk, so the control sets its own HUD shift.
    FWidescreenHudShiftScope const WidescreenScope(WidescreenHudShiftPixelsFor(this), true);
#endif
    // LAB_00588770.
    Win3D::Draw(); // FUN_0059e5c0
    if (GetGameOption(kGameOptNeuronet) == 0)
        return;
    if (GetGameOption(kGameOptInterfaceGroup) == 0)
        return;
    if (GetGameOption(kGameOptDrawStaticControls) == 0)
        return;
    EngineLightClearRegisteredList(); // FUN_004101e0
    if (!HasInstance(this))
    {
        Slot39(); // slot 39 (+0x9c), 0x00588ce2
        return;
    }

    // 0x005887cb: the mesh colour and the wire colour -- the pair from property 0x18, else the
    // idle cyan for both; an inactive control's wire goes grey, "no_coloring" whitens the mesh.
    unsigned char Mesh[4];
    unsigned char Wire[4];
    if (bUseColours != 0)
    {
        memcpy(Mesh, MeshColour, sizeof(Mesh));
        memcpy(Wire, WireColour, sizeof(Wire));
    }
    else
    {
        memcpy(Mesh, &G_NnPaletteArgb_679bb0, sizeof(Mesh));
        memcpy(Wire, &G_NnPaletteArgb_679bb0, sizeof(Wire));
    }
    if ((Flags & kWin3DActive) == 0)
        memcpy(Wire, &G_NnPaletteArgb_679b94, sizeof(Wire));
    if (bNoColoring != 0)
    {
        Mesh[0] = 0xff;
        Mesh[1] = 0xff;
        Mesh[2] = 0xff;
        Mesh[3] = 0xff;
    }

    // 0x00588827: the draw scale -- "force_radius" over the instance's radius (Engine +0x88).
    float Radius = 0.0f;
    float Centre[3];
    DALib::Engine->GetInstanceExtent(MeshHolder.EngineInstance, 0, &Radius, Centre);
    float Scale = 1.0f;
    if (ForceRadius != 0.0f) // FCOMP 0 / TEST AH,0x44 / JNP: equal keeps 1.0
        Scale = ForceRadius / Radius;

    FSpaceCamera* ViewCamera = Win3DGetCamera(); // DAT_0067dbf8
    int SavedViewport[4];
    float Placed[3];
    if (bRenderToWindow != 0)
    {
        // 0x00588883: the instance's box projected by the Win3D camera, truncated (ftol),
        // shrunk by two pixels and flipped in y; off-screen or clipped boxes draw nothing.
        Win3DGetViewport(0xba2, SavedViewport); // FUN_00422390
        float const* const At = ClientMeshHolderGetPosition(&MeshHolder); // FUN_00420fb0
        Placed[0] = At[0];
        Placed[1] = At[1];
        Placed[2] = At[2];
        ICamera* const WinCamera = AsCamera(Win3DGetCamera());
        float X = 0.0f;
        float Y = 0.0f;
        float Z = 0.0f;
        float Corner[3];
        Corner[0] = Placed[0] - HalfExtents[0];
        Corner[1] = Placed[1] - HalfExtents[1];
        Corner[2] = Placed[2] - HalfExtents[2];
        WinCamera->Project(&X, &Y, &Z, Corner); // camera slot 13 (+0x34)
        int Left = static_cast<int>(X);
        int Bottom = static_cast<int>(Y);
        Corner[0] = Placed[0] + HalfExtents[0];
        Corner[1] = Placed[1] + HalfExtents[1];
        Corner[2] = Placed[2] + HalfExtents[2];
        WinCamera->Project(&X, &Y, &Z, Corner);
        int Right = static_cast<int>(X);
        int Top = static_cast<int>(Y);
        if (Left < 0 || Top < 0 || Right < 0 || Bottom < 0)
        {
            Bottom = 0;
            Right = 0;
            Top = 0;
            Left = 0;
        }
        int Rect[4];
        Rect[0] = Left + 2;
        Rect[1] = Top + 2;
        Rect[2] = Right - 2;
        Rect[3] = Bottom - 2;
        int const Height = static_cast<int>(G_RenderSurfaceHeight); // DAT_00610854
        if (Rect[1] >= Height || Rect[3] >= Height)
            return;
        int const Width = static_cast<int>(G_RenderSurfaceWidth); // DAT_00610850
        if (Rect[0] >= Width || Rect[2] >= Width)
            return;
        if (Rect[0] < 0 || Rect[2] < 0 || Rect[1] < 0 || Rect[3] < 0)
            return;

        // 0x00588a07: the instance moved in front of the window camera, the camera fitted to
        // the rect with the Win3D camera's near / far (FUN_0040fb60, two stores), a 35 degree
        // vertical field of view, and made current.
        float const Behind[3] = {0.0f, 0.0f, kWindowDepth};
        ClientMeshHolderSetPosition(&MeshHolder, Behind); // FUN_00420fd0
        DALib::Engine->UpdateInstance(MeshHolder.EngineInstance, 0, nullptr);
        SpaceCameraSetViewportEx(&Camera, Rect, 1); // FUN_0040fb80
        FSpaceCamera const* const SceneCamera = Win3DGetCamera();
        Camera.ZNear = SceneCamera->ZNear; // DAT_0067dc9c
        Camera.ZFar = SceneCamera->ZFar;   // DAT_0067dca0
        SpaceCameraSetFovY(&Camera, kWindowFovY);                  // FUN_0040fa10
        Win3DSetViewport(AsCamera(&Camera), kStaticControlCpp, 0x13d); // FUN_00422610
        Win3DApplyCameraPerspective(AsCamera(&Camera));             // FUN_00422910
        ViewCamera = &Camera;
    }

    // 0x00588a99: the mesh pass.
    if (bNoMeshRender == 0)
    {
        SetHolderTint(&MeshHolder, Mesh);
        if (bNoZEnable != 0)
        {
            Win3DEnable(0xb71); // FUN_00421dd0
            if (bZClear != 0)
            {
                // 0x00588b37: cull 8, lighting on, then the instance drawn straight through
                // IRenderer2 +0x1c with a scaled identity (flags 0x30).
                IRenderPipeline* const Pipe = DALib::Pipeline;
                Pipe->SetRenderState(D3DRS_ZFUNC, 8);
                Pipe->SetRenderState(D3DRS_ZWRITEENABLE, 1);
                float Scaled[12];
                SetIdentityTransform(Scaled);
                StaticMathEngine()->Scale(*reinterpret_cast<FMatrix*>(Scaled),
                                          *reinterpret_cast<FMatrix const*>(Scaled), Scale);
                DALib::Renderer->RenderInstance(ViewCamera, MeshHolder.EngineInstance, 1.0f, 0x30u,
                                       Scaled, nullptr);
            }
            else
            {
                SubmitWin3DEngineInstance(&MeshHolder, ViewCamera, 1, Scale, 0); // FUN_0059efa0
            }
        }
        else
        {
            Win3DDisable(0xb71); // FUN_00421ed0
            SubmitWin3DEngineInstance(&MeshHolder, ViewCamera, 0, Scale, 0);
        }
    }

    // 0x00588c29: the wire pass.
    Win3DDisable(0xb71);
    if (bNoWireRender == 0)
    {
        if (bNoZEnable != 0)
        {
            SubmitWin3DTintedEngineInstanceWrapped(ViewCamera, MeshHolder.EngineInstance, 1, Wire,
                                                   0, Scale, 0); // FUN_00434630
        }
        else
        {
            SubmitWin3DOutlinedEngineInstance(ViewCamera, MeshHolder.EngineInstance, 1, Wire,
                                              Scale, 0); // FUN_00434670
        }
    }

    // 0x00588c7f: the Win3D camera, the saved viewport and the instance's place back.
    if (bRenderToWindow != 0)
    {
        Win3DApplyCamera(AsCamera(Win3DGetCamera())); // FUN_00422950(&DAT_0067dbf8)
        Win3DSetViewportRect(SavedViewport[0], SavedViewport[1], SavedViewport[2],
                             SavedViewport[3], kStaticControlCpp, 0x185); // FUN_00421620
        ClientMeshHolderSetPosition(&MeshHolder, Placed);
    }
    Win3DDisable(0xb71);
    Slot39();
}

bool NnStaticControl::MouseEvent(FWin3DMouseEvent const* Event)
{
    // FUN_00588fe0 (RET 4): a button event (Field0 -1) is taken; the parent (as a WinControl)
    // hears 0x1b for a right press (Field1 3), 0x1a otherwise.
    if (Event->Field0 != -1)
        return false;
    if (Parent == nullptr)
        return true;
    WinControl* const ParentControl = Parent->AsWinControl();
    if (ParentControl == nullptr)
        return true;
    int const Msg = (Event->Field1 == 3) ? 0x1b : 0x1a;
    ParentControl->SetProperty(Msg, reinterpret_cast<uintptr_t>(AsWinControl()), 0);
    return true;
}

void NnStaticControl::ShowToolTip()
{
    // FUN_005897d0: ButtonControl's FUN_0055bde0 without the command -- key -1 and no alt id
    // first, the empty caption (FUN_00413df0 on null), then both ids from +0xcc, the aux dword
    // left 0, the instance's position (Engine +0xa0 [9..11]) or +0x60 + +0xa8, the half extents
    // +0xb4 and the live byte; FUN_004be370; then the record's teardown (~FToolTipHoverInfo).
    FToolTipHoverInfo Info;           // caption +0x08 / +0x0c null, FUN_004144b0 on +0x10
    Info.IdsAlt = 0;
    Info.Ids = -1;
    Info.AuxField = 0;
    Info.bLive = 0;
    Info.Ids = ToolTipIds;
    Info.IdsAlt = ToolTipIds;
    if (HasInstance(this))
    {
        float Transform[12];
        memcpy(Transform, DALib::Engine->GetInstanceTransform(MeshHolder.EngineInstance),
               sizeof(Transform));
        Info.WorldPos[0] = Transform[9];
        Info.WorldPos[1] = Transform[10];
        Info.WorldPos[2] = Transform[11];
    }
    else
    {
        Info.WorldPos[0] = Position[0] + Offset[0];
        Info.WorldPos[1] = Offset[1] + Position[1];
        Info.WorldPos[2] = Offset[2] + Position[2];
    }
    Info.HalfExtents[0] = HalfExtents[0];
    Info.HalfExtents[1] = HalfExtents[1];
    Info.HalfExtents[2] = HalfExtents[2];
    Info.bLive = 1;
    NnToolTipFeedHover(&Info);
}

intptr_t NnStaticControl::SetProperty(int Msg, uintptr_t A, uintptr_t B)
{
    // LAB_00589050 (RET 0xc): byte table 0x00589660 indexed by Msg - 0xc (0..0x41) into the
    // dword table 0x0058962c; everything not listed tail-calls WinControl's FUN_005a13c0.
    switch (Msg)
    {
    case 0x0c:
        Field_0x347 = (A != 0) ? 1 : 0;
        return 0;
    case 0x0d:
        // Roll (+0x32c): the instance turned back and forth on columns 0/1; the new angle is
        // also the start roll (+0x340).
        if (HasInstance(this))
            TurnInstanceAngle(this, 0, 1, CurRoll, FloatFromBits(A));
        CurRoll = FloatFromBits(A);
        StartYpr[2] = FloatFromBits(A);
        return 0;
    case 0x0e:
        return BitsFromFloat(CurRoll);
    case 0x0f:
        // Pitch (+0x330) on columns 1/2; also the start pitch (+0x33c).
        if (HasInstance(this))
            TurnInstanceAngle(this, 1, 2, CurPitch, FloatFromBits(A));
        CurPitch = FloatFromBits(A);
        StartYpr[1] = FloatFromBits(A);
        return 0;
    case 0x10:
        return BitsFromFloat(CurPitch);
    case 0x11:
        // Yaw (+0x334) on columns 2/0; also the start yaw (+0x338).
        if (HasInstance(this))
            TurnInstanceAngle(this, 2, 0, CurYaw, FloatFromBits(A));
        CurYaw = FloatFromBits(A);
        StartYpr[0] = FloatFromBits(A);
        return 0;
    case 0x12:
        return BitsFromFloat(CurYaw);
    case 0x17:
    {
        // 0x005890fc: a new engine instance handed in -- the holder released (FUN_0059eee0),
        // A bound, every angle cleared, and the instance placed at offset + position.
        ClientUiMeshHolderRelease(&MeshHolder);
        MeshHolder.EngineInstance = reinterpret_cast<void*>(A);
        StartYpr[0] = 0.0f;
        StartYpr[1] = 0.0f;
        StartYpr[2] = 0.0f;
        CurRoll = 0.0f;
        CurPitch = 0.0f;
        CurYaw = 0.0f;
        if (HasInstance(this))
            PlaceInstanceAtOffset(this);
        return 0;
    }
    case 0x18:
        bUseColours = 1;
        memcpy(MeshColour, reinterpret_cast<void const*>(A), sizeof(MeshColour));
        memcpy(WireColour, reinterpret_cast<void const*>(B), sizeof(WireColour));
        return 0;
    case 0x19:
        // Two flags per word: A = (no_mesh_render, no_wire_render), B = (no_z_enable,
        // no_coloring), low word first.
        bNoMeshRender = ((A & 0xffff) != 0) ? 1 : 0;
        bNoWireRender = ((A >> 16) != 0) ? 1 : 0;
        bNoZEnable = ((B & 0xffff) != 0) ? 1 : 0;
        bNoColoring = ((B >> 16) != 0) ? 1 : 0;
        return 0;
    case 0x1c:
    {
        bEnableRotation = (A == 1) ? 1 : 0;
        if (bEnableRotation != 0)
            return 0;
        float const Identity[9] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
        SetOrientation(Identity); // slot 7
        return 0;
    }
    case 0x4d:
        // FILD qword {A, 0}: the unsigned dword as a float.
        ForceRadius = static_cast<float>(static_cast<unsigned long long>(static_cast<unsigned>(A)));
        return 0;
    default:
        return WinControl::SetProperty(Msg, A, B); // FUN_005a13c0
    }
}

void NnStaticControl::Init(char const* NewMeshPath, float const* NewOffset, char const* Hardpoint,
                           float const* NewHalfExtents, unsigned char bNewNoZEnable,
                           unsigned char bNewZClear, unsigned char bNewNoMeshRender,
                           unsigned char bNewNoWireRender, unsigned char bNewNoColoring,
                           unsigned char bNewRenderToWindow, unsigned char bNewNoClearRender,
                           int NewForceRadius, int NewRotationSpeed,
                           unsigned char bNewEnableRotation, float const* NewStartYpr)
{
    // FUN_00588540 (RET 0x3c).
    if (NewMeshPath != nullptr)
        SetMesh(NewMeshPath, 0); // slot 38
    HalfExtents[0] = NewHalfExtents[0];
    HalfExtents[1] = NewHalfExtents[1];
    HalfExtents[2] = NewHalfExtents[2];
    Offset[0] = NewOffset[0];
    Offset[1] = NewOffset[1];
    Offset[2] = NewOffset[2];
    if (HasInstance(this))
        PlaceInstanceAtOffset(this);

    if (Hardpoint != nullptr)
    {
        // 0x00588654: the name kept at +0x1d0, the attach transform reset and filled from this
        // control's parent's hardpoint; the answer is +0x328.
        strcpy(HardpointName, Hardpoint);
        AttachOrientation[0] = 1.0f;
        AttachOrientation[1] = 0.0f;
        AttachOrientation[2] = 0.0f;
        AttachOrientation[3] = 0.0f;
        AttachOrientation[4] = 1.0f;
        AttachOrientation[5] = 0.0f;
        AttachOrientation[6] = 0.0f;
        AttachOrientation[7] = 0.0f;
        AttachOrientation[8] = 1.0f;
        AttachPosition[0] = 0.0f;
        AttachPosition[1] = 0.0f;
        AttachPosition[2] = 0.0f;
        if (Parent != nullptr)
        {
            WinControl* const ParentControl = Parent->AsWinControl();
            if (ParentControl != nullptr)
                bAttached = ParentControl->GetHardpointTransform(Hardpoint, AttachOrientation) ? 1
                                                                                               : 0;
        }
    }

    bNoZEnable = bNewNoZEnable;
    ForceRadius = static_cast<float>(NewForceRadius);
    bZClear = bNewZClear;
    bNoMeshRender = bNewNoMeshRender;
    RotationSpeed = static_cast<float>(NewRotationSpeed);
    bNoWireRender = bNewNoWireRender;
    bNoColoring = bNewNoColoring;
    bRenderToWindow = bNewRenderToWindow;
    bNoClearRender = bNewNoClearRender;
    bEnableRotation = bNewEnableRotation;
    StartYpr[0] = NewStartYpr[0];
    StartYpr[1] = NewStartYpr[1];
    StartYpr[2] = NewStartYpr[2];
    CurYaw = StartYpr[0];
    CurPitch = StartYpr[1];
    CurRoll = StartYpr[2];
}
