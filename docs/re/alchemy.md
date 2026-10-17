# alchemy.dll — effects system

The module creates and updates effect graphs, stores node prototypes and archetypes,
manages particle pools and hands geometry to the render pipeline.
The sources are in `src/alchemy/`, the public types are in
[`src/include/alchemy.h`](../../src/include/alchemy.h).

The source of behaviour is `EXE/Disasm/alchemy.dll.txt`; when operands are truncated,
the original PE `EXE/alchemy.dll`. This document describes architecture and formats; statuses,
address evidence, slots, source mappings and specific gaps are kept
in decomp-tracker. Shared debt — [deviations.md](deviations.md), contract rules —
[cross_dll_protocol.md](cross_dll_protocol.md).

## Loading and shared state

DACOM loads our `bin/alchemy.dll`; the retail DLL serves only as the RE reference.
There is no fallback to the retail DLL on a load error. The PE publishes no exports;
components are reachable through DACOM factory registration.

Process attach order: disable thread notifications, choose the CPU/math dispatch,
register Alchemy and FxRuntime, read `[Alchemy]`, create the eight appearance
pools, then register the remaining components. The INI parser is built into dacom and
is obtained through `IProfileParser`; luaprofile is not involved in this chain.
A separate Alchemy Initialize obtains the pipeline interfaces and links the initial
node. Moving the INI and the pools into Initialize changes the order of side effects.

Process detach frees the pools in the order Oriented, Mesh, Rect, Perp, Particle,
Basic, Dust, Beam. Cleanup of the pipeline, effects and drawing has its own
owners; it must not be added to DLL detach merely for convenience.

Callbacks, runtime bindings, the pipeline cache, draw flags and batch lists belong
to the DLL, not to an individual Alchemy instance. Independent binding parameters keep
independent values even if one caller passes identical pointers.
Changing ownership requires tracing all producers, consumers and teardown.

Local 3DMath caches keep independent state: an identical service name
does not prove that instances can be merged and the number of CreateInstance calls changed.
Reads of caches, property sets and links after an external callback must take
the current value. An owning reference and a borrowed set cache may be updated
at different moments of one operation; deriving the cache from the owner is permitted only
after proving that order on all relevant paths.

Alchemy and FxRuntime have an inner unknown with its own reference counter and
a reference to the owner. The public facets delegate QI/AddRef/Release to the controlling
outer; for a standalone object this role is played by its own inner.
The aggregation descriptor uses the shared type from `dacom.h`. The FxEffect creation
descriptor has a separate contract: archetype and root parent.

The effect library binding stores `IAlchemy*` for tree operations and
`IFxNodeLibrary*` for prototype lookup separately. The shared node library interface is declared
in the public header and is the base of `IAlchemy`. Load and save take
`IFileSystem*`; the chunk uses the file descriptor from `readfile.h`.

## Object model

The node library maps the hash of an instance name to a prototype. Creation from a prototype
goes through its CloneNode; the instance name is not the name of a DACOM factory.
The effect library maps a key to an archetype. CreateEffect receives a ready
32-bit key; an additional lookup by name with a different hash changes the contract.
Name hashing uses the Alchemy/DACOM CRC table without case folding.

The node loader, the node library and the effect library have independent static
buffers of resolved names. A returned borrowed string persists until the next
generation in the same buffer; a call to a neighbouring generator does not overwrite it.

Both libraries keep an owning map by hash and a separate insertion-order chain.
Traversal and saving walk the chain; after a callback the current Next link is read.
Cleanup first breaks the chain, then frees the map in reverse key order.

An archetype stores node records and association pairs. An effect instance creates/clones
nodes, links parents and sibling lists, then emitter with appearance and appearance
with fields. The links have their own reference ownership. An identical numeric slot offset
on different facets does not mean the same method.

In an effect instance the root records form a separate prefix, ordered
by the node sort key. The number of roots differs from the total number of records.
Linked appearance fields have separate public count/Out and index/Out methods;
internal traversal uses borrowed references.

