// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Client voice archetypes (E:\FL\Scratch\Source\Client\cmn\Voice.cpp).
//
// RTTI names from freelancer.exe.txt: Archetype::IVoice (pure base, vtable
// 005cad10) implemented by VoiceProfile (vtable 005cacec); Archetype::IVoiceMsg
// implemented by VoiceMsg (vtable 005cabfc).
//
// FUN_00430b00 loads `voices =` files listed in freelancer.ini [Data];
// FUN_00430ab0 resolves a profile by CreateID(nickname); FUN_00431850 clears
// the registry on engine shutdown. Retail keeps the registry in the VC6
// std::map at DAT_00667e18 (head DAT_00667e1c, size DAT_00667e28) — the port
// uses std::map, layout is deliberately not retail-compatible.

#include <list>
#include <map>
#include <string>

#include "common_id.h"
#include "sound_archetype.h"

struct FVoiceProfile;

// VoiceMsg — retail object 0x14 bytes, vtable 005cabfc (the 15 slots of Archetype::ISound, via
// Archetype::IVoiceMsg; RTTI in sound_archetype.h). It IS a sound archetype: HUD_Comm hands it
// to the sound heap FUN_00428680 directly (0x004cb188 / 0x004cc01b).
// FUN_004311d0 is the default ctor; FUN_00431690 copies +4..+0x13.
struct FVoiceMsg : Archetype::IVoiceMsg
{
    ID_String Id;            // +0x04 CreateID(msg)
    int SmArchId;            // +0x08 ISoundManager archetype, -1 when not created
    FVoiceProfile* Owner;    // +0x0C
    signed char Priority;    // +0x10 `Priority =`
    signed char Attenuation; // +0x11 `attenuation =`
    short RefCount;          // +0x12

    FVoiceMsg();

    // slot 0 LAB_0042adc0 — MOV EAX,[ECX+8].
    int GetSmArchId() override { return SmArchId; }
    // slot 1 LAB_00431200 — Owner->GetSoundFile(Id).
    char const* GetFileName() override;
    // slot 2 LAB_00517320 — MOV EAX,2.
    unsigned GetSourceType() override { return 2; }
    // slot 3 FUN_00431220 — create the wav archetype on first use, ++RefCount; SETNZ (id != -1).
    unsigned char Acquire() override;
    // slot 4 FUN_00431260 — --RefCount, destroy archetype at 0; returns MOVSX of the count.
    int Release() override;
    // slot 5 LAB_004312a0 — MOVSX [+0x12].
    int GetUseCount() override { return RefCount; }
    // slot 6 LAB_004312b0 — MOVSX [+0x11].
    int GetAttenuation() override { return Attenuation; }
    // slots 7/8 LAB_004312c0 — FLD -1.0f (DAT_005c7500).
    float GetRangeMin() override { return -1.f; }
    float GetRangeMax() override { return -1.f; }
    // slot 9 FUN_0044fd00 — XOR AL,AL (not streamed).
    unsigned char IsStreamer() override { return 0; }
    // slot 10 FUN_004b6c50 — MOV AL,1 (2D).
    unsigned char Is2d() override { return 1; }
    // slot 11 LAB_004312d0 — FLD 0.0f (DAT_005c7474).
    float GetRandomPitch() override { return 0.f; }
    // slot 12 FUN_0043d950 — MOV EAX,ECX.
    Archetype::ISound* GetLinked() override { return this; }
    // slot 13 FUN_0044fd00 — XOR AL,AL.
    unsigned char IsPitchBendable() override { return 0; }
    // slot 14 LAB_00431210 — MOVSX [+0x10].
    int GetPriority() override { return Priority; }
};

// VoiceProfile — retail object 0x50 bytes, vtable 005cacec (the 8 slots of Archetype::IVoice,
// sound_archetype.h). Retail callers reach every method through that table (e.g. VoiceMsg
// slot 1 LAB_00431200 calls the owner's slot 1, slot 3 FUN_00431220 its slot 2).
struct FVoiceProfile : Archetype::IVoice
{
    ID_String Id;                                  // +0x04 CreateID(nickname)
    char* Name;                                    // +0x08 StringAlloc(nickname)
    std::map<unsigned int, FVoiceMsg> Messages;    // +0x0C
    std::list<std::string> Scripts;                // +0x20 `script =`
    std::string UtfPath;                           // +0x2C DATA\AUDIO\<nickname>.utf
    std::map<unsigned int, std::string> SoundFiles; // +0x3C `file =` per msg

    FVoiceProfile();

    // slot 0 LAB_00431410 — ignored while UtfPath is set.
    void SetSoundFile(unsigned int MsgId, char const* File) override;
    // slot 1 LAB_004314e0 — "0x%X" of the msg id when the profile has a .utf.
    char const* GetSoundFile(unsigned int MsgId) override;
    // slot 2 LAB_00431590 — DALib::Data CreateInstance("FileSystem"/"UTF", UtfPath).
    void* OpenScriptFileSystem() override;
    // slot 3 LAB_004313f0.
    unsigned int GetId() const override { return Id.Id; }
    // slot 4 LAB_00431400 — ID_String::get_string, i.e. the hex id, not Name.
    char const* GetName() const override { return Id.get_string(); }
    // slot 5 LAB_00431630.
    FVoiceMsg* FindMessage(unsigned int MsgId) override;
    // slot 6 LAB_00431680 — &Scripts.
    std::list<std::string>& GetScripts() override { return Scripts; }
    // slot 7 LAB_00431580 — always ""; RET 4, the one argument is not read.
    char const* GetUnusedString(void* /*Unused*/) const override { return ""; }

    // FUN_004312e0 — resolve DATA\AUDIO\<Name>.utf into UtfPath when it exists.
    void ResolveScriptFile();
};

// FUN_00430b00 — parse one `voices =` file. Returns the number of profiles added.
int LoadClientVoicesIni(char const* FullPath);

// FUN_00430ab0 — profile by CreateID(nickname), null when unknown.
FVoiceProfile* LookupClientVoice(unsigned int VoiceId);

// FUN_00431850 — drop every profile (engine shutdown, FUN_005b22af).
void ClearClientVoices();

// Registry size (retail DAT_00667e28).
unsigned int GetClientVoiceCount();

// FUN_00445720 (the six `$BGCS_*` context tokens) is FCharacterObject::ResolveBgcsContextToken
// (gf_character_object.h), called on the speaking character.
