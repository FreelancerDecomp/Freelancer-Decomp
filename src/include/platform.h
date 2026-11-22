// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

#pragma once

// Platform layer: the operating-system services the engine uses -- files, threads and
// synchronisation, timers, loadable modules and paths -- behind one small set of functions,
// so that game code does not call the OS directly.
//
// Rules of the layer:
// - Each function is the one OS call retail made, with the same arguments, so behaviour,
//   return values, GetLastError() and every module's import table stay as in retail. The
//   functions are inline: no new library, no project changes, nothing crosses a DLL boundary.
// - Types keep their Win32 size and alignment (handles are pointer-sized, CriticalSection is
//   0x18 bytes on x86, DateTime is SYSTEMTIME, FindData is WIN32_FIND_DATAA), so retail
//   structure layouts and the layout static_asserts are unaffected.
// - Flag and result constants carry the Win32 values. Code that passes Win32 flags through
//   an interface retail defined (IFileSystem) can hand them on unchanged; another backend
//   translates them.
// - Only Windows is implemented. Window, input, graphics, sound, registry and dialog APIs are
//   outside the layer, and so are subsystems that exist only on Windows (the MFC server UI,
//   crash reporting through inherited handles, MemoryDiagnostics).
//
// Win32 equivalents are named next to each function.

#if !defined(_WIN32)
#error "platform.h: only the Win32 backend exists so far"
#endif

#include <windows.h>
#include <mmsystem.h> // timeGetTime; WIN32_LEAN_AND_MEAN leaves it out of windows.h

#include <cstdint>

namespace Platform
{
    // ---------------------------------------------------------------------------------------
    // Handles
    // ---------------------------------------------------------------------------------------

    // A file, thread, event or mutex. Pointer-sized, as HANDLE.
    using Handle = HANDLE;

    // FileOpen's and FindFirst's failure value (INVALID_HANDLE_VALUE). The other creators
    // return nullptr on failure.
    inline Handle InvalidHandle() { return INVALID_HANDLE_VALUE; }

    // CloseHandle.
    inline bool CloseHandle(Handle Object) { return ::CloseHandle(Object) != FALSE; }

    // ---------------------------------------------------------------------------------------
    // Files
    // ---------------------------------------------------------------------------------------

    // Access, sharing and creation values (GENERIC_*, FILE_SHARE_*, the creation disposition).
    constexpr FlULong kAccessRead = GENERIC_READ;
    constexpr FlULong kAccessWrite = GENERIC_WRITE;
    constexpr FlULong kShareRead = FILE_SHARE_READ;
    constexpr FlULong kShareWrite = FILE_SHARE_WRITE;
    constexpr FlULong kCreateAlways = CREATE_ALWAYS;
    constexpr FlULong kOpenExisting = OPEN_EXISTING;
    constexpr FlULong kOpenAlways = OPEN_ALWAYS;

    // CreateFileA with no security attributes and no template. Returns InvalidHandle() on
    // failure.
    inline Handle FileOpen(char const* Path, FlULong Access, FlULong Share,
                           FlULong Creation, FlULong FlagsAndAttributes = 0)
    {
        return ::CreateFileA(Path, Access, Share, nullptr, Creation, FlagsAndAttributes, nullptr);
    }

    // ReadFile, synchronous. BytesRead receives the count.
    inline bool FileRead(Handle File, void* Buffer, FlULong Size, FlULong* BytesRead)
    {
        return ::ReadFile(File, Buffer, Size, BytesRead, nullptr) != FALSE;
    }

    // WriteFile, synchronous.
    inline bool FileWrite(Handle File, void const* Buffer, FlULong Size,
                          FlULong* BytesWritten)
    {
        return ::WriteFile(File, Buffer, Size, BytesWritten, nullptr) != FALSE;
    }

    // GetFileSize: the low 32 bits; 0xffffffff (INVALID_FILE_SIZE) on failure.
    inline FlULong FileGetSize(Handle File) { return ::GetFileSize(File, nullptr); }

