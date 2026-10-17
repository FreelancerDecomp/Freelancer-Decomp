// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_phyarch.h"

#include "common_ini.h"
#include "DALib.h"
#include "dacom.h"
#include "readfile.h"

#include <map>
#include <new>
#include <stdio.h>
#include <string.h>
#include <utility>
#include <vector>

// IVP's 16-byte aligned block allocator (FUN_06343fc0(size, align)) and its release
// (FUN_06343ff0): ivp_malloc_aligned / ivp_free_aligned.
#include "../IVP/ivp_utility/ivp_malloc.h"
#include "../IVP/ivp_collision/ivp_compact_surface.h" // max_deviation_and_size (+0x1c)

int CallDacomStricmp(const char* Left, const char* Right); // common_dacom_stricmp.cpp

namespace
{
    // .rdata:0639b81c
    const char kPhyArchSource[] = "E:\\FL\\Scratch\\Source\\Common\\PhyArch.cpp";
    // .rdata:0639b6f4: the only .sur version LoadSurfaces accepts.
    const float kSurVersion = 2.0f;

    typedef std::map<unsigned, PhyArch::Part> FPartMap;

    // A compact surface's byte size: the dword at +0x1c, arithmetic shift right by 8
    // (.text:0627fc42..0627fc45, SAR).
    unsigned PhyArch_SurfaceByteSize(IVP_Compact_Surface const* Surface)
    {
        int const SizeAndFlags = (int)Surface->max_deviation_and_size;   // +0x1c
        return (unsigned)(SizeAndFlags >> 8);
    }

    // IFileSystem slot +0x14 with handle 0 and no position: Read(0, Buffer, Size, &Got, 0).
    int PhyArch_Read(IFileSystem* Filesys, void* Buffer, unsigned long Size, unsigned long* Got)
    {
        return Filesys->Read(0, Buffer, Size, Got, 0);
    }

    // The inline release every LoadSurfaces exit performs: `if (fs) fs->vtbl[2]()`.
    void PhyArch_Release(IFileSystem* Filesys)
    {
        if (Filesys != nullptr)
            Filesys->Release();
    }
}

// .text:0627f9b0..0627f9cc: byte +0x00 = 1; +0x04 = 0; the vector's allocator byte, then
// begin / end / capacity = 0.
PhyArch::Part::Part()
    : bNotFixed(1)
    , Surface(nullptr)
{
    // Very strange, but retail: +0x08..+0x1f (both extents) are not written here, so a part
    // without an 'exts' chunk keeps whatever the storage held. Reproduced: the port does not
    // write them either. (The VC6 allocator byte at +0x20 -- a stack byte copied into a
    // stateless allocator -- has no counterpart in the port's std::vector.)
}

// .text:0626c600..0626c69c: byte, surface pointer (shallow), both extents, then a fresh
// vector copy of the hardpoint ids.
PhyArch::Part::Part(Part const& Other)
    : bNotFixed(Other.bNotFixed)
    , Surface(Other.Surface)
    , ExtentsMin(Other.ExtentsMin)
    , ExtentsMax(Other.ExtentsMax)
    , HardpointIds(Other.HardpointIds)
{
}

// .text:0627f9d0..0627fa29: a surface is released through FUN_06343ff0, then the vector
// buffer is freed.
PhyArch::Part::~Part()
{
    if (Surface != nullptr)
        ivp_free_aligned(Surface); // FUN_06343ff0
}

// .text:0627fa90..0627fc76.
PhyArch::Part& PhyArch::Part::operator=(Part const& Other)
{
    // Very strange, but retail: there is no self-assignment test. On `a = a` the own surface
    // is freed here and then read back below as the source (size and bytes from the freed
    // block) -- reproduced; what the freed block still holds is not.
    // .text:0627fa94..0627faa8: the own surface is released (the pointer is not cleared).
    if (Surface != nullptr)
        ivp_free_aligned(Surface); // FUN_06343ff0

    // .text:0627faab..0627fae5: byte and both extents.
    bNotFixed = Other.bNotFixed;
    ExtentsMin = Other.ExtentsMin;
    ExtentsMax = Other.ExtentsMax;

    // .text:0627fae8..0627fc37: vector assignment (skipped for the same vector).
    HardpointIds = Other.HardpointIds;

    // .text:0627fc3b..0627fc6d: a source surface is duplicated into a new 16-byte aligned block
    // of its own size (REP MOVSD + MOVSB).
    if (Other.Surface != nullptr)
    {
        unsigned const Size = PhyArch_SurfaceByteSize(Other.Surface);
        IVP_Compact_Surface* const Copy = (IVP_Compact_Surface*)ivp_malloc_aligned(Size, 0x10); // FUN_06343fc0
        memcpy(Copy, Other.Surface, Size);
        Surface = Copy;
    }
    // Very strange, but retail (0627fc40 JZ straight to the epilogue): without a source
    // surface +0x04 keeps the pointer just freed above, so a later destructor or clear()
    // frees it again. Reproduced -- the port does not clear it either.
    return *this;
}

