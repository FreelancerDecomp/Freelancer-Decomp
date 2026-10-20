// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "hud_status_core.h"

#include "character_select_equip.h"
#include "client_gameobj.h"
#include "client_objdb.h"
#include "common_archetype.h"
#include "common_cequip.h"
#include "common_csys.h"
#include "common_misc.h"
#include "common_cobject.h"
#include "common_id.h"
#include "common_iface.h"
#include "dacom.h"
#include "DALib.h"
#include "game_options.h"
#include "globals.h"
#include "widescreen.h"   // port-only (FREELANCER_WIDESCREEN_SUPPORT)
#include "hud_command_dispatch.h"
#include "hud_targetable_objects.h"
#include "hud_weapon_groups.h"
#include "ids_strings.h"
#include "login_mgr.h"
#include "menu_keymap_config.h"
#include "menu_ui_init.h"
#include "nn_inventory.h"
#include "nn_tooltip.h"
#include "input_manager.h"
#include "nn_modal.h"
#include "nn_text_control.h"     // NnTextControl::SetNumberText (FUN_0058a580)
#include "nn_text_scratch.h"     // G_NnTextScratchA/B (DAT_0066dc60 / DAT_0066fc60), DAT_006119f8
#include "nn_ui_pulse.h"
#include "player_behavior.h"
#include "player_spawn.h"
#include "render/IRenderPipeline.h"
#include "screen_quad_state.h"
#include "ship_view_control.h"
#include "space_camera.h"
#include "space_letterbox.h"
#include "ui_scroll.h"
#include "win3d.h"
#include "x86math.h"

#include <math.h>
#include <cstdio>
#include <cstring>
#include <new>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

// ---------------------------------------------------------------------------------------------
// Shield sampler
// ---------------------------------------------------------------------------------------------

namespace
{
    // DAT_00674950 -- set by the static initializer LAB_004d4b30: CreateID("Ku07_02"). No other
    // writer in the PE; FUN_004d4b50 is its only reader.
    unsigned const G_ShieldlessShipId = CreateID("Ku07_02");

    // DAT_00674a9c -- HUD_Status.cpp's 3DMathEngine (FUN_005b73e0 on first use, FUN_004de0a0).
    I3DMathEngine* G_HudStatusMath = nullptr;
}

void HudSampleShieldHitPoints(CEqObj* Object, float* OutMax, float* OutCur, bool* OutAnyActive)
{
    // FUN_004d4b50.
    *OutMax = 0.0f;
    *OutCur = 0.0f;
    if (Object->Id == G_ShieldlessShipId)
    {
        return;
    }

    CEquipTraverser Walk(EquipTypeShield); // 0x100
    CEquip* Item = Object->EquipManager.Traverse(Walk);
    while (Item != nullptr)
    {
        CEShield* const Shield = CEShield::cast(Item);
        if (Shield != nullptr)
        {
            // 0x004d4bcc..0x004d4be1: Cap stays on the FPU; the floor goes through ftol and is
            // stored back as a float. The product of two floats is exact in a double.
            float const Cap = Shield->GetMaxHitPoints();
            int const FloorInt = static_cast<int>(static_cast<double>(Shield->OfflineThreshold) *
                                                  static_cast<double>(Cap));
            float const Floor = static_cast<float>(FloorInt);

            // 0x004d4be5..0x004d4c03: FCOM 0 / TEST AH,5 / JP keeps a difference that is >= 0
            // or unordered; anything below 0 becomes 0.
            double Spare = static_cast<double>(Cap) - static_cast<double>(Floor);
            if (Spare < 0.0)
            {
                Spare = 0.0;
            }
            *OutMax = static_cast<float>(Spare + static_cast<double>(*OutMax));

            // 0x004d4c06..0x004d4c17
            if (Shield->IsActive() && OutAnyActive != nullptr)
            {
                *OutAnyActive = true;
            }

            // 0x004d4c1a..0x004d4c4b
            if (Shield->IsFunctioning())
            {
                double Left = static_cast<double>(Shield->GetHitPoints()) -
                              static_cast<double>(Floor);
                if (Left < 0.0)
                {
                    Left = 0.0;
                }
                *OutCur = static_cast<float>(Left + static_cast<double>(*OutCur));
            }
        }
        Item = Object->EquipManager.Traverse(Walk);
    }
}

// ---------------------------------------------------------------------------------------------
// Panel singletons
// ---------------------------------------------------------------------------------------------

namespace
{
    // DAT_00674aa0.
    HUD_Status* G_HudStatus = nullptr;
}

HudTargetBase* G_HudTargetPanel = nullptr;

HUD_Status* GetHudStatus()
{
    return G_HudStatus;
}

bool HudStatusIsTurretView()
{
    // 0x004c870f: MOV EAX,[DAT_00674aa0] / MOV CL,[EAX+0x5b9] -- no null test.
    return G_HudStatus->bTurretView != 0;
}

bool HudStatusIsRearView()
{
    // 0x004c8722: MOV CL,[EAX+0x5ba].
    return G_HudStatus->bRearView != 0;
}

// ---------------------------------------------------------------------------------------------
// HUD_Core
// ---------------------------------------------------------------------------------------------

namespace
{
    // FUN_004d5410 constants (.rdata).
    double const kHalfThicknessPerLine = 0.005208333333333333; // 005d7e68 (1/192)
    double const kSegmentWidthA = 0.08544921875;               // 005d7e60
    double const kSegmentWidthB = 0.125;                        // 005d7e58
    double const kFirstChipScale = 0.8;                         // 005d7e50
    double const kHalf = 0.5;                                   // 005c75e8
    double const kDesignLineToScreen = 0.0013020833333333333;   // 005d7e38 (1/768)

    // DAT_00674980 -- the screen-quad list key FUN_004d5410 flushes; its static initializer
    // LAB_004d4af0 sets it to -1 and nothing else writes it (XREFs 004d4af0 W, 004d5673 R). -1
    // equals FUN_00403ea0's "current list" key DAT_0061648c, so the flush takes the container
    // of the current material key, the one the chips were just queued on.
    unsigned long G_HudCoreListKey = 0xffffffffu;
}

HUD_Core::HUD_Core(INI_Reader& /*Reader*/)
    : HealthBar(nullptr)
    , ShieldBar(nullptr)
    , PowerBar(nullptr)
    , VelocityText(nullptr)
    , ThrustText(nullptr)
    , LastVelocity(0)
    , LastThrust(0)
    , Background2(nullptr)
{
    // FUN_004d4db0: the bases (HUDItem inlined, WinControl FUN_0059f860, the handler), HUDItem's
    // name "Core", Create(0, 0) through the direct call at 0x004d4e86, then the Win3D name and
    // class name "HUD_Core" and the two read-out caches. The six child pointers are not written
    // by the ctor; Create fills them before anything reads them.
    HUDItem::Name = "Core";
    HUD_Core::Create(nullptr, nullptr);
    Win3D::SetName("HUD_Core");
    ClassName = "HUD_Core";
    LastVelocity = -999;   // 0xfffffc19
    LastThrust = -1;
}

HUD_Core::~HUD_Core()
{
    // FUN_004d4f20: Event::Handler FUN_00576010 on +0x364, ~WinControl FUN_0059fa50 on +0x38,
    // then HUDItem's watcher -- the base destructors, in that order.
}

bool HUD_Core::Create(Win3D* NewParent, void* Data)
{
    // FUN_004d4fa0 (this = the WinControl face).
    SetMesh("Interface\\hud\\HUD_guagewindow.cmp", 0);

    float const Zero[3] = {0.0f, 0.0f, 0.0f};

    float const BackgroundPos[3] = {0.005f, -0.365f, -1.0f};
    Background2 = CreateStaticControl("Background2", "Interface\\hud\\HUD_numberboxes.cmp",
                                      BackgroundPos, nullptr, Zero, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0,
                                      Zero);

    float const BarHalf[3] = {0.085f, 0.0055f, 0.0f};

    float const HealthPos[3] = {0.005f, -0.376f, -1.0f};
    HealthBar = CreateStaticControl("HealthBar", "Interface\\hud\\HUD_hullgauge.3db", HealthPos,
                                    nullptr, BarHalf, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, Zero);
    HealthBar->SetProperty(3, 0x470, 0);

    float const ShieldPos[3] = {0.005f, -0.366f, -1.0f};
    ShieldBar = CreateStaticControl("ShieldBar", "Interface\\hud\\HUD_shieldgauge.3db", ShieldPos,
                                    nullptr, BarHalf, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, Zero);
    ShieldBar->SetProperty(3, 0x471, 0);

    float const PowerPos[3] = {0.005f, -0.355f, -1.0f};
    PowerBar = CreateStaticControl("PowerBar", "Interface\\hud\\HUD_energygauge.3db", PowerPos,
                                   nullptr, BarHalf, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, Zero);
    PowerBar->SetProperty(3, 0x3f2, 0);

    float const VelocityPos[3] = {0.108f, 0.008f, 0.0f};
    VelocityText = CreateTextControl("VelocityText", "Normal", VelocityPos, nullptr, 0.056f, 0.03f,
                                     1.0f, 0, 1, 0, 1);

    float const ThrustPos[3] = {-0.16f, 0.008f, 0.0f};
    ThrustText = CreateTextControl("ThrustText", "Normal", ThrustPos, nullptr, 0.056f, 0.03f, 1.0f,
                                   0, 1, 0, 1);

    // 0x004d5298..0x004d52f9
    HalfExtents[0] = 0.1f;
    HalfExtents[1] = 0.02f;
    HalfExtents[2] = 0.0f;
    HitOffset[0] = 0.005f;
    HitOffset[1] = 0.0f;
    HitOffset[2] = 0.0f;

    // 0x004d52fc: FUN_005760d0(0x6d) on the handler at +0x364.
    RegisterEvent(0x6d);

    SetOffset(0.005f, -0.365f, -1.0f);
    SetPosition(Position);
    return Win3D::Create(NewParent, Data);
}

bool HUD_Core::OnEvent(unsigned MsgId)
{
    // LAB_004d5370.
    if (HudIsMousePickBlocked())
    {
        return false;
    }
    if (MsgId != 0x6d)
    {
        return false;
    }

    HUD_Status* const Status = G_HudStatus;
    HudTargetBase* Target = G_HudTargetPanel;
    unsigned char const bStatusOpen = Status->bExpanded;
    if (bStatusOpen == 0 && Target->bExpanded == 0)
    {
        // 0x004d53ab: both closed -- both open, the target panel re-read from DAT_00674b04.
        static_cast<EventHandler*>(Status)->OnEvent(0x6d);
        static_cast<EventHandler*>(G_HudTargetPanel)->OnEvent(0x6d);
        NnMainButtonOnModalOpen();
        return true;
    }

    // 0x004d53d7..0x004d5403: close whichever is open (the target flag read after the status
    // panel has handled the command, from a fresh DAT_00674b04 when the status panel was open).
    if (bStatusOpen != 0)
    {
        static_cast<EventHandler*>(Status)->OnEvent(0x6d);
        Target = G_HudTargetPanel;
    }
    if (Target->bExpanded != 0)
    {
        static_cast<EventHandler*>(Target)->OnEvent(0x6d);
    }
    NnMainButtonOnModalOpen();
    return true;
}

