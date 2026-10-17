// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "nn_rollover.h"

#include "cargo_bar_control.h" // ToolTipHoverSetCaption
#include "client_state.h"
#include "common_ini.h"
#include "common_misc.h" // GetFileSysPath
#include "font_atlas.h"
#include "font_imp.h"
#include "font_string.h"
#include "game_options.h"
#include "ids_strings.h"
#include "input_manager.h"
#include "knowledge_map.h"
#include "nn_text_scratch.h"
#include "nn_ui_pulse.h"
#include "perf_options.h"
#include "space_letterbox.h"
#include "win3d_immediate.h"

#include <math.h>
#include <string.h>
#include <wchar.h>

namespace
{
    NnRolloverControl* G_RolloverSingleton = nullptr; // DAT_00672288
    int G_RolloverCursorX = 0;                        // DAT_0067228c
    int G_RolloverCursorY = 0;                        // DAT_00672290

    // FUN_005b36b0("enable_rollover") -> the record's +0x14 (the applied value) against 0.999
    // (DAT_005c8b68, a double): FCOMP / TEST AH,5 / JNP leaves -- only a value below 0.999 bails
    // (equal and unordered go on). No null test on the record in retail.
    bool IsRolloverEnabled()
    {
        FPerfOption const* const Option = FindPerfOption("enable_rollover");
        return !(static_cast<double>(Option->Applied) < 0.999);
    }

    // A record {Ids, Ids, empty caption} as FUN_004b62f0 / FUN_004b6440 build it on the stack
    // (0x004b6319..0x004b6362): +0x08 / +0x0c zeroed and the Font::String constructed
    // (FUN_004144b0) -- the record's constructor, run by the caller's declaration -- then
    // FUN_004963a0 inlined (IdsAlt 0, Ids -1, +0x44 0, the caption's first wchar cleared when it
    // has one, +0x60 0), then Ids = IdsAlt = the argument.
    void BuildIdsRecord(FToolTipHoverInfo* Record, int Ids)
    {
        ToolTipHoverReset(Record);
        Record->Ids = Ids;
        Record->IdsAlt = Ids;
    }
}

// WINFACTORY<Rollover> DAT_0067224c: static initializer LAB_004b6260 -> FUN_004b65c0(&DAT_0067224c,
// "Rollover") + atexit(LAB_004b6280 -> FUN_00579380).
RolloverFactory G_RolloverFactory("Rollover");

NnRolloverControl::NnRolloverControl()
    : HoldSeconds(0.0f)
    , bWasVisible(0)
    , HiddenGrace(0.0f)
    , CursorStillSeconds(0.0f)
    , AnchorXSpace(0.0f)
    , AnchorXIntro(0.0f)
{
    // FUN_004b6630 0x004b66cd..0x004b671e: the +0xec record -- +0xf4 / +0xf8 zeroed and the
    // Font::String at +0xfc constructed (FUN_004144b0), the member's own ctor -- then
    // FUN_004963a0; the map at +0x150 (FUN_004b6800, an empty tree) is the member's own ctor.
    // Retail leaves the anchor (+0x16c) unset until Create; the port zeroes it.
    ToolTipHoverReset(&Entry);
    Anchor[0] = 0.0f;
    Anchor[1] = 0.0f;
    Anchor[2] = 0.0f;
    InitRolloverDefaults();
}

NnRolloverControl::~NnRolloverControl()
{
    // FUN_004b5340: the singleton cleared when it is this one, the map torn down (inline tree
    // erase), then the +0xec record (FUN_004144f0 on its Font::String, the child vector, the
    // text buffer, the caption buffer), then the TextWin dtor FUN_005a1af0. Both are the
    // members' own dtors, in retail's reverse declaration order (map, then ~FToolTipHoverInfo).
    if (G_RolloverSingleton == this)
        G_RolloverSingleton = nullptr;
}

