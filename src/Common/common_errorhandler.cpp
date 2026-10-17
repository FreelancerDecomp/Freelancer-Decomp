// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// ErrorHandler -- retail FDUMP backend (0x0630ab90-0x0630b6e0). Declared in
// common_errorhandler.h.
#include "common_errorhandler.h"
#include "common_ini.h"  // INI_Reader (activate)
#include "common_misc.h" // File (the log node's embedded file)
#include "dacom.h"       // FDUMP

#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <new>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

// ============================================================================
// ErrorHandler — retail FDUMP backend (Common.dll.c 0xaad30-0xab680 cluster).
// ============================================================================

// DACOM.DLL!stricmp through the isolated TU common_dacom_stricmp.cpp.
int CallDacomStricmp(const char* Left, const char* Right);

namespace
{
    // Common.dll.c ErrorHandler log-channel node, sizeof 0xA0 (operator_new(0xa0) at
    // every allocation site: new_log @ 0xab320, activate's ini loop @ 0xaad30).
    struct LogEntry
    {
        LogEntry* Next;   // +0x00
        bool bEnabled;    // +0x04
        int LogId;         // +0x08  FdumpHook matches it against (Flags >> 4) unless -1
        int SeverityLimit; // +0x0C  FdumpHook drops a call whose severity nibble is
                           //        above it (unsigned compare, .text:0630ac35); 100 by
                           //        default, the exe passes 7 (new_log(path, -1, 7, ..))
        bool bActive;      // +0x10  cached bEnabled && (bText||bWindow||File.Handle);
                           //        the gate FdumpHook reads (.text:0630ac18)
        bool bText;        // +0x11  "$Text" virtual sink; FdumpHook upper-cases the line
                           //        for such a channel when severity <= 2 (.text:0630ac58)
        bool bWindow;      // +0x12  "$Window" virtual sink
        ::File File;       // +0x14  the exported File class (common_misc.h);
                           //        File.Handle is node +0x94
    };
    static_assert(sizeof(LogEntry) == 0xA0, "ErrorHandler LogEntry sizeof 0xA0");

    // Common.dll.c FUN_0630b0d0 @ 0xab0d0 -- default-reset a freshly allocated node.
    void LogEntryResetDefaults(LogEntry* E)
    {
        E->LogId = -1;
        E->SeverityLimit = 100;
        E->bActive = false;
        E->bText = false;
        E->bWindow = false;
        E->bEnabled = true;
    }

    // Common.dll.c FUN_0630b0f0 @ 0xab0f0 -- (re)configure a node's name/routing.
    // Name[0]=='$' selects a virtual sink (DACOM stricmp, .text:0630b12d-0630b15e)
    // instead of opening a real file ("wb", .rdata:063a3494). Retail copies Name into
    // the 0x80-byte embedded buffer with an unbounded byte loop (.text:0630b188); a
    // longer name is undefined behaviour there, the port stops at the buffer.
    void LogEntryConfigure(LogEntry* E, char const* Name, int LogId, int SeverityLimit,
                            bool bFlagText, bool bFlagWindow)
    {
        E->LogId = LogId;
        E->SeverityLimit = SeverityLimit;
        E->bText = bFlagText;
        E->bWindow = bFlagWindow;
        E->File.close();
        if (Name != nullptr)
        {
            if (Name[0] == '$')
            {
                if (CallDacomStricmp(Name, "$Text") == 0)
                    E->bText = true;
                else if (CallDacomStricmp(Name, "$Window") == 0)
                    E->bWindow = true;
            }
            else
            {
                if (E->File.Handle != nullptr)
                    E->File.close();
                if (Name[0] != '\0')
                {
                    E->File.close();
                    size_t Index = 0;
                    for (; Index < sizeof(E->File.Name) - 1 && Name[Index] != '\0'; ++Index)
                        E->File.Name[Index] = Name[Index];
                    E->File.Name[Index] = '\0';
                    E->File.Handle = fopen(E->File.Name, "wb");
                    if (E->File.Handle != nullptr)
                        E->File.get_info();
                }
            }
        }
        E->bActive = E->bEnabled && (E->bText || E->bWindow || E->File.Handle != nullptr);
    }