void HUD_Core::DrawBars(float DesignY, unsigned long Color, float Ratio)
{
    // FUN_004d5410. The surface size is DAT_00610850 / DAT_00610854 (FILD, signed).
    int const SurfaceWidth = static_cast<int>(G_RenderSurfaceWidth);
    int const SurfaceHeight = static_cast<int>(G_RenderSurfaceHeight);

    // 0x004d5428..0x004d548a
    float const HalfThickness =
        static_cast<float>(static_cast<double>(SurfaceHeight) * kHalfThicknessPerLine);
#ifdef FREELANCER_WIDESCREEN_SUPPORT
    // Port-only Hor+: the bars keep their 4:3 size (height-scaled) and stay centred on the
    // full surface below.
    int const SegmentWidth = static_cast<int>(WidescreenEquivalentSurfaceWidth() * kSegmentWidthA *
                                              kSegmentWidthB); // ftol
#else
    int const SegmentWidth = static_cast<int>(static_cast<double>(SurfaceWidth) * kSegmentWidthA *
                                              kSegmentWidthB); // ftol
#endif
    int const SpanInt = SegmentWidth << 4;
    double const SegmentWidthD = static_cast<double>(SegmentWidth);
    float const FirstChip = static_cast<float>(SegmentWidthD * kFirstChipScale);
    float const Span = static_cast<float>(SpanInt);
    float const HalfSpan = static_cast<float>(static_cast<double>(SpanInt) * kHalf);

    // 0x004d548e..0x004d54a7: FUN_00402d30(CRC("core_bars")), FUN_00402e30(&DAT_006101d8, 0, 0)
    // -- DAT_006101d8 is the constant {no texture, UV 0,0,1,1} descriptor (never written).
    ScreenQuadSetMaterialKey(DACOM_CRC::GetCRC32("core_bars"));
    ScreenQuadBeginBatchFrame(G_ScreenQuadDefaultFrame, 0, false);

    // 0x004d54ae..0x004d5511
    float Left = static_cast<float>(
        (static_cast<double>(SurfaceWidth / 2) - static_cast<double>(HalfSpan)) +
        SegmentWidthD * kHalf);
    float const Y0 = static_cast<float>(static_cast<double>(DesignY) * kDesignLineToScreen *
                                        static_cast<double>(SurfaceHeight));
    float Right = static_cast<float>(static_cast<double>(Left) + static_cast<double>(FirstChip));
    float const Y1 = static_cast<float>(static_cast<double>(HalfThickness) * 2.0 +
                                        static_cast<double>(Y0));
    float const FillEnd = static_cast<float>(
        static_cast<double>(Span) * static_cast<double>(Ratio) + static_cast<double>(Left));

    // 0x004d5515..0x004d5528
    if (Right > FillEnd)
    {
        Right = FillEnd;
    }

    // 0x004d552c..0x004d5636: one record per chip, appended inline into the node FUN_00402e30
    // returned (EBP) -- count + 1, growth to count + 1 + 0x20 past the capacity, then the 0x2c
    // record written at the old count with no test of its extents: {old left, Y0, right, Y1,
    // 1.0f, 1.0f, DAT_006101dc..DAT_006101e8 = 0, 0, 1, 1, the colour dword}.
    float const Step = static_cast<float>(SegmentWidth);
    int Chip = 0;
    do
    {
        float const ChipLeft = Left;
        double const NextLeft = static_cast<double>(Step) + static_cast<double>(Left);
        Left = static_cast<float>(NextLeft);
        FScreenQuadRecord Record;
        Record.X0 = ChipLeft;
        Record.Y0 = Y0;
        Record.X1 = Right;
        Record.Y1 = Y1;
        Record.DepthA = 1.0f;
        Record.DepthB = 1.0f;
        std::memcpy(&Record.U0, &G_ScreenQuadDefaultFrame[1], sizeof(float));
        std::memcpy(&Record.V0, &G_ScreenQuadDefaultFrame[2], sizeof(float));
        std::memcpy(&Record.U1, &G_ScreenQuadDefaultFrame[3], sizeof(float));
        std::memcpy(&Record.V1, &G_ScreenQuadDefaultFrame[4], sizeof(float));
        std::memcpy(&Record.ColorBits, &Color, sizeof(Record.ColorBits));
        ScreenQuadAppendRecord(Record);
        // TEST AH,0x41 / JZ: only a strictly greater (ordered) next left edge ends the row.
        if (NextLeft > static_cast<double>(FillEnd))
        {
            break;
        }
        double const NextRight = static_cast<double>(Step) + static_cast<double>(Right);
        Right = static_cast<float>(NextRight);
        if (NextRight > static_cast<double>(FillEnd))
        {
            Right = FillEnd;
        }
        ++Chip;
    } while (Chip < 0x10);

    // 0x004d563c..0x004d56ab: render state 0x17 saved, forced to 8 around the flush and put
    // back, whatever GetRenderState returned. (Retail reads the saved value even when the call
    // fails; the port starts it at 0 instead of stack garbage.)
    IRenderPipeline* const Pipe = DALib::Pipeline;
    int Saved = 0;
    Pipe->GetRenderState(D3DRS_ZFUNC, &Saved);
    Pipe->SetRenderState(D3DRS_ZFUNC, 8);
    ScreenQuadFlush(Pipe, G_HudCoreListKey); // FUN_00403ea0(Pipe, DAT_00674980)
    Pipe->SetRenderState(D3DRS_ZFUNC, Saved);
}

void HUD_Core::Draw()
{
    // LAB_004d56d0 (this = the WinControl face).
    if (IsSpaceInitInProgress())                        // FUN_0054bd40
    {
        return;
    }
    if (GetGameOption(kGameOptInterfaceGroup) == 0)     // FUN_005792b0(6)
    {
        return;
    }
    if (GetGameOption(kGameOptHud) == 0)                // FUN_005792b0(0xa)
    {
        return;
    }
    if (GetClientMouseCursorDrawFlag() == 0)            // FUN_0041dd90
    {
        return;
    }
    if (GetPlayerObjRW() == nullptr)                    // FUN_0054baf0
    {
        return;
    }
    if (IsLetterboxVisible())                           // FUN_0041a3e0
    {
        return;
    }

    Background2->Draw();
    DrawStaticMesh();                                   // FUN_005a06a0

    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW());
    if (Player == nullptr)
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
    CShip* const Ship = static_cast<CShip*>(Object);

    // 0x004d5779..0x004d57ba: power. TEST AH,0x41 / JP divides when MaxPower is above 0 or NaN.
    float PowerRatio = 0.0f;
    if (!(Ship->MaxPower <= 0.0f))
    {
        PowerRatio = static_cast<float>(static_cast<double>(Ship->Power) /
                                        static_cast<double>(Ship->MaxPower));
    }
    DrawBars(735.0f, 0xffa7a15eu, PowerRatio);

    // 0x004d57bf..0x004d584e: shield, through the IObjInspectImpl's CBase (+0xc) object.
    CBase* const Base = static_cast<CBase*>(static_cast<IObjInspectImpl*>(Player));
    float ShieldMax = 0.0f;
    float ShieldCur = 0.0f;
    if (Base != nullptr && Base->Object != nullptr &&
        (static_cast<unsigned>(Base->Object->ObjectClass) & 0x103u) == 0x103u)
    {
        HudSampleShieldHitPoints(static_cast<CEqObj*>(Base->Object), &ShieldMax, &ShieldCur,
                                 nullptr);
    }
    float ShieldRatio = 0.0f;
    if (ShieldMax > 0.0f)
    {
        double const Quotient = static_cast<double>(ShieldCur) / static_cast<double>(ShieldMax);
        ShieldRatio = static_cast<float>(Quotient);
        if (Quotient > 1.0)
        {
            ShieldRatio = 1.0f;
        }
    }
    DrawBars(745.0f, 0xff3e3db5u, ShieldRatio);

    // 0x004d5853..0x004d5898: hull.
    if (Base == nullptr)
    {
        return;
    }
    CObject* const HullObject = Base->Object;
    if (HullObject == nullptr)
    {
        return;
    }
    if ((static_cast<unsigned>(HullObject->ObjectClass) & 0x103u) != 0x103u)
    {
        return;
    }
    float const Hull = static_cast<CSimple*>(HullObject)->get_relative_health();
    DrawBars(755.0f, 0xff782a33u, Hull);

    // 0x004d589d..0x004d5966: speed along the ship's forward axis (orientation column 2), the
    // tradelane speed in a lane, capped at 300 in cruise, then pushed away from 0 to +-1.
    float Speed = 0.0f;
    bool bSkipPositive = false;
    if (Ship->is_using_tradelane())
    {
        Speed = Ship->get_tradelane_speed();
    }
    else
    {
        float const AxisX = Ship->Orientation.E[0][2]; // +0x10
        float const AxisY = Ship->Orientation.E[1][2]; // +0x1c
        float const AxisZ = Ship->Orientation.E[2][2]; // +0x28
        Vector const Velocity = Ship->get_velocity();  // slot +0x54
        double Dot = static_cast<double>(AxisZ) * static_cast<double>(Velocity.z) +
                     static_cast<double>(AxisY) * static_cast<double>(Velocity.y);
        Dot += static_cast<double>(AxisX) * static_cast<double>(Velocity.x);
        Speed = static_cast<float>(-Dot);
        if (Ship->is_cruise_active() && Speed > 300.0f)
        {
            Speed = 300.0f;
            bSkipPositive = true;
        }
    }

    bool bRounded = false;
    if (!bSkipPositive && Speed > 1.0e-5f && Speed < 1.0f)
    {
        Speed = 1.0f;
        bRounded = true;
    }
    if (!bRounded && static_cast<double>(Speed) < -1.0e-5 && Speed > -1.0f)
    {
        Speed = -1.0f;
    }
    int const SpeedInt = static_cast<int>(Speed); // ftol

    char Text[32];
    if (LastVelocity != SpeedInt)
    {
        LastVelocity = SpeedInt;
        if (SpeedInt > 999)
        {
            VelocityText->SetProperty(0x1e, 0x526, 0);
        }
        else
        {
            sprintf(Text, "%d", SpeedInt);
            // 0x004d59af..0x004d59ba: FUN_0058a580(Text) on +0x33c. Text is used as a printf
            // format by the NumberString child (FUN_004159a0 -> slot 12 FUN_00414db0).
            static_cast<NnTextControl*>(VelocityText)->SetNumberText(Text);
        }
    }

    // 0x004d59bf..0x004d5a49: thrust percentage (CShip +0x2a0 / +0x2a4).
    double ThrustRatio = 0.0;
    if (!(Ship->MaxThrustPower <= 0.0f))
    {
        ThrustRatio = (double)Ship->ThrustPower / (double)Ship->MaxThrustPower;
    }
    int const Thrust = static_cast<int>(ThrustRatio * 100.0); // FMUL 100.0f, ftol
    if (LastThrust != Thrust)
    {
        LastThrust = Thrust;
        if (Thrust < 25)
        {
            ThrustText->SetProperty(0x33, reinterpret_cast<uintptr_t>(&G_NnPaletteArgb_679ba8), 1);
        }
        else
        {
            ThrustText->SetProperty(0x33, reinterpret_cast<uintptr_t>(&G_NnPaletteArgb_679bb0), 1);
        }
        sprintf(Text, "%d%%%%", Thrust);
        // 0x004d5a3b..0x004d5a49: FUN_0058a580(Text) on +0x340; "NN%%" prints as "NN%" through
        // the NumberString's own printf.
        static_cast<NnTextControl*>(ThrustText)->SetNumberText(Text);
    }

    VelocityText->Draw();
    ThrustText->Draw();
    Slot39();
}

// ---------------------------------------------------------------------------------------------
// HUD_Status
// ---------------------------------------------------------------------------------------------

namespace
{
    // 0x004da383..0x004da5b9: the 0x3b command ids the ctor registers on its handler, in order.
    unsigned const kStatusCommandIds[0x3b] = {
        0x5e, 0xbc, 0xc0, 0xc1, 0xbd, 0xbe, 0x3a, 0x3c, 0x3b, 0x3e, 0x3d, 0x56, 0x57, 0xa1, 0xa2,
        0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0x97, 0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d,
        0x9e, 0x9f, 0xa0, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6,
        0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x8f, 0x90, 0x0f, 0xb7, 0xb8, 0xb9, 0xba, 0x6f,
    };

    // .rdata 0x005d86b8: the wide placeholder both count texts start with (pushed at 0x004dadf0
    // and 0x004daec3). Raw bytes 31 00 32 00 33 00 00 00 -- L"123", eight bytes, 4-aligned;
    // Ghidra types it as a one-byte DAT showing only the '1'. Property 0x1d reads a wchar_t*.
    wchar_t const kStatusCountPlaceholder[] = L"123";
}

FHudStatusCaption::FHudStatusCaption()
{
    // 0x004da1cf / 0x004da1d5.
    Text = nullptr;
    Capacity = 0;
}

FHudStatusCaption::~FHudStatusCaption()
{
    // 0x004da7e6..0x004da803: both words are zeroed only when there was text to free.
    if (Text != nullptr)
    {
        free(Text);
        Text = nullptr;
        Capacity = 0;
    }
}

HUD_Status::HUD_Status(INI_Reader& /*Reader*/)
    : Byte500(1)
    , ActiveWeaponGroup(0)
    , Byte584(0)
    , Byte585(0)
    , Byte586(0)
    , Field588(1.0f)
    , Field5a0(1)
    , Field5a4(1)
    , Byte5b8(0)
    , bTurretView(0)
    , bRearView(0)
    , WeaponTopRow(0)
    , Byte5cc(1)
    , Byte5cd(0)
{
    // FUN_004da170: HUD_Target_Base FUN_004e6020, ButtonScaler FUN_0055e340 at +0x4e0, the
    // vectors at +0x58c (Weapons) / +0x5dc (HpRowCache) emptied and the caption at +0x5d0
    // zeroed (their default constructors), the vptrs, then 0x004da229:
    // publish this in DAT_00674aa0. HomePosition, Field5a0 / Field5a4 and the HUDItem name are
    // written before the Create call; the rest after it (Create reads none of them).
    G_HudStatus = this;

    HomePosition.X = 0.295f;   // +0x4d4 {0.295, -0.367, 0}
    HomePosition.Y = -0.367f;
    HomePosition.Z = 0.0f;

    HUDItem::Name = "Test_Status";
    // 0x004da2b5: a direct (non-virtual) call of FUN_004da930 with no parent and no data.
    HUD_Status::Create(nullptr, nullptr);
    Win3D::SetName("HUD_Status");
    ClassName = "HUD_Status";

    HitCenter.X = 0.418f;      // +0x4b4 {0.418, -0.29, -1}
    HitCenter.Y = -0.29f;
    HitCenter.Z = -1.0f;
    HitHalfWidth = 0.11f;      // +0x4c0
    HitHalfHeight = 0.11f;     // +0x4c4

    // 0x004da5c4..0x004da5da: FUN_005760d0 for each id, on the handler at +0x384.
    for (unsigned Index = 0; Index < 0x3b; ++Index)
    {
        RegisterEvent(kStatusCommandIds[Index]);
    }
}

