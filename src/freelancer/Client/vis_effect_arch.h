// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "beam_effect.h"

#include <windows.h>

#include <string>
#include <vector>

// Client EffectArch / VisEffect / BeamSpear-Bolt / EffectType / EffectLOD — FUN_0040eac0.

namespace Archetype
{
    struct FCliMaterialEntry;
}

// Retail VisEffect (Client\cmn\VisEffectArch.cpp), 0x38 bytes, held by value in the map
// DAT_00667df0. Default state from the map's node build (FUN_0042eaa0, 0x0042eadf..0x0042eb07):
// both vectors empty, has_alpha 0, RefCount 0, AleHandle -1. Retail offsets in the comments;
// the port's layout is its own (no VC6 ABI), and the ale name is held in a buffer rather than
// as a StringAlloc'd CacheString.
struct FVisEffectArch
{
    // +0x00  CacheString: the StringAlloc'd nickname FUN_0042dc40 stores (0x0042dca4); the
    // node build clears it first (CacheString::clear, the port's "").
    char const* Nickname = "";
    char AlchemyPath[MAX_PATH];      // +0x08  "alchemy", StringAlloc'd
    unsigned long EffectCrc = 0;     // +0x04  "effect_crc" bit pattern (may be negative in source)
    std::vector<std::string> Textures; // +0x0c..+0x18  "textures", one per key
    bool bHasAlpha = false;          // +0x1c  bit 0, "has_alpha"
    int RefCount = 0;                // +0x20
    int AleHandle = -1;              // +0x24  FUN_00401160 handle, -1 = not loaded
    std::vector<Archetype::FCliMaterialEntry*> TextureSlots; // +0x28..+0x34  FUN_0041aa50 entries

    FVisEffectArch() = default;
    // FUN_0042edb0 (the map node pair's destructor, VisEffect at pair + 4): a VisEffect still
    // referenced gives back its texture slots and its .ale use first. Runs from the map's
    // teardown (atexit LAB_0042d920 in retail), so the material cache (client_cliobj.cpp) and
    // the ALE registry must still be alive then -- see the note on G_VisEffects.
    ~FVisEffectArch();
    // Only ever built in place in G_VisEffects; a copy would release the same slots twice.
    FVisEffectArch(FVisEffectArch const&) = delete;
    FVisEffectArch& operator=(FVisEffectArch const&) = delete;
};

// FUN_0042e080 (thiscall) -- VisEffectArch AddRef. On the first reference (RefCount <= 0) the
// texture libraries are (re)resolved when their count differs from the texture names, and the
// .ale is loaded through FUN_00401160. The count goes up on every path. True only when this call
// loaded the .ale successfully.
bool VisEffectArchAddRef(FVisEffectArch* Vis);
// FUN_0042e160 (thiscall) -- VisEffectArch Release. At zero: release every texture slot
// (FUN_0041b030) and the .ale (FUN_00401300), handle back to -1; true only then.
bool VisEffectArchRelease(FVisEffectArch* Vis);

// Client\cmn\ALELib.cpp: .ale files by name (map DAT_00616440, compared with stricmp), each
// with a use count and the handle handed out from DAT_006101d0 (starts at 1).
// FUN_00401160 -- empty name -> -1; known name -> ++count, its handle; otherwise, with
// DALib::Alchemy present, open the file through DALib::Data, Alchemy LoadNodeLibrary +
// FxEffectLibrary LoadAle, and register it with count 1 and a fresh handle. -1 on failure.
int AleLibLoad(char const* AleName);
// FUN_00401300 -- -1 for handle -1 or an unknown handle; otherwise --count (an ERROR FDUMP
// when it drops below zero) and the new count. Nothing is unloaded at zero.
int AleLibRelease(int Handle);
// FUN_00401380 (ALELib::Shutdown, client teardown 0x005b22fb) -- a WARNING FDUMP for every
// name whose count is still positive, then the registry emptied. Nothing is unloaded from
// Alchemy. Returns how many names were still in use.
int AleLibShutdown();

