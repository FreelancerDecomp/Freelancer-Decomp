// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// The docking camera: ctor FUN_0051ea20, slots FUN_0051eaa0 (0x58), LAB_0051ea50 (0x64),
// FUN_0051ea80 (0x68), LAB_0051eae0 (0x6c), and the non-virtual arm FUN_0051ebd0.

#include "space_camera_docking.h"

#include "fade_to_black.h"
#include "player_behavior.h"
#include "player_spawn.h"
#include "space_camera.h"
#include "space_letterbox.h"

#include "common_csys.h"
#include "common_iface.h"   // IObjInspect -- the ship's thiscall getters

#include <string.h>

namespace
{
    // 0x0051ea5e / 0x0051ea88: the same third of a second every cinematic camera uses.
    const float kLetterboxSpeed = 3.0f;

    // 0x0051ebe1: the elapsed counter is seeded with a tenth of a second, not with zero.
    const float kDockingElapsedSeed = 0.1f;

    // Same `- 8` as everywhere else: the watcher holds the Watchable, which is eight bytes into
    // the IObjRW.
    const int kWatchableOffset = 8;

    // FUN_0054baf0: the player's ship as an IObjRW, or null. Retail reads the Watched pointer out
    // of the BaseWatcher at .data 0x00679744 and subtracts eight; the port's watcher accessor
    // hands back that same pointer.
    void* GetPlayerShipObjRW()
    {
        void* const Watched = GetPlayerShipWatcher();
        if (Watched == nullptr)
            return nullptr;
        return static_cast<unsigned char*>(Watched) - kWatchableOffset;
    }

    // PTR_LAB_005dc2d4: the GameCamera table with 0x58 FUN_0051eaa0, 0x60 FUN_0044fd00 (the
    // shared `XOR AL,AL / RET` -- "never finished"), 0x64 LAB_0051ea50, 0x68 FUN_0051ea80 and
    // 0x6c LAB_0051eae0. Slot 0x54 stays GameCamera's name (LAB_0051b3b0). Leaves take `this` on
    // the stack, the port's slot convention.

}

void DockingCameraConstruct(FDockingCamera* Cam)
{
    // 0x0051ea23: the FGameCamera ctor, then the three own fields, then the vptr swap. No null
    // test on `this`.
    GameCameraConstruct((FGameCamera*)Cam);

    Cam->bFadePending = 0;          // 0x0051ea2a, a BYTE
    Cam->TotalSeconds = 0.0f;       // 0x0051ea30, written as a zero dword
    Cam->ElapsedSeconds = 0.0f;     // 0x0051ea36

    // 0x0051ea3c: vptr = PTR_LAB_005dc2d4.
}

void DockingCameraAttach(FDockingCamera* Cam, void* Object)
{
    // 0x0051eaa3 and 0x0051eaaa: before the watcher is touched at all, the player ship is told
    // to stop steering -- its current behaviour direction is reset, and its slide/strafe burst
    // is cancelled with StrafeNone. Both go through IBehaviorManager on the player IObjRW.
    ResetPlayerBehaviorDirection();
    SetPlayerStrafeBurst(0);   // 0x0051eaa8, PUSH 0 == StrafeNone

    // 0x0051eab6: the usual watcher swap, and the usual short-circuit when it already holds
    // exactly that target.
    Watchable const* const Target =
        (Object != nullptr)
            ? reinterpret_cast<Watchable const*>(static_cast<unsigned char*>(Object) +
                                                 kWatchableOffset)
            : nullptr;

    if (Target != Cam->Watched.target())
        Cam->Watched.set(Target);
}

bool DockingCameraActivate(FDockingCamera* Cam, const int* ViewRect4)
{
    // 0x0051ea59: FGameCamera's activate, and its result is what comes back.
    const bool bBaseResult = GameCameraActivate((FGameCamera*)Cam, ViewRect4);

    // 0x0051ea6a: bars in.
    SetLetterboxTarget(1.0f, kLetterboxSpeed);

    // 0x0051ea72: the countdown is disarmed on activate. Note it clears the FLAG only -- the two
    // floats keep whatever the last sequence left in them, and the arm below rewrites both.
    Cam->bFadePending = 0;

    // Unlike the script and static cameras, this one does NOT notify the ship. Worth saying out
    // loud: the three calls are simply absent from 0x0051ea50, not deferred.

    return bBaseResult;
}