HUD_Status::~HUD_Status()
{
    // FUN_004da740: the vptrs put back, then 0x004da799: DAT_00674aa0 = 0. It frees HpRowCache
    // (+0x5dc), the caption (+0x5d0) and Weapons (+0x58c) next -- here the member destructors,
    // in that same (reverse declaration) order; the ButtonScaler list at +0x4e8 and the
    // HUD_Target_Base members (camera
    // watcher, part cache, +0x398 watcher, handler, mover list, WinControl, HUDItem watcher) go
    // through the base destructors.
    G_HudStatus = nullptr;
}

void HUD_Status::Slot13(bool bHide)
{
    // LAB_004db120: the byte is MOVZX'd into A of SetProperty(1, A, 0).
    uintptr_t const Hide = static_cast<uintptr_t>(bHide ? 1u : 0u);
    if (Field5a0 != 0)
    {
        // 0x004db135..0x004db173: +0x508, +0x530, +0x558 of each row.
        for (int Index = 0; Index < 10; ++Index)
        {
            WeaponText[Index]->SetProperty(1, Hide, 0);
            WeaponNumberText[Index]->SetProperty(1, Hide, 0);
            AmmoText[Index]->SetProperty(1, Hide, 0);
        }
        WeaponListScroll->SetProperty(1, Hide, 0);
    }
    ViewPanel->SetProperty(1, Hide, 0);
    CloseButton->SetProperty(1, Hide, 0);
}

bool HUD_Status::Create(Win3D* NewParent, void* Data)
{
    // FUN_004da930 (this = the WinControl face). No result is tested for null.
    float const Zero[3] = {0.0f, 0.0f, 0.0f};

    // 0x004da93a..0x004da9ba
    float const BackgroundPos[3] = {0.3387f, -0.243f, -0.91f};
    ViewPanel = CreateStaticControl("StatusBackground", "Interface\\hud\\HUD_shipinfo.cmp",
                                    BackgroundPos, nullptr, Zero, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0,
                                    Zero);

    // 0x004da9c0..0x004daa3e
    float const MinimizedPos[3] = {0.4101f, -0.37f, -1.0f};
    MinimizedFrame = CreateStaticControl("StatusMinimizedFrame",
                                         "Interface\\hud\\HUD_shipinfo_min.cmp", MinimizedPos,
                                         nullptr, Zero, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, Zero);

    // 0x004daa44..0x004daabd
    HalfExtents[0] = 0.105f;
    HalfExtents[1] = 0.02f;
    HalfExtents[2] = 0.0f;
    HitOffset[0] = 0.4f;
    HitOffset[1] = -0.371f;
    HitOffset[2] = 0.0f;

    // 0x004daac0..0x004dab09: the open button sits at HomePosition (+0x4d4).
    float const OpenHalf[3] = {0.02f, 0.02f, 0.0f};
    ToggleButton = CreateButtonControl("HudStatusOpen", "interface\\hud\\hud_ShipInfo_button.3db",
                                       nullptr, "ui_window_open", nullptr, &HomePosition.X,
                                       nullptr, OpenHalf, 0, 1, 0, 0, 0x3ff, 0, 0);
    ToggleButton->SetProperty(0x36, 1, 0);

    // 0x004dab0f..0x004dacc1: ten rows. Step = float(Index * 0.018f) (FILD, FMUL DAT_005d3ee8,
    // FSTP dword). The number column is -0.199f (DAT_005d8768) - Step; the name and ammo
    // columns are -0.199 (the double DAT_005d8750, FSUBR qword) - Step, stored once as a float
    // and reused. The double subtraction rounds to the same float as the x87 extended one for
    // all ten rows (checked offline, every Index 0..9).
    // 0.016999999f is 0x3c8b4395 (the name columns); the ammo column's 0.017f is 0x3c8b4396.
    char ControlName[64];
    for (int Index = 0; Index < 10; ++Index)
    {
        sprintf(ControlName, "WeaponNumberText%d", Index);
        float const Step = static_cast<float>(Index) * 0.018f;
        float const NumberPos[3] = {0.27f, -0.199f - Step, 0.0f};
        WeaponNumberText[Index] = CreateTextControl(ControlName, "HudSmall", NumberPos, nullptr,
                                                    0.02f, 0.016999999f, 1.0f,
                                                    static_cast<unsigned>(0x515 + Index), 1, 0, 1);
        WeaponNumberText[Index]->SetProperty(0x24, 1, 0);

        sprintf(ControlName, "WeaponText%d", Index);
        float const RowY = static_cast<float>(-0.199 - static_cast<double>(Step));
        float const TextPos[3] = {0.288f, RowY, 0.0f};
        WeaponText[Index] = CreateTextControl(ControlName, "HudSmall", TextPos, nullptr, 0.165f,
                                              0.016999999f, 1.0f, 0, 0, 0, 1);
        WeaponText[Index]->SetProperty(0x24, 1, 0);

        sprintf(ControlName, "AmmoText%d", Index);
        float const AmmoPos[3] = {0.458f, RowY, 0.0f};
        AmmoText[Index] = CreateTextControl(ControlName, "HudSmall", AmmoPos, nullptr, 0.04f, 0.017f,
                                            1.0f, 0x514, 0, 0, 1);
        AmmoText[Index]->SetProperty(0x24, 1, 0);

        WeaponNumberText[Index]->SetProperty(1, 1, 0);
        WeaponText[Index]->SetProperty(1, 1, 0);
        AmmoText[Index]->SetProperty(1, 1, 0);
    }

    float const SmallHalf[3] = {0.014f, 0.014f, 0.0f};

    // 0x004dacc7..0x004dad42
    float const ClosePos[3] = {0.383f, -0.307f, -0.5f};
    CloseButton = CreateButtonControl("StatusCloseButton", "interface\\NeuroNet\\Inventory\\x.3db",
                                      nullptr, "ui_select_item", nullptr, ClosePos, nullptr,
                                      SmallHalf, 0, 1, 0, 0, 0x409, 0, 0);

    // 0x004dad2e..0x004dae01
    float const RepairPos[3] = {0.341f, -0.371f, 0.0f};
    RepairButton = CreateButtonControl("HudRepairButton", "interface\\hud\\HUD_repairactivate.3db",
                                       nullptr, "ui_item_select", nullptr, RepairPos, nullptr,
                                       SmallHalf, 0, 1, 0, 0, 0x43e, 0, 0x8f);
    float const RepairCountPos[3] = {0.347f, -0.359f, 0.0f};
    RepairCountText = CreateTextControl("RepairCountText", "HudSmall", RepairCountPos, nullptr,
                                        0.034f, 0.03f, 0.8f, 0, 1, 0x4b2, 1);
    RepairCountText->SetProperty(0x1d, reinterpret_cast<uintptr_t>(kStatusCountPlaceholder),
                                 0x10); // 0x004dadee..0x004dae01, L"123" at 0x005d86b8

    // 0x004dae07..0x004daed4
    float const BatteryPos[3] = {0.397f, -0.371f, 0.0f};
    UseBatteryButton = CreateButtonControl("UseBatteryButton",
                                           "interface\\hud\\HUD_shieldgaugeicon.3db", nullptr,
                                           "ui_item_select", nullptr, BatteryPos, nullptr,
                                           SmallHalf, 0, 1, 0, 0, 0x4b5, 0, 0x90);
    float const BatteryCountPos[3] = {0.404f, -0.359f, 0.0f};
    BatteryCountText = CreateTextControl("BatteryCountText", "HudSmall", BatteryCountPos, nullptr,
                                         0.034f, 0.03f, 0.8f, 0, 1, 0x4b3, 1);
    BatteryCountText->SetProperty(0x1d, reinterpret_cast<uintptr_t>(kStatusCountPlaceholder),
                                  0x10); // 0x004daec1..0x004daed4, L"123" at 0x005d86b8

    // 0x004daeda..0x004daf25: the scroll bar, then HUDItem slot 13 (through the vftable) with 1.
    float const ScrollPos[3] = {0.49f, -0.234f, -1.0f};
    WeaponListScroll = CreateUIScroll("WeaponListScroll", ScrollPos, 0.016f, 0.118f, 0, 5, 10, 0);
    Slot13(true);

    // 0x004daf28..0x004dafdf: the special-mode captions, both with Flags &= 0xfc.
    float const SpecialPos[3] = {-0.513f, -0.29f, 0.0f};
    SpecialModeText = CreateTextControl("SpecialModeText", "PageHeader", SpecialPos, nullptr,
                                        1.022f, 0.03f, 1.5f, 0, 1, 0, 1);
    float const CancelPos[3] = {-0.513f, -0.321f, 0.0f};
    SpecialModeTextCancel = CreateTextControl("SpecialModeTextCancel", "Normal", CancelPos,
                                              nullptr, 1.028f, 0.03f, 1.3f, 0, 1, 0, 1);
    SpecialModeText->Flags &= 0xfc;
    SpecialModeTextCancel->Flags &= 0xfc;

    // 0x004dafe2
    RebuildWeaponList();

    // 0x004dafe7..0x004db054
    float const LootPos[3] = {0.21f, -0.37f, 0.0f};
    LootButton = CreateButtonControl("LootButton", "interface\\HUD\\HUD_loot_bw.3db", nullptr,
                                     "ui_select_item", nullptr, LootPos, nullptr, SmallHalf, 0, 1,
                                     0, 0, 0x3a2, 0, 0x6f);
    LootButton->Flags &= 0xfc;

    // 0x004db057..0x004db0c0: slot 31 SetOffset(0, 0, -1) by value, slot 5 SetPosition(+0x60),
    // FUN_004b5060.
    SetOffset(0.0f, 0.0f, -1.0f);
    SetPosition(Position);
    return Win3D::Create(NewParent, Data);
}

int HUD_Status::FindHardpointRow(char const* Hardpoint)
{
    // FUN_004dd730. 0x004dd749..0x004dd79b: the cache first, matched case-sensitively --
    // basic_string::compare(0, size(), Hardpoint, strlen(Hardpoint)) == 0. strlen runs on every
    // entry, so a null name with a non-empty cache faults here, as in retail.
    for (std::vector<FHudStatusHpRow>::const_iterator It = HpRowCache.begin();
         It != HpRowCache.end(); ++It)
    {
        if (It->Hardpoint.compare(0, It->Hardpoint.size(), Hardpoint, std::strlen(Hardpoint)) == 0)
        {
            return It->Row;
        }
    }

    // 0x004dd7a8..0x004dd801: no name, no selected ship archetype (FUN_004c3e10) or no Ship
    // record -> 0, nothing cached.
    if (Hardpoint == nullptr)
    {
        return 0;
    }
    unsigned const ShipId = GetPlayerShipArchetypeIdRetail();
    if (ShipId == 0)
    {
        return 0;
    }
    Archetype::Ship const* const ShipArch = Archetype::GetShip(ShipId);
    if (ShipArch == nullptr)
    {
        return 0;
    }

    // 0x004dd806..0x004dd8a3: every hp type (record +0x00) whose hardpoint list names Hardpoint
    // (DACOM stricmp), once per matching name.
    std::vector<int> Types;
    int const GroupCount = static_cast<int>(ShipArch->LegalHps.size());
    for (int Group = 0; Group < GroupCount; ++Group)
    {
        Archetype::LegalHpTypes const& Legal = ShipArch->LegalHps[Group];
        int const NameCount = static_cast<int>(Legal.Hardpoints.size());
        for (int NameIndex = 0; NameIndex < NameCount; ++NameIndex)
        {
            if (_stricmp(Hardpoint, Legal.Hardpoints[NameIndex].Value) == 0)
            {
                Types.push_back(static_cast<int>(Legal.Type));
            }
        }
    }
    if (Types.empty())
    {
        return 0; // 0x004dd8a5..0x004dd8bc -- nothing cached either
    }

    // 0x004dd8c2..0x004dd901: the row of the caption table (FUN_00584710 rows, FUN_00584720
    // type) holding the largest matched type; signed compares, the first such row wins.
    int BestRow = 0;
    int BestType = 0;
    int const RowCount = ShipViewHpTypeTableCount();
    for (int Row = 0; Row < RowCount; ++Row)
    {
        int const Type = ShipViewHpTypeTableType(Row);
        for (int Index = 0; Index < static_cast<int>(Types.size()); ++Index)
        {
            if (Type == Types[Index] && Type > BestType)
            {
                BestType = Type;
                BestRow = Row;
            }
        }
    }

    // 0x004dd903..0x004dd97e: cache {Hardpoint, BestRow} at the end (FUN_004df080).
    FHudStatusHpRow Entry;
    Entry.Hardpoint.assign(Hardpoint, std::strlen(Hardpoint));
    Entry.Row = BestRow;
    HpRowCache.insert(HpRowCache.end(), 1, Entry);
    return BestRow;
}

