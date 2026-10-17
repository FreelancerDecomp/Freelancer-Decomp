// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "RendComp.h"
#include "readfile.h"
#include "render/shading.h"
#include "x86math.h"
#include "common_misc.h" // ICamera::GetViewTransform (+0x40), read by PipelineApplySubmit

#include <cstdio>
#include <cstring>
#include <new>

#pragma comment(lib, "dacom.lib")

IRenderPipeline* G_Pipeline = nullptr;
IRPDraw* G_Draw = nullptr;
IRPIndexBuffer* G_IndexBuffer = nullptr;
IRPVertexBuffer* G_VertexBuffer = nullptr;
IVertexBufferManager* G_VbManager = nullptr;
IMaterialLibrary* G_MaterialLibrary = nullptr;
IEngine* G_Engine = nullptr;

// The per-caller 3DMathEngine caches (RendComp.h, MathEngineCached).
I3DMathEngine* G_MathTriMeshDraw = nullptr; // DAT_06c37cc0
I3DMathEngine* G_MathSphere = nullptr;      // DAT_06c37cc8
I3DMathEngine* G_MathVMesh = nullptr;       // DAT_06c37d44
I3DMathEngine* G_MathRenderMgr = nullptr;   // DAT_06c37d48

struct FRendCompXform
{
    float Rotation[3][3];
    float Translation[3];
};

// IRenderPipeline's matrix slots take the bare 12 floats. FRendCompXform is exactly that
// layout and nothing else, so this is a view of the same storage, not a conversion.
static_assert(sizeof(FRendCompXform) == 12 * sizeof(float), "FRendCompXform is 12 floats");

inline const float* AsFloat12(const FRendCompXform* Xform)
{
    return (const float*)Xform;
}

inline float* AsFloat12(FRendCompXform* Xform)
{
    return (float*)Xform;
}

// LogStream::LogEvent is dacom.dll's export (dacom.def, declared in dacom.h); the local
// definition that used to sit here shadowed it with a log-file writer.

// Retail pushes (Message, Line, __FILE__, fmt, Flags) and calls through the FDUMP import with
// no null check (e.g. FUN_06c2dd20 @ 06c2de02..06c2de1f).
void RendCompFdumpAt(int Flags, const char* File, int Line, const char* Message)
{
    const char* Format = G_FdumpFmtNotice;
    if (Flags == kFdumpError)
        Format = G_FdumpFmtError;
    else if (Flags == kFdumpWarning)
        Format = G_FdumpFmtWarning;
    FDUMP(Flags, Format, File, Line, Message);
}

bool RendCompIidEqual(const char* Left, const char* Right)
{
    if (Left == nullptr || Right == nullptr)
        return false;
    return std::strcmp(Left, Right) == 0;
}

// RendComp carries the FileSystem component as void*; this is the single place that says
// what it actually is, so every helper below calls ReadFile's published interface.
static IFileSystem* AsFileSystem(void* File)
{
    return (IFileSystem*)File;
}

int FileOpenNode(void* File, const char* Name)
{
    if (File == nullptr || Name == nullptr)
        return 0;
    return AsFileSystem(File)->OpenNode(Name); // +0x60
}

// Retail closes every handle it opened with vt+0x10 directly (e.g. FUN_06c249d0 after
// its vt+0x7c open). There is no "handle 0 means leave the node" rule and no null test:
// ReadFile's handle 0 names the current file, and an open never returns it.
int FileClose(void* File, int Handle)
{
    return AsFileSystem(File)->CloseFile((unsigned long)Handle); // +0x10
}

int FileRead(void* File, int Handle, void* Buffer, unsigned long Size, unsigned long* ReadOut)
{
    if (File == nullptr)
        return -1;
    return AsFileSystem(File)->Read((unsigned long)Handle, Buffer, Size,
                                    ReadOut, 0); // +0x14
}

unsigned long FileGetSize(void* File, int Handle)
{
    if (File == nullptr)
        return 0;
    return AsFileSystem(File)->GetSize((unsigned long)Handle, 0); // +0x28
}

