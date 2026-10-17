// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// NeuroNetNavMap -- lifecycle, Create and the WinControl overrides (see nn_navmap.h).
// Retail Client\Interface\NeuroNet\NavMap.cpp, 0x0048d700..0x004a3f90.

#include "nn_navmap.h"
#include "best_path.h"               // BestPathGetWaypointCount (FUN_004c4fb0)
#include "character_select_equip.h"  // GetPlayerStarSystemIdRetail, GetPlayerVisitFlagsRetail
#include "font_string.h"             // FFontString (the item label's dtor)
#include "gameflow.h"                // IsMissionBriefingNavMapUp / RequestMissionBriefingContinue
#include "hud_command_dispatch.h"    // HudPostCommand (FUN_005763b0)
#include "ids_strings.h"             // GetIdsStringWWithRawFallback (FUN_004347e0)
#include "login_mgr.h"               // G_Server (DAT_0067ecd0)
#include "nn_modal.h"                // NnFloatBits
#include "nn_text_control.h"         // NnTextControl (+0x334 Font::String)
#include "nn_text_scratch.h"         // DAT_0066dc60 / DAT_0066fc60 / DAT_006119f8
#include "nn_ui_pulse.h"             // palette colours DAT_00679ba4 / DAT_00679bb0
#include "player_spawn.h"            // GetPlayerConnectionId, GetPlayerWaypointCountRetail
#include "rich_text_win.h"
#include "ui_scroll.h"
#include "win3d.h"                   // Win3DReparent, Win3DDeleteTree
#include "win3d_engine_submit.h"     // Win3DRayHitsAabbHalfExtents (FUN_005a1130)
#include "win_factory.h"
#include "x86math.h"                 // FTransform, I3DMathEngine
#include "common_csys.h"             // Acquire3DMathEngine (FUN_005b73e0)
#include "common_id.h"               // CreateID
#include "common_server.h"           // IServer
#include "common_misc.h"             // SinglePlayer, GetFileSysPath
#include "common_universe.h"
#include "dacom.h"
#include "DALib.h"                   // DALib::Engine / Pipeline / Data
#include "engine/engbase.h"          // IEngine
#include "render/IRenderPipeline.h" // IRPVertexBuffer / IRPIndexBuffer / IRPDraw

#include <io.h>                      // _access
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>                 // CharUpperW (FUN_005b74d9)

// --- Globals (retail NavMap.cpp .bss / .data) ---
int G_NavMapFilterMode = 0;               // DAT_006720a0
int G_NavMapZoneFilter = 0;               // DAT_006720a4
unsigned char G_bNavMapNoBestPath = 0;    // DAT_006720a8
// DAT_00672048 -- LAB_0048d730 constructs it empty and registers LAB_0048d760 (free) with atexit.
std::vector<FNavMapGroupMember> G_NavMapGroupPositions;
unsigned G_NavMapRequestedSystem = 0;     // DAT_006720c4
float G_NavMapRequestedPos[3] = {0.0f, 0.0f, 0.0f}; // DAT_00672084..8c
unsigned char G_bNavMapRequestedPos = 0;  // DAT_006720c8
// DAT_0067203c..44 -- the XC-table initializer LAB_00494900 stores 0.005f / 0.048f / 0.
float G_NavMapGlobal_67203c[3] = {0.005f, 0.048f, 0.0f};
unsigned char G_bNavMapRouteChanged = 0;  // DAT_006720c9
unsigned char G_bNavMapShowLabels = 1;    // DAT_00611c5c

// DAT_0067209c -- the lazily acquired I3DMathEngine (FUN_005b73e0), shared by every function
// of the file.
I3DMathEngine* G_NavMapMath = nullptr;
// DAT_006720ac / DAT_006720b0 / DAT_006720b4 -- the pipeline interfaces the ctor queries once.
IRPVertexBuffer* G_NavMapVertexBuffer = nullptr;
IRPIndexBuffer* G_NavMapIndexBuffer = nullptr;
IRPDraw* G_NavMapDraw = nullptr;
// DAT_006720b8 / DAT_006720bc / DAT_006720c0 -- the vertex buffers of the terrain patches
// (FUN_0049c260), the zone shading (FUN_0049dcc0) and the trade lanes (FUN_004992b0); the dtor
// destroys all three through G_NavMapVertexBuffer's DestroyVb. The index data goes through the
// index buffer's shared scratch (handle -1), which owns no handle here.
void* G_NavMapBufferHandles[3] = {nullptr, nullptr, nullptr};

void NavMapScaleMeshRotation(FMeshHolder* Holder, float Factor)
{
    // Inlined in retail (e.g. 0x0048f5db..0x0048f644, 0x00491534..0x00491591): IEngine slot 40
    // GetInstanceTransform copied (0x30 bytes, REP MOVSD 0xc), DAT_0067209c acquired on first
    // use, I3DMathEngine slot 4 Scale(rotation, rotation, Factor), IEngine slot 39
    // SetInstanceMatrix. No test of the instance handle.
    IEngine* const Engine = DALib::Engine;
    FTransform Transform;
    memcpy(&Transform, Engine->GetInstanceTransform(Holder->EngineInstance), sizeof(Transform));
    if (G_NavMapMath == nullptr)
        G_NavMapMath = Acquire3DMathEngine();
    G_NavMapMath->Scale(Transform.Rotation, Transform.Rotation, Factor);
    Engine->SetInstanceMatrix(Holder->EngineInstance, &Transform);
}

namespace
{
    // WINFACTORY<NeuroNetNavMap> "NeuroNetNavMap" (static initializer LAB_0048d700 ->
    // FUN_004a2dd0, atexit LAB_0048d720; Create FUN_004a2e40).
    WINFACTORY<NeuroNetNavMap> G_NeuroNetNavMapFactory("NeuroNetNavMap");

    // DAT_005d2ed4 / DAT_005d2ed8 -- the two bases Create adds its right-panel offsets to.
    float const kNavMapPanelBaseA = -0.434f; // 0xbede353f
    float const kNavMapPanelBaseB = 0.421f;  // 0x3ed78d50

    char const kNavMapSpaceObjectsDir[] = "Interface\\NeuroNet\\NavMap\\NewNavMap\\SpaceObjects\\";
    char const kNavMapSpaceObjectsMat[] =
        "Interface\\NeuroNet\\NavMap\\NewNavMap\\SpaceObjects\\spaceobjects.mat";

    // DAT_00611c60 -- the terrain shape names (the last entry repeats nav_terrain_ice).
    char const* const kNavMapTerrainShapeNames[9] = {
        "asteroidtest",         "nav_terrain_debris", "dustcloud",
        "nav_terrain_mines",    "nav_terrain_badlands", "nav_terrain_ice",
        "nav_terrain_lava",     "nav_terrain_nomadast", "nav_terrain_ice"};

    // 0x0048f5db..0x0048f644, repeated for the seven tool buttons: the button's instance
    // transform, its rotation scaled by 1.7 through I3DMathEngine::Scale (slot 4), written
    // back with SetInstanceMatrix. The button is not null-tested (neither is it in retail).
    void ScaleToolButtonMesh(WinControl* Button)
    {
        NavMapScaleMeshRotation(&Button->MeshHolder, 1.7f);
    }

    // 0x00490249..0x00490262 (and the two copies after it): the objective line's Font::String
    // goes multi-line, dropping its layout when it changes.
    void SetObjectiveTextMultiLine(WinControl* Text)
    {
        FFontString* const Font = static_cast<NnTextControl*>(Text)->GetFontString();
        if (Font->bMultiLine != 1)
        {
            Font->bMultiLine = 1;
            Font->LayoutGen = 0;
        }
    }
}

// ----------------------------------------------------------------------------------------
// FNavMapItem
// ----------------------------------------------------------------------------------------

FNavMapItem::FNavMapItem()
    : System(nullptr)
    , Label(nullptr)
    , bLabelStarred(0)
    , bLabelCrowded(0)
{
    // FUN_00491f90 00491fd3..0049200d: FUN_00420d40 + FUN_00492250 + vftable + anim -1 are the
    // FClientMeshHolder member initialisers; +0x5c / +0x60 are the empty wide string. +0x64 is
    // written by AddItem right after (0x0049209a); it is initialised here with it.
}

FNavMapItem::~FNavMapItem()
{
    // FUN_0048dd40: the label, the +0x5c wide string (member dtor), the holder (FUN_0059ef20).
    if (Label != nullptr)
    {
        // 0x0048dd67..0x0048ddda: the Font::String at +0x78 -- its dtor inlined (FUN_004144f0,
        // the child slots left, the child buffer, the text through msvcrt free), then the
        // object freed: delete on the FFontString (EnsureItemLabel allocates it with new).
        delete Label;
        Label = nullptr;
    }
    Field_0x5c.clear();
    // Then the member dtors: the +0x5c string, the holder (FUN_0059ef20, ~FClientMeshHolder).
}

// ----------------------------------------------------------------------------------------
// NeuroNetNavMap -- construction / destruction
// ----------------------------------------------------------------------------------------

