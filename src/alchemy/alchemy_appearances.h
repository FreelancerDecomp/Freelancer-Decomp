// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "alchemy_genericfx.h"

// alchemy.dll — DACOM particle FX scaffolding.
//
// The 8 "generic" appearance leaves (RegisterComponent names FxBasicAppearance/
// FxParticleAppearance/FxRectAppearance/FxPerpAppearance/FxMeshAppearance/
// FxOrientedAppearance/FLDustAppearance/FLBeamAppearance — docs/re/alchemy.md's
// RegisterComponent table, entries #26-#33).
//
// ALC-6 (2026-09-30): every appearance's properties live in its retail property set
// (alchemy_types.h): the six billboards in an FAppearanceRampDescriptor behind
// FFxBillboardAppearance's table-driven slots, particle and mesh in their own sets. Each
// leaf creates its set in CreatePropertySet (node slot +0x48, run by
// InitializeFromFactory); clones share the archetype's (FUN_0623fc30).
//
// === Wave 17 (alchemy-wave17-lane1, 2026-09-10): Cluster A private
// particle-tracking list ===
// Waves 14-16 traced a real, unported retail mechanism (decomp-tracker
// 0x0623a000/a070/a2e0/a360/a4a0/06239f80/06239f00/06238780/0623b410) but
// left its exact shape ambiguous ("a third, separate {Head,Tail,Pool} list"
// distinct from the one FUN_0623b410 populates). This wave resolved that:
// FUN_06238780 is dispatched through the appearance's OWN embedded
// sub-object vtable (PTR_FUN_0624eee8, assigned at object+0xC — see
// FUN_06238050's ctor), so its `this` parameter (called `param_1` in the
// decomp) is object+0xC, not the outer object. Its "+0xd8" field reference
// therefore resolves to object+0xC+0xD8 = object+0xE4 — the EXACT SAME
// {Head,Tail,Pool,RefCount} quad FUN_0623b410/FUN_0623a2e0 populate at
// object+0xE4/0xE8/0xEC/0xF0. There is only ONE list, not three:
//   - Ctor (FUN_06238050/06215d80/0622bb10/06232950): zero-inits the quad,
//     Pool = the type's own already-shared FAppearancePool global
//     (DAT_06257b68/a04/a9c/b14 == this port's G_PoolRect/Basic/Oriented/
//     Perp — confirmed by matching FUN_0623ebc0(&DAT_...,...,stride) init
//     call sites, same globals, same strides 0x30/0x30/0x30/0x3c).
//   - Spawn (FUN_0623b410 -> FUN_0623a2e0): AllocPoolItem on that pool,
//     copy 6 floats (pos+vel) + Age/Duration/InvDuration (FPooledParticle
//     layout, stride 0x30/0x3c — confirmed, not kStrideParticle's 0x74),
//     `*(Appearance+0xE4) += 1` (the SAME quad's RefCount word, i.e. an
//     owner-refcount on `this`), link onto Head/Tail via the item's own
//     Next/Prev (FPooledParticle+0x0/+0x4 — unused by this port's
//     AllocPoolItem/FreePoolItem, confirmed free for this purpose).
//   - Erase (FUN_06239f80/06239f00): unlink, `*(Appearance+0xE4) -= 1`,
//     FreePoolItem back to the SAME pool.
//   - Age/drive (FUN_06238780 -> FUN_0623a4a0): once per frame, walk the
//     list, Age+=Dt, Pos+=Vel*Dt while Age<=Duration, else erase.
// CONFIRMED PRESENT (byte-identical ctor pattern) on: FxRectAppearance
// (FUN_06238050), FxBasicAppearance (FUN_06215d80), FxOrientedAppearance
// (FUN_0622bb10), FxPerpAppearance (FUN_06232950).
// CONFIRMED ABSENT on the other 4 (see each class below for its own
// evidence): FxMeshAppearance, FxParticleAppearance (both show an
// unrelated, smaller 3-word {link,?,Pool} ctor pattern with no RefCount —
// Cluster B's per-instance child-object pool, already documented in
// alchemy_genericfx.cpp above EmitOne, not this mechanism), FLDustAppearance
// and FLBeamAppearance (both have their OWN distinct, unrelated function at
// vtable slot 47 — neither calls FUN_0623a2e0 at all).
//
// CORRECTED 2026-09-26: wave17's own finding above -- one list -- was right, and
// the port then built two anyway: Pad47 spawned into a per-leaf PrivateHead_ list
// that no draw path walked, while DrawOrEnqueue drew InstanceListHead_, which
// nothing but the Beam spawned into. The private list, its helpers
// (PrivateListSpawn/Erase/Clear/Age) and OnAfterIntegrate are gone. The one list
// is FGenericFxObjectBase::InstanceListHead_/Tail_/LiveInstanceCount_ (node+0xd8/
// +0xdc/+0xe4), spawned by SpawnListParticle and aged by UpdateListAppearance from
// each leaf's own Pad5 (see alchemy_genericfx.cpp). Everything from the PORT NOTE
// down to the wave66 audit describes the removed mechanism and is kept only as
// history.
//
// PORT NOTE (fixed alchemy-wave20-lane1, 2026-09-11): the 4 classes below
// previously used DEDICATED heap allocation (new/delete per node) instead of
// carving items out of the shared per-type FAppearancePool arena the way
// retail's FUN_0623a2e0 literally does, because sharing the real pool
// without safeguards would let this port's own IntegratePool (which walks
// every item reachable from Pool->UsedHead once per frame doing the
// IDENTICAL Age+=Dt / Pos+=Vel*Dt math as FUN_0623a4a0) double-integrate a
// tracked particle or free it out from under the private list's own
// Head_/Tail_/Next/Prev pointers. Wave 19 (alchemy_common.h/.cpp) closed
// that gap with AllocPrivatePoolItem/FreePrivatePoolItem — they draw/return
// a slot from the SAME shared arena (G_PoolBasic/G_PoolRect/G_PoolOriented/
// G_PoolPerp below, matching retail bytes) while excising the node from
// Pool->UsedHead on allocation so IntegratePool's sweep can never see or
// double-free it. This wave wired PrivateListSpawn/Erase/Clear/Age
// (alchemy_appearances.cpp) through those two functions instead of
// new/delete, passing each leaf's own already-shared pool global. Retail-
// accurate allocation source, no more dedicated heap traffic.
//
// SPAWN ENTRY POINT (WIRED UP wave 18, alchemy-wave18-lane1, 2026-09-10):
// retail's only real trigger for inserting into this list is FUN_0623b410,
// which lives at IGenericFx vtable slot 47 (+0xBC) — this port's `Pad47()`.
// Wave 17 assumed Pad47's real signature was (this, const float* Six, float
// Duration) — 2 args beyond `this` — but a byte-level re-check of
// FUN_0623b410 found RET 0x10 (4 stack dwords, i.e. `this` + 3 args, not 2):
// Ghidra's own decompiled prototype for FUN_0623b410 only lists 3 params
// (param_1/2/3) but its own RET count proves a 4th exists that Ghidra's
// stack-frame recovery missed. Tracing both args into FUN_0623a2e0's field
// writes against this port's own FPooledParticle layout (Age@+0x24,
// Duration@+0x28, InvDuration@+0x2c — alchemy_types.h) resolved it: the
// ORIGINAL 3rd arg (param_3) lands at item+0x24 == Age, and the actual 4th
// arg (the one Ghidra dropped) is what FUN_0623a2e0's `FLD 1.0 / FDIV`
// divides by and stores raw at item+0x28 == Duration. So the real signature
// is `Pad47(const float* Six, float Age, float Duration)` — retail lets the
// spawning caller seed a non-zero initial Age, not just a Duration. Wave 17's
// SpawnPrivateInstance(Six, Duration) hardcoded Age=0.0f; this wave adds the
// Age parameter through Pad47 -> SpawnPrivateInstance -> PrivateListSpawn
// (alchemy_appearances.cpp) so a caller-supplied non-zero Age is no longer
// silently dropped. Pad47 is now overridden on the 4 leaves below to call
// SpawnPrivateInstance directly — the mechanism is reachable from real COM
// dispatch as of this wave.
//
// === Wave 66 (sonnet:alchemy-wave66-lane2, 2026-09-11): fresh skeptical
// re-audit applying post-wave-40 bug classes ===
// Re-derived, from raw bytes with no reliance on prior comments/statuses,
// the 3 specific bug classes this marathon has since cracked elsewhere:
//   1. Release()-reentrancy-guard (waves 59-63, missing in 7/9 classes
//      checked): neither this header nor alchemy_appearances.cpp declares
//      an OWN Release() override for any of the 8 classes here — all 8
//      inherit FGenericFxObjectBase::Release() (fixed wave63). Independently
//      verified this is retail-correct, not an inherited gap, by tracing 2
//      of the 8 leaves' own COMPILED Release()s directly: FUN_06218630
//      (FxBasicAppearance's real vtable slot 2, PTR_LAB_0624cc40, single
//      XREF) and FUN_0623a190 (FxRectAppearance's, PTR_LAB_0624f008, single
//      XREF) are BOTH byte-identical to the base class's current (post-
//      wave63) underflow-guard + reentrancy-guard pattern. Retail generates
//      a mechanically duplicated Release() per concrete leaf class (same
//      family already sampled via FUN_06202f60 for the wave63 fix itself);
//      this wave adds 2 more independent samples confirming that
//      generalization holds for this file's own classes specifically (see
//      decomp-tracker comments on 0x06218630/0x0623a190). No override
//      needed — C++ virtual dispatch through the shared base already
//      reproduces retail's per-class duplication exactly, since RefCount_
//      lives at a fixed base-class offset regardless of leaf type.
//   2. Calling-convention miscalibration (the FxCollideField/Oriented-
//      fStack_164 technique): re-derived FUN_0623b410's (Pad47's retail
//      entry point) raw x86 byte-by-byte from the .txt listing (not the
//      .c decomp, which undercounts its own stack args) — RET 0x10, 4
//      explicit stack dwords (this + Six-pointer + 2 float scalars) with
//      NO register-passed `this`, i.e. a plain STDMETHOD-style stack-only
//      call. Cross-traced both scalar's placement into FUN_0623a2e0's own
//      field writes (item+0x24/+0x28) to confirm which stack slot is Age
//      vs. Duration. Result matches the existing Pad47(Six,Age,Duration)
//      signature and PrivateListSpawn/PrivateListErase/PrivateListAge
//      exactly, field for field — same conclusion wave40's own independent
//      fresh re-audit already reached (decomp-tracker items 134776/134659/
//      134650/134661/134649, all dated 2026-09-11). No discrepancy found
//      across 2 independent re-derivations.
//   3. AddRef-on-accessor (waves 43-44, re-checked here for the wave57
//      fields specifically): neither EffectScaleRamp_ (FxMeshAppearance/
//      FxParticleAppearance) nor AxisOverrideRamp (Descriptor_, built by
//      the per-leaf descriptor builders) has any public accessor
//      declared anywhere in this file — both are opaque, private,
//      construction/teardown-only fields with no GetXxx() to leak a ref
//      through. Bug class does not apply; nothing to fix.
// No bugs found. No code changed this wave — every mechanism this file
// owns (Cluster A private list, Pad47, the 2 wave57 standalone ramps, the
// 2 wave55/56 AxisOverrideRamp ctors) re-verified byte-clean from scratch.
//
// === Wave 76 (sonnet:alchemy-wave76-lane1, 2026-09-11): ComposeAppearance-
// WorldTransform (FUN_062401a0, alchemy_common.cpp/.h, decomp-tracker item
// 135093) — exhaustive in-module call-site search, CLOSED NEGATIVE ===
// Wave 75 confirmed the real compose body and identified exactly 6 classes
// (all declared in THIS file — FLBeamAppearance/FLDustAppearance included;
// neither lives in alchemy_fields.cpp) whose own private "descriptor+4"
// helper installs FUN_062401a0 at vtable+0x18/+0x60, but left "wire it to a
// live call site" as future work. This wave searched for that call site and
// found there ISN'T one anywhere in alchemy.dll: each of the 6 classes' own
// vtable (PTR_FUN_0624b658/c040/cac8/e160/e858/ee90) has exactly ONE XREF in
// the raw listing — the owning class's own constructor writing `*this =
// &PTR_FUN_...` at construction — and zero other references anywhere in the
// 160k-line disassembly. A vtable pointer installed once and never read
// again is never locally dispatched. (The only 2 genuine direct CALLs to
// FUN_062401a0 in the whole binary belong to FxMeshAppearance's
// PoseOneMeshExtra2 chain and FxParticleAppearance's EmitOne tail — neither
// is one of these 6 classes, both already separately covered.) Conclusion:
// this compose method is a pure COM-exposed callback for a caller OUTSIDE
// alchemy.dll (presumably a scene-graph/animation system in another module
// that QueryInterfaces the appearance or its aggregated descriptor+4
// sub-object) — there is no retail call for these 6 leaves' own port methods
// to reproduce. Deliberately left UNWIRED rather than inventing a
// ParentXform12 source/call site with no retail backing. See item 135093's
// own wave76 note for the full XREF-line citations. Not re-openable by a
// future wave without new evidence from a DIFFERENT module.
namespace AlchemyImpl
{
    __declspec(noinline) double ForwardAppearanceSingleEval(
        INestedSingleRamp* Ramp, const float& OuterTime, const float& InnerTime);
    __declspec(noinline) unsigned long ForwardAppearanceColorEval(
        IRampColorRamp* Ramp, const float& OuterTime, const float& InnerTime);
    __declspec(noinline) float* ForwardAppearanceRgbEval(
        IRampColorRamp* Ramp, float* Out, const float& OuterTime, const float& InnerTime);
    __declspec(noinline) void ForwardAppearanceSinCos(const float& Angle, float* Output);