    // Common.dll.txt 0630b220-0630b26b: the node destructor - File::close from the body,
    // then the member File::~File (= close again, a no-op on the nulled handle) - followed
    // by operator delete at both call sites.
    void LogEntryDestroy(LogEntry* E)
    {
        E->File.close();
        delete E;
    }

    LogEntry* g_ErrorLogListHead = nullptr;  // DAT_063fcf28
    LogEntry* g_ErrorLogListTail = nullptr;  // DAT_063fcf2c
    void(__cdecl* g_PreviousFdump)(int, char const*, ...) = nullptr;  // DAT_063fcf38
    // DAT_063fcf30 / DAT_063fcf34: FdumpHook counts severity-2 (warning) and severity-1
    // (error) calls; activate zeroes both (.text:0630ad7e/0630ad84). Retail never reads
    // them back (XREFs: those writes and the two INCs only).
    int g_FdumpWarningCount = 0;
    int g_FdumpErrorCount = 0;

    // Common.dll.txt 303346-303361 (0x0630ab90, LAB_0630ab90). Retail statically initializes
    // PTR_LAB_063ed214 (g_ErrorAssertHandler below) to point directly at this MessageBoxA
    // wrapper -- it IS the real default AssertHandler, not merely a documentation placeholder,
    // and stays installed whenever ErrorHandler::activate() is called with AssertHandler==null
    // (retail's activate leaves the pointer untouched in that case; see activate() below).
    // Traced push order at the call site (0630aba9-0630abb2): MessageBoxA(hWnd=0, lpText=Text,
    // lpCaption=Caption, uType). uType = bIgnoreIcon ? MB_ABORTRETRYIGNORE (2)
    // : MB_ICONHAND|MB_ABORTRETRYIGNORE (0x12) -- derived from the 3rd (bool) argument at
    // [ESP+0xc] read into CL, matching the AssertHandler callback's own (char const*, char
    // const*, bool) signature.
    int __cdecl AssertHandlerDefault(char const* Text, char const* Caption, bool bIgnoreIcon)
    {
        UINT Style = bIgnoreIcon ? MB_ABORTRETRYIGNORE : (MB_ICONHAND | MB_ABORTRETRYIGNORE);
        return MessageBoxA(nullptr, Text, Caption, Style);
    }

    int(__cdecl* g_ErrorAssertHandler)(char const*, char const*, bool) =
        &AssertHandlerDefault;  // PTR_LAB_063ed214

    // Recomputes and returns a node's cached "effectively active" flag.
    bool LogEntryRefreshActive(LogEntry* E)
    {
        E->bActive = E->bEnabled && (E->bText || E->bWindow || E->File.Handle != nullptr);
        return E->bActive;
    }
}

namespace ErrorHandler
{
    void* __cdecl new_log(char const* Name, int LogId, int SeverityLimit, bool bText, bool bWindow)
    {
        // Common.dll.c ErrorHandler::new_log @ 0xab320. Retail's operator new returns
        // null on failure and the next read ([ESI+4], .text:0630b3a3) faults; the port's
        // throws, so there is no null branch.
        // operator new(0xa0), then File::File on the embedded File (.text:0630b35a); the
        // other fields stay indeterminate until the stores below.
        LogEntry* E = new LogEntry;
        E->Next = nullptr;
        LogEntryResetDefaults(E);
        LogEntryConfigure(E, Name, LogId, SeverityLimit, bText, bWindow);

        if (!LogEntryRefreshActive(E))
        {
            LogEntryDestroy(E);
            return nullptr;
        }

        // Retail's asymmetric insert: non-$Window channels are pushed to the front of
        // the list; $Window channels are appended at the back. First-ever node becomes
        // both head and tail either way.
        if (!E->bWindow)
        {
            E->Next = g_ErrorLogListHead;
            if (g_ErrorLogListTail != nullptr)
            {
                g_ErrorLogListHead = E;
                return E;
            }
        }
        else if (g_ErrorLogListTail != nullptr)
        {
            g_ErrorLogListTail->Next = E;
        }
        if (g_ErrorLogListTail == nullptr)
            g_ErrorLogListHead = E;
        g_ErrorLogListTail = E;
        return E;
    }

