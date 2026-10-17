// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "hud_contact_list.h"

#include "best_path.h"               // BestPath* (FUN_004c4fb0 / 004c4ff0 / 004c5110 / 004c52d0)
#include "character_select_equip.h"  // GetPlayerStarSystemIdRetail (FUN_004c4830), character name
#include "client_gameobj.h"          // FClientGameobj, GameobjWatchList (DAT_0066d33c)
#include "client_mesh_load.h"        // Win3DModelCachePreload (FUN_0059e980)
#include "client_objdb.h"            // ClientObjDb_* (FUN_00540fc0 / 00540a00 / 005416c0 ...)
#include "font_imp.h"                // FindMenuFontImp (FUN_004177b0)
#include "font_string.h"             // FontStringSetFont / SetScale / SetText
#include "game_options.h"            // GetGameOption (FUN_005792b0)
#include "globals.h"                 // GetClientFrameTimeCapped, ShouldSkipLocalServerTick
#include "widescreen.h"   // port-only (FREELANCER_WIDESCREEN_SUPPORT)
#include "hud_comm.h"                // HudCommIsWatchedObjectId (FUN_004cc880)
#include "hud_command_dispatch.h"    // FSpaceCommand, SpaceSendCommand (FUN_0054bcb0)
#include "hud_status_core.h"         // G_HudTargetPanel (DAT_00674b04)
#include "hud_target.h"              // HudTarget, HudReputationColor, HudIsUnvisitedSolar ...
#include "hud_waypoint.h"            // GetHudWaypoint (DAT_00674bc8), HudWaypointSelect*
#include "ids_strings.h"             // GetIdsStringWWithRawFallback (FUN_004347e0)
#include "input_manager.h"           // cursor flag / position (FUN_0041dd90, DAT_00616840)
#include "menu_ui_init.h"            // PlayOrRewindUiSoundSlot (FUN_005645c0)
#include "menu_ui_widgets.h"         // RetailU32QueueSize / At (FUN_004c5890 / 004c58b0)
#include "misc_util.h"               // FmtStrSprintf (FUN_0041c970)
#include "nn_button_control.h"       // NnButtonControl::MontageTile (+0x338)
#include "nn_montage_draw.h"         // FindButtonMontageTile (FUN_0055a360)
#include "nn_text_control.h"         // NnTextControl (+0x334 Font::String)
#include "nn_ui_pulse.h"             // NnUiPulseBlendFromArgb (FUN_00561f20), palette
#include "player_behavior.h"         // GetPlayerObjRW (FUN_0054baf0)
#include "player_spawn.h"            // player waypoints (FUN_004c4690 / 004c46a0)
#include "space_camera.h"            // FSpaceCamera
#include "space_letterbox.h"         // IsLetterboxVisible (FUN_0041a3e0)
#include "ui_scroll.h"
#include "win3d.h"                   // Win3DGetCamera, Win3DTransformByMatrix
#include "x86math.h"                 // FVector, FMatrix, I3DMathEngine

#include "common_archetype.h"
#include "common_cequip.h"
#include "common_cobject.h"
#include "common_csys.h"             // Acquire3DMathEngine (FUN_005b73e0)
#include "common_iface.h"
#include "common_misc.h"             // Geometry::Sphere, SUBOBJ_ID_NONE
#include "common_reputation.h"
#include "common_text.h"             // FmtStr
#include "common_universe.h"         // Universe::get_system, SpatialFill

#include "dacom.h"                   // FDUMP

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <wchar.h>

unsigned char G_bHudContactListRefreshRequested = 0;

namespace
{
    // DAT_006748fc -- set while building the list once an entry for the player's target
    // (DAT_00674900) has been pushed; RebuildContacts adds the target itself when it is not.
    unsigned char G_bContactTargetListed = 0;
    // DAT_00674900 -- the CSimple id of the player ship's target, 0 without one.
    unsigned G_ContactTargetId = 0;
    // DAT_00674904 / 08 / 0c -- the player's position at the start of the rebuild.
    Vector G_ContactPlayerPos = {0.0f, 0.0f, 0.0f};
    // DAT_00674948 -- the 3DMathEngine the relayout's cursor ray uses, FUN_005b73e0 on first use
    // (0x004d3b57).
    I3DMathEngine* G_ContactListMath = nullptr;
    // .data DAT_006124b8 (initial 0xffffffff) -- the scroll position the rows were last laid out
    // for. Only Draw reads (0x004d26ef) and writes (0x004d2b59) it.
    int G_ContactListLastScroll = -1;

    // .data DAT_00612438 -- the icon mesh of each row kind (kinds 3 and 4 share one string).
    char const* const kContactIconPaths[16] = {
        "interface\\HUD\\HUD_contactship.3db",          // 0  ship
        "interface\\HUD\\HUD_contactplanet.3db",        // 1  planet
        "interface\\HUD\\HUD_contactstation.3db",       // 2  station
        "interface\\HUD\\HUD_contactwaypoint.3db",      // 3  best-path waypoint
        "interface\\HUD\\HUD_contactwaypoint.3db",      // 4  player waypoint
        "interface\\HUD\\HUD_contactbattleship.3db",    // 5
        "interface\\HUD\\HUD_contactjumpgate.3db",      // 6
        "interface\\HUD\\HUD_contacttradelane.3db",     // 7
        "interface\\HUD\\HUD_contactfrieghter.3db",     // 8
        "interface\\HUD\\HUD_contacttransport.3db",     // 9
        "interface\\HUD\\HUD_contactnomad.3db",         // 10
        "interface\\HUD\\HUD_contactloot.3db",          // 11
        "interface\\HUD\\HUD_contactweaponplat.3db",    // 12
        "interface\\HUD\\HUD_contactlootabledepot.3db", // 13
        "interface\\HUD\\HUD_contactotherplayers.3db",  // 14 another player
        "interface\\HUD\\HUD_contactgroup.3db",         // 15 a player of the own group
    };
    // .rdata 0x005d7a70 -- the icon of a row whose object is on the comm list.
    char const* const kContactChatIconPath = "interface\\HUD\\HUD_chat.3db";
    // .data DAT_00612478 -- the icon tooltip ids of each row kind.
    unsigned const kContactTooltipIds[16] = {
        0x44a, 0x44b, 0x44c, 0x44d, 0x44e, 0x46e, 0x46f, 0x4fa,
        0x502, 0x503, 0x504, 0x520, 0x56c, 0x56d, 0x60e, 0x60f,
    };

    // Constants (addresses of the retail copies).
    float const kDefaultRadarRange = 2500.0f;       // .data 0x00612434 (0x451c4000)
    float const kUnlimitedRange = 1.0e30f;          // 0x7149f2ca stored inline (0x004d05f4)
    float const kSpatialQueryRadius = 14000.0f;     // 0x465ac000 stored inline (0x004d1749)
    float const kNearRangeSq = 196000000.0f;        // DAT_005d7968 = 14000^2
    float const kRebuildSeconds = 3.0f;             // DAT_005d7964
    float const kNameRefreshSeconds = 30.0f;        // DAT_005d3e40
    float const kLabelWidthScale = 125.0f;          // .data DAT_00612430
    float const kLabelWidthFactor = 0.00125f;       // DAT_005c8d30
    float const kLabelScale = 0.16f;                // DAT_005d795c
    double const kGroupNameDrop = 0.004;            // DAT_005d7bf0 (double)
    float const kRowStep = 0.018f;                  // DAT_005d3ee8
    float const kRowTop = -0.00199999847f;          // DAT_005d7bc4 (0xbb031268)
    float const kRangeRowOffset = 0.0075f;          // DAT_005d7bc0
    float const kNameRowOffset = 0.015f;            // DAT_005d1f04
    float const kIconRowTop = 0.00100000016f;       // DAT_005d7b90 (0x3a831270)

    // Watchable is IObjRW's third base, at +8 (retail spells `obj ? obj + 8 : 0` out).
    Watchable const* WatchableOf(IObjRW* Obj)
    {
        if (Obj == nullptr)
        {
            return nullptr;
        }
        return reinterpret_cast<Watchable const*>(reinterpret_cast<unsigned char const*>(Obj) + 8);
    }

    // The chain retail inlines on the player object: IObjRW + 0xc (the IObjInspectImpl CBase),
    // then CBase::Object (+4). Null for a null object.
    CObject* InlineCObjectOf(IObjRW* Rw)
    {
        if (Rw == nullptr)
        {
            return nullptr;
        }
        IObjInspectImpl* const Impl = static_cast<IObjInspectImpl*>(Rw);
        CBase* const Base = Impl;
        return Base->Object;
    }

    bool IsClass(CObject const* Object, unsigned Mask)
    {
        return (static_cast<unsigned>(Object->ObjectClass) & Mask) == Mask;
    }

    // (class & 0x503) == 0x503, else null.
    CShip const* AsShip(CObject const* Object)
    {
        if (Object == nullptr || !IsClass(Object, 0x503u))
        {
            return nullptr;
        }
        return static_cast<CShip const*>(Object);
    }

    // (class & 3) == 3, else null.
    CSimple const* AsSimple(CObject const* Object)
    {
        if (Object == nullptr || (static_cast<unsigned>(Object->ObjectClass) & 3u) != 3u)
        {
            return nullptr;
        }
        return static_cast<CSimple const*>(Object);
    }

    // A CEqObj (0x103) whose archetype (+0x88) has the nomad byte (+0x80) set.
    bool IsNomadEqObj(CObject const* Object)
    {
        if (!IsClass(Object, 0x103u))
        {
            return false;
        }
        Archetype::EqObj const* const Arch = static_cast<Archetype::EqObj const*>(Object->Archetype);
        return Arch != nullptr && Arch->bNomad;
    }

    // CShip +0x1b4 -- the player group id (the PhySys::Controller tail slot the port keeps
    // opaque). Retail reads it with no test of the ship pointer: a null ship faults on
    // [0 + 0x1b4], as it does here.
    unsigned ShipGroupId(CObject const* Ship)
    {
        unsigned Value = 0;
        memcpy(&Value, reinterpret_cast<unsigned char const*>(Ship) + 0x1b4, sizeof(Value));
        return Value;
    }

    // The player ship through the inline chain (0x004d401a..0x004d403b, 0x004d02a4..,
    // 0x004d095f..): a non-ship gives null, which the caller then reads +0x1b4 of.
    CObject const* PlayerShipForGroup(IObjRW* Player)
    {
        CObject* const Object = InlineCObjectOf(Player);
        if (Object == nullptr || !IsClass(Object, 0x503u))
        {
            return nullptr;
        }
        return Object;
    }