    // One case of a billboard set's kind 0x200 (+0x3c) or 0x202 (+0x40) getter: the hash
    // and the set field it hands out, AddRef'd.
    struct FBillboardRampSlot
    {
        unsigned long Hash;
        INestedSingleRamp* FAppearanceRampDescriptor::*Field;
    };

    // What one billboard set class (basic, rect, perp, oriented, dust, beam) knows about
    // its properties; the six sets share every slot body but these tables. Filled per
    // class in alchemy_appearances.cpp from the listing.
    struct FBillboardPropertyClass
    {
        // +0x04 / +0x08: the Flags (+0x60) bit table (FFxBillboardPropertySet::GetPropBool).
        const FDescriptorFlagBit* Flags;
        unsigned long FlagCount;
        // +0x3c: first match wins; no match -> FUN_06244330 (Appearance_LODCurve).
        const FBillboardRampSlot* SingleRamps;
        unsigned long SingleRampCount;
        // +0x40: first match wins; no match -> -8.
        const FBillboardRampSlot* AnimatedRamps;
        unsigned long AnimatedRampCount;
        // +0x50: this hash -> LocalTransform (+0x8c) AddRef'd, any other -> FUN_0623fb50;
        // 0 = the class's +0x50 is FUN_0623fb50 itself.
        unsigned long LocalTransformHash;
        // +0x54: the class table walked after the base one (FUN_06244320).
        const FNodePropertyRow* Rows;
    };