void NnRolloverControl::InitRolloverDefaults()
{
    // FUN_004b4f10.
    DrawLayer = 10;                // +0x70
    HoldSeconds = 0.0f;            // +0x164
    HiddenGrace = 0.0f;            // +0x17c
    CursorStillSeconds = 0.0f;     // +0x180
    AnchorXSpace = -0.51f;         // +0x184 = 0xbf028f5c
    AnchorXIntro = 0.2f;           // +0x188 = 0x3e4ccccd
    bWasVisible = 0;               // +0x178

    // 0x004b4f4e: FUN_004177b0("Rollover"); when it differs from the Font::String's font (+0x98):
    // store it, slot +8 on every child of the vector at +0x8c..+0x90, zero the layout generation
    // (+0xa4) -- TextWin::SetFont's body without the scale store.
    IFont* const Font = FindMenuFontImp("Rollover");
    if (Text.FontIface != Font)
    {
        Text.FontIface = Font;
        for (unsigned Index = 0;
             Text.ChildBegin != nullptr &&
             Index < static_cast<unsigned>(Text.ChildEnd - Text.ChildBegin);
             ++Index)
        {
            // 0x004b4f8b..0x004b4f97: the child's vt+8 with +0x98 re-read, no null test.
            Text.ChildBegin[Index].Child->BindFont(Text.FontIface);
        }
        Text.LayoutGen = 0; // +0xa4
    }
    Text.Field08 = 4; // +0x84
    if (Text.bMultiLine != 1)
    {
        Text.bMultiLine = 1; // +0xac
        Text.LayoutGen = 0;
    }
}

bool NnRolloverControl::HitTest(float const* /*Ray*/)
{
    // slot 4 FUN_0053f330: XOR AL,AL / RET 4.
    return false;
}

bool NnRolloverControl::Create(Win3D* NewParent, void* /*Data*/)
{
    // FUN_004b54d0 (RET 8).
    Win3DReparent(this, NewParent); // FUN_0059dd00
    Win3DRegister(this);            // FUN_0059dbf0
    Flags = static_cast<unsigned char>((Flags & 0xfb) | kWin3DActive | kWin3DVisible);
    InitTextDefaults(); // FUN_005a1c50
    Width = 0.1f;       // +0xb4 = 0x3dcccccd
    Height = 0.1f;      // +0xb8
    Flags = static_cast<unsigned char>(Flags & 0xfd);
    DrawSubLayer = 7;         // +0x78
    FlagE0 = 0;               // +0xe0
    TextScale = 0.0011f;      // +0xb0 = 0x3a902de0
    FlagE1 = 1;               // +0xe1
    ColorD8 = 0xff434343u;    // +0xd8, bytes 43 43 43 ff
    ColorIdle = 0xffe0c387u;  // +0xcc, bytes 87 c3 e0 ff

    // 0x004b5571: FUN_005b0880 (the current state is the intro menu) picks +0x188 over +0x184.
    Anchor[0] = IsClientIntroEnvironment() ? AnchorXIntro : AnchorXSpace;
    Anchor[1] = 0.385f; // 0x3ec51eb8
    Anchor[2] = -1.0f;  // 0xbf800000

    LoadRolloverTable(); // FUN_004b59b0
    return true;
}

void NnRolloverControl::LoadRolloverTable()
{
    // FUN_004b59b0. The path buffer is 0x104 bytes (Stack[0x20]..Stack[0x124]).
    char FullPath[0x104];
    GetFileSysPath(FullPath, "interface\\Rollover.ini", nullptr);

    INI_Reader Reader;
    if (!Reader.open(FullPath, false))
        return;
    if (!Reader.read_header())
        return;
    do
    {
        if (Reader.is_header("RolloverTable") && Reader.read_value())
        {
            do
            {
                if (Reader.is_value("map"))
                {
                    // 0x004b5a7a..0x004b5a9f: both values through ftol -- key first.
                    int const Key = static_cast<int>(Reader.get_value_float(0));
                    int const Value = static_cast<int>(Reader.get_value_float(1));
                    // 0x004b5aa4..0x004b5b93: the unique-key insert (FUN_004b68b0 only when the
                    // key is not there yet) -- a repeated key keeps its first value.
                    Table.insert(std::pair<int const, int>(Key, Value));
                }
            } while (Reader.read_value());
        }
    } while (Reader.read_header());
}

bool NnRolloverControl::Slot23(unsigned Msg, unsigned /*A*/, unsigned /*B*/)
{
    // LAB_004b5700 (RET 0xc).
    if (Msg != 0xef02)
        return false;
    Flags = static_cast<unsigned char>(Flags & 0xfd);
    return true;
}