    // The same-group test every "other player" row makes: kind 0xf when Ship is in a group
    // (+0x1b4 != 0) and the player ship's group is the same, 0xe otherwise.
    int OtherPlayerKind(CObject const* Ship)
    {
        unsigned const Group = ShipGroupId(Ship);
        if (Group != 0)
        {
            IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW()); // FUN_0054baf0
            if (Player != nullptr && ShipGroupId(PlayerShipForGroup(Player)) == Group)
            {
                return 0xf;
            }
        }
        return 0xe;
    }

    bool IsShortNameShown(unsigned ShortName)
    {
        return ShortName != 0 && ShortName != 0xffffffffu && ShortName != 0x30386u &&
               ShortName != 0x303b8u;
    }

    // FUN_004347e0 into Buf; a zero result leaves an empty string (the callers' own
    // `MOV word ptr [Buf], 0`).
    void IdsInto(unsigned Ids, wchar_t* Buf, int Chars)
    {
        if (GetIdsStringWWithRawFallback(Ids, Buf, Chars) == 0)
        {
            Buf[0] = 0;
        }
    }

    // FUN_004d4120 (cdecl) -- Entry stands for the object TargetId, or for the waypoint the
    // HUD waypoint (DAT_00674bc8) sits on; bByObject also accepts a waypoint whose record names
    // TargetId.
    bool IsEntryTarget(FHudContactEntry const* Entry, unsigned TargetId, bool bByObject)
    {
        if (Entry->Object() != nullptr && Entry->Id == TargetId)
        {
            return true;
        }
        if (Entry->Kind == 4)
        {
            int const Index = Entry->Index;
            if (Index >= GetPlayerWaypointCountRetail())          // FUN_004c4690, JGE
            {
                return false;
            }
            FPlayerWaypointRecord* const Record = GetPlayerWaypointRetail(Index); // FUN_004c46a0
            if (GetHudWaypoint()->IsWaypointAt(&Record->Pos))     // FUN_004f4b90
            {
                return true;
            }
            return bByObject && Record->ObjectId != 0 && Record->ObjectId == TargetId;
        }
        if (Entry->Kind == 3)
        {
            int const Index = Entry->Index;
            if (Index >= BestPathGetWaypointCount())              // FUN_004c4fb0, JGE
            {
                return false;
            }
            FBestPathWaypoint* const Waypoint = BestPathGetWaypoint(Index); // FUN_004c4ff0
            Vector Pos;
            unsigned System = 0;
            if (BestPathWaypointResolvePosition(Waypoint, &Pos, &System) && // FUN_004c52d0
                GetHudWaypoint()->IsWaypointAt(&Pos))
            {
                return true;
            }
            // 0x004d41ee: the object is resolved before bByObject is looked at.
            IObjRW* const Object = BestPathWaypointResolveObject(Waypoint); // FUN_004c5110
            return bByObject && Object != nullptr && Object->get_id() == TargetId;
        }
        return false;
    }

    // FUN_00540990 (cdecl; retail Client\...\ObjDB, only caller 0x004d1792) -- a Gameobj that
    // belongs in the large-solar vector: radius (+0x38 of its CObject, slot +0x150) above
    // DAT_00613ec4 = 200 (TEST AH,0x41 / JNZ: not above, or NaN, is false) and bit 7 of its
    // get_type (slot +0x88) clear.
    bool IsLargeSolarCandidate(FClientGameobj* Gameobj)
    {
        if (!(Gameobj->cobject()->Radius > 200.0f))
        {
            return false;
        }
        unsigned Type = 0;
        Gameobj->get_type(Type);
        return (Type & 0x80u) == 0;
    }

    // The zeroed command record, id 0x1c "select target", sent through FUN_0054bcb0
    // (0x004d012c..0x004d015e).
    void SendSelectTarget(unsigned ObjectId)
    {
        FSpaceCommand Command = {};
        Command.Id = 0x1c;
        Command.ObjectId = ObjectId;
        Command.SubObjectId = SUBOBJ_ID_NONE;
        Command.Byte26 = 1;
        SpaceSendCommand(&Command);
    }

    // The cursor ray Draw builds for the hover test (0x004d292d..0x004d2a1d and
    // 0x004d3ac4..0x004d3bde): the cursor pixel (DAT_00616840 / 44) onto the near plane of the
    // Win3D camera DAT_0067dbf8, turned by a copy of its orientation DAT_0067dbfc -- through
    // FUN_00423b90 in the refresh pass, through this TU's lazy 3DMathEngine (slot +0x2c) in the
    // layout pass -- then scaled by 1 / length.
    void ContactCursorRay(float* OutRay, bool bThroughEngine)
    {
        FSpaceCamera* const Cam = Win3DGetCamera();
        int CursorX = 0;
        int CursorY = 0;
        GetClientSoftwareCursor(&CursorX, &CursorY, nullptr);
        FVector Local;
        Local.X = static_cast<float>((static_cast<double>(CursorX) - Cam->ScreenCenterX) /
                                     Cam->XScale * Cam->ZNear);
        Local.Y = static_cast<float>((static_cast<double>(CursorY) - Cam->ScreenCenterY) /
                                     Cam->YScale * Cam->ZNear);
        Local.Z = -Cam->ZNear;
        FMatrix Orientation;
        memcpy(&Orientation, Cam->World, sizeof(Orientation));
        FVector World;
        if (bThroughEngine)
        {
            if (G_ContactListMath == nullptr)
            {
                G_ContactListMath = Acquire3DMathEngine();   // FUN_005b73e0
            }
            G_ContactListMath->TransformMatrix(World, Orientation, Local);
        }
        else
        {
            Win3DTransformByMatrix(World, Orientation, Local); // FUN_00423b90
        }
        double const Inv = 1.0 / sqrt(static_cast<double>(World.X) * World.X +
                                      static_cast<double>(World.Y) * World.Y +
                                      static_cast<double>(World.Z) * World.Z);
        OutRay[0] = static_cast<float>(World.X * Inv);
        OutRay[1] = static_cast<float>(World.Y * Inv);
        OutRay[2] = static_cast<float>(World.Z * Inv);
    }

    // The label both name paths of the layout pass build (0x004d3431..0x004d3888 for a ship,
    // 0x004d375b..0x004d3888 otherwise): the client object's Font::String (+0x48) flagged
    // (FUN_0053aa10(1)), given Font (FUN_004ea670 inlined), scaled (FUN_004145d0(0.16, -1)),
    // Name cut from the end until its measured width (IFont slot +0x20) is at most
    // surface width * 125 * 0.00125, then set (FUN_00415860).
    FFontString* ApplyRowLabel(FClientGameobj* Gameobj, IFont* Font, wchar_t* Name)
    {
        Gameobj->SetFlag3(true);
        FFontString* const Label = Gameobj->GetFontString48();
        FontStringSetFont(Label, Font);
        FontStringSetScale(Label, kLabelScale, -1.0f);
        IFont* const Measure = Label->FontIface;
        int Size[2] = {0, 0};
        Measure->MeasureString(Size, Name, -1);
#ifdef FREELANCER_WIDESCREEN_SUPPORT
        // Port-only Hor+: the list is interface, sized by height; its limit follows the width
        // of a 4:3 surface of this height.
        float const Limit = static_cast<float>(
            WidescreenEquivalentSurfaceWidth() * kLabelWidthScale * kLabelWidthFactor);
#else
        float const Limit = static_cast<float>(
            static_cast<double>(static_cast<int>(G_RenderSurfaceWidth)) * kLabelWidthScale *
            kLabelWidthFactor);
#endif
        // FILD qword {W, 0} / FCOMP Limit / TEST AH,0x41 / JNZ: the loop leaves once the width
        // is not above the limit (or the compare is unordered).
        while (static_cast<double>(static_cast<unsigned>(Size[0])) > Limit)
        {
            int const Last = static_cast<int>(wcslen(Name)) - 1;
            if (Last <= 0)
            {
                break;
            }
            Name[Last] = 0;
            Measure->MeasureString(Size, Name, Last);
        }
        FontStringSetText(Label, Name);
        return Label;
    }

    // The swap Draw inlines around WinControl::Draw (0x004d2ad6.., 0x004d3d16..): every row with
    // a label lends it to its name text (FUN_0058a5d0 / FUN_0058a5e0), the second pass puts the
    // text's own Font::String back.
    void SwapRowLabels(HUD_ContactList* Self)
    {
        for (int Row = 0; Row < 8; ++Row)
        {
            FFontString* const Label = Self->RowLabels[Row];
            if (Label == nullptr)
            {
                continue;
            }
            NnTextControl* const Text = static_cast<NnTextControl*>(Self->NameTexts[Row]);
            Self->RowLabels[Row] = Text->GetFontString();
            Text->FontString = Label;
        }
    }
}

// -------------------------------------------------------------------------------------------
// FHudContactEntry
// -------------------------------------------------------------------------------------------

FHudContactEntry::FHudContactEntry(int InKind, IObjRW* Obj, float InDistance, unsigned InId,
                                   int InIndex)
    : Kind(InKind)
    , Distance(InDistance)
    , Id(InId)
    , Index(InIndex)
    , bWatchListed(0)
{
    // FUN_004cf030. The watcher takes Obj (+8), set_pointer only when it differs.
    Watcher.set(WatchableOf(Obj));

    IObjRW* const Watched = Object();
    if (Watched != nullptr)
    {
        // 0x004cf0b2..0x004cf0cf: the id, and the "target is listed" latch.
        Id = Watched->get_id();
        if (G_ContactTargetId == Id)
        {
            G_bContactTargetListed = 1;
        }
        // 0x004cf0e4..0x004cf162: not the player -- distance from the player to the surface of
        // the physical sphere (slot +0x64), z / y / x squared in that order, clamped at 0.
        if (Watched != static_cast<IObjRW*>(GetPlayerObjRW()))
        {
            Vector Center;
            float const Radius = Watched->cobject()->get_physical_radius_r(Center);
            double const Dx = static_cast<double>(Center.x) - G_ContactPlayerPos.x;
            double const Dy = static_cast<double>(Center.y) - G_ContactPlayerPos.y;
            double const Dz = static_cast<double>(Center.z) - G_ContactPlayerPos.z;
            Distance = static_cast<float>(sqrt(Dz * Dz + Dy * Dy + Dx * Dx) - Radius);
            if (Distance < 0.0f)
            {
                Distance = 0.0f;
            }
        }
    }
    else if (Kind == 4)
    {
        // 0x004cf172..0x004cf255: a player waypoint.
        if (Index >= GetPlayerWaypointCountRetail())
        {
            Distance = 0.0f;
        }
        else
        {
            FPlayerWaypointRecord* const Record = GetPlayerWaypointRetail(Index);
            double const Dx = static_cast<double>(Record->Pos.x) - G_ContactPlayerPos.x;
            double const Dy = static_cast<double>(Record->Pos.y) - G_ContactPlayerPos.y;
            double const Dz = static_cast<double>(Record->Pos.z) - G_ContactPlayerPos.z;
            Distance = static_cast<float>(sqrt(Dz * Dz + Dy * Dy + Dx * Dx));
            if (Record->ObjectId != 0)
            {
                // 0x004cf1e5..0x004cf226: on an object -- the distance truncated (ftol) and the
                // object's physical radius taken off.
                FClientGameobj* const Gameobj = ClientObjDb_FindObject(Record->ObjectId, 0);
                if (Gameobj != nullptr)
                {
                    Distance = static_cast<float>(static_cast<int>(Distance));
                    Vector Center;
                    float const Radius = Gameobj->cobject()->get_physical_radius_r(Center);
                    Distance = static_cast<float>(static_cast<double>(Distance) - Radius);
                }
            }
        }
        if (G_bContactTargetListed == 0 && IsEntryTarget(this, G_ContactTargetId, true))
        {
            G_bContactTargetListed = 1;
        }
    }
    else if (Kind == 3)
    {
        // 0x004cf25a..0x004cf363: a best-path waypoint; the latch first.
        if (G_bContactTargetListed == 0 && IsEntryTarget(this, G_ContactTargetId, true))
        {
            G_bContactTargetListed = 1;
        }
        if (Index >= BestPathGetWaypointCount())
        {
            Distance = 0.0f;
        }
        else
        {
            FBestPathWaypoint* const Waypoint = BestPathGetWaypoint(Index);
            Vector Pos;
            unsigned System = 0;
            // 0x004cf2b9 / 0x004cf2d8: no position, or not in the player's system -- 0, and the
            // final clamp is skipped (LAB_004cf375).
            if (!BestPathWaypointResolvePosition(Waypoint, &Pos, &System))
            {
                Distance = 0.0f;
                return;
            }
            if (System != Universe::get_system(GetPlayerStarSystemIdRetail())->GetId())
            {
                Distance = 0.0f;
                return;
            }
            // 0x004cf2de..0x004cf324: the differences stored as floats; z times its unrounded
            // self, then y and x squared as floats.
            double const Dx = static_cast<double>(Pos.x) - G_ContactPlayerPos.x;
            double const Dy = static_cast<double>(Pos.y) - G_ContactPlayerPos.y;
            double const Dz = static_cast<double>(Pos.z) - G_ContactPlayerPos.z;
            float const DxF = static_cast<float>(Dx);
            float const DyF = static_cast<float>(Dy);
            float const DzF = static_cast<float>(Dz);
            Distance = static_cast<float>(sqrt(Dz * DzF + static_cast<double>(DyF) * DyF +
                                               static_cast<double>(DxF) * DxF));
            IObjRW* const Resolved = BestPathWaypointResolveObject(Waypoint);
            if (Resolved != nullptr)
            {
                Distance = static_cast<float>(static_cast<int>(Distance));
                Vector Center;
                float const Radius = Resolved->cobject()->get_physical_radius_r(Center);
                Distance = static_cast<float>(static_cast<double>(Distance) - Radius);
            }
        }
    }

    // 0x004cf365..0x004cf375: TEST AH,5 / JP -- only an ordered "below zero" is clamped.
    if (Distance < 0.0f)
    {
        Distance = 0.0f;
    }
}

