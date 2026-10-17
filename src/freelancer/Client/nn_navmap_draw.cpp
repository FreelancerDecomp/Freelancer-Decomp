// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// NeuroNetNavMap -- the frame's drawing: slot 13 Draw and the helpers only it reaches (the map
// meshes, the jump links and routes, the zone shading, the terrain patches, the trade lanes,
// the labels, the group markers). See nn_navmap.h. Retail Client\Interface\NeuroNet\NavMap.cpp.
//
// Floating point: retail keeps intermediates on the x87 stack (extended precision) and rounds
// only where it stores a float. The port models the stack with double and rounds with
// static_cast<float> exactly where retail has an FSTP dword; each function's comments name the
// spots where retail's rounding differs between otherwise identical sequences.

#include "nn_navmap.h"
#include "best_path.h"              // BestPathGetWaypointCount (FUN_004c4fb0), FUN_004c5040 / 5490
#include "character_select_equip.h" // GetPlayerStarSystemIdRetail (FUN_004c4830), visit flags
#include "client_mesh_load.h"       // ClientMeshLoadFromPath, ClientMeshHolderRender / Position
#include "font_atlas.h"             // FontAtlasSetDrawColor (FUN_00417780)
#include "font_imp.h"               // FindMenuFontImp (FUN_004177b0)
#include "font_string.h"            // Font::String (FUN_004144b0 ...)
#include "game_options.h"           // GetGameOption (FUN_005792b0)
#include "globals.h"                // G_RenderSurfaceWidth / Height, GetClientFrameTimeCapped
#include "widescreen.h"   // port-only (FREELANCER_WIDESCREEN_SUPPORT)
#include "hud_target.h"             // HudShipOfObjRw (FUN_00543320)
#include "ids_strings.h"            // GetIdsStringWWithRawFallback (FUN_004347e0)
#include "input_manager.h"          // GetClientSoftwareCursor (DAT_00616840 / 44)
#include "nn_inventory.h"           // IsPlayerDockedRetail (FUN_004c4810)
#include "nn_text_scratch.h"        // G_NnTextScratchB (DAT_0066fc60)
#include "nn_ui_pulse.h"            // NnUiPulseBlendArgb, NnReputationColorArgb, the palette
#include "player_behavior.h"        // GetPlayerObjRW (FUN_0054baf0)
#include "player_spawn.h"           // GetPlayerWaypointCountRetail (FUN_004c4690)
#include "rich_text_win.h"          // RichTextWin : WinControl
#include "space_camera.h"           // FSpaceCamera, AsCamera
#include "star_sphere.h"            // Win3DBindModelview (FUN_00422690)
#include "text_win.h"               // TextWinProjectExtent / TextWinProjectPoint
#include "texture_shapes.h"         // TexShapeGetGpuHandle (FUN_00425640)
#include "ui_scroll.h"              // UiScroll : WinControl
#include "win3d.h"                  // kWin3DVisible, Win3DGetCamera
#include "win3d_immediate.h"        // Win3DBegin / Vertex / Color / Viewport ...
#include "win3d_render.h"           // Win3DApplyCamera (FUN_00422950)
#include "win3d_renderables.h"      // Win3DFlushBeforeOverlayDraw (FUN_0045a9f0)
#include "wire_controls.h"          // WireControlsJoinColumn (FUN_005a4b50)
#include "x86math.h"                // FTransform, FMatrix, FVector, I3DMathEngine
#include "common_cobject.h"         // CShip::get_target
#include "common_csys.h"            // Acquire3DMathEngine (FUN_005b73e0), Transform
#include "common_iface.h"           // IObjRW
#include "common_reputation.h"      // Reputation::Vibe::GetGroupFeelingsTowards
#include "common_misc.h"            // SinglePlayer
#include "common_universe.h"
#include "DALib.h"                  // DALib::Engine / Pipeline
#include "dacom.h"                  // FDUMP
#include "engine/engbase.h"         // IEngine
#include "font.h"                   // IFont
#include "render/IRenderPipeline.h"

#include <math.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>

// DAT_006720ac / b0 / b4 and the three buffer handles DAT_006720b8 / bc / c0 (nn_navmap.cpp).
extern IRPVertexBuffer* G_NavMapVertexBuffer;
extern IRPIndexBuffer* G_NavMapIndexBuffer;
extern IRPDraw* G_NavMapDraw;
extern void* G_NavMapBufferHandles[3];

namespace
{
    // s_E:\FL\Scratch\Source\Client\nn\NN_NavMap.cpp -- the source file retail's viewport calls
    // name (0x00492aaa, 0x0049329f).
    char const kNavMapCpp[] = "E:\\FL\\Scratch\\Source\\Client\\nn\\NN_NavMap.cpp";
    char const kNavMapLegendMesh[] = "Interface\\NeuroNet\\NavMap\\NewNavMap\\NAV_legend.3db";
    char const kNavMapGroupMarkerMesh[] =
        "Interface\\NeuroNet\\NavMap\\NewNavMap\\SpaceObjects\\NAV_playership.3db";

    // DAT_005c7f68 -- 1/255 as the float 0x3b808081; DAT_005c75d8 -- 255.0f.
    float const kNavMapInv255 = 0.003921568859368563f;
    float const kNavMap255 = 255.0f;
    // DAT_005d3e0c -- a system-view item is drawn only from this size on (Size * SystemScale).
    float const kNavMapMinDrawSize = 550.0f;
    // DAT_005d3e08 -- the zoomed view draws an item only within this (squared) of the pan.
    float const kNavMapZoomDrawDist2 = 0.30000001192092896f; // 0x3e99999a
    // DAT_005d3e04 -- the half gap between a two-digit waypoint number's digits.
    float const kNavMapDigitHalfGap = 0.00800000037997961f; // 0x3c03126f
    // 0x3ecccccd -- a numberless waypoint marker is drawn at 0.4 of its size.
    float const kNavMapPlainMarkerScale = 0.4000000059604645f;
    // 0xbf35c28f -- the flat map's depth in the info-card / legend / base-list modes.
    float const kNavMapFlatDepth = -0.7099999785423279f;
    // 0xbf0ccccc -- the depth of the zone shading, terrain, trade lanes and group markers (one
    // ulp above -0.55f).
    float const kNavMapZoneDepth = -0.5499999523162842f;
    // DAT_005d3c50 / DAT_005d3c48 (doubles) -- the legend mesh's stretch.
    double const kNavMapLegendScaleX = 1.175;
    double const kNavMapLegendScaleY = 1.5;
    // DAT_005d3e88 (double) -- a quarter turn as retail spells it (not the nearest double to
    // pi/2: 0x3ff921fb54411744).
    double const kNavMapQuarterTurn = 1.57079632675;
    // 0x3ba3d70a / 0x3b656042 -- the band half-widths.
    float const kNavMapLinkWidth = 0.004999999888241291f;
    float const kNavMapRouteWidth = 0.003500000108033419f;
    // 0x3f000000 -- the texture's centre line.
    float const kNavMapHalf = 0.5f;
    // DAT_005c89b8 / DAT_005c89c0 / DAT_005c75e8 (doubles).
    double const kNavMapOne = 1.0;
    double const kNavMapByteMax = 255.0;
    double const kNavMapHalfD = 0.5;
    // DAT_005d3d30 -- a trade lane animation frame lasts 1/15 s.
    float const kNavMapTradeLaneFrameTime = 0.06666667014360428f; // 0x3d888889
    // DAT_005d3e98 / DAT_005d3e94 / DAT_005d3e90 -- the trade lane half width from the zoom.
    float const kNavMapLaneWidthPerZoom = 0.1315789520740509f;  // 0x3e06bca2
    float const kNavMapLaneWidthScale = 0.0020000000949949026f; // 0x3b03126f
    float const kNavMapLaneWidthBase = 0.003000000026077032f;   // 0x3b449ba6
    // DAT_005d3f04 / DAT_005d3f00 -- pi/16 and pi/8 as floats (the zone outlines' steps).
    float const kNavMapStep32 = 0.19634954631328583f; // 0x3e490fdb
    float const kNavMapStep16 = 0.39269909262657166f; // 0x3ec90fdb
    // DAT_005d29b0 -- the terrain texture's scale per zoom.
    float const kNavMapTerrainUvScale = 0.09000000357627869f; // 0x3db851ec
    // DAT_005d3f0c / DAT_005d3f08 -- the terrain alpha during a transition: 191 / zoom + 64.
    float const kNavMapTerrainAlphaSpan = 191.0f;
    float const kNavMapTerrainAlphaBase = 64.0f;
    // DAT_005ca220 -- the brightening of a 0x8000 zone's colour.
    float const kNavMapZoneColourBoost = 1.5f;
    // DAT_005d2e2c / DAT_005d2e30 -- the hostile / friendly bounds of the zone filter.
    float const kNavMapHostileFeel = -0.6000000238418579f; // 0xbf19999a
    float const kNavMapFriendlyFeel = 0.6000000238418579f; // 0x3f19999a
    // DAT_005d3e4c -- the group markers' size factor.
    float const kNavMapGroupMarkerFactor = 60.0f;
    // DAT_005d3eec / DAT_005d3ee8 -- a label's height from the font's fixed height.
    float const kNavMapLabelHeightScale = 33.333335876464844f;   // 0x42055556
    float const kNavMapLabelHeightFactor = 0.017999999225139618f; // 0x3c9374bc
    // DAT_005d043c / DAT_005d3ee4 -- the label frame's half extents.
    float const kNavMapLabelFrameX = 0.20000000298023224f; // 0x3e4ccccd
    float const kNavMapLabelFrameY = 0.18000000715255737f; // 0x3e3851ec
    // DAT_005cb88c / DAT_005d3ee0 / 0x3cf5c28f -- the label's lift, the second projection's
    // depth offset and the projected box's height.
    float const kNavMapLabelLift = 0.05000000074505806f;  // 0x3d4ccccd
    float const kNavMapLabelBack = 0.10000000149011612f;  // 0x3dcccccd
    float const kNavMapLabelBoxH = 0.029999999329447746f; // 0x3cf5c28f
    // DAT_005d3ed8 / DAT_005d3ed0 (doubles) -- the rect test shrinks X by 0.9 and widens W by 1.2.
    double const kNavMapLabelRectX = 0.9;
    double const kNavMapLabelRectW = 1.2;
    // DAT_005d2ee8 -- a placed label reserves 1.4 times its size.
    float const kNavMapLabelReserve = 1.399999976158142f; // 0x3fb33333
    // DAT_005d3efc / DAT_005d3ef8 / DAT_005d3ef4 / DAT_005d3ef0 -- the label frame's bounds.
    float const kNavMapLabelMaxRight = 0.20999999344348907f; // 0x3e570a3d
    float const kNavMapLabelMinLeft = -0.20999999344348907f; // 0xbe570a3d
    float const kNavMapLabelMaxDrop = 0.19200000166893005f;  // 0x3e449ba6
    float const kNavMapLabelMinTop = -0.1979999989271164f;   // 0xbe4ac083

    // The render-state and texture-stage tables at 0x005d2e34 / 0x005d2e4c / 0x005d2e5c /
    // 0x005d2e94 ({state, value} pairs up to a zero state).
    struct FNavMapStatePair
    {
        int State;
        int Value;
    };
    FNavMapStatePair const kNavMapRsOn[] = {{0x8d, 1}, {0x91, 1}, {0, 0}};             // 5d2e34
    FNavMapStatePair const kNavMapRsOff[] = {{0x8d, 0}, {0, 0}};                        // 5d2e4c
    FNavMapStatePair const kNavMapTssTexture[] = {{1, 4}, {2, 2}, {3, 0}, {4, 4},
                                                  {5, 2}, {6, 0}, {0, 0}};              // 5d2e5c
    FNavMapStatePair const kNavMapTssDiffuse[] = {{1, 1}, {2, 2}, {3, 1}, {4, 1},
                                                  {5, 2}, {6, 1}, {0, 0}};              // 5d2e94

    void ApplyRenderStates(FNavMapStatePair const* Table)
    {
        IRenderPipeline* const Pipeline = DALib::Pipeline;
        for (FNavMapStatePair const* Pair = Table; Pair->State != 0; ++Pair)
            Pipeline->SetRenderState((D3DRENDERSTATETYPE)Pair->State, Pair->Value); // +0xf0
    }

    void ApplyStageStates(int Stage, FNavMapStatePair const* Table)
    {
        IRenderPipeline* const Pipeline = DALib::Pipeline;
        for (FNavMapStatePair const* Pair = Table; Pair->State != 0; ++Pair)
            Pipeline->SetTextureStageState(Stage, Pair->State, Pair->Value); // +0xf8
    }

    // The lighting (D3DRS_LIGHTING, 0x89) saver the three pipeline drawers hold on the stack
    // (its unwind funclet LAB_005bca5b / LAB_005bcadb restores it): the old value read, lighting
    // off, the old value put back on every way out.
    class FNavMapLightingOff
    {
    public:
        FNavMapLightingOff() : Pipeline(DALib::Pipeline), State(0x89), Saved(0)
        {
            Pipeline->GetRenderState((D3DRENDERSTATETYPE)State, &Saved); // +0xf4
            Pipeline->SetRenderState((D3DRENDERSTATETYPE)State, 0);
        }
        ~FNavMapLightingOff()
        {
            Pipeline->SetRenderState((D3DRENDERSTATETYPE)State, Saved);
        }

    private:
        FNavMapLightingOff(FNavMapLightingOff const&) = delete;
        FNavMapLightingOff& operator=(FNavMapLightingOff const&) = delete;

        IRenderPipeline* Pipeline;
        int State;
        int Saved;
    };

    // The vertex-buffer half the three pipeline drawers share (FUN_0049dcc0 0x0049dee4..
    // 0x0049e036, FUN_0049c260 0x0049c3f4..0x0049c53e, FUN_004992b0 0x00499561..0x004996a6):
    // keep the buffer when nothing was rebuilt and SelectVb takes it; otherwise destroy it,
    // create one for Count vertices of Fvf, copy Desc in (*OutOffset zeroed first, then the
    // copy's offset) and select it. False on any failure.
    bool NavMapUploadVertices(bool bRebuilt, void** Handle, unsigned Fvf, int Count,
                              FRpVertexSrcDesc* Desc, unsigned* OutOffset)
    {
        IRPVertexBuffer* const Vb = G_NavMapVertexBuffer;
        if (!bRebuilt && Vb->SelectVb(*Handle) == 0) // +0x28
            return true;
        if (*Handle != nullptr)
        {
            Vb->DestroyVb(Handle); // +0x10
            *Handle = nullptr;
        }
        if (Vb->CreateVb(Fvf, static_cast<unsigned>(Count), reinterpret_cast<unsigned**>(Handle),
                         0) != 0) // +0x0c
            return false;
        *OutOffset = 0;
        if (Vb->CopyVertices(*Handle, OutOffset, Desc, 0, (unsigned int)Count) != 0) // +0x18
            return false;
        return Vb->SelectVb(*Handle) == 0;
    }

    // The two vertex layouts: FVF 0x42 {position, diffuse} (0x10 bytes) and FVF 0x142
    // {position, diffuse, uv} (0x18 bytes).
    struct FNavMapColorVertex
    {
        float X;
        float Y;
        float Z;
        unsigned long Diffuse;
    };
    struct FNavMapTexVertex
    {
        float X;
        float Y;
        float Z;
        unsigned long Diffuse;
        float U;
        float V;
    };

    // The source descriptors (all dwords zeroed, then Flags 2, the streams and the FVF).
    FRpVertexSrcDesc MakeColorDesc(unsigned char const* Vertices, int Count)
    {
        FRpVertexSrcDesc Desc;
        memset(&Desc, 0, sizeof(Desc));
        Desc.Flags = 2;
        Desc.Position.Base = Vertices;
        Desc.Position.ByteSize = 0xc;
        Desc.Position.Stride = 0x10;
        Desc.Diffuse.Base = Vertices + 0xc;
        Desc.Diffuse.ByteSize = 4;
        Desc.Diffuse.Stride = 0x10;
        Desc.SrcFvf = 0x42;
        Desc.SrcCount = static_cast<unsigned>(Count);
        return Desc;
    }
    FRpVertexSrcDesc MakeTexDesc(unsigned char const* Vertices, int Count)
    {
        FRpVertexSrcDesc Desc;
        memset(&Desc, 0, sizeof(Desc));
        Desc.Flags = 2;
        Desc.Position.Base = Vertices;
        Desc.Position.ByteSize = 0xc;
        Desc.Position.Stride = 0x18;
        Desc.Diffuse.Base = Vertices + 0xc;
        Desc.Diffuse.ByteSize = 4;
        Desc.Diffuse.Stride = 0x18;
        Desc.Tex[0].Base = Vertices + 0x10;
        Desc.Tex[0].ByteSize = 8;
        Desc.Tex[0].Stride = 0x18;
        Desc.SrcFvf = 0x142;
        Desc.SrcCount = static_cast<unsigned>(Count);
        return Desc;
    }

