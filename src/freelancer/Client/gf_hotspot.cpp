// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// supt\Hotspot.cpp -- freelancer.exe 0x00555d00..0x0055624f. See gf_hotspot.h.

#include "gf_hotspot.h"

#include "gameflow.h"     // ClearHoveredHotspotIf (FUN_00558de0)
#include "room_control.h" // FRoomObject slots 10..12

#include "dacom.h"        // FDUMP

namespace
{
    char const kHotspotCpp[] = "E:\\FL\\Scratch\\Source\\Client\\supt\\Hotspot.cpp";

    // DAT_006799a4 -- LAB_00555d00 (static initializer) stores .rdata DAT_005e1210 (0.6f) here;
    // nothing in the PE reads it.
    float G_Hotspot6799a4 = 0.6f;

    // DAT_00679998 / DAT_0067999c / DAT_006799a0 -- LAB_00555d10 builds the empty list and
    // registers LAB_00555d50 with atexit, which frees every node (not the hotspots) and the
    // head. The std::list's own construction and destruction are that pair.
    std::list<FHotspot*> G_Hotspots;
}

std::list<FHotspot*>& GetHotspotList()
{
    return G_Hotspots;
}

FHotspot::FHotspot()
    : Owner(nullptr)
    , Cookie(0xffffffffu)
    , bEnabled(0)
    , bHovered(0)
{
    // FUN_00555db0: +0x10..+0x1c are zeroed twice (0x00555db7..0x00555dc0 and again
    // 0x00555dd9..0x00555de2), +0x04 = 0, +0x08 = -1, +0x0c = +0x0d = 0; then a node is
    // appended at the tail (0x00555de6..0x00555e13) and DAT_006799a0 incremented.
    Pad0E[0] = 0;
    Pad0E[1] = 0;
    ScreenRect.Left = 0;
    ScreenRect.Top = 0;
    ScreenRect.Width = 0;
    ScreenRect.Height = 0;
    G_Hotspots.push_back(this);
}

FHotspot::~FHotspot()
{
    // FUN_00555ea0: the tracker lets go of this hotspot first (FUN_00558de0).
    ClearHoveredHotspotIf(this);

    // 0x00555eaf..0x00555eeb: the first node whose value is this is unlinked and freed, the
    // size decremented; no node, nothing happens.
    for (std::list<FHotspot*>::iterator It = G_Hotspots.begin(); It != G_Hotspots.end(); ++It)
    {
        if (*It == this)
        {
            G_Hotspots.erase(It);
            break;
        }
    }
}

void FHotspot::SendEvent(int Event, int const* Point)
{
    // FUN_00555f00.
    if (Owner == nullptr)
        return;
    if (Event == 1)
        Owner->Slot10(Cookie, Point);
    else if (Event == 2)
        Owner->Slot11(Cookie, Point);
    else if (Event == 3)
        Owner->Slot12(Cookie, Point);
}

bool FindHotspotAt(int const* Point, FHotspot** OutHotspot)
{
    // FUN_00556090. Best / BestScore start at 0 (0x0055609d / 0x005560a5).
    FHotspot* Best = nullptr;
    float BestScore = 0.0f;

    for (std::list<FHotspot*>::iterator It = G_Hotspots.begin(); It != G_Hotspots.end(); ++It)
    {
        FHotspot* const Hotspot = *It;
        // 0x005560c3: +0x0c clear -> next.
        if (!Hotspot->bEnabled)
            continue;
        // 0x005560ce..0x005560e2: Rect::point_is_inside(x, y) on +0x10.
        if (!Hotspot->ScreenRect.point_is_inside(Point[0], Point[1]))
            continue;

        // 0x005560e8..0x00556113: Rect::get_center on a copy of the rectangle.
        Rect const Copy = Hotspot->ScreenRect;
        int CenterX = 0;
        int CenterY = 0;
        Copy.get_center(&CenterX, &CenterY);

        // 0x00556119: the score starts as 1.0f (DAT_005c75dc); 0x0055612d..0x0055615d replace it
        // only when Width > 0 and Height > 0 (both JLE): ((x - cx) / w)^2 + ((y - cy) / h)^2,
        // with FIDIV on the integer width / height (x87 extended precision).
        double Score = 1.0;
        if (Copy.Width > 0 && Copy.Height > 0)
        {
            double const Dx = static_cast<double>(Point[0] - CenterX) /
                              static_cast<double>(Copy.Width);
            double const Dy = static_cast<double>(Point[1] - CenterY) /
                              static_cast<double>(Copy.Height);
            Score = Dy * Dy + Dx * Dx;
        }

        // 0x005561a2..0x005561bd: no best yet -> take it; otherwise FCOM against the stored
        // float with TEST AH,0x05 / JP -- the jump (keep the old best) is taken unless C0 alone
        // is set, i.e. the new one is taken only when Score < BestScore (not on equal, not on
        // an unordered compare).
        if (Best == nullptr || Score < static_cast<double>(BestScore))
        {
            BestScore = static_cast<float>(Score); // FSTP float [ESP+0x10]
            Best = Hotspot;
        }
    }

    // 0x005561d5..0x005561fc.
    if (Best != nullptr)
    {
        *OutHotspot = Best;
        return true;
    }
    *OutHotspot = nullptr;
    return false;
}

bool ReportLeakedHotspots()
{
    // FUN_00556200.
    for (std::list<FHotspot*>::iterator It = G_Hotspots.begin(); It != G_Hotspots.end(); ++It)
    {
        FHotspot* const Hotspot = *It;
        FDUMP(0x100002, "%s(%d) : *** WARNING: hotspot = %p id = %d not freed\n", kHotspotCpp,
              0x161, Hotspot, Hotspot->Cookie);
    }
    // 0x00556241..0x0055624b: DAT_006799a0 == 0.
    return G_Hotspots.size() == 0;
}