FHudContactEntry::FHudContactEntry(FHudContactEntry const& Other)
    : Kind(Other.Kind)
    , Distance(Other.Distance)
    , Id(Other.Id)
    , Index(Other.Index)
    , bWatchListed(Other.bWatchListed)
{
    // FUN_004d49b0: a fresh watcher on what the other one watches.
    Watcher.set(Other.Watcher.target());
}

IObjRW* FHudContactEntry::Object() const
{
    return ObjRwFromWatchable(const_cast<Watchable*>(Watcher.target()));
}

// -------------------------------------------------------------------------------------------
// HUD_ContactList
// -------------------------------------------------------------------------------------------

static_assert(offsetof(HUD_ContactList, Tiles) == 0x368, "HUD_ContactList +0x368 tiles");
static_assert(offsetof(HUD_ContactList, Icons) == 0x3ac, "HUD_ContactList +0x3ac icons");
static_assert(offsetof(HUD_ContactList, Scroll) == 0x40c, "HUD_ContactList +0x40c scroll");
static_assert(sizeof(FHudContactEntry) == 0x1c, "retail contact entry is 0x1c bytes");

HUD_ContactList::HUD_ContactList(INI_Reader& /*Reader*/)
{
    // FUN_004cf390: HUDItem inlined, WinControl FUN_0059f860, the list head (operator new 0x24),
    // the three vptrs, HUDItem's name, then Create(0, 0) through the direct call at 0x004cf4a5,
    // the Win3D name and class name. RowLabels is not written by the ctor (Draw zeroes it
    // before any read).
    HUDItem::Name = "ContactList";
    HUD_ContactList::Create(nullptr, nullptr);
    Win3D::SetName("HUD_ContactList");
    ClassName = "HUD_ContactList";

    // 0x004cf4e6..0x004cf517.
    ScrollPos = 0;
    RebuildTimer = kRebuildSeconds;
    FilterMode = 0;
    bDirty = 0;
    LastPanelMode = -1;
    Scanner = nullptr;

    // 0x004cf51d..0x004cf559: the player ship's scanner. Retail runs FindFirst on
    // `EAX + 0xe4` with EAX null for a player that is not a ship.
    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW()); // FUN_0054baf0
    if (Player != nullptr)
    {
        CShip const* const Ship = AsShip(Player->cobject());       // slot +0x150
        Scanner = const_cast<CEquip*>(Ship->EquipManager.FindFirst(0x80000));
    }

    // 0x004cf55f..0x004cf590.
    HudSmallFont = FindMenuFontImp("HudSmall");
    PlayerNameFont = FindMenuFontImp("PlayerName");
    NameRefreshTimer = 0.0f;
    G_bHudContactListRefreshRequested = 0;
}

HUD_ContactList::~HUD_ContactList()
{
    // FUN_004cf5d0: the list is emptied (FUN_004d47a0) and its head freed, then Event::Handler
    // FUN_00576010 on +0x364, ~WinControl FUN_0059fa50 on +0x38 and HUDItem's watcher -- the
    // member and the base destructors, in that order.
}

bool HUD_ContactList::Create(Win3D* NewParent, void* Data)
{
    // FUN_004cf6a0 (this = the WinControl face).

    // 0x004cf6ac..0x004cf6c9: FUN_0059e980(path, 0) on the Win3D model cache DAT_0067dcf4 for
    // each of the 16 icon meshes (DAT_00612438 up to DAT_00612478, a preload); the result is
    // not looked at.
    for (int Index = 0; Index < 16; ++Index)
        Win3DModelCachePreload(kContactIconPaths[Index], nullptr);

    // 0x004cf6cb..0x004cf72c: the formation-list title, hidden.
    float const TitlePos[3] = {-0.189f, 0.043f, 0.0f};   // 0xbe418937, 0x3d3020c5
    FormationTitle = CreateTextControl("FormationTitle", "PageHeader", TitlePos, nullptr, 0.269f,
                                       0.015f, 1.7f, 0x60d, 1, 0, 1);
    FormationTitle->Flags = static_cast<unsigned char>(FormationTitle->Flags & 0xfc);

    // 0x004cf730..0x004cf860: the range and name text of each row.
    for (int Index = 0; Index < 8; ++Index)
    {
        char ControlName[0x100];
        _snprintf(ControlName, 0x100, "ContactListRangeText%d", Index);
        // FILD / FMUL 0.018 / FSUBR -0.002: the row's top, kept unrounded for the range text
        // and stored as a float (FST) for the name text.
        double const RowTop = static_cast<double>(kRowTop) -
                              static_cast<double>(Index) * static_cast<double>(kRowStep);
        float const RowTopF = static_cast<float>(RowTop);
        float const RangePos[3] = {-0.126f, static_cast<float>(RowTop + kRangeRowOffset), 0.0f};
        RangeTexts[Index] = CreateTextControl(ControlName, "HudSmall", RangePos, nullptr, 0.043f,
                                              0.015f, 0.9f, 0, 1, 0, 0);
        RangeTexts[Index]->SetProperty(0x24, 1, 0);

        _snprintf(ControlName, 0x100, "ContactListNameText%d", Index);
        float const NamePos[3] = {-0.107f,
                                  static_cast<float>(static_cast<double>(RowTopF) + kNameRowOffset),
                                  0.0f};
        NameTexts[Index] = CreateTextControl(ControlName, "HudSmall", NamePos, nullptr, kLabelScale,
                                             kRowStep, 0.9f, 0, 0, 0, 1);
        NameTexts[Index]->HomeOffset[0] = NamePos[0];
        NameTexts[Index]->HomeOffset[1] = NamePos[1];
        NameTexts[Index]->HomeOffset[2] = NamePos[2];
        NameTexts[Index]->SetProperty(0x24, 1, 0);
        static_cast<NnTextControl*>(NameTexts[Index])->SetFontStringField08(1); // FUN_0058b770
    }

    // 0x004cf866..0x004cf904: the row icons.
    float const IconHalf[3] = {0.006f, 0.006f, 0.0f};    // 0x3bc49ba6
    for (int Index = 0; Index < 8; ++Index)
    {
        char ControlName[0x100];
        _snprintf(ControlName, 0x100, "ContactListIcon%d", Index);
        float const IconPos[3] = {
            -0.17f,
            static_cast<float>(static_cast<double>(kIconRowTop) -
                               static_cast<double>(Index) * static_cast<double>(kRowStep)),
            0.0f};
        Icons[Index] = CreateButtonControl(ControlName, "Interface\\hud\\HUD_contactwaypoint.3db",
                                           nullptr, "ui_select_item", nullptr, IconPos, nullptr,
                                           IconHalf, 0, 1, 0, 0, 1, 0, 0);
    }

    // 0x004cf90a..0x004cfb25: the five filter buttons.
    float const FilterHalf[3] = {0.014f, 0.014f, 0.0f};  // 0x3c656042
    float const ImportantPos[3] = {-0.128f, 0.024f, 0.0f};
    ImportantButton = CreateButtonControl("ImportantFilterButton",
                                          "interface\\HUD\\HUD_contactimportant.3db", nullptr,
                                          "ui_filter_select", nullptr, ImportantPos, nullptr,
                                          FilterHalf, 0, 1, 0, 0, 0x623, 0, 0);
    float const ShipsPos[3] = {-0.094f, 0.024f, 0.0f};
    ShipsButton = CreateButtonControl("ShipsFilterButton", "interface\\HUD\\HUD_contactship.3db",
                                      nullptr, "ui_filter_select", nullptr, ShipsPos, nullptr,
                                      FilterHalf, 0, 1, 0, 0, 0x4fc, 0, 0);
    float const SolarPos[3] = {-0.06f, 0.024f, 0.0f};
    SolarButton = CreateButtonControl("SolarFilterButton",
                                      "interface\\HUD\\HUD_contactstation.3db", nullptr,
                                      "ui_filter_select", nullptr, SolarPos, nullptr, FilterHalf,
                                      0, 1, 0, 0, 0x4fd, 0, 0);
    float const LootPos[3] = {-0.026f, 0.024f, 0.0f};
    LootButton = CreateButtonControl("LootFilterButton", "interface\\HUD\\HUD_contactloot.3db",
                                     nullptr, "ui_filter_select", nullptr, LootPos, nullptr,
                                     FilterHalf, 0, 1, 0, 0, 0x520, 0, 0);
    float const AllPos[3] = {0.008f, 0.024f, 0.0f};
    AllButton = CreateButtonControl("AllFilterButton", "interface\\HUD\\HUD_contactall.3db",
                                    nullptr, "ui_filter_select", nullptr, AllPos, nullptr,
                                    FilterHalf, 0, 1, 0, 0, 0x624, 0, 0);

    // 0x004cfb2b..0x004cfb41: the "all" button is hidden in single player.
    AllButton->SetProperty(1, SinglePlayer() ? 1u : 0u, 0);

    // 0x004cfb47..0x004cfb85: the scroll bar (range 0..5, page 8, vertical).
    float const ScrollPos3[3] = {-0.267f, -0.234f, -1.0f};
    Scroll = CreateUIScroll("ContactListScroll", ScrollPos3, 0.016f, 0.118f, 0, 5, 8, 0);

    // 0x004cfb8b..0x004cfbd1.
    SetOffset(-0.323f, -0.241f, -1.0f);                    // slot 31
    SetPosition(Position);                                 // slot 5 with +0x60
    SetHiddenWithChildren(true);                           // FUN_005a0c30(1)

    // 0x004cfbd6..0x004cfc00: previous / next contact.
    RegisterEvent(0x1d);
    RegisterEvent(0x1e);

    // 0x004cfc02..0x004cfdbe: the montage tile of each icon mesh and of the chat icon.
    for (int Kind = 0; Kind < 16; ++Kind)
    {
        Tiles[Kind] = FindButtonMontageTile(std::string(kContactIconPaths[Kind]));
    }
    ChatTile = FindButtonMontageTile(std::string(kContactChatIconPath));

    // 0x004cfdcf: Win3D::Create FUN_004b5060, its result folded to a bool.
    return Win3D::Create(NewParent, Data);
}

