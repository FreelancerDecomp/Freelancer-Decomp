# Server stack: Server.dll, rpclocal, RemoteServer/RemoteClient, FLServer

In Freelancer the client and the game logic talk through two abstract interfaces: `IServer` (client →
server, 92 methods + `PushToServer`) and `IClient` (server → client, 126 slots). Single player runs the
server in the same process; the network game plays the same calls over DirectPlay 8. This page describes the five
modules that make this work and the way they are connected; the packet protocol is covered in
[rpclocal.md](rpclocal.md). It is needed by anyone working on the single-player campaign, the network mode or
the dedicated server. Function statuses, slots, argument layouts and addresses live in decomp-tracker and in the
`src/include/` headers; open divergences from retail are in [deviations.md](deviations.md).

| Module | Sources | Role |
|--------|---------|------|
| `Server.dll` | `src/Server/` | Game logic: players, ships, bases, missions, `pub::*` for Content |
| `rpclocal.dll` | `src/rpclocal/` | SP local transport: the `IServer` proxy for the client and the `IClient` proxy for the server, packets in queues |
| `RemoteServer.dll` | `src/RemoteServer/` | Loaded by the **client**: `IServer` over DirectPlay (from the client's point of view, the "remote server") |
| `RemoteClient.dll` | `src/RemoteClient/` | Loaded by the **server**: `IClient`/`IChatClient` over DirectPlay (remote clients) |
| `FLServer.exe` | `src/FLServer/` | MFC shell of the dedicated server |

The DLL names are misleading: `RemoteServer` sits on the client side, `RemoteClient` on the server side. Both are
built from one code base (`Network\FLServerPacketHandler.cpp`) with complementary sets of `#define`s; the shared
codecs live in `src/RemoteCommon/` and are compiled into both DLLs as separate units. They are not DACOM components
(they are not registered in `COManager`) but ordinary DLLs with exports.

## Server.dll

Exports `GetServerInterface()` (the global `IServerImpl`), `GetChatServerInterface()` and about
270 named symbols (`SERVER_API`, defined by the `SERVER_EXPORTS` macro; there is no `.def` file).
The surface is checked against `dumpbin -exports EXE\Server.dll`. Content.dll imports on the order of two
hundred `pub::` namespace symbols from Server (object creation, players, market, sound, AI, controllers, phantoms,
player groups) — this is the contract between the game logic and the mission scripts.

Game objects and databases (`PlayerDB`, `CObject`/`CEqObj`/`CShip`, `DataConfig`, `Reputation`, timers)
live in `Common.dll`, and Server imports them; the port's facades are `src/include/server_*.h`, `common_server*.h`.
Details are in [common.md](common.md).

`IServerImpl::Startup(SStartupInfo)` initialises timing, `PlayerDB` (maximum players, SP flag),
`DataConfig::startup` (reads `freelancer.ini`, the `[Initial SP DLLs]` section, and loads the Content modules:
GameSupport, SpaceSupport, BaseSupport and others) and calls slot 10 of `IClient` (in rpclocal it loads the exe and fetches the real `IClient`; it must return `true`).
Significant fields of `SStartupInfo` (size `0x214`): `+0x000` module path (`0x104` bytes), `+0x208` maximum
players, `+0x210` `bAllowPvp`, `+0x211` `bAllowNewPlayers`; the server uses them for damage between players and
account creation. `Update` in Server is only a heartbeat; physics is pushed by `ElapseTime`.

SP initial state: `Restart.fl` is assembled from the `NewPlayer.fl` template next to the exe and the `initialworld.ini`
of the current `DATA`, and is then reused (changing the DATA set does not recreate it). Jump-hole locks
are stored in `[locked_gates]`/`[mPlayer]` and are tied to a particular set of missions. The indestructibility of static
objects is guarded by their `SUBOBJ_ID_ROOT` hull, which comes from `CEqObj::inst_to_subobj_id`.

## Single player: how the client connects to the server

```
freelancer.exe  (server_connect.cpp)
  LoadLibrary("rpclocal.dll")  -> GetServerInterface() -> IServerImpl (rpclocal)
  IServer::Startup(SStartupInfo)                     [rpclocal]
    remembers the exe path from SStartupInfo (+0x000)
    LoadLibrary("Server.dll") -> GetServerInterface / GetChatServerInterface
    copies 0x214 bytes of SStartupInfo, REPLACES the first 0x104 bytes with the path of rpclocal.dll
    Server::Startup(copy)                            [Server.dll]
      Server does LoadLibrary(ModulePath) = rpclocal.dll,
      GetProcAddress("GetClientInterface") -> rpclocal IClient (126 slots)
      IClient slot 10: LoadLibrary(exe path) -> GetProcAddress("GetClientInterface")
                       -> the client's real IClient
    creates the packet serialiser and registers the types
  IServer::Login / CharacterSelect / ... -> channel 3 packets -> Server.dll
  every frame: IServer::Update, ElapseTime
```

Consequences: (1) both the exe and rpclocal export `GetClientInterface`, and this is not a contradiction; (2) `ModulePath`
sits at offset `+0x000`; (3) the retail `rpclocal` from `EXE\` must not be mixed with our `Server` — they have to
be loaded from the same directory (`bin\`), and a name that is not found must fail loudly, with no fallback to `EXE\`;
(4) if slot 10 of `IClient` does not return `true`, the connection does not happen.

Each frame `IServerImpl::Update` (rpclocal): `GiveServerRunPermission` → drain the server's packet queue to the client
(`HandlePacket` calls the slots of the real `IClient`) → `WaitForClientRunPermission` →
update the client proxy and drain the client's queue to the server. `ElapseTime` is a direct tail jump into
`Server::ElapseTime`. These permissions (`Give…`/`Wait…RunPermission`) are ordinary imports from `Common.dll`.

## rpclocal and the protocol

Details are in [rpclocal.md](rpclocal.md). In short: rpclocal is not a thin wrapper but a bidirectional RPC proxy.
Almost every `IServerImpl` method serialises the call into a `CDAPacket` (channel 1 or 3) and puts a pointer into a queue;
the rpclocal `IClient` slots do the same in channel 2. Inside the process the packets are not turned into bytes: the queue
holds pointers; the codecs are needed by the network transport.

## Network mode: RemoteServer and RemoteClient

**Client → server.** `RemoteServer.dll` exports `Server` (a derived `IServerImpl` with 93+1 slots).
An outgoing call (`FireWeapon` and the like) picks the packet type in the serialiser (channel/opcode), fills in the fields and
sends it through `CDPClient::Send` (DALib, DirectPlay 8). The per-frame `Update`: `GiveServerRunPermission` →
`CDPClient::DispatchMsgs(100)` → `WaitForClientRunPermission`; the received buffer carries a two-byte
header (opcode, then channel), the receiver creates a packet of the required type, decodes it and calls
`HandlePacket`, which frees the packet and invokes a slot of `Client` (the `IClient` from `freelancer.exe`).
`CDPClient` (`0x90` bytes) is the client DP8 session: two message queues (incoming and the frame's working queue),
the application GUID, a list of connection attempts, and the `OnReceive`/`OnConnect`/`OnDisconnect` handlers.

**Server → client.** `RemoteClient.dll` exports `Client` and the administrative API. `Startup` creates
`CDPServer`, starts hosting, and registers the COMMON, SERVER, CLIENT types (each next stage only if the
previous one succeeded). A connection takes the first free slot (player identifiers 1–128 are not the DirectPlay
`PlayerId`), sends `CONNECTRESPONSE` and notifies the local `Server.dll`. An incoming packet is
decoded and its fields turn into calls to `Server`; an allocation or decoding error calls `Disconnect`.
An outgoing packet is encoded into a `0x2800` buffer (`0x19000` on failure) and goes out through `CDPClientProxy::Send`.

General rules of network packets: the wire format is defined only by the field codecs (the VC6 container layout,
structure padding and pointers do not reach the network; our containers are modern STL with per-element
serialisation). Retail quirks that are kept on purpose: the length of the second block of the dialog
(`FmtStr::flatten`) is declared but copied with the length of the first; when a deal is accepted, the identifier
is written into the incoming buffer rather than read; the capacity of the character name differs (`RemoteClient` — 24
characters, `RemoteServer` — 64), so the decoder of each DLL has its own copy order; in the receive loop of
`RemoteClient` the pointer advances only by the header. Buffer COMMON packets distinguish borrowing,
transfer of ownership and copying; the length and the content go on the wire.

`SetRemoteClientResponseData` builds the host discovery response: an ASCII prefix with the PvP and new-player-admission flags,
the version, the server ID and signature, and a UTF-16 description.

## FLServer.exe

An MFC application (in the port — dynamic MFC 14, multibyte), one document-oriented window:
a console (`CFLServerView`) and a summary form (`CFLSummaryForm`), switched by `SwitchView`.
`InitInstance` loads `ServerResources.dll` (`src/Resources/serverresources`) as the resource handle: all
menus and dialogs are taken from there.

- **Export.** Retail exports 71 names: 70 are its own copies of members of classes from shared headers
  (`CAccount`, `PlayerDB`, `IServerImpl`, `CDPServer`, `CGunWrapper`, `CDeadReckonedVector`, …), byte-for-byte
  identical to the copies in the owner DLLs, plus `ServerLogf`. The port reproduces the surface through
  `src/FLServer/FLServer.def`: 70 forwarders to the exports of the owner DLLs (ordinals as in retail)
  and one of its own; the `.def` trims nothing here, it only forwards.
- **Threads.** The start-up thread (runs `IServerImpl::Startup`, the result is collected by `PollStartupThread`); the server
  thread (loads `motd.dat`, runs the `Update` / `ElapseTime` loop under `GServerMutex` with the run-permission handshake and writes the
  frame time / `ServerStats` into the document); the "gun" thread (once a second drives `CGunWrapper` — list server/
  matchmaking; only if publishing is enabled); the shutdown thread (`IServerImpl::Shutdown` while a modal wait
  dialog is shown). `LockServer`/`UnlockServer` serialise the UI thread's access to the Server.dll data.
- **Config.** The settings dialog stores `FLServer.cfg` in the user directory (binary, no padding):
  the version (3 is written), server name (UTF-16), description, password, "Allow New Players", "Internet
  Accessible", maximum players 1..128, "Allow Players to Harm Other Players" (since version 3). The `/C` command-line
  switch accepts the saved settings without showing the dialog.
- **Log.** `ServerLogf(ErrorCode, fmt, …)` filters by level (low nibble) and category, writes to
  `OutputDebugString`, the console, an optional file and (levels 0/1) to `c:\fl-server-errors.log`; it is installed as the
  DACOM `FDUMP` hook for the lifetime of the console and passed to `RemoteClient` through `SetServerLogFunction`.
- **Crash report.** An unhandled exception closes the DP session and launches `dw15.exe` with a shared block (like
  the client filter in `freelancer.exe`; two copies, no shared library).
- **Chat.** The chat windows (account / system / the whole universe) send `IServerImpl::SubmitChat` with a two-node
  rich-text list in `BinaryRDLWriter`; the receive hooks are installed at initialisation.
- **Code that retail builds but does not call** is not ported or is carried over as "class only":
  the "Server Settings" dialog (130), the second `motd.dat` loader, two `CFLServerApp` functions; `CCreateAccountDlg`
  and `CServerInfoDlg` exist as classes with no constructors in the call graph.
- Header split: `server_iserver.h` (`IServer`, `IChatServer`, chat hooks) and
  `server_iserverimpl.h` (the `IServerImpl` of Server.dll) are separated so that MFC code does not pull in Freelancer's `CObject`
  and the DX8 headers; `common_server.h` must not include `server_iserverimpl.h` (rpclocal declares its own
  `IServerImpl`).

## Known limitations

- `IServer` is exactly 92 slots (`+0x000..+0x16c`); the 93rd slot `PushToServer` (`+0x170`) belongs to each
  `IServerImpl`, not to the interface. Argument layouts (`XFireWeaponInfo`, `SSPObjUpdateInfo`, …) and signatures are in
  `common_server.h`, `common_server_types.h` and the `IServer` card in the tracker.
- Many `pub::SpaceObj`/`pub::Player` functions are simplified (object creation without the full loadout pipeline,
  AI formations, physics impulses via messages instead of direct IVP calls, and so on) — per function this is
  `simplified` in the tracker, architecturally it is the Server.dll section in deviations.
- Of the 162 packet types, the payload layout has not been recovered for all (see [rpclocal.md](rpclocal.md)).
- The SERVER-channel network paths need a "sender — codec — receiver" cross-check for each type; the presence of a
  class in the registry does not by itself prove a match with retail.
- The `IServer` game logic stays in `Server.dll`; it must not be replaced by loading `RemoteServer.dll` or by the
  local SP transport.
