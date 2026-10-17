# Deformable2.dll

Deformable2 ties together the skeleton, animation, deformable meshes and character
materials. DACOM registers the `Deformable` component; EngBase attaches it as a child
engine component. Shared interfaces and part descriptions are published in
`src/include/engine/engbase.h`; the internal models are in `src/Deformable2/`.
EyeMaterial is a separate `IMaterial` implementation in the same module.

## Component and dependencies

`FDeformableObject` implements `IDeformable` and `IEngineComponent`. The outer
QueryInterface/AddRef/Release are delegated to the controlling component; the nested
`FDeformableInner : IDAComponent` serves the non-delegating side of aggregation.
The factory creates the component, and `IDeformable::Create` creates a separate
`FDeformInstance` with parts and a queue of animation/IK events.

Component creation obtains the system container and the engine. Initialization
connects RP8, the material library, the VB/IB managers, animation, channels and the
RendComp renderer. Each cache keeps its own ownership contract:
some QueryInterface results are released immediately and remain
borrowed pointers. Converting such fields to concrete types
adds no references and does not extend object lifetimes.

## Loading parts and meshes

The module walks the UTF through `IFileSystem` from ReadFile. After a child
node is processed, the `".."` transition returns the file system to the parent. The open
descriptor uses the published `FFileSystemDesc`, preserving the size of the passed
prefix; additional fields of other file system modes are not implied.

A part description specifies the file source, the LODs and their fractions. Loading joins
the `DFMSkeleton` skeleton, face groups, geometry and weight tables. The mesh cache
is indexed by the CRC of the name and holds a shared `FDeformMesh`: either its own DFM mesh
`FMeshArchetype` or an `FVmeshWrapper` with a VMesh library obtained through the engine.
A cache hit keeps the original reference protocol.

| UTF nodes | Data |
|---|---|
| `DFMSkeleton`, `Bone to root` | Bone hierarchy and initial transforms |
| `Geometry`, `Points`, `Vertex_normals` | Positions and normals |
| `Point_indices`, `UV0`, `UV0_indices` | Position indices and UV streams |
| `Face_groups`, `Material_name`, `Face_indices`, `Tristrip_indices` | Material groups and triangles |
| `Point_bone_count`, `Bone_id_chain`, `Bone_weight_chain` | Vertex-to-bone links and weights |
| `UV_bone_id` and related UV parameters | UV deformation records for the material |

## Transforms and drawing

`FLodHolder` links the skeleton to `FBoneRuntime : IInstanceTransform` and to the
EngBase bone instances. The local pose and the skinning matrix are separate
areas of the runtime bone. `MarkWorldDirty` passes world-matrix dirtiness
through the installed callback, preserving the flag transition and the return value.

A part holds a list of LOD meshes, CPU data and GPU buffer descriptors. Skinning
transforms positions and normals by the weights; remap distributes the result among
vertices that share a position index. The VB listing passes the chosen
buffer and the base vertex to the material, and the IB holds the face group indices.

When drawing, `FMaterialSubmitContext` links the geometry, the VB listing,
the world matrix, the view and the material. `FFaceGroup::UvBinding` borrows the UV
deformation record, the runtime bone and the counter of applied transforms for the duration of
Bind/Draw/Teardown. EyeMaterial uses this context for the texture
matrix; it is registered by the pattern `^eye*`. The skinning and UV counters are available
through the mesh statistics query.

## Animation and IK

`FDeformInstance : IChannelSink` receives channel notifications and forwards them to the
caller's stored sink. Typed `FIkListNode` entries link
events; the payload distinguishes an animation handle from an `FIkEvent`. Removing a node
frees the corresponding event or animation in the original order.

An IK event owns the typed channels it creates and serves as their callback owner.
The single-joint branch and the DLS solvers use EngBase and
3DMathEngine transforms, shared static current/trial poses and an SVD pseudo-inverse
matrix. The split between float storage and double intermediate computations is preserved.
`DeformRate` is set through the DACOM profile and participates in the instance cookie.

## Memory and boundaries

Scalar objects use plain `new` with the original initialization.
Raw counted buffers preserve the byte count computations, headers,
remainders of the input data and the cleanup order; paired
`new unsigned char[]`/`delete[]` with a conversion at the write boundary are used for them.
This does not imply constructing an array of C++ objects or rounding the
size. The malloc/calloc/realloc families keep their failure contract.
The shared factory base is freed separately, since there is no virtual destructor
in its interface.

Addresses, historical RE hypotheses, slot mappings and specific discrepancies
are kept in decomp-tracker. Shared debt is described in [deviations.md](deviations.md),
and the client's substitutions in [substitutions.md](substitutions.md).
