// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// NeuroNetNavMap -- the per-frame work: slot 11 Update and the helpers only it reaches. See
// nn_navmap.h. Retail Client\Interface\NeuroNet\NavMap.cpp.

#include "nn_navmap.h"
#include "best_path.h"              // BestPath* (FUN_004c4fb0 / 4ff0 / 5530 / 52d0 / 58e0)
#include "cargo_bar_control.h"      // ToolTipHoverInit / SetCaption
#include "character_select_equip.h" // GetPlayerStarSystemIdRetail / BaseId / LastPosition
#include "client_mesh_load.h"       // ClientMeshLoadFromPath (FUN_0059ec80)
#include "globals.h"                // GetClientFrameTimeCapped (FUN_0042d680)
#include "hud_element_list.h"       // HudVectorAngle (FUN_0053eb10)
#include "ids_strings.h"            // GetIdsStringWWithRawFallback (FUN_004347e0)
#include "input_manager.h"          // GetClientSoftwareCursor, GetClientMouseButtons
#include "login_mgr.h"              // G_Server (DAT_0067ecd0)
#include "menu_ui_init.h"           // PlayOrRewindUiSoundSlot (FUN_005645c0)
#include "mission_created_solars.h" // FindMissionCreatedSolar (FUN_00476fe0)
#include "nn_inventory.h"           // IsPlayerDockedRetail (FUN_004c4810)
#include "nn_text_scratch.h"        // G_NnTextScratchB (DAT_0066fc60), capacity DAT_006119f8
#include "nn_tooltip.h"             // FUN_004be2d0 / FUN_004be2f0 / FUN_004be370
#include "nn_ui_pulse.h"            // G_NnPaletteArgb_679bb0 / _679b80
#include "player_spawn.h"           // waypoints (FUN_004c4690 / 46a0), GetPlayerConnectionId
#include "space_camera.h"           // FSpaceCamera, AsCamera, FUN_0040f8c0 / FUN_0040fb80
#include "tooltip_hover.h"
#include "win3d.h"                  // Win3DGetMouseRay / Win3DGetCamera / FUN_00423b90
#include "x86math.h"                // FTransform, FVector, FMatrix, I3DMathEngine
#include "common_csys.h"            // Acquire3DMathEngine (FUN_005b73e0)
#include "common_misc.h"            // Geometry::Sphere
#include "common_reputation.h"      // Reputation::Vibe::GetAffiliation / GetName
#include "common_server.h"          // IServer
#include "common_text.h"            // FmtStr
#include "common_universe.h"
#include "DALib.h"                  // DALib::Engine / Data
#include "engine/engbase.h"         // IEngine

#include <io.h>                     // _access
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>                // CharUpperW (FUN_005b74d9)

namespace
{
    // DAT_005c7f68 -- 1/255 as the float 0x3b808081.
    float const kNavMapInv255 = 0.003921568859368563f;
    // DAT_005d1494 -- 0.001f.
    float const kNavMapMilli = 0.001f;
    // DAT_005d3f50 (double) -- the pulse's swing of +0x3c.
    double const kNavMapPulseSwing = 1000.0;
    // DAT_005d3f08 / DAT_005d3f0c.
    float const kNavMapPulseAlphaScale = 64.0f;
    float const kNavMapPulseBase = 191.0f;

    float NavMapByteToUnit(unsigned char Value)
    {
        // FILD of the zero-extended byte, FMUL DAT_005c7f68, FSTP dword.
        return static_cast<float>(static_cast<double>(Value) * kNavMapInv255);
    }

    // DAT_005d3d34 / DAT_005d3d28 / DAT_005d3d24 -- the universe view's system-to-map scale.
    float const kNavMapUniverseScale = 13333.3330078125f; // 0x46505555
    float const kNavMapUniverseOffset = 100000.0f;
    float const kNavMapUniverseLift = 5000.0f;
    // DAT_005d3d38 / DAT_005ca23c.
    float const kNavMapRadToDeg = 57.2957763671875f; // 0x42652ee0 (not the nearest float)
    float const kNavMapFullTurn = 360.0f;

    // DAT_005d3dc8 (double) -- the map plane's depth in front of the camera: a picking ray is
    // scaled to reach it.
    double const kNavMapPlaneDepth = -0.55;
    // DAT_005d3e6c -- the pick circles' margin.
    float const kNavMapPickMargin = 1.0499999523162842f; // 0x3f866666
    // DAT_005ca220 -- a universe-view system's (Type bit 2) pick circle.
    float const kNavMapSystemPickScale = 1.5f;
    // DAT_005d3f10 / DAT_005d1494 -- FUN_0049e890's cursor offsets.
    float const kNavMapRayOffsetX = 0.0024999999441206455f; // 0x3b23d70a
    float const kNavMapRayOffsetY = 0.0010000000474974513f; // 0x3a83126f
    // DAT_005d3e44 -- the object tools close once the cursor moved this far (squared).
    float const kNavMapToolsCloseDist2 = 0.004999999888241291f; // 0x3ba3d70a
    // DAT_005d011c -- a hover tip's y offset below the cursor.
    float const kNavMapHoverDrop = 0.029999999329447746f; // 0x3cf5c28f
    // DAT_005d3e68 / DAT_005d3e64 -- the edge a Kind 2 item (the player) is clamped to.
    float const kNavMapEdge = 112000.0f;
    // DAT_005c77f4 -- degrees to radians (float).
    float const kNavMapDegToRad = 0.01745329238474369f; // 0x3c8efa35
    // DAT_005c9800 / DAT_005d3e4c -- PlaceItems' mesh scale terms.
    float const kNavMapSystemMeshScale = 1.2000000476837158f; // 0x3f99999a
    float const kNavMapMeshScale = 60.0f;
    // FUN_00496600 0x00496bcc: the item's depth, an immediate 0xbf0ccccc -- one ulp short of
    // the nearest float to -0.55 (0xbf0ccccd), so it is spelled out.
    float const kNavMapItemDepth = -0.54999995231628418f; // 0xbf0ccccc
    // DAT_005c8a5c -- pi as a float.
    float const kNavMapPi = 3.1415927410125732f; // 0x40490fdb
    // DAT_005d2edc -- a view switch's duration.
    float const kNavMapSwitchSeconds = 0.4000000059604645f; // 0x3ecccccd
    // DAT_005d3e40 -- the group-position poll period.
    float const kNavMapGroupPollSeconds = 30.0f;
    // 0x41000000 -- NoBestPathText's time on screen.
    float const kNavMapNoBestPathSeconds = 8.0f;

    char const kNavMapSpaceObjectsDir[] = "Interface\\NeuroNet\\NavMap\\NewNavMap\\SpaceObjects\\";
    char const kNavMapSpaceObjectsMat[] =
        "Interface\\NeuroNet\\NavMap\\NewNavMap\\SpaceObjects\\spaceobjects.mat";

    // The universe / system / waypoint markers' tint (FUN_00496e50 0x00496eed..0x00496f4f):
    // the palette colour's four bytes scaled by DAT_005c7f68 into the holder's user floats.
    void NavMapTintFromPalette(FNavMapItem* Item, unsigned long Rgba)
    {
        unsigned char const* const Bytes = reinterpret_cast<unsigned char const*>(&Rgba);
        Item->Mesh.UserFloats[0] = NavMapByteToUnit(Bytes[0]);
        Item->Mesh.UserFloats[1] = NavMapByteToUnit(Bytes[1]);
        Item->Mesh.UserFloats[2] = NavMapByteToUnit(Bytes[2]);
        Item->Mesh.UserFloats[4] = NavMapByteToUnit(Bytes[3]);
    }

    // FUN_00496600 0x00496888..0x00496a06: a Kind 2 item's mesh swapped for Name (the holder
    // released through FUN_0059eee0, then "...\SpaceObjects\" Name ".3db" loaded when the file
    // exists).
    void NavMapReloadItemMesh(FNavMapItem* Item, char const* Name)
    {
        ClientUiMeshHolderRelease(&Item->Mesh); // FUN_0059eee0
        char Path[0x104];
        strcpy(Path, kNavMapSpaceObjectsDir);
        strcat(Path, Name);
        strcat(Path, ".3db");
        char FullPath[0x104];
        GetFileSysPath(FullPath, Path, DALib::Data);
        if (_access(FullPath, 0) == -1)
            return;
        // FUN_0059ec80(path, spaceobjects.mat, 1) (0x00496948..0x00496959 and
        // 0x004969f4..0x00496a06).
        ClientMeshLoadFromPath(&Item->Mesh, Path, kNavMapSpaceObjectsMat, true);
    }

    // The hover tip Update feeds (0x00494e7b..0x00494f95 and its three copies): the cursor's
    // ray point 0.03 lower, no extents, live; the ToolTip's current tip (FUN_004be2f0, a
    // scratch record built with FUN_004144b0 / FUN_004963a0 and torn down by FUN_00444ee0)
    // keeps its position when it shows the same ids; then FUN_004be370.
    void NavMapPlaceHoverAtCursor(FToolTipHoverInfo* Record)
    {
        float Ray[3];
        Win3DGetMouseRay(Ray); // FUN_0059da70
        Record->WorldPos[0] = Ray[0];
        Record->WorldPos[1] = static_cast<float>(static_cast<double>(Ray[1]) -
                                                 static_cast<double>(kNavMapHoverDrop));
        Record->WorldPos[2] = Ray[2];
        Record->HalfExtents[0] = 0.0f;
        Record->HalfExtents[1] = 0.0f;
        Record->HalfExtents[2] = 0.0f;
        Record->bLive = 1;
    }

    void NavMapSubmitHover(FToolTipHoverInfo* Record)
    {
        FToolTipHoverInfo Current;          // caption null + FUN_004144b0
        ToolTipHoverReset(&Current);        // FUN_004963a0
        if (NnToolTipReadHover(&Current) && Current.IdsAlt == Record->IdsAlt)
        {
            Record->WorldPos[0] = Current.WorldPos[0];
            Record->WorldPos[1] = Current.WorldPos[1];
            Record->WorldPos[2] = Current.WorldPos[2];
        }
        NnToolTipFeedHover(Record); // FUN_004be370
        // FUN_00444ee0 on Current: ~FToolTipHoverInfo.
    }

    // The system name the nav map shows (InitialShow 0x004911ec, Update 0x00494c8b /
    // 0x00495f4b): FUN_004347e0 into DAT_0066fc60, CharUpperW, property 0x1d; the hidden
    // systems (and none) show ids 0x62a.
    void NavMapShowSystemName(WinControl* Text, Universe::ISystem const* System,
                              unsigned const* SkipSystemIds)
    {
        bool bHidden = System == nullptr;
        if (!bHidden)
        {
            unsigned const Id = System->GetId();
            for (int Index = 0; Index < 4; ++Index)
            {
                if (SkipSystemIds[Index] == Id)
                {
                    bHidden = true;
                    break;
                }
            }
        }
        if (bHidden)
        {
            Text->SetProperty(0x1e, 0x62a, 0);
            return;
        }
        GetIdsStringWWithRawFallback(static_cast<unsigned>(System->GetStridName()), G_NnTextScratchB,
                                     G_NnTextScratchCapacity);
        CharUpperW(G_NnTextScratchB); // FUN_005b74d9
        Text->SetProperty(0x1d, reinterpret_cast<uintptr_t>(G_NnTextScratchB), 0);
    }
}

