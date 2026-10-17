// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "client_mesh_load.h"

#include "DALib.h"
#include "common_archetype.h"
#include "common_cobject.h" // CompoundInstanceAnalyze
#include "common_csys.h"    // Acquire3DMathEngine (FUN_005b73e0)
#include "x86math.h"        // I3DMathEngine::TransformXform
#include "dacom.h"
#include "readfile.h" // IFileSystem (00420e58 CreateInstance, 00420f05 Release)
#include "engine/engbase.h"
#include "render/rendcomp.h" // IRenderer2
#include "render/RenderPipeline.h"

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cmath>
#include <vector>
#include <windows.h>

namespace
{
    // FUN_0059ec60 (cdecl Instance, Context) -- the CompoundInstanceAnalyze callback of
    // FUN_0059ec80: IEngine vt+0x7c SetInstanceUserData(Instance, Context), nothing else.
    void __cdecl MeshAnalyzeSetUserData(long Instance, void* Context)
    {
        DALib::Engine->SetInstanceUserData(reinterpret_cast<void*>(Instance), Context);
    }

    // FUN_0059ec80 0x0059ecd9..0x0059ecec (and its two copies): analyze the bound instance
    // with the holder's floats +0x14 as the context.
    void AnalyzeBoundInstance(FClientMeshHolder* Holder)
    {
        long const Inst = static_cast<long>(reinterpret_cast<ptrdiff_t>(Holder->EngineInstance));
        CompoundInstanceAnalyze(Inst, MeshAnalyzeSetUserData, Holder->UserFloats);
    }

    // The DALib::Data FileSystem description FUN_0059ec80 (0x0059ed1c..0x0059ed7b) and
    // FUN_0059e980 (0x0059ea47..0x0059ea9c) build on the stack: the 0x34-byte desc,
    // "FileSystem", no backend, the path, and the fixed open flags.
    void BuildMeshFileSystemDesc(FFileSystemAggDesc* Desc, char const* Path)
    {
        memset(Desc, 0, sizeof(*Desc));
        Desc->Size = 0x34;
        Desc->ComponentName = "FileSystem";
        Desc->DataPath = Path;
        Desc->Field10 = 0x80000000u;
        Desc->Field14 = 1;
        Desc->Field1C = 3;
        Desc->Field20 = 0x8000080u;
        Desc->Field30 = 0xffffffffu;
    }

    // DAT_0067dcf4 -- the Win3D model cache. Constructed empty by the static initializer
    // LAB_0059ec10 (FUN_0059ec40), freed at exit by LAB_0059ec30 (FUN_004de730) without
    // releasing anything; FUN_0059e8d0 is what releases the entries.
    std::vector<FWin3DModelCacheEntry> G_Win3DModelCache;

    // The linear search FUN_0059ec80 (0x0059edc8..0x0059ee0b) and FUN_0059e980
    // (0x0059e9d1..0x0059ea0a, 0x0059eb0e..0x0059eb4b) run on the cache: first entry whose
    // handle is ArchHandle, or null.
    FWin3DModelCacheEntry* FindModelCacheEntry(void* ArchHandle)
    {
        size_t const Count = G_Win3DModelCache.size();
        for (size_t Index = 0; Index < Count; ++Index)
        {
            if (G_Win3DModelCache[Index].ArchHandle == ArchHandle)
                return &G_Win3DModelCache[Index];
        }
        return nullptr;
    }

    // FUN_0059ebf0 + FUN_0059f5b0 / FUN_00470c90: append {ArchHandle, 0, 0}.
    void AppendModelCacheEntry(void* ArchHandle)
    {
        FWin3DModelCacheEntry Entry;
        Entry.ArchHandle = ArchHandle;
        Entry.MaterialEntry = nullptr;
        Entry.AuxMaterialEntry = nullptr;
        G_Win3DModelCache.push_back(Entry);
    }
}

namespace
{
    bool IsValidArchetypeHandle(void* Handle)
    {
        return Handle != nullptr && Handle != G_InvalidEngineHandle;
    }
}