    I3DMathEngine* NavMapMath()
    {
        if (G_NavMapMath == nullptr)
            G_NavMapMath = Acquire3DMathEngine(); // FUN_005b73e0
        return G_NavMapMath;
    }

    // The modelview every map drawer binds (FUN_00497360 0x0049736c..0x0049743d and its
    // copies): the rotation +0x3d4 with the offset (0, 0, Depth), pushed through
    // I3DMathEngine::TransformXform (slot 10) of the zero vector, bound with FUN_00422690(0, ...).
    void BindMapModelview(NeuroNetNavMap const* Map, float Depth)
    {
        FTransform Offset;
        memcpy(&Offset.Rotation, Map->Field_0x3d4, sizeof(Offset.Rotation));
        Offset.Translation.X = 0.0f;
        Offset.Translation.Y = 0.0f;
        Offset.Translation.Z = Depth;
        FTransform Model;
        memcpy(&Model.Rotation, Map->Field_0x3d4, sizeof(Model.Rotation));
        FVector Zero;
        Zero.X = 0.0f;
        Zero.Y = 0.0f;
        Zero.Z = 0.0f;
        I3DMathEngine* const Math = NavMapMath();
        Math->TransformXform(Model.Translation, Offset, Zero);
        Win3DBindModelview(nullptr, reinterpret_cast<float const*>(&Model)); // FUN_00422690
    }

    // BindMapModelview, then lighting, depth test, culling and texturing off, blending on at
    // SRCALPHA / INVSRCALPHA (0x00497442..0x00497478).
    void BeginMapLines(NeuroNetNavMap const* Map, float Depth)
    {
        BindMapModelview(Map, Depth);
        Win3DDisable(kWin3DLightingCap); // FUN_00421ed0
        Win3DDisable(kWin3DDepthTest);
        Win3DDisable(kWin3DCullFace);
        Win3DDisable(kWin3DTexture2D);
        Win3DEnable(kWin3DBlend);        // FUN_00421dd0
        Win3DSetBlendFunc(kWin3DBlendSrcAlpha, kWin3DBlendInvSrcAlpha); // FUN_004220c0
    }

    // The map frame the zone / lane builders share (FUN_00498bd0 0x00498cb6..0x00498e58,
    // FUN_0049b600 0x0049b7a2..0x0049b94f, FUN_0049c740 0x0049ca08..0x0049cb91): the origin of
    // the map in map-frame units at the current pan / zoom, and MapSize scaled by the zoom. The
    // rounding of these three is the same and differs from FUN_004a1ce0 / FUN_00499c20:
    // Origin.x adds the UNROUNDED Pan.x * Zoom, Origin.y / .z the rounded ones, and the z term
    // HalfZ * (Zoom - 1) stays unrounded.
    struct FNavMapFrame
    {
        float Origin[3];
        float Size[3];
    };

    FNavMapFrame MakeZoneFrame(NeuroNetNavMap const* Map)
    {
        double const Zoom = static_cast<double>(Map->ZoomScale);
        float const Half0 = Map->MapSize[0] * 0.5f; // exact
        float const Half1 = Map->MapSize[1] * 0.5f;
        float const Half2 = Map->MapSize[2] * 0.5f;
        float const ZoomM1 = static_cast<float>(Zoom - 1.0);
        float const Term0 =
            static_cast<float>(static_cast<double>(Half0) * static_cast<double>(ZoomM1));
        double const Term1 = static_cast<double>(Half1) * static_cast<double>(ZoomM1);
        double const Term2 = static_cast<double>(Half2) * static_cast<double>(ZoomM1);
        float const Base0 =
            static_cast<float>(static_cast<double>(-Half0) - static_cast<double>(Term0));
        float const Base1 = static_cast<float>(static_cast<double>(-Half1) - Term1);
        double const Base2 = static_cast<double>(-Half2) - Term2;
        double const Pan0 = static_cast<double>(Map->PanOffset[0]) * Zoom;
        float const Pan1 = static_cast<float>(static_cast<double>(Map->PanOffset[1]) * Zoom);
        float const Pan2 = static_cast<float>(static_cast<double>(Map->PanOffset[2]) * Zoom);
        FNavMapFrame Frame;
        Frame.Origin[0] = static_cast<float>(Pan0 + static_cast<double>(Base0));
        Frame.Origin[1] = static_cast<float>(static_cast<double>(Pan1) + static_cast<double>(Base1));
        Frame.Origin[2] = static_cast<float>(static_cast<double>(Pan2) + Base2);
        Frame.Size[0] = static_cast<float>(static_cast<double>(Map->MapSize[0]) * Zoom);
        Frame.Size[1] = static_cast<float>(static_cast<double>(Map->MapSize[1]) * Zoom);
        Frame.Size[2] = static_cast<float>(static_cast<double>(Map->MapSize[2]) * Zoom);
        return Frame;
    }

    // A point in map units to the map frame, in place (the builders' projection loops, e.g.
    // FUN_00498bd0 0x00498ef0..0x00498f67): x from (EX + x) / 2EX, y from 1 - (EZ + z) / 2EZ,
    // z the frame's depth. 2EX / 2EZ are rounded to float here (they are not in FUN_004a1ce0).
    void ProjectToFrame(NeuroNetNavMap const* Map, FNavMapFrame const& Frame, float* Point)
    {
        float const ExtentX = Map->MapExtentX;
        float const ExtentZ = Map->MapExtentZ;
        float const TwoX = static_cast<float>(static_cast<double>(ExtentX) + static_cast<double>(ExtentX));
        float const TwoZ = static_cast<float>(static_cast<double>(ExtentZ) + static_cast<double>(ExtentZ));
        double const X = (static_cast<double>(ExtentX) + static_cast<double>(Point[0])) /
                         static_cast<double>(TwoX) * static_cast<double>(Frame.Size[0]);
        double const Y = (kNavMapOne - (static_cast<double>(ExtentZ) + static_cast<double>(Point[2])) /
                                           static_cast<double>(TwoZ)) *
                         static_cast<double>(Frame.Size[1]);
        Point[0] = static_cast<float>(static_cast<double>(Frame.Origin[0]) + X);
        Point[1] = static_cast<float>(static_cast<double>(Frame.Origin[1]) + Y);
        Point[2] = static_cast<float>(static_cast<double>(Frame.Size[2]) +
                                      static_cast<double>(Frame.Origin[2]));
    }

    // The zone's own visit byte (IZone slot 0x80) or'ed with the visit map's flags
    // (FUN_004c4da0), tested against Bit -- the inline sequence of FUN_0049b600 / FUN_0049c740.
    bool IsZoneVisited(Universe::IZone const* Zone, unsigned char Bit)
    {
        unsigned char Flags = 0;
        GetPlayerVisitFlagsRetail(Zone->GetId(), &Flags, 1, 0); // FUN_004c4da0(id, &flags, 1, 0)
        unsigned char const Own = Zone->GetVisit();
        return ((Flags | Own) & Bit) != 0;
    }

    // Win3DColor4ub with the four bytes of an RGBA quad (every FUN_00421b70 call of the band
    // drawers loads them from memory as R, G, B, A).
    void BandColor(unsigned char const* Rgba)
    {
        Win3DColor4ub(Rgba[0], Rgba[1], Rgba[2], Rgba[3]); // FUN_00421b70
    }

    // The corners FUN_00498470 / FUN_00498730 compute from the segment From -> To and the
    // half width: the side offset from the angle atan2(dx, dy) + a quarter turn, with retail's
    // mixed rounding (the x offset is used unrounded once, the y offset is float(float(cos) * W)).
    struct FNavMapBand
    {
        float Near1[2]; // From + side (x unrounded side, y rounded)
        float Far1[2];  // To + side (both rounded)
        float Near2[2]; // From - side
        float Far2[2];  // To - side
    };

    FNavMapBand MakeBand(float const* From, float const* To, float Width)
    {
        // 0x00498473..0x0049850e: FPATAN of (To.x - From.x, To.y - From.y) plus the constant,
        // FSIN / FCOS of it; cos is stored as a float before its multiply, sin is not.
        double const Dx = static_cast<double>(To[0]) - static_cast<double>(From[0]);
        double const Dy = static_cast<double>(To[1]) - static_cast<double>(From[1]);
        double const Angle = atan2(Dx, Dy) + kNavMapQuarterTurn;
        float const CosF = static_cast<float>(cos(Angle));
        double const SideX = sin(Angle) * static_cast<double>(Width);
        float const SideXF = static_cast<float>(SideX);
        float const SideYF =
            static_cast<float>(static_cast<double>(CosF) * static_cast<double>(Width));
        FNavMapBand Band;
        Band.Near1[0] = static_cast<float>(SideX + static_cast<double>(From[0]));
        Band.Near1[1] = static_cast<float>(static_cast<double>(SideYF) + static_cast<double>(From[1]));
        Band.Far1[0] = static_cast<float>(static_cast<double>(SideXF) + static_cast<double>(To[0]));
        Band.Far1[1] = static_cast<float>(static_cast<double>(SideYF) + static_cast<double>(To[1]));
        // 0x004985d9..0x0049864a (0x004988ea..0x00498957): the other side, from the rounded
        // offsets.
        Band.Near2[0] = static_cast<float>(static_cast<double>(From[0]) - static_cast<double>(SideXF));
        Band.Near2[1] = static_cast<float>(static_cast<double>(From[1]) - static_cast<double>(SideYF));
        Band.Far2[0] = static_cast<float>(static_cast<double>(To[0]) - static_cast<double>(SideXF));
        Band.Far2[1] = static_cast<float>(static_cast<double>(To[1]) - static_cast<double>(SideYF));
        return Band;
    }

    // FUN_00498470 (cdecl From, To by value, CenterRgba, EdgeRgba, Width) -- a band of two
    // triangle pairs either side of From -> To, CenterRgba on the line, EdgeRgba at the edges
    // (the caller's Win3DBegin(4) triangle list).
    void NavMapDrawBand(float const* From, float const* To, unsigned char const* CenterRgba,
                        unsigned char const* EdgeRgba, float Width)
    {
        FNavMapBand const Band = MakeBand(From, To, Width);
        BandColor(CenterRgba);
        Win3DVertex3f(From[0], From[1], From[2]); // FUN_00421cc0
        BandColor(EdgeRgba);
        Win3DVertex3f(Band.Near1[0], Band.Near1[1], From[2]);
        BandColor(CenterRgba);
        Win3DVertex3f(To[0], To[1], To[2]);
        Win3DVertex3f(To[0], To[1], To[2]);
        BandColor(EdgeRgba);
        Win3DVertex3f(Band.Far1[0], Band.Far1[1], To[2]);
        Win3DVertex3f(Band.Near1[0], Band.Near1[1], From[2]);

        BandColor(CenterRgba);
        Win3DVertex3f(From[0], From[1], From[2]);
        BandColor(EdgeRgba);
        Win3DVertex3f(Band.Near2[0], Band.Near2[1], From[2]);
        BandColor(CenterRgba);
        Win3DVertex3f(To[0], To[1], To[2]);
        Win3DVertex3f(To[0], To[1], To[2]);
        BandColor(EdgeRgba);
        Win3DVertex3f(Band.Far2[0], Band.Far2[1], To[2]);
        Win3DVertex3f(Band.Near2[0], Band.Near2[1], From[2]);
    }

    // FUN_00498730 -- FUN_00498470 with texture coordinates (FUN_00421ca0): u 0.5 on the line,
    // 1 at the edges; v 0 at From, 1 at To.
    void NavMapDrawTexturedBand(float const* From, float const* To,
                                unsigned char const* CenterRgba, unsigned char const* EdgeRgba,
                                float Width)
    {
        FNavMapBand const Band = MakeBand(From, To, Width);
        BandColor(CenterRgba);
        Win3DTexCoord2f(kNavMapHalf, 0.0f);
        Win3DVertex3f(From[0], From[1], From[2]);
        BandColor(EdgeRgba);
        Win3DTexCoord2f(1.0f, 0.0f);
        Win3DVertex3f(Band.Near1[0], Band.Near1[1], From[2]);
        BandColor(CenterRgba);
        Win3DTexCoord2f(kNavMapHalf, 1.0f);
        Win3DVertex3f(To[0], To[1], To[2]);
        Win3DTexCoord2f(kNavMapHalf, 1.0f);
        Win3DVertex3f(To[0], To[1], To[2]);
        BandColor(EdgeRgba);
        Win3DTexCoord2f(1.0f, 1.0f);
        Win3DVertex3f(Band.Far1[0], Band.Far1[1], To[2]);
        Win3DTexCoord2f(1.0f, 0.0f);
        Win3DVertex3f(Band.Near1[0], Band.Near1[1], From[2]);

        BandColor(CenterRgba);
        Win3DTexCoord2f(kNavMapHalf, 0.0f);
        Win3DVertex3f(From[0], From[1], From[2]);
        BandColor(EdgeRgba);
        Win3DTexCoord2f(1.0f, 0.0f);
        Win3DVertex3f(Band.Near2[0], Band.Near2[1], From[2]);
        BandColor(CenterRgba);
        Win3DTexCoord2f(kNavMapHalf, 1.0f);
        Win3DVertex3f(To[0], To[1], To[2]);
        Win3DTexCoord2f(kNavMapHalf, 1.0f);
        Win3DVertex3f(To[0], To[1], To[2]);
        BandColor(EdgeRgba);
        Win3DTexCoord2f(1.0f, 1.0f);
        Win3DVertex3f(Band.Far2[0], Band.Far2[1], To[2]);
        Win3DTexCoord2f(1.0f, 0.0f);
        Win3DVertex3f(Band.Near2[0], Band.Near2[1], From[2]);
    }

    // The mesh draw every map piece goes through: IRenderer2 slot 7 (Camera, instance, 1.0f,
    // Flags, 0, 0) -- FUN_00421120's body, inlined at each retail site.
    void DrawHolder(FMeshHolder* Holder, FSpaceCamera* Camera, unsigned long Flags)
    {
        ClientMeshHolderRender(Holder, Camera, Flags);
    }

    bool IsShown(WinControl const* Control)
    {
        return (Control->Flags & kWin3DVisible) != 0;
    }

    // The zone qualifiers of FUN_0049c740's two walks (0x0049c883..0x0049c98e and
    // 0x0049cbd2..0x0049cdf4) by the filter mode: 1 populated zones without IZone slot 0x70,
    // 2 populated ones with it, 5 zones with visit bit 2, anything else populated zones among
    // the best path's mission zones.
    bool ShadeZoneQualifies(Universe::IZone const* Zone, int Population, int FilterMode,
                            std::vector<unsigned> const& MissionZoneIds, int MissionZoneCount)
    {
        if (FilterMode == 1)
            return Population != 0 && !Zone->HasPathLabel();
        if (FilterMode == 2)
            return Population != 0 && Zone->HasPathLabel();
        if (FilterMode == 5)
            return IsZoneVisited(Zone, 4);
        if (Population == 0)
            return false;
        unsigned const Id = Zone->GetId();
        for (int Index = 0; Index < MissionZoneCount; ++Index)
        {
            if (MissionZoneIds[static_cast<size_t>(Index)] == Id)
                return true;
        }
        return false;
    }

    // The jitter FUN_0049b600 gives a terrain point unless the zone is a 0x30000 one
    // (0x0049bd82..0x0049bdbe): from v = ftol(x), x += ((v & 0xff) - 0x80) << 5 and
    // y += ((v >> 3) & 0x1fe0) - 0x1000 (arithmetic shift).
    void JitterTerrainPoint(float* Point)
    {
        int const Whole = static_cast<int>(Point[0]); // ftol
        int const JitterX = ((Whole & 0xff) - 0x80) << 5;
        Point[0] = static_cast<float>(static_cast<double>(JitterX) + static_cast<double>(Point[0]));
        int const JitterY = ((Whole >> 3) & 0x1fe0) - 0x1000;
        Point[1] = static_cast<float>(static_cast<double>(JitterY) + static_cast<double>(Point[1]));
    }

    // A zone's transform as FUN_0049b600 / FUN_0049c740 use it: IZone slot 0x0c with the
    // position's x / z scaled by SystemScale and its y zeroed.
    FTransform ZoneMapTransform(Universe::IZone const* Zone, float SystemScale)
    {
        FTransform Xform;
        memcpy(&Xform, &Zone->GetTransform(), sizeof(Xform));
        Xform.Translation.X = static_cast<float>(static_cast<double>(Xform.Translation.X) *
                                                 static_cast<double>(SystemScale));
        Xform.Translation.Z = static_cast<float>(static_cast<double>(Xform.Translation.Z) *
                                                 static_cast<double>(SystemScale));
        Xform.Translation.Y = 0.0f;
        return Xform;
    }

