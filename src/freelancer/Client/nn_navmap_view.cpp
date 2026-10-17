// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// NeuroNetNavMap -- the view switches and the pieces of the frame that follow the view: the
// map meshes' pan / zoom, the grid labels, the zoom on a click, the universe / system rebuilds
// and the side panels' slide. See nn_navmap.h. Retail Client\Interface\NeuroNet\NavMap.cpp.

#include "nn_navmap.h"
#include "character_select_equip.h" // GetPlayerStarSystemIdRetail (FUN_004c4830)
#include "DALib.h"                  // DALib::Engine
#include "engine/engbase.h"         // IEngine
#include "menu_ui_init.h"           // PlayOrRewindUiSoundSlot (FUN_005645c0)
#include "rich_text_win.h"          // RichTextWin : WinControl
#include "ui_scroll.h"              // UiScroll : WinControl
#include "win3d.h"                  // kWin3DVisible
#include "common_universe.h"
#include "x86math.h"                // FTransform

#include <string.h>

namespace
{
    // DAT_005c7f68 -- 1/255 as the float 0x3b808081.
    float const kNavMapInv255 = 0.003921568859368563f;

    // FILD of the zero-extended byte, FMUL DAT_005c7f68, FSTP dword.
    float NavMapByteToUnit(unsigned char Value)
    {
        return static_cast<float>(static_cast<double>(Value) * kNavMapInv255);
    }

    // The alpha FUN_004963d0 gives a map mesh: ftol(Alpha * 255.0f) (DAT_005c75d8), its low
    // byte, back to a float through DAT_005c7f68.
    float NavMapAlphaByte(float Alpha)
    {
        int const Whole = static_cast<int>(static_cast<double>(Alpha) * 255.0);
        return NavMapByteToUnit(static_cast<unsigned char>(Whole));
    }

    // DAT_005d3e48 -- the zoomed map mesh's scale per unit of zoom (0x3dee23b8, 5/43).
    float const kNavMapZoomedMeshScale = 0.11627906560897827f;
    // DAT_005ca220.
    float const kNavMapFlatMeshScale = 1.5f;

    // FUN_0049fbf0's constants: DAT_005d3f48 / DAT_005d3f40 / DAT_005d3f44 the grid's offset
    // from the pan, DAT_005d3168 the label step, DAT_005d3164 / DAT_005d314c the first label,
    // DAT_005d3f3c / DAT_005d3f38 and DAT_005d3f34 / DAT_005d3f30 the frame's edges, the two
    // fixed rows / columns -0.294 / -0.309, and the zoomed scale 8.6.
    float const kNavMapGridOffsetX = 0.027000000700354576f;   // 0x3cdd2f1b
    float const kNavMapGridOffsetY = 0.011500000022351742f;   // 0x3c3c6a7f
    float const kNavMapGridOffsetNegY = -0.011500000022351742f; // 0xbc3c6a7f
    float const kNavMapGridStep = 0.07000000029802322f;       // 0x3d8f5c29
    float const kNavMapGridFirstLetter = 0.27900001406669617f; // 0x3e8ed917
    float const kNavMapGridFirstNumber = 0.25200000405311584f; // 0x3e810625
    float const kNavMapGridLetterMax = 0.23000000417232513f;  // 0x3e6b851f
    float const kNavMapGridLetterMin = -0.3100000023841858f;  // 0xbe9eb852
    float const kNavMapGridNumberMax = 0.27399998903274536f;  // 0x3e8c49ba
    float const kNavMapGridNumberMin = -0.25699999928474426f; // 0xbe839581
    float const kNavMapGridLetterRow = -0.29399999976158142f; // 0xbe96872b
    float const kNavMapGridNumberColumn = -0.30899998545646667f; // 0xbe9e353f
    float const kNavMapZoomedScale = 8.6000003814697266f;     // 0x4109999a

    // FUN_0049f1a0: DAT_005d3f28 (double) -- the zoomed-in distance per unit of MapExtentX.
    double const kNavMapZoomedDistance = 0.11627906976744186;
    // 0x3fc00000 -- the zoom's duration.
    float const kNavMapZoomSeconds = 1.5f;

