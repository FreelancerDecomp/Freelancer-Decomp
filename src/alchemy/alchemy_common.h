// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "alchemy_types.h"

struct ICamera;

// alchemy.dll — DACOM particle FX scaffolding.
// Shared constants, CRC/hash helpers, string helpers, FDUMP/debug helpers,
// appearance-pool alloc/free, COM-pointer helpers, particle/math helpers,
// mesh-pose helpers, byte-bag helpers, generic COM/VB call-thunk helpers and
// the ALE node-lib parsing helpers used by more than one of the concrete
// alchemy classes. Every shared global is declared extern here and defined
// exactly once in alchemy_common.cpp.
namespace AlchemyImpl
{
    // ---- Constants ----------------------------------------------------
    // GR_INTERFACE_UNSUPPORTED / GR_OUT_OF_MEMORY are DACOM-wide and come from dacom.h;
    // only alchemy's own ALE codes live here.
    extern const GENRESULT GR_ALE_LOAD_FAIL;
    extern const GENRESULT GR_ALE_CREATE_FAIL;
    extern const void* kRegisterPriority;

    extern const char kAleNodeName[];
    extern const char kNodeLibName[];
    extern const char kFxNodeFactory[];
    extern const char kColideTypo[];
    extern const char kCollideFixed[];
    extern const unsigned long kNodeParentRoot;
    extern const char kAleLeaveNode[];
    extern const char kAleFileSystemName[];
    extern const unsigned long kAleDescSize;
    extern const unsigned long kAleAccessRead;
    extern const unsigned long kAleAccessWrite;
    extern const unsigned long kAleShareRead;
    extern const unsigned long kAleOpenExisting;
    extern const unsigned long kAleCreateAlways;
    extern const unsigned long kAleFileFlags;
    extern const unsigned long kAleVersion10Bits; // 1.0f DAT_0624bac4
    extern const unsigned long kAleVersion11Bits; // 1.1f
    // Used as an array bound (char NameBuf[kAleNameMax]) across translation
    // units, so it must be a real constant expression, not an extern const.
    constexpr unsigned long kAleNameMax = 0x100;
    extern const unsigned long kCrc32Poly;

    extern const char kAlchemySection[];
    // FUN_062013f0 / LAB_06201330 FDUMP - EXE/alchemy.dll exact bytes.
    extern const char kAlchemyCppPath[];
    extern const char kFdumpErrorFmt[];
    extern const char kFdumpWarningFmt[];
    extern const char kMsgAcquirePipeline[];
    extern const char kMsgAcquireTexLib[];
    extern const char kMsgAcquireVb[];
    extern const char kMsgAcquireIb[];
    extern const char kMsgAcquireDraw[];
    extern const char kMsgCreateVb[];
    extern const char kMsgDrawWithoutBegin[];
    extern const char kMsgBeginWithoutEnd[];
    extern const char kMsgEndWithoutBegin[];
    extern const unsigned int kAlchemyVbFvf; // D3DFVF_XYZ|DIFFUSE|TEX1
    extern const unsigned long kAlchemyVertexStride; // FUN_06208aa0 pointer += 0x18
    extern const unsigned long kInitNodeBoundBits;
    extern const unsigned long kBatchListCount; // DAT_062578b4 .. DAT_062578e4 stride 0xC
    extern const char kDrawContextCpp[];
    extern const char kMsgEndBeforeBegin[];
    extern const char kMsgMoreVerts[];
    extern const char kMsgMoreIndices[];
    extern const char kMsgEndBeforeDraw[];
    extern const unsigned long kAssocEmitterAppearance;
    extern const unsigned long kAssocAppearanceField;

    // FUN_06203c20 / FUN_06203480 defaults when the key or parser is missing.
    extern const unsigned long kDefaultPoolSize;
    extern const unsigned long kDefaultUseMaterialBatcher;

    // FUN_0623ebc0 element sizes (third arg) per appearance pool.
    extern const unsigned long kStrideBasic;
    extern const unsigned long kStrideParticle;
    extern const unsigned long kStrideRect;
    extern const unsigned long kStridePerp;
    extern const unsigned long kStrideMesh;
    extern const unsigned long kStrideOriented;
    extern const unsigned long kStrideDust;
    extern const unsigned long kStrideBeam;