NeuroNetNavMap::NeuroNetNavMap()
{
    // FUN_0048de20. FUN_0059f860 (WinControl), the Event::Handler vptr, FUN_0055dec0
    // (ButtonMover) are the base constructors. The mesh holders (+0x350 / +0x378 / +0x3a0 /
    // +0x89c / +0xc1c, the ten at +0x930 via FUN_005b7fba(.., FUN_0048e600)) are
    // FClientMeshHolder's member initialisers; the lists / vectors are default-constructed;
    // +0x880 is FUN_004a2f10 (FShapeDB); +0xc80 is FUN_0040f150.
    SpaceCameraConstruct(&Camera);

    // 0048df36 / 0048df40
    ShipMarkerSize = 5000.0f;
    WaypointMarkerSize = 4500.0f;

    // 0048e130..0048e172: +0x3c8 zero, +0x3d4 identity.
    Field_0x3c8[0] = 0.0f;
    Field_0x3c8[1] = 0.0f;
    Field_0x3c8[2] = 0.0f;
    Field_0x3d4[0] = 1.0f;
    Field_0x3d4[1] = 0.0f;
    Field_0x3d4[2] = 0.0f;
    Field_0x3d4[3] = 0.0f;
    Field_0x3d4[4] = 1.0f;
    Field_0x3d4[5] = 0.0f;
    Field_0x3d4[6] = 0.0f;
    Field_0x3d4[7] = 0.0f;
    Field_0x3d4[8] = 1.0f;

    // 0048e178..0048e193 -- the three map holders' instance slot (already -1), the object
    // cache key and +0xaf0.
    ZoomedMapMesh.EngineInstance = reinterpret_cast<void*>(static_cast<intptr_t>(-1));
    ZoomedMapMesh2.EngineInstance = reinterpret_cast<void*>(static_cast<intptr_t>(-1));
    PrettyMapMesh.EngineInstance = reinterpret_cast<void*>(static_cast<intptr_t>(-1));
    CachedObjectId = 0xffffffffu;
    HoveredObjective = -1;

    ZoomScale = 1.0f;
    bInitialShowDone = 0;
    PlayerItem = nullptr;
    ViewMode = 1;
    bMouseDown = 0;
    bDragging = 0;
    Field_0x818 = 0;
    bTransition = 0;
    PendingSystem = nullptr;
    bSystemViewPending = 0;
    bDrawnOnce = 0;
    CurrentSystem = nullptr;
    bObjectToolsShown = 0;
    ObjectiveFirstRow = 0;

    // 0048e1f3..0048e20b: the two 0x63-dword arrays.
    for (int Index = 0; Index < 0x63; ++Index)
    {
        WaypointItems[Index] = nullptr;
        BestPathItems[Index] = nullptr;
    }

    // 0048e225..0048e26e
    MeshPosition[0] = 0.0f;
    MeshPosition[1] = 0.0f;
    MeshPosition[2] = -0.71f;
    Field_0x828[0] = 0.0f;
    Field_0x828[1] = 0.0f;
    Field_0x828[2] = -0.55f;
    CachedWaypointHash = 0;

    // 0048e277..0048e2f8
    ScreenRectSize[0] = 1.0f;
    ScreenRectSize[1] = 1.0f;
    ScreenRectSize[2] = 0.0f;
    ScreenRectMin[0] = 0.0f;
    ScreenRectMin[1] = 0.0f;
    ScreenRectMin[2] = 0.0f;
    MapOrigin[0] = -0.234f;
    MapOrigin[1] = -0.234f;
    MapOrigin[2] = 0.0f;
    MapSize[0] = 0.468f;
    MapSize[1] = 0.468f;
    MapSize[2] = 0.0f;

    // 0048e2fb..0048e37a
    MapExtentX = 112000.0f;
    MapExtentZ = 112000.0f;
    BlinkTimer = 0.0f;
    bBlink = 0;
    SolarTypeMask = 0x2020bdffu;
    bResourcesLoaded = 0;
    Field_0x87e = 0;
    bDrawZoneOutlines = 0;
    GroupMarkerMesh.EngineInstance = reinterpret_cast<void*>(static_cast<intptr_t>(-1));
    Field_0x8c4 = 0;
    bViewSwitch = 0;
    Field_0xb48 = 0;
    bSidePanelsOpen = 0;
    PulseTimer = 0;
    PulseIndex = -1;
    ZoomDistance = 1.0f;
    bBestPathObjectives = 1;
    LegendMesh.EngineInstance = reinterpret_cast<void*>(static_cast<intptr_t>(-1));
    bItemClamped = 0;

    // 0048e380
    G_bNavMapNoBestPath = 0;

    // 0048e30f / 0048e386..0048e439
    TradeLaneCachePan[0] = 0.0f;
    TradeLaneCachePan[1] = 0.0f;
    TradeLaneCachePan[2] = 0.0f;
    NoBestPathTimer = 0.0f;
    GroupPollTimer = 0.0f;
    TradeLaneFrame = 0;
    TradeLaneFrameTimer = 0.0f;
    LastWaypointCount = -1;
    TradeLaneCacheZoom = -1.0f;
    TradeLaneCacheSystem = 0;
    TradeLaneCacheSegments = 0;
    TerrainCachePan[0] = 0.0f;
    TerrainCachePan[1] = 0.0f;
    TerrainCachePan[2] = 0.0f;
    ShadeCachePan[0] = 0.0f;
    ShadeCachePan[1] = 0.0f;
    TerrainCacheZoom = -1.0f;
    TerrainCacheSystem = 0;
    ShadeCachePan[2] = 0.0f;
    ShadeCacheZoom = -1.0f;
    ShadeCacheSystem = 0;
    ShadeCacheZoneFilter = -1;
    ShadeCacheFilterMode = -1;
    bBriefing = 0;

    // 0048e43f..0048e463: in multiplayer ask the server for the group positions
    // (IServer slot 75, one zero dword).
    if (!SinglePlayer())
    {
        unsigned Zero = 0;
        G_Server->RequestGroupPositions(GetPlayerConnectionId(),
                                        reinterpret_cast<unsigned char*>(&Zero), 4);
    }

    // 0048e469..0048e50a: the six scratch buffers and their counters.
    TradeLaneVertices = static_cast<unsigned char*>(operator new(0x6000));
    TradeLaneIndices = static_cast<unsigned short*>(operator new(0x1800));
    TradeLaneVertexCount = 0;
    TradeLaneIndexCount = 0;
    TerrainVertices = static_cast<unsigned char*>(operator new(0x9000));
    TerrainIndices = static_cast<unsigned short*>(operator new(0x2400));
    TerrainVertexCount = 0;
    TerrainIndexCount = 0;
    TerrainZoneCount = 0;
    ShadeZoneCount = 0;
    TerrainRecheckTimer = 1.0f; // DAT_005d2ecc
    ShadeVertices = static_cast<unsigned char*>(operator new(0x17700));
    ShadeIndices = static_cast<unsigned short*>(operator new(0x8ca0));
    ShadeVertexCount = 0;
    ShadeIndexCount = 0;
    ShadeRecheckTimer = 1.0f; // DAT_005d2ed0

    // 0048e510..0048e571: the pipeline interfaces, once per process.
    if (G_NavMapVertexBuffer == nullptr)
        DALib::Pipeline->QueryInterface(Iids::RPVertexBuffer,
                                        reinterpret_cast<void**>(&G_NavMapVertexBuffer));
    if (G_NavMapIndexBuffer == nullptr)
        DALib::Pipeline->QueryInterface(Iids::RPIndexBuffer,
                                        reinterpret_cast<void**>(&G_NavMapIndexBuffer));
    if (G_NavMapDraw == nullptr)
        DALib::Pipeline->QueryInterface(Iids::RPDraw, reinterpret_cast<void**>(&G_NavMapDraw));

    // 0048e571..0048e5af: the systems the nav map never shows a name for.
    SkipSystemIds[0] = Universe::get_system_id("St02");
    SkipSystemIds[1] = Universe::get_system_id("St03");
    SkipSystemIds[2] = Universe::get_system_id("St03b");
    SkipSystemIds[3] = Universe::get_system_id("St02c");

    // 0048e5b5..0048e5d4
    FilterModeBeforeInfoCard = 0;
    SystemScale = 1.0f;
    G_bNavMapRouteChanged = 0;
    bBaseListBuilt = 0;
    BaseListFirstRow = 0;
    bBaseListByBase = 1;
}