void NnRolloverControl::TickRollover(double DeltaSeconds)
{
    // FUN_004b5600 (RET 8, the frame time as a double).
    if (GetClientMouseCursorDrawFlag() == 0) // FUN_0041dd90
    {
        Flags = static_cast<unsigned char>(Flags & 0xfd);
        HoldSeconds = 0.0f;
        return;
    }

    if ((Flags & kWin3DVisible) == 0)
    {
        // 0x004b5628: hidden -- bit 0 cleared; the grace time runs down while it is positive
        // and is clamped to 0 when the (unrounded) difference goes below 0 (TEST AH,5 / JP).
        Flags = static_cast<unsigned char>(Flags & 0xfe);
        if (HiddenGrace > 0.0f)
        {
            double const Left = static_cast<double>(HiddenGrace) - DeltaSeconds;
            HiddenGrace = static_cast<float>(Left);
            if (Left < 0.0)
                HiddenGrace = 0.0f;
        }
    }
    else
    {
        HiddenGrace = 0.5f; // 0x004b5665: 0x3f000000
    }

    bWasVisible = static_cast<unsigned char>((Flags >> 1) & 1); // +0x178

    // 0x004b567c: the cursor (DAT_00616840 / DAT_00616844) against the last one seen.
    int CursorX = 0;
    int CursorY = 0;
    GetClientSoftwareCursor(&CursorX, &CursorY, nullptr);
    if (G_RolloverCursorX == CursorX && G_RolloverCursorY == CursorY)
    {
        CursorStillSeconds =
            static_cast<float>(static_cast<double>(CursorStillSeconds) + DeltaSeconds);
    }
    else
    {
        G_RolloverCursorX = CursorX;
        G_RolloverCursorY = CursorY;
        CursorStillSeconds = 0.0f;
    }

    // 0x004b56bc: the hold time runs down while it is positive; at or below 0 it is zeroed
    // and the rollover hidden. Not positive to begin with: hidden.
    if (HoldSeconds > 0.0f)
    {
        double const Left = static_cast<double>(HoldSeconds) - DeltaSeconds;
        HoldSeconds = static_cast<float>(Left);
        if (Left > 0.0)
            return;
        HoldSeconds = 0.0f;
    }
    Flags = static_cast<unsigned char>(Flags & 0xfd);
}

void NnRolloverControl::FeedHover(FToolTipHoverInfo const* Info)
{
    // FUN_004b5ca0 (RET 4).
    if (Info->IdsAlt == 0)
        return;
    if (GetClientMouseCursorDrawFlag() == 0)
    {
        Flags = static_cast<unsigned char>(Flags & 0xfd);
        return;
    }
    if (!IsRolloverEnabled())
        return;
    // 0x004b5d04: a record still held (+0x164 > 0) is not replaced.
    if (HoldSeconds > 0.0f)
        return;
    // 0x004b5d1b: the same record while it was visible goes straight on; otherwise, once the
    // grace time is over (+0x17c <= 0), the cursor must have rested for 0.25s (DAT_005c94f8).
    if (Entry.Ids != Info->Ids || bWasVisible == 0)
    {
        if (!(HiddenGrace > 0.0f) && CursorStillSeconds < 0.25f)
            return;
    }

    // 0x004b5d57: the table maps the hover's IdsAlt to the ids shown; no entry, nothing.
    std::map<int, int>::const_iterator const Found = Table.find(Info->IdsAlt);
    if (Found == Table.end())
        return;

    // 0x004b5d83..0x004b5e26: the record {Info->Ids, mapped ids, a copy of Info's caption}.
    FToolTipHoverInfo Record;   // caption null + FUN_004144b0; dtor FUN_00444ee0
    ToolTipHoverReset(&Record); // with FUN_00413df0(0, -1) on the empty caption
    Record.IdsAlt = Found->second;
    Record.Ids = Info->Ids;
    if (Info->CaptionW != nullptr)
    {
        // FUN_004142a0(Length + 1), wcsncpy, NUL.
        ToolTipHoverSetCaption(&Record, Info->CaptionW);
    }
    else if (Record.CaptionW != nullptr && Record.CaptionW[0] != L'\0')
    {
        Record.CaptionW[0] = L'\0';
    }

    Flags = static_cast<unsigned char>(Flags | kWin3DActive);
    if (Entry.Ids == Record.Ids)
    {
        // 0x004b5e3c: already showing it.
        Flags = static_cast<unsigned char>(Flags | kWin3DVisible);
        return;
    }

    // 0x004b5e9f: the anchor, the record's ids, the text, the position, the knowledge map.
    Anchor[0] = IsClientIntroEnvironment() ? AnchorXIntro : AnchorXSpace;
    Anchor[1] = 0.385f;
    Anchor[2] = -1.0f;
    Entry.Ids = Record.Ids;
    Flags = static_cast<unsigned char>(Flags | kWin3DVisible);
    Entry.IdsAlt = Record.IdsAlt;
    if (Record.IdsAlt != 0)
    {
        GetIdsStringWWithRawFallback(static_cast<unsigned>(Record.IdsAlt), G_NnTextScratchB,
                                     G_NnTextScratchCapacity); // FUN_004347e0
        SetText(G_NnTextScratchB);                             // slot 28 (+0x70)
    }
    else
    {
        SetText(Record.CaptionW);
    }
    SetPosition(Anchor); // slot 5 (+0x14)
    KnowledgeMapReveal(static_cast<unsigned>(Record.IdsAlt)); // FUN_00459930
}