    extern const char kCharsetValue[];
    extern const char kCharsetDigits[];

    // Field descriptor ramp defaults; see the definitions in alchemy_common.cpp for the
    // builder addresses and property hashes.
    extern const float kDefaultGravity;
    extern const float kDefaultAirApproach;
    extern const float kDefaultAirMagnitude;
    // FUN_06235f50 SetKey literals 0x41200000/0x3e99999a/0x42480000
    // (10.0/0.3/50.0), confirmed by direct disasm read wave 12
    // (alchemy-wave12-lane2). Read order in FGenericFxObject::BeginField's
    // FxRadialField branch matches the ctor's ramp-build order (Magnitude,
    // Attenuation, Approach).
    // ALC-4: FxRadialField's descriptor ramp defaults, named by the property table at
    // .data 0x062569c8 (Magnitude 10.0, Approach 0.3, Radius 50.0).
    extern const float kDefaultRadialMagnitude;
    extern const float kDefaultRadialApproach;
    extern const float kDefaultRadialRadius;
    // Turbulence descriptor ramps: inner +0x4c TurbulenceField_Approach 1.0, +0x50
    // TurbulenceField_Magnitude 10.0 (see alchemy_common.cpp).
    extern const float kDefaultTurbulenceApproach;
    extern const float kDefaultTurbulenceMagnitude;
    // wave 14 (sonnet:alchemy-wave14-lane1): FUN_06219a30 (FxCollideField's
    // real ctor) hardcodes 0.7f — the sole occurrence of that literal in the
    // whole binary. Was previously an arbitrary 1.0f placeholder.
    extern const float kDefaultCollideReflect;
    extern const float kDefaultCollideWidth;
    extern const float kDefaultCollideHeight;
    extern const unsigned long kDustKillAgeBits;

    // ---- Shared globals -------------------------------------------------
    // FUN_0623ef70 / IAlchemy+0x14: DAT_06256d68 table (= dacom DAT_065b8530),
    // case-preserving. NOT IEEE 0xEDB88320. NOT DACOM_CRC::GetCRC32 (lowercase).
    extern unsigned long G_Crc32Table[256];
    extern unsigned char G_bCrc32Ready;
    extern char G_ResolvedName[0x100];
    extern char G_NodeLibraryResolvedName[0x100];
    extern char G_EffectResolvedName[0x100];

    extern FAlchemyDefaults G_Defaults;
    extern FAppearancePool G_PoolBasic;
    extern FAppearancePool G_PoolParticle;
    extern FAppearancePool G_PoolRect;
    extern FAppearancePool G_PoolPerp;
    extern FAppearancePool G_PoolMesh;
    extern FAppearancePool G_PoolOriented;
    extern FAppearancePool G_PoolDust;
    extern FAppearancePool G_PoolBeam;

    extern unsigned long G_MaxVertices; // FUN_06202260 / DAT_06255064
    extern unsigned long G_MaxIndices;  // FUN_06202270 / DAT_06255068

