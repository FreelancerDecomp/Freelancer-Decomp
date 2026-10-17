// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// The two intermediate camera classes every concrete in-flight camera derives from.
//
// Dumping the eleven camera vtables and their slot 0x54 (the class-name literal) settled the
// hierarchy: all seven named classes share the base's first eighteen slots VERBATIM and all
// seven override exactly slots 0x48 and 0x4c with the same pair of functions, which is only
// possible if those overrides live in a common intermediate class. That class names itself
// "GameCamera" (vtable .rdata 0x005dbf5c, ctor 0x0051b2f0). A second intermediate, "HUDCamera"
// (vtable 0x005dc08c, ctor 0x0051ca80), adds a secondary base and is what the four cockpit
// cameras actually derive from:
//
//   FSpaceCamera        0x000..0x0c0   vtable 0x005c8944, ctor 0x0040f150   <- fully ported
//     FGameCamera       0x0c0..0x0f8   vtable 0x005dbf5c, ctor 0x0051b2f0
//       FHudCamera      0x0f8..0x100   vtable 0x005dc08c, ctor 0x0051ca80
//         FirstPerson   0x005db944  ctor 0x005171a0-ish
//         ThirdPerson   0x005dbb14  ctor 0x00518600
//         Turret        0x005dbcc4  ctor 0x0051a270-ish
//         RearView      0x005dbe24  ctor 0x0051ad30            <- ported, space_camera_rear.h
//       ScriptCamera    0x005dc164  (derives from FGameCamera, not FHudCamera)
//
// CALLING CONVENTIONS, now settled from a real call site rather than inferred. At 0x0051b431
// the property override reads the half-FOV back with `push edi; call [ecx+0x18]`, i.e. slot
// 0x18 takes `this` as an explicit STACK argument -- which is how every slot from 0x00 to 0x44
// behaves. Two instructions earlier, at 0x0051b3fe, the very same function calls the base's
// slot 0x4c as `mov ecx, this; push value; push key; call` -- __thiscall. So the camera vtable
// genuinely MIXES the two: 0x00..0x44 are explicit-this, 0x48 and 0x4c are __thiscall. The port
// reproduces the mix (space_camera.h: DACOM_API/stdcall for 0x00..0x44, plain virtual/thiscall
// for GetVelocity and SetProperty; see the mixed-calling-convention camera entry in
// docs/re/deviations.md); binary compatibility with retail is not a goal, the semantics of
// each slot are.

#include "space_camera.h"
#include "event_handler.h"

#include "common_goods.h"    // BaseWatcher — retail type at FGameCamera+0xc0

#include <stddef.h>

// Extra vtable slots the GameCamera layer adds past the base's twenty, with the retail
// addresses of the implementations each concrete class installs. Only the ones marked below
// are ported; the rest need live objects, the math engine or the behaviour manager.
//
//   +0x50  0x0051c8b0 (FirstPerson uses 0x00401e90 instead)              <- ported
//   +0x54  the class name literal                                     <- ported per class
//   +0x58  attach: GameCamera 0x0051b3c0, others their own            <- ported for GameCamera
//   +0x5c  0x0051cb90 / GameCamera 0x0051b490
//   +0x60  0x005172f0 "no object watched"                             <- ported
//   +0x64  per class
//   +0x68  0x0051b0b0 / GameCamera 0x0051b480 (tail-jumps to 0x00401e90)
//   +0x6c  per class
//   +0x70  0x0051ad20 (tail-jumps to 0x0051da40) / GameCamera 0x0053e180
//   +0x74  0x0051da70
//   +0x78  0x0051b250 / GameCamera 0x0051c200
//   +0x7c  the sound environment the frame hands FUN_00428030 (0x0054b0b9): LAB_00517320
//          (`MOV EAX,2`) in HUDCamera and all four cockpit cameras, LAB_0051ca40 (`MOV EAX,1`)
//          in GameCamera and the cameras built on it (.rdata 0x005dbfd8, 0x005dc1e0, 0x005dc350)
enum EGameCameraSlot
{
    kGameCamSlotUnk50        = 0x50,
    kGameCamSlotGetName      = 0x54,
    kGameCamSlotAttach       = 0x58,
    kGameCamSlotUnk5c        = 0x5c,
    kGameCamSlotHasNoTarget  = 0x60,
    kGameCamSlotUnk64        = 0x64,
    kGameCamSlotUnk68        = 0x68,
    kGameCamSlotUnk6c        = 0x6c,
    kGameCamSlotUnk70        = 0x70,
    kGameCamSlotUnk74        = 0x74,
    kGameCamSlotUnk78        = 0x78,
    kGameCamSlotSoundMode    = 0x7c,
    kGameCamSlotCount        = 32          // the vtable is 32 slots, 0x80 bytes (HUDCamera's
                                           // 0x005dc08c..0x005dc108, the name string at 0x005dc10c)
};

