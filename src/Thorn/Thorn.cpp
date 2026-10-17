// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Thorn.h"
#include "readfile.h"
#include "ThornScene.h"

#include <cstdio>
#include <cstring>
#include <new>
#include <memory>
#include <string>

// DllMain FUN_06f3b8b0 — docs/re/thorn.md (boot, RegisterComponent ×15, Lua 3.2).
// Retail PE export table is empty: no .def, no exported names.

namespace
{
    const int kFdumpWarning = 0x100002;
}

void ThornFdump(const char* Message)
{
    if (Message != nullptr)
        OutputDebugStringA(Message);
    if (FDUMP != nullptr)
    {
        FDUMP(kFdumpWarning, "%s(%d) : WARNING:General:%s", "Thorn.cpp", 0,
              Message != nullptr ? Message : "");
    }
}

void ThornLogStub(const char* What)
{
    char Line[256];
    _snprintf(Line, sizeof(Line), "Thorn: stub %s\n", What != nullptr ? What : "?");
    Line[sizeof(Line) - 1] = '\0';
    ThornFdump(Line);
}

bool ThornNameEqual(const char* Left, const char* Right)
{
    if (Left == nullptr || Right == nullptr)
        return false;
    // Retail IID-table lookup uses a case-insensitive byte comparison
    // (the same contract used by the engine's other interface dispatchers).
    // Keep that behavior at the modern boundary without reproducing the
    // retail table/object layout.
    return _stricmp(Left, Right) == 0;
}

I3DMathEngine* ThornAcquire3DMathEngine()
{
    I3DMathEngine* MathEngine;
    ICOManager* Manager = DACOM_Acquire();

    FAggDescC Desc;
    Desc.Size = G_AggDescSizeMath;
    Desc.NameAt4 = G_Name3DMathEngine;
    Desc.Field8 = nullptr;
    Manager->CreateInstance(reinterpret_cast<AGGDESC*>(&Desc),
                            reinterpret_cast<void**>(&MathEngine));
    return MathEngine;
}

GENRESULT ThornMathTransform(I3DMathEngine* Math, float* OutVec3, const float* Xform12,
                             const float* InVec3)
{
    if (Math == nullptr || OutVec3 == nullptr || Xform12 == nullptr || InVec3 == nullptr)
        return GR_GENERIC;
    return Math->TransformXform(*reinterpret_cast<FVector*>(OutVec3), // +0x28
                                *reinterpret_cast<const FTransform*>(Xform12),
                                *reinterpret_cast<const FVector*>(InVec3));
}

GENRESULT ThornMathMulMatrix(I3DMathEngine* Math, float* Out9, const float* Left9,
                             const float* Right9)
{
    if (Math == nullptr || Out9 == nullptr || Left9 == nullptr || Right9 == nullptr)
        return GR_GENERIC;
    return Math->MulMatrix(*reinterpret_cast<FMatrix*>(Out9), // +0x20
                           *reinterpret_cast<const FMatrix*>(Left9),
                           *reinterpret_cast<const FMatrix*>(Right9));
}

GENRESULT ThornMathMulTransform(I3DMathEngine* Math, float* Out12, const float* Left12,
                                const float* Right12)
{
    if (Math == nullptr || Out12 == nullptr || Left12 == nullptr || Right12 == nullptr)
        return GR_GENERIC;
    return Math->MulTransform(*reinterpret_cast<FTransform*>(Out12), // +0x1C
                              *reinterpret_cast<const FTransform*>(Left12),
                              *reinterpret_cast<const FTransform*>(Right12));
}

GENRESULT ThornMathRotate(I3DMathEngine* Math, float* OutVec3, const float* Xform12,
                          const float* InVec3)
{
    if (Math == nullptr || OutVec3 == nullptr || Xform12 == nullptr || InVec3 == nullptr)
        return GR_GENERIC;
    return Math->Rotate(*reinterpret_cast<FVector*>(OutVec3), // +0x30
                        *reinterpret_cast<const FTransform*>(Xform12),
                        *reinterpret_cast<const FVector*>(InVec3));
}

GENRESULT ThornMatrixToQuat(I3DMathEngine* Math, FQuat& Dst, const float* Src9)
{
    if (Math == nullptr || Src9 == nullptr)
        return GR_GENERIC;
    return Math->MatrixToQuaternion(reinterpret_cast<FQuaternion&>(Dst), // +0x50
                                    *reinterpret_cast<const FMatrix*>(Src9));
}

GENRESULT ThornMathTransposeTransform(I3DMathEngine* Math, float* OutVec3, const float* Matrix9,
                                      const float* InVec3)
{
    if (Math == nullptr || OutVec3 == nullptr || Matrix9 == nullptr || InVec3 == nullptr)
        return GR_GENERIC;
    return Math->TransposeTransform(*reinterpret_cast<FVector*>(OutVec3), // +0x38
                                    *reinterpret_cast<const FMatrix*>(Matrix9),
                                    *reinterpret_cast<const FVector*>(InVec3));
}

namespace
{
    void DebugPrint(const char* Message)
    {
        if (Message != nullptr)
            OutputDebugStringA(Message);
    }

    bool FactoryIidEqual(const char* Iid)
    {
        for (int Index = 0; Index < G_FactoryIidCompareBytes; ++Index)
        {
            if (Iid[Index] != Iids::ComponentFactory[Index])
                return false;
        }
        return true;
    }

