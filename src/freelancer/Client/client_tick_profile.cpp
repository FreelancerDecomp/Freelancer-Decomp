// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "client_tick_profile.h"

#include "font_atlas.h"
#include "font_imp.h"
#include "font_string.h"
#include "globals.h"
#include "win3d_immediate.h"
#include "win3d_renderables.h"

#if FREELANCER_ENABLE_TICK_PROFILE
#include <charconv>
#include <windows.h>
#endif

namespace
{
#if FREELANCER_ENABLE_TICK_PROFILE
    constexpr int G_SectionCount = static_cast<int>(EClientTickSection::Count);
    int64_t G_SectionTicks[G_SectionCount] = {};
    double G_CompletedMilliseconds[G_SectionCount] = {};
    int64_t G_ProfileFrequency = 0;
    int64_t G_RenderStart = 0;
    bool G_bProfileActive = false;
    bool G_bProfileRendering = false;

    int64_t ReadProfileTicks()
    {
        LARGE_INTEGER Ticks;
        QueryPerformanceCounter(&Ticks);
        return Ticks.QuadPart;
    }
#endif

#if FREELANCER_BETA_LABEL || FREELANCER_ENABLE_TICK_PROFILE
    struct FClientTickOverlay
    {
#if FREELANCER_BETA_LABEL
        FFontString Beta;
#endif

#if FREELANCER_ENABLE_TICK_PROFILE
        FFontString Labels[G_SectionCount];
        FFontString Digits[12];
        FFontString Units;
#endif

        explicit FClientTickOverlay(IFont* Font)
        {
#if FREELANCER_BETA_LABEL
            FontStringSetFont(&Beta, Font);
            FontStringSetText(&Beta, L"Beta 2");
#endif
#if FREELANCER_ENABLE_TICK_PROFILE
            // PlayerName uses readable Arial digits instead of the narrow HUD face.
            IFont* SmallFont = FindMenuFontImp("PlayerName");
            if (SmallFont == nullptr)
                SmallFont = FindMenuFontImp("ListText");
            if (SmallFont == nullptr)
                SmallFont = Font;
            wchar_t const* const Names[] = {
                L"Tick (prev):", L"->UI tick:", L"->Server tick:", L"->Game tick:",
                L"->->Objects:", L"->->Physics / hits:",
                L"->->->Physics step:", L"->->->->IVP simulate:",
                L"->->->->->PSI prepare:", L"->->->->->Sim units:",
                L"->->->->->Commit / hulls:", L"->->->->->Contacts / events:",
                L"->->->->Sync objects:", L"->->->Move beams:",
                L"->->->Find beam hits:", L"->->->Dispatch hits:", L"->->World / FX:",
                L"->->Sound:", L"->->Render:", L"->->Present:"
            };
            static_assert(sizeof(Names) / sizeof(Names[0]) == G_SectionCount,
                          "one label per profiling section");
            FontStringSetFont(&Units, SmallFont);
            FontStringSetText(&Units, L"ms.");
            for (int Index = 0; Index < G_SectionCount; ++Index)
            {
                FontStringSetFont(&Labels[Index], SmallFont);
                FontStringSetText(&Labels[Index], Names[Index]);
            }
            wchar_t const Characters[] = L"0123456789.-";
            for (int Index = 0; Index < 12; ++Index)
            {
                wchar_t const Glyph[] = {Characters[Index], L'\0'};
                FontStringSetFont(&Digits[Index], SmallFont);
                FontStringSetText(&Digits[Index], Glyph);
            }
#endif
        }
    };

    // Explicitly released before font teardown, rather than a static RAII destructor
    // after FontShutdown. Cache ordinary digit glyphs: changing values must neither
    // rasterize changing lines nor depend on the separate NumberString face/UV path.
    FClientTickOverlay* G_TickOverlay = nullptr;
#endif
}

#if FREELANCER_ENABLE_TICK_PROFILE
FClientTickProfileFrame::FClientTickProfileFrame()
    : Start(0), bOwnsFrame(!G_bProfileActive)
{
    if (!bOwnsFrame)
        return;
    if (G_ProfileFrequency == 0)
    {
        LARGE_INTEGER Frequency;
        QueryPerformanceFrequency(&Frequency);
        G_ProfileFrequency = Frequency.QuadPart;
    }
    for (int Index = 0; Index < G_SectionCount; ++Index)
        G_SectionTicks[Index] = 0;
    G_bProfileRendering = false;
    G_bProfileActive = true;
    Start = ReadProfileTicks();
}

FClientTickProfileFrame::~FClientTickProfileFrame()
{
    if (!bOwnsFrame)
        return;
    G_SectionTicks[static_cast<int>(EClientTickSection::Tick)] = ReadProfileTicks() - Start;
    G_bProfileActive = false;
    G_bProfileRendering = false;
    for (int Index = 0; Index < G_SectionCount; ++Index)
    {
        G_CompletedMilliseconds[Index] =
            static_cast<double>(G_SectionTicks[Index]) * 1000.0 /
            static_cast<double>(G_ProfileFrequency);
    }
}

FClientTickScope::FClientTickScope(EClientTickSection InSection)
    : Section(InSection), Start(0), bRunning(G_bProfileActive)
{
    if (bRunning)
        Start = ReadProfileTicks();
}

FClientTickScope::~FClientTickScope()
{
    Stop();
}

void FClientTickScope::Stop()
{
    if (!bRunning)
        return;
    G_SectionTicks[static_cast<int>(Section)] += ReadProfileTicks() - Start;
    bRunning = false;
}