void NnRolloverControl::ShowHover(FToolTipHoverInfo const* Info, float Hold)
{
    // FUN_004b5fd0 (RET 8).
    if (Info->IdsAlt == 0)
        return;
    if (GetClientMouseCursorDrawFlag() == 0)
    {
        Flags = static_cast<unsigned char>(Flags & 0xfd);
        return;
    }
    if (!IsRolloverEnabled())
        return;
    Flags = static_cast<unsigned char>(Flags | kWin3DActive);
    // 0x004b6031: the same record while it is still held only turns visible again.
    if (Entry.Ids == Info->Ids && HoldSeconds > 0.0f)
    {
        Flags = static_cast<unsigned char>(Flags | kWin3DVisible);
        return;
    }

    // 0x004b6059: FUN_005b0880 picks the anchor X.
    Anchor[0] = IsClientIntroEnvironment() ? AnchorXIntro : AnchorXSpace;
    Anchor[1] = 0.385f; // 0x3ec51eb8
    Anchor[2] = -1.0f;  // 0xbf800000
    HoldSeconds = Hold; // +0x164
    Flags = static_cast<unsigned char>(Flags | kWin3DVisible);
    Entry.Ids = Info->Ids;       // +0xec
    Entry.IdsAlt = Info->IdsAlt; // +0xf0
    if (Info->IdsAlt != 0)
    {
        GetIdsStringWWithRawFallback(static_cast<unsigned>(Info->IdsAlt), G_NnTextScratchB,
                                     G_NnTextScratchCapacity); // FUN_004347e0
        SetText(G_NnTextScratchB);                             // slot 28 (+0x70)
    }
    else
    {
        SetText(Info->CaptionW);
    }
    SetPosition(Anchor); // slot 5 (+0x14)
    KnowledgeMapReveal(static_cast<unsigned>(Info->IdsAlt)); // FUN_00459930
}

void NnRolloverControl::ShowHoverAt(FToolTipHoverInfo const* Info, float Hold, float AnchorX)
{
    // FUN_004b6130 (RET 0xc): FUN_004b5fd0 with the anchor X passed in.
    if (Info->IdsAlt == 0)
        return;
    if (GetClientMouseCursorDrawFlag() == 0)
    {
        Flags = static_cast<unsigned char>(Flags & 0xfd);
        return;
    }
    if (!IsRolloverEnabled())
        return;
    Flags = static_cast<unsigned char>(Flags | kWin3DActive);
    if (Entry.Ids == Info->Ids && HoldSeconds > 0.0f)
    {
        Flags = static_cast<unsigned char>(Flags | kWin3DVisible);
        return;
    }

    Anchor[0] = AnchorX;
    Anchor[1] = 0.385f;
    Anchor[2] = -1.0f;
    HoldSeconds = Hold;
    Flags = static_cast<unsigned char>(Flags | kWin3DVisible);
    Entry.Ids = Info->Ids;
    Entry.IdsAlt = Info->IdsAlt;
    if (Info->IdsAlt != 0)
    {
        GetIdsStringWWithRawFallback(static_cast<unsigned>(Info->IdsAlt), G_NnTextScratchB,
                                     G_NnTextScratchCapacity);
        SetText(G_NnTextScratchB);
    }
    else
    {
        SetText(Info->CaptionW);
    }
    SetPosition(Anchor);
    KnowledgeMapReveal(static_cast<unsigned>(Info->IdsAlt));
}