    // The six billboard set classes (FAppearanceRampDescriptor's layout): each retail set
    // class's slots are the same bodies over its own tables (e.g. basic, outer 0x0624cd20 /
    // inner 0x0624cac8: +0x04 FUN_06215930, +0x08 FUN_06215a00, +0x1c FUN_06218b60, +0x20
    // FUN_06218b90, +0x30 FUN_06218c60, +0x34 FUN_06218c90, +0x38 FUN_06218bf0, +0x3c
    // FUN_06215b50, +0x40 FUN_06215900, +0x50 FUN_0623fb50, +0x54 FUN_06218cd0; the outer
    // +0x24 / +0x28 / +0x3c / +0x40 / +0x44 are own `MOV EAX,-8` bodies). Int (inner
    // +0x0c / +0x10) is -8 and float (+0x14 / +0x18) the base set's in all six.
    class FFxBillboardPropertySet final : public FAppearanceRampDescriptor
    {
    public:
        explicit FFxBillboardPropertySet(const FBillboardPropertyClass* PropertyClass);

        GENRESULT DACOM_API GetPropBool(unsigned long Hash, unsigned char* Out) override;
        GENRESULT DACOM_API SetPropBool(unsigned long Hash, unsigned char Value) override;
        GENRESULT DACOM_API GetPropPair(unsigned long Hash, unsigned long* Out2) override;
        GENRESULT DACOM_API SetPropPair(unsigned long Hash, const unsigned long* In2) override;
        GENRESULT DACOM_API GetPropName(unsigned long Hash, const char** Out) override;
        GENRESULT DACOM_API SetPropName(unsigned long Hash, const char* Text) override;
        GENRESULT DACOM_API GetPropColorRampRamp(unsigned long Hash, IRampColorRamp** Out) override;
        GENRESULT DACOM_API GetPropSingleRampRamp(unsigned long Hash,
                                                  INestedSingleRamp** Out) override;
        GENRESULT DACOM_API GetPropAnimatedSingleRamp(unsigned long Hash,
                                                      INestedSingleRamp** Out) override;
        GENRESULT DACOM_API GetPropTransform(unsigned long Hash, IAnimatedTransform** Out) override;
        int DACOM_API EnumerateProps(FNodePropertyRowCallback Callback, void* Context) override;