// Client-only port helper (moved here from Common.dll, which exported it although retail
// Common.dll has no such export). Archetype::Load (a retail Common export), then an optional
// IEngine::CreateInstance. NOT a retail function: it stands in for the per-caller retail code
// in FUN_0059ec80 / FUN_00420dd0 and the room / thorn spawners.
bool ArchetypeLoadFromPath(IEngine* Engine, char const* RelPath, void** OutHandle,
                           void** OutInstance)
{
    if (OutHandle != nullptr)
        *OutHandle = G_InvalidEngineHandle;
    if (OutInstance != nullptr)
        *OutInstance = G_InvalidEngineHandle;

    if (RelPath == nullptr || RelPath[0] == '\0')
        return false;

    char AltPath[260];
    char const* TryPaths[3];
    int TryCount = 0;
    char const* Ext = strrchr(RelPath, '.');
    if (Ext != nullptr && _stricmp(Ext, ".sph") == 0)
    {
        const unsigned long StemLen = static_cast<unsigned long>(Ext - RelPath);
        if (StemLen + 5 < sizeof(AltPath))
        {
            memcpy(AltPath, RelPath, StemLen);
            memcpy(AltPath + StemLen, ".cmp", 5);
            TryPaths[TryCount++] = AltPath;
        }
    }
    TryPaths[TryCount++] = RelPath;

    long EngineIndex = -1;
    char const* LoadedPath = RelPath;
    int TryIndex = 0;
    while (TryIndex < TryCount)
    {
        EngineIndex = -1;
        if (Archetype::Load(TryPaths[TryIndex], &EngineIndex, nullptr)
            && IsValidArchetypeHandle(reinterpret_cast<void*>(EngineIndex)))
        {
            LoadedPath = TryPaths[TryIndex];
            break;
        }
        EngineIndex = -1;
        TryIndex += 1;
    }
    if (EngineIndex == -1 || !IsValidArchetypeHandle(reinterpret_cast<void*>(EngineIndex)))
        return false;
    (void)LoadedPath;

    void* Handle = reinterpret_cast<void*>(EngineIndex);
    if (OutHandle != nullptr)
        *OutHandle = Handle;

    if (OutInstance == nullptr)
        return IsValidArchetypeHandle(Handle);

    IEngine* UseEngine = Engine;
    if (UseEngine == nullptr)
        UseEngine = DALib::Engine;

    // Same effect as Common's former helper: IEngine::CreateInstance(Handle, null transform)
    // when there is an engine.
    void* Instance = (UseEngine != nullptr) ? UseEngine->CreateInstance(Handle, nullptr)
                                            : G_InvalidEngineHandle;
    *OutInstance = Instance;

    // Keep the Load handle: IEngine+0x3C Release recurses Cmpnd children and can
    // drop Part_* claims before IRenderer Slot7 walks FirstChild.
    (void)OutHandle;

    return IsValidArchetypeHandle(Instance);
}

FMeshHolder::~FMeshHolder()
{
    // ObjectHandle only changes its vptr; instance/material ownership is released explicitly.
}

void ClientMeshHolderConstruct(FClientMeshHolder* Holder)
{
    // FUN_00420d40 + FUN_00492250 on +0x14 + the Object3D vptr + +0x10 = -1: the inlined
    // Object3D constructor sequence. None of the retail sites tests the holder for null.
    Holder->EngineInstance = G_InvalidEngineHandle;
    Holder->MaterialEntry = nullptr;
    Holder->AuxMaterialEntry = nullptr;
    Holder->AnimOrArchIndex = -1;
    Holder->UserFloats[0] = 1.0f;
    Holder->UserFloats[1] = 1.0f;
    Holder->UserFloats[2] = 1.0f;
    Holder->UserFloats[3] = 1.0f;
    Holder->UserFloats[4] = 1.0f;
}

