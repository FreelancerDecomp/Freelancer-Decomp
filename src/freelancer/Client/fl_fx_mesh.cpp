// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "fl_fx_mesh.h"

#include "client_cliobj.h"     // Archetype::ReleaseMaterialEntrySlot (FUN_0041b030)
#include "client_mesh_load.h"  // FMeshHolder (retail ObjectHandle) and its FUN_00420xxx helpers

#include "alchemy.h"           // IFxMesh, IFxMeshFactory
#include "common_archetype.h"  // Archetype::Load
#include "DALib.h"
#include "dacom.h"
#include "engine/engbase.h"
#include "render/IRenderPipeline.h"

#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <new>

IFxMeshFactory* G_FLFxMeshFactory = nullptr;

namespace
{
    // Retail keeps DAT_00674cdc as a DACOM smart pointer: static initializer LAB_004f6f80 zeroes
    // it and registers LAB_004f6fa0 through atexit (FUN_005b7e6c), which releases a held factory
    // through vt+8 and zeroes the slot. This guard's destructor is that atexit entry.
    struct FFxMeshFactoryAtExit
    {
        ~FFxMeshFactoryAtExit()
        {
            if (G_FLFxMeshFactory != nullptr)
            {
                G_FLFxMeshFactory->Release();
                G_FLFxMeshFactory = nullptr;
            }
        }
    };
    FFxMeshFactoryAtExit G_FxMeshFactoryAtExit;

    // FUN_004f6ac0's destination: a length and 32 characters, 0x24 bytes.
    struct FFxMeshPath
    {
        unsigned Length; // +0x00
        char Text[32];   // +0x04
    };
    static_assert(sizeof(FFxMeshPath) == 0x24, "FUN_004f6ac0 string is 0x24 bytes");

    // s_fx\misc\%s.3db_005d98d0.
    char const kFxMeshPathFormat[] = "fx\\misc\\%s.3db";

    // FUN_004f6ac0 (cdecl Out, Format, ...): vsprintf into a 0x20-byte local, the length capped
    // at 0x1f, that many characters copied to Out+4 and terminated, the length stored at +0.
    void FxMeshPathFormat(FFxMeshPath* Out, char const* Format, ...)
    {
        char Buffer[0x20];
        va_list Args;
        va_start(Args, Format);
        vsprintf(Buffer, Format, Args);
        va_end(Args);
        size_t Length = strlen(Buffer);
        if (Length >= 0x20)
            Length = 0x1f;
        memcpy(Out->Text, Buffer, Length);
        Out->Text[Length] = '\0';
        Out->Length = static_cast<unsigned>(Length);
    }

    // The two render-state guards FUN_004f6970 keeps on its stack (EH states 0 and 1): the
    // pipeline, the state and its value read at construction (IRenderPipeline slot 61 +0xf4),
    // written back on destruction (slot 60 +0xf0).
    struct FFxMeshRenderState
    {
        IRenderPipeline* Pipe;
        int State;
        int Saved;

        explicit FFxMeshRenderState(int InState)
            : Pipe(DALib::Pipeline), State(InState), Saved(0)
        {
            Pipe->GetRenderState((D3DRENDERSTATETYPE)State, &Saved);
        }

        ~FFxMeshRenderState()
        {
            Pipe->SetRenderState((D3DRENDERSTATETYPE)State, Saved);
        }
    };

    struct FLFxMesh : IFxMesh, FMeshHolder
    {
        ~FLFxMesh() override;

        GENRESULT DACOM_API Slot3(void* Transform) override;
        float* DACOM_API Slot4() override;
        GENRESULT DACOM_API Slot5(void* Arg) override;
        GENRESULT DACOM_API Slot6(void* Camera) override;
        GENRESULT DACOM_API Slot7() override;
        GENRESULT DACOM_API Slot8(void* Arg) override;
    };
    static_assert(offsetof(FLFxMesh, EngineInstance) == 0x08, "FLFxMesh +0x04 ObjectHandle base");
    static_assert(sizeof(FLFxMesh) == 0x14, "FLFxMesh base is 0x14");

    struct FLFxMeshComponent : FLFxMesh
    {
        unsigned long RefCount; // +0x14 DAComponent<FLFxMesh>

        FLFxMeshComponent() : RefCount(1) {}

        GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
        unsigned long DACOM_API AddRef() override;
        unsigned long DACOM_API Release() override;
    };
    static_assert(offsetof(FLFxMeshComponent, EngineInstance) == 0x08, "FLFxMeshComponent instance +0x08");
    static_assert(offsetof(FLFxMeshComponent, RefCount) == 0x14, "FLFxMeshComponent reference count +0x14");
    static_assert(sizeof(FLFxMeshComponent) == 0x18, "FLFxMeshComponent is operator new(0x18)");

