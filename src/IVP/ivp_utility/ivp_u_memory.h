// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>

// ============================================================================
// IVP_U_Memory -- the 0x14-byte bump/chunk arena installed twice into every
// IVP_Environment (at +0xA0 and +0xA4). Added 2026-09-17 by lane
// claude:common-ivpenv while closing FUN_0634bf50 (IVP_Environment's real
// constructor, decomp-tracker item 3031).
//
// PROVENANCE / WHY THIS TYPE REPLACES `CorePool06344120`.
// src/Common/common_stubs.cpp used to carry a second, independent
// transcription of this exact 0x14-byte object under the name
// `CorePool06344120` (a file-local struct plus `FUN_06344120`). That copy was
// derived from the CONSTRUCTOR ALONE (FUN_06344120 -> FUN_06344130, which
// only zeroes four dwords and stamps `state = 3`), so every field came out
// typed `unsigned` with no meaning attached, and the name asserted a "core
// pool" role the disassembly never supports. Tracing the environment
// constructor's own two call sites settled it:
//
//   .text:0634c553  MOV EDI,[EBX+0x8]        ; Parms->MemoryBlockSize
//   .text:0634c556  MOV ECX,[EBX+0x4]        ; Parms->MemoryBlock
//   .text:0634c559  SAR EDI,0x1              ; half of it
//   .text:0634c55b  PUSH EDI                 ; size
//   .text:0634c55c  PUSH ECX                 ; base
//   .text:0634c55d  MOV ECX,[ESI+0xa0]
//   .text:0634c56b  CALL FUN_06344010        ; arena_a0.init_mem(base, half)
//   .text:0634c570  MOV EDX,[EBX+0x4]
//   .text:0634c573  MOV ECX,[ESI+0xa4]
//   .text:0634c579  PUSH EDI                 ; same half size
//   .text:0634c57a  ADD EDX,EDI              ; base + half
//   .text:0634c57c  PUSH EDX
//   .text:0634c57d  CALL FUN_06344010        ; arena_a4.init_mem(base+half, half)
//
// i.e. one caller-supplied raw memory block is split down the middle and
// handed to the two objects, which is a memory arena and nothing else. That
// also matches the independent conclusion already recorded in
// src/IVP/ivp_physics/ivp_contact_point.cpp:590, which calls +0xA0 a
// "per-environment scratch arena".
//
// FIELD LAYOUT -- every offset below is proven by FUN_06344010
// (Common.dll.txt:405009-405063) and FUN_06344130 (via FUN_06344120), not by
// any name.
//   +0x00 void*   ChunkListStart : set to ChunkListHead after a successful
//                 install (`MOV EDX,[ESI+0x4]; MOV [ESI],EDX`, 0634403a/
//                 06344046) and again in the reset path (0634505d-06344060).
//   +0x04 void*   ChunkListHead  : head of the singly-linked chunk chain; the
//                 newly installed block's own first dword is overwritten with
//                 the PREVIOUS head (`MOV EDX,[ESI+0x4]; MOV [EAX],EDX`,
//                 06344025/0634402f), so each chunk carries its `next` inline.
//   +0x08 char*   Cursor         : the 32-byte-aligned first usable byte of
//                 the current chunk, `(block + 0x23) & ~0x1F` (0634403d-
//                 06344043). +0x23 == +0x20 (one chunk header) + 0x1F
//                 (round-up slack), so the header is 32 bytes.
//   +0x0C char*   End            : Cursor + UsableSize (06344048-0634404a).
//   +0x10 uint16  State          : 0 once a real block is installed
//                 (0634401b); FUN_06344130 leaves it at 3 on a fresh object,
//                 so 3 means "constructed, no block yet".
//   +0x12 uint16  UsableSize     : Size - 0x20, i.e. the block minus its
//                 32-byte header (06344028-0634402b). SIXTEEN bits, not 32 --
//                 both writes are `MOV word ptr`, so a block larger than
//                 0xFFFF + 0x20 silently truncates in retail too.
// ============================================================================
struct IVP_U_Memory
{
    void*    ChunkListStart; // +0x00
    void*    ChunkListHead;  // +0x04
    char*    Cursor;         // +0x08
    char*    End;            // +0x0C
    uint16_t State;          // +0x10
    uint16_t UsableSize;     // +0x12