    void __cdecl log_enable(int LogId)
    {
        // Common.dll.c ErrorHandler::log_enable @ 0xab460
        for (LogEntry* E = g_ErrorLogListHead; E != nullptr; E = E->Next)
        {
            if (LogId == -1 || E->LogId == LogId)
            {
                E->bEnabled = true;
                LogEntryRefreshActive(E);
            }
        }
    }

    void __cdecl log_disable(int LogId)
    {
        // Common.dll.c ErrorHandler::log_disable @ 0xab4b0
        for (LogEntry* E = g_ErrorLogListHead; E != nullptr; E = E->Next)
        {
            if (LogId == -1 || E->LogId == LogId)
            {
                E->bEnabled = false;
                E->bActive = false;
            }
        }
    }

    bool __cdecl is_log_enabled(int LogId)
    {
        // Common.dll.c ErrorHandler::is_log_enabled @ 0xab4e0. No -1 wildcard here,
        // unlike log_enable/log_disable -- only an exact LogId match is checked.
        for (LogEntry* E = g_ErrorLogListHead; E != nullptr; E = E->Next)
        {
            if (E->LogId == LogId)
                return LogEntryRefreshActive(E);
        }
        return false;
    }

    void __cdecl log_comment(int LogId, char const* Format, ...)
    {
        // Common.dll.txt:0630b540-0630b67d. Per node (LogId -1 matches every node,
        // else [+8] must equal it): recompute [+0x10] (bEnabled && any sink); skip an
        // inactive node; format with vsprintf only while the buffer's first byte is
        // still 0 (.text:0630b5a7 - the "formatted" flag is Buffer[0]); if the text is
        // non-empty, does not end in '\n' and len+1 < 0x200, copy it into the second
        // buffer (0x200 bytes, [ESP+0x10c]) and append "\n"; then, only if the node has
        // a FILE* (+0x94), fwrite + fflush. Retail's format buffer is 0x100 bytes
        // ([ESP+0xc]) and vsprintf is unbounded: a line of 0x100 or more characters runs
        // into the second buffer and past it - undefined behaviour. The port formats
        // into 0x400 bytes (_vsnprintf, truncated), so every line shorter than 0x100
        // is written exactly as retail writes it.
        char Buffer[0x400];
        Buffer[0] = '\0';

        for (LogEntry* E = g_ErrorLogListHead; E != nullptr; E = E->Next)
        {
            if (LogId != -1 && E->LogId != LogId)
                continue;
            if (!LogEntryRefreshActive(E))
                continue;

            if (Buffer[0] == '\0')
            {
                va_list Args;
                va_start(Args, Format);
                _vsnprintf(Buffer, sizeof(Buffer), Format, Args);
                va_end(Args);
                Buffer[sizeof(Buffer) - 1] = '\0';
            }

            char const* Line = Buffer;
            char LineWithNewline[0x200];
            int const Len = (int)strlen(Buffer);
            if (Len > 0 && Buffer[Len - 1] != '\n' &&
                (unsigned)(Len + 1) < sizeof(LineWithNewline))
            {
                memcpy(LineWithNewline, Buffer, Len);
                LineWithNewline[Len] = '\n';
                LineWithNewline[Len + 1] = '\0';
                Line = LineWithNewline;
            }

            if (E->File.Handle == nullptr)
                continue;
            fwrite(Line, 1, strlen(Line), E->File.Handle);
            fflush(E->File.Handle);
        }
    }