NeuroNetNavMap::~NeuroNetNavMap()
{
    // FUN_0048e650.
    // 0048e693..0048e711: the six scratch buffers.
    operator delete(TerrainVertices);
    TerrainVertices = nullptr;
    operator delete(TerrainIndices);
    TerrainIndices = nullptr;
    operator delete(ShadeVertices);
    ShadeVertices = nullptr;
    operator delete(ShadeIndices);
    ShadeIndices = nullptr;
    operator delete(TradeLaneVertices);
    TradeLaneVertices = nullptr;
    operator delete(TradeLaneIndices);
    TradeLaneIndices = nullptr;

    // 0048e703..0048e76b: FUN_0059eee0 on the bound map / aux holders.
    if (ZoomedMapMesh.EngineInstance != reinterpret_cast<void*>(static_cast<intptr_t>(-1)))
        ClientUiMeshHolderRelease(&ZoomedMapMesh);
    if (ZoomedMapMesh2.EngineInstance != reinterpret_cast<void*>(static_cast<intptr_t>(-1)))
        ClientUiMeshHolderRelease(&ZoomedMapMesh2);
    if (PrettyMapMesh.EngineInstance != reinterpret_cast<void*>(static_cast<intptr_t>(-1)))
        ClientUiMeshHolderRelease(&PrettyMapMesh);
    if (GroupMarkerMesh.EngineInstance != reinterpret_cast<void*>(static_cast<intptr_t>(-1)))
        ClientUiMeshHolderRelease(&GroupMarkerMesh);
    if (LegendMesh.EngineInstance != reinterpret_cast<void*>(static_cast<intptr_t>(-1)))
        ClientUiMeshHolderRelease(&LegendMesh);

    // 0048e770..0048e78b: FUN_0059eee0 on all ten +0x930 holders, unconditionally.
    for (int Index = 0; Index < 10; ++Index)
        ClientUiMeshHolderRelease(&DigitMeshes[Index]);

    // 0048e78d..0048e7d3: the jump links -- here (and only here) their records are freed.
    while (!JumpLinks.empty())
    {
        FNavMapJumpLink* const Link = JumpLinks.front();
        operator delete(Link);
        JumpLinks.pop_front();
    }

    // 0048e7d5..0048e81a: the items (scalar deleting dtor FUN_0048eb80 on each non-null one).
    while (!Items.empty())
    {
        FNavMapItem* const Item = Items.front();
        if (Item != nullptr)
            delete Item;
        Items.pop_front();
    }

    // 0048e81c..0048e836
    for (int Index = 0; Index < 0x63; ++Index)
    {
        WaypointItems[Index] = nullptr;
        BestPathItems[Index] = nullptr;
    }

    // 0048e838: FUN_00425a20.
    Shapes.Clear();

    // 0048e843..0048e8d6: the three buffer handles through DestroyVb, then the interfaces.
    if (G_NavMapVertexBuffer != nullptr)
    {
        for (int Index = 0; Index < 3; ++Index)
        {
            if (G_NavMapBufferHandles[Index] != nullptr)
            {
                G_NavMapVertexBuffer->DestroyVb(&G_NavMapBufferHandles[Index]);
                G_NavMapBufferHandles[Index] = nullptr;
            }
        }
        G_NavMapVertexBuffer->Release();
        G_NavMapVertexBuffer = nullptr;
    }
    if (G_NavMapIndexBuffer != nullptr)
    {
        G_NavMapIndexBuffer->Release();
        G_NavMapIndexBuffer = nullptr;
    }
    if (G_NavMapDraw != nullptr)
    {
        G_NavMapDraw->Release();
        G_NavMapDraw = nullptr;
    }

    // 0048e8dc..0048eb11: the member destructors (the wide-string vectors, +0xfd8, the camera
    // -- its dtor is the RET at 0x00401e90 --, the +0xc1c / +0x930 / +0x89c holders
    // (FUN_0059ef20 each), FUN_004a2c70 on the ShapeDB, the three +0x848.. vectors, the jump
    // link list erase (FUN_004a2f80, records not freed: the loop above emptied it), the item
    // list erase, the three map holders). C++ runs them as member destructors in that order;
    // the holders' FUN_0059ef20 is ~FClientMeshHolder (the +0x930 array through FUN_005b7ec6,
    // last to first).
    // 0048eb16..0048eb64: ButtonMover's list (member dtor), FUN_00576010 (~Event::Handler),
    // FUN_0059fa50 (~WinControl) -- the base destructors.
}

// ----------------------------------------------------------------------------------------
// WinControl overrides
// ----------------------------------------------------------------------------------------

bool NeuroNetNavMap::HitTest(float const* Ray)
{
    // LAB_00492270.
    if (WinControl::HitTest(Ray)) // FUN_005a0340
        return true;
    if (bSidePanelsOpen == 0)
        return false;

    // 004922a9..0049233a: identity, or the bound background mesh's instance transform.
    float Transform[12] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f};
    if (ClientMeshHolderHasInstance(&MeshHolder))
        memcpy(Transform, DALib::Engine->GetInstanceTransform(MeshHolder.EngineInstance),
               sizeof(Transform));

    // 0049233c..004923b0: the 0.19 x 0.28 box 0.54 to the right.
    float const HalfExtents[3] = {0.19f, 0.28f, 0.0f};
    float Box[12];
    memcpy(Box, Transform, sizeof(Box));
    Box[9] = Transform[9] + 0.54f;
    if (Win3DRayHitsAabbHalfExtents(HalfExtents, Box, Ray))
        return true;

    // 004923bd..004923fd: the same box 0.536 to the left (DAT_005d3dc0).
    memcpy(Box, Transform, sizeof(Box));
    Box[9] = Transform[9] - 0.536f;
    return Win3DRayHitsAabbHalfExtents(HalfExtents, Box, Ray);
}

void NeuroNetNavMap::SetPosition(float const* NewPosition)
{
    // LAB_004948a0: FUN_005a0880, then +0x3c8 = position + (0.2, -0.25, 0).
    WinControl::SetPosition(NewPosition);
    Field_0x3c8[0] = NewPosition[0] + 0.2f;
    Field_0x3c8[1] = NewPosition[1] - 0.25f;
    Field_0x3c8[2] = NewPosition[2];
}

