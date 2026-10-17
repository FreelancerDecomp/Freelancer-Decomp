// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "hud_element_list.h"

#include "common_cequip.h"
#include "common_cobject.h"
#include "common_csys.h"
#include "common_iface.h"
#include "common_ini.h"
#include "font_string.h"
#include "game_options.h"
#include "globals.h"
#include "widescreen.h"   // port-only (FREELANCER_WIDESCREEN_SUPPORT)
#include "hud_status_core.h"
#include "input_manager.h"
#include "nn_ui_pulse.h"
#include "player_behavior.h"
#include "space_letterbox.h"
#include "texture_shapes.h"
#include "tooltip_hover.h"
#include "win3d.h"

#include "DALib.h"
#include "dacom.h"
#include "engine/engbase.h"
#include "render/rendcomp.h"
#include "render/shading.h"
#include "x86math.h"

#include <math.h>
#include <string.h>

namespace
{
    HUDItem_Factory* G_HudFactoryHead = nullptr; // DAT_00673510
    HUDItem_Factory* G_HudFactoryTail = nullptr; // DAT_00673514

    HUDItem* G_HudItemHead = nullptr; // DAT_00673518
    HUDItem* G_HudItemTail = nullptr; // DAT_0067351c

    // DAT_00673408 / DAT_0067340c -- FUN_004c7370's copy of the hud.ini path (cap 0x103).
    unsigned G_HudIniPathLength = 0;
    char G_HudIniPath[0x104] = {};

    // DAT_00673570 -- the [HUD] static_txm texture (FTextureRef's ctor is the static
    // initializer LAB_004cc950, its dtor the atexit thunk LAB_004cc970).
    FTextureRef G_HudStaticTexture;

    // DAT_00612374 -- [HUD] static_blend, initialised data 2.
    int G_HudStaticBlend = 2;

    // DAT_00673548 -- HUD_Steer::Render's 3DMathEngine (FUN_005b73e0 on first use).
    I3DMathEngine* G_HudSteerMath = nullptr;

    char const kHudCpp[] = "E:\\FL\\Scratch\\Source\\Client\\space\\HUD.cpp"; // .rdata 0x005d6dcc

    // 0x004c868d .. 0x004c86b1 (and the same lines in FUN_004c7cb0): the player's IObjRW from
    // FUN_0054baf0, `+ 0xc` tested against null, the CEqObj* at [+0x10], and the ship class
    // test ([CObject+0x4c] & 0x503) == 0x503.
    CEqObj* HudPlayerShip()
    {
        void* const ObjRW = GetPlayerObjRW();
        // `ADD EAX,0xc / TEST EAX,EAX`: an IObjRW* of -0xc is the other "no object" value.
        if (ObjRW == nullptr || ObjRW == reinterpret_cast<void*>(static_cast<intptr_t>(-0xc)))
            return nullptr;
        CEqObj* const EqObj =
            *reinterpret_cast<CEqObj**>(static_cast<unsigned char*>(ObjRW) + 0x10);
        if (EqObj == nullptr)
            return nullptr;
        if ((static_cast<unsigned>(EqObj->ObjectClass) & 0x503u) != 0x503u)
            return nullptr;
        return EqObj;
    }

    // The cursor Y block repeated at 0x004c8745 and 0x004c8885: with the mouse-flight lock on
    // (FUN_0045a470 clear and DAT_0067354c set) or the hold timer past 0.25s, "INVERSE MOUSE"
    // (option 0x17) flips the cursor against the laid-out surface height DAT_00610854.
    int HudSteerCursorY(int CursorY)
    {
        bool const bFlightView = IsMouseFlightLocked() || IsMouseFlightHoldThresholdReached();
        if (bFlightView && GetGameOption(kGameOptInverseMouse) != 0)
            return static_cast<int>(G_RenderSurfaceHeight) - CursorY;
        return CursorY;
    }
}