void HUD_Status::RebuildWeaponList()
{
    // FUN_004dd9e0. 0x004dda01..0x004dda1d: FUN_004dea90(begin, end) empties Weapons.
    Weapons.erase(Weapons.begin(), Weapons.end());

    // 0x004dda22..0x004ddbc4: the player ship's launchers (traverser mask 0xe0), gathered in a
    // local vector. The walk stops at the first item CELauncher::cast rejects.
    std::vector<FHudStatusWeapon> Found;
    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW()); // FUN_0054baf0
    if (Player != nullptr)
    {
        CObject* const Object = const_cast<CObject*>(Player->cobject()); // slot +0x150
        if (Object != nullptr &&
            (static_cast<unsigned>(Object->ObjectClass) & 0x103u) == 0x103u)
        {
            CEqObj* const Owner = static_cast<CEqObj*>(Object);
            CEquipTraverser Walk(EquipTypeMineDropper | EquipTypeCmDropper | EquipTypeGun);
            CELauncher* Launcher = CELauncher::cast(Owner->EquipManager.Traverse(Walk));
            while (Launcher != nullptr)
            {
                Archetype::Equipment* const Arch = Launcher->Arch; // +0x0c, read first
                EquipDesc Desc;
                // EBP: the hardpoint when GetEquipDesc (slot +0x28) succeeds, else null.
                char const* HardpointName = nullptr;
                if (Launcher->GetEquipDesc(Desc))
                {
                    HardpointName = Desc.Hardpoint;
                }
                FHudStatusWeapon Row;
                Row.SortKey = FindHardpointRow(HardpointName);
                Row.SubId = Launcher->Id;
                Row.bActive = Launcher->IsActive() ? 1 : 0;
                Row.IdsName = Arch->IdsName;
                Row.AmmoCount = Launcher->GetAmmoCount(); // slot +0xa0
                // 0x004ddb39..0x004ddb63: strlen on the name, no null test.
                Row.Hardpoint.assign(HardpointName, std::strlen(HardpointName));
                Found.insert(Found.end(), Row); // FUN_004de8b0
                Launcher = CELauncher::cast(Owner->EquipManager.Traverse(Walk));
            }
        }
    }

    // 0x004ddbca..0x004ddc33: an index vector 0..Count-1 (FUN_004deb70 per element).
    int const Count = static_cast<int>(Found.size());
    std::vector<int> Order;
    for (int Index = 0; Index < Count; ++Index)
    {
        Order.insert(Order.end(), Index);
    }

    // 0x004ddc3d..0x004ddcd7: bubble sort of the indices, swapping a pair when the first has
    // the smaller SortKey or, on equal keys, the smaller name (DACOM stricmp < 0) -- so the
    // indices end up in descending order.
    int const Last = Count - 1;
    bool bSorted = true;
    do
    {
        bSorted = true;
        if (Last <= 0)
        {
            break;
        }
        for (int Index = 0; Index < Last; ++Index)
        {
            FHudStatusWeapon const& First = Found[Order[Index]];
            FHudStatusWeapon const& Second = Found[Order[Index + 1]];
            bool bSwap = false;
            if (First.SortKey < Second.SortKey)
            {
                bSwap = true;
            }
            else if (First.SortKey == Second.SortKey &&
                     _stricmp(First.Hardpoint.c_str(), Second.Hardpoint.c_str()) < 0)
            {
                bSwap = true;
            }
            if (bSwap)
            {
                int const Swap = Order[Index];
                Order[Index] = Order[Index + 1];
                Order[Index + 1] = Swap;
                bSorted = false;
            }
        }
    } while (!bSorted);

    // 0x004ddcdd..0x004dddfc: copied into Weapons from the last index down, each inserted at
    // the end (FUN_004ded10), the name re-read through c_str().
    for (int Index = Last; Index >= 0; --Index)
    {
        FHudStatusWeapon const& Source = Found[Order[Index]];
        FHudStatusWeapon Copy;
        Copy.SubId = Source.SubId;
        Copy.AmmoCount = Source.AmmoCount;
        Copy.IdsName = Source.IdsName;
        Copy.bActive = Source.bActive;
        Copy.Hardpoint = Source.Hardpoint.c_str();
        Copy.SortKey = Source.SortKey;
        Weapons.insert(Weapons.end(), 1, Copy);
    }
    // 0x004dde02..: the index and row vectors are freed on the way out.
}

void HUD_Status::RefreshWeaponRows()
{
    // FUN_004de0a0.
    RebuildWeaponList();

    // 0x004de0b2..0x004de0f7: the top row is clamped (JBE, unsigned) to Count - 10, or to 0
    // while the list fits.
    int const Count = static_cast<int>(Weapons.size());
    int MaxTop = 0;
    if (Count > 10)
    {
        MaxTop = Count - 10;
    }
    if (static_cast<unsigned>(WeaponTopRow) > static_cast<unsigned>(MaxTop))
    {
        WeaponTopRow = MaxTop;
    }

    // 0x004de0fd..0x004de1f7: the cursor ray -- the cursor pixel onto the near plane of the Win3D
    // camera DAT_0067dbf8, through a copy of its orientation DAT_0067dbfc (this TU's lazy
    // 3DMathEngine DAT_00674a9c, slot +0x2c), then scaled by 1.0f / length.
    FSpaceCamera* const Cam = Win3DGetCamera();
    int CursorX = 0;
    int CursorY = 0;
    GetClientSoftwareCursor(&CursorX, &CursorY, nullptr); // DAT_00616840 / DAT_00616844
    FVector Local;
    Local.X = static_cast<float>((static_cast<double>(CursorX) - Cam->ScreenCenterX) /
                                 Cam->XScale * Cam->ZNear);
    Local.Y = static_cast<float>((static_cast<double>(CursorY) - Cam->ScreenCenterY) /
                                 Cam->YScale * Cam->ZNear);
    Local.Z = -Cam->ZNear;
    FMatrix Orientation;
    memcpy(&Orientation, Cam->World, sizeof(Orientation));
    if (G_HudStatusMath == nullptr)
    {
        G_HudStatusMath = Acquire3DMathEngine(); // FUN_005b73e0
    }
    FVector World;
    G_HudStatusMath->TransformMatrix(World, Orientation, Local);
    double const Inv = 1.0 / sqrt(static_cast<double>(World.X) * World.X +
                                  static_cast<double>(World.Y) * World.Y +
                                  static_cast<double>(World.Z) * World.Z);
    float Ray[3];
    Ray[0] = static_cast<float>(World.X * Inv);
    Ray[1] = static_cast<float>(World.Y * Inv);
    Ray[2] = static_cast<float>(World.Z * Inv);

    // 0x004de1ad..0x004de2df: up to ten rows from WeaponTopRow (signed compares against Count).
    // A row is the idle colour DAT_00679bb0, or DAT_00679bac when its launcher is active; when
    // the ray hits any of its three texts (slot 4, number / name / ammo, first hit wins) and
    // the mouse pick is not blocked (FUN_0045a740), the colour is pulsed (FUN_00561f20). All
    // three texts then get SetProperty(0x33, &Colour, 1).
    int Row = WeaponTopRow;
    if (Row >= Count)
    {
        return;
    }
    for (int Slot = 0;;)
    {
        unsigned long Color = G_NnPaletteArgb_679bb0;
        if (Weapons[Row].bActive != 0)
        {
            Color = G_NnPaletteArgb_679bac;
        }
        if (WeaponNumberText[Slot]->HitTest(Ray) || WeaponText[Slot]->HitTest(Ray) ||
            AmmoText[Slot]->HitTest(Ray))
        {
            if (!HudIsMousePickBlocked())
            {
                unsigned long Pulsed = 0;
                NnUiPulseBlendFromArgb(&Pulsed, Color);
                Color = Pulsed;
            }
        }
        WeaponNumberText[Slot]->SetProperty(0x33, reinterpret_cast<uintptr_t>(&Color), 1);
        WeaponText[Slot]->SetProperty(0x33, reinterpret_cast<uintptr_t>(&Color), 1);
        AmmoText[Slot]->SetProperty(0x33, reinterpret_cast<uintptr_t>(&Color), 1);

        ++Slot;
        if (Slot >= 10)
        {
            break;
        }
        ++Row;
        if (Row >= Count)
        {
            break;
        }
    }
}

void HUD_Status::Draw()
{
    // FUN_004db2c0 (this = the WinControl face).
    if (IsSpaceInitInProgress()) // FUN_0054bd40
    {
        return;
    }
    if (IsLetterboxVisible()) // FUN_0041a3e0
    {
        return;
    }

    // 0x004db2d5..0x004db305: while either animation (mover +0x364, scaler +0x4e0; FUN_0055e280)
    // runs, the loot button draws itself (slot 13) when it exists and its Flags bit 1 is set --
    // the only null test here is retail's.
    if ((ButtonMover::IsActive() || ButtonScaler::IsActive()) && LootButton != nullptr &&
        (LootButton->Flags & 0x02) != 0)
    {
        LootButton->Draw();
    }

    // 0x004db308..0x004db320: while the scaler runs only the background draws (tail jump).
    if (ButtonScaler::IsActive())
    {
        ViewPanel->Draw();
        return;
    }

    // 0x004db323..0x004db32e: FUN_004de0a0 on the HUDItem face, then a tail jump into
    // HUD_Target_Base's Draw FUN_004e7890.
    RefreshWeaponRows();
    HudTargetBase::Draw();
}

void HUD_Status::SetTarget(IObjRW* NewTarget)
{
    // LAB_004db0d0: FUN_004c81a0 directly, then HUDItem slot 11 (SetViewObject) through the
    // vftable, then FUN_004f5520 on DAT_00674c0c with the view watcher's object (+0x398; the
    // watched pointer - 8, or 0).
    HUDItem::SetTarget(NewTarget);
    SetViewObject(NewTarget);
    G_WeaponGroups.SetShip(ObjRwFromWatchable(const_cast<Watchable*>(ViewObject.target())));
}

void HUD_Status::Slot12()
{
    // LAB_004dccf0 (this = the HUDItem face). Nothing while either animation runs.
    if (ButtonMover::IsActive()) // FUN_0055e280 on +0x364
    {
        return;
    }
    if (ButtonScaler::IsActive()) // on +0x4e0
    {
        return;
    }

    if (bExpanded == 0)
    {
        // 0x004dcd30..0x004dcee9: opening, stage 1 -- the minimized controls slide down to
        // -0.4 over 0.3 s (DAT_005d8488); OnMoveFinished continues.
        ButtonMover::Start(0.3f); // FUN_0055e120
        float const ToggleEnd[3] = {0.371f, -0.27f, 0.0f};
        ButtonMover::AddButton(ToggleButton, &HomePosition.X, ToggleEnd); // FUN_0055dee0
        float const FrameStart[3] = {0.4101f, -0.37f, -1.0f};
        float const FrameEnd[3] = {0.4101f, -0.4f, -1.0f};
        ButtonMover::AddButton(MinimizedFrame, FrameStart, FrameEnd);
        float const RepairStart[3] = {0.341f, -0.371f, 0.0f};
        float const RepairEnd[3] = {0.341f, -0.4f, 0.0f};
        ButtonMover::AddButton(RepairButton, RepairStart, RepairEnd);
        float const RepairCountStart[3] = {0.347f, -0.359f, 0.0f};
        float const RepairCountEnd[3] = {0.347f, -0.4f, 0.0f};
        ButtonMover::AddButton(RepairCountText, RepairCountStart, RepairCountEnd);
        float const BatteryStart[3] = {0.397f, -0.371f, 0.0f};
        float const BatteryEnd[3] = {0.397f, -0.4f, 0.0f};
        ButtonMover::AddButton(UseBatteryButton, BatteryStart, BatteryEnd);
        float const BatteryCountStart[3] = {0.404f, -0.359f, 0.0f};
        float const BatteryCountEnd[3] = {0.404f, -0.4f, 0.0f};
        ButtonMover::AddButton(BatteryCountText, BatteryCountStart, BatteryCountEnd);
        Field588 = 1.0f; // DAT_005d8484
        return;
    }

    // 0x004dceea..0x004dcf69: closing, stage 1 -- HUDItem slot 13 with 1 through the vftable,
    // the background shrinks 1.0 -> 0.1 over 0.3 s (OnScaleFinished continues), and the weapon
    // rows and the scroll bar are hidden.
    Slot13(true);
    ButtonScaler::Start(0.3f); // FUN_0055e590
    ButtonScaler::AddButton(ViewPanel, 1.0f, 0.1f); // FUN_0055e360
    for (int Index = 0; Index < 10; ++Index)
    {
        WeaponNumberText[Index]->SetProperty(1, 1, 0);
        WeaponText[Index]->SetProperty(1, 1, 0);
        AmmoText[Index]->SetProperty(1, 1, 0);
    }
    WeaponListScroll->SetProperty(1, 1, 0);
}

void HUD_Status::OnMoveFinished()
{
    // LAB_004dcf80 (this = the ButtonMover face at +0x364).
    Field588 = 1.0f; // DAT_005d8484

    if (bExpanded == 0)
    {
        // 0x004dcf9b..0x004dcff8: opening, stage 2 -- HUDItem slot 13 with 1, the toggle button
        // and the minimized frame lose Flags bits 0..1, and the background grows 0.1 -> 1.0
        // over 0.3 s; then a tail jump into the scaler's Update FUN_0055e5e0.
        Slot13(true);
        ToggleButton->Flags &= 0xfc;
        MinimizedFrame->Flags &= 0xfc;
        ButtonScaler::Start(0.3f);
        ButtonScaler::AddButton(ViewPanel, 0.1f, 1.0f);
        ButtonScaler::Update();
        return;
    }

    // 0x004dcffd..0x004dd078: closing, done -- collapsed, session flag 2 cleared
    // (FUN_004c8120), and the minimized hit box back.
    bExpanded = 0;
    SetGameSessionFlags(2, false);
    HalfExtents[0] = 0.105f;
    HalfExtents[1] = 0.02f;
    HalfExtents[2] = 0.0f;
    HitOffset[0] = 0.4f;
    HitOffset[1] = -0.371f;
    HitOffset[2] = 0.0f;
}