void HUD_ContactList::Render(FSpaceCamera* /*Camera*/, float /*DeltaSeconds*/)
{
    // FUN_004d0170 (this = the HUDItem face).
    int const Mode = static_cast<HudTarget*>(G_HudTargetPanel)->GetMode(false); // FUN_004e5470
    if (Mode != 0 && Mode != 2)
    {
        LastPanelMode = -1;
        Flags = static_cast<unsigned char>(Flags & 0xfc);
        return;
    }
    Flags = static_cast<unsigned char>(Flags | 3);
    if (Mode == LastPanelMode)
    {
        return;
    }
    LastPanelMode = Mode;
    bDirty = 1;
    if (Mode == 2)
    {
        // 0x004d01d4..0x004d0208: the formation list -- no filter buttons, the title shown.
        ImportantButton->Flags = static_cast<unsigned char>(ImportantButton->Flags & 0xfc);
        ShipsButton->Flags = static_cast<unsigned char>(ShipsButton->Flags & 0xfc);
        SolarButton->Flags = static_cast<unsigned char>(SolarButton->Flags & 0xfc);
        AllButton->Flags = static_cast<unsigned char>(AllButton->Flags & 0xfc);
        LootButton->Flags = static_cast<unsigned char>(LootButton->Flags & 0xfc);
        FormationTitle->Flags = static_cast<unsigned char>(FormationTitle->Flags | 3);
        return;
    }
    // 0x004d0210..0x004d023d.
    ImportantButton->Flags = static_cast<unsigned char>(ImportantButton->Flags | 3);
    ShipsButton->Flags = static_cast<unsigned char>(ShipsButton->Flags | 3);
    SolarButton->Flags = static_cast<unsigned char>(SolarButton->Flags | 3);
    LootButton->Flags = static_cast<unsigned char>(LootButton->Flags | 3);
    AllButton->Flags = static_cast<unsigned char>(AllButton->Flags | 3);
    FormationTitle->Flags = static_cast<unsigned char>(FormationTitle->Flags & 0xfc);
}

void HUD_ContactList::Update()
{
    // LAB_004d3db0 (this = the WinControl face): tail jump FUN_005a17b0.
    if (!HudIsMousePickBlocked())                // FUN_0045a740
    {
        Win3D::Update();
    }
}

void HUD_ContactList::UpdateInactive()
{
    // LAB_004d3dd0 (this = the WinControl face).
    int const Mode = static_cast<HudTarget*>(G_HudTargetPanel)->GetMode(false);
    if ((Mode == 0 || Mode == 2) && GetClientMouseCursorDrawFlag() != 0 &&
        GetPlayerObjRW() != nullptr && !IsLetterboxVisible())
    {
        SetHiddenWithChildren(false);            // FUN_005a0c30(0)
    }
    // 0x004d3e0d..0x004d3e26: LAB_004682f0 inlined -- slot 12 on every child whose bit0 is
    // clear.
    Win3D::UpdateInactive();
}

intptr_t HUD_ContactList::SetProperty(int Msg, uintptr_t A, uintptr_t B)
{
    // LAB_004cfdf0 (this = the WinControl face). Nothing goes on to WinControl's handler.
    if (Msg == 0x26)
    {
        // 0x004cfe8e..0x004cff8e: a click -- a filter button, or a row.
        if (HudIsMousePickBlocked())
        {
            return 0;
        }
        if (A == reinterpret_cast<uintptr_t>(ImportantButton))
        {
            FilterMode = 0;
            bDirty = 1;
            return 1;
        }
        if (A == reinterpret_cast<uintptr_t>(ShipsButton))
        {
            FilterMode = 1;
            bDirty = 1;
            return 1;
        }
        if (A == reinterpret_cast<uintptr_t>(SolarButton))
        {
            FilterMode = 2;
            bDirty = 1;
            return 1;
        }
        if (A == reinterpret_cast<uintptr_t>(AllButton))
        {
            FilterMode = 4;
            bDirty = 1;
            return 1;
        }
        if (A == reinterpret_cast<uintptr_t>(LootButton))
        {
            FilterMode = 3;
            bDirty = 1;
            return 1;
        }
        for (int Row = 0; Row < 8; ++Row)
        {
            if (A == reinterpret_cast<uintptr_t>(Icons[Row]) ||
                A == reinterpret_cast<uintptr_t>(RangeTexts[Row]) ||
                A == reinterpret_cast<uintptr_t>(NameTexts[Row]))
            {
                SelectEntry(static_cast<int>(ScrollPos) + Row);
                PlayOrRewindUiSoundSlot(0);      // FUN_005645c0(0)
                return 1;
            }
        }
        return 0;
    }
    if (Msg == 0x27)
    {
        // 0x004cfe2d..0x004cfe8b: a double click on a row -- select it and open the target
        // view (HUD_Target mode 1). No pick test here.
        for (int Row = 0; Row < 8; ++Row)
        {
            if (A == reinterpret_cast<uintptr_t>(Icons[Row]) ||
                A == reinterpret_cast<uintptr_t>(RangeTexts[Row]) ||
                A == reinterpret_cast<uintptr_t>(NameTexts[Row]))
            {
                SelectEntry(static_cast<int>(ScrollPos) + Row);
                static_cast<HudTarget*>(G_HudTargetPanel)->SetMode(1); // FUN_004e5460
                PlayOrRewindUiSoundSlot(0);
                return 1;
            }
        }
        return 0;
    }
    if (Msg == 0x3e)
    {
        // 0x004cfe08..0x004cfe21: the scroll bar moved.
        if (!HudIsMousePickBlocked() && A == reinterpret_cast<uintptr_t>(Scroll))
        {
            ScrollPos = static_cast<unsigned>(B);
        }
        return 0;
    }
    return 0;
}

void HUD_ContactList::SelectEntry(int Index)
{
    // FUN_004cffa0 (this = the HUDItem face).
    int const Count = static_cast<int>(Contacts.size());
    if (Count <= 0 || Index >= Count)
    {
        return;
    }
    std::list<FHudContactEntry>::iterator It = Contacts.begin();
    while (Index != 0)
    {
        ++It;
        --Index;
        if (It == Contacts.end())
        {
            return;
        }
    }
    FHudContactEntry& Entry = *It;
    if (Entry.Kind == 4)
    {
        // 0x004cfff5..0x004d006c: the body of FUN_004ceeb0 inlined, on the entry's index.
        HudWaypointSelectPlayerWaypoint(Entry.Index);
        return;
    }
    if (Entry.Kind == 3)
    {
        // 0x004d0078..0x004d0114: the body of FUN_004cef30 inlined.
        HudWaypointSelectBestPath(Entry.Index);
        return;
    }
    // 0x004d0117..0x004d0163: target the object, unless it is the player.
    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW());
    if (Player != nullptr && Player->get_id() == Entry.Id)
    {
        return;
    }
    SendSelectTarget(Entry.Id);
}

bool HUD_ContactList::OnEvent(unsigned MsgId)
{
    // LAB_004d3e30 (this = the handler face).
    if (HudIsMousePickBlocked())
    {
        return false;
    }
    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW());
    if (Player == nullptr || static_cast<FClientGameobj*>(Player)->ManagerKind != 1)
    {
        return false;
    }
    CShip const* const Ship = AsShip(InlineCObjectOf(Player));
    if (Ship == nullptr)
    {
        return false;
    }
    int const Count = static_cast<int>(Contacts.size());
    if (Count == 0)
    {
        return false;
    }

    // 0x004d3ea0..0x004d3ee5: the row the player's target is on.
    IObjRW* const Target = Ship->get_target();
    if (Target != nullptr)
    {
        unsigned const TargetId = Target->get_id();
        int Found = 0;
        std::list<FHudContactEntry>::iterator It = Contacts.begin();
        for (; It != Contacts.end(); ++It, ++Found)
        {
            if (IsEntryTarget(&*It, TargetId, true))
            {
                break;
            }
        }
        if (It != Contacts.end())
        {
            if (MsgId == 0x1e)
            {
                // 0x004d3f16..0x004d3f69: the previous row, wrapping; a row that is the player
                // is stepped over once more.
                int Row = Found - 1;
                if (Row < 0)
                {
                    Row = Count - 1;
                }
                std::list<FHudContactEntry>::iterator Walk = Contacts.begin();
                for (int Step = Row; Step != 0 && Walk != Contacts.end(); --Step)
                {
                    ++Walk;
                }
                if (Walk->Id == Player->get_id())
                {
                    --Row;
                    if (Row < 0)
                    {
                        Row = Count - 1;
                    }
                }
                SelectEntry(Row);
                return true;
            }
            if (MsgId == 0x1d)
            {
                // 0x004d3f71..0x004d3fc4: the next row, the same way.
                int Row = Found + 1;
                if (Row >= Count)
                {
                    Row = 0;
                }
                std::list<FHudContactEntry>::iterator Walk = Contacts.begin();
                for (int Step = Row; Step != 0 && Walk != Contacts.end(); --Step)
                {
                    ++Walk;
                }
                if (Walk->Id == Player->get_id())
                {
                    ++Row;
                    if (Row >= Count)
                    {
                        Row = 0;
                    }
                }
                SelectEntry(Row);
                return true;
            }
            return false;
        }
    }

    // 0x004d3ee7..0x004d3efc: no target, or not listed -- the first row.
    SelectEntry(0);
    return true;
}

int HUD_ContactList::ClassifyContact(CSimple const* Object)
{
    // FUN_004d3fd0.
    if (Object == nullptr)
    {
        return -1;
    }
    unsigned const TypeBits = Object->TypeBits;
    if (IsClass(Object, 0x503u))
    {
        CShip const* const Ship = static_cast<CShip const*>(Object);
        if (Ship->bIsPlayer != 0)
        {
            return OtherPlayerKind(Ship);
        }
        if ((TypeBits & 0x420000u) != 0)
        {
            return 8;
        }
        if (IsNomadEqObj(Object))
        {
            return 0xa;
        }
        if ((TypeBits & 0x100000u) != 0)
        {
            return 9;
        }
        return (TypeBits & 0x200000u) != 0 ? 5 : 0;
    }
    if ((TypeBits & 0x80u) != 0)
    {
        return 7;
    }
    if ((TypeBits & 3u) != 0)
    {
        return 1;
    }
    if ((TypeBits & 0xc40u) != 0)
    {
        return 6;
    }
    if ((TypeBits & 0x600de0u) != 0)
    {
        return 2;
    }
    if ((TypeBits & 0x9010u) != 0)
    {
        return 0xc;
    }
    if ((TypeBits & 0x2000u) != 0)
    {
        return 0xd;
    }
    return (TypeBits & 0x10000000u) != 0 ? 0xb : -1;
}

void HUD_ContactList::AddShipEntry(CShip const* Ship, FClientGameobj* Gameobj)
{
    // FUN_004d0250.
    unsigned const TypeBits = Ship->TypeBits;
    if (Gameobj == nullptr)
    {
        return;
    }
    int Kind = 0;
    if (Ship->bIsPlayer != 0)
    {
        Kind = OtherPlayerKind(Ship);
    }
    else if ((TypeBits & 0x420000u) != 0)
    {
        Kind = 8;
    }
    else if (IsNomadEqObj(Ship))
    {
        Kind = 0xa;
    }
    else if ((TypeBits & 0x100000u) != 0)
    {
        Kind = 9;
    }
    else if ((TypeBits & 0x200000u) != 0)
    {
        Kind = 5;
    }
    Contacts.push_back(FHudContactEntry(Kind, Gameobj, 0.0f, 0, 0));
}

