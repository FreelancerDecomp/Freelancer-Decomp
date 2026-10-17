// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "alchemy_nodelibrary.h"
#include "msvcrt_compat.h" // MsvcrtAtoi: retail MSVCRT.DLL wraps mod 2^32, the UCRT saturates

#include "alchemy_common.h"
#include "alchemy_genericfx.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>

// Independent node-library instances share registry code. The map owns
// prototypes; their actual Next/Prev sibling links form the borrowed insertion
// list used by enumeration, saving and teardown.
namespace AlchemyImpl
{
    namespace
    {
        template <typename TInterface>
        struct FLoadedInterfaceReference
        {
            FLoadedInterfaceReference() : Value(nullptr) {}
            ~FLoadedInterfaceReference()
            {
                if (Value != nullptr)
                    Value->Release();
            }
            FLoadedInterfaceReference(const FLoadedInterfaceReference&) = delete;
            FLoadedInterfaceReference& operator=(const FLoadedInterfaceReference&) = delete;

            TInterface* Value;
        };
    }

    IDAComponent* FNodePrototypeRegistry::Find(unsigned long Hash)
    {
        FPrototypeMap::iterator Entry = Entries_->find(Hash);
        return Entry == Entries_->end() ? nullptr : Entry->second.Component;
    }

    GENRESULT FNodePrototypeRegistry::FindWithRef(unsigned long Hash, IDAComponent** Out)
    {
        FPrototypeMap::iterator Entry = Entries_->find(Hash);
        if (Entry == Entries_->end())
            return GR_ALE_CREATE_FAIL;
        *Out = Entry->second.Component;
        Entry->second.Component->AddRef();
        return GR_OK;
    }

    GENRESULT FNodePrototypeRegistry::Insert(IFxNodeLibrary* Library, const char* Name,
                                             IDAComponent* Node, unsigned long* OutHash)
    {
        const unsigned long Hash = Library->HashNodeName(Name);
        FAlchemyComponentReference Existing;
        Library->NodeLibSlot12(Hash, &Existing.Component);
        if (Existing.Component != nullptr)
        {
            const char* ExistingName;
            AsFxNode(Existing.Component)->GetInstanceName(&ExistingName);
            if (std::strcmp(ExistingName, Name) != 0)
            {
                FDUMP(0x100001, "%s(%d) : ERROR:General:%s",
                      "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\Alchemy\\FxNodeLibrary.cpp",
                      0x43, "Different name generates same Hash");
            }
            return GR_OK;
        }

        AsFxNode(Node)->SetInstanceName(Name);
        if (OutHash != nullptr)
            *OutHash = Hash;
        bool bInserted = false;
        {
            FAlchemyComponentReference Owner(Node);
            {
                FAlchemyComponentReference Pair(Owner);
                FPrototypeMap::iterator Position = Entries_->lower_bound(Hash);
                if (Position == Entries_->end() || Position->first != Hash)
                {
                    Entries_->emplace_hint(Position, Hash, Pair);
                    bInserted = true;
                }
            }
        }
        if (!bInserted)
            return GR_GENERIC;

        AsFxNode(Node)->SetNextSibling(nullptr);
        if (Tail_ != nullptr)
        {
            AsFxNode(Tail_)->SetNextSibling(Node);
            AsFxNode(Node)->SetPrevSibling(Tail_);
        }
        else
        {
            AsFxNode(Node)->SetPrevSibling(nullptr);
            Head_ = Node;
        }
        Tail_ = Node;
        ++Count_;
        return GR_OK;
    }

    IDAComponent* FNodePrototypeRegistry::UnlinkPrototype(IDAComponent* Node)
    {
        IDAComponent* Prev = AsFxNode(Node)->GetPrevSibling();
        IDAComponent* Next = AsFxNode(Node)->GetNextSibling();
        if (Prev != nullptr)
            AsFxNode(Prev)->SetNextSibling(Next);
        else
            Head_ = Next;
        if (Next != nullptr)
            AsFxNode(Next)->SetPrevSibling(Prev);
        else
            Tail_ = Prev;
        AsFxNode(Node)->SetPrevSibling(nullptr);
        AsFxNode(Node)->SetNextSibling(nullptr);
        return Next;
    }

    GENRESULT FNodePrototypeRegistry::Remove(unsigned long Hash)
    {
        FPrototypeMap::iterator Entry = Entries_->find(Hash);
        if (Entry == Entries_->end() || Entry->first == 0)
            return GR_GENERIC;
        UnlinkPrototype(Entry->second.Component);
        {
            FPrototypeMap::node_type Removed = Entries_->extract(Entry);
            Removed.mapped().Reset();
        }
        --Count_;
        return GR_OK;
    }

