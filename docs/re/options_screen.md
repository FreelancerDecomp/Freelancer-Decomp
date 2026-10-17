# Options screen: files and storage

The options window in retail is `NN_Preferences : WinControl` (one vtable `0x005d4a5c`; the port is
`src/freelancer/Client/nn_preferences.{h,cpp}`). This document describes only the data the window reads
and writes, and the boundaries with neighbouring tables. The window's own structure, slots and function
status are in the comments of `nn_preferences.h` and in decomp-tracker.

## Files

| File | Contents | Port code |
|---|---|---|
| `<user>\PerfOptions.ini` | floating-point performance/sound parameters, optional `[Display]` section | `LoadPerfOptions` / `SavePerfOptions` (`perf_options.cpp`); the window saves through `SavePerfOptions("PerfOptions.ini")` |
| `DATA\INTERFACE\optlist.ini` | list of rows for the three options pages (by IDS); seeds the defaults on first run | `NnPreferences` (`kOptListPath`), seeding — `NnPreferences::SeedGraphicsDefaults` |
| `DATA\INTERFACE\keylist.ini` | groups and key list of the Controls page (`USER_*`) | `NnPreferences` (`kKeyListPath`) |
| `DATA\INTERFACE\KeyMap.ini` | base `KeyCmd` entries and groups | `LoadKeyMapIni` (`menu_keymap.cpp`) |
| `<user>\UserKeyMap.ini` | user overrides | `LoadUserKeyMapIni`, `SaveUserKeyMapIni`, path — `EnsureUserKeyMapPath` |

Live editing of a binding on the Controls page is `UpdateKeyCmdBinding` over the `KeyCmd` table
owned by `menu_keymap.cpp`.

## What does not belong to the options screen

- The graphics preset (`default_graphics`) is chosen on first run by the hardware probe
  `ProbeMachineGraphicsLevel` (`perf_options.cpp`; the estimate is the minimum of CPU and GPU via
  `QueryDeviceAbility` and `Timing::GetMachineSpeed`). Divergences from retail in `PerfOptions` are in
  `deviations.md`.
- `game_options` (`GetGameOption`, retail `DAT_00679be8`) is a separate table of debug switches. The
  menu that edited it is cut from the shipped build, so retail does not write to it after
  initialization, and the options window does not touch it. Details are in `game_options.h` and
  [freelancer.md](freelancer.md).
