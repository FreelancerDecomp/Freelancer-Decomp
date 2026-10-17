// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "part_cache.h"

#include "DALib.h"
#include "common_csys.h"
#include "common_misc.h"               // ICamera
#include "dacom.h"
#include "dacom_iids.h"
#include "engine/engbase.h"
#include "render/IRenderPipeline.h"  // IVertexBufferManager
#include "render/mesh_iface.h"
#include "render/rendcomp.h"
#include "x86math.h"

#include <cstdint>
#include <cstring>
#include <new>

namespace
{
    // DAT_0061649c -- this unit's lazily filled I3DMathEngine. Every use tests it and, while
    // null, stores FUN_005b73e0's result (0x0040611e..0x0040612c in FUN_00405fb0,
    // 0x00406412..0x00406423 in FUN_00406230, 0x0040678d..0x0040679b in FUN_00406510); those
    // are its only references.
    I3DMathEngine* G_PartCacheMath = nullptr;

    I3DMathEngine* PartCacheMath()
    {
        if (G_PartCacheMath == nullptr)
            G_PartCacheMath = Acquire3DMathEngine();
        return G_PartCacheMath;
    }

    // FUN_00526a20: release the held interface, if any, and clear the slot.
    void ReleaseCom(IDAComponent** Pp)
    {
        IDAComponent* Component = *Pp;
        if (Component != nullptr)
        {
            Component->Release();
            *Pp = nullptr;
        }
    }

    // The stack holder FUN_00405b10 keeps its QueryInstance results in: EH-registered, so its
    // destructor (FUN_00526a20) also runs on the unwind path.
    struct FPartComHolder
    {
        IDAComponent* Ptr = nullptr;

        FPartComHolder() = default;
        FPartComHolder(FPartComHolder const&) = delete;
        FPartComHolder& operator=(FPartComHolder const&) = delete;
        ~FPartComHolder()
        {
            ReleaseCom(&Ptr);
        }
    };

    void FillUshortOnes(unsigned short* Dest, unsigned Count)
    {
        unsigned ByteCount = Count * 2U;
        unsigned DwordCount = ByteCount / 4U;
        unsigned* Dwords = reinterpret_cast<unsigned*>(Dest);
        unsigned Index = 0;
        for (; Index < DwordCount; ++Index)
            Dwords[Index] = 0xFFFFFFFFu;
        unsigned char* Bytes = reinterpret_cast<unsigned char*>(Dwords + DwordCount);
        unsigned Rem = ByteCount & 3U;
        unsigned ByteIndex = 0;
        for (; ByteIndex < Rem; ++ByteIndex)
            Bytes[ByteIndex] = 0xFFu;
    }

    void MakeIdentityTransform(FTransform* Out)
    {
        std::memset(Out, 0, sizeof(FTransform));
        Out->Rotation.D[0][0] = 1.0f;
        Out->Rotation.D[1][1] = 1.0f;
        Out->Rotation.D[2][2] = 1.0f;
    }
}

// FUN_004061a0
int FvfTexcoordBlockBytes(unsigned Fvf)
{
    int Total = 0;
    unsigned SetCount = (Fvf >> 8) & 0xFu;
    if (SetCount != 0)
    {
        unsigned Shift = 0x10;
        unsigned Remaining = SetCount;
        do
        {
            unsigned Code = (Fvf >> (Shift & 0x1Fu)) & 3U;
            if (Code == 0)
                Total += 8;
            else if (Code == 1)
                Total += 0xC;
            else if (Code == 2)
                Total += 0x10;
            else if (Code == 3)
                Total += 4;
            Shift += 2;
            Remaining -= 1;
        } while (Remaining != 0);
    }
    return Total;
}

// FUN_00406880
void CopyTransform12(FTransform* Dest, FTransform const* Source)
{
    unsigned* Dst = reinterpret_cast<unsigned*>(Dest);
    unsigned const* Src = reinterpret_cast<unsigned const*>(Source);
    int Index = 0xC;
    for (; Index != 0; Index -= 1)
    {
        *Dst = *Src;
        Src += 1;
        Dst += 1;
    }
}

// FUN_004058b0 (thiscall, no test of the entry): free +4, clear it, free +0xc, clear it.
void PartCacheEntryDestroy(FPartCacheEntry* Entry)
{
    operator delete(Entry->Vertices);
    Entry->Vertices = nullptr;
    operator delete(Entry->Indices);
    Entry->Indices = nullptr;
}

// LAB_00405f80
void PartCacheEntryConstruct(FPartCacheEntry* Entry)
{
    Entry->VertexCount = 0;
    Entry->Vertices = nullptr;
    Entry->IndexCount = 0;
    Entry->Indices = nullptr;
    Entry->InstanceId = reinterpret_cast<void*>(static_cast<ptrdiff_t>(-1));
    Entry->Tag = 0xFFFFFFFFu;
    Entry->bHidden = 0;
    Entry->bActive = 1;
    Entry->Field1A = 0;
}

