# Tests

The port has no unit-test framework. Its checks are tools that write a deterministic text file.
CI runs each tool twice and requires the two files to be identical. On a pull request, it then
compares the file with the one the PR's base commit produces. A change that keeps retail
behaviour must leave the file unchanged. A change that alters it on purpose carries the tool's
"expected change" label: the difference is then reported but does not fail the job.

| Tool | Checks | Label |
|------|--------|-------|
| `FLRegress.exe` (`src/FLRegress/`) | Game state after the server starts on the retail data | `regress: expected change` |
| `RpcCodecTest.exe` (`src/RpcCodecTest/`) | The network packet codecs: wire bytes and result codes | `codecs: expected change` |

Layout `static_assert`s (struct sizes and field offsets against retail) and the FLDedicated
start-and-stop job ([dedicated_server.md](dedicated_server.md)) complete the checks.

## RpcCodecTest

`RpcCodecTest.exe` compiles the packet codecs of `RemoteClient.dll`, the server's side of the
network: `src/RemoteClient/rpc_client_packets.cpp` and `src/RemoteCommon/`. It uses
RemoteClient's own defines, so the codecs compile as they do in the DLL. It registers every packet
type the way RemoteClient's `Startup` does (COMMON, SERVER, then CLIENT). No retail module
changes: the only edit outside the test is an inline read-only accessor,
`FPacketSerializer::GetTypes`.

For each packet type it records:

- **fresh**: a newly constructed packet, encoded as is.
- **Fixed inputs** (`empty`, `zeros`, `small`, `ascending`, `ones`, four seeded pseudo-random
  buffers): Decode, then Encode the decoded packet. The line holds Decode's result and the bytes
  it consumed, and Encode's result, length and bytes. It also holds two flags:
  - `rt`: whether the encoded payload equals the bytes Decode consumed.
  - `stable`: whether decoding and re-encoding the output gives the same bytes again.
- **prefixes**: Decode's result on every prefix (0..48 bytes) of the `small` input. This shows
  where each codec stops and which error it reports for a short packet.

The `small` input repeats the little-endian DWORDs 1..5, so count and length fields read small
values and variable-length bodies decode. The file is written twice: once as a multiplayer server
and once in single-player mode, since FIREWEAPON and PLAYER_TRADE depend on those flags.

`rt=diff` and `stable=no` are not failures. Retail has codecs that do not round-trip (see
[re/server.md](re/server.md)); the file records today's behaviour, and the base comparison
catches any change to it. The flags help when reviewing a codec. The test is mainly for the x64
port: an x64 build must produce the same file as the Win32 build.

**Determinism.** Packet constructors leave some fields unset, as retail does, and a failed
Decode leaves others unset. The test therefore replaces `operator new` with a zero-filling
allocator that refuses requests above 64 MiB. Packets are never destroyed, because a destructor
would free pointers that a failed Decode never set. A codec that faults is recorded as
`exception:<code>` and the test carries on.

Run it from `bin\`:

```
RpcCodecTest.exe [--out rpc_codecs.txt]
```

The output holds no game data, so CI uploads it as the `rpc-codecs` artifact.

Not covered yet: `RemoteServer.dll`'s copy of the codecs (the client side), and the codecs'
callers (packet dispatch and the DirectPlay transport).
