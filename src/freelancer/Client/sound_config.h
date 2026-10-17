// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Client audio configuration + Gf master fade (E:\FL\Scratch\Source\Client\cmn\Sound.cpp
// and Gf\ScriptSound-family helpers).
//
// FUN_00427a70  — parse `audio\SoundCFG.ini` `[CFG]` (called at the tail of the client's
//                 freelancer.ini pass, FUN_005b0c30 @ 005b0fc4).
// FUN_00428be0  — ducking triple: down_by / down_time (RTC vs spaceflight) + music_fade_time.
// FUN_00428030  — audio mode selector (DAT_00667cac; 3 = RTC).
// FUN_004569b0/d0/a40, FUN_00456990 — the Gf audio fade object at DAT_0066d2d0
//                 (dB attenuation animated toward a target; FUN_00456650 ticks it).

// FUN_00427a70. Defaults stay in place for keys the file omits.
void LoadClientSoundCfgIni(char const* FullPath);

// FUN_00428030 / DAT_00667cac. Mode 3 selects the RTC ducking pair.
void SetClientAudioMode(int Mode);
int GetClientAudioMode();

// FUN_00428be0 — any out pointer may be null.
void GetClientDuckingConfig(float* OutDownBy, float* OutDownTime, float* OutMusicFadeTime);

// DAT_006108d0 / DAT_006108d4 — used by the [Sound] background writers FUN_004288e0 /
// FUN_00428a40 when the caller passes a negative fade.
float GetClientMusicFadeTime();
float GetClientCrossFadeSilence();
// DAT_00667c9c — `COCKPIT_ATTENUATION`; no consumer ported yet.
float GetClientCockpitAttenuation();
// DAT_006108e4 / DAT_006108e8 — `ducking_spaceflight_down_by` / `_down_time`, read directly
// (whatever the audio mode) by the first push of FUN_00428db0.
float GetClientSpaceflightDuckDownBy();
float GetClientSpaceflightDuckDownTime();
// DAT_006108ec — `ducking_spaceflight_up_time`, used by FUN_00428de0 on the last pop.
float GetClientSpaceflightDuckUpTime();
// DAT_006108f0 / DAT_006108f4 / DAT_006108f8 -- `ducking_comm_down_by` / `_down_time` /
// `_up_time`. HUD_Comm's ducking (hud_comm.cpp FCommVoice::Play and every release path) reads
// them directly.
float GetClientCommDuckDownBy();
float GetClientCommDuckDownTime();
float GetClientCommDuckUpTime();
// DAT_006108fc / DAT_00610900 -- `spaceflight_dialogue_pan_range` / `_pan_factor`, read by the
// comm pan FUN_004cc060 (hud_comm.cpp FCommVoice::Pan).
float GetClientDialoguePanRange();
float GetClientDialoguePanFactor();

// Gf audio fade (DAT_0066d2d0). Value is in dB: -96 = silent, 0 = full.
// FUN_004569b0 — snap current+target, clear rate, raise the dirty flag.
void SnapClientAudioFade(float Value);
// FUN_004569d0 — target + seconds; seconds <= 0 falls back to the ducking down_time,
// and a zero-length fade stores the retail 1000000.0f rate.
void SetClientAudioFade(float Target, float Seconds);
// FUN_00456a40 — advance by DeltaSeconds, returns true when the value changed
// (retail then re-applies volume on every Gf script sound).
bool TickClientAudioFade(float DeltaSeconds);
// The Gf script sequence's direct writes of target (+0x18) and rate (+0x1c): FUN_00455290
// (0x004553ba / 0x004553c9) and FUN_004554c0 (0x00455616 / 0x0045561f) store a fixed rate
// instead of FUN_004569d0's seconds.
void SetClientAudioFadeTargetRate(float Target, float Rate);
float GetClientAudioFade();