// ----------------------------------------------------------------------------------------
// Positions
// ----------------------------------------------------------------------------------------

float* NavMapBasePosition(float* Out, unsigned BaseId, unsigned SystemId)
{
    // FUN_0048d780 (cdecl) -- FUN_00491e10 without the cache.
    Universe::ISystem const* const System = Universe::get_system(SystemId);
    if (System != nullptr)
    {
        // 0048d7a7..0048d7cf: the object record's four TStrings emptied.
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
            if (Object.DockWith != BaseId)
                continue;
            Out[0] = Object.Pos.x;
            Out[1] = Object.Pos.y;
            Out[2] = Object.Pos.z;
            return Out;
        }

        // 0048d80d..0048d851: a base a mission created.
        float Pos[3];
        int IdsInfo;
        if (FindMissionCreatedSolar(BaseId, Pos, &IdsInfo))
        {
            Out[0] = Pos[0];
            Out[1] = Pos[1];
            Out[2] = Pos[2];
            return Out;
        }
    }
    Out[0] = 0.0f;
    Out[1] = 0.0f;
    Out[2] = 0.0f;
    return Out;
}

float* NeuroNetNavMap::CachedBasePosition(float* Out, unsigned BaseId,
                                          Universe::ISystem const* System)
{
    // FUN_00491e10.
    if (CachedObjectId == BaseId)
    {
        Out[0] = CachedObjectPosition[0];
        Out[1] = CachedObjectPosition[1];
        Out[2] = CachedObjectPosition[2];
        return Out;
    }

    if (System != nullptr)
    {
        // 00491e6a..00491e99: the object record's four TStrings emptied.
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
            if (Object.DockWith != BaseId)
                continue;
            // 00491f22..00491f56: cached, then returned.
            CachedObjectPosition[0] = Object.Pos.x;
            CachedObjectPosition[1] = Object.Pos.y;
            CachedObjectPosition[2] = Object.Pos.z;
            CachedObjectId = BaseId;
            Out[0] = Object.Pos.x;
            Out[1] = Object.Pos.y;
            Out[2] = Object.Pos.z;
            return Out;
        }

        // 00491eda..00491f1f: a base a mission created -- not cached.
        float Pos[3];
        int IdsInfo;
        if (FindMissionCreatedSolar(BaseId, Pos, &IdsInfo))
        {
            Out[0] = Pos[0];
            Out[1] = Pos[1];
            Out[2] = Pos[2];
            return Out;
        }
    }
    Out[0] = 0.0f;
    Out[1] = 0.0f;
    Out[2] = 0.0f;
    return Out;
}

void NeuroNetNavMap::PlayerMapPosition(float* OutPos, float* OutHeading)
{
    // FUN_00491be0.
    if (ViewMode == 0)
    {
        // 00491bf7..00491cc8: the universe view -- the player's system's map position, not
        // scaled by SystemScale.
        OutPos[0] = 0.0f;
        OutPos[1] = 0.0f;
        OutPos[2] = 0.0f;
        *OutHeading = 0.0f;
        for (Universe::ISystem* System = Universe::GetFirstSystem(); System != nullptr;
             System = Universe::GetNextSystem())
        {
            if (System->GetId() != GetPlayerStarSystemIdRetail())
                continue;
            Vector const* const Pos = System->GetPos();
            OutPos[0] = Pos->x;
            OutPos[1] = Pos->y;
            OutPos[2] = Pos->z;
            OutPos[2] = static_cast<float>(static_cast<double>(OutPos[1]) *
                                               static_cast<double>(kNavMapUniverseScale) -
                                           static_cast<double>(kNavMapUniverseOffset));
            OutPos[1] = 0.0f;
            OutPos[0] = static_cast<float>(static_cast<double>(OutPos[0]) *
                                               static_cast<double>(kNavMapUniverseScale) -
                                           static_cast<double>(kNavMapUniverseOffset));
            OutPos[2] = static_cast<float>(static_cast<double>(OutPos[2]) +
                                           static_cast<double>(kNavMapUniverseLift));
            return;
        }
        return;
    }

    // 00491ccb..00491cea: the system view -- the player's last position.
    float const* const LastPos = GetPlayerLastPositionRetail();
    OutPos[0] = LastPos[0];
    OutPos[1] = LastPos[1];
    OutPos[2] = LastPos[2];
    *OutHeading = 0.0f;

    if (IsPlayerDockedRetail())
    {
        // 00491cf9..00491d49: docked -- the base's solar (FUN_00491e10). The base is read
        // through IBase slot 0 (its nickname id).
        Universe::IBase const* const Base = Universe::get_base(GetPlayerBaseIdRetail());
        if (Base != nullptr)
        {
            Universe::ISystem const* const System =
                Universe::get_system(GetPlayerStarSystemIdRetail());
            float Pos[3];
            float const* const Result = CachedBasePosition(Pos, Base->NicknameId, System);
            OutPos[0] = Result[0];
            OutPos[1] = Result[1];
            OutPos[2] = Result[2];
        }
    }
    else
    {
        // 00491d4e..00491de5: in space -- the heading from the orientation's third column
        // (m[0][2], m[2][2]) normalised, in degrees, wrapped into [0, 360).
        float const* const Orientation = GetPlayerLastOrientationRetail();
        double const A = static_cast<double>(Orientation[2]);
        double const B = static_cast<double>(Orientation[8]);
        double const InvLength = 1.0 / sqrt(B * B + A * A);
        float const X = static_cast<float>(A * InvLength);
        float const Y = static_cast<float>(B * InvLength);
        double Heading =
            -static_cast<double>(HudVectorAngle(X, Y)) * static_cast<double>(kNavMapRadToDeg);
        *OutHeading = static_cast<float>(Heading);
        // TEST AH,5 + JP: only "less than 0" enters the loop (the compare runs on the
        // unrounded value).
        if (Heading < 0.0)
        {
            do
            {
                Heading += static_cast<double>(kNavMapFullTurn);
            } while (Heading < 0.0);
            *OutHeading = static_cast<float>(Heading);
        }
        // TEST AH,1 + JNE: C0 clear (greater or equal, not unordered) enters the loop.
        double Wrapped = static_cast<double>(*OutHeading);
        if (Wrapped >= static_cast<double>(kNavMapFullTurn))
        {
            do
            {
                Wrapped -= static_cast<double>(kNavMapFullTurn);
            } while (Wrapped >= static_cast<double>(kNavMapFullTurn));
            *OutHeading = static_cast<float>(Wrapped);
        }
    }

    // 00491de9..00491e00: scaled by the system's NavMapScale.
    double const Scale = static_cast<double>(SystemScale);
    OutPos[0] = static_cast<float>(Scale * static_cast<double>(OutPos[0]));
    OutPos[1] = static_cast<float>(Scale * static_cast<double>(OutPos[1]));
    OutPos[2] = static_cast<float>(Scale * static_cast<double>(OutPos[2]));
}

// ----------------------------------------------------------------------------------------
// Item pulse
// ----------------------------------------------------------------------------------------

void NeuroNetNavMap::AnimateItems()
{
    // FUN_004a1710.
    float const Dt = static_cast<float>(GetClientFrameTimeCapped());

    // 004a1721..004a1797: the highlight timer. TEST AH,0x41 + JP skips the re-arm when the
    // timer is still greater than 0 (or unordered); <= 0 re-arms it.
    double const Remaining = static_cast<double>(PulseTimer) - static_cast<double>(Dt);
    PulseTimer = static_cast<float>(Remaining);
    if (Remaining <= 0.0)
    {
        int const Delay = rand() * 0x7d0 / 0x8000 + 0x64;
        int const ItemCount = static_cast<int>(Items.size());
        PulseTimer = static_cast<float>(static_cast<double>(static_cast<unsigned>(Delay)) *
                                            static_cast<double>(kNavMapMilli) +
                                        static_cast<double>(PulseTimer));
        PulseIndex = rand() * ItemCount * 3 / 0x8000;
    }

    int Index = 0;
    for (std::list<FNavMapItem*>::iterator It = Items.begin(); It != Items.end(); ++It, ++Index)
    {
        FNavMapItem* const Item = *It;
        if (Item->bShown == 0 || Item->Kind != 0)
            continue;

        // 004a17cd..004a17fd: the phase advances and wraps while it is above 1 (the compare
        // runs on the unrounded sum; only the final value is stored).
        double Phase = static_cast<double>(Dt) * static_cast<double>(Item->PulseRate) +
                       static_cast<double>(Item->PulsePhase);
        Item->PulsePhase = static_cast<float>(Phase);
        if (Phase > 1.0)
        {
            do
            {
                Phase -= 1.0;
            } while (Phase > 1.0);
            Item->PulsePhase = static_cast<float>(Phase);
        }

        // 004a1801..004a181f: a triangle wave, 0 -> 1 -> 0 over one phase (TEST AH,5 + JP:
        // only "less than 0.5" keeps the phase itself).
        double Tri;
        if (Item->PulsePhase < 0.5f)
            Tri = static_cast<double>(Item->PulsePhase);
        else
            Tri = 1.0 - static_cast<double>(Item->PulsePhase);
        double const Wave = Tri + Tri;

        Item->Size =
            static_cast<float>(kNavMapPulseSwing * Wave + static_cast<double>(Item->RestSize));

        // 004a182f..004a1870: alpha = min(level * 4, 255), 255 for the highlighted item;
        // the tint = level + 191 (truncated to a byte).
        int const Level = static_cast<int>(Wave * static_cast<double>(kNavMapPulseAlphaScale));
        int Alpha = Level * 4;
        if (Alpha > 0xff)
            Alpha = 0xff;
        int const Tint =
            static_cast<int>(static_cast<double>(Level) + static_cast<double>(kNavMapPulseBase));
        if (PulseIndex != Index)
            Alpha = 0xff;
        unsigned char const TintByte = static_cast<unsigned char>(Tint);
        unsigned char const AlphaByte = static_cast<unsigned char>(Alpha);

        float* const Colour = Item->Mesh.UserFloats;
        switch (Item->Variant)
        {
        case 0:
            // 004a1885: red full, green / blue the tint.
            Colour[0] = 1.0f;
            Colour[1] = NavMapByteToUnit(TintByte);
            Colour[2] = Colour[1];
            Colour[4] = NavMapByteToUnit(AlphaByte);
            break;
        case 1:
            // 004a18be: green full.
            Colour[1] = 1.0f;
            Colour[0] = NavMapByteToUnit(TintByte);
            Colour[2] = Colour[0];
            Colour[4] = NavMapByteToUnit(AlphaByte);
            break;
        case 2:
            // 004a18f7: blue full.
            Colour[2] = 1.0f;
            Colour[0] = NavMapByteToUnit(TintByte);
            Colour[1] = Colour[0];
            Colour[4] = NavMapByteToUnit(AlphaByte);
            break;
        case 3:
            // 004a192f: green and blue full.
            Colour[1] = 1.0f;
            Colour[2] = 1.0f;
            Colour[0] = NavMapByteToUnit(TintByte);
            Colour[4] = NavMapByteToUnit(AlphaByte);
            break;
        case 4:
        {
            // 004a1969: {0xff, tint, 0xff, alpha} packed (FUN_004a1ad0), then every byte
            // scaled back (so red / blue are 255 * (1/255), not a literal 1.0).
            unsigned char const Rgba[4] = {0xff, TintByte, 0xff, AlphaByte};
            Colour[0] = NavMapByteToUnit(Rgba[0]);
            Colour[1] = NavMapByteToUnit(Rgba[1]);
            Colour[2] = NavMapByteToUnit(Rgba[2]);
            Colour[4] = NavMapByteToUnit(Rgba[3]);
            break;
        }
        case 5:
        {
            // 004a19e0: {0xff, 0xff, tint, alpha} (FUN_004a1ad0).
            unsigned char const Rgba[4] = {0xff, 0xff, TintByte, AlphaByte};
            Colour[0] = NavMapByteToUnit(Rgba[0]);
            Colour[1] = NavMapByteToUnit(Rgba[1]);
            Colour[2] = NavMapByteToUnit(Rgba[2]);
            Colour[4] = NavMapByteToUnit(Rgba[3]);
            break;
        }
        case 6:
        {
            // 004a1a54: {tint, tint, tint, alpha} (FUN_004a1ab0) through FUN_004a1af0.
            unsigned char const Rgba[4] = {TintByte, TintByte, TintByte, AlphaByte};
            ClientMeshHolderSetColor(&Item->Mesh, Rgba);
            break;
        }
        default:
            // 004a1878: above 6 -- untouched.
            break;
        }
    }
}