    void __cdecl deactivate()
    {
        // Common.dll.txt:0630b2a0-0630b317. Restores whatever FDUMP was installed before
        // activate(), zeroes bEnabled/bActive of every node with LogId 0x52
        // (.text:0630b2d1-0630b2d9), then frees every node (FUN_0630b220 + delete) and
        // clears head and tail.
        FDUMP = g_PreviousFdump;
        g_PreviousFdump = nullptr;

        for (LogEntry* E = g_ErrorLogListHead; E != nullptr; E = E->Next)
        {
            if (E->LogId == 0x52)
            {
                E->bEnabled = false;
                E->bActive = false;
            }
        }

        LogEntry* E = g_ErrorLogListHead;
        while (E != nullptr)
        {
            LogEntry* NextEntry = E->Next;
            LogEntryDestroy(E);
            E = NextEntry;
        }
        g_ErrorLogListHead = nullptr;
        g_ErrorLogListTail = nullptr;
    }

    namespace
    {
        // LAB_0630abd0 (.text:0630abd0-0630ad2b), installed as FDUMP by activate
        // (.text:0630ad5f). Retail returns 0 in EAX; the port's FDUMP pointer type
        // (dacom.h) is void.
        void __cdecl FdumpHook(int Flags, char const* Format, ...)
        {
            // Port-only debugger echo (owner's lines, kept on purpose): format once and
            // send it to the debugger. Not part of retail.
            char Buffer[1024];
            va_list Args;
            va_start(Args, Format);
            _vsnprintf(Buffer, sizeof(Buffer), Format, Args);
            va_end(Args);
            Buffer[sizeof(Buffer) - 1] = '\0';
            OutputDebugStringA(Buffer); /// PORT ONLY, DEVIATION, FOR DEBUGGING
            OutputDebugStringA("\n"); /// PORT ONLY, DEVIATION, FOR DEBUGGING

            // Severity = the signed low nibble of Flags (SHL 0x1c / SAR 0x1c,
            // .text:0630abde-0630abe2). Unsigned <= 2: 2 counts a warning, 1 an error.
            int const Severity = (int)((unsigned)Flags << 28) >> 28;
            unsigned const SeverityBits = (unsigned)Severity;
            if (SeverityBits <= 2)
            {
                if (Severity == 2)
                    ++g_FdumpWarningCount;
                if (Severity == 1)
                    ++g_FdumpErrorCount;
            }

            for (LogEntry* E = g_ErrorLogListHead; E != nullptr; E = E->Next)
            {
                // .text:0630ac18-0630ac38: the cached active flag, the LogId route
                // (Flags SAR 4 against [+8] unless [+8] is -1) and the severity limit
                // (unsigned JA against [+0xc]).
                if (!E->bActive)
                    continue;
                if (E->LogId != -1 && (Flags >> 4) != E->LogId)
                    continue;
                if (SeverityBits > (unsigned)E->SeverityLimit)
                    continue;

                // vsprintf per node into 0x400 bytes at [ESP+0x210] (.text:0630ac52).
                // Retail's vsprintf is unbounded (a longer line is undefined behaviour);
                // the port truncates at the buffer.
                char Line[0x400];
                va_start(Args, Format);
                _vsnprintf(Line, sizeof(Line), Format, Args);
                va_end(Args);
                Line[sizeof(Line) - 1] = '\0';

                if (E->bText && SeverityBits <= 2)
                    _strupr(Line);

                // .text:0630ac78-0630acd0: append "\n" through the 0x200-byte buffer at
                // [ESP+0x10] when the text is non-empty, lacks it and len+1 < 0x200.
                char const* Out = Line;
                char LineWithNewline[0x200];
                int const Len = (int)strlen(Line);
                if (Len > 0 && Line[Len - 1] != '\n' &&
                    (unsigned)(Len + 1) < sizeof(LineWithNewline))
                {
                    memcpy(LineWithNewline, Line, Len);
                    LineWithNewline[Len] = '\n';
                    LineWithNewline[Len + 1] = '\0';
                    Out = LineWithNewline;
                }

                // .text:0630acd4-0630ad1a: only a node with a FILE* (+0x94) is written.
                if (E->File.Handle == nullptr)
                    continue;
                fwrite(Out, 1, strlen(Out), E->File.Handle);
                fflush(E->File.Handle);
            }
        }
    }

