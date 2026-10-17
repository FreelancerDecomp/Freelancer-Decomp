// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Base in-flight camera. FSpaceCamera implements the common 20-method ICamera prefix;
// FGameCamera and the concrete camera modes derive from it.

#include "common_misc.h"

struct FSpaceCamera;

// Slot ids, in bytes, exactly as retail indexes them.
// Most leaves are __stdcall (this on stack). Exceptions matching ICamera / listing:
//   +0x48 GetVelocity — __thiscall, one stack arg (LAB_0040f5d0 RET 0x4)
//   +0x4C SetProperty — __thiscall, two stack args (FUN_0040f5f0 RET 0x8)
enum ESpaceCameraSlot
{
    kCamSlotQuery0            = 0x00,  // LAB_0040f350  (*out = 0; return -1)
    kCamSlotIsA               = 0x04,  // LAB_0040f360  (return 1)
    kCamSlotIsA2              = 0x08,  // LAB_0040f360  (same function, second slot)
    kCamSlotGetPosition       = 0x0c,  // LAB_0040f370  (this + 0x28)
    kCamSlotGetOrientation    = 0x10,  // LAB_0040f380  (this + 0x04)
    kCamSlotGetViewport       = 0x14,  // LAB_0040f470  (this + 0xac -- the ViewRect)
    kCamSlotGetHalfFovXDeg    = 0x18,  // LAB_0040f480  (float this + 0x98)
    kCamSlotGetFovYDeg        = 0x1c,  // LAB_0040f490  (float this + 0x9c)
    kCamSlotGetZNear          = 0x20,  // LAB_0040f4a0  (float this + 0xa4)
    kCamSlotGetZFar           = 0x24,  // LAB_0040f4b0  (float this + 0xa8)
    kCamSlotGetAspect         = 0x28,  // LAB_0040f4c0  (float this + 0xa0)
    kCamSlotGetXScaleOverNear = 0x2c,  // LAB_0040f4d0  (this+0x78 / this+0xa4)
    kCamSlotGetYScaleOverNear = 0x30,  // LAB_0040f4e0  (this+0x7c / this+0xa4)
    kCamSlotProject           = 0x34,  // FUN_0040ff50  (this, &x, &y, &z, worldPoint)
    kCamSlotUnproject         = 0x38,  // LAB_0040f520
    kCamSlotClassifySphere    = 0x3c,  // FUN_0040ffe0  1 = out, 3 = straddling, 4 = in
    kCamSlotGetViewTransform  = 0x40,  // LAB_0040f390  (lazily rebuilds this + 0x38)
    kCamSlotProjectVec        = 0x44,  // LAB_0040f4f0  (this, outVec, worldPoint)
    kCamSlotGetVelocity       = 0x48,  // LAB_0040f5d0  (writes a zero vector)
    kCamSlotSetProperty       = 0x4c   // FUN_0040f5f0  "fovx" / "fovy" / "znear" / "zfar"
};

// Retail object layout, byte for byte with FUN_0040f150. Everything is a raw float array so
// this header stays independent of Common's Vector/Matrix (and of whatever is being changed
// there); the offsets are what matters for a faithful port.
struct FSpaceCamera : ICamera
{
    GENRESULT DACOM_API QueryInterface(char const* Iid, void** Out) override;
    unsigned long DACOM_API AddRef() override;
    unsigned long DACOM_API Release() override;
    float const* DACOM_API GetPosition() override;
    float const* DACOM_API GetOrientation() override;
    int const* DACOM_API GetViewport() override;
    float DACOM_API GetHalfFovXDeg() override;
    float DACOM_API GetFovYDeg() override;
    float DACOM_API GetZNear() override;
    float DACOM_API GetZFar() override;
    float DACOM_API GetAspect() override;
    double DACOM_API GetXScaleOverNear() override;
    double DACOM_API GetYScaleOverNear() override;
    int DACOM_API Project(float* OutX, float* OutY, float* OutZ, float const* WorldPoint) override;
    void DACOM_API Unproject(float* OutWorld3, float ScreenX, float ScreenY) override;
    int DACOM_API ClassifySphere(float const* Center, float Radius) override;
    float const* DACOM_API GetViewTransform() override;
    int DACOM_API ProjectVec(float* Out3, float const* WorldPoint) override;
    void GetVelocity(float* Out3) override;
    int SetProperty(char const* Key, char const* Value) override;

