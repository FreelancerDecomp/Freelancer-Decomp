// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// The camera block inside a ship archetype's client companion (Archetype::Root + 0x04, the
// ICliObj). Written by the ship ini value handler at 0x0040763f .. 0x00407816, and read back
// by the third-person camera's attach (FUN_00518af0 at 0x00518b75).
//
// These are the only ICliObj fields the port needs so far, so the struct covers just them and
// records their retail offsets rather than pretending to be the whole companion.

class INI_Reader;

// Field offsets inside the ship ICliObj, for the record:
//   +0x5c  bay_doors_close_snd   (CreateID)      -- not part of this block
//   +0x60  cockpit               (StringAlloc)   -- not part of this block
//   +0x64  pilot_mesh            (CreateID)      -- not part of this block
//   +0x68  camera_offset[0]
//   +0x6c  camera_offset[1]
//   +0x70  camera_angular_acceleration
//   +0x74  camera_angular_slerp_multiplier
//   +0x78  camera_horizontal_turn_angle          (clamped, radians)
//   +0x7c  camera_vertical_turn_up_angle         (clamped, radians)
//   +0x80  camera_vertical_turn_down_angle       (clamped, radians)
//   +0x84  camera_turn_look_ahead_slerp_amount
struct FShipCameraParams
{
    float Offset[2];                    // +0x68 / +0x6c  camera_offset
    float AngularAcceleration;          // +0x70
    float AngularSlerpMultiplier;       // +0x74
    float HorizontalTurnAngle;          // +0x78  radians
    float VerticalTurnUpAngle;          // +0x7c  radians
    float VerticalTurnDownAngle;        // +0x80  radians
    float TurnLookAheadSlerpAmount;     // +0x84
};

// The three turn angles are clamped to [0, 90] DEGREES and then converted to radians
// (DAT_005c7474 = 0.0, DAT_005c77f0 = 90.0, DAT_005c77f4 = 0.01745329238474369f).
float ShipCameraClampTurnAngle(float Degrees);

// One step of the ship ini value handler, covering only the camera keys. Returns true when the
// current value was one of them. Retail tests them in this order, each with its own early
// return, inside a much longer is_value chain.
bool ShipCameraParamsReadValue(FShipCameraParams* Params, INI_Reader* Reader);

// The state a fresh companion starts in. Retail zero-fills the companion on allocation and
// never assigns camera defaults, so an ini with no camera keys leaves every field at zero --
// including the offset, which is why every shipped ship entry carries camera_offset.
void ShipCameraParamsClear(FShipCameraParams* Params);