void HudVec3ScaleByInverse(float* Out, float const* In, float Length)
{
    // FUN_004c8b10: FLD 1.0 / FDIV Length / FST float -- Z uses the unrounded reciprocal, Y and
    // X the float re-read.
    double const InvExact = 1.0 / static_cast<double>(Length);
    float const Inv = static_cast<float>(InvExact);
    float const Z = static_cast<float>(InvExact * In[2]);
    float const Y = static_cast<float>(static_cast<double>(Inv) * In[1]);
    float const X = static_cast<float>(static_cast<double>(Inv) * In[0]);
    Out[0] = X;
    Out[1] = Y;
    Out[2] = Z;
}

void HudMatrixRotationY(float* M, float Angle)
{
    // FUN_004c8b40: the zeros and the 1, then cos / sin (FCOS / FSIN of the same argument).
    double const C = cos(static_cast<double>(Angle));
    double const S = sin(static_cast<double>(Angle));
    M[1] = 0.0f;
    M[3] = 0.0f;
    M[4] = 1.0f;
    M[5] = 0.0f;
    M[7] = 0.0f;
    M[0] = static_cast<float>(C);
    M[2] = static_cast<float>(S);
    M[6] = static_cast<float>(-S);
    M[8] = static_cast<float>(C);
}

void HudMatrixRotateRows12(float* M, float Angle)
{
    // FUN_004c8b80.
    double const C = cos(static_cast<double>(Angle));
    double const S = sin(static_cast<double>(Angle));
    for (int Col = 0; Col < 3; ++Col)
    {
        double const A = M[3 + Col];
        double const B = M[6 + Col];
        M[3 + Col] = static_cast<float>(A * C - S * B);
        M[6 + Col] = static_cast<float>(A * S + C * B);
    }
}

void HudMatrixRotateRows01(float* M, float Angle)
{
    // FUN_004c8bc0.
    double const C = cos(static_cast<double>(Angle));
    double const S = sin(static_cast<double>(Angle));
    for (int Col = 0; Col < 3; ++Col)
    {
        double const A = M[Col];
        double const B = M[3 + Col];
        M[Col] = static_cast<float>(A * C - S * B);
        M[3 + Col] = static_cast<float>(C * B + A * S);
    }
}

// ============================================================================================
// HUDItem
// ============================================================================================

HUDItem::HUDItem()
{
    // The inlined constructor (0x004c84de .. 0x004c8516, 0x004c8de3 .. 0x004c8e05).
    Name.Length = 0;
    Name.Data[0] = '\0';
    Target.set(nullptr);
    Next = nullptr;
    Target.set(nullptr);
    ToolTipIds = 0;
}

HUDItem::~HUDItem()
{
    // FUN_004c8630: `if (+0x28) set_pointer(0)` -- BaseWatcher::set(0); the member's own
    // destructor then finds nothing to unlink.
    if (Target.target() != nullptr)
        Target.set(nullptr);
}

bool HUDItem::Slot1()
{
    // FUN_0044fd00.
    return false;
}

void HUDItem::SetViewport(int const* ViewRect4)
{
    // FUN_0053e4a0.
    (void)ViewRect4;
}

bool HUDItem::Init()
{
    // FUN_004b6c50.
    return true;
}

void HUDItem::Shutdown()
{
    // FUN_00401e90.
}

void HUDItem::SetTarget(IObjRW* NewTarget)
{
    // FUN_004c81a0: the Watchable is IObjRW + 8 (null stays null); set_pointer only when it
    // differs from the one watched, which is BaseWatcher::set.
    Watchable const* const Watched = (NewTarget == nullptr)
        ? nullptr
        : reinterpret_cast<Watchable const*>(reinterpret_cast<unsigned char const*>(NewTarget) + 8);
    Target.set(Watched);
}

void HUDItem::Render(FSpaceCamera* Camera, float DeltaSeconds)
{
    // FUN_0053aaa0.
    (void)Camera;
    (void)DeltaSeconds;
}

bool HUDItem::Slot7(void* Command)
{
    // FUN_0053f330.
    (void)Command;
    return false;
}

bool HUDItem::Slot8(void* Command)
{
    // FUN_0053f330.
    (void)Command;
    return false;
}

bool HUDItem::Slot9(void* Command)
{
    // FUN_0053f330.
    (void)Command;
    return false;
}

void HUDItem::Slot10()
{
    // FUN_00401e90.
}