void HUD_ContactList::RebuildContacts()
{
    // FUN_004d0490 (this = the HUDItem face).
    if (ShouldSkipLocalServerTick())                 // FUN_0042d740
    {
        return;
    }
    Contacts.clear();                                // FUN_004d47a0(begin, end)

    // 0x004d04e5..0x004d051f: a player ship is needed.
    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW());
    if (Player == nullptr)
    {
        return;
    }
    CShip const* const PlayerShip = AsShip(InlineCObjectOf(Player));
    if (PlayerShip == nullptr)
    {
        return;
    }

    // 0x004d0525..0x004d0566.
    G_ContactPlayerPos = Player->get_position();     // slot 0
    Vector const PlayerPos = G_ContactPlayerPos;
    G_ContactTargetId = 0;
    G_bContactTargetListed = 0;
    unsigned ExcludeId = 0;

    // 0x004d056a..0x004d05cd: with the "important" / "all" filter, the target's id and the id of
    // the first best-path point's object (which is left out of the list).
    if (FilterMode == 0 || FilterMode == 4)
    {
        IObjRW* const Target = PlayerShip->get_target();
        if (Target != nullptr)
        {
            CSimple const* const TargetSimple = AsSimple(Target->cobject());
            G_ContactTargetId = TargetSimple->Id;    // no test of the narrowing, as retail
        }
        if (BestPathGetWaypointCount() > 0)
        {
            FBestPathWaypoint* const First = BestPathGetWaypoint(0);
            if (First != nullptr)
            {
                IObjRW* const Resolved = BestPathWaypointResolveObject(First);
                if (Resolved != nullptr)
                {
                    ExcludeId = Resolved->get_id();
                }
            }
        }
    }

    // 0x004d05d1..0x004d061c: the scanner's range; under 2500 it limits the search.
    float Range = kDefaultRadarRange;
    if (Scanner != nullptr)
    {
        Range = static_cast<CEScanner const*>(Scanner)->GetRadarRange();
    }
    bool bLimited = false;
    float RangeLimit = kUnlimitedRange;
    if (Range < kDefaultRadarRange)                  // TEST AH,5 / JP
    {
        RangeLimit = Range;
        bLimited = true;
    }
    float const RangeSq = static_cast<float>(static_cast<double>(Range) * Range);

    int const PanelMode = static_cast<HudTarget*>(G_HudTargetPanel)->GetMode(false);
    if (PanelMode == 2)
    {
        // 0x004d062c..0x004d07dd: the formation list.
        if (SinglePlayer())
        {
            // The ship's formation leader, then the leader's followers.
            IObjRW* const Leader =
                ObjRwFromWatchable(const_cast<Watchable*>(PlayerShip->FormationLeader.target()));
            if (Leader != nullptr)
            {
                CShip const* const LeaderShip = AsShip(Leader->cobject());
                if (LeaderShip != nullptr)
                {
                    AddShipEntry(LeaderShip, ClientObjDb_FindObject(Leader->get_id(), 1));
                    // VC6 size(): _First == 0 ? 0 : (_Last - _First) / 8.
                    int const FollowerCount = static_cast<int>(LeaderShip->FormationFollowers.size());
                    for (int Follower = 0; Follower < FollowerCount; ++Follower)
                    {
                        // The 8-byte watchers of the follower array (CShip's FObjRWWatcher).
                        IObjRW* const FollowerRw = LeaderShip->FormationFollowers[Follower].get();
                        if (FollowerRw == nullptr)
                        {
                            continue;
                        }
                        FClientGameobj* const Gameobj =
                            ClientObjDb_FindObject(FollowerRw->get_id(), 1);
                        if (Gameobj == nullptr)
                        {
                            continue;
                        }
                        CShip const* const FollowerShip = AsShip(Gameobj->cobject());
                        if (FollowerShip != nullptr)
                        {
                            AddShipEntry(FollowerShip, Gameobj);
                        }
                    }
                }
            }
        }
        else
        {
            // Multiplayer: the group member ids (FUN_004c5890 / FUN_004c58b0). Without a
            // formation leader the list is shown only when the player heads the group.
            int const MemberCount = static_cast<int>(RetailU32QueueSize());
            bool bShow = true;
            if (ObjRwFromWatchable(const_cast<Watchable*>(PlayerShip->FormationLeader.target())) ==
                nullptr)
            {
                bShow = MemberCount != 0 && RetailU32QueueAt(0) == PlayerShip->Id;
            }
            if (bShow)
            {
                for (int Member = 0; Member < MemberCount; ++Member)
                {
                    unsigned const MemberId = RetailU32QueueAt(static_cast<unsigned>(Member));
                    if (MemberId == 0)
                    {
                        continue;
                    }
                    FClientGameobj* const Gameobj = ClientObjDb_FindObject(MemberId, 1);
                    if (Gameobj == nullptr)
                    {
                        continue;
                    }
                    CShip const* const MemberShip = AsShip(Gameobj->cobject());
                    if (MemberShip != nullptr)
                    {
                        AddShipEntry(MemberShip, Gameobj);
                    }
                }
            }
        }
    }
    else
    {
        // 0x004d07e2..0x004d0e80: ships (filters important, ships, all).
        if (FilterMode == 0 || FilterMode == 1 || FilterMode == 4)
        {
            int const ShipKind = 1;
            for (FClientGameobj* Gameobj = ClientObjDb_GetKindHead(&ShipKind); Gameobj != nullptr;
                 Gameobj = Gameobj->ListNext)
            {
                CShip const* const Ship = AsShip(Gameobj->cobject());
                if (Ship == nullptr)
                {
                    continue;
                }
                // TEST AH,1 / JZ: only a cloak factor at or above 1 (ordered) is skipped.
                if (Ship->CloakPercent >= 1.0f)
                {
                    continue;
                }
                if (Ship->Id == ExcludeId)
                {
                    continue;
                }
                // 0x004d0870..0x004d08db: within scanner range (y, z, x squared, FUN_00422b80's
                // float differences), or else another player's ship at any range.
                float const Dx = static_cast<float>(static_cast<double>(Ship->WorldPosition.x) - PlayerPos.x);
                float const Dy = static_cast<float>(static_cast<double>(Ship->WorldPosition.y) - PlayerPos.y);
                float const Dz = static_cast<float>(static_cast<double>(Ship->WorldPosition.z) - PlayerPos.z);
                IObjRW* const GameobjRw = Gameobj;
                bool bInRange = false;
                if (GameobjRw != Player)
                {
                    double const DistSq = static_cast<double>(Dy) * Dy + static_cast<double>(Dz) * Dz +
                                          static_cast<double>(Dx) * Dx;
                    bInRange = DistSq < RangeSq;
                }
                if (!bInRange && (Ship->bIsPlayer == 0 || GameobjRw == Player))
                {
                    continue;
                }

                int const Attitude = Gameobj->GetCachedAttitude();   // FUN_0053a960
                if (Ship->bIsPlayer == 0)
                {
                    // 0x004d08f4..0x004d0920: an NPC needs a faction.
                    int const Vibe = static_cast<int>(Ship->VibeId);
                    unsigned Group = 0;
                    if (Reputation::Vibe::GetAffiliation(Vibe, Group, false) != 0 ||
                        Group == 0xffffffffu)
                    {
                        continue;
                    }
                }
                unsigned const TypeBits = Ship->TypeBits;
                if (Ship->bIsPlayer != 0)
                {
                    // 0x004d093a..0x004d0a3b: other players, with "important" / "all" only.
                    if (FilterMode != 0 && FilterMode != 4)
                    {
                        continue;
                    }
                    Contacts.push_back(FHudContactEntry(OtherPlayerKind(Ship), GameobjRw, 0.0f, 0, 0));
                    continue;
                }
                int Kind = 0;
                if ((TypeBits & 0x420000u) != 0)
                {
                    Kind = 8;
                }
                else if ((TypeBits & 0x100000u) != 0)
                {
                    Kind = 9;
                }
                else if ((TypeBits & 0x200000u) != 0)
                {
                    Kind = 5;
                }
                else if (IsNomadEqObj(Ship))
                {
                    Kind = 0xa;
                }
                // 0x004d0a4b / 0x004d0aa6 (and the same pair per kind): a hostile always, any
                // other attitude with the "ships" or "all" filter.
                if (Attitude == -1 || FilterMode == 1 || FilterMode == 4)
                {
                    Contacts.push_back(FHudContactEntry(Kind, GameobjRw, 0.0f, 0, 0));
                }
            }
        }

        // 0x004d0e86..0x004d0ffc: loot (filters loot, all).
        if (FilterMode == 3 || FilterMode == 4)
        {
            int const LootKind = 7;
            for (FClientGameobj* Gameobj = ClientObjDb_GetKindHead(&LootKind); Gameobj != nullptr;
                 Gameobj = Gameobj->ListNext)
            {
                if (Gameobj->get_id() == ExcludeId)
                {
                    continue;
                }
                if (bLimited)
                {
                    // |distance to the physical sphere| against the scanner range (x, y, z).
                    Vector Center;
                    float const Radius = Gameobj->cobject()->get_physical_radius_r(Center);
                    float const Dx = static_cast<float>(static_cast<double>(Center.x) - PlayerPos.x);
                    float const Dy = static_cast<float>(static_cast<double>(Center.y) - PlayerPos.y);
                    float const Dz = static_cast<float>(static_cast<double>(Center.z) - PlayerPos.z);
                    double const Gap = fabs(sqrt(static_cast<double>(Dx) * Dx + static_cast<double>(Dy) * Dy +
                                                 static_cast<double>(Dz) * Dz) - Radius);
                    if (Gap > RangeLimit)            // TEST AH,0x41 / JZ
                    {
                        continue;
                    }
                }
                else
                {
                    Vector const& Pos = Gameobj->get_position();
                    float const Dx = static_cast<float>(static_cast<double>(Pos.x) - PlayerPos.x);
                    float const Dy = static_cast<float>(static_cast<double>(Pos.y) - PlayerPos.y);
                    float const Dz = static_cast<float>(static_cast<double>(Pos.z) - PlayerPos.z);
                    double const DistSq = static_cast<double>(Dx) * Dx + static_cast<double>(Dy) * Dy +
                                          static_cast<double>(Dz) * Dz;
                    if (DistSq > kNearRangeSq)
                    {
                        continue;
                    }
                }
                Contacts.push_back(FHudContactEntry(0xb, Gameobj, 0.0f, 0, 0));
            }
        }

        // 0x004d1002..0x004d1ee0: solars and everything else near (every filter but "ships").
        if (FilterMode == 0 || FilterMode == 2 || FilterMode == 4 || FilterMode == 3)
        {
            bool const bSolarsShown = FilterMode == 2 || FilterMode == 4;

            // 0x004d1023..0x004d16e8: the large solars (FUN_00540a00 / FUN_00540a20).
            size_t LargeIt = 0;
            for (FClientGameobj* Gameobj = ClientObjDb_LargeSolarFirst(&LargeIt); Gameobj != nullptr;
                 Gameobj = ClientObjDb_LargeSolarNext(&LargeIt))
            {
                CObject* const Object = InlineCObjectOf(Gameobj);
                CEqObj const* EqObj = nullptr;
                CSolar const* Solar = nullptr;
                if (Object != nullptr && IsClass(Object, 0x103u))
                {
                    EqObj = static_cast<CEqObj const*>(Object);
                    if (EqObj->CloakPercent >= 1.0f)
                    {
                        continue;
                    }
                    if (IsClass(Object, 0x303u))
                    {
                        Solar = static_cast<CSolar const*>(Object);
                        if (!Solar->GetParentNickname().IsEmpty())
                        {
                            continue;
                        }
                    }
                }
                // 0x004d10a2: the id is read through the solar pointer, null for a large entry
                // that is not a solar (retail faults there too).
                if (Solar->Id == ExcludeId)
                {
                    continue;
                }
                unsigned char bNear = 0;
                if (bLimited)
                {
                    Vector Center;
                    float const Radius = Gameobj->cobject()->get_physical_radius_r(Center);
                    float const Dx = static_cast<float>(static_cast<double>(Center.x) - PlayerPos.x);
                    float const Dy = static_cast<float>(static_cast<double>(Center.y) - PlayerPos.y);
                    float const Dz = static_cast<float>(static_cast<double>(Center.z) - PlayerPos.z);
                    double const Gap = fabs(sqrt(static_cast<double>(Dx) * Dx + static_cast<double>(Dy) * Dy +
                                                 static_cast<double>(Dz) * Dz) - Radius);
                    if (Gap > RangeLimit)
                    {
                        continue;
                    }
                }
                int const Attitude = Gameobj->GetCachedAttitude();
                float const Dx = static_cast<float>(static_cast<double>(EqObj->WorldPosition.x) - PlayerPos.x);
                float const Dy = static_cast<float>(static_cast<double>(EqObj->WorldPosition.y) - PlayerPos.y);
                float const Dz = static_cast<float>(static_cast<double>(EqObj->WorldPosition.z) - PlayerPos.z);
                double const DistSq = static_cast<double>(Dx) * Dx + static_cast<double>(Dy) * Dy +
                                      static_cast<double>(Dz) * Dz;
                // 0x004d115b..0x004d11be: a hostile close by is shown under "important".
                if (Attitude == -1 && FilterMode == 0 && DistSq < kNearRangeSq)
                {
                    bNear = 1;
                }
                bool const bShown = bSolarsShown || bNear != 0;
                unsigned const TypeBits = EqObj->TypeBits;
                if ((TypeBits & 0x600de3u) != 0)
                {
                    if (HudIsUnvisitedSolar(EqObj))  // FUN_004d4c70
                    {
                        continue;
                    }
                    if ((TypeBits & 0x80u) != 0)
                    {
                        if (!(DistSq < kNearRangeSq) || !bShown)
                        {
                            continue;
                        }
                        Contacts.push_back(FHudContactEntry(7, Gameobj, 0.0f, 0, 0));
                    }
                    else if ((TypeBits & 3u) != 0)
                    {
                        if (bShown)
                        {
                            Contacts.push_back(FHudContactEntry(1, Gameobj, 0.0f, 0, 0));
                        }
                    }
                    else if ((TypeBits & 0x200000u) != 0)
                    {
                        continue;
                    }
                    else if ((TypeBits & 0xc40u) != 0)
                    {
                        if (bShown)
                        {
                            Contacts.push_back(FHudContactEntry(6, Gameobj, 0.0f, 0, 0));
                        }
                    }
                    else if (bShown)
                    {
                        Contacts.push_back(FHudContactEntry(2, Gameobj, 0.0f, 0, 0));
                    }
                    continue;
                }
                if (!(DistSq < kNearRangeSq))
                {
                    continue;
                }
                if (IsNomadEqObj(EqObj) && FilterMode != 3)
                {
                    if (Attitude == -1 || bSolarsShown)
                    {
                        Contacts.push_back(FHudContactEntry(0xa, Gameobj, 0.0f, 0, 0));
                    }
                    continue;
                }
                if ((TypeBits & 0x9010u) != 0)
                {
                    if (!HudIsUnvisitedSolar(EqObj) && bShown)
                    {
                        Contacts.push_back(FHudContactEntry(0xc, Gameobj, 0.0f, 0, 0));
                    }
                    continue;
                }
                if ((TypeBits & 0x2000u) != 0)
                {
                    if (!HudIsUnvisitedSolar(EqObj) && bShown)
                    {
                        Contacts.push_back(FHudContactEntry(0xd, Gameobj, 0.0f, 0, 0));
                    }
                }
            }

            // 0x004d16ee..0x004d1eda: everything within 14000 of the player in the object DB's
            // spatial bucket (slot 0xc, mask 0xfdff), large solars left out.
            ISpatialPartition* const Spatial =
                static_cast<ISpatialPartition*>(GetClientObjDb()->GetBucket(0xfdff));
            Geometry::Sphere Query;
            Query.Center = PlayerPos;
            Query.Radius = kSpatialQueryRadius;
            FSpatialQueryResult Result;              // retail: 0x2008 bytes of its stack frame
            Result.Capacity = 0x800;
            Result.Count = 0;
            if (SpatialFill(Spatial, &Query, &Result) != 0 && Result.Count != 0)
            {
                for (unsigned Item = 0; Item < Result.Count; ++Item)
                {
                    FClientGameobj* const Gameobj = static_cast<FClientGameobj*>(Result.Items[Item]);
                    if (IsLargeSolarCandidate(Gameobj))
                    {
                        continue;
                    }
                    if (Gameobj->get_id() == ExcludeId)
                    {
                        continue;
                    }
                    CObject const* const Object = Gameobj->cobject();
                    CEqObj const* EqObj = nullptr;
                    if (Object != nullptr && IsClass(Object, 0x103u))
                    {
                        if (static_cast<CEqObj const*>(Object)->CloakPercent >= 1.0f)
                        {
                            continue;
                        }
                        EqObj = static_cast<CEqObj const*>(Object);
                    }
                    CSimple const* const Simple = AsSimple(Gameobj->cobject());
                    if (Simple == nullptr)
                    {
                        continue;
                    }
                    if (EqObj != nullptr && IsClass(EqObj, 0x303u) &&
                        !static_cast<CSolar const*>(EqObj)->GetParentNickname().IsEmpty())
                    {
                        continue;
                    }
                    if (bLimited)
                    {
                        // The differences stay on the FPU stack here (no float stores).
                        Vector Center;
                        float const Radius = Gameobj->cobject()->get_physical_radius_r(Center);
                        double const Dx = static_cast<double>(Center.x) - PlayerPos.x;
                        double const Dy = static_cast<double>(Center.y) - PlayerPos.y;
                        double const Dz = static_cast<double>(Center.z) - PlayerPos.z;
                        double const Gap = fabs(sqrt(Dx * Dx + Dy * Dy + Dz * Dz) - Radius);
                        if (Gap > RangeLimit)
                        {
                            continue;
                        }
                    }
                    int const Attitude = Gameobj->GetCachedAttitude();
                    double const Dx = static_cast<double>(Simple->WorldPosition.x) - PlayerPos.x;
                    double const Dy = static_cast<double>(Simple->WorldPosition.y) - PlayerPos.y;
                    double const Dz = static_cast<double>(Simple->WorldPosition.z) - PlayerPos.z;
                    double const DistSq = Dx * Dx + Dy * Dy + Dz * Dz;
                    unsigned char bNear = 0;
                    if (Attitude == -1 && FilterMode == 0 && DistSq < kNearRangeSq)
                    {
                        bNear = 1;
                    }
                    bool const bShown = bSolarsShown || bNear != 0;
                    unsigned const TypeBits = Simple->TypeBits;
                    if ((TypeBits & 0x600de3u) != 0)
                    {
                        if (HudIsUnvisitedSolar(Simple))
                        {
                            continue;
                        }
                        if ((TypeBits & 0x80u) != 0)
                        {
                            if (!(DistSq < kNearRangeSq) || !bShown)
                            {
                                continue;
                            }
                            Contacts.push_back(FHudContactEntry(7, Gameobj, 0.0f, 0, 0));
                        }
                        else if ((TypeBits & 3u) != 0)
                        {
                            if (bShown)
                            {
                                Contacts.push_back(FHudContactEntry(1, Gameobj, 0.0f, 0, 0));
                            }
                        }
                        else if ((TypeBits & 0x200000u) != 0)
                        {
                            continue;
                        }
                        else if ((TypeBits & 0xc40u) != 0)
                        {
                            if (bShown)
                            {
                                Contacts.push_back(FHudContactEntry(6, Gameobj, 0.0f, 0, 0));
                            }
                        }
                        else if (bShown)
                        {
                            Contacts.push_back(FHudContactEntry(2, Gameobj, 0.0f, 0, 0));
                        }
                        continue;
                    }
                    if (!(DistSq < kNearRangeSq))
                    {
                        continue;
                    }
                    if (IsNomadEqObj(Simple) && FilterMode != 3)
                    {
                        if (Attitude == -1 || bSolarsShown)
                        {
                            Contacts.push_back(FHudContactEntry(0xa, Gameobj, 0.0f, 0, 0));
                        }
                        continue;
                    }
                    if ((TypeBits & 0x9010u) != 0)
                    {
                        if (!HudIsUnvisitedSolar(Simple) && bShown)
                        {
                            Contacts.push_back(FHudContactEntry(0xc, Gameobj, 0.0f, 0, 0));
                        }
                        continue;
                    }
                    if ((TypeBits & 0x2000u) != 0)
                    {
                        // 0x004d1e33..0x004d1e49: here the loot and "all" filters show it (the
                        // large-solar pass above uses solars / all).
                        if (!HudIsUnvisitedSolar(Simple) &&
                            (FilterMode == 3 || FilterMode == 4 || bNear != 0))
                        {
                            Contacts.push_back(FHudContactEntry(0xd, Gameobj, 0.0f, 0, 0));
                        }
                    }
                }
            }
        }

        // 0x004d1ee0..0x004d20fc: the waypoints (filters important, all).
        if (FilterMode == 0 || FilterMode == 4)
        {
            // The first player waypoint in the current system.
            int const WaypointCount = GetPlayerWaypointCountRetail();
            for (int Waypoint = 0; Waypoint < WaypointCount; ++Waypoint)
            {
                FPlayerWaypointRecord* const Record = GetPlayerWaypointRetail(Waypoint);
                if (Record != nullptr && Record->SystemId == GetPlayerStarSystemIdRetail())
                {
                    Contacts.push_back(FHudContactEntry(4, nullptr, 0.0f, 0, Waypoint));
                    break;
                }
            }
            // The first best-path point: on an object, or at a position in this system.
            int PathCount = BestPathGetWaypointCount();
            if (PathCount > 1)
            {
                PathCount = 1;
            }
            Universe::ISystem const* const System =
                Universe::get_system(GetPlayerStarSystemIdRetail());
            for (int Point = 0; Point < PathCount; ++Point)
            {
                FBestPathWaypoint* const Waypoint = BestPathGetWaypoint(Point);
                if (BestPathWaypointResolveObject(Waypoint) != nullptr)
                {
                    Contacts.push_back(FHudContactEntry(3, nullptr, 0.0f, 0, Point));
                    continue;
                }
                Vector Pos;
                unsigned PointSystem = 0;
                if (BestPathWaypointResolvePosition(Waypoint, &Pos, &PointSystem) &&
                    PointSystem == System->GetId())
                {
                    Contacts.push_back(FHudContactEntry(3, nullptr, 0.0f, 0, Point));
                }
            }
        }
    }

    // 0x004d2102..0x004d22f2: the client watch list (DAT_0066d33c) -- rows the filter allows;
    // one already listed only gets its watch-list byte.
    unsigned char bDeadWatcher = 0;
    std::list<BaseWatcher> const& WatchList = GameobjWatchList();
    for (std::list<BaseWatcher>::const_iterator WatchIt = WatchList.begin(); WatchIt != WatchList.end();
         ++WatchIt)
    {
        IObjRW* const Watched = ObjRwFromWatchable(const_cast<Watchable*>(WatchIt->target()));
        if (Watched == nullptr)
        {
            bDeadWatcher = 1;
            continue;
        }
        CSimple const* const Simple = AsSimple(Watched->cobject());
        if (Simple == nullptr || Simple->Id == ExcludeId)
        {
            continue;
        }
        int const Kind = ClassifyContact(Simple);
        if (Kind < 0)
        {
            continue;
        }
        bool bAllowed = true;
        if (FilterMode == 3)
        {
            bAllowed = Kind == 0xb;
        }
        else if (FilterMode == 1)
        {
            bAllowed = Kind == 0 || Kind == 5 || Kind == 8 || Kind == 9 || Kind == 0xa;
        }
        else if (FilterMode == 2)
        {
            bAllowed = Kind == 1 || Kind == 2 || Kind == 6 || Kind == 7 || Kind == 0xc ||
                       Kind == 0xd;
        }
        if (!bAllowed)
        {
            continue;
        }
        bool bListed = false;
        for (std::list<FHudContactEntry>::iterator It = Contacts.begin(); It != Contacts.end(); ++It)
        {
            if (It->Object() == Watched)
            {
                It->bWatchListed = 1;
                bListed = true;
                break;
            }
        }
        if (!bListed)
        {
            Contacts.push_back(FHudContactEntry(Kind, Watched, 0.0f, 0, 0));
            Contacts.back().bWatchListed = 1;
        }
    }
    if (bDeadWatcher != 0)
    {
        GameobjWatchListRemove(nullptr);             // FUN_0045a990(0)
    }

    // 0x004d22f5..0x004d2470: the player's target when nothing above listed it.
    if (static_cast<HudTarget*>(G_HudTargetPanel)->GetMode(false) != 2 &&
        (FilterMode == 0 || FilterMode == 4))
    {
        IObjRW* const Target = PlayerShip->get_target();
        if (Target != nullptr && G_bContactTargetListed == 0)
        {
            CSimple const* const Simple = AsSimple(Target->cobject());
            int const Kind = ClassifyContact(Simple);
            if (Kind >= 0)
            {
                FClientGameobj* const Gameobj = ClientObjDb_FindObject(Simple->Id, 0);
                if (Gameobj != nullptr)
                {
                    Contacts.push_back(FHudContactEntry(Kind, Gameobj, 0.0f, 0, 0));
                }
            }
        }
    }

    // 0x004d2475..0x004d2489: outside the formation list, nearest first. FUN_004d4340 is VC6
    // list::sort (merge through 0x10 bins, FUN_004d4820 moving a right element ahead only when
    // it is strictly nearer): a stable ascending sort, as std::list::sort is.
    if (static_cast<HudTarget*>(G_HudTargetPanel)->GetMode(false) != 2)
    {
        Contacts.sort([](FHudContactEntry const& Left, FHudContactEntry const& Right)
                      { return Left.Distance < Right.Distance; });
    }
}

