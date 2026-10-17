// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail Client\cmn\SwappedMaterial.cpp (freelancer.exe 0x0042cc10..0x0042d612): a material
// whose "Dt_name" parameter (the diffuse texture name) is swapped for a texture loaded from a
// UTF file, and put back afterwards. Held by value in NN_MissionVendor (+0x35c, the video
// window's "MV_vidmaterial") and at 0x004a4031 (+ FUN_004a41ea / 004a579c / 004a57d8).
//
// Not polymorphic, 0x14 bytes, no RTTI. The strings are Common's malloc_strcpy copies and are
// freed with malloc_strfree; the texture records are DALib::TextureLib slot 12 lookups and are
// released with slot 14.
struct FSwappedMaterial
{
    // FUN_0042cc10 (thiscall, returns this) -- every field zero.
    FSwappedMaterial();

    // FUN_0042cc50 is also the destructor: NN_MissionVendor's EH state 2 (FUN_0048b510
    // 0x0048b67b) and its ctor's unwind run it on the member.
    ~FSwappedMaterial() { Release(); }

    FSwappedMaterial(FSwappedMaterial const&) = delete;
    FSwappedMaterial& operator=(FSwappedMaterial const&) = delete;

    // FUN_0042cc30 (thiscall) -- every field zero again (the same stores as the ctor).
    void Reset();

    // FUN_0042cc50 (thiscall) -- put the original texture back when swapped, release both
    // texture records, free both strings and zero every field.
    void Release();

    // FUN_0042d4c0 (thiscall, RET 4) -- copy MaterialName, capture the material's current
    // Dt_name (FUN_0042cd00) and clear bSwapped; returns what the capture returned (false when
    // no material in DALib::MaterialLib's group 0 starts with the name).
    bool Init(char const* NewMaterialName);

    // FUN_0042d4f0 (thiscall, RET 8) -- undo an earlier swap, open UtfFile as a "UTF" file
    // system, load TextureName from it and set it as the material's Dt_name. The texture name
    // is the first argument ([ESP+0x18] at 0x0042d561), the file the second ([ESP+0x1c] at
    // 0x0042d544): NN_MissionVendor passes (offer icon texture, "...MissionVendor.txm").
    void Swap(char const* TextureName, char const* UtfFile);

    // FUN_0042cd00 (thiscall) -- find the first material of MaterialLib group 0 whose name
    // starts with MaterialName (_strnicmp over MaterialName's length) and keep its string
    // Dt_name (OriginalTexture, its TextureLib record in OriginalTexRecord). FDUMP when the
    // material has no string Dt_name. False only when no material matches.
    bool CaptureOriginal();

    // FUN_0042d1d0 (thiscall, RET 4) -- set the matching material's string Dt_name parameter
    // to TextureName (slot 19), then apply-verify the material at 1.0 (slot 6). FDUMP when no
    // material matches or it has no string Dt_name.
    void SetDtName(char const* TextureName);

    char* MaterialName;        // +0x00 malloc_strcpy
    char* OriginalTexture;     // +0x04 malloc_strcpy of the captured Dt_name
    void* OriginalTexRecord;   // +0x08 TextureLib slot 12 record of OriginalTexture
    void* SwapTexRecord;       // +0x0c TextureLib slot 12 record of the swapped-in texture
    unsigned char bSwapped;    // +0x10
    unsigned char Pad11[3];
};

static_assert(sizeof(FSwappedMaterial) == 0x14, "SwappedMaterial is 0x14 bytes (NN_MissionVendor +0x35c..+0x370)");