    float  World[9];            // +0x004  orientation, row-major; ctor writes identity
    float  WorldPos[3];         // +0x028
    unsigned char bViewDirty;   // +0x034  ctor sets 1; slot 0x40 clears it after rebuilding
    unsigned char Pad035[3];
    float  View[9];             // +0x038  cached transpose(World)
    float  ViewPos[3];          // +0x05c  cached -(World * WorldPos)
    float  ScreenCenterX;       // +0x068  NOT initialised by the base ctor
    float  ScreenCenterY;       // +0x06c  NOT initialised by the base ctor
    float  HalfWidth;           // +0x070  320.0f
    float  HalfHeight;          // +0x074  240.0f
    float  XScale;              // +0x078  HalfWidth / tan(HalfFovX)
    float  YScale;              // +0x07c  -(HalfHeight / (0.75 * tan(HalfFovX)))
    float  PlaneX[3];           // +0x080  normalise(1, 0, tan(HalfFovX))
    float  PlaneY[3];           // +0x08c  normalise(0, 1, 0.75 * tan(HalfFovX))
    float  HalfFovXDeg;         // +0x098  35.0f
    float  FovYDeg;             // +0x09c  atan(0.75 * tan(HalfFovX)) in degrees
    float  Aspect;              // +0x0a0  1.3333334f
    float  ZNear;               // +0x0a4  3.0f      (DAT_00610530)
    float  ZFar;                // +0x0a8  250000.0f (DAT_00610534)
    // +0x0ac  the viewport, as Common's ViewRect: left, top, right, bottom in pixels. Slot 0x14
    // hands the whole thing out and the base ctor leaves it alone -- it is FUN_0040fd60 (and its
    // two-argument twin FUN_0040fb80) that fills it in, deriving the aspect, the half-extents,
    // the screen centre and both frustum planes from it. Retail stores four INTS here, which is
    // why 0x0040fd87 reads them back with FILD and not FLD.
    int    Viewport[4];
    unsigned char FlagBC;       // +0x0bc  ctor sets 0
    unsigned char Pad0BD[3];
};

// The base ctor's highest write is the byte at +0xbc, and the third-person camera derived from
// it puts its own first field at +0xc0 (FUN_00518af0 reads the watched object from there), so
// the base subobject is exactly 0xc0 bytes.
static_assert(sizeof(FSpaceCamera) == 0xc0, "FSpaceCamera must be 0xc0");

inline ICamera* AsCamera(FSpaceCamera* Cam) { return Cam; }

// FUN_0040f150: explicit field initialization; the C++ object already owns its vptr.
void SpaceCameraConstruct(FSpaceCamera* Cam);

// FUN_0040f8c0 -- a NON-virtual method, not a vtable slot. Takes the HALF horizontal angle in
// degrees, stores it at +0x98, derives the vertical half-angle at +0x9c through the live aspect
// and rebuilds both frustum planes and both screen scales. The ctor and the "fovx" branch of
// FUN_0040f5f0 are inlined copies of exactly this; the GameCamera ctor (0x0051b2f0) calls it.
void SpaceCameraSetHalfFovX(FSpaceCamera* Cam, float HalfFovXDeg);

// FUN_0040fa10 -- the vertical twin of the one above (thiscall, one stack float, RET 4): stores
// FovYDeg at +0x9c, derives the horizontal half-angle at +0x98 through the live aspect, then
// rebuilds both frustum planes and both screen scales. The "fovy" branch of FUN_0040f5f0 is an
// inlined copy; ShipViewControl's draw (0x00584509) calls it.
void SpaceCameraSetFovY(FSpaceCamera* Cam, float FovYDeg);

// FUN_0040f5f0 -- the base SetProperty ("fovx" / "fovy" / "znear" / "zfar"; nonzero when the key
// was one of them), called DIRECTLY, not through slot 0x4c: the GameCamera override
// (0x0051b3fe) calls the base body this way. Through the vtable it is ICamera::SetProperty.
int SpaceCameraSetProperty(FSpaceCamera* Cam, const char* Key, const char* Value);

#ifdef FREELANCER_WIDESCREEN_SUPPORT
// Port-only (widescreen.h): the Hor+ factor for this camera's stored viewport. Under the define
// +0x98 is the horizontal half-angle on a 4:3 surface; the planes, XScale and +0x9c use
// tan(+0x98) times this factor.
double SpaceCameraHorPlusScale(const FSpaceCamera* Cam);
#endif

// FUN_0040fd60 -- the other non-virtual rebuild, and the mirror image of the one above: the
// HALF-FOV is held fixed and the VIEWPORT changes, so the aspect comes out of the rectangle
// rather than out of the ini. Stores the four edges at +0xac, derives the aspect, both
// half-extents, the screen centre and the vertical half-angle, then rebuilds the two frustum
// planes and both screen scales.
//
// ViewRect4 is Common's ViewRect: left, top, right, bottom, INCLUSIVE -- retail's extents are
// `right - left + 1` and `bottom - top + 1` (0x0040fd91 and 0x0040fdac both INC the difference).
//
// Always returns true. That is not an interpretation: 0x0040ff29 loads AL with 1 outright and
// the only thing between there and the RET is an FPU compare that cannot touch AL. Retail's
// callers still branch on it -- FGameCamera's activate slot does -- so the port keeps the
// return value rather than quietly making it void.
bool SpaceCameraSetViewport(FSpaceCamera* Cam, const int* ViewRect4);