void HUD_ContactList::Draw()
{
    // LAB_004d24b0 (this = the WinControl face).
    if (GetGameOption(kGameOptInterfaceGroup) == 0 || GetGameOption(kGameOptHud) == 0 ||
        GetClientMouseCursorDrawFlag() == 0 || GetPlayerObjRW() == nullptr || IsLetterboxVisible())
    {
        return;
    }
    if (G_bHudContactListRefreshRequested != 0)
    {
        bDirty = 1;
        G_bHudContactListRefreshRequested = 0;
    }

    // 0x004d2539..0x004d2557.
    NameRefreshTimer = static_cast<float>(GetClientFrameTimeCapped() + NameRefreshTimer);
    for (int Row = 0; Row < 8; ++Row)
    {
        RowLabels[Row] = nullptr;
    }

    // 0x004d2559..0x004d25f2: the selected filter button is highlighted.
    ImportantButton->SetProperty(0xc, FilterMode == 0 ? 1 : 0, 0);
    ShipsButton->SetProperty(0xc, FilterMode == 1 ? 1 : 0, 0);
    SolarButton->SetProperty(0xc, FilterMode == 2 ? 1 : 0, 0);
    AllButton->SetProperty(0xc, FilterMode == 4 ? 1 : 0, 0);
    LootButton->SetProperty(0xc, FilterMode == 3 ? 1 : 0, 0);

    // 0x004d25f8..0x004d2654: rebuild every 3 s (the sum kept unrounded for the compare and the
    // subtraction; TEST AH,1 / JZ rebuilds at or above 3, a NaN falls to the dirty test), or
    // when dirty.
    bool bRebuilt = false;
    double const Timer = GetClientFrameTimeCapped() + RebuildTimer;
    RebuildTimer = static_cast<float>(Timer);
    if (Timer >= kRebuildSeconds || bDirty != 0)
    {
        double const Left = Timer - kRebuildSeconds;
        RebuildTimer = static_cast<float>(Left);
        if (Left < 0.0)
        {
            RebuildTimer = 0.0f;
        }
        RebuildContacts();
        bDirty = 0;
        bRebuilt = true;
    }

    // 0x004d2656..0x004d2699: the player ship's target.
    unsigned PlayerTargetId = 0;
    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW());
    if (Player != nullptr)
    {
        CShip const* const Ship = AsShip(Player->cobject());
        if (Ship != nullptr)
        {
            IObjRW* const Target = Ship->get_target();
            if (Target != nullptr)
            {
                PlayerTargetId = Target->get_id();
            }
        }
    }

    // 0x004d269d..0x004d26eb: the first visible row, clamped (unsigned) to Count - 8.
    unsigned const Count = static_cast<unsigned>(Contacts.size());
    unsigned MaxTop = 0;
    if (Count > 8)
    {
        MaxTop = Count - 8;
    }
    if (ScrollPos > MaxTop)
    {
        ScrollPos = MaxTop;
    }
    std::list<FHudContactEntry>::iterator It = Contacts.begin();
    for (unsigned Step = 0; Step != ScrollPos; ++Step)
    {
        if (It == Contacts.end())
        {
            break;
        }
        ++It;
    }

    if (G_ContactListLastScroll == static_cast<int>(ScrollPos) && !bRebuilt)
    {
        // 0x004d2703..0x004d2ad0: the same rows as last time -- labels, icons and colours only.
        int Row = 0;
        while (It != Contacts.end())
        {
            FHudContactEntry& Entry = *It;
            IObjRW* const Obj = Entry.Object();
            if (Obj == nullptr)
            {
                if (Entry.Kind != 4 && Entry.Kind != 3)
                {
                    bDirty = 1;
                    ++It;
                    continue;
                }
            }
            else
            {
                FClientGameobj* const Gameobj = static_cast<FClientGameobj*>(Obj);
                if (Gameobj->GetFlag3IfSurfaceWidthUnchanged())   // FUN_0053a9f0
                {
                    RowLabels[Row] = Gameobj->GetFontString48();  // FUN_0053a9e0
                }
            }

            unsigned char const bTarget =
                IsEntryTarget(&Entry, PlayerTargetId, FilterMode != 4) ? 1 : 0;
            char const* Path = kContactIconPaths[Entry.Kind];
            FButtonMontageTile* Tile = Tiles[Entry.Kind];
            if (Obj != nullptr && HudCommIsWatchedObjectId(Obj->get_id()))
            {
                Tile = ChatTile;
                Path = kContactChatIconPath;
            }
            char MeshPath[0x100];
            Icons[Row]->SetProperty(0xb, reinterpret_cast<uintptr_t>(MeshPath), 0x100);
            if (strcmp(MeshPath, Path) != 0)
            {
                Icons[Row]->SetProperty(4, reinterpret_cast<uintptr_t>(Path), 0);
                static_cast<NnButtonControl*>(Icons[Row])->MontageTile = Tile;
            }

            // 0x004d2838..0x004d290b: waypoints violet, the rest by reputation; a white NPC
            // ship with no faction marks the list dirty and is not shown.
            unsigned long Colour = G_NnPaletteArgb_679bb0;
            if (Entry.Kind == 4 || Entry.Kind == 3)
            {
                Colour = G_NnPaletteArgb_679b80;
            }
            else
            {
                unsigned long RepColour = 0;
                HudReputationColor(&RepColour, Player, Entry.Object());   // FUN_0045a4e0
                Colour = RepColour;
                if ((Colour & 0xffffffu) == (G_NnPaletteArgb_679b84 & 0xffffffu))
                {
                    CShip const* const Ship = AsShip(Obj->cobject());
                    if (Ship != nullptr && Ship->bIsPlayer == 0)
                    {
                        int const Vibe = static_cast<int>(Ship->VibeId);
                        unsigned Group = 0;
                        if (Reputation::Vibe::GetAffiliation(Vibe, Group, false) != 0 ||
                            Group == 0xffffffffu)
                        {
                            bDirty = 1;
                            ++It;
                            continue;
                        }
                    }
                }
            }
            if (bTarget != 0)
            {
                Colour = G_NnPaletteArgb_679ba4;
            }
            else
            {
                float Ray[3];
                ContactCursorRay(Ray, false);
                if ((Icons[Row]->HitTest(Ray) || RangeTexts[Row]->HitTest(Ray) ||
                     NameTexts[Row]->HitTest(Ray)) &&
                    !HudIsMousePickBlocked())
                {
                    unsigned long Pulsed = 0;
                    NnUiPulseBlendFromArgb(&Pulsed, Colour);   // FUN_00561f20
                    Colour = Pulsed;
                }
            }
            Icons[Row]->SetProperty(0x33, reinterpret_cast<uintptr_t>(&Colour), 1);
            RangeTexts[Row]->SetProperty(0x33, reinterpret_cast<uintptr_t>(&Colour), 1);
            NameTexts[Row]->SetProperty(0x33, reinterpret_cast<uintptr_t>(&Colour), 1);
            ++Row;
            if (Row >= 8)
            {
                break;
            }
            ++It;
        }

        SwapRowLabels(this);
        WinControl::Draw();                          // FUN_005a0760
        SwapRowLabels(this);
        Slot39();                                    // slot 39 (+0x9c)
        return;
    }

    // 0x004d2b59..0x004d3cfa: lay the rows out again.
    G_ContactListLastScroll = static_cast<int>(ScrollPos);
    bool bRefreshNames = false;
    if (NameRefreshTimer > kNameRefreshSeconds)      // TEST AH,0x41 / JNZ
    {
        NameRefreshTimer = 0.0f;
        bRefreshNames = true;
    }

    int Row = 0;
    while (It != Contacts.end())
    {
        Icons[Row]->Flags = static_cast<unsigned char>(Icons[Row]->Flags | 3);
        RangeTexts[Row]->Flags = static_cast<unsigned char>(RangeTexts[Row]->Flags | 3);
        NameTexts[Row]->Flags = static_cast<unsigned char>(NameTexts[Row]->Flags | 3);

        FHudContactEntry& Entry = *It;
        IObjRW* const Obj = Entry.Object();
        if (Obj == nullptr && Entry.Kind != 4 && Entry.Kind != 3)
        {
            bDirty = 1;
            ++It;
            continue;
        }
        FClientGameobj* const Gameobj = static_cast<FClientGameobj*>(Obj);

        // 0x004d2bf6..0x004d2cd5: the range text -- metres under 2000, "N.NK" under 10000,
        // "NK" under 99999, else ids 0x3ce. FUN_00402180 is _vsnprintf into 0x2000 chars.
        int const Metres = static_cast<int>(Entry.Distance);
        char RangeText[0x2000];
        NnTextControl* const RangeControl = static_cast<NnTextControl*>(RangeTexts[Row]);
        if (Metres < 2000)
        {
            _snprintf(RangeText, 0x2000, "%d", Metres);
            RangeControl->SetNumberText(RangeText);      // FUN_0058a580 (0x004d2c26)
        }
        else if (Metres < 10000)
        {
            _snprintf(RangeText, 0x2000, "%d.%dK", Metres / 1000, (Metres % 1000) / 100);
            RangeControl->SetNumberText(RangeText);      // FUN_0058a580 (0x004d2c87)
        }
        else if (Metres < 99999)
        {
            _snprintf(RangeText, 0x2000, "%dK", Metres / 1000);
            RangeControl->SetNumberText(RangeText);      // FUN_0058a580 (0x004d2c26)
        }
        else
        {
            RangeTexts[Row]->SetProperty(0x1e, 0x3ce, 0);
        }

        // 0x004d2cd5..0x004d38b3: the name.
        wchar_t Name[0x100];
        Name[0] = 0;
        unsigned char bNamed = 0;          // [ESP + 0x12]
        bool bSkipRow = false;
        CShip const* LabelShip = nullptr;  // the ship label path (0x004d3431)
        bool bSimpleLabel = false;         // the HudSmall label path (0x004d375b)
        if (Obj == nullptr)
        {
            if (Entry.Kind == 4)
            {
                // 0x004d2cf3..0x004d2d64: "waypoint N" (ids 0x444 as the format), or ids 0x442.
                if (Entry.Index < GetPlayerWaypointCountRetail())
                {
                    FPlayerWaypointRecord* const Record = GetPlayerWaypointRetail(Entry.Index);
                    wchar_t Format[0x40];
                    GetIdsStringWWithRawFallback(0x444, Format, 0x40);
                    swprintf(Name, 0x100, Format, Record->AutoId);
                }
                else
                {
                    GetIdsStringWWithRawFallback(0x442, Name, 0xff);
                }
            }
            else if (Entry.Kind == 3)
            {
                GetIdsStringWWithRawFallback(0x443, Name, 0xff);
            }
            else
            {
                wcscat(Name, L"?");                      // LAB_004d389d
            }
        }
        else if (Gameobj->GetFlag3IfSurfaceWidthUnchanged() && !(bRefreshNames && Entry.Kind == 7))
        {
            // 0x004d2d98..0x004d2dc0: the object's label is still good.
            RowLabels[Row] = Gameobj->GetFontString48();
            wcscat(Name, L"?");
        }
        else
        {
            CObject const* const Object = Obj->cobject();
            CShip const* const Ship = AsShip(Object);
            if (Ship != nullptr)
            {
                // 0x004d2df4..0x004d3427: a ship. The second slot +0x150 call narrows the same
                // object to a CSimple.
                CSimple const* const Simple = AsSimple(Obj->cobject());
                int const Vibe = static_cast<int>(Ship->VibeId);
                bool bOwnPlayer = false;
                if (Ship->bIsPlayer == 0 && Entry.bWatchListed == 0)
                {
                    // 0x004d2e34..0x004d3170: an NPC -- faction short name, then the pilot's
                    // FmtStr name, or else the ship type's name.
                    unsigned ShortName = 0;
                    unsigned Group = 0;
                    if (Reputation::Vibe::GetAffiliation(Vibe, Group, false) == 0)
                    {
                        ShortName = Reputation::get_short_name(Group);
                    }
                    FmtStr NameA;
                    FmtStr NameB;
                    wchar_t const* Display = nullptr;
                    if (Reputation::Vibe::GetName(Vibe, NameA, NameB, Display) == 0 &&
                        NameA.MadLibId == 0 && NameB.MadLibId != 0)
                    {
                        if (IsShortNameShown(ShortName))
                        {
                            wchar_t Faction[0x80];
                            IdsInto(ShortName, Faction, 0x80);
                            wcscat(Name, Faction);
                            wcscat(Name, L" ");
                        }
                        wchar_t Pilot[0xff];
                        FmtStrSprintf(&NameB, Pilot, 0xff);
                        wcscat(Name, Pilot);
                        bNamed = 1;
                    }
                    else
                    {
                        if (IsShortNameShown(ShortName))
                        {
                            wchar_t Faction[0x80];
                            IdsInto(ShortName, Faction, 0x80);
                            wcscat(Name, Faction);
                            if (wcslen(Name) == 1 && Name[0] == L' ')
                            {
                                Name[0] = 0;
                            }
                            else
                            {
                                bNamed = 1;
                            }
                        }
                        if (Simple != nullptr)
                        {
                            if (bNamed != 0)
                            {
                                wcscat(Name, L" ");
                            }
                            wchar_t TypeName[0x80];
                            IdsInto(GetObjectIdsName(Simple), TypeName, 0x80); // FUN_005472a0
                            wcscat(Name, TypeName);
                        }
                    }
                }
                else
                {
                    // 0x004d3175..0x004d3414: a player (or a watched NPC).
                    if (Entry.bWatchListed != 0)
                    {
                        unsigned Group = 0;
                        if (Reputation::Vibe::GetAffiliation(Vibe, Group, false) == 0)
                        {
                            unsigned const ShortName = Reputation::get_short_name(Group);
                            if (IsShortNameShown(ShortName))
                            {
                                wchar_t Faction[0x80];
                                IdsInto(ShortName, Faction, 0x80);
                                wcscat(Name, Faction);
                                if (wcslen(Name) == 1 && Name[0] == L' ')
                                {
                                    Name[0] = 0;
                                }
                                else
                                {
                                    wcscat(Name, L" ");
                                    bNamed = 1;
                                }
                            }
                        }
                    }
                    if (Player != nullptr && Entry.Id == Player->get_id())
                    {
                        // 0x004d3286..0x004d3298: the player's own character name.
                        wcscat(Name, GetPlayerCharacterNameRetail());   // DAT_006732f0
                        bOwnPlayer = true;
                    }
                    else
                    {
                        FmtStr NameA;
                        FmtStr NameB;
                        wchar_t const* Display = nullptr;
                        if (Reputation::Vibe::GetName(Vibe, NameA, NameB, Display) == 0)
                        {
                            wcscat(Name, Display);
                            bNamed = 1;
                        }
                    }
                }
                if (bOwnPlayer || bNamed != 0)
                {
                    LabelShip = Ship;
                }
            }

            if (LabelShip == nullptr)
            {
                // 0x004d35af..0x004d3758: not a ship, or a ship without a name.
                int const Kind = Entry.Kind;
                if (Kind == 0 || Kind == 8 || Kind == 9)
                {
                    bSkipRow = true;
                }
                else
                {
                    CSimple const* const Simple = AsSimple(Obj->cobject());
                    if (Simple == nullptr)
                    {
                        // LAB_004d3895.
                        if (bNamed == 0)
                        {
                            wcscat(Name, L"?");
                        }
                    }
                    else if (Kind == 0xa && (Simple->TypeBits & 0x7f0000u) != 0)
                    {
                        bSkipRow = true;
                    }
                    else if (HudIsUnvisitedSolar(Simple))
                    {
                        // 0x004d3616..0x004d3655: an unvisited solar shows its type only.
                        wchar_t TypeName[0x80];
                        IdsInto(HudObjectTypeIds(Simple), TypeName, 0x80); // FUN_004d4d50
                        wcscat(Name, TypeName);
                    }
                    else
                    {
                        if ((Simple->TypeBits & 0x80u) != 0)
                        {
                            // 0x004d3668..0x004d3711: a trade lane ring -- the location caption.
                            FHudWideText Caption;
                            Caption.Text = nullptr;
                            Caption.Capacity = 0;
                            if (HudBuildObjectCaption(Simple, &Caption, 1, false)) // FUN_004e8100
                            {
                                wcscpy(Name, Caption.Text);
                            }
                            else
                            {
                                wchar_t IdsName[0x80];
                                IdsInto(GetObjectIdsName(Simple), IdsName, 0x80);
                                wcscat(Name, IdsName);
                            }
                            if (Caption.Text != nullptr)
                            {
                                free(Caption.Text);
                                Caption.Text = nullptr;
                                Caption.Capacity = 0;
                            }
                        }
                        else
                        {
                            wchar_t IdsName[0x80];
                            IdsInto(GetObjectIdsName(Simple), IdsName, 0x80);
                            wcscat(Name, IdsName);
                        }
                        bSimpleLabel = true;
                    }
                }
            }
        }
        if (bSkipRow)
        {
            ++It;
            continue;
        }
        if (LabelShip != nullptr)
        {
            RowLabels[Row] = ApplyRowLabel(Gameobj, LabelShip->bIsPlayer != 0 ? PlayerNameFont
                                                                               : HudSmallFont,
                                           Name);
        }
        else if (bSimpleLabel)
        {
            RowLabels[Row] = ApplyRowLabel(Gameobj, HudSmallFont, Name);
        }

        // 0x004d38b7..0x004d3985: the name text -- other players' names in PlayerName, dropped
        // a little; everything else in HudSmall at the rest offset.
        NnTextControl* const NameText = static_cast<NnTextControl*>(NameTexts[Row]);
        if (Entry.Kind == 0xe || Entry.Kind == 0xf)
        {
            if (RowLabels[Row] == nullptr)
            {
                NameText->SetFont(PlayerNameFont);        // FUN_0058a740
            }
            NameText->Offset[0] = NameText->HomeOffset[0];
            NameText->Offset[1] = NameText->HomeOffset[1];
            NameText->Offset[2] = NameText->HomeOffset[2];
            NameText->Offset[1] =
                static_cast<float>(static_cast<double>(NameText->Offset[1]) - kGroupNameDrop);
        }
        else
        {
            if (RowLabels[Row] == nullptr)
            {
                NameText->SetFont(HudSmallFont);
            }
            NameText->Offset[0] = NameText->HomeOffset[0];
            NameText->Offset[1] = NameText->HomeOffset[1];
            NameText->Offset[2] = NameText->HomeOffset[2];
        }
        NameText->SetProperty(6, reinterpret_cast<uintptr_t>(NameText->Offset), 0);
        if (RowLabels[Row] == nullptr)
        {
            NameTexts[Row]->SetProperty(0x1d, reinterpret_cast<uintptr_t>(Name), 0);
        }

        // 0x004d3985..0x004d3a62: the icon.
        unsigned char const bTarget = IsEntryTarget(&Entry, PlayerTargetId, FilterMode != 4) ? 1 : 0;
        FButtonMontageTile* Tile = Tiles[Entry.Kind];
        char const* Path = kContactIconPaths[Entry.Kind];
        if (Obj != nullptr && HudCommIsWatchedObjectId(Obj->get_id()))
        {
            Tile = ChatTile;
            Path = kContactChatIconPath;
        }
        char MeshPath[0x100];
        Icons[Row]->SetProperty(0xb, reinterpret_cast<uintptr_t>(MeshPath), 0x100);
        if (strcmp(MeshPath, Path) != 0)
        {
            Icons[Row]->SetProperty(4, reinterpret_cast<uintptr_t>(Path), 0);
            static_cast<NnButtonControl*>(Icons[Row])->MontageTile = Tile;
        }

        // 0x004d3a62..0x004d3c7d: the colour (no faction test in this pass).
        unsigned long Colour = G_NnPaletteArgb_679bb0;
        if (Entry.Kind == 4 || Entry.Kind == 3)
        {
            Colour = G_NnPaletteArgb_679b80;
        }
        else
        {
            unsigned long RepColour = 0;
            HudReputationColor(&RepColour, Player, Entry.Object());
            Colour = RepColour;
        }
        if (bTarget != 0)
        {
            Colour = G_NnPaletteArgb_679ba4;
        }
        else
        {
            float Ray[3];
            ContactCursorRay(Ray, true);
            if ((Icons[Row]->HitTest(Ray) || RangeTexts[Row]->HitTest(Ray) ||
                 NameTexts[Row]->HitTest(Ray)) &&
                !HudIsMousePickBlocked())
            {
                unsigned long Pulsed = 0;
                NnUiPulseBlendFromArgb(&Pulsed, Colour);
                Colour = Pulsed;
            }
        }
        Icons[Row]->SetProperty(0x33, reinterpret_cast<uintptr_t>(&Colour), 1);
        RangeTexts[Row]->SetProperty(0x33, reinterpret_cast<uintptr_t>(&Colour), 1);
        NameTexts[Row]->SetProperty(0x33, reinterpret_cast<uintptr_t>(&Colour), 1);
        Icons[Row]->SetProperty(3, kContactTooltipIds[Entry.Kind], 0);
        ++Row;
        if (Row >= 8)
        {
            break;
        }
        ++It;
    }

    // 0x004d3cd5..0x004d3cfa: the rows past the end are hidden.
    for (int Hidden = Row; Hidden < 8; ++Hidden)
    {
        RangeTexts[Hidden]->Flags = static_cast<unsigned char>(RangeTexts[Hidden]->Flags & 0xfc);
        NameTexts[Hidden]->Flags = static_cast<unsigned char>(NameTexts[Hidden]->Flags & 0xfc);
        Icons[Hidden]->Flags = static_cast<unsigned char>(Icons[Hidden]->Flags & 0xfc);
    }

    // 0x004d3cfc..0x004d3d10: the scroll range.
    Scroll->SetProperty(0x37, 0, static_cast<uintptr_t>(static_cast<int>(Contacts.size()) - 1));

    SwapRowLabels(this);
    WinControl::Draw();
    SwapRowLabels(this);
    Slot39();
}

namespace
{
    // The static HUDFACTORY<HUD_ContactList> DAT_00674910: XC entry LAB_004cefe0 runs the ctor
    // FUN_004d4630 ("ContactList", appended to the factory chain by FUN_004c72b0) and registers
    // LAB_004cf000 with atexit (-> FUN_004cf010, the unlink). Its Create FUN_004d46a0 is
    // operator new(0x478) + FUN_004cf390, null when the allocation fails.
    HUDFACTORY<HUD_ContactList> G_HudContactListFactory("ContactList");
}