    private:
        const FBillboardPropertyClass* PropertyClass_;
    };

    // The six billboard appearances: the node part. Descriptor_ is PropertySet_ typed.
    class FFxBillboardAppearance : public FGenericFxObjectBase
    {
    public:
        FFxBillboardAppearance(const char* PrimaryIid, const char* FactoryName,
                               const FBillboardPropertyClass* PropertyClass);

    protected:
        void OnPropertySetChanged() override;
        // The six creators' shared body (see alchemy_appearances.cpp): a new set of this
        // class, filled by the class's builder, adopted.
        void AdoptBillboardSet(void (*Build)(FAppearanceRampDescriptor&));

    private:
        const FBillboardPropertyClass* PropertyClass_;
    };

    // Set FUN_06215570 (create FUN_06215e80), tables in alchemy_appearances.cpp.
    //
    // List-driven (see file header): particles live on the node's one
    // InstanceListHead_/Tail_ list; GetBoundingSphere is FUN_06216090.
    class FxBasicAppearance final : public FFxBillboardAppearance
    {
    public:
        FxBasicAppearance(const char* PrimaryIid, const char* FactoryName);
        ~FxBasicAppearance() override;

        GENRESULT DACOM_API Pad47(const float* Six, float Age, float Duration) override;

        // FUN_06217640: draw now, or queue on batch list 0 when the material batcher is on.
        GENRESULT DACOM_API DrawOrEnqueue() override;
        // Outer +0xA4, LAB_062194b0 -> LAB_06217a60: the batch flush's per-appearance fill.
        GENRESULT DACOM_API FillBatchedGeometry() override;
        // Outer +0x9C FUN_06219390 / +0xA0 FUN_062193b0: batch begin, and end-and-draw.
        GENRESULT DACOM_API BeginBatchedDraw(unsigned long Verts, unsigned long Indices) override;
        GENRESULT DACOM_API EndBatchedDraw() override;

        // Outer +0x14 (FUN_06216420), +0x8C (FUN_06216090). See the .cpp.
        GENRESULT DACOM_API Pad5(float Dt) override;
        GENRESULT DACOM_API GetBoundingSphere(float* OutSphere) override;

        // FUN_06215d80 (outer thunk 06218df0).
        GENRESULT DACOM_API CloneNode(IDAComponent** Out) override;

    protected:
        // Node slot +0x48 FUN_06215e80: the set FUN_06215570.
        void CreatePropertySet() override;

    private:
        // FUN_06216550 (descriptor flag 2 set) and FUN_06216f20 (clear): one camera-space
        // quad / triangle per particle of this appearance's list into the open draw context.
        void FillQuadGeometry();
        void FillTriangleGeometry();
    };

    // Set: FFxParticleAppearancePropertySet (create FUN_0622f550, set vtable 0x0624e790).
    //
    // Cluster A (see file header): CONFIRMED ABSENT (alchemy-wave17-lane1,
    // 2026-09-10). Real ctor is FUN_0622f430/FUN_0622f480 (operator_new
    // 0xD4, vtable PTR_FUN_0624e530 — the class-descriptor vtable
    // immediately following Particle's own factory vtable PTR_LAB_0624e4c8
    // at CreateInstance LAB_06230c60). It zeros only 3 words
    // (param_1[0x2d..0x2f], NOT 6) with Pool at the LAST word
    // (param_1[0x2f] = &DAT_06257ac4 — stride 0x74, confirmed == this
    // port's own kStrideParticle/G_PoolParticle). No RefCount word exists.
    // This matches Cluster B's per-instance child-object-pool shape (see
    // alchemy_genericfx.cpp above EmitOne: "this[0x2f] private pool /
    // this+0x2d list sentinel"), not Cluster A.
    //
    // ALC-1 2026-09-26: the class is ported. Each "particle" is a 0x74 record from
    // G_PoolParticle on this node's list (retail node+0xb4/+0xb8, pool +0xbc) that owns
    // two child EFFECTS created through IFxEffectLibrary::CreateEffect: the life effect
    // (ParticleApp_LifeName, +0x6c) at spawn and the death effect (ParticleApp_DeathName,
    // +0x70) once the record has aged out. The appearance draws nothing itself (outer
    // +0x18 FUN_06231de0 returns 0): the children are ordinary effects. Outer vtable
    // 0x0624e650, node (this+0xc) vtable 0x0624e530.
    // A G_PoolParticle record (stride 0x74). FUN_06231d30 fills +0x08..+0x2c and
    // +0x6c/+0x70, the spawn FUN_0622f6a0 +0x30..+0x68.
    struct FParticleAppRecord
    {
        FPooledParticle Base;   // Next, Prev, owner node, Six, Age, Duration, 1/Duration
        // Raw spawn words; the complete field Apply contract exposes only +0x0c..+0x27.
        // Particle's own operations never consume these six seeded words.
        std::uint32_t UnusedSpawnWords_0x30[3];
        float BlendQuat[4];     // +0x3c, w x y z: the SmoothRotation slerp state
        std::uint32_t UnusedSpawnWords_0x4c[3];
        float VelQuat[4];       // +0x58, w x y z: orientation from the velocity
        float Blend;            // +0x68
        IFxEffect* LifeEffect;  // +0x6c
        IFxEffect* DeathEffect; // +0x70
    };
    static_assert(sizeof(FParticleAppRecord) == 0x74, "retail particle record stride");