    // File attribute values (FILE_ATTRIBUTE_*).
    constexpr FlULong kInvalidAttributes = INVALID_FILE_ATTRIBUTES;
    constexpr FlULong kAttributeDirectory = FILE_ATTRIBUTE_DIRECTORY;

    // GetFileAttributesA: kInvalidAttributes when the path does not exist.
    inline FlULong PathAttributes(char const* Path) { return ::GetFileAttributesA(Path); }

    // CopyFileA. FailIfExists false overwrites the destination.
    inline bool FileCopy(char const* From, char const* To, bool FailIfExists)
    {
        return ::CopyFileA(From, To, FailIfExists ? TRUE : FALSE) != FALSE;
    }

    // MoveFileA.
    inline bool FileMove(char const* From, char const* To) { return ::MoveFileA(From, To) != FALSE; }

    // Directory enumeration. FindData is WIN32_FIND_DATAA: IFileSystem::FindFirst/FindNext
    // hand it across the DLL boundary, so its layout is part of the retail interface.
    using FindData = WIN32_FIND_DATAA;

    // FindFirstFileA: InvalidHandle() when nothing matches.
    inline Handle FindFirst(char const* Pattern, FindData* Data) { return ::FindFirstFileA(Pattern, Data); }

    // FindNextFileA.
    inline bool FindNext(Handle Search, FindData* Data) { return ::FindNextFileA(Search, Data) != FALSE; }

    // FindClose.
    inline bool FindClose(Handle Search) { return ::FindClose(Search) != FALSE; }

    // ---------------------------------------------------------------------------------------
    // Threads
    // ---------------------------------------------------------------------------------------

    // A thread entry point (LPTHREAD_START_ROUTINE).
    using ThreadProc = DWORD(WINAPI*)(void* Parameter);

    // Thread priorities (THREAD_PRIORITY_*).
    constexpr int kPriorityNormal = THREAD_PRIORITY_NORMAL;
    constexpr int kPriorityAboveNormal = THREAD_PRIORITY_ABOVE_NORMAL;
    constexpr int kPriorityHighest = THREAD_PRIORITY_HIGHEST;

    // CreateThread with no security attributes. StackSize 0 takes the module default.
    // Suspended creates it with CREATE_SUSPENDED. ThreadId may be nullptr. Returns nullptr on
    // failure.
    inline Handle ThreadCreate(ThreadProc Proc, void* Parameter, FlULong StackSize = 0,
                               bool Suspended = false, FlULong* ThreadId = nullptr)
    {
        return ::CreateThread(nullptr, StackSize, Proc, Parameter,
                              Suspended ? CREATE_SUSPENDED : 0, ThreadId);
    }

    // ResumeThread: the previous suspend count, or 0xffffffff on failure.
    inline FlULong ThreadResume(Handle Thread) { return ::ResumeThread(Thread); }

    // SetThreadPriority.
    inline bool ThreadSetPriority(Handle Thread, int Priority)
    {
        return ::SetThreadPriority(Thread, Priority) != FALSE;
    }

    // TerminateThread. Kills the thread without unwinding, as retail does.
    inline bool ThreadTerminate(Handle Thread, FlULong ExitCode)
    {
        return ::TerminateThread(Thread, ExitCode) != FALSE;
    }

    // GetCurrentThreadId.
    inline FlULong CurrentThreadId() { return ::GetCurrentThreadId(); }

    // Sleep. 0 gives up the rest of the time slice.
    inline void SleepMs(FlULong Milliseconds) { ::Sleep(Milliseconds); }

    // Thread-local slots. TlsSlotAlloc returns kInvalidTlsSlot (TLS_OUT_OF_INDEXES) on failure.
    constexpr FlULong kInvalidTlsSlot = TLS_OUT_OF_INDEXES;
    inline FlULong TlsSlotAlloc() { return ::TlsAlloc(); }
    inline void* TlsSlotGet(FlULong Slot) { return ::TlsGetValue(Slot); }
    inline bool TlsSlotSet(FlULong Slot, void* Value) { return ::TlsSetValue(Slot, Value) != FALSE; }

    // ---------------------------------------------------------------------------------------
    // Synchronisation
    // ---------------------------------------------------------------------------------------