    // DAT_005d2ed4 / DAT_005d2ed8 -- the two bases Create adds its right-panel offsets to (the
    // same constants as nn_navmap.cpp's; FUN_004a0a70 derives the panels' off-screen x from
    // them).
    float const kNavMapPanelBaseA = -0.434f; // 0xbede353f
    float const kNavMapPanelBaseB = 0.421f;  // 0x3ed78d50

    // One control FUN_004a0a70 slides: off-screen x (the panel base plus an offset, computed
    // in the FPU and stored as a float), on-screen x, and the y both share (z 0).
    struct FNavMapSlide
    {
        WinControl* Control;
        float OffX;
        float OnX;
        float Y;
    };

    // The FPU sum FUN_004a0a70 stores as a panel control's off-screen x.
    float NavMapPanelX(float Base, double Offset)
    {
        return static_cast<float>(static_cast<double>(Base) + Offset);
    }
}

// ----------------------------------------------------------------------------------------
// The map meshes and the grid
// ----------------------------------------------------------------------------------------

void NeuroNetNavMap::SetMapMeshTransform(int View, float Alpha)
{
    // FUN_004963d0.
    IEngine* const Engine = DALib::Engine;
    double const Zoom = static_cast<double>(ZoomScale);
    double const PanX = static_cast<double>(PanOffset[0]) * Zoom;
    double const PanY = static_cast<double>(PanOffset[1]) * Zoom;

    if (View == 2)
    {
        // 004963e5..004964a4: the zoomed mesh (+0x378), placed at +0x828 plus the scaled pan,
        // its rotation's [0][0] / [1][1] = ZoomScale * 5/43 (the rest of the rotation kept).
        float const Scale = static_cast<float>(Zoom * static_cast<double>(kNavMapZoomedMeshScale));
        FTransform Transform;
        memcpy(&Transform, Engine->GetInstanceTransform(ZoomedMapMesh2.EngineInstance),
               sizeof(Transform));
        Transform.Rotation.D[0][0] = Scale;
        Transform.Rotation.D[1][1] = Scale;
        Transform.Translation.X = static_cast<float>(static_cast<double>(Field_0x828[0]) + PanX);
        Transform.Translation.Y = static_cast<float>(static_cast<double>(Field_0x828[1]) + PanY);
        Transform.Translation.Z = Field_0x828[2];
        Engine->SetInstanceMatrix(ZoomedMapMesh2.EngineInstance, &Transform);

        // 004964aa..004964e9: white, the alpha through a byte.
        ZoomedMapMesh2.UserFloats[0] = 1.0f;
        ZoomedMapMesh2.UserFloats[1] = 1.0f;
        ZoomedMapMesh2.UserFloats[2] = 1.0f;
        ZoomedMapMesh2.UserFloats[4] = NavMapAlphaByte(Alpha);
        return;
    }

    // 004964f6..004965ac: the flat mesh (+0x350) at MeshPosition plus the scaled pan, scale
    // ZoomScale * 1.5.
    float const Scale = static_cast<float>(Zoom * static_cast<double>(kNavMapFlatMeshScale));
    FTransform Transform;
    memcpy(&Transform, Engine->GetInstanceTransform(ZoomedMapMesh.EngineInstance),
           sizeof(Transform));
    Transform.Rotation.D[0][0] = Scale;
    Transform.Rotation.D[1][1] = Scale;
    Transform.Translation.X = static_cast<float>(static_cast<double>(MeshPosition[0]) + PanX);
    Transform.Translation.Y = static_cast<float>(static_cast<double>(MeshPosition[1]) + PanY);
    Transform.Translation.Z = MeshPosition[2];
    Engine->SetInstanceMatrix(ZoomedMapMesh.EngineInstance, &Transform);

    // 004965b2..004965ef
    ZoomedMapMesh.UserFloats[0] = 1.0f;
    ZoomedMapMesh.UserFloats[1] = 1.0f;
    ZoomedMapMesh.UserFloats[2] = 1.0f;
    ZoomedMapMesh.UserFloats[4] = NavMapAlphaByte(Alpha);
}

