// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "hud_cruise_progress.h"

#include "common_archetype.h"
#include "common_cequip.h"
#include "common_cobject.h"
#include "common_iface.h"
#include "game_options.h"
#include "ids_strings.h"
#include "input_manager.h"
#include "nn_text_control.h"
#include "nn_ui_pulse.h"
#include "player_behavior.h"
#include "space_letterbox.h"

#include <math.h>
#include <stdint.h>
#include <wchar.h>

static_assert(sizeof(HUD_CruiseProgress) == 0x42c, "retail HUD_CruiseProgress is 0x42c bytes");

HUD_CruiseProgress::HUD_CruiseProgress(INI_Reader& /*Reader*/)
    : CruiseProgressText(nullptr)
    , bCharging(0)
{
    // FUN_004d5bd0: HUDItem inlined (0x004d5bed..0x004d5c1e), WinControl FUN_0059f860
    // (0x004d5c2e), +0x364 = 0, the two vptrs, the two captions' first wchar = 0
    // (0x004d5c5a / 0x004d5c61), HUDItem's name, then Create(0, 0) through the direct call at
    // 0x004d5ca6, the Win3D name, Flags &= 0xfe (0x004d5ceb..0x004d5cf4), the class name and
    // +0x428 = 0 (0x004d5d02).
    ChargingCaption[0] = 0;
    CountdownCaption[0] = 0;
    HUDItem::Name = "HUD_CruiseProgress";
    HUD_CruiseProgress::Create(nullptr, nullptr);
    Win3D::SetName("HUD_CruiseProgress");
    Flags = static_cast<unsigned char>(Flags & 0xfe);
    ClassName = "HUD_CruiseProgress";
    bCharging = 0;
}

HUD_CruiseProgress::~HUD_CruiseProgress()
{
    // FUN_004d5d40: ~WinControl FUN_0059fa50 on +0x38, then HUDItem's vptr and its watcher
    // cleared (set_pointer(0) when watching) -- the base destructors, in that order.
}

bool HUD_CruiseProgress::Create(Win3D* NewParent, void* Data)
{
    // FUN_004d5db0 (this = the WinControl face).
    float const TextPos[3] = {-0.243f, -0.263f, 0.0f}; // 0xbe78d4fe, 0xbe86a7f0, 0
    CruiseProgressText = CreateTextControl("CruiseProgressText", "SubHeader", TextPos, nullptr,
                                           0.485f, 0.04f, 0.9f, 0, 1, 0, 1);

    // 0x004d5e01..0x004d5e11: the label's Font::String goes multi-line (and its layout is
    // dropped) unless it already is. The factory's result is not tested, as in retail.
    FFontString* const Text = static_cast<NnTextControl*>(CruiseProgressText)->GetFontString();
    if (Text->bMultiLine != 1)
    {
        Text->bMultiLine = 1;   // +0x30
        Text->LayoutGen = 0;    // +0x28
    }

    SetOffset(0.0f, 0.0f, -1.0f); // slot 31 (+0x7c), 0x004d5e4d

    // 0x004d5e50..0x004d5e7e: FUN_004347e0 into the two 0x30-wchar captions.
    GetIdsStringWWithRawFallback(0x585, ChargingCaption, 0x30);
    GetIdsStringWWithRawFallback(0x64f, CountdownCaption, 0x30);

    // 0x004d5e92: Win3D::Create FUN_004b5060, its result folded to a bool.
    return Win3D::Create(NewParent, Data);
}

void HUD_CruiseProgress::Update()
{
    // FUN_004d6100 (this = the WinControl face).
    if (GetClientMouseCursorDrawFlag() != 0 && GetPlayerObjRW() != nullptr)
    {
        SetHiddenWithChildren(false);             // FUN_005a0c30(0)
        Flags = static_cast<unsigned char>(Flags | kWin3DActive);
    }
    else
    {
        SetHiddenWithChildren(true);              // FUN_005a0c30(1)
    }
    Win3D::Update();                              // tail jump FUN_005a17b0
}

void HUD_CruiseProgress::UpdateInactive()
{
    // FUN_004d6140 (this = the WinControl face).
    if (GetClientMouseCursorDrawFlag() != 0 && GetPlayerObjRW() != nullptr)
    {
        SetHiddenWithChildren(false);             // FUN_005a0c30(0)
    }
    // 0x004d615e..0x004d6177: LAB_004682f0 inlined -- slot 12 on every child whose bit0 is
    // clear, the next sibling read after each call.
    Win3D::UpdateInactive();
}