    // Lua 3.2 process state: ThornLua.cpp (FUN_06f24c80 / FUN_06f24cb0).

    class FThornDefaultSaveParser final : public IThornSaveParser
    {
    public:
        explicit FThornDefaultSaveParser(FThornScene& Scene)
            : Scene_(Scene), EntityCount_(0), EventCount_(0), DurationMs_(0)
        {
        }

        void DACOM_API SetEntityCount(unsigned int Count) override { EntityCount_ = Count; }
        void DACOM_API SetEventCount(unsigned int Count) override { EventCount_ = Count; }
        void DACOM_API SetDuration(unsigned int Ms) override { DurationMs_ = Ms; }

        GENRESULT DACOM_API Save(IFileSystem* FileSystem, IScriptEngine* Engine,
                                 IThornComContext* ComContext) override
        {
            // 06f2aa33..06f2aa55: default writer needs FS, engine, and either
            // entities or a nonzero duration. ComContext is passed by Slot5Save.
            if (FileSystem == nullptr || Engine == nullptr ||
                (EntityCount_ == 0 && DurationMs_ == 0))
                return GR_GENERIC;
            return Scene_.SaveToFileSystem(FileSystem, ComContext, DurationMs_);
        }

    private:
        FThornScene& Scene_;
        unsigned int EntityCount_;
        unsigned int EventCount_;
        unsigned int DurationMs_;
    };

    struct FThornFileSystemRelease
    {
        void operator()(IFileSystem* FileSystem) const { FileSystem->Release(); }
    };

    struct FThornContextTailRelease
    {
        explicit FThornContextTailRelease(IThornComContext*& Context) : Context_(Context) {}
        ~FThornContextTailRelease() noexcept(false)
        {
            // 37f33..37f45: a callback may install a new context during Clear.
            if (Context_ != nullptr)
            {
                Context_->Release();
                Context_ = nullptr;
            }
        }
        IThornComContext*& Context_;
    };