    // IZone::GetSize scaled by SystemScale (each component rounded).
    Vector ZoneMapSize(Universe::IZone const* Zone, float SystemScale)
    {
        Vector Size = Zone->GetSize();
        Size.x = static_cast<float>(static_cast<double>(Size.x) * static_cast<double>(SystemScale));
        Size.y = static_cast<float>(static_cast<double>(Size.y) * static_cast<double>(SystemScale));
        Size.z = static_cast<float>(static_cast<double>(Size.z) * static_cast<double>(SystemScale));
        return Size;
    }

    // The ellipse point of the zone outlines (FUN_0049b600 0x0049bce5..0x0049bd7d, FUN_0049c740
    // 0x0049d215..0x0049d2bf): the direction (sin, 0, cos) of Angle through IZone slot 0x40,
    // times SystemScale, about the zone's scaled position.
    void ZoneEllipsePoint(Universe::IZone const* Zone, float Angle, float SystemScale,
                          FTransform const& Xform, float* OutPoint)
    {
        Vector Dir;
        Dir.x = static_cast<float>(sin(static_cast<double>(Angle)));
        Dir.y = 0.0f;
        Dir.z = static_cast<float>(cos(static_cast<double>(Angle)));
        double const Radius = static_cast<double>(Zone->GetRadiusAlong(Dir)) * static_cast<double>(SystemScale);
        float const Mid = static_cast<float>(static_cast<double>(Dir.y) * Radius);
        float const Far = static_cast<float>(static_cast<double>(Dir.z) * Radius);
        OutPoint[0] = static_cast<float>(static_cast<double>(Dir.x) * Radius +
                                         static_cast<double>(Xform.Translation.X));
        OutPoint[1] = static_cast<float>(static_cast<double>(Mid) +
                                         static_cast<double>(Xform.Translation.Y));
        OutPoint[2] = static_cast<float>(static_cast<double>(Far) +
                                         static_cast<double>(Xform.Translation.Z));
    }

    // TransformXform of a local point through Xform into OutPoint.
    void TransformPoint(FTransform const& Xform, float X, float Y, float Z, float* OutPoint)
    {
        FVector Local;
        Local.X = X;
        Local.Y = Y;
        Local.Z = Z;
        FVector World;
        NavMapMath()->TransformXform(World, Xform, Local); // slot 10 (+0x28)
        OutPoint[0] = World.X;
        OutPoint[1] = World.Y;
        OutPoint[2] = World.Z;
    }
}

// ----------------------------------------------------------------------------------------
// Draw (slot 13)
// ----------------------------------------------------------------------------------------

void NeuroNetNavMap::Draw()
{
    // LAB_004926d0 0x004926dc..0x0049293d: the frame's controls as two null-terminated lists on
    // the stack, taken at entry (the first ends at the zero stored at +0xbc).
    WinControl* const Panels[14] = {RightWindow,
                                    LeftWindow,
                                    InfoTitle,
                                    static_cast<WinControl*>(InfoText),
                                    ClearAllWaypointsButton,
                                    ClearAllWaypointsText,
                                    AbortMissionButton,
                                    AbortMissionText,
                                    AbortYesButton,
                                    AbortYesText,
                                    AbortNoButton,
                                    AbortNoText,
                                    static_cast<WinControl*>(ObjectiveScroll),
                                    nullptr};
    WinControl* const Tools[34] = {UniverseButton,   LabelButton,       ShipButton,
                                   CloseButton,      DebugText,         NoBestPathText,
                                   PhysicalFilter,   LegendFilter,      BaseListFilter,
                                   PoliticalFilter,  FactionLabels[0],  FactionLabels[1],
                                   FactionLabels[2], FactionLabels[3],  PatrolFilter,
                                   MissionFilter,    MineableFilter,    FilterTitle,
                                   AllFilter,        HostileFilter,     FriendlyFilter,
                                   InZoomButton,     OutZoomButton,     InfoButton,
                                   ClearWaypointButton, OptionBridge,   SetWaypointButton,
                                   BestPathButton,   JumpGateButton,    SelectObjectButton,
                                   static_cast<WinControl*>(InfoCard),
                                   CloseInfocardButton, CloseInfocardText, nullptr};

    // 0x00492944..0x00492960: the side panels' controls first (slot 13 on the visible ones).
    for (int Index = 0; Panels[Index] != nullptr; ++Index)
    {
        if (IsShown(Panels[Index]))
            Panels[Index]->Draw();
    }

    // 0x00492962..0x0049298a: the briefing's three, untested.
    if (bBriefing != 0)
    {
        MissionBriefingText->Draw();
        ContinueButton->Draw();
        ContinueText->Draw();
    }

    // 0x0049298d..0x004929f3: the objective rows (the hovered one, HoveredObjective, last, over
    // the rest), then their wire joins (FUN_005a4b50).
    for (int Index = 0; Index < 3; ++Index)
    {
        WinControl* const Objective = MissionObjective[Index];
        if (Objective != nullptr && IsShown(Objective) && Index != HoveredObjective)
            Objective->Draw();
        WinControl* const Text = ObjectiveText[Index];
        if (Text != nullptr && IsShown(Text))
            Text->Draw();
    }
    for (int Index = 0; Index < 3; ++Index)
    {
        WinControl* const Objective = MissionObjective[Index];
        if (Objective != nullptr && IsShown(Objective) && Index == HoveredObjective)
            Objective->Draw();
    }
    WireControlsJoinColumn(MissionObjective, 3); // FUN_005a4b50

    // 0x00492a00..0x00492a25: flush the overlay batches while the panels slide, or while they
    // are shut with the clear-all button up.
    if (ButtonMover::IsActive() ||
        (bSidePanelsOpen == 0 && IsShown(ClearAllWaypointsButton))) // FUN_0055e280
        Win3DFlushBeforeOverlayDraw(); // FUN_0045a9f0

    WinControl::DrawStaticMesh(); // FUN_005a06a0

    // 0x00492a31..0x00492ac3: the viewport cut to the map frame (clipped to the surface), the
    // map camera in; the old viewport kept.
    int SavedViewport[4];
    Win3DGetViewport(0xba2, SavedViewport); // FUN_00422390
    int Width = static_cast<int>(ScreenRectSize[0]);  // ftol
    int Height = static_cast<int>(ScreenRectSize[1]);
    int Top = static_cast<int>(ScreenRectMin[1]);
    int Left = static_cast<int>(ScreenRectMin[0]);
    int const SurfaceHeight = static_cast<int>(G_RenderSurfaceHeight); // DAT_00610854
    if (Top + Height > SurfaceHeight)
        Height = SurfaceHeight - Top;
    if (Left + Width > static_cast<int>(G_RenderSurfaceWidth)) // DAT_00610850
        Width = static_cast<int>(G_RenderSurfaceWidth) - Left;
    if (Top < 0)
        Top = 0;
    if (Left < 0)
        Left = 0;
    Win3DSetViewportRect(Left, SurfaceHeight - Top - Height, Width, Height, kNavMapCpp,
                         0x838);                  // FUN_00421620
    Win3DApplyCamera(AsCamera(&Camera));          // FUN_00422950

    int const FilterMode = G_NavMapFilterMode; // DAT_006720a0
    if (FilterMode != 6 && FilterMode != 7 && FilterMode != 4)
    {
        // 0x00492aeb..0x00492b8a: the map meshes by view -- the flat one on the universe view,
        // the zoomed ones in a system (and while a transition heads for them).
        if (ViewMode == 0)
            DrawHolder(&PrettyMapMesh, &Camera, 0x300);
        if (ViewMode == 1 || (TransitionView == 1 && ViewMode != 0))
            DrawHolder(&ZoomedMapMesh, &Camera, 0x300);
        if (ViewMode == 2 || TransitionView == 2)
            DrawHolder(&ZoomedMapMesh2, &Camera, 0x300);

        // 0x00492b8d..0x00492bc1: the lines under the markers.
        if (ViewMode == 0)
        {
            DrawJumpLinks(); // FUN_00497360
        }
        else
        {
            DrawZoneShading();    // FUN_0049dcc0
            DrawTerrainPatches(); // FUN_0049c260
            DrawTradeLanes();     // FUN_004992b0
            DrawZoneOutlines();   // FUN_0049aa30
        }
        DrawRouteLines();       // FUN_0049fe70
        BestPathFirstWaypointNotKind3(); // FUN_004c5490 -- its answer is not used

        // 0x00492bc6..0x00492bfa: the player ship's target (its id), unless docked.
        unsigned TargetId = 0;
        if (!IsPlayerDockedRetail()) // FUN_004c4810
        {
            IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW()); // FUN_0054baf0
            if (Player != nullptr)
            {
                // FUN_00543320's CShip is handed to get_target untested, as retail does.
                IObjRW* const Target = HudShipOfObjRw(Player)->get_target();
                if (Target != nullptr)
                    TargetId = Target->get_id(); // IObjInspect slot 8 (+0x20)
            }
        }

        // 0x00492bfe..0x00492fde: the markers, with the map-marker option (0x1b) on.
        if (GetGameOption(0x1b) != 0) // FUN_005792b0
        {
            IEngine* const Engine = DALib::Engine;
            for (std::list<FNavMapItem*>::iterator It = Items.begin(); It != Items.end(); ++It)
            {
                FNavMapItem* const Item = *It;
                if (Item->bShown == 0)
                    continue;

                // 0x00492c32..0x00492cc1: out of a transition, the system view drops the small
                // ones and the zoomed view the ones away from the pan.
                if (bTransition == 0)
                {
                    if (ViewMode == 1)
                    {
                        // FCOMP DAT_005d3e0c, TEST AH,5 + JNP: skipped when less.
                        if (static_cast<double>(Item->Size) * static_cast<double>(SystemScale) <
                            static_cast<double>(kNavMapMinDrawSize))
                            continue;
                    }
                    else if (ViewMode == 2)
                    {
                        float const* const Pos = Engine->GetInstancePosition(
                            Item->Mesh.EngineInstance); // IEngine slot 36
                        double const Dx = static_cast<double>(Pos[0]) -
                                          static_cast<double>(PanOffset[0]);
                        double const Dy = static_cast<double>(Pos[1]) -
                                          static_cast<double>(PanOffset[1]);
                        // FCOMP DAT_005d3e08, TEST AH,0x41 + JZ: skipped when greater.
                        if (Dy * Dy + Dx * Dx > static_cast<double>(kNavMapZoomDrawDist2))
                            continue;
                    }
                }

                // 0x00492cc7..0x00492cfe: once drawn, the blink hides the player marker
                // (Kind 2) and the targeted star (Kind 0 with the target's id).
                if (bDrawnOnce != 0)
                {
                    if (Item->Kind == 2 && bBlink != 0)
                        continue;
                    if (Item->Kind == 0 && bBlink != 0 && Item->Id == TargetId)
                        continue;
                }

                // 0x00492d04..0x00492d2f: the waypoint markers (Kind 3 / 4) are drawn with
                // their numbers once drawn; a best-path stop without a number (-1) not at all.
                int const Number = Item->Variant;
                if (bDrawnOnce != 0 && Item->Kind == 4 && Number == -1)
                    continue;
                if (bDrawnOnce == 0 || (Item->Kind != 3 && Item->Kind != 4))
                {
                    // 0x00492f98..0x00492fcf: the transform read (unused) and the draw.
                    Engine->GetInstanceTransform(Item->Mesh.EngineInstance); // IEngine slot 40
                    DrawHolder(&Item->Mesh, &Camera, 0x308);
                    continue;
                }

                if (Number == 0)
                {
                    // 0x00492d3d..0x00492e28: no number -- drawn at 0.4 of its size, the
                    // transform put back after.
                    FTransform Saved;
                    memcpy(&Saved, Engine->GetInstanceTransform(Item->Mesh.EngineInstance),
                           sizeof(Saved));
                    FTransform Scaled = Saved;
                    NavMapMath()->Scale(Scaled.Rotation, Scaled.Rotation,
                                        kNavMapPlainMarkerScale); // slot 4
                    Engine->SetInstanceMatrix(Item->Mesh.EngineInstance, &Scaled); // slot 39
                    Engine->UpdateInstance(Item->Mesh.EngineInstance, 0, nullptr); // slot 27
                    DrawHolder(&Item->Mesh, &Camera, 0x308);
                    Engine->SetInstanceMatrix(Item->Mesh.EngineInstance, &Saved);
                    Engine->UpdateInstance(Item->Mesh.EngineInstance, 0, nullptr);
                    continue;
                }

                // 0x00492e2d..0x00492f93: the marker, then its number from the digit meshes
                // at the marker's position -- one digit, or two either side.
                DrawHolder(&Item->Mesh, &Camera, 0x308);
                float Pos[3];
                memcpy(Pos, Engine->GetInstancePosition(Item->Mesh.EngineInstance), sizeof(Pos));
                if (Number < 10)
                {
                    FClientMeshHolder* const Digit = &DigitMeshes[Number];
                    Engine->SetInstancePosition(Digit->EngineInstance, Pos);       // slot 35
                    Engine->UpdateInstance(Digit->EngineInstance, 0, nullptr);
                    DrawHolder(Digit, &Camera, 0x308);
                    continue;
                }
                int const Tens = Number / 10; // IDIV 10
                int const Ones = Number % 10;
                float Left3[3];
                Left3[0] = static_cast<float>(static_cast<double>(Pos[0]) -
                                              static_cast<double>(kNavMapDigitHalfGap));
                Left3[1] = Pos[1];
                Left3[2] = Pos[2];
                FClientMeshHolder* const TensDigit = &DigitMeshes[Tens];
                Engine->SetInstancePosition(TensDigit->EngineInstance, Left3);
                Engine->UpdateInstance(TensDigit->EngineInstance, 0, nullptr);
                DrawHolder(TensDigit, &Camera, 0x308);
                float Right3[3];
                Right3[0] = static_cast<float>(static_cast<double>(Pos[0]) +
                                               static_cast<double>(kNavMapDigitHalfGap));
                Right3[1] = Pos[1];
                Right3[2] = Pos[2];
                FClientMeshHolder* const OnesDigit = &DigitMeshes[Ones];
                Engine->SetInstancePosition(OnesDigit->EngineInstance, Right3);
                Engine->UpdateInstance(OnesDigit->EngineInstance, 0, nullptr);
                DrawHolder(OnesDigit, &Camera, 0x308);
            }
        }
        DrawGroupMarkers(); // FUN_004a1ce0
    }
    else
    {
        // 0x00492ff0..0x004930f4: the info-card / legend / base-list modes -- the zoomed map
        // flat at depth -0.71 (identity rotation), its transform put back after.
        IEngine* const Engine = DALib::Engine;
        FTransform Saved;
        memcpy(&Saved, Engine->GetInstanceTransform(ZoomedMapMesh.EngineInstance), sizeof(Saved));
        FTransform Flat;
        Flat.Rotation.D[0][0] = 1.0f;
        Flat.Rotation.D[0][1] = 0.0f;
        Flat.Rotation.D[0][2] = 0.0f;
        Flat.Rotation.D[1][0] = 0.0f;
        Flat.Rotation.D[1][1] = 1.0f;
        Flat.Rotation.D[1][2] = 0.0f;
        Flat.Rotation.D[2][0] = 0.0f;
        Flat.Rotation.D[2][1] = 0.0f;
        Flat.Rotation.D[2][2] = 1.0f;
        Flat.Translation.X = 0.0f;
        Flat.Translation.Y = 0.0f;
        Flat.Translation.Z = kNavMapFlatDepth;
        Engine->SetInstanceMatrix(ZoomedMapMesh.EngineInstance, &Flat);
        Engine->UpdateInstance(ZoomedMapMesh.EngineInstance, 0, nullptr);
        DrawHolder(&ZoomedMapMesh, &Camera, 0x300);
        Engine->SetInstanceMatrix(ZoomedMapMesh.EngineInstance, &Saved);
        Engine->UpdateInstance(ZoomedMapMesh.EngineInstance, 0, nullptr);

        // 0x0049310c..0x004931fe: the legend (mode 4), loaded on first use and stretched.
        if (G_NavMapFilterMode == 4)
        {
            if (LegendMesh.EngineInstance == reinterpret_cast<void*>(static_cast<intptr_t>(-1)))
            {
                ClientMeshLoadFromPath(&LegendMesh, kNavMapLegendMesh, 0, false); // FUN_0059ec80
                Engine->SetInstancePosition(LegendMesh.EngineInstance, MeshPosition);
                FTransform Legend;
                memcpy(&Legend, Engine->GetInstanceTransform(LegendMesh.EngineInstance),
                       sizeof(Legend));
                Legend.Rotation.D[0][0] = static_cast<float>(
                    static_cast<double>(Legend.Rotation.D[0][0]) * kNavMapLegendScaleX);
                Legend.Rotation.D[1][1] = static_cast<float>(
                    static_cast<double>(Legend.Rotation.D[1][1]) * kNavMapLegendScaleY);
                Engine->SetInstanceMatrix(LegendMesh.EngineInstance, &Legend);
                Engine->UpdateInstance(LegendMesh.EngineInstance, 0, nullptr);
            }
            DrawHolder(&LegendMesh, &Camera, 0x300);
        }
    }

    // 0x00493201..0x00493282: a view switch fades the zoomed map in (its alpha from the
    // transition's ease), the tint put back to white after.
    if (bViewSwitch != 0)
    {
        int const Whole = static_cast<int>(static_cast<double>(TransitionEase) *
                                           static_cast<double>(kNavMap255)); // ftol
        unsigned char const Alpha = static_cast<unsigned char>(Whole);
        ZoomedMapMesh.UserFloats[0] = 1.0f;
        ZoomedMapMesh.UserFloats[1] = 1.0f;
        ZoomedMapMesh.UserFloats[2] = 1.0f;
        ZoomedMapMesh.UserFloats[4] = static_cast<float>(static_cast<double>(Alpha) *
                                                         static_cast<double>(kNavMapInv255));
        DrawHolder(&ZoomedMapMesh, &Camera, 0x300);
        ZoomedMapMesh.UserFloats[0] = 1.0f;
        ZoomedMapMesh.UserFloats[1] = 1.0f;
        ZoomedMapMesh.UserFloats[2] = 1.0f;
        ZoomedMapMesh.UserFloats[4] = 1.0f;
    }

    // 0x00493288..0x004932b9: the old viewport and the Win3D camera back.
    Win3DSetViewportRect(SavedViewport[0], SavedViewport[1], SavedViewport[2], SavedViewport[3],
                         kNavMapCpp, 0x934);            // FUN_00421620
    Win3DApplyCamera(AsCamera(Win3DGetCamera()));      // FUN_00422950(&DAT_0067dbf8)

    // 0x004932be..0x004932e7: the labels (not on the universe view, with the label toggle on),
    // then a flush when the option bridge shows.
    if (ViewMode != 0 && G_bNavMapShowLabels != 0) // DAT_00611c5c
        DrawLabels(); // FUN_004999d0
    if (IsShown(OptionBridge))
        Win3DFlushBeforeOverlayDraw(); // FUN_0045a9f0

    // 0x004932f5: the first drawn frame -- Update may now build the pending system view.
    bDrawnOnce = 1;

    // 0x004932ec..0x0049335c: the tool controls, then the grid letters and numbers.
    for (int Index = 0; Tools[Index] != nullptr; ++Index)
    {
        if (IsShown(Tools[Index]))
            Tools[Index]->Draw();
    }
    for (int Index = 0; Index < 8; ++Index)
    {
        WinControl* const Letter = GridLetters[Index];
        if (Letter != nullptr && IsShown(Letter))
            Letter->Draw();
    }
    for (int Index = 0; Index < 8; ++Index)
    {
        WinControl* const Number = GridNumbers[Index];
        if (Number != nullptr && IsShown(Number))
            Number->Draw();
    }

    // 0x0049335e..0x004935fb: the base list (mode 7) -- each row lit by the cursor ray.
    if (G_NavMapFilterMode == 7)
    {
        // 0x0049336c..0x00493465: the cursor's ray through the Win3D camera: the screen point
        // to the near plane, rotated by the camera (I3DMathEngine slot 11), normalised.
        FSpaceCamera* const WinCamera = Win3DGetCamera(); // DAT_0067dbf8
        int CursorX = 0;
        int CursorY = 0;
        int CursorZ = 0;
        GetClientSoftwareCursor(&CursorX, &CursorY, &CursorZ); // DAT_00616840 / 44
        FVector Near;
        Near.X = static_cast<float>((static_cast<double>(CursorX) -
                                     static_cast<double>(WinCamera->ScreenCenterX)) /
                                    static_cast<double>(WinCamera->XScale) *
                                    static_cast<double>(WinCamera->ZNear));
        Near.Y = static_cast<float>((static_cast<double>(CursorY) -
                                     static_cast<double>(WinCamera->ScreenCenterY)) /
                                    static_cast<double>(WinCamera->YScale) *
                                    static_cast<double>(WinCamera->ZNear));
        Near.Z = -WinCamera->ZNear;
        FMatrix CameraRotation;
        memcpy(&CameraRotation, WinCamera->World, sizeof(CameraRotation)); // DAT_0067dbfc
        FVector Rotated;
        NavMapMath()->TransformMatrix(Rotated, CameraRotation, Near); // slot 11 (+0x2c)
        double const Length = sqrt(static_cast<double>(Rotated.X) * static_cast<double>(Rotated.X) +
                                   static_cast<double>(Rotated.Y) * static_cast<double>(Rotated.Y) +
                                   static_cast<double>(Rotated.Z) * static_cast<double>(Rotated.Z));
        double const InvLength = 1.0 / Length; // FDIVR DAT_005c75dc
        float Ray[3];
        Ray[0] = static_cast<float>(static_cast<double>(Rotated.X) * InvLength);
        Ray[1] = static_cast<float>(static_cast<double>(Rotated.Y) * InvLength);
        Ray[2] = static_cast<float>(static_cast<double>(Rotated.Z) * InvLength);

        // 0x00493467..0x004934fc: the fifteen base rows -- both halves shown, pulsed when the
        // ray hits either (slot 4), coloured through SetProperty(0x33, &colour, 1), drawn.
        for (int Index = 0; Index < 15; ++Index)
        {
            WinControl* const BaseText = BaseTexts[Index];
            if (BaseText == nullptr || !IsShown(BaseText))
                continue;
            WinControl* const SystemText = BaseSystemTexts[Index];
            if (SystemText == nullptr || !IsShown(SystemText))
                continue;
            unsigned long Colour = G_NnPaletteArgb_679bb0;
            if (BaseText->HitTest(Ray) || SystemText->HitTest(Ray))
            {
                unsigned long Pulsed = 0;
                NnUiPulseBlendArgb(&Pulsed); // FUN_00561da0
                Colour = Pulsed;
            }
            BaseText->SetProperty(0x33, reinterpret_cast<uintptr_t>(&Colour), 1);
            SystemText->SetProperty(0x33, reinterpret_cast<uintptr_t>(&Colour), 1);
            BaseText->Draw();
            SystemText->Draw();
        }

        // 0x00493502..0x00493517: the list's scroll bar.
        WinControl* const Scroll = static_cast<WinControl*>(BaseListScroll);
        if (Scroll != nullptr && IsShown(Scroll))
            Scroll->Draw();

        // 0x00493517..0x00493586: the two column titles -- the sorted one amber (bBaseListByBase
        // 1: bases), the other pulsed under the ray.
        if (BaseTitleText != nullptr && IsShown(BaseTitleText))
        {
            unsigned long Colour = G_NnPaletteArgb_679bb0;
            if (bBaseListByBase != 0)
            {
                Colour = G_NnPaletteArgb_679ba4;
            }
            else if (BaseTitleText->HitTest(Ray))
            {
                unsigned long Pulsed = 0;
                NnUiPulseBlendArgb(&Pulsed);
                Colour = Pulsed;
            }
            BaseTitleText->SetProperty(0x33, reinterpret_cast<uintptr_t>(&Colour), 1);
            BaseTitleText->Draw();
        }
        if (SystemTitleText != nullptr && IsShown(SystemTitleText))
        {
            unsigned long Colour = G_NnPaletteArgb_679bb0;
            if (bBaseListByBase == 0)
            {
                Colour = G_NnPaletteArgb_679ba4;
            }
            else if (SystemTitleText->HitTest(Ray))
            {
                unsigned long Pulsed = 0;
                NnUiPulseBlendArgb(&Pulsed);
                Colour = Pulsed;
            }
            SystemTitleText->SetProperty(0x33, reinterpret_cast<uintptr_t>(&Colour), 1);
            SystemTitleText->Draw();
        }
    }

    // 0x00493600..0x0049365a: the legend's five labels (mode 4, untested), else the system's
    // name outside the info-card / base-list modes.
    int const ModeNow = G_NavMapFilterMode;
    if (ModeNow == 4)
    {
        for (int Index = 0; Index < 5; ++Index)
            LegendLabels[Index]->Draw();
    }
    else if (ModeNow != 6 && ModeNow != 7 && SystemNameText != nullptr && IsShown(SystemNameText))
    {
        SystemNameText->Draw();
    }

    // 0x0049365d: slot 39, WinControl's no-op tail.
    Slot39();
}