void NeuroNetNavMap::PlaceGridLabels()
{
    // FUN_0049fbf0. A label's place is From + (To - From) * Ease, each end = (the grid's
    // offset + the label's step) * the end's scale.
    float ScaleFrom = 1.0f;
    float ScaleTo = 1.0f;
    float Ease = 0.0f;
    float FromX = 0.0f;
    float FromY = 0.0f;
    float ToX = 0.0f;
    float ToY = 0.0f;

    if (bTransition != 0)
    {
        Ease = TransitionEase;
        if (ViewMode == 2)
        {
            // 0049fc49..0049fc77 / 0049fcee: zooming out of view 2.
            FromX = static_cast<float>(static_cast<double>(kNavMapGridOffsetX) -
                                       static_cast<double>(TransitionDelta[0]));
            ScaleTo = 1.0f;
            FromY = static_cast<float>(static_cast<double>(kNavMapGridOffsetNegY) -
                                       static_cast<double>(TransitionDelta[1]));
            ScaleFrom = kNavMapZoomedScale;
        }
        else
        {
            // 0049fc79..0049fcc3: zooming in.
            ScaleFrom = 1.0f;
            ScaleTo = kNavMapZoomedScale;
            ToX = static_cast<float>(static_cast<double>(TransitionDelta[0]) +
                                     static_cast<double>(kNavMapGridOffsetX));
            FromX = kNavMapGridOffsetX;
            FromY = kNavMapGridOffsetNegY;
            ToY = static_cast<float>(static_cast<double>(TransitionDelta[1]) -
                                     static_cast<double>(kNavMapGridOffsetY));
        }
    }
    else if (ViewMode == 2)
    {
        // 0049fcca..0049fcf2: zoomed in, following the pan.
        ScaleTo = kNavMapZoomedScale;
        FromX = static_cast<float>(static_cast<double>(PanOffset[0]) +
                                   static_cast<double>(kNavMapGridOffsetX));
        FromY = static_cast<float>(static_cast<double>(PanOffset[1]) -
                                   static_cast<double>(kNavMapGridOffsetY));
        ScaleFrom = kNavMapZoomedScale;
    }

    // 0049fcfa..0049fdae: the letters A..H along the bottom. TEST AH,0x41 + JE hides one right
    // of 0.23; TEST AH,5 + JNP hides one left of -0.31 (an unordered x stays shown).
    for (int Index = 0; Index < 8; ++Index)
    {
        double const Step = static_cast<double>(Index) * static_cast<double>(kNavMapGridStep);
        double const A = ((static_cast<double>(FromX) + Step) -
                          static_cast<double>(kNavMapGridFirstLetter)) *
                         static_cast<double>(ScaleFrom);
        double const B = ((Step + static_cast<double>(ToX)) -
                          static_cast<double>(kNavMapGridFirstLetter)) *
                         static_cast<double>(ScaleTo);
        float const X = static_cast<float>((B - A) * static_cast<double>(Ease) + A);
        WinControl* const Label = GridLetters[Index];
        if (X > kNavMapGridLetterMax || X < kNavMapGridLetterMin)
        {
            Label->SetProperty(1, 1, 0);
            continue;
        }
        Label->SetProperty(1, 0, 0);
        float const Pos[3] = {X, kNavMapGridLetterRow, 0.0f};
        Label->SetProperty(6, reinterpret_cast<uintptr_t>(Pos), 0);
    }

    // 0049fdb4..0049fe62: the numbers 1..8 down the left side.
    for (int Index = 0; Index < 8; ++Index)
    {
        double const Step = static_cast<double>(kNavMapGridFirstNumber) -
                            static_cast<double>(Index) * static_cast<double>(kNavMapGridStep);
        double const A = (static_cast<double>(FromY) + Step) * static_cast<double>(ScaleFrom);
        double const B = (Step + static_cast<double>(ToY)) * static_cast<double>(ScaleTo);
        float const Y = static_cast<float>((B - A) * static_cast<double>(Ease) + A);
        WinControl* const Label = GridNumbers[Index];
        if (Y > kNavMapGridNumberMax || Y < kNavMapGridNumberMin)
        {
            Label->SetProperty(1, 1, 0);
            continue;
        }
        Label->SetProperty(1, 0, 0);
        float const Pos[3] = {kNavMapGridNumberColumn, Y, 0.0f};
        Label->SetProperty(6, reinterpret_cast<uintptr_t>(Pos), 0);
    }
}