void HUD_Status::OnScaleFinished()
{
    // LAB_004dd080 (this = the ButtonScaler face at +0x4e0).
    if (bExpanded == 0)
    {
        // 0x004dd092..0x004dd24c: opening, done -- expanded, session flag 2 set, HUDItem slot 13
        // with 0, the repair / battery controls placed on the panel (property 6), the weapon
        // rows and the scroll bar shown when Field5a0 is set, the minimized frame hidden, and
        // the expanded hit box.
        bExpanded = 1;
        SetGameSessionFlags(2, true);
        Slot13(false);
        float const RepairPos[3] = {0.3f, -0.171f, 0.0f};
        RepairButton->SetProperty(6, reinterpret_cast<uintptr_t>(RepairPos), 0);
        float const RepairCountPos[3] = {0.306f, -0.16f, 0.0f};
        RepairCountText->SetProperty(6, reinterpret_cast<uintptr_t>(RepairCountPos), 0);
        float const BatteryPos[3] = {0.357f, -0.171f, 0.0f};
        UseBatteryButton->SetProperty(6, reinterpret_cast<uintptr_t>(BatteryPos), 0);
        float const BatteryCountPos[3] = {0.365f, -0.16f, 0.0f};
        BatteryCountText->SetProperty(6, reinterpret_cast<uintptr_t>(BatteryCountPos), 0);
        if (Field5a0 != 0)
        {
            for (int Index = 0; Index < 10; ++Index)
            {
                WeaponNumberText[Index]->SetProperty(1, 0, 0);
                WeaponText[Index]->SetProperty(1, 0, 0);
                AmmoText[Index]->SetProperty(1, 0, 0);
            }
            WeaponListScroll->SetProperty(1, 0, 0);
        }
        MinimizedFrame->SetProperty(1, 1, 0);
        HalfExtents[0] = 0.131f;
        HalfExtents[1] = 0.117f;
        HalfExtents[2] = 0.0f;
        HitOffset[0] = 0.376f;
        HitOffset[1] = -0.267f;
        HitOffset[2] = 0.0f;
        return;
    }

    // 0x004dd24d..0x004dd4d5: closing, stage 2 -- the minimized frame shown, the repair /
    // battery controls put at -0.4 and, with the frame and the toggle button, slid back up
    // over 0.3 s; OnMoveFinished finishes.
    MinimizedFrame->SetProperty(1, 0, 0);
    ButtonMover::Start(0.3f);
    float const ToggleStart[3] = {0.371f, -0.27f, 0.0f};
    ButtonMover::AddButton(ToggleButton, ToggleStart, &HomePosition.X);
    float const RepairLow[3] = {0.341f, -0.4f, 0.0f};
    RepairButton->SetProperty(6, reinterpret_cast<uintptr_t>(RepairLow), 0);
    float const RepairCountLow[3] = {0.347f, -0.4f, 0.0f};
    RepairCountText->SetProperty(6, reinterpret_cast<uintptr_t>(RepairCountLow), 0);
    float const BatteryLow[3] = {0.397f, -0.4f, 0.0f};
    UseBatteryButton->SetProperty(6, reinterpret_cast<uintptr_t>(BatteryLow), 0);
    float const BatteryCountLow[3] = {0.404f, -0.4f, 0.0f};
    BatteryCountText->SetProperty(6, reinterpret_cast<uintptr_t>(BatteryCountLow), 0);
    float const RepairHome[3] = {0.341f, -0.371f, 0.0f};
    ButtonMover::AddButton(RepairButton, RepairLow, RepairHome);
    float const RepairCountHome[3] = {0.347f, -0.359f, 0.0f};
    ButtonMover::AddButton(RepairCountText, RepairCountLow, RepairCountHome);
    float const BatteryHome[3] = {0.397f, -0.371f, 0.0f};
    ButtonMover::AddButton(UseBatteryButton, BatteryLow, BatteryHome);
    float const BatteryCountHome[3] = {0.404f, -0.359f, 0.0f};
    ButtonMover::AddButton(BatteryCountText, BatteryCountLow, BatteryCountHome);
    float const FrameLow[3] = {0.4101f, -0.4f, -1.0f};
    float const FrameHome[3] = {0.4101f, -0.37f, -1.0f};
    ButtonMover::AddButton(MinimizedFrame, FrameLow, FrameHome);
    Field588 = 1.0f;
}

void HUD_Status::ToggleLauncher(unsigned short SubId)
{
    // FUN_004db930 (this = the HUDItem face). Only a launcher of the target.
    CEquip* const Equip = FindTargetEquip(SubId); // FUN_004e7e20
    if (Equip == nullptr)
    {
        return;
    }
    if (CELauncher::cast(Equip) == nullptr)
    {
        return;
    }

    // 0x004db95e..0x004db996: UI sound slot 0x17 (FUN_005645c0), then the toggle record: ten
    // zero dwords, id 0x19, +0x10 SubId, +0x14 2, +0x26 1 (FUN_0054bcb0).
    PlayOrRewindUiSoundSlot(0x17);
    FSpaceCommand Command;
    memset(&Command, 0, sizeof(Command));
    Command.Byte26 = 1;
    Command.Id = 0x19;
    Command.SubObjectId = 2;
    Command.ObjectId = SubId;
    SpaceSendCommand(&Command);

    // 0x004db99b..0x004db9cd: the ship of the HUDItem target (+0x28; class & 0x503), used
    // without a null test.
    CShip* const Ship = HudShipOfObjRw(ObjRwFromWatchable(const_cast<Watchable*>(Target.target())));

    // 0x004db9cd..0x004dba00: the ship's target object when its class has bits 0..1.
    CSimple* TargetObject = nullptr;
    if (Ship->get_target() != nullptr)
    {
        CObject const* const Object = Ship->get_target()->cobject(); // slot +0x150
        if (Object != nullptr && (static_cast<unsigned>(Object->ObjectClass) & 3u) == 3u)
        {
            TargetObject = static_cast<CSimple*>(const_cast<CObject*>(Object));
        }
    }

    // 0x004dba00..0x004dba28: an active gun is aimed there.
    CEGun* const Gun = CEGun::cast(Equip);
    if (Equip->IsActive() && Gun != nullptr)
    {
        Gun->SetTarget(TargetObject, Ship->get_sub_target());
    }

    // 0x004dba2e..0x004dba3e: FUN_004f5580(FUN_004f6170()) on DAT_00674c0c.
    G_WeaponGroups.StoreActive(G_WeaponGroups.GetCurrent());
}

bool HUD_Status::MouseEvent(FWin3DMouseEvent const* Event)
{
    // LAB_004db8f0 (this = the WinControl face). Always consumed.
    if (Event->Field0 == -1)
    {
        NnToolTipHideGlobal(); // FUN_004be390
        unsigned short const SubId = SubTargetId; // +0x3b4
        if (SubId != SUBOBJ_ID_NONE && Event->Field1 == 2)
        {
            ToggleLauncher(SubId);
        }
    }
    return true;
}

intptr_t HUD_Status::SetProperty(int Msg, uintptr_t A, uintptr_t B)
{
    // FUN_004db1c0 (this = the WinControl face).
    if (Msg == 0x26)
    {
        // 0x004db204..0x004db21e: the close button -> HUDItem slot 12 through the vftable, 1.
        if (A == reinterpret_cast<uintptr_t>(CloseButton))
        {
            Slot12();
            return 1;
        }

        // 0x004db221..0x004db296: a weapon row's text (name, number, ammo -- in that order);
        // a row inside Weapons (unsigned compare, the top row not added) toggles the launcher
        // at WeaponTopRow + row and restarts Field588.
        int Row = 0;
        for (; Row < 10; ++Row)
        {
            if (A == reinterpret_cast<uintptr_t>(WeaponText[Row]) ||
                A == reinterpret_cast<uintptr_t>(WeaponNumberText[Row]) ||
                A == reinterpret_cast<uintptr_t>(AmmoText[Row]))
            {
                break;
            }
        }
        if (Row < 10 && static_cast<unsigned>(Row) < Weapons.size())
        {
            ToggleLauncher(Weapons[WeaponTopRow + Row].SubId);
            Field588 = 1.0f; // DAT_005d8484
        }
    }
    else if (Msg == 0x3e)
    {
        // 0x004db1d3..0x004db1ff: the scroll bar moved.
        if (!HudIsMousePickBlocked() && A == reinterpret_cast<uintptr_t>(WeaponListScroll))
        {
            WeaponTopRow = static_cast<int>(B);
            Field588 = 1.0f;
        }
    }

    // 0x004db29c: FUN_004e7950.
    return HudTargetBase::SetProperty(Msg, A, B);
}

namespace
{
    // DAT_00674aa5 / DAT_00674aa6 (.bss, 0 at load) -- the last IsMouseFlightUnlocked /
    // IsMouseFlightHoldThresholdReached Update saw. Only Update reads or writes them.
    unsigned char G_StatusLastMouseFlightUnlocked = 0;
    unsigned char G_StatusLastMouseFlightHoldReached = 0;

    // Retail's shared scratch buffers (nn_text_scratch.h): the special-mode caption's format
    // is DAT_0066dc60 (0x004dcb44), its line DAT_0066fc60 (0x004dcc23), and the weapon rows'
    // name scratch DAT_0066fc60 too (0x004ddf44 / 0x004ddf50). Each capacity passed is the
    // DAT_006119f8 read.
    wchar_t* const G_StatusScratchFormat = G_NnTextScratchA; // DAT_0066dc60
    wchar_t* const G_StatusScratchLine = G_NnTextScratchB;   // DAT_0066fc60
}