    // FUN_06344120 (.text:06344120-0634412b): `CALL FUN_06344130; MOV EAX,ESI` --
    // the constructor is init_object. IVP_Environment's ctor news two of these
    // (.text:0634c4f7-0634c511 / 0634c52a-0634c544).
    IVP_U_Memory()
    {
        init_object();
    }

    // First usable byte of a chunk: past the 32-byte header, rounded up to 32 bytes
    // (`LEA EAX,[block+0x23]; AND EAX,0xffffffe0`, inlined at every retail site).
    static char* ChunkPayload(void* Chunk)
    {
        return (char*)(((uintptr_t)Chunk + 0x23) & ~(uintptr_t)0x1F);
    }

    // FUN_06344000 (`JMP FUN_06344150`): ~IVP_Environment calls it on +0xa0 / +0xa4
    // before operator delete (.text:0634c8c5 / 0634c8e5), i.e. `delete Arena`.
    ~IVP_U_Memory()
    {
        free_mem();
    }

    // FUN_06344130 (decomp-tracker item 2756,
    // Common.dll.txt:405159-405168). Zeroes the four leading dwords, then
    // stamps State = 3 and UsableSize = 0.
    void init_object()
    {
        ChunkListStart = nullptr;
        ChunkListHead  = nullptr;
        Cursor         = nullptr;
        End            = nullptr;
        State          = 3;
        UsableSize     = 0;
    }

    // FUN_06344010 (decomp-tracker item 2753, Common.dll.txt:405009-405063,
    // 32 insns, __thiscall with two stack args). Installs a caller-supplied
    // block as the arena's current chunk.
    //
    // NOT PORTED 1:1 -- the `Block == nullptr` reset path is a documented
    // open gap, see the TODO below.
    void init_mem(char* Block, int32_t Size)
    {
        State = 0; // 0634401b -- written on BOTH paths, before the branch.

        if (Block == nullptr)
        {
            // Retail LAB_06344051 (06344051-06344063): UsableSize = 0,
            // `PUSH 0; CALL FUN_063440d0` (GetMemNewBlock(0) -- allocates the
            // first heap chunk), then ChunkListStart = ChunkListHead.
            UsableSize = 0;
            GetMemNewBlock(0);
            ChunkListStart = ChunkListHead;
            return;
        }

        UsableSize = (uint16_t)(Size - 0x20); // 06344028-0634402b

        // The block's own first dword becomes the `next` link (06344025/
        // 0634402f), then the block becomes the new head (06344037).
        *(void**)Block = ChunkListHead;
        ChunkListHead = Block;

        // 32-byte-aligned first usable byte, past the 32-byte chunk header.
        Cursor = ChunkPayload(Block); // 0634403d-06344043
        End = Cursor + UsableSize;      // 06344048-0634404a
        ChunkListStart = ChunkListHead; // 0634403a/06344046
    }

    // FUN_06344070 (decomp-tracker item 2754). Frees every chunk in the chain
    // ABOVE the original one, then rewinds the cursor back to the start of
    // that original chunk -- the arena's "release everything allocated since
    // init_mem" reset.
    //
    // Moved here 2026-09-17 (lane claude:common-ivpenv) from a file-local
    // duplicate: src/IVP/ivp_collision/ivp_mindist_limiter.cpp carried its own
    // `struct FrozenCorePool` + `ResetFrozenCorePoolChain()` transcription of
    // this same object and this same function. Its own comment conceded the
    // identification was a guess ("likely a per-environment bump allocator"),
    // and it is now confirmed to be exactly this type -- note that its
    // independently-derived `(Base + 0x23) & ~0x1F` alignment and 0x7fe0
    // default size agree byte-for-byte with init_mem above, which is what
    // makes the merge safe.
    void reset_chunks()
    {
        void* Node = ChunkListHead;
        while (Node != nullptr)
        {
            if (Node == ChunkListStart)
                break; // the original chunk is the sentinel -- never freed
            void* Next = *(void**)Node;
            free(Node); // FUN_06343fb0: confirmed 1:1 free() thunk
            Node = Next;
        }

        char* FreeStart = ChunkPayload(ChunkListStart);
        Cursor = FreeStart;
        // A zero UsableSize means "never had a real block installed"; retail
        // falls back to the 0x7fe0 default chunk payload in that case.
        End = FreeStart + (UsableSize != 0 ? UsableSize : 0x7fe0);
        ChunkListHead = ChunkListStart;
    }