void HUD_CruiseProgress::Draw()
{
    // LAB_004d5eb0 (this = the WinControl face).
    if (GetGameOption(kGameOptInterfaceGroup) == 0)     // FUN_005792b0(6)
    {
        return;
    }
    if (GetGameOption(kGameOptHud) == 0)                // FUN_005792b0(0xa)
    {
        return;
    }
    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW()); // FUN_0054baf0
    if (Player == nullptr)
    {
        return;
    }
    if (GetClientMouseCursorDrawFlag() == 0)            // FUN_0041dd90
    {
        return;
    }
    if (IsLetterboxVisible())                           // FUN_0041a3e0
    {
        return;
    }
    CObject* const Object = const_cast<CObject*>(Player->cobject()); // slot +0x150
    if (Object == nullptr)
    {
        return;
    }
    if ((static_cast<unsigned>(Object->ObjectClass) & 0x503u) != 0x503u)
    {
        return;
    }
    CEqObj* const Ship = static_cast<CEqObj*>(Object);
    CEEngine* const Engine = CEEngine::cast(Ship->EquipManager.FindFirst(0x20000)); // +0xe4
    if (Engine == nullptr)
    {
        return;
    }

    // 0x004d5f53..0x004d5f58 / 0x004d60d6: no cruise -- the latch drops, nothing is drawn.
    if (Engine->bCruiseActive == 0)
    {
        bCharging = 0;
        return;
    }

    // 0x004d5f60..0x004d5f77: TEST AH,0x44 / JNP leaves on an exactly-zero charge time (a NaN
    // one goes on).
    float const ChargeTime = Engine->EngineArch()->CruiseChargeTime;   // Engine +0x80
    if (ChargeTime == 0.0f)
    {
        return;
    }
    float const Elapsed = Engine->CruiseChargeTimeElapsed;              // CEEngine +0x50
    // 0x004d5f84..0x004d5f8a: FDIV in x87 precision, stored as a float.
    float const Ratio =
        static_cast<float>(static_cast<double>(Elapsed) / static_cast<double>(ChargeTime));

    NnTextControl* const Text = static_cast<NnTextControl*>(CruiseProgressText);
    wchar_t Line[0x3c]; // the 0x78-byte frame buffer at [ESP + 0x10]

    // 0x004d5f90..0x004d5f9f: TEST AH,5 / JP -- falls through only when Ratio < 0.
    if (Ratio < 0.0f)
    {
        // 0x004d5fa5..0x004d6023: the countdown before the charge starts.
        bCharging = 0;
        Text->SetCachedDraw(0, 0);                                      // FUN_0058ad70(0, 0)
        Text->SetProperty(0x33, reinterpret_cast<uintptr_t>(&G_NnPaletteArgb_679ba8), 1);
        int const Seconds = static_cast<int>(ceil(-static_cast<double>(Elapsed))); // ftol
        // Retail's format is u"%s - %d%" (0x005d7fdc). VC6 msvcrt's _woutput leaves its format
        // loop at the terminator right after the lone trailing '%' and prints nothing for it;
        // the current CRT rejects an unterminated conversion through the invalid-parameter
        // handler instead, so the same output is produced here without that '%'.
        swprintf(Line, 0x3c, L"%s - %d", CountdownCaption, Seconds);
        Text->SetProperty(0x1d, reinterpret_cast<uintptr_t>(Line), 0);
        Text->Draw();                                                   // slot 13 (+0x34)
        return;
    }

    // 0x004d602f..0x004d603e: TEST AH,5 / JP -- 1.0 or more (or NaN) draws nothing.
    if (Ratio < 1.0f)
    {
        // 0x004d6044..0x004d60ca: the charge percentage.
        Text->SetCachedDraw(1, static_cast<unsigned char>(bCharging == 0)); // FUN_0058ad70(1, !+0x428)
        bCharging = 1;
        Text->SetProperty(0x33, reinterpret_cast<uintptr_t>(&G_NnPaletteArgb_679bb0), 1);
        // FMUL 100.0f (0x005ca230), FADD 0.5 (double 0x005c75e8), ftol.
        int const Percent = static_cast<int>(static_cast<double>(Ratio) * 100.0 + 0.5);
        swprintf(Line, 0x3c, L"%s - %d%%", ChargingCaption, Percent);   // u"%s - %d%%" 0x005d7fc8
        Text->SetProperty(0x1d, reinterpret_cast<uintptr_t>(Line), 0);
        Text->Draw();
    }
}

namespace
{
    // The static HUDFACTORY<HUD_CruiseProgress> DAT_00674998: XC entry LAB_004d5ba0 runs the
    // ctor FUN_004d6180 ("CruiseProgress", appended to the factory chain by FUN_004c72b0) and
    // registers LAB_004d5bc0 with atexit (-> FUN_004cf010, the unlink). Its Create FUN_004d61f0
    // is operator new(0x42c) + FUN_004d5bd0, null when the allocation fails.
    HUDFACTORY<HUD_CruiseProgress> G_HudCruiseProgressFactory("CruiseProgress");
}
