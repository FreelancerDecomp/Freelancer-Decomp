// Freelancer Decomp: FLRegress, the headless regression harness (port-only, not part of retail).
//
// See regress_crash.h. The dump and report are written from a helper thread: the crashed
// thread may have run out of stack, and StackWalk64 wants to walk a thread that is stopped.

#include "regress_crash.h"

#include <windows.h>
#include <dbghelp.h>

#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
    // RaiseException codes for CRT failures that never reach the unhandled-exception filter
    // on their own ("E" for error, "FLR" in the low bytes).
    const DWORD CrashCodeAbort = 0xE0464C01;
    const DWORD CrashCodePureCall = 0xE0464C02;
    const DWORD CrashCodeInvalidParameter = 0xE0464C03;

    const int CrashExitCode = 4;
    const int MaxFrames = 64;

    char GDirectory[MAX_PATH] = ".";
    volatile const char* const* GPhase = nullptr;
    volatile LONG GCrashing = 0;

    struct FCrashRequest
    {
        EXCEPTION_POINTERS* Pointers;
        DWORD ThreadId;
    };

    const char* ExceptionName(DWORD Code)
    {
        switch (Code)
        {
        case EXCEPTION_ACCESS_VIOLATION: return "EXCEPTION_ACCESS_VIOLATION";
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
        case EXCEPTION_DATATYPE_MISALIGNMENT: return "EXCEPTION_DATATYPE_MISALIGNMENT";
        case EXCEPTION_FLT_DENORMAL_OPERAND: return "EXCEPTION_FLT_DENORMAL_OPERAND";
        case EXCEPTION_FLT_DIVIDE_BY_ZERO: return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
        case EXCEPTION_FLT_INEXACT_RESULT: return "EXCEPTION_FLT_INEXACT_RESULT";
        case EXCEPTION_FLT_INVALID_OPERATION: return "EXCEPTION_FLT_INVALID_OPERATION";
        case EXCEPTION_FLT_OVERFLOW: return "EXCEPTION_FLT_OVERFLOW";
        case EXCEPTION_FLT_STACK_CHECK: return "EXCEPTION_FLT_STACK_CHECK";
        case EXCEPTION_FLT_UNDERFLOW: return "EXCEPTION_FLT_UNDERFLOW";
        case EXCEPTION_ILLEGAL_INSTRUCTION: return "EXCEPTION_ILLEGAL_INSTRUCTION";
        case EXCEPTION_IN_PAGE_ERROR: return "EXCEPTION_IN_PAGE_ERROR";
        case EXCEPTION_INT_DIVIDE_BY_ZERO: return "EXCEPTION_INT_DIVIDE_BY_ZERO";
        case EXCEPTION_INT_OVERFLOW: return "EXCEPTION_INT_OVERFLOW";
        case EXCEPTION_PRIV_INSTRUCTION: return "EXCEPTION_PRIV_INSTRUCTION";
        case EXCEPTION_STACK_OVERFLOW: return "EXCEPTION_STACK_OVERFLOW";
        case 0xC0000409: return "STATUS_STACK_BUFFER_OVERRUN";
        case 0xE06D7363: return "C++ exception";
        case CrashCodeAbort: return "abort()";
        case CrashCodePureCall: return "pure virtual call";
        case CrashCodeInvalidParameter: return "CRT invalid parameter";
        default: return "unknown exception";
        }
    }

    // "Server.dll" from the module that contains Address, or "?".
    void ModuleName(DWORD64 Address, char* Out, size_t Size, DWORD64* Base)
    {
        *Base = SymGetModuleBase64(GetCurrentProcess(), Address);
        char Path[MAX_PATH] = "";
        if (*Base == 0 || GetModuleFileNameA((HMODULE)(uintptr_t)*Base, Path, MAX_PATH) == 0)
        {
            strncpy(Out, "?", Size);
            Out[Size - 1] = '\0';
            return;
        }
        const char* Slash = strrchr(Path, '\\');
        strncpy(Out, Slash != nullptr ? Slash + 1 : Path, Size);
        Out[Size - 1] = '\0';
    }

    // One stack frame as "Module!Function+0xN (file:line)" or "Module+0xOffset".
    // Returns true when the frame resolved to a symbol from one of our PDBs.
    bool DescribeFrame(DWORD64 Address, char* Out, size_t Size, char* Short, size_t ShortSize)
    {
        HANDLE Process = GetCurrentProcess();
        char Module[MAX_PATH];
        DWORD64 Base = 0;
        ModuleName(Address, Module, sizeof(Module), &Base);

        alignas(SYMBOL_INFO) char Buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME];
        SYMBOL_INFO* Symbol = (SYMBOL_INFO*)Buffer;
        memset(Buffer, 0, sizeof(Buffer));
        Symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        Symbol->MaxNameLen = MAX_SYM_NAME;
        DWORD64 Displacement = 0;
        if (!SymFromAddr(Process, Address, &Displacement, Symbol))
        {
            _snprintf(Out, Size, "%s+0x%llx", Module, (unsigned long long)(Address - Base));
            _snprintf(Short, ShortSize, "%s+0x%llx", Module, (unsigned long long)(Address - Base));
            Out[Size - 1] = '\0';
            Short[ShortSize - 1] = '\0';
            return false;
        }

        IMAGEHLP_LINE64 Line;
        memset(&Line, 0, sizeof(Line));
        Line.SizeOfStruct = sizeof(Line);
        DWORD LineDisplacement = 0;
        if (SymGetLineFromAddr64(Process, Address, &LineDisplacement, &Line))
        {
            const char* File = Line.FileName;
            const char* Src = strstr(File, "\\src\\");
            _snprintf(Out, Size, "%s!%s+0x%llx (%s:%lu)", Module, Symbol->Name,
                (unsigned long long)Displacement, Src != nullptr ? Src + 1 : File, Line.LineNumber);
        }
        else
        {
            _snprintf(Out, Size, "%s!%s+0x%llx", Module, Symbol->Name, (unsigned long long)Displacement);
        }
        _snprintf(Short, ShortSize, "%s!%s", Module, Symbol->Name);
        Out[Size - 1] = '\0';
        Short[ShortSize - 1] = '\0';
        return true;
    }

    // Our CRT-failure frames and the crash handler itself are not where the bug is.
    bool IsHandlerFrame(const char* Short)
    {
        return strstr(Short, "RaiseCrash") != nullptr || strstr(Short, "!raise") != nullptr ||
               strstr(Short, "!abort") != nullptr || strstr(Short, "RaiseException") != nullptr ||
               strstr(Short, "_purecall") != nullptr || strstr(Short, "_invalid_parameter") != nullptr ||
               strstr(Short, "_invoke_watson") != nullptr || strstr(Short, "_CxxThrowException") != nullptr;
    }

    void WriteMinidump(const FCrashRequest& Request, const char* Path)
    {
        HANDLE File = CreateFileA(Path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (File == INVALID_HANDLE_VALUE)
        {
            fprintf(stderr, "FLRegress: cannot write %s\n", Path);
            return;
        }
        MINIDUMP_EXCEPTION_INFORMATION Info;
        Info.ThreadId = Request.ThreadId;
        Info.ExceptionPointers = Request.Pointers;
        Info.ClientPointers = FALSE;
        const MINIDUMP_TYPE Type = (MINIDUMP_TYPE)(MiniDumpNormal | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);
        if (!MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), File, Type, &Info, nullptr, nullptr))
        {
            fprintf(stderr, "FLRegress: MiniDumpWriteDump failed (%lu)\n", GetLastError());
        }
        CloseHandle(File);
    }

    // Writes the report to each of Outs (stderr and the report file).
    void WriteReport(const FCrashRequest& Request, FILE* const* Outs, int OutCount)
    {
        HANDLE Process = GetCurrentProcess();
        const EXCEPTION_RECORD* Record = Request.Pointers->ExceptionRecord;
        CONTEXT Context = *Request.Pointers->ContextRecord;

        SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_FAIL_CRITICAL_ERRORS);
        char ExeDirectory[MAX_PATH] = "";
        GetModuleFileNameA(nullptr, ExeDirectory, MAX_PATH);
        if (char* Slash = strrchr(ExeDirectory, '\\'))
        {
            *Slash = '\0';
        }
        SymInitialize(Process, ExeDirectory, TRUE);

        char Frames[MaxFrames][512];
        char Shorts[MaxFrames][256];
        bool Symbolised[MaxFrames];
        int FrameCount = 0;

        STACKFRAME64 Frame;
        memset(&Frame, 0, sizeof(Frame));
        Frame.AddrPC.Offset = Context.Eip;
        Frame.AddrPC.Mode = AddrModeFlat;
        Frame.AddrFrame.Offset = Context.Ebp;
        Frame.AddrFrame.Mode = AddrModeFlat;
        Frame.AddrStack.Offset = Context.Esp;
        Frame.AddrStack.Mode = AddrModeFlat;
        HANDLE Thread = OpenThread(THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION | THREAD_SUSPEND_RESUME, FALSE, Request.ThreadId);
        while (FrameCount < MaxFrames &&
               StackWalk64(IMAGE_FILE_MACHINE_I386, Process, Thread != nullptr ? Thread : GetCurrentThread(), &Frame,
                   &Context, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
        {
            if (Frame.AddrPC.Offset == 0)
            {
                break;
            }
            Symbolised[FrameCount] = DescribeFrame(Frame.AddrPC.Offset, Frames[FrameCount], sizeof(Frames[0]),
                Shorts[FrameCount], sizeof(Shorts[0]));
            ++FrameCount;
        }
        if (Thread != nullptr)
        {
            CloseHandle(Thread);
        }

        // Signature: the first frame with a symbol that is not part of the CRT failure path,
        // else the faulting module+offset.
        const char* Top = FrameCount > 0 ? Shorts[0] : "?";
        for (int i = 0; i < FrameCount; ++i)
        {
            if (Symbolised[i] && !IsHandlerFrame(Shorts[i]))
            {
                Top = Shorts[i];
                break;
            }
        }

        for (int o = 0; o < OutCount; ++o)
        {
            FILE* Out = Outs[o];
            fprintf(Out, "exception: 0x%08lX %s at 0x%p\n", Record->ExceptionCode,
                ExceptionName(Record->ExceptionCode), Record->ExceptionAddress);
            if (Record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && Record->NumberParameters >= 2)
            {
                const ULONG_PTR Kind = Record->ExceptionInformation[0];
                fprintf(Out, "access: %s 0x%08lX\n", Kind == 0 ? "read of" : Kind == 1 ? "write to" : "execute at",
                    (unsigned long)Record->ExceptionInformation[1]);
            }
            fprintf(Out, "phase: %s\n", GPhase != nullptr && *GPhase != nullptr ? (const char*)*GPhase : "?");
            fprintf(Out, "signature: 0x%08lX %s\n", Record->ExceptionCode, Top);
            fprintf(Out, "stack:\n");
            for (int i = 0; i < FrameCount; ++i)
            {
                fprintf(Out, "  #%02d %s\n", i, Frames[i]);
            }
            fflush(Out);
        }
        SymCleanup(Process);
    }

    DWORD WINAPI CrashThread(LPVOID Parameter)
    {
        const FCrashRequest& Request = *(const FCrashRequest*)Parameter;
        CreateDirectoryA(GDirectory, nullptr);

        char Base[MAX_PATH];
        _snprintf(Base, sizeof(Base), "%s\\FLRegress-%lu", GDirectory, GetCurrentProcessId());
        Base[sizeof(Base) - 1] = '\0';
        char DumpPath[MAX_PATH];
        char ReportPath[MAX_PATH];
        _snprintf(DumpPath, sizeof(DumpPath), "%s.dmp", Base);
        _snprintf(ReportPath, sizeof(ReportPath), "%s.txt", Base);
        DumpPath[sizeof(DumpPath) - 1] = '\0';
        ReportPath[sizeof(ReportPath) - 1] = '\0';

        fprintf(stderr, "FLRegress: crashed; writing %s and %s\n", DumpPath, ReportPath);
        fflush(stderr);
        WriteMinidump(Request, DumpPath);

        FILE* Outs[2] = { stderr, nullptr };
        int OutCount = 1;
        FILE* Report = fopen(ReportPath, "w");
        if (Report != nullptr)
        {
            Outs[OutCount++] = Report;
        }
        WriteReport(Request, Outs, OutCount);
        if (Report != nullptr)
        {
            fclose(Report);
        }
        return 0;
    }

    LONG WINAPI CrashFilter(EXCEPTION_POINTERS* Pointers)
    {
        // A second crash (another thread, or inside the handler) just ends the process.
        if (InterlockedExchange(&GCrashing, 1) != 0)
        {
            TerminateProcess(GetCurrentProcess(), CrashExitCode);
            return EXCEPTION_EXECUTE_HANDLER;
        }
        FCrashRequest Request = { Pointers, GetCurrentThreadId() };
        HANDLE Thread = CreateThread(nullptr, 0, CrashThread, &Request, 0, nullptr);
        if (Thread != nullptr)
        {
            WaitForSingleObject(Thread, 60 * 1000);
            CloseHandle(Thread);
        }
        else
        {
            fprintf(stderr, "FLRegress: crashed (0x%08lX) and could not start the crash writer\n",
                Pointers->ExceptionRecord->ExceptionCode);
        }
        fflush(stderr);
        TerminateProcess(GetCurrentProcess(), CrashExitCode);
        return EXCEPTION_EXECUTE_HANDLER;
    }

    // __declspec(noinline) keeps these frames recognisable for IsHandlerFrame.
    __declspec(noinline) void RaiseCrash(DWORD Code)
    {
        RaiseException(Code, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    }

    void __cdecl OnAbort(int)
    {
        RaiseCrash(CrashCodeAbort);
    }

    void __cdecl OnPureCall()
    {
        RaiseCrash(CrashCodePureCall);
    }

    void __cdecl OnInvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t)
    {
        RaiseCrash(CrashCodeInvalidParameter);
    }
}

void InstallCrashHandler(const char* Directory, volatile const char* const* Phase)
{
    GetFullPathNameA(Directory, MAX_PATH, GDirectory, nullptr);
    GPhase = Phase;

    SetUnhandledExceptionFilter(CrashFilter);
    // abort() would otherwise go straight to Windows Error Reporting, past the filter.
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    signal(SIGABRT, OnAbort);
    _set_purecall_handler(OnPureCall);
    _set_invalid_parameter_handler(OnInvalidParameter);
}