    // FUN_06f37b50 stores three vtables: +0 IScriptEngine (.rdata:06f5ad50), +4
    // IGeoTransformControl (06f5ad34, seven slots) and +8 IScriptControl (06f5ad18, seven
    // slots) -- three real bases, answered by the {IID, offset} table at .rdata:06f5b010
    // (offsets 0 / 4 / 8). Until 2026-10-16 the port built the two facets from hand-filled
    // `void*[7]` arrays and `this - 4` / `this - 8` adjustors instead of deriving from the
    // public interfaces in thorn_controls.h, so callers reached them through a type the
    // object did not have.
    class FScriptEngine final : public IScriptEngine, public IGeoTransformControl,
                                public IScriptControl
    {
    public:
        // FUN_06f37b50 (0x118-byte object): +0/+4/+8 the three facet vtables,
        // +0xc a VC6 basic_string (ScriptPath_ -- assigned by LAB_06f38110
        // Slot43LoadPath / LAB_06f38590 Slot44SavePath, read back only by
        // SavePath as the "FileSystem"/"DOS" AGGDESC DataPath), the scene
        // containers (+0xa4 map, +0xbc/+0xcc/+0xdc vectors, +0xe8/+0xf4
        // lists), +0x108 controlling unknown and +0x10c the embedded inner
        // unknown {PTR_LAB_06f5b004, RefCount=1, Owner}.
        FScriptEngine()
            : Outer_(nullptr),
              Inner_(this, &FScriptEngine::InnerQueryInterface, &FScriptEngine::InnerDestroy),
              ComContext_(nullptr), ContextTailRelease_(ComContext_), LoadParser_(nullptr), SaveParser_(nullptr)
        {
            Outer_ = &Inner_;
            Scene_.ForwardCsys().Pos[0] = 0.0f;
            Scene_.ForwardCsys().Pos[1] = 0.0f;
            Scene_.ForwardCsys().Pos[2] = 0.0f;
            std::memset(Scene_.ForwardCsys().Orient, 0, sizeof(Scene_.ForwardCsys().Orient));
            Scene_.ForwardCsys().Orient[0] = 1.0f;
            Scene_.ForwardCsys().Orient[4] = 1.0f;
            Scene_.ForwardCsys().Orient[8] = 1.0f;
            // FUN_06f37b50 initializes the inverse to identity/+0 directly.
            std::memset(SceneCsys_, 0, sizeof(SceneCsys_));
            SceneCsys_[0] = 1.0f;
            SceneCsys_[4] = 1.0f;
            SceneCsys_[8] = 1.0f;
        }

        ~FScriptEngine() noexcept(false)
        {
            // FUN_06f37e00 (thorn.dll.c:17241-17250) releases the embedded
            // COM context twice on teardown, re-checking for null between
            // calls -- the same double-Release contract Slot40SetCom already
            // matches for its double-AddRef.  Mirror it exactly here so a
            // context installed via SetCom is fully released on destruction.
            if (ComContext_ != nullptr)
            {
                ComContext_->Release();
                if (ComContext_ != nullptr)
                {
                    ComContext_->Release();
                    ComContext_ = nullptr;
                }
            }
            // 37e5e is reached only after both initial context releases.
            Scene_.Clear();
        }

        // COM aggregation (LAB_06f3f4c0 factory): the public facets'
        // QI/AddRef/Release (FUN_06f3f7a0 / FUN_06f3f7c0 / FUN_06f3f7e0 =
        // `MOV EAX,[this+0x108]; MOV ECX,[EAX]; JMP [ECX+slot]`) forward to
        // the controlling unknown, which is either the aggregating Outer
        // passed in the AGGDESC or the object's own inner unknown at +0x10c.
        IDAComponent* Aggregate(IDAComponent* Outer)
        {
            Outer_ = (Outer != nullptr) ? Outer : static_cast<IDAComponent*>(&Inner_);
            return &Inner_;
        }

        GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override
        {
            return Outer_->QueryInterface(Iid, Out);
        }

        unsigned long DACOM_API AddRef() override { return Outer_->AddRef(); }

        unsigned long DACOM_API Release() override { return Outer_->Release(); }

        // LAB_06f3f690 (inner unknown slot 0): the retail IScriptEngine table
        // walk (.rdata:06f5b010 = {IID, facet offset} triples) is a plain
        // byte-exact strcmp loop (no case-folding instructions at all),
        // unlike some of the other DACOM dispatchers in this module that do
        // use a case-insensitive contract.  On a hit the facet's OWN vtbl+4
        // (i.e. the delegating AddRef above) is invoked, then *Out = facet
        // and 0 is returned; a miss stores NULL and returns -3.
        GENRESULT NonDelegatingQueryInterface(const char* Iid, void** Out)
        {
            void* Interface;
            if (std::strcmp(Iid, Iids::ScriptEngine) == 0)
                Interface = static_cast<IScriptEngine*>(this);
            else if (std::strcmp(Iid, Iids::GeoTransformControl) == 0)
                Interface = static_cast<IGeoTransformControl*>(this);
            else if (std::strcmp(Iid, Iids::ScriptControl) == 0)
                Interface = static_cast<IScriptControl*>(this);
            else
            {
                *Out = nullptr;
                return GR_INTERFACE_UNSUPPORTED;
            }

            AddRef();
            *Out = Interface;
            return GR_OK;
        }

        static GENRESULT InnerQueryInterface(void* Owner, const char* Iid, void** Out)
        {
            return static_cast<FScriptEngine*>(Owner)->NonDelegatingQueryInterface(Iid, Out);
        }

        // LAB_06f3f760 (inner Release, count reached 0): FUN_06f37e00 dtor +
        // operator delete on the owner.
        static void InnerDestroy(void* Owner) { delete static_cast<FScriptEngine*>(Owner); }

        GENRESULT DACOM_API Slot3(void* A, void* B) override
        {
            // LAB_06f3a910: XOR EAX,EAX / RET 0xC.
            (void)A;
            (void)B;
            return GR_OK;
        }

        GENRESULT DACOM_API Slot4Load(void* FileSystem) override
        {
            // LAB_06f38250: if state≠0 Clear; NULL FS → EAX=0 without parser.
            if (Scene_.PlaybackState() != 0)
                Scene_.Clear();
            if (FileSystem == nullptr)
                return GR_OK;
            const bool bDefaultParser = LoadParser_ == nullptr;
            if (bDefaultParser)
                LoadParser_ = ThornCreateLuaLoadParser(&Scene_);
            GENRESULT Result = GR_GENERIC;
            const GENRESULT LoadResult = LoadParser_->Load(static_cast<IFileSystem*>(FileSystem));
            if (LoadResult >= 0)
            {
                Scene_.SetDuration(static_cast<int>(LoadParser_->GetDuration()));
                LoadParser_->Build(static_cast<IFileSystem*>(FileSystem),
                                   static_cast<IScriptEngine*>(this), ComContext_);
                Scene_.MarkLoaded();
                Scene_.FinalizeLoad(static_cast<int>(LoadParser_->GetDuration()));
                Result = GR_OK;
            }
            if (bDefaultParser)
            {
                if (LoadParser_ != nullptr)
                    LoadParser_->Destroy(1);
                LoadParser_ = nullptr;
            }
            if (Result >= 0)
            {
                // LAB_06f38442..3856c: exactly one monitor and camera, with no
                // existing SET_CAMERA event, produces a zero-time default event.
                int MonitorCount = 0;
                int CameraCount = 0;
                int MonitorIndex = 0;
                int CameraIndex = 0;
                for (unsigned long Index = 0; Index < Scene_.EntityCount(); ++Index)
                {
                    const int Handle = static_cast<int>(reinterpret_cast<std::intptr_t>(
                        Slot7EntityHandle(static_cast<int>(Index))));
                    const FThornEntity* Entity = Scene_.EntityAt(Handle);
                    if (Entity->Snap.Type == kThornEntityMonitor)
                    {
                        ++MonitorCount;
                        MonitorIndex = static_cast<int>(Index);
                    }
                    else if (Entity->Snap.Type == kThornEntityCamera)
                    {
                        ++CameraCount;
                        CameraIndex = static_cast<int>(Index);
                    }
                }
                if (MonitorCount == 1 && CameraCount == 1)
                {
                    if (!Scene_.HasEventType(kThornTypeSetCamera))
                    {
                        int Desc[11] = {};
                        Desc[0] = kThornTypeSetCamera;
                        Desc[5] = -1;
                        Desc[7] = MonitorIndex;
                        Desc[8] = CameraIndex;
                        // 38530..3856c constructs this heap event and never frees it.
                        FThornEvent* Temporary = new(std::nothrow) FThornEvent(kThornTypeSetCamera, Desc);
                        const void* TemporaryDesc = Temporary != nullptr
                            ? &Temporary->SetCameraDesc : nullptr;
                        int EventId;
                        Slot38AddEvent(TemporaryDesc, &EventId);
                    }
                }
            }
            return Result;
        }

        GENRESULT DACOM_API Slot5Save(void* FileSystem) override
        {
            if (FileSystem == nullptr)
                return GR_GENERIC;
            IFileSystem* Fs = static_cast<IFileSystem*>(FileSystem);
            Fs->AddRef();
            std::unique_ptr<IFileSystem, FThornFileSystemRelease> FileSystemReference(Fs);
            const bool bDefaultParser = SaveParser_ == nullptr;
            if (bDefaultParser)
                SaveParser_ = new(std::nothrow) FThornDefaultSaveParser(Scene_);
            SaveParser_->SetEntityCount(static_cast<unsigned int>(Scene_.LiveEntityCount()));
            SaveParser_->SetEventCount(static_cast<unsigned int>(Scene_.EventCount()));
            SaveParser_->SetDuration(static_cast<unsigned int>(Scene_.DurationMs()));
            const GENRESULT Hr = SaveParser_->Save(Fs, static_cast<IScriptEngine*>(this), ComContext_);
            if (bDefaultParser)
            {
                // 3874d reloads the current member after callbacks; 38754
                // deallocates without a virtual destructor call.
                ::operator delete(SaveParser_);
                SaveParser_ = nullptr;
            }
            return Hr;
        }

        unsigned long DACOM_API Slot6EntityCount() override { return Scene_.LiveEntityCount(); }

        void* DACOM_API Slot7EntityHandle(int Index) override { return Scene_.EntityHandle(Index); }

        void* DACOM_API Slot8(int Index) override
        {
            // LAB_06f388c0: same inorder walk as slot 7, inner vtable+0x1C snapshot; fail → 0.
            return Scene_.EntitySnapInorder(Index);
        }

        void* DACOM_API Slot9Entity(int Index) override { return Scene_.EntitySnap(Index); }

        GENRESULT DACOM_API Slot10GetPos(int Index, float* OutVec3) override
        {
            return Scene_.GetEntityPos(Index, OutVec3);
        }

        GENRESULT DACOM_API Slot11GetOrient(int Index, float* OutMat9) override
        {
            return Scene_.GetEntityOrient(Index, OutMat9);
        }

        GENRESULT DACOM_API Slot12Attach(int Index, void* EngineObj) override
        {
            if (static_cast<unsigned int>(Index) >= Scene_.EntityCount())
                return GR_GENERIC;
            return Scene_.Attach(Index, EngineObj);
        }

        GENRESULT DACOM_API Slot13Detach(int Index) override
        {
            if (static_cast<unsigned int>(Index) >= Scene_.EntityCount())
                return GR_GENERIC;
            return Scene_.Detach(Index);
        }

        unsigned long DACOM_API Slot14EventCount() override { return Scene_.EventCount(); }

        void* DACOM_API Slot15EventByIndex(int Index) override
        {
            return Scene_.EventDescByIndex(Index);
        }

        void* DACOM_API Slot16EventById(int Id) override { return Scene_.EventDescById(Id); }

        int DACOM_API Slot17GetDuration() override { return Scene_.DurationMs(); }

        GENRESULT DACOM_API Slot18SetDuration(int Ms) override
        {
            Scene_.SetDuration(Ms);
            return GR_OK;
        }

        int DACOM_API Slot19GetClock() override { return Scene_.ClockMs(); }

        GENRESULT __thiscall Slot20SetLoop(unsigned char Value) override
        {
            // LAB_06f39430: MOV AL,[ESP+4]; MOV [ECX+0x30],AL; XOR EAX,EAX; RET 4.
            Scene_.SetLoop(Value);
            return GR_OK;
        }

        GENRESULT DACOM_API Slot21GetState() override
        {
            // LAB_06f39440: return this+0x2c.
            return static_cast<GENRESULT>(Scene_.PlaybackState());
        }

        GENRESULT DACOM_API Slot22Start(int Arg) override { return Scene_.StartPlayback(Arg); }

        GENRESULT DACOM_API Slot23Stop() override { return Scene_.StopPlayback(); }

        GENRESULT DACOM_API Slot24Pause() override { return Scene_.PausePlayback(); }

        GENRESULT DACOM_API Slot25Start(int Arg) override
        {
            // LAB_06f39450: JMP [vtable+0x58] = slot 22, same arity RET 0x8.
            return Slot22Start(Arg);
        }

        GENRESULT DACOM_API Slot26Tick(int DtMs) override { return Scene_.Tick(DtMs); }

        GENRESULT DACOM_API Slot27(void* A) override
        {
            (void)A;
            return GR_OK;
        }

        GENRESULT DACOM_API Slot28(void* A, void* B) override
        {
            (void)A;
            (void)B;
            return GR_OK;
        }

        GENRESULT DACOM_API Slot29(void* A, void* B, void* C) override
        {
            (void)A;
            (void)B;
            (void)C;
            return GR_GENERIC;
        }

        GENRESULT DACOM_API Slot30(void* A, void* B, void* C) override
        {
            (void)A;
            (void)B;
            (void)C;
            return GR_GENERIC;
        }

        unsigned long DACOM_API Slot31(int Index) override
        {
            // LAB_06f38ca0: lookup entity; return inner+0x80 (_Mysize of string userprops).
            return Scene_.UserPropCount(Index);
        }

        const char* DACOM_API Slot32(int Index, int N) override
        {
            // LAB_06f38d20: N-th inorder key of the same map as slots 33/34 (inner+0x74).
            return Scene_.UserPropKey(Index, N);
        }

        GENRESULT DACOM_API Slot33UserProp(void* Handle, const char* Key, char** Out) override
        {
            return Scene_.UserProp(Handle, Key, Out);
        }

        GENRESULT DACOM_API Slot34SetUserProp(int Index, const char* Key, const char* Value) override
        {
            return Scene_.SetUserProp(Index, Key, Value);
        }

        GENRESULT DACOM_API Slot35AddEntity(void* Desc, int* OutIndex) override
        {
            return Scene_.AddEntityFromDesc(Desc, OutIndex);
        }

        GENRESULT DACOM_API Slot36RemoveEntity(int Index) override
        {
            return Scene_.RemoveEntityAt(Index);
        }

        GENRESULT DACOM_API Slot37UpdateEntity(int Index, void* Desc) override
        {
            return Scene_.UpdateEntityFromDesc(Index, Desc);
        }

        GENRESULT DACOM_API Slot38AddEvent(const void* Desc, int* OutId) override
        {
            return Scene_.AddEventFromDesc(Desc, OutId);
        }

        GENRESULT DACOM_API Slot39RemoveEvent(int Id) override
        {
            return Scene_.RemoveEventById(Id);
        }

        GENRESULT DACOM_API Slot40SetCom(IThornComContext* Com) override
        {
            // FUN_06f3a380: double Release old this+0x1c; write p; AddRef; if non-null AddRef again.
            if (ComContext_ != nullptr)
            {
                ComContext_->Release();
                if (ComContext_ != nullptr)
                {
                    ComContext_->Release();
                    ComContext_ = nullptr;
                }
            }
            if (ComContext_ != Com)
            {
                if (ComContext_ != nullptr)
                {
                    ComContext_->Release();
                    ComContext_ = nullptr;
                }
                ComContext_ = Com;
                if (Com != nullptr)
                    Com->AddRef();
            }
            if (ComContext_ != nullptr)
            {
                ComContext_->AddRef();
                return GR_OK;
            }
            return GR_GENERIC;
        }

        GENRESULT DACOM_API Slot41SetLoadParser(IThornLoadParser* Parser) override
        {
            LoadParser_ = Parser;
            return GR_OK;
        }

        GENRESULT DACOM_API Slot42SetSaveParser(IThornSaveParser* Parser) override
        {
            SaveParser_ = Parser;
            return GR_OK;
        }

        GENRESULT DACOM_API Slot43LoadPath(const char* Path) override
        {
            // LAB_06f38110: FileSystem read 0x80000000 / creation 3 → slot 4. NULL path still slot 4.
            // The path is first stored into the +0xc basic_string
            // (`LEA EBX,[this+0xc]` ... basic_string::assign(Path, strlen))
            // -- only on the non-NULL branch.
            IFileSystem* FileSystem = nullptr;
            if (Path != nullptr)
            {
                ScriptPath_ = Path;
                FFileSystemDesc Desc = {};
                Desc.Size = 0x34;
                Desc.ComponentName = "FileSystem";
                Desc.Path = Path;
                Desc.Access = 0x80000000;
                Desc.ShareMode = 1;
                Desc.Creation = 3;
                Desc.Flags = 0x08000080;
                Desc.Field30 = 0xFFFFFFFF;
                ICOManager* Manager = DACOM_Acquire();
                const GENRESULT Result = Manager->CreateInstance(
                    reinterpret_cast<AGGDESC*>(&Desc),
                    reinterpret_cast<void**>(&FileSystem));
                if (Result < 0)
                    return Result;
            }
            const GENRESULT Hr = Slot4Load(FileSystem);
            if (FileSystem != nullptr)
                FileSystem->Release();
            return Hr;
        }

        GENRESULT DACOM_API Slot44SavePath(const char* Path) override
        {
            // LAB_06f38590: no null path gate, factory HRESULT controls Save;
            // Release follows on both HRESULT paths, with no Win32 fallback.
            ScriptPath_ = Path;
            FFileSystemDesc Desc = {};
            Desc.Size = 0x34;
            Desc.ComponentName = "FileSystem";
            Desc.Path = ScriptPath_.c_str();
            Desc.Access = 0xC0000000;
            Desc.ShareMode = 1;
            Desc.Creation = 2;
            Desc.Flags = 0x08000080;
            Desc.Field30 = 0xFFFFFFFF;
            IFileSystem* FileSystem;
            ICOManager* Manager = DACOM_Acquire();
            GENRESULT Result = Manager->CreateInstance(reinterpret_cast<AGGDESC*>(&Desc),
                                                       reinterpret_cast<void**>(&FileSystem));
            if (Result >= 0)
                Result = Slot5Save(FileSystem);
            FileSystem->Release();
            return Result;
        }

        // IGeoTransformControl facet (+4, .rdata:06f5ad34). QueryInterface / AddRef /
        // Release are the overrides above (retail FUN_06f3f7a0 / 06f3f7c0 / 06f3f7e0 forward
        // to the controlling unknown for every facet).

        // Slot 3, LAB_06f3b530: snapshot the inverse csys, raw-copy three dwords into the
        // forward position, rebuild the inverse and push it to the bound entities.
        GENRESULT DACOM_API SetPosition(float* Position) override
        {
            FTransform OldInverse;
            std::memcpy(&OldInverse, SceneCsys(), sizeof(OldInverse));
            const float* Source = Position;
            // The three raw forward stores are sequential, matching overlapping input.
            for (int Index = 0; Index < 3; ++Index)
            {
                unsigned int Word;
                std::memcpy(&Word, Source + Index, sizeof(Word));
                std::memcpy(ScenePos() + Index, &Word, sizeof(Word));
            }
            RebuildSceneCsys();
            Scene_.PushBoundGeoFromScene(OldInverse, false);
            return GR_OK;
        }

        // Slot 4, LAB_06f3b6c0: the same for the nine orientation dwords (REP MOVSD).
        GENRESULT DACOM_API SetOrientation(float* Orient9) override
        {
            FTransform OldInverse;
            std::memcpy(&OldInverse, SceneCsys(), sizeof(OldInverse));
            const float* Source = Orient9;
            // REP9 is a forward raw copy, with no floating-point conversion.
            for (int Index = 0; Index < 9; ++Index)
            {
                unsigned int Word;
                std::memcpy(&Word, Source + Index, sizeof(Word));
                std::memcpy(SceneOrient() + Index, &Word, sizeof(Word));
            }
            RebuildSceneCsys();
            Scene_.PushBoundGeoFromScene(OldInverse, true);
            return GR_OK;
        }

        // Slot 5, LAB_06f3b860: three dwords from facet+0x54 into *OutPosition, return 0,
        // RET 8. Nothing is null-tested.
        GENRESULT DACOM_API GetPosition(float* OutPosition) override
        {
            std::memcpy(OutPosition, ScenePos(), 3 * sizeof(float));
            return GR_OK;
        }

        // Slot 6, LAB_06f3b880: nine dwords from facet+0x30 (REP MOVSD), return 0, RET 8.
        GENRESULT DACOM_API GetOrientation(float* OutOrient9) override
        {
            std::memcpy(OutOrient9, SceneOrient(), 9 * sizeof(float));
            return GR_OK;
        }

        // IScriptControl facet (+8, .rdata:06f5ad18). Each slot is an adjustor thunk:
        // `this -= 8; JMP [vtbl + n]` into this object's IScriptEngine slot.
        // Slot 3, LAB_06f3c080 -> +0x58, slot 22.
        GENRESULT DACOM_API Slot3Start(int Arg) override { return Slot22Start(Arg); }
        // Slot 4, LAB_06f3c0a0 -> +0x68, slot 26.
        GENRESULT DACOM_API Slot4Tick(int DtMs) override { return Slot26Tick(DtMs); }
        // Slot 5, LAB_06f3c0c0 -> +0x5c, slot 23.
        GENRESULT DACOM_API Slot5Stop() override { return Slot23Stop(); }
        // Slot 6, LAB_06f3c0e0 -> +0x44, slot 17.
        int DACOM_API Slot6GetDuration() override { return Slot17GetDuration(); }

        float* ScenePos() { return Scene_.ForwardCsys().Pos; }
        float* SceneOrient() { return Scene_.ForwardCsys().Orient; }
        FThornScene& Scene() { return Scene_; }

        void RebuildSceneCsys()
        {
            // Retail 3b9a0 calls foreign math+0x38, then 23090/3b670 pack inverse.
            Scene_.BuildInverseCsys(SceneCsys_);
        }

        const float* SceneCsys() const { return SceneCsys_; }

    private:
        // Retail +0x108 controlling unknown / +0x10c embedded inner unknown;
        // the object's reference count lives in Inner_ exactly as in retail.
        IDAComponent* Outer_;
        FThornInnerUnknown Inner_;
        // Retail +0xc basic_string: the last path handed to LoadPath/SavePath.
        std::string ScriptPath_;
        IThornComContext* ComContext_;
        // Reverse member destruction: scene teardown, late context Release, path string.
        FThornContextTailRelease ContextTailRelease_;
        FThornScene Scene_;
        float SceneCsys_[12];
        IThornLoadParser* LoadParser_;
        IThornSaveParser* SaveParser_;
    };