    // CreateEventA with no security attributes. Name nullptr makes an unnamed event.
    // Returns nullptr on failure.
    inline Handle EventCreate(bool ManualReset, bool InitiallySet, char const* Name = nullptr)
    {
        return ::CreateEventA(nullptr, ManualReset ? TRUE : FALSE, InitiallySet ? TRUE : FALSE, Name);
    }

    // SetEvent / ResetEvent.
    inline bool EventSet(Handle Event) { return ::SetEvent(Event) != FALSE; }
    inline bool EventReset(Handle Event) { return ::ResetEvent(Event) != FALSE; }

    // CreateMutexA with no security attributes, not initially owned. Returns nullptr on
    // failure.
    inline Handle MutexCreate(char const* Name = nullptr) { return ::CreateMutexA(nullptr, FALSE, Name); }

    // ReleaseMutex.
    inline bool MutexRelease(Handle Mutex) { return ::ReleaseMutex(Mutex) != FALSE; }

    // Wait timeouts and results (INFINITE, WAIT_*).
    constexpr FlULong kWaitForever = INFINITE;
    constexpr FlULong kWaitSignaled = WAIT_OBJECT_0;
    constexpr FlULong kWaitAbandoned = WAIT_ABANDONED;
    constexpr FlULong kWaitTimeout = WAIT_TIMEOUT;
    constexpr FlULong kWaitFailed = WAIT_FAILED;

    // WaitForSingleObject.
    inline FlULong WaitOne(Handle Object, FlULong TimeoutMs)
    {
        return ::WaitForSingleObject(Object, TimeoutMs);
    }

    // WaitForMultipleObjects. WaitAll false returns kWaitSignaled + the index of the object
    // that ended the wait.
    inline FlULong WaitMany(FlULong Count, Handle const* Objects, bool WaitAll,
                                  FlULong TimeoutMs)
    {
        return ::WaitForMultipleObjects(Count, Objects, WaitAll ? TRUE : FALSE, TimeoutMs);
    }

    // An in-process lock (CRITICAL_SECTION: 0x18 bytes on x86, recursive for its owner).
    using CriticalSection = CRITICAL_SECTION;

    // InitializeCriticalSection / EnterCriticalSection / LeaveCriticalSection /
    // DeleteCriticalSection.
    inline void LockInit(CriticalSection* Lock) { ::InitializeCriticalSection(Lock); }
    inline void LockEnter(CriticalSection* Lock) { ::EnterCriticalSection(Lock); }
    inline void LockLeave(CriticalSection* Lock) { ::LeaveCriticalSection(Lock); }
    inline void LockDelete(CriticalSection* Lock) { ::DeleteCriticalSection(Lock); }

    // Interlocked*: full-barrier atomics on a 32-bit value. Increment and Decrement return
    // the new value, Exchange and CompareExchange the previous one.
    inline FlLong AtomicIncrement(FlLong volatile* Value) { return ::InterlockedIncrement(Value); }
    inline FlLong AtomicDecrement(FlLong volatile* Value) { return ::InterlockedDecrement(Value); }
    inline FlLong AtomicExchange(FlLong volatile* Value, FlLong NewValue)
    {
        return ::InterlockedExchange(Value, NewValue);
    }
    inline FlLong AtomicCompareExchange(FlLong volatile* Value, FlLong NewValue, FlLong Expected)
    {
        return ::InterlockedCompareExchange(Value, NewValue, Expected);
    }

    // ---------------------------------------------------------------------------------------
    // Time
    // ---------------------------------------------------------------------------------------

    // timeGetTime: milliseconds, wraps every 49.7 days. Its resolution follows
    // timeBeginPeriod; the two clocks are kept apart because retail reads both.
    inline FlULong TimerMs() { return ::timeGetTime(); }

    // GetTickCount: milliseconds since boot, wraps every 49.7 days, system-tick resolution.
    inline FlULong TickCountMs() { return ::GetTickCount(); }