    GENRESULT FNodePrototypeRegistry::Enumerate(FNodeLibraryEnumCallback Callback, void* Context)
    {
        IDAComponent* Node = Head_;
        while (Node != nullptr)
        {
            if (Callback(Node, Context) != 0)
                break;
            Node = AsFxNode(Node)->GetNextSibling();
        }
        return GR_OK;
    }

    FNodePrototypeRegistry::FMapStorageOwner::~FMapStorageOwner() noexcept(false)
    {
        if (Storage != nullptr)
        {
            FPrototypeMap* Current = Storage;
            Storage = nullptr;
            Owner->DestroyMapStorage(Current);
        }
    }

    FNodePrototypeRegistry::~FNodePrototypeRegistry() noexcept(false)
    {
        if (MapOwner_.Storage != nullptr)
            Destroy();
    }

    void FNodePrototypeRegistry::Destroy()
    {
        // Map-only ownership is consumed before the detachment callbacks.
        // Entries_ remains published until all releases and deletion succeed.
        FMapStorageOwner Storage(this, MapOwner_.Storage);
        MapOwner_.Storage = nullptr;
        DetachPrototypes();
    }

    void FNodePrototypeRegistry::DetachPrototypes()
    {
        while (Head_ != nullptr)
        {
            IDAComponent* Node = Head_;
            Head_ = AsFxNode(Node)->GetNextSibling();
            AsFxNode(Node)->SetNextSibling(nullptr);
            AsFxNode(Node)->SetPrevSibling(nullptr);
        }
        Head_ = nullptr;
        Tail_ = nullptr;
    }

    void FNodePrototypeRegistry::ClearMapEntries(FPrototypeMap* Storage)
    {
        for (FPrototypeMap::reverse_iterator Entry = Storage->rbegin();
             Entry != Storage->rend(); ++Entry)
        {
            Entry->second.Reset();
        }
        Storage->clear();
    }

    void FNodePrototypeRegistry::DestroyMapStorage(FPrototypeMap* Storage)
    {
        ClearMapEntries(Storage);
        delete Storage;
        Entries_ = nullptr;
    }

    void FNodePrototypeRegistry::Clear()
    {
        DetachPrototypes();
        ClearMapEntries(Entries_);
        Count_ = 0;
    }

    // 06228b90: the shared embedded/standalone loader retains one IFxNode and
    // its property set until insertion. Header failures close without leaving;
    // record failures leave then close. EH only closes, after releasing owners.
    GENRESULT FNodePrototypeRegistry::Load(IFxNodeLibrary* Library, IFileSystem* Filesys)
    {
        if (Filesys->OpenNode(kNodeLibName) == 0)
            return GR_ALE_LOAD_FAIL;

        FAleChunk Chunk;
        Chunk.OpenChunk(Filesys, kNodeLibName, 0);
        unsigned long VersionBits = 0;
        int Count = 0;
        if (Chunk.ReadBytes(&VersionBits, 4) < 0)
        {
            Chunk.CloseChunk();
            return GR_ALE_LOAD_FAIL;
        }
        GENRESULT Result = Chunk.ReadBytes(&Count, 4);
        if (Result < 0)
        {
            Chunk.CloseChunk();
            return GR_ALE_LOAD_FAIL;
        }

        for (int Index = 0; Index < Count; ++Index)
        {
            char Name[256];
            Result = Chunk.ReadString(Name, kAleNameMax);
            if (Result < 0)
                break;
            FixColideFieldAlias(Name);

            FLoadedInterfaceReference<IDAComponent> Created;
            FLoadedInterfaceReference<IGenericFx> Fx;
            AGGDESC Desc;
            Desc.size = sizeof(Desc);
            Desc.interface_name = Name;
            Result = DACOM_Acquire()->CreateInstance(
                &Desc, reinterpret_cast<void**>(&Created.Value));
            if (Result < 0)
            {
                const std::string Message = std::string("Unable to create instance of node \"") +
                    Name + "\". Aborting node library load.\n";
                FDUMP(0x100001, "%s(%d) : ERROR:General:%s",
                      "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\Alchemy\\FxNodeLibrary.cpp",
                      0x3bb, Message.c_str());
                break;
            }

            Result = Created.Value->QueryInterface(
                Iids::FxNode, reinterpret_cast<void**>(&Fx.Value));
            if (Result < 0)
            {
                const std::string Message = std::string("DACOM class \"") + Name +
                    "\" does not implement IFxNode. Aborting node library load.\n";
                FDUMP(0x100001, "%s(%d) : ERROR:General:%s",
                      "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\Alchemy\\FxNodeLibrary.cpp",
                      0x3c8, Message.c_str());
                break;
            }

            FLoadedInterfaceReference<IFxNodePropertySet> Set;
            Fx.Value->GetPropertySet(&Set.Value);
            ReadNodeLibProperties(&Chunk, Set.Value, VersionBits);
            const char* CurrentName = nullptr;
            Set.Value->GetPropName(0xefa8ce01, &CurrentName);
            if (CurrentName == nullptr || CurrentName[0] == '\0')
            {
                FDUMP(0x100002, "%s(%d) : WARNING:General:%s",
                      "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\Alchemy\\FxNodeLibrary.cpp",
                      0x3d8, "This node has empty name. Alchemy give new name for this node.");
                CurrentName = "unnamed";
                unsigned long Suffix = 0;
                while (Entries_->find(Library->HashNodeName(CurrentName)) != Entries_->end())
                {
                    // The inline loader starts from the fixed literal "unnamed":
                    // every collision retains this base and an empty extension.
                    // Unsigned increment preserves the retail 32-bit counter wrap.
                    ++Suffix;
                    std::sprintf(G_ResolvedName, "unnamed#%d", static_cast<int>(Suffix));
                    CurrentName = G_ResolvedName;
                }
                Set.Value->SetPropName(0xefa8ce01, CurrentName);
                Set.Value->GetPropName(0xefa8ce01, &CurrentName);
            }
            Result = Library->NodeLibSlot6(CurrentName, Fx.Value, nullptr);
        }
        Filesys->OpenNode(kAleLeaveNode);
        Chunk.CloseChunk();
        return Result;
    }