    enum EThornFactoryKind
    {
        KindScriptEngine,
        KindParamCurve,
        KindSplinePos,
        KindSplineOrient,
    };

    class FThornFactory final : public IComponentFactory
    {
    public:
        FThornFactory(const char* Name, EThornFactoryKind Kind, bool bDeadSecondLinear)
            : RefCount_(1), Name_(Name), Kind_(Kind), bDeadSecondLinear_(bDeadSecondLinear)
        {
        }

        const char* Name() const { return Name_; }

        GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override
        {
            *Out = nullptr;
            if (!FactoryIidEqual(Iid))
                return GR_INTERFACE_UNSUPPORTED;
            *Out = static_cast<IComponentFactory*>(this);
            AddRef();
            return GR_OK;
        }

        unsigned long DACOM_API AddRef() override { return ++RefCount_; }

        unsigned long DACOM_API Release() override
        {
            if (RefCount_ > 0)
                --RefCount_;
            const unsigned long Next = RefCount_;
            if (Next == 0)
            {
                RefCount_ = 1;
                delete this;
            }
            return Next;
        }

        // LAB_06f37400 (CV_CRSplinePath / CV_CROrientationSplinePath,
        // listing 43219-43286) and LAB_06f3f4c0 (IScriptEngine, listing
        // 51466-51535) share one shape (stdcall, RET 0xc):
        //   [Desc] != 0x14 || strcmp([Desc+4], [factory+8]) != 0
        //                              -> *Instance = 0, return -3
        //   operator_new(0x80) / calloc(1, 0x118) == 0
        //                              -> *Instance = 0, return -4
        //   ctor (FUN_06f374d0 / FUN_06f37b50)
        //   [obj+0x70] / [obj+0x108] = [Desc+8] (Outer) if non-NULL,
        //                              else &obj->inner (+0x74 / +0x10c)
        //   if (Outer) *[Desc+0xc] = &obj->inner   (retail derefs [Desc+0xc]
        //                              without a NULL test)
        //   FUN_06f34830 / FUN_06f3b520 post-init are `XOR EAX,EAX; RET` stubs
        //   *Instance = obj; return 0
        // The name compare is a byte-exact strcmp loop (no case folding);
        // every in-tree caller (ThornLua.cpp, harness/ThornHarness.cpp)
        // already passes the exact-case names.
        GENRESULT DACOM_API CreateInstance(AGGDESC* Desc, void** Instance) override
        {
            if (Desc->size != G_AggDescSizeThorn)
            {
                *Instance = nullptr;
                return GR_INTERFACE_UNSUPPORTED;
            }

            FThornAggDesc14* Extended = reinterpret_cast<FThornAggDesc14*>(Desc);
            if (std::strcmp(Extended->NameAt4, Name_) != 0)
            {
                *Instance = nullptr;
                return GR_INTERFACE_UNSUPPORTED;
            }

            IDAComponent* Object = nullptr;
            IDAComponent* Inner = nullptr;
            if (Kind_ == KindScriptEngine)
            {
                FScriptEngine* Engine = new (std::nothrow) FScriptEngine();
                if (Engine == nullptr)
                {
                    *Instance = nullptr;
                    return GR_OUT_OF_MEMORY;
                }
                Inner = Engine->Aggregate(Extended->Outer);
                Object = static_cast<IScriptEngine*>(Engine);
            }
            else if (Kind_ == KindParamCurve)
            {
                // Raw PE 06f31e70-06f31f6a and its eleven factory clones:
                // neither Desc+8 nor Desc+0xc is read; curves do not aggregate.
                Object = ThornCreateParamCurve(Name_, bDeadSecondLinear_);
                if (Object == nullptr)
                {
                    *Instance = nullptr;
                    return GR_OUT_OF_MEMORY;
                }
            }
            else if (Kind_ == KindSplinePos || Kind_ == KindSplineOrient)
            {
                // LAB_06f37400: only allocation failure returns -4 here;
                // math is acquired lazily by spline operations.
                Object = ThornCreateSplinePath(Name_, Kind_ == KindSplineOrient, Extended->Outer,
                                               &Inner);
                if (Object == nullptr)
                {
                    *Instance = nullptr;
                    return GR_OUT_OF_MEMORY;
                }
            }
            else
            {
                return GR_GENERIC;
            }

            if (Kind_ != KindParamCurve && Extended->Outer != nullptr)
                *Extended->InnerOut = Inner;

            *Instance = Object;
            return GR_OK;
        }