// ----------------------------------------------------------------------------------------
// The lines: jump links, routes
// ----------------------------------------------------------------------------------------

void NeuroNetNavMap::DrawJumpLinks()
{
    // FUN_00497360: the modelview at depth 0 and the line state, one triangle list.
    BeginMapLines(this, 0.0f);
    Win3DBegin(kWin3DTriangles); // FUN_00421670(4)

    for (std::list<FNavMapJumpLink*>::iterator It = JumpLinks.begin(); It != JumpLinks.end(); ++It)
    {
        FNavMapJumpLink* const Link = *It;
        // 0x004974a1..0x004974bc: both ends' instance positions (FUN_00420fb0), From copied
        // before To is read.
        float From[3];
        memcpy(From, ClientMeshHolderGetPosition(&Link->From->Mesh), sizeof(From));
        float To[3];
        memcpy(To, ClientMeshHolderGetPosition(&Link->To->Mesh), sizeof(To));
        if (Link->Kind == 0)
        {
            // 0x004974c8..0x00497514: a plain link, blue {00,40,80,ff} fading to {00,10,20,00}.
            unsigned char const Center[4] = {0x00, 0x40, 0x80, 0xff};
            unsigned char const Edge[4] = {0x00, 0x10, 0x20, 0x00};
            NavMapDrawBand(From, To, Center, Edge, kNavMapLinkWidth); // FUN_00498470
        }
        else if (Link->Kind == 2 || Link->Kind == 1)
        {
            // 0x00497520..0x00497550: a route link in the palette colour DAT_00679b80, its
            // alpha byte cleared for the edges.
            unsigned char Edge[4];
            memcpy(Edge, &G_NnPaletteArgb_679b80, sizeof(Edge));
            Edge[3] = 0;
            NavMapDrawBand(From, To, reinterpret_cast<unsigned char const*>(&G_NnPaletteArgb_679b80),
                           Edge, kNavMapLinkWidth);
        }
    }

    Win3DEnd(); // FUN_00421690 (tail jump)
}

void NeuroNetNavMap::DrawRouteLines()
{
    // FUN_0049fe70: only once Draw has run and not on the universe view.
    if (bDrawnOnce == 0 || ViewMode == 0)
        return;

    // 0x0049fe98..0x0049ffd1: the line state, the waypath texture bound, white, one list.
    BeginMapLines(this, 0.0f);
    Win3DBindTexture(TexShapeGetGpuHandle(WaypathShape, 0)); // FUN_004220a0(FUN_00425640)
    Win3DColor3ub(0xff, 0xff, 0xff);                         // FUN_00421a70
    Win3DBegin(kWin3DTriangles);

    unsigned char Edge[4];

    // 0x0049ffd9..0x004a00f3: the player's waypoints, from the player's marker to the first
    // (index -1, only when the first is shown and the map shows the player's system), then
    // between neighbours when both exist and are shown.
    int const WaypointLast = GetPlayerWaypointCountRetail() - 1; // FUN_004c4690
    for (int Index = -1; Index < WaypointLast; ++Index)
    {
        FNavMapItem* From = nullptr;
        FNavMapItem* To = nullptr;
        if (Index == -1 && WaypointItems[0] != nullptr)
        {
            if (WaypointItems[0]->bShown == 0)
                continue;
            unsigned const SystemId = CurrentSystem->GetId();
            if (GetPlayerStarSystemIdRetail() != SystemId) // FUN_004c4830
                continue;
            From = PlayerItem;
            To = WaypointItems[0];
        }
        else
        {
            // 0x004a003e: [+0x4b8 + 4 * Index] -- for Index -1 (no first waypoint) that is
            // +0x4b4, the player's marker, and its partner WaypointItems[0] is null.
            From = (Index == -1) ? PlayerItem : WaypointItems[Index];
            if (From == nullptr)
                continue;
            To = WaypointItems[Index + 1];
            if (To == nullptr || From->bShown == 0 || To->bShown == 0)
                continue;
        }
        float FromPos[3];
        memcpy(FromPos, ClientMeshHolderGetPosition(&From->Mesh), sizeof(FromPos));
        float ToPos[3];
        memcpy(ToPos, ClientMeshHolderGetPosition(&To->Mesh), sizeof(ToPos));
        memcpy(Edge, &G_NnPaletteArgb_679b80, sizeof(Edge));
        Edge[3] = 0;
        NavMapDrawTexturedBand(FromPos, ToPos,
                               reinterpret_cast<unsigned char const*>(&G_NnPaletteArgb_679b80),
                               Edge, kNavMapRouteWidth); // FUN_00498730
    }

    // 0x004a00f9..0x004a01fa: the best path the same way over BestPathItems, the first leg
    // from the player's marker without the system test, at the link width.
    int const BestPathLast = BestPathGetWaypointCount() - 1; // FUN_004c4fb0
    for (int Index = -1; Index < BestPathLast; ++Index)
    {
        FNavMapItem* From = nullptr;
        FNavMapItem* To = nullptr;
        if (Index == -1 && BestPathItems[0] != nullptr)
        {
            if (BestPathItems[0]->bShown == 0)
                continue;
            From = PlayerItem;
            To = BestPathItems[0];
        }
        else
        {
            // 0x004a0145: for Index -1 the slot before BestPathItems[0] is WaypointItems[0x62],
            // and the partner BestPathItems[0] is null.
            From = (Index == -1) ? WaypointItems[0x62] : BestPathItems[Index];
            if (From == nullptr)
                continue;
            To = BestPathItems[Index + 1];
            if (To == nullptr || From->bShown == 0 || To->bShown == 0)
                continue;
        }
        float FromPos[3];
        memcpy(FromPos, ClientMeshHolderGetPosition(&From->Mesh), sizeof(FromPos));
        float ToPos[3];
        memcpy(ToPos, ClientMeshHolderGetPosition(&To->Mesh), sizeof(ToPos));
        memcpy(Edge, &G_NnPaletteArgb_679b80, sizeof(Edge));
        Edge[3] = 0;
        NavMapDrawTexturedBand(FromPos, ToPos,
                               reinterpret_cast<unsigned char const*>(&G_NnPaletteArgb_679b80),
                               Edge, kNavMapLinkWidth);
    }

    Win3DEnd(); // FUN_00421690 (tail jump)
}

// ----------------------------------------------------------------------------------------
// The labels
// ----------------------------------------------------------------------------------------

