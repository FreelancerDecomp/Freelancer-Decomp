// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

#pragma once

// PORT(macOS, Linux): the POSIX backend of the platform layer (platform.h), included from there
// outside Windows. The same functions as the Win32 backend, with Win32's values, results and
// structure layouts (x64-client-plan.md, phase 6, M2):
//
// - Handles are pointers to small objects (files, searches, threads, events, mutexes);
//   InvalidHandle() is the all-ones value, as INVALID_HANDLE_VALUE.
// - Paths take '\' as well as '/', and each component is matched without regard to case: retail
//   DATA\ spells names in mixed case that its ini files spell differently.
// - Waits (WaitOne, WaitMany) follow WaitForSingleObject / WaitForMultipleObjects: an auto-reset
//   event is consumed, a mutex is taken (recursively, by its owner), a thread is signalled when
//   it ends. They share one lock and condition variable, which keeps WaitMany simple and exact.
// - FindData keeps WIN32_FIND_DATAA's field names and 320-byte layout: IFileSystem hands it
//   across modules. DateTime keeps SYSTEMTIME's.
// - Not available here: ThreadTerminate (POSIX has no safe equivalent; it fails), and
//   ResourceModuleLoad (resource DLLs are Windows PE images; reading their resources is M3).

#include "fl_layout.h" // FlULong / FlLong (also forced into every source)

#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <ctime>
#include <mutex>
#include <new>
#include <string>
#include <thread>