    extern bool G_bInDraw;              // DAT_06257918
    extern IAlchemy* G_EffectAlchemy;             // DAT_06257a3c
    extern IFxNodeLibrary* G_EffectNodeLibrary;    // DAT_06257a38
    extern IAlchemy* G_InitializedAlchemy;        // DAT_06257bb8
    // DAT_06257b9c: IFxRuntime +0x24 LAB_0623b6a0 store / +0x28 LAB_0623b740 get+AddRef.
    // Client FUN_004f70d0 writes DAT_00674cdc = FUN_004f67e0 (8-byte, vtable 005d98bc).
    // Not IEngine. Mesh Extra2 is the OUT of factory+0x0C = FUN_004f6810, not this pointer.
    extern IDAComponent* G_FxMeshFactory;
    // IFxRuntime Bind* raw stores (no AddRef): DAT_06257b98 (LAB_0623b670, the IAlchemy),
    // DAT_06257b94 (LAB_0623b680) and DAT_06257b90 (LAB_0623b690, the IFxEffectLibrary).
    // The particle appearance's child-effect spawn FUN_0622f6a0 / FUN_0622fcf0 reads
    // b98 and b90 directly. Were FFxRuntime members until 2026-09-26.
    extern void* G_RuntimeAlchemy;
    extern void* G_RuntimeUnknown;
    extern void* G_RuntimeEffectLibrary;
    extern I3DMathEngine* G_3DMath;    // DAT_06257a6c via FUN_0623e830
    extern IDAComponent* G_RootNode;  // DAT_062578b0 - GetRootNode / Detach root
    extern IDAComponent* G_PipelineOuter;   // DAT_0625790c desc+8
    extern IRenderPipeline* G_RenderPipeline;  // DAT_06257bc0
    extern IDAComponent* G_TextureLibrary;  // DAT_06257bbc
    extern IDAComponent* G_RpVertexBuffer;  // DAT_06257bc4
    extern IDAComponent* G_RpIndexBuffer;   // DAT_06257bc8
    extern IDAComponent* G_RpDraw;          // DAT_06257bcc
    extern void* G_VbHandle;                // DAT_0625791c
    extern void* G_IbHandle;
    extern unsigned long G_DrawCookie;            // DAT_06257908
    extern unsigned long G_DrawCounters[9];       // DAT_062578e4
    extern unsigned char G_DrawFlagA;             // DAT_06255060
    extern unsigned char G_DrawFlagB;             // DAT_06255061

    extern FBatchList G_BatchLists[];

    // The six draw contexts (each FUN_06240f40-initialised from the __xc table) and the VB
    // handle FUN_06240f50 snapshots for all of them.
    extern FAlDrawContext G_BeamDrawContext;     // DAT_0625792c
    extern FAlDrawContext G_DustDrawContext;     // DAT_0625797c
    extern FAlDrawContext G_BasicDrawContext;    // DAT_062579dc
    extern FAlDrawContext G_OrientedDrawContext; // DAT_06257a74
    extern FAlDrawContext G_PerpDrawContext;     // DAT_06257aec
    extern FAlDrawContext G_RectDrawContext;     // DAT_06257b40
    extern void* G_DrawVbHandle;                 // DAT_06257bd4

    extern unsigned long G_AlchemyRandState;
    extern unsigned long G_AlchemyRandCounter;

    // ---- CRC / hash / string helpers ------------------------------------
    void EnsureCrc32Table();
    unsigned long HashNameCrc(const char* Name);
    void CopyCString(char* Dest, unsigned long DestChars, const char* Src);
    void FixColideFieldAlias(char* Name);

    // ---- FDUMP helpers ----------------------------------------------------
    void FdumpAlchemyError(int Line, const char* Message);
    void FdumpAlchemyWarning(int Line, const char* Message);
    void FdumpDrawContextError(int Line, const char* Message);
    bool IidEqual(const char* Left, const char* Right);
    bool NameEqual(const char* Left, const char* Right);

    GENRESULT CreateNamedFx(const char* Name, IDAComponent** Out);

    // FUN_06227710: map hit → prototype->vtable[+0x0C](out). Port: CreateNamedFx
    // by GetFxFactoryName(), then the archetype copy (the prototype's property set
    // shared, FUN_0623fc30, and an appearance's field list).
    // Entry->Name is the instance-name key (Rec.B), not a DACOM class name.
    GENRESULT InstantiateFromNodeLibPrototype(IDAComponent* Prototype, IDAComponent** Out);

    // Forward declaration only (defined in alchemy_genericfx.h): lets
    // IntegratePool/ApplyFieldsToPool below take a pointer to the owning
    // appearance instance without this header including
    // alchemy_genericfx.h (which itself includes this header — a real
    // include cycle). alchemy_common.cpp includes alchemy_genericfx.h
    // directly in the .cpp (not here) to see the full type for member
    // access; see IntegratePool/ApplyFieldsToPool's doc comments there.
    class FGenericFxObjectBase;

