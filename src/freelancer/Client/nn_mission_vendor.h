// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// The mission board of a base (retail NN_MissionVendor, freelancer.exe 0x0048b280..0x0048d6b6,
// "E:\FL\Scratch\Source\Client\nn\NN_MissionVendor.cpp").
//
// RTTI .?AVNN_MissionVendor@@ : WinControl, Event::Handler. operator_new(0x45c) (0x0048d638).
// WINFACTORY<NN_MissionVendor> DAT_00671ffc "NN_MissionVendor": static init LAB_0048b2b0 (ctor
// FUN_0048d5b0, atexit LAB_0048b2d0 -> FUN_00579380); Create FUN_0048d620 (operator_new, the
// ctor FUN_0048b2e0, the class-name pointer, slot 9; a failed Create is followed by the scalar
// deleting destructor). FUN_0048d5b0 is the WINFACTORY ctor inlined for this type (name clamped
// to 31, FUN_0059dab0, vftable 0x005d2d90).
//
// vftable 0x005d26bc -- WinControl's with these overrides (raw dword dump):
//   0 FUN_0048b4f0 scalar deleting dtor (FUN_0048b510), 9 LAB_0048b6e0 Create, 11 LAB_0048d440
//   Update, 13 FUN_0048d360 Draw, 42 LAB_0048c460 SetProperty.
// IDataViewOwner face +0x7c: vftable 0x005d26ac (WinControl's).
// Event::Handler face +0x32c: vftable 0x005d26a4, slot 0 LAB_0048d540.
//
// Up to five rows of the base's mission offers (the LocationController's MissionVendorProperties
// bucket, copied once by LoadOffers), a scroll bar, the selected offer's text, its icon and a
// video window whose material "MV_vidmaterial" gets the offer's icon texture swapped in.
//
// Layout: retail's, except that the std::string at +0x34c is the current CRT's (retail's VC6
// string is 0x10 bytes); every later member sits MissionVendorStringDelta bytes further on. The
// comments give retail offsets. The row arrays +0x3b0..+0x458 keep retail's relative layout,
// which Draw depends on (it indexes them out of range, see RowDword).

#include "event_handler.h"
#include "swapped_material.h"
#include "wincontrol_base.h"

#include "common_text.h" // FmtStr

#include <stddef.h>
#include <stdint.h>
#include <string>

class MissionVendorAcceptance;
class MissionVendorProperties;
class RichTextWin;
class UiScroll;

// DAT_00672038 -- why the base has no offers (0..4, ids 0x3d0 / 0x384 / 0x385 / 0x386 / 0x3d0
// in Create's no-missions text). Its only writer is LAB_00437a40 (RET 8, the second argument):
// IClient slot 42 (ch2 0x5a GFMISSIONVENDORWHYEMPTY), ported as
// IClientImpl::SendServerGFMissionVendorWhyEmpty (client_iclient.cpp).
extern int G_MissionVendorNoMissionsReason;

class NnMissionVendor : public WinControl, public EventHandler
{
public:
    // cdecl (Context, bAccepted): 0 when the window is closed or cancelled, 1 on accept. No
    // writer of +0x33c / +0x340 exists in freelancer.exe besides the ctor's zero.
    typedef void (*FResultCallback)(void* Context, int bAccepted);

    // FUN_0048b2e0.
    NnMissionVendor();
    // FUN_0048b510 (FUN_0048b4f0 the scalar deleting wrapper).
    ~NnMissionVendor() override;

    // slot 9 LAB_0048b6e0 (RET 8) -- every control, the offers, Win3D's Create last.
    bool Create(Win3D* NewParent, void* Data) override;
    // slot 11 LAB_0048d440 -- Win3D's Update, then a close requested by OnAcceptResult.
    void Update() override;
    // slot 13 FUN_0048d360 -- row layers and the two frame columns, then WinControl's Draw.
    void Draw() override;
    // slot 42 LAB_0048c460 (RET 0xc) -- clicks (0x26), hover (0x2c / 0x2d), scroll (0x3e),
    // close (0x4e); returns 0.
    intptr_t SetProperty(int Msg, uintptr_t A, uintptr_t B) override;

    // Event::Handler face LAB_0048d540 (RET 4) -- USER_CANCEL (0x10) closes the window.
    bool OnEvent(unsigned MsgId) override;

    // FUN_0048c720 (thiscall) -- NavBar FUN_004424f0(0), then (for a non-null window) every
    // child, slot 10, delete. Caller: the destructor at 0x0044d580 (vftable 0x005cc0a4), on the
    // window FUN_0059da10("NN_MissionVendor") finds.
    void CloseWindow();

    // FUN_0048d4a0 (cdecl) -- the server's answer to an accept: bAccepted closes the window at
    // the next Update, otherwise the selection is cleared and the reason is shown. Caller: the
    // MirrorDatabase listener 0x004379b0 (unported, HOLE).
    static void OnAcceptResult(MissionVendorAcceptance const& Result);