// FUN_004058e0 (thiscall, no test of the cache): the count-prefixed entry array through the
// vector destructor iterator (FUN_004058b0 per 0x1c record), then the merged index buffer.
void PartCacheClear(FPartCache* Cache)
{
    FPartCacheEntry* Entries = Cache->Entries;
    if (Entries != nullptr)
    {
        unsigned* Prefix = reinterpret_cast<unsigned*>(Entries) - 1;
        unsigned Count = *Prefix;
        unsigned Index = 0;
        for (; Index < Count; ++Index)
            PartCacheEntryDestroy(Entries + Index);
        operator delete(Prefix);
    }
    operator delete(Cache->MergedIndices);
    Cache->Entries = nullptr;
    Cache->PartCount = 0;
    Cache->TotalIndices = 0;
    Cache->TotalVertices = 0;
    Cache->MergedIndices = nullptr;
}

// FUN_00405fb0 (thiscall on the entry, RET 8: Wire, Xform). The wire is used untested; the
// only early returns are retail's own (0x00405fd4 / 0x00405fdc / 0x00405fea).
void PartCacheBuildFromWire(FPartCacheEntry* Entry, IVMeshWire* Wire, FTransform const* Xform)
{
    unsigned char* HeaderBytes = static_cast<unsigned char*>(Wire->GetMeshData());
    unsigned char* PartBytes = static_cast<unsigned char*>(Wire->GetWireData());
    if (HeaderBytes == nullptr || PartBytes == nullptr)
        return;

    FVmeshDataHeader const* MeshHeader = reinterpret_cast<FVmeshDataHeader*>(HeaderBytes);
    FVwireDataHeader const* PartHeader = reinterpret_cast<FVwireDataHeader*>(PartBytes);

    unsigned RemapCount = PartHeader->RemapCount;
    Entry->VertexCount = RemapCount;
    if (RemapCount == 0)
        return;

    unsigned Fvf = MeshHeader->Fvf;
    unsigned ExtraPos = 0;
    if ((Fvf & 0xCu) == 0xCu)
        ExtraPos = 0x1Cu;
    else if ((Fvf & 4U) == 4U)
        ExtraPos = 0x10U;
    else if ((Fvf & 2U) == 2U)
        ExtraPos = 0xCu;

    unsigned ExtraUv2 = 0;
    if ((Fvf & 0x10U) != 0)
        ExtraUv2 = 0xCu;

    unsigned ExtraTail = 0;
    if ((Fvf & 0x40U) != 0)
        ExtraTail = 4;
    if ((Fvf & 0x80U) != 0)
        ExtraTail += 4;

    int TexBytes = FvfTexcoordBlockBytes(Fvf);
    unsigned Stride = static_cast<unsigned>(TexBytes) + ExtraTail + ExtraUv2 + ExtraPos;

    Entry->Vertices = static_cast<FPartCacheVertex*>(
        operator new(static_cast<std::size_t>(Entry->VertexCount) << 4));
    Entry->IndexCount = PartHeader->Count;
    Entry->Indices = static_cast<unsigned short*>(
        operator new(static_cast<std::size_t>(Entry->IndexCount) * 2U));

    unsigned short const HeaderVerts = MeshHeader->Vertices;
    unsigned short* Remap = static_cast<unsigned short*>(
        operator new(static_cast<std::size_t>(HeaderVerts) * 2U));
    FillUshortOnes(Remap, HeaderVerts);

    unsigned short FaceGroups = MeshHeader->FaceGroups;
    unsigned short MeshIndices = MeshHeader->Indices;
    unsigned char const* VertexPool =
        HeaderBytes + 0x10
        + (static_cast<unsigned>(MeshIndices) + static_cast<unsigned>(FaceGroups) * 6U) * 2U;

    unsigned short Compacted = 0;
    unsigned IndexIter = 0;
    unsigned short const* PartIndices =
        reinterpret_cast<unsigned short const*>(PartBytes + 0x10);

    if (Entry->IndexCount != 0)
    {
        do
        {
            unsigned short RawIndex = PartIndices[IndexIter];
            unsigned short IbBias = PartHeader->IbBias;
            unsigned short* RemapSlot = Remap + RawIndex;
            unsigned short Mapped = *RemapSlot;
            if (Mapped == 0xFFFFu)
            {
                *RemapSlot = Compacted;
                FVector const* Source = reinterpret_cast<FVector const*>(
                    VertexPool
                    + static_cast<unsigned>(static_cast<unsigned short>(IbBias + RawIndex))
                          * Stride);
                FVector Transformed;
                PartCacheMath()->TransformXform(Transformed, *Xform, *Source);
                FPartCacheVertex* OutVert = Entry->Vertices + Compacted;
                OutVert->X = Transformed.X;
                OutVert->Y = Transformed.Y;
                OutVert->Z = Transformed.Z;
                OutVert->Tag = Entry->Tag;
                Mapped = Compacted;
                Compacted = static_cast<unsigned short>(Compacted + 1);
            }
            Entry->Indices[IndexIter] = Mapped;
            IndexIter += 1;
        } while (IndexIter < Entry->IndexCount);
    }

    operator delete(Remap);
}