    // ---- Appearance-pool alloc/free --------------------------------------
    void UnlinkPoolNode(FPoolLink** Head, FPoolLink** Tail, FPoolLink* Node);
    void AppendPoolNode(FPoolLink** Head, FPoolLink** Tail, FPoolLink* Node);
    // wave23 (sonnet:alchemy-wave23-lane1, 2026-09-11): same shape as
    // UnlinkPoolNode/AppendPoolNode above, but typed for FPooledParticle's
    // OWN Next/Prev fields (alchemy_types.h) instead of FPoolLink's. Backs
    // the per-owner-instance active-particle list (FGenericFxObjectBase::
    // InstanceListHead_/InstanceListTail_, alchemy_genericfx.h) that
    // IntegratePool/ApplyFieldsToPool now walk instead of Pool->UsedHead —
    // see decomp-tracker item 132111 for the full retail trace this closes.
    void UnlinkParticleNode(FPooledParticle** Head, FPooledParticle** Tail, FPooledParticle* Node);
    void AppendParticleNode(FPooledParticle** Head, FPooledParticle** Tail, FPooledParticle* Node);
    void PrependParticleNode(FPooledParticle** Head, FPooledParticle** Tail, FPooledParticle* Node);
    void ResetAppearancePoolLists(FAppearancePool* Pool);
    void FreeAppearancePool(FAppearancePool* Pool);
    unsigned long PoolUsedCount(const FAppearancePool* Pool);
    FAppearancePool* PoolForAppearanceFactory(const char* Factory);
    void AppearanceDrawUnits(const char* Factory, unsigned long* VertsPer,
                             unsigned long* IndicesPer);

    // ---- COM-pointer helpers ----------------------------------------------
    IDAComponent** AssignComPtr(IDAComponent** Slot, IDAComponent* Value);
    void ReleaseComPtr(IDAComponent** Slot);
    // Typed Release-and-null for a smart-pointer member (the FUN_0620a010 /
    // FUN_06209ff0 / FUN_0620a030 / FUN_0620a0d0 family: release a non-null pointer, null
    // the slot).
    template <typename TInterface>
    void ReleaseDescriptorSlot(TInterface*& Slot)
    {
        if (Slot != nullptr)
        {
            Slot->Release();
            Slot = nullptr;
        }
    }

    // ---- Particle / math helpers -------------------------------------------
    FPooledParticle* ParticleFromUsedLink(FAppearancePool* Pool, FPoolLink* Link);
    void* EmitOneParticle(FAppearancePool* Pool, FGenericFxObjectBase* Appearance, const float* Six, float Age,
                          float Duration);
    void FillIdentity3x3(float* Nine);
    // 12-float FTransform (I3DMathEngine layout): Rotate (+0x30) / TransformXform (+0x28).
    void TransformDirection(const float* Xform12, float X, float Y, float Z, float* Out);
    void TransformPoint(const float* Xform12, float X, float Y, float Z, float* Out);
    void LerpVel3(float* Vel, const float* Target, float Alpha);
    void LerpVel3(float* Vel, double TargetX, float TargetY, float TargetZ, float Alpha);
    double AlchemyRand01(); // FUN_0623f1f0: the x87 product, unrounded
    // wave23 (sonnet:alchemy-wave23-lane1, 2026-09-11): Owner is the
    // appearance instance whose OWN active list (InstanceListHead_/
    // InstanceListTail_) this walks — see the .cpp definition and
    // decomp-tracker item 132111 for the full retail trace and the removal
    // of wave21's now-superseded (and, if reinstated, actively unsafe —
    // see Pad25's doc comment) ShouldRunPoolTick guard this replaces.
    void IntegratePool(FGenericFxObjectBase* Owner, FAppearancePool* Pool, const float& Dt);