// Retail Effect record (EffectArch.cpp) — operator_new(0x58) in FUN_0040eac0.
struct FEffectArch
{
    int EffectType;                      // +0x00  EFT_* index
    unsigned NicknameId;                 // +0x04  CreateID
    FVisEffectArch* VisEffect;           // +0x08
    char const* VisEffectName;           // +0x0C  CacheString: the found VisEffect's first field
    FVisEffectArch* VisGeneric;          // +0x10
    FBeamEffectArch* VisBeam;            // +0x14
    void* SndEffect;                     // +0x18  SoundArch* (LookupClientSoundArch / FUN_0042ae40)
    unsigned LgtEffectLen;               // +0x1C
    char LgtEffect[0x20];                // +0x20
    float LgtRangeScale;                 // +0x40
    float LgtTimeScale;                  // +0x44
    float LgtRadius;                     // +0x48
    unsigned char LgtColorR;             // +0x4C
    unsigned char LgtColorG;             // +0x4D
    unsigned char LgtColorB;             // +0x4E
    unsigned char LgtColorA;             // +0x4F
    unsigned char Flags50;               // +0x50  bit0 from EffectType.RunTime<0
    unsigned char Pad51[3];
    int RefCount;                        // +0x54
};
static_assert(sizeof(FEffectArch) == 0x58, "EffectArch 0x58");

// FUN_0040eac0 (cdecl Path, bReload; caller the [Data] effects = key, 0x005b0dd6 with 0) --
// binds the FxRuntime once (FUN_0042e290), then reads [Effect] (new EffectArch into the
// DAT_006164b0 registry; a repeated nickname is merged through FUN_0040e6e0 when bReload,
// else warned about and dropped), [EffectLOD] (FUN_0042dae0), [EffectType] (FUN_004fd5a0),
// [VisEffect] (FUN_0042dc40) and [BeamSpear] / [BeamBolt] (FUN_0042e850). Returns how many
// EffectArchs the registry gained.
int EffectArchLoadIni(char const* Path, bool bReload);

// FUN_0040e250 (thiscall) -- EffectArch's constructor (also inlined in FUN_0040eac0): only
// the fields below are written; +0x21..+0x3f stay as they were.
void EffectArchConstruct(FEffectArch* Arch);
// FUN_0040e2a0 (thiscall) -- EffectArch's destructor (inlined in FUN_0040eac0 and
// FUN_0040ef30): with references still out, a warning, then every one of them dropped.
void EffectArchDestruct(FEffectArch* Arch);
// FUN_0040ef30 (client teardown 0x005b22aa) -- every registered EffectArch destroyed and
// freed, the registry emptied, then FUN_0042e2f0 unbinds the FxRuntime.
void EffectArchShutdown();

FVisEffectArch* FindVisEffectByNickname(char const* Nickname);
FBeamEffectArch* FindBeamEffectArchByNickname(char const* Nickname);
// FUN_0040f080 (cdecl) -- the EffectArch registered under *NicknameId (retail std::map
// DAT_006164b0 lower_bound + key compare), null when there is none. Retail takes the key by
// pointer; every caller passes the address of a CreateID value.
FEffectArch* FindEffectArch(unsigned const* NicknameId);
FEffectArch* FindEffectArchByNickname(char const* Nickname);

// FUN_0040e960 (thiscall, RET 4; the stack argument is never read) -- EffectArch::AddRef:
// VisEffect +0x08 through FUN_0042e080, SoundArch +0x18 through its slot 3 (+0x0c), then
// ++RefCount (+0x54).
void EffectArchAddRef(FEffectArch* Arch, unsigned UnusedArg);
// FUN_0040e990 (thiscall) -- EffectArch::Release: VisEffect through FUN_0042e160, SoundArch
// through slot 4 (+0x10), then --RefCount.
void EffectArchRelease(FEffectArch* Arch);