void HUD_Status::Update()
{
    // LAB_004db340 (this = the WinControl face).

    // 0x004db347..0x004db384: once the view object is live, sync the panel with session flag 2
    // (HUDItem slot 12 through the vftable when they differ). FUN_004c8110 answers 0 or 1.
    if (Byte5cc != 0 && ObjRwFromWatchable(const_cast<Watchable*>(ViewObject.target())) != nullptr)
    {
        Byte5cc = 0;
        unsigned char const bFlag = TestGameSessionFlags(2) ? 1 : 0; // FUN_004c8110
        if (bFlag != bExpanded)
        {
            Slot12();
        }
    }

    // 0x004db387..0x004db3c0: the captions follow the mouse-flight state.
    if ((IsMouseFlightUnlocked() ? 1 : 0) != G_StatusLastMouseFlightUnlocked) // FUN_004c7270
    {
        UpdateSpecialModeCaptions();
        G_StatusLastMouseFlightUnlocked = IsMouseFlightUnlocked() ? 1 : 0;
    }
    if ((IsMouseFlightHoldThresholdReached() ? 1 : 0) != G_StatusLastMouseFlightHoldReached) // FUN_004c7c60
    {
        UpdateSpecialModeCaptions();
        G_StatusLastMouseFlightHoldReached = IsMouseFlightHoldThresholdReached() ? 1 : 0;
    }

    // 0x004db3c5..0x004db551: the player ship (FUN_0054baf0, +0xc, CBase::Object, class & 0x503
    // -- HudShipOfObjRw's chain) and its behaviour manager.
    CShip* const Ship = HudShipOfObjRw(static_cast<IObjRW*>(GetPlayerObjRW()));
    if (Ship != nullptr)
    {
        IBehaviorManager* const Behavior = Ship->get_behavior_interface();
        if (Behavior != nullptr)
        {
            if (Behavior->Opaque168[0] == 0 && !Ship->is_using_tradelane() &&
                Behavior->DockingPortStatus != 0)
            {
                // 0x004db43b..0x004db4e1: ids 0x63b shown, the cancel caption hidden; with a ship
                // target whose TypeBits has bit 7, the cancel caption gets the target's caption.
                SpecialModeText->SetProperty(0x1e, 0x63b, 0);
                SpecialModeText->Flags |= 3;
                SpecialModeTextCancel->Flags &= 0xfc;
                Byte5cd = 1;
                IObjRW* const ShipTarget = Ship->get_target();
                if (ShipTarget != nullptr)
                {
                    CObject* Object = const_cast<CObject*>(ShipTarget->cobject()); // slot +0x150
                    if (Object != nullptr &&
                        (static_cast<unsigned>(Object->ObjectClass) & 3u) != 3u)
                    {
                        Object = nullptr;
                    }
                    // 0x004db498: byte +0xe0 is read with no null test (retail faults on a target
                    // without a CSimple).
                    CSimple* const Simple = static_cast<CSimple*>(Object);
                    if ((Simple->TypeBits & 0x80u) != 0)
                    {
                        // 0x004db4a6..0x004db4dd: FUN_004e8100(Simple, &Caption (+0x5d0), 1, 0)
                        // (cdecl, ADD ESP,0x10); on true the caption goes to the cancel label
                        // (+0x5c0) as property 0x1d and the label is shown.
                        if (HudBuildObjectCaption(Simple, &Caption, 1, false))
                        {
                            SpecialModeTextCancel->SetProperty(
                                0x1d, reinterpret_cast<uintptr_t>(Caption.Text), 0);
                            SpecialModeTextCancel->Flags |= 3;
                        }
                    }
                }
            }
            else if (Byte5cd != 0 || Ship->is_using_tradelane())
            {
                // 0x004db4e3..0x004db551
                if (Ship->is_using_tradelane())
                {
                    SpecialModeTextCancel->SetProperty(0x1d, reinterpret_cast<uintptr_t>(Caption.Text), 0);
                    SpecialModeTextCancel->Flags |= 3;
                    Byte5cd = 1;
                }
                else
                {
                    Byte5cd = 0;
                    SpecialModeText->Flags &= 0xfc;
                    SpecialModeTextCancel->Flags &= 0xfc;
                    UpdateSpecialModeCaptions();
                }
            }
        }
    }

    // 0x004db554..0x004db586: HUD_Target_Base's update FUN_004e7980, the scaler's FUN_0055e5e0;
    // nothing more while either animation runs.
    HudTargetBase::Update();
    ButtonScaler::Update();
    if (!ButtonMover::IsActive() && !ButtonScaler::IsActive())
    {
        // 0x004db58c..0x004db666: Field5a0 changed -- the rows and the scroll bar follow it.
        if (Field5a0 != Field5a4)
        {
            Field5a4 = Field5a0;
            Field588 = 1.0f; // DAT_005d8484
            if (Field5a0 == 0)
            {
                for (int Index = 0; Index < 10; ++Index)
                {
                    WeaponNumberText[Index]->SetProperty(1, 1, 0);
                    WeaponText[Index]->SetProperty(1, 1, 0);
                    AmmoText[Index]->SetProperty(1, 1, 0);
                }
                WeaponListScroll->SetProperty(1, 1, 0);
                Field588 = 1.0f;
            }
            else
            {
                for (int Index = 0; Index < 10; ++Index)
                {
                    WeaponNumberText[Index]->SetProperty(1, 0, 0);
                    WeaponText[Index]->SetProperty(1, 0, 0);
                    AmmoText[Index]->SetProperty(1, 0, 0);
                }
                WeaponListScroll->SetProperty(1, 0, 0);
            }
        }

        // 0x004db66c..0x004db6b9: the first update with a live view object selects weapon
        // group 0 -- stored from the active launchers when it does not exist yet.
        if (Byte500 != 0 && ObjRwFromWatchable(const_cast<Watchable*>(ViewObject.target())) != nullptr)
        {
            Byte500 = 0;
            if (G_WeaponGroups.FindGroupIndex(0) == -1) // FUN_004f5540
            {
                G_WeaponGroups.StoreActive(0);          // FUN_004f5580
                G_WeaponGroups.SetCurrent(0);           // FUN_004f6180
            }
            else
            {
                G_WeaponGroups.Activate(0);             // FUN_004f5920
            }
        }

        // 0x004db6be..0x004db6f0: with a live HUDItem target, the rest runs once Field588 (plus
        // the frame time FUN_0042d680, kept on the FPU) passes 1.0 -- TEST AH,0x41 skips on
        // <= 1.0 and on NaN. The sum is stored before the test and 1.0 taken off after it.
        if (ObjRwFromWatchable(const_cast<Watchable*>(Target.target())) != nullptr)
        {
            double const Sum = GetClientFrameTimeCapped() + static_cast<double>(Field588);
            Field588 = static_cast<float>(Sum);
            if (Sum > 1.0)
            {
                Field588 = static_cast<float>(Sum - 1.0);
                if (!ButtonMover::IsActive() && !ButtonScaler::IsActive())
                {
                    // 0x004db724..0x004db736
                    Byte584 = 0;
                    Byte585 = 0;
                    Byte5b8 = 0;
                    Byte586 = 0;

                    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW()); // FUN_0054baf0
                    if (Player != nullptr)
                    {
                        // 0x004db749..0x004db7d1: the first mine dropper (0x20) and the first
                        // countermeasure dropper (0x40) are asked for their ammo count; both
                        // answers are dropped.
                        CObject* const Object = const_cast<CObject*>(Player->cobject()); // +0x150
                        if (Object != nullptr &&
                            (static_cast<unsigned>(Object->ObjectClass) & 0x103u) == 0x103u)
                        {
                            CEqObj* const Owner = static_cast<CEqObj*>(Object);
                            CEquipTraverser MineWalk(EquipTypeMineDropper);
                            CEquip* const Mine = Owner->EquipManager.Traverse(MineWalk);
                            if (Mine != nullptr)
                            {
                                CELauncher* const Launcher = CELauncher::cast(Mine);
                                if (Launcher != nullptr)
                                {
                                    Launcher->GetAmmoCount(); // slot +0xa0
                                }
                            }
                            CEquipTraverser CmWalk(EquipTypeCmDropper);
                            CEquip* const Cm = Owner->EquipManager.Traverse(CmWalk);
                            if (Cm != nullptr)
                            {
                                CELauncher* const Launcher = CELauncher::cast(Cm);
                                if (Launcher != nullptr)
                                {
                                    Launcher->GetAmmoCount();
                                }
                            }
                        }

                        // 0x004db7d4..0x004db84f: the ship-state bytes. When the chain gives no
                        // ship retail goes on with a null ship (0x004db879: EDI = 0, then
                        // is_using_tradelane on it) and faults; the port does the same.
                        CShip* const PlayerShip = HudShipOfObjRw(static_cast<IObjRW*>(GetPlayerObjRW()));
                        if (PlayerShip != nullptr)
                        {
                            Byte584 = PlayerShip->any_thrusters_on() ? 1 : 0;
                        }
                        if (PlayerShip->is_using_tradelane())
                        {
                            Byte5b8 = 1;
                        }
                        if (PlayerShip->is_cruise_active())
                        {
                            Byte586 = 1;
                        }
                        if (PlayerShip->EquipManager.FindFirst(EquipTypeThruster) != nullptr) // 0x400
                        {
                            Byte585 = 1;
                        }
                    }

                    // 0x004db856..0x004db894
                    UpdateRepairBatteryCounts();
                    if (IsLootInReach())
                    {
                        LootButton->Flags |= 3;
                    }
                    else
                    {
                        LootButton->Flags &= 0xfc;
                    }
                    if (bExpanded != 0)
                    {
                        ShowWeaponRows();
                    }
                }
            }
        }
    }

    // 0x004db898..0x004db8d6 (also reached from 0x004db8b9 while an animation runs): the toggle
    // button is hidden while the panel is open and the mover is idle.
    uintptr_t const bHideToggle = (bExpanded != 0 && !ButtonMover::IsActive()) ? 1u : 0u;
    ToggleButton->SetProperty(1, bHideToggle, 0);
}

void HUD_Status::UpdateSpecialModeCaptions()
{
    // FUN_004dca20 (this = the HUDItem face).
    // 0x004dca2b..0x004dca7e: the player ship (inline FUN_0054baf0 / +0xc / Object / 0x503
    // chain): in a tradelane, and whether the current behaviour is 10.
    bool bTradelane = false;  // [ESP+0xe]
    bool bHideText = false;   // BL
    CShip* const Ship = HudShipOfObjRw(static_cast<IObjRW*>(GetPlayerObjRW()));
    if (Ship != nullptr)
    {
        if (Ship->is_using_tradelane())
        {
            bTradelane = true;
        }
        IBehaviorManager* const Behavior = Ship->get_behavior_interface();
        if (Behavior != nullptr && Behavior->CurrentBehaviorIndex == 10)
        {
            bHideText = true;
        }
    }

    // 0x004dca80..0x004dcac6: outside the turret view, with mouse flight unlocked (or behaviour
    // 10) and no tradelane, the cancel caption is hidden; the text is hidden as well unless the
    // rear view is up or the mouse-flight hold has fired without behaviour 10. Otherwise both
    // show.
    bool bHideCancel = false; // [ESP+0xf]
    if (bTurretView == 0 && (IsMouseFlightUnlocked() || bHideText) && !bTradelane)
    {
        bHideCancel = true;
        if (bRearView != 0)
        {
            bHideText = false;
        }
        else if (IsMouseFlightHoldThresholdReached() && !bHideText)
        {
            bHideText = false;
        }
        else
        {
            bHideText = true;
        }
    }
    else
    {
        bHideCancel = false;
        bHideText = false;
    }

    SpecialModeText->SetProperty(1, bHideText ? 1u : 0u, 0);
    SpecialModeTextCancel->SetProperty(1, bHideCancel ? 1u : 0u, 0);
    if (bHideText)
    {
        return;
    }

    // 0x004dcafd..0x004dcbda: the text's ids.
    if (bTurretView != 0)
    {
        SpecialModeText->SetProperty(0x1e, IsMouseFlightUnlocked() ? 0x615u : 0x633u, 0);
        if (bTradelane)
        {
            return;
        }
    }
    else if (bRearView != 0)
    {
        SpecialModeText->SetProperty(0x1e, 0x62b, 0);
        if (bTradelane)
        {
            return;
        }
    }
    else if (bTradelane)
    {
        SpecialModeText->Flags &= 0xfc;
        return;
    }
    else
    {
        SpecialModeText->SetProperty(0x1e, 0x62c, 0);
    }

    // 0x004dcb37..0x004dcc44: the cancel caption -- ids 0x62d as a format around the name of the
    // key bound to the command that leaves the mode ("?" when none is bound): 0xbf in the turret
    // view, 0xf with mouse flight unlocked, 0xc8 otherwise.
    GetIdsStringWWithRawFallback(0x62d, G_StatusScratchFormat,
                                 G_NnTextScratchCapacity); // FUN_004347e0
    wchar_t KeyName[0x100];
    wcscpy(KeyName, L"?");
    unsigned Command = 0xc8;
    if (bTurretView != 0)
    {
        Command = 0xbf;
    }
    else if (IsMouseFlightUnlocked())
    {
        Command = 0xf;
    }
    FKeyBindingLookup Binding;
    if (KeyMapFindActiveBinding(Command, &Binding)) // FUN_00577dd0
    {
        KeyMapFormatBindingName(&Binding, KeyName, 0x100); // FUN_00577f50
    }
    swprintf(G_StatusScratchLine, 0x1000, G_StatusScratchFormat, KeyName);
    SpecialModeTextCancel->SetProperty(0x1d, reinterpret_cast<uintptr_t>(G_StatusScratchLine), 0);
}

void HUD_Status::UpdateRepairBatteryCounts()
{
    // FUN_004de2f0: the count of the first non-mission entry (FUN_004c45d0), 0 without one,
    // capped at 999 (signed), as "%d" into a 0x40-byte stack buffer; SetProperty's B is 0x10.
    wchar_t Text[0x20];
    EquipDesc const* const Repair = FindPlayerNonMissionEquipRetail(G_NanobotArchId);
    int RepairCount = 0;
    if (Repair != nullptr)
    {
        RepairCount = Repair->Count; // +0x14
        if (RepairCount > 999)
        {
            RepairCount = 999;
        }
    }
    swprintf(Text, 0x20, L"%d", RepairCount);
    RepairCountText->SetProperty(0x1d, reinterpret_cast<uintptr_t>(Text), 0x10);

    EquipDesc const* const Battery = FindPlayerNonMissionEquipRetail(G_ShieldBatteryArchId);
    int BatteryCount = 0;
    if (Battery != nullptr)
    {
        BatteryCount = Battery->Count;
        if (BatteryCount > 999)
        {
            BatteryCount = 999;
        }
    }
    swprintf(Text, 0x20, L"%d", BatteryCount);
    BatteryCountText->SetProperty(0x1d, reinterpret_cast<uintptr_t>(Text), 0x10);
}

bool HUD_Status::IsLootInReach()
{
    // FUN_004de3a0.
    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW()); // FUN_0054baf0
    if (Player == nullptr)
    {
        return false;
    }
    if (GetPlayerEngineCruiseActive()) // FUN_004c7b90
    {
        return false;
    }
    CShip* const Ship = HudShipOfObjRw(Player); // inline +0xc / Object / 0x503 chain
    if (Ship == nullptr)
    {
        return false;
    }
    IBehaviorManager* const Behavior = Ship->get_behavior_interface();
    if (Behavior == nullptr)
    {
        return false;
    }
    if (Behavior->Opaque168[0] == 0)
    {
        return false;
    }
    if (Ship->is_using_tradelane())
    {
        return false;
    }

    // 0x004de418..0x004de4ae: bucket 7 (FUN_00540fc0), along ListNext (+0x14).
    int Kind = 7;
    FClientGameobj* Loot = ClientObjDb_GetKindHead(&Kind);
    while (Loot != nullptr)
    {
        // Slot 0 (get_position) of the player first, then of the loot; the distance squared is
        // summed x, z, y on the FPU. FCOM / TEST AH,5 / JP skips >= 1e6 (DAT_005d848c) and NaN.
        Vector const& PlayerPos = Player->get_position();
        Vector const& LootPos = static_cast<IObjRW*>(Loot)->get_position();
        double const Dx = static_cast<double>(LootPos.x) - static_cast<double>(PlayerPos.x);
        double const Dy = static_cast<double>(LootPos.y) - static_cast<double>(PlayerPos.y);
        double const Dz = static_cast<double>(LootPos.z) - static_cast<double>(PlayerPos.z);
        double const DistanceSquared = Dx * Dx + Dz * Dz + Dy * Dy;
        if (DistanceSquared < 1000000.0)
        {
            // 0x004de475..0x004de4a7: the CLoot behind it (+0xc, Object, class & 0x803) -- no
            // null test before +0xec is read -- and the ship's room for its contents.
            CBase* const Base = static_cast<CBase*>(static_cast<IObjInspectImpl*>(Loot));
            CObject* LootObject = nullptr;
            if (Base != nullptr && Base->Object != nullptr &&
                (static_cast<unsigned>(Base->Object->ObjectClass) & 0x803u) == 0x803u)
            {
                LootObject = Base->Object;
            }
            CLoot* const Container = static_cast<CLoot*>(LootObject);
            if (Ship->get_space_for_cargo_type(Container->ContentsArch) > 0)
            {
                return true;
            }
        }
        Loot = Loot->ListNext;
    }
    return false;
}