// Retail layout, every offset traced to the store in FUN_0051b2f0.
struct FGameCamera : FSpaceCamera
{
    ~FGameCamera() = default;
    void GetVelocity(float* Out3) override;
    int SetProperty(char const* Key, char const* Value) override;
    virtual void ScaleEnvironmentEffects();               // formerly slot +0x50
    virtual char const* GetName();                        // formerly slot +0x54
    virtual void Attach(void* Object);                   // formerly slot +0x58
    virtual void ApplyViewport(int const* ViewRect4);     // formerly slot +0x5c
    virtual bool IsFinished();                           // formerly slot +0x60
    virtual bool Activate(int const* ViewRect4);          // formerly slot +0x64
    virtual void Deactivate();                           // formerly slot +0x68
    virtual void Update(float DeltaSeconds);              // formerly slot +0x6c
    virtual int HandleCommand70(void* Command);           // formerly slot +0x70
    virtual int HandleCommand74(void* Command);           // formerly slot +0x74
    virtual void DrawScene(unsigned LayerFlags, unsigned Param2); // formerly slot +0x78
    virtual int GetSoundMode();                          // formerly slot +0x7c


    BaseWatcher    Watched;          // +0x0c0  Common.dll BaseWatcher (sizeof 8)
    float          BaseHalfFovXDeg;  // +0x0c8  35.0f; re-cached whenever fovx/fovy is set
    unsigned char  Pad0CC[4];        // +0x0cc  the ctor never touches this
    float          VectorD0[3];      // +0x0d0  zeroed by the ctor
    float          Velocity[3];      // +0x0dc  zeroed; slot 0x48 hands this out
    unsigned char  Flags;            // +0x0e8  the ctor clears its low two bits; bit 0 = clear the
                                     //         colour buffer, bit 1 = clear depth (FUN_0051b500)
    unsigned char  ClearColor[3];    // +0x0e9  R, G, B bytes (written at 0x0051c303, 0x0051c881,
                                     //         0x00549cfe; read by FUN_0051b500)
    unsigned       FieldEC;          // +0x0ec  zeroed
    unsigned       FieldF0;          // +0x0f0  zeroed
    unsigned char  bFlagF4;          // +0x0f4  zeroed
    unsigned char  bFlagF5;          // +0x0f5  zeroed
    unsigned char  bFlagF6;          // +0x0f6  zeroed
    unsigned char  Pad0F7;
};

// The HUDCamera ctor's first own write is the vptr at +0xf8, so FGameCamera is exactly 0xf8.
static_assert(sizeof(FGameCamera) == 0x0f8, "FGameCamera must be 0xf8");
static_assert(offsetof(FGameCamera, Watched) == 0x0c0, "GC +0x0c0");
static_assert(offsetof(FGameCamera, BaseHalfFovXDeg) == 0x0c8, "GC +0x0c8");
static_assert(offsetof(FGameCamera, VectorD0) == 0x0d0, "GC +0x0d0");
static_assert(offsetof(FGameCamera, Velocity) == 0x0dc, "GC +0x0dc");
static_assert(offsetof(FGameCamera, Flags) == 0x0e8, "GC +0x0e8");
static_assert(offsetof(FGameCamera, FieldEC) == 0x0ec, "GC +0x0ec");
static_assert(offsetof(FGameCamera, FieldF0) == 0x0f0, "GC +0x0f0");
static_assert(offsetof(FGameCamera, bFlagF4) == 0x0f4, "GC +0x0f4");
static_assert(offsetof(FGameCamera, bFlagF6) == 0x0f6, "GC +0x0f6");