A node and its property set are different objects and interfaces. Typed property sets
hold values and real ramp/animation objects; the class tables describe
names, hashes, kinds and bounds of properties. Enumeration first walks the base set,
then the set of the specific class and preserves callback short-circuiting. A generic bag
of unknown values does not replace this protocol.

Private C++ classes and containers use the modern CRT/STL. Their binary
layout need not reproduce VC6. A cross-module interface is declared once
in a shared header; the known type is preserved at the caller and the receiver.

## ALE formats

In the UTF container the catalogs `ALEffectLib` and `AlchemyNodeLibrary` contain data
leaves of the same name. First enter the catalog, then open the leaf from inside it.
On a normal exit the library moves to `..` before closing the chunk.
Error exits have their own cleanup order. The passed FileSystem
serves the whole operation, including errors and cleanup.

`ALEffectLib` begins with a float version and a signed effect count. An effect record
contains the name, for version 1.1 an additional Header16, then a count and an array of
16-byte node records, a count and an array of 8-byte associations. The public
`FAleRec16`/`FAleRec8` describe the record values: IDs, links and prototype keys.
The permutation of fields between file storage and the accessor API must be preserved.

String: uint16 length with NUL, then bytes; an odd length is padded with one padding
byte. Numeric fields are little-endian.

Node property stream: uint16 Type; zero terminates the record, otherwise a uint32 Hash
and payload follow. `Kind = Type & 0x7fff`; the high bit of the bool type encodes the value without
additional bytes.

| Kind | Payload |
|------|---------|
| 1 | Bool in the high bit of Type |
| 2, 3 | Four-byte scalar value |
| 0x100, 0x101 | Mode, key count, scalar/color ramp keys |
| 0x102 | AnimatedSingle track |
| 0x103 | String with length and padding |
| 0x104 | Pair of 32-bit values |
| 0x105 | AnimatedTransform and its nested tracks |
| 0x200–0x202 | Nested ramp/animation data, version-dependent |

The legacy remap of hashes and kinds is applied before the typed accessor is chosen. An unknown
type has an error path; the payload size must not be guessed and reading continued.
Short-read, reset, mutation order and exception cleanup are part of the format.
Loading reads the chunk directly: Reset, setters and key additions are interleaved
with file reads. Temporary target/child references are released on normal
exits and on short read; cleanup on exceptions matches the retail EH chain.

## Pools, simulation and drawing

Basic, Particle, Rect, Perp, Mesh, Oriented, Dust and Beam have separate pools.
A pool owns item storage, free/used lists and counters. Spawn, reclaim, unlink,
re-creation and teardown preserve the owner of the record. Additional
COM objects in a Mesh/Particle item are released under their own contract;
the factory pointer and the created object must not be mixed up.

Issuing and returning pool memory does not release the record's COM references. The eraser first
breaks the list and cleans up the record's objects, then returns the memory
to the pool. Temporary owners of these references preserve the cleanup order when an exception
is thrown from a callback. The memory of the COM object itself is freed after a successful destructor.

The emitter computes the frequency and the number of new items; the appearance applies fields,
integration and its own transform/rotation rules. Begin/Apply/End of fields and the order of
ramp evaluation remain part of the graph. Math dispatch is checked against the actual
consumers, not against the generic name "x87".

Small numeric operations are factored into `alchemy_math.cpp`, which in all
configurations is built with `/O2 /Oi /fp:fast /arch:IA32`. This yields
direct x87 instructions and a single final `float` store via C++ means;
the rest of the arithmetic keeps the project's precise floating-point mode. Conversion
to integer temporarily changes only the x87 rounding to truncation and restores it
after a successful conversion, leaving MXCSR untouched.

BeginDraw/EndDraw manage the draw protocol, VB recovery, saving/restoring of
render states, the four batch lists and counters. The material state block defines
render states and texture-stage states; it is not a vertex-format declaration.
A buffer creation error gives no right to add a repeated SelectVB or to change
InDraw differently from retail.

The appearance builds the geometry of its kind; Mesh obtains the object created by
the client through the public `IFxMeshFactory`. Camera, textures and pipeline are
cross-module contracts. A null fallback or approximate geometry is not considered
equivalent to the retail path.