void NeuroNetNavMap::ZoomToClick()
{
    // FUN_0049f1a0.
    HideObjectTools(); // FUN_0049f0e0

    // 0049f1ab..0049f25d: the pan that puts the click at the centre, less the current pan.
    double const HalfX = static_cast<double>(MapSize[0]) * 0.5;
    double const HalfY = static_cast<double>(MapSize[1]) * 0.5;
    TransitionDelta[2] = 0.0f;
    TransitionDelta[0] = static_cast<float>(
        HalfX - static_cast<double>(ClickU) * static_cast<double>(MapSize[0]));
    TransitionDelta[1] = static_cast<float>(
        HalfY - (1.0 - static_cast<double>(ClickV)) * static_cast<double>(MapSize[1]));
    TransitionDelta[0] = static_cast<float>(static_cast<double>(TransitionDelta[0]) -
                                            static_cast<double>(PanOffset[0]));
    TransitionDelta[1] = static_cast<float>(static_cast<double>(TransitionDelta[1]) -
                                            static_cast<double>(PanOffset[1]));
    TransitionDelta[2] = static_cast<float>(static_cast<double>(TransitionDelta[2]) -
                                            static_cast<double>(PanOffset[2]));

    // 0049f25d..0049f2c4: a 1.5 s view-2 transition from the current pan; the distance goes
    // from MapExtentX / ZoomScale to MapExtentX * 5/43.
    TransitionTime = 0.0f;
    TransitionStart[0] = PanOffset[0];
    TransitionStart[1] = PanOffset[1];
    bTransition = 1;
    TransitionView = 2;
    TransitionDuration = kNavMapZoomSeconds;
    TransitionStart[2] = PanOffset[2];
    double const Distance = static_cast<double>(MapExtentX) / static_cast<double>(ZoomScale);
    TransitionDelta[2] = static_cast<float>(
        static_cast<double>(MapExtentX) * kNavMapZoomedDistance - Distance);
    ZoomDistance = static_cast<float>(Distance);

    PlayOrRewindUiSoundSlot(6); // FUN_005645c0(6)
}

// ----------------------------------------------------------------------------------------
// View rebuilds
// ----------------------------------------------------------------------------------------

void NeuroNetNavMap::EnterSystemView()
{
    // FUN_0049e1d0.
    if (G_NavMapFilterMode == 6)
        SetFilterMode(FilterModeBeforeInfoCard);
    ViewMode = 1;
    CachedObjectId = 0xffffffffu;

    // 0049e209..0049e246: the jump links, their records freed (FUN_0053e430 / FUN_00516f70).
    while (!JumpLinks.empty())
    {
        FNavMapJumpLink* const Link = JumpLinks.front();
        operator delete(Link);
        JumpLinks.pop_front();
    }

    // 0049e248..0049e294: every item (FUN_0048dd40 + operator delete on a non-null one).
    while (!Items.empty())
    {
        FNavMapItem* const Item = Items.front();
        if (Item != nullptr)
            delete Item;
        Items.pop_front();
    }

    // 0049e297..0049e2b1
    for (int Index = 0; Index < 0x63; ++Index)
        WaypointItems[Index] = nullptr;
    for (int Index = 0; Index < 0x63; ++Index)
        BestPathItems[Index] = nullptr;

    BuildSystemItems(); // FUN_004917c0
    AddPlayerMarker();  // FUN_00491410
    UniverseButton->SetProperty(1, 0, 0);

    // 0049e2d5..0049e301: the row hides with no system or one of the four hidden ones.
    unsigned char bHidden = 1;
    if (CurrentSystem != nullptr)
    {
        unsigned const Id = CurrentSystem->GetId();
        bHidden = 0;
        for (int Index = 0; Index < 4; ++Index)
        {
            if (SkipSystemIds[Index] == Id)
            {
                bHidden = 1;
                break;
            }
        }
    }

    LabelButton->SetProperty(1, bHidden, 0);
    PhysicalFilter->SetProperty(1, bHidden, 0);
    PoliticalFilter->SetProperty(1, bHidden, 0);
    PatrolFilter->SetProperty(1, bHidden, 0);
    MineableFilter->SetProperty(1, bHidden, 0);

    // 0049e363..0049e396: the mission filter needs mission zones (a null vector counts as
    // none).
    unsigned const bNoMission = (MissionZoneIds.empty() || bHidden != 0) ? 1u : 0u;
    MissionFilter->SetProperty(1, bNoMission, 0);

    // 0049e39c..0049e41c: the political sub-filters only in modes 1 / 2.
    if (G_NavMapFilterMode == 1 || G_NavMapFilterMode == 2)
    {
        AllFilter->SetProperty(1, bHidden, 0);
        HostileFilter->SetProperty(1, bHidden, 0);
        FriendlyFilter->SetProperty(1, bHidden, 0);
    }
    else
    {
        AllFilter->SetProperty(1, 1, 0);
        HostileFilter->SetProperty(1, 1, 0);
        FriendlyFilter->SetProperty(1, 1, 0);
    }

    FilterTitle->SetProperty(1, bHidden, 0);
    LegendFilter->SetProperty(1, bHidden, 0);
    BaseListFilter->SetProperty(1, bHidden, 0);
    for (int Index = 0; Index < 4; ++Index)
        FactionLabels[Index]->SetProperty(1, 1, 0);

    // 0049e4ab..0049e546: no pan, the flat mesh at full alpha, the items and the grid placed,
    // then every grid label shown.
    PanOffset[0] = 0.0f;
    PanOffset[1] = 0.0f;
    PanOffset[2] = 0.0f;
    SetMapMeshTransform(ViewMode, 1.0f); // FUN_004963d0
    PlaceItems();                        // FUN_00496600
    PlaceGridLabels();                   // FUN_0049fbf0
    for (int Index = 0; Index < 8; ++Index)
        GridLetters[Index]->SetProperty(1, 0, 0);
    for (int Index = 0; Index < 8; ++Index)
        GridNumbers[Index]->SetProperty(1, 0, 0);
}