// FUN_0051ca80. Adds a SECOND base whose vptr lives at +0xf8. That base has exactly one virtual
// method -- the abstract table at .rdata 0x005cbed8 holds a single _purecall thunk -- and the
// implementations (0x0051d7f0 for HUDCamera and RearView, 0x0051a190 for ThirdPerson) all take
// one stack argument, run it through a keycode range test (0x2a..0xc8 at 0x0051d819) and return
// a bool. It is an input sink: the camera gets first refusal on a key before the game does.
struct FHudCamera : FGameCamera, EventHandler
{
    ~FHudCamera() = default;
    char const* GetName() override;
    void Attach(void* Object) override;
    void ApplyViewport(int const* ViewRect4) override;
    bool IsFinished() override;
    bool Activate(int const* ViewRect4) override;
    void Deactivate() override;
    void Update(float DeltaSeconds) override;
    int HandleCommand70(void* Command) override;
    int HandleCommand74(void* Command) override;
    void DrawScene(unsigned LayerFlags, unsigned Param2) override;
    int GetSoundMode() override;
    bool OnEvent(unsigned MsgId) override;

    unsigned char bFlagFC;          // +0x0fc  ctor sets 0
    unsigned char bFlagFD;          // +0x0fd  the FirstPerson ctor clears this at 0x005172a5,
                                    //         which is the only evidence it is a field at all
    unsigned char bFlagFE;          // +0x0fe  ctor sets 1
    unsigned char PadFF;
};

// The RearView ctor's first own write is +0x100, so FHudCamera is exactly 0x100.
static_assert(sizeof(FHudCamera) == 0x100, "FHudCamera must be 0x100");
static_assert(offsetof(FHudCamera, bFlagFC) == 0x0fc, "HC +0x0fc");
static_assert(offsetof(FHudCamera, bFlagFE) == 0x0fe, "HC +0x0fe");

// Single inheritance with the base first, so the subobject is always at offset zero. These
// two spell that out where a chain of `.Base.Base` would only obscure the intent.
inline FSpaceCamera* SpaceCameraOf(FGameCamera* Cam) { return Cam; }
inline FSpaceCamera* SpaceCameraOf(FHudCamera* Cam)  { return Cam; }

// The half-FOV both intermediate ctors start from: 0x420c0000.
extern const float kGameCameraDefaultHalfFovXDeg;

// Explicit field initialization preserves the callers' existing initialization boundaries.
// C++ construction owns vptrs; these helpers never reconstruct a live base subobject.
void GameCameraConstruct(FGameCamera* Cam);
void HudCameraConstruct(FHudCamera* Cam);

// Slot 0x54: the two intermediate classes' own name literals (0x0051b3b0 and 0x0051cac0).
const char* GameCameraGetName();
const char* HudCameraGetName();

// Slot 0x48 (0x0051b700): copies the three floats at +0xdc into the caller's vector, replacing
// the base's write-a-zero-vector stub.
void GameCameraGetVelocity(const FGameCamera* Cam, float* OutVelocity);

// Slot 0x6c (FUN_0051b650): the per-frame update every GameCamera shares. With a non-zero dt
// the velocity (+0xdc) becomes (position - last position) / dt; the position (the camera's
// own slot 0x0c) is then remembered in +0xd0. Derived updates call it last (HUDCamera at
// 0x0051d7b7).
void GameCameraUpdate(FGameCamera* Cam, float DeltaSeconds);

// FUN_0051b4b0 (non-virtual, thiscall): the space frame's per-camera preamble, called from
// FUN_0054b000 at 0x0054b0fd right before the camera's slot 0x78. Advances the letterbox with
// the capped frame time (FUN_0041a420), runs FUN_0051b500 when +0xe8 has either clear bit, then
// sets this camera's viewport (FUN_00422610, "Gamecam.cpp" line 0x85) and applies it to Win3D
// (FUN_00422950). The port's frame (client_frame_tick.cpp RenderWorldFrame) does not call it yet.
void GameCameraBeginFrame(FGameCamera* Cam);
// FUN_0051b500: +0xe8 bit 0 clears the colour buffer with ClearColor/255 (black first over the
// whole surface when the letterbox shows and the colour is not black, then the colour inside
// the camera's viewport), bit 1 adds the depth buffer; the first clear is always full-surface.
void GameCameraClear(FGameCamera* Cam);

