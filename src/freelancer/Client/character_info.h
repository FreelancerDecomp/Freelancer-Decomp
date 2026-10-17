// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Client\gf\CharacterInfo.cpp (string 0x005caddc) -- the character animation script database.
//
// One object, DAT_00668660 (0x30 bytes, static init LAB_004354b0 -> FUN_004349f0, atexit
// LAB_004354d0 -> FUN_00434b90). It knows every script named in the costume descriptions'
// "[Animations] anim =" files (bodyparts.ini, CostumeDescriptions +0x00) and loads a script into
// one shared IAnimation2 script (+0x2c) the first time somebody acquires it, reference-counted.
// Users hand the returned script to IDeformable::Play -- HUD_Comm's portrait (FUN_004cb250) and
// the Gf character code.
//
// The class has no vtable and no RTTI, so its name is the port's.

#include <list>
#include <map>
#include <string>

class CostumeDescriptions;

// The map value (node +0x10), built by FUN_00434f40.
struct FCharacterScript
{
    std::string Name;         // +0x00 the script (a directory under Animation\Script)
    unsigned Id;              // +0x10 CreateID(Name), the map key again
    int RefCount;             // +0x14 users; the script is loaded while this is non-zero
    std::string const* File;  // +0x18 the .utf the script lives in (an element of ScriptFiles)
};

class FCharacterInfoDb
{
public:
    // FUN_004349f0: three empty containers, AnimScript -1.
    FCharacterInfoDb();
    // FUN_00434b90: the three containers emptied, AnimScript closed (IAnimation2 slot 4) when
    // it is not -1, then the containers themselves.
    ~FCharacterInfoDb();

    // FUN_00434d10 (thiscall, RET 4). Unknown id -> -1. Otherwise, on the first reference the
    // script is added to AnimScript (IAnimation2 slot 30) out of its .utf ("Failed to open"
    // FDUMP, line 0x92, when the file does not open -- the reference is counted anyway).
    // Returns AnimScript.
    void* AcquireScript(unsigned Id);
    // FUN_00434e20 (thiscall, RET 4): a known id loses a reference; at zero the script leaves
    // AnimScript (IAnimation2 slot 31).
    void ReleaseScript(unsigned Id);
    // FUN_00434e90 (thiscall, RET 4): AcquireScript(CreateID(Name)).
    void* AcquireScriptByName(char const* Name);
    // FUN_00434eb0 (thiscall, RET 4): ReleaseScript(CreateID(Name)), inlined.
    void ReleaseScriptByName(char const* Name);
    // FUN_00434f40 (thiscall, RET 8): insert {CreateID(Name) -> {Name, id, 0, File}}; an id that
    // is already there keeps its first entry.
    void AddScript(char const* Name, std::string const* File);
    // FUN_00435100 (thiscall, RET 4): AnimScript = IAnimation2 slot 29 (a new empty script), then
    // every not-yet-seen file of Descs +0x00 is remembered in ScriptFiles and its
    // Animation\Script directories enumerated into AddScript (IAnimation2 slot 32 with the
    // callback LAB_00434f20); "Failed to open" FDUMP (line 0x149) when the file does not open.
    void Load(CostumeDescriptions const* Descs);
    // FUN_00435320 (thiscall, RET 4): a known name is queued for FlushQueuedScripts.
    void QueueScript(char const* Name);
    // FUN_004353a0: the queue in rounds -- per round every queued script of the first entry's
    // file (at most 0x100) takes a reference and leaves the queue, and the ones that were not
    // loaded yet are added to AnimScript in one IAnimation2 slot 30 call ("Failed to open"
    // FDUMP, line 0x1c2).
    void FlushQueuedScripts();

    std::list<std::string> ScriptFiles;           // +0x00 each "anim =" file once
    std::map<unsigned, FCharacterScript> Scripts; // +0x0c by CreateID(name)
    std::list<FCharacterScript*> Queued;          // +0x20 QueueScript's entries
    void* AnimScript;                             // +0x2c IAnimation2 script, -1 = none

private:
    FCharacterInfoDb(FCharacterInfoDb const&) = delete;
    FCharacterInfoDb& operator=(FCharacterInfoDb const&) = delete;
};

// FUN_004356c0: &DAT_00668660.
FCharacterInfoDb* GetCharacterInfoDb();
// FUN_004354e0: Load(GetCostumeDescriptions()). Caller: FUN_00558850 (0x0055885f).
void LoadCharacterInfo();
// FUN_00435500: the three containers emptied and AnimScript closed. Callers: 0x00558870 and
// WinMain's teardown (0x005b2202).
void ShutdownCharacterInfo();
// FUN_004355e0: AcquireScript for every "preload =" name (CostumeDescriptions +0x0c). Caller:
// FUN_00558850's tail (0x00558864).
void PreloadCharacterScripts();
// FUN_00435630: every reference count back to 0 and, with an IAnimation2, AnimScript replaced by
// a new empty script. Callers: FUN_0054aec0 (0x0054aee4) and FUN_00558a50 (0x00558a76).
void ResetCharacterScripts();
