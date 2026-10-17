// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// The UI sound table of the post-DataConfig bootstrap (FUN_00564520 at 0x005b1d58). The flat
// intro-menu shell InitMenuUiSlice that used to be declared here was a port scaffold and is gone.

// FUN_00564520 / FUN_00564570 / FUN_005645c0 / FUN_00564610 / FUN_00564540 — DAT_0067a4a8[0x2d].
void InitUiSoundTable();
int FindUiSoundSlot(const char* Name);
// FUN_00564570 -- FUN_004285f0(Arch, 0, 0): start only if no live source plays this slot.
void PlayUiSoundSlot(unsigned int Slot);
// FUN_00564650 -- the same lazy fill, then FUN_004285f0(Arch, &Key, 0) with a play key whose
// +4 flag byte has bit 0 set (bits 1..2 cleared) and every other field zero. Callers:
// NN_Preferences::SetSliderOption (0x004b1778 / 004b17af / 004b190e / 004b1949 / 004b1967),
// 0x0058b147.
void PlayUiSoundSlotFlag1(unsigned int Slot);
// FUN_005645c0 -- FUN_00428640(Arch, 0, 0): rewind a live source of this slot, else start one.
void PlayOrRewindUiSoundSlot(unsigned int Slot);
// FUN_005646e0 -- FUN_004285c0(DAT_0067a4a8[Slot]): stop every live source of the cached
// archetype (an empty slot is passed through as null, like retail).
void StopUiSoundSlot(unsigned int Slot);
// FUN_00564700 -- "ui_rollover" (slot 0x1d) through FUN_00428680, only while the intro menu is
// the current state (FUN_005b0880) or the local server tick is held (FUN_0042d730).
void PlayUiRolloverSound();
// FUN_00564760 -- true when a live source plays any cached UI archetype (DAT_0067a4a8 up to the
// end sentinel 0x0067a55c, FUN_00428000 on each non-null slot). Caller: the MissionBriefing
// state's update LAB_0043d360 (0x0043d3c6).
bool IsAnyUiSoundPlaying();
const char* GetUiSoundSlotName(unsigned int Slot);
// FUN_00564540 — release every cached UI-sound archetype reference and null the table.
void ShutdownUiSoundTable();