void NnRolloverControl::Draw()
{
    // LAB_004b5720.
    if (GetGameOption(kGameOptInterfaceGroup) == 0) // FUN_005792b0(6)
        return;
    if ((Flags & kWin3DVisible) == 0)
        return;
    if (IsLetterboxVisible()) // FUN_0041a3e0
        return;

    Win3DSetDepthWrite(0);           // FUN_00422190(0)
    Win3DDisable(kWin3DDepthTest);   // FUN_00421ed0(0xb71)
    Win3DDisable(kWin3DCullFace);    // FUN_00421ed0(0xb44)

    FontStringSetScale(&Text, 0.29f, Position[2]); // FUN_004145d0(0x3e947ae1, +0x68)
    int Extent[2];
    FontStringGetExtent(&Text, Extent); // FUN_00414570
    // 0x004b5793: both extents widened as unsigned (FILD qword with a zero high dword).
    float const ExtentW = static_cast<float>(static_cast<unsigned>(Extent[0]));
    float const ExtentH = static_cast<float>(static_cast<unsigned>(Extent[1]));
    Width = ExtentW;
    Bounds[0] = 0.0f;
    Bounds[1] = 0.0f;
    Height = ExtentH;
    Bounds[2] = ExtentW;
    Bounds[3] = -ExtentH;

    float ScreenX = 0.0f;
    float ScreenY = 0.0f;
    TextWinProjectPoint(Position, &ScreenX, &ScreenY); // FUN_00417730(+0x60)

    if (FlagE1 != 0)
    {
        // 0x004b57fd: the box -- floored origin, extent + {3, 2} (DAT_005d4f28 / DAT_005d55b4),
        // filled with +0xd8 then outlined in DAT_00679bb4, in the 2D projection. The
        // OnDisconnect(0x1701) calls around it are the ICF-folded bare RET 0x00401e90.
        float const X0 = static_cast<float>(floor(static_cast<double>(ScreenX)));
        float const Y0 = static_cast<float>(floor(static_cast<double>(ScreenY)));
        float const PadW = Width + 3.0f;
        float const PadH = Height + 2.0f;
        Win3DPushProjection(); // FUN_00422a70
        Win3DSetOrtho2D();     // FUN_004225e0
        Win3DColor4ubv(reinterpret_cast<unsigned char const*>(&ColorD8)); // FUN_00421ba0(+0xd8)
        Win3DDisable(kWin3DTexture2D); // FUN_00421ed0(0xde1)
        float const X1 = X0 + PadW;
        float const Y1 = Y0 + PadH;
        Win3DBegin(kWin3DQuads); // FUN_00421670(8)
        Win3DVertex3f(X0, Y0, 0.0f);
        Win3DVertex3f(X1, Y0, 0.0f);
        Win3DVertex3f(X1, Y1, 0.0f);
        Win3DVertex3f(X0, Y1, 0.0f);
        Win3DEnd(); // FUN_00421690
        Win3DColor4ubv(G_HudChromeColorRgba); // DAT_00679bb4
        Win3DBegin(kWin3DLineLoop);           // FUN_00421670(7)
        Win3DVertex3f(X0, Y0, 0.0f);
        Win3DVertex3f(X1, Y0, 0.0f);
        Win3DVertex3f(X1, Y1, 0.0f);
        Win3DVertex3f(X0, Y1, 0.0f);
        Win3DEnd();
        Win3DPopProjection(); // FUN_00422ab0
    }

    FontAtlasSetDrawColor(static_cast<unsigned>(ColorIdle)); // FUN_00417780(+0xcc)

    // 0x004b5939: the origin {x + 1, y, width, height, 0, 0} into FUN_004143c0(+0x7c, 0, 0).
    FFontStringDrawOrigin Origin;
    Origin.X = ScreenX + 1.0f;
    Origin.Y = ScreenY;
    Origin.Scale = ExtentW;
    Origin.Height = ExtentH;
    Origin.Field10 = 0;
    Origin.Field14 = 0;
    FontStringDrawChildren(&Origin, &Text, nullptr, nullptr);

    Win3DEnable(kWin3DDepthTest); // FUN_00421dd0(0xb71)
    Win3DSetDepthWrite(1);        // FUN_00422190(1)
}

