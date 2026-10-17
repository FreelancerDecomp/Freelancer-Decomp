// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "character_info.h"

#include "DALib.h"              // DALib::Anim
#include "common_id.h"          // CreateID
#include "common_reputation.h"  // CostumeDescriptions, GetCostumeDescriptions
#include "common_misc.h"        // DAEngine_create_filesystem / DAEngine_release_filesystem
#include "dacom.h"              // FDUMP
#include "engine/engbase.h"     // IAnimation2
#include "readfile.h"           // IFileSystem

#include <cstdint>
#include <string.h>

namespace
{
    char const kCharacterInfoCpp[] = "E:\\FL\\Scratch\\Source\\Client\\gf\\CharacterInfo.cpp"; // 0x005caddc
    char const kFailedToOpen[] = "%s(%d) : *** WARNING: Failed to open %s\n";                 // 0x005cadb0

    // The "no script" value of +0x2c (the ctor's MOV [EDI+0x2c],-1 and every CMP ...,-1).
    void* NoAnimScript()
    {
        return reinterpret_cast<void*>(static_cast<intptr_t>(-1));
    }

    // FUN_00435100's stack record for the enumeration callback: 0x00435261..0x00435274 stores
    // the database at +0 and the remembered file name at +4.
    struct FScriptEnumContext
    {
        FCharacterInfoDb* Db;
        std::string const* File;
    };

    // LAB_00434f20 (cdecl): one Animation\Script directory -> AddScript(Name, Ctx->File).
    void __cdecl AddEnumeratedScript(char const* Name, void* Context)
    {
        FScriptEnumContext* const Ctx = static_cast<FScriptEnumContext*>(Context);
        Ctx->Db->AddScript(Name, Ctx->File);
    }

    // DAT_00668660.
    FCharacterInfoDb G_CharacterInfoDb;
}

FCharacterInfoDb::FCharacterInfoDb()
    : AnimScript(NoAnimScript())
{
    // FUN_004349f0: the list sentinel (0x18-byte node), the map head (0x30-byte node, colour
    // black, isnil), the second list sentinel (0xc-byte node), +0x2c = -1.
}

FCharacterInfoDb::~FCharacterInfoDb()
{
    // FUN_00434b90: 0x00434bb2..0x00434bc8 ScriptFiles erased, 0x00434bcd..0x00434bea the map,
    // 0x00434bef..0x00434c0c the queue; then +0x2c through IAnimation2 slot 4 (no test of
    // DALib::Anim, as retail) and back to -1. The containers' own destructors follow.
    ScriptFiles.clear();
    Scripts.clear();
    Queued.clear();
    if (AnimScript != NoAnimScript())
    {
        DALib::Anim->CloseScript(AnimScript);
        AnimScript = NoAnimScript();
    }
}

void* FCharacterInfoDb::AcquireScript(unsigned Id)
{
    // FUN_00434d10. 0x00434d2c..0x00434d49: FUN_00432240 lookup; no entry -> -1.
    std::map<unsigned, FCharacterScript>::iterator const It = Scripts.find(Id);
    if (It == Scripts.end())
    {
        return NoAnimScript();
    }
    FCharacterScript& Script = It->second;
    if (Script.RefCount == 0)
    {
        // 0x00434d58..0x00434d7a: DAEngine_create_filesystem(file, &fs, 0).
        char const* const Path = Script.File->c_str();
        IFileSystem* File = nullptr;
        DAEngine_create_filesystem(Path, &File, nullptr);
        if (File != nullptr)
        {
            // 0x00434d8b..0x00434dc0: IAnimation2 slot 30 (+0x78)(AnimScript, fs, &name, 1),
            // then the file system released.
            char const* Names[1];
            Names[0] = Script.Name.c_str();
            DALib::Anim->AddScriptNames(AnimScript, File, Names, 1);
            DAEngine_release_filesystem(&File);
        }
        else
        {
            // 0x00434de0..0x00434dfd.
            FDUMP(0x100002, kFailedToOpen, kCharacterInfoCpp, 0x92, Path);
        }
    }
    // 0x00434dc6..0x00434dce / 0x00434e00..0x00434e05: counted on both paths.
    ++Script.RefCount;
    return AnimScript;
}