void MeshHolderConstruct(FMeshHolder* Holder)
{
    // FUN_00420d40 field stores; the compiler owns the existing object's vptr.
    Holder->EngineInstance = G_InvalidEngineHandle;
    Holder->MaterialEntry = nullptr;
    Holder->AuxMaterialEntry = nullptr;
}

bool ClientMeshHolderHasInstance(FMeshHolder const* Holder)
{
    return Holder->EngineInstance != G_InvalidEngineHandle;
}

void ClientMeshHolderRelease(FMeshHolder* Holder)
{
    if (Holder->EngineInstance != G_InvalidEngineHandle)
        DALib::Engine->DestroyInstance(Holder->EngineInstance);
    Holder->EngineInstance = G_InvalidEngineHandle;
    Archetype::ReleaseMaterialEntrySlot(&Holder->MaterialEntry);
    Archetype::ReleaseMaterialEntrySlot(&Holder->AuxMaterialEntry);
}

FClientMeshHolder::~FClientMeshHolder()
{
    // FUN_0059ef20 (thiscall, plain RET): the Object3D vptr, then 0x0059ef43..0x0059ef75 --
    // FUN_0059eee0's body inlined (anim +0x10 != -1 -> DALib::Anim vt+0x10 and -1; a bound
    // instance -> FUN_00420f70) -- then FUN_00420d10, which only resets the vptr.
    ClientUiMeshHolderRelease(this);
}

void ClientUiMeshHolderRelease(FClientMeshHolder* Holder)
{
    if (Holder->AnimOrArchIndex != -1)
    {
        DALib::Anim->CloseScript(reinterpret_cast<void*>(Holder->AnimOrArchIndex));
        Holder->AnimOrArchIndex = -1;
    }
    if (ClientMeshHolderHasInstance(Holder))
        ClientMeshHolderRelease(Holder);
}

bool ClientMeshHolderLoad(FMeshHolder* Holder, char const* Path, char const* AuxPath)
{
    // FUN_00420dd0 (thiscall, RET 8).
    bool bOk = false;

    // 00420df6..00420e4a: the DALib::OpenData-shaped FileSystem desc with no backend name.
    FFileSystemAggDesc Desc;
    memset(&Desc, 0, sizeof(Desc));
    Desc.Size = 0x34;
    Desc.ComponentName = "FileSystem";
    Desc.DataPath = Path;
    Desc.Field10 = 0x80000000u;
    Desc.Field14 = 1;
    Desc.Field1C = 3;
    Desc.Field20 = 0x8000080u;
    Desc.Field30 = 0xffffffffu;

    void* FsOut = nullptr;
    // 00420e58: DALib::Data vt+0xc CreateInstance(&Desc, &Fs).
    if (static_cast<IFileSystem*>(DALib::Data)
            ->CreateInstance(reinterpret_cast<AGGDESC*>(&Desc), &FsOut) == 0)
    {
        // 00420e63..00420e8a: material-cache entries for the path and the aux path.
        if (Path != nullptr)
            Holder->MaterialEntry = Archetype::ResolveMaterialEntry(Path, true, 0xffffffffu);
        if (AuxPath != nullptr)
            Holder->AuxMaterialEntry = Archetype::ResolveMaterialEntry(AuxPath, true, 0xffffffffu);

        long Handle = -1;
        bOk = Archetype::Load(static_cast<IFileSystem*>(FsOut), &Handle, nullptr);
        if (bOk)
        {
            // 00420eac: an instance already bound is destroyed and BOTH cache slots released --
            // including the two entries resolved just above; retail does exactly this.
            if (Holder->EngineInstance != G_InvalidEngineHandle)
            {
                DALib::Engine->DestroyInstance(Holder->EngineInstance);
                Holder->EngineInstance = G_InvalidEngineHandle;
                Archetype::ReleaseMaterialEntrySlot(&Holder->MaterialEntry);
                Archetype::ReleaseMaterialEntrySlot(&Holder->AuxMaterialEntry);
            }
            // 00420ede: CreateInstance(Handle, 0), then ReleaseHandle(Handle).
            void* const HandlePtr = reinterpret_cast<void*>(static_cast<ptrdiff_t>(Handle));
            Holder->EngineInstance = DALib::Engine->CreateInstance(HandlePtr, nullptr);
            DALib::Engine->ReleaseHandle(HandlePtr);
        }
    }

    // 00420f05: the FileSystem reference is dropped whether or not the create succeeded.
    if (FsOut != nullptr)
        static_cast<IFileSystem*>(FsOut)->Release();
    return bOk;
}