void NeuroNetNavMap::DrawLabels()
{
    // FUN_004999d0: not in the info-card / legend / base-list modes.
    int const Mode = G_NavMapFilterMode;
    if (Mode == 4 || Mode == 6 || Mode == 7)
        return;

    // 0x004999f9..0x00499a29: last frame's rects dropped (FUN_004a3bf0 erase), lighting and
    // the depth test off.
    LabelRects.clear();
    Win3DDisable(kWin3DLightingCap);
    Win3DDisable(kWin3DDepthTest);

    // 0x00499a3b..0x00499a8d: the label font by the surface width (compared signed).
#ifdef FREELANCER_WIDESCREEN_SUPPORT
    // Port-only Hor+: retail knows only its five 4:3 widths. The map is interface, so the font
    // follows the width of a 4:3 surface of this height, by range rather than exact match.
    int const SurfaceWidth = static_cast<int>(WidescreenEquivalentSurfaceWidth() + 0.5);
    char const* FontName = "NavMap800";
    if (SurfaceWidth >= 0x640)
        FontName = "NavMap1600";
    else if (SurfaceWidth >= 0x480)
        FontName = "NavMap1280";
    else if (SurfaceWidth >= 0x400)
        FontName = "NavMap1024";
#else
    int const SurfaceWidth = static_cast<int>(G_RenderSurfaceWidth); // DAT_00610850
    char const* FontName = "NavMap800";
    if (SurfaceWidth >= 0x640)
        FontName = "NavMap1600";
    else if (SurfaceWidth == 0x400)
        FontName = "NavMap1024";
    else if (SurfaceWidth == 0x500 || SurfaceWidth == 0x480)
        FontName = "NavMap1280";
#endif
    IFont* const Font = FindMenuFontImp(FontName); // FUN_004177b0

    // 0x00499a8f..0x00499adb: the idle colour; while a transition runs into this view (or into
    // the universe view) its alpha byte fades out with the ease.
    unsigned char Colour[4];
    memcpy(Colour, &G_NnPaletteArgb_679bb0, sizeof(Colour));
    if (bTransition != 0 && (TransitionView == 0 || ViewMode == TransitionView))
    {
        double const Fade = (kNavMapOne - static_cast<double>(TransitionEase)) * kNavMapByteMax;
        Colour[3] = static_cast<unsigned char>(static_cast<int>(Fade)); // ftol, low byte
    }
    unsigned Packed = 0;
    memcpy(&Packed, Colour, sizeof(Packed));
    FontAtlasSetDrawColor(Packed); // FUN_00417780(&colour)

    // 0x00499add..0x00499b22: the zoomed view (or a transition into it) starts every label
    // uncrowded.
    if ((ViewMode != 1 && ViewMode != 0) || (bTransition != 0 && TransitionView == 2))
    {
        for (std::list<FNavMapItem*>::iterator It = Items.begin(); It != Items.end(); ++It)
            (*It)->bLabelCrowded = 0;
    }

    // 0x00499b24..0x00499c0f: three passes over the named, shown objects (Kind 0) by type --
    // 2 first, then 0xc40, then 0x200101 -- so the earlier ones claim their spots first.
    unsigned const PassMasks[3] = {2u, 0xc40u, 0x200101u};
    for (int Pass = 0; Pass < 3; ++Pass)
    {
        for (std::list<FNavMapItem*>::iterator It = Items.begin(); It != Items.end(); ++It)
        {
            FNavMapItem* const Item = *It;
            if (Item->StridName == 0 || Item->bShown == 0 || Item->Kind != 0 ||
                (static_cast<unsigned>(Item->Type) & PassMasks[Pass]) == 0)
                continue;
            EnsureItemLabel(Font, Item); // FUN_004997d0
            DrawItemLabel(Font, Item);   // FUN_00499c20
        }
    }
}

void NeuroNetNavMap::EnsureItemLabel(IFont* Font, FNavMapItem* Item)
{
    // FUN_004997d0.
    wchar_t Name[0x80];
    if (Item->Label == nullptr)
    {
        // 0x00499805..0x00499890: a new Font::String in Font, scaled (1, the label depth).
        FFontString* const Label = new FFontString; // operator_new(0x34) + FUN_004144b0
        Item->Label = Label;
        FontStringSetFont(Label, Font); // FUN_004ea670, inline at 0x00499837..0x0049987b
        FontStringSetScale(Label, 1.0f, Field_0x3c8[2]); // FUN_004145d0

        // 0x00499895..0x004998eb: the name (empty when the ids is missing), its width cached.
        if (GetIdsStringWWithRawFallback(static_cast<unsigned>(Item->StridName), Name, 0x80) == 0)
            Name[0] = 0;
        FontStringSetText(Label, Name); // FUN_00415860
        int Extent[2] = {0, 0};
        Font->MeasureString(Extent, Name, -1); // IFont slot 8
        Item->LabelWidth = static_cast<float>(static_cast<unsigned>(Extent[0])); // FILD qword
        return;
    }

    // 0x004998f0..0x004999af: the crowded flag changed -- the name again, with a "*" in front
    // while crowded (built in G_NnTextScratchB), and its width.
    if (Item->bLabelCrowded == Item->bLabelStarred)
        return;
    Item->bLabelStarred = Item->bLabelCrowded;
    if (GetIdsStringWWithRawFallback(static_cast<unsigned>(Item->StridName), Name, 0x80) == 0)
        Name[0] = 0;
    if (Item->bLabelStarred != 0)
    {
        wcscpy(G_NnTextScratchB, L"*"); // DAT_005d3e9c
        wcscat(G_NnTextScratchB, Name);
    }
    else
    {
        wcscpy(G_NnTextScratchB, Name);
    }
    FontStringSetText(Item->Label, G_NnTextScratchB);
    int Extent[2] = {0, 0};
    Font->MeasureString(Extent, G_NnTextScratchB, -1);
    Item->LabelWidth = static_cast<float>(static_cast<unsigned>(Extent[0]));
}

void NeuroNetNavMap::DrawItemLabel(IFont* Font, FNavMapItem* Item)
{
    // FUN_00499c20: the label's height from the font's fixed height (IFont slot 2).
    float const Height = static_cast<float>(static_cast<double>(Font->GetFixedHeight()) *
                                            static_cast<double>(kNavMapLabelHeightScale) *
                                            static_cast<double>(kNavMapLabelHeightFactor));
#ifdef FREELANCER_WIDESCREEN_SUPPORT
    // Port-only Hor+: pixels to map units through the width of a 4:3 surface of this height.
    double const WidthExt =
        static_cast<double>(Item->LabelWidth) / WidescreenEquivalentSurfaceWidth();
#else
    int const SurfaceWidth = static_cast<int>(G_RenderSurfaceWidth); // FILD DAT_00610850
    double const WidthExt =
        static_cast<double>(Item->LabelWidth) / static_cast<double>(SurfaceWidth);
#endif
    float const Width = static_cast<float>(WidthExt);
    float const Depth = Field_0x3c8[2];

    bool const bZoomed = (ViewMode != 1 && ViewMode != 0) ||
                         (bTransition != 0 && TransitionView == 2);
    float X = 0.0f;
    float Y = 0.0f;
    if (bZoomed)
    {
        // 0x00499c6c..0x00499dbe: the map's origin at the current pan / zoom. Here the x term
        // multiplies the UNROUNDED Zoom - 1 (the y / z terms use it rounded), and the pan
        // products are rounded before they are added.
        double const Zoom = static_cast<double>(ZoomScale);
        float const Half0 = MapSize[0] * 0.5f;
        float const Half1 = MapSize[1] * 0.5f;
        float const Half2 = MapSize[2] * 0.5f;
        double const ZoomM1 = Zoom - 1.0;
        float const ZoomM1F = static_cast<float>(ZoomM1);
        float const Term0 = static_cast<float>(ZoomM1 * static_cast<double>(Half0));
        double const Term1 = static_cast<double>(ZoomM1F) * static_cast<double>(Half1);
        double const Term2 = static_cast<double>(Half2) * static_cast<double>(ZoomM1F);
        float const Base0 = static_cast<float>(static_cast<double>(-Half0) - static_cast<double>(Term0));
        float const Base1 = static_cast<float>(static_cast<double>(-Half1) - Term1);
        double const Base2 = static_cast<double>(-Half2) - Term2;
        float const Pan0 = static_cast<float>(static_cast<double>(PanOffset[0]) * Zoom);
        float const Pan1 = static_cast<float>(static_cast<double>(PanOffset[1]) * Zoom);
        float const OriginX = static_cast<float>(static_cast<double>(Pan0) + static_cast<double>(Base0));
        float const OriginY = static_cast<float>(static_cast<double>(Pan1) + static_cast<double>(Base1));
        // OriginZ (Zoom * Pan.z + Base2) is built too (0x00499d8f..0x00499d98) and not read.
        static_cast<void>(Base2);

        // 0x00499dbe..0x00499e70: the item's point in the frame (2EX / 2EZ unrounded here).
        float const SizeX = static_cast<float>(static_cast<double>(MapSize[0]) * Zoom);
        float const SizeY = static_cast<float>(static_cast<double>(MapSize[1]) * Zoom);
        double const ExtentX = static_cast<double>(MapExtentX);
        double const ExtentZ = static_cast<double>(MapExtentZ);
        double const FrameX = (static_cast<double>(Item->Position[0]) + ExtentX) / (ExtentX + ExtentX) *
                              static_cast<double>(SizeX);
        double const FrameY =
            (kNavMapOne - (static_cast<double>(Item->Position[2]) + ExtentZ) / (ExtentZ + ExtentZ)) *
            static_cast<double>(SizeY);
        float const PointX = static_cast<float>(static_cast<double>(OriginX) + FrameX);
        double const PointY = static_cast<double>(OriginY) + FrameY;

        // 0x00499e72..0x00499edc: to the label frame (+-0.2 wide, +-0.18 high), centred.
        double const ScreenX =
            static_cast<double>(PointX) / static_cast<double>(MapSize[0] * 0.5f);
        float const Half1F = MapSize[1] * 0.5f;
        float const FrameYF = static_cast<float>(-(PointY / static_cast<double>(Half1F)));
        X = static_cast<float>(ScreenX * static_cast<double>(kNavMapLabelFrameX) - WidthExt * 0.5);
        Y = static_cast<float>(-(static_cast<double>(FrameYF) * static_cast<double>(kNavMapLabelFrameY)));
    }
    else
    {
        // 0x00499fce..0x0049a043: the system view -- straight from the extents.
        float const X0 = static_cast<float>(static_cast<double>(Item->Position[0]) /
                                            static_cast<double>(MapExtentX) *
                                            static_cast<double>(kNavMapLabelFrameX));
        double const Y0 = static_cast<double>(Item->Position[2]) / static_cast<double>(MapExtentZ) *
                          static_cast<double>(kNavMapLabelFrameY);
        X = static_cast<float>(static_cast<double>(X0) - WidthExt * 0.5);
        Y = static_cast<float>(-Y0);
    }

    if (!PlaceLabel(&X, &Y, Width, Height, Item)) // FUN_0049a2f0
    {
        if (bZoomed)
            return;
        // 0x0049a058..0x0049a10f: the system view marks every placed label this one would
        // have overlapped (X * 0.9, W * 1.2) as crowded.
        float const RectX = static_cast<float>(static_cast<double>(X) * kNavMapLabelRectX);
        float const RectW = static_cast<float>(static_cast<double>(Width) * kNavMapLabelRectW);
        int const Count = static_cast<int>(LabelRects.size());
        for (int Index = 0; Index < Count; ++Index)
        {
            FNavMapLabelRect const& Rect = LabelRects[static_cast<size_t>(Index)];
            if (static_cast<double>(Rect.W) + static_cast<double>(Rect.X) < static_cast<double>(RectX))
                continue;
            if (static_cast<double>(RectX) + static_cast<double>(RectW) < static_cast<double>(Rect.X))
                continue;
            if (static_cast<double>(Rect.H) + static_cast<double>(Rect.Y) < static_cast<double>(Y))
                continue;
            if (static_cast<double>(Y) + static_cast<double>(Height) < static_cast<double>(Rect.Y))
                continue;
            if (Rect.Item != nullptr)
                Rect.Item->bLabelCrowded = 1;
        }
        return;
    }

    // 0x00499eef..0x00499fc9 / 0x0049a11b..0x0049a1ad: the box (1 x 0.03 at the label depth)
    // and its corner projected through the Win3D camera; the zoomed view takes x from a point
    // just in front and y from one behind.
    FFontStringDrawOrigin Origin;
    Origin.Field10 = 0;
    Origin.Field14 = 0;
    TextWinProjectExtent(1.0f, kNavMapLabelBoxH, Depth, &Origin.Scale, &Origin.Height); // FUN_004176c0
    float const LiftedY = static_cast<float>(static_cast<double>(Y) + static_cast<double>(kNavMapLabelLift));
    if (bZoomed)
    {
        float Front[3];
        Front[0] = X;
        Front[1] = LiftedY;
        Front[2] = static_cast<float>(static_cast<double>(Depth) - static_cast<double>(kNavMapLabelLift));
        float FrontX = 0.0f;
        float FrontY = 0.0f;
        TextWinProjectPoint(Front, &FrontX, &FrontY); // FUN_00417730
        float Back[3];
        Back[0] = X;
        Back[1] = LiftedY;
        Back[2] = static_cast<float>(static_cast<double>(Depth) + static_cast<double>(kNavMapLabelBack));
        float BackX = 0.0f;
        float BackY = 0.0f;
        TextWinProjectPoint(Back, &BackX, &BackY);
        Origin.X = FrontX;
        Origin.Y = BackY;
    }
    else
    {
        float Point[3];
        Point[0] = X;
        Point[1] = LiftedY;
        Point[2] = Depth;
        float PointX = 0.0f;
        float PointY = 0.0f;
        TextWinProjectPoint(Point, &PointX, &PointY);
        Origin.X = PointX;
        Origin.Y = PointY;
    }
    FontStringDrawChildren(&Origin, Item->Label, nullptr, nullptr); // FUN_004143c0
}

bool NeuroNetNavMap::PlaceLabel(float* X, float* Y, float W, float H, FNavMapItem* Item)
{
    // FUN_0049a2f0: where it is, or nudged by Step pixels (Step 0..14) up, down, right, left
    // and along the four diagonals, the first spot LabelRectFits takes.
    bool bPlaced = LabelRectFits(*X, *Y, W, H);
    if (!bPlaced)
    {
#ifdef FREELANCER_WIDESCREEN_SUPPORT
        // Port-only Hor+: as the label width above.
        float const InvWidth = static_cast<float>(kNavMapOne / WidescreenEquivalentSurfaceWidth());
#else
        float const InvWidth = static_cast<float>(
            kNavMapOne / static_cast<double>(static_cast<int>(G_RenderSurfaceWidth)));
#endif
        float const InvHeight = static_cast<float>(
            kNavMapOne / static_cast<double>(static_cast<int>(G_RenderSurfaceHeight)));
        for (int Step = 0; Step < 15 && !bPlaced; ++Step)
        {
            float const Dy = static_cast<float>(static_cast<double>(InvHeight) * static_cast<double>(Step));
            float const Dx = static_cast<float>(static_cast<double>(Step) * static_cast<double>(InvWidth));
            float const Up = static_cast<float>(static_cast<double>(Dy) + static_cast<double>(*Y));
            float const Down = static_cast<float>(static_cast<double>(*Y) - static_cast<double>(Dy));
            float const Right = static_cast<float>(static_cast<double>(Dx) + static_cast<double>(*X));
            float const LeftX = static_cast<float>(static_cast<double>(*X) - static_cast<double>(Dx));
            if (LabelRectFits(*X, Up, W, H))
            {
                *Y = Up;
                bPlaced = true;
            }
            else if (LabelRectFits(*X, Down, W, H))
            {
                *Y = Down;
                bPlaced = true;
            }
            else if (LabelRectFits(Right, *Y, W, H))
            {
                *X = Right;
                bPlaced = true;
            }
            else if (LabelRectFits(LeftX, *Y, W, H))
            {
                *X = LeftX;
                bPlaced = true;
            }
            else if (LabelRectFits(Right, Up, W, H))
            {
                *X = Right;
                *Y = Up;
                bPlaced = true;
            }
            else if (LabelRectFits(LeftX, Down, W, H))
            {
                *X = LeftX;
                *Y = Down;
                bPlaced = true;
            }
            else if (LabelRectFits(Right, Down, W, H))
            {
                *X = Right;
                *Y = Down;
                bPlaced = true;
            }
            else if (LabelRectFits(LeftX, Up, W, H))
            {
                *X = LeftX;
                *Y = Up;
                bPlaced = true;
            }
        }
        if (!bPlaced)
            return false;
    }

    // 0x0049a52d..0x0049a569: the spot reserved at 1.4 times the size (FUN_00477510 insert at
    // the end).
    FNavMapLabelRect Rect;
    Rect.X = *X;
    Rect.Y = *Y;
    Rect.W = static_cast<float>(static_cast<double>(W) * static_cast<double>(kNavMapLabelReserve));
    Rect.H = static_cast<float>(static_cast<double>(H) * static_cast<double>(kNavMapLabelReserve));
    Rect.Item = Item;
    LabelRects.push_back(Rect);
    return true;
}

bool NeuroNetNavMap::LabelRectFits(float X, float Y, float W, float H)
{
    // FUN_0049a1e0: the rect (X * 0.9 .. + W * 1.2, Y, H) inside the frame...
    float const Left = static_cast<float>(static_cast<double>(X) * kNavMapLabelRectX);
    float const Right = static_cast<float>(static_cast<double>(Left) +
                                           static_cast<double>(W) * kNavMapLabelRectW);
    if (Right > kNavMapLabelMaxRight)                                            // JZ: greater
        return false;
    if (Left < kNavMapLabelMinLeft)                                              // JNP: less
        return false;
    if (static_cast<double>(H) - static_cast<double>(Y) > static_cast<double>(kNavMapLabelMaxDrop))
        return false;
    if (-static_cast<double>(Y) < static_cast<double>(kNavMapLabelMinTop))
        return false;

    // 0x0049a25f..0x0049a2d8: ...and clear of every recorded one. A rect left of it, right of
    // it or above it is skipped; one it does not end above (Y + H not less than its Y) fails.
    int const Count = static_cast<int>(LabelRects.size());
    for (int Index = 0; Index < Count; ++Index)
    {
        FNavMapLabelRect const& Rect = LabelRects[static_cast<size_t>(Index)];
        if (static_cast<double>(Rect.W) + static_cast<double>(Rect.X) < static_cast<double>(Left))
            continue;
        if (Rect.X > Right)
            continue;
        if (static_cast<double>(Rect.Y) + static_cast<double>(Rect.H) < static_cast<double>(Y))
            continue;
        // TEST AH,5 + JP: fails unless Y + H is less than the rect's Y (unordered fails too).
        if (!(static_cast<double>(Y) + static_cast<double>(H) < static_cast<double>(Rect.Y)))
            return false;
    }
    return true;
}

