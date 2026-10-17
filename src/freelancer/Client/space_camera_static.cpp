// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// The static in-flight camera: ctor step at 0x005493c1/0x005493c6, slots LAB_0051e920 (0x64),
// LAB_0051e980 (0x68) and LAB_0051e9d0 (0x74).

#include "space_camera_static.h"

#include "space_camera.h"
#include "space_letterbox.h"

namespace
{
    // 0x0051e92b / 0x0051e9b7: the same third of a second the script camera uses.
    const float kLetterboxSpeed = 3.0f;

    // .rdata 0x005e0594 (COL 0x005ef664 -> .?AVStaticCamera@@): the GameCamera table 0x005dbf5c
    // dword for dword except three slots -- 0x64 LAB_0051e920 (GameCamera 0x0051b450), 0x68
    // LAB_0051e980 (0x0051b480) and 0x74 LAB_0051e9d0 (0x0053e180). Leaves take `this` on the
    // stack, the port's slot convention.

}

void StaticCameraConstruct(FStaticCamera* Cam)
{
    // 0x005493c1: the controller calls the FGameCamera ctor directly and then swaps the vptr at
    // 0x005493c6. There is no ctor of its own -- which is the strongest evidence the class has
    // no fields to initialise. No null test: the one caller passes SpaceApp + 0x9a4.
    GameCameraConstruct((FGameCamera*)Cam);
}

bool StaticCameraActivate(FStaticCamera* Cam, const int* ViewRect4)
{
    // 0x0051e926: straight into FGameCamera's activate, and its result is what comes back.
    const bool bBaseResult = GameCameraActivate((FGameCamera*)Cam, ViewRect4);

    // 0x0051e937: bars in.
    SetLetterboxTarget(1.0f, kLetterboxSpeed);

    // 0x0051e93f: the same three calls the script camera makes.
    NotifyCinematicView(true);

    return bBaseResult;
}

void StaticCameraDeactivate(FStaticCamera* Cam)
{
    (void)Cam;

    // 0x0051e980: the base deactivate is IDPMsgHandler::OnDisconnect, an empty function the
    // linker folded the slot onto. Nothing to call.

    // 0x0051e985: the mirror, with a 0.
    NotifyCinematicView(false);

    // 0x0051e9b7: bars out. Note the order is the reverse of activate's -- the notify happens
    // BEFORE the bars here and AFTER them there.
    SetLetterboxTarget(0.0f, kLetterboxSpeed);
}

int StaticCameraOnEvent(FStaticCamera* Cam, const int* Event)
{
    (void)Cam;

    // 0x0051e9d4: one event code, and it is a plain integer compare against 15. Anything else
    // falls straight through.
    // No null test: 0x0051e9d4 dereferences the argument outright.
    if (*Event == kStaticCameraReleaseEvent)
        RequestSpaceCameraMode("player");

    // 0x0051e9e6: always zero, whether or not the event matched.
    return 0;
}

bool FStaticCamera::Activate(int const* ViewRect4)
{
    return StaticCameraActivate(this, ViewRect4);
}

void FStaticCamera::Deactivate()
{
    StaticCameraDeactivate(this);
}

int FStaticCamera::HandleCommand74(void* Command)
{
    return StaticCameraOnEvent(this, (int const*)Command);
}