void ClientMeshHolderSetMaterials(FMeshHolder* Holder, char const* Path, char const* AuxPath)
{
    // FUN_00420f30.
    if (Path != nullptr)
        Holder->MaterialEntry = Archetype::ResolveMaterialEntry(Path, true, 0xffffffffu);
    if (AuxPath != nullptr)
        Holder->AuxMaterialEntry = Archetype::ResolveMaterialEntry(AuxPath, true, 0xffffffffu);
}

bool ClientMeshHolderBindInstance(FMeshHolder* Holder, void* ArchHandle)
{
    // FUN_00420d70. 0x00420d73..0x00420da2: a bound instance is destroyed, the slot set back to
    // -1 and both material entries released.
    void* const NoInstance = reinterpret_cast<void*>(static_cast<ptrdiff_t>(-1));
    if (Holder->EngineInstance != NoInstance)
    {
        DALib::Engine->DestroyInstance(Holder->EngineInstance);
        Holder->EngineInstance = NoInstance;
        Archetype::ReleaseMaterialEntrySlot(&Holder->MaterialEntry);
        Archetype::ReleaseMaterialEntrySlot(&Holder->AuxMaterialEntry);
    }
    // 0x00420da5..0x00420dc8: CreateInstance(ArchHandle, 0) into +0x04; true unless -1.
    Holder->EngineInstance = DALib::Engine->CreateInstance(ArchHandle, nullptr);
    return Holder->EngineInstance != NoInstance;
}

float* ClientMeshHolderGetTransform(FMeshHolder const* Holder)
{
    // FUN_00421030: return Engine->vt+0xa0(Engine, Holder+0x04).
    return DALib::Engine->GetInstanceTransform(Holder->EngineInstance);
}

namespace
{
    // DAT_00616870 -- FUN_00421090's lazily acquired math engine (no other reader in the PE).
    I3DMathEngine* G_MeshHolderMathEngine = nullptr;
}

float ClientMeshHolderGetBoundingSphere(FMeshHolder const* Holder, float* OutCenter)
{
    // FUN_00421090.
    float Radius = 0.0f;
    DALib::Engine->GetInstanceExtent(Holder->EngineInstance, 0, &Radius, OutCenter);
    float* const Transform = DALib::Engine->GetInstanceTransform(Holder->EngineInstance);
    if (G_MeshHolderMathEngine == nullptr)
        G_MeshHolderMathEngine = Acquire3DMathEngine();
    FVector World;
    G_MeshHolderMathEngine->TransformXform(World, *reinterpret_cast<FTransform const*>(Transform),
                                           *reinterpret_cast<FVector const*>(OutCenter));
    OutCenter[0] = World.X;
    OutCenter[1] = World.Y;
    OutCenter[2] = World.Z;
    return Radius;
}

void ClientMeshHolderSetMatrix(FMeshHolder* Holder, void* Transform)
{
    // FUN_00421050: Engine->vt+0x9c(Engine, Holder+0x04, Transform).
    DALib::Engine->SetInstanceMatrix(Holder->EngineInstance, Transform);
}

void ClientMeshHolderRender(FMeshHolder* Holder, void* Camera, unsigned long Flags)
{
    // FUN_00421120: Renderer->vt+0x1c(Renderer, Camera, Holder+0x04, 1.0f, Flags, 0, 0).
    DALib::Renderer->RenderInstance(Camera, Holder->EngineInstance, 1.0f, Flags, nullptr, nullptr);
}