    class FxParticleAppearance final : public FGenericFxObjectBase
    {
    public:
        FxParticleAppearance(const char* PrimaryIid, const char* FactoryName);
        ~FxParticleAppearance() override;

        // Outer +0x10 FUN_06231da0 -> FUN_0622fcf0: death effects, then FUN_062401a0.
        GENRESULT DACOM_API ComposeNodeTransform(void* Unused, const float* Parent12) override;
        // Outer +0x14 FUN_06231dc0 -> FUN_06230370: age, fields, records.
        GENRESULT DACOM_API Pad5(float Dt) override;
        // Outer +0x5C FUN_06231f40 -> FUN_0622f8b0 / +0x88 FUN_06232090 -> FUN_0622f960.
        GENRESULT DACOM_API SetNodeOuterTime(float OuterTime) override;
        GENRESULT DACOM_API SetNodeLodInput(float LodInput) override;
        // Outer +0x8C FUN_062320b0 -> FUN_0622fa10: the children's bounds merged.
        GENRESULT DACOM_API GetBoundingSphere(float* OutSphere) override;
        // Outer +0xBC FUN_06232270 -> FUN_0622f6a0: spawn one record + life effect.
        GENRESULT DACOM_API Pad47(const float* Six, float Age, float Duration) override;

        // FUN_0622f480 (outer thunk 06231e10).
        GENRESULT DACOM_API CloneNode(IDAComponent** Out) override;

    protected:
        // Node slot +0x48: FUN_0622f550. The set's slots (inner 0x0624e790) are
        // FFxParticleAppearancePropertySet's (alchemy_types.h).
        void CreatePropertySet() override;
        void OnPropertySetChanged() override;

    private:
        static void ForwardEffectFloat(IFxEffect* Effect,
                                       GENRESULT (DACOM_API IFxEffect::*Method)(float),
                                       const float& Value);
        void ForwardChildFloat(GENRESULT (DACOM_API IFxEffect::*Method)(float),
                               const float& Value);

        FFxParticleAppearancePropertySet* ParticleProps() const
        {
            return ParticlePropertySet_;
        }

        // Node +0xc0: borrowed typed cache; clones read the source after callbacks.
        FFxParticleAppearancePropertySet* ParticlePropertySet_;

        // FUN_0622ff00 (node this, record, Dt): move, orient, hand the life effect its
        // transform.
        __declspec(noinline) void IntegrateRecord(FPooledParticle* Record, const float& Dt);
        // FUN_062306e0 on the node's list: unlink, tear the record down (FUN_06230600,
        // inside FreePoolItem) and give it back to the pool. Returns the next record.
        FPooledParticle* EraseRecord(FPooledParticle* Record);
    };

    // Set FUN_06237840 (create FUN_06238150).
    //
    // List-driven (see file header). GetBoundingSphere is FUN_06238280, the side-length
    // member of the list-bounds family (ListBoundsSphere, alchemy_appearances.cpp): the
    // radius is the larger side ramp (+0x7c / +0x80, B > C ? B : C) times the
    // +0x78 ramp times the EffectScaleRamp value. (wave90's reading -- a discarded
    // SizeRamp and a node+0x8c scale -- is superseded by that trace.)
    class FxRectAppearance final : public FFxBillboardAppearance
    {
    public:
        FxRectAppearance(const char* PrimaryIid, const char* FactoryName);
        ~FxRectAppearance() noexcept(false) override;

        GENRESULT DACOM_API Pad47(const float* Six, float Age, float Duration) override;

        // FUN_062388b0: draw now, or queue on batch list 1 when the material batcher is on.
        GENRESULT DACOM_API DrawOrEnqueue() override;
        // Outer +0x9C LAB_0623b140, +0xA0 FUN_0623b1a0, +0xA4 FUN_0623b2a0 (-> FUN_06238a70).
        GENRESULT DACOM_API BeginBatchedDraw(unsigned long Verts, unsigned long Indices) override;
        GENRESULT DACOM_API EndBatchedDraw() override;
        GENRESULT DACOM_API FillBatchedGeometry() override;

        // Outer +0x14 (FUN_06238780), +0x8C (FUN_06238280). See the .cpp.
        GENRESULT DACOM_API Pad5(float Dt) override;
        GENRESULT DACOM_API GetBoundingSphere(float* OutSphere) override;
        // FUN_0623b2c0, outer +0xA8: fixed unsupported result, no side effects.
        GENRESULT DACOM_API Pad42() override { return static_cast<GENRESULT>(0xfffffff9); }