bool NeuroNetNavMap::Create(Win3D* NewParent, void* Data)
{
    // LAB_0048eba0.
    bBriefing = IsMissionBriefingNavMapUp(); // FUN_0043d570
    SetMesh("Interface\\NeuroNet\\NavMap\\NewNavMap\\navmap_background.cmp", 0);

    float const Zero3[3] = {0.0f, 0.0f, 0.0f};
    float const Zero3b[3] = {0.0f, 0.0f, 0.0f};

    // 0048ebc9..0048ecdd: the two side panels.
    {
        float const Position[3] = {0.19f, -0.23f, -0.22f};
        RightWindow = CreateStaticControl(
            "NavMapRightWindow", "Interface\\NeuroNet\\NavMap\\NewNavMap\\NAV_navmap_right.cmp",
            Position, nullptr, Zero3, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, Zero3b);
    }
    {
        float const Position[3] = {1.045f, -0.23f, -0.22f};
        LeftWindow = CreateStaticControl(
            "NavMapLeftWindow", "Interface\\NeuroNet\\NavMap\\NewNavMap\\NAV_navmap_left.cmp",
            Position, nullptr, Zero3, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, Zero3b);
    }

    // 0048eccb..0048ed18: the map's own pick box -- LEA ECX,[EBP+0xb4]; 0x3eb33333 (0.35),
    // 0x3ea8f5c3 (0.33), 0. Without it WinControl::HitTest (FUN_005a0340) never hits the map
    // area, Win3DPick never returns the map and the click falls through to the space pick.
    HalfExtents[0] = 0.35f;
    HalfExtents[1] = 0.33f;
    HalfExtents[2] = 0.0f;

    // The frame buttons: 0.018 half extents on the top edge (y 0.295).
    float const SmallHalf[3] = {0.018f, 0.018f, 0.0f};
    {
        float const Position[3] = {-0.26f, 0.295f, 0.0f};
        UniverseButton = CreateButtonControl(
            "NavMapUniverseButton", "Interface\\NeuroNet\\NavMap\\NewNavMap\\NAV_universebutton.3db",
            nullptr, "ui_select_item", nullptr, Position, nullptr, SmallHalf, 0, 0, 0, 0, 0x219c, 0,
            0);
    }
    {
        float const Position[3] = {-0.214f, 0.295f, 0.0f};
        LabelButton = CreateButtonControl(
            "NavMapLabelButton", "Interface\\NeuroNet\\NavMap\\NewNavMap\\NAV_labels.3db", nullptr,
            "ui_select_item", nullptr, Position, nullptr, SmallHalf, 0, 0, 0, 0, 0x416, 0, 0);
    }
    {
        float const Position[3] = {0.258f, 0.295f, 0.0f};
        CloseButton = CreateButtonControl(
            "NavMapCloseButton", "Interface\\NeuroNet\\Inventory\\x.3db", nullptr,
            "ui_select_item", nullptr, Position, nullptr, SmallHalf, 0, 0, 0, 0, 0x409, 0, 0);
    }

    // 0048ee4a..0048ef67: briefing only -- no close button, the briefing text and the
    // continue button, and the 0xf event.
    if (bBriefing != 0)
    {
        CloseButton->Flags = static_cast<unsigned char>(CloseButton->Flags & 0xfc);
        {
            float const Position[3] = {-0.76f, 0.47f, 0.0f};
            MissionBriefingText = CreateTextControl("MissionBriefingText", "MissionObjective",
                                                    Position, nullptr, 1.48f, 0.06f, 4.0f, 0x627,
                                                    1, 0, 1);
        }
        {
            float const HalfExtents[3] = {0.153f, 0.036f, 0.0f};
            float const Position[3] = {-0.004f, -0.443f, 0.0f};
            ContinueButton = CreateWireButtonControl("NavMapContinueButton", "ui_select_item",
                                                     Position, HalfExtents, 0, 0, 0x326, 0, 0);
        }
        {
            float const Position[3] = {-0.76f, -0.418f, 0.0f};
            ContinueText = CreateTextControl("NavMapContinueText", "ButtonText", Position,
                                             nullptr, 1.51f, 0.03f, 3.0f, 0x48e, 1, 0, 1);
        }
        RegisterEvent(0xf); // FUN_005760d0
    }

    {
        float const Position[3] = {0.212f, 0.295f, 0.0f};
        ShipButton = CreateButtonControl(
            "NavMapShipButton", "Interface\\neuronet\\inventory\\gotoship.3db", nullptr,
            "ui_select_item", nullptr, Position, nullptr, SmallHalf, 0, 0, 0, 0, 0x4de, 0, 0);
    }
    {
        float const Position[3] = {-0.274f, -0.252f, 0.0f};
        SystemNameText = CreateTextControl("NavMapSystemName", "SubHeader", Position, nullptr,
                                           0.549f, 0.06f, 2.2f, 0, 1, 0, 1);
    }
    {
        float const Position[3] = {-0.314f, -0.222f, 0.0f};
        DebugText = CreateTextControl("NavMapDebugText", "Normal", Position, nullptr, 0.64f,
                                      0.06f, 2.2f, 0x4d2, 1, 0, 1);
    }
    DebugText->SetProperty(0x33, reinterpret_cast<uintptr_t>(&G_NnPaletteArgb_679ba4), 1);
    {
        float const Position[3] = {-0.314f, -0.222f, 0.0f};
        NoBestPathText = CreateTextControl("NavMapNoBestPathText", "Normal", Position, nullptr,
                                           0.64f, 0.06f, 2.2f, 0x573, 1, 0, 1);
    }
    NoBestPathText->SetProperty(0x33, reinterpret_cast<uintptr_t>(&G_NnPaletteArgb_679ba4), 1);

    // 0048f0ec..0048f568: the filter toggles along the top edge.
    {
        float const Position[3] = {0.092f, 0.295f, 0.0f};
        LegendFilter = CreateButtonControl(
            "NavMapLegendFilter", "Interface\\NeuroNet\\NavMap\\NewNavMap\\NAV_legendtoggle.3db",
            nullptr, "ui_filter_select", nullptr, Position, nullptr, SmallHalf, 0, 0, 0, 0, 0x54e,
            0, 0);
    }
    {
        float const Position[3] = {0.138f, 0.295f, 0.0f};
        BaseListFilter = CreateButtonControl(
            "NavMapBaseListFilter", "Interface\\NeuroNet\\NavMap\\NewNavMap\\NAV_knownbases.3db",
            nullptr, "ui_filter_select", nullptr, Position, nullptr, SmallHalf, 0, 0, 0, 0, 0x3c7,
            0, 0);
    }
    {
        float const Position[3] = {-0.092f, 0.295f, 0.0f};
        PhysicalFilter = CreateButtonControl(
            "NavMapPhysicalFilter",
            "Interface\\NeuroNet\\NavMap\\NewNavMap\\NAV_filter_physical.3db", nullptr,
            "ui_filter_select", nullptr, Position, nullptr, SmallHalf, 0, 0, 0, 0, 0x4d3, 0, 0);
    }
    PhysicalFilter->SetProperty(0xc, 1, 0);
    {
        float const Position[3] = {-0.046f, 0.295f, 0.0f};
        PoliticalFilter = CreateButtonControl(
            "NavMapPoliticalFilter",
            "Interface\\NeuroNet\\NavMap\\NewNavMap\\NAV_filter_political.3db", nullptr,
            "ui_filter_select", nullptr, Position, nullptr, SmallHalf, 0, 0, 0, 0, 0x4d4, 0, 0);
    }
    {
        float const Position[3] = {0.0f, 0.295f, 0.0f};
        PatrolFilter = CreateButtonControl(
            "NavMapPatrolFilter", "Interface\\NeuroNet\\NavMap\\NewNavMap\\NAV_patrol.3db",
            nullptr, "ui_filter_select", nullptr, Position, nullptr, SmallHalf, 0, 0, 0, 0, 0x521,
            0, 0);
    }
    {
        float const Position[3] = {0.046f, 0.295f, 0.0f};
        MineableFilter = CreateButtonControl(
            "NavMapmineableFilter",
            "Interface\\NeuroNet\\NavMap\\NewNavMap\\NAV_minablezonefilter.3db", nullptr,
            "ui_filter_select", nullptr, Position, nullptr, SmallHalf, 0, 0, 0, 0, 0x54f, 0, 0);
    }
    {
        float const Position[3] = {-0.018f, 0.295f, 0.0f};
        MissionFilter = CreateButtonControl(
            "NavMapMissionFilter",
            "Interface\\NeuroNet\\NavMap\\NewNavMap\\NAV_filter_missions.3db", nullptr,
            "ui_filter_select", nullptr, Position, nullptr, SmallHalf, 0, 0, 0, 0, 0x522, 0, 0);
    }
    {
        float const Position[3] = {0.161f, 0.246f, 0.0f};
        AllFilter = CreateButtonControl(
            "NavMapAllFilter", "Interface\\NeuroNet\\NavMap\\NewNavMap\\NAV_allzonefilter.3db",
            nullptr, "ui_filter_select", nullptr, Position, nullptr, SmallHalf, 0, 0, 0, 0, 0x559,
            0, 0);
    }
    AllFilter->SetProperty(1, 1, 0);
    {
        float const Position[3] = {0.207f, 0.246f, 0.0f};
        HostileFilter = CreateButtonControl(
            "NavMapHostileFilter",
            "Interface\\NeuroNet\\NavMap\\NewNavMap\\NAV_hostilezonefilter.3db", nullptr,
            "ui_filter_select", nullptr, Position, nullptr, SmallHalf, 0, 0, 0, 0, 0x55a, 0, 0);
    }
    HostileFilter->SetProperty(1, 1, 0);
    {
        float const Position[3] = {0.253f, 0.246f, 0.0f};
        FriendlyFilter = CreateButtonControl(
            "NavMapFriendlyFilter",
            "Interface\\NeuroNet\\NavMap\\NewNavMap\\NAV_friendlyzonefilter.3db", nullptr,
            "ui_filter_select", nullptr, Position, nullptr, SmallHalf, 0, 0, 0, 0, 0x55b, 0, 0);
    }
    FriendlyFilter->SetProperty(1, 1, 0);

    // 0048f56e..0048fc48: the object tools, each mesh scaled by 1.7.
    float const ToolHalf[3] = {0.02f, 0.02f, 0.0f};
    float const ZoomPosition[3] = {0.25f, 0.298f, 0.0f};
    float const WaypointPosition[3] = {-0.68f, 0.228f, 0.0f};
    InZoomButton = CreateButtonControl(
        "NavMapInZoom", "Interface\\NeuroNet\\NavMap\\NewNavMap\\NAV_zoomin.cmp", nullptr,
        "ui_select_item", nullptr, ZoomPosition, nullptr, ToolHalf, 0, 0, 0, 0, 0x4d5, 0, 0);
    ScaleToolButtonMesh(InZoomButton);
    OutZoomButton = CreateButtonControl(
        "NavMapOutZoom", "Interface\\NeuroNet\\NavMap\\NewNavMap\\NAV_zoomout.cmp", nullptr,
        "ui_select_item", nullptr, ZoomPosition, nullptr, ToolHalf, 0, 0, 0, 0, 0x4d6, 0, 0);
    ScaleToolButtonMesh(OutZoomButton);
    SetWaypointButton = CreateButtonControl(
        "NavMapSetWaypointButton", "Interface\\NeuroNet\\NavMap\\NewNavMap\\NAV_addwaypoint.cmp",
        nullptr, "ui_select_item", nullptr, WaypointPosition, nullptr, ToolHalf, 0, 0, 0, 0,
        0x4d7, 0, 0);
    ScaleToolButtonMesh(SetWaypointButton);
    BestPathButton = CreateButtonControl(
        "NavMapBestPathButton", "Interface\\NeuroNet\\NavMap\\NewNavMap\\NAV_shortestpath.cmp",
        nullptr, "ui_select_item", nullptr, WaypointPosition, nullptr, ToolHalf, 0, 0, 0, 0,
        0x56e, 0, 0);
    ScaleToolButtonMesh(BestPathButton);
    JumpGateButton = CreateButtonControl(
        "NavMapJumpGateButton", "Interface\\NeuroNet\\NavMap\\NewNavMap\\NAV_jumptosystem.cmp",
        nullptr, "ui_select_item", nullptr, WaypointPosition, nullptr, ToolHalf, 0, 0, 0, 0,
        0x571, 0, 0);
    ScaleToolButtonMesh(JumpGateButton);
    SelectObjectButton = CreateButtonControl(
        "NavMapSelectObjectButton",
        "Interface\\NeuroNet\\NavMap\\NewNavMap\\NAV_selectorhand.cmp", nullptr, "ui_select_item",
        nullptr, WaypointPosition, nullptr, ToolHalf, 0, 0, 0, 0, 0x572, 0, 0);
    ScaleToolButtonMesh(SelectObjectButton);
    InfoButton = CreateButtonControl(
        "NavMapInfo", "Interface\\NeuroNet\\NavMap\\NewNavMap\\NAV_info.cmp", nullptr,
        "ui_select_item", nullptr, ZoomPosition, nullptr, ToolHalf, 0, 0, 0, 0, 0x4d9, 0, 0);
    ScaleToolButtonMesh(InfoButton);
    ClearWaypointButton = CreateButtonControl(
        "NavMapClearWaypointButton",
        "Interface\\NeuroNet\\NavMap\\NewNavMap\\NAV_subtractwaypoint.cmp", nullptr,
        "ui_select_item", nullptr, WaypointPosition, nullptr, ToolHalf, 0, 0, 0, 0, 0x4d8, 0, 0);
    ScaleToolButtonMesh(ClearWaypointButton);

    // 0048fc4e..0048fcf2: the info text and the info card (both lose +0x6c bit 1).
    {
        float const AttachOffset[3] = {kNavMapPanelBaseA + 0.93f, -0.06f, 0.0f};
        InfoText = CreateRichTextWin("NavMapInfoText", 0.27f, 0.33f, AttachOffset, true);
    }
    InfoText->Flags = static_cast<unsigned char>(InfoText->Flags & 0xfd);
    {
        float const AttachOffset[3] = {0.4f, -0.02f, 0.0f};
        InfoCard = CreateRichTextWin("NavMapInfoCard", 0.43f, 0.41f, AttachOffset, true);
    }
    InfoCard->Flags = static_cast<unsigned char>(InfoCard->Flags & 0xfd);
    {
        float const Position[3] = {kNavMapPanelBaseA + 0.36f, 0.256f, 0.0f};
        InfoTitle = CreateTextControl("NavMapInfoTitle", "Normal", Position, nullptr, 0.32f,
                                      0.03f, 2.1f, 0, 1, 0, 1);
    }

    // 0048fd32..0049004e: the waypoint / mission buttons under the info text.
    {
        float const HalfExtents[3] = {0.146f, 0.026f, 0.0f};
        float const Position[3] = {kNavMapPanelBaseB - 0.516f, -0.237f, 0.0f};
        ClearAllWaypointsButton = CreateWireButtonControl(
            "NavMapClearAllWaypointsButton", "ui_select_item", Position, HalfExtents, 0, 0, 0x327,
            0, 0);
    }
    {
        float const Position[3] = {kNavMapPanelBaseB - 0.659f, -0.209f, 0.0f};
        ClearAllWaypointsText = CreateTextControl("NavMapClearAllWaypointsText", "ButtonText",
                                                  Position, nullptr, 0.288f, 0.03f, 1.9f,
                                                  0x219d, 1, 0, 1);
    }
    {
        float const HalfExtents[3] = {0.133f, 0.026f, 0.0f};
        float const Position[3] = {kNavMapPanelBaseA + 0.53f, -0.23f, 0.0f};
        AbortMissionButton = CreateWireButtonControl(
            "NavMapAbortMissionButton", "ui_select_item", Position, HalfExtents, 0, 0, 0x328, 0,
            0);
    }
    {
        float const Position[3] = {kNavMapPanelBaseA + 0.397f, -0.206f, 0.0f};
        AbortMissionText = CreateTextControl("NavMapAbortMissionText", "ButtonText", Position,
                                             nullptr, 0.266f, 0.03f, 1.9f, 0x4da, 1, 0, 1);
    }
    AbortMissionButton->SetProperty(1, 1, 0);
    AbortMissionText->SetProperty(1, 1, 0);
    {
        float const HalfExtents[3] = {0.067f, 0.026f, 0.0f};
        float const Position[3] = {kNavMapPanelBaseA + 0.447f, -0.23f, 0.0f};
        AbortYesButton = CreateWireButtonControl("NavMapAbortYesButton", "ui_select_item",
                                                 Position, HalfExtents, 0, 0, 0x329, 0, 0);
    }
    {
        float const Position[3] = {kNavMapPanelBaseA + 0.381f, -0.206f, 0.0f};
        AbortYesText = CreateTextControl("NavMapAbortYesText", "ButtonText", Position, nullptr,
                                         0.132f, 0.03f, 1.9f, 0x4dc, 1, 0, 1);
    }
    {
        float const HalfExtents[3] = {0.067f, 0.026f, 0.0f};
        float const Position[3] = {kNavMapPanelBaseA + 0.617f, -0.23f, 0.0f};
        AbortNoButton = CreateWireButtonControl("NavMapAbortNoButton", "ui_select_item",
                                                Position, HalfExtents, 0, 0, 0x32a, 0, 0);
    }
    {
        float const Position[3] = {kNavMapPanelBaseA + 0.55f, -0.206f, 0.0f};
        AbortNoText = CreateTextControl("NavMapAbortNoText", "ButtonText", Position, nullptr,
                                        0.132f, 0.03f, 1.9f, 0x4dd, 1, 0, 1);
    }
    AbortYesButton->SetProperty(1, 1, 0);
    AbortYesText->SetProperty(1, 1, 0);
    AbortNoButton->SetProperty(1, 1, 0);
    AbortNoText->SetProperty(1, 1, 0);

    // 00490054..004900b4
    OptionBridge = CreateStaticControl("NavMapOptionBridge",
                                       "Interface\\NeuroNet\\NavMap\\NewNavMap\\NAV_selector.3db",
                                       Zero3, nullptr, Zero3, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0,
                                       Zero3b);
    OptionBridge->SetProperty(1, 1, 0);

    // 004900ba..00490354: the three objective boxes, their scroll bar and texts.
    {
        float const HalfExtents[3] = {0.146f, 0.068f, 0.0f};
        float const X = kNavMapPanelBaseB - 0.547f;
        float const Position1[3] = {X, 0.181f, 0.0f};
        MissionObjective[0] = CreateWireStaticControl("NavMapMissionObjective1", Position1,
                                                      HalfExtents, 0, 0, 0, 0);
        float const Position2[3] = {X, 0.046f, 0.0f};
        MissionObjective[1] = CreateWireStaticControl("NavMapMissionObjective2", Position2,
                                                      HalfExtents, 0, 0, 0, 0);
        float const Position3[3] = {X, -0.089f, 0.0f};
        MissionObjective[2] = CreateWireStaticControl("NavMapMissionObjective3", Position3,
                                                      HalfExtents, 0, 0, 0, 0);
    }
    {
        float const Position[3] = {kNavMapPanelBaseB + 0.3261f, -0.058f, 0.0f};
        ObjectiveScroll = CreateUIScroll("NavMapMissionObjectiveScroll", Position, 0.019f,
                                         0.285f, 0, 5, 3, 0);
    }
    {
        float const X = kNavMapPanelBaseB - 0.686f;
        float const Position1[3] = {X, NnFloatBits(0x3e808313), 0.0f}; // 0.251 (listing bits)
        ObjectiveText[0] = CreateTextControl("NavMapMissionObjectiveText1", "NewsMission",
                                             Position1, nullptr, 0.285f, 0.03f, 1.4f, 0, 0, 0, 1);
        ObjectiveText[0]->SetProperty(0x22, 0, 5);
        SetObjectiveTextMultiLine(ObjectiveText[0]);
        float const Position2[3] = {X, 0.116f, 0.0f};
        ObjectiveText[1] = CreateTextControl("NavMapMissionObjectiveText2", "NewsMission",
                                             Position2, nullptr, 0.285f, 0.03f, 1.4f, 0, 0, 0, 1);
        ObjectiveText[1]->SetProperty(0x22, 0, 5);
        SetObjectiveTextMultiLine(ObjectiveText[1]);
        float const Position3[3] = {X, NnFloatBits(0xbc9ba5e4), 0.0f}; // -0.019 (DAT bits)
        ObjectiveText[2] = CreateTextControl("NavMapMissionObjectiveText3", "NewsMission",
                                             Position3, nullptr, 0.285f, 0.03f, 1.4f, 0, 0, 0, 1);
        ObjectiveText[2]->SetProperty(0x22, 0, 5);
        SetObjectiveTextMultiLine(ObjectiveText[2]);
    }

    // 00490354..004904be: the four faction labels of the universe view, hidden.
    {
        float const Position[3] = {-0.055f, -0.09f, 0.0f};
        FactionLabels[0] = CreateTextControl("NavMapLibertyLabel", "SubHeader", Position,
                                             nullptr, 0.132f, 0.03f, 1.9f, 0x4e1, 0, 0, 1);
    }
    FactionLabels[0]->SetProperty(1, 1, 0);
    {
        float const Position[3] = {-0.245f, -0.123f, 0.0f};
        FactionLabels[1] = CreateTextControl("NavMapBretoniaLabel", "SubHeader", Position,
                                             nullptr, 0.132f, 0.03f, 1.9f, 0x4e2, 0, 0, 1);
    }
    FactionLabels[1]->SetProperty(1, 1, 0);
    {
        float const Position[3] = {0.005f, 0.121f, 0.0f};
        FactionLabels[2] = CreateTextControl("NavMapKusariLabel", "SubHeader", Position,
                                             nullptr, 0.132f, 0.03f, 1.9f, 0x4e3, 0, 0, 1);
    }
    FactionLabels[2]->SetProperty(1, 1, 0);
    {
        float const Position[3] = {0.065f, -0.15f, 0.0f};
        FactionLabels[3] = CreateTextControl("NavMapRheinlandLabel", "SubHeader", Position,
                                             nullptr, 0.132f, 0.03f, 1.9f, 0x4e4, 0, 0, 1);
    }
    FactionLabels[3]->SetProperty(1, 1, 0);

    // 004904c4..00490503
    {
        float const Position[3] = {-0.274f, 0.266f, 0.0f};
        FilterTitle = CreateTextControl("NavMapFilterTitle", "Normal", Position, nullptr, 0.547f,
                                        0.03f, 1.5f, 0, 1, 0, 1);
    }

    // 00490508..004905a8: the grid letters A..H (ids 0x506 + i), x = i * 0.07 - 0.279.
    char Name[0x40];
    for (int Index = 0; Index < 8; ++Index)
    {
        sprintf(Name, "NavMapGridLetter%c", 'A' + Index);
        float const Position[3] = {
            static_cast<float>(static_cast<double>(Index) * 0.07f - 0.279f), -0.294f, 0.0f};
        GridLetters[Index] =
            CreateTextControl(Name, "Normal", Position, nullptr, 0.08f, 0.03f, 1.9f,
                              static_cast<unsigned>(Index + 0x506), 1, 0, 1);
    }
    // 004905ae..00490646: the grid numbers 1..8 (ids 0x515 + i), y = 0.252 - i * 0.07.
    for (int Index = 0; Index < 8; ++Index)
    {
        sprintf(Name, "NavMapGridNumber%d", Index + 1);
        float const Position[3] = {
            -0.309f, static_cast<float>(0.252f - static_cast<double>(Index) * 0.07f), 0.0f};
        GridNumbers[Index] =
            CreateTextControl(Name, "Normal", Position, nullptr, 0.032f, 0.03f, 1.9f,
                              static_cast<unsigned>(Index + 0x515), 1, 0, 1);
    }

    // 0049064c..004908a3: the legend labels, hidden, in the idle palette colour (one stack
    // copy of DAT_00679bb0 handed to all five).
    unsigned long LegendColor = G_NnPaletteArgb_679bb0;
    static float const kLegendY[5] = {0.194f, 0.09f, 0.14f, -0.047f, -0.198f};
    static char const* const kLegendNames[5] = {"NavMapHostileLabel", "NavMapFriendlyLabel",
                                                "NavMapNeutralLabel", "NavMapPlayerShipLabel",
                                                "NavMapMissionWaypointsLabel"};
    static unsigned const kLegendIds[5] = {0x553, 0x555, 0x556, 0x557, 0x3af};
    for (int Index = 0; Index < 5; ++Index)
    {
        float const Position[3] = {-0.159f, kLegendY[Index], 0.0f};
        LegendLabels[Index] = CreateTextControl(kLegendNames[Index], "Normal", Position, nullptr,
                                                0.53f, 0.03f, 2.2f, kLegendIds[Index], 0, 0, 1);
        LegendLabels[Index]->SetProperty(1, 1, 0);
        LegendLabels[Index]->SetProperty(0x33, reinterpret_cast<uintptr_t>(&LegendColor), 1);
    }

    // 004908a9..0049096f: the info card's close button and caption, hidden.
    {
        float const HalfExtents[3] = {0.133f, 0.026f, 0.0f};
        float const Position[3] = {0.0f, -0.254f, 0.0f};
        CloseInfocardButton = CreateWireButtonControl("NavMapCloseInfocardButton",
                                                      "ui_select_item", Position, HalfExtents, 0,
                                                      0, 0x32b, 0, 0);
    }
    {
        float const Position[3] = {-0.133f, -0.229f, 0.0f};
        CloseInfocardText = CreateTextControl("NavMapCloseInfocardText", "ButtonText", Position,
                                              nullptr, 0.266f, 0.03f, 1.9f, 0x3a1, 1, 0, 1);
    }
    CloseInfocardButton->Flags = static_cast<unsigned char>(CloseInfocardButton->Flags & 0xfc);
    CloseInfocardText->Flags = static_cast<unsigned char>(CloseInfocardText->Flags & 0xfc);

    // 00490972..00490a54: the fifteen known-base rows, y = 0.171 - i * 0.03.
    for (int Index = 0; Index < 15; ++Index)
    {
        sprintf(Name, "NavMapBaseText%d", Index);
        float const Y = static_cast<float>(0.171f - static_cast<double>(Index) * 0.03f);
        float const BasePosition[3] = {-0.259f, Y, 0.0f};
        BaseTexts[Index] = CreateTextControl(Name, "ItemName", BasePosition, nullptr, 0.39f,
                                             0.03f, 1.9f, 0, 0, 0, 1);
        sprintf(Name, "NavMapBaseSystemText%d", Index);
        float const SystemPosition[3] = {0.019f, Y, 0.0f};
        BaseSystemTexts[Index] = CreateTextControl(Name, "ItemName", SystemPosition, nullptr,
                                                   0.21f, 0.03f, 1.9f, 0, 0, 0, 1);
    }
    {
        float const Position[3] = {0.829f, -0.058f, 0.0f};
        BaseListScroll = CreateUIScroll("NavMapBaseListScroll", Position, 0.019f, 0.406f, 0, 5,
                                        3, 0);
    }
    {
        float const Position[3] = {-0.261f, 0.219f, 0.0f};
        BaseTitleText = CreateTextControl("NavMapBaseTitleText", "SubHeader", Position, nullptr,
                                          0.279f, 0.03f, 1.9f, 0x3cc, 0, 0, 1);
    }
    {
        float const Position[3] = {0.016f, 0.219f, 0.0f};
        SystemTitleText = CreateTextControl("NavMapSystemTitleText", "SubHeader", Position,
                                            nullptr, 0.219f, 0.03f, 1.9f, 0x3cd, 0, 0, 1);
    }

    // 00490b3b..00490b75: "clear all waypoints" is hidden with no waypoints or in best-path
    // mode (NEG / SBB / INC = the count is zero).
    unsigned char bHideClearAll = GetPlayerWaypointCountRetail() == 0 ? 1 : 0;
    if (bBestPathObjectives != 0)
        bHideClearAll = 1;
    ClearAllWaypointsButton->SetProperty(1, bHideClearAll, 0);
    ClearAllWaypointsText->SetProperty(1, bHideClearAll, 0);

    // 00490b7b..00490b99
    RefreshObjectives();
    BuildJumpLinks();
    HideObjectTools();
    RefreshMissionInfo();
    RefreshObjectives();

    // 00490b9e..00490bc2: a best path forces the mission filter; the info-card modes do not
    // survive a reopen.
    if (BestPathGetWaypointCount() > 0)
        G_NavMapFilterMode = 3;
    else if (G_NavMapFilterMode == 6 || G_NavMapFilterMode == 7)
        G_NavMapFilterMode = 0;

    // 00490bcc..00490c50
    LabelButton->SetProperty(0xc, G_bNavMapShowLabels, 0);
    SetFilterMode(G_NavMapFilterMode);
    AllFilter->SetProperty(0xc, G_NavMapZoneFilter == 0 ? 1 : 0, 0);
    HostileFilter->SetProperty(0xc, G_NavMapZoneFilter == 1 ? 1 : 0, 0);
    FriendlyFilter->SetProperty(0xc, G_NavMapZoneFilter == 2 ? 1 : 0, 0);

    // 00490c56..00490cb6: hidden until shown (0xef01), the frame offset, the position again
    // (through slot 5), then FUN_004b5060.
    Flags = static_cast<unsigned char>(Flags & 0xfc);
    SetOffset(0.624f, -0.23f, -0.22f);
    SetPosition(Position);
    return Win3D::Create(NewParent, Data);
}