void FCharacterInfoDb::ReleaseScript(unsigned Id)
{
    // FUN_00434e20.
    std::map<unsigned, FCharacterScript>::iterator const It = Scripts.find(Id);
    if (It == Scripts.end())
    {
        return;
    }
    FCharacterScript& Script = It->second;
    // 0x00434e4e: DEC then JNZ -- only the reference that reaches zero unloads.
    --Script.RefCount;
    if (Script.RefCount != 0)
    {
        return;
    }
    // 0x00434e53..0x00434e79: IAnimation2 slot 31 (+0x7c)(AnimScript, &name, 1).
    char const* Names[1];
    Names[0] = Script.Name.c_str();
    DALib::Anim->RemoveScriptNames(AnimScript, Names, 1);
}

void* FCharacterInfoDb::AcquireScriptByName(char const* Name)
{
    // FUN_00434e90.
    return AcquireScript(CreateID(Name));
}

void FCharacterInfoDb::ReleaseScriptByName(char const* Name)
{
    // FUN_00434eb0: FUN_00434e20's body with CreateID in front.
    ReleaseScript(CreateID(Name));
}

void FCharacterInfoDb::AddScript(char const* Name, std::string const* File)
{
    // FUN_00434f40: 0x00434f66 CreateID(Name); 0x00434f7d..0x00434fd3 a string of Name;
    // 0x00434fd7..0x00435002 FUN_00436be0 builds {id, {name, id, 0, File}}; 0x00435050
    // FUN_004360e0 -- the unique insert, whose result is not looked at.
    unsigned const Id = CreateID(Name);
    FCharacterScript Script;
    Script.Name = Name;
    Script.Id = Id;
    Script.RefCount = 0;
    Script.File = File;
    Scripts.insert(std::map<unsigned, FCharacterScript>::value_type(Id, Script));
}

void FCharacterInfoDb::Load(CostumeDescriptions const* Descs)
{
    // FUN_00435100. 0x00435118..0x00435130: +0x2c = IAnimation2 slot 29 (+0x74), no test of
    // DALib::Anim and no release of a script already there.
    AnimScript = DALib::Anim->CreateEmptyScript();

    // 0x00435133..0x00435302: every "anim =" file of CostumeDescriptions +0x00.
    for (std::list<std::string>::const_iterator FileIt = Descs->AnimationNames.begin();
         FileIt != Descs->AnimationNames.end(); ++FileIt)
    {
        char const* const FileName = FileIt->c_str();

        // 0x00435160..0x004351ba: a file already in ScriptFiles (inline strcmp) is skipped.
        bool bKnown = false;
        for (std::list<std::string>::const_iterator KnownIt = ScriptFiles.begin();
             KnownIt != ScriptFiles.end(); ++KnownIt)
        {
            if (strcmp(FileName, KnownIt->c_str()) == 0)
            {
                bKnown = true;
                break;
            }
        }
        if (bKnown)
        {
            continue;
        }

        // 0x004351c4..0x00435242: remembered at the end of ScriptFiles; the scripts keep a
        // pointer to that element (list nodes never move).
        ScriptFiles.push_back(std::string(FileName));
        std::string const* const Stored = &ScriptFiles.back();

        // 0x00435245..0x0043525f: DAEngine_create_filesystem(file, &fs, 0).
        IFileSystem* File = nullptr;
        DAEngine_create_filesystem(FileName, &File, nullptr);
        if (File != nullptr)
        {
            // 0x00435261..0x00435293: IAnimation2 slot 32 (+0x80)(LAB_00434f20, fs, &{this,
            // file}), then the file system released.
            FScriptEnumContext Context;
            Context.Db = this;
            Context.File = Stored;
            DALib::Anim->EnumFsScripts(reinterpret_cast<void*>(&AddEnumeratedScript), File,
                                       &Context);
            DAEngine_release_filesystem(&File);
        }
        else
        {
            // 0x00435298..0x004352b6.
            FDUMP(0x100002, kFailedToOpen, kCharacterInfoCpp, 0x149, FileName);
        }
    }
}

void FCharacterInfoDb::QueueScript(char const* Name)
{
    // FUN_00435320: a known script's map value goes to the end of the queue (+0x20), count +1.
    std::map<unsigned, FCharacterScript>::iterator const It = Scripts.find(CreateID(Name));
    if (It == Scripts.end())
    {
        return;
    }
    Queued.push_back(&It->second);
}

