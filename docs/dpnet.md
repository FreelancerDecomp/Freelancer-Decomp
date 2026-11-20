# DPNet: the DirectPlay 8 protocol

`src/DPNet/` is a port-only implementation of the network protocol of Microsoft DirectPlay 8,
which Freelancer uses for multiplayer. It is the step that decides whether a server can run
without Windows. It also removes the dependency on a Windows component that is deprecated,
switched off by default on Windows Server, and of uncertain availability on Windows on ARM.

The goal is wire compatibility with the real thing. A server built on DPNet must accept retail
Freelancer clients that run Microsoft's `dpnet.dll`, and their packets must not tell the
difference. The Win32 build keeps using Microsoft's DirectPlay; DPNet is only for the port's
own builds, so it changes no retail module.

## Sources

Microsoft published the protocol as three Open Specifications. Section numbers in the code
refer to them:

| Document | Covers |
|----------|--------|
| [MC-DPL8R] DirectPlay 8 Protocol: Reliable | Connections over UDP: the CONNECT/CONNECTED handshake, sequenced data frames, acknowledgements and SACK masks, retries, coalescing, large messages, KeepAlives, graceful and hard disconnects |
| [MC-DPLHP] DirectPlay 8 Protocol: Host and Port Enumeration | EnumQuery / EnumResponse, which feed the in-game server list |
| [MC-DPL8CS] DirectPlay 8 Protocol: Core and Service Providers | The session on top: the player connect message, `DN_SEND_CONNECT_INFO`, refusals, `DN_TERMINATE_SESSION`, the name table, DPNIDs |

The specifications are at
<https://learn.microsoft.com/en-us/openspecs/windows_protocols/mc-dpl8r/>,
`.../mc-dplhp/` and `.../mc-dpl8cs/`.

## Layout

| File | Contents |
|------|----------|
| `dpnet_wire.h/.cpp` | Encoders and decoders for every frame and message. Everything is little-endian. |
| `dpnet_link.h/.cpp` | `FLink`, one [MC-DPL8R] connection. |
| `dpnet_session.h/.cpp` | `FServer` (what `IDirectPlay8Server` does for FLServer), `FClient` (what `IDirectPlay8Client` does for the game), `FAddress`, `ITransport`. |
| `tests/dpnet_tests.cpp` | The tests (below). |
| `CMakeLists.txt` | Standalone build of the library and its tests. |

The code is portable C++20 with no operating-system headers. It has no sockets, clocks or
threads of its own. The owner feeds in each received datagram with its sender's address, calls
`Tick` every 10–20 ms with a millisecond clock, and sends what comes out through `ITransport`.
That keeps the protocol deterministic and testable. The owner (the coming IDirectPlay8 adapter)
supplies the UDP socket, the clock and the thread.

## What is implemented

- **Reliable layer:** handshake with retries and the lost-CONNECTED recovery, sequencing over
  the 8-bit space with a 64-frame window, cumulative and SACK acknowledgements, delayed
  acknowledgements (100 ms, 20 ms for out-of-order frames, at once on POLL), retries with the
  recommended backoff (cap 5 s, 10 retries), send masks for dropped unreliable frames,
  coalescing (receiving always; sending for `DPNSEND_COALESCE` messages, which wait for the
  next `Tick` to share a frame), large messages split and reassembled, KeepAlives (25 s),
  END_STREAM graceful close, HARD_DISCONNECT, and partners older than protocol 1.5 (no
  coalescing, KeepAlives without payload).
- **Enumeration:** EnumQuery with or without an application GUID, EnumResponse with the
  session description and the game's reply data.
- **Session, client/server mode:** the player connect message and its DirectPlay 9 `_EX` form,
  validation in DirectPlay's order (interface, instance, application, password, player limit),
  the application's indicate/refuse decision with reply data, `DN_SEND_CONNECT_INFO` with the
  two name-table entries, `DN_ACK_CONNECT_INFO`, `DN_CONNECT_FAILED`, `DN_TERMINATE_SESSION`
  (DestroyClient and session close), process-completion requests, and the events DALib's
  `CDPServer` / `CDPClient` handle: indicate connect, connect aborted, create and destroy player
  (with the DirectPlay reasons), receive, send complete. Refusals carry the `dplay8.h` codes;
  Freelancer's client shows "server full" for `DPNERR_SESSIONFULL` (`0x80158610`) and
  "wrong password" for `DPNERR_INVALIDPASSWORD`.
- **Join limits:** a connection that has not become a player within 30 s is dropped, and at
  most 64 may be joining at once (`FServer::SetJoinLimits`), so half-open connections cannot
  hold the player slots.

## Left out

- Peer-to-peer mode, groups and host migration: Freelancer is client/server only.
- Packet signing ([MC-DPL8R] 1.6): only used when a session asks for it, and Freelancer's
  don't. A signed CONNECTED is ignored.
- DirectPlay Voice (`PACKET_COMMAND_USER_2` messages are dropped), the NAT locator ([MC-DPLNAT])
  and the DPNSVR forwarder on port 6073.
- Send priorities (`DPNSEND_PRIORITY_*` are accepted, and everything is sent in order).
  `DPNSEND_COMPLETEONPROCESS` is sent as guaranteed and completes on acknowledgement. DALib
  uses neither: it sends everything guaranteed, sequential and coalescable
  (`DPNSEND_GUARANTEED | DPNSEND_COALESCE`, flags `0x108`).

