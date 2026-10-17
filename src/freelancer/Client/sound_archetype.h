// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Archetype::ISound -- the client sound-archetype interface (freelancer.exe only).
//
// RTTI: ".?AUISound@Archetype@@" (type descriptor 0x006109b0). Two implementations:
//   Archetype::SoundImpl (TD 0x006109d0), vtable 0x005ca36c -- the [Sound] archetypes of
//     SoundArch.cpp, port FClientSoundArch in menu_sound_arch.cpp;
//   VoiceMsg : Archetype::IVoiceMsg : Archetype::ISound (TDs 0x00610a70 / 0x00610a4c),
//     vtable 0x005cabfc -- one line of a voice profile, port FVoiceMsg in voice_profiles.h.
// Both tables are 15 slots. The sound heap (FUN_00428680 and Sound::Source Start FUN_004291c0)
// only ever talks to an archetype through this table, which is how HUD_Comm can hand a VoiceMsg
// straight to FUN_00428680 (0x004cb188, 0x004cc01b).
//
// Slot names come from what both implementations return; the calling convention is the plain
// member-function one (retail ECX = this, callee pops the stack arguments -- there are none).

#include <list>
#include <string>

namespace Archetype
{
    struct ISound
    {
        // 0  SoundImpl FUN_0042bee0 (the lod-linked archetype's SoundManager id), VoiceMsg
        //    LAB_0042adc0 (MOV EAX,[ECX+8]).
        virtual int GetSmArchId() = 0;
        // 1  SoundImpl LAB_0042adc0 (+8 file), VoiceMsg LAB_00431200 (profile slot 1 on its id).
        virtual char const* GetFileName() = 0;
        // 2  SoundImpl LAB_0042add0 (type nibble of the flags), VoiceMsg LAB_00517320 (2).
        virtual unsigned GetSourceType() = 0;
        // 3  SoundImpl FUN_0042b840, VoiceMsg FUN_00431220 -- create the wav on first use and
        //    count the reference; SETNZ of "has a SoundManager id".
        virtual unsigned char Acquire() = 0;
        // 4  SoundImpl FUN_0042b8a0, VoiceMsg FUN_00431260 -- both return the remaining count
        //    sign-extended from its word (MOVSX).
        virtual int Release() = 0;
        // 5  SoundImpl FUN_0042b910, VoiceMsg LAB_004312a0 -- the reference count.
        virtual int GetUseCount() = 0;
        // 6  SoundImpl LAB_0042ade0, VoiceMsg LAB_004312b0 -- attenuation in dB (signed byte).
        virtual int GetAttenuation() = 0;
        // 7  SoundImpl FUN_0042adf0, VoiceMsg LAB_004312c0 (-1.0f).
        virtual float GetRangeMin() = 0;
        // 8  SoundImpl LAB_0042ae00, VoiceMsg LAB_004312c0 (-1.0f).
        virtual float GetRangeMax() = 0;
        // 9  SoundImpl LAB_0042ae10 (flag bit 26), VoiceMsg FUN_0044fd00 (false).
        virtual unsigned char IsStreamer() = 0;
        // 10 SoundImpl LAB_0042ae20 (flag bit 27), VoiceMsg FUN_004b6c50 (true).
        virtual unsigned char Is2d() = 0;
        // 11 SoundImpl LAB_0042bf10 (random pitch), VoiceMsg LAB_004312d0 (0.0f).
        virtual float GetRandomPitch() = 0;
        // 12 SoundImpl FUN_0042bf70 (resolve the lod link), VoiceMsg FUN_0043d950 (this).
        virtual ISound* GetLinked() = 0;
        // 13 SoundImpl LAB_0042ae30 (flag bit 24), VoiceMsg FUN_0044fd00 (false).
        virtual unsigned char IsPitchBendable() = 0;
        // 14 SoundImpl LAB_004b5010 (0), VoiceMsg LAB_00431210 (MOVSX [+0x10], `priority =`).
        virtual int GetPriority() = 0;
    };

    // RTTI ".?AUIVoiceMsg@Archetype@@": no slots of its own beyond ISound's 15.
    struct IVoiceMsg : ISound
    {
    };

    // RTTI ".?AUIVoice@Archetype@@": the voice-profile interface, vtable 0x005cad10 -- eight
    // slots, every one _purecall (LAB_005b8064), stored into VoiceProfile objects at 0x00430ba0 /
    // 0x00431db0 / 0x00433bfa (the inlined base-class vptr stores). Its one implementation is
    // VoiceProfile (vtable 0x005cacec, port FVoiceProfile in voice_profiles.h). There is no
    // virtual destructor slot. Plain member-function convention (ECX = this, callee pops).
    struct IVoice
    {
        // 0  LAB_00431410 -- `file =` of one message (ignored once the profile has a .utf).
        virtual void SetSoundFile(unsigned int MsgId, char const* File) = 0;
        // 1  LAB_004314e0 -- the message's sound file ("0x%X" of the id inside a .utf profile).
        virtual char const* GetSoundFile(unsigned int MsgId) = 0;
        // 2  LAB_00431590 -- the profile's .utf as an IFileSystem, null without one.
        virtual void* OpenScriptFileSystem() = 0;
        // 3  LAB_004313f0 -- the profile id.
        virtual unsigned int GetId() const = 0;
        // 4  LAB_00431400 -- ID_String::get_string of the id.
        virtual char const* GetName() const = 0;
        // 5  LAB_00431630 -- the message by id, null when absent.
        virtual IVoiceMsg* FindMessage(unsigned int MsgId) = 0;
        // 6  LAB_00431680 -- the `script =` list.
        virtual std::list<std::string>& GetScripts() = 0;
        // 7  LAB_00431580 -- MOV EAX,"" (DAT_005c865c); RET 4: one stack argument, never read.
        virtual char const* GetUnusedString(void* Unused) const = 0;
    };
}