#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace Platform
{
    // ---------------------------------------------------------------------------------------
    // Handles
    // ---------------------------------------------------------------------------------------

    using Handle = void*;

    inline Handle InvalidHandle() { return reinterpret_cast<Handle>(static_cast<std::intptr_t>(-1)); }

    namespace Detail
    {
        // Every handle points at one of these; CloseHandle deletes it.
        struct FObject
        {
            virtual ~FObject() = default;
        };

        // The one lock and condition variable behind every wait (see the header comment).
        inline std::mutex& WaitLock()
        {
            static std::mutex Lock;
            return Lock;
        }
        inline std::condition_variable& WaitSignal()
        {
            static std::condition_variable Signal;
            return Signal;
        }

        // A thread's 32-bit id, as GetCurrentThreadId's: handed out once per thread, never 0.
        inline FlULong ThisThreadId()
        {
            static std::atomic<FlULong> Next{1};
            thread_local FlULong const Id = Next.fetch_add(1);
            return Id;
        }

        // Something a wait can end on. Signalled and Acquire run under WaitLock.
        struct FWaitable : FObject
        {
            virtual bool Signalled() const = 0;
            virtual void Acquire() {}
        };

        inline bool Valid(Handle Object) { return Object != nullptr && Object != InvalidHandle(); }
    }

    inline bool CloseHandle(Handle Object)
    {
        if (!Detail::Valid(Object))
            return false;
        delete static_cast<Detail::FObject*>(Object);
        return true;
    }

    // ---------------------------------------------------------------------------------------
    // Paths
    // ---------------------------------------------------------------------------------------

    namespace Detail
    {
        // Path with '\' as '/' and each existing component spelled as it is on disk, matched
        // without regard to case. Components that do not exist (a file about to be created) stay
        // as written. Drive letters do not exist here and are dropped ("C:\x" is "/x").
        inline std::string ResolvePath(char const* Path)
        {
            std::string In = Path != nullptr ? Path : "";
            for (char& Character : In)
            {
                if (Character == '\\')
                    Character = '/';
            }
            if (In.size() >= 2 && In[1] == ':')
                In.erase(0, 2);
            std::string Out;
            std::size_t Position = 0;
            if (!In.empty() && In[0] == '/')
            {
                Out = "/";
                Position = 1;
            }
            bool bMissing = false;
            while (Position <= In.size())
            {
                std::size_t Slash = In.find('/', Position);
                if (Slash == std::string::npos)
                    Slash = In.size();
                std::string Component = In.substr(Position, Slash - Position);
                Position = Slash + 1;
                if (Component.empty())
                    continue;
                std::string Directory = Out.empty() ? "." : Out;
                if (!bMissing && Component != "." && Component != "..")
                {
                    struct stat Info;
                    std::string const Exact = (Out.empty() || Out.back() == '/' ? Out : Out + "/") + Component;
                    if (stat(Exact.c_str(), &Info) != 0)
                    {
                        bool bFound = false;
                        if (DIR* const Entries = opendir(Directory.c_str()))
                        {
                            while (dirent* const Entry = readdir(Entries))
                            {
                                if (strcasecmp(Entry->d_name, Component.c_str()) == 0)
                                {
                                    Component = Entry->d_name;
                                    bFound = true;
                                    break;
                                }
                            }
                            closedir(Entries);
                        }
                        bMissing = !bFound;
                    }
                }
                if (!Out.empty() && Out.back() != '/')
                    Out += '/';
                Out += Component;
            }
            return Out.empty() ? std::string(".") : Out;
        }

        // Windows FILETIME (100 ns since 1601) from a Unix time.
        inline void ToFileTime(std::time_t Seconds, FlULong* Low, FlULong* High)
        {
            std::uint64_t const Ticks = (static_cast<std::uint64_t>(Seconds) + 11644473600ull) * 10000000ull;
            *Low = static_cast<FlULong>(Ticks & 0xFFFFFFFFu);
            *High = static_cast<FlULong>(Ticks >> 32);
        }
    }

    // ---------------------------------------------------------------------------------------
    // Files
    // ---------------------------------------------------------------------------------------

    // Access, sharing and creation values, as GENERIC_*, FILE_SHARE_* and the creation
    // disposition. Sharing is not enforced on POSIX.
    constexpr FlULong kAccessRead = 0x80000000u;
    constexpr FlULong kAccessWrite = 0x40000000u;
    constexpr FlULong kShareRead = 0x1;
    constexpr FlULong kShareWrite = 0x2;
    constexpr FlULong kCreateNew = 1;
    constexpr FlULong kCreateAlways = 2;
    constexpr FlULong kOpenExisting = 3;
    constexpr FlULong kOpenAlways = 4;
    constexpr FlULong kTruncateExisting = 5;

    namespace Detail
    {
        struct FFile : FObject
        {
            int Descriptor = -1;
            ~FFile() override
            {
                if (Descriptor >= 0)
                    close(Descriptor);
            }
        };
    }

    // CreateFileA: InvalidHandle() on failure.
    inline Handle FileOpen(char const* Path, FlULong Access, FlULong Share, FlULong Creation,
                           FlULong FlagsAndAttributes = 0)
    {
        (void)Share;
        (void)FlagsAndAttributes;
        int Flags = (Access & kAccessWrite) != 0 ? ((Access & kAccessRead) != 0 ? O_RDWR : O_WRONLY) : O_RDONLY;
        switch (Creation)
        {
        case kCreateNew: Flags |= O_CREAT | O_EXCL; break;
        case kCreateAlways: Flags |= O_CREAT | O_TRUNC; break;
        case kOpenAlways: Flags |= O_CREAT; break;
        case kTruncateExisting: Flags |= O_TRUNC; break;
        default: break; // kOpenExisting
        }
        std::string const Resolved = Detail::ResolvePath(Path);
        struct stat Info;
        if (stat(Resolved.c_str(), &Info) == 0 && S_ISDIR(Info.st_mode))
            return InvalidHandle(); // CreateFileA opens no directory without FILE_FLAG_BACKUP_SEMANTICS
        int const Descriptor = open(Resolved.c_str(), Flags | O_CLOEXEC, 0644);
        if (Descriptor < 0)
            return InvalidHandle();
        Detail::FFile* const File = new (std::nothrow) Detail::FFile();
        if (File == nullptr)
        {
            close(Descriptor);
            return InvalidHandle();
        }
        File->Descriptor = Descriptor;
        return File;
    }

    // ReadFile, synchronous: reads until Size bytes or the end of the file.
    inline bool FileRead(Handle File, void* Buffer, FlULong Size, FlULong* BytesRead)
    {
        if (BytesRead != nullptr)
            *BytesRead = 0;
        if (!Detail::Valid(File))
            return false;
        int const Descriptor = static_cast<Detail::FFile*>(File)->Descriptor;
        FlULong Done = 0;
        while (Done < Size)
        {
            ssize_t const Count = read(Descriptor, static_cast<char*>(Buffer) + Done, Size - Done);
            if (Count < 0 && errno == EINTR)
                continue;
            if (Count < 0)
                return false;
            if (Count == 0)
                break;
            Done += static_cast<FlULong>(Count);
        }
        if (BytesRead != nullptr)
            *BytesRead = Done;
        return true;
    }

    // WriteFile, synchronous.
    inline bool FileWrite(Handle File, void const* Buffer, FlULong Size, FlULong* BytesWritten)
    {
        if (BytesWritten != nullptr)
            *BytesWritten = 0;
        if (!Detail::Valid(File))
            return false;
        int const Descriptor = static_cast<Detail::FFile*>(File)->Descriptor;
        FlULong Done = 0;
        while (Done < Size)
        {
            ssize_t const Count = write(Descriptor, static_cast<char const*>(Buffer) + Done, Size - Done);
            if (Count < 0 && errno == EINTR)
                continue;
            if (Count <= 0)
                return false;
            Done += static_cast<FlULong>(Count);
        }
        if (BytesWritten != nullptr)
            *BytesWritten = Done;
        return true;
    }

    // GetFileSize: the low 32 bits; 0xffffffff (INVALID_FILE_SIZE) on failure.
    inline FlULong FileGetSize(Handle File)
    {
        struct stat Info;
        if (!Detail::Valid(File) || fstat(static_cast<Detail::FFile*>(File)->Descriptor, &Info) != 0)
            return 0xFFFFFFFFu;
        return static_cast<FlULong>(static_cast<std::uint64_t>(Info.st_size) & 0xFFFFFFFFu);
    }

    // File attribute values (FILE_ATTRIBUTE_*).
    constexpr FlULong kInvalidAttributes = 0xFFFFFFFFu;
    constexpr FlULong kAttributeReadOnly = 0x1;
    constexpr FlULong kAttributeDirectory = 0x10;
    constexpr FlULong kAttributeNormal = 0x80;

    // GetFileAttributesA: kInvalidAttributes when the path does not exist.
    inline FlULong PathAttributes(char const* Path)
    {
        struct stat Info;
        if (stat(Detail::ResolvePath(Path).c_str(), &Info) != 0)
            return kInvalidAttributes;
        if (S_ISDIR(Info.st_mode))
            return kAttributeDirectory;
        return (Info.st_mode & S_IWUSR) != 0 ? kAttributeNormal : kAttributeReadOnly;
    }

    // CopyFileA. FailIfExists false overwrites the destination.
    inline bool FileCopy(char const* From, char const* To, bool FailIfExists)
    {
        Handle const Source = FileOpen(From, kAccessRead, kShareRead, kOpenExisting);
        if (Source == InvalidHandle())
            return false;
        Handle const Target = FileOpen(To, kAccessWrite, 0, FailIfExists ? kCreateNew : kCreateAlways);
        bool bOk = Target != InvalidHandle();
        char Buffer[64 * 1024];
        while (bOk)
        {
            FlULong Read = 0;
            FlULong Written = 0;
            bOk = FileRead(Source, Buffer, sizeof(Buffer), &Read);
            if (!bOk || Read == 0)
                break;
            bOk = FileWrite(Target, Buffer, Read, &Written) && Written == Read;
        }
        CloseHandle(Source);
        if (Target != InvalidHandle())
            CloseHandle(Target);
        return bOk;
    }

    // MoveFileA: fails when the destination exists, as on Windows.
    inline bool FileMove(char const* From, char const* To)
    {
        std::string const Target = Detail::ResolvePath(To);
        struct stat Info;
        if (stat(Target.c_str(), &Info) == 0)
            return false;
        return std::rename(Detail::ResolvePath(From).c_str(), Target.c_str()) == 0;
    }

    // WIN32_FIND_DATAA with its field names and layout (320 bytes).
    struct FileTime
    {
        FlULong dwLowDateTime;
        FlULong dwHighDateTime;
    };
    struct FindData
    {
        FlULong dwFileAttributes;
        FileTime ftCreationTime;
        FileTime ftLastAccessTime;
        FileTime ftLastWriteTime;
        FlULong nFileSizeHigh;
        FlULong nFileSizeLow;
        FlULong dwReserved0;
        FlULong dwReserved1;
        char cFileName[260];
        char cAlternateFileName[14];
    };
    static_assert(sizeof(FindData) == 320, "WIN32_FIND_DATAA's layout");

    namespace Detail
    {
        struct FFind : FObject
        {
            DIR* Entries = nullptr;
            std::string Directory;
            std::string Pattern;
            ~FFind() override
            {
                if (Entries != nullptr)
                    closedir(Entries);
            }
        };

        // FindFirstFileA's wildcards, without regard to case: '*' any run, '?' one character.
        // "*.*" also matches names without a dot, as on Windows.
        inline bool Matches(char const* Pattern, char const* Name)
        {
            if (std::strcmp(Pattern, "*.*") == 0)
                return true;
            if (*Pattern == 0)
                return *Name == 0;
            if (*Pattern == '*')
                return Matches(Pattern + 1, Name) || (*Name != 0 && Matches(Pattern, Name + 1));
            if (*Name == 0)
                return false;
            if (*Pattern == '?' || std::tolower(static_cast<unsigned char>(*Pattern)) ==
                                       std::tolower(static_cast<unsigned char>(*Name)))
                return Matches(Pattern + 1, Name + 1);
            return false;
        }

        // The next entry of Search that matches, filled into Data.
        inline bool NextMatch(FFind* Search, FindData* Data)
        {
            while (dirent* const Entry = readdir(Search->Entries))
            {
                if (!Matches(Search->Pattern.c_str(), Entry->d_name))
                    continue;
                std::string const Full = Search->Directory + "/" + Entry->d_name;
                struct stat Info;
                if (stat(Full.c_str(), &Info) != 0)
                    continue;
                std::memset(Data, 0, sizeof(*Data));
                Data->dwFileAttributes = S_ISDIR(Info.st_mode) ? kAttributeDirectory : kAttributeNormal;
                std::uint64_t const Size = static_cast<std::uint64_t>(Info.st_size);
                Data->nFileSizeHigh = static_cast<FlULong>(Size >> 32);
                Data->nFileSizeLow = static_cast<FlULong>(Size & 0xFFFFFFFFu);
                ToFileTime(Info.st_mtime, &Data->ftLastWriteTime.dwLowDateTime, &Data->ftLastWriteTime.dwHighDateTime);
                ToFileTime(Info.st_atime, &Data->ftLastAccessTime.dwLowDateTime, &Data->ftLastAccessTime.dwHighDateTime);
                ToFileTime(Info.st_ctime, &Data->ftCreationTime.dwLowDateTime, &Data->ftCreationTime.dwHighDateTime);
                std::snprintf(Data->cFileName, sizeof(Data->cFileName), "%s", Entry->d_name);
                return true;
            }
            return false;
        }
    }

    // FindFirstFileA: InvalidHandle() when nothing matches.
    inline Handle FindFirst(char const* Pattern, FindData* Data)
    {
        std::string Text = Pattern != nullptr ? Pattern : "";
        for (char& Character : Text)
        {
            if (Character == '\\')
                Character = '/';
        }
        std::size_t const Slash = Text.find_last_of('/');
        Detail::FFind* const Search = new (std::nothrow) Detail::FFind();
        if (Search == nullptr)
            return InvalidHandle();
        Search->Directory = Detail::ResolvePath(Slash == std::string::npos ? "." : Text.substr(0, Slash).c_str());
        Search->Pattern = Slash == std::string::npos ? Text : Text.substr(Slash + 1);
        Search->Entries = opendir(Search->Directory.c_str());
        if (Search->Entries == nullptr || !Detail::NextMatch(Search, Data))
        {
            delete Search;
            return InvalidHandle();
        }
        return Search;
    }

    // FindNextFileA.
    inline bool FindNext(Handle Search, FindData* Data)
    {
        return Detail::Valid(Search) && Detail::NextMatch(static_cast<Detail::FFind*>(Search), Data);
    }

    // FindClose.
    inline bool FindClose(Handle Search) { return CloseHandle(Search); }

    // ---------------------------------------------------------------------------------------
    // Synchronisation
    // ---------------------------------------------------------------------------------------

    constexpr FlULong kWaitForever = 0xFFFFFFFFu;
    constexpr FlULong kWaitSignaled = 0;
    constexpr FlULong kWaitAbandoned = 0x80;
    constexpr FlULong kWaitTimeout = 0x102;
    constexpr FlULong kWaitFailed = 0xFFFFFFFFu;

    namespace Detail
    {
        struct FEvent : FWaitable
        {
            bool bManualReset = false;
            bool bSet = false;
            bool Signalled() const override { return bSet; }
            void Acquire() override
            {
                if (!bManualReset)
                    bSet = false;
            }
        };

        struct FMutex : FWaitable
        {
            FlULong Owner = 0;
            FlULong Count = 0;
            bool Signalled() const override { return Count == 0 || Owner == ThisThreadId(); }
            void Acquire() override
            {
                Owner = ThisThreadId();
                ++Count;
            }
        };

        // Waits for one object (WaitAll false: any, true: all), as WaitForMultipleObjects.
        inline FlULong Wait(FlULong Count, Handle const* Objects, bool WaitAll, FlULong TimeoutMs)
        {
            if (Count == 0 || Objects == nullptr)
                return kWaitFailed;
            for (FlULong Index = 0; Index < Count; ++Index)
            {
                if (!Valid(Objects[Index]))
                    return kWaitFailed;
            }
            auto Ready = [&](FlULong* Which) {
                for (FlULong Index = 0; Index < Count; ++Index)
                {
                    bool const bSignalled = static_cast<FWaitable*>(Objects[Index])->Signalled();
                    if (!WaitAll && bSignalled)
                    {
                        *Which = Index;
                        return true;
                    }
                    if (WaitAll && !bSignalled)
                        return false;
                }
                *Which = 0;
                return WaitAll;
            };
            std::unique_lock<std::mutex> Lock(WaitLock());
            FlULong Which = 0;
            auto const Deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(TimeoutMs);
            while (!Ready(&Which))
            {
                if (TimeoutMs == kWaitForever)
                    WaitSignal().wait(Lock);
                else if (WaitSignal().wait_until(Lock, Deadline) == std::cv_status::timeout && !Ready(&Which))
                    return kWaitTimeout;
            }
            if (WaitAll)
            {
                for (FlULong Index = 0; Index < Count; ++Index)
                    static_cast<FWaitable*>(Objects[Index])->Acquire();
                return kWaitSignaled;
            }
            static_cast<FWaitable*>(Objects[Which])->Acquire();
            return kWaitSignaled + Which;
        }
    }

    // CreateEventA. Named events are not shared between processes here. nullptr on failure.
    inline Handle EventCreate(bool ManualReset, bool InitiallySet, char const* Name = nullptr)
    {
        (void)Name;
        Detail::FEvent* const Event = new (std::nothrow) Detail::FEvent();
        if (Event != nullptr)
        {
            Event->bManualReset = ManualReset;
            Event->bSet = InitiallySet;
        }
        return Event;
    }

    // SetEvent / ResetEvent.
    inline bool EventSet(Handle Event)
    {
        if (!Detail::Valid(Event))
            return false;
        {
            std::lock_guard<std::mutex> Lock(Detail::WaitLock());
            static_cast<Detail::FEvent*>(Event)->bSet = true;
        }
        Detail::WaitSignal().notify_all();
        return true;
    }
    inline bool EventReset(Handle Event)
    {
        if (!Detail::Valid(Event))
            return false;
        std::lock_guard<std::mutex> Lock(Detail::WaitLock());
        static_cast<Detail::FEvent*>(Event)->bSet = false;
        return true;
    }

    // CreateMutexA, not initially owned. nullptr on failure.
    inline Handle MutexCreate(char const* Name = nullptr)
    {
        (void)Name;
        return new (std::nothrow) Detail::FMutex();
    }

    // ReleaseMutex: fails for a thread that does not own it.
    inline bool MutexRelease(Handle Mutex)
    {
        if (!Detail::Valid(Mutex))
            return false;
        {
            std::lock_guard<std::mutex> Lock(Detail::WaitLock());
            Detail::FMutex* const Object = static_cast<Detail::FMutex*>(Mutex);
            if (Object->Count == 0 || Object->Owner != Detail::ThisThreadId())
                return false;
            if (--Object->Count == 0)
                Object->Owner = 0;
        }
        Detail::WaitSignal().notify_all();
        return true;
    }

    // WaitForSingleObject.
    inline FlULong WaitOne(Handle Object, FlULong TimeoutMs) { return Detail::Wait(1, &Object, false, TimeoutMs); }

    // WaitForMultipleObjects. WaitAll false returns kWaitSignaled + the index of the object that
    // ended the wait.
    inline FlULong WaitMany(FlULong Count, Handle const* Objects, bool WaitAll, FlULong TimeoutMs)
    {
        return Detail::Wait(Count, Objects, WaitAll, TimeoutMs);
    }

    // An in-process lock, recursive for its owner, as CRITICAL_SECTION (its size only matters
    // on Win32).
    struct CriticalSection
    {
        alignas(std::recursive_mutex) unsigned char Storage[sizeof(std::recursive_mutex)];
    };

    inline void LockInit(CriticalSection* Lock) { new (Lock->Storage) std::recursive_mutex(); }
    inline void LockEnter(CriticalSection* Lock) { reinterpret_cast<std::recursive_mutex*>(Lock->Storage)->lock(); }
    inline void LockLeave(CriticalSection* Lock) { reinterpret_cast<std::recursive_mutex*>(Lock->Storage)->unlock(); }
    inline void LockDelete(CriticalSection* Lock)
    {
        reinterpret_cast<std::recursive_mutex*>(Lock->Storage)->~recursive_mutex();
    }

    // Interlocked*: full-barrier atomics on a 32-bit value. Increment and Decrement return the
    // new value, Exchange and CompareExchange the previous one.
    inline FlLong AtomicIncrement(FlLong volatile* Value) { return __atomic_add_fetch(Value, 1, __ATOMIC_SEQ_CST); }
    inline FlLong AtomicDecrement(FlLong volatile* Value) { return __atomic_sub_fetch(Value, 1, __ATOMIC_SEQ_CST); }
    inline FlLong AtomicExchange(FlLong volatile* Value, FlLong NewValue)
    {
        return __atomic_exchange_n(Value, NewValue, __ATOMIC_SEQ_CST);
    }
    inline FlLong AtomicCompareExchange(FlLong volatile* Value, FlLong NewValue, FlLong Expected)
    {
        __atomic_compare_exchange_n(Value, &Expected, NewValue, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
        return Expected; // the previous value, whether or not it was replaced
    }

    // ---------------------------------------------------------------------------------------
    // Threads
    // ---------------------------------------------------------------------------------------

    // A thread entry point (LPTHREAD_START_ROUTINE).
    using ThreadProc = FlULong (*)(void* Parameter);

    // Thread priorities (THREAD_PRIORITY_*). Accepted and not applied: SCHED_OTHER has no
    // per-thread priorities without privileges.
    constexpr int kPriorityNormal = 0;
    constexpr int kPriorityAboveNormal = 1;
    constexpr int kPriorityHighest = 2;

    namespace Detail
    {
        struct FThread : FWaitable
        {
            ThreadProc Proc = nullptr;
            void* Parameter = nullptr;
            FlULong Suspended = 0;
            FlULong Id = 0;
            FlULong ExitCode = 0;
            bool bDone = false;
            pthread_t Native{};
            bool Signalled() const override { return bDone; }
            ~FThread() override { pthread_detach(Native); }
        };

        inline void* RunThread(void* Argument)
        {
            FThread* const Thread = static_cast<FThread*>(Argument);
            {
                std::unique_lock<std::mutex> Lock(WaitLock());
                Thread->Id = ThisThreadId();
                WaitSignal().notify_all();
                WaitSignal().wait(Lock, [Thread] { return Thread->Suspended == 0; });
            }
            FlULong const Result = Thread->Proc(Thread->Parameter);
            {
                std::lock_guard<std::mutex> Lock(WaitLock());
                Thread->ExitCode = Result;
                Thread->bDone = true;
            }
            WaitSignal().notify_all();
            return nullptr;
        }
    }

    // CreateThread. StackSize 0 takes the default. Suspended starts it held until ThreadResume.
    // ThreadId may be nullptr. nullptr on failure. The handle must outlive the thread (closing it
    // while the thread runs is not supported here).
    inline Handle ThreadCreate(ThreadProc Proc, void* Parameter, FlULong StackSize = 0, bool Suspended = false,
                               FlULong* ThreadId = nullptr)
    {
        Detail::FThread* const Thread = new (std::nothrow) Detail::FThread();
        if (Thread == nullptr)
            return nullptr;
        Thread->Proc = Proc;
        Thread->Parameter = Parameter;
        Thread->Suspended = Suspended ? 1 : 0;
        pthread_attr_t Attributes;
        pthread_attr_init(&Attributes);
        if (StackSize != 0)
            pthread_attr_setstacksize(&Attributes, StackSize < PTHREAD_STACK_MIN ? PTHREAD_STACK_MIN : StackSize);
        int const Result = pthread_create(&Thread->Native, &Attributes, &Detail::RunThread, Thread);
        pthread_attr_destroy(&Attributes);
        if (Result != 0)
        {
            delete Thread;
            return nullptr;
        }
        std::unique_lock<std::mutex> Lock(Detail::WaitLock());
        Detail::WaitSignal().wait(Lock, [Thread] { return Thread->Id != 0; });
        if (ThreadId != nullptr)
            *ThreadId = Thread->Id;
        return Thread;
    }

    // ResumeThread: the previous suspend count, or 0xffffffff on failure.
    inline FlULong ThreadResume(Handle Thread)
    {
        if (!Detail::Valid(Thread))
            return 0xFFFFFFFFu;
        FlULong Previous = 0;
        {
            std::lock_guard<std::mutex> Lock(Detail::WaitLock());
            Detail::FThread* const Object = static_cast<Detail::FThread*>(Thread);
            Previous = Object->Suspended;
            if (Object->Suspended > 0)
                --Object->Suspended;
        }
        Detail::WaitSignal().notify_all();
        return Previous;
    }

    // SetThreadPriority: accepted, not applied (see kPriorityNormal).
    inline bool ThreadSetPriority(Handle Thread, int Priority)
    {
        (void)Priority;
        return Detail::Valid(Thread);
    }

    // TerminateThread: POSIX has no safe equivalent, so it fails.
    inline bool ThreadTerminate(Handle Thread, FlULong ExitCode)
    {
        (void)Thread;
        (void)ExitCode;
        return false;
    }

    // GetCurrentThreadId.
    inline FlULong CurrentThreadId() { return Detail::ThisThreadId(); }

    // Sleep. 0 gives up the rest of the time slice.
    inline void SleepMs(FlULong Milliseconds)
    {
        if (Milliseconds == 0)
            std::this_thread::yield();
        else
            std::this_thread::sleep_for(std::chrono::milliseconds(Milliseconds));
    }

    // Thread-local slots. TlsSlotAlloc returns kInvalidTlsSlot (TLS_OUT_OF_INDEXES) on failure.
    constexpr FlULong kInvalidTlsSlot = 0xFFFFFFFFu;
    inline FlULong TlsSlotAlloc()
    {
        pthread_key_t Key;
        if (pthread_key_create(&Key, nullptr) != 0 || static_cast<std::uint64_t>(Key) >= kInvalidTlsSlot)
            return kInvalidTlsSlot;
        return static_cast<FlULong>(Key);
    }
    inline void* TlsSlotGet(FlULong Slot) { return pthread_getspecific(static_cast<pthread_key_t>(Slot)); }
    inline bool TlsSlotSet(FlULong Slot, void* Value)
    {
        return pthread_setspecific(static_cast<pthread_key_t>(Slot), Value) == 0;
    }

    // ---------------------------------------------------------------------------------------
    // Time
    // ---------------------------------------------------------------------------------------

    namespace Detail
    {
        inline std::uint64_t MonotonicNs()
        {
            timespec Now;
            clock_gettime(CLOCK_MONOTONIC, &Now);
            return static_cast<std::uint64_t>(Now.tv_sec) * 1000000000ull + static_cast<std::uint64_t>(Now.tv_nsec);
        }
    }

    // timeGetTime and GetTickCount: milliseconds from a monotonic clock, wrapping every 49.7 days.
    inline FlULong TimerMs() { return static_cast<FlULong>(Detail::MonotonicNs() / 1000000ull); }
    inline FlULong TickCountMs() { return static_cast<FlULong>(Detail::MonotonicNs() / 1000000ull); }

    // QueryPerformanceCounter / QueryPerformanceFrequency: nanoseconds.
    inline bool PerfCounter(int64_t* Ticks)
    {
        *Ticks = static_cast<int64_t>(Detail::MonotonicNs());
        return true;
    }
    inline bool PerfFrequency(int64_t* TicksPerSecond)
    {
        *TicksPerSecond = 1000000000;
        return true;
    }

    // SYSTEMTIME with its field names: eight 16-bit words.
    struct DateTime
    {
        std::uint16_t wYear;
        std::uint16_t wMonth;
        std::uint16_t wDayOfWeek;
        std::uint16_t wDay;
        std::uint16_t wHour;
        std::uint16_t wMinute;
        std::uint16_t wSecond;
        std::uint16_t wMilliseconds;
    };
    static_assert(sizeof(DateTime) == 16, "SYSTEMTIME's layout");

    // GetLocalTime.
    inline void LocalTime(DateTime* Now)
    {
        timespec Clock;
        clock_gettime(CLOCK_REALTIME, &Clock);
        std::tm Fields{};
        localtime_r(&Clock.tv_sec, &Fields);
        Now->wYear = static_cast<std::uint16_t>(Fields.tm_year + 1900);
        Now->wMonth = static_cast<std::uint16_t>(Fields.tm_mon + 1);
        Now->wDayOfWeek = static_cast<std::uint16_t>(Fields.tm_wday);
        Now->wDay = static_cast<std::uint16_t>(Fields.tm_mday);
        Now->wHour = static_cast<std::uint16_t>(Fields.tm_hour);
        Now->wMinute = static_cast<std::uint16_t>(Fields.tm_min);
        Now->wSecond = static_cast<std::uint16_t>(Fields.tm_sec);
        Now->wMilliseconds = static_cast<std::uint16_t>(Clock.tv_nsec / 1000000);
    }

    // ---------------------------------------------------------------------------------------
    // Modules
    // ---------------------------------------------------------------------------------------

    using Module = void*;
    using Symbol = void*;

    // LoadLibraryA: dlopen. A name ending in .dll is also tried as .dylib (macOS) or .so.
    // nullptr on failure.
    inline Module ModuleLoad(char const* Path)
    {
        std::string Resolved = Detail::ResolvePath(Path);
        if (Module const Loaded = dlopen(Resolved.c_str(), RTLD_NOW | RTLD_LOCAL))
            return Loaded;
        if (Resolved.size() > 4 && strcasecmp(Resolved.c_str() + Resolved.size() - 4, ".dll") == 0)
        {
#if defined(__APPLE__)
            Resolved.replace(Resolved.size() - 4, 4, ".dylib");
#else
            Resolved.replace(Resolved.size() - 4, 4, ".so");
#endif
            return dlopen(Resolved.c_str(), RTLD_NOW | RTLD_LOCAL);
        }
        return nullptr;
    }

    // A resource-only DLL: Windows PE images, which need a resource reader here (M3). nullptr.
    inline Module ResourceModuleLoad(char const* Path)
    {
        (void)Path;
        return nullptr;
    }

    // FreeLibrary.
    inline bool ModuleFree(Module Library) { return Library != nullptr && dlclose(Library) == 0; }

    // GetModuleHandleA: an already loaded module. nullptr names the executable.
    inline Module ModuleFind(char const* Name)
    {
        if (Name == nullptr)
            return dlopen(nullptr, RTLD_NOW);
        return dlopen(Name, RTLD_NOW | RTLD_NOLOAD);
    }

    // GetProcAddress: nullptr when the module has no such export.
    inline Symbol ModuleSymbol(Module Library, char const* Name)
    {
        return Library != nullptr ? dlsym(Library, Name) : nullptr;
    }

    // GetModuleFileNameA for the executable (Library nullptr): the length written, 0 on failure.
    // Other modules are not named here yet.
    inline FlULong ModulePath(Module Library, char* Path, FlULong Size)
    {
        if (Library != nullptr || Path == nullptr || Size == 0)
            return 0;
        char Buffer[4096] = {};
#if defined(__APPLE__)
        std::uint32_t Length = sizeof(Buffer);
        if (_NSGetExecutablePath(Buffer, &Length) != 0)
            return 0;
#else
        ssize_t const Length = readlink("/proc/self/exe", Buffer, sizeof(Buffer) - 1);
        if (Length <= 0)
            return 0;
        Buffer[Length] = 0;
#endif
        std::size_t const Wanted = std::strlen(Buffer);
        std::size_t const Copied = Wanted < Size ? Wanted : Size - 1;
        std::memcpy(Path, Buffer, Copied);
        Path[Copied] = 0;
        return static_cast<FlULong>(Copied);
    }

    // ---------------------------------------------------------------------------------------
    // Paths
    // ---------------------------------------------------------------------------------------

    namespace Detail
    {
        // Copies Text into Out as GetCurrentDirectoryA / GetFullPathNameA do: the length written,
        // or the size needed (with the terminator) when Size is too small.
        inline FlULong CopyOut(std::string const& Text, char* Out, FlULong Size)
        {
            if (Out == nullptr || Size <= Text.size())
                return static_cast<FlULong>(Text.size() + 1);
            std::memcpy(Out, Text.c_str(), Text.size() + 1);
            return static_cast<FlULong>(Text.size());
        }
    }

    // GetCurrentDirectoryA: the length written, or the size needed when Size is too small.
    inline FlULong CurrentDirectory(char* Path, FlULong Size)
    {
        char Buffer[4096];
        if (getcwd(Buffer, sizeof(Buffer)) == nullptr)
            return 0;
        return Detail::CopyOut(Buffer, Path, Size);
    }

    // SetCurrentDirectoryA.
    inline bool ChangeDirectory(char const* Path) { return chdir(Detail::ResolvePath(Path).c_str()) == 0; }

    // GetFullPathNameA: the length written, or the size needed when Size is too small. FilePart
    // (may be nullptr) receives the last component. The path is made absolute and its '.' and
    // '..' are folded; it is not resolved against the disk.
    inline FlULong FullPath(char const* Path, char* Full, FlULong Size, char** FilePart = nullptr)
    {
        std::string Text = Path != nullptr ? Path : "";
        for (char& Character : Text)
        {
            if (Character == '\\')
                Character = '/';
        }
        if (Text.empty() || Text[0] != '/')
        {
            char Buffer[4096];
            if (getcwd(Buffer, sizeof(Buffer)) == nullptr)
                return 0;
            Text = std::string(Buffer) + "/" + Text;
        }
        std::string Folded;
        std::size_t Position = 0;
        while (Position <= Text.size())
        {
            std::size_t Slash = Text.find('/', Position);
            if (Slash == std::string::npos)
                Slash = Text.size();
            std::string const Component = Text.substr(Position, Slash - Position);
            Position = Slash + 1;
            if (Component.empty() || Component == ".")
                continue;
            if (Component == "..")
            {
                std::size_t const Last = Folded.find_last_of('/');
                Folded.erase(Last == std::string::npos ? 0 : Last);
                continue;
            }
            Folded += "/" + Component;
        }
        if (Folded.empty())
            Folded = "/";
        FlULong const Result = Detail::CopyOut(Folded, Full, Size);
        if (FilePart != nullptr && Full != nullptr && Result < Size)
            *FilePart = Full + Folded.find_last_of('/') + 1;
        return Result;
    }

    // GetTempPathA: the length written, with the trailing separator.
    inline FlULong TempDirectory(char* Path, FlULong Size)
    {
        char const* const Variable = std::getenv("TMPDIR");
        std::string Directory = Variable != nullptr && *Variable != 0 ? Variable : "/tmp";
        if (Directory.back() != '/')
            Directory += '/';
        return Detail::CopyOut(Directory, Path, Size);
    }
} // namespace Platform
