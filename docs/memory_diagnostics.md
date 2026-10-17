# Memory leak hunting (`MemoryDiagnostics`)

The `MemoryDiagnostics` project in `src/Freelancer.sln` builds a small Win32 utility,
`MemoryDiagnostics.exe`, that drives UMDH (User-Mode Dump Heap) to find leaks in the game's
Windows heap. The game code itself is not instrumented.

## Requirements

**Debugging Tools for Windows** from the Windows SDK. The utility looks for a `Debuggers/x86`
folder containing `gflags.exe` and `umdh.exe`; you can point it at one manually.

## Workflow

1. Pick the normal Debug client in `bin`, the log folder and, if needed, the symbol path.
2. Click **Enable stack tracing**, then **Launch game**. Windows asks for elevation only for
   GFlags. The flag is tied to the EXE name and applies to every new process with that name; a
   game that is already running must be restarted.
3. After the scenario has warmed up, click **Take snapshot**. Repeat the action, return to the
   starting state and take another snapshot.
4. Select two snapshots with Ctrl/Shift and click **Compare pair**; the text report opens
   automatically. A stack that keeps growing across several comparisons is a candidate leak —
   check ownership and release of those objects.
5. When finished, click **Disable stack tracing**. Closing the utility neither clears the saved
   flag nor closes the game.

Snapshots are separated by process ID and process creation time. **Attach to game** finds an
already running process by its full path; with several instances, give the PID. Attaching this
way does not know which tracing mode the process was started with.

## Details

- Logs go to `D:/Temp/FreelancerLeaks` by default. Symbols come from the configured
  `_NT_SYMBOL_PATH` (default cache `J:/pdb_cache`). The field accepts WinDbg syntax
  (`cache*`, `srv*`, folders separated by `;`). PDBs next to the EXE are picked up automatically
  and must match the running build. The **Windows symbols** checkbox can be cleared to compare
  against local PDBs and already cached symbols without downloading.
- The utility launches the game with `OANOCACHE=1` to disable the BSTR cache.
- UMDH output and errors are written next to the snapshot as `.tool.txt`; unfinished snapshots
  keep the `.pending` suffix and are ignored by comparison.
- UMDH analyses the Windows heap only. Private pools, `VirtualAlloc` and video memory need other
  tools. `DebugASan` is good for memory corruption, but its own memory retention distorts leak
  measurements.

See the [UMDH guide](https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/using-umdh-to-find-a-user-mode-memory-leak).