void HUDItem::ShowToolTip()
{
    // FUN_004c81d0. Nothing without a tool-tip id (0x004c81f3).
    if (ToolTipIds == 0)
        return;

    // 0x004c81f9 .. 0x004c8250: the hover record on the stack -- caption null, the Font::String
    // built (FUN_004144b0) -- the record's constructor -- then +0x04 = 0, +0x00 = -1, +0x44 = 0
    // and the (null) caption emptied, +0x60 = 0, then +0x04 = the id and +0x00 = this + id (the
    // ToolTip's "same source" key).
    FToolTipHoverInfo Info;
    Info.IdsAlt = 0;
    Info.Ids = -1;
    Info.AuxField = 0;
    Info.bLive = 0;
    Info.IdsAlt = ToolTipIds;
    Info.Ids = static_cast<int>(reinterpret_cast<intptr_t>(this) + ToolTipIds);
    NnToolTipFeedHover(&Info); // FUN_004be370

    // 0x004c8258 .. 0x004c82d7: the record's teardown (~FToolTipHoverInfo) -- FUN_004144f0,
    // the child releases (FUN_00418f70 per slot), the child vector freed, then the caption
    // buffer (null here).
}

// ============================================================================================
// HUDItem_Factory
// ============================================================================================

HUDItem_Factory::HUDItem_Factory(char const* Name)
{
    // The inlined base constructor (e.g. 0x004cd270 .. 0x004cd2cb): +0x08 = 0, +0x0c = 0,
    // +0x04 = 0, the name (a null name leaves it empty), then FUN_004c72b0.
    SectionName.Length = 0;
    SectionName.Data[0] = '\0';
    Next = nullptr;
    SectionName = Name;
    HudElementFactoryListAdd(this);
}

HUDItem_Factory::~HUDItem_Factory()
{
    // LAB_004c8350: the base vptr back, then FUN_004c72e0's body inlined.
    HudElementFactoryListRemove(this);
}

HUDItem* HUDItem_Factory::Create(INI_Reader& Reader)
{
    // FUN_0053e180.
    (void)Reader;
    return nullptr;
}

HUDItem* HUDItem_Factory::Slot1(void* Arg)
{
    // FUN_0053e180.
    (void)Arg;
    return nullptr;
}

// ============================================================================================
// Registry
// ============================================================================================

void HudElementFactoryListInit()
{
    // LAB_004c72a0.
    G_HudFactoryHead = nullptr;
    G_HudFactoryTail = nullptr;
}

void HudElementFactoryListAdd(HUDItem_Factory* Factory)
{
    // FUN_004c72b0: no null test on the argument.
    if (G_HudFactoryTail != nullptr)
    {
        G_HudFactoryTail->Next = Factory;
        G_HudFactoryTail = Factory;
        return;
    }
    G_HudFactoryHead = Factory;
    G_HudFactoryTail = Factory;
}

void HudElementFactoryListRemove(HUDItem_Factory* Factory)
{
    // FUN_004c72e0.
    HUDItem_Factory* Prev = nullptr;
    HUDItem_Factory* Cur = G_HudFactoryHead;
    if (Cur == nullptr)
        return;
    while (Cur != Factory)
    {
        Prev = Cur;
        Cur = Cur->Next;
        if (Cur == nullptr)
            return;
    }
    if (Prev != nullptr)
        Prev->Next = Cur->Next;
    else
        G_HudFactoryHead = Cur->Next;
    if (Cur == G_HudFactoryTail)
        G_HudFactoryTail = Prev;
    Factory->Next = nullptr;
}

void HudElementListInit()
{
    // LAB_004c7260.
    G_HudItemHead = nullptr;
    G_HudItemTail = nullptr;
}

void HudElementListSetTarget(IObjRW* NewTarget)
{
    // FUN_004c7330.
    for (HUDItem* Item = G_HudItemHead; Item != nullptr; Item = Item->Next)
        Item->SetTarget(NewTarget);
}

void HudElementListSetViewport(int const* ViewRect4)
{
    // FUN_004c78a0.
    for (HUDItem* Item = G_HudItemHead; Item != nullptr; Item = Item->Next)
        Item->SetViewport(ViewRect4);
}