// ----------------------------------------------------------------------------------------
// Group positions (IClient slot 96)
// ----------------------------------------------------------------------------------------

void NavMapSetGroupPositions(void const* Data)
{
    // FUN_0048d8a0 (cdecl). 0x0048d8a0..0x0048d8d3: erase(first, last) inline.
    G_NavMapGroupPositions.clear();

    int Count = 0;
    memcpy(&Count, Data, sizeof(Count));
    // 0x0048d8d9..0x0048d8e1: EBP = Data + 4 and it is never advanced inside the loop (the
    // only ADD EBP, 0x0048da9d, is on the grow path's scratch copy, and 0x0048dae8 reloads the
    // original). Retail therefore stores the FIRST record Count times -- kept as retail does.
    FNavMapGroupMember const* const Record = reinterpret_cast<FNavMapGroupMember const*>(
        static_cast<unsigned char const*>(Data) + sizeof(int));
    double const kNearOrigin = 128.0; // DAT_005d2ee0 (double)
    for (int Index = 0; Index < Count; ++Index)
    {
        FNavMapGroupMember Member;
        if (Record->BaseId != 0)
        {
            // 0x0048d8f0..0x0048d986: docked -- the base's position (FUN_0048d780). Each |c|
            // against 128.0 with TEST AH,5 + JP (taken when not less, or unordered): the member
            // is kept unless all three components lie within 128 of the origin.
            float Pos[3];
            NavMapBasePosition(Pos, Record->BaseId, Record->SystemId);
            if (!(fabs(static_cast<double>(Pos[0])) < kNearOrigin) ||
                !(fabs(static_cast<double>(Pos[1])) < kNearOrigin) ||
                !(fabs(static_cast<double>(Pos[2])) < kNearOrigin))
            {
                Member.Pos[0] = Pos[0];
                Member.Pos[1] = Pos[1];
                Member.Pos[2] = Pos[2];
                Member.BaseId = Record->BaseId;
                Member.SystemId = Record->SystemId;
                Member.RepId = Record->RepId;
                G_NavMapGroupPositions.push_back(Member); // FUN_004a3680(last, 1, &rec)
            }
            continue;
        }

        // 0x0048d9a3..0x0048dc07: in space -- the record's own position under the same 128.0
        // test, BaseId 0, pushed back (the vector growth inline).
        if (!(fabs(static_cast<double>(Record->Pos[0])) < kNearOrigin) ||
            !(fabs(static_cast<double>(Record->Pos[1])) < kNearOrigin) ||
            !(fabs(static_cast<double>(Record->Pos[2])) < kNearOrigin))
        {
            Member.Pos[0] = Record->Pos[0];
            Member.Pos[1] = Record->Pos[1];
            Member.Pos[2] = Record->Pos[2];
            Member.BaseId = 0;
            Member.SystemId = Record->SystemId;
            Member.RepId = Record->RepId;
            G_NavMapGroupPositions.push_back(Member);
        }
    }
}

int NeuroNetNavMap::GroupMemberAt(float const* MapPos)
{
    // FUN_004a21f0.
    if (SinglePlayer())
        return -1;
    int const Count = static_cast<int>(G_NavMapGroupPositions.size());

    // 004a2234..004a2273: the pick radius ShipMarkerSize / max(ZoomScale / 2, 1) * 1.05,
    // squared (TEST AH,5 + JP: only "less than 1" is raised to 1).
    double Half = static_cast<double>(ZoomScale) * 0.5;
    if (Half < 1.0)
        Half = 1.0;
    double const Radius =
        static_cast<double>(ShipMarkerSize) / Half * static_cast<double>(kNavMapPickMargin);
    float const Radius2 = static_cast<float>(Radius * Radius);

    for (int Index = 0; Index < Count; ++Index)
    {
        FNavMapGroupMember const& Member = G_NavMapGroupPositions[static_cast<size_t>(Index)];
        if (Member.SystemId != CurrentSystem->GetId())
            continue;
        double const Dx = static_cast<double>(MapPos[0]) - static_cast<double>(Member.Pos[0]);
        double const Dy = static_cast<double>(MapPos[1]) - static_cast<double>(Member.Pos[1]);
        double const Dz = static_cast<double>(MapPos[2]) - static_cast<double>(Member.Pos[2]);
        // TEST AH,0x41 + JNP: inside when less than or equal.
        if (Dx * Dx + Dz * Dz + Dy * Dy <= static_cast<double>(Radius2))
            return Member.RepId;
    }
    return -1;
}

// ----------------------------------------------------------------------------------------
// Picking
// ----------------------------------------------------------------------------------------

Universe::IZone const* NavMapZoneAt(Universe::ISystem const* System, float const* Pos,
                                    unsigned char bNamed, unsigned char bInfo,
                                    unsigned char bShowAll)
{
    // FUN_004a1b50 (cdecl).
    ISpatialPartition* const Tree = System->GetZoneTree();
    if (Tree == nullptr)
        return nullptr;

    // 004a1b76..004a1bb8: a 0.1 sphere at Pos into a 0x800-slot result.
    Geometry::Sphere Query;
    Query.Center.x = Pos[0];
    Query.Center.y = Pos[1];
    Query.Center.z = Pos[2];
    Query.Radius = 0.1f;
    FSpatialQueryResult Result; // 0x2008 bytes on the stack, as retail's __chkstk frame
    Result.Capacity = 0x800;
    Result.Count = 0;
    if (SpatialFill(Tree, &Query, &Result) == 0)
        return nullptr;

    Universe::IZone const* Best = nullptr;
    float BestRadius = 1.0e20f; // 0x60ad78ec
    // 004a1c72: retail hands the caller's float[3] straight in as the Vector.
    Vector Point;
    Point.x = Pos[0];
    Point.y = Pos[1];
    Point.z = Pos[2];
    for (unsigned Index = 0; Index < Result.Count; ++Index)
    {
        Universe::IZone const* const Zone =
            static_cast<Universe::IZone const*>(Result.Items[Index]);
        if (bNamed != 0 && Zone->GetName() == 0)
            continue;
        if (bInfo != 0 && Zone->GetIdsInfo() == 0)
            continue;
        if (bShowAll == 0)
        {
            // 004a1c2f..004a1c70: the visit map's flags | the zone's own visit byte.
            unsigned char VisitFlags = 0;
            GetPlayerVisitFlagsRetail(Zone->GetId(), &VisitFlags, 1, 0); // FUN_004c4da0 0x004a1c45
            unsigned char const Visit = static_cast<unsigned char>(VisitFlags | Zone->GetVisit());
            if ((Visit & 1) == 0)
                continue;
            if (bInfo != 0 && (Visit & 8) == 0)
                continue;
        }
        if (!Zone->InZoneXZ(Point, 0.0f))
            continue;
        // TEST AH,5 + JP: only a strictly smaller radius wins.
        float const Radius = Zone->GetRadius();
        if (Radius < BestRadius)
        {
            BestRadius = Radius;
            Best = Zone;
        }
    }
    return Best;
}

