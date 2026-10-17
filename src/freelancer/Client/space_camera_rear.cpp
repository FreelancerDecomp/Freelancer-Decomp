// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Retail rear-view camera (in-flight controller + 0x500, vtable .rdata 0x005dbe24).
//
// Ported: the constructor (0x0051ad30), the name slot (0x0051ad70), the ini loader (0x0051ad80)
// and the per-frame update (LAB_0051b0d0, slot 0x6c). Every remaining vtable slot is an
// implementation it shares with the other cockpit cameras, listed in the header.

#include "space_camera_rear.h"
#include "space_scene_obj_draw.h"

#include "space_camera_first.h"  // G_FirstPersonMathEngine (DAT_00675220, shared Cockpit.cpp cache)

#include "common_cobject.h"
#include "common_csys.h"         // Acquire3DMathEngine (FUN_005b73e0)
#include "common_iface.h"        // IObjRW (thiscall getters)
#include "common_ini.h"
#include "dacom.h"
#include "x86math.h"

#include <math.h>
#include <string.h>

namespace
{
    const char kCockpitCpp[] = "E:\\FL\\Scratch\\Source\\Client\\space\\Cockpit.cpp";


    // .rdata 0x005dbf48: the double 0x400921fb60000000, i.e. (double)(float)pi -- the yaw the
    // rear view turns the ship's orientation by.
    const double kRearViewYaw = 3.1415927410125732;

}

void RearViewCameraConstruct(FRearViewCamera* Cam)
{
    // FUN_0051ad30.
    HudCameraConstruct((FHudCamera*)Cam);

    // 0x0051ad38: the literals are dword stores, not FPU work -- 0, 0x40a00000, 0x41a00000.
    Cam->ViewPosition[0] = 0.0f;
    Cam->ViewPosition[1] = 5.0f;
    Cam->ViewPosition[2] = 20.0f;

    // 0x0051ad5c: the face table 0x005dbe18, whose one slot is HUDCamera's own FUN_0051d7f0 --
    // the same content as HUDCamera's table 0x005dc084, which the port shares.
}

const char* RearViewCameraGetName()
{
    // 0x0051ad70: MOV EAX, .rdata / RET. No `this`, no arguments -- the same shape as every
    // other camera's name slot.
    return "RearView";
}

void RearViewCameraUpdate(FRearViewCamera* Cam, float DeltaSeconds)
{
    // LAB_0051b0d0 (slot 0x6c, thiscall RET 4).
    FHudCamera* const Hud = (FHudCamera*)Cam;
    FSpaceCamera* const Space = SpaceCameraOf(Hud);

    // 0x0051b0d6..0x0051b112: only with a watched client ship -- watcher - 8, its +0xc'd
    // pointer's +0x4, CObject mask 0x503. Every test falls through to HUDCamera's update.
    bool bShip = false;
    {
        unsigned char* const Held = const_cast<unsigned char*>(
            reinterpret_cast<const unsigned char*>(Hud->Watched.target()));
        if (Held != nullptr)
        {
            unsigned char* const Object = Held - 8;
            if (Object != nullptr)
            {
                unsigned char* const Plus0c = Object + 0xc;
                if (Plus0c != nullptr)
                {
                    CEqObj* const EqObj = *reinterpret_cast<CEqObj**>(Plus0c + 4);
                    if (EqObj != nullptr &&
                        (static_cast<unsigned>(EqObj->ObjectClass) & 0x503u) == 0x503u)
                        bShip = true;
                }
            }
        }
    }

    if (bShip)
    {
        IObjRW* const Object = static_cast<IObjRW*>(GameCameraGetTarget((FGameCamera*)Hud));

        // 0x0051b12d..0x0051b143: the ship's orientation (slot 0xc), nine dwords to the stack.
        float Orientation[9];
        memcpy(Orientation, &Object->get_transform().Rotation.E[0][0], sizeof(Orientation));

        // 0x0051b130..0x0051b180: FCOS and FSIN of the double at 0x005dbf48, kept on the FPU;
        // per row r, x' = x*cos - z*sin into [r][0] and z' = x*sin + z*cos into [r][2], both
        // from the row's original x and z. Column 1 is untouched.
        const double Cos = cos(kRearViewYaw);
        const double Sin = sin(kRearViewYaw);
        for (int Row = 0; Row < 3; ++Row)
        {
            const double X = Orientation[Row * 3 + 0];
            Orientation[Row * 3 + 0] =
                static_cast<float>(X * Cos - Sin * static_cast<double>(Orientation[Row * 3 + 2]));
            Orientation[Row * 3 + 2] =
                static_cast<float>(X * Sin + Cos * static_cast<double>(Orientation[Row * 3 + 2]));
        }

        // 0x0051b179..0x0051b184: FUN_0051dbd0 shakes it.
        HudCameraApplyShake(Hud, Orientation);

        // 0x0051b189..0x0051b1b0: DAT_00675220 (FUN_005b73e0 on first use), vt+0x2c -- the
        // view position (+0x100) through the shaken orientation.
        if (G_FirstPersonMathEngine == nullptr)
            G_FirstPersonMathEngine = Acquire3DMathEngine();
        FVector Offset;
        G_FirstPersonMathEngine->TransformMatrix(
            Offset, *reinterpret_cast<FMatrix const*>(Orientation),
            *reinterpret_cast<FVector const*>(Cam->ViewPosition));

        // 0x0051b1c3..0x0051b20b: onto the ship's position (slot 0), each sum stored as float.
        Vector const& ShipPos = Object->get_position();
        float Position[3];
        Position[0] = Offset.X + ShipPos.x;
        Position[1] = Offset.Y + ShipPos.y;
        Position[2] = Offset.Z + ShipPos.z;

        // 0x0051b1e6..0x0051b236: {orientation, position} -- twelve dwords -- to the camera's
        // world transform (+0x04), view dirty (+0x34).
        memcpy(Space->World, Orientation, 9 * sizeof(float));
        Space->WorldPos[0] = Position[0];
        Space->WorldPos[1] = Position[1];
        Space->WorldPos[2] = Position[2];
        Space->bViewDirty = 1;
    }

    // 0x0051b23b..0x0051b242: HUDCamera's update.
    HudCameraUpdate(Hud, DeltaSeconds);
}