    // FUN_06344150 (.text:06344150-06344198; also reached through the JMP thunk
    // 06344000, which ~IVP_Environment calls on +0xa0 / +0xa4 before operator
    // delete). Walks the chunk chain from ChunkListHead: the next link is read
    // first; when UsableSize != 0 and the chunk is ChunkListStart (the
    // caller-supplied block) the walk stops without freeing it; otherwise the
    // chunk is freed (06343fb0). Then Cursor, End, ChunkListHead and
    // ChunkListStart are zeroed. cmn-phy7 2026-10-08.
    void free_mem()
    {
        void* Node = ChunkListHead;                          // :06344153
        while (Node != nullptr)                              // :06344156-:06344159 / :06344175
        {
            void* const Next = *(void**)Node;                 // :06344160
            if (UsableSize != 0 && Node == ChunkListStart)    // :0634415b-:06344166
            {
                break;
            }
            free(Node);                                      // :0634416c (null cannot reach here)
            Node = Next;
        }
        Cursor = nullptr;                                    // :0634417c
        End = nullptr;                                       // :06344183
        ChunkListHead = nullptr;                             // :0634418a
        ChunkListStart = nullptr;                            // :06344191
    }

    // FUN_063440d0 (Common.dll.txt .text:063440d0-06344113, __thiscall, RET 4).
    // Slow path of GetMem: mallocs a fresh chunk of max(0x7fd8, round32(Size))
    // payload bytes plus 0x28 (FUN_06343f70 = malloc), links it in front of the
    // chain (its first dword = previous head), and hands out its first 32-byte
    // aligned byte. Cursor moves past the requested size, End = start + payload.
    // ChunkListStart is NOT touched (only init_mem/reset_chunks write it).
    void* GetMemNewBlock(int Size)
    {
        unsigned int Rounded = ((unsigned int)Size + 0x1f) & 0xffffffe0u;             // 063440d6-063440de
        unsigned int Payload = 0x7fd8;                                                // 063440d9
        if (Rounded > Payload)                                                       // CMP ESI,EBX; JBE
            Payload = Rounded;
        char* Block = (char*)malloc(Payload + 0x28);                                 // 063440ea-063440ee
        *(void**)Block = ChunkListHead;                                              // 063440f3-063440f6
        ChunkListHead = Block;                                                       // 063440f8
        char* Start = ChunkPayload(Block);                                           // 063440fb-063440fe
        Cursor = Start + Rounded;                                                    // 06344104/0634410a
        End = Start + Payload;                                                       // 06344107/0634410d
        return Start;
    }

    // FUN_06347380 (Common.dll.txt .text:06347380-063473a7, __thiscall, RET 4).
    // Bump allocation: the block starts at the current Cursor; the new Cursor is
    // (Cursor + Size + 0x1f) & ~0x1f. If that is below End (unsigned JC) it is
    // committed, otherwise the request goes to GetMemNewBlock. Several retail
    // callers inline this exact sequence (e.g. 063666f0, 06368300, 06368560).
    void* GetMem(int Size)
    {
        char* Old = Cursor;                                                          // 06347380
        char* New = (char*)(((uintptr_t)Old + (unsigned int)Size + 0x1f) & ~(uintptr_t)0x1F); // 06347388-06347390
        if ((uintptr_t)New < (uintptr_t)End)                                         // CMP EDX,EDI; JC
        {
            Cursor = New;                                                            // 063473a3
            return Old;
        }
        return GetMemNewBlock(Size);                                                 // 06347398
    }
};

static_assert(sizeof(IVP_U_Memory) == 0x14, "IVP_U_Memory must be 0x14 bytes");
static_assert(offsetof(IVP_U_Memory, ChunkListHead) == 0x04, "IVP_U_Memory+0x04");
static_assert(offsetof(IVP_U_Memory, Cursor) == 0x08, "IVP_U_Memory+0x08");
static_assert(offsetof(IVP_U_Memory, End) == 0x0C, "IVP_U_Memory+0x0C");
static_assert(offsetof(IVP_U_Memory, State) == 0x10, "IVP_U_Memory+0x10");
static_assert(offsetof(IVP_U_Memory, UsableSize) == 0x12, "IVP_U_Memory+0x12");