bool NeuroNetNavMap::MapPointUnderCursor(float* OutMapPos)
{
    // FUN_00492410. 00492413..00492480: the cursor inside ScreenRect (x kept in the FPU, y
    // stored); TEST AH,1 + JNE rejects a negative (or unordered) offset, TEST AH,5 + JP (taken
    // on greater, EQUAL or unordered) one at or past the size: the rect is [0, size).
    int MouseX = 0;
    int MouseY = 0;
    GetClientSoftwareCursor(&MouseX, &MouseY, nullptr); // DAT_00616840 / DAT_00616844
    float const ScreenX = static_cast<float>(MouseX);
    float const ScreenY = static_cast<float>(MouseY);
    double const Dx = static_cast<double>(MouseX) - static_cast<double>(ScreenRectMin[0]);
    float const Dy = static_cast<float>(static_cast<double>(MouseY) -
                                        static_cast<double>(ScreenRectMin[1]));
    if (!(Dx >= 0.0) || !(Dy >= 0.0f))
        return false;
    if (!(Dx < static_cast<double>(ScreenRectSize[0])) || !(Dy < ScreenRectSize[1]))
        return false;

    if (ViewMode == 1 || ViewMode == 0)
    {
        // 00492648..004926b5: the camera ray to the map plane, then the map-frame fractions to
        // map units.
        float Ray[3];
        AsCamera(&Camera)->Unproject(Ray, ScreenX, ScreenY);
        double const K = kNavMapPlaneDepth / static_cast<double>(Ray[2]);
        double const A = static_cast<double>(Ray[0]) * K;
        double const B = static_cast<double>(Ray[1]) * K;
        float const V = static_cast<float>(-(B / (static_cast<double>(MapSize[1]) * 0.5)));
        OutMapPos[0] = static_cast<float>(A / (static_cast<double>(MapSize[0]) * 0.5) *
                                          static_cast<double>(MapExtentX));
        OutMapPos[1] = 0.0f;
        OutMapPos[2] = static_cast<float>(static_cast<double>(V) *
                                          static_cast<double>(MapExtentZ));
        return true;
    }
    if (ViewMode != 2)
        return false;

    // 004924a6..00492645: zoomed in -- the ray's point as fractions of the zoomed frame, less
    // the pan, back to map units.
    float Ray[3];
    AsCamera(&Camera)->Unproject(Ray, ScreenX, ScreenY);
    double const K = kNavMapPlaneDepth / static_cast<double>(Ray[2]);
    double const A = static_cast<double>(Ray[0]) * K;
    float const B = static_cast<float>(static_cast<double>(Ray[1]) * K);
    double const U = A / (static_cast<double>(MapSize[0]) * 0.5) * 0.5 + 0.5;
    double const V = -1.0 / (static_cast<double>(MapSize[1]) * 0.5) * static_cast<double>(B) *
                         0.5 +
                     0.5;
    double const InvZoom = 1.0 / static_cast<double>(ZoomScale);
    float const ZoomedX = static_cast<float>(static_cast<double>(MapSize[0]) * InvZoom);
    float const ZoomedY = static_cast<float>(static_cast<double>(MapSize[1]) * InvZoom);
    float const FracX = static_cast<float>(U * static_cast<double>(ZoomedX));
    float const HalfZoomedY = static_cast<float>(static_cast<double>(ZoomedY) * 0.5);
    double const LocalX = static_cast<double>(FracX) - static_cast<double>(ZoomedX) * 0.5;
    float const LocalY = static_cast<float>(V * static_cast<double>(ZoomedY) -
                                            static_cast<double>(HalfZoomedY));
    float const NegPanY = -PanOffset[1];
    double const PannedX = LocalX - static_cast<double>(PanOffset[0]);
    double const PannedY = static_cast<double>(LocalY) - static_cast<double>(NegPanY);
    float const HalfY = static_cast<float>(static_cast<double>(MapSize[1]) * 0.5);
    float const SumX = static_cast<float>(static_cast<double>(MapSize[0]) * 0.5 + PannedX);
    double const SumY = static_cast<double>(HalfY) + PannedY;
    double const FracY = SumY / static_cast<double>(MapSize[1]);
    OutMapPos[0] = static_cast<float>(
        ((static_cast<double>(SumX) / static_cast<double>(MapSize[0])) * 2.0 - 1.0) *
        static_cast<double>(MapExtentX));
    OutMapPos[1] = 0.0f;
    OutMapPos[2] = static_cast<float>((FracY * 2.0 - 1.0) * static_cast<double>(MapExtentZ));
    return true;
}

bool NeuroNetNavMap::CursorRayPoint(float* OutPoint)
{
    // FUN_0049e890 -- the same ScreenRect test as FUN_00492410.
    int MouseX = 0;
    int MouseY = 0;
    GetClientSoftwareCursor(&MouseX, &MouseY, nullptr);
    float const ScreenX = static_cast<float>(MouseX);
    float const ScreenY = static_cast<float>(MouseY);
    double const Dx = static_cast<double>(MouseX) - static_cast<double>(ScreenRectMin[0]);
    float const Dy = static_cast<float>(static_cast<double>(MouseY) -
                                        static_cast<double>(ScreenRectMin[1]));
    if (!(Dx >= 0.0) || !(Dy >= 0.0f))
        return false;
    if (!(Dx < static_cast<double>(ScreenRectSize[0])) || !(Dy < ScreenRectSize[1]))
        return false;

    // 0049e906..0049e97f: the cursor on the Win3D camera's near plane, turned by the camera
    // (a copy of its orientation, FUN_00423b90).
    FSpaceCamera const* const Cam = Win3DGetCamera(); // DAT_0067dbf8
    FVector Local;
    Local.X = static_cast<float>((static_cast<double>(ScreenX) -
                                  static_cast<double>(Cam->ScreenCenterX)) /
                                 static_cast<double>(Cam->XScale) *
                                 static_cast<double>(Cam->ZNear));
    Local.Y = static_cast<float>((static_cast<double>(ScreenY) -
                                  static_cast<double>(Cam->ScreenCenterY)) /
                                 static_cast<double>(Cam->YScale) *
                                 static_cast<double>(Cam->ZNear));
    Local.Z = -Cam->ZNear;
    FMatrix Orientation;
    memcpy(&Orientation, Cam->World, sizeof(Orientation));
    FVector Turned;
    Win3DTransformByMatrix(Turned, Orientation, Local);

    // 0049e984..0049ea31: scaled onto this control's depth (Offset z + Position z), moved by
    // (0.0025, -0.001) and made relative to Offset + Position in x / y; z is the scaled ray's
    // own and then 0.
    double const K = (static_cast<double>(Offset[2]) + static_cast<double>(Position[2])) /
                     static_cast<double>(Turned.Z);
    float const X = static_cast<float>(static_cast<double>(Turned.X) * K);
    float const Y = static_cast<float>(static_cast<double>(Turned.Y) * K);
    double const Z = K * static_cast<double>(Turned.Z);
    double const OriginX = static_cast<double>(Position[0]) + static_cast<double>(Offset[0]);
    float const OriginY = static_cast<float>(static_cast<double>(Offset[1]) +
                                             static_cast<double>(Position[1]));
    OutPoint[0] = static_cast<float>(static_cast<double>(X) +
                                     static_cast<double>(kNavMapRayOffsetX));
    OutPoint[1] = static_cast<float>(static_cast<double>(Y) -
                                     static_cast<double>(kNavMapRayOffsetY));
    OutPoint[2] = static_cast<float>(Z);
    OutPoint[0] = static_cast<float>(static_cast<double>(OutPoint[0]) - OriginX);
    OutPoint[1] = static_cast<float>(static_cast<double>(OutPoint[1]) -
                                     static_cast<double>(OriginY));
    OutPoint[2] = 0.0f;
    return true;
}

FNavMapItem* NeuroNetNavMap::ItemAt(float const* MapPos, unsigned char bSkipWaypoints)
{
    // FUN_00496d40.
    for (std::list<FNavMapItem*>::iterator It = Items.begin(); It != Items.end(); ++It)
    {
        FNavMapItem* const Item = *It;
        if (bSkipWaypoints != 0 && (Item->Kind == 3 || Item->Kind == 4))
            continue;
        if (Item->bShown == 0)
            continue;

        // 00496d89..00496dc5: the distance in the XZ plane, squared.
        double const Dx = static_cast<double>(MapPos[0]) - static_cast<double>(Item->Position[0]);
        double const Dz = static_cast<double>(MapPos[2]) - static_cast<double>(Item->Position[2]);
        double const Dist2 = Dz * Dz + Dx * Dx;

        // 00496dc2..00496e10: the pick radius -- a universe-view system's is 1.5 x; the markers
        // (Kind 2 / 3 / 4) shrink with the zoom (over max(ZoomScale / 2, 1)), the rest follow
        // SystemScale; then the 1.05 margin.
        double Radius = static_cast<double>(Item->Size);
        if ((Item->Type & 4) != 0)
            Radius *= static_cast<double>(kNavMapSystemPickScale);
        if (Item->Kind == 2 || Item->Kind == 3 || Item->Kind == 4)
        {
            double Half = static_cast<double>(ZoomScale) * 0.5;
            if (Half < 1.0)
                Half = 1.0;
            Radius /= Half;
        }
        else
        {
            Radius *= static_cast<double>(SystemScale);
        }
        Radius *= static_cast<double>(kNavMapPickMargin);

        // TEST AH,0x41 + JNP: inside when less than or equal.
        if (Dist2 <= Radius * Radius)
            return Item;
    }
    return nullptr;
}

// ----------------------------------------------------------------------------------------
// Waypoint markers
// ----------------------------------------------------------------------------------------

bool NeuroNetNavMap::ResolveBestPathWaypoint(int Index, float* OutPos, unsigned* OutSystem)
{
    // FUN_004a0800. 004a0813..004a0866: the cached answer while the waypoint's hash matches.
    unsigned const Hash = BestPathWaypointHash(Index); // FUN_004c5530
    if (Hash == CachedWaypointHash)
    {
        OutPos[0] = CachedWaypointPos[0];
        OutPos[1] = CachedWaypointPos[1];
        OutPos[2] = CachedWaypointPos[2];
        *OutSystem = CachedWaypointSystem;
        return true;
    }

    // 004a0869..004a0880: the record is used without a null test, as in retail.
    FmtStr::Val const* Token = BestPathGetWaypoint(Index)->Marker.Head; // FUN_004c4ff0
    unsigned char bSystemKnown = 0;
    for (; Token != nullptr; Token = Token->Next)
    {
        unsigned short const Tag = Token->TypeTag;
        if (Tag == 0x53)
        {
            // 004a08b0: 'S' -- the star system.
            *OutSystem = Token->Value;
            bSystemKnown = 1;
            continue;
        }
        if (Tag == 0x4e)
        {
            // 004a099a..004a09f8: 'N' -- a nav marker: {system, ?, position}.
            unsigned const* const Marker = Token->Marker->Data;
            *OutSystem = Marker[0];
            float const* const MarkerPos = reinterpret_cast<float const*>(Marker + 2);
            OutPos[0] = MarkerPos[0];
            OutPos[1] = MarkerPos[1];
            OutPos[2] = MarkerPos[2];
            CachedWaypointPos[0] = MarkerPos[0];
            CachedWaypointPos[1] = MarkerPos[1];
            CachedWaypointPos[2] = MarkerPos[2];
            CachedWaypointSystem = *OutSystem;
            CachedWaypointHash = Hash;
            return true;
        }
        if (Tag != 0x42)
            continue;

        // 004a08c6..004a0973: 'B' -- the base: the system object docking with it, searched in
        // the 'S' system (when one came first) and then, as retail's iterator runs on, in every
        // system after the first.
        unsigned const BaseId = Token->Value;
        Universe::ISystem const* System = Universe::GetFirstSystem();
        if (bSystemKnown != 0)
            System = Universe::get_system(*OutSystem);
        while (System != nullptr)
        {
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
            for (unsigned Slot = 0; Slot < Count; ++Slot)
            {
                if (!System->GetSystemObject(Slot, &Object))
                    continue;
                if (Object.DockWith != BaseId)
                    continue;
                // 004a09fb..004a0a5f
                OutPos[0] = Object.Pos.x;
                OutPos[1] = Object.Pos.y;
                OutPos[2] = Object.Pos.z;
                *OutSystem = System->GetId();
                CachedWaypointPos[0] = OutPos[0];
                CachedWaypointPos[1] = OutPos[1];
                CachedWaypointPos[2] = OutPos[2];
                CachedWaypointSystem = *OutSystem;
                CachedWaypointHash = Hash;
                return true;
            }
            System = Universe::GetNextSystem();
        }
    }
    return false;
}