        // FUN_06238050 (outer thunk 0623ac80).
        GENRESULT DACOM_API CloneNode(IDAComponent** Out) override;

    protected:
        // Node slot +0x48 FUN_06238150: the set FUN_06237840.
        void CreatePropertySet() override;

    private:
        // FUN_06238a70: one node-local quad per particle into the open draw context.
        void FillGeometry();
    };

    // Set FUN_062323a0 (create FUN_06232b00).
    //
    // List-driven (see file header); GetBoundingSphere is FUN_06232cc0.
    //
    // wave68 (sonnet:alchemy-wave68-lane1, 2026-09-11): real override of
    // the new port-only IGenericFx::DrawOrEnqueue slot (alchemy_types.h),
    // reproducing FUN_06233170's own per-instance draw dispatch (decomp-
    // tracker item 134306). wave69 (sonnet:alchemy-wave69-lane2,
    // 2026-09-11) closed the two pieces wave68 left open: the
    // material-batcher gate (DAT_06257919 == already-ported
    // G_Defaults.bUseMaterialBatcher, IAlchemy::SetUseMaterialBatcher/
    // GetUseMaterialBatcher) and the DIP Begin/Fill/End/state/Draw
    // lifecycle (confirmed fully self-contained inside retail's own
    // function, wired via this port's existing AlDrawContextBegin/End/Draw).
    // See the doc comment on this override (alchemy_appearances.cpp) for
    // the full byte trace. DrawOrEnqueue is IGenericFx +0x18 (outer thunk
    // FUN_06235700 @0624e9e8), reached from the IAlchemy draw walk LAB_06201910.
    class FxPerpAppearance final : public FFxBillboardAppearance
    {
    public:
        FxPerpAppearance(const char* PrimaryIid, const char* FactoryName);
        ~FxPerpAppearance() override;

        GENRESULT DACOM_API Pad47(const float* Six, float Age, float Duration) override;
        // FUN_06233170: draw now, or queue on batch list 2 when the material batcher is on.
        GENRESULT DACOM_API DrawOrEnqueue() override;
        // Outer +0x9C FUN_06235a40, +0xA0 FUN_06235a60, +0xA4 FUN_06235b40 (-> FUN_06233330).
        GENRESULT DACOM_API BeginBatchedDraw(unsigned long Verts, unsigned long Indices) override;
        GENRESULT DACOM_API EndBatchedDraw() override;
        GENRESULT DACOM_API FillBatchedGeometry() override;

        // Outer +0x14 (FUN_06233040), +0x8C (FUN_06232cc0). See the .cpp.
        GENRESULT DACOM_API Pad5(float Dt) override;
        GENRESULT DACOM_API GetBoundingSphere(float* OutSphere) override;

        // FUN_06232a00 (outer thunk 06235730).
        GENRESULT DACOM_API CloneNode(IDAComponent** Out) override;

    protected:
        // Node slot +0x48 FUN_06232b00: the set FUN_062323a0.
        void CreatePropertySet() override;

    private:
        // FUN_06233330: one node-local quad per particle into the open draw context.
        void FillGeometry();
    };

    // Set: FFxMeshAppearancePropertySet (FUN_06224710, create FUN_06224b60, set vtable
    // 0x0624dff0). Overrides the node compose (FUN_06225240)
    // to pose each instance's IFxMesh after composing its own transform; that
    // is the only place retail poses the meshes.
    //
    // Cluster A (see file header): CONFIRMED ABSENT (alchemy-wave17-lane1,
    // 2026-09-10). Real ctor is FUN_06224a40/FUN_06224a90 (operator_new
    // 0xD4, vtable PTR_FUN_0624dd90 — the class-descriptor vtable following
    // Mesh's factory vtable PTR_LAB_0624dd24 at CreateInstance
    // LAB_06225e50). Same reduced 3-word {link,?,Pool} pattern as
    // FxParticleAppearance above (param_1[0x2d..0x2f], Pool =
    // param_1[0x2f] = &DAT_06257a48, stride 0x34, == this port's
    // G_PoolMesh) — no RefCount word. Cluster B shape, not Cluster A.
    class FxMeshAppearance final : public FGenericFxObjectBase
    {
    public:
        FxMeshAppearance(const char* PrimaryIid, const char* FactoryName);
        ~FxMeshAppearance() override;

        // FUN_06224c20 (vtable 0x0624dd90 +0x68): the mesh emit-one. Asks the bound
        // IFxMeshFactory (DAT_06257b9c) for the particle's IFxMesh; see the body.
        GENRESULT DACOM_API EmitOne(const float* Six, unsigned long Extra,
                                    float Duration) override;
        // Outer +0xBC is FUN_06227330, the this+0xc thunk into FUN_06224c20 -- the
        // body EmitOne above holds. Until 2026-09-26 +0xBC fell through to the
        // base default, so emitters never reached the mesh spawn.
        GENRESULT DACOM_API Pad47(const float* Six, float Age, float Duration) override
        {
            unsigned long AgeBits = 0;
            std::memcpy(&AgeBits, &Age, sizeof(AgeBits));
            return EmitOne(Six, AgeBits, Duration);
        }
        // Outer +0x14 -> FUN_06225470. See the .cpp.
        GENRESULT DACOM_API Pad5(float Dt) override;

