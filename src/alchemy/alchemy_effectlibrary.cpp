// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "alchemy_effectlibrary.h"
#include "msvcrt_compat.h" // MsvcrtAtoi: retail MSVCRT.DLL wraps mod 2^32, the UCRT saturates

#include "alchemy_common.h"
#include "alchemy_effectarchetype.h"
#include "alchemy_effectobject.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

// alchemy.dll — DACOM particle FX scaffolding.
// Out-of-line method bodies for FFxEffectLibrary.
namespace AlchemyImpl
{
    FFxEffectLibrary::FMapStorageOwner::~FMapStorageOwner() noexcept(false)
    {
        if (Storage != nullptr)
        {
            FArchetypeMap* Current = Storage;
            Storage = nullptr;
            Owner->DestroyMapStorage(Current);
        }
    }

    FFxEffectLibrary::~FFxEffectLibrary() noexcept(false)
    {
        DetachEffectList();
    }

    GENRESULT DACOM_API FFxEffectLibrary::QueryInterface(const char* Iid, void** Out)
    {
        if (IidEqual(Iid, Iids::FxEffectLibrary) || IidEqual(Iid, Iids::DAComponent))
        {
            AddRef();
            *Out = static_cast<IFxEffectLibrary*>(this);
            return GR_OK;
        }
        *Out = nullptr;
        return GR_INTERFACE_UNSUPPORTED;
    }

    unsigned long DACOM_API FFxEffectLibrary::Release()
    {
        // FUN_06222ab0 (alchemy.dll.txt:71581-71606, wired as IFxEffectLibrary
        // vtable slot 2 at 0624d980, immediately after QueryInterface/AddRef
        // at 0624d978/97c): retail guards the decrement against underflow
        // (`if (refcount != 0) refcount--;` before the zero-check) rather
        // than decrementing unconditionally -- the same underflow-guard
        // pattern already fixed in FComponentFactory::Release,
        // FFxRuntime::Release, and FFxEffectArchetype::Release. A stray
        // extra Release() call past 0 previously underflowed RefCount_ to
        // 0xFFFFFFFF here instead of retail's no-op. Fixed as part of the
        // wave-42 targeted COM-refcount sweep.
        //
        // [wave61-lane2] fresh re-derivation from raw bytes also found retail
        // stores 0x1 back into [ESI+0x24] (`MOV dword ptr [ESI+0x24],0x1`,
        // 06222ac9) immediately before calling the map-teardown helper
        // (FUN_06222cc0) and operator delete (FUN_062460e0) -- the same
        // teardown reentrancy-guard pattern documented for FFxEffectArchetype
        // ::Release (this wave), FFxNodeLibrary,
        // FFxAnimatedSingle, and FFxEffectObject. Mirrored here to match
        // retail exactly.
        if (RefCount_ != 0)
            --RefCount_;
        if (RefCount_ == 0)
        {
            RefCount_ = 1;
            this->~FFxEffectLibrary();
            ::operator delete(this);
            return 0;
        }
        return RefCount_;
    }

    GENRESULT DACOM_API FFxEffectLibrary::BindAlchemy(IAlchemy* Alchemy, IFxNodeLibrary* NodeLibrary)
    {
        G_EffectAlchemy = Alchemy;
        G_EffectNodeLibrary = NodeLibrary;
        return GR_OK;
    }