void HudElementListRender(FSpaceCamera* Camera, float DeltaSeconds)
{
    // FUN_004c8090.
    DALib::MaterialBatcher->SetBinOrder(1, -1);
    for (HUDItem* Item = G_HudItemHead; Item != nullptr; Item = Item->Next)
        Item->Render(Camera, DeltaSeconds);
    DALib::MaterialBatcher->SetBinOrder(1, 1);
}

bool HudElementListAnySlot7(void* Command)
{
    // FUN_004c7820.
    for (HUDItem* Item = G_HudItemHead; Item != nullptr; Item = Item->Next)
    {
        if (Item->Slot7(Command))
            return true;
    }
    return false;
}

bool HudElementListAnySlot8(void* Command)
{
    // FUN_004c7860.
    for (HUDItem* Item = G_HudItemHead; Item != nullptr; Item = Item->Next)
    {
        if (Item->Slot8(Command))
            return true;
    }
    return false;
}

void HudElementListShutdown()
{
    // FUN_004c77a0.
    if (G_HudStaticTexture.Texture != nullptr)
    {
        DALib::TextureLib->ReleaseTextureRecord(G_HudStaticTexture.Texture);
        G_HudStaticTexture.Texture = nullptr;
    }
    for (HUDItem* Item = G_HudItemHead; Item != nullptr; Item = Item->Next)
        Item->Shutdown();
    HUDItem* Item = G_HudItemHead;
    while (Item != nullptr)
    {
        HUDItem* const NextItem = Item->Next;
        delete Item;
        Item = NextItem;
    }
    G_HudItemTail = nullptr;
    G_HudItemHead = nullptr;
    // 0x004c7810: JMP FUN_00425a20 on DAT_00673520.
    G_HudShapeDB.Clear();
}

void SetHudIniPath(char const* Path)
{
    // FUN_004c7370.
    if (Path == nullptr)
    {
        G_HudIniPathLength = 0;
        G_HudIniPath[0] = '\0';
        return;
    }
    unsigned Len = static_cast<unsigned>(strlen(Path));
    if (Len >= 0x104)
        Len = 0x103;
    memcpy(G_HudIniPath, Path, Len);
    G_HudIniPath[Len] = '\0';
    G_HudIniPathLength = Len;
}

char const* GetHudIniPath()
{
    return G_HudIniPath;
}

FTextureRef* GetHudStaticTexture()
{
    return &G_HudStaticTexture;
}

int GetHudStaticBlend()
{
    return G_HudStaticBlend;
}

void LoadHudElementsFromIni()
{
    // FUN_004c73d0.
    INI_Reader Reader;
    if (!Reader.open(G_HudIniPath, false))
    {
        // 0x004c75a0.
        FDUMP(0x100002, "%s(%d) : *** WARNING: hud file not found '%s'\n", kHudCpp, 0x13d,
              G_HudIniPath);
    }
    else if (Reader.read_header())
    {
        do
        {
            if (Reader.is_header("HUD"))
            {
                while (Reader.read_value())
                {
                    if (Reader.is_value("shapes"))
                    {
                        // 0x004c747a: FUN_00425b30 on DAT_00673520 against DALib::Data.
                        G_HudShapeDB.Load(Reader.get_value_string(), DALib::Data);
                    }
                    else if (Reader.is_value("static_txm"))
                    {
                        // 0x004c74a1: FUN_004255c0 on DAT_00673570.
                        TextureRefLoad(&G_HudStaticTexture, Reader.get_value_string());
                    }
                    else if (Reader.is_value("static_blend"))
                    {
                        // 0x004c74c1: get_value_float(0), ftol.
                        G_HudStaticBlend = static_cast<int>(Reader.get_value_float(0));
                    }
                }
                continue;
            }

            // 0x004c74e0: every factory whose name matches the header is asked in chain order;
            // the first one that returns an element wins.
            HUDItem* Item = nullptr;
            for (HUDItem_Factory* Factory = G_HudFactoryHead; Factory != nullptr;
                 Factory = Factory->Next)
            {
                if (!Reader.is_header(Factory->SectionName.Data))
                    continue;
                Item = Factory->Create(Reader);
                if (Item != nullptr)
                    break;
            }

            if (Item == nullptr)
            {
                // 0x004c7557.
                FDUMP(0x100002, "%s(%d) : *** WARNING: HUD - unknown INI entry '%s'\n", kHudCpp,
                      0x136, Reader.get_header_ptr());
                continue;
            }

            if (Item->Init())
            {
                // 0x004c752b: append through +0x34.
                if (G_HudItemTail != nullptr)
                {
                    G_HudItemTail->Next = Item;
                    G_HudItemTail = Item;
                }
                else
                {
                    G_HudItemHead = Item;
                    G_HudItemTail = Item;
                }
            }
            else
            {
                // 0x004c754d: vt[0](1).
                delete Item;
            }
        } while (Reader.read_header());
    }

    // 0x004c75c5 .. 0x004c75ee.
    ResetMouseFlightState();
}