// Slot 0x4c (0x0051b3f0): forwards to the base setter and, when it accepted a "fovx" or "fovy",
// re-caches the resulting half-angle into +0xc8. Returns false without doing anything else when
// the base refused the key.
bool GameCameraSetProperty(FGameCamera* Cam, const char* Key, const char* Value);

// Slot 0x58 (0x0051b3c0): binds the camera to an object. Object is an IObjRW*; retail stores it
// PLUS 8 (its Watchable subobject) and skips the work entirely when that is already the value.
// Passing null unbinds. Returns whether the watcher was actually re-pointed.
bool GameCameraAttach(FGameCamera* Cam, void* Object);

// Slot 0x60 (0x005172f0): true when the camera has nothing bound. Retail computes
// `(watched - 8) == 0`, so a watcher holding the degenerate value 8 also reads as empty.
bool GameCameraHasNoTarget(const FGameCamera* Cam);

// Slot 0x64 (FUN_0051b450) -- the activate every derived camera either inherits or calls into.
// It takes the viewport, hands it to the base rebuild, and, when that succeeds, calls slot 0x5c
// with the SAME rectangle. Since the base rebuild always succeeds, both always run.
//
// Retail therefore applies the viewport TWICE per activate: once through the one-argument
// FUN_0040fd60 and once through slot 0x5c's two-argument FUN_0040fb80. The second is not
// redundant to retail -- slot 0x5c is virtual, so a derived class could do something else with
// the rectangle -- but for GameCamera itself it is the same maths run again. Reproduced.
bool GameCameraActivate(FGameCamera* Cam, const int* ViewRect4);

// Slot 0x5c (FUN_0051b490). Applies the viewport and then forgets which camera the letterbox
// was last drawn on, so the bars are reapplied on the next update. Note it ignores `this` for
// the viewport call in the sense that matters: retail passes the rectangle, and `this` is
// whatever was already in ECX -- the camera being activated.
void GameCameraApplyViewport(FGameCamera* Cam, const int* ViewRect4);

// Slot 0x58 of HUDCamera and RearView (FUN_0051cbb0): GameCamera's watcher swap, then
// HudElementListSetTarget with the watched object when it is a client ship (Gameobj +0x1c == 1),
// else null.
void HudCameraAttach(FHudCamera* Cam, void* Object);

// Slot 0x5c of every HUDCamera (FUN_0051cb90): FUN_0051b490, then HudElementListSetViewport.
void HudCameraApplyViewport(FHudCamera* Cam, const int* ViewRect4);

// Slots 0x70 (FUN_0051da40 / LAB_0051ad20) and 0x74 (LAB_0051da70) of every HUDCamera: offer a
// command to the HUD elements unless a NeuroNet window is up. The vtable leaves are
// `int __stdcall (camera, void* Command)` -- the port's explicit-this slot convention; retail
// is __thiscall with RET 4. GameCamera's own 0x70/0x74 (LAB_0053e180) return 0.
int HudCameraCommand70(void* Command);
int HudCameraCommand74(void* Command);

