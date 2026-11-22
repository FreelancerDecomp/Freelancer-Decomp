// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// PORT(macOS): checks of the port's platform headers outside Windows (src/PortTests/CMakeLists.txt).
// Each check prints what failed; the exit code is the number of failures.

#include "handle_value.h"
#include "x64_low_arena.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace
{
    int G_Failures = 0;

    void Check(bool bOk, const char* What)
    {
        if (!bOk)
        {
            std::printf("FAIL: %s\n", What);
            ++G_Failures;
        }
    }

#if defined(FL_PTR64)
    struct FHandle
    {
        FL_LOW_ARENA_NEW
        int Value = 0;
        char Payload[40] = {};
    };

    // Retail keeps arena handles in 32-bit fields and reads them back: every block must survive
    // Encode and Decode, as a positive value (retail sign-extends it), and 0 / -1 keep meaning
    // null / invalid.
    void TestLowArenaRoundTrip()
    {
        std::vector<FHandle*> Handles;
        for (int Index = 0; Index < 10000; ++Index)
        {
            FHandle* const Handle = new FHandle();
            Handle->Value = Index;
            Handles.push_back(Handle);
            const FlLong Value = LowArena::Encode(Handle);
            Check(Value > 0, "an arena block encodes to a positive 32-bit value");
            Check(LowArena::Decode<FHandle>(Value) == Handle, "Decode(Encode(block)) is the block");
        }
        for (int Index = 0; Index < 10000; ++Index)
            Check(Handles[Index]->Value == Index, "arena blocks keep their contents");
        Check(LowArena::Encode(nullptr) == 0, "null encodes to 0");
        Check(LowArena::Decode<FHandle>(0) == nullptr, "0 decodes to null");
        Check(LowArena::Decode<FHandle>(-1) == (FHandle*)(std::intptr_t)(-1), "-1 stays the invalid handle");
        Check(LowArena::Encode((const void*)(std::intptr_t)(-1)) == -1, "the invalid handle encodes to -1");
        for (FHandle* Handle : Handles)
            delete Handle;
    }

    // A freed block is handed out again for the same size.
    void TestLowArenaReuse()
    {
        void* const First = LowArena::Allocate(48);
        LowArena::Free(First, 48);
        void* const Second = LowArena::Allocate(48);
        Check(First == Second, "a freed block is reused for the same size");
        LowArena::Free(Second, 48);
    }

    // A block larger than a chunk gets a chunk of its own and stays usable and encodable.
    void TestLowArenaLargeBlock()
    {
        const std::size_t Size = 40u << 20;
        char* const Block = static_cast<char*>(LowArena::Allocate(Size));
        std::memset(Block, 0x5A, Size);
        Check(Block[0] == 0x5A && Block[Size - 1] == 0x5A, "a large block is committed end to end");
        Check(LowArena::Decode<char>(LowArena::Encode(Block)) == Block, "a large block round-trips");
        LowArena::Free(Block, Size);
    }
#endif

    // HandleValue names any object (not only arena blocks) by a 32-bit id on 64-bit targets.
    void TestHandleValue()
    {
        int Objects[4] = {};
        FlULong Ids[4] = {};
        for (int Index = 0; Index < 4; ++Index)
        {
            Ids[Index] = HandleValue::Of(&Objects[Index]);
            Check(Ids[Index] != 0 && Ids[Index] != 0xFFFFFFFFu, "an id is never 0 or -1");
            Check(HandleValue::Of(&Objects[Index]) == Ids[Index], "the same object gets the same id");
        }
        for (int Index = 0; Index < 4; ++Index)
            Check(HandleValue::ObjectOf(Ids[Index]) == &Objects[Index], "an id names its object");
        HandleValue::Forget(&Objects[0]);
    }
}

int main()
{
    static_assert(sizeof(FlULong) == 4 && sizeof(FlLong) == 4, "Win32's 32-bit long");
#if defined(FL_PTR64)
    TestLowArenaRoundTrip();
    TestLowArenaReuse();
    TestLowArenaLargeBlock();
#endif
    TestHandleValue();
    std::printf(G_Failures == 0 ? "port_tests: all passed\n" : "port_tests: %d failed\n", G_Failures);
    return G_Failures;
}