    // ---- 3D-math / mesh-pose helpers ---------------------------------------
    I3DMathEngine* Create3DMath();
    I3DMathEngine* Acquire3DMath();
    ICamera* DrawCameraFromCookie();
    bool GetDrawCameraPosition(float* Out);
    const float* GetDrawCameraTransform();
    const float* GetDrawCameraOrientation();
    // wave48 (wave48-lane2, 2026-09-11): FUN_0622d2e0 (decomp-tracker item
    // 133933) -- wraps I3DMathEngine::GeneralInverse (vtable+0x5c, see
    // src/x86math/x86math.h) on a 12-float rest transform (this file's
    // Xform12 layout). See the .cpp definition for the full retail trace and
    // the three confirmed callers (Oriented/Perp/Rect's own per-appearance
    // "local rest space" setup).
    FVector* TransformOrientedBoundsCenter(const FTransform& Transform, FVector& Out,
                                           const FVector& Input);
    FVector* TransformPerpBoundsCenter(const FTransform& Transform, FVector& Out,
                                       const FVector& Input);
    FTransform* ComputeGeneralInverseTransform(const FTransform& Source,
                                              FTransform& Out, float& OutW);
    // 2026-09-26: the node-transform cache (FUN_062400b0 / FUN_062400f0 / FUN_062401a0,
    // node+0x4 flags, +0x1c raw, +0x4c composed, provider +0x80 -> Node_Transform) is
    // now FGenericFxObjectBase itself (GetNodeTransform / SetNodeTransform /
    // ComposeNodeTransform, alchemy_genericfx.h). The raw-offset helpers that stood in
    // for it here (GetAppearanceXformSlot, SetAppearanceXformSlot,
    // GetComposeMixinProvider, SetComposeMixinProvider, GetComposeAnimatedTransform,
    // ComposeAppearanceWorldTransform) had no callers and are gone, as are the mesh-pose
    // helpers (PoseOneMeshExtra2 / PoseMeshExtra2 / PoseAllMeshExtra2), replaced by
    // FxMeshAppearance::ComposeNodeTransform (FUN_06225240). The old compose helper
    // also had two defects: it multiplied by the unused first argument instead of the
    // parent, and composed the animated tail as Mul(Local, world) where retail
    // (0x062402dd) does Mul(world, Local).

    void* AllocPoolItem(FAppearancePool* Pool);
    void FreePoolItem(FAppearancePool* Pool, void* Item);
    void ReleaseAppearancePoolItem(FAppearancePool* Pool, void* Item);
    void AllocateAppearancePool(FAppearancePool* Pool, unsigned long Count, unsigned long Stride);
    void FreeAllAppearancePools();
    unsigned long MaxULong(unsigned long Left, unsigned long Right);
    void ComputeDrawBufferHints();

    // ---- Byte-bag helpers ---------------------------------------------------

    // ---- Generic COM/VB call-thunk helpers -----------------------------------
    GENRESULT CallIfaceQi(IDAComponent* Outer, const char* Iid, IDAComponent** Out);
    void ReleaseCachedIface(IDAComponent* Ptr);
    GENRESULT CallCreateVb(IDAComponent* Vb, unsigned int Fvf, unsigned int Count, void** Out,
                           unsigned int Flags);
    GENRESULT CallDestroyVb(IDAComponent* Vb, void** Handle);
    GENRESULT CallSelectVb(IDAComponent* Vb, void* Handle);
    GENRESULT CallGetRenderState(IRenderPipeline* Pipeline, int State, int* Out);
    GENRESULT CallSetRenderState(IRenderPipeline* Pipeline, int State, int Value);
    GENRESULT RecreateAlchemyVertexBuffer();
    void ClearPipelineCache();
    GENRESULT CallLockVb(IDAComponent* Vb, void* Handle, unsigned int* Offset, void** Out,
                         unsigned int Count);
    GENRESULT CallUnlockVb(IDAComponent* Vb, void* Handle);
    GENRESULT CallLockIb(IDAComponent* Ib, void* Handle, unsigned int* Offset, void** Out,
                         unsigned int Count);
    GENRESULT CallUnlockIb(IDAComponent* Ib, void* Handle);
    GENRESULT CallSelectIb(IDAComponent* Ib, void* Handle, unsigned int BaseVertexIndex);
    GENRESULT CallDrawIndexed(IDAComponent* Draw, unsigned long Prim, unsigned int MinIndex,
                              unsigned int NumVerts, unsigned int StartIndex, unsigned int Count);
    // FUN_06208640: Out = I3DMathEngine::MulTransform(A, B), 12-float transforms.
    void ComposeTransform(float* Out12, const float* A12, const float* B12);
    unsigned short AlSubmitVertex(FAlDrawContext& Context, float X, float Y, float Z,
                                  unsigned long Diffuse, float U, float V);
    void AlSubmitQuadIndices(FAlDrawContext& Context, unsigned short I0, unsigned short I1,
                             unsigned short I2, unsigned short I3);
    void AlSubmitTriangleIndices(FAlDrawContext& Context, unsigned short I0, unsigned short I1,
                                 unsigned short I2);
    bool AlDrawContextBegin(FAlDrawContext& Context, unsigned long Prim, unsigned long VertCount,
                            unsigned long IndexCount);
    unsigned long AlDrawContextEnd(FAlDrawContext& Context);
    GENRESULT AlDrawContextDraw(FAlDrawContext& Context);
    void ClearBatchLists();
    GENRESULT EnqueueAppearanceToList(unsigned long ListIndex, IDAComponent* Appearance);
    void FlushAppearanceBatches();
    unsigned long ParseAlchemyIniValue(char* Buffer, unsigned long Default);
    unsigned long ReadAlchemyIniValue(ICOManager* Manager, IProfileParser* Parser,
                                     const char* SectionName, const char* Key,
                                     unsigned long Default, unsigned long* Out);
    void ApplyAlchemyIniAndPools(ICOManager* Manager);