// FUN_00406230 (thiscall on the entry, RET 8: Mesh, Xform). Stack cells named after the frame
// base B (ESP after the four register PUSHes); every LEA below is corrected for the PUSHes
// already made before it (LEA [ESP+n] after k PUSHes addresses B+n-4k).
//   prologue Slot22(0x80,  &B20 Positions, &B4c, &B48, &B38 VertCap)   0x0040628b..0x004062a7
//   group    Slot22(0x100, &B40 Weights,   &B58, &B54, &B50)           0x00406340..0x0040635c
//            Slot24(0x100, &B3c WeightIndex, &B5c)                     0x0040635f..0x00406371
//            Slot24(0x80,  &B44 RawIndex,  &B30 IndexCount)            0x00406374..0x00406386
unsigned PartCacheBuildFromMesh(FPartCacheEntry* Entry, IMesh* Mesh, FTransform const* Xform)
{
    unsigned short* Remap = nullptr;     // EDI / [B+0x10]
    unsigned ReturnCursor = 0;           // [B+0x1c], the return value
    unsigned Compacted = 0;              // [B+0x18]
    unsigned WriteCursor = 0;            // [B+0x24]
    float* Positions = nullptr;          // [B+0x20] -- written only by the prologue Slot22

    // 0x00406244..0x00406263: only while IndexCount is set and the index buffer is not.
    if (Entry->IndexCount != 0 && Entry->Indices == nullptr)
    {
        Entry->Indices = static_cast<unsigned short*>(
            operator new(static_cast<std::size_t>(Entry->IndexCount) * 2U));
        Mesh->OpenFaceGroup(0, 1);
        Mesh->OpenFaceGroupChannels(0x80U);

        void* CellB4c = nullptr;
        void* CellB48 = nullptr;
        unsigned VertCap = 0;            // [B+0x38]
        Mesh->GetFaceGroupData(0x80, reinterpret_cast<void**>(&Positions), &CellB4c, &CellB48,
                     reinterpret_cast<void**>(&VertCap));

        Entry->Vertices = static_cast<FPartCacheVertex*>(
            operator new(static_cast<std::size_t>(VertCap) << 4));
        Mesh->CloseFaceGroup();
        Remap = static_cast<unsigned short*>(
            operator new(static_cast<std::size_t>(VertCap) * 2U));
        FillUshortOnes(Remap, VertCap);
    }

    // 0x004062ef: [B+0x14] = 0, then Slot8 fills it; JBE / JC make both tests unsigned.
    int GroupCountCell = 0;
    Mesh->GetFaceGroupCount(&GroupCountCell);
    unsigned const GroupCount = static_cast<unsigned>(GroupCountCell);

    for (unsigned GroupIndex = 0; GroupIndex < GroupCount; ++GroupIndex)
    {
        // 0x00406319: JL -- a negative select result skips the group (no Slot10).
        if (Mesh->OpenFaceGroup(static_cast<int>(GroupIndex), 1) < 0)
            continue;

        // 0x0040632c: -8 from Slot18 unlocks and returns 0 at once. Retail neither frees the
        // remap table nor stores VertexCount on this path (0x004064f8..0x00406507).
        if (Mesh->OpenFaceGroupChannels(0x180U) == -8)
        {
            Mesh->CloseFaceGroup();
            return 0;
        }
        Mesh->LockFaceGroupChannels(0x180U);

        float* Weights = nullptr;        // [B+0x40]
        void* CellB58 = nullptr;
        void* CellB54 = nullptr;
        void* CellB50 = nullptr;
        Mesh->GetFaceGroupData(0x100, reinterpret_cast<void**>(&Weights), &CellB58, &CellB54, &CellB50);

        unsigned short* WeightIndex = nullptr;   // [B+0x3c]
        void* CellB5c = nullptr;
        Mesh->GetFaceGroupIndices(0x100, reinterpret_cast<void**>(&WeightIndex), &CellB5c);

        unsigned short* RawIndices = nullptr;    // [B+0x44]
        unsigned IndexCount = 0;                 // [B+0x30]
        Mesh->GetFaceGroupIndices(0x80, reinterpret_cast<void**>(&RawIndices),
                     reinterpret_cast<void**>(&IndexCount));

        // 0x0040638f: TEST EAX,0xfffffffe / JBE -- fewer than two indices, no pairs.
        if ((IndexCount & 0xFFFFFFFEu) != 0)
        {
            unsigned Pair = 0;           // ECX / [B+0x2c]
            do
            {
                // 0x004063a0..0x004063c1: unless byte +0x1b is set, the pair's weight (float,
                // indexed through WeightIndex[Pair]) against the .rdata DOUBLE 0.1 (DAT_005c7720):
                // FCOMP / TEST AH,0x41 / JNP -- exactly one of C0/C3 set (less or equal, ordered)
                // leaves the group; greater and unordered go on.
                if (Entry->bSkipWeightTest == 0)
                {
                    double const Weight = static_cast<double>(Weights[WeightIndex[Pair]]);
                    if (Weight <= 0.1)
                        break;
                }

                // 0x004063c7: with no index buffer yet (the counting pass) only the cursor moves.
                if (Entry->Indices != nullptr)
                {
                    // [B+0x70] (param_1's slot reused): the byte offset of the pair.
                    unsigned PairOffset = Pair * 4U;
                    for (int PairLeft = 2; PairLeft != 0; --PairLeft)
                    {
                        unsigned const RawIndex = *reinterpret_cast<unsigned short const*>(
                            reinterpret_cast<unsigned char const*>(RawIndices) + PairOffset);
                        unsigned short Mapped = Remap[RawIndex];
                        if (Mapped == 0xFFFFu)
                        {
                            // 0x004063fc..0x00406464: the next compacted slot, the position at
                            // Positions + RawIndex*12 through the math engine's slot 0x28 with
                            // the Xform argument, then the entry's tag at +0xc.
                            Mapped = static_cast<unsigned short>(Compacted);
                            Compacted += 1;
                            Remap[RawIndex] = Mapped;

                            FVector const* Source =
                                reinterpret_cast<FVector const*>(Positions + RawIndex * 3U);
                            FVector Transformed;
                            PartCacheMath()->TransformXform(Transformed, *Xform, *Source);

                            FPartCacheVertex* OutVert = Entry->Vertices + Mapped;
                            OutVert->X = Transformed.X;
                            OutVert->Y = Transformed.Y;
                            OutVert->Z = Transformed.Z;
                            OutVert->Tag = Entry->Tag;
                        }

                        Entry->Indices[WriteCursor] = Mapped;
                        WriteCursor += 1;
                        PairOffset += 2;
                    }
                }

                ReturnCursor += 2;
                Pair += 1;
            } while (Pair < (IndexCount >> 1));
        }

        // 0x004064b4: unlock after the pairs, including the weight break.
        Mesh->CloseFaceGroup();
    }

    // 0x004064cf..0x004064e7.
    if (Entry->Vertices != nullptr)
        Entry->VertexCount = Compacted;
    if (Remap != nullptr)
        operator delete(Remap);
    return ReturnCursor;
}

