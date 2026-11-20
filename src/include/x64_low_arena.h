// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

#pragma once

// Fixed-size blocks below 2 GB, for the port-only x64 build (docs/x64.md).
//
// Retail code keeps some engine handles, which are addresses, in 32-bit `long` / `int` fields
// and turns them back into pointers with a sign-extending cast: Common's
// Archetype::Root::EngineIndex and Munition::BEAM_ARCH_INDEX, among about 225 uses. On x64 an
// address only survives that round trip when it is below 2 GB. EngBase allocates its archetype,
// instance and transform handles here on x64, so those fields stay as retail declared them.
// Win32 does not use this header's allocator: there every address already fits.
//
// The arena reserves and commits 16 MB chunks of free address space below 2 GB, hands out
// blocks by bumping a cursor, and keeps freed blocks on a list per block size for reuse. It
// never returns memory to the system. With FL_HIGH_MEMORY=1 (x64_high_memory.h) all other
// space below 4 GB is reserved at startup; the window below is left free for the arena.

#include <cstddef>
#include <cstdint>

#if defined(_WIN64)
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#endif

namespace LowArena
{
    // The space FL_HIGH_MEMORY leaves free so the arena still finds room below 2 GB.
    inline constexpr std::uintptr_t WindowBase = 0x60000000;
    inline constexpr std::uintptr_t WindowSize = 0x10000000; // 256 MB
    inline constexpr std::uintptr_t Limit = 0x80000000;      // a sign-extended 32-bit value stays positive

#if defined(_WIN64)
    namespace Detail
    {
        inline constexpr std::size_t ChunkSize = 16 << 20;
        inline constexpr std::uintptr_t Granularity = 0x10000; // VirtualAlloc reserves in 64 KB units

        struct FState
        {
            std::mutex Lock;
            char* Cursor = nullptr;
            char* End = nullptr;
            std::map<std::size_t, void*> FreeHeads; // block size -> first free block
        };

        inline FState& State()
        {
            static FState Instance;
            return Instance;
        }

        // The first free range below Limit that holds a whole chunk, reserved and committed.
        inline bool NewChunk(FState& Arena)
        {
            std::uintptr_t Address = Granularity;
            MEMORY_BASIC_INFORMATION Info;
            while (Address < Limit && VirtualQuery((void*)(Address), &Info, sizeof(Info)) == sizeof(Info))
            {
                const std::uintptr_t Base = (std::uintptr_t)(Info.BaseAddress);
                const std::uintptr_t End = Base + Info.RegionSize;
                if (Info.State == MEM_FREE)
                {
                    const std::uintptr_t Start = (Base + Granularity - 1) & ~(Granularity - 1);
                    const std::uintptr_t Stop = End < Limit ? End : Limit;
                    if (Start < Stop && Stop - Start >= ChunkSize)
                    {
                        void* Chunk = VirtualAlloc((void*)(Start), ChunkSize, MEM_RESERVE | MEM_COMMIT,
                                                   PAGE_READWRITE);
                        if (Chunk != nullptr)
                        {
                            Arena.Cursor = (char*)(Chunk);
                            Arena.End = Arena.Cursor + ChunkSize;
                            return true;
                        }
                    }
                }
                Address = End;
            }
            return false;
        }

        inline std::size_t BlockSize(std::size_t Size)
        {
            const std::size_t Rounded = (Size + 15) & ~(std::size_t)(15);
            return Rounded != 0 ? Rounded : 16;
        }
    }

    // A block of Size bytes below 2 GB, not cleared. Running out of space below 2 GB ends the
    // process: falling back to a high address would bring the truncation back silently.
    inline void* Allocate(std::size_t Size)
    {
        const std::size_t Block = Detail::BlockSize(Size);
        Detail::FState& Arena = Detail::State();
        std::lock_guard<std::mutex> Guard(Arena.Lock);
        void*& FreeHead = Arena.FreeHeads[Block];
        if (FreeHead != nullptr)
        {
            void* Element = FreeHead;
            FreeHead = *(void**)(Element);
            return Element;
        }
        if (Arena.Cursor == nullptr || (std::size_t)(Arena.End - Arena.Cursor) < Block)
        {
            if (!Detail::NewChunk(Arena))
            {
                std::fprintf(stderr, "LowArena: no free address space below 2 GB\n");
                std::abort();
            }
        }
        void* Element = Arena.Cursor;
        Arena.Cursor = Arena.Cursor + Block;
        return Element;
    }

    // Returns a block from Allocate; Size is the size it was allocated with.
    inline void Free(void* Element, std::size_t Size)
    {
        if (Element == nullptr)
            return;
        const std::size_t Block = Detail::BlockSize(Size);
        Detail::FState& Arena = Detail::State();
        std::lock_guard<std::mutex> Guard(Arena.Lock);
        void*& FreeHead = Arena.FreeHeads[Block];
        *(void**)(Element) = FreeHead;
        FreeHead = Element;
    }
#endif
}