bool NeuroNetNavMap::RefreshWaypointItems()
{
    // FUN_00496e50.
    unsigned char bMoved = 0;

    // 00496e5e..0049701a: one marker per player waypoint (created on first use).
    int const WaypointCount = GetPlayerWaypointCountRetail(); // FUN_004c4690
    for (int Index = 0; Index < WaypointCount; ++Index)
    {
        if (WaypointItems[Index] == nullptr)
        {
            // 00496e80..00496f4f: no null test of the new item, as in retail.
            FNavMapItem* const Item =
                AddItem(3, 0, 0.0f, 0.0f, 0.0f,
                        static_cast<float>(static_cast<double>(WaypointMarkerSize) +
                                           static_cast<double>(WaypointMarkerSize)),
                        "NAV_waypointdiamond", 0x219b, 0, 0.0f, 0, 0);
            WaypointItems[Index] = Item;
            Item->Variant = Index + 1;
            Item->Ordinal = Index + 1;
            Item->bShown = 1;
            NavMapTintFromPalette(Item, G_NnPaletteArgb_679b80);
        }

        FNavMapItem* const Item = WaypointItems[Index];
        FPlayerWaypointRecord const* const Record = GetPlayerWaypointRetail(Index); // FUN_004c46a0
        // 00496f65..00496f72: the record's +0x0c is compared with the current system's id.
        if (Record == nullptr || Record->SystemId != CurrentSystem->GetId())
        {
            Item->bShown = 0;
            continue;
        }

        // 00496f78..00497008: the first and the last waypoint show their id; the markers
        // between show none.
        int Number = Record->AutoId;
        if (Index > 0 && Index < WaypointCount - 1)
            Number = 0;
        Item->bShown = 1;
        Item->Ordinal = Record->AutoId;
        double const Scale = static_cast<double>(SystemScale);
        float const Pos[3] = {static_cast<float>(Scale * static_cast<double>(Record->Pos.x)),
                              static_cast<float>(Scale * static_cast<double>(Record->Pos.y)),
                              static_cast<float>(Scale * static_cast<double>(Record->Pos.z))};
        // TEST AH,0x44 + JP: any component not equal (or unordered) moves the marker.
        if (!(Item->Position[0] == Pos[0]) || !(Item->Position[1] == Pos[1]) ||
            !(Item->Position[2] == Pos[2]) || Item->Variant != Number)
        {
            Item->Position[0] = Pos[0];
            Item->Position[1] = Pos[1];
            Item->Position[2] = Pos[2];
            Item->Variant = Number;
            bMoved = 1;
        }
    }
    for (int Index = WaypointCount; Index < 0x63; ++Index)
    {
        if (WaypointItems[Index] != nullptr)
            WaypointItems[Index]->bShown = 0;
    }

    // 00497043..0049724f: one marker per best-path waypoint.
    unsigned char bPathChanged = 0;
    int const PathCount = BestPathGetWaypointCount(); // FUN_004c4fb0
    int NextNumber = 1;
    for (int Index = 0; Index < PathCount; ++Index)
    {
        if (BestPathItems[Index] == nullptr)
        {
            FNavMapItem* const Item =
                AddItem(4, 0, 0.0f, 0.0f, 0.0f,
                        static_cast<float>(static_cast<double>(WaypointMarkerSize) +
                                           static_cast<double>(WaypointMarkerSize)),
                        "NAV_waypointdiamond", 0x219b, 0, 0.0f, 0, 0);
            BestPathItems[Index] = Item;
            Item->bShown = 0;
            bPathChanged = 1;
            NavMapTintFromPalette(Item, G_NnPaletteArgb_679b80);
        }

        FNavMapItem* const Item = BestPathItems[Index];
        // 00497143..00497180: the record's kind (low nibble of +0x18): 0xa numbered stops,
        // 9 no number, anything else -1. No null test of the record, as in retail.
        FBestPathWaypoint* const Waypoint = BestPathGetWaypoint(Index); // FUN_004c4ff0
        unsigned const RecordKind = Waypoint->Field_0x18 & 0xf;
        if (RecordKind == 0xa)
            Item->Variant = NextNumber++;
        else if (RecordKind == 9)
            Item->Variant = 0;
        else
            Item->Variant = -1;

        // 00497180..004971a9: the record's own position (FUN_004c52d0), else FUN_004a0800.
        Vector Pos;
        unsigned SystemId = 0;
        bool bResolved = BestPathWaypointResolvePosition(Waypoint, &Pos, &SystemId);
        if (!bResolved)
        {
            float Resolved[3];
            bResolved = ResolveBestPathWaypoint(Index, Resolved, &SystemId);
            Pos.x = Resolved[0];
            Pos.y = Resolved[1];
            Pos.z = Resolved[2];
        }
        if (!bResolved)
        {
            Item->bShown = 0;
            continue;
        }
        if (SystemId != CurrentSystem->GetId())
        {
            Item->bShown = 0;
            continue;
        }

        // 004971c1..00497237
        Item->bShown = 1;
        double const Scale = static_cast<double>(SystemScale);
        float const Scaled[3] = {static_cast<float>(static_cast<double>(Pos.x) * Scale),
                                 static_cast<float>(static_cast<double>(Pos.y) * Scale),
                                 static_cast<float>(static_cast<double>(Pos.z) * Scale)};
        if (!(Item->Position[0] == Scaled[0]) || !(Item->Position[1] == Scaled[1]) ||
            !(Item->Position[2] == Scaled[2]))
        {
            Item->Position[0] = Scaled[0];
            Item->Position[1] = Scaled[1];
            Item->Position[2] = Scaled[2];
            bMoved = 1;
            bPathChanged = 1;
        }
    }
    for (int Index = PathCount; Index < 0x63; ++Index)
    {
        if (BestPathItems[Index] != nullptr)
            BestPathItems[Index]->bShown = 0;
    }

    if (bPathChanged != 0)
        RefreshMissionZones(); // FUN_004a0580

    // 00497287..004972d6: the objectives follow the waypoints while there are some and no best
    // path with a text, else the best path.
    if (GetPlayerWaypointCountRetail() == 0 || !BestPathHasText())
    {
        if (GetPlayerWaypointCountRetail() != 0 && bBestPathObjectives != 0)
        {
            bBestPathObjectives = 0;
            RefreshObjectives(); // FUN_0049f7a0
        }
        else if (BestPathHasText() && bBestPathObjectives == 0)
        {
            bBestPathObjectives = 1;
            RefreshObjectives();
        }
    }

    // 004972d6..004972e8
    if (G_bNavMapRouteChanged != 0)
    {
        RefreshObjectives();
        RefreshMissionInfo(); // FUN_0049a580
    }

    // 004972ed..0049734e: the clear-all button with the waypoint count.
    if (GetPlayerWaypointCountRetail() != LastWaypointCount)
    {
        LastWaypointCount = GetPlayerWaypointCountRetail();
        unsigned const bHide =
            (GetPlayerWaypointCountRetail() == 0 || bBestPathObjectives != 0) ? 1u : 0u;
        ClearAllWaypointsButton->SetProperty(1, bHide, 0);
        ClearAllWaypointsText->SetProperty(1, bHide, 0);
        RefreshObjectives();
        BuildJumpLinks(); // FUN_004975a0
    }
    return bMoved != 0;
}

// ----------------------------------------------------------------------------------------
// Item placement
// ----------------------------------------------------------------------------------------