int FileGetCurrentPath(void* File, char* Out, unsigned long MaxChars)
{
    if (File == nullptr || Out == nullptr || MaxChars == 0)
        return 0;
    Out[0] = '\0';
    return AsFileSystem(File)->GetCurrentPath(MaxChars, Out); // +0x5C
}

int FileOpenByName(void* File, const char* ChildName)
{
    if (File == nullptr || ChildName == nullptr)
        return -1;
    struct FOpenDesc
    {
        unsigned long Size;
        const char* ComponentName;
        const char* BackendName;
        const char* DataPath;
        unsigned long Field10;
        unsigned long Field14;
        unsigned long Field18;
        unsigned long Field1C;
        unsigned long Field20;
        unsigned long Field24;
        unsigned long Field28;
        unsigned long Field2C;
        unsigned long Field30;
    };
    FOpenDesc Desc;
    std::memset(&Desc, 0, sizeof(Desc));
    Desc.Size = 0x34;
    Desc.ComponentName = "FileSystem";
    Desc.DataPath = ChildName;
    Desc.Field10 = 0x80000000;
    Desc.Field14 = 1;
    Desc.Field1C = 3;
    Desc.Field20 = 0x8000080;
    Desc.Field30 = 0xFFFFFFFF;
    const int Handle =
        AsFileSystem(File)->OpenByDesc((FFileSystemDesc*)(&Desc)); // +0x7C
    if (Handle != -1)
        return Handle;
    // UTF +0x7C is a stub: OpenNode + current handle 0 (EngBase FileSystemReadNamed).
    if (FileOpenNode(File, ChildName) == 0)
        return -1;
    return 0;
}

// FUN_06c24980 (thiscall desc, RET 8): +4 = ComponentName, +8..+0x33 zeroed, Access 0x80000000,
// ShareMode 1, Creation 3, Flags 0x8000080, Path, Size 0x34, +0x30 = -1.
FFileSystemDesc* FileDescInit(FFileSystemDesc* Desc, const char* Path, const char* ComponentName)
{
    std::memset(Desc, 0, sizeof(FFileSystemDesc));
    Desc->ComponentName = ComponentName;
    Desc->Access = 0x80000000;
    Desc->ShareMode = 1;
    Desc->Creation = 3;
    Desc->Flags = 0x8000080;
    Desc->Path = Path;
    Desc->Size = 0x34;
    Desc->Field30 = 0xFFFFFFFF;
    return Desc;
}

void* QueryIid(IDAComponent* Object, const char* Iid)
{
    if (Object == nullptr || Iid == nullptr)
        return nullptr;
    void* Out = nullptr;
    if (Object->QueryInterface(Iid, &Out) != GR_OK)
        return nullptr;
    return Out;
}

void ReleaseIf(IDAComponent* Object)
{
    if (Object != nullptr)
        Object->Release();
}

// FUN_06c34e60 (06c34e60..06c34e98): desc at ESP+4 {0xC, "3DMathEngine", 0}, out at ESP+0;
// DACOM_Acquire's result is dereferenced unchecked and the CreateInstance result is ignored.
// Retail's out slot is uninitialised stack; the port's starts at nullptr.
I3DMathEngine* AcquireMathEngine()
{
    ICOManager* Manager = DACOM_Acquire();
    FMathDesc Desc;
    Desc.Size = 0xC;
    Desc.InterfaceName = "3DMathEngine";
    Desc.Implementation = nullptr;
    void* Math = nullptr;
    Manager->CreateInstance((AGGDESC*)(&Desc), &Math);
    return (I3DMathEngine*)Math;
}