    // QueryPerformanceCounter / QueryPerformanceFrequency. False when the call fails (never
    // on Windows XP and later); the value is then 0.
    inline bool PerfCounter(int64_t* Ticks)
    {
        FL_LAYOUT_ASSERT(sizeof(LARGE_INTEGER) == sizeof(int64_t), "LARGE_INTEGER is 64-bit");
        LARGE_INTEGER Value;
        Value.QuadPart = 0;
        BOOL const Ok = ::QueryPerformanceCounter(&Value);
        *Ticks = Value.QuadPart;
        return Ok != FALSE;
    }
    inline bool PerfFrequency(int64_t* TicksPerSecond)
    {
        LARGE_INTEGER Value;
        Value.QuadPart = 0;
        BOOL const Ok = ::QueryPerformanceFrequency(&Value);
        *TicksPerSecond = Value.QuadPart;
        return Ok != FALSE;
    }

    // Calendar time in fields (SYSTEMTIME: eight 16-bit words).
    using DateTime = SYSTEMTIME;

    // GetLocalTime.
    inline void LocalTime(DateTime* Now) { ::GetLocalTime(Now); }

    // ---------------------------------------------------------------------------------------
    // Modules
    // ---------------------------------------------------------------------------------------

    // A loaded executable or library (HMODULE).
    using Module = HMODULE;

    // An exported symbol's address (FARPROC); cast it to the function type it is.
    using Symbol = FARPROC;

    // LoadLibraryA: nullptr on failure.
    inline Module ModuleLoad(char const* Path) { return ::LoadLibraryA(Path); }

    // A DLL loaded only for its resources (string tables, dialogs, icons): the retail
    // resources.dll / infocards.dll and the [Resources] DLLs that data and mods list. Win32 uses
    // LoadLibraryA, as retail. Those DLLs are 32-bit images, which a 64-bit process cannot load
    // as code, so x64 maps them as image resources: LoadString, FindResource, DialogBoxParam and
    // FreeLibrary work on the handle; GetProcAddress does not. nullptr on failure.
    inline Module ResourceModuleLoad(char const* Path)
    {
#if defined(_WIN64)
        return ::LoadLibraryExA(Path, nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
#else
        return ::LoadLibraryA(Path);
#endif
    }

    // FreeLibrary.
    inline bool ModuleFree(Module Library) { return ::FreeLibrary(Library) != FALSE; }

    // GetModuleHandleA: an already loaded module, without a reference. nullptr names the
    // executable.
    inline Module ModuleFind(char const* Name) { return ::GetModuleHandleA(Name); }

    // GetProcAddress: nullptr when the module has no such export.
    inline Symbol ModuleSymbol(Module Library, char const* Name) { return ::GetProcAddress(Library, Name); }

    // GetModuleFileNameA: the length written, 0 on failure. Library nullptr names the
    // executable.
    inline FlULong ModulePath(Module Library, char* Path, FlULong Size)
    {
        return ::GetModuleFileNameA(Library, Path, Size);
    }

    // ---------------------------------------------------------------------------------------
    // Paths
    // ---------------------------------------------------------------------------------------

    // GetCurrentDirectoryA: the length written, or the size needed when Size is too small.
    inline FlULong CurrentDirectory(char* Path, FlULong Size)
    {
        return ::GetCurrentDirectoryA(Size, Path);
    }

    // SetCurrentDirectoryA. (Not named SetCurrentDirectory: windows.h makes that a macro.)
    inline bool ChangeDirectory(char const* Path) { return ::SetCurrentDirectoryA(Path) != FALSE; }

    // GetFullPathNameA: the length written, or the size needed when Size is too small.
    // FilePart may be nullptr.
    inline FlULong FullPath(char const* Path, char* Full, FlULong Size,
                                  char** FilePart = nullptr)
    {
        return ::GetFullPathNameA(Path, Size, Full, FilePart);
    }

    // GetTempPathA: the length written (with the trailing backslash).
    inline FlULong TempDirectory(char* Path, FlULong Size) { return ::GetTempPathA(Size, Path); }

    // Per-user folders (UserFolder) are in platform_folders.h: it needs the shell headers.
} // namespace Platform