void NeuroNetNavMap::PlaceItems()
{
    // FUN_00496600. 00496609..00496797: the zoomed frame -- its size (MapSize * ZoomScale)
    // and its corner (the pan scaled, less the half size scaled), with retail's float stores.
    double const Zoom = static_cast<double>(ZoomScale);
    float const ZoomLess1 = static_cast<float>(Zoom - 1.0);
    float const NegHalfX = static_cast<float>(-(static_cast<double>(MapSize[0]) * 0.5));
    float const NegHalfY = static_cast<float>(-(static_cast<double>(MapSize[1]) * 0.5));
    float const HalfZ = static_cast<float>(static_cast<double>(MapSize[2]) * 0.5);
    float const NegHalfZ = -HalfZ;
    float const GrowX = static_cast<float>(static_cast<double>(MapSize[0]) * 0.5 *
                                           static_cast<double>(ZoomLess1));
    double const GrowY = static_cast<double>(MapSize[1]) * 0.5 * static_cast<double>(ZoomLess1);
    double const GrowZ = static_cast<double>(HalfZ) * static_cast<double>(ZoomLess1);
    float const CornerX0 = static_cast<float>(static_cast<double>(NegHalfX) -
                                              static_cast<double>(GrowX));
    float const CornerY0 = static_cast<float>(static_cast<double>(NegHalfY) - GrowY);
    double const CornerZ0 = static_cast<double>(NegHalfZ) - GrowZ;
    float const PanY = static_cast<float>(static_cast<double>(PanOffset[1]) * Zoom);
    float const PanZ = static_cast<float>(static_cast<double>(PanOffset[2]) * Zoom);
    float const OriginX = static_cast<float>(static_cast<double>(PanOffset[0]) * Zoom +
                                             static_cast<double>(CornerX0));
    float const OriginY = static_cast<float>(static_cast<double>(PanY) +
                                             static_cast<double>(CornerY0));
    float const OriginZ = static_cast<float>(static_cast<double>(PanZ) + CornerZ0);
    float const FrameX = static_cast<float>(static_cast<double>(MapSize[0]) * Zoom);
    float const FrameY = static_cast<float>(static_cast<double>(MapSize[1]) * Zoom);
    float const FrameZ = static_cast<float>(static_cast<double>(MapSize[2]) * Zoom);

    IEngine* const Engine = DALib::Engine;
    for (std::list<FNavMapItem*>::iterator It = Items.begin(); It != Items.end(); ++It)
    {
        FNavMapItem* const Item = *It;
        int const Kind = Item->Kind;
        int const Type = Item->Type;
        // 004967b5..004967c2: stars and objects only with a type the mask lets through.
        if ((Kind == 0 || Kind == 1) && (SolarTypeMask & static_cast<unsigned>(Type)) == 0)
            continue;

        if (Kind == 2)
        {
            // 004967d1..00496872: the player marker pinned to the map's edge, pointing out.
            // TEST AH,0x41 + JNE (greater) and TEST AH,5 + JP (less).
            unsigned char const bWasClamped = bItemClamped;
            bItemClamped = 0;
            if (Item->Position[0] > kNavMapEdge)
            {
                Item->Position[0] = kNavMapEdge;
                bItemClamped = 1;
                Item->Heading = 0.0f;
            }
            if (Item->Position[0] < -kNavMapEdge)
            {
                Item->Position[0] = -kNavMapEdge;
                bItemClamped = 1;
                Item->Heading = 180.0f;
            }
            if (Item->Position[2] > kNavMapEdge)
            {
                Item->Position[2] = kNavMapEdge;
                bItemClamped = 1;
                Item->Heading = 270.0f;
            }
            if (Item->Position[2] < -kNavMapEdge)
            {
                Item->Position[2] = -kNavMapEdge;
                bItemClamped = 1;
                Item->Heading = 90.0f;
            }
            // 00496872..00496a0b: the mesh follows the change -- the off-screen arrow, or the
            // position marker back.
            if (bWasClamped != bItemClamped)
            {
                if (bItemClamped != 0)
                    NavMapReloadItemMesh(Item, "NAV_offscreenarrow");
                else
                    NavMapReloadItemMesh(Item, "NNM_SM_INFO_POSITION");
            }
        }
        else
        {
            // 00496a0d..00496a6f: the rest clamped to the extents.
            if (Item->Position[0] > MapExtentX)
                Item->Position[0] = MapExtentX;
            if (-MapExtentX > Item->Position[0])
                Item->Position[0] = -MapExtentX;
            if (Item->Position[2] > MapExtentZ)
                Item->Position[2] = MapExtentZ;
            if (-MapExtentZ > Item->Position[2])
                Item->Position[2] = -MapExtentZ;
        }

        // 00496a71..00496b04: the item's place in the zoomed frame (z at the map plane).
        float const TwiceExtZ = static_cast<float>(static_cast<double>(MapExtentZ) +
                                                   static_cast<double>(MapExtentZ));
        double const U = (static_cast<double>(Item->Position[0]) + static_cast<double>(MapExtentX)) /
                         (static_cast<double>(MapExtentX) + static_cast<double>(MapExtentX));
        double const V = (static_cast<double>(MapExtentZ) + static_cast<double>(Item->Position[2])) /
                         static_cast<double>(TwiceExtZ);
        float Place[3];
        Place[0] = static_cast<float>(static_cast<double>(OriginX) + U * static_cast<double>(FrameX));
        Place[1] = static_cast<float>(static_cast<double>(OriginY) +
                                      (1.0 - V) * static_cast<double>(FrameY));
        Place[2] = static_cast<float>(static_cast<double>(OriginZ) + static_cast<double>(FrameZ));

        // 00496b01..00496bc2: the size -- the markers (Kind 2 / 3 / 4) shrink with the zoom
        // (during a zoom from the start distance to the end one), the rest follow SystemScale.
        double Size = static_cast<double>(Item->Size);
        if (Kind == 2 || Kind == 3 || Kind == 4)
        {
            if (bTransition != 0 && bViewSwitch == 0)
            {
                double HalfFrom = static_cast<double>(MapExtentX) /
                                  static_cast<double>(ZoomDistance) * 0.5;
                if (HalfFrom < 1.0)
                    HalfFrom = 1.0;
                double const SizeFrom = Size / HalfFrom;
                double HalfTo = static_cast<double>(MapExtentX) /
                                (static_cast<double>(TransitionDelta[2]) +
                                 static_cast<double>(ZoomDistance)) *
                                0.5;
                if (HalfTo < 1.0)
                    HalfTo = 1.0;
                Size = (Size / HalfTo - SizeFrom) * static_cast<double>(TransitionEase) + SizeFrom;
            }
            else
            {
                double Half = Zoom * 0.5;
                if (Half < 1.0)
                    Half = 1.0;
                Size = Size / Half;
            }
        }
        else
        {
            Size *= static_cast<double>(SystemScale);
        }
        Size = Size / static_cast<double>(TwiceExtZ) * static_cast<double>(FrameX);
        if ((Type & 4) != 0)
            Size *= static_cast<double>(kNavMapSystemMeshScale);
        float const MeshScale = static_cast<float>(Size * static_cast<double>(kNavMapMeshScale));
        Place[2] = kNavMapItemDepth; // overwrites the frame z computed above, as retail does

        // 00496be0..00496cd2: the rotation about z by Heading (cos of the unrounded angle, sin
        // of the float one), scaled through I3DMathEngine::Scale, written back; then the
        // position and UpdateInstance.
        double const Radians = static_cast<double>(Item->Heading) *
                               static_cast<double>(kNavMapDegToRad);
        float const RadiansF = static_cast<float>(Radians);
        FTransform Transform;
        memcpy(&Transform, Engine->GetInstanceTransform(Item->Mesh.EngineInstance),
               sizeof(Transform));
        double const Cos = cos(Radians);
        double const Sin = sin(static_cast<double>(RadiansF));
        Transform.Rotation.D[0][2] = 0.0f;
        Transform.Rotation.D[1][2] = 0.0f;
        Transform.Rotation.D[2][0] = 0.0f;
        Transform.Rotation.D[2][1] = 0.0f;
        Transform.Rotation.D[2][2] = 1.0f;
        Transform.Rotation.D[0][0] = static_cast<float>(Cos);
        Transform.Rotation.D[0][1] = static_cast<float>(-Sin);
        Transform.Rotation.D[1][0] = static_cast<float>(Sin);
        Transform.Rotation.D[1][1] = static_cast<float>(Cos);
        if (G_NavMapMath == nullptr)
            G_NavMapMath = Acquire3DMathEngine(); // FUN_005b73e0
        G_NavMapMath->Scale(Transform.Rotation, Transform.Rotation, MeshScale);
        Engine->SetInstanceMatrix(Item->Mesh.EngineInstance, &Transform);
        Engine->SetInstancePosition(Item->Mesh.EngineInstance, Place);
        Engine->UpdateInstance(Item->Mesh.EngineInstance, 0, nullptr);

        // 00496cd5..00496d0e: an object with an id takes the visit map's flags; bit 0 shows it
        // (or Field_0x87e every one).
        if (Item->Id != 0)
        {
            unsigned char VisitFlags = 0;
            GetPlayerVisitFlagsRetail(Item->Id, &VisitFlags, 0, 0); // FUN_004c4da0 0x00496ceb
            Item->Visit = static_cast<unsigned char>(Item->Visit | VisitFlags);
            Item->bShown = static_cast<unsigned char>(Item->Visit & 1);
            if (Field_0x87e != 0)
                Item->bShown = 1;
        }
    }
}

// ----------------------------------------------------------------------------------------
// Update (slot 11)
// ----------------------------------------------------------------------------------------

namespace
{
    // Update's text colour writes (0x00495cfd..0x00495dc9 and its copies): DAT_00679bb0 with
    // Alpha as its top byte, property 0x33 with bFlag, on the system name and the four faction
    // labels, then -- from a second copy of the same colour -- on the sixteen grid labels.
    void NavMapTintTexts(NeuroNetNavMap* Map, unsigned char Alpha, uintptr_t bFlag)
    {
        unsigned long Colour = G_NnPaletteArgb_679bb0;
        reinterpret_cast<unsigned char*>(&Colour)[3] = Alpha;
        Map->SystemNameText->SetProperty(0x33, reinterpret_cast<uintptr_t>(&Colour), bFlag);
        for (int Index = 0; Index < 4; ++Index)
            Map->FactionLabels[Index]->SetProperty(0x33, reinterpret_cast<uintptr_t>(&Colour),
                                                   bFlag);
        unsigned long GridColour = G_NnPaletteArgb_679bb0;
        reinterpret_cast<unsigned char*>(&GridColour)[3] = Alpha;
        for (int Index = 0; Index < 8; ++Index)
            Map->GridLetters[Index]->SetProperty(0x33, reinterpret_cast<uintptr_t>(&GridColour),
                                                 bFlag);
        for (int Index = 0; Index < 8; ++Index)
            Map->GridNumbers[Index]->SetProperty(0x33, reinterpret_cast<uintptr_t>(&GridColour),
                                                 bFlag);
    }

    // DAT_00679bb0's own alpha byte (the colour copied unchanged at the end of a switch).
    unsigned char NavMapPaletteAlpha()
    {
        return reinterpret_cast<unsigned char const*>(&G_NnPaletteArgb_679bb0)[3];
    }

    // The fade's alpha (0x00496075..0x004960a8): ftol((1 - Ease) * the palette's alpha byte).
    unsigned char NavMapFadeAlpha(float Ease)
    {
        int const PaletteAlpha = NavMapPaletteAlpha();
        return static_cast<unsigned char>(
            static_cast<int>((1.0 - static_cast<double>(Ease)) * static_cast<double>(PaletteAlpha)));
    }
}