// ----------------------------------------------------------------------------------------
// The group markers
// ----------------------------------------------------------------------------------------

void NeuroNetNavMap::DrawGroupMarkers()
{
    // FUN_004a1ce0: not on the universe view, not in single player, not without members.
    if (ViewMode == 0 || SinglePlayer() || G_NavMapGroupPositions.empty())
        return;

    // 0x004a1d32..0x004a1dae: the marker mesh on first use, tinted with the palette's
    // DAT_00679bac bytes / 255.
    if (GroupMarkerMesh.EngineInstance == reinterpret_cast<void*>(static_cast<intptr_t>(-1)))
    {
        ClientMeshLoadFromPath(&GroupMarkerMesh, kNavMapGroupMarkerMesh, 0, false); // FUN_0059ec80
        unsigned char const* const Tint = reinterpret_cast<unsigned char const*>(&G_NnPaletteArgb_679bac);
        GroupMarkerMesh.UserFloats[0] = static_cast<float>(static_cast<double>(Tint[0]) * static_cast<double>(kNavMapInv255));
        GroupMarkerMesh.UserFloats[1] = static_cast<float>(static_cast<double>(Tint[1]) * static_cast<double>(kNavMapInv255));
        GroupMarkerMesh.UserFloats[2] = static_cast<float>(static_cast<double>(Tint[2]) * static_cast<double>(kNavMapInv255));
        GroupMarkerMesh.UserFloats[4] = static_cast<float>(static_cast<double>(Tint[3]) * static_cast<double>(kNavMapInv255));
    }

    // 0x004a1db1..0x004a1efc: the map's origin -- every Zoom - 1 term with the rounded value,
    // the x / y pan products rounded before they are added, the z one not.
    double const Zoom = static_cast<double>(ZoomScale);
    float const Half0 = MapSize[0] * 0.5f;
    float const Half1 = MapSize[1] * 0.5f;
    float const Half2 = MapSize[2] * 0.5f;
    float const ZoomM1 = static_cast<float>(Zoom - 1.0);
    float const Term0 = static_cast<float>(static_cast<double>(Half0) * static_cast<double>(ZoomM1));
    double const Term1 = static_cast<double>(Half1) * static_cast<double>(ZoomM1);
    double const Term2 = static_cast<double>(Half2) * static_cast<double>(ZoomM1);
    float const Base0 = static_cast<float>(static_cast<double>(-Half0) - static_cast<double>(Term0));
    float const Base1 = static_cast<float>(static_cast<double>(-Half1) - Term1);
    double const Base2 = static_cast<double>(-Half2) - Term2;
    float const Pan0 = static_cast<float>(static_cast<double>(PanOffset[0]) * Zoom);
    float const Pan1 = static_cast<float>(static_cast<double>(PanOffset[1]) * Zoom);
    float const OriginX = static_cast<float>(static_cast<double>(Pan0) + static_cast<double>(Base0));
    float const OriginY = static_cast<float>(static_cast<double>(Pan1) + static_cast<double>(Base1));
    // OriginZ (0x004a1ed5..0x004a1edb) is built and only feeds the z the loop overwrites.
    float const OriginZ = static_cast<float>(Zoom * static_cast<double>(PanOffset[2]) + Base2);
    float const SizeX = static_cast<float>(static_cast<double>(MapSize[0]) * Zoom);
    float const SizeY = static_cast<float>(static_cast<double>(MapSize[1]) * Zoom);
    float const SizeZ = static_cast<float>(static_cast<double>(MapSize[2]) * Zoom);

    // 0x004a1f43..0x004a1fd8: the marker size -- ShipMarkerSize over max(zoom / 2, 1), or during
    // a transition the ease between the start and end distances' values.
    double const Ship = static_cast<double>(ShipMarkerSize);
    double Size = 0.0;
    if (bTransition != 0)
    {
        double StartDiv = static_cast<double>(MapExtentX) / static_cast<double>(ZoomDistance) * 0.5;
        if (StartDiv < 1.0)
            StartDiv = 1.0;
        double const StartSize = Ship / StartDiv;
        double EndDiv = static_cast<double>(MapExtentX) /
                        (static_cast<double>(TransitionDelta[2]) + static_cast<double>(ZoomDistance)) * 0.5;
        if (EndDiv < 1.0)
            EndDiv = 1.0;
        Size = (Ship / EndDiv - StartSize) * static_cast<double>(TransitionEase) + StartSize;
    }
    else
    {
        double Div = Zoom * 0.5;
        if (Div < 1.0)
            Div = 1.0;
        Size = Ship / Div;
    }
    // 0x004a1fda..0x004a1ff0: to the frame (2EZ unrounded), times 60.
    double const ExtentX = static_cast<double>(MapExtentX);
    double const ExtentZ = static_cast<double>(MapExtentZ);
    float const MarkerScale = static_cast<float>(Size / (ExtentZ + ExtentZ) *
                                                 static_cast<double>(SizeX) *
                                                 static_cast<double>(kNavMapGroupMarkerFactor));

    // 0x004a1ffc..0x004a21e2: each member in the current system -- the marker at its point,
    // unrotated (cos / sin of 0), scaled, at the zone depth.
    IEngine* const Engine = DALib::Engine;
    for (size_t Index = 0; Index < G_NavMapGroupPositions.size(); ++Index)
    {
        FNavMapGroupMember const& Member = G_NavMapGroupPositions[Index];
        if (Member.SystemId != CurrentSystem->GetId())
            continue;
        double const Scale = static_cast<double>(SystemScale);
        double const MemberX = Scale * static_cast<double>(Member.Pos[0]);
        float const MemberZ = static_cast<float>(Scale * static_cast<double>(Member.Pos[2]));
        double const FrameX = (MemberX + ExtentX) / (ExtentX + ExtentX) * static_cast<double>(SizeX);
        double const FrameY = (kNavMapOne - (static_cast<double>(MemberZ) + ExtentZ) / (ExtentZ + ExtentZ)) *
                              static_cast<double>(SizeY);
        float const PointX = static_cast<float>(static_cast<double>(OriginX) + FrameX);
        float const PointY = static_cast<float>(static_cast<double>(OriginY) + FrameY);
        // 0x004a20f3..0x004a2116: Pz = OriginZ + SizeZ is computed and replaced by the depth.
        static_cast<void>(static_cast<float>(static_cast<double>(OriginZ) + static_cast<double>(SizeZ)));

        double const Angle = 0.0; // DAT_005c8ba8
        float const Cos = static_cast<float>(cos(Angle));
        float const Sin = static_cast<float>(sin(Angle));
        FTransform Marker;
        Marker.Rotation.D[0][0] = Cos;
        Marker.Rotation.D[0][1] = -Sin;
        Marker.Rotation.D[0][2] = 0.0f;
        Marker.Rotation.D[1][0] = Sin;
        Marker.Rotation.D[1][1] = Cos;
        Marker.Rotation.D[1][2] = 0.0f;
        Marker.Rotation.D[2][0] = 0.0f;
        Marker.Rotation.D[2][1] = 0.0f;
        Marker.Rotation.D[2][2] = 1.0f;
        NavMapMath()->Scale(Marker.Rotation, Marker.Rotation, MarkerScale); // slot 4 (+0x10)
        Marker.Translation.X = PointX;
        Marker.Translation.Y = PointY;
        Marker.Translation.Z = kNavMapZoneDepth;
        Engine->SetInstanceMatrix(GroupMarkerMesh.EngineInstance, &Marker); // +0x9c
        Engine->UpdateInstance(GroupMarkerMesh.EngineInstance, 0, nullptr); // +0x6c
        DrawHolder(&GroupMarkerMesh, &Camera, 0x308);
    }
}

// ----------------------------------------------------------------------------------------
// The zone shading
// ----------------------------------------------------------------------------------------

void NeuroNetNavMap::DrawZoneShading()
{
    // FUN_0049dcc0: only in filter modes 1 / 2 / 3 / 5.
    int const Mode = G_NavMapFilterMode;
    if (Mode != 1 && Mode != 2 && Mode != 3 && Mode != 5)
        return;
    bool const bRebuilt = BuildZoneShading(); // FUN_0049c740
    if (ShadeVertexCount == 0)
        return;

    // 0x0049dd16..0x0049de1e: the modelview at the zone depth, the line state.
    BeginMapLines(this, kNavMapZoneDepth);
    // 0x0049de23..0x0049dea8: the pipeline states, both stages on diffuse.
    ApplyRenderStates(kNavMapRsOn);
    ApplyStageStates(0, kNavMapTssDiffuse);
    ApplyStageStates(1, kNavMapTssDiffuse);
    FNavMapLightingOff LightingOff; // 0x0049deaa..0x0049deea

    // 0x0049dee4..0x0049e036: the vertices into handle DAT_006720bc.
    FRpVertexSrcDesc Desc = MakeColorDesc(ShadeVertices, ShadeVertexCount);
    if (!NavMapUploadVertices(bRebuilt, &G_NavMapBufferHandles[1], 0x42, ShadeVertexCount, &Desc,
                              &ShadeVbOffset))
        return;

    // 0x0049e038..0x0049e0e6: the indices, then the triangle list.
    unsigned IndexOffset = 0;
    if (G_NavMapIndexBuffer->CopyIndices(reinterpret_cast<void*>(static_cast<intptr_t>(-1)),
                                         &IndexOffset, ShadeIndices,
                                         static_cast<unsigned>(ShadeIndexCount)) != 0) // +0x18
        return;
    if (G_NavMapIndexBuffer->SelectIb(reinterpret_cast<void*>(static_cast<intptr_t>(-1)),
                                      ShadeVbOffset, 0, 0) != 0) // +0x24
        return;
    G_NavMapDraw->DrawIndexedPrimitive(4, 0, static_cast<unsigned>(ShadeVertexCount), IndexOffset,
                                       static_cast<unsigned>(ShadeIndexCount)); // +0x0c

    // 0x0049e0e9..0x0049e146: the state table 0x005d2e4c, twice.
    ApplyRenderStates(kNavMapRsOff);
    ApplyRenderStates(kNavMapRsOff);
}

