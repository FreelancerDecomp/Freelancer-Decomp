// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include <cstdint>

// Retail tests the movement-state byte of IVP_Real_Object::flags (+0x78) and
// IVP_Core::MovementState (+0x60) as `SHL reg,0x18; CMP reg,imm; Jcc` (signed),
// i.e. the signed low byte of the dword against imm >> 24. This is that byte.
//
// Do not spell it `static_cast<int32_t>(Value << 24) < 0x08000000`: MSVC 14.51
// (v145) folds that into a signed compare of the whole dword
// (`CMP dword ptr [flags],8`), dropping the shift, so any value with bits set
// above the low byte is misread -- a moving body's flags 0x101 compare as
// 257 >= 8, "not moving". See IVP_Sweep_Frame_Cache::Init.
inline int32_t IVP_SignedLowByte(uint32_t Value)
{
    return static_cast<int8_t>(static_cast<uint8_t>(Value & 0xffu));
}