void DockingCameraDeactivate(FDockingCamera* Cam)
{
    // 0x0051ea83: the base deactivate is the empty IDPMsgHandler::OnDisconnect.
    // 0x0051ea8f: bars out.
    SetLetterboxTarget(0.0f, kLetterboxSpeed);

    // 0x0051ea97: and the countdown is disarmed again.
    Cam->bFadePending = 0;
}

void DockingCameraUpdate(FDockingCamera* Cam, float DeltaSeconds)
{
    // 0x0051eae6: the aim half runs only while there is a ship to aim at. The countdown half
    // below runs either way.
    void* const Ship = GetPlayerShipObjRW();
    if (Ship != nullptr)
    {
        // 0x0051eaf9: `MOV ECX,ship / CALL [EDX]` -- IObjInspect::get_position (slot 0,
        // __thiscall).
        const Vector& ShipPosition = static_cast<IObjInspect*>(Ship)->get_position();

        const float Ship3[3] = {ShipPosition.x, ShipPosition.y, ShipPosition.z};

        // 0x0051eb12: and the camera's own position comes out of its slot 0x0c -- read through
        // the interface, not off the field, because a derived class could override it.
        FSpaceCamera* const Base = (FSpaceCamera*)Cam;
        const float* const CameraPosition = AsCamera(Base)->GetPosition();

        // 0x0051eb15 .. 0x0051eb3b: the direction is ship minus camera, computed Z first because
        // that is the order the FPU stack unwinds in.
        Vector Direction;
        Direction.x = Ship3[0] - CameraPosition[0];
        Direction.y = Ship3[1] - CameraPosition[1];
        Direction.z = Ship3[2] - CameraPosition[2];

        // 0x0051eb3f: Common's LookMatrixYup, the same export retail calls.
        const Matrix Look = LookMatrixYup(Direction);

        // 0x0051eb4b: the position is re-read after the call, and it is the camera's OWN -- the
        // docking camera never moves, it only turns.
        const float* const HoldPosition = AsCamera(Base)->GetPosition();

        // 0x0051eb7d: twelve dwords in one run into the world Csys, then the view cache is
        // invalidated.
        memcpy(Base->World, &Look.E[0][0], 9 * sizeof(float));
        Base->WorldPos[0] = HoldPosition[0];
        Base->WorldPos[1] = HoldPosition[1];
        Base->WorldPos[2] = HoldPosition[2];
        Base->bViewDirty = 1;
    }

    // 0x0051eb85: the docking fade. Nothing happens until the sequence has been armed.
    if (!Cam->bFadePending)
        return;

    // 0x0051eb8f..0x0051eb99: the elapsed counter takes the frame time. FST stores the float
    // but leaves the sum on the FPU, so the subtraction below uses the unrounded sum.
    const double ElapsedSum =
        static_cast<double>(DeltaSeconds) + static_cast<double>(Cam->ElapsedSeconds);
    Cam->ElapsedSeconds = static_cast<float>(ElapsedSum);

    // 0x0051eb9f..0x0051ebb4: and once the REMAINING time has dropped below the fade's own
    // duration, the fade starts -- so it finishes exactly as the docking sequence does.
    // FCOMP / TEST AH,0x5 / JP: skip unless C0 alone is set, i.e. only on an ordered "less".
    const double Remaining = static_cast<double>(Cam->TotalSeconds) - ElapsedSum;
    if (!(Remaining < static_cast<double>(GetFadeDuration())))
        return;

    // 0x0051ebb6: disarmed first, so the fade is started exactly once.
    Cam->bFadePending = 0;
    FadeOut();
}

void DockingCameraBeginSequence(FDockingCamera* Cam, float Seconds)
{
    // FUN_0051ebd0.
    Cam->bFadePending = 1;
    Cam->TotalSeconds = Seconds;
    Cam->ElapsedSeconds = kDockingElapsedSeed;
}

void FDockingCamera::Attach(void* Object)
{
    DockingCameraAttach(this, Object);
}

bool FDockingCamera::IsFinished()
{
    return false;   // FUN_0044fd00
}

bool FDockingCamera::Activate(int const* ViewRect4)
{
    return DockingCameraActivate(this, ViewRect4);
}

void FDockingCamera::Deactivate()
{
    DockingCameraDeactivate(this);
}

void FDockingCamera::Update(float DeltaSeconds)
{
    DockingCameraUpdate(this, DeltaSeconds);
}