void PipelineApplySubmit(ICamera* Camera, void* Inst, const void* Transform,
                         const FTransform** OutCameraView, const FTransform** OutWorld)
{
    const FTransform* CameraView = (const FTransform*)(Camera->GetViewTransform());
    const FTransform* World =
        (const FTransform*)(G_Engine->GetInstanceTransform(Inst));
    *OutCameraView = CameraView;
    *OutWorld = World;
    int State = 1;
    G_Pipeline->GetPipelineState(7, &State);

    const FTransform* Extra = (const FTransform*)Transform;
    if (State != 0)
    {
        FTransform Modelview;
        if (Extra != nullptr)
        {
            FTransform ExtraWorld;
            MathEngineCached(G_MathTriMeshDraw)->MulTransform(ExtraWorld, *Extra, *World);
            MathEngineCached(G_MathTriMeshDraw)->MulTransform(Modelview, *CameraView, ExtraWorld);
        }
        else
        {
            MathEngineCached(G_MathTriMeshDraw)->MulTransform(Modelview, *CameraView, *World);
        }
        G_Pipeline->SetModelview((const float*)(&Modelview));
        return;
    }

    if (Extra != nullptr)
    {
        FTransform Unused;
        MathEngineCached(G_MathTriMeshDraw)->MulTransform(Unused, *Extra, *World);
    }
    G_Pipeline->SetWorld((const float*)World);
}

// FUN_06c33090 (thiscall on Src, RET 4): Dest = -Src. Only caller IRenderer Slot7.
void VectorNegate3(const float* Src, float* Dest)
{
    Dest[0] = -Src[0];
    Dest[1] = -Src[1];
    Dest[2] = -Src[2];
}

// FUN_06c330b0 (thiscall on Src, RET 4): Dest = the transpose of Src's 3x3; returns Dest.
float* TransposeXform3x3(const float* Src12, float* Dest9)
{
    Dest9[0] = Src12[0];
    Dest9[1] = Src12[3];
    Dest9[2] = Src12[6];
    Dest9[3] = Src12[1];
    Dest9[4] = Src12[4];
    Dest9[5] = Src12[7];
    Dest9[6] = Src12[2];
    Dest9[7] = Src12[5];
    Dest9[8] = Src12[8];
    return Dest9;
}

// FUN_06c33100 (cdecl, Dest, Matrix, Vec): the math engine (DAT_06c37d48, created on first use)
// slot 14 +0x38 TransposeTransform into a local, copied to Dest; returns Dest. No fallback.
float* TransformPoint3(float* Dest3, const float* Matrix12, const float* Vec3)
{
    FVector Local;
    MathEngineCached(G_MathRenderMgr)->TransposeTransform(Local, *(const FMatrix*)Matrix12,
                                            *(const FVector*)Vec3);
    Dest3[0] = Local.X;
    Dest3[1] = Local.Y;
    Dest3[2] = Local.Z;
    return Dest3;
}

GENRESULT DACOM_API PluginFactoryCreate(FDacomFactory* This, AGGDESC* Desc, void** Out)
{
    FRenderPluginDesc* Extended = (FRenderPluginDesc*)Desc;
    if (Extended->Size != 0x10 || std::strcmp(Extended->Name, This->Name) != 0)
    {
        *Out = nullptr;
        return GR_INTERFACE_UNSUPPORTED;
    }
    if (std::strcmp(This->Name, G_NameTriMesh) == 0)
        return CreateTriMeshPlugin(Extended, Out);
    return CreateSphereMeshPlugin(Extended, Out);
}

// LAB_06c221d0 (slot 0 of every RendComp factory vtable): *Out = 0, then a 0x17-byte
// REPE CMPSB against "1.11_IComponentFactory" (NUL included, so an exact match); miss -> -3,
// hit -> *Out = this, AddRef through the vtable, 0.
GENRESULT DACOM_API FDacomFactory::QueryInterface(const char* Iid, void** Out)
{
    *Out = nullptr;
    if (std::strcmp(Iid, Iids::ComponentFactory) != 0)
        return GR_INTERFACE_UNSUPPORTED;
    *Out = static_cast<IComponentFactory*>(this);
    AddRef();
    return GR_OK;
}

// LAB_06c306b0: ++[this+4], return it.
unsigned long DACOM_API FDacomFactory::AddRef()
{
    RefCount += 1;
    return RefCount;
}

