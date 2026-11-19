# Platform layer

`src/include/platform.h` (and `platform_folders.h` for per-user folders) puts the operating-system
services the engine uses behind one set of functions in namespace `Platform`:

| Area | Functions |
|---|---|
| Files | `FileOpen`, `FileRead`, `FileWrite`, `FileGetSize`, `PathAttributes`, `FileCopy`, `FileMove`, `FindFirst` / `FindNext` / `FindClose`, `CloseHandle` |
| Threads | `ThreadCreate`, `ThreadResume`, `ThreadSetPriority`, `ThreadTerminate`, `CurrentThreadId`, `SleepMs`, `TlsSlotAlloc` / `Get` / `Set` |
| Synchronisation | `EventCreate` / `Set` / `Reset`, `MutexCreate` / `Release`, `WaitOne`, `WaitMany`, `CriticalSection` + `LockInit` / `Enter` / `Leave` / `Delete`, `Atomic*` |
| Time | `TimerMs` (timeGetTime), `TickCountMs` (GetTickCount), `PerfCounter`, `PerfFrequency`, `LocalTime` |
| Modules | `ModuleLoad`, `ModuleFree`, `ModuleFind`, `ModuleSymbol`, `ModulePath` |
| Paths | `CurrentDirectory`, `ChangeDirectory`, `FullPath`, `TempDirectory`, `UserFolder` |

## Rules

- Windows is the only backend. Each function is the single Win32 call retail made, inline,
  with the same arguments: behaviour, return values, `GetLastError()` and every module's import
  table are unchanged. Retail compatibility is not affected.
- Types keep their Win32 size (`Handle`, `Module`, `CriticalSection`, `DateTime`, `FindData`),
  so structure layouts and the layout `static_assert`s hold.
- Constants carry the Win32 values. Interfaces retail defined with Win32 flags
  (`IFileSystem`) pass them through unchanged; a future backend translates them.
- A call that cannot be expressed exactly through the layer stays on Win32 rather than
  changing behaviour.

## What still calls Win32 directly

- Everything outside the five areas: windows and the message loop, input, Direct3D,
  DirectSound, DirectPlay, DirectShow, IME, the registry, message boxes, debug output, heap and
  virtual memory, string conversion, file mapping, `SearchPathA`, file version info.
- Calls the layer cannot express exactly: `ReadFile.dll`'s `CreateFileA` with the caller's
  security attributes and template handle, its `GetFileSize` with a high-dword pointer;
  `GetFileAttributesExA` in the client.
- Code that exists only on Windows: the crash-report hand-off in `freelancer.exe` and
  `FLServer.exe` (inheritable events and mutexes), the MFC server UI (`src/FLServer`),
  `src/MemoryDiagnostics`, `src/movie` (DirectShow plus its own copy of the VC6 runtime), and
  `src/imeui`.
- Header fields still spell `HANDLE`, `HMODULE` and `CRITICAL_SECTION`. They are the same types
  as the `Platform` ones; retyping them would pull `platform.h` into widely included headers.