void NeuroNetNavMap::EnterUniverseView()
{
    // FUN_0049e550.
    ViewMode = 0;
    if (G_NavMapFilterMode == 4 || G_NavMapFilterMode == 7)
        SetFilterMode(0);
    else if (G_NavMapFilterMode == 6)
        SetFilterMode(FilterModeBeforeInfoCard);

    // 0049e589..0049e5d5 / 0049e5d7..0049e61b: the jump links (records freed) and the items.
    while (!JumpLinks.empty())
    {
        FNavMapJumpLink* const Link = JumpLinks.front();
        operator delete(Link);
        JumpLinks.pop_front();
    }
    while (!Items.empty())
    {
        FNavMapItem* const Item = Items.front();
        if (Item != nullptr)
            delete Item;
        Items.pop_front();
    }
    for (int Index = 0; Index < 0x63; ++Index)
        WaypointItems[Index] = nullptr;
    for (int Index = 0; Index < 0x63; ++Index)
        BestPathItems[Index] = nullptr;

    BuildUniverseItems(); // FUN_004915b0
    AddPlayerMarker();    // FUN_00491410

    // 0049e647..0049e692: the player marker onto the player's system's star (no null test of
    // PlayerItem or of the star's system, as in retail).
    unsigned const PlayerSystem = GetPlayerStarSystemIdRetail();
    for (std::list<FNavMapItem*>::iterator It = Items.begin(); It != Items.end(); ++It)
    {
        FNavMapItem* const Item = *It;
        if (Item->Kind != 0)
            continue;
        if (Item->System->GetId() != PlayerSystem)
            continue;
        PlayerItem->Position[0] = Item->Position[0];
        PlayerItem->Position[1] = Item->Position[1];
        PlayerItem->Position[2] = Item->Position[2];
        break;
    }

    // 0049e695..0049e7e3: the filter row hidden, the faction labels shown.
    LabelButton->SetProperty(1, 1, 0);
    UniverseButton->SetProperty(1, 1, 0);
    PhysicalFilter->SetProperty(1, 1, 0);
    PoliticalFilter->SetProperty(1, 1, 0);
    PatrolFilter->SetProperty(1, 1, 0);
    MissionFilter->SetProperty(1, 1, 0);
    MineableFilter->SetProperty(1, 1, 0);
    LegendFilter->SetProperty(1, 1, 0);
    BaseListFilter->SetProperty(1, 1, 0);
    AllFilter->SetProperty(1, 1, 0);
    HostileFilter->SetProperty(1, 1, 0);
    FriendlyFilter->SetProperty(1, 1, 0);
    FilterTitle->SetProperty(1, 1, 0);
    for (int Index = 0; Index < 4; ++Index)
        FactionLabels[Index]->SetProperty(1, 0, 0);

    // 0049e7e9..0049e87a: no pan, the items placed, the grid hidden, the title "Universe"
    // (ids 0x505).
    PanOffset[0] = 0.0f;
    PanOffset[1] = 0.0f;
    PanOffset[2] = 0.0f;
    PlaceItems(); // FUN_00496600
    for (int Index = 0; Index < 8; ++Index)
        GridLetters[Index]->SetProperty(1, 1, 0);
    for (int Index = 0; Index < 8; ++Index)
        GridNumbers[Index]->SetProperty(1, 1, 0);
    SystemNameText->SetProperty(0x1e, 0x505, 0);
}

