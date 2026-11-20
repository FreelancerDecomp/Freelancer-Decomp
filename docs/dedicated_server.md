# FLDedicated: console dedicated server

`FLDedicated.exe` (`src/FLDedicated/`) is a port-only program with no retail counterpart. It runs
the multiplayer server without a window: no MFC, no dialogs, no message boxes. It is meant for
headless machines and services, and it is the first step towards a server that builds on other
platforms. The plan is to reuse its `main` once the server modules compile there.

It changes no retail module. It loads the same DLLs as `FLServer.exe` (Server.dll, with
RemoteClient.dll as the client module, DirectPlay 8 for the network). It also follows
FLServer's startup, 30 Hz frame loop and shutdown in the same order, so retail clients see the
same server.

## Running

From `bin\`, next to the game DLLs:

```
FLDedicated.exe --name "My server" --max-players 16 --pvp
```

| Option | FLServer equivalent | Meaning |
|--------|---------------------|---------|
| `--name <text>` | Server Configuration dialog | Server name, up to 32 characters. Required unless `FLServer.cfg` has one. |
| `--description <text>` | dialog | Up to 128 characters. |
| `--password <text>` | dialog | Up to 16 characters. |
| `--max-players <n>` | dialog | 1..128 (default 16). |
| `--pvp` / `--no-pvp` | dialog | Allow players to harm other players (default off). |
| `--new-players` / `--no-new-players` | dialog | Allow new players (default on). |
| `--save-config` | dialog's OK | Write the resulting settings to `FLServer.cfg`. |
| `--port <n>` | `/P` | DirectPlay host port; 0 (the default) lets DirectPlay choose. |
| `--log <file>` | `/L` | Also write the log to a file. |
| `--log-level <1..7>` | `/S` | Most verbose level shown (default 4). |
| `--log-category <n>` | `/E` | Also show this log category. |
| `--unmask-fpu` | `/X` | Unmask FPU zero-divide/overflow/invalid exceptions. |
| `--run-for <seconds>` | none | Stop after this many seconds (used by CI). |

Settings not on the command line come from `FLServer.cfg` in the server's user directory: the
file FLServer's Server Configuration dialog saves. A machine set up with FLServer therefore runs
the same server. FLDedicated writes the file only with `--save-config`. FLServer writes it every
time you press OK.

While it runs, it reads commands from the console. `status` prints players, connections and the
last frame time. `quit` stops the server. Ctrl+C and closing the console also stop it cleanly:
the players are saved through `IServerImpl::Shutdown` as in FLServer.

Exit codes: 0 after a clean stop, 1 when startup fails, 2 for bad arguments or settings.

## What differs from FLServer

- No EULA prompt, no crash reporter (`dw15.exe`), no `ServerResources.dll`.
- The log goes to stdout (and `--log`), with FLServer's level and category filter. Lines of
  level 0/1 are not copied to `c:\fl-server-errors.log`.
- DALib's critical warnings are logged, and the server carries on as if the user had answered
  Yes. FLServer asks, and exits on No.
- No list-server reporting (the "gun" thread and `gundll.dll`). The "Internet accessible"
  setting is kept in `FLServer.cfg` but has no effect.
- No bandwidth log (`/B`), and none of the UI's admin tools (accounts, chat, server news). The
  message of the day is still read from `motd.dat`.

## CI

The `dedicated` job in `.github/workflows/build.yml` starts FLDedicated on the retail game
data, lets it run for 20 seconds and stops it. It checks the exit code and the startup and
shutdown lines. Windows Server does not ship DirectPlay enabled, so the job turns it on first.
