// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "alchemy_types.h"
#include "alchemy_common.h"
#include <map>

// FAlchemy's embedded IFxNodeLibrary and the standalone FxNodeLibrary own
// separate instances of the same registry. An unsigned-key map owns each
// prototype; Head/Tail borrow the node's actual sibling links in insertion order.
// Modern std::map replaces only compiler storage, not linkage or reference calls.
// The independent retail constructor/destructor chains are 0622a620/06202550
// and 06201010/06201190; shared registry operations start at 062273c0.
namespace AlchemyImpl
{
    using ::IFxNodeLibrary;

    struct FAlchemyComponentReference
    {
        FAlchemyComponentReference() : Component(nullptr) {}
        explicit FAlchemyComponentReference(IDAComponent* Value) : Component(Value)
        {
            if (Component != nullptr)
                Component->AddRef();
        }
        FAlchemyComponentReference(const FAlchemyComponentReference& Other)
            : FAlchemyComponentReference(Other.Component) {}
        ~FAlchemyComponentReference() { Reset(); }
        FAlchemyComponentReference& operator=(const FAlchemyComponentReference&) = delete;

        void Reset()
        {
            if (Component != nullptr)
            {
                Component->Release();
                Component = nullptr;
            }
        }

        IDAComponent* Component;
    };

    class FNodePrototypeRegistry
    {
    public:
        FNodePrototypeRegistry()
            : Entries_(new FPrototypeMap), MapOwner_(this, Entries_),
              Count_(0), Head_(nullptr), Tail_(nullptr) {}
        ~FNodePrototypeRegistry() noexcept(false);
        FNodePrototypeRegistry(const FNodePrototypeRegistry&) = delete;
        FNodePrototypeRegistry& operator=(const FNodePrototypeRegistry&) = delete;

        IDAComponent* Find(FlULong Hash);
        GENRESULT FindWithRef(FlULong Hash, IDAComponent** Out);
        GENRESULT Insert(IFxNodeLibrary* Library, const char* Name, IDAComponent* Node,
                         FlULong* OutHash);
        GENRESULT Remove(FlULong Hash);
        GENRESULT Enumerate(FNodeLibraryEnumCallback Callback, void* Context);
        GENRESULT Load(IFxNodeLibrary* Library, IFileSystem* Filesys);
        void Clear();
        void Destroy();
        FlULong Count() const { return Count_; }
        const FlULong* CountAddress() const { return &Count_; }
        IDAComponent* First() const { return Head_; }

    private:
        IDAComponent* UnlinkPrototype(IDAComponent* Node);
        using FPrototypeMap = std::map<FlULong, FAlchemyComponentReference>;
        struct FMapStorageOwner
        {
            FMapStorageOwner(FNodePrototypeRegistry* InOwner, FPrototypeMap* InStorage)
                : Owner(InOwner), Storage(InStorage) {}
            ~FMapStorageOwner() noexcept(false);
            FMapStorageOwner(const FMapStorageOwner&) = delete;
            FMapStorageOwner& operator=(const FMapStorageOwner&) = delete;

            FNodePrototypeRegistry* Owner;
            FPrototypeMap* Storage;
        };
        void DetachPrototypes();
        void ClearMapEntries(FPrototypeMap* Storage);
        void DestroyMapStorage(FPrototypeMap* Storage);
        FPrototypeMap* Entries_;
        FMapStorageOwner MapOwner_;
        FlULong Count_;
        IDAComponent* Head_;
        IDAComponent* Tail_;
    };

    class FFxNodeLibrary final : public IFxNodeLibrary
    {
    public:
        FFxNodeLibrary()
            : RefCount_(1)
        {
        }

        ~FFxNodeLibrary() noexcept(false);

        GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
        FlULong DACOM_API AddRef() override { return ++RefCount_; }
        FlULong DACOM_API Release() override;

        GENRESULT DACOM_API LoadNodeLibrary(IFileSystem* Filesys) override;
        GENRESULT DACOM_API SaveNodeLibrary(IFileSystem* Filesys) override;
        FlULong DACOM_API HashNodeName(const char* Name) override
        {
            return HashNameCrc(Name);
        }
        // FUN_062274b0 RET 0x10: name, prototype, optional output hash.
        GENRESULT DACOM_API NodeLibSlot6(const char* Name, IDAComponent* Node,
                                       FlULong* OutHash) override;
        GENRESULT DACOM_API NodeLibSlot7(FlULong Hash) override;
        // FUN_06228b00: enumerate this object's own node-lib entries,
        // Callback(Node, Context) cdecl, stop at first nonzero result.
        GENRESULT DACOM_API NodeLibSlot8(FNodeLibraryEnumCallback Callback, void* Context) override;
        GENRESULT DACOM_API ClearNodeLibrary() override;
        // FUN_062277e0: "#N" collision-avoidance MakeUniqueName, checked
        // against this object's own table (not FAlchemy's).
        GENRESULT DACOM_API NodeLibSlot10(void* Name, void* Out) override;
        // LAB_06227710: find the prototype by hash, then CloneNode(Out).
        // Miss = GR_ALE_CREATE_FAIL (-8), output untouched.
        GENRESULT DACOM_API NodeLibSlot11(FlULong Hash, IDAComponent** Out) override;
        // LAB_06227770: find CRC in this object's own table, out = stored
        // node, AddRef.
        GENRESULT DACOM_API NodeLibSlot12(FlULong Hash, IDAComponent** Out) override;

    private:
        IDAComponent* FindNodeEntry(FlULong Hash);
        GENRESULT InsertNodeByName(const char* Name, IDAComponent* Node,
                                  FlULong* OutHash = nullptr);
        GENRESULT RemoveNodeByHash(FlULong Hash);
        void ClearNodeList();

        FlULong RefCount_;
        FNodePrototypeRegistry NodeRegistry_;
    };

} // namespace AlchemyImpl