    private:
        unsigned long RefCount_;
        const char* Name_;
        EThornFactoryKind Kind_;
        bool bDeadSecondLinear_;
    };

    struct FThornFactoryWarning
    {
        const char* Message;
        int Line;
    };

    struct FThornCurveRegistration
    {
        const char* Name;
        FThornFactoryWarning RegisterFailure;
        FThornFactoryWarning AllocationFailure;
    };

    const char* const G_ParamCurveRegistrationSource = "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\THORN\\ParamCurve.cpp";
    const char* const G_SplineRegistrationSource = "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\THORN\\SplinePath.cpp";
    const char* const G_ThornRegistrationSource = "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\THORN\\Thorn.cpp";

    const FThornCurveRegistration G_ParamCurveRegistrations[] = {
        {"CatmullRomPCurve",
         {"ParamCurve: DllMain: unable to register CLSID_CR_PCurve component!\n", 852},
         {"ParamCurve: DllMain: unable to create CLSID_CR_PCurve component factory!\n", 858}},
        {"ThornParamCurve",
         {"ParamCurve: DllMain: unable to register CLSID_THORN_CR_PCurve component!\n", 865},
         {"ParamCurve: DllMain: unable to create CLSID_CR_PCurve component factory!\n", 871}},
        {"LinearPCurve",
         {"ParamCurve: DllMain: unable to register CLSID_LinearPCurve component!\n", 878},
         {"ParamCurve: DllMain: unable to create CLSID_LinearPCurve component factory!\n", 884}},
        {"ThornLPCurve",
         {"ParamCurve: DllMain: unable to register CLSID_ThornLPCurve component!\n", 891},
         {"ParamCurve: DllMain: unable to create CLSID_LinearPCurve component factory!\n", 897}},
        {"SmoothPCurve",
         {"ParamCurve: DllMain: unable to register CLSID_Smooth_PCurve component!\n", 904},
         {"ParamCurve: DllMain: unable to create CLSID_Smooth_PCurve component factory!\n", 910}},
        {"StepPCurve",
         {"ParamCurve: DllMain: unable to register CLSID_Step_PCurve component!\n", 917},
         {"ParamCurve: DllMain: unable to create CLSID_Step_PCurve component factory!\n", 923}},
        {"LinearPCurve",
         {"ParamCurve: DllMain: unable to register CLSID_Linear_PCurve component!\n", 930},
         {"ParamCurve: DllMain: unable to create CLSID_Linear_PCurve component factory!\n", 936}},
        {"RampUpPCurve",
         {"ParamCurve: DllMain: unable to register CLSID_RampUp_PCurve component!\n", 943},
         {"ParamCurve: DllMain: unable to create CLSID_Linear_PCurve component factory!\n", 949}},
        {"RampDownPCurve",
         {"ParamCurve: DllMain: unable to register CLSID_RampDown_PCurve component!\n", 956},
         {"ParamCurve: DllMain: unable to create CLSID_RampDown_PCurve component factory!\n", 962}},
        {"BumpOutPCurve",
         {"ParamCurve: DllMain: unable to register CLSID_BumpOut_PCurve component!\n", 969},
         {"ParamCurve: DllMain: unable to create CLSID_BumpOut_PCurve component factory!\n", 975}},
        {"BumpInPCurve",
         {"ParamCurve: DllMain: unable to register CLSID_BumpIn_PCurve component!\n", 982},
         {"ParamCurve: DllMain: unable to create CLSID_BumpIn_PCurve component factory!\n", 988}},
        {"FreeFormPCurve",
         {"ParamCurve: DllMain: unable to register CLSID_FreeForm_PCurve component!\n", 995},
         {"ParamCurve: DllMain: unable to create CLSID_FreeForm_PCurve component factory!\n", 1001}},
    };