// FUN_00406200
void PartCacheBuildFromMeshTwice(FPartCacheEntry* Entry, IMesh* Mesh, FTransform const* Xform)
{
    unsigned First = PartCacheBuildFromMesh(Entry, Mesh, Xform);
    Entry->IndexCount = First;
    PartCacheBuildFromMesh(Entry, Mesh, Xform);
}

namespace
{
    // The record search FUN_00405930 / FUN_00405a00 inline for the instance and for every
    // child: the first record whose +0x10 matches, PartCount compared SIGNED (TEST/JLE, then
    // CMP/JL) and re-read per pass; null when none matches.
    FPartCacheEntry* PartCacheFindRecord(FPartCache* Cache, void* Instance)
    {
        int const PartCount = static_cast<int>(Cache->PartCount);
        for (int PartIndex = 0; PartIndex < PartCount; ++PartIndex)
        {
            FPartCacheEntry* Entry = Cache->Entries + PartIndex;
            if (Entry->InstanceId == Instance)
                return Entry;
        }
        return nullptr;
    }

    // FUN_00405a00's per-record body (0x00405a32..0x00405a64 and its child copy at
    // 0x00405aae..0x00405ae0): found, active; a different tag is stored and written into every
    // vertex (+0xc), VertexCount compared signed and re-read per pass.
    void PartCacheActivateRecord(FPartCacheEntry* Entry, unsigned Tag)
    {
        Entry->bActive = 1;
        if (Tag == Entry->Tag)
            return;
        Entry->Tag = Tag;
        for (int VertIndex = 0; VertIndex < static_cast<int>(Entry->VertexCount); ++VertIndex)
            Entry->Vertices[VertIndex].Tag = Tag;
    }
}