// FUN_0040fb80 -- byte for byte the same body as FUN_0040fd60 with two differences: it takes a
// second argument and it returns nothing. The second argument is dead. Retail never reads it:
// the only stack slots the body touches are the ViewRect pointer and three locals, and the one
// write that lands on an argument slot (0x0040fbb2) is the compiler reusing the FIRST
// argument's slot as scratch. Both overloads survived into the shipped build, so both are here.
void SpaceCameraSetViewportEx(FSpaceCamera* Cam, const int* ViewRect4, int UnusedFlag);

// The eight camera slots FUN_00549e70 selects between, at their retail offsets inside the
// in-flight controller (the `this` of FUN_00549e70).
enum ESpaceCameraMode
{
    kSpaceCameraFirstPerson = 0,   // controller + 0x00c
    kSpaceCameraThirdPerson = 1,   // controller + 0x1b0
    kSpaceCameraTurret      = 2,   // controller + 0x3c8
    kSpaceCameraRearview    = 3,   // controller + 0x500
    kSpaceCameraDeath       = 4,   // controller + 0x60c
    kSpaceCameraScript      = 5,   // controller + 0x760
    kSpaceCameraStatic      = 6,   // controller + 0x9a4
    kSpaceCameraDocking     = 7,   // controller + 0xa9c
    kSpaceCameraCount       = 8
};

// 0x0054ad25: the in-flight init opens the cockpit ini once and runs all four cockpit camera
// loaders over it, in the order FirstPerson, ThirdPerson, Turret, RearView. Returns false when
// any of them complained (a missing file, or an aspect that is not 4:3).
bool SpaceCameraLoadCockpit(const char* CockpitIniPath);

// FUN_00549e70. ModeName is one of "player", "first_person", "third_person", "turret",
// "rearview", "death", "script", "static", "docking"; anything else selects nothing and
// returns null, exactly as retail does. Returns the selected camera.
FSpaceCamera* SwitchSpaceCameraMode(const char* ModeName, void* AttachTarget);

// The camera FUN_00549e70 last selected, or null before the first successful switch.
FSpaceCamera* GetActiveSpaceCamera();

// FUN_0054bc30 / FUN_0054bc40 / FUN_0054bc50 (cdecl, `MOV EAX,imm / RET`) -- the addresses of
// the first-person (0x00678ba4), third-person (0x00678d48) and turret (0x00678f60) cameras
// inside the in-flight controller. Caller: the tunnel particle's visibility slot 0x005134c0.
FSpaceCamera* GetFirstPersonSpaceCamera();
FSpaceCamera* GetThirdPersonSpaceCamera();
FSpaceCamera* GetTurretSpaceCamera();

// FUN_0054bd50 (cdecl, no arguments) -- `if (DAT_00679738) DAT_00679738->+0xbc = 1`: the active
// camera's FlagBC raised, the same flag a hard cut raises (FUN_00549bc0). Callers: the jump-in
// FUN_00543630 (0x0054368f), the asteroid and nebula detail options (0x005267fb, 0x00509b83).
void RaiseActiveSpaceCameraCut();

// DAT_00678ba0 (SpaceApp + 0x08), the "controller exists" byte FUN_0054bcb0 (SpaceSendCommand)
// tests first. Writes: 1 by the space init (0x0054ab8e), 0 by the space teardown (0x0054af8d).
bool IsSpaceCameraControllerReady();

// FUN_0054bd40 -- MOV AL,[DAT_00678ba3]: in-flight controller 0x00678b98 + 0x0b, raised at the
// head of the space init (0x0054ab68) and dropped on each of its exits (0x0054abf2, 0x0054ac09,
// 0x0054ae8b). The HUD draws test it first (HUD_Core 0x004d56d9, 0x004d8698, HUD_Status
// 0x004db2c3, 0x004e1bb3) and skip the frame while the space init is running.
bool IsSpaceInitInProgress();

// FUN_0054bc80: is the camera FUN_0040f130 hands out (the port's GetClientSoundListenerObject)
// the first-person camera, i.e. .data 0x00678ba4 = in-flight controller 0x00678b98 + 0x0c?
bool IsFirstPersonCameraCurrent();

// FUN_0054bca0: the same test against the docking camera (.data 0x00679634). Caller: the Ship's
// land request (0x00543c11).
bool IsDockingCameraCurrent();