bool NeuroNetNavMap::BuildZoneShading()
{
    // FUN_0049c740.
    int const MissionZoneCount = static_cast<int>(MissionZoneIds.size());
    std::list<Universe::FSystemZone> const* const Zones = CurrentSystem->GetZones(); // ISystem +0x18
    BestPathGetFirst(); // FUN_004c5040 -- its answer is not used

    // 0x0049c787..0x0049c995: nothing moved -- once a second (DAT_005d2ed0) count the
    // qualifying zones, and rebuild only when that count changed.
    if (PanOffset[0] == ShadeCachePan[0] && PanOffset[1] == ShadeCachePan[1] &&
        PanOffset[2] == ShadeCachePan[2] && ShadeCacheZoom == ZoomScale &&
        ShadeCacheSystem == CurrentSystem->GetId() &&
        ShadeCacheZoneFilter == G_NavMapZoneFilter && ShadeCacheFilterMode == G_NavMapFilterMode)
    {
        double const Sum = GetClientFrameTimeCapped() + static_cast<double>(ShadeRecheckTimer);
        ShadeRecheckTimer = static_cast<float>(Sum);
        if (Sum < 1.0) // TEST AH,5 + JP: on only when not less
            return false;
        ShadeRecheckTimer = static_cast<float>(Sum - 1.0);
        int Count = 0;
        for (std::list<Universe::FSystemZone>::const_iterator It = Zones->begin(); It != Zones->end();
             ++It)
        {
            Universe::IZone const* const Zone = It->Zone;
            int const Population = Zone->GetPopulationCount();
            if (!ShadeZoneQualifies(Zone, Population, G_NavMapFilterMode, MissionZoneIds,
                                    MissionZoneCount))
                continue;
            if (Field_0x87e != 0 || IsZoneVisited(Zone, 1))
                ++Count;
        }
        if (Count == ShadeZoneCount)
            return false;
        ShadeZoneCount = Count;
    }

    // 0x0049c99b..0x0049c9f0: the view it is built for.
    ShadeCachePan[0] = PanOffset[0];
    ShadeCachePan[1] = PanOffset[1];
    ShadeCachePan[2] = PanOffset[2];
    ShadeCacheZoom = ZoomScale;
    ShadeCacheSystem = CurrentSystem->GetId();
    ShadeCacheZoneFilter = G_NavMapZoneFilter;
    ShadeCacheFilterMode = G_NavMapFilterMode;
    FNavMapColorVertex* Vertex = reinterpret_cast<FNavMapColorVertex*>(ShadeVertices);
    ShadeVertexCount = 0;
    ShadeIndexCount = 0;
    FNavMapFrame const Frame = MakeZoneFrame(this);
    int const PlayerVibe = GetPlayerSpawnSystemIdRetail(); // FUN_004c4670 (DAT_0061226c)

    for (std::list<Universe::FSystemZone>::const_iterator It = Zones->begin(); It != Zones->end(); ++It)
    {
        Universe::IZone const* const Zone = It->Zone;
        int const Population = Zone->GetPopulationCount();
        if (!ShadeZoneQualifies(Zone, Population, G_NavMapFilterMode, MissionZoneIds,
                                MissionZoneCount))
            continue;
        if (Field_0x87e == 0 && !IsZoneVisited(Zone, 1))
            continue;

        // 0x0049cc72..0x0049cd2b: the population's feeling towards the player, weighted.
        float WeightedFeel = 0.0f;
        float TotalWeight = 0.0f;
        if (Population > 0)
        {
            for (int Index = 0; Index < Population; ++Index)
            {
                unsigned Group = 0;
                float Weight = 0.0f;
                if (!Zone->GetPopulationEntry(Index, Group, Weight))
                    continue;
                float Feel = 0.0f;
                if (Reputation::Vibe::GetGroupFeelingsTowards(PlayerVibe, Group, Feel) != 0)
                    continue;
                WeightedFeel = static_cast<float>(static_cast<double>(Feel) * static_cast<double>(Weight) +
                                                  static_cast<double>(WeightedFeel));
                TotalWeight = static_cast<float>(static_cast<double>(Weight) +
                                                 static_cast<double>(TotalWeight));
            }
            if (TotalWeight == 0.0f) // TEST AH,0x44 + JP: kept unless equal
                TotalWeight = 1.0f;
        }
        else
        {
            TotalWeight = 1.0f;
        }
        float const Feel = static_cast<float>(static_cast<double>(WeightedFeel) /
                                              static_cast<double>(TotalWeight));

        // 0x0049cd1f..0x0049ce29: filter modes 1 / 2 keep only the zone filter's band -- all
        // (0): neither hostile nor friendly; 1: at or below -0.6; 2: at or above 0.6.
        int const Mode = G_NavMapFilterMode;
        if (Mode == 1 || Mode == 2)
        {
            int const ZoneFilter = G_NavMapZoneFilter;
            if (ZoneFilter == 0)
            {
                if (!(Feel > kNavMapHostileFeel))
                    continue;
                if (!(Feel < kNavMapFriendlyFeel))
                    continue;
            }
            else if (ZoneFilter == 1)
            {
                if (Feel > kNavMapHostileFeel)
                    continue;
            }
            else if (ZoneFilter == 2)
            {
                if (Feel < kNavMapFriendlyFeel)
                    continue;
            }
        }

        // 0x0049ce2f..0x0049ce79: the colour by feeling (amber in mode 5), at half alpha outside
        // modes 1 / 2.
        unsigned long ColourDword = 0;
        NnReputationColorArgb(&ColourDword, Feel); // FUN_0045a650
        if (Mode == 5)
            ColourDword = G_NnPaletteArgb_679ba4;
        unsigned char Rgba[4];
        memcpy(Rgba, &ColourDword, sizeof(Rgba));
        if (Mode != 2 && Mode != 1)
            Rgba[3] = static_cast<unsigned char>(
                static_cast<int>(static_cast<double>(Rgba[3]) * static_cast<double>(kNavMapHalf)));

        // 0x0049ce7d..0x0049cf16: the zone's size and transform in map units, its shape.
        Vector Size = ZoneMapSize(Zone, SystemScale);
        FTransform const Xform = ZoneMapTransform(Zone, SystemScale);
        unsigned const Shape = Zone->GetShape();

        float Points[38][3];
        int PointCount = 0;
        if (Shape == 1)
        {
            // caseD_1 0x0049d11d..0x0049d1f2: 16 points round the sphere's equator, the centre.
            float Angle = 0.0f;
            for (int Index = 0; Index < 16; ++Index)
            {
                float const LocalX = static_cast<float>(sin(static_cast<double>(Angle)) * static_cast<double>(Size.x));
                float const LocalZ = static_cast<float>(cos(static_cast<double>(Angle)) * static_cast<double>(Size.x));
                TransformPoint(Xform, LocalX, 0.0f, LocalZ, Points[Index]);
                Angle = static_cast<float>(static_cast<double>(Angle) + static_cast<double>(kNavMapStep16));
            }
            memcpy(Points[16], &Xform.Translation, sizeof(Points[16]));
            PointCount = 17;
        }
        else if (Shape == 2)
        {
            // caseD_2 0x0049d1f7..0x0049d2ff: 16 points of the ellipsoid's outline, the centre.
            float Angle = 0.0f;
            for (int Index = 0; Index < 16; ++Index)
            {
                ZoneEllipsePoint(Zone, Angle, SystemScale, Xform, Points[Index]);
                Angle = static_cast<float>(static_cast<double>(Angle) + static_cast<double>(kNavMapStep16));
            }
            memcpy(Points[16], &Xform.Translation, sizeof(Points[16]));
            PointCount = 17;
        }
        else if (Shape == 3)
        {
            // caseD_3 0x0049cf1d..0x0049d118: the box's four top corners.
            float const HalfX = static_cast<float>(static_cast<double>(Size.x) * 0.5);
            float const HalfY = static_cast<float>(static_cast<double>(Size.y) * 0.5);
            float const HalfZ = static_cast<float>(static_cast<double>(Size.z) * 0.5);
            TransformPoint(Xform, HalfX, HalfY, HalfZ, Points[0]);
            TransformPoint(Xform, -HalfX, HalfY, HalfZ, Points[1]);
            TransformPoint(Xform, -HalfX, HalfY, -HalfZ, Points[2]);
            TransformPoint(Xform, HalfX, HalfY, -HalfZ, Points[3]);
            PointCount = 4;
        }
        else if (Shape == 4)
        {
            // caseD_4 0x0049d304..0x0049d878: the cylinder -- its side as a rectangle and both
            // caps as 16-point rings with their centres (38 points). In the zoomed view (or a
            // transition) the radius shrinks with the zoom.
            Size.y = static_cast<float>(static_cast<double>(Size.y) * 0.5);
            if (bTransition != 0 || ViewMode == 2)
            {
                double const Shrink = (static_cast<double>(ZoomScale) - 1.0) *
                                      static_cast<double>(kNavMapLaneWidthPerZoom);
                Size.x = static_cast<float>(static_cast<double>(Size.x) / (Shrink + Shrink + 1.0));
            }
            float const Radius = Size.x;
            float const HalfLen = Size.y;
            TransformPoint(Xform, Radius, HalfLen, 0.0f, Points[0]);
            TransformPoint(Xform, -Radius, HalfLen, 0.0f, Points[1]);
            TransformPoint(Xform, -Radius, -HalfLen, 0.0f, Points[2]);
            TransformPoint(Xform, Radius, -HalfLen, 0.0f, Points[3]);

            // 0x0049d509..0x0049d6e9: the caps' centres along the local y axis (column 1 of
            // the rotation) -- the x offset rounded, the y / z ones not -- with identity
            // rotations.
            float const OffsetX = static_cast<float>(static_cast<double>(Xform.Rotation.D[0][1]) *
                                                     static_cast<double>(HalfLen));
            double const OffsetY = static_cast<double>(Xform.Rotation.D[1][1]) * static_cast<double>(HalfLen);
            double const OffsetZ = static_cast<double>(Xform.Rotation.D[2][1]) * static_cast<double>(HalfLen);
            FTransform Top;
            FTransform Bottom;
            memset(&Top, 0, sizeof(Top));
            Top.Rotation.D[0][0] = 1.0f;
            Top.Rotation.D[1][1] = 1.0f;
            Top.Rotation.D[2][2] = 1.0f;
            Bottom = Top;
            Top.Translation.X = static_cast<float>(static_cast<double>(OffsetX) +
                                                   static_cast<double>(Xform.Translation.X));
            Top.Translation.Y = static_cast<float>(OffsetY + static_cast<double>(Xform.Translation.Y));
            Top.Translation.Z = static_cast<float>(static_cast<double>(Xform.Translation.Z) + OffsetZ);
            Bottom.Translation.X = static_cast<float>(static_cast<double>(Xform.Translation.X) -
                                                      static_cast<double>(OffsetX));
            Bottom.Translation.Y = static_cast<float>(static_cast<double>(Xform.Translation.Y) - OffsetY);
            Bottom.Translation.Z = static_cast<float>(static_cast<double>(Xform.Translation.Z) - OffsetZ);

            // 0x0049d704..0x0049d81e: both rings (sin / cos kept as doubles for the second).
            float Angle = 0.0f;
            for (int Index = 0; Index < 16; ++Index)
            {
                double const SinA = sin(static_cast<double>(Angle));
                double const CosA = cos(static_cast<double>(Angle));
                TransformPoint(Top, static_cast<float>(SinA * static_cast<double>(Radius)), 0.0f,
                               static_cast<float>(CosA * static_cast<double>(Radius)), Points[4 + Index]);
                TransformPoint(Bottom, static_cast<float>(static_cast<double>(Radius) * SinA), 0.0f,
                               static_cast<float>(static_cast<double>(Radius) * CosA), Points[21 + Index]);
                Angle = static_cast<float>(static_cast<double>(Angle) + static_cast<double>(kNavMapStep16));
            }
            memcpy(Points[20], &Top.Translation, sizeof(Points[20]));
            memcpy(Points[37], &Bottom.Translation, sizeof(Points[37]));
            PointCount = 38;
        }
        else
        {
            continue;
        }

        // 0x0049d881..0x0049d95b: to the map frame.
        for (int Index = 0; Index < PointCount; ++Index)
            ProjectToFrame(this, Frame, Points[Index]);

        // 0x0049d963..0x0049d994: the ARGB dword; the 6000-vertex buffer ends the build.
        unsigned long const Argb = (static_cast<unsigned long>(Rgba[3]) << 24) |
                                   (static_cast<unsigned long>(Rgba[0]) << 16) |
                                   (static_cast<unsigned long>(Rgba[1]) << 8) |
                                   static_cast<unsigned long>(Rgba[2]);
        if (ShadeVertexCount + PointCount >= 0x1770)
            return true;
        for (int Index = 0; Index < PointCount; ++Index)
        {
            Vertex->X = Points[Index][0];
            Vertex->Y = Points[Index][1];
            Vertex->Z = Points[Index][2];
            Vertex->Diffuse = Argb;
            ++Vertex;
        }

        // 0x0049d9eb..0x0049dc84: the triangles -- a box as two, the cylinder as its side and
        // two fans, the rest as one fan round the last point.
        int const Base = ShadeVertexCount;
        unsigned short* const Index16 = ShadeIndices;
        if (PointCount == 4)
        {
            ShadeIndexCount += 6;
            int At = ShadeIndexCount - 6;
            Index16[At++] = static_cast<unsigned short>(Base);
            Index16[At++] = static_cast<unsigned short>(Base + 1);
            Index16[At++] = static_cast<unsigned short>(Base + 2);
            Index16[At++] = static_cast<unsigned short>(Base);
            Index16[At++] = static_cast<unsigned short>(Base + 2);
            Index16[At++] = static_cast<unsigned short>(Base + 3);
        }
        else if (Shape == 4)
        {
            int At = ShadeIndexCount;
            ShadeIndexCount += 6;
            Index16[At++] = static_cast<unsigned short>(Base);
            Index16[At++] = static_cast<unsigned short>(Base + 1);
            Index16[At++] = static_cast<unsigned short>(Base + 2);
            Index16[At++] = static_cast<unsigned short>(Base);
            Index16[At++] = static_cast<unsigned short>(Base + 2);
            Index16[At++] = static_cast<unsigned short>(Base + 3);
            ShadeIndexCount += 0x30;
            for (int Ring = 4; Ring < 0x13; ++Ring)
            {
                Index16[At++] = static_cast<unsigned short>(Base + Ring);
                Index16[At++] = static_cast<unsigned short>(Base + Ring + 1);
                Index16[At++] = static_cast<unsigned short>(Base + 0x14);
            }
            Index16[At++] = static_cast<unsigned short>(Base + 0x13);
            Index16[At++] = static_cast<unsigned short>(Base + 4);
            Index16[At++] = static_cast<unsigned short>(Base + 0x14);
            ShadeIndexCount += 0x30;
            for (int Ring = 0x15; Ring < 0x24; ++Ring)
            {
                Index16[At++] = static_cast<unsigned short>(Base + Ring);
                Index16[At++] = static_cast<unsigned short>(Base + Ring + 1);
                Index16[At++] = static_cast<unsigned short>(Base + 0x25);
            }
            Index16[At++] = static_cast<unsigned short>(Base + 0x24);
            Index16[At++] = static_cast<unsigned short>(Base + 0x15);
            Index16[At++] = static_cast<unsigned short>(Base + 0x25);
        }
        else
        {
            int At = ShadeIndexCount;
            ShadeIndexCount += PointCount * 3 - 3;
            for (int Fan = 0; Fan < PointCount - 2; ++Fan)
            {
                Index16[At++] = static_cast<unsigned short>(Base + Fan);
                Index16[At++] = static_cast<unsigned short>(Base + Fan + 1);
                Index16[At++] = static_cast<unsigned short>(Base + PointCount - 1);
            }
            Index16[At++] = static_cast<unsigned short>(Base + PointCount - 2);
            Index16[At++] = static_cast<unsigned short>(Base);
            Index16[At++] = static_cast<unsigned short>(Base + PointCount - 1);
        }
        ShadeVertexCount += PointCount;
    }
    return true;
}

// ----------------------------------------------------------------------------------------
// The terrain patches
// ----------------------------------------------------------------------------------------

void NeuroNetNavMap::DrawTerrainPatches()
{
    // FUN_0049c260.
    bool const bRebuilt = BuildTerrainPatches(); // FUN_0049b600
    if (TerrainVertexCount == 0)
        return;

    // 0x0049c29a..0x0049c38a: the modelview at the zone depth; depth test and culling off,
    // blending on (lighting and texturing are left as they are).
    BindMapModelview(this, kNavMapZoneDepth);
    Win3DDisable(kWin3DDepthTest);
    Win3DDisable(kWin3DCullFace);
    Win3DEnable(kWin3DBlend);
    Win3DSetBlendFunc(kWin3DBlendSrcAlpha, kWin3DBlendInvSrcAlpha);
    ApplyRenderStates(kNavMapRsOn);
    FNavMapLightingOff LightingOff; // 0x0049c3bb..0x0049c3fa

    // 0x0049c3f4..0x0049c53e: the vertices into handle DAT_006720b8.
    FRpVertexSrcDesc Desc = MakeTexDesc(TerrainVertices, TerrainVertexCount);
    if (!NavMapUploadVertices(bRebuilt, &G_NavMapBufferHandles[0], 0x142, TerrainVertexCount, &Desc,
                              &TerrainVbOffset))
        return;

    // 0x0049c540..0x0049c5a4: the indices.
    unsigned IndexOffset = 0;
    if (G_NavMapIndexBuffer->CopyIndices(reinterpret_cast<void*>(static_cast<intptr_t>(-1)),
                                         &IndexOffset, TerrainIndices,
                                         static_cast<unsigned>(TerrainIndexCount)) != 0)
        return;
    if (G_NavMapIndexBuffer->SelectIb(reinterpret_cast<void*>(static_cast<intptr_t>(-1)),
                                      TerrainVbOffset, 0, 0) != 0)
        return;

    // 0x0049c5c9..0x0049c6b3: one draw per patch with its terrain texture on stage 0.
    IRenderPipeline* const Pipeline = DALib::Pipeline;
    for (int Patch = 0; Patch < TerrainPatchCount; ++Patch)
    {
        Pipeline->SetTextureStageTexture(
            0, TexShapeGetGpuHandle(TerrainShapes[TerrainPatchShape[Patch]], 0)); // +0x108
        Pipeline->SetTextureStageState(0, 0xb, 0);
        ApplyStageStates(0, kNavMapTssTexture);
        ApplyStageStates(1, kNavMapTssDiffuse);
        G_NavMapDraw->DrawIndexedPrimitive(
            4, static_cast<unsigned>(TerrainMinIndex[Patch]),
            static_cast<unsigned>(TerrainNumVertices[Patch]),
            static_cast<unsigned>(TerrainStartIndex[Patch]) + IndexOffset,
            static_cast<unsigned>(TerrainPatchIndexCount[Patch]));
    }

    // 0x0049c6bb..0x0049c6f6: the texture off, the state table 0x005d2e4c.
    Pipeline->SetTextureStageTexture(0, nullptr);
    ApplyRenderStates(kNavMapRsOff);
}

