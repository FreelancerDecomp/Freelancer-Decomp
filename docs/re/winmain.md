# WinMain: client start-up order

How `freelancer.exe` comes up to the game loop and in what order it depends on the other modules. Source: retail exe `WinMain` (`FUN_005b2f50`, source file `E:\FL\Scratch\Source\Client\main.cpp`); our code: `src/freelancer/Client/main.cpp`, `engine_init.cpp`. The status of individual functions lives in decomp-tracker; exe gaps are in [freelancer.md](freelancer.md).

## Order

1. Mutex `FreelancerClient`; `SetCurrentDirectory` to the exe directory (the working directory set in VS has no effect after this).
2. Check for `..\DATA\CHARACTERS\ANIMATIONS\FACIALMALE.ANM`; if it is missing, retail starts `FLServer.exe` and exits.
3. EULA (`FUN_005b2d60`, `RunEulaGate`): the third-party `EBUEula.dll` is loaded from `bin\` as is and its `EBUEula` export is called; refusal → return code 1.
4. `CoInitialize`, `LoadLibrary(resources.dll)`, `DACOM_GetDllVersion`, spew log.
5. `LoadStartupConfig` (`FUN_005621a0`): `freelancer.ini`, the data path; then the display parameters are parsed (`ApplyDisplayCmdlineOverrides`) — **before** the window is created, as in retail.
6. `RegisterClass("Freelancer")`, `CreateGameWindow` (`FUN_004246e0`).
7. `InitializeEngine` (`FUN_005b1300`): `DALib::Startup` / `OpenData(data_path)` (loading `[Libraries]` from `DACOM.INI`), check that the mandatory components are present (`FDUMP` on failure), `Async::Startup`, `Timing::init`, `Pipeline::Init`, `PerfOptions.ini`, render device check, `DataConfig::startup`.
8. Game loop: `UpdateClientTiming` (`FUN_005b2360`) + `PumpMessages` (`FUN_005b0b60`).

## Command line

Parsed in `FUN_005b2cb0` (`ApplyClientCommandLineFlags`): `+G`/`-G`, `+P<port>`, `+S<host:port>`; the sign is ignored, and so is the case; a token without a sign means nothing. The display parameters (windowed / full-screen mode) are parsed by `ApplyDisplayCmdlineOverrides`. Retail `WinMain` has no other string checks of the command line; the quick start in SP goes through the `[Login]` auto-login in `freelancer.ini` (`FUN_005b20a0`).

The build output directory is `bin\` (next to `DATA` via `..\DATA`).