    void __cdecl activate(char const* IniPath,
                           int (__cdecl* AssertHandler)(char const*, char const*, bool))
    {
        // Common.dll.txt:0630ad30-0630b0c8. Saves FDUMP, installs FdumpHook, stores a
        // non-null AssertHandler into PTR_LAB_063ed214, zeroes the two severity
        // counters, then reads [Error] "log" lines from IniPath (open(IniPath, false)
        // with no null check of its own).
        g_PreviousFdump = FDUMP;
        FDUMP = &FdumpHook;
        if (AssertHandler != nullptr)
            g_ErrorAssertHandler = AssertHandler;
        g_FdumpWarningCount = 0;
        g_FdumpErrorCount = 0;

        INI_Reader Reader;
        if (!Reader.open(IniPath, false) || !Reader.read_header())
            return;
        do
        {
            if (Reader.is_header("Error") && Reader.read_value())
            {
                do
                {
                    if (Reader.is_value("log"))
                    {
                        // Common.dll.txt:0630ae1e-0630ae70. get_value_string(0) returns
                        // StringIndex's shared static buffer (DAT_06401350), which the
                        // get_value_float(1/2) calls below overwrite, so retail copies the
                        // name into a local 128-byte buffer first: length = strlen capped
                        // at 0x7f, REP MOVS, NUL at [len]; a null result leaves it empty.
                        // new_log receives this local copy (0630af00 LEA EAX,[ESP+0x28]).
                        char Name[0x80];
                        Name[0] = '\0';
                        char const* NameSource = Reader.get_value_string(0);
                        if (NameSource != nullptr)
                        {
                            size_t NameLen = strlen(NameSource);
                            if (NameLen >= 0x80)
                                NameLen = 0x7f;
                            memcpy(Name, NameSource, NameLen);
                            Name[NameLen] = '\0';
                        }
                        // Common.dll.txt:0630ae76-0630af85. LogId defaults to -1 and the
                        // severity limit to 100; the optional 2nd/3rd fields override
                        // them (ftol). Retail then builds the node inline: operator
                        // new(0xa0), File::File, FUN_0630b0d0, FUN_0630b0f0(Name, LogId,
                        // Limit, false, false), the active test, FUN_0630b220 + delete
                        // when inactive, else FUN_0630b6e0 (append) for $Window or a
                        // push to the front - the same steps new_log takes, so the port
                        // calls new_log.
                        int LogId = -1;
                        int SeverityLimit = 100;
                        if (!Reader.is_value_empty(1))
                            LogId = (int)(long long)Reader.get_value_float(1);
                        if (!Reader.is_value_empty(2))
                            SeverityLimit = (int)(long long)Reader.get_value_float(2);
                        new_log(Name, LogId, SeverityLimit, false, false);
                    }
                } while (Reader.read_value());
            }
        } while (Reader.read_header());
    }

    void __cdecl log_last_error(char const* Source, int Line, SeverityLevel Severity,
                                 char* Format, unsigned long ErrorCode)
    {
        // Common.dll.txt:0630b680. Full trace: ECX=ErrorCode captured up front (the
        // caller's stack slot for it is then reused as the local `char* MsgBuf` that
        // receives FormatMessageA's allocated pointer, since ALLOCATE_BUFFER treats
        // lpBuffer as an LPSTR* out-param). FDUMP's arg order (verified via push
        // order/stack offsets, not the missing decompilation) is
        // FDUMP(flags, Format, Source, Line, MsgBuf) -- Format is param_4, not a
        // hardcoded literal; Source/Line/MsgBuf are its %s/%d/%s substitutions.
        char* MsgBuf = nullptr;
        FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                            FORMAT_MESSAGE_IGNORE_INSERTS,
                        nullptr, ErrorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                        (LPSTR)&MsgBuf, 0, nullptr);

        int Flags = (Severity & 0xf) | 0x100000;
        FDUMP(Flags, Format, Source, Line, MsgBuf);

        LocalFree(MsgBuf);
    }
}