bool NeuroNetNavMap::Slot23(unsigned Msg, unsigned A, unsigned B)
{
    // LAB_004945e0.
    (void)A;
    (void)B;
    if (Msg == 0xef01)
    {
        // 004945f2..00494627: follow the parent, first show, flags |= 3.
        if (GetParent() != nullptr)
            SetPosition(GetParent()->Position);
        InitialShow();
        TransitionTime = TransitionDuration;
        Flags = static_cast<unsigned char>(Flags | 3);
        Field_0x818 = 0;

        // 0049462e..00494806: the system view's filter row.
        if (ViewMode != 0)
        {
            UniverseButton->SetProperty(1, 0, 0);
            LabelButton->SetProperty(1, 0, 0);
            PhysicalFilter->SetProperty(1, 0, 0);
            PoliticalFilter->SetProperty(1, 0, 0);
            PatrolFilter->SetProperty(1, 0, 0);
            FilterTitle->SetProperty(1, 0, 0);
            LegendFilter->SetProperty(1, 0, 0);
            BaseListFilter->SetProperty(1, 0, 0);
            MineableFilter->SetProperty(1, 0, 0);

            unsigned char const bNoZones = MissionZoneIds.empty() ? 1 : 0;
            MissionFilter->SetProperty(1, bNoZones, 0);
            if (bNoZones != 0 && G_NavMapFilterMode == 3)
                SetFilterMode(0);

            if (G_NavMapFilterMode == 1 || G_NavMapFilterMode == 2)
            {
                AllFilter->SetProperty(1, 0, 0);
                HostileFilter->SetProperty(1, 0, 0);
                FriendlyFilter->SetProperty(1, 0, 0);
            }
            else
            {
                AllFilter->SetProperty(1, 1, 0);
                HostileFilter->SetProperty(1, 1, 0);
                FriendlyFilter->SetProperty(1, 1, 0);
            }

            for (int Index = 0; Index < 4; ++Index)
                FactionLabels[Index]->SetProperty(1, 1, 0);
        }

        // 0049480c..0049483c
        DebugText->SetProperty(1, Field_0x818 == 0 ? 1 : 0, 0);
        NoBestPathText->SetProperty(1, 1, 0);
        return false;
    }

    if (Msg == 0xef02)
    {
        // 00494850..00494895: hide, detach, delete the children, Destroy, delete this.
        Flags = static_cast<unsigned char>(Flags & 0xfc);
        Win3DReparent(this, nullptr);
        Win3D* Child = GetChildHead();
        while (Child != nullptr)
        {
            Win3D* const Next = Child->GetNextSibling();
            Win3DDeleteTree(Child);
            Child = Next;
        }
        Destroy();
        delete this;
        return true;
    }

    return false;
}