        // FUN_06225240 (node vtable 0x0624dd90 slot 2; outer thunk FUN_06226e60,
        // this+0xc): FUN_062401a0, then pose every instance's IFxMesh.
        GENRESULT DACOM_API ComposeNodeTransform(void* Unused, const float* Parent12) override;
        // Outer +0x18 FUN_06226ea0 -> FUN_062256a0: each instance's IFxMesh draws itself.
        GENRESULT DACOM_API DrawOrEnqueue() override;

        // FUN_06224a90 (outer thunk 06226ed0).
        GENRESULT DACOM_API CloneNode(IDAComponent** Out) override;

        // FUN_06225000 (inner +0x34), raw center/radius output.
        GENRESULT DACOM_API GetBoundingSphere(float* OutSphere) override;

    protected:
        // Node slot +0x48: FUN_06224b60. The set's slots (inner 0x0624dff0) are
        // FFxMeshAppearancePropertySet's (alchemy_types.h).
        void CreatePropertySet() override;

    private:
        FFxMeshAppearancePropertySet* MeshProps() const
        {
            return MeshPropertySet_;
        }

        FFxMeshAppearancePropertySet* MeshPropertySet_; // inner +0xc0, non-owning cache.
    };

    // Set FUN_0622b3e0 (create FUN_0622bcc0).
    //
    // List-driven (see file header); GetBoundingSphere is FUN_0622bed0. (wave108
    // concluded this class had no bounds function because it found no FSQRT in the
    // class's range. None of the five copies uses FSQRT: the distance is the
    // 0x5f3759df reciprocal square root FUN_06206b30 times LenSq.)
    class FxOrientedAppearance final : public FFxBillboardAppearance
    {
    public:
        FxOrientedAppearance(const char* PrimaryIid, const char* FactoryName);
        ~FxOrientedAppearance() override;

        GENRESULT DACOM_API Pad47(const float* Six, float Age, float Duration) override;

        // FUN_0622c500: one function that draws every particle of the node immediately
        // (oriented has no material-batcher branch -- the four FUN_06201c70 call sites
        // are basic/rect/perp/beam). See the .cpp.
        GENRESULT DACOM_API DrawOrEnqueue() override;
        // Outer +0x14 (FUN_0622c3d0), +0x8C (FUN_0622bed0 via FUN_0622eca0). See the .cpp.
        GENRESULT DACOM_API Pad5(float Dt) override;
        GENRESULT DACOM_API GetBoundingSphere(float* OutSphere) override;

        GENRESULT DACOM_API GetQueuedVertCount(unsigned long* Out) override;

        GENRESULT DACOM_API GetQueuedIndexCount(unsigned long* Out) override;

        // FUN_0622bbc0 (outer thunk 0622ea00).
        GENRESULT DACOM_API CloneNode(IDAComponent** Out) override;

    protected:
        // Node slot +0x48 FUN_0622bcc0: the set FUN_0622b3e0.
        void CreatePropertySet() override;
    };

    // FLDustAppearance. CORRECTED 2026-09-26 (ALC-1): the outer ctor installs
    // PTR_LAB_0624c1b8 (0x0620f651) with the node vtable PTR_FUN_0624c098 at outer+0xc
    // (node ctor FUN_0620f4e0). Outer +0x14 FUN_06212220 -> node FUN_0620fb70 (update),
    // outer +0x18 FUN_06212240 -> node +0x10 FUN_0620fcb0 (draw), outer +0xBC
    // FUN_06212700 (spawn), outer +0xD0 / +0xD4 FUN_062125e0 / FUN_06212600 (count * 4 /
    // count * 6). The earlier wave108 reading (slot 47 = FUN_062445c0 at 0x0624c0fc)
    // indexed the wrong table: PTR_FUN_0624c040 is 0x58 bytes long and 0x0624c0fc is
    // node-vtable PTR_FUN_0624c098 + 0x64.
    // Node fields beyond the shared list layout (node+0xd8 head, +0xdc tail, +0xe0 pool,
    // +0xe4 count, +0xe8 descriptor), all set by FUN_0620f4e0:
    //   +0xec  12 floats, the view transform of the last draw (ctor: identity);
    //   +0x11c the last update's Dt (ctor: 0.05, 0x3d4ccccd);
    //   +0x120 the camera position of the last draw, +0x12c its view-space step length
    //          (ctor: 0).
    // Particles carry their previous view-space position at +0x30 (pool stride 0x3C).
    class FLDustAppearance final : public FFxBillboardAppearance
    {
    public:
        FLDustAppearance(const char* PrimaryIid, const char* FactoryName);

        // Outer +0x14 -> FUN_0620fb70, the per-node update. See the .cpp.
        GENRESULT DACOM_API Pad5(float Dt) override;
        // Outer +0xBC FUN_06212700, the spawn. See the .cpp.
        GENRESULT DACOM_API Pad47(const float* Six, float Age, float Duration) override;
        // Outer +0x18 -> FUN_0620fcb0: draws every particle at once, in view space (no
        // batcher branch). See the .cpp.
        GENRESULT DACOM_API DrawOrEnqueue() override;

        GENRESULT DACOM_API GetQueuedVertCount(unsigned long* Out) override;

        GENRESULT DACOM_API GetQueuedIndexCount(unsigned long* Out) override;

        // FUN_0620f600 (outer thunk 06212270).
        GENRESULT DACOM_API CloneNode(IDAComponent** Out) override;

    protected:
        // Node slot +0x48 FUN_0620f6b0: the set FUN_0620ef80.
        void CreatePropertySet() override;