bool RearViewCameraLoad(FRearViewCamera* Cam, const char* CockpitIniPath)
{
    // FUN_0051ad80, in retail's order. The warnings carry the source file and line the retail
    // build was compiled from: E:\FL\Scratch\Source\Client\space\Cockpit.cpp, lines 1763
    // (0x6e3) and 1770 (0x6ea).
    bool bOk = true;

    INI_Reader Reader;
    if (!Reader.open(CockpitIniPath, false))
    {
        // 0x0051add1..0x0051adf0: FDUMP called through the import with the raw path -- retail
        // tests neither.
        FDUMP(0x100002, "%s(%d) : *** WARNING: rearview camera file not found '%s'\n",
              kCockpitCpp, 0x6e3, CockpitIniPath);
        bOk = false;
    }

    while (Reader.read_header())
    {
        // [RearView] went the same way [ThirdPerson] did: the section is still recognised
        // purely so the loader can complain about it, and not one key is read out of it.
        if (Reader.is_header("RearView"))
        {
            // 0x0051ae1a..: the same unguarded call.
            FDUMP(0x100002, "%s(%d) : *** WARNING: [RearView] is obsolete in file %s\n",
                  kCockpitCpp, 0x6ea, CockpitIniPath);
            continue;
        }

        if (!Reader.is_header("RearViewCamera"))
            continue;

        while (Reader.read_value())
        {
            if (!Reader.is_value("view_position"))
                continue;

            // 0x0051ae77: retail pulls the components in the order 1, 0, 2 -- that is just how
            // it juggles the FPU stack -- and stores them in the order 0, 1, 2. Reading them
            // straight through is equivalent; a missing component yields whatever
            // get_value_float returns for an absent index, exactly as in retail.
            const float X = Reader.get_value_float(0);
            const float Y = Reader.get_value_float(1);
            const float Z = Reader.get_value_float(2);

            Cam->ViewPosition[0] = X;
            Cam->ViewPosition[1] = Y;
            Cam->ViewPosition[2] = Z;
        }
    }

    return bOk;
}

char const* FRearViewCamera::GetName()
{
    return RearViewCameraGetName();
}

void FRearViewCamera::Update(float DeltaSeconds)
{
    RearViewCameraUpdate(this, DeltaSeconds);
}

void FRearViewCamera::Deactivate()
{
    HudCameraDerivedDeactivate(this);
}

void FRearViewCamera::DrawScene(unsigned LayerFlags, unsigned Param2)
{
    SpaceCameraRenderSlot78(this, LayerFlags, Param2);
}