    // FUN_0621fd50: the unsigned-key map owns archetypes; each stable map
    // record also carries borrowed Next/Prev links for insertion-order iteration.
    // Temporary smart-pointer copies preserve the individual reference calls.
    GENRESULT DACOM_API FFxEffectLibrary::InsertArchetype(IFxEffectArchetype* Archetype,
                                                          unsigned long* OutHash)
    {
        const unsigned long Hash = G_EffectAlchemy->HashNodeName(Archetype->GetName());
        FAleLibEntry::FArchetypeReference Existing;
        if (FindByHash(Hash, &Existing.Archetype) >= 0)
        {
            const char* NewName = Archetype->GetName();
            const char* ExistingName = Existing.Archetype->GetName();
            if (std::strcmp(ExistingName, NewName) != 0)
            {
                FDUMP(0x100001, "%s(%d) : ERROR:General:%s",
                      "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\Alchemy\\FxEffect.cpp",
                      0x2d3, "Different name generates same Hash");
            }
            return GR_GENERIC;
        }
        if (OutHash != nullptr)
            *OutHash = Hash;
        FAleLibEntry* Inserted = nullptr;
        {
            FAleLibEntry::FArchetypeReference Owner(Archetype);
            {
                FAleLibEntry Pair(Owner);
                FArchetypeMap::iterator Position = Entries_->lower_bound(Hash);
                if (Position == Entries_->end() || Position->first != Hash)
                {
                    FArchetypeMap::iterator Entry = Entries_->emplace_hint(Position, Hash, Pair);
                    Inserted = &Entry->second;
                }
            }
        }
        if (Inserted == nullptr)
            return GR_GENERIC;
        Inserted->Next = nullptr;
        if (Tail_ != nullptr)
        {
            Tail_->Next = Inserted;
            Inserted->Prev = Tail_;
        }
        else
        {
            Inserted->Prev = nullptr;
            Head_ = Inserted;
        }
        Tail_ = Inserted;
        ++EntryCount_;
        return GR_OK;
    }

    GENRESULT DACOM_API FFxEffectLibrary::RemoveByHash(unsigned long Hash)
    {
        FArchetypeMap::iterator Found = Entries_->find(Hash);
        if (Found == Entries_->end())
            return GR_GENERIC;
        FAleLibEntry* Entry = &Found->second;
        FAleLibEntry* Prev = Entry->Prev;
        FAleLibEntry* Next = Entry->Next;
        if (Prev != nullptr)
            Prev->Next = Next;
        else
            Head_ = Next;
        if (Next != nullptr)
            Next->Prev = Prev;
        else
            Tail_ = Prev;
        Entry->Prev = nullptr;
        Entry->Next = nullptr;
        {
            FArchetypeMap::node_type Removed = Entries_->extract(Found);
            Removed.mapped().Reference.Reset();
        }
        --EntryCount_;
        return GR_OK;
    }

    // FUN_06220020 (RET 0x10: this, Param1, Key, Out), 0x06220020..0x0622012f:
    //   operator_new(0x24) and the effect constructor inline (count +0x20 = 1) come FIRST;
    //   this->+0x2C FindByHash(Key as a raw dword, &Archetype); non-zero -> Release the
    //   archetype if one came back, return -8 -- the new effect is not released (retail
    //   leaks it on this path).
    //   Otherwise Desc = {8, "FxEffect" (0x06256054), Archetype, Param1}; Result = effect
    //   +0x48 Initialize(&Desc); if Out != 0, *Out = effect -- whatever Result is; the
    //   archetype reference is released; return Result.
    GENRESULT DACOM_API FFxEffectLibrary::CreateEffect(void* Param1, void* Key, IFxEffect** Out)
    {
        FFxEffectObject* const Effect = new FFxEffectObject();
        IFxEffectArchetype* Archetype = nullptr;
        const unsigned long Hash = static_cast<unsigned long>(reinterpret_cast<unsigned int>(Key));
        if (FindByHash(Hash, &Archetype) != GR_OK)
        {
            if (Archetype != nullptr)
                Archetype->Release();
            return GR_ALE_CREATE_FAIL;
        }

        FFxEffectCreateDesc Desc;
        Desc.Size = 8;
        Desc.NameAt4 = "FxEffect";
        Desc.Archetype = Archetype;
        Desc.RootParent = static_cast<IDAComponent*>(Param1);
        const GENRESULT Result = Effect->Initialize(reinterpret_cast<AGGDESC*>(&Desc));
        if (Out != nullptr)
            *Out = Effect;
        if (Archetype != nullptr)
            Archetype->Release();
        return Result;
    }