void ClientMeshHolderSetPosition(FMeshHolder* Holder, float const Position[3])
{
    // FUN_00420fd0: Engine->vt+0x8c(Engine, Holder+0x04, Position).
    DALib::Engine->SetInstancePosition(Holder->EngineInstance, const_cast<float*>(Position));
}

float* ClientMeshHolderGetPosition(FMeshHolder const* Holder)
{
    // FUN_00420fb0: return Engine->vt+0x90(Engine, Holder+0x04).
    return DALib::Engine->GetInstancePosition(Holder->EngineInstance);
}

float* ClientMeshHolderGetOrientation(FMeshHolder const* Holder)
{
    // FUN_00420ff0: return Engine->vt+0x98(Engine, Holder+0x04).
    return DALib::Engine->GetInstanceOrientation(Holder->EngineInstance);
}

void ClientMeshHolderSetOrientation(FMeshHolder* Holder, float const Orientation[9])
{
    // FUN_00421010: Engine->vt+0x94(Engine, Holder+0x04, Orientation).
    DALib::Engine->SetInstanceOrientation(Holder->EngineInstance,
                                          const_cast<float*>(Orientation));
}

void ClientMeshHolderUpdate(FMeshHolder* Holder)
{
    // FUN_00421070: Engine->vt+0x6c(Engine, Holder+0x04, 0, 0).
    DALib::Engine->UpdateInstance(Holder->EngineInstance, 0, nullptr);
}

void ClientMeshHolderSetColor(FClientMeshHolder* Holder, unsigned char const* Rgba)
{
    // FUN_004a1af0: each byte FILD, FMUL float 1/255 (DAT_005c7f68), stored as a float into
    // +0x14 / +0x18 / +0x1c / +0x24. +0x20 (UserFloats[3]) is not written.
    double const Inv255 = static_cast<double>(0.003921568859368563f);
    Holder->UserFloats[0] = static_cast<float>(static_cast<double>(Rgba[0]) * Inv255);
    Holder->UserFloats[1] = static_cast<float>(static_cast<double>(Rgba[1]) * Inv255);
    Holder->UserFloats[2] = static_cast<float>(static_cast<double>(Rgba[2]) * Inv255);
    Holder->UserFloats[4] = static_cast<float>(static_cast<double>(Rgba[3]) * Inv255);
}

