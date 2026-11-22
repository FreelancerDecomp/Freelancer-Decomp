// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

#pragma once

// Fixed-size blocks below 2 GB, for the x64 build (PORT(x64), docs/x64.md).
//
// Retail code keeps some engine handles, which are addresses, in 32-bit `long` / `int` fields
// and turns them back into pointers with a sign-extending cast: Common's
// Archetype::Root::EngineIndex and Munition::BEAM_ARCH_INDEX, among about 225 uses. On x64 an
// address only survives that round trip when it is below 2 GB. EngBase allocates its archetype,
// instance and transform handles here on x64, so those fields stay as retail declared them.
// Win32 does not use this header's allocator: there every address already fits.
//
// FL_LOW_ARENA_NEW, inside a class, gives that class operator new / delete from the arena on
// x64 and nothing on Win32. Plain new ends the process when the arena is full (Allocate); new
// (std::nothrow) returns null instead (TryAllocate), so its callers' null checks hold. RendComp's render plugins use it for the handles they return through
// IRenderComponent::Claim, which retail declared as `int`.
//
// The arena reserves and commits 16 MB chunks of free address space below 2 GB, hands out
// blocks by bumping a cursor, and keeps freed blocks on a list per block size for reuse. It
// never returns memory to the system. With FL_HIGH_MEMORY=1 (x64_high_memory.h) all other
// space below 4 GB is reserved at startup; the window below is left free for the arena.

#include <cstddef>
#include <cstdint>
#include <new>

#if defined(_WIN64)
#include <windows.h>
#elif defined(FL_PTR64)
#include <sys/mman.h>
#endif

#if defined(FL_PTR64)
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

#if defined(FL_PTR64)
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

#if defined(_WIN64)
        // The first free range below Limit that holds a whole chunk, reserved and committed. A
        // block larger than a chunk gets a chunk of its own size.
        inline bool NewChunk(FState& Arena, std::size_t MinBytes)
        {
            const std::size_t Bytes =
                MinBytes > ChunkSize ? (MinBytes + Granularity - 1) & ~(std::size_t)(Granularity - 1)
                                     : ChunkSize;
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
                    if (Start < Stop && Stop - Start >= Bytes)
                    {
                        void* Chunk = VirtualAlloc((void*)(Start), Bytes, MEM_RESERVE | MEM_COMMIT,
                                                   PAGE_READWRITE);
                        if (Chunk != nullptr)
                        {
                            Arena.Cursor = (char*)(Chunk);
                            Arena.End = Arena.Cursor + Bytes;
                            return true;
                        }
                    }
                }
                Address = End;
            }
            return false;
        }
#else
        // PORT(macOS, Linux): a 64-bit macOS process has no address below 4 GB, so the arena is
        // one reservation of Limit bytes wherever the system puts it, and the 32-bit fields hold
        // offsets from its start (Encode, Decode). Chunks are committed from it in order; the
        // first Granularity bytes are never handed out, so offset 0 stays null.
        struct FReservation
        {
            char* Base = nullptr;
            char* Next = nullptr;
            char* End = nullptr;
        };

        inline FReservation& Reservation()
        {
            static FReservation Instance = [] {
                FReservation Made;
                void* const Base = mmap(nullptr, Limit, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
                if (Base == MAP_FAILED)
                {
                    std::fprintf(stderr, "LowArena: cannot reserve the 2 GB arena\n");
                    std::abort();
                }
                Made.Base = static_cast<char*>(Base);
                Made.Next = Made.Base + Granularity;
                Made.End = Made.Base + Limit;
                return Made;
            }();
            return Instance;
        }

        // The next chunk of the reservation, committed. A block larger than a chunk gets a chunk
        // of its own size.
        inline bool NewChunk(FState& Arena, std::size_t MinBytes)
        {
            const std::size_t Bytes =
                MinBytes > ChunkSize ? (MinBytes + Granularity - 1) & ~(std::size_t)(Granularity - 1)
                                     : ChunkSize;
            FReservation& Space = Reservation();
            if ((std::size_t)(Space.End - Space.Next) < Bytes)
                return false;
            if (mprotect(Space.Next, Bytes, PROT_READ | PROT_WRITE) != 0)
                return false;
            Arena.Cursor = Space.Next;
            Arena.End = Space.Next + Bytes;
            Space.Next += Bytes;
            return true;
        }
#endif

        inline std::size_t BlockSize(std::size_t Size)
        {
            const std::size_t Rounded = (Size + 15) & ~(std::size_t)(15);
            return Rounded != 0 ? Rounded : 16;
        }
    }

    // A block of Size bytes from the arena (below 2 GB on x64 Windows), not cleared, or null when
    // the arena has no room left for it (no free range below 2 GB holds it, on x64 Windows). For
    // callers that can report the failure (a texture's pixels, for example).
    inline void* TryAllocate(std::size_t Size)
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
            if (!Detail::NewChunk(Arena, Block))
                return nullptr;
        }
        void* Element = Arena.Cursor;
        Arena.Cursor = Arena.Cursor + Block;
        return Element;
    }

    // A block of Size bytes from the arena, not cleared. Running out of arena space ends the
    // process: falling back to a high address would bring the truncation back silently.
    inline void* Allocate(std::size_t Size)
    {
        void* const Element = TryAllocate(Size);
        if (Element == nullptr)
        {
            std::fprintf(stderr, "LowArena: no free address space below 2 GB\n");
            std::abort();
        }
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

    // Encode: the 32-bit value retail's field keeps for a handle (an arena block, or null).
    // Decode: the handle back from it. On Windows they are the plain casts retail and the x64
    // build use (an x64 handle is below 2 GB); on other 64-bit targets the value is the block's
    // offset in the arena. 0 is null and -1 (the interfaces' "invalid") stays -1 everywhere.
    inline FlLong Encode(const void* Object)
    {
#if defined(FL_PTR64) && !defined(_WIN64)
        if (Object == nullptr)
            return 0;
        if (Object == (const void*)(std::intptr_t)(-1))
            return -1;
        return (FlLong)((const char*)Object - Detail::Reservation().Base);
#else
        return (FlLong)(std::intptr_t)(Object);
#endif
    }

    template <class T>
    inline T* Decode(FlLong Value)
    {
#if defined(FL_PTR64) && !defined(_WIN64)
        if (Value == 0)
            return nullptr;
        if (Value == -1)
            return (T*)(std::intptr_t)(-1);
        return (T*)(Detail::Reservation().Base + (std::uint32_t)(Value));
#else
        return (T*)(std::intptr_t)(Value);
#endif
    }
}

#if defined(FL_PTR64)
#define FL_LOW_ARENA_NEW                                                                           \
    static void* operator new(std::size_t Size)                                                    \
    {                                                                                              \
        return LowArena::Allocate(Size);                                                           \
    }                                                                                              \
    static void* operator new(std::size_t Size, const std::nothrow_t&) noexcept                    \
    {                                                                                              \
        return LowArena::TryAllocate(Size);                                                        \
    }                                                                                              \
    static void operator delete(void* Element, std::size_t Size)                                   \
    {                                                                                              \
        LowArena::Free(Element, Size);                                                             \
    }
#else
#define FL_LOW_ARENA_NEW
#endif