void FCharacterInfoDb::FlushQueuedScripts()
{
    // FUN_004353a0.
    for (;;)
    {
        // 0x004353b0..0x004353bb: an empty queue ends it.
        if (Queued.empty())
        {
            return;
        }

        // 0x004353c1..0x0043541c: one pass. The first entry picks the file (EDI); entries of
        // another file stay. Up to 0x100 matching entries leave the queue with a reference
        // each, and the names of the ones nobody held yet are collected.
        std::string const* File = nullptr;
        char const* Names[0x100];
        int Count = 0;
        std::list<FCharacterScript*>::iterator It = Queued.begin();
        while (It != Queued.end())
        {
            FCharacterScript* const Script = *It;
            if (File == nullptr)
            {
                File = Script->File;
            }
            if (File != Script->File)
            {
                ++It;
                continue;
            }
            if (Count >= 0x100)
            {
                break;
            }
            if (Script->RefCount == 0)
            {
                Names[Count] = Script->Name.c_str();
                ++Count;
            }
            ++Script->RefCount;
            It = Queued.erase(It);
        }

        // 0x0043541e..0x00435420: nothing new to load -> the next round.
        if (Count <= 0)
        {
            continue;
        }

        // 0x00435422..0x00435476: the file opened, IAnimation2 slot 30 (+0x78)(AnimScript, fs,
        // names, count), released.
        char const* const Path = File->c_str();
        IFileSystem* Fs = nullptr;
        DAEngine_create_filesystem(Path, &Fs, nullptr);
        if (Fs != nullptr)
        {
            DALib::Anim->AddScriptNames(AnimScript, Fs, Names, Count);
            DAEngine_release_filesystem(&Fs);
        }
        else
        {
            // 0x00435480..0x0043549d.
            FDUMP(0x100002, kFailedToOpen, kCharacterInfoCpp, 0x1c2, Path);
        }
    }
}

FCharacterInfoDb* GetCharacterInfoDb()
{
    // FUN_004356c0: MOV EAX,DAT_00668660.
    return &G_CharacterInfoDb;
}

void LoadCharacterInfo()
{
    // FUN_004354e0.
    G_CharacterInfoDb.Load(GetCostumeDescriptions());
}

void ShutdownCharacterInfo()
{
    // FUN_00435500: 0x00435504..0x0043555d ScriptFiles node by node, 0x0043555f..0x00435572 the
    // map (FUN_004362f0 over begin..end), 0x00435577..0x004355ae the queue; then
    // 0x004355b0..0x004355cc the script through IAnimation2 slot 4 (no test of DALib::Anim) and
    // +0x2c = -1.
    G_CharacterInfoDb.ScriptFiles.clear();
    G_CharacterInfoDb.Scripts.clear();
    G_CharacterInfoDb.Queued.clear();
    if (G_CharacterInfoDb.AnimScript != NoAnimScript())
    {
        DALib::Anim->CloseScript(G_CharacterInfoDb.AnimScript);
        G_CharacterInfoDb.AnimScript = NoAnimScript();
    }
}

void PreloadCharacterScripts()
{
    // FUN_004355e0: every "preload =" name (CostumeDescriptions +0x0c) acquired by CreateID.
    CostumeDescriptions const* const Descs = GetCostumeDescriptions();
    for (std::list<std::string>::const_iterator It = Descs->PreloadNames.begin();
         It != Descs->PreloadNames.end(); ++It)
    {
        G_CharacterInfoDb.AcquireScript(CreateID(It->c_str()));
    }
}

void ResetCharacterScripts()
{
    // FUN_00435630. 0x00435630..0x0043567e: every map node's +0x24 (value +0x14, RefCount)
    // cleared, in tree order.
    for (std::map<unsigned, FCharacterScript>::iterator It = G_CharacterInfoDb.Scripts.begin();
         It != G_CharacterInfoDb.Scripts.end(); ++It)
    {
        It->second.RefCount = 0;
    }
    // 0x00435680..0x004356b4: only with an IAnimation2 -- the old script closed (slot 4) when
    // there is one, then a new empty script (slot 29).
    if (DALib::Anim == nullptr)
    {
        return;
    }
    if (G_CharacterInfoDb.AnimScript != NoAnimScript())
    {
        DALib::Anim->CloseScript(G_CharacterInfoDb.AnimScript);
        G_CharacterInfoDb.AnimScript = NoAnimScript();
    }
    G_CharacterInfoDb.AnimScript = DALib::Anim->CreateEmptyScript();
}