bool ClientMeshLoadFromPath(FClientMeshHolder* Holder, char const* RelPath,
                            char const* MaterialPath, bool bRegisterCache)
{
    // FUN_0059ec80 (thiscall on the holder, RET 0xc).

    // 0x0059ec9e..0x0059ecc1: FindArchetype({RelPath, GetCRC32(RelPath)}) (IEngine vt+0x44).
    FEngineName Name = {};
    Name.Text = RelPath;
    Name.Crc = DACOM_CRC::GetCRC32(RelPath);
    void* const Found = DALib::Engine->FindArchetype(&Name);
    if (Found != G_InvalidEngineHandle)
    {
        // 0x0059eccd..0x0059ed19: bind, analyze on success, release the FindArchetype handle
        // (vt+0x3c) either way, return FUN_00420d60. No FileSystem, no material entries.
        if (ClientMeshHolderBindInstance(Holder, Found))
            AnalyzeBoundInstance(Holder);
        DALib::Engine->ReleaseHandle(Found);
        return ClientMeshHolderHasInstance(Holder);
    }

    // 0x0059ed1c..0x0059ed85: open RelPath as a DALib::Data FileSystem (vt+0xc CreateInstance).
    // The FileSystem is the EH-guarded local released at 0x0059eeb2 whatever happened.
    FFileSystemAggDesc Desc;
    BuildMeshFileSystemDesc(&Desc, RelPath);
    void* FsOut = nullptr;
    if (static_cast<IFileSystem*>(DALib::Data)
            ->CreateInstance(reinterpret_cast<AGGDESC*>(&Desc), &FsOut) == 0)
    {
        // 0x0059ed90..0x0059ed98: FUN_00420f30(RelPath, MaterialPath) -- the material-cache
        // entries of the mesh itself (+0x08) and of the material library (+0x0c).
        ClientMeshHolderSetMaterials(Holder, RelPath, MaterialPath);

        // Archetype::Load(Fs, &handle, holder +0x10) on both branches; the handle lands in
        // retail's own RelPath argument slot.
        long LoadedHandle = -1;
        int* const AnimIndex = reinterpret_cast<int*>(&Holder->AnimOrArchIndex);
        if (bRegisterCache)
        {
            // 0x0059eda9..0x0059ee54: a loaded handle is kept -- appended to the model cache
            // unless already there -- and NOT released here (FUN_0059e8d0 does that).
            if (Archetype::Load(static_cast<IFileSystem*>(FsOut), &LoadedHandle, AnimIndex))
            {
                void* const Loaded = reinterpret_cast<void*>(static_cast<ptrdiff_t>(LoadedHandle));
                if (FindModelCacheEntry(Loaded) == nullptr)
                    AppendModelCacheEntry(Loaded); // 0x0059ee0d..0x0059ee29 FUN_0059f5b0
                if (ClientMeshHolderBindInstance(Holder, Loaded))
                    AnalyzeBoundInstance(Holder);
            }
        }
        else
        {
            // 0x0059ee56..0x0059eea6: bind, analyze on success, then release the handle.
            if (Archetype::Load(static_cast<IFileSystem*>(FsOut), &LoadedHandle, AnimIndex))
            {
                void* const Loaded = reinterpret_cast<void*>(static_cast<ptrdiff_t>(LoadedHandle));
                if (ClientMeshHolderBindInstance(Holder, Loaded))
                    AnalyzeBoundInstance(Holder);
                DALib::Engine->ReleaseHandle(Loaded);
            }
        }
    }

    // 0x0059eea9..0x0059eec5: result = FUN_00420d60, then the FileSystem is released (vt+8).
    bool const bResult = ClientMeshHolderHasInstance(Holder);
    if (FsOut != nullptr)
        static_cast<IFileSystem*>(FsOut)->Release();
    return bResult;
}