    GENRESULT DACOM_API FFxEffectLibrary::SaveAle(IFileSystem* Filesys)
    {
        FAleChunk Chunk;
        Chunk.Filesys = Filesys;
        Chunk.DeleteNode(kAleNodeName);
        Chunk.OpenNode(kAleNodeName);
        Chunk.OpenChunk(Filesys, kAleNodeName, 1);
        const unsigned long VersionBits = kAleVersion11Bits;
        Chunk.WriteBytes(&VersionBits, 4);
        Chunk.WriteBytes(&EntryCount_, 4);
        FAleLibEntry* Entry = Head_;
        while (Entry != nullptr)
        {
            IFxEffectArchetype* Archetype = Entry->Reference.Archetype;
            Chunk.WriteString(Archetype->GetName());
            unsigned char Header16[16];
            Archetype->GetHeader16(Header16);
            Chunk.WriteBytes(Header16, 12);
            Chunk.WriteBytes(Header16 + 12, 4);
            unsigned long RecCount = Archetype->GetRec16Count();
            Chunk.WriteBytes(&RecCount, 4);
            for (long Index = 0; Index < static_cast<long>(RecCount); ++Index)
            {
                FAleRec16 Rec;
                Archetype->GetRec16(static_cast<unsigned long>(Index), &Rec);
                Chunk.WriteBytes(&Rec, 16);
            }
            RecCount = Archetype->GetRec8Count();
            Chunk.WriteBytes(&RecCount, 4);
            for (long Index = 0; Index < static_cast<long>(RecCount); ++Index)
            {
                FAleRec8 Rec;
                Archetype->GetRec8(static_cast<unsigned long>(Index), &Rec);
                Chunk.WriteBytes(&Rec, 8);
            }
            Entry = Entry->Next;
        }
        Chunk.OpenNode(kAleLeaveNode);
        Chunk.CloseChunk();
        return GR_OK;
    }

    // LAB_06220310: failed header reads close only; failed record-count reads
    // take the successful leave/close epilogue. Neither path releases the current
    // archetype. Its construction EH owns storage only until construction ends.
    GENRESULT DACOM_API FFxEffectLibrary::LoadAle(IFileSystem* Filesys)
    {
        if (Filesys->OpenNode(kAleNodeName) == 0)
            return GR_ALE_LOAD_FAIL;

        FAleChunk Chunk;
        Chunk.OpenChunk(Filesys, kAleNodeName, 0);
        unsigned long VersionBits = 0;
        long Count = 0;
        if (Chunk.ReadBytes(&VersionBits, 4) < 0 || Chunk.ReadBytes(&Count, 4) < 0)
        {
            Chunk.CloseChunk();
            return GR_ALE_LOAD_FAIL;
        }

        float Version;
        std::memcpy(&Version, &VersionBits, sizeof(Version));
        const unsigned long ThresholdBits = kAleVersion11Bits;
        float Threshold;
        std::memcpy(&Threshold, &ThresholdBits, sizeof(Threshold));

        for (long Index = 0; Index < Count; ++Index)
        {
            FFxEffectArchetype* const Archetype = new (std::nothrow) FFxEffectArchetype();
            char Name[kAleNameMax];
            if (Chunk.ReadString(Name, kAleNameMax) < 0)
            {
                Archetype->Release();
                continue;
            }
            Archetype->SetName(Name);

            unsigned char Header16[16];
            // FNSTSW AX; AND EAX,0x100; JNZ skips reads for less/unordered;
            // greater/equal falls through to the two header reads.
            if (Version >= Threshold)
            {
                if (Chunk.ReadBytes(Header16, 12) < 0 || Chunk.ReadBytes(Header16 + 12, 4) < 0)
                {
                    Chunk.CloseChunk();
                    return GR_ALE_LOAD_FAIL;
                }
            }
            else
            {
                std::memset(Header16, 0, sizeof(Header16));
            }
            Archetype->SetHeader16(Header16);

            long RecCount = 0;
            if (Chunk.ReadBytes(&RecCount, 4) < 0)
                break;
            for (long RecIndex = 0; RecIndex < RecCount; ++RecIndex)
            {
                FAleRec16 Rec;
                if (Chunk.ReadBytes(&Rec, 16) < 0)
                    break;
                Archetype->AddRec16(&Rec, nullptr, Rec.D);
            }

            if (Chunk.ReadBytes(&RecCount, 4) < 0)
                break;
            for (long RecIndex = 0; RecIndex < RecCount; ++RecIndex)
            {
                FAleRec8 Rec;
                if (Chunk.ReadBytes(&Rec, 8) < 0)
                    break;
                Archetype->AddRec8(&Rec);
            }
            InsertArchetype(Archetype, nullptr);
            Archetype->Release();
        }

        Filesys->OpenNode(kAleLeaveNode);
        Chunk.CloseChunk();
        return GR_OK;
    }