    private:
        float ViewXform_[12];    // node+0xec
        float LastDt_;           // node+0x11c
        float LastCameraPos_[3]; // node+0x120
        float LastCameraStep_;   // node+0x12c
    };

    // Set FUN_06204670 (create FUN_06205000).
    //
    // FUN_0620e430 (+0xBC), node twin LAB_062055a0: evict the pool's oldest used
    // item when full, allocate/fill through FUN_0620be90, prepend to the owning
    // node's list, then toggle the ribbon parity byte. This list is the same one
    // used by update and draw; there is no separate private particle list.
    //
    // Draw: FUN_06205620 (batch list 3), outer +0x9C/+0xA0/+0xA4 through the
    // node vtable PTR_FUN_0624b6b0 (+0x4c LAB_062069c0, +0x50 LAB_062069e0,
    // +0x54 LAB_06206a20), draw state FUN_06206850, counts +0x7c LAB_06206a30 /
    // +0x80 LAB_06206a70. See the .cpp.
    // Outer +0x64 on this vtable (PTR_LAB_0624b7d0 + 0x64 = LAB_0620e080,
    // `FLD [ESP+8]; FSTP [outer+0x90]`) is the node Age store: the appearance
    // node sits at outer+0xC, so outer+0x90 = node+0x84. The shared inline
    // SetNodeAge covers it; the former Pad25 override and its
    // EffectOuterScale_ field (the same dword under a guessed name) were
    // removed 2026-09-26.
    class FLBeamAppearance final : public FFxBillboardAppearance
    {
    public:
        FLBeamAppearance(const char* PrimaryIid, const char* FactoryName);

        GENRESULT DACOM_API Pad47(const float* Six, float Age, float Duration) override;

        // wave124 (sonnet:wave124:beampool, 2026-09-15): real override of the
        // IGenericFx::Pad5 slot, reproducing retail FUN_0620de30 (decomp-tracker
        // item 132227): the appearance per-node update on outer +0x14, run by
        // the IAlchemy Slot16 walk every frame. `*(outer+0x90) += Dt` is Age_.
        GENRESULT DACOM_API Pad5(float Dt) override;
        // Outer +0x8C FUN_0620e1b0 (`ADD [ESP+4],0xc; JMP`) -> FUN_062050c0. See the .cpp.
        GENRESULT DACOM_API GetBoundingSphere(float* OutSphere) override;

        // FUN_06205620: draw now, or queue on batch list 3 when the material batcher is on.
        GENRESULT DACOM_API DrawOrEnqueue() override;
        // LAB_062069c0 / LAB_062069e0 / LAB_06206a20 (-> FUN_06205720).
        GENRESULT DACOM_API BeginBatchedDraw(unsigned long Verts, unsigned long Indices) override;
        GENRESULT DACOM_API EndBatchedDraw() override;
        GENRESULT DACOM_API FillBatchedGeometry() override;
        // LAB_06206a30 / LAB_06206a70: 5 verts / 0x18 indices per particle, one more
        // unit when descriptor flag 0x100 is set.
        GENRESULT DACOM_API GetQueuedVertCount(unsigned long* Out) override;
        GENRESULT DACOM_API GetQueuedIndexCount(unsigned long* Out) override;

        // FUN_06204ef0 (outer thunk 0620df10).
        GENRESULT DACOM_API CloneNode(IDAComponent** Out) override;

    protected:
        // Node slot +0x48 FUN_06205000: the set FUN_06204670.
        void CreatePropertySet() override;

    private:
        // FUN_06205720's stack struct at [ESP+0x6c], handed to FUN_06205f00 / FUN_06206020
        // as ECX: the previous ring's four vertex indices (Idx[0] == 0xffff until the first
        // ring), the ring counter, the node, the ribbon basis MulTransform(camera vt+0x10,
        // node+0x4c) and the previous ring's centre.
        struct FBeamRingState
        {
            unsigned short Idx[4];  // +0x00
            long Counter;           // +0x08
            FLBeamAppearance* Node; // +0x0c
            float Xform[12];        // +0x10
            float PrevPos[3];       // +0x40
        };

        // FUN_06206850: blend, texture, fill mode (scoped) and the node's view transform.
        void ApplyDrawState();
        // FUN_06205720: the ribbon over the particle list (newest first).
        void FillRibbon();
        // FUN_06205f00: half width, {R, G, B, A} and the ring's V for particle P.
        void EvalRingParams(const FBeamRingState& State, const FPooledParticle* P, float* OutHalfWidth,
                            float* OutRgba, float* OutV);
        // The callers' packing of that {R, G, B, A} into a diffuse colour.
        static unsigned long PackRingColor(const float* Rgba);
        // FUN_06206020: one ring of four vertices at Pos, joined to the previous ring.
        void EmitRing(FBeamRingState& State, const FPooledParticle* P, const float* Pos);
        // The join both of them inline once Idx[0] != 0xffff: flag 2 -> a centre vertex and
        // eight triangles (FUN_06208b00 order), else two quads (FUN_06208b50 order).
        void JoinRing(const FBeamRingState& State, const float* Pos, unsigned long Diffuse,
                      const unsigned short* NewIdx);

        // node+0xe1: cleared by the node ctor FUN_06204e30, flipped by every spawn
        // (FUN_0620e430 / 0x06205606); FUN_06205f00 / FUN_06206020 pick V0 or V1 by
        // comparing it with the ring counter's low bit.
        bool bRibbonParity_;
    };

} // namespace AlchemyImpl
