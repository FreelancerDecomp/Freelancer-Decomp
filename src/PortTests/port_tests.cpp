// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// PORT(macOS): checks of the port's platform headers outside Windows (src/PortTests/CMakeLists.txt).
// Each check prints what failed; the exit code is the number of failures.

#include "handle_value.h"
#include "platform.h"
#include "x64_low_arena.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

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

    // A scratch directory under the temp folder, removed again by the test that made it.
    std::string ScratchDirectory()
    {
        char Temp[1024];
        Platform::TempDirectory(Temp, sizeof(Temp));
        std::string Directory = std::string(Temp) + "port_tests_" + std::to_string(getpid());
        mkdir(Directory.c_str(), 0755);
        return Directory;
    }

    // Retail paths use '\' and spell names in another case than the disk does.
    void TestPlatformFiles()
    {
        std::string const Root = ScratchDirectory();
        mkdir((Root + "/Data").c_str(), 0755);
        mkdir((Root + "/Data/Sub").c_str(), 0755);
        std::string const Written = Root + "/Data/Sub/File.TXT";
        Platform::Handle File = Platform::FileOpen(Written.c_str(), Platform::kAccessWrite, 0, Platform::kCreateAlways);
        Check(File != Platform::InvalidHandle(), "FileOpen creates a file");
        FlULong Count = 0;
        Check(Platform::FileWrite(File, "freelancer", 10, &Count) && Count == 10, "FileWrite writes");
        Platform::CloseHandle(File);

        std::string const Retail = Root + "\\data\\SUB\\file.txt";
        File = Platform::FileOpen(Retail.c_str(), Platform::kAccessRead, Platform::kShareRead, Platform::kOpenExisting);
        Check(File != Platform::InvalidHandle(), "a '\\' path in another case opens the file");
        Check(Platform::FileGetSize(File) == 10, "FileGetSize");
        char Buffer[32] = {};
        Check(Platform::FileRead(File, Buffer, sizeof(Buffer), &Count) && Count == 10 &&
                  std::memcmp(Buffer, "freelancer", 10) == 0,
              "FileRead reads to the end");
        Platform::CloseHandle(File);
        Check(Platform::FileOpen((Root + "/missing").c_str(), Platform::kAccessRead, 0, Platform::kOpenExisting) ==
                  Platform::InvalidHandle(),
              "a missing file fails with InvalidHandle()");
        Check(Platform::PathAttributes((Root + "\\DATA").c_str()) == Platform::kAttributeDirectory, "a directory's attributes");
        Check(Platform::PathAttributes((Root + "/nothing").c_str()) == Platform::kInvalidAttributes, "a missing path's attributes");

        Check(Platform::FileCopy(Retail.c_str(), (Root + "/Data/Copy.txt").c_str(), true), "FileCopy");
        Check(!Platform::FileCopy(Retail.c_str(), (Root + "/Data/Copy.txt").c_str(), true), "FileCopy fails onto an existing file");
        Check(Platform::FileMove((Root + "/Data/Copy.txt").c_str(), (Root + "/Data/Moved.txt").c_str()), "FileMove");

        Platform::FindData Found;
        int Matches = 0;
        Platform::Handle Search = Platform::FindFirst((Root + "\\data\\*.TXT").c_str(), &Found);
        Check(Search != Platform::InvalidHandle(), "FindFirst finds *.TXT without regard to case");
        while (Search != Platform::InvalidHandle())
        {
            ++Matches;
            Check(std::strcmp(Found.cFileName, "Moved.txt") == 0 && Found.nFileSizeLow == 10, "the found file's name and size");
            if (!Platform::FindNext(Search, &Found))
                break;
        }
        Check(Matches == 1, "FindFirst / FindNext see only the matching files");
        if (Search != Platform::InvalidHandle())
            Platform::FindClose(Search);

        std::string const Command = "rm -rf '" + Root + "'";
        Check(std::system(Command.c_str()) == 0, "scratch directory removed");
    }

    FlULong G_ThreadResult = 0;
    FlULong ThreadBody(void* Parameter)
    {
        G_ThreadResult = *static_cast<FlULong*>(Parameter) * 2;
        return 7;
    }

    void TestPlatformThreadsAndWaits()
    {
        FlULong Input = 21;
        FlULong Id = 0;
        Platform::Handle Thread = Platform::ThreadCreate(&ThreadBody, &Input, 0, true, &Id);
        Check(Thread != nullptr && Id != 0 && Id != Platform::CurrentThreadId(), "ThreadCreate, suspended, with its own id");
        Check(Platform::WaitOne(Thread, 50) == Platform::kWaitTimeout, "a suspended thread does not run");
        Check(Platform::ThreadResume(Thread) == 1, "ThreadResume returns the previous suspend count");
        Check(Platform::WaitOne(Thread, Platform::kWaitForever) == Platform::kWaitSignaled && G_ThreadResult == 42,
              "a thread is signalled when it ends");
        Platform::CloseHandle(Thread);

        Platform::Handle Events[2] = {Platform::EventCreate(false, false), Platform::EventCreate(true, false)};
        Check(Platform::WaitMany(2, Events, false, 10) == Platform::kWaitTimeout, "WaitMany times out");
        Platform::EventSet(Events[1]);
        Check(Platform::WaitMany(2, Events, false, 0) == Platform::kWaitSignaled + 1, "WaitMany reports which object");
        Check(Platform::WaitOne(Events[1], 0) == Platform::kWaitSignaled, "a manual-reset event stays set");
        Platform::EventSet(Events[0]);
        Check(Platform::WaitOne(Events[0], 0) == Platform::kWaitSignaled, "an auto-reset event signals once");
        Check(Platform::WaitOne(Events[0], 0) == Platform::kWaitTimeout, "an auto-reset event resets itself");
        Platform::EventSet(Events[0]);
        Check(Platform::WaitMany(2, Events, true, 0) == Platform::kWaitSignaled, "WaitMany waits for all");
        Platform::CloseHandle(Events[0]);
        Platform::CloseHandle(Events[1]);

        Platform::Handle Mutex = Platform::MutexCreate();
        Check(Platform::WaitOne(Mutex, 0) == Platform::kWaitSignaled && Platform::WaitOne(Mutex, 0) == Platform::kWaitSignaled,
              "a mutex is recursive for its owner");
        Check(Platform::MutexRelease(Mutex) && Platform::MutexRelease(Mutex) && !Platform::MutexRelease(Mutex),
              "MutexRelease fails once the mutex is free");
        Platform::CloseHandle(Mutex);

        Platform::CriticalSection Lock;
        Platform::LockInit(&Lock);
        Platform::LockEnter(&Lock);
        Platform::LockEnter(&Lock);
        Platform::LockLeave(&Lock);
        Platform::LockLeave(&Lock);
        Platform::LockDelete(&Lock);

        FlULong const Slot = Platform::TlsSlotAlloc();
        int Value = 0;
        Check(Slot != Platform::kInvalidTlsSlot && Platform::TlsSlotSet(Slot, &Value) && Platform::TlsSlotGet(Slot) == &Value,
              "TLS slots");

        FlLong volatile Counter = 5;
        Check(Platform::AtomicIncrement(&Counter) == 6 && Platform::AtomicDecrement(&Counter) == 5, "atomic increment / decrement");
        Check(Platform::AtomicExchange(&Counter, 9) == 5 && Counter == 9, "atomic exchange returns the previous value");
        Check(Platform::AtomicCompareExchange(&Counter, 1, 9) == 9 && Counter == 1, "atomic compare-exchange replaces on a match");
        Check(Platform::AtomicCompareExchange(&Counter, 3, 9) == 1 && Counter == 1, "atomic compare-exchange keeps on a miss");
    }

    void TestPlatformTimeAndPaths()
    {
        FlULong const Start = Platform::TimerMs();
        Platform::SleepMs(20);
        Check(Platform::TimerMs() - Start >= 15, "TimerMs advances");
        int64_t Frequency = 0;
        int64_t Ticks = 0;
        Check(Platform::PerfFrequency(&Frequency) && Frequency > 0 && Platform::PerfCounter(&Ticks) && Ticks > 0,
              "performance counter");
        Platform::DateTime Now;
        Platform::LocalTime(&Now);
        Check(Now.wYear >= 2024 && Now.wMonth >= 1 && Now.wMonth <= 12, "LocalTime");

        char Full[512];
        char* Part = nullptr;
        FlULong const Length = Platform::FullPath("/a/b/../c/./d.ini", Full, sizeof(Full), &Part);
        Check(Length == std::strlen("/a/c/d.ini") && std::strcmp(Full, "/a/c/d.ini") == 0 && Part != nullptr &&
                  std::strcmp(Part, "d.ini") == 0,
              "FullPath folds . and ..");
        Check(Platform::FullPath("/x", Full, 2) == 3, "FullPath reports the size needed");
        char Here[512];
        Check(Platform::CurrentDirectory(Here, sizeof(Here)) > 0 && Here[0] == '/', "CurrentDirectory");
        char Executable[1024];
        Check(Platform::ModulePath(nullptr, Executable, sizeof(Executable)) > 0, "ModulePath of the executable");
        Platform::Module Self = Platform::ModuleFind(nullptr);
        Check(Self != nullptr && Platform::ModuleSymbol(Self, "malloc") != nullptr, "ModuleFind / ModuleSymbol");
    }

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
    TestPlatformFiles();
    TestPlatformThreadsAndWaits();
    TestPlatformTimeAndPaths();
    std::printf(G_Failures == 0 ? "port_tests: all passed\n" : "port_tests: %d failed\n", G_Failures);
    return G_Failures;
}