bool Win3DModelCachePreload(char const* Path, char const* AuxPath)
{
    // FUN_0059e980 (thiscall on DAT_0067dcf4, RET 8).

    // 0x0059e9a0..0x0059e9c3: FindArchetype({Path, GetCRC32(Path)}).
    FEngineName Name = {};
    Name.Text = Path;
    Name.Crc = DACOM_CRC::GetCRC32(Path);
    void* const Found = DALib::Engine->FindArchetype(&Name);
    if (Found != G_InvalidEngineHandle)
    {
        // 0x0059e9d1..0x0059ea2c: not cached yet -> the FindArchetype handle itself is
        // appended (FUN_00470c90, one element {handle, 0, 0} at end) and kept; already cached
        // -> 0x0059ea31 releases it. True either way.
        if (FindModelCacheEntry(Found) == nullptr)
        {
            AppendModelCacheEntry(Found);
            return true;
        }
        DALib::Engine->ReleaseHandle(Found);
        return true;
    }

    // 0x0059ea47..0x0059eab4: open Path as a DALib::Data FileSystem; failure -> false.
    bool bResult = false;
    FFileSystemAggDesc Desc;
    BuildMeshFileSystemDesc(&Desc, Path);
    void* FsOut = nullptr;
    if (static_cast<IFileSystem*>(DALib::Data)
            ->CreateInstance(reinterpret_cast<AGGDESC*>(&Desc), &FsOut) == 0)
    {
        IFileSystem* const Filesys = static_cast<IFileSystem*>(FsOut);
        // 0x0059eaba..0x0059eac4: FUN_0041ade0(Fs, Path, 1, -1) -- the mesh's own materials,
        // from the file system already open. 0x0059eac9..0x0059ead7: FUN_0041aa50(AuxPath, 1,
        // -1), AuxPath passed as is.
        Archetype::FCliMaterialEntry* const Material =
            Archetype::ResolveMaterialEntryFromFs(Filesys, Path, true, 0xffffffffu);
        Archetype::FCliMaterialEntry* const AuxMaterial =
            Archetype::ResolveMaterialEntry(AuxPath, true, 0xffffffffu);

        // 0x0059eadc..0x0059eb08: Archetype::Load(Fs, &handle, &anim) with anim = -1.
        long LoadedHandle = -1;
        int AnimIndex = -1;
        if (Archetype::Load(Filesys, &LoadedHandle, &AnimIndex))
        {
            void* const Loaded = reinterpret_cast<void*>(static_cast<ptrdiff_t>(LoadedHandle));
            // 0x0059eb0e..0x0059eb67: append {handle, 0, 0} when not cached (FUN_0059ebf0 +
            // FUN_0059f5b0).
            if (FindModelCacheEntry(Loaded) == nullptr)
                AppendModelCacheEntry(Loaded);
            // 0x0059eb6b..0x0059ebb0: every entry of that handle takes both material entries
            // (whatever it held before is overwritten, not released).
            size_t const Count = G_Win3DModelCache.size();
            for (size_t Index = 0; Index < Count; ++Index)
            {
                if (G_Win3DModelCache[Index].ArchHandle == Loaded)
                {
                    G_Win3DModelCache[Index].MaterialEntry = Material;
                    G_Win3DModelCache[Index].AuxMaterialEntry = AuxMaterial;
                }
            }
            bResult = true; // 0x0059ebb2
        }
    }

    // 0x0059ebb4..0x0059ebca: the FileSystem is released whatever happened.
    if (FsOut != nullptr)
        static_cast<IFileSystem*>(FsOut)->Release();
    return bResult;
}

void Win3DModelCacheClear()
{
    // FUN_0059e8d0 (thiscall on DAT_0067dcf4).
    // 0x0059e8d4..0x0059e93b: per entry, a handle other than -1 is released (IEngine
    // vt+0x3c) and set to -1, then both material-cache slots go through FUN_0041b030.
    size_t const Count = G_Win3DModelCache.size();
    for (size_t Index = 0; Index < Count; ++Index)
    {
        FWin3DModelCacheEntry& Entry = G_Win3DModelCache[Index];
        if (Entry.ArchHandle != G_InvalidEngineHandle)
        {
            DALib::Engine->ReleaseHandle(Entry.ArchHandle);
            Entry.ArchHandle = G_InvalidEngineHandle;
        }
        Archetype::ReleaseMaterialEntrySlot(&Entry.MaterialEntry);
        Archetype::ReleaseMaterialEntrySlot(&Entry.AuxMaterialEntry);
    }
    // 0x0059e93d..0x0059e971: erase(begin, end) -- the storage is kept.
    G_Win3DModelCache.clear();
}

bool ClientMeshQueryAabb(FClientMeshHolder* Holder, FClientMeshAabb* OutAabb)
{
    // FUN_0059f0d0 (thiscall on the holder, RET 4). The only test is the instance against -1
    // (0x0059f0d3); OutAabb is not cleared, and the Renderer's answer is not looked at -- the
    // function returns true (0x0059f10f) whatever Slot4 did.
    if (!ClientMeshHolderHasInstance(Holder))
        return false; // LAB_0059f115

    // Engine vt+0x64 GetInstanceArchetype (0x0059f0e4).
    void* Arch = DALib::Engine->GetInstanceArchetype(Holder->EngineInstance);
    // IRenderer2 Slot4 (+0x10) -- GetExtents(arch, lod=1.0, out6) (0x0059f0fe).
    DALib::Renderer->GetBounds(Arch, 1.0f, OutAabb->Values);
    // Engine vt+0x3c ReleaseHandle (0x0059f10c).
    DALib::Engine->ReleaseHandle(Arch);
    return true;
}
