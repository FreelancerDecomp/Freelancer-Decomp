// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail Client/Interface UIPage — the object a PAGE_*.txt layout becomes once instantiated.
//
// RTTI (freelancer.exe COL/CHD):
//   UIPage::IUIPage            vftable 0x005e534c  (slot 0 = 0x005914d0, slots 1..4 _purecall)
//   UIPage::Page : IUIPage     vftable 0x005e5334  (0x1c bytes, operator_new in FUN_00594590)
//   UIPage::MasterWin : Win3D, IUIPage (+0x7c), Event::Handler (+0x80)
//                              vftables 0x005e5384 (main), 0x005e536c (IUIPage),
//                              0x005e5364 (Event::Handler)
//
// MasterWin derives from Win3D (win3d.h); the UI* widgets from WinControl, TextWin or WinObject.

#include "event_handler.h"
#include "ui_page_desc.h"
#include "wincontrol_base.h"

#include "x86math.h"

#include <map>
#include <string>

// Retail DAT_0067dbc0 — the UI_PAGE widgets' shared lazy FUN_005b73e0 (3DMathEngine) cache,
// used by every UI* slot-8 override (FUN_00599b70, FUN_0059a530, ...).
I3DMathEngine* UiWidgetMathEngine();

// Shared body of the UI* slot-8 overrides after their SetLayout (+0x1c) / SetPosition (+0x14)
// calls (FUN_00599b70 00599b8c.., FUN_0059a530 0059a54c..): with M = the 3x3 at +0x3c and
// Z = the new translation Z,
//   OutWidth  = |(M * (Width, 0, Z)).x - (M * (0, 0, Z)).x|
//   OutHeight = |(M * (0, Height, Z)).y - (M * (0, 0, Z)).y|
// via four I3DMathEngine::TransformMatrix (+0x2c) calls in that order.
void UiWidgetProjectExtent(float const Matrix[9], float Z, float Width, float Height,
                           float* OutWidth, float* OutHeight);

namespace UIPage
{
    // vftable 0x005e534c.
    class IUIPage
    {
    public:
        // slot 0 — FUN_005914d0 scalar deleting destructor.
        virtual ~IUIPage() {}
        // slot 1 (+0x04) — widget count.
        virtual unsigned GetCount() = 0;
        // slot 2 (+0x08) — the Index-th widget name in map order, or null.
        virtual char const* GetWidgetName(unsigned Index) = 0;
        // slot 3 (+0x0c) — the Index-th widget in map order, or null.
        virtual Win3D* GetWidget(unsigned Index) = 0;
        // slot 4 (+0x10) — widget by instance name, or null.
        virtual Win3D* FindWidget(char const* Name) = 0;
    };

    // vftable 0x005e5334. Retail layout: +0x04 std::map<std::string, Win3D*> (VC6 _Tree, size
    // at +0x14), +0x18 float Aspect (1.0f from FUN_00594590's construction, then the page
    // descriptor's value).
    class Page : public IUIPage
    {
    public:
        Page();
        // FUN_00591510 (via FUN_005914f0): delete every mapped widget, then the map.
        ~Page() override;

        // LAB_00415070 — MOV EAX,[ECX+0x14] (map size).
        unsigned GetCount() override;
        // FUN_00591900
        char const* GetWidgetName(unsigned Index) override;
        // FUN_00591980
        Win3D* GetWidget(unsigned Index) override;
        // FUN_005919f0
        Win3D* FindWidget(char const* Name) override;

        // FUN_005916b0 — build every entry through the WINFACTORY registry, place it and
        // record it by name. Returns the result of the last child recursion (1 for an empty
        // array); a factory miss returns 0 at once.
        bool Instantiate(FUiWidgetEntry** Entries, Win3D* Parent, FTransform const* Matrix);

        float Aspect;   // +0x18

    private:
        std::map<std::string, Win3D*> Widgets;   // +0x04

        Page(Page const&) = delete;
        Page& operator=(Page const&) = delete;

        friend Page* CreatePage(FUiPageData* Data, FTransform const* Matrix, Win3D* Parent);
    };

    // FUN_00594590 (cdecl) — new Page, Aspect = Data->Aspect, Instantiate(Data->Entries, Parent,
    // Matrix); on failure the page is deleted and null returned.
    Page* CreatePage(FUiPageData* Data, FTransform const* Matrix, Win3D* Parent);

    // RTTI .?AVMasterWin@UIPage@@ : Win3D, IUIPage (+0x7c, vftable 0x005e534c), Event::Handler
    // (+0x80). vftable 0x005e5384 (28 slots). Retail +0x84 = FUiPageData*, +0x88 = Page*.
    // Event::Handler face (event_handler.h) at +0x80, vftable 0x005e5364: slot 0 = 0053f330
    // (XOR AL,AL / RET 4) -- MasterWin consumes no command; the PAGE screens that derive from it
    // override the slot.
    class MasterWin : public Win3D, public IUIPage, public EventHandler
    {
    public:
        // FUN_00591ae0 — Data is the owning screen's FUiPageDesc::GetPageData().
        explicit MasterWin(FUiPageData* Data);
        // FUN_00591ba0 (scalar deleting FUN_00591b80, IUIPage thunk LAB_00595320).
        ~MasterWin() override;

        // slot 9 (+0x24) — FUN_00591cd0.
        bool Create(Win3D* NewParent, void* Payload) override;
        // slot 10 (+0x28) — FUN_00591de0.
        void Destroy() override;

        // IUIPage face at +0x7c — LAB_00591c60 / 00591c70 / 00591c90 / 00591cb0 forward to
        // the Page at +0x88 (0 when there is none).
        unsigned GetCount() override;
        char const* GetWidgetName(unsigned Index) override;
        Win3D* GetWidget(unsigned Index) override;
        Win3D* FindWidget(char const* Name) override;

        // Event::Handler face at +0x80, slot 0 — FUN_0053f330: false.
        bool OnEvent(unsigned MsgId) override;

        Page* GetPage() const { return PageObject; }

    protected:
        FUiPageData* PageData;   // +0x84
        Page* PageObject;        // +0x88

    private:
        MasterWin(MasterWin const&) = delete;
        MasterWin& operator=(MasterWin const&) = delete;
    };
}