bool NeuroNetNavMap::OnEvent(unsigned MsgId)
{
    // LAB_0049e190.
    if (MsgId == 0xf && bBriefing != 0)
    {
        RequestMissionBriefingContinue(); // FUN_0043d580
        Slot23(0xef02, 0, 0);
        return true;
    }
    return false;
}

// ----------------------------------------------------------------------------------------
// Resources and the first show
// ----------------------------------------------------------------------------------------

void NeuroNetNavMap::LoadMapResources()
{
    // FUN_00490cd0.
    if (bResourcesLoaded != 0)
        return;
    bResourcesLoaded = 1;

    // 00490cea..00490d28 (FUN_0059ec80 x3, arguments (path, 0, 1)).
    ClientMeshLoadFromPath(&ZoomedMapMesh,
                           "Interface\\NeuroNet\\NavMap\\NewNavMap\\zoomedmap_liberty.3db", 0,
                           true);
    ClientMeshLoadFromPath(&ZoomedMapMesh2,
                           "Interface\\NeuroNet\\NavMap\\NewNavMap\\zoomedmap_liberty.3db", 0,
                           true);
    ClientMeshLoadFromPath(&PrettyMapMesh,
                           "Interface\\NeuroNet\\NavMap\\NewNavMap\\NAV_prettymap.3db", 0, true);

    IEngine* const Engine = DALib::Engine;
    float Transform[12];

    // 00490d2d..00490e1e: the first zoomed map at MeshPosition, its rotation scaled by 1.5.
    Engine->SetInstancePosition(ZoomedMapMesh.EngineInstance, MeshPosition);
    memcpy(Transform, Engine->GetInstanceTransform(ZoomedMapMesh.EngineInstance),
           sizeof(Transform));
    for (int Index = 0; Index < 9; ++Index)
        Transform[Index] = Transform[Index] * 1.5f; // DAT_005ca220
    Engine->SetInstanceMatrix(ZoomedMapMesh.EngineInstance, Transform);
    Engine->UpdateInstance(ZoomedMapMesh.EngineInstance, 0, nullptr);

    // 00490e24..00490e4e: the second one only positioned.
    Engine->SetInstancePosition(ZoomedMapMesh2.EngineInstance, MeshPosition);
    Engine->UpdateInstance(ZoomedMapMesh2.EngineInstance, 0, nullptr);

    // 00490e54..00490eda: the pretty map, [0] * 1.175 and [4] * 1.5 (double constants
    // DAT_005d3c50 / DAT_005d3c48).
    Engine->SetInstancePosition(PrettyMapMesh.EngineInstance, MeshPosition);
    memcpy(Transform, Engine->GetInstanceTransform(PrettyMapMesh.EngineInstance),
           sizeof(Transform));
    Transform[0] = static_cast<float>(static_cast<double>(Transform[0]) * 1.175);
    Transform[4] = static_cast<float>(static_cast<double>(Transform[4]) * 1.5);
    Engine->SetInstanceMatrix(PrettyMapMesh.EngineInstance, Transform);
    Engine->UpdateInstance(PrettyMapMesh.EngineInstance, 0, nullptr);

    // 00490ee0..004910e0: the shapes (the inline ShapeDB.h:0xf2 find, "cmn\shapedb.h").
    Shapes.Load("Interface\\NeuroNet\\NavMap\\NewNavMap\\NavMapTextures.ini", DALib::Data);
    for (int Index = 0; Index < 9; ++Index)
        TerrainShapes[Index] = Shapes.Find(kNavMapTerrainShapeNames[Index], kShapeDbHeaderLower);
    WaypathShape = Shapes.Find("nav_waypathtexture", kShapeDbHeaderLower);
    char Name[0x20];
    for (int Index = 1; Index < 0x11; ++Index)
    {
        sprintf(Name, "TLRanimated_%.2d", Index);
        TradeLaneShapes[Index - 1] = Shapes.Find(Name, kShapeDbHeaderLower);
    }
}