    // ---- ALE node-lib parsing helpers (shared by FAlchemy / FFxEffectArchetype
    //      / FFxEffectObject / FGenericFxObject) ------------------------------
    IGenericFx* QueryGenericFx(IDAComponent* Node);
    // FUN_0623f6b0 (cos -> Out[0], sin -> Out[1]); FUN_0623f590 when the CPU has SSE.
    void ComputeSinCosPair(float AngleRadians, float* OutCos, float* OutSin);
    using FnAlchemySinCos = void (*)(float, float*);
    extern FnAlchemySinCos G_AlchemySinCos;
    void SelectAlchemyMathImplementation();
    // bLiveItemsOnly: apply only to items with Age < Duration (the particle appearance's
    // inline walk, 0x0623046b); every other retail copy applies to every listed item.
    void ApplyFieldsToPool(FGenericFxObjectBase* Owner, bool bLiveItemsOnly);
    // Direct node-library readers; mutations and COM cleanup follow each chunk read.
    int ReadRampSingleBlock(FAleChunk* Chunk, IRampSingle* Target);
    int ReadRampColorBlock(FAleChunk* Chunk, IRampColor* Target);
    int ReadAnimatedSingleBlock(FAleChunk* Chunk, IFxAnimatedSingle* Target);
    int ReadAnimatedSingleRamp(FAleChunk* Chunk, INestedSingleRamp* Outer,
                               unsigned long VersionBits);
    int ReadAnimatedTransform(FAleChunk* Chunk, IAnimatedTransform* Anim,
                              unsigned long VersionBits);
    void CallAssociateAppearance(IGenericFx* Sink, IDAComponent* Appearance);
    void CallAssociateField(IGenericFx* Sink, IDAComponent* Field);
    IGenericFx* AsFxNode(void* Node);
    IDAComponent* CallGetAttachParent(void* Node);
    IDAComponent* CallGetFirstChild(void* Node);
    IDAComponent* CallGetPrevSibling(void* Node);
    IDAComponent* CallGetNextSibling(void* Node);
    int CallGetSortKey(void* Node);
    void CallSetParent(IDAComponent* Node, IDAComponent* Parent);
    void CallSetFirstChild(IDAComponent* Node, IDAComponent* Child);
    void CallSetPrevSibling(IDAComponent* Node, IDAComponent* Prev);
    void CallSetNextSibling(IDAComponent* Node, IDAComponent* Next);
    void LinkNodeTree(IDAComponent* Parent, IDAComponent* Child);
    GENRESULT UnlinkNodeTree(IDAComponent* Node);
    unsigned long RemapNodeLibHash(unsigned long Hash);
    unsigned long RemapNodeLibKind(unsigned long Kind, unsigned long VersionBits);
    // The node library's property stream: the loader's loop over FUN_062291a0 and the
    // save's per-node body of FUN_062281c0 (class name, LAB_06228350 per row, zero type).
    int ReadNodeLibProperties(FAleChunk* Chunk, IFxNodePropertySet* Set, unsigned long VersionBits);
    int WriteNodeLibProperties(FAleChunk* Chunk, IDAComponent* Node);

} // namespace AlchemyImpl