void NnRolloverControl::HideRollover()
{
    HoldSeconds = 0.0f;                               // 0x004b65a9 +0x164 = 0
    Flags = static_cast<unsigned char>(Flags & 0xfd); // 0x004b65b8 +0x6c &= 0xfd
}

Win3D* RolloverFactory::Create(Win3D* Parent, void* Payload)
{
    // FUN_004b6630: operator new(0x18c), the ctor (FUN_004b4f10 inside), +0x6c &= 0xfd, the
    // singleton DAT_00672288, +0x10 = the factory's name (+0xc), then slot 9; on failure the
    // scalar deleting dtor and null.
    NnRolloverControl* const Tip = new NnRolloverControl();
    Tip->Flags = static_cast<unsigned char>(Tip->Flags & 0xfd);
    G_RolloverSingleton = Tip;
    Tip->ClassName = ClassName;
    if (!Tip->Create(Parent, Payload))
    {
        delete Tip;
        return nullptr;
    }
    return Tip;
}

NnRolloverControl* GetNnRolloverSingleton()
{
    return G_RolloverSingleton;
}

void NnRolloverTickGlobal(double DeltaSeconds)
{
    // FUN_004b6290.
    if (G_RolloverSingleton == nullptr)
        return;
    G_RolloverSingleton->TickRollover(DeltaSeconds);
}

void NnRolloverFeedHoverGlobal(FToolTipHoverInfo const* Info)
{
    // FUN_004b62b0.
    if (G_RolloverSingleton == nullptr)
        return;
    G_RolloverSingleton->FeedHover(Info);
}

void NnRolloverShowHoverGlobal(FToolTipHoverInfo const* Info, float HoldSeconds)
{
    // FUN_004b62d0.
    if (G_RolloverSingleton == nullptr)
        return;
    G_RolloverSingleton->ShowHover(Info, HoldSeconds);
}

void NnRolloverShowIdsGlobal(int Ids, float HoldSeconds)
{
    // FUN_004b62f0: the record is built, shown through FUN_004b5fd0 and destroyed
    // (0x004b6385..0x004b6407) only when the singleton exists.
    if (G_RolloverSingleton == nullptr)
        return;
    FToolTipHoverInfo Record;
    BuildIdsRecord(&Record, Ids);
    G_RolloverSingleton->ShowHover(&Record, HoldSeconds);
}

void NnRolloverShowHoverAtGlobal(FToolTipHoverInfo const* Info, float HoldSeconds,
                                 float AnchorX)
{
    // FUN_004b6420 (Ghidra splits it at LAB_004b6425: the raw bytes are MOV ECX,[DAT_00672288]
    // / TEST ECX,ECX / JZ ret / the three arguments / CALL FUN_004b6130 / RET).
    if (G_RolloverSingleton == nullptr)
        return;
    G_RolloverSingleton->ShowHoverAt(Info, HoldSeconds, AnchorX);
}

void NnRolloverShowIdsAtGlobal(int Ids, float HoldSeconds, float AnchorX)
{
    // FUN_004b6440: FUN_004b62f0's shape around FUN_004b6130.
    if (G_RolloverSingleton == nullptr)
        return;
    FToolTipHoverInfo Record;
    BuildIdsRecord(&Record, Ids);
    G_RolloverSingleton->ShowHoverAt(&Record, HoldSeconds, AnchorX);
}

void NnRolloverHideGlobal()
{
    // FUN_004b65a0: DAT_00672288 null -> nothing; else the two stores (the singleton is
    // re-read between them, same object).
    if (G_RolloverSingleton == nullptr)
        return;
    G_RolloverSingleton->HideRollover();
}

void NnRolloverDestroyGlobal()
{
    // FUN_004b6580: slot 10, then the singleton re-read (the dtor clears it only on delete, so
    // it is the same object) and its scalar deleting dtor with 1.
    if (G_RolloverSingleton == nullptr)
        return;
    G_RolloverSingleton->Destroy();
    if (G_RolloverSingleton == nullptr)
        return;
    delete G_RolloverSingleton;
}
