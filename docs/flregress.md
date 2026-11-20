# FLRegress: the headless regression harness

`FLRegress.exe` (`src/FLRegress/`) is a port-only program with no retail counterpart. It
starts Server.dll as a multiplayer server, as `FLServer.exe` does. FLRegress itself takes the
place of `RemoteClient.dll` as the client module, so there is no window, renderer, sound or
network. It writes the game state to a text file. Two runs of the same build must give
identical files. A change that keeps retail behaviour must not change the file either.

It runs the multiplayer server because the single-player server cannot run without its
client. In single player the server takes each system's solars from the client sharing its
process (`StarSystem::load_solars`), so with no client a launch finds no solars and crashes.
The multiplayer server builds them from the system files itself.

```
FLRegress.exe [--out <file>] [--timeout <seconds>] [--frames <count>]
              [--crash-dir <dir>] [--crash-test]
```

Run it from `bin\`, next to the game DLLs, with the retail `DATA\` beside `bin\`. Exit codes:
0 on success, 1 on a startup failure, 2 on bad arguments, 3 when the watchdog ends the run
(`--timeout`, default 300 s), 4 on a crash.

**Crashes.** An unhandled exception, `abort()`, a pure virtual call or a CRT invalid parameter
writes `FLRegress-<pid>.dmp` (a minidump: thread stacks and modules, no heap) and
`FLRegress-<pid>.txt` to `--crash-dir` (default `crashes`, relative to the working directory).
The text report has the exception, the phase, the stack symbolised from the PDBs beside the
executable, and a `signature:` line: the exception code and the first symbolised frame outside
the CRT failure path. It has no offsets, so the same bug keeps its signature across builds.
`--crash-test` writes to a null pointer after startup, to exercise this path.

## What it checks

**Stage A: startup.** `IServerImpl::Startup` with FLRegress itself as the client module, then
the calls the server made to the client, and the universe as Common loaded it: systems, their
connections, objects and zones, then bases.

**Stage B: a scripted player.** FLRegress makes the IServer calls a multiplayer client makes
for a new account:

1. `OnConnect`, then `Login` as `FLRegress` with account creation.
2. `CharacterInfoReq`, then `CreateNewCharacter` named `FLRegress`. The character uses the
   first faction of the new-character database (`DATA\characters\newcharacter.ini`, in
   ascending nickname-id order) with its first base, package and pilot.
3. `CharacterSelect` of the new character.
4. `BaseEnter` of the character's base, 30 frames docked, `BaseExit`.
5. `RequestCreateShip`, `PlayerLaunch` with the reserved ship id, then `LaunchComplete` with
   the launch object the server named.
6. `--frames` more frames (default 900, 30 s of game time) in space.

The player's ship gets no input after the launch: a real client would send its movement. The
NPCs around it, their AI, physics and the encounters all run on the server.

The dump then lists:
- the script's progress (`player ...` lines): each step's answer from the server;
- every 30 frames, the object count of each class (`tick ...`);
- how often the server called each IClient slot during stage B (`client_calls ...`);
- every ship, solar, loot, mine, missile, countermeasure and beam (`objects ...`), in
  creation order, with id, archetype, owner, hit points, position, orientation and velocity.
  Asteroids and the id-less classes are counted only.

If a step gets no answer, the script stops there, and the frames and the dump still run. The
`player` lines then show how far it got, and stderr says the player did not reach space.

## Determinism

- **Fixed frames.** Every frame passes exactly 1/30 s to `Timing::UpdateGlobalTime` and
  `IServerImpl::ElapseTime`. No wall clock is involved.
- **Virtual clock.** The game also reads the wall clock itself. `ElapseTime`'s
  `Timing::Delta` drives `pub::GetTime`, the weapons-offline repair uses `Timing::read`,
  and `IServerImpl::Startup` seeds `rand` and makes the server's account id from
  `timeGetTime`. From `Server::Startup` on, `QueryPerformanceCounter`, `timeGetTime` and
  `GetTickCount` return a clock that starts at the same value every run and moves only by
  the frame step. FLRegress does this by rewriting the import tables of the game modules
  (the DLLs beside the executable) in its own process (`regress_hooks.cpp`). No game module
  changes.
- **Random numbers.** FLRegress also calls `srand(1)` before stage B. Every module uses the
  shared CRT, so they share that state.
- **User folder.** Accounts and saves normally live under `My Documents\My Games\Freelancer`.
  `SHGetFolderPathA(CSIDL_PERSONAL)` answers `bin\FLRegressUser` instead, which is emptied at
  every start. Each run therefore begins without accounts, and a run never touches a
  player's real accounts or saves.
- **Exact floats.** Floats are written as their bit pattern followed by a readable value.
  The same build on the same machine must match bit for bit. Any drift, however small, shows
  up as a diff, which is the point. A build with different floating-point code (another
  compiler, x64's SSE instead of x87) will differ in the low bits. Comparing those needs a
  tolerance on the readable values, not the bits.

## CI

The `regress` job in `.github/workflows/build.yml` runs FLRegress twice and requires identical
files. On a pull request it also runs the base commit's build on the same data and requires
the PR's file to match. A pull request that changes the result on purpose (a retail-fidelity
fix, or a change to FLRegress's own output) carries the label `regress: expected change`. The
job then reports the difference but does not fail. The job prints the script's progress and
the object counts. The dump itself holds values read from the game data, so it is never
uploaded.

When FLRegress crashes on a push to `master` or a manual run, the job files the crash report
as a GitHub issue labelled `flregress-crash`, titled with the signature's frame and an id
derived from the signature (`[crash:1a2b3c4d]`). If an open issue already has that id, the job
comments on it instead. The minidump stays on the runner, since its stacks can hold game data.
Pull requests only fail. A manual run with the `crash_test` input set passes `--crash-test`, to
test the issue path end to end.