    float BasePosition[3];               // +0x330 {0, -0.1, -1.498}, set by Create
    FResultCallback ResultCallback;      // +0x33c
    void* ResultContext;                 // +0x340
    unsigned char bCloseRequested;       // +0x344 OnAcceptResult -> Update
    unsigned char Pad345[3];
    int MaterialGroup;                   // +0x348 MV_videoscreen.3db (FUN_0041c850); the ctor
                                         //        leaves it unwritten
    std::string VideoTexture;            // +0x34c the swapped-in texture name
    FSwappedMaterial VideoMaterial;      // +0x35c "MV_vidmaterial"
    int ScrollOffset;                    // +0x370 first offer in row 0
    int Field_0x374;                     // +0x374 TODO: zeroed by the ctor and LoadOffers, never read
    int Selected;                        // +0x378 offer index, -1 none
    FmtStr StatusText;                   // +0x37c the full text without a selection
    MissionVendorProperties* Offers;     // +0x394 new[] copy of the bucket
    int OfferCount;                      // +0x398
    char const** IconPaths;              // +0x39c 7 mesh paths (operator_new(0x1c))
    unsigned IconPathCount;              // +0x3a0 7
    WinControl* AcceptButton;            // +0x3a4 "MissionVendorAcceptButton"
    WinControl* CloseButton;             // +0x3a8 "MissionVendorCloseButton"
    WinControl* Title;                   // +0x3ac "MissionVendorTitle"
    WinControl* SummaryIcons[5];         // +0x3b0 "MissionVendorSummaryIcon%d"
    WinControl* IconFrames[5];           // +0x3c4 "MissionVendorIconFrame%d"
    WinControl* SummaryBoxes[5];         // +0x3d8 "MissionVendorSummaryBox%d"
    WinControl* SummaryDests[5];         // +0x3ec "MissionVendorSummaryDest%d" (offer +0x14)
    WinControl* SummaryTitles[5];        // +0x400 "MissionVendorSummaryTitle%d" (offer +0x2c)
    WinControl* SummaryRewards[5];       // +0x414 "MissionVendorSummaryReward%d"
    UiScroll* SummaryScroll;             // +0x428 "MissionVendorSummaryScroll"
    WinControl* MissionDest;             // +0x42c "MissionVendorMissionDest"
    WinControl* MissionTitle;            // +0x430 "MissionVendorMissionTitle"
    WinControl* MissionReward;           // +0x434 "MissionVendorMissionReward"
    RichTextWin* FullText;               // +0x438 "MissionVendorMissionFullText"
    unsigned Field_0x43c;                // +0x43c TODO: not written or read by this class
    WinControl* MissionReject;           // +0x440 "MissionVendorMissionReject"
    unsigned Field_0x444;                // +0x444 TODO: not written or read by this class
    int Hovered;                         // +0x448 offer index under the mouse, -1 none
    int Clicked;                         // +0x44c row last clicked, -1 none
    WinControl* MissionIcon;             // +0x450 "MissionVendorIcon"
    WinControl* Background;              // +0x454 "MissionVendorBackground"
    unsigned char bNoMissions;           // +0x458
    unsigned char Pad459[3];

private:
    // The tail every close shares (e.g. 0x0048c4a5..0x0048c4b2): FUN_0043d2a0 on every child
    // (the next sibling read first), slot 10, then slot 0 with 1.
    void DeleteSelf();
    // FUN_0048c770 -- copy the current base's offers, rows from the top, select offer 0.
    void LoadOffers();
    // FUN_0048c8a0 -- fill the five rows from ScrollOffset.
    void RefreshRows();
    // FUN_0048cce0 -- the selected offer's texts (or StatusText), then UpdateVideo.
    void ShowSelected();
    // FUN_0048d140 -- the selected offer's texture in the video window and its icon.
    void UpdateVideo();
    // 0x0048b593..0x0048b644 / 0x0048d17c..0x0048d236: the string cleared, the swap released,
    // the string cleared again, the swap's fields zeroed.
    void ResetVideo();
    // The three FUN_00459950 calls on the selected offer (Create, SetProperty 0x26,
    // OnAcceptResult).
    void RevealSelectedOffer();
    // Draw's `[this + Row*4 + 0x3c4]` / `+0x3d8` with Row outside 0..4 (a selection or hover
    // scrolled out of the rows): the dword that far from the array in retail's layout.
    uintptr_t RowDword(size_t ArrayOffset, int Index) const;

    NnMissionVendor(NnMissionVendor const&) = delete;
    NnMissionVendor& operator=(NnMissionVendor const&) = delete;
};

// Retail's VC6 std::string is 0x10 bytes; everything after +0x34c moves by the difference.
constexpr size_t MissionVendorStringDelta = sizeof(std::string) - 0x10;

static_assert(offsetof(NnMissionVendor, BasePosition) == 0x330, "NN_MissionVendor +0x330");
static_assert(offsetof(NnMissionVendor, MaterialGroup) == 0x348, "NN_MissionVendor +0x348");
static_assert(offsetof(NnMissionVendor, VideoTexture) == 0x34c, "NN_MissionVendor +0x34c");
static_assert(offsetof(NnMissionVendor, VideoMaterial) == 0x35c + MissionVendorStringDelta, "NN_MissionVendor +0x35c");
static_assert(offsetof(NnMissionVendor, StatusText) == 0x37c + MissionVendorStringDelta, "NN_MissionVendor +0x37c");
static_assert(offsetof(NnMissionVendor, AcceptButton) == 0x3a4 + MissionVendorStringDelta, "NN_MissionVendor +0x3a4");
static_assert(offsetof(NnMissionVendor, SummaryIcons) == 0x3b0 + MissionVendorStringDelta, "NN_MissionVendor +0x3b0");
static_assert(offsetof(NnMissionVendor, IconFrames) == 0x3c4 + MissionVendorStringDelta, "NN_MissionVendor +0x3c4");
static_assert(offsetof(NnMissionVendor, SummaryScroll) == 0x428 + MissionVendorStringDelta, "NN_MissionVendor +0x428");
static_assert(offsetof(NnMissionVendor, Hovered) == 0x448 + MissionVendorStringDelta, "NN_MissionVendor +0x448");
static_assert(offsetof(NnMissionVendor, bNoMissions) == 0x458 + MissionVendorStringDelta, "NN_MissionVendor +0x458");
static_assert(sizeof(NnMissionVendor) == 0x45c + MissionVendorStringDelta, "retail NN_MissionVendor is 0x45c bytes (operator_new 0x0048d638)");
