# DALib

DALib links DACOM components together and provides shared access to the engine, sound,
renderer and file system interfaces. Public declarations live in `src/include/DALib.h`,
`dalib_net.h` and `md5hash.h`; implementations are in `src/DALib/`.

Initialization obtains interfaces through SystemContainer, then sets up sound, the engine
and the effects library. Global references are released in a fixed order. The last Release
result is kept between calls so that remaining references can be checked.
Exported type names and static members are preserved; interfaces use the shared component
headers. The open-data descriptor keeps its own size and contract.

DirectPlay notifications are placed into synchronized queues and handed to the game
handlers during DispatchMsgs. Received DirectPlay buffers are returned through ReturnBuffer;
copied enumeration replies and outgoing data have separate malloc/free ownership.
The registry of server-side client proxies is shared by the module, not per server.
zlib streams are created lazily and independently for receiving and sending, and are reset
after each operation.
Known unverified and incomplete branches of message merging are described in the tracker.

The GUN wrapper obtains a COM factory from the library at the given path and owns the
references of the connection and of additional facets. Release is moved to a worker thread;
Shutdown waits for those threads. The semantics of the confirmed connection methods are
published in the shared header; unknown selectors and individual gaps in the browser
contracts remain in the tracker.
The GUN provider itself is not implemented in this module.

MD5Hash is exported as a class. Special members and accessors defined in the header
preserve the exports in consumer modules; the remaining bodies are provided by DALib.
