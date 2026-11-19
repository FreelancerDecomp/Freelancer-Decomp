# FLRegress: the headless regression harness

`FLRegress.exe` (`src/FLRegress/`) is a port-only program with no retail counterpart. It
starts Server.dll the way the single-player client does, with no window, renderer, sound or
network, and writes the game state to a text file. Two runs of the same build must give
identical files. A change that keeps retail behaviour must not change the file either.

```
FLRegress.exe [--out <file>] [--timeout <seconds>] [--frames <count>]
```

Run it from `bin\`, next to the game DLLs, with the retail `DATA\` beside `bin\`. Exit codes:
0 on success, 1 on a startup failure, 2 on bad arguments, 3 when the watchdog ends the run
(`--timeout`, default 300 s).

## What it checks

**Stage A: startup.** `IServerImpl::Startup` with FLRegress itself as the client module, then
the calls the server made to the client, and the universe as Common loaded it: systems, their
connections, objects and zones, then bases.

**Stage B: a scripted single-player game.** FLRegress makes the same IServer calls as the
single-player client starting a new game:

1. `OnConnect`, `Login` as `SinglePlayer`. The server makes `Restart.fl` from `NewPlayer.fl`.
2. `ReqDifficultyScale(1.0)`, `CharacterInfoReq`, `CharacterSelect` of `Restart.fl`.
3. `BaseEnter` of the character's base, 30 frames docked, `BaseExit`.
4. `RequestCreateShip`, `PlayerLaunch` with the reserved ship id, `LaunchComplete` with the
   launch object the server named.
5. `--frames` more frames (default 900, 30 s of game time) in space.

The player's ship sits where it launched. In single player the client flies the ship, and the
server has no input for it. The NPCs around it, the AI, physics, encounters and the story
mission's scripts all run on the server.

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
- **Virtual clock.** The game also reads the wall clock itself: `ElapseTime`'s
  `Timing::Delta` drives `pub::GetTime`, and the weapons-offline repair uses
  `Timing::read`. From stage B on, `QueryPerformanceCounter`, `timeGetTime` and
  `GetTickCount` return a clock that starts at the same value every run and moves only by
  the frame step. FLRegress does this by rewriting the import tables of the game modules
  (the DLLs beside the executable) in its own process (`regress_hooks.cpp`). No game module
  changes.
- **Random numbers.** `IServerImpl::Startup` seeds `rand` from the wall clock. FLRegress
  calls `srand(1)` before stage B. Every module uses the shared CRT, so they share that
  state.
- **User folder.** Accounts and saves normally live under `My Documents\My Games\Freelancer`.
  `SHGetFolderPathA(CSIDL_PERSONAL)` answers `bin\FLRegressUser` instead, which is emptied at
  every start. Each run therefore begins without saves, and a run never touches a player's
  real saves.
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