void HUD_Status::ShowWeaponRows()
{
    // FUN_004ddeb0.
    RebuildWeaponList(); // FUN_004dd9e0

    // 0x004ddebd..0x004ddf04: the row count ((end - begin) / 0x24, 0 with no buffer) and the
    // highest first row; WeaponTopRow is clamped with an unsigned compare.
    int const Count = static_cast<int>(Weapons.size());
    int MaxTop = 0;
    if (Count > 10)
    {
        MaxTop = Count - 10;
    }
    if (static_cast<unsigned>(WeaponTopRow) > static_cast<unsigned>(MaxTop))
    {
        WeaponTopRow = MaxTop;
    }

    // 0x004ddf0a..0x004de02b: fill rows 0.. from WeaponTopRow while both last.
    int Row = 0;
    int Index = WeaponTopRow;
    if (Index < Count)
    {
        for (;;)
        {
            FHudStatusWeapon const& Weapon = Weapons[Index];
            G_StatusScratchLine[0] = 0;
            GetIdsStringWWithRawFallback(static_cast<unsigned>(Weapon.IdsName), G_StatusScratchLine,
                                         G_NnTextScratchCapacity); // FUN_004347e0
            WeaponText[Row]->SetProperty(1, 0, 0);
            WeaponNumberText[Row]->SetProperty(1, 0, 0);
            WeaponText[Row]->SetProperty(0x1d, reinterpret_cast<uintptr_t>(G_StatusScratchLine), 0);

            ++Index;
            char NumberText[0x20];
            sprintf(NumberText, "%d", Index);
            // 0x004ddfa8..0x004ddfb0: FUN_0058a580(NumberText) on WeaponNumberText[Row].
            static_cast<NnTextControl*>(WeaponNumberText[Row])->SetNumberText(NumberText);

            if (Weapon.AmmoCount >= 0)
            {
                AmmoText[Row]->SetProperty(1, 0, 0);
                char AmmoCountText[0x20];
                sprintf(AmmoCountText, "%d", Weapon.AmmoCount);
                // 0x004ddfe1..0x004ddfec: FUN_0058a580(AmmoCountText) on AmmoText[Row].
                static_cast<NnTextControl*>(AmmoText[Row])->SetNumberText(AmmoCountText);
            }
            else
            {
                AmmoText[Row]->SetProperty(1, 1, 0);
            }

            ++Row;
            if (Row >= 10)
            {
                break;
            }
            if (Index >= Count)
            {
                break;
            }
        }
    }

    // 0x004de02f..0x004de03d: the scroll bar's range.
    WeaponListScroll->SetProperty(0x37, 0, static_cast<uintptr_t>(Count - 1));

    // 0x004de043..0x004de096: the rows left over are hidden.
    for (; Row < 10; ++Row)
    {
        WeaponNumberText[Row]->SetProperty(1, 1, 0);
        AmmoText[Row]->SetProperty(1, 1, 0);
        WeaponText[Row]->SetProperty(1, 1, 0);
    }
}

namespace
{
    // DAT_00612700 (.data, 1 at load) / DAT_00674aa4 (.bss, 0) -- the auto-level and
    // auto-avoidance toggles OnEvent 0x3e / 0x3d flip. Nothing else in the PE touches them.
    unsigned char G_StatusAutoLevel = 1;
    unsigned char G_StatusAutoAvoidance = 0;

    // FUN_00544d00 (thiscall on the player's IObjRW, RET 4; Client\space\ship.cpp, whose only
    // caller is FUN_004de4c0) -- the ship's first tractor (FindFirst 0x100000) pulls in every
    // object of Targets: IObjAffect slot 15 with the tractor's id (widened into a dword local),
    // the list and the connection id. False without a tractor.
    bool ShipTractorInTargets(IObjRW* Player, std::vector<IObjRW*> const& Targets)
    {
        // 0x00544d02..0x00544d27: the +0xc / Object / 0x503 chain; a null ship is not tested
        // again -- FindFirst runs on null + 0xe4 and faults, in retail as here.
        CShip* const Ship = HudShipOfObjRw(Player);
        CEquip* const Tractor = Ship->EquipManager.FindFirst(EquipTypeTractor);
        if (Tractor == nullptr)
        {
            return false;
        }
        unsigned short TractorId = Tractor->Id;
        static_cast<IObjAffect*>(Player)->slot_3c(&TractorId, Targets,
                                                  static_cast<int>(GetPlayerConnectionId()));
        return true;
    }
}

void HUD_Status::FireSeekerGuns(int Mode)
{
    // FUN_004dd4e0 (this = the HUDItem face). The player ship (FUN_0054baf0, slot +0x150,
    // class & 0x503) and its first gun (mask 0x80, CEGun::cast); nothing without them.
    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW());
    if (Player == nullptr)
    {
        return;
    }
    CObject* const Object = const_cast<CObject*>(Player->cobject());
    if (Object == nullptr)
    {
        return;
    }
    if ((static_cast<unsigned>(Object->ObjectClass) & 0x503u) != 0x503u)
    {
        return;
    }
    CShip* const Ship = static_cast<CShip*>(Object);

    // The ids go to the frame at +0x44 (26 slots up to the frame's end); retail writes them with
    // no bound test.
    unsigned short Ids[26];
    int Count = 0;
    CEquipTraverser Walk(EquipTypeGun);
    CEGun* Gun = CEGun::cast(Ship->EquipManager.Traverse(Walk));
    if (Gun == nullptr)
    {
        return;
    }
    while (Gun != nullptr)
    {
        // 0x004dd567..0x004dd579: only guns whose munition seeks (+0x90) count.
        if (Gun->MunitionArch()->Seeker != 0)
        {
            bool bTorpedo = false;         // [ESP+0x12]
            bool bCruiseDisruptor = false; // [ESP+0x13]
            if (Gun->MunitionArch()->bCruiseDisruptor) // +0xac, a second MunitionArch call
            {
                bCruiseDisruptor = true;
            }
            else
            {
                // 0x004dd5a1..0x004dd660: the gun's hardpoint (slot +0x60) against every name
                // of the ship archetype's legal hp types (+0x88 -> +0x134, DACOM stricmp); a
                // match under type 0x0a (torpedo), 0x24 or 0x23 (torpedo specials) marks it.
                CacheString const HpName = Gun->GetParentHPName();
                Archetype::Ship const* const ShipArch =
                    static_cast<Archetype::Ship const*>(Ship->Archetype);
                int const TypeCount = static_cast<int>(ShipArch->LegalHps.size());
                for (int TypeIndex = 0; TypeIndex < TypeCount; ++TypeIndex)
                {
                    Archetype::LegalHpTypes const& Legal = ShipArch->LegalHps[TypeIndex];
                    int const NameCount = static_cast<int>(Legal.Hardpoints.size());
                    for (int NameIndex = 0; NameIndex < NameCount; ++NameIndex)
                    {
                        if (_stricmp(Legal.Hardpoints[NameIndex].Value, HpName.Value) == 0)
                        {
                            unsigned const Type = static_cast<unsigned>(Legal.Type);
                            if (Type == 0x0au || Type == 0x24u || Type == 0x23u)
                            {
                                bTorpedo = true;
                            }
                        }
                    }
                }
            }

            // 0x004dd66c..0x004dd6be
            bool bTake = false;
            if (Mode == 0)
            {
                bTake = !bTorpedo && !bCruiseDisruptor;
            }
            else if (Mode == 1)
            {
                bTake = bTorpedo;
            }
            else if (Mode == 2)
            {
                bTake = bCruiseDisruptor;
            }
            if (bTake)
            {
                Ids[Count] = Gun->Id;
                ++Count;
            }
        }
        Gun = CEGun::cast(Ship->EquipManager.Traverse(Walk));
    }

    // 0x004dd6e9..0x004dd71a: one IObjAffect slot 13 call at the targetable-objects aim point
    // (DAT_00674b70, no null test).
    if (Count == 0)
    {
        return;
    }
    Vector Aim;
    G_HudTargetableObjects->GetAimPoint(&Aim); // FUN_004f1da0
    static_cast<IObjAffect*>(Player)->slot_34(Ids, static_cast<unsigned>(Count), &Aim,
                                              static_cast<int>(GetPlayerConnectionId()));
}

void HUD_Status::FireWeaponGroup(int Group)
{
    // FUN_004dcc60 (this = the HUDItem face).
    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW()); // FUN_0054baf0
    if (Player == nullptr)
    {
        return;
    }
    Vector Aim;
    G_HudTargetableObjects->GetAimPoint(&Aim); // FUN_004f1da0 on DAT_00674b70
    if (Group == ActiveWeaponGroup)
    {
        // 0x004dcc8e..0x004dcca3: the active group -- a null list fires every active launcher.
        static_cast<IObjAffect*>(Player)->slot_34(nullptr, 0, &Aim,
                                                  static_cast<int>(GetPlayerConnectionId()));
        return;
    }
    // 0x004dccae..0x004dccdc: the group's launchers, at most 0x10.
    unsigned short Ids[0x10];
    int const Count = G_WeaponGroups.GetGroupEquipIds(Group, Ids, 0x10); // FUN_004f5790
    if (Count > 0)
    {
        static_cast<IObjAffect*>(Player)->slot_34(Ids, static_cast<unsigned>(Count), &Aim,
                                                  static_cast<int>(GetPlayerConnectionId()));
    }
}

void HUD_Status::TractorInAllLoot()
{
    // FUN_004de4c0.
    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW()); // FUN_0054baf0
    if (Player == nullptr)
    {
        return;
    }

    // 0x004de505..0x004de588: the loot (bucket 7) within 1000 m. FUN_00423b60 takes the
    // player's position (slot 0, asked first) from the loot's into three floats; the square is
    // summed y, z, x on the FPU against 1e6 (DAT_005d848c), TEST AH,5 / JP skipping >= and NaN.
    std::vector<IObjRW*> Loot; // retail vector<IObjInspect*> at [ESP+0x40]
    int Kind = 7;
    FClientGameobj* Item = ClientObjDb_GetKindHead(&Kind); // FUN_00540fc0
    while (Item != nullptr)
    {
        Vector const& PlayerPos = Player->get_position();
        Vector const& ItemPos = static_cast<IObjRW*>(Item)->get_position();
        float const OffsetZ = ItemPos.z - PlayerPos.z;
        float const OffsetY = ItemPos.y - PlayerPos.y;
        float const OffsetX = ItemPos.x - PlayerPos.x;
        double DistanceSquared = static_cast<double>(OffsetY) * static_cast<double>(OffsetY) +
                                 static_cast<double>(OffsetZ) * static_cast<double>(OffsetZ);
        DistanceSquared += static_cast<double>(OffsetX) * static_cast<double>(OffsetX);
        if (DistanceSquared < 1000000.0)
        {
            Loot.insert(Loot.end(), static_cast<IObjRW*>(Item));
        }
        Item = Item->ListNext;
    }

    // 0x004de58a..0x004de67d: a value per loot -- 1e30 for contents of volume 0, 1e20 for
    // nanobots and shield batteries (both ids read once, before the loop), else the good's
    // price (Arch2Good, GoodList::find_by_id, +0x58), else 0; pushed through FUN_00537260.
    int const Count = static_cast<int>(Loot.size());
    unsigned const NanobotArchId = G_NanobotArchId;
    unsigned const BatteryArchId = G_ShieldBatteryArchId;
    std::vector<float> Values;
    for (int Index = 0; Index < Count; ++Index)
    {
        float Value = 0.0f;
        CObject const* const Object = Loot[Index]->cobject(); // slot +0x150
        if (Object != nullptr && (static_cast<unsigned>(Object->ObjectClass) & 0x803u) == 0x803u)
        {
            Archetype::Equipment const* const Contents =
                static_cast<CLoot const*>(Object)->ContentsArch; // +0xec
            unsigned const ContentsId = Contents->get_id();       // +0x08
            if (Contents->Volume == 0.0f)                         // +0x5c; TEST AH,0x44 / JP
            {
                Value = 1.0e30f;                                  // 0x7149f2ca
            }
            else if (ContentsId == NanobotArchId || ContentsId == BatteryArchId)
            {
                Value = 1.0e20f;                                  // 0x60ad78ec
            }
            else
            {
                GoodInfo const* const Good = GoodList::find_by_id(Arch2Good(ContentsId));
                if (Good != nullptr)
                {
                    Value = Good->Price;                          // +0x58
                }
            }
        }
        Values.insert(Values.end(), 1, Value);
    }

    // 0x004de68b..0x004de6db: bubble sort, highest value first, the loot swapped alongside.
    // FCOM / TEST AH,5 / JP: a pair swaps only when the first is strictly below the second.
    int const Last = Count - 1;
    bool bSorted = true;
    do
    {
        bSorted = true;
        if (Last <= 0)
        {
            break;
        }
        for (int Index = 0; Index < Last; ++Index)
        {
            if (Values[Index] < Values[Index + 1])
            {
                float const Higher = Values[Index + 1];
                Values[Index + 1] = Values[Index];
                Values[Index] = Higher;
                IObjRW* const Swapped = Loot[Index];
                Loot[Index] = Loot[Index + 1];
                Loot[Index + 1] = Swapped;
                bSorted = false;
            }
        }
    } while (!bSorted);

    // 0x004de6dd..0x004de6ec: the ship's tractor takes the list when the player is a ship
    // gameobj (+0x1c == 1); both vectors are freed either way.
    FClientGameobj* const PlayerObject =
        static_cast<FClientGameobj*>(static_cast<IObjInspectImpl*>(Player));
    if (PlayerObject->ManagerKind == 1)
    {
        ShipTractorInTargets(Player, Loot); // FUN_00544d00
    }
}