    GENRESULT DACOM_API FFxEffectLibrary::ClearEffects()
    {
        EntryCount_ = 0;
        ClearEffectList();
        return GR_OK;
    }

    GENRESULT DACOM_API FFxEffectLibrary::EnumerateArchetypes(FEffectLibraryEnumCallback Callback,
                                                            void* Context)
    {
        FAleLibEntry* Entry = Head_;
        while (Entry != nullptr)
        {
            if (Callback(Entry->Reference.Archetype, Context) != 0)
                break;
            Entry = Entry->Next;
        }
        return GR_OK;
    }

    // LAB_06220760 (RET 0xC: this, Hash, Out): FUN_06222000 lower_bound on the map at this+4;
    // end or node key (+0xC) above Hash -> return -1, *Out untouched. Hit: *Out = node value
    // (+0x18) -- no null test on Out -- then AddRef on it, return 0.
    GENRESULT DACOM_API FFxEffectLibrary::FindByHash(unsigned long Hash, IFxEffectArchetype** Out)
    {
        FArchetypeMap::iterator Entry = Entries_->find(Hash);
        if (Entry == Entries_->end())
            return GR_GENERIC;
        IFxEffectArchetype* Found = Entry->second.Reference.Archetype;
        *Out = Found;
        Found->AddRef();
        return GR_OK;
    }

    GENRESULT DACOM_API FFxEffectLibrary::MakeUniqueName(const char* Name, const char** Out)
    {
        const char* CurrentName = Name;
        char Base[0x100];
        char Ext[0x20];
        int Suffix = 0;
        while (ContainsHash(HashNameCrc(CurrentName)))
        {
            if (Suffix == 0)
            {
                std::strcpy(Base, CurrentName);
                char* HashMark = std::strrchr(Base, '#');
                if (HashMark != nullptr)
                {
                    *HashMark = '\0';
                    Suffix = MsvcrtAtoi(HashMark + 1);
                }
                const char* Dot = std::strrchr(CurrentName, '.');
                if (Dot != nullptr)
                    std::strcpy(Ext, Dot);
                else
                    Ext[0] = '\0';
            }
            // INC EBP wraps all 32 bits; avoid signed C++ overflow at INT_MAX.
            unsigned int SuffixBits;
            std::memcpy(&SuffixBits, &Suffix, sizeof(SuffixBits));
            ++SuffixBits;
            std::memcpy(&Suffix, &SuffixBits, sizeof(Suffix));
            std::sprintf(G_EffectResolvedName, "%s#%d%s", Base, Suffix, Ext);
            CurrentName = G_EffectResolvedName;
        }
        *Out = CurrentName;
        return GR_OK;
    }

    GENRESULT DACOM_API FFxEffectLibrary::RemapAllRec16FieldD(unsigned long OldId,
                                                               unsigned long NewId)
    {
        FAleLibEntry* Entry = Head_;
        while (Entry != nullptr)
        {
            Entry->Reference.Archetype->RemapRec16FieldD(OldId, NewId);
            Entry = Entry->Next;
        }
        return GR_OK;
    }

    unsigned char FFxEffectLibrary::ContainsHash(unsigned long Hash) const
    {
        return Entries_->find(Hash) != Entries_->end() ? 1 : 0;
    }

    void FFxEffectLibrary::DetachEffectList()
    {
        while (Head_ != nullptr)
        {
            FAleLibEntry* Entry = Head_;
            Head_ = Entry->Next;
            Entry->Next = nullptr;
            Entry->Prev = nullptr;
        }
        Head_ = nullptr;
        Tail_ = nullptr;
    }

    void FFxEffectLibrary::ClearMapEntries(FArchetypeMap* Storage)
    {
        for (FArchetypeMap::reverse_iterator Entry = Storage->rbegin();
             Entry != Storage->rend(); ++Entry)
        {
            Entry->second.Reference.Reset();
        }
        Storage->clear();
    }

    void FFxEffectLibrary::DestroyMapStorage(FArchetypeMap* Storage)
    {
        ClearMapEntries(Storage);
        delete Storage;
        Entries_ = nullptr;
    }

    void FFxEffectLibrary::ClearEffectList()
    {
        DetachEffectList();
        ClearMapEntries(Entries_);
    }

} // namespace AlchemyImpl
