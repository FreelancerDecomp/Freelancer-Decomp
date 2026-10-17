# Shading.dll reconstruction ledger

- Current stage: `NATIVE_PARTIAL`
- Retail oracle: `EXE/Disasm/shading.dll.c`, `EXE/Disasm/shading.dll.txt`
- Native source: `src/Shading/`
- Tracker module: `Shading.dll`
- Tracker agent: `codex:shading-rebuild-20260909`
- Original binary forwarding/loading: none in the native implementation

## Tracker checkpoint 2026-09-11

- Functions: 643 tracked; 222 unprocessed, 185 `ready_unverified`, 130 `ready_verified`,
  97 skipped, 9 `partial`.
- Instruction-weight completion: 14,973 / 38,768 non-skipped instructions (38.62%).
- Globals: 111 tracked / 103 non-skipped; 98 unprocessed, 5 `ready_unverified`,
  8 skipped; byte-weight completion 144 / 4,793 (3.00%).
- `simplified`: 0; `in_progress`: 0.
- This iteration restored the MaterialLibrary group model, global record slots, remap ABI,
  filesystem-load group output, lookup/create paths, and the MaterialBatcher-controlled
  `BatchedMaterial` wrapper path. It also corrected the two-argument retail ABI of
  `IMaterial::ApplyVerify`, and mapped the retail TGA/DDS mip-loader entries to the
  native decode/upload pipeline with their remaining format-normalization gaps recorded.
  Exact no-op/success helpers and BatchedMaterial face-adjustment thunks were also
  classified from their complete instruction listings; the MSVC `_purecall` import
  thunk is recorded as a compiler placeholder rather than native library behavior.
  Sixteen additional VC6 container/storage helpers are classified as superseded by the
  native typed containers and RAII. Exact FVF position/color/texture-size decoders, the
  DDS mip cursor transition, both MaterialBatcher sort diagnostics, shared material
  teardown/name-copy methods, and the texture-address decoder are mapped to their native
  equivalents. The standalone variadic formatter, runtime metadata iterator, and CRT
  `_onexit` wrapper are classified as infrastructure rather than shading behavior.
  TextureLibrary retail comparison found two native side effects/gates that were still
  wrong: `SetWrapMode` was recomputing playback even though retail only writes the mode,
  and `GetGroupPixelFormat` incorrectly required two frames even though retail queries
  the first frame whenever the collection is non-empty. Both were corrected with red-first
  regressions. Release/add-ref/count/name/tile-rate accessors, the 3DMathEngine acquisition,
  and both pixel-format table lookups are now mapped as well.
  Group-name access was also corrected: retail reserves id zero and changes only the
  display-name allocation, whereas the port accepted id zero and silently rewrote the
  record CRC. Red-first coverage now locks the reserved-id and stable-identity contracts;
  the auto-increment texture-entry allocator is mapped alongside them.
  Seven material class-name vtable slots were then verified instruction-for-instruction
  against the exact factory names returned by the consolidated native `FMaterial` model.
  `NullMaterial` now returns before all animation, cached-pipeline, render-state, bind,
  teardown, and draw work in the four retail success-stub slots. Constant-return material
  slots now reproduce the retail values `1`, `0xA2`, `0x48`, and `8` for their respective
  vtables, including the retail non-null output-pointer contract. Controlling-unknown
  delegation is restored for `MaterialAnimation`, `TextureLibrary`, and `MaterialBatcher`,
  including their embedded `IAggregateComponent`/`IMaterialLibrary` adjustment paths;
  21 retail forwarding thunks are mapped to these native paths. Material components now
  also delegate `QueryInterface`/`AddRef`/`Release` to the descriptor's controlling unknown,
  matching the retail controller pointer and preserving standalone lifetime behavior. The
  fully mapped 27-slot embedded `FMaterialLibrary` vtable is recorded as the first completed
  global ABI table. The shared material-family controlling thunks at retail offsets `+0x68`,
  `+0x6C`, `+0x78`, and `+0x80` are mapped to the same native aggregation contract.
  Standalone `MaterialLibrary` and `BatchedMaterial` components now also delegate both their
  main and adjusted interface faces to the descriptor's controlling unknown.
  `IMAData` now exposes the retail five-slot public vtable instead of the previous three-slot
  `IDAComponent` surface: queried clients can call virtual `KeyBlock` and `CrcOut` at slots 3
  and 4. Its zero-ref `Release` path now also matches retail's saturating decrement and
  pre-destruction refcount reset rather than underflowing. Both ABI slots, queried identity,
  shared refcount, field offsets, and the zero-ref regression are covered by the native test.
  `IMAInstance` likewise now exposes its complete retail seven-slot face, including virtual
  key-block access, playback reset/tick, and UV-matrix output. Its queried-face dispatch,
  shared identity/refcount, field offsets, reset, matrix baseline, and zero-ref release path
  are covered; the animation and matrix bodies remain `ready_unverified` pending differential
  animated-frame vectors.
  The library-level instance creation (`IMatAnimLibrary` slot 5) and global tick traversal
  (slot 6) were rechecked against their complete listings and mapped to the native map/list
  implementation with container/allocation differences retained as verification gaps.
  The material-animation load slot now returns the retail opaque per-load handle instead of
  collapsing success to integer zero. The matching unload slot no longer acts as an empty
  success stub: it removes only the CRC/data objects owned by that handle and releases their
  references. A fake-filesystem contract test covers load, handle return, map visibility,
  handle round-trip, and removal; `Shading`, `flmaterials`, and `DALib` rebuild against the
  corrected public return type.
  The retail `IMAInstance` head, tail, and live-count globals are now all represented by the
  native intrusive-list lifecycle; the previously absent count is updated on every link/unlink.
  `ParseMaKeys` now uses the retail `IFileSystem` descriptor/open/read/close ABI for `MACount`,
  `MAFlags`, `MADeltas`, and `MAKeys` instead of the earlier node-navigation approximation.
  The CRC-map ownership-copy helper is mapped to native reference acquisition; two purely
  compiler-generated smart-wrapper/tree-layout helpers are explicitly skipped.
  Two additional generic MSVC value-copy helpers (8-byte and 56-byte container payloads)
  were verified from their complete bodies and excluded as compiler/container machinery.
  The registered `MaterialAnimation` factory now returns its embedded aggregate face through
  the descriptor's `InnerOut` field, matching the retail factory contract; a real
  `ICOManager::CreateInstance` regression test covers the external aggregation descriptor.
  The TextureLibrary and MaterialAnimation main-interface identity tables are mapped, and
  both owning `Release` paths now reproduce retail's saturating zero-ref decrement and
  pre-destruction reset; the zero-ref cases are covered by native regressions.
  MaterialBatcher registration, construction, rule ownership teardown, destruction, and
  controlling-unknown initialization are mapped. Its factory now returns the dedicated
  nondelegating controller through `InnerOut`, not the embedded aggregate face. The controller
  reproduces the retail IID map for `IMaterialBatcher`, `IMaterialBatcher2`, `IMaterialLibrary`,
  and `IAggregateComponent`; the manager-level identity tests cover the returned faces.
  The 496-instruction MaterialBatcher aggregate-initialization path is now mapped to
  `StartupFromOuter`. Required `IProfileParser`/`IRenderPipeline` acquisition failures
  propagate as retail `-1` instead of the prior unconditional success; the missing-dependency
  regression failed before the correction and passes afterward.
  MaterialBatcher rule parsing/matching/add/remove/clear and runtime teardown are mapped.
  Slots 6-8 now use the retail two-descriptor model: slot 7's public ABI was corrected from
  one argument to two (matching `RET 0x0C`), slot 6 selects a descriptor, and slot 8 assigns
  signed keys and rebuilds the two-entry ascending order. Flush slots now enforce the retail
  two-descriptor bound instead of the prior four-bin approximation; their geometry-bake body
  remains unverified. Native regressions cover bounds, field writes, ordering, rule lifecycle,
  and the corrected return values.
  Slots 3 and 12 were also corrected to their no-explicit-argument retail ABI (`RET 4`).
  Slot 12 now clears both descriptors and synchronizes the retail frame/pending-frame pair.
  The owned interface set/get pair at slots 14/15 and the plain dword set/get pairs at
  slots 16-19 are verified with reference-count and identity regressions.
  MaterialBatcher slot 11 now has its correct nine-argument public ABI (`RET 0x28`). Its
  negative-key immediate path is native and verified: `IBatchedMaterial` dispatch executes
  Bind/Submit/Teardown and releases the queried face, while an unsupported QI falls back to
  direct `IMaterial::Submit` with all seven draw arguments preserved. The nonnegative-key
  path now owns a retail-shaped 0xF0 payload in `map<IMaterial*, list<Record>>`. It copies the
  two source transforms, composes them as retail `B*A` in 3x3-plus-translation layout, clones
  optional data and vertex streams under their flag contracts, and either retains an external
  index source (`flags&8`) or owns the exact `Zero`-element subrange beginning at `Count`.
  The controller `Release` path also now saturates a zero refcount instead of underflowing;
  red-first regressions cover all of these paths.
  Queued flush now matches the entire 370-instruction 06ec9f10 listing:
  shared arena dispatch20/triangle24 scratch, qsort with exact x87 predicates,
  previous-context Slot7, callbacks reloaded after invocation, and actual
  GetPipelineState(7) transform selection. Dispatch construction releases the
  borrowed IBatchedMaterial face before dispatch as in 06ec9da0; mandatory
  buffer/stride/count calls have no port-only guards. Per-triangle order uses
  packed float3 and actual scratch index arrays, not a stable heap-vector sort.
  The remaining four unprocessed tiny thunks were also resolved: texture-coordinate flag
  normalization, aggregate Outer assignment, and the pixel-format table accessor map to native
  code/data; the VC6 child-vector end reset is superseded by `std::vector` construction. There
  are now no `simplified`, `partial`, or unprocessed `thunk` functions.
  Eleven additional tiny public methods were verified against their complete listings: the
  six BatchedMaterial adjusted/forwarding slots and five TextureLibrary accessors. This pass
  found and removed an extra native side effect in `SetTileRate`: retail changes only the rate
  field, while the former port also recomputed playback and could change the current frame.
  A red-first regression now locks the one-field contract. The next material pass corrected
  slots 12, 13, and 19 to their retail two-argument ABI, restored all BatchedMaterial slot
  11–19 forwarding (including slot 15's `| 0x10` result transform and null-inner failures),
  and restored the two-pass detail material's pass count of 2. Twelve more functions are now
  mapped to these tested native paths. Ten additional VC6 vector/tree/arena implementation
  helpers are explicitly superseded by native RAII containers; the texture-frame default
  initializer and pixel-format descriptor selector are mapped to their native data paths.
  The material flag-query slot was then split back into its retail per-family behavior:
  `DcDtTwo` returns `1/3`, `DcDtEt` and `BtDetailMapMaterial` return `0x48/0x4A`,
  `BtDetailMapTwoMaterial` returns `0x41/0x43`, and Nebula returns constant `3`.
  Red-first tests cover both alpha states. Six retail slot bodies are mapped to this native
  implementation. Eleven more VC6 smart-pointer/vector/tree/arena/aggregate helpers are
  explicitly superseded by typed native ownership. The retail process-attach entry point is
  also mapped to native `DllMain`: both disable thread callbacks, register all component
  families, and return success.
  Texture playback was then restored from the raw x87 listing instead of the former merged
  approximation. `SetLocalRate` now keeps a separate local value and active-rate selection,
  duration recomputation no longer changes elapsed/current-frame state, and absolute frame
  selection uses retail's `floor(fmod(effectiveRate*elapsed, frameCount))`. Clamp, loop, and
  the one-boundary-flip ping-pong branches now follow `FUN_06ebe660` instruction-for-instruction.
  Red-first tests cover the local/external rate split and the `-3` unsigned frame-selector
  case; upper and lower ping-pong crossings are also covered. The playback constructor and
  13 core/wrapper functions are verified. `CreatePlaybackGroup` now allocates a distinct
  id-keyed playback record which borrows the canonical source, holds independent state, and
  registers only multi-frame records in `ActivePlayback`; `TickAllPlayback` walks that set in
  insertion/id order. This removes the former merged-id and tick-every-entry deviations, and
  both functions are now `ready_verified` with contract coverage.
  The exact TextureFormatMaps end-to-begin clear helper is also mapped to the native vector
  clear path, which retains capacity just like the retail storage.
  All 27 component-registration helpers and their shared factory QI/Release behavior are now
  mapped to the native registration table; the shared Release path was corrected to saturate
  zero and restore refcount one before destruction. A later raw-asm pass corrected the old
  interpretation of IMaterial slot 5: `FUN_06eb1720` serializes current properties to its
  IFileSystem argument rather than reapplying them. Native SaveProperties now emits the ten
  SinglePass entries and performs the retail three-step read-modify-write of `Dt_flags` for
  wrap/U/V. The common read/write descriptor helpers are mapped as well. A fake-filesystem
  regression checks all descriptor fields, write order, vector/scalar/string sizes and the
  final packed flags; the internal temporary variant ownership helpers remain
  `ready_unverified` because native typed fields eliminate that representation.
  The shared material and BatchedMaterial nondelegating Release paths now also saturate
  zero and restore refcount one before destruction. BatchedMaterial initialization was
  corrected to balance its `IMaterialBatcher` QI immediately and retain a borrowed pointer;
  its destructor no longer performs an extra Release. Zero-ref and borrowed-ownership
  regressions cover both defects, and constructor/destructor/initialize/core-release are
  now individually mapped.
  BatchedMaterial's `Bin` persistence and render entry path were then restored from raw asm.
  Load/save now read and write the four-byte `Bin` leaf before forwarding to the inner
  material; ApplyVerify forwards both arguments and performs the retail one-shot alpha-bit
  cache. Submit no longer bypasses batching: it selects `Field8`, calls MaterialBatcher slot
  11, and for flag `0x40` supplies the negated transformed Z depth with flag `0x20` set.
  The face's material replacement path now retains the newly created native inner material
  instead of accidentally resolving aggregation back to its own wrapper, and creation failure
  clears the old inner while returning retail success. Main/adjusted QI identity, base material
  bind's mesh-driven COLORVERTEX predicate, and the shared submit kernel's IRPDraw argument
  layout are covered by new regressions. The large Atmosphere submit and TextureLibrary format-
  map initialization bodies are mapped to their existing native implementations as
  `ready_unverified`, retaining focused math/parser differential checks as explicit gaps.
  TextureLibrary slots 5/6 were then restored to their actual retail ABI. Slot 5 now accepts
  `(IFileSystem*, outGroupId*)`, enumerates `Texture library`, and returns a distinct group;
  slot 6 accepts `(IFileSystem*, childName)` and loads only that child. The old nickname lookup
  misuse was removed from materials and client callers in favor of the explicit helper. The
  texture id counter now starts at retail value 1. Focused empty-group, missing-root, and
  named-child tests cover the wrapper behavior; the full client call sites now use slot 5.
  MaterialRegex.cpp/.h now implement the actual Spencer compiler and bytecode
  executor; ECMAScript equivalence was disproved. MaterialMap and FBatchRule own
  persistent compiled programs, preserving compilation, matching and ordered
  destruction. The focused regression covers 45 searches and 9 invalid patterns.
  Native std::map ownership replaces individually traced private VC6 tree helpers.
  Legacy TEX/RECT/SPRP loading 06ec1140 is fully listing-verified, including child
  temporary refs, x87 frame stores, owned canonical publication and fallback order.
  Three 323-instruction material-submit vtable bodies were normalized across their complete
  listings and proved identical modulo relocated addresses. They map to the tested native
  `FMaterial::SubmitKernel`, including IRPDraw argument layout, index bias, VB/index fallback,
  pipeline draw selection, and teardown, and are now `ready_verified`.
  The 348-instruction DcDtTwo submit slot was separately rechecked and mapped to the native
  first-pass submit plus mesh-gated second pass, VB lock, normal-orientation handling, cull swap,
  and trailing-zero draw call. It remains `ready_unverified` pending the reserved live RP8/VB
  differential run.
  The normal `DetailMap2Dm1Msk2Pass` submit branch now preserves and restores render states
  `0x0F`, `0x18`, and `0x17`, performs the exact `0xFE/2` first-pass and `1/5/4` second-pass
  transitions, and issues two draws. A red-first pipeline probe locks the complete nine-write
  sequence and both draw calls. The outer submit and the two related family state helpers are
  recorded as three additional honest `partial` functions: their family-specific texture-stage
  tables, transform-scale field, and alternate reset branches still require native modeling.
  Two further VC6 vector fill-insert bodies were identified from their begin/end/capacity layout,
  complete in-place and growth paths, old-allocation release, and exact five-dword/13-dword
  element copies. They are superseded by native `std::vector` for 20-byte and 52-byte records.
  `IllumDetailMapMaterial` and `Masked2DetailMapMaterial` were separated from the formerly shared
  submit approximation. Both now report two passes and issue two draws. IllumDetail preserves
  five scoped states with its exact eleven-write order; Masked2Detail preserves four states with
  its distinct ten-write order, including restoration of state `0x19`. Red-first probes cover
  both sequences. Their outer slots remain `partial` only for the explicit family texture/TSS
  helpers and alternate pass-mode branches. The IllumDetail property-schema getter now returns
  the retail count 18; its contiguous 18-record metadata table/output pointer remains a recorded
  structural gap.
  The common render-state and texture-stage table walkers plus the scoped render-state guard
  constructor/destructor are now mapped to `ApplyStateTable` and the explicit saved-state
  submit sequences as `ready_verified`. IllumDetail and Masked2Detail teardown were also split
  from the generic approximation: each applies its six-entry stage-1 reset table, disables
  transforms on stages 0 and 1, and restores stage-1 selector `0x0B`. Red-first probes verify
  all nine stage/state/value writes for both families. The Atmosphere property-schema entry is
  recorded as `partial`: its nine typed fields/count are native, while the contiguous retail
  metadata-table output remains absent.
- Debug builds: `Shading.vcxproj`, dependent `flmaterials.vcxproj`, `DALib.vcxproj`, and
  `freelancer.vcxproj`, zero errors (the full client retains unrelated existing warnings).
- Native aggregation contract test: `src/tests/shading_aggregation_tests.cpp` passes.
- Current built candidate `src/bin/Shading.dll` SHA-256:
  `6DA61458F12CAEEF74AF6887586E93BAFE2DD3F23D9814A34FADA2150B3B88F4`.
- Installed `bin/Shading.dll` remains the preceding analyzed candidate SHA-256:
  `AA4775B011AB0B27D5E7F8A622713E7B62857A7DEA31B1B3B7A30035D9E26767`;
  deployment/runtime smoke remains reserved for the `user-windbg` session.

## Stage record

- `INVENTORY`: 643 counted functions and 111 counted globals after tracker normalization;
  public vtables recovered from the retail listing.
- `PROXY_BASELINE`: historical project state; no proxy implementation is used by current `src/Shading`.
- `OBSERVATION`: static callers, callees, strings, vtables, decompilation and instruction listing are the current oracle. Runtime observation is reserved for a Cursor session with `user-windbg`.
- `NATIVE_PARTIAL`: active. TextureLibrary, MaterialLibrary, MaterialAnimation, MaterialBatcher and native material factories exist; confirmed simplified/stubbed functions are being resolved individually through decomp-tracker.
- `NATIVE_DIAGNOSTIC`: not reached.
- `NATIVE_STABLE`: not reached.

## Gates still open

- Resolve every understood simplified/partial function or record its exact structural gap.
- Complete listing verification of public/vtable behavior and all reachable helpers.
- Build solution-level dependents, run contract checks, preserve candidate DLL/PDB, and hand off bounded runtime smoke to a Cursor `user-windbg` session.
- Prove the final load path contains no retail `Shading.dll` fallback before `NATIVE_STABLE`.
