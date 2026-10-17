// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail ButtonMover / ButtonScaler (RTTI .?AVButtonMover@@ / .?AVButtonScaler@@) -- two small
// timed animators a UI class inherits to slide (ButtonMover) or scale (ButtonScaler) a set of
// WinControls over a fixed duration. Shared, not HUD-specific: NN_MainButton (0x0045b744 /
// 0x0045b756), the object at 0x0043d715 / 0x0043d725, NavBar (+0x32c / +0x34c, wave 4),
// HUD_Target_Base (ButtonMover at +0x364) and HUD_Target / HUD_Status (ButtonScaler at +0x4e0)
// all embed them as base-class subobjects.
//
// Retail layout of both (ctor FUN_0055dec0 / FUN_0055e340, 0x20 bytes):
//   +0x00 vftable (ButtonMover 0x005e18f8, ButtonScaler 0x005e1904), two slots each:
//         slot 0 LAB_00401e90 (RET)      -- OnFinished: called once the animation completes
//         slot 1 LAB_0043d8a0 (RET 8)    -- OnStep(Control, Value): true = the derived class
//                                           applied the value itself, skip the default
//   +0x04 std::vector (VC6: allocator byte, first, last, end) of entries
//         ButtonMover:  0x1c bytes {Start[3], End[3], WinControl*}
//         ButtonScaler: 0x0c bytes {From, To, WinControl*}
//   +0x14 float elapsed, +0x18 bool active, +0x1c float duration
// Neither class has a destructor of its own (no virtual dtor slot); the derived destructors free
// the vector inline. The port holds the entries in a std::vector (no retail ABI, AGENTS.md), so
// the member layout is not byte-exact.
//
// The two classes' slots carry distinct names (OnMove* / OnScale*) because HUD_Target and
// HUD_Status inherit BOTH and override each face with a different body (HUD_Target: mover
// slot 0 FUN_004e3320, scaler slot 0 FUN_004e3480); one shared C++ name would force a single
// override for both faces.

#include <vector>

class WinControl;

// Retail 0x1c-byte ButtonMover entry (FUN_0055dee0 builds it on the stack in this order).
struct FButtonMoveEntry
{
    float Start[3];      // +0x00
    float End[3];        // +0x0c
    WinControl* Control; // +0x18
};

// Retail 0x0c-byte ButtonScaler entry (FUN_0055e360).
struct FButtonScaleEntry
{
    float From;          // +0x00
    float To;            // +0x04
    WinControl* Control; // +0x08
};

class ButtonMover
{
public:
    // FUN_0055dec0 -- empty list, inactive; elapsed and duration are left unwritten.
    ButtonMover();

    // slot 0 (LAB_00401e90) -- called by Update / Finish when the move completes.
    virtual void OnMoveFinished();
    // slot 1 (LAB_0043d8a0, RET 8) -- per-control hook with the interpolated position; false
    // lets Update apply it through the control's SetProperty(6, Position, 0).
    virtual bool OnMoveStep(WinControl* Control, float const* Position);

    // FUN_0055dee0 (RET 0xc) -- append {Start, End, Control}.
    void AddButton(WinControl* Control, float const* Start, float const* End);
    // FUN_0055e120 (RET 4) -- Duration = Seconds, active, elapsed = 0, and clear the list (the
    // callers Start first and AddButton after, e.g. HUD_Target 0x004e3028).
    void Start(float Seconds);
    // FUN_0055e170 -- advance by the frame time and move every control (see .cpp).
    void Update();
    // FUN_0055e280 -- the +0x18 active flag (the same function serves ButtonScaler).
    bool IsActive() const { return bActive; }
    // FUN_0055e290 -- when active: stop and put every control at its End, then OnFinished.
    void Finish();
#ifdef FREELANCER_WIDESCREEN_SUPPORT
    // Port-only (hud_widescreen.cpp): how far the running move is, 0..1; 1 when idle.
    float GetProgress() const { return bActive && Duration > 0.0f ? Elapsed / Duration : 1.0f; }
#endif

protected:
    // No destructor in retail; derived destructors free the list inline.
    ~ButtonMover() {}

    std::vector<FButtonMoveEntry> Entries; // +0x04
    float Elapsed;                         // +0x14
    bool bActive;                          // +0x18
    float Duration;                        // +0x1c
};

class ButtonScaler
{
public:
    // FUN_0055e340 -- empty list, inactive.
    ButtonScaler();

    // slot 0 (LAB_00401e90) -- called by Update when the scale completes.
    virtual void OnScaleFinished();
    // slot 1 (LAB_0043d8a0, RET 8) -- per-control hook with the interpolated scale; false lets
    // Update rescale the control's engine instance.
    virtual bool OnScaleStep(WinControl* Control, float const* Scale);

    // FUN_0055e360 (RET 0xc) -- append {From, To, Control}.
    void AddButton(WinControl* Control, float From, float To);
    // FUN_0055e590 (RET 4) -- Duration = Seconds, active, elapsed = 0, clear the list.
    void Start(float Seconds);
    // FUN_0055e5e0 -- advance by the frame time and rescale every control (see .cpp).
    void Update();
    // FUN_0055e280 -- shared with ButtonMover (same +0x18 byte).
    bool IsActive() const { return bActive; }

protected:
    ~ButtonScaler() {}

    std::vector<FButtonScaleEntry> Entries; // +0x04
    float Elapsed;                          // +0x14
    bool bActive;                           // +0x18
    float Duration;                         // +0x1c
};