float HudVectorAngle(float X, float Y)
{
    // FUN_0053eb10.
    float const AbsX = fabsf(X);
    float const AbsY = fabsf(Y);
    if (X == 0.0f && Y == 0.0f)
        return 0.0f;

    double const HalfPi = 1.5707963705062866;  // DAT_005dc88c
    double const Pi = 3.1415927410125732;      // DAT_005c8a5c
    double const MinusPi = -3.1415927410125732; // DAT_005df814

    if (AbsX > AbsY)
    {
        // 0x0053eb62: FPATAN of (Y / |X|, 1).
        double const A = atan(static_cast<double>(Y) / AbsX);
        if (X > 0.0f)
            return static_cast<float>(A - HalfPi);
        return static_cast<float>(HalfPi - A);
    }

    // 0x0053eb93: FPATAN of (-X / |Y|, 1).
    double const A = atan(-(static_cast<double>(X) / AbsY));
    if (!(Y < 0.0f))
        return static_cast<float>(A);
    if (A > 0.0)
        return static_cast<float>(Pi - A);
    return static_cast<float>(MinusPi - A);
}

// ============================================================================================
// HUD_Steer
// ============================================================================================

HUD_Steer::HUD_Steer(INI_Reader& Reader)
{
    // FUN_004c84a0 after operator new(0x74): the HUDItem part, +0x30 = 0, the holder at +0x4c
    // (FUN_00420d40, five 1.0f, anim -1), the name "Steer" (FUN_00417c50, cap 0x20), +0x38 = 0,
    // an inlined SetTarget(0), the three floats and +0x48, then the steering-arrow mesh
    // (FUN_0059ec80(path, 0, 1)) and the ini overrides.
    ClientMeshHolderConstruct(&MeshHolder);
    MeshHolder.UserFloats[4] = 1.0f;
    MeshHolder.UserFloats[3] = 1.0f;
    MeshHolder.UserFloats[2] = 1.0f;
    MeshHolder.UserFloats[1] = 1.0f;
    MeshHolder.UserFloats[0] = 1.0f;
    MeshHolder.AnimOrArchIndex = -1;
    Name = "Steer";
    Byte38 = 0;
    Target.set(nullptr);
    Field44 = 0.2f;
    Range = 200.0f;
    Radius = 40.0f;
    Byte48 = 0;
    ClientMeshLoadFromPath(&MeshHolder, "interface\\hud\\HUD_steeringarrow.3db", 0, true);

    while (Reader.read_value())
    {
        if (Reader.is_value("radius"))
            Radius = Reader.get_value_float(0);
        else if (Reader.is_value("range"))
            Range = Reader.get_value_float(0);
    }
}

HUD_Steer::~HUD_Steer()
{
    // FUN_004c8c20: with an instance bound, FUN_0059eee0; then the holder's destructor
    // FUN_0059ef20 (~FClientMeshHolder: the same release again, which finds nothing); then
    // ~HUDItem.
    if (MeshHolder.EngineInstance != reinterpret_cast<void*>(static_cast<intptr_t>(-1)))
        ClientUiMeshHolderRelease(&MeshHolder);
}

bool HUD_Steer::Slot1()
{
    // LAB_004c8670.
    return Byte38 != 0;
}