void NeuroNetNavMap::InitialShow()
{
    // FUN_00491100.
    if (bInitialShowDone != 0)
        return;
    bInitialShowDone = 1;

    LoadMapResources();
    PanOffset[0] = 0.0f;
    PanOffset[1] = 0.0f;
    PanOffset[2] = 0.0f;

    if (ViewMode != 0)
    {
        // 00491162..00491190: the requested system (consumed), else the player's.
        if (G_NavMapRequestedSystem != 0)
        {
            CurrentSystem = Universe::get_system(G_NavMapRequestedSystem);
            G_NavMapRequestedSystem = 0;
        }
        else
        {
            CurrentSystem = Universe::get_system(GetPlayerStarSystemIdRetail());
        }
        bDrawnOnce = 0;
        bSystemViewPending = 1;

        // 004911af..00491233: the system name, unless there is no system or it is one of the
        // four hidden ones (then ids 0x62a).
        bool bNameHidden = CurrentSystem == nullptr;
        if (!bNameHidden)
        {
            unsigned const Id = CurrentSystem->GetId();
            for (int Index = 0; Index < 4; ++Index)
            {
                if (SkipSystemIds[Index] == Id)
                {
                    bNameHidden = true;
                    break;
                }
            }
        }
        if (bNameHidden)
        {
            SystemNameText->SetProperty(0x1e, 0x62a, 0);
        }
        else
        {
            GetIdsStringWWithRawFallback(static_cast<unsigned>(CurrentSystem->GetStridName()),
                                         G_NnTextScratchB, G_NnTextScratchCapacity);
            CharUpperW(G_NnTextScratchB); // FUN_005b74d9
            SystemNameText->SetProperty(0x1d, reinterpret_cast<uintptr_t>(G_NnTextScratchB), 0);
        }
    }
    else
    {
        BuildUniverseItems();
    }

    // 0049123c..00491300: the map's four corners.
    for (int Axis = 0; Axis < 3; ++Axis)
    {
        MapCorner0[Axis] = MapOrigin[Axis];
        MapCorner1[Axis] = MapOrigin[Axis];
    }
    MapCorner1[0] = MapCorner1[0] + MapSize[0];
    MapCorner2[0] = MapSize[0] + MapOrigin[0];
    MapCorner2[1] = MapOrigin[1] + MapSize[1];
    MapCorner2[2] = MapOrigin[2] + MapSize[2];
    for (int Axis = 0; Axis < 3; ++Axis)
        MapCorner3[Axis] = MapOrigin[Axis];
    MapCorner3[1] = MapSize[1] + MapCorner3[1];

    // 00491306..004913f8: everything on the filter row hidden.
    UniverseButton->SetProperty(1, 1, 0);
    LabelButton->SetProperty(1, 1, 0);
    PhysicalFilter->SetProperty(1, 1, 0);
    PoliticalFilter->SetProperty(1, 1, 0);
    PatrolFilter->SetProperty(1, 1, 0);
    MissionFilter->SetProperty(1, 1, 0);
    MineableFilter->SetProperty(1, 1, 0);
    FilterTitle->SetProperty(1, 1, 0);
    LegendFilter->SetProperty(1, 1, 0);
    BaseListFilter->SetProperty(1, 1, 0);
    AllFilter->SetProperty(1, 1, 0);
    HostileFilter->SetProperty(1, 1, 0);
    FriendlyFilter->SetProperty(1, 1, 0);

    // 004913fe..00491408: tail call through slot 11 (virtual; ButtonMover::Update shares the
    // name, so the Win3D face is named explicitly).
    static_cast<Win3D*>(this)->Update();
}