    FLFxMesh::~FLFxMesh()
    {
        // FUN_004f6b50 (entered on the ObjectHandle face): the abstract vtables (005d991c /
        // 005d9914), then FUN_004f6a70 called directly (not through the vtable), then
        // FUN_00420d10 on the handle, which only restores its own vtable.
        FLFxMesh::Slot7();
    }

    GENRESULT DACOM_API FLFxMeshComponent::QueryInterface(const char* Iid, void** Out)
    {
        // LAB_004f6c20: the one-entry table at 005d9960 {"1.11_IFxMesh" (005d9958), offset 0}.
        // A hit AddRefs the found face through its slot 1 and hands it out; a miss nulls *Out
        // and returns 0xfffffffd.
        if (strcmp(Iids::FxMesh, Iid) == 0)
        {
            AddRef();
            *Out = static_cast<IFxMesh*>(this);
            return GR_OK;
        }
        *Out = nullptr;
        return GR_INTERFACE_UNSUPPORTED;
    }

    unsigned long DACOM_API FLFxMeshComponent::AddRef()
    {
        // LAB_004f6cd0.
        RefCount += 1;
        return RefCount;
    }

    unsigned long DACOM_API FLFxMeshComponent::Release()
    {
        // LAB_004f6ce0: an unsigned decrement that stops at zero; at zero the count is set back
        // to 1 and the ObjectHandle face's slot 0 (FUN_004f6bc0, the scalar deleting destructor)
        // runs with 1, returning 0.
        if (RefCount > 0)
            RefCount -= 1;
        if (RefCount != 0)
            return RefCount;
        RefCount = 1;
        delete this;
        return 0;
    }

    GENRESULT DACOM_API FLFxMesh::Slot3(void* Transform)
    {
        // LAB_004f6910 (RET 8): FUN_00421050 on the handle.
        ClientMeshHolderSetMatrix(this, Transform);
        return GR_OK;
    }

    float* DACOM_API FLFxMesh::Slot4()
    {
        // LAB_004f6930 (RET 4): FUN_00421030 on the handle, its result returned.
        return ClientMeshHolderGetTransform(this);
    }

    GENRESULT DACOM_API FLFxMesh::Slot5(void* Arg)
    {
        // LAB_004f6940 (RET 8): DALib::Engine slot 27 UpdateInstance(+0x08, 0, Arg).
        DALib::Engine->UpdateInstance(EngineInstance, 0, Arg);
        return GR_OK;
    }

    GENRESULT DACOM_API FLFxMesh::Slot6(void* Camera)
    {
        // FUN_004f6970 (RET 8). 0x004f6988..0x004f69bf: D3DRS_ZWRITEENABLE (0xe) saved and set
        // to 0.
        FFxMeshRenderState ZWrite(0xe);
        ZWrite.Pipe->SetRenderState((D3DRENDERSTATETYPE)ZWrite.State, 0);

        // 0x004f69bf..0x004f69fe: D3DRS_ZFUNC (0x17) saved and set to 4 (D3DCMP_LESSEQUAL).
        FFxMeshRenderState ZFunc(0x17);
        ZFunc.Pipe->SetRenderState((D3DRENDERSTATETYPE)ZFunc.State, 4);

        // 0x004f69fe..0x004f6a14: FUN_00421120 on the handle with the camera and 0x100.
        ClientMeshHolderRender(this, Camera, 0x100u);

        // 0x004f6a19..0x004f6a4e: the guards restore ZFUNC, then ZWRITEENABLE.
        return GR_OK;
    }

    GENRESULT DACOM_API FLFxMesh::Slot7()
    {
        // FUN_004f6a70 (RET 4): FUN_0041b030 on the material entry (+0x0c; the auxiliary one at
        // +0x10 is left alone), then a bound instance (+0x08 != -1) destroyed through
        // DALib::Engine slot 24 and set back to -1.
        Archetype::ReleaseMaterialEntrySlot(&MaterialEntry);
        void* const NoInstance = reinterpret_cast<void*>(static_cast<ptrdiff_t>(-1));
        if (EngineInstance != NoInstance)
        {
            DALib::Engine->DestroyInstance(EngineInstance);
            EngineInstance = NoInstance;
        }
        return GR_OK;
    }

    GENRESULT DACOM_API FLFxMesh::Slot8(void* /*Arg*/)
    {
        // LAB_004f6ab0: `OR EAX,0xffffffff; RET 8`.
        return GR_GENERIC;
    }

    struct FLFxMeshFactory : IFxMeshFactory
    {
        virtual ~FLFxMeshFactory() {} // FUN_004f6bf0; private implementation slot 4.