// FUN_06c2a3a0: decrement when above 0; still non-zero -> return it; zero -> pin the count
// at 1, operator delete (FUN_06c34ea0), return 0.
unsigned long DACOM_API FDacomFactory::Release()
{
    if (RefCount > 0)
        RefCount -= 1;
    if (RefCount != 0)
        return RefCount;
    RefCount = 1;
    delete this;
    return 0;
}

// Slot 3: the factory's own CreateInstance -- RenderManagerFactoryCreate,
// VMeshLibraryFactoryCreate, VMeshWireFactoryCreate, VMeshPluginFactoryCreate
// (PTR_LAB_06c36414, FUN_06c2dae0) or PluginFactoryCreate, chosen by the registrar below.
GENRESULT DACOM_API FDacomFactory::CreateInstance(AGGDESC* Desc, void** Out)
{
    return Create(this, Desc, Out);
}

namespace
{
    void RegisterFactoryUnchecked(FDacomFactoryCreateFn Create, const char* Name, const char* Iid)
    {
        FDacomFactory* Factory = new (std::nothrow) FDacomFactory(Create);
        if (Factory == nullptr)
            return;
        Factory->RefCount = 1;
        Factory->Name = Name != nullptr ? Name : Iid;
        ICOManager* Manager = DACOM_Acquire();
        Manager->RegisterComponent(static_cast<IComponentFactory*>(Factory), Name,
                                   (void*)G_RegisterPriority);
        Factory->Release();
    }

    FDacomFactory* RegisterFactoryChecked(FDacomFactoryCreateFn Create, const char* Name,
                                          const char* Iid)
    {
        FDacomFactory* Factory = new FDacomFactory(Create);
        Factory->RefCount = 1;
        Factory->Name = Name != nullptr ? Name : Iid;
        ICOManager* Manager = DACOM_Acquire();
        if (Manager != nullptr)
            Manager->RegisterComponent(static_cast<IComponentFactory*>(Factory), Name,
                                       (void*)G_RegisterPriority);
        return Factory;
    }

    // FUN_06c2ecf0.
    void RegisterVMeshLibrary()
    {
        RegisterFactoryUnchecked(&VMeshLibraryFactoryCreate, G_NameVMeshLibrary, Iids::VMeshLibrary);
    }

    // FUN_06c2f490.
    void RegisterVMeshWire()
    {
        RegisterFactoryUnchecked(&VMeshWireFactoryCreate, G_NameVMeshWire, nullptr);
    }

    // FUN_06c21a90.
    void RegisterTriMesh()
    {
        FDacomFactory* Factory =
            RegisterFactoryChecked(&PluginFactoryCreate, G_NameTriMesh, Iids::RenderComponent);
        Factory->Release();
    }

    // FUN_06c2a000.
    void RegisterSphereMesh()
    {
        FDacomFactory* Factory =
            RegisterFactoryChecked(&PluginFactoryCreate, G_NameSphereMesh, Iids::RenderComponent);
        Factory->Release();
    }

    // FUN_06c2dae0: registers VMesh, then VMeshLibrary and VMeshWire, and only then releases
    // its own factory.
    void RegisterVMesh()
    {
        FDacomFactory* Factory =
            RegisterFactoryChecked(&VMeshPluginFactoryCreate, G_NameVMesh, Iids::RenderComponent);
        RegisterVMeshLibrary();
        RegisterVMeshWire();
        Factory->Release();
    }
}

BOOL APIENTRY DllMain(HMODULE Module, DWORD Reason, LPVOID /*Reserved*/)
{
    if (Reason == DLL_PROCESS_ATTACH)
    {
        // FUN_06c34080: RenderManager inline (06c34093), then FUN_06c21a90, FUN_06c2a000,
        // FUN_06c2dae0 (which chains FUN_06c2ecf0 and FUN_06c2f490).
        DisableThreadLibraryCalls(Module);
        RegisterFactoryUnchecked(&RenderManagerFactoryCreate, G_NameRenderManager, Iids::Renderer2);
        RegisterTriMesh();
        RegisterSphereMesh();
        RegisterVMesh();
    }
    return TRUE;
}