    const char* const G_SplineNames[] = {
        "CV_CRSplinePath",
        "CV_CROrientationSplinePath",
    };

    void RegisterOneFactory(const char* Name, EThornFactoryKind Kind,
                            bool bDeadSecondLinear, const FThornCurveRegistration* Warnings)
    {
        FThornFactory* Factory =
            new (std::nothrow) FThornFactory(Name, Kind, bDeadSecondLinear);
        if (Factory == nullptr)
        {
            if (Warnings != nullptr)
                FDUMP(kFdumpWarning, "%s(%d) : WARNING:General:%s",
                      G_ParamCurveRegistrationSource, Warnings->AllocationFailure.Line,
                      Warnings->AllocationFailure.Message);
            return;
        }

        ICOManager* Manager = DACOM_Acquire();
        const GENRESULT Result =
            Manager->RegisterComponent(Factory, Name, reinterpret_cast<void*>(G_ThornRegisterPriority));
        if (Result != GR_OK && Warnings != nullptr)
            FDUMP(kFdumpWarning, "%s(%d) : WARNING:General:%s",
                  G_ParamCurveRegistrationSource, Warnings->RegisterFailure.Line,
                  Warnings->RegisterFailure.Message);
        Factory->Release();
    }

    void RegisterParamCurveComponents()
    {
        ICOManager* Manager = DACOM_Acquire();
        if (Manager == nullptr)
        {
            FDUMP(kFdumpWarning, "%s(%d) : WARNING:General:%s",
                  G_ParamCurveRegistrationSource, 0x34c, "register_param_curve_components: unable to get DACOM!\n");
            return;
        }

        const int Count = static_cast<int>(sizeof(G_ParamCurveRegistrations) / sizeof(G_ParamCurveRegistrations[0]));
        for (int Index = 0; Index < Count; ++Index)
        {
            const FThornCurveRegistration& Registration = G_ParamCurveRegistrations[Index];
            const bool bDeadSecondLinear = Index == 6;
            RegisterOneFactory(Registration.Name, KindParamCurve, bDeadSecondLinear, &Registration);
        }
    }

