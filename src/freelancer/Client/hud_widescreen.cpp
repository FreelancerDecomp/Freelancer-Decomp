// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Port-only: the in-flight HUD's corner windows at the edges of a widescreen surface. See
// hud_widescreen.h; nothing here exists in retail.

#include "hud_widescreen.h"

#ifdef FREELANCER_WIDESCREEN_SUPPORT

#include "button_mover.h"        // ButtonMover::IsActive
#include "hud_comm.h"            // HUD_Receiver
#include "input_manager.h"       // GetClientSoftwareCursor
#include "nn_main_button.h"      // NN_MainButton
#include "space_camera.h"
#include "type_select.h"         // TypeSelect (the main menu column)
#include "widescreen.h"
#include "win3d.h"
#include "win3d_render.h"        // Win3DApplyCamera
#include "win3d_renderables.h"   // the immediate and montage batches

#include <math.h>
#include <string.h>

namespace
{
    // The shift on the Win3D camera now, and the camera state it replaced.
    float G_HudShift = 0.0f;
    float G_BaseScreenCenterX = 0.0f;
    float G_BaseView[9] = {};
    float G_BaseViewPos[3] = {};

    // Children that stay centred inside a shifted window: HUD_Status's special-mode captions
    // (hud_status_core.cpp) span the whole screen from x = -0.513; NN_MainButton's
    // "ScaledBackground" grows into the frame of the centred NeuroNet screen.
    bool IsCentredChild(Win3D const* Object)
    {
        return strcmp(Object->Name, "SpecialModeText") == 0 ||
               strcmp(Object->Name, "SpecialModeTextCancel") == 0 ||
               strcmp(Object->Name, "ScaledBackground") == 0;
    }

    bool ChainContains(Win3D const* Object, Win3D const* Root, Win3D const* Wanted)
    {
        if (Wanted == nullptr)
            return false;
        for (Win3D const* Walk = Object; Walk != nullptr && Walk != Root; Walk = Walk->Parent)
        {
            if (Walk == Wanted)
                return true;
        }
        return false;
    }

    // The NeuroNet buttons (top right). The bar sits at the edge; a button that opens a panel
    // flies from the bar into the header of the centred NeuroNet screen (the panel window is
    // reparented under it, nn_main_button.cpp FUN_0045d370), so its share of the shift runs out
    // with the move, and a closing one gets it back on the way home. Returns that share.
    float NeuroNetShare(NN_MainButton const* Main, Win3D const* Object)
    {
        ButtonMover const* const Mover = Main;
        if (ChainContains(Object, Main, Main->OpenButton))
            return Mover->IsActive() ? 1.0f - Mover->GetProgress() : 0.0f;
        if (ChainContains(Object, Main, Main->ClosingButton))
            return Mover->IsActive() ? Mover->GetProgress() : 0.0f;
        return 1.0f;
    }

    // The signed share of the spare width Object moves by: +1 right, -1 left, 0 not in a corner
    // window (Root is Object's root).
    float CornerShare(Win3D const* Root, Win3D const* Object)
    {
        if (strcmp(Root->Name, "HUD_Status") == 0)
            return 1.0f;
        if (strcmp(Root->Name, "HUD_Target") == 0 || strcmp(Root->Name, "HUD_ContactList") == 0)
            return -1.0f;
        if (dynamic_cast<HUD_Receiver const*>(Root) != nullptr)
            return -1.0f;
        if (NN_MainButton const* const Main = dynamic_cast<NN_MainButton const*>(Root))
            return NeuroNetShare(Main, Object);

        // The main menu column: the buttons to the left, the "Version" caption to the right.
        if (dynamic_cast<TypeSelect const*>(Root) != nullptr)
        {
            for (Win3D const* Walk = Object; Walk != nullptr && Walk != Root; Walk = Walk->Parent)
            {
                if (strcmp(Walk->Name, "Version") == 0)
                    return 1.0f;
            }
            return -1.0f;
        }
        return 0.0f;
    }

    void RestoreCamera(FSpaceCamera* Cam)
    {
        memcpy(Cam->View, G_BaseView, sizeof(G_BaseView));
        memcpy(Cam->ViewPos, G_BaseViewPos, sizeof(G_BaseViewPos));
        Cam->ScreenCenterX = G_BaseScreenCenterX;
        // Rebuilt from the untouched world transform on the next read, in case the camera
        // moved while it was shifted.
        Cam->bViewDirty = 1;
    }

