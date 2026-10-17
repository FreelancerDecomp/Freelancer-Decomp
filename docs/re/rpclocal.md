# rpclocal: the `IServer` / `IClient` transport and `CDAPacket` packets

`rpclocal.dll` is the local RPC proxy between `freelancer.exe` (the client half) and `Server.dll`
(the game logic) in single-player. It also defines the packet model that the network
`RemoteServer.dll` / `RemoteClient.dll` follow. This page is for whoever adds or edits a packet, a dispatcher
or an `IClient`/`IServer` slot. For the overall picture of the modules and the connection sequence see
[server.md](server.md). Function statuses, slots and the payload layouts of individual packets live in
decomp-tracker, the headers and the code: the registry of all types is `src/rpclocal/rpc_packet_registry.cpp`, payloads are in
`rpc_payloads.h`, `rpc_payload_ch1/2/3.cpp`, codec serializers are in `rpc_wire_*.cpp`, containers are in
`rpc_containers.{h,cpp}`. Open divergences from retail are in [deviations.md](deviations.md).

## What it is

Retail `rpclocal.dll` is not a thin wrapper around `Server.dll` but a bidirectional proxy:

- `freelancer.exe` gets an `IServer`; rpclocal's `IServerImpl` **serializes the call into a
  packet** in almost every method and puts it on the client -> server queue (`PushToServer`, vtable `+0x170`). The exceptions, which
  go straight into `Server.dll`, are `Startup`, `Shutdown`, `ElapseTime`, `Connect`, `DisConnect`.
- `Server.dll` gets an `IClient`, rpclocal's own object (`GetClientInterface`, 126 slots), and each
  slot puts a packet on the server -> client queue; slot 9 (`+0x24`) is `PushToClient`.
- `IServerImpl::Update` drains the server -> client queue and `HandlePacket` calls a slot of the **real**
  `IClient` from the exe (and `IChatClient` for channel 1 opcode 5); draining of the second queue goes through
  `RpcDispatchToServer` (retail `FUN_06d86a70`), which calls slots of the real `IServer` from `Server.dll`.
- Stubs in retail itself: `GetServerID` (always 0), `GetServerSig` (empty string), `GetServerStats` /
  `OnConnect` / `SetActiveConnection` (no-op), `SwapConnections` (always `true`).
- `rpclocal.dll` exports: `GetClientInterface`, `GetChatClientInterface` (the second base class of the same
  object, one slot), `GetServerInterface`, `GetChatServerInterface`.

## The `CDAPacket` packet

The packet base class is exactly `0x20` bytes (`FPacketBase`, `src/include/rpc_packet.h`), followed by the fields of
the concrete type:

| Offset | Contents |
|----------|-----------|
| `+0x00` | `vptr`, 4 slots: destructor, Encode (codec A), Decode (codec B), dump |
| `+0x04` | use counter (the constructor sets 1) |
| `+0x08` | reserved for the queue link |
| `+0x0C` | **`PacketId` = `Channel << 16 \| Opcode`**; the dispatchers read exactly this |
| `+0x20` | start of the type-specific payload |

The total object size differs per type (the argument of its `operator new`, from `0x24` to `0xEC`). In SP packets are not
turned into bytes: the queue stores **pointers** (`FPacketQueue`/`FRpcConnection`), no copies are made;
the Encode/Decode slots are needed by the network transport. Destruction is slot 0 (`scalar deleting destructor`).

`Channel = PacketId >> 16`, `Opcode = PacketId & 0xFFF`; the dispatcher drops an unknown channel.

| Channel | Direction | Types | Opcode | Handled by |
|-------|-------------|------:|--------|---------------|
| 1 | both directions ("fast": physics/combat/trade/state buffers) | 21 | 1..0x1D | client: `HandlePacket`; server: `RpcDispatchToServer` |
| 2 | server -> client (major game events) | 81 | 1..0x5B | `HandlePacket` |
| 3 | client -> server (player requests) | 60 | 1..0x47 | `RpcDispatchToServer` |

There are **162** `FLPACKET_*` types in total; they are registered by three registrars (`FUN_06d9ace0` is channel 1,
`FUN_06d9bc80` is channel 2, `FUN_06d9f100` is channel 3), and the registry is kept in `FPacketSerializer`
(`RegisterType(Id, Name, Factory)`), created at `Startup` and shared by all instances in the DLL.
The "opcode -> `IClient`/`IServer` slot" table is read from the dispatchers and not stored separately: in
`rpc_dispatch.cpp` and `rpc_payload_ch1/2/3.cpp` each case is the Deliver column of the corresponding type's codec.

## Delivery

- **Queues.** `CDAConnection` (`FRpcConnection`) holds three pointer queues under one critical
  section; `Connect` creates it and starts it (`PushToServer` without a connection silently drops the packet).
- **Synchronous or queued.** The decision is made by type (the codec's `bSelfContained`): self-contained
  packets (they own all nodes, records and strings) go to the queue, as in retail; the "buffer family"
  of channel 1 and `POP_UP_DIALOG` carry a caller-side pointer in the payload (retail does this itself) and
  are delivered synchronously; for types whose payload layout is not recovered the port stores raw dword
  arguments, which is self-consistent inside one process but is not the retail format.
- **Containers.** Lists (equipment, collision groups, names, missions, etc.) are serialized by
  `rpc_containers`: the packet owns the nodes, the records and the strings. In several places a type cast
  (`std::string`/`std::vector`) remains across the `Common.dll` boundary; their list is kept in deviations.
- **Threading model.** Run permission (`GiveServerRunPermission` / `WaitForClientRunPermission`) are ordinary
  `Common.dll` imports; the order of the `Update` steps is described in [server.md](server.md).

## What is still missing

- The payload layout per packet type is kept in `rpc_payload_ch1/2/3.cpp` and in the tracker; where it is
  not recovered, the payload holds raw dword arguments (see "Delivery"). The only channel 3 type with no sender and no handler
  in retail itself is `GFREQUESTSHIPINFO` (0x0A).
- The unpackers `HandleCharacterInfo` and `HandleDamageObject` (channel 2, op `0x03` / `0x05`) are called in retail
  bypassing `HandlePacket`.
- The numeric output of `RpcDumpServerLaunch` / `RpcDumpServerCharSelectVerified` differs from MSVCP60 in the `float`/`double`
  format (see [crt_compat.md](crt_compat.md)); binary serialization is not affected.