// FUN_00405930 (thiscall, RET 8: Instance, bRecurseChildren). No test of the cache or of
// DALib::Engine.
void PartCacheMarkHidden(FPartCache* Cache, void* Instance, bool bRecurseChildren)
{
    void* const kInvalid = reinterpret_cast<void*>(static_cast<ptrdiff_t>(-1));

    FPartCacheEntry* Found = PartCacheFindRecord(Cache, Instance);
    if (Found != nullptr)
        Found->bHidden = 1;

    if (!bRecurseChildren)
        return;

    void* Child = DALib::Engine->GetRelatedInstance(Instance, 0, kInvalid);
    while (Child != kInvalid)
    {
        Found = PartCacheFindRecord(Cache, Child);
        if (Found != nullptr)
            Found->bHidden = 1;
        Child = DALib::Engine->GetRelatedInstance(Instance, 0, Child);
    }
}

// FUN_004059d0 (thiscall): from the second record on, a hidden record loses its active flag.
// PartCount (re-read each pass) compared signed: CMP/JLE against 1, then CMP/JL.
void PartCacheClearHiddenActive(FPartCache* Cache)
{
    for (int PartIndex = 1; PartIndex < static_cast<int>(Cache->PartCount); ++PartIndex)
    {
        FPartCacheEntry* Entry = Cache->Entries + PartIndex;
        if (Entry->bHidden != 0)
            Entry->bActive = 0;
    }
}

// FUN_00405a00 (thiscall, RET 0xc: Instance, bRecurseChildren, Tag). Returns whether any
// record matched ([ESP+0x13]). No test of the cache, of the vertex array or of DALib::Engine.
bool PartCacheActivatePart(FPartCache* Cache, void* Instance, bool bRecurseChildren, unsigned Tag)
{
    void* const kInvalid = reinterpret_cast<void*>(static_cast<ptrdiff_t>(-1));
    bool bFound = false;

    FPartCacheEntry* Found = PartCacheFindRecord(Cache, Instance);
    if (Found != nullptr)
    {
        bFound = true;
        PartCacheActivateRecord(Found, Tag);
    }

    if (!bRecurseChildren)
        return bFound;

    void* Child = DALib::Engine->GetRelatedInstance(Instance, 0, kInvalid);
    while (Child != kInvalid)
    {
        Found = PartCacheFindRecord(Cache, Child);
        if (Found != nullptr)
        {
            bFound = true;
            PartCacheActivateRecord(Found, Tag);
        }
        Child = DALib::Engine->GetRelatedInstance(Instance, 0, Child);
    }
    return bFound;
}