void NeuroNetNavMap::Update()
{
    // LAB_00494920. Retail keeps the hover record(s) below in an SEH frame (0x005bca00); the
    // port tears them down explicitly on its only (non-throwing) path.
    Win3D::Update();       // FUN_005a17b0
    ButtonMover::Update(); // FUN_0055e170 (+0x330)
    SyncSidePanels();      // FUN_004a1620

    // 00494958..004949a8: the ship button shows unless the player's own system is open (not
    // the universe) or a transition runs; SetProperty(1, hide) only on a change of the visible
    // bit.
    unsigned const ShownSystem = CurrentSystem->GetId();
    unsigned char bShipButton = 1;
    if (ShownSystem == GetPlayerStarSystemIdRetail() && ViewMode != 0)
        bShipButton = 0;
    if (bTransition != 0)
        bShipButton = 0;
    if (bShipButton != static_cast<unsigned char>((ShipButton->Flags >> 1) & 1))
        ShipButton->SetProperty(1, bShipButton == 0 ? 1u : 0u, 0);

    // 004949ae..00494a17: the object tools close once the cursor moved away from where they
    // opened (squared distance over 0.005, TEST AH,0x41 + JNE: only "greater" closes).
    if (bObjectToolsShown != 0)
    {
        float Ray[3];
        if (CursorRayPoint(Ray))
        {
            float const Dx = static_cast<float>(static_cast<double>(Ray[0]) -
                                                static_cast<double>(ClickRayPos[0]));
            float const Dy = static_cast<float>(static_cast<double>(Ray[1]) -
                                                static_cast<double>(ClickRayPos[1]));
            Ray[2] = static_cast<float>(static_cast<double>(Ray[2]) -
                                        static_cast<double>(ClickRayPos[2]));
            if (static_cast<double>(Dy) * static_cast<double>(Dy) +
                    static_cast<double>(Dx) * static_cast<double>(Dx) >
                static_cast<double>(kNavMapToolsCloseDist2))
            {
                bObjectToolsShown = 0;
                HideObjectTools(); // FUN_0049f0e0
            }
        }
    }

    // 00494a1c..00494ae7: the map frame's corners from MapOrigin / MapSize.
    MapCorner0[0] = MapOrigin[0];
    MapCorner0[1] = MapOrigin[1];
    MapCorner0[2] = MapOrigin[2];
    MapCorner1[0] = MapOrigin[0];
    MapCorner1[1] = MapOrigin[1];
    MapCorner1[2] = MapOrigin[2];
    MapCorner1[0] = static_cast<float>(static_cast<double>(MapSize[0]) +
                                       static_cast<double>(MapCorner1[0]));
    MapCorner2[0] = MapOrigin[0];
    MapCorner2[1] = MapOrigin[1];
    MapCorner2[2] = MapOrigin[2];
    {
        // FUN_00422b80: the Vector built from the three rounded sums, then copied in.
        float const X = static_cast<float>(static_cast<double>(MapCorner2[0]) +
                                           static_cast<double>(MapSize[0]));
        float const Y = static_cast<float>(static_cast<double>(MapCorner2[1]) +
                                           static_cast<double>(MapSize[1]));
        float const Z = static_cast<float>(static_cast<double>(MapCorner2[2]) +
                                           static_cast<double>(MapSize[2]));
        MapCorner2[0] = X;
        MapCorner2[1] = Y;
        MapCorner2[2] = Z;
    }
    MapCorner3[0] = MapOrigin[0];
    MapCorner3[1] = MapOrigin[1];
    MapCorner3[2] = MapOrigin[2];
    MapCorner3[1] = static_cast<float>(static_cast<double>(MapSize[1]) +
                                       static_cast<double>(MapCorner3[1]));

    // 00494ad8..00494b90: the first frame after Draw builds the system view; a requested focus
    // point (FUN_004a2aa0) is scaled into the map and zoomed on.
    if (bSystemViewPending != 0 && bDrawnOnce != 0)
    {
        bSystemViewPending = 0;
        BuildSystemItems(); // FUN_004917c0
        AddPlayerMarker();  // FUN_00491410
        PlaceItems();       // FUN_00496600
        if (G_bNavMapRequestedPos != 0)
        {
            G_bNavMapRequestedPos = 0;
            double const Scale = static_cast<double>(SystemScale);
            G_NavMapRequestedPos[0] =
                static_cast<float>(static_cast<double>(G_NavMapRequestedPos[0]) * Scale);
            G_NavMapRequestedPos[1] =
                static_cast<float>(static_cast<double>(G_NavMapRequestedPos[1]) * Scale);
            G_NavMapRequestedPos[2] =
                static_cast<float>(Scale * static_cast<double>(G_NavMapRequestedPos[2]));
            ClickU = static_cast<float>(
                (static_cast<double>(G_NavMapRequestedPos[0]) + static_cast<double>(MapExtentX)) /
                (static_cast<double>(MapExtentX) + static_cast<double>(MapExtentX)));
            ClickV = static_cast<float>(
                (static_cast<double>(G_NavMapRequestedPos[2]) + static_cast<double>(MapExtentZ)) /
                (static_cast<double>(MapExtentZ) + static_cast<double>(MapExtentZ)));
            ZoomToClick(); // FUN_0049f1a0
        }
    }

    // 00494b95..00494cf0: a pending system (set by MouseEvent / SetProperty) once no transition
    // runs.
    if (bTransition == 0 && PendingSystem != nullptr)
    {
        Universe::ISystem const* const Pending = PendingSystem;
        if (Pending == CurrentSystem && (ViewMode == 2 || ViewMode == 1))
        {
            // 00494bc7: it is open -- done.
            PendingSystem = nullptr;
        }
        else if (ViewMode != 1 && ViewMode != 2)
        {
            // 00494be2..00494c42: the universe view zooms into its star first (sound 8); no
            // star, no switch.
            for (std::list<FNavMapItem*>::iterator It = Items.begin(); It != Items.end(); ++It)
            {
                FNavMapItem* const Item = *It;
                if (Item->System != Pending)
                    continue;
                CurrentSystem = Item->System;
                bViewSwitch = 1;
                bTransition = 1;
                TransitionView = 1;
                TransitionTime = 0.0f;
                TransitionDuration = kNavMapSwitchSeconds;
                PlayOrRewindUiSoundSlot(8); // FUN_005645c0(8)
                break;
            }
        }
        else
        {
            // 00494c47..00494cf0: from a system straight to the other one.
            SetFilterMode(0); // FUN_004a0220
            CurrentSystem = PendingSystem;
            NavMapShowSystemName(SystemNameText, CurrentSystem, SkipSystemIds);
            ZoomScale = 1.0f;
            bViewSwitch = 0;
            bTransition = 0;
            EnterSystemView(); // FUN_0049e1d0
            TransitionView = ViewMode;
        }
    }

    unsigned char bRebuild = 0;
    float const FrameTime = static_cast<float>(GetClientFrameTimeCapped()); // FUN_0042d680

    // 00494d06..004957e1: the hover tip -- skipped when the ToolTip was fed this frame or the
    // info card / base list / known-objects filter mode (4 / 6 / 7) is up.
    if (!NnToolTipWasFedThisFrame() && G_NavMapFilterMode != 4 && G_NavMapFilterMode != 6 &&
        G_NavMapFilterMode != 7)
    {
        float MapPos[3];
        if (MapPointUnderCursor(MapPos))
        {
            FNavMapItem* const Item = ItemAt(MapPos, 0);
            if (Item != nullptr)
            {
                FToolTipHoverInfo Record;
                ToolTipHoverInit(&Record); // FUN_004144b0 + FUN_00413df0(0, -1), -1 / 0 / 0
                if (Item->Kind == 4 && Item->Field_0x5c.c_str()[0] != L'\0')
                {
                    // 00494d5e..00494ff5: a best-path marker with its own text (wcscmp against
                    // L"" at DAT_005c8ce0) shows that text, always fed.
                    ToolTipHoverSetCaption(&Record, Item->Field_0x5c.c_str());
                    NavMapPlaceHoverAtCursor(&Record);
                    NavMapSubmitHover(&Record);
                }
                else
                {
                    // 00494ffa..004951c1: the item's name, keyed by the item; fed only with a
                    // name.
                    Record.IdsAlt = Item->StridName;
                    Record.Ids = static_cast<int>(reinterpret_cast<intptr_t>(Item));
                    NavMapPlaceHoverAtCursor(&Record);
                    if (Record.IdsAlt != 0)
                        NavMapSubmitHover(&Record);
                }
                // The record's dtor: ~FToolTipHoverInfo.
            }
            else if (ViewMode != 0)
            {
                int Rep = GroupMemberAt(MapPos); // FUN_004a21f0
                if (Rep != -1)
                {
                    // 004951ed..00495587: a group member -- named (Reputation::Vibe::GetName's
                    // display name) unless it has an affiliation.
                    FToolTipHoverInfo Record;
                    ToolTipHoverInit(&Record);
                    // Only read when GetAffiliation returns 0 (it then wrote it).
                    unsigned Affiliation = 0;
                    if (Reputation::Vibe::GetAffiliation(Rep, Affiliation, false) != 0 ||
                        Affiliation == 0xffffffffu)
                    {
                        FmtStr Name1; // [esp+0xd4]
                        FmtStr Name2; // [esp+0xbc]
                        wchar_t const* Display = nullptr;
                        if (Reputation::Vibe::GetName(Rep, Name1, Name2, Display) == 0)
                        {
                            if (Display != nullptr)
                            {
                                ToolTipHoverSetCaption(&Record, Display);
                            }
                            else if (Record.CaptionW != nullptr && Record.CaptionW[0] != L'\0')
                            {
                                Record.CaptionW[0] = L'\0'; // FUN_00414280
                            }
                            Record.Ids = Rep + 1;
                            NavMapPlaceHoverAtCursor(&Record);
                            NavMapSubmitHover(&Record);
                        }
                    }
                    // The record's dtor: ~FToolTipHoverInfo.
                }
                else
                {
                    // 0049558c..004957b1: the smallest named, visited zone under the cursor
                    // (map units back to system units).
                    float const InvScale =
                        static_cast<float>(1.0 / static_cast<double>(SystemScale));
                    float Pos[3];
                    Pos[0] = static_cast<float>(static_cast<double>(MapPos[0]) *
                                                static_cast<double>(InvScale));
                    Pos[1] = static_cast<float>(static_cast<double>(InvScale) *
                                                static_cast<double>(MapPos[1]));
                    Pos[2] = static_cast<float>(static_cast<double>(InvScale) *
                                                static_cast<double>(MapPos[2]));
                    Universe::IZone const* const Zone =
                        NavMapZoneAt(CurrentSystem, Pos, 1, 0, Field_0x87e); // FUN_004a1b50
                    if (Zone != nullptr)
                    {
                        FToolTipHoverInfo Record;
                        ToolTipHoverInit(&Record);
                        Record.IdsAlt = Zone->GetName(); // IZone +0x84
                        Record.Ids = static_cast<int>(reinterpret_cast<intptr_t>(Zone));
                        NavMapPlaceHoverAtCursor(&Record);
                        if (Record.IdsAlt != 0)
                            NavMapSubmitHover(&Record);
                    }
                }
            }
        }
    }

    // 004957e4..004959bf: the drag. A press (MouseEvent) arms it; with the left button still
    // down and no transition it starts after more than 4 px (Manhattan) and pans the map by
    // twice the cursor's move over the frame's pixel size, clamped to half MapSize.
    if (bMouseDown != 0)
    {
        int MouseX = 0;
        int MouseY = 0;
        GetClientSoftwareCursor(&MouseX, &MouseY, nullptr);
        if (bTransition == 0 && (GetClientMouseButtons() & 1) != 0)
        {
            if (bDragging == 0 && abs(DragMouseY - MouseY) + abs(DragMouseX - MouseX) > 4)
                bDragging = 1;
        }
        else
        {
            bMouseDown = 0;
            bDragging = 0;
        }
        if (bDragging != 0)
        {
            double const MoveX = static_cast<double>(MouseX - DragMouseX) * 2.0 /
                                 static_cast<double>(ScreenRectSize[0]);
            double const MoveY = static_cast<double>(DragMouseY - MouseY) * 2.0 /
                                 static_cast<double>(ScreenRectSize[1]);
            float const PanScale = static_cast<float>(
                static_cast<double>(MapSize[0]) /
                (static_cast<double>(ZoomScale) + static_cast<double>(ZoomScale)));
            float const PanX = static_cast<float>(MoveX * static_cast<double>(PanScale));
            double const PanY = MoveY * static_cast<double>(PanScale);
            double const PanZ = static_cast<double>(PanScale) * 0.0;
            PanOffset[0] = static_cast<float>(static_cast<double>(PanX) +
                                              static_cast<double>(PanOffset[0]));
            PanOffset[1] = static_cast<float>(PanY + static_cast<double>(PanOffset[1]));
            PanOffset[2] = static_cast<float>(PanZ + static_cast<double>(PanOffset[2]));

            // 004958e4..0049598e: TEST AH,0x41 + JNE -- each bound only when strictly passed;
            // the upper one is tested only when the lower one was not.
            float const HalfX = static_cast<float>(static_cast<double>(MapSize[0]) * 0.5);
            float const HalfY = static_cast<float>(static_cast<double>(MapSize[1]) * 0.5);
            if (-HalfX > PanOffset[0])
                PanOffset[0] = -HalfX;
            else if (PanOffset[0] > HalfX)
                PanOffset[0] = HalfX;
            if (-HalfY > PanOffset[1])
                PanOffset[1] = -HalfY;
            else if (PanOffset[1] > HalfY)
                PanOffset[1] = HalfY;

            SetMapMeshTransform(ViewMode, 1.0f); // FUN_004963d0
            DragMouseX = MouseX;
            DragMouseY = MouseY;
            bRebuild = 1;
            PlaceGridLabels(); // FUN_0049fbf0
        }
    }

    // 004959c4..00495ba5: ScreenRect -- the frame's two corners (with G_NavMapGlobal_67203c's
    // offset, at z -1.25) through the Win3D camera, floored; then this map's own camera takes
    // the Win3D camera's near / far / half fov and ScreenRect as its viewport.
    FSpaceCamera* const SceneCamera = Win3DGetCamera(); // DAT_0067dbf8
    float Corner[3];
    Corner[0] = static_cast<float>(static_cast<double>(G_NavMapGlobal_67203c[0]) +
                                   static_cast<double>(MapCorner0[0]));
    Corner[1] = static_cast<float>(static_cast<double>(G_NavMapGlobal_67203c[1]) +
                                   static_cast<double>(MapCorner2[1]));
    Corner[2] = -1.25f;
    AsCamera(SceneCamera)->Project(&ScreenRectMin[0], &ScreenRectMin[1], &ScreenRectMin[2],
                                   Corner);
    ScreenRectMin[0] = static_cast<float>(floor(static_cast<double>(ScreenRectMin[0])));
    ScreenRectMin[1] = static_cast<float>(floor(static_cast<double>(ScreenRectMin[1])));
    Corner[0] = static_cast<float>(static_cast<double>(G_NavMapGlobal_67203c[0]) +
                                   static_cast<double>(MapCorner2[0]));
    Corner[1] = static_cast<float>(static_cast<double>(G_NavMapGlobal_67203c[1]) +
                                   static_cast<double>(MapCorner0[1]));
    float Far[3];
    AsCamera(SceneCamera)->Project(&Far[0], &Far[1], &Far[2], Corner);
    Far[0] = static_cast<float>(floor(static_cast<double>(Far[0])));
    Far[1] = static_cast<float>(floor(static_cast<double>(Far[1])));
    ScreenRectSize[0] = static_cast<float>(static_cast<double>(Far[0]) -
                                           static_cast<double>(ScreenRectMin[0]));
    ScreenRectSize[1] = static_cast<float>(static_cast<double>(Far[1]) -
                                           static_cast<double>(ScreenRectMin[1]));
    ScreenRectSize[2] = static_cast<float>(static_cast<double>(Far[2]) -
                                           static_cast<double>(ScreenRectMin[2]));
    int Viewport[4];
    Viewport[0] = static_cast<int>(ScreenRectMin[0]); // ftol
    Viewport[1] = static_cast<int>(ScreenRectMin[1]);
    Viewport[2] = static_cast<int>(static_cast<double>(ScreenRectSize[0]) +
                                   static_cast<double>(ScreenRectMin[0]));
    Viewport[3] = static_cast<int>(static_cast<double>(ScreenRectSize[1]) +
                                   static_cast<double>(ScreenRectMin[1]));
    Camera.ZNear = SceneCamera->ZNear; // FUN_0040fb60: the two stores
    Camera.ZFar = SceneCamera->ZFar;
    SpaceCameraSetHalfFovX(&Camera, SceneCamera->HalfFovXDeg); // FUN_0040f8c0
    SpaceCameraSetViewportEx(&Camera, Viewport, 1);           // FUN_0040fb80

    // 00495baa..00495c4a: the system view follows the player (marker position / heading) and
    // the waypoints; the universe view pulses its stars.
    if (ViewMode != 0)
    {
        if (PlayerItem != nullptr)
        {
            float Pos[3];
            float Heading = 0.0f;
            PlayerMapPosition(Pos, &Heading); // FUN_00491be0
            // TEST AH,0x44 + JP: any difference (or unordered) moves the marker.
            if (!(Pos[0] == PlayerItem->Position[0]) || !(Pos[1] == PlayerItem->Position[1]) ||
                !(Pos[2] == PlayerItem->Position[2]) || !(PlayerItem->Heading == Heading))
            {
                PlayerItem->Position[0] = Pos[0];
                PlayerItem->Position[1] = Pos[1];
                PlayerItem->Position[2] = Pos[2];
                PlayerItem->Heading = Heading;
                bRebuild = 1;
            }
        }
        if (RefreshWaypointItems()) // FUN_00496e50
            bRebuild = 1;
    }
    else
    {
        AnimateItems(); // FUN_004a1710
        bRebuild = 1;
    }

    // 00495c4f..00495c97: the blink flag toggles once per whole second.
    {
        double Blink = GetClientFrameTimeCapped() + static_cast<double>(BlinkTimer);
        BlinkTimer = static_cast<float>(Blink);
        if (Blink > 1.0)
        {
            unsigned char bNewBlink = bBlink;
            do
            {
                Blink -= 1.0;
                bNewBlink = bNewBlink == 0 ? 1 : 0;
            } while (Blink > 1.0);
            BlinkTimer = static_cast<float>(Blink);
            bBlink = bNewBlink;
        }
    }

    // 00495c9b..00496291: the transition.
    if (bTransition != 0)
    {
        double const Time = static_cast<double>(FrameTime) + static_cast<double>(TransitionTime);
        TransitionTime = static_cast<float>(Time);
        // TEST AH,1 + JNE: still running while less (or unordered).
        if (Time >= static_cast<double>(TransitionDuration))
        {
            // 00495ccb..0049600e: it ended.
            int const View = TransitionView;
            TransitionTime = TransitionDuration;
            bTransition = 0;
            if (View == 0)
            {
                bViewSwitch = 0;
                if (ViewMode == 0)
                {
                    // 00495cfd..00495dd2: back on the universe -- the texts opaque again.
                    NavMapTintTexts(this, NavMapPaletteAlpha(), 0);
                    RefreshMissionInfo(); // FUN_0049a580
                    return;
                }
                // 00495dd7..00495e0e: out of a system -- the universe view, faded back in.
                ZoomScale = 1.0f;
                EnterUniverseView(); // FUN_0049e550
                bViewSwitch = 1;
                bTransition = 1;
                TransitionView = 0;
                TransitionTime = 0.0f;
                TransitionDuration = kNavMapSwitchSeconds;
                return;
            }
            if (View == 1 && ViewMode != 2)
            {
                bViewSwitch = 0;
                if (ViewMode == 1)
                {
                    // 00495e39..00495f07
                    NavMapTintTexts(this, NavMapPaletteAlpha(), 0);
                    RefreshMissionInfo();
                    return;
                }
                // 00495f0c..00495fb6: into the system -- its view, its name, faded back in.
                EnterSystemView(); // FUN_0049e1d0
                NavMapShowSystemName(SystemNameText, CurrentSystem, SkipSystemIds);
                bViewSwitch = 1;
                bTransition = 1;
                TransitionView = 1;
                TransitionTime = 0.0f;
                TransitionDuration = kNavMapSwitchSeconds;
                return;
            }
            // 00495fd3..0049600e: a zoom ended -- the view is the target one; both zoomed map
            // meshes opaque.
            ViewMode = View;
            ZoomedMapMesh.UserFloats[0] = 1.0f;
            ZoomedMapMesh.UserFloats[1] = 1.0f;
            ZoomedMapMesh.UserFloats[2] = 1.0f;
            ZoomedMapMesh.UserFloats[4] = 1.0f;
            ZoomedMapMesh2.UserFloats[0] = 1.0f;
            ZoomedMapMesh2.UserFloats[1] = 1.0f;
            ZoomedMapMesh2.UserFloats[2] = 1.0f;
            ZoomedMapMesh2.UserFloats[4] = 1.0f;
        }

        // 00496015..00496043: Ease = 1 - (cos(pi t / T) + 1) / 2.
        float const Ease = static_cast<float>(
            1.0 - (cos(static_cast<double>(TransitionTime) /
                       static_cast<double>(TransitionDuration) * static_cast<double>(kNavMapPi)) +
                   1.0) *
                      0.5);
        TransitionEase = Ease;
        if (bViewSwitch != 0)
        {
            // 00496057..00496199: a view switch fades the texts (out, then -- on the way into
            // the target view -- back in); nothing moves.
            if (TransitionView == ViewMode)
                TransitionEase = static_cast<float>(1.0 - static_cast<double>(Ease));
            NavMapTintTexts(this, NavMapFadeAlpha(TransitionEase), 1);
        }
        else
        {
            // 0049619e..00496291: a zoom pans / scales towards the target.
            PanOffset[0] = static_cast<float>(static_cast<double>(Ease) *
                                              static_cast<double>(TransitionDelta[0]));
            PanOffset[1] = static_cast<float>(static_cast<double>(Ease) *
                                              static_cast<double>(TransitionDelta[1]));
            PanOffset[2] = static_cast<float>(static_cast<double>(Ease) *
                                              static_cast<double>(TransitionDelta[2]));
            bRebuild = 1;
            double const Distance =
                static_cast<double>(ZoomDistance) + static_cast<double>(PanOffset[2]);
            PanOffset[2] = 0.0f;
            ZoomScale = static_cast<float>(static_cast<double>(MapExtentX) / Distance);
            PanOffset[0] = static_cast<float>(static_cast<double>(PanOffset[0]) +
                                              static_cast<double>(TransitionStart[0]));
            PanOffset[1] = static_cast<float>(static_cast<double>(PanOffset[1]) +
                                              static_cast<double>(TransitionStart[1]));
            PanOffset[2] = static_cast<float>(static_cast<double>(PanOffset[2]) +
                                              static_cast<double>(TransitionStart[2]));
            if (TransitionView != 0)
            {
                if (ViewMode == 2)
                {
                    SetMapMeshTransform(ViewMode, static_cast<float>(1.0 - static_cast<double>(Ease)));
                    SetMapMeshTransform(TransitionView, Ease);
                }
                else if (ViewMode == 1)
                {
                    SetMapMeshTransform(TransitionView, Ease);
                    SetMapMeshTransform(ViewMode, 1.0f);
                }
            }
            PlaceGridLabels(); // FUN_0049fbf0
        }
    }

    // 00496296..00496312: IClient slot 93's "no best path" answer shows NoBestPathText for 8 s.
    if (G_bNavMapNoBestPath != 0)
    {
        G_bNavMapNoBestPath = 0;
        NoBestPathText->SetProperty(1, 0, 0);
        NoBestPathTimer = kNavMapNoBestPathSeconds;
    }
    if (NoBestPathTimer > 0.0f)
    {
        double const Left = static_cast<double>(NoBestPathTimer) - GetClientFrameTimeCapped();
        NoBestPathTimer = static_cast<float>(Left);
        // TEST AH,0x41 + JP: hidden once less or equal (not while greater or unordered).
        if (Left <= 0.0)
        {
            NoBestPathText->SetProperty(1, 1, 0);
            NoBestPathTimer = 0.0f;
        }
    }

    // 00496318..00496371: in multiplayer the group's positions are asked for every 30 s
    // (IServer +0x12c with one zero dword).
    {
        double const Poll = GetClientFrameTimeCapped() + static_cast<double>(GroupPollTimer);
        GroupPollTimer = static_cast<float>(Poll);
        if (Poll > static_cast<double>(kNavMapGroupPollSeconds))
        {
            GroupPollTimer =
                static_cast<float>(Poll - static_cast<double>(kNavMapGroupPollSeconds));
            if (!SinglePlayer())
            {
                unsigned Zero = 0;
                G_Server->RequestGroupPositions(GetPlayerConnectionId(),
                                                reinterpret_cast<unsigned char*>(&Zero), 4);
            }
        }
    }

    // 00496375..0049637d
    if (bRebuild != 0)
        PlaceItems();
}