        GENRESULT DACOM_API Slot3(void* ArgA, void* ArgB, float const* Position,
                                  IFxMesh** Out) override;
    };
    static_assert(sizeof(FLFxMeshFactory) == 0x4, "FLFxMeshFactory abstract base is 0x4");

    struct FLFxMeshFactoryComponent : FLFxMeshFactory
    {
        unsigned long RefCount; // +0x04 DAComponent<FLFxMeshFactory>

        FLFxMeshFactoryComponent() : RefCount(1) {}

        GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
        unsigned long DACOM_API AddRef() override;
        unsigned long DACOM_API Release() override;
    };
    static_assert(offsetof(FLFxMeshFactoryComponent, RefCount) == 0x4, "FLFxMeshFactoryComponent reference count +0x4");
    static_assert(sizeof(FLFxMeshFactoryComponent) == 0x8, "FLFxMeshFactoryComponent is operator new(8)");

    GENRESULT DACOM_API FLFxMeshFactoryComponent::QueryInterface(const char* Iid, void** Out)
    {
        // LAB_004f6d10: the one-entry table at 005d9990 {"1.11_IFxMeshFactory" (005d9978),
        // offset 0}; the same hit / miss handling as FLFxMesh's.
        if (strcmp(Iids::FxMeshFactory, Iid) == 0)
        {
            AddRef();
            *Out = static_cast<IFxMeshFactory*>(this);
            return GR_OK;
        }
        *Out = nullptr;
        return GR_INTERFACE_UNSUPPORTED;
    }

    unsigned long DACOM_API FLFxMeshFactoryComponent::AddRef()
    {
        // LAB_0051e8e0 (shared with other DAComponent<> instantiations whose count is at +4).
        RefCount += 1;
        return RefCount;
    }

    unsigned long DACOM_API FLFxMeshFactoryComponent::Release()
    {
        // LAB_004f6dc0: as FLFxMesh::Release, but the destructor is this face's own slot 4
        // (FUN_004f6b30: FUN_004f6bf0 restores the abstract vtable 005d9944, then the free).
        if (RefCount > 0)
            RefCount -= 1;
        if (RefCount != 0)
            return RefCount;
        RefCount = 1;
        delete this;
        return 0;
    }

    GENRESULT DACOM_API FLFxMeshFactory::Slot3(void* /*ArgA*/, void* /*ArgB*/,
                                               float const* Position, IFxMesh** Out)
    {
        // FUN_004f6810 (RET 0x14). ArgA and ArgB (alchemy's appearance +0x60 / +0x74) are never
        // read. 0x004f682b..0x004f6868: operator new(0x18) and the inlined constructor; a failed
        // allocation leaves a null mesh that the success path below would dereference, as in
        // retail.
        FLFxMeshComponent* const Mesh = new (std::nothrow) FLFxMeshComponent;

        // 0x004f686a..0x004f6899: the path. Retail formats "fx\misc\%s.3db" with the path
        // object itself -- still empty (length 0) -- copied by value as the variadic argument,
        // so %s reads its first dword, 0, as the string: the name is always "(null)". The port
        // passes the object the same way.
        FFxMeshPath Path;
        Path.Length = 0;
        Path.Text[0] = '\0';
        FxMeshPathFormat(&Path, kFxMeshPathFormat, Path);

        // 0x004f689e..0x004f68b5: Archetype::Load(path, &handle, 0); failure returns 0 with *Out
        // untouched and the new mesh never released.
        long ArchHandle = 0;
        if (!Archetype::Load(Path.Text, &ArchHandle, nullptr))
            return GR_OK;

        // 0x004f68b7..0x004f68f6: *Out = the mesh; FUN_00420f30(path, 0) on its handle,
        // FUN_00420d70(handle), FUN_00420fd0(Position), then DALib::Engine slot 15
        // ReleaseHandle on the archetype handle.
        *Out = Mesh;
        void* const Loaded = reinterpret_cast<void*>(static_cast<ptrdiff_t>(ArchHandle));
        ClientMeshHolderSetMaterials(Mesh, Path.Text, nullptr);
        ClientMeshHolderBindInstance(Mesh, Loaded);
        ClientMeshHolderSetPosition(Mesh, Position);
        DALib::Engine->ReleaseHandle(Loaded);
        return GR_OK;
    }
}

bool FLFxMeshFactoryCreate(IFxMeshFactory** Out)
{
    // FUN_004f67e0: operator new(8); non-null -> vtable 005d98bc, +4 = 1, *Out = it; null ->
    // *Out = 0. AL = 1 on both paths.
    FLFxMeshFactoryComponent* const Factory = new (std::nothrow) FLFxMeshFactoryComponent;
    *Out = Factory;
    return true;
}
