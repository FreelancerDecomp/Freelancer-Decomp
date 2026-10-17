// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// NN voice queue -- E:\FL\Scratch\Source\Client\nn\NN_Messaging.cpp (freelancer.exe
// 0x00489a50..0x00489f70). The same retail file's NeuroNetMissionMessage pop-up
// (0x00489fe0..0x0048b280) is nn_mission_message.h / .cpp.
//
// The NeuroNet voice lines ("insufficient credits", "incoming missile", "scanning cargo", ...)
// all go through one short FIFO: the VC6 std::list at DAT_00671f50 (head DAT_00671f54, size
// DAT_00671f58), capacity 4. Each entry is {message id, flag byte, playing ISoundSource*}. The
// head entry plays through the NNVoice profile (DAT_00671f5c = CreateID("NNVoice"),
// DAT_00671f70 = its FVoiceProfile); the per-frame tick drops the head when its source has
// finished and starts the next one.
//
// Callers of NnVoiceEnqueue (FUN_00489e80, 21 sites) and NnVoiceEnqueueByName (FUN_00489ea0) are
// spread over the NeuroNet screens and the HUD. NnVoiceInit is FUN_0045a100's tail
// (NnSystemInit, nn_modal.cpp) and NnVoiceTick is called from NN_MainButton's update
// (LAB_0045c8c0, 0x0045c957; nn_main_button.cpp).

// FUN_00489de0 -- DAT_00671f5c = CreateID("NNVoice"), DAT_00671f70 = FUN_00430ab0 of it.
void NnVoiceInit();

// FUN_00489e00 (cdecl; the float frame time it is passed is never read) -- drop the head once
// its source has finished, then start the next one.
void NnVoiceTick(float Unused);

// FUN_00489e80 -- FUN_00489b50 on DAT_00671f50: queue MsgId (ignored when already queued, FDUMP
// "NN Message inbox overflow!" at 4); the first entry starts at once. bKeyBit2 lands in bit 2 of
// the play key's flag byte (FUN_00489a80).
void NnVoiceEnqueue(unsigned MsgId, unsigned char bKeyBit2);

// FUN_00489ea0 -- NnVoiceEnqueue(CreateID(Name), 0).
void NnVoiceEnqueueByName(char const* Name);