    FFxNodeLibrary::~FFxNodeLibrary() noexcept(false) {}

    GENRESULT DACOM_API FFxNodeLibrary::QueryInterface(const char* Iid, void** Out)
    {
        // LAB_0622afe0 (raw listing 88630-88719): a real 2-entry IID table
        // walked by index (stride 8, {namePtr, offset} pairs at
        // 0624e114/0624e11c) — the candidate strings Ghidra resolves at
        // those slots are "1.11_IFxNodeLibrary" and "1.11_IDAComponent",
        // both mapping back to offset 0 (this object has no second vtable
        // to adjust into, unlike FAlchemy's outer/inner split). docs/re/
        // alchemy.md line 88's RegisterComponent table only lists the
        // primary "1.11_IFxNodeLibrary" IID; the raw QI bytes show
        // "1.11_IDAComponent" is accepted too, same as FAlchemy's own QI
        // (alchemy_alchemy.cpp).
        if (IidEqual(Iid, Iids::FxNodeLibrary) || IidEqual(Iid, Iids::DAComponent))
        {
            AddRef();
            *Out = static_cast<IFxNodeLibrary*>(this);
            return GR_OK;
        }
        *Out = nullptr;
        return GR_INTERFACE_UNSUPPORTED;
    }

    unsigned long DACOM_API FFxNodeLibrary::Release()
    {
        // FUN_0622b0a0 (alchemy.dll.txt:88728-88753, wired as the standalone
        // "FxNodeLibrary" component's outer vtable slot 2 / Release,
        // PTR_LAB_0624e0a4+0x8): re-examined this wave -- retail guards the
        // decrement itself against underflow (`if (refcount != 0)
        // refcount--;` before the zero-check), not merely "floors" a
        // separate clamp step as a prior wave's notes characterized it. This
        // is the same underflow-guard pattern already fixed in
        // FComponentFactory::Release, FFxRuntime::Release, and
        // FFxEffectArchetype::Release: a stray extra Release() call past 0
        // previously underflowed RefCount_ to 0xFFFFFFFF here (a real
        // permanent-leak bug, not just a defensive nicety) instead of
        // retail's no-op. Fixed as part of the wave-42 targeted COM-refcount
        // sweep.
        if (RefCount_ != 0)
            --RefCount_;
        if (RefCount_ != 0)
            return RefCount_;

        // [wave59-lane1] re-examined: byte-verified 0x0622b0b7-0x0622b0c0
        // (MOV [ESI+0x28],1 immediately before the teardown calls
        // FUN_0622b140/FUN_062460e0) -- retail re-arms RefCount_ to 1 right
        // before tearing the object down, guarding the teardown path against
        // observing/acting on a RefCount_==0 object if it were to reenter
        // AddRef/Release on `this` (e.g. via a QueryInterface loop-back).
        // This port's destructor only Releases owned FX nodes and never
        // touches `this`, so today there is no such reentrancy path, but
        // mirroring the guard costs nothing and matches retail exactly.
        RefCount_ = 1;
        this->~FFxNodeLibrary();
        ::operator delete(this);
        return 0;
    }