// Slot 0x64 of HUDCamera (FUN_0051cad0, thiscall RET 4): raise the cockpit-strings flag
// (FUN_004c7290(1)), activate the "IDR_COCKPIT" accelerator group, register the Event::Handler
// face at +0xf8 for the nine cockpit command ids {0x5c, 0x2a..0x2f, 0xc7, 0xc8}, then
// GameCamera's activate (FUN_0051b450) -- whose answer is this one's.
bool HudCameraActivate(FHudCamera* Cam, const int* ViewRect4);
// Slot 0x68 of HUDCamera (FUN_0051cb70): drop "IDR_COCKPIT", lower the cockpit-strings flag,
// then the empty IDPMsgHandler::OnDisconnect. The nine event registrations stay.
void HudCameraDeactivate(FHudCamera* Cam);
// FUN_0051b0b0 -- slot 0x68 of all four HUDCamera subclasses (FirstPerson .rdata 0x005db9ac,
// ThirdPerson 0x005dbb7c, Turret 0x005dbd2c, RearView 0x005dbe8c): unregister the Event::Handler
// face at +0xf8 from every command (FUN_00576190), then HUDCamera's own slot 0x68 (FUN_0051cb70).
// Each cockpit camera overrides Deactivate with this shared body.
void HudCameraDerivedDeactivate(FHudCamera* Cam);
// FUN_0051b250 -- slot 0x78 of ThirdPerson (.rdata 0x005dbb8c), Turret (0x005dbd3c) and RearView
// (0x005dbe9c): SpaceCameraRenderSlot78 (space_scene_obj_draw.cpp). FirstPerson's 0x78 is its own
// LAB_00518420. The concrete overrides call this shared body.
// Slot 0x78 of HUDCamera (FUN_0051daa0, thiscall RET 8, both stack arguments unread): with a
// watched object and options 6 ([INTERFACE]) and 10 (HUD) on, apply this camera to Win3D
// (FUN_00422950) and render the HUD element list with the capped frame time (FUN_004c8090).
// Only GameCamera fields are read, and FUN_0051b250 calls it directly (0x0051b294) on its own
// `this`, so it takes the GameCamera.
void HudCameraDraw(FGameCamera* Cam);
// FUN_0051d7f0 -- slot 0 of the HUDCamera Event::Handler face (+0xf8; also RearView's). Blocked
// mouse pick (FUN_0045a740) -> false. 0x2a..0x2f set the six keyboard steering halves from the
// record's key-down byte, 0xc7 (unless the cursor is the arrow) the fire latch, 0xc8 toggles the
// mouse-flight lock; all of those answer false. 0x5c builds a 0x28-byte command record and hands
// it to the watched client ship's slot 0x1cc, answering true.
bool HudCameraOnEvent(FHudCamera* Cam, unsigned MsgId);
// Slot 0x6c of HUDCamera (FUN_0051d070, thiscall RET 4); FirstPerson (0x005183ab), ThirdPerson
// (0x00518f08), Turret (0x0051a7bf) and RearView (0x0051b242) call it from their own updates.
// Cinematic exclusion -> cursor latch / letterbox, fire latch upkeep, aim point to every gun,
// firing while the latch is up, keyboard steering (ramp or shift-to-target falloff) forwarded to
// the behavior manager when it moved, the once-a-second visit scan (FUN_0051cc00), the HUD
// hazard warning (FUN_004f5330), then GameCamera's update.
void HudCameraUpdate(FHudCamera* Cam, float DeltaSeconds);
// FUN_0051db00 (thiscall): decay the cockpit shake amplitude DAT_00675294 by DAT_0061373c
// (zero under 1e-4), then raise it from the watched client ship's shake flags (Gameobj +0x18c,
// +0x158/+0x188 -- not ported, see the .cpp).
void HudCameraUpdateShake(FHudCamera* Cam);
// The space init's store DAT_00675294 = 0 (0x0054ad78).
void ResetCameraShakeAmount();
// FUN_0051dbd0 (thiscall, RET 4): HudCameraUpdateShake, then with a positive amplitude two random
// rotations of the 3x3 orientation (rows 0/2, then rows 0/1) by angles in [-amp, amp). Callers:
// the FirstPerson (0x00518351) and RearView (0x0051b184) updates.
void HudCameraApplyShake(FHudCamera* Cam, float* Orientation);

// FUN_00516fe0 (cdecl, 5 callers: the third-person solver FUN_00518f20 and the turret
// FUN_0051a7d0). Keeps M's forward column (column 2) and rebuilds the other two around Up:
// right = normalise(Up x F) unless |F . Up| >= 0.999, when M's column 0 is kept; then
// C = F x right. Out is row-major {right, C, F} by column. Out must not alias M.
void CameraOrientFromForwardAndUp(float* Out, const float* M, const float* Up);

// The IObjRW the watcher currently holds, or null. Retail spells this `watched - 8` inline at
// every use site (0x005172fa, 0x0051b0e4, 0x0051b25d and others).
void* GameCameraGetTarget(const FGameCamera* Cam);