    void RegisterSplinePathComponents()
    {
        ICOManager* Manager = DACOM_Acquire();
        if (Manager == nullptr)
        {
            FDUMP(kFdumpWarning, "%s(%d) : WARNING:General:%s",
                  G_SplineRegistrationSource, 0x2bc, "register_spline_path_components: unable to get DACOM!\n");
            return;
        }

        const int Count = static_cast<int>(sizeof(G_SplineNames) / sizeof(G_SplineNames[0]));
        for (int Index = 0; Index < Count; ++Index)
        {
            const EThornFactoryKind Kind = Index == 0 ? KindSplinePos : KindSplineOrient;
            RegisterOneFactory(G_SplineNames[Index], Kind, false, nullptr);
        }
    }

    void RegisterThornComponents()
    {
        ICOManager* Manager = DACOM_Acquire();
        if (Manager == nullptr)
        {
            FDUMP(kFdumpWarning, "%s(%d) : WARNING:General:%s",
                  G_ThornRegistrationSource, 0x6a6, "Thorn: DllMain: unable to get DACOM!\n");
            return;
        }

        FThornFactory* ScriptFactory =
            new (std::nothrow) FThornFactory(G_NameScriptEngine, KindScriptEngine, false);
        if (ScriptFactory != nullptr)
        {
            Manager = DACOM_Acquire();
            Manager->RegisterComponent(ScriptFactory, G_NameScriptEngine,
                                       reinterpret_cast<void*>(G_ThornRegisterPriority));
            ScriptFactory->Release();
            ThornLuaOpen();
        }

        RegisterParamCurveComponents();
        RegisterSplinePathComponents();
    }
}

BOOL WINAPI DllMain(HINSTANCE Instance, DWORD Reason, LPVOID /*Reserved*/)
{
    if (Reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(Instance);
        RegisterThornComponents();
    }
    else if (Reason == DLL_PROCESS_DETACH)
    {
        ThornLuaClose();
    }
    return TRUE;
}