bool HUD_Status::OnEvent(unsigned MsgId)
{
    // LAB_004dba50 (this = the Event::Handler face at +0x384).

    // 0x004dba50..0x004dba86: nothing without a server; while the HUD may not pick
    // (FUN_0045a740) only the afterburner key's release is acted on.
    if (G_Server == nullptr) // DAT_0067ecd0
    {
        return false;
    }
    if (HudIsMousePickBlocked())
    {
        if (MsgId == 0x3a && HudCommandRecord()->Byte18 == 0) // FUN_00576240
        {
            SetPlayerAfterburner(false); // FUN_004c7970(0)
        }
        return false;
    }

    // 0x004dba95..0x004dbac5: the player must be a ship gameobj (+0x1c == 1) whose object is a
    // ship (+0xc / Object / 0x503).
    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW()); // FUN_0054baf0 -> [ESP+0x10]
    if (Player == nullptr)
    {
        return false;
    }
    FClientGameobj* const PlayerObject =
        static_cast<FClientGameobj*>(static_cast<IObjInspectImpl*>(Player));
    if (PlayerObject->ManagerKind != 1)
    {
        return false;
    }
    CShip* const Ship = HudShipOfObjRw(Player); // EDI
    if (Ship == nullptr)
    {
        return false;
    }

    // 0x004dbac7..0x004dbadc: ids 0x3a..0xc1 through the byte table 0x004dc994; the rest (and
    // the table's default entry) answer false. Every case answers true.
    switch (MsgId)
    {
    case 0x3a:
        // 0x004dbdb4: afterburner key -- post 0x3c while it is down, 0x3b once it is up.
        HudPostCommand(HudCommandRecord()->Byte18 != 0 ? 0x3cu : 0x3bu, 0); // FUN_005763b0
        return true;

    case 0x3b:
        SetPlayerAfterburner(false); // 0x004dbdf2
        return true;

    case 0x3c:
        SetPlayerAfterburner(true); // 0x004dbddc
        return true;

    case 0x3d:
        // 0x004dbe2c: FUN_004c79d0 with the flipped toggle.
        G_StatusAutoAvoidance = (G_StatusAutoAvoidance == 0) ? 1 : 0;
        SetPlayerAutoAvoidance(G_StatusAutoAvoidance != 0);
        return true;

    case 0x3e:
        // 0x004dbe07: FUN_004c79a0 with the flipped toggle.
        G_StatusAutoLevel = (G_StatusAutoLevel == 0) ? 1 : 0;
        SetPlayerAutoLevel(G_StatusAutoLevel != 0);
        return true;

    case 0x56:
    case 0x57:
        // 0x004dbe51 / 0x004dbe87: open the panel (HUDItem slot 12) when it is closed, then
        // hide (0x56) or show (0x57) the weapon list -- Field5a0 and session flag 4.
        if (bExpanded == 0)
        {
            Slot12();
        }
        Field5a0 = (MsgId == 0x57) ? 1 : 0;
        SetGameSessionFlags(4, MsgId == 0x57); // FUN_004c8120
        return true;

    case 0x5e:
    case 0x6d:
        // 0x004dbc2d / 0x004dbc12: toggle the panel (HUDItem slot 12).
        Slot12();
        return true;

    case 0x6f:
        TractorInAllLoot(); // 0x004dbae3, FUN_004de4c0
        return true;

    case 0x8f:
    {
        // 0x004dc775: nanobots -- used (IObjAffect slot 22 with the item's id and count, UI
        // sound 0x28) unless there are none or the hull is exactly whole (TEST AH,0x44 / JNP;
        // NaN uses them); else UI sound 0x2a.
        EquipDesc const* const Repair = FindPlayerNonMissionEquipRetail(G_NanobotArchId);
        if (Repair != nullptr && !(Ship->get_relative_health() == 1.0f))
        {
            static_cast<IObjAffect*>(Player)->slot_58(Repair->Id, static_cast<unsigned>(Repair->Count),
                                                      static_cast<int>(GetPlayerConnectionId()));
            PlayOrRewindUiSoundSlot(0x28); // FUN_005645c0
            return true;
        }
        PlayOrRewindUiSoundSlot(0x2a);
        return true;
    }

    case 0x90:
    {
        // 0x004dc7e4: shield batteries -- used (slot 22, UI sound 0x29) on the first shield
        // (mask 0x100) whose relative health (CEquip +0x44) is below 1 and whose max hit points
        // (+0x38) are above 0; else UI sound 0x2b.
        EquipDesc const* const Battery = FindPlayerNonMissionEquipRetail(G_ShieldBatteryArchId);
        if (Battery != nullptr)
        {
            CEquipTraverser Walk(EquipTypeShield);
            CEquip* Shield = Ship->EquipManager.Traverse(Walk);
            while (Shield != nullptr)
            {
                if (Shield->GetRelativeHealth() < 1.0f && Shield->GetMaxHitPoints() > 0.0f)
                {
                    static_cast<IObjAffect*>(Player)->slot_58(
                        Battery->Id, static_cast<unsigned>(Battery->Count),
                        static_cast<int>(GetPlayerConnectionId()));
                    PlayOrRewindUiSoundSlot(0x29);
                    return true;
                }
                Shield = Ship->EquipManager.Traverse(Walk);
            }
        }
        PlayOrRewindUiSoundSlot(0x2b);
        return true;
    }

    case 0x91:
    case 0x92:
    case 0x93:
    case 0x94:
    case 0x95:
    case 0x96:
    {
        // 0x004dc415..0x004dc5e4: select weapon group 0..5 -- the current one is stored first.
        int const Group = static_cast<int>(MsgId - 0x91);
        G_WeaponGroups.StoreActive(G_WeaponGroups.GetCurrent()); // FUN_004f6170, FUN_004f5580
        G_WeaponGroups.SetCurrent(Group);                        // FUN_004f6180
        G_WeaponGroups.Activate(Group);                          // FUN_004f5920
        ActiveWeaponGroup = Group;
        Field588 = 1.0f; // DAT_005d8484
        return true;
    }

    case 0x97:
    case 0x98:
    case 0x99:
    case 0x9a:
    case 0x9b:
    case 0x9c:
    case 0x9d:
    case 0x9e:
    case 0x9f:
    case 0xa0:
    {
        // 0x004dc252..0x004dc403: toggle launcher 0..9 of Weapons (FUN_005756d0 size test,
        // no WeaponTopRow offset) and restart Field588.
        unsigned const Row = MsgId - 0x97;
        if (Weapons.size() > Row)
        {
            ToggleLauncher(Weapons[Row].SubId); // FUN_004db930
            Field588 = 1.0f;
        }
        return true;
    }

    case 0xa1:
    case 0xa2:
    case 0xa3:
    case 0xa4:
    case 0xa5:
    case 0xa6:
    case 0xa7:
    case 0xa8:
    case 0xa9:
    case 0xaa:
    {
        // 0x004dbec2..0x004dc243: fire launcher 0..9 of Weapons alone at the aim point.
        unsigned const Row = MsgId - 0xa1;
        if (Weapons.size() > Row)
        {
            IObjRW* const Shooter = static_cast<IObjRW*>(GetPlayerObjRW());
            if (Shooter == nullptr)
            {
                return true;
            }
            unsigned short SubId = Weapons[Row].SubId;
            Vector Aim;
            G_HudTargetableObjects->GetAimPoint(&Aim); // FUN_004f1da0
            static_cast<IObjAffect*>(Shooter)->slot_34(&SubId, 1, &Aim,
                                                       static_cast<int>(GetPlayerConnectionId()));
        }
        return true;
    }

    case 0xab:
    {
        // 0x004dc5e7: fire weapon group 0 -- FireWeaponGroup(0) inlined.
        IObjRW* const Shooter = static_cast<IObjRW*>(GetPlayerObjRW());
        if (Shooter == nullptr)
        {
            return true;
        }
        Vector Aim;
        G_HudTargetableObjects->GetAimPoint(&Aim);
        if (ActiveWeaponGroup == 0)
        {
            static_cast<IObjAffect*>(Shooter)->slot_34(nullptr, 0, &Aim,
                                                       static_cast<int>(GetPlayerConnectionId()));
            return true;
        }
        unsigned short Ids[0x10];
        int const Count = G_WeaponGroups.GetGroupEquipIds(0, Ids, 0x10);
        if (Count > 0)
        {
            static_cast<IObjAffect*>(Shooter)->slot_34(Ids, static_cast<unsigned>(Count), &Aim,
                                                       static_cast<int>(GetPlayerConnectionId()));
        }
        return true;
    }

    case 0xac:
    case 0xad:
    case 0xae:
    case 0xaf:
    case 0xb0:
        // 0x004dc669..0x004dc6e3: fire weapon group 1..5 (FUN_004dcc60).
        FireWeaponGroup(static_cast<int>(MsgId - 0xab));
        return true;

    case 0xb1:
    case 0xb2:
    case 0xb3:
    case 0xb4:
    case 0xb5:
    case 0xb6:
        // 0x004dc6e6..0x004dc772: store the active launchers as weapon group 0..5.
        G_WeaponGroups.StoreActive(static_cast<int>(MsgId - 0xb1)); // FUN_004f5580
        return true;

    case 0xb7:
    case 0xb8:
    case 0xb9:
    case 0xba:
    {
        // 0x004dbafa..0x004dbc0f: nothing in the rear view; else post (skipping modifiers) the
        // turret command 0x32..0x35 in the turret view, or 0x2c / 0x2d / 0x2a / 0x2b.
        static unsigned const kTurretCommands[4] = {0x32, 0x33, 0x34, 0x35};
        static unsigned const kShipCommands[4] = {0x2c, 0x2d, 0x2a, 0x2b};
        if (bRearView != 0)
        {
            return true;
        }
        unsigned const Slot = MsgId - 0xb7;
        HudPostCommand(bTurretView != 0 ? kTurretCommands[Slot] : kShipCommands[Slot], 1);
        return true;
    }

    case 0xbc:
        FireSeekerGuns(0); // 0x004dbc48, FUN_004dd4e0(0): missiles
        return true;

    case 0xc0:
        FireSeekerGuns(1); // 0x004dbc60: torpedoes
        return true;

    case 0xc1:
        FireSeekerGuns(2); // 0x004dbc79: cruise disruptors
        return true;

    case 0xbd:
    case 0xbe:
    {
        // 0x004dbc92 / 0x004dbd37: drop from every mine dropper (0xbd, mask 0x20) or every
        // countermeasure dropper (0xbe, mask 0x40) of the player's CEqObj -- the raw CEquip ids
        // (+0x08) into the frame at +0x24 (24 slots up to the frame's end, no bound test), then
        // IObjAffect slot 13 with no target position.
        IObjRW* const Dropper = static_cast<IObjRW*>(GetPlayerObjRW());
        if (Dropper == nullptr)
        {
            return true;
        }
        CObject* const Object = const_cast<CObject*>(Dropper->cobject()); // slot +0x150
        if (Object == nullptr)
        {
            return true;
        }
        if ((static_cast<unsigned>(Object->ObjectClass) & 0x103u) != 0x103u)
        {
            return true;
        }
        CEqObj* const Owner = static_cast<CEqObj*>(Object);
        CEquipTraverser Walk(MsgId == 0xbd ? EquipTypeMineDropper : EquipTypeCmDropper);
        CEquip* Equip = Owner->EquipManager.Traverse(Walk);
        if (Equip == nullptr)
        {
            return true;
        }
        unsigned short Ids[24];
        int Count = 0;
        while (Equip != nullptr)
        {
            Ids[Count] = Equip->Id;
            ++Count;
            Equip = Owner->EquipManager.Traverse(Walk);
        }
        if (Count == 0)
        {
            return true;
        }
        static_cast<IObjAffect*>(Dropper)->slot_34(Ids, static_cast<unsigned>(Count), nullptr,
                                                   static_cast<int>(GetPlayerConnectionId()));
        return true;
    }

    default:
        return false;
    }
}

// ---------------------------------------------------------------------------------------------
// Factories
// ---------------------------------------------------------------------------------------------

namespace
{
    // DAT_00674954 "Core" (XC LAB_004d4b00): Create FUN_004d5af0 = operator new(0x388) + ctor.
    HUDFACTORY<HUD_Core> G_HudCoreFactory("Core");
    // "Status": Create FUN_004de840 = operator new(0x5ec) + ctor FUN_004da170.
    HUDFACTORY<HUD_Status> G_HudStatusFactory("Status");
}