// FUN_00405b10 (thiscall on the cache, RET 8: Instance, Tag).
bool PartCacheBuild(FPartCache* Cache, void* Instance, unsigned Tag)
{
    void* const kInvalid = reinterpret_cast<void*>(static_cast<ptrdiff_t>(-1));

    // 0x00405b33: an unset instance returns false before anything is touched.
    if (Instance == kInvalid)
        return false;

    IEngine* Engine = DALib::Engine;

    // 0x00405b42..0x00405b7f: FUN_004058e0's body inlined -- tear down any prior contents.
    if (Cache->Entries != nullptr)
    {
        unsigned* Prefix = reinterpret_cast<unsigned*>(Cache->Entries) - 1;
        unsigned Count = *Prefix;
        unsigned Index = 0;
        for (; Index < Count; ++Index)
            PartCacheEntryDestroy(Cache->Entries + Index);
        operator delete(Prefix);
    }
    operator delete(Cache->MergedIndices);
    Cache->Entries = nullptr;
    Cache->PartCount = 0;
    Cache->TotalIndices = 0;
    Cache->TotalVertices = 0;
    Cache->MergedIndices = nullptr;

    // Save current transform, force identity, UpdateInstance.
    FTransform SavedTransform;
    float* TransformPtr = Engine->GetInstanceTransform(Instance);
    CopyTransform12(&SavedTransform, reinterpret_cast<FTransform const*>(TransformPtr));

    FTransform Identity;
    MakeIdentityTransform(&Identity);
    Engine->SetInstanceMatrix(Instance, &Identity);
    Engine->UpdateInstance(Instance, 0, nullptr);

    // Count related instances (root + children). First cursor is -1 (listing 00405c30).
    Cache->PartCount = 1;
    void* Child = Engine->GetRelatedInstance(Instance, 0, kInvalid);
    while (Child != kInvalid)
    {
        Cache->PartCount += 1;
        Child = Engine->GetRelatedInstance(Instance, 0, Child);
    }

    unsigned PartCount = Cache->PartCount;
    unsigned* Prefix = static_cast<unsigned*>(
        operator new(static_cast<std::size_t>(PartCount) * sizeof(FPartCacheEntry) + 4U));
    FPartCacheEntry* Entries = nullptr;
    if (Prefix != nullptr)
    {
        *Prefix = PartCount;
        Entries = reinterpret_cast<FPartCacheEntry*>(Prefix + 1);
        unsigned Index = 0;
        for (; Index < PartCount; ++Index)
            PartCacheEntryConstruct(Entries + Index);
    }

    // 0x00405cb6..0x00405cde: no test of the array -- retail writes the root record through it
    // whatever operator new returned.
    Cache->Entries = Entries;
    Cache->TotalVertices = 0;
    Cache->TotalIndices = 0;

    // Root entry.
    Entries[0].Tag = Tag;
    Entries[0].InstanceId = Instance;
    Entries[0].bSkipWeightTest = 0;

    FTransform WorldXform;
    TransformPtr = Engine->GetInstanceTransform(Instance);
    CopyTransform12(&WorldXform, reinterpret_cast<FTransform const*>(TransformPtr));

    {
        // EH state 1 ([ESP+0x14]) and, inside the IMesh branch, state 2 ([ESP+0x10]): both are
        // FUN_00526a20 release-and-clear holders, destroyed Mesh first, then Wire.
        FPartComHolder Wire;
        if (Engine->QueryInstance(Entries[0].InstanceId, Iids::VMeshWire,
                                  reinterpret_cast<void**>(&Wire.Ptr)) >= 0)
        {
            PartCacheBuildFromWire(&Entries[0], static_cast<IVMeshWire*>(Wire.Ptr), &WorldXform);
        }
        else
        {
            FPartComHolder Mesh;
            if (Engine->QueryInstance(Entries[0].InstanceId, Iids::Mesh,
                                      reinterpret_cast<void**>(&Mesh.Ptr)) >= 0)
                PartCacheBuildFromMeshTwice(&Entries[0], static_cast<IMesh*>(Mesh.Ptr),
                                            &WorldXform);
        }
    }

    Cache->TotalIndices += Entries[0].IndexCount;
    Cache->TotalVertices += Entries[0].VertexCount;

    // 0x00405db6..0x00405ee2: the children, one record each, until GetRelatedInstance hands
    // back -1. No bound against PartCount -- the counting walk above saw the same chain.
    FPartCacheEntry* Entry = Entries + 1;
    Child = Engine->GetRelatedInstance(Instance, 0, kInvalid);
    while (Child != kInvalid)
    {
        Entry->Tag = Tag;
        Entry->InstanceId = Child;
        Entry->bSkipWeightTest = 0;

        TransformPtr = Engine->GetInstanceTransform(Child);
        CopyTransform12(&WorldXform, reinterpret_cast<FTransform const*>(TransformPtr));

        {
            // EH states 3 / 4, the same two holders as the root's.
            FPartComHolder Wire;
            if (Engine->QueryInstance(Entry->InstanceId, Iids::VMeshWire,
                                      reinterpret_cast<void**>(&Wire.Ptr)) >= 0)
            {
                PartCacheBuildFromWire(Entry, static_cast<IVMeshWire*>(Wire.Ptr), &WorldXform);
            }
            else
            {
                FPartComHolder Mesh;
                if (Engine->QueryInstance(Entry->InstanceId, Iids::Mesh,
                                          reinterpret_cast<void**>(&Mesh.Ptr)) >= 0)
                    PartCacheBuildFromMeshTwice(Entry, static_cast<IMesh*>(Mesh.Ptr),
                                                &WorldXform);
            }
        }

        Cache->TotalIndices += Entry->IndexCount;
        Cache->TotalVertices += Entry->VertexCount;

        Entry += 1;
        Child = Engine->GetRelatedInstance(Instance, 0, Child);
    }

    // 0x00405eef..0x00405f17: restore the saved transform.
    Engine->SetInstanceMatrix(Instance, &SavedTransform);
    Engine->UpdateInstance(Instance, 0, nullptr);

    // 0x00405f1a: CMP EAX,0x3e80 / JLE -- a SIGNED compare against 16000 indices.
    if (static_cast<int>(Cache->TotalIndices) > 0x3E80)
    {
        FDUMP(0x100004,
              "%s(%d) : TRACE_1:General:%s",
              "E:\\FL\\Scratch\\Source\\Client\\cmn\\cached_wire.cpp",
              0xE0,
              "way too many indicies in the wireframe object");
        PartCacheClear(Cache);
        return false;
    }

    Cache->MergedIndices = static_cast<unsigned short*>(
        operator new(static_cast<std::size_t>(Cache->TotalIndices) * 2U));
    return true;
}

namespace
{
    // DAT_005c7608 / DAT_005c7628 / DAT_005c7640: {render state, value} pairs ended by state 0.
    // 0x89 D3DRS_LIGHTING, 0x1d D3DRS_SPECULARENABLE, 0x1b D3DRS_ALPHABLENDENABLE,
    // 0x8d D3DRS_COLORVERTEX, 0x91 D3DRS_DIFFUSEMATERIALSOURCE.
    unsigned long const kPartDrawStatesOff[] = {0x89, 0, 0x1d, 0, 0x1b, 0, 0};
    unsigned long const kPartDrawColourVertexOn[] = {0x8d, 1, 0x91, 1, 0};
    unsigned long const kPartDrawColourVertexOff[] = {0x8d, 0, 0};
    // DAT_005c7650: {texture stage state, value} pairs for stage 0, ended by type 0 -- COLOROP
    // SELECTARG1, COLORARG1 DIFFUSE, COLORARG2 CURRENT, ALPHAOP SELECTARG1, ALPHAARG1 DIFFUSE,
    // ALPHAARG2 CURRENT.
    unsigned long const kPartDrawStage0[] = {1, 1, 2, 2, 3, 1, 4, 1, 5, 2, 6, 1, 0};