## Choices the specifications leave open

These are where DPNet may differ from Microsoft's implementation. Each one is to be checked
against captures of a real FLServer and client (see Next steps):

- **Name-table entry flags** in a client/server `DN_SEND_CONNECT_INFO`: the server's entry is
  `SERVER | HOST`, the client's `CLIENT`. The specification only shows a peer-to-peer example.
  Name-table indices: the all-players group is 1, the server's player 2, clients from 3. Each
  join and leave bumps the name-table version, which is part of the DPNID ([MC-DPL8CS] 2.2.7).
- **DirectPlay version** sent in name-table entries: 8 (DirectX 9).
- **Field order** of the variable data in an EnumResponse (front to back: session name,
  application reserved data, reply). `DN_SEND_CONNECT_INFO` is packed from the end backwards,
  exactly as in the specification's example, which the tests reproduce byte for byte.
- **Frame size:** at most 1,472 bytes per datagram (Ethernet MTU minus IP and UDP headers).
- **When to set POLL:** when the window fills, on resends, KeepAlives and END_STREAM.
  Otherwise the partner's delayed acknowledgement (100 ms), or one riding on its own traffic,
  answers. Setting it on every frame would make the partner send a dedicated SACK for each.
- **Congestion window:** starts at 2 frames, grows by one per acknowledgement, halves on a retry.
- **Round-trip time:** sampled only from the newest frame an acknowledgement covers, and only
  when none of the frames it covers was resent or already SACKed (otherwise the sample measures
  a lost acknowledgement, not the network).
- **Early retry on a SACK gap** ([MC-DPL8R] 3.1.5.2.3 says 10 ms): only once three later frames
  are in and the missing one has been out for 1.5 round trips. Without that, a frame that was
  merely overtaken is sent twice. It happens once per frame, does not count against the ten
  retries, and leaves the backoff alone.
- **Lost acknowledgements:** a SACKed frame is not retried early, but when its normal timer
  runs out one such frame per `Tick` is resent so that the partner acknowledges again. A
  receiver that gets a send mask naming frames it is already past acknowledges again (20 ms):
  the sender evidently missed its acknowledgement and would otherwise sit with a full window.
- **Graceful close:** after our END_STREAM is acknowledged, we wait up to 10 s for the
  partner's. A close that times out after both END_STREAMs went out still counts as graceful.

## Tests

```
cmake -S src/DPNet -B build/dpnet && cmake --build build/dpnet && ctest --test-dir build/dpnet
```

`dpnet_tests` checks:

1. **The specifications' byte dumps.** The connection sequence and KeepAlives of [MC-DPL8R]
   4.1, the data frame and SACK of 4.2, and both Ethernet captures of [MC-DPL8CS] 4: a
   DirectPlay 9 player connect message and the host's `DN_SEND_CONNECT_INFO`. DPNet decodes
   them, and its encoders reproduce them byte for byte.
2. **A simulated network** with latency, jitter (so packets overtake each other), loss and
   duplication, running FServer against FClients:
   - enumeration, and refusals: wrong password, session full, wrong application, rejected by
     the application, nobody there;
   - hundreds of messages of 1 byte to 40 KB both ways at 0, 10 and 25% loss, and against a
     protocol 1.4 partner: everything arrives once and in order, and every send completes;
   - unreliable sends at 15% loss: some are lost, none repeat, none arrive out of order;
   - a non-sequential message that overtakes the middle of a large one, and a connection that
     links but never joins (dropped after the join timeout);
   - two idle minutes held by KeepAlives, `DestroyClient` with reply data, a client that
     vanishes, and closing the session;
   - a hard disconnect.
3. **Malformed input:** 30,000 random and corrupted datagrams, from strangers and from a
   connected client's own address. Nothing may crash, and other clients are unaffected.

`dpnet_tests --stress N` adds N more traffic runs with other seeds and loss rates up to 15%.
The `ctest` run includes 200 of them.

CI (`dpnet` job in `.github/workflows/build.yml`) builds and runs the tests on Linux (GCC, with
AddressSanitizer and UndefinedBehaviorSanitizer) and on Windows (MSVC, Win32 and x64).

## Next steps

1. **The adapter.** A UDP socket, a network thread, and COM objects that implement
   `IDirectPlay8Server`, `IDirectPlay8Client` and `IDirectPlay8Address` on top of FServer and
   FClient. DALib keeps calling the same interfaces, so its code stays as it is; only where the
   object comes from changes. Microsoft's on Win32, DPNet's in the port builds (the x64
   FLDedicated first). `IDirectPlay8Server::Close` blocks until every player is gone, while
   `FServer::Close` finishes over later `Tick`s, so the adapter waits for `IsIdle()` (with a
   time limit) before returning.
2. **Reference captures.** Retail FLServer and a retail client on Windows, recorded, to settle
   the choices above.
3. **Interoperability in CI, on Windows.** The DPNet server with a Microsoft DirectPlay client,
   and a Microsoft DirectPlay server with the DPNet client, both through FLRegress's scripted
   player.
4. **Other platforms.** With the adapter's socket code ported, the server no longer needs
   DirectPlay from the operating system.