bool NeuroNetNavMap::BuildTerrainPatches()
{
    // FUN_0049b600.
    int const ZoneCount = static_cast<int>(MapZones.size());

    // 0x0049b627..0x0049b768: nothing moved -- once a second (DAT_005d2ecc) count the visited
    // terrain zones, and rebuild only when that count changed.
    if (PanOffset[0] == TerrainCachePan[0] && PanOffset[1] == TerrainCachePan[1] &&
        PanOffset[2] == TerrainCachePan[2] && TerrainCacheZoom == ZoomScale &&
        TerrainCacheSystem == CurrentSystem->GetId())
    {
        double const Sum = GetClientFrameTimeCapped() + static_cast<double>(TerrainRecheckTimer);
        TerrainRecheckTimer = static_cast<float>(Sum);
        if (Sum < 1.0) // TEST AH,5 + JP: on only when not less
            return false;
        TerrainRecheckTimer = static_cast<float>(Sum - 1.0);
        int Count = 0;
        for (int Index = 0; Index < ZoneCount; ++Index)
        {
            Universe::IZone const* const Zone = MapZones[static_cast<size_t>(Index)];
            if ((Zone->GetProperties() & 0x3ffc0) == 0)
                continue;
            if (Field_0x87e != 0 || IsZoneVisited(Zone, 1))
                ++Count;
        }
        if (Count == TerrainZoneCount)
            return false;
        TerrainZoneCount = Count;
    }

    // 0x0049b76a..0x0049b94f: the view it is built for, the frame, everything emptied.
    TerrainCachePan[0] = PanOffset[0];
    TerrainCachePan[1] = PanOffset[1];
    TerrainCachePan[2] = PanOffset[2];
    TerrainCacheZoom = ZoomScale;
    TerrainCacheSystem = CurrentSystem->GetId();
    FNavMapFrame const Frame = MakeZoneFrame(this);
    TerrainVertexCount = 0;
    TerrainIndexCount = 0;
    TerrainPatchCount = 0;
    int PreviousShape = -1;
    FNavMapTexVertex* Vertex = reinterpret_cast<FNavMapTexVertex*>(TerrainVertices);

    for (int ZoneIndex = 0; ZoneIndex < ZoneCount; ++ZoneIndex)
    {
        Universe::IZone const* const Zone = MapZones[static_cast<size_t>(ZoneIndex)];
        unsigned long const Properties = Zone->GetProperties();
        if ((Properties & 0x3ffc0) == 0)
            continue;
        if (Field_0x87e == 0 && !IsZoneVisited(Zone, 1))
            continue;

        // 0x0049b9bf..0x0049bad3: the terrain texture by the first property bit; a 0x8000
        // zone takes IZone slot 0x24's colour, brightened by 1.5 and clamped to 1.
        int Shape = -1;
        bool bNoJitter = false;
        float Colour[3] = {0.0f, 0.0f, 0.0f};
        if ((Properties & 0x40) != 0)
            Shape = 0;
        else if ((Properties & 0x80) != 0)
            Shape = 1;
        else if ((Properties & 0x100) != 0)
            Shape = 5;
        else if ((Properties & 0x200) != 0)
            Shape = 6;
        else if ((Properties & 0x400) != 0)
            Shape = 7;
        else if ((Properties & 0x800) != 0)
            Shape = 8;
        else if ((Properties & 0x1000) != 0)
            Shape = 3;
        else if ((Properties & 0x2000) != 0)
            Shape = 4;
        else if ((Properties & 0x8000) != 0)
        {
            Shape = 2;
            Vector const& Tint = Zone->GetFogColor();
            Colour[0] = static_cast<float>(static_cast<double>(Tint.x) * static_cast<double>(kNavMapZoneColourBoost));
            Colour[1] = static_cast<float>(static_cast<double>(Tint.y) * static_cast<double>(kNavMapZoneColourBoost));
            Colour[2] = static_cast<float>(static_cast<double>(Tint.z) * static_cast<double>(kNavMapZoneColourBoost));
            for (int Channel = 0; Channel < 3; ++Channel)
            {
                if (Colour[Channel] > 1.0f) // TEST AH,0x41 + JNZ: kept unless greater
                    Colour[Channel] = 1.0f;
            }
        }

        // 0x0049badb..0x0049bb30: 0x30000 zones are black, texture 2, unjittered; no texture
        // at all (or a missing shape) is no patch.
        if ((Properties & 0x30000) != 0)
        {
            Colour[0] = 0.0f;
            Colour[1] = 0.0f;
            Colour[2] = 0.0f;
            Shape = 2;
            bNoJitter = true;
        }
        else if (Shape == -1)
        {
            continue;
        }
        if (TerrainShapes[Shape] == nullptr)
            continue;

        // 0x0049bb36..0x0049bb6e: opaque, fading with the zoom during a transition, faint in
        // the zoomed view.
        int Alpha = 0xff;
        if (bTransition != 0)
            Alpha = static_cast<int>(static_cast<double>(kNavMapTerrainAlphaSpan) /
                                         static_cast<double>(ZoomScale) +
                                     static_cast<double>(kNavMapTerrainAlphaBase)); // ftol
        else if (ViewMode == 2)
            Alpha = 0x56;

        // 0x0049bb6e..0x0049bbce: a texture change opens a new patch.
        if (Shape != PreviousShape)
        {
            int const Patch = TerrainPatchCount;
            TerrainMinIndex[Patch] = TerrainVertexCount;
            TerrainPatchShape[Patch] = Shape;
            TerrainNumVertices[Patch] = 0;
            TerrainStartIndex[Patch] = TerrainIndexCount;
            TerrainPatchIndexCount[Patch] = 0;
            PreviousShape = Shape;
            TerrainPatchCount = Patch + 1;
        }

        // 0x0049bbd4..0x0049bc35: white at that alpha, or texture 2's colour.
        unsigned long Argb = (static_cast<unsigned long>(Alpha) << 24) | 0xffffffu;
        if (Shape == 2)
        {
            int const Red = static_cast<int>(static_cast<double>(Colour[0]) * static_cast<double>(kNavMap255));
            int const Green = static_cast<int>(static_cast<double>(Colour[1]) * static_cast<double>(kNavMap255));
            int const Blue = static_cast<int>(static_cast<double>(Colour[2]) * static_cast<double>(kNavMap255));
            unsigned long Packed = (static_cast<unsigned long>(Red) & 0xff) |
                                   (static_cast<unsigned long>(Alpha) << 8);
            Packed = (Packed << 8) | (static_cast<unsigned long>(Green) & 0xff);
            Packed = (Packed << 8) | (static_cast<unsigned long>(Blue) & 0xff);
            Argb = Packed;
        }

        // 0x0049bc40..0x0049bccb: the zone in map units; spheres and ellipsoids only.
        Vector const Size = ZoneMapSize(Zone, SystemScale);
        FTransform const Xform = ZoneMapTransform(Zone, SystemScale);
        unsigned const ZoneShape = Zone->GetShape();
        float Points[33][3];
        if (ZoneShape == 1)
        {
            // 0x0049be5c..0x0049bf46: 32 points round the sphere's equator.
            float Angle = 0.0f;
            for (int Index = 0; Index < 32; ++Index)
            {
                float const LocalX = static_cast<float>(sin(static_cast<double>(Angle)) * static_cast<double>(Size.x));
                float const LocalZ = static_cast<float>(cos(static_cast<double>(Angle)) * static_cast<double>(Size.x));
                TransformPoint(Xform, LocalX, 0.0f, LocalZ, Points[Index]);
                Angle = static_cast<float>(static_cast<double>(Angle) + static_cast<double>(kNavMapStep32));
                if (!bNoJitter)
                    JitterTerrainPoint(Points[Index]);
            }
        }
        else if (ZoneShape == 2)
        {
            // 0x0049bcd1..0x0049bdd3: 32 points of the ellipsoid's outline.
            float Angle = 0.0f;
            for (int Index = 0; Index < 32; ++Index)
            {
                ZoneEllipsePoint(Zone, Angle, SystemScale, Xform, Points[Index]);
                if (!bNoJitter)
                    JitterTerrainPoint(Points[Index]);
                Angle = static_cast<float>(static_cast<double>(Angle) + static_cast<double>(kNavMapStep32));
            }
        }
        else
        {
            continue;
        }
        // 0x0049bdd9..0x0049be57 / 0x0049bf4c..0x0049bfcd: the centre, jittered the same way.
        memcpy(Points[32], &Xform.Translation, sizeof(Points[32]));
        if (!bNoJitter)
            JitterTerrainPoint(Points[32]);

        // 0x0049bfd4..0x0049c09d: to the map frame.
        for (int Index = 0; Index < 33; ++Index)
            ProjectToFrame(this, Frame, Points[Index]);

        // 0x0049c0a3..0x0049c17d: the 33 vertices -- the texture spans 1 / (zoom * 0.09) map
        // units round the centre.
        int const Base = TerrainVertexCount;
        TerrainVertexCount = Base + 33;
        TerrainNumVertices[TerrainPatchCount - 1] += 33;
        double const UvScale = kNavMapOne / (static_cast<double>(ZoomScale) *
                                             static_cast<double>(kNavMapTerrainUvScale));
        for (int Index = 0; Index < 32; ++Index)
        {
            Vertex->X = Points[Index][0];
            Vertex->Y = Points[Index][1];
            Vertex->Z = Points[Index][2];
            Vertex->Diffuse = Argb;
            Vertex->U = static_cast<float>((static_cast<double>(Points[Index][0]) -
                                            static_cast<double>(Points[32][0])) * UvScale + kNavMapHalfD);
            Vertex->V = static_cast<float>((static_cast<double>(Points[Index][1]) -
                                            static_cast<double>(Points[32][1])) * UvScale + kNavMapHalfD);
            ++Vertex;
        }
        Vertex->X = Points[32][0];
        Vertex->Y = Points[32][1];
        Vertex->Z = Points[32][2];
        Vertex->Diffuse = Argb;
        Vertex->U = 0.5f;
        Vertex->V = 0.5f;
        ++Vertex;

        // 0x0049c181..0x0049c231: 32 triangles round the centre.
        TerrainPatchIndexCount[TerrainPatchCount - 1] += 0x60;
        unsigned short* const Index16 = TerrainIndices;
        for (int Fan = 0; Fan < 31; ++Fan)
        {
            Index16[TerrainIndexCount++] = static_cast<unsigned short>(Base + Fan);
            Index16[TerrainIndexCount++] = static_cast<unsigned short>(Base + Fan + 1);
            Index16[TerrainIndexCount++] = static_cast<unsigned short>(Base + 32);
        }
        Index16[TerrainIndexCount++] = static_cast<unsigned short>(Base + 31);
        Index16[TerrainIndexCount++] = static_cast<unsigned short>(Base);
        Index16[TerrainIndexCount++] = static_cast<unsigned short>(Base + 32);
    }
    return true;
}

// ----------------------------------------------------------------------------------------
// The trade lanes
// ----------------------------------------------------------------------------------------

void NeuroNetNavMap::DrawTradeLanes()
{
    // FUN_004992b0 0x004992d1..0x0049931b: the animation frame, one step per 1/15 s.
    double const Sum = GetClientFrameTimeCapped() + static_cast<double>(TradeLaneFrameTimer);
    TradeLaneFrameTimer = static_cast<float>(Sum);
    if (Sum > static_cast<double>(kNavMapTradeLaneFrameTime)) // TEST AH,0x41 + JNZ: on only when greater
    {
        TradeLaneFrameTimer = static_cast<float>(Sum - static_cast<double>(kNavMapTradeLaneFrameTime));
        int const Frame = TradeLaneFrame + 1;
        TradeLaneFrame = Frame;
        if (Frame >= 0x10)
            TradeLaneFrame = 0;
    }

    bool const bRebuilt = BuildTradeLanes(); // FUN_00498bd0
    if (TradeLaneVertexCount == 0)
        return;

    // 0x00499332..0x0049944e: the modelview at the zone depth, the line state.
    BeginMapLines(this, kNavMapZoneDepth);
    // 0x00499453..0x00499526: the frame's texture on stage 0, the stage and render tables.
    IRenderPipeline* const Pipeline = DALib::Pipeline;
    Pipeline->SetTextureStageTexture(0, TexShapeGetGpuHandle(TradeLaneShapes[TradeLaneFrame], 0));
    Pipeline->SetTextureStageState(0, 0xb, 0);
    ApplyStageStates(0, kNavMapTssTexture);
    ApplyStageStates(1, kNavMapTssDiffuse);
    ApplyRenderStates(kNavMapRsOn);
    FNavMapLightingOff LightingOff; // 0x00499528..0x00499565

    // 0x00499561..0x004996a6: the vertices into handle DAT_006720c0.
    FRpVertexSrcDesc Desc = MakeTexDesc(TradeLaneVertices, TradeLaneVertexCount);
    if (!NavMapUploadVertices(bRebuilt, &G_NavMapBufferHandles[2], 0x142, TradeLaneVertexCount,
                              &Desc, &TradeLaneVbOffset))
        return;

    // 0x004996a8..0x0049974f: the indices, the triangle list.
    unsigned IndexOffset = 0;
    if (G_NavMapIndexBuffer->CopyIndices(reinterpret_cast<void*>(static_cast<intptr_t>(-1)),
                                         &IndexOffset, TradeLaneIndices,
                                         static_cast<unsigned>(TradeLaneIndexCount)) != 0)
        return;
    if (G_NavMapIndexBuffer->SelectIb(reinterpret_cast<void*>(static_cast<intptr_t>(-1)),
                                      TradeLaneVbOffset, 0, 0) != 0)
        return;
    G_NavMapDraw->DrawIndexedPrimitive(4, 0, static_cast<unsigned>(TradeLaneVertexCount), IndexOffset,
                                       static_cast<unsigned>(TradeLaneIndexCount));

    // 0x00499752..0x0049978d: the texture off, the state table 0x005d2e4c.
    Pipeline->SetTextureStageTexture(0, nullptr);
    ApplyRenderStates(kNavMapRsOff);
}

bool NeuroNetNavMap::BuildTradeLanes()
{
    // FUN_00498bd0: rebuilt only when the pan, zoom, system or segment count changed.
    int const SegmentCount = static_cast<int>(LaneSegments.size());
    if (PanOffset[0] == TradeLaneCachePan[0] && PanOffset[1] == TradeLaneCachePan[1] &&
        PanOffset[2] == TradeLaneCachePan[2] && TradeLaneCacheZoom == ZoomScale &&
        TradeLaneCacheSystem == CurrentSystem->GetId() && TradeLaneCacheSegments == SegmentCount)
        return false;

    // 0x00498c7e..0x00498cc2: the view it is built for, everything emptied.
    TradeLaneCachePan[0] = PanOffset[0];
    TradeLaneCachePan[1] = PanOffset[1];
    TradeLaneCachePan[2] = PanOffset[2];
    TradeLaneCacheZoom = ZoomScale;
    TradeLaneCacheSystem = CurrentSystem->GetId();
    TradeLaneCacheSegments = SegmentCount;
    TradeLaneVertexCount = 0;
    TradeLaneIndexCount = 0;

    // 0x00498cc8..0x00498e70: the frame and the band's half width from the zoom.
    FNavMapFrame const Frame = MakeZoneFrame(this);
    float const ZoomM1 = static_cast<float>(static_cast<double>(ZoomScale) - 1.0);
    float const HalfWidth = static_cast<float>(static_cast<double>(ZoomM1) *
                                                   static_cast<double>(kNavMapLaneWidthPerZoom) *
                                                   static_cast<double>(kNavMapLaneWidthScale) +
                                               static_cast<double>(kNavMapLaneWidthBase));
    if (SegmentCount <= 0)
        return true;

    FNavMapTexVertex* Vertex = reinterpret_cast<FNavMapTexVertex*>(TradeLaneVertices);
    unsigned short* const Index16 = TradeLaneIndices;
    for (int Segment = 0; Segment < SegmentCount; ++Segment)
    {
        // 0x00498e86..0x00498f67: both ends to the map frame.
        FNavMapSegment const& Lane = LaneSegments[static_cast<size_t>(Segment)];
        float From[3];
        float To[3];
        memcpy(From, Lane.Start, sizeof(From));
        memcpy(To, Lane.End, sizeof(To));
        ProjectToFrame(this, Frame, From);
        ProjectToFrame(this, Frame, To);

        // 0x00498f6b..0x004990d7: six triangles' indices (two quads either side of the line).
        int const Base = TradeLaneVertexCount;
        int At = TradeLaneIndexCount;
        TradeLaneIndexCount += 12;
        int const Offsets[12] = {0, 1, 2, 2, 3, 1, 4, 5, 6, 6, 7, 5};
        for (int Corner = 0; Corner < 12; ++Corner)
            Index16[At++] = static_cast<unsigned short>(Base + Offsets[Corner]);
        TradeLaneVertexCount = Base + 8;

        // 0x00498f71..0x004990a6: the side offset from atan2(dx, dy) + a quarter turn -- sin
        // times the half width unrounded; cos rounded, times the half width, used unrounded on
        // this side and rounded on the other.
        double const Dx = static_cast<double>(To[0]) - static_cast<double>(From[0]);
        double const Dy = static_cast<double>(To[1]) - static_cast<double>(From[1]);
        double const Angle = atan2(Dx, Dy) + kNavMapQuarterTurn;
        double const SideX = sin(Angle) * static_cast<double>(HalfWidth);
        float const CosF = static_cast<float>(cos(Angle));
        double const SideY = static_cast<double>(CosF) * static_cast<double>(HalfWidth);
        float const SideYF = static_cast<float>(SideY);
        float const NearPlusX = static_cast<float>(SideX + static_cast<double>(From[0]));
        float const NearPlusY = static_cast<float>(static_cast<double>(From[1]) + SideY);
        float const FarPlusX = static_cast<float>(SideX + static_cast<double>(To[0]));
        float const FarPlusY = static_cast<float>(static_cast<double>(To[1]) + SideY);
        float const NearMinusX = static_cast<float>(-SideX + static_cast<double>(From[0]));
        float const NearMinusY = static_cast<float>(static_cast<double>(From[1]) + static_cast<double>(-SideYF));
        float const FarMinusX = static_cast<float>(-SideX + static_cast<double>(To[0]));
        float const FarMinusY = static_cast<float>(static_cast<double>(To[1]) + static_cast<double>(-SideYF));

        // 0x004990d7..0x00499293: the eight vertices -- on the line opaque at u 0.5, at the
        // edges transparent at u 1; v 0 at From, 1 at To.
        FNavMapTexVertex const Band[8] = {
            {From[0], From[1], From[2], 0xffffffffu, 0.5f, 0.0f},
            {NearPlusX, NearPlusY, From[2], 0x00ffffffu, 1.0f, 0.0f},
            {To[0], To[1], To[2], 0xffffffffu, 0.5f, 1.0f},
            {FarPlusX, FarPlusY, To[2], 0x00ffffffu, 1.0f, 1.0f},
            {From[0], From[1], From[2], 0xffffffffu, 0.5f, 0.0f},
            {NearMinusX, NearMinusY, From[2], 0x00ffffffu, 1.0f, 0.0f},
            {To[0], To[1], To[2], 0xffffffffu, 0.5f, 1.0f},
            {FarMinusX, FarMinusY, To[2], 0x00ffffffu, 1.0f, 1.0f}};
        for (int Corner = 0; Corner < 8; ++Corner)
        {
            *Vertex = Band[Corner];
            ++Vertex;
        }
    }
    return true;
}

// ----------------------------------------------------------------------------------------
// The zone outlines
// ----------------------------------------------------------------------------------------

void NeuroNetNavMap::DrawZoneOutlines()
{
    // FUN_0049aa30 0x0049aa3a..0x0049aa44: nothing unless bDrawZoneOutlines (+0x898). The
    // byte's only writer in freelancer.exe is the constructor's zero (0x0048e337): no other
    // `[reg + 0x898]` access falls inside NavMap.cpp (0x0048dc10..0x004a3f90) and none takes
    // its address. The remaining body (0x0049aa4a..0x0049b5e3: every zone's outline as line
    // strips by shape, with the +0xb48 byte -- also only ever zeroed, 0x0048e34f -- limiting
    // it to terrain zones) is therefore unreachable in retail, and the port stops here.
    if (bDrawZoneOutlines == 0)
        return;
}