// ----------------------------------------------------------------------------------------
// Side panels
// ----------------------------------------------------------------------------------------

void NeuroNetNavMap::SyncSidePanels()
{
    // FUN_004a1620 -- not while a slide runs.
    if (ButtonMover::IsActive()) // FUN_0055e280
        return;

    // 004a1639..004a16ad: the eight waypoint / abort controls, scanned up to the first null.
    WinControl* const Controls[9] = {ClearAllWaypointsButton, ClearAllWaypointsText,
                                     AbortMissionButton,      AbortMissionText,
                                     AbortYesButton,          AbortYesText,
                                     AbortNoButton,           AbortNoText,
                                     nullptr};
    unsigned char bAnyShown = 0;
    for (int Index = 0; Controls[Index] != nullptr; ++Index)
    {
        if ((Controls[Index]->Flags & kWin3DVisible) != 0)
        {
            bAnyShown = 1;
            break;
        }
    }

    // 004a16af..004a16e0: the objective rows (each null-tested), then the info text (not).
    if (bAnyShown == 0)
    {
        for (int Index = 0; Index < 3; ++Index)
        {
            WinControl* const Objective = MissionObjective[Index];
            if (Objective != nullptr && (Objective->Flags & kWin3DVisible) != 0)
            {
                bAnyShown = 1;
                break;
            }
            WinControl* const Text = ObjectiveText[Index];
            if (Text != nullptr && (Text->Flags & kWin3DVisible) != 0)
            {
                bAnyShown = 1;
                break;
            }
        }
    }
    if (bAnyShown == 0 && (static_cast<WinControl*>(InfoText)->Flags & kWin3DVisible) != 0)
        bAnyShown = 1;

    if (bAnyShown != bSidePanelsOpen)
        SlideSidePanels(bAnyShown); // FUN_004a0a70
}