    // FUN_00406510's loops: the first pair's state is an immediate, then [cursor + 8] until 0.
    void PartDrawApplyStates(IRenderPipeline* Pipeline, unsigned long const* Table)
    {
        unsigned long State = Table[0];
        unsigned long const* Cursor = Table;
        while (State != 0)
        {
            Pipeline->SetRenderState((D3DRENDERSTATETYPE)State, static_cast<int>(Cursor[1]));
            State = Cursor[2];
            Cursor += 2;
        }
    }
}

void PartCacheDraw(FPartCache* Cache, ICamera* Camera, FTransform const* Xform)
{
    // FUN_00406510.
    PartDrawApplyStates(DALib::Pipeline, kPartDrawStatesOff);
    DALib::Pipeline->SetTextureStageTexture(0, nullptr);
    PartDrawApplyStates(DALib::Pipeline, kPartDrawColourVertexOn);
    if (Cache->TotalVertices == 0)
    {
        return;
    }

    // 0x00406593: the constant part of the source block, then the lock. The result of the lock
    // is not tested.
    // The source block lives on retail's stack at [ESP+0x34]; only the fields below are ever
    // written (the normal and texcoord streams stay as the stack left them).
    FRpVertexSrcDesc Source;
    Source.Flags = 2;
    Source.Position.ByteSize = 0xc;
    Source.Position.Stride = 0x10;
    Source.Position.IndexArray = nullptr;
    Source.Diffuse.ByteSize = 4;
    Source.Diffuse.Stride = 0x10;
    Source.Diffuse.IndexArray = nullptr;
    Source.SrcFvf = 0x42;
    unsigned IndexTotal = 0;
    FVertexMemoryLock LockOut; // vertex buffer, FVF, base vertex, memory
    DALib::VertexBuffer->LockVertexMemory(0x42, Cache->TotalVertices, &LockOut);

    // 0x004065e6: the stride of the locked FVF -- position 0x1c / 0x10 / 0xc by bits 0xc, 4, 2,
    // normal 0xc, diffuse 4, specular 4, then the texcoords (FUN_004061a0).
    unsigned const Fvf = static_cast<unsigned>(LockOut.Listing.Fvf);
    int PositionBytes = 0;
    if ((Fvf & 0xcu) == 0xcu)
    {
        PositionBytes = 0x1c;
    }
    else if ((Fvf & 4u) == 4u)
    {
        PositionBytes = 0x10;
    }
    else if ((Fvf & 2u) == 2u)
    {
        PositionBytes = 0xc;
    }
    int NormalBytes = 0;
    if ((Fvf & 0x10u) != 0)
    {
        NormalBytes = 0xc;
    }
    int ColourBytes = 0;
    if ((Fvf & 0x40u) != 0)
    {
        ColourBytes = 4;
    }
    if ((Fvf & 0x80u) != 0)
    {
        ColourBytes += 4;
    }
    int const Stride = FvfTexcoordBlockBytes(Fvf) + ColourBytes + NormalBytes + PositionBytes;

    // 0x00406677: every active part -- its indices rebased by the vertices already placed (a
    // 16-bit add) appended to MergedIndices, its vertices copied after them.
    unsigned VertexTotal = 0;
    FPartCacheEntry* Entry = Cache->Entries;
    for (int PartIndex = 0; PartIndex < static_cast<int>(Cache->PartCount); ++PartIndex, ++Entry)
    {
        if (Entry->bActive == 0)
        {
            continue;
        }
        unsigned short* Merged = Cache->MergedIndices + IndexTotal;
        for (int Index = 0; Index < static_cast<int>(Entry->IndexCount); ++Index)
        {
            Merged[Index] = static_cast<unsigned short>(
                Entry->Indices[Index] + static_cast<unsigned short>(VertexTotal));
        }
        IndexTotal += Entry->IndexCount;

        Source.Position.Base = reinterpret_cast<unsigned char const*>(Entry->Vertices);
        Source.Position.Count = Entry->VertexCount;
        Source.Diffuse.Base = reinterpret_cast<unsigned char const*>(Entry->Vertices) + 0xc;
        Source.Diffuse.Count = Entry->VertexCount;
        Source.SrcCount = Entry->VertexCount;
        unsigned char* const Dest =
            (unsigned char*)LockOut.Vertices + Stride * static_cast<int>(VertexTotal);
        DALib::VertexBuffer->CopyVertexData(
            Dest, LockOut.Listing.Fvf, &Source, 0, Entry->VertexCount);
        VertexTotal += Entry->VertexCount;
    }
    DALib::VertexBuffer->UnlockVertexMemory(&LockOut);

    unsigned long const* Stage = kPartDrawStage0;
    unsigned long Type = Stage[0];
    while (Type != 0)
    {
        DALib::Pipeline->SetTextureStageState(0, static_cast<int>(Type), static_cast<int>(Stage[1]));
        Type = Stage[2];
        Stage += 2;
    }

    // 0x0040675a: pipeline state 7 (seeded 1 before the query) picks one combined modelview --
    // the camera's view (slot 0x40) times Xform through the math engine (DAT_0061649c, the
    // TU's lazily acquired 3DMathEngine) -- or a separate world and view.
    int bCombined = 1;
    DALib::Pipeline->GetPipelineState(7, &bCombined);
    if (bCombined != 0)
    {
        FTransform const* const View =
            reinterpret_cast<FTransform const*>(Camera->GetViewTransform());
        FTransform Product;
        PartCacheMath()->MulTransform(Product, *View, *Xform);
        FTransform Modelview = Product;
        DALib::Pipeline->SetModelview(reinterpret_cast<float const*>(&Modelview));
    }
    else
    {
        DALib::Pipeline->SetWorld(reinterpret_cast<float const*>(Xform));
        DALib::Pipeline->SetView(Camera->GetViewTransform());
    }

    // D3DPT_LINELIST over everything placed above.
    DALib::Pipeline->DrawIndexedPrimitiveVb(2, LockOut.Listing.VertexBuffer,
                                            static_cast<unsigned>(LockOut.Listing.BaseVertex), VertexTotal,
                                            Cache->MergedIndices, IndexTotal, 0);
    PartDrawApplyStates(DALib::Pipeline, kPartDrawColourVertexOff);
}