// The three-call tail the script and static cameras run when they take the view and when they
// give it back (0x0051e615 / 0x0051e6f1 and 0x0051e93f / 0x0051e985). All three are identical
// between the two cameras, so they live here rather than being written out twice:
//
//   1. take the player's IObjRW (FUN_0054baf0: the watcher at .data 0x00679744, minus eight);
//   2. its IObjInspect slot 8 (+0x20) get_id;
//   3. IServer (DAT_0067ecd0) slot 57 (+0xe4) SPRequestInvincibility(id, bActive, 5,
//      the connection id at .data 0x00673344) -- the ship is invincible while the view is
//      cinematic.
//
// Does nothing when there is no player object, which is retail's only guard (the server
// pointer is not tested).
void NotifyCinematicView(bool bActive);

// FUN_0054bc10 -- the guarded front door to the switch above, and what the cameras themselves
// call when they want to hand the view back. Retail's guard is the byte at .data 0x00678ba0,
// which is SpaceApp + 0x08 (raised by the space init, dropped by the teardown).
void RequestSpaceCameraMode(const char* ModeName);

// FUN_0054bb90 -- the ship's docking code hands the docking camera its fixed Csys (twelve
// floats: orientation then position), raises its view-dirty byte and, when the controller
// exists, switches to "docking". Callers are in Ship (0x00543cbf / 0x00543ec9).
void SetDockingCameraTransform(const float* Csys12);

// FUN_0054bbd0 -- arm the docking fade countdown (FUN_0051ebd0) on the docking camera;
// FDUMPs an ERROR (space.cpp:0x8ad) when that camera is not the active one, then arms it
// regardless. Caller: 0x00543cce.
void BeginDockingCameraSequence(float Seconds);

// FUN_0054bb10 -- point the script camera (FUN_0051e3b0) at Object under ScriptName from
// Transform12 (nine orientation floats then three of position; null keeps the stored one), switch
// to "script" when the controller exists, and raise the latch below. Caller: the IClient
// REQUESTSPACESCRIPT handler LAB_0053d9d0 (0x0053d9ff / 0x0053da7f).
void SetupSpaceScriptCamera(void* Object, const float* Transform12, const char* ScriptName);

// FUN_0054bc90 -- DAT_00679740 = SpaceApp + 0xba8: set to 1 by FUN_0054bb10, cleared by the
// space frame (0x0054b1fa), the teardown (0x0054af2e) and the ctor. Reader: FCommChannel::
// TickVoice (0x004cbc8b).
bool GetSpaceScriptCameraLatch();

// .data 0x0061402c = 0.95f, read at exactly one instruction in the whole PE (0x00549deb).
// It is a cosine: the view direction may rotate by up to ~18.2 degrees across a camera switch
// before the switch counts as a cut.
extern const float kSpaceCameraCutCosine;

// FUN_00549bc0's closing test (0x00549da8 .. 0x00549e18). Both orientations are nine floats,
// row-major; the axis compared is COLUMN 2 -- elements 2, 5 and 8 -- which is why retail reads
// them at a stride of 0x0c starting eight bytes into each matrix.
//
// The comparison is FCOMP + TEST AH,5 + JNP, which fires only on a strict, ordered "less than":
// a NaN anywhere leaves the dot unordered and does NOT count as a cut on its own. Having had no
// previous camera does count, unconditionally.
bool SpaceCameraIsHardCut(const float* OldOrientation, const float* NewOrientation,
                          bool bHadNoPreviousCamera);

// FUN_00549bc0 (RET 8, returns nothing): deactivate the outgoing camera (slot 0x68) and drop the
// sound listener; for a new camera, attach it to the player (slot 0x58), activate it with the
// window's client rect or the surface size (slot 0x64), take the fog colour at its position as
// its clear colour and clear (FUN_0051b500); make it active and the sound listener
// (FUN_0040f140); raise its +0xbc when the view jumps. Passing null deactivates. bForce
// reinstalls even when the camera is already the active one.
void InstallSpaceCamera(FSpaceCamera* NewCam, bool bForce);

// FUN_0054aec0 0x0054aef4..0x0054af18 -- the space teardown's camera block: FUN_00518410 on the
// first-person camera (controller + 0x00c, releases its cockpit mesh), then the non-virtual
// IDPMsgHandler::OnDisconnect (0x00401e90, a bare RET) on the third-person (+0x1b0), turret
// (+0x3c8) and rear-view (+0x500) cameras of G_SpaceApp.
void ReleaseSpaceCameraControllerParts();

// Retail keeps the player's first/third-person preference in controller + 0xba4 and reuses it
// whenever the mode is spelled "player".
int GetSpaceCameraPlayerPreference();
// The space init's write to controller + 0xba4 (0x0054ae06, with 0 under session flag 8).
void SetSpaceCameraPlayerPreference(int Preference);

// FUN_00549e70's local_28: true when the last selected mode was "script".
bool WasLastSpaceCameraModeScript();

// DAT_00614028 -- raised by every SwitchSpaceCameraMode call, even the ones that select nothing.
extern unsigned char G_SpaceCameraSwitched;