// .text:0627fa30..0627fa83: byte = 1; a surface is released and cleared; +0x08..+0x1f = 0;
// the hardpoint vector is erased (end = begin).
void PhyArch::Part::clear()
{
    bNotFixed = 1;
    if (Surface != nullptr)
    {
        ivp_free_aligned(Surface); // FUN_06343ff0
        Surface = nullptr;
    }
    ExtentsMin.x = 0.0f;
    ExtentsMin.y = 0.0f;
    ExtentsMin.z = 0.0f;
    ExtentsMax.x = 0.0f;
    ExtentsMax.y = 0.0f;
    ExtentsMax.z = 0.0f;
    HardpointIds.clear();
}

// .text:0627fc80..0628023d.
bool __cdecl PhyArch::LoadSurfaces(char const* SurPath, std::map<unsigned int, Part>& Parts)
{
    // .text:0627fca9..0627fd25: a "DOS" FileSystem over SurPath through DALib::Data's
    // CreateInstance (+0x0c). The descriptor is 0x34 bytes, +0x08..+0x33 zeroed first.
    FFileSystemDesc Desc;
    memset(&Desc, 0, sizeof(Desc));
    Desc.Size = 0x34;
    Desc.ComponentName = "FileSystem";
    Desc.Implementation = "DOS";
    Desc.Path = SurPath;
    Desc.Access = 0x80000000;
    Desc.ShareMode = 1;
    Desc.Creation = 3;
    Desc.Flags = 0x8000080;
    Desc.Field30 = 0xFFFFFFFF;
    IFileSystem* Filesys = nullptr;
    // FFileSystemDesc begins with AGGDESC's {size, interface name} (readfile.h).
    if (DALib::Data->CreateInstance((AGGDESC*)&Desc, (void**)&Filesys) != 0)
    {
        // .text:062801f6..0628020b with BL = 0.
        PhyArch_Release(Filesys);
        return false;
    }

    // .text:0627fd30..0627fd60: the first tag; a failed or short read fails silently. The
    // version is preset to 1.0f (.text:0627fd46) and read only after a "vers" tag.
    unsigned long Got = 0;
    char Tag[5];
    float Version = 1.0f;
    int ReadResult = PhyArch_Read(Filesys, Tag, 4, &Got);
    if (ReadResult == 0 || Got != 4)
    {
        PhyArch_Release(Filesys);
        return false;
    }
    Tag[4] = '\0';
    if (CallDacomStricmp(Tag, "vers") == 0)
    {
        // .text:0627fd84..0627fdc4: a failed or short version read fails silently.
        ReadResult = PhyArch_Read(Filesys, &Version, 4, &Got);
        if (ReadResult == 0 || Got != 4)
        {
            PhyArch_Release(Filesys);
            return false;
        }
    }

    // .text:0627fdc9..0627fe26: FCOMP float 2.0f; TEST AH,0x44; JNP continues only when equal
    // (C3 alone); less, greater and unordered warn and fail.
    if (!(Version == kSurVersion))
    {
        FDUMP(0x100002, "%s(%d) : *** WARNING: %s uses old format, must be rebuilt to version %f.  Non-ignorable.",
              kPhyArchSource, 0xb1, SurPath, (double)kSurVersion);
        PhyArch_Release(Filesys);
        return false;
    }

    // The unchecked reads below land in stack slots retail never re-initialises: the chunk
    // count [ESP+0x38], the surface size [ESP+0x34], the hardpoint count [ESP+0x3c] and the
    // chunk tag [ESP+0x20] (a buffer of its own, not the "vers" tag at [ESP+0x44]). A failed
    // read keeps the value of the previous part / chunk, so they live for the whole call.
    // KNOWN retail UB: before their first successful read they are uninitialised stack; the
    // port starts them at 0.
    unsigned PartId = 0;
    int ChunkCount = 0;
    unsigned SurfaceSize = 0;
    int HardpointCount = 0;
    char ChunkTag[5] = {};

    for (;;)
    {
        // .text:0627fe30..0627fe55: the next part id; a failed or short read ends the file
        // successfully (.text:062801f1, BL = 1).
        ReadResult = PhyArch_Read(Filesys, &PartId, 4, &Got);
        if (ReadResult == 0 || Got != 4)
        {
            PhyArch_Release(Filesys);
            return true;
        }

        // .text:0627fe5b..0627ff29: a default Part, copied into a (PartId, Part) pair and
        // inserted (FUN_06280560); the temporaries are destroyed.
        std::pair<FPartMap::iterator, bool> const Inserted = Parts.insert(FPartMap::value_type(PartId, Part()));
        Part& Current = Inserted.first->second;
        if (!Inserted.second)
        {
            // .text:0627ff3a..0627ff9b: a duplicate id is reported and the existing part
            // cleared (Part::clear inlined).
            FDUMP(0x100001, "%s(%d) : *** ERROR: Error reading %s. Duplicate part ids: %u. Second used.",
                  kPhyArchSource, 0xc6, SurPath, PartId);
            Current.clear();
        }

        // .text:0627ffa0..0627ffc0: the chunk count, read without a result test (a failed
        // read keeps the previous count, see above); JLE / JL are signed.
        PhyArch_Read(Filesys, &ChunkCount, 4, &Got);
        for (int Chunk = 0; Chunk < ChunkCount; ++Chunk)
        {
            // .text:0627ffc6..0627ffe8: the chunk tag, read without a result test.
            PhyArch_Read(Filesys, ChunkTag, 4, &Got);
            ChunkTag[4] = '\0';

            if (CallDacomStricmp(ChunkTag, "surf") == 0)
            {
                // .text:0627fff6..06280034: byte count, a 16-byte aligned block of that size,
                // the surface read into it, stored at +0x04 (a previous surface is not freed).
                // Both reads use their own out-count slot [ESP+0x50], never tested.
                unsigned long SurfGot = 0;
                PhyArch_Read(Filesys, &SurfaceSize, 4, &SurfGot);
                IVP_Compact_Surface* const SurfaceBlock =
                    (IVP_Compact_Surface*)ivp_malloc_aligned(SurfaceSize, 0x10); // FUN_06343fc0
                PhyArch_Read(Filesys, SurfaceBlock, SurfaceSize, &SurfGot);
                Current.Surface = SurfaceBlock;
            }
            else if (CallDacomStricmp(ChunkTag, "exts") == 0)
            {
                // .text:0628004f..0628007a: two 12-byte reads into +0x08 and +0x14.
                PhyArch_Read(Filesys, &Current.ExtentsMin, 0xc, &Got);
                PhyArch_Read(Filesys, &Current.ExtentsMax, 0xc, &Got);
            }
            else if (CallDacomStricmp(ChunkTag, "!fxd") == 0)
            {
                // .text:06280095: the byte gets AL, the stricmp result 0.
                Current.bNotFixed = 0;
            }
            else if (CallDacomStricmp(ChunkTag, "hpid") == 0)
            {
                // .text:062800b4..06280181: a count, then each id appended unless present.
                PhyArch_Read(Filesys, &HardpointCount, 4, &Got);
                for (int Index = 0; Index < HardpointCount; ++Index)
                {
                    unsigned HardpointId = 0;  // .text:062800f2: zeroed before every read
                    PhyArch_Read(Filesys, &HardpointId, 4, &Got);
                    std::vector<unsigned>::iterator Found = Current.HardpointIds.begin();
                    for (; Found != Current.HardpointIds.end(); ++Found)
                    {
                        if (*Found == HardpointId)
                            break;
                    }
                    if (Found == Current.HardpointIds.end())
                        Current.HardpointIds.insert(Current.HardpointIds.end(), HardpointId);
                }
            }
            else
            {
                // .text:062801a7..062801ef: an unknown chunk aborts the file.
                FDUMP(0x100001, "%s(%d) : *** ERROR: File(%s) Part(%u) contains unrecognized info type. Aborting.",
                      kPhyArchSource, 0xfe, SurPath, PartId);
                PhyArch_Release(Filesys);
                return false;
            }
        }
    }
}