FClientPhysicsProfileScope::FClientPhysicsProfileScope()
    : Previous(nullptr), bRunning(G_bProfileActive)
{
    if (bRunning)
        Previous = PhySys::SetTickProfile(&Counters);
}

FClientPhysicsProfileScope::~FClientPhysicsProfileScope()
{
    Stop();
}

void FClientPhysicsProfileScope::Stop()
{
    if (!bRunning)
        return;
    PhySys::SetTickProfile(Previous);
    EClientTickSection const Destinations[] = {
        EClientTickSection::PhysicsSimulate, EClientTickSection::PhysicsPsiPrepare,
        EClientTickSection::PhysicsSimUnits, EClientTickSection::PhysicsCommitHulls,
        EClientTickSection::PhysicsContacts, EClientTickSection::PhysicsSync
    };
    static_assert(sizeof(Destinations) / sizeof(Destinations[0]) ==
                  static_cast<int>(EPhysicsTickSection::Count), "physics profile mapping");
    for (int Index = 0; Index < static_cast<int>(EPhysicsTickSection::Count); ++Index)
        G_SectionTicks[static_cast<int>(Destinations[Index])] += Counters.Ticks[Index];
    bRunning = false;
}

void BeginClientProfileRender()
{
    if (G_bProfileActive && !G_bProfileRendering)
    {
        G_RenderStart = ReadProfileTicks();
        G_bProfileRendering = true;
    }
}

void EndClientProfileRender()
{
    if (G_bProfileRendering)
    {
        G_SectionTicks[static_cast<int>(EClientTickSection::Render)] +=
            ReadProfileTicks() - G_RenderStart;
        G_bProfileRendering = false;
    }
}
#endif

void DrawClientTickProfile()
{
#if FREELANCER_BETA_LABEL || FREELANCER_ENABLE_TICK_PROFILE
    IFont* const Font = FindMenuFontImp("Normal");
    if (Font == nullptr)
        return;
    if (G_TickOverlay == nullptr)
        G_TickOverlay = new FClientTickOverlay(Font);

    UiOverlayPrepPipeline();
    int SavedViewport[4] = {};
    Win3DGetViewport(0xba2, SavedViewport);
    Win3DSetViewportRect(0, 0, static_cast<int>(G_RenderSurfaceWidth),
                        static_cast<int>(G_RenderSurfaceHeight), __FILE__, __LINE__);
    unsigned const SavedColor = FontAtlasDrawColor();
    FontAtlasSetDrawColor(0xffffffffu);

    FFontStringDrawOrigin Origin = {};
    Origin.X = 12.0f;
    Origin.Y = 12.0f;
    Origin.Scale = static_cast<float>(G_RenderSurfaceWidth) - 24.0f;
    Origin.Height = static_cast<float>(G_RenderSurfaceHeight) - 24.0f;
#if FREELANCER_BETA_LABEL
    FontStringDrawChildren(&Origin, &G_TickOverlay->Beta, nullptr, nullptr);
#endif
#if FREELANCER_ENABLE_TICK_PROFILE
    int Size[2] = {};
    float RowY = 12.0f;
#if FREELANCER_BETA_LABEL
    FontStringGetExtent(&G_TickOverlay->Beta, Size);
    RowY += static_cast<float>(Size[1]) + 4.0f;
#endif

    int LabelWidth = 0;
    for (int Index = 0; Index < G_SectionCount; ++Index)
    {
        FontStringGetExtent(&G_TickOverlay->Labels[Index], Size);
        if (Size[0] > LabelWidth)
            LabelWidth = Size[0];
    }
    int CellWidth = 0;
    int RowHeight = 0;
    for (int Index = 0; Index < 12; ++Index)
    {
        FontStringGetExtent(&G_TickOverlay->Digits[Index], Size);
        if (Size[0] > CellWidth)
            CellWidth = Size[0];
        if (Size[1] > RowHeight)
            RowHeight = Size[1];
    }
    for (int Index = 0; Index < G_SectionCount; ++Index)
    {
        Origin.X = 12.0f;
        Origin.Y = RowY;
        FontStringDrawChildren(&Origin, &G_TickOverlay->Labels[Index], nullptr, nullptr);
        char Value[32] = {};
        // Locale-independent decimal point; QPC's int64 range fits this buffer.
        std::to_chars(Value, Value + sizeof(Value) - 1, G_CompletedMilliseconds[Index],
                      std::chars_format::fixed, 1);
        Origin.X = 12.0f + static_cast<float>(LabelWidth) + 8.0f;
        for (int CharacterIndex = 0; Value[CharacterIndex] != '\0'; ++CharacterIndex)
        {
            char const Character = Value[CharacterIndex];
            int const GlyphIndex = Character == '.' ? 10 :
                                   Character == '-' ? 11 : Character - '0';
            Origin.Y = RowY;
            FontStringDrawChildren(&Origin, &G_TickOverlay->Digits[GlyphIndex], nullptr, nullptr);
            Origin.X += static_cast<float>(CellWidth);
        }
        Origin.X += 4.0f;
        Origin.Y = RowY;
        FontStringDrawChildren(&Origin, &G_TickOverlay->Units, nullptr, nullptr);
        RowY += static_cast<float>(RowHeight) + 2.0f;
    }

#endif

    UiOverlayPrepPipeline();
    FontAtlasSetDrawColor(SavedColor);
    Win3DSetViewportRect(SavedViewport[0], SavedViewport[1], SavedViewport[2],
                        SavedViewport[3], __FILE__, __LINE__);
#endif
}

void ShutdownClientTickProfile()
{
#if FREELANCER_BETA_LABEL || FREELANCER_ENABLE_TICK_PROFILE
    delete G_TickOverlay;
    G_TickOverlay = nullptr;
#endif
}