void HUD_Steer::Render(FSpaceCamera* Camera, float DeltaSeconds)
{
    // LAB_004c8680 (RET 8). Neither argument is read.
    (void)Camera;
    (void)DeltaSeconds;

    // 0x004c868b .. 0x004c86da: shown unless the player's ship says otherwise -- its behaviour
    // interface byte +0x168 when it has one, then a tradelane ride or the letterbox hides it.
    // Without a ship the arrow stays enabled.
    unsigned char bShow = 1;
    CEqObj* const Ship = HudPlayerShip();
    if (Ship != nullptr)
    {
        IBehaviorManager* const Behavior = Ship->get_behavior_interface();
        if (Behavior != nullptr)
            bShow = reinterpret_cast<unsigned char const*>(Behavior)[0x168];
        if (static_cast<CShip*>(Ship)->is_using_tradelane())
            bShow = 0;
        else if (IsLetterboxVisible())
            bShow = 0;
    }

    // 0x004c86dc .. 0x004c8709: under the 0.25s hold, only a locked mouse-flight draws
    // (FUN_0045a470 clear and DAT_0067354c set -- IsMouseFlightLocked).
    if (!IsMouseFlightHoldThresholdReached() && !IsMouseFlightLocked())
        return;
    // 0x004c870f .. 0x004c872a: not in the turret or rear view (HUD_Status +0x5b9 / +0x5ba).
    if (HudStatusIsTurretView() || HudStatusIsRearView())
        return;
    // 0x004c8730: FUN_0042d740.
    if (ShouldSkipLocalServerTick())
        return;
    if (bShow == 0)
        return;

    int CursorX = 0;
    int CursorY = 0;
    int CursorZ = 0;
    GetClientSoftwareCursor(&CursorX, &CursorY, &CursorZ);
    int const SurfaceW = static_cast<int>(G_RenderSurfaceWidth);   // DAT_00610850
    int const SurfaceH = static_cast<int>(G_RenderSurfaceHeight);  // DAT_00610854

    // 0x004c8794 .. 0x004c8850: the cursor's offset from the surface centre, divided by
    // -(range * width * 0.00125) (FUN_004c8b10), and radius / range as the dead zone.
    float const CursorYf = static_cast<float>(HudSteerCursorY(CursorY));
    float const HalfW = static_cast<float>(SurfaceW / 2);
    float const HalfH = static_cast<float>(SurfaceH / 2);
#ifdef FREELANCER_WIDESCREEN_SUPPORT
    // Port-only Hor+: the steering circle is interface, scaled by height like the rest of it.
    float const RangePixels = static_cast<float>(
        static_cast<double>(Range) * WidescreenEquivalentSurfaceWidth() * 0.0012499999720603228);
#else
    float const RangePixels =
        static_cast<float>(static_cast<double>(Range) * SurfaceW * 0.0012499999720603228);
#endif
    int const CursorYi = static_cast<int>(CursorYf);
    float const Cursor[3] = {static_cast<float>(CursorX), static_cast<float>(CursorYi), 0.0f};
    float const Offset[3] = {Cursor[0] - HalfW, Cursor[1] - HalfH, 0.0f}; // FUN_00423b60
    float Dir[3];
    HudVec3ScaleByInverse(Dir, Offset, -RangePixels);
    float const DeadZone = static_cast<float>(static_cast<double>(Radius) / Range);

    // 0x004c8850 .. 0x004c8885: FUN_00410190's length clamped to 1, and zero inside the dead
    // zone.
    double const LengthExact = sqrt(static_cast<double>(Dir[0]) * Dir[0] +
                                    static_cast<double>(Dir[1]) * Dir[1] +
                                    static_cast<double>(Dir[2]) * Dir[2]);
    float Length = static_cast<float>(LengthExact);
    if (LengthExact > 1.0)
        Length = 1.0f;
    // 0x004c8878: TEST AH,0x41 / JP keeps the length when it is above the dead zone or unordered.
    if (Length <= DeadZone)
        Length = 0.0f;

    // 0x004c888b .. 0x004c8924: the arrow's roll, -FUN_0053eb10(dx, dy) with dx = x - w/2 and
    // dy = h/2 - y.
    int const CursorYi2 = static_cast<int>(static_cast<float>(HudSteerCursorY(CursorY)));
    float const Dx = static_cast<float>(CursorX - SurfaceW / 2);
    float const Dy = static_cast<float>(SurfaceH / 2 - CursorYi2);
    float const Angle = -HudVectorAngle(Dx, Dy);

    // 0x004c8928: FUN_0059e500.
    Win3DBeginDraw();

    // 0x004c892d .. 0x004c8990: the HUD chrome colour into the holder's user floats.
    double const Inv255 = 0.003921568859368563; // DAT_005c7f68
    MeshHolder.UserFloats[0] = static_cast<float>(G_HudChromeColorRgba[0] * Inv255);
    MeshHolder.UserFloats[1] = static_cast<float>(G_HudChromeColorRgba[1] * Inv255);
    MeshHolder.UserFloats[2] = static_cast<float>(G_HudChromeColorRgba[2] * Inv255);
    MeshHolder.UserFloats[4] = static_cast<float>(G_HudChromeColorRgba[3] * Inv255);

    // 0x004c8993 .. 0x004c89b5: FUN_004c8b40(0), FUN_004c8b80(0), then FUN_004c8bc0 with the
    // roll times -0.99999994f (DAT_005d6ee0).
    FMatrix Rotation;
    HudMatrixRotationY(&Rotation.D[0][0], 0.0f);
    HudMatrixRotateRows12(&Rotation.D[0][0], 0.0f);
    HudMatrixRotateRows01(&Rotation.D[0][0],
                          static_cast<float>(static_cast<double>(Angle) * -0.9999999403953552));

    // 0x004c89ba .. 0x004c8af1: one arrow segment every 0.15 (DAT_005d6edc) while below the
    // length: placed at Rotation * (0, t * 0.265, 0) with z = -1, scaled by t * 0.5 + 0.1, drawn
    // through the Win3D camera DAT_0067dbf8.
    float const Step = 0.15000000596046448f;
    float T = Step;
    if (Step < Length)
    {
        do
        {
            FVector Local;
            Local.X = 0.0f;
            Local.Y = static_cast<float>(static_cast<double>(T) * 0.26499998569488525);
            Local.Z = 0.0f;
            if (G_HudSteerMath == nullptr)
                G_HudSteerMath = Acquire3DMathEngine();
            FVector Placed;
            G_HudSteerMath->TransformMatrix(Placed, Rotation, Local);

            FTransform Xform;
            Xform.Rotation = Rotation;
            Xform.Translation.X = Placed.X;
            Xform.Translation.Y = Placed.Y;
            Xform.Translation.Z = -1.0f;
            if (G_HudSteerMath == nullptr)
                G_HudSteerMath = Acquire3DMathEngine();
            float const Scale = static_cast<float>(static_cast<double>(T) * 0.5 +
                                                   0.10000000149011612);
            G_HudSteerMath->Scale(Xform.Rotation, Xform.Rotation, Scale);

            DALib::Engine->SetInstanceMatrix(MeshHolder.EngineInstance, &Xform);
            DALib::Engine->UpdateInstance(MeshHolder.EngineInstance, 0, nullptr);
            DALib::Renderer->RenderInstance(Win3DGetCamera(), MeshHolder.EngineInstance, 1.0f, 0, nullptr,
                                   nullptr);

            // 0x004c8ada: FADD / FST into T, then FCOMP of the unrounded sum against the length.
            double const Sum = static_cast<double>(T) + Step;
            T = static_cast<float>(Sum);
            if (!(Sum < Length))
                break;
        } while (true);
    }

    // 0x004c8af7: FUN_0059e5a0.
    Win3DEndDraw();
}

HUDItem* HUD_SteerFactory::Create(INI_Reader& Reader)
{
    // FUN_004c84a0: operator new(0x74); a failed allocation returns 0.
    return new HUD_Steer(Reader);
}

namespace
{
    // DAT_006733dc -- the static factory (XC entry LAB_004c82f0: FUN_004c8400(this, "Steer"),
    // atexit LAB_004c8310 -> LAB_004c8350).
    HUD_SteerFactory G_HudSteerFactory("Steer");
}