void NeuroNetNavMap::SlideSidePanels(unsigned char bOpen)
{
    // FUN_004a0a70.
    if (bOpen == bSidePanelsOpen)
        return;
    PlayOrRewindUiSoundSlot(5);         // FUN_005645c0(5)
    ButtonMover::Start(0.4f);           // FUN_0055e120(0.4)
    bSidePanelsOpen = bOpen;

    // 004a0aa6..004a0b3d / 004a105f..004a10b4: the two side windows (z -0.22). Opening, the
    // left one comes in from 1.045 to 0.657 and the right one goes out from 0.19 to 0.585;
    // closing reverses both.
    float const LeftOut[3] = {1.045f, -0.23f, -0.22f};
    float const LeftIn[3] = {0.657f, -0.23f, -0.22f};
    float const RightIn[3] = {0.19f, -0.23f, -0.22f};
    float const RightOut[3] = {0.585f, -0.23f, -0.22f};
    if (bOpen != 0)
    {
        ButtonMover::AddButton(LeftWindow, LeftOut, LeftIn);
        ButtonMover::AddButton(RightWindow, RightIn, RightOut);
    }
    else
    {
        ButtonMover::AddButton(LeftWindow, LeftIn, LeftOut);
        ButtonMover::AddButton(RightWindow, RightOut, RightIn);
    }

    // 004a0b42..004a1604 (open) / 004a10b9..004a1604 (close): every panel control, in this
    // order, between its off-screen x (a panel base plus an offset, rounded to a float) and its
    // place; opening moves it in, closing out.
    // The offsets are the float constants FSUB / FADD read: DAT_005d33dc, DAT_005d33b8,
    // DAT_005d3410, DAT_005d2ac0, DAT_005d3398, DAT_005d3378, DAT_005d335c, DAT_005d3340,
    // DAT_005d3328, DAT_005d3310, DAT_005d32ac, DAT_005d3260, DAT_005d323c.
    // The on-screen places are MOV immediates; six of them are one ulp away from the nearest
    // float to their decimal (0x3ea76c8c, 0x3efe76c8, 0x3eba5e36, 0x3eb22d0f, 0x3e808313,
    // 0xbc9ba5e4), so those are spelled with their exact value.
    FNavMapSlide const Slides[17] = {
        {ClearAllWaypointsButton, NavMapPanelX(kNavMapPanelBaseB, -static_cast<double>(0.516f)),
         -0.516f, -0.237f},
        {ClearAllWaypointsText, NavMapPanelX(kNavMapPanelBaseB, -static_cast<double>(0.659f)),
         -0.659f, -0.209f},
        {InfoText, NavMapPanelX(kNavMapPanelBaseA, static_cast<double>(0.93f)), 0.897f, -0.06f},
        {InfoTitle, NavMapPanelX(kNavMapPanelBaseA, static_cast<double>(0.36f)),
         0.3270000219345093f, 0.256f},
        {AbortMissionButton, NavMapPanelX(kNavMapPanelBaseA, static_cast<double>(0.53f)),
         0.49699997901916504f, -0.23f},
        {AbortMissionText, NavMapPanelX(kNavMapPanelBaseA, static_cast<double>(0.397f)),
         0.36400002241134644f, -0.206f},
        {AbortYesButton, NavMapPanelX(kNavMapPanelBaseA, static_cast<double>(0.447f)), 0.414f,
         -0.23f},
        {AbortYesText, NavMapPanelX(kNavMapPanelBaseA, static_cast<double>(0.381f)),
         0.34800001978874207f, -0.206f},
        {AbortNoButton, NavMapPanelX(kNavMapPanelBaseA, static_cast<double>(0.617f)), 0.584f,
         -0.23f},
        {AbortNoText, NavMapPanelX(kNavMapPanelBaseA, static_cast<double>(0.55f)), 0.517f,
         -0.206f},
        {MissionObjective[0], NavMapPanelX(kNavMapPanelBaseB, -static_cast<double>(0.547f)),
         -0.517f, 0.181f},
        {MissionObjective[1], NavMapPanelX(kNavMapPanelBaseB, -static_cast<double>(0.547f)),
         -0.517f, 0.046f},
        {MissionObjective[2], NavMapPanelX(kNavMapPanelBaseB, -static_cast<double>(0.547f)),
         -0.517f, -0.089f},
        {ObjectiveScroll, NavMapPanelX(kNavMapPanelBaseB, static_cast<double>(0.3261f)), 0.3261f,
         -0.058f},
        {ObjectiveText[0], NavMapPanelX(kNavMapPanelBaseB, -static_cast<double>(0.686f)),
         -0.656f, 0.2510000169277191f},
        {ObjectiveText[1], NavMapPanelX(kNavMapPanelBaseB, -static_cast<double>(0.686f)),
         -0.656f, 0.116f},
        {ObjectiveText[2], NavMapPanelX(kNavMapPanelBaseB, -static_cast<double>(0.686f)),
         -0.656f, -0.01900000125169754f},
    };

    for (int Index = 0; Index < 17; ++Index)
    {
        FNavMapSlide const& Slide = Slides[Index];
        float const Off[3] = {Slide.OffX, Slide.Y, 0.0f};
        float const On[3] = {Slide.OnX, Slide.Y, 0.0f};
        if (bOpen != 0)
            ButtonMover::AddButton(Slide.Control, Off, On);
        else
            ButtonMover::AddButton(Slide.Control, On, Off);
    }
}