// FUN_00402580
int BindRendererLods(void* Instance, int PreferredLod, FRendererLodMap* Saved)
{
    int LargestMax = 0; // [ESP+0x10]
    if (Instance == reinterpret_cast<void*>(-1))
        return 0;

    // 004025a6..004025e4: Slot9 -> FUN_00402790 insert(Instance, 0), node value = the LOD.
    if (Saved != nullptr)
    {
        int Current = 0;
        if (DALib::Renderer->GetInstanceCurrentLod(Instance, &Current) >= 0)
            (*Saved)[Instance] = Current;
    }

    int LodCount = 0;
    DALib::Renderer->GetInstanceLodCount(Instance, &LodCount);
    int MaxIndex = 0;
    if (LodCount > 0)
        MaxIndex = LodCount - 1;
    int Lod = PreferredLod;
    if (PreferredLod > MaxIndex)
        Lod = MaxIndex;
    DALib::Renderer->SetInstanceLod(Instance, Lod);
    if (MaxIndex > 0)
        LargestMax = MaxIndex;

    void* Child = DALib::Engine->GetRelatedInstance(Instance, 0, reinterpret_cast<void*>(-1));
    while (Child != reinterpret_cast<void*>(-1))
    {
        if (Saved != nullptr)
        {
            int Current = 0;
            if (DALib::Renderer->GetInstanceCurrentLod(Child, &Current) >= 0)
                (*Saved)[Child] = Current;
        }

        int ChildCount = 0;
        DALib::Renderer->GetInstanceLodCount(Child, &ChildCount);
        int ChildMax = 0;
        if (ChildCount > 0)
            ChildMax = ChildCount - 1;
        // 004026c6..004026d1: the child clamp is max index + 1, not max index.
        int ChildLod = PreferredLod;
        if (PreferredLod > ChildMax + 1)
            ChildLod = ChildMax + 1;
        DALib::Renderer->SetInstanceLod(Child, ChildLod);
        if (ChildMax > LargestMax)
            LargestMax = ChildMax;

        Child = DALib::Engine->GetRelatedInstance(Instance, 0, Child);
    }
    return LargestMax;
}

// FUN_00402720
void ApplyRendererDetailQueue(void* /*UnusedInstance*/, FRendererLodMap const* Saved)
{
    for (FRendererLodMap::const_iterator It = Saved->begin(); It != Saved->end(); ++It)
    {
        void* const Inst = It->first;
        int DetailIndex = It->second;

        int LodCount = 0;
        DALib::Renderer->GetInstanceLodCount(Inst, &LodCount);
        int MaxIndex = 0;
        if (LodCount > 0)
            MaxIndex = LodCount - 1;
        if (DetailIndex > MaxIndex)
            DetailIndex = MaxIndex;

        DALib::Renderer->SetInstanceLod(Inst, DetailIndex);
    }
}