    GENRESULT DACOM_API FFxNodeLibrary::LoadNodeLibrary(IFileSystem* Filesys)
    {
        return NodeRegistry_.Load(this, Filesys);
    }

    GENRESULT DACOM_API FFxNodeLibrary::SaveNodeLibrary(IFileSystem* Filesys)
    {
        FAleChunk Chunk;
        Chunk.Filesys = Filesys;
        Chunk.DeleteNode(kNodeLibName);
        Chunk.OpenNode(kNodeLibName);
        Chunk.OpenChunk(Filesys, kNodeLibName, 1);
        const unsigned long VersionBits = kAleVersion11Bits;
        Chunk.WriteBytes(&VersionBits, 4);
        Chunk.WriteBytes(NodeRegistry_.CountAddress(), 4);
        IDAComponent* Node = NodeRegistry_.First();
        while (Node != nullptr)
        {
            WriteNodeLibProperties(&Chunk, Node);
            Node = AsFxNode(Node)->GetNextSibling();
        }
        Chunk.OpenNode(kAleLeaveNode);
        Chunk.CloseChunk();
        return GR_OK;
    }

    GENRESULT DACOM_API FFxNodeLibrary::NodeLibSlot6(const char* Name, IDAComponent* Node,
                                                       unsigned long* OutHash)
    {
        return InsertNodeByName(Name, Node, OutHash);
    }

    GENRESULT DACOM_API FFxNodeLibrary::NodeLibSlot7(unsigned long Hash)
    {
        return RemoveNodeByHash(Hash);
    }

    GENRESULT DACOM_API FFxNodeLibrary::NodeLibSlot8(FNodeLibraryEnumCallback Callback, void* Context)
    {
        return NodeRegistry_.Enumerate(Callback, Context);
    }

    GENRESULT DACOM_API FFxNodeLibrary::ClearNodeLibrary()
    {
        ClearNodeList();
        return GR_OK;
    }

    GENRESULT DACOM_API FFxNodeLibrary::NodeLibSlot10(void* NameRaw, void* OutRaw)
    {
        // 062277e0: borrowed original name or the independent DAT_06257d80 buffer.
        const char* CurrentName = static_cast<const char*>(NameRaw);
        const char** Out = static_cast<const char**>(OutRaw);
        char Base[256];
        char Ext[32];
        int Suffix = 0;
        while (FindNodeEntry(HashNodeName(CurrentName)) != nullptr)
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
            unsigned int SuffixBits;
            std::memcpy(&SuffixBits, &Suffix, sizeof(SuffixBits));
            ++SuffixBits;
            std::memcpy(&Suffix, &SuffixBits, sizeof(Suffix));
            std::sprintf(G_NodeLibraryResolvedName, "%s#%d%s", Base, Suffix, Ext);
            CurrentName = G_NodeLibraryResolvedName;
        }
        *Out = CurrentName;
        return GR_OK;
    }

    GENRESULT DACOM_API FFxNodeLibrary::NodeLibSlot11(unsigned long Hash, IDAComponent** Out)
    {
        IDAComponent* Prototype = FindNodeEntry(Hash);
        if (Prototype == nullptr)
            return GR_ALE_CREATE_FAIL;
        return InstantiateFromNodeLibPrototype(Prototype, Out);
    }

    GENRESULT DACOM_API FFxNodeLibrary::NodeLibSlot12(unsigned long Hash, IDAComponent** Out)
    {
        return NodeRegistry_.FindWithRef(Hash, Out);
    }

    IDAComponent* FFxNodeLibrary::FindNodeEntry(unsigned long Hash)
    {
        return NodeRegistry_.Find(Hash);
    }

    GENRESULT FFxNodeLibrary::InsertNodeByName(const char* Name, IDAComponent* Node,
                                             unsigned long* OutHash)
    {
        return NodeRegistry_.Insert(this, Name, Node, OutHash);
    }

    GENRESULT FFxNodeLibrary::RemoveNodeByHash(unsigned long Hash)
    {
        return NodeRegistry_.Remove(Hash);
    }

    void FFxNodeLibrary::ClearNodeList()
    {
        NodeRegistry_.Clear();
    }

} // namespace AlchemyImpl
