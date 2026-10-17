// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Ship ICliObj camera block: the ini value handler at 0x0040763f .. 0x00407816.
//
// Verified against the listing key by key. Two things are easy to get wrong and are reproduced
// exactly: camera_angular_acceleration lands at +0x70 while camera_angular_slerp_multiplier --
// which retail tests FIRST -- lands at +0x74, and the three turn angles are clamped before the
// degrees-to-radians conversion, not after.

#include "ship_camera_params.h"

#include "common_ini.h"

namespace
{
    // .rdata 0x005c7474, 0x005c77f0, 0x005c77f4.
    const float kMinTurnAngleDegrees = 0.0f;
    const float kMaxTurnAngleDegrees = 90.0f;
    const float kDegreesToRadians = 0.01745329238474369f;
}

float ShipCameraClampTurnAngle(float Degrees)
{
    // FCOM 0.0 / TEST AH,0x5 / JP, then FCOM 90.0 / TEST AH,0x41 / JNZ.
    //
    // TEST AH,0x5 looks at C0 (0x01) and C2 (0x04), and JP is taken on EVEN parity:
    //   value <  0   -> C0=1, C2=0 -> one bit  -> odd  -> no jump -> clamped to 0
    //   value >= 0   -> C0=0, C2=0 -> no bits  -> even -> jump    -> upper-bound test
    //   unordered    -> C0=1, C2=1 -> two bits -> even -> jump    -> upper-bound test
    // and the upper-bound test (TEST AH,0x41 over C0 and C3) is also non-zero for unordered,
    // so retail KEEPS a NaN and multiplies it by the conversion factor: a NaN turn angle in the
    // ini stays NaN all the way into the camera.
    //
    // Match that table: only a *strictly negative* finite value clamps to 0. NaN is not `< 0`,
    // so it falls through; the upper test uses `>` so NaN also skips the 90 clamp.
    if (Degrees < kMinTurnAngleDegrees)
        return kMinTurnAngleDegrees * kDegreesToRadians;
    if (Degrees > kMaxTurnAngleDegrees)
        return kMaxTurnAngleDegrees * kDegreesToRadians;
    return Degrees * kDegreesToRadians;
}

void ShipCameraParamsClear(FShipCameraParams* Params)
{
    Params->Offset[0] = 0.0f;
    Params->Offset[1] = 0.0f;
    Params->AngularAcceleration = 0.0f;
    Params->AngularSlerpMultiplier = 0.0f;
    Params->HorizontalTurnAngle = 0.0f;
    Params->VerticalTurnUpAngle = 0.0f;
    Params->VerticalTurnDownAngle = 0.0f;
    Params->TurnLookAheadSlerpAmount = 0.0f;
}

bool ShipCameraParamsReadValue(FShipCameraParams* Params, INI_Reader* Reader)
{
    // 0x0040763f: camera_offset takes TWO parameters off the same line.
    if (Reader->is_value("camera_offset"))
    {
        Params->Offset[0] = Reader->get_value_float(0);
        Params->Offset[1] = Reader->get_value_float(1);
        return true;
    }

    // 0x0040766f. Retail tests the slerp multiplier BEFORE the acceleration even though the
    // acceleration sits at the lower offset; order matters only if a line could match both,
    // which it cannot, but the order is kept so the listing reads straight across.
    if (Reader->is_value("camera_angular_slerp_multiplier"))
    {
        Params->AngularSlerpMultiplier = Reader->get_value_float(0);   // +0x74
        return true;
    }

    // 0x00407692.
    if (Reader->is_value("camera_angular_acceleration"))
    {
        Params->AngularAcceleration = Reader->get_value_float(0);      // +0x70
        return true;
    }

    // 0x004076b5 / 0x0040771c / 0x00407783 -- all three share the clamp-then-convert shape.
    if (Reader->is_value("camera_horizontal_turn_angle"))
    {
        Params->HorizontalTurnAngle = ShipCameraClampTurnAngle(Reader->get_value_float(0));
        return true;
    }
    if (Reader->is_value("camera_vertical_turn_up_angle"))
    {
        Params->VerticalTurnUpAngle = ShipCameraClampTurnAngle(Reader->get_value_float(0));
        return true;
    }
    if (Reader->is_value("camera_vertical_turn_down_angle"))
    {
        Params->VerticalTurnDownAngle = ShipCameraClampTurnAngle(Reader->get_value_float(0));
        return true;
    }

    // 0x004077f0 -- stored raw, no clamp.
    if (Reader->is_value("camera_turn_look_ahead_slerp_amount"))
    {
        Params->TurnLookAheadSlerpAmount = Reader->get_value_float(0);
        return true;
    }

    return false;
}