    void SetShift(float ShiftPx)
    {
        FSpaceCamera* const Cam = Win3DGetCamera();
        if (G_HudShift != 0.0f)
            RestoreCamera(Cam);
        G_HudShift = ShiftPx;
        if (ShiftPx == 0.0f)
            return;

        // The view is rebuilt first when dirty, then kept clean while sheared: v = V*w + T with
        // the rows as output components (FUN_0040f390), so x' = x - s*z is row 0 minus s times
        // row 2. A point at depth d = -z moves s*d sideways, which XScale (pixels per unit at
        // depth 1) turns into ShiftPx on screen at every depth.
        Cam->GetViewTransform();
        memcpy(G_BaseView, Cam->View, sizeof(G_BaseView));
        memcpy(G_BaseViewPos, Cam->ViewPos, sizeof(G_BaseViewPos));
        G_BaseScreenCenterX = Cam->ScreenCenterX;

        double const S = static_cast<double>(ShiftPx) / static_cast<double>(Cam->XScale);
        for (int Column = 0; Column < 3; ++Column)
        {
            Cam->View[Column] = static_cast<float>(static_cast<double>(Cam->View[Column]) -
                                                   S * static_cast<double>(Cam->View[6 + Column]));
        }
        Cam->ViewPos[0] = static_cast<float>(static_cast<double>(Cam->ViewPos[0]) -
                                             S * static_cast<double>(Cam->ViewPos[2]));
        // Project and Unproject go through the world transform and the screen centre.
        Cam->ScreenCenterX = static_cast<float>(static_cast<double>(Cam->ScreenCenterX) + ShiftPx);
    }

    // The world-space immediate batch and the montage buttons are flushed at the end of the
    // frame with whatever view is current then; what was queued under the outgoing state goes
    // out now (the same flushes FUN_0045a9f0 makes mid-frame, without its overlay setup).
    void FlushQueuedWin3DBatches()
    {
        Win3DImmedBatchFlushAll();
        Win3DImmedBatchReset();
        ButtonMontageFlushBatch();
        ButtonMontageEnsure();
    }
}

float WidescreenHudShiftPixelsFor(Win3D const* Object)
{
    if (Object == nullptr)
        return 0.0f;
    Win3D const* Root = Object;
    for (Win3D const* Walk = Object; Walk != nullptr; Walk = Walk->Parent)
    {
        if (IsCentredChild(Walk))
            return 0.0f;
        Root = Walk;
    }
    float const Share = CornerShare(Root, Object);
    if (Share == 0.0f)
        return 0.0f;
    // Half of the width the 4:3 interface leaves free, in whole pixels.
    double const Spare = (static_cast<double>(G_RenderSurfaceWidth) -
                          WidescreenEquivalentSurfaceWidth()) * 0.5;
    if (!(Spare >= 1.0))
        return 0.0f;
    return static_cast<float>(floor(static_cast<double>(Share) * floor(Spare) + 0.5));
}

float WidescreenHudCurrentShift()
{
    return G_HudShift;
}

bool WidescreenHudShearModelview(float const* In12, float* Out12)
{
    if (G_HudShift == 0.0f)
        return false;
    FSpaceCamera const* const Cam = Win3DGetCamera();
    double const S = static_cast<double>(G_HudShift) / static_cast<double>(Cam->XScale);
    memcpy(Out12, In12, sizeof(float) * 12);
    for (int Column = 0; Column < 3; ++Column)
    {
        Out12[Column] = static_cast<float>(static_cast<double>(In12[Column]) -
                                           S * static_cast<double>(In12[6 + Column]));
    }
    Out12[9] = static_cast<float>(static_cast<double>(In12[9]) - S * static_cast<double>(In12[11]));
    return true;
}

void WidescreenHudCursorRay(float* OutRay3)
{
    // The mouse-move ray of the Win3D input handler (0x0059d22c): the cursor onto the near
    // plane, through the camera orientation, normalised.
    int CursorX = 0;
    int CursorY = 0;
    GetClientSoftwareCursor(&CursorX, &CursorY, nullptr);
    FSpaceCamera* const Cam = Win3DGetCamera();
    FVector Local;
    Win3DScreenToNearPlane(Cam, Local, static_cast<float>(CursorX), static_cast<float>(CursorY));
    FMatrix Orientation;
    memcpy(&Orientation, Cam->World, sizeof(Orientation));
    FVector World;
    Win3DTransformByMatrix(World, Orientation, Local);
    double const Length = sqrt(static_cast<double>(World.X) * World.X +
                               static_cast<double>(World.Y) * World.Y +
                               static_cast<double>(World.Z) * World.Z);
    double const Inverse = Length > 0.0 ? 1.0 / Length : 0.0;
    OutRay3[0] = static_cast<float>(World.X * Inverse);
    OutRay3[1] = static_cast<float>(World.Y * Inverse);
    OutRay3[2] = static_cast<float>(World.Z * Inverse);
}

FWidescreenHudShiftScope::FWidescreenHudShiftScope(float ShiftPx, bool bDrawIn)
    : PreviousShift(G_HudShift)
    , bDraw(bDrawIn)
    , bChanged(ShiftPx != G_HudShift)
{
    if (!bChanged)
        return;
    if (bDraw)
        FlushQueuedWin3DBatches();
    SetShift(ShiftPx);
    if (bDraw)
        Win3DApplyCamera(AsCamera(Win3DGetCamera()));
}

FWidescreenHudShiftScope::~FWidescreenHudShiftScope()
{
    if (!bChanged)
        return;
    if (bDraw)
        FlushQueuedWin3DBatches();
    SetShift(PreviousShift);
    if (bDraw)
        Win3DApplyCamera(AsCamera(Win3DGetCamera()));
}

#endif