// ----------------------------------------------------------------------------------------
// Items
// ----------------------------------------------------------------------------------------

void NeuroNetNavMap::BuildUniverseItems()
{
    // FUN_004915b0.
    SystemScale = 1.0f;
    MapExtentX = 112000.0f;
    MapExtentZ = 112000.0f;

    for (Universe::ISystem* System = Universe::GetFirstSystem(); System != nullptr;
         System = Universe::GetNextSystem())
    {
        // 004915f0..0049161d: rand() * 2000 / 32768 + 3000, kept as a float.
        int const Random = rand() * 0x7d0 / 0x8000 + 0xbb8;
        float const RandomRadius = static_cast<float>(Random);

        // 00491621..00491684: the [system] pos scaled to the map (1/15 * 200000 - 100000; z
        // is rounded to float after the first product and shifted by 5000).
        Vector const* const Pos = System->GetPos();
        float const PosX = Pos->x;
        float const PosY = Pos->y;
        float const ScaleK = NnFloatBits(0x3d888889); // DAT_005d3d30, 1/15
        float const ScaledY = PosY * ScaleK;
        float const X =
            static_cast<float>(static_cast<double>(PosX) * ScaleK * 200000.0f - 100000.0f);
        float const Z = static_cast<float>(
            static_cast<double>(ScaledY) * 200000.0f - 100000.0f + 5000.0f);

        // 00491688..004916dc: id (slot 0), visit (slot 18), ids_info (slot 4), strid (slot 3),
        // in retail's call order.
        unsigned const Id = System->GetId();
        unsigned char const Visit = System->GetVisit();
        int const IdsInfo = System->GetIdsInfo();
        int const StridName = System->GetStridName();
        FNavMapItem* const Item = AddItem(0, 4, X, 0.0f, Z, RandomRadius, "NAV_star", StridName,
                                          IdsInfo, 0, Id, Visit);

        // 004916e1..00491758 -- the item is used without a null test, as in retail.
        Item->System = System;
        Item->Variant = rand() * 7 / 0x8000;
        int const Twinkle = rand() * 0x384 / 0x8000 + 0x64;
        Item->PulsePhase = static_cast<float>(static_cast<double>(Twinkle) * 0.001f);
        int const Pulse = rand() * 0x64 / 0x8000 + 1;
        Item->RestSize = RandomRadius;
        Item->PulseRate = static_cast<float>(static_cast<double>(Pulse) * 0.01f);

        // 00491742..0049178f: the visit map's flags for the system join the item's; bit 0
        // shows it (or +0x87e forces every one).
        if (Item->Id != 0)
        {
            unsigned char VisitFlags = 0;
            GetPlayerVisitFlagsRetail(Item->Id, &VisitFlags, 0, 0); // FUN_004c4da0 0x0049176a
            Item->Visit = static_cast<unsigned char>(Item->Visit | VisitFlags);
            Item->bShown = static_cast<unsigned char>(Item->Visit & 1);
            if (Field_0x87e != 0)
                Item->bShown = 1;
        }
    }

    // 004917a3
    BuildJumpLinks();
}

FNavMapItem* NeuroNetNavMap::AddItem(int Kind, int Type, float X, float Y, float Z, float Arg6,
                                     char const* MeshName, int StridName, int IdsInfo,
                                     float Heading, unsigned Id, unsigned char Visit)
{
    // FUN_00491f90.
    FNavMapItem* const Item = new FNavMapItem();

    // 00492017..004920a6
    Item->Position[0] = X;
    Item->Position[1] = Y;
    Item->Position[2] = Z;
    Item->Type = Type;
    Item->Size = Arg6;
    Item->Kind = Kind;
    Item->StridName = StridName;
    Item->Heading = Heading;
    Item->IdsInfo = IdsInfo;
    Item->Id = Id;
    Item->bShown = 1;
    Item->System = nullptr;
    Item->Variant = 0;
    Item->Ordinal = 0;
    Item->RestSize = Arg6;
    Item->Visit = Visit;

    // 004920a9..00492118: "...\SpaceObjects\" + name + ".3db".
    char Path[0x104]; // [ESP+0x1c] (local_214..local_110)
    strcpy(Path, kNavMapSpaceObjectsDir);
    strcat(Path, MeshName);
    strcat(Path, ".3db");

    // 0049211b..00492146: GetFileSysPath against DALib::Data, then _access(.., 0).
    char FullPath[0x104];
    GetFileSysPath(FullPath, Path, DALib::Data);
    if (_access(FullPath, 0) == -1)
    {
        // 004921b6..0049223d: the item's destructor inline, operator delete, null.
        delete Item;
        return nullptr;
    }

    // 00492148..00492157: FUN_0059ec80(path, spaceobjects.mat, 1) -- the material path goes
    // to FUN_00420f30 (holder +0x0c) on a FindArchetype miss.
    ClientMeshLoadFromPath(&Item->Mesh, Path, kNavMapSpaceObjectsMat, true);

    // 0049215c..00492193: push_back.
    Items.push_back(Item);
    return Item;
}

// ----------------------------------------------------------------------------------------
// Pending focus (free functions)
// ----------------------------------------------------------------------------------------

void NavMapPostPendingOpen()
{
    // FUN_004a2a80.
    if (G_NavMapRequestedSystem != 0)
        HudPostCommand(0x63, 0); // FUN_005763b0(0x63, 0)
}

void NavMapRequestFocus(unsigned Id, unsigned char bZone)
{
    // FUN_004a2aa0.
    if (bZone != 0)
    {
        // 004a2ab6..004a2b4c: every system's zone list (slot 6). Retail copies each 0xc0c
        // record to the stack (REP MOVSD) and reads only the zone pointer from the copy.
        for (Universe::ISystem* System = Universe::GetFirstSystem(); System != nullptr;
             System = Universe::GetNextSystem())
        {
            std::list<Universe::FSystemZone> const* const Zones = System->GetZones();
            for (std::list<Universe::FSystemZone>::const_iterator It = Zones->begin();
                 It != Zones->end(); ++It)
            {
                Universe::IZone const* const Zone = It->Zone;
                if (Zone->GetId() != Id)
                    continue;
                Vector const& Pos = Zone->GetPos();
                float const X = Pos.x;
                float const Y = Pos.y;
                float const Z = Pos.z;
                unsigned const SystemId = System->GetId();
                G_NavMapRequestedPos[0] = X;
                G_NavMapRequestedPos[1] = Y;
                G_NavMapRequestedPos[2] = Z;
                G_bNavMapRequestedPos = 1;
                G_NavMapRequestedSystem = SystemId;
                return;
            }
        }
        return;
    }

    // 004a2b51..004a2bee: a system with that id; else one of its [Object]s by nickname hash.
    for (Universe::ISystem* System = Universe::GetFirstSystem(); System != nullptr;
         System = Universe::GetNextSystem())
    {
        if (System->GetId() == Id)
        {
            // 004a2bff..004a2c06: slot 0 called again for the stored id; no position.
            G_NavMapRequestedSystem = System->GetId();
            return;
        }

        // 004a2b7a..004a2ba2: the record's four TStrings emptied (length 0, first char 0).
        Universe::FSystemObject Object;
        Object.Nickname.Length = 0;
        Object.Nickname.Data[0] = '\0';
        Object.Reputation.Length = 0;
        Object.Reputation.Data[0] = '\0';
        Object.NextRing.Length = 0;
        Object.NextRing.Data[0] = '\0';
        Object.PrevRing.Length = 0;
        Object.PrevRing.Data[0] = '\0';
        unsigned const Count = static_cast<unsigned>(System->GetObjectCount());
        for (unsigned Index = 0; Index < Count; ++Index)
        {
            if (!System->GetSystemObject(Index, &Object))
                continue;
            if (CreateID(Object.Nickname.Data) != Id)
                continue;
            // 004a2c13..004a2c3e.
            float const X = Object.Pos.x;
            float const Y = Object.Pos.y;
            float const Z = Object.Pos.z;
            unsigned const SystemId = System->GetId();
            G_NavMapRequestedPos[0] = X;
            G_NavMapRequestedPos[1] = Y;
            G_NavMapRequestedPos[2] = Z;
            G_bNavMapRequestedPos = 1;
            G_NavMapRequestedSystem = SystemId;
            return;
        }
    }
}
