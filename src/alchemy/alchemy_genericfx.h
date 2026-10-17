// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "alchemy_types.h"
#include "alchemy_common.h"

#include <cstring>

// alchemy.dll — DACOM particle FX scaffolding.
//
// FGenericFxObjectBase: shared COM/tree machinery and the property-set protocol
// (each class's retail property set, see FFxNodePropertySet) for every
// FxNode/emitter/field/appearance concrete type. Originally this was
// ONE class (FGenericFxObject) dispatching on FactoryName_ string compares
// with type-specific state (field magnitude/approach/attenuation/etc.) all
// living on the same object regardless of which of the 19 retail types the
// instance actually was. That shape was replaced (2026-09-10, user-directed
// after wave 12 found kDefaultApproach had been silently mis-shared between
// FxRadialField and FxTurbulenceField) with a real class hierarchy: each of
// the 19 concrete types below is now its own C++ class with only the fields
// it actually needs, so a bug like "type X's state accidentally leaks into
// type Y's code path" is a compile error, not a silent runtime bug.
//
// Concrete leaves (declared in their own family headers):
//   FxNode                                            — this file (trivial)
//   FxSphereEmitter/FxConeEmitter/FxCubeEmitter        — alchemy_emitters.h
//   FxAirField/FxGravityField/FxCollideField/
//     FxRadialField/FxTurbulenceField/
//     FLDustField/FLBeamField                          — alchemy_fields.h
//   FxBasicAppearance/FxParticleAppearance/
//     FxRectAppearance/FxPerpAppearance/
//     FxMeshAppearance/FxOrientedAppearance/
//     FLDustAppearance/FLBeamAppearance                — alchemy_appearances.h
//
// Construction: alchemy_factory.cpp's CreateComponent constructs the exact
// leaf class by retail component name (no more single parameterized class).
//
// Virtual hooks a leaf overrides (all default to a no-op in this base,
// matching what a non-applicable type already did via its old FactoryName_
// guard): BeginField/ApplyFieldToVel (IGenericFx interface methods — only
// the 7 field leaves override; FLBeamField genuinely leaves both as the
// inherited no-op, matching retail's own LAB_0620ed20/30 no-op),
// OnAfterIntegrate (protected, not part of IGenericFx — only
// FxMeshAppearance overrides, for the Extra2/PoseMeshExtra2 special case).
// OnParticleEmitted (protected, not part of IGenericFx, added wave16-lane1
// 2026-09-10 — only FxParticleAppearance overrides, as a documented no-op;
// see its own doc comment).
// The emitter machinery (FUN_06245bf0 and the shape samplers) lives on
// FFxEmitterBase in alchemy_emitters.h since 2026-09-26; the invented
// SampleShape / StoreEmitCountField hooks and the Pad25 emitter tick are gone.
//
// Deliberately NOT modeled here (2026-09-10, alchemy-wave15-lane1): a real
// retail per-instance particle-instance-tracking mechanism (private pool +
// intrusive list + owner refcount for emitter-spawned instances; a second,
// separate per-particle "child COM object" slot for FxParticleAppearance).
// Fully traced byte-for-byte (see the large comment above EmitOne in
// alchemy_genericfx.cpp) but concluded to belong on specific EMITTER and
// FxParticleAppearance leaves (alchemy_emitters.*/alchemy_appearances.*),
// not here — retail overloads the same base-class byte offset with two
// different per-leaf-type meanings for this, which is exactly the
// FactoryName_-gated god-class pattern this file's own split (above) was
// written to eliminate. Open TODO for a future wave with those files in
// scope (the emitter-side "Cluster A" list is still fully open; the
// FxParticleAppearance-side "Cluster B" child-object slot got a new
// port-only virtual hook this wave — OnParticleEmitted below — see its own
// doc comment and EmitOne's doc comment in alchemy_genericfx.cpp for what
// wave16-lane1 confirmed vs. left open).
//
// NOT virtual — called directly and non-virtually from the relevant leaf
// constructors instead (a virtual call from a BASE class constructor can
// never reach a derived override in C++, since the derived part of the
// object does not exist yet during base construction — so these are plain
// protected helpers a leaf ctor opts into after its own base subobject is
// already fully constructed): BuildDefaultAppearanceRamps() (called by the
// perp and dust ctors; basic, rect, oriented and beam have their own builders in
// alchemy_appearances.cpp). FxRadialField's
// old InitRadialFalloffRamp() is no longer here at all — it is a private
// method directly on FxRadialField now, since only that one type needs it.
namespace AlchemyImpl
{
    // The lookup key ITextureLibrary +0x30 (Slot12) takes: name + CRC, as
    // the other modules' {Name, Crc} queries. The appearance texture path
    // (FUN_06244b80 / FUN_06244df0) always passes the name "???"
    // (DAT_06255804) and the descriptor's texture CRC.
    struct FFxTextureKey
    {
        const char* Name;
        unsigned long Crc;
    };

    // Retail ImpFxTexInfo (FDUMP path "...\Src\ALIL\ImpFxTexInfo.cpp"): the
    // texture state every billboard appearance embeds (node+0xb4; the beam
    // node's at node+0xb8), 0x24 bytes, built by FUN_06244b20. The texture
    // CRC comes from the appearance descriptor (+0x64), which the
    // descriptor's name property 0x1BA23359 sets to GetCRC32(name)
    // (e.g. LAB_062328c0 on the perp descriptor).
    struct FFxTextureWrapper
    {
        unsigned long TexCrc;     // +0x00
        int Handle;               // +0x04 ITextureLibrary +0x34 id, -1 = none
        // +0x08..+0x18: ITextureLibrary +0x70 GetFrame output -- the frame's texture
        // dword, then its UV rect. The draw fills read [1..4] as floats and copy [0]
        // out bitwise.
        float Frame[5];
        int FrameCount;           // +0x1C ITextureLibrary +0x68
        // +0x20: zeroed by FUN_06244b20; no other access in alchemy.dll (every
        // `+ 0xd4]` operand on an appearance is a vtable call). TODO: meaning unknown.
        unsigned long Field_0x20;

        FFxTextureWrapper();                        // FUN_06244b20
        void ReleaseHandle();                       // FUN_06244b50
        void SetTexture(const FFxTextureKey* Key);  // FUN_06244b80
        void Resolve();                             // FUN_06244df0
        void Advance(const float& Dt);              // FUN_06244ef0
        void LookupFrame(int FrameIndex, float* Out); // FUN_06244f30
    };

    // One case of a billboard descriptor's bool setter (descriptor vt+0x20): the hash and
    // the Flags (descriptor +0x60) bit it sets or clears. Mask 0 = accepted and ignored
    // (the beam's 0x03503B61 / 0xE2F60EEB / 0x0ABE0402 jump straight to `return 0`).
    struct FDescriptorFlagBit
    {
        unsigned long Hash;
        unsigned long Mask;
    };

    class FGenericFxObjectBase : public IGenericFx
    {
        // wave23 (sonnet:alchemy-wave23-lane1, 2026-09-11): IntegratePool and
        // ApplyFieldsToPool (alchemy_common.h/.cpp — plain namespace
        // functions, not members) need direct access to
        // InstanceListHead_/InstanceListTail_ (and IntegratePool to
        // LiveInstanceCount_, on particle expiry) below. Friending these two
        // specific, already-shared, already-documented helpers is narrower
        // than making the fields public, and keeps the list's invariants
        // (only ever mutated via UnlinkParticleNode/AppendParticleNode)
        // enforced at exactly two call sites instead of anywhere in the
        // module. See decomp-tracker item 132111 for the full retail trace
        // this pairing implements.
        friend void IntegratePool(FGenericFxObjectBase* Owner, FAppearancePool* Pool, const float& Dt);
        friend void ApplyFieldsToPool(FGenericFxObjectBase* Owner, bool bLiveItemsOnly);

    public:
        FGenericFxObjectBase(const char* PrimaryIid, const char* FactoryName);
        virtual ~FGenericFxObjectBase() noexcept(false);

        GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
        unsigned long DACOM_API AddRef() override { return ++RefCount_; }
        unsigned long DACOM_API Release() override;

        // Outer +0x0C, CloneNode: each leaf class overrides it with its retail clone.
        // FUN_062401a0, the shared node-base compose (node vtable slot 2). The mesh
        // appearance overrides it with FUN_06225240 (compose, then pose its meshes).
        GENRESULT DACOM_API ComposeNodeTransform(void* Unused, const float* Parent12) override;
        // FUN_06240180 (node vtable slot 3): TickLifetime(Dt) through the object's own
        // vtable, return 0. FLBeamAppearance and the three emitters override it
        // (FUN_0620de30; LAB_0621b9d0 / LAB_0621d360 / LAB_0623bcf0).
        GENRESULT DACOM_API Pad5(float Dt) override;
        // Outer +0x18, the per-node draw. FUN_062403f0 (node vtable slot 4 default):
        // `XOR EAX,EAX; RET 4`. The appearances override it with their draw.
        GENRESULT DACOM_API DrawOrEnqueue() override { return GR_OK; }
        // Outer +0x1C (FxNode LAB_062026a0 and every sibling): `[node+4] >> 31`.
        GENRESULT DACOM_API IsNodeDead() override
        {
            return static_cast<GENRESULT>(NodeFlags_ >> 31);
        }
        // Outer +0x20 / +0x24 (FxNode LAB_062026d0 / LAB_062026f0): the flags dword.
        GENRESULT DACOM_API GetNodeFlags(unsigned long* Out) override
        {
            *Out = NodeFlags_;
            return GR_OK;
        }
        GENRESULT DACOM_API SetNodeFlags(unsigned long Flags) override
        {
            NodeFlags_ = Flags;
            return GR_OK;
        }
        IDAComponent* DACOM_API GetAttachParent() override { return AttachParent_; }
        IDAComponent* DACOM_API GetFirstChild() override { return FirstChild_; }
        IDAComponent* DACOM_API GetPrevSibling() override { return PrevSibling_; }
        IDAComponent* DACOM_API GetNextSibling() override { return NextSibling_; }
        // Outer +0x38, `MOV EAX,imm; RET 4`, one constant per family (all 19 outer
        // vtables, located by their +0x44 thunk into FUN_0623ffb0): FxNode 0x190
        // (LAB_06202740); every appearance 0x64 (beam 0620dfa0, dust 06212300, basic
        // 06218e80, 06226f60, 0622ea90, 06231ea0, 062357c0, 0623ad10);
        // every field 0xc8 (0620eaa0, 06213880, air 06214900, 0621a890, 06223ee0,
        // 06236b10, 0623e0f0); every emitter 0x12c (cone 0621c220, cube 0621da00, sphere
        // 0623c420). LinkNodeTree (FUN_06201e00) orders siblings by it. The port has no
        // family base classes, so the family tag bytes (+0x94 / +0x95 / +0x96) select it.
        // Until ALC-5 this returned 0 for every node, so sibling order was insertion order.
        int DACOM_API GetSortKey() override
        {
            if (bIsAppearanceNode_ != 0)
                return 0x64;
            if (bIsFieldNode_ != 0)
                return 0xc8;
            if (bIsEmitterNode_ != 0)
                return 0x12c;
            return 0x190;
        }
        // FUN_062400f0 / FUN_062400b0 (node vtable slots 6 / 5).
        GENRESULT DACOM_API SetNodeTransform(unsigned long Index, const float* Source12) override;
        GENRESULT DACOM_API GetNodeTransform(unsigned long Index, float** OutSlot) override;
        // wave89 (sonnet:alchemy-wave89-lane1, 2026-09-15): see
        // IGenericFx::TickLifetime's own doc comment (alchemy_types.h) for
        // the full byte trace this corrects (was a no-op `Pad17()` stub
        // through every prior wave). Real body: accumulate Dt into Age_,
        // and once Age_ exceeds Duration_ (strictly), latch the expired bit into
        // NodeFlags_ -- defined out-of-line in alchemy_genericfx.cpp since
        // it now has real logic instead of a one-line stub.
        GENRESULT DACOM_API TickLifetime(float Dt) override;
        GENRESULT DACOM_API SetParent(IDAComponent* Parent) override
        {
            AttachParent_ = Parent;
            return GR_OK;
        }
        GENRESULT DACOM_API SetFirstChild(IDAComponent* Child) override
        {
            FirstChild_ = Child;
            return GR_OK;
        }
        GENRESULT DACOM_API SetPrevSibling(IDAComponent* Prev) override
        {
            PrevSibling_ = Prev;
            return GR_OK;
        }
        GENRESULT DACOM_API SetNextSibling(IDAComponent* Next) override
        {
            NextSibling_ = Next;
            return GR_OK;
        }
        // Outer +0x58..+0x6C (FxNode LAB_062027c0..LAB_06202860): get/set of node+0x8c,
        // +0x84 and +0x88 -- see the block comment in IGenericFx (alchemy_types.h).
        GENRESULT DACOM_API GetNodeOuterTime(float* Out) override
        {
            *Out = AppearanceOuterTime_;
            return GR_OK;
        }
        GENRESULT DACOM_API SetNodeOuterTime(float OuterTime) override
        {
            AppearanceOuterTime_ = OuterTime;
            return GR_OK;
        }
        GENRESULT DACOM_API GetNodeAge(float* Out) override
        {
            *Out = Age_;
            return GR_OK;
        }
        GENRESULT DACOM_API SetNodeAge(float Age) override
        {
            Age_ = Age;
            return GR_OK;
        }

        // FUN_06215f40 / FUN_0620e430 / FUN_0620be90. Declared at the end of IGenericFx
        // now: it used to occupy +0x68, which the retail call sites prove belongs to
        // GetSubtreeDuration.
        GENRESULT DACOM_API EmitOne(const float* Six, unsigned long Extra,
                                    float Duration) override;
        // Outer +0x68 (FxNode LAB_06202840): `*Out = [node+0x88]`. Reported 0 until
        // 2026-09-26.
        GENRESULT DACOM_API GetSubtreeDuration(float* OutSeconds) override
        {
            *OutSeconds = Duration_;
            return GR_OK;
        }
        // Outer +0x6C (FxNode LAB_06202860): `FSTP [node+0x88]`. A stub until 2026-09-26.
        GENRESULT DACOM_API SetNodeDuration(float Value) override
        {
            Duration_ = Value;
            return GR_OK;
        }
        // Outer +0x70 (beam LAB_0620def0, field LAB_0620e9f0): the factory name.
        const char* DACOM_API GetFxFactoryName() override { return PropertySet_->ClassName.Buffer; }
        // Outer +0x74 / +0x78 / +0x7C (FxNode LAB_062028b0 / LAB_062028d0 / FUN_062028f0):
        // tail jumps into the property set's outer +0x14 / +0x10 / +0xC, i.e. the set's
        // Node_Name hash (+0x2c), buffer (+0x38) and assign (FUN_06209c50 and the
        // per-class copies FUN_06231900 / FUN_062318e0 / FUN_06231810, ...). The name
        // belongs to the shared set, so a clone renames its archetype too.
        GENRESULT DACOM_API GetInstanceNameHash(unsigned long* Out) override
        {
            return PropertySet_->GetInstanceNameHash(Out);
        }
        GENRESULT DACOM_API GetInstanceName(const char** Out) override
        {
            return PropertySet_->GetInstanceName(Out);
        }
        GENRESULT DACOM_API SetInstanceName(const char* Name) override
        {
            return PropertySet_->SetInstanceName(Name);
        }
        // Outer +0x80 (FxNode 06202880 and its fourteen twins): the set's outer, AddRef'd.
        GENRESULT DACOM_API GetPropertySet(IFxNodePropertySet** Out) override
        {
            *Out = PropertySet_;
            PropertySet_->AddRef();
            return GR_OK;
        }
        // Outer +0x84 / +0x88 (FxNode LAB_06202910 / LAB_06202930): node+0x90.
        GENRESULT DACOM_API GetNodeLodInput(float* Out) override
        {
            *Out = LodInput_;
            return GR_OK;
        }
        GENRESULT DACOM_API SetNodeLodInput(float LodInput) override
        {
            LodInput_ = LodInput;
            return GR_OK;
        }
        // Outer +0x8C for every node that has no sphere: FxNode FUN_06202950, cone
        // emitter LAB_0621c430, field LAB_0620ecb0 are all `MOV EAX,0xfffffff9;
        // RET 8`. Until 2026-09-26 this reported success without writing, so
        // FFxEffectObject's union seeded itself from an unwritten buffer. Basic,
        // rect and perp override it; oriented (FUN_0622bed0), dust (FUN_0620f830),
        // beam (FUN_062050c0), particle (FUN_0622fa10) and mesh (FUN_06225000)
        // compute one in retail and still land here -- not ported.
        GENRESULT DACOM_API GetBoundingSphere(float* /*OutSphere*/) override
        {
            return static_cast<GENRESULT>(0xfffffff9);
        }
        // Outer +0x90 / +0x94 / +0x98 (FxNode LAB_06202960 / 980 / 9a0): the family tag
        // bytes node+0x94 (appearance) / +0x95 (emitter) / +0x96 (field).
        GENRESULT DACOM_API GetFlagBit4(unsigned char* Out) override
        {
            *Out = bIsAppearanceNode_;
            return GR_OK;
        }
        GENRESULT DACOM_API GetFlagBit1(unsigned char* Out) override
        {
            *Out = bIsEmitterNode_;
            return GR_OK;
        }
        GENRESULT DACOM_API GetFlagBit2(unsigned char* Out) override
        {
            *Out = bIsFieldNode_;
            return GR_OK;
        }
        GENRESULT DACOM_API Pad40() override { return GR_OK; }
        GENRESULT DACOM_API FillBatchedGeometry() override { return GR_OK; }
        // Batch begin / end-and-draw (IGenericFx, retail appearance +0x9C / +0xA0). The four
        // batched appearance kinds override them. Every other appearance reaches the
        // appearance node defaults (node vtable 0x0624fbd0 slots +0x4c / +0x50, entered by
        // `ADD [ESP+4],0xc; JMP`, e.g. dust 0x06212580 / 0x06212590): FUN_06244690
        // `XOR EAX,EAX; RET 0xC` and FUN_062446a0 `XOR EAX,EAX; RET 4` -- return 0, no
        // side effect. (FillBatchedGeometry's default above is the third one, FUN_062446b0.)
        GENRESULT DACOM_API BeginBatchedDraw(unsigned long /*Verts*/,
                                             unsigned long /*Indices*/) override
        {
            return GR_OK;
        }
        GENRESULT DACOM_API EndBatchedDraw() override { return GR_OK; }
        GENRESULT DACOM_API Pad42() override { return GR_OK; }
        // FUN_06244430, inner +0x5c / appearance +0xB0: the unchecked iterator
        // starts at the by-value argument itself. Out of line; this deliberately
        // preserves that malformed algorithm, not a search by field or index.
        GENRESULT DACOM_API Pad44(IDAComponent* Value) override;
        // Appearance +0xB4 (every appearance vtable: `ADD this,0xc; JMP FUN_06244580`,
        // basic LAB_062195a0). Begins every associated field. Out of line.
        GENRESULT DACOM_API Pad45() override;
        // Appearance +0xB8 (`ADD this,0xc; JMP FUN_062445c0` on all eight appearance
        // vtables, basic LAB_062195b0): will a particle spawned with (Six, Age,
        // Duration) survive this frame's fields? Out of line.
        GENRESULT DACOM_API Pad46(const float* Six, float Age, float Duration) override;
        // Appearance +0xBC: the spawn. Every appearance has its own body (basic
        // FUN_06215f40, rect FUN_0623b410, perp FUN_06235db0, oriented FUN_0622bd80,
        // beam FUN_0620e430, dust FUN_06212700, particle FUN_0622f6a0, mesh
        // FUN_06224c20). The leaves that are ported override it; this default spawns
        // nothing.
        //
        // Until 2026-09-26 this default carried the body of FUN_062445c0, which
        // belongs to +0xB8 (Pad46) -- one slot off. Dust and Particle still have no
        // spawn and fall through to here: they never emit.
        GENRESULT DACOM_API Pad47(const float* /*Six*/, float /*Age*/,
                                  float /*Duration*/) override
        {
            return GR_OK;
        }
        // Appearance +0xC0 (`ADD this,0xc; JMP FUN_06244660`, basic LAB_062195d0):
        // calls each associated field's +0xa0. Out of line.
        GENRESULT DACOM_API Pad48() override;
        // Appearance +0xC4 / +0xC8 (basic LAB_062195e0 / LAB_06219600): INC / DEC of
        // node+0xb0. Only appearance vtables have these slots in retail.
        GENRESULT DACOM_API AddEmitterRef() override
        {
            ++EmitterRefCount_;
            return GR_OK;
        }
        GENRESULT DACOM_API ReleaseEmitterRef() override
        {
            --EmitterRefCount_;
            return GR_OK;
        }

        GENRESULT DACOM_API GetLiveInstanceCount(unsigned long* Out) override;
        GENRESULT DACOM_API GetQueuedVertCount(unsigned long* Out) override;
        GENRESULT DACOM_API GetQueuedIndexCount(unsigned long* Out) override;

        GENRESULT DACOM_API SetLiveInstanceCount(unsigned long Count) override
        {
            LiveInstanceCount_ = Count;
            return GR_OK;
        }

        // The node's property set (retail +0x7c outer / +0x80 set). Never null once the
        // node went through InitializeFromFactory or InitNodeFromArchetype. The property
        // protocol is the set's (IFxNodePropertySet, alchemy_types.h); each node class's
        // CreatePropertySet builds its own set class.
        FFxNodePropertySet* NodePropertySet() const { return PropertySet_; }

        // FUN_0623fde0 (node vtable slot 1, called by every node factory after
        // construction): create this class's property set (slot +0x48, CreatePropertySet),
        // give it the class name (the creation descriptor's +4 name, hashed), then
        // Duration = set LifeSpan. A null descriptor FDUMPs "you should set DACOMDESC for
        // create FxNode" and skips the name.
        GENRESULT InitializeFromFactory(const FAggDesc8* Desc);

        // Retail IFxEmitter+0x9C (this, appearance). One slot; last wins. No emit.
        GENRESULT DACOM_API AssociateAppearance(IDAComponent* Appearance) override;

        // Retail IFxAppearance+0xAC = FUN_062443b0: AddRef, insert at end of the
        // field vector (node+0x98 object, begin +0x9c / end +0xa0), Release. A null
        // field is stored too. Walk is FUN_06244580, not last-wins.
        GENRESULT DACOM_API AssociateField(IDAComponent* Field) override;

        IDAComponent* DACOM_API GetAssociatedField() override;
        GENRESULT DACOM_API GetAssociatedFieldCount(unsigned long* Out) override
        {
            *Out = FieldCount_;
            return GR_OK;
        }
        GENRESULT DACOM_API GetAssociatedFieldAt(unsigned long Index, IDAComponent** Out) override;
        // Internal field-update loops borrow a vector entry directly in retail.
        unsigned long BorrowAssociatedFieldCount() const { return FieldCount_; }
        IDAComponent* BorrowAssociatedFieldAt(unsigned long Index);
        // wave104 (sonnet:alchemy-wave104-lane2, 2026-09-15): retail's own
        // field-array auto-compaction-on-expiry (FUN_06218780 else-branch,
        // decomp-tracker item 132871 -- confirmed via FUN_0620d690/FUN_0620bd70,
        // a smart-pointer-array shift-erase whose net effect is "release the
        // expired slot, shift everything after it down by one, shrink the
        // count"). Port-only helper (not part of IGenericFx, same category as
        // BorrowAssociatedFieldAt above) -- public so the free
        // function ApplyFieldsToPool (alchemy_common.cpp) can call it when a
        // field's own +0x1C gate reports done/expired, instead of leaving the
        // expired field attached forever.
        void RemoveAssociatedFieldAt(unsigned long Index);
        IDAComponent** EraseAssociatedField(IDAComponent** Position);
        // FUN_06244120 (the appearance archetype copy base, called by all eight
        // appearance clone ctors) after FUN_0623fc30: vector<ComPtr> operator= of the
        // field list (node+0x98). Out of line.
        void CopyAssociatedFieldsFrom(FGenericFxObjectBase* Source);
        const FAppearanceRampDescriptor* DACOM_API GetAppearanceRampDescriptor() override
        {
            return Descriptor_;
        }

        // wave22 (sonnet:alchemy-wave22-lane1): see IGenericFx::
        // GetAppearanceOuterTime/SetAppearanceOuterTime (alchemy_types.h)
        // for the full retail [this+0x8c] trace and the exhaustive,
        // module-wide write-site search that came up empty. Backed by a
        // real field (AppearanceOuterTime_ below) rather than a hardcoded
        // constant so a future wave that DOES locate a real producer can
        // wire it up via SetAppearanceOuterTime without another interface
        // change.
        float DACOM_API GetAppearanceOuterTime() override { return AppearanceOuterTime_; }
        GENRESULT DACOM_API SetAppearanceOuterTime(float Time) override
        {
            AppearanceOuterTime_ = Time;
            return GR_OK;
        }

        // wave117 (sonnet:wave117:globals-final): see IGenericFx::
        // GetDustPrevCameraDistance/SetDustPrevCameraDistance (alchemy_types.h)
        // for the full retail [this+0x12C] trace (FLDustAppearance-only).
        // Backed by a real field (DustPrevCameraDistance_ below) defaulting
        // to 0.0f, same convention as AppearanceOuterTime_ above.
        float DACOM_API GetDustPrevCameraDistance() override { return DustPrevCameraDistance_; }
        GENRESULT DACOM_API SetDustPrevCameraDistance(float Distance) override
        {
            DustPrevCameraDistance_ = Distance;
            return GR_OK;
        }

        // ALC-4 (2026-09-30): the appearance texture state (node+0xb4 in retail) is the
        // FFxTextureWrapper Texture_ below. The old AtlasFrameCount_/AtlasFrameUV_/
        // TextureHandle_ trio and their accessors were that wrapper's +0x1c/+0x08/+0x04
        // split apart with no writer; the history that concluded "no writer exists" is
        // wrong -- the descriptor's name property 0x1BA23359 stores GetCRC32(name) at
        // descriptor +0x64 (LAB_062328c0 and eleven siblings), and every appearance
        // update resolves it through FUN_06244b80 / FUN_06244df0.

        // wave54 (wave54-lane2, 2026-09-11): investigated item 134306's
        // (FUN_06233170) own wave47 blocker (a) -- a believed-missing
        // "per-appearance blend-factor field" that its
        // SetRenderState(D3DRS_SRCBLEND/D3DRS_DESTBLEND) call site
        // (alchemy.dll.c:32227-32231, DAT_06257bc0 vtable+0xf0 with
        // render-state ids 0x13/0x14 = D3DRS_SRCBLEND/D3DRS_DESTBLEND)
        // appeared to need, now that TextureHandle_ above (the OTHER named
        // blocker) exists. Traced both values back to their real source:
        //   *(int*)(Appearance+0xf0)+0x68 (SrcBlend arg) and
        //   *(int*)(Appearance+0xf0)+0x6c (DestBlend arg)
        // i.e. NOT appearance-INSTANCE state at all -- both live on the
        // shared FAppearanceRampDescriptor object (alchemy_types.h,
        // already modeled for Flags/SizeRamp/ColorRamp/etc, cached at
        // Appearance+0xf0 by FUN_06232b00). The identical
        // (Descriptor+0x68/+0x6c) pair, read by the identical
        // (DAT_06257bc0 vtable+0xf0, ids 0x13/0x14) SetRenderState call
        // shape, recurs in at least 4 OTHER appearance-kind fill functions
        // too (alchemy.dll.c:4794-4805, 15464-15473, 28162-28165 -- this
        // one spells out the render-state ids as literal call args, e.g.
        // `(**(*DAT_06257bc0+0xf0))(DAT_06257bc0,0x13,*(int*)(*(int*)
        // (param_1+0xf0)+0x68))`, removing any doubt about the SetRenderState
        // identification -- and 35617-35626), confirming this is the whole
        // shared billboard-appearance family's blend state, not
        // FxPerpAppearance-specific.
        //
        // Then traced the VALUES to their write site: SIX independent
        // per-appearance-kind descriptor constructors (siblings of
        // FUN_062323a0, which is the one FUN_06233170's own Perp descriptor
        // uses -- alchemy.dll.c:3569-3570, 10968-10969, 14136-14137,
        // 27134-27135, 31705-31706 [FUN_062323a0 itself], 34491-34492) each
        // hardcode the IDENTICAL literal pair `param_1[0x19] = 5;
        // param_1[0x1a] = 2;` (local offsets 0x64/0x68 -> absolute
        // Descriptor+0x68/+0x6c, i.e. the exact two dwords FUN_06233170
        // reads) with ZERO variation between them. A full sweep of every
        // `+ 0x68) =`, `+ 0x6c) =`, `[0x19] =`, `[0x1a] =` write in the
        // whole decompiled module found no other writer and no
        // property-driven/.ale-authored override path anywhere. 5 =
        // D3DBLEND_SRCALPHA, 2 = D3DBLEND_ONE -- every
        // FAppearanceRampDescriptor-based appearance in retail draws with a
        // fixed additive-glow blend (SRCALPHA, ONE), not the plain alpha
        // blend (SRCALPHA, INVSRCALPHA) one might otherwise guess.
        //
        // CONCLUSION: this is a compile-time constant baked identically
        // into every retail descriptor build, not per-instance state.
        // Per this project's "document, don't invent" rule, deliberately
        // NOT adding SrcBlend_/DestBlend_ fields to this class -- there is
        // no real per-instance value for them to hold. A future wave
        // wiring FUN_06233170 (or any sibling fill function) should call
        // RenderPipeline->SetRenderState(D3DRS_SRCBLEND, 5) and
        // SetRenderState(D3DRS_DESTBLEND, 2) as plain literals, exactly as
        // retail does, unless a future .ale-driven override is later found
        // (in which case the right home is two new plain dword fields on
        // FAppearanceRampDescriptor itself, alchemy_types.h -- out of this
        // lane's file ownership this wave -- not on this class). See
        // decomp-tracker items 134306/134307 for the full byte citations.
        // This closes out blocker (a) from item 134306's wave47 note as
        // "answered: no field needed" -- blockers (c) (persistent
        // camera-rotation cache) and (d) (a new IGenericFx per-frame-driver
        // slot) remain genuinely open and out of scope.
        //
        // wave67 (sonnet:alchemy-wave67-lane2, 2026-09-11): closed blocker
        // (d) for real -- added the new port-only IGenericFx::DrawOrEnqueue
        // slot (alchemy_types.h) plus a base implementation here
        // (DrawOrEnqueue, declared below near GetQueuedIndexCount, defined
        // in alchemy_genericfx.cpp) that wires the one piece of
        // FUN_06233170's own dispatch that is genuinely generic across every
        // leaf type -- the begin()/end() empty-instance-particle-list guard.
        //
        // Also RE-EXAMINED blocker (c) from scratch (this wave's own
        // mandate) rather than taking "still open" on faith. Byte-traced
        // FUN_06233170's own camera-related call sequence
        // (alchemy.dll.txt:0x062332b3-0x06233311) end to end:
        //   MOV EAX,[DAT_06257908]; ...; CALL [EAX_vtbl+0x40]
        // is the EXACT same G_DrawCookie-vtable+0x40 call shape wave48
        // already resolved into GetDrawCameraTransform() (alchemy_common.h/
        // .cpp) -- confirmed by directly comparing this function's raw bytes
        // against GetDrawCameraTransform's own documented retail trace, not
        // by analogy. The result is passed straight into a math-engine
        // MulTransform(CameraTransform, this+0x4c) call (vtable+0x1c on the
        // DAT_06257b38 3DMathEngine singleton -- Acquire3DMath() in this
        // port), whose OUTPUT is written to a STACK-local scratch buffer and
        // handed directly to RenderPipeline::SetTransform (DAT_06257bc0
        // vtable+0x7c) within the SAME function call -- never written back
        // to the appearance object, never read again anywhere in this
        // function's own body. Unlike the sibling Dust/Basic vertex-fill
        // functions (alchemy_common.cpp's own AppearanceRestMatrix doc
        // comment, ~line 2135), which DO persist a composed transform to a
        // real object field (Appearance+0xec) for reuse across the
        // per-particle loop, FUN_06233170 computes and consumes its own
        // camera*rest composition ENTIRELY on the stack, once per dispatch,
        // with zero cross-frame or cross-call persistence in retail's own
        // bytes.
        //
        // CONCLUSION: like blocker (a), this is answered, not implemented as
        // a new field -- no persistent camera-rotation cache exists in
        // retail for THIS function, so adding one here would be exactly the
        // "invent a field with no retail backing" this project's rules
        // forbid. The two real, already-modeled pieces this function's own
        // camera math needs are already present: the live view transform via
        // GetDrawCameraTransform() (queried fresh every call, matching
        // retail's own fresh-query-every-dispatch bytes) and the persistent
        // per-instance rest transform via GetNodeTransform(0)/NodeWorldXform_
        // (already on this class since the original port). No new field
        // added. A future wave writing FxPerpAppearance::DrawOrEnqueue
        // (alchemy_appearances.cpp) should call
        // MulTransform(GetDrawCameraTransform(), GetNodeTransform())
        // straight into a local buffer, exactly as retail does, rather than
        // caching the result on the object.

        // Field types (7 leaves in alchemy_fields.h) override these; every
        // other leaf inherits the no-op below, matching what its old
        // FactoryName_ guard already did (and matching retail's own
        // FLBeamField, whose real Begin/Apply genuinely are XOR EAX,EAX RET
        // no-ops — see alchemy_fields.h's FLBeamField for that citation).
        void DACOM_API BeginField(IDAComponent* /*Appearance*/, float /*Dt*/) override {}
        void DACOM_API ApplyFieldToVel(float* /*PosVel6*/, float* /*Age*/, float /*Dt*/) override {}

        // wave30 (sonnet:alchemy-wave30-lane2, 2026-09-11): read-only
        // traversal entry point for THIS instance's own active-particle
        // list (InstanceListHead_/Tail_ below, added wave23/24). Added so a
        // future alchemy_common.cpp-owning wave can replace FillBeamRibbon's
        // documented O(pool-size) workaround (it currently walks the
        // entire shared FAppearancePool::UsedHead chain and filters by
        // `Particle->Appearance == Appearance`, since alchemy_genericfx.h
        // was out of wave28's file ownership) with a direct O(instance-
        // size) walk of exactly this list — see decomp-tracker items
        // 131849/131852 for that workaround's own documentation and this
        // wave's comment there for the exact call-site swap.
        //
        // Deliberately read-only and additive:
        //   - Returns a `const FPooledParticle*`, not the mutable
        //     InstanceListHead_ itself, so a caller cannot reassign this
        //     instance's own head pointer through the returned value.
        //   - FPooledParticle::Next/Prev (alchemy_types.h) are already
        //     plain public members with no accessor of their own — any
        //     external code that already compiles against FPooledParticle*
        //     (as FillBeamRibbon's current workaround already does, via
        //     Pool->UsedHead) can already walk ->Next once it holds a
        //     starting pointer, so this accessor does not expose anything
        //     that was not already externally walkable; it only supplies a
        //     narrower, correctly-scoped starting point than
        //     Pool->UsedHead.
        //   - Does not touch InstanceListHead_/InstanceListTail_'s existing
        //     private-to-this-class mutation path (still only ever written
        //     by AppendParticleNode/UnlinkParticleNode, called from EmitOne
        //     above and from the two wave23-friended pool functions) — this
        //     is a pure new getter, no existing behavior of EmitOne,
        //     IntegratePool, ApplyFieldsToPool, or ~FGenericFxObjectBase
        //     changes.
        //   - Intentionally NOT wired into FillBeamRibbon by this wave —
        //     alchemy_common.cpp is owned by a concurrent lane this wave
        //     (see this wave's own task scope); left for that future wave.
        const FPooledParticle* GetFirstInstanceParticle() const { return InstanceListHead_; }

        // Retail outer +4 / +8: the batch-list links FUN_06201c70 writes (prev, next) and
        // FUN_06201cc0 walks through [outer+8]. See FBatchList (alchemy_types.h).
        IGenericFx* BatchPrev_ = nullptr;
        IGenericFx* BatchNext_ = nullptr;

        // wave89 (sonnet:alchemy-wave89-lane1, 2026-09-15): read-only
        // accessor for Age_/Duration_ below -- see TickLifetime's own doc
        // comment above and Age_/Duration_'s own field comment for the full
        // retail trace. Not part of IGenericFx -- a plain port-only helper.
        float GetLifetimeAge() const { return Age_; }
        float GetLifetimeDuration() const { return Duration_; }

        // FUN_0623fc30 -- the node-base half of every class's archetype copy constructor
        // (FUN_06244120 / FUN_06244f90 / FUN_06245b30, 18 callers). Duration_ =
        // [[Archetype+0x80]+0x48] (Node_LifeSpan); the own +0x7c is Released, the
        // archetype's copied and AddRef'd, +0x80 copied: the clone shares the archetype's
        // property set. The billboard set cache (node+0xf0, dust +0xe8) follows it, as
        // every appearance clone ctor copies it (basic FUN_06215d80 0x06215e4b).
        void InitNodeFromArchetype(FGenericFxObjectBase* Archetype);

        // FUN_06244120 (RET 4), the appearance copy base all eight appearance clones call:
        // FUN_0623fc30, the appearance set cache +0xa8 copied (PropertySet_ here), then the
        // field list (+0x98) assigned from the archetype's (CopyAssociatedFieldsFrom).
        void InitAppearanceFromArchetype(FGenericFxObjectBase* Archetype);

        // The composed world transform, node+0x4c (what GetNodeTransform(0) hands out).
        // I3DMathEngine FTransform layout: 3x3 row-major rotation, then translation.
        const float* GetNodeWorldXform() const { return NodeWorldXform_; }

        // FUN_0623ffb0 -- node vtable slot 7 (+0x1c), shared by all 21 node vtables:
        // Out12 = GeneralInverse(Other's composed transform) * this node's composed
        // transform, i.e. this node expressed in Other's space. Other is an outer
        // object (its +0x40 GetNodeTransform(0) is called). Not an IGenericFx slot here:
        // the outer vtables do forward +0x44 to it (FxNode LAB_06202770), but the port's
        // +0x44 is TickLifetime and every known caller reaches this through the node
        // sub-object (emitter body FUN_06245bf0 0x06245c56, field Begin 0x06224219 ...).
        GENRESULT ComputeRelativeTransform(IDAComponent* Other, float* Out12);

        // [[this+0x80]+0x24]: the property set's Node_Transform IAnimatedTransform (the
        // set constructor's default FxAnimatedTransform, filled in place by FUN_062280a0
        // when the .ale has the property). No reference is added.
        virtual IAnimatedTransform* FindNodeAnimatedTransform() const { return PropertySet_->Transform; }

    protected:
        // Node slot +0x48: create this class's property set. The base body is FLBeamField's
        // FUN_06244fb0 (a base set under the field vtables 0x0624fda8 / 0x0624fcf0); every
        // other class, FxNode included (FUN_0623ff00), overrides it with its own creator.
        virtual void CreatePropertySet();
        // Store a freshly created set the way every creator does (e.g. sphere 0x0623bc8f..
        // 0x0623bcbb: Release the old +0x7c, store and AddRef the new, Release the
        // creation reference) and cache it (+0x80 and the class caches).
        void AdoptPropertySet(FFxNodePropertySet* Set);
        // Port-only: the class cache (retail +0x9c / +0xa8 / +0xf0) of a leaf that keeps a
        // typed pointer next to PropertySet_; called whenever PropertySet_ changes.
        virtual void OnPropertySetChanged();

        // The particle list at node+0xd8 = {head +0xd8, tail +0xdc, pool +0xe0,
        // count +0xe4} is InstanceListHead_ / InstanceListTail_ / the leaf's pool /
        // LiveInstanceCount_. These two helpers are the list operations the four
        // billboard appearances (basic, rect, perp, oriented) share; retail compiles
        // a copy per class.
        //
        // SpawnListParticle: basic FUN_06215f40, rect FUN_0623b410 + FUN_0623a2e0,
        // perp FUN_06235db0 + FUN_06234ab0, oriented FUN_0622bd80. Returns the new
        // particle (the list tail) so perp can seed its axis.
        FPooledParticle* SpawnListParticle(FAppearancePool* Pool, const float* Six, const float& Age,
                                           const float& Duration);
        FPooledParticle* EraseListParticle(FAppearancePool* Pool, FPooledParticle* Particle);
        void EvictOldestListParticle(FAppearancePool* Pool);
        // UpdateListAppearance: the per-node update, basic FUN_06216420, rect
        // FUN_06238780, perp FUN_06233040, oriented FUN_0622c3d0, dust FUN_0620fb70.
        // FrameRamp is the descriptor ramp the class evaluates for a flag-8 frame.
        void UpdateListAppearance(FAlDrawContext& DrawContext, FAppearancePool* Pool, float Dt,
                                  INestedSingleRamp* FrameRamp);

        void ReleaseAssociatedFields();
        void DestroyListAppearance(FAppearancePool* Pool);
        void ReleaseRectFields();
        void DetachRectList();
        void ReleaseRectPropertySet();
        unsigned long ResolveLiveInstanceCount() const;

        // (UpdateInstanceBoundsSphere / InstanceBoundsSphere_, wave71, were removed
        // 2026-09-26: FUN_062050c0 is FLBeamAppearance::GetBoundingSphere, reached through
        // the beam's outer +0x8C thunk FUN_0620e1b0.)

        unsigned long RefCount_;

        // Node-base +0x4 (FUN_0623fb80 zeroes it), the node flags:
        //   0x1 -- lifetime over (TickLifetime, LAB_06240150: Age_ > Duration_),
        //   0x2 -- skip the Node_Transform animation (read by the compose FUN_062401a0),
        //   0x4 -- the raw transform was set from outside (SetNodeTransform index 1),
        //   0x8 -- the composed transform was set from outside (SetNodeTransform index
        //          0); the next compose clears it and keeps the value.
        // The sign bit means "dead" (IsNodeDead, outer +0x1C); the emitter body
        // FUN_06245bf0 tests it on itself with `TEST EAX,EAX; JS` (0x06245c3d).
        unsigned long NodeFlags_;

        const char* PrimaryIid_;
        const char* FactoryName_;
        // Retail node +0x7c (the set's outer, AddRef'd) / +0x80 (the set): one pointer
        // holding one reference. Null until InitializeFromFactory or InitNodeFromArchetype.
        FFxNodePropertySet* PropertySet_;
        IDAComponent* AssociatedAppearance_;
        IDAComponent** AssociatedFields_;
        unsigned long FieldCount_;
        unsigned long FieldCapacity_;
        IDAComponent* AttachParent_;
        IDAComponent* FirstChild_;
        IDAComponent* PrevSibling_;
        IDAComponent* NextSibling_;
        unsigned long LiveInstanceCount_;

        // Node-base +0x1c: the raw (local) transform, identity from FUN_0623fb80. The
        // compose overwrites it with the evaluated Node_Transform while flag 0x4 is clear;
        // SetNodeTransform(1) sets it and flag 0x4.
        float NodeLocalXform_[12];
        // Node-base +0x4c: the composed (world) transform, identity from FUN_0623fb80.
        // Written by the compose FUN_062401a0 and by SetNodeTransform(0); read through
        // GetNodeTransform(0) and by every emitter / appearance / bounds consumer.
        float NodeWorldXform_[12];

        // Node-base +0x90, 1.0f from FUN_0623fb80 (0x0623fc02); outer +0x84/+0x88.
        float LodInput_;
        // Node-base +0x94 / +0x95 / +0x96, zero from FUN_0623fb80; the family
        // constructors set one of them (see IGenericFx::GetFlagBit4). The appearance
        // and field leaves set theirs in this class's constructor from PrimaryIid_
        // (retail sets them in the shared appearance init 0x0624439f and in each field
        // constructor), FFxEmitterBase sets +0x95.
        unsigned char bIsAppearanceNode_;
        unsigned char bIsEmitterNode_;
        unsigned char bIsFieldNode_;
        // Appearance node+0xb0 (outer +0xbc): how many emitters feed this appearance
        // (AddEmitterRef / ReleaseEmitterRef). Zero-initialised like the rest of the
        // appearance block.
        long EmitterRefCount_;

        // wave23 (sonnet:alchemy-wave23-lane1, 2026-09-11): this instance's
        // OWN active-particle list — every FPooledParticle this instance has
        // emitted that has not yet expired, threaded through the particle
        // struct's own (previously unused) Next/Prev fields
        // (alchemy_types.h). Rooted here, on the owning appearance instance,
        // NOT on FAppearancePool (which stays a purely shared block arena —
        // Items/FreeNodes/Capacity/FreeCount/UsedHead/UsedTail/FreeHead/
        // FreeTail, alchemy_types.h — its own Used/Free bookkeeping is a
        // physically separate FPoolLink*-typed list from this one and is
        // untouched by this change).
        //
        // Matches retail's confirmed per-owner-instance sentinel list: five
        // independently byte-verified call sites (FUN_06215f40 emit-and-link,
        // FUN_062188c0 per-instance age/integrate, FUN_06217ae0 unlink+free,
        // FUN_06219070 destructor erase-range, FUN_06218780 per-instance
        // field-apply — decomp-tracker items 132827/132872/132839/132921/
        // 132871 — plus FUN_06230370's independently-retraced parallel
        // lineage on a different concrete leaf class) all root this same
        // shape at a small fixed byte offset on the owning instance, not on
        // the shared pool. See decomp-tracker item 132111's wave22 comment
        // for the complete trace and staged plan this field pair implements
        // (steps 1-7); EmitOne links a new particle in
        // (alchemy_genericfx.cpp), IntegratePool ages/unlinks-and-frees
        // expired ones, ApplyFieldsToPool walks it read/write per associated
        // field (both alchemy_common.cpp, both friended above).
        //
        // wave24 (sonnet:alchemy-wave24-lane1, 2026-09-11): decomp-tracker
        // item 132111 step 8 / item 132921 CLOSED — ~FGenericFxObjectBase()
        // (alchemy_genericfx.cpp) now erase-ranges this list (unlink + free
        // each remaining node back to PoolForAppearanceFactory(FactoryName_))
        // before any other teardown, mirroring FUN_06219070/FUN_062184c0/
        // FUN_06217ae0's confirmed retail order. See that destructor's own
        // doc comment for the full byte-level trace.
        FPooledParticle* InstanceListHead_;
        FPooledParticle* InstanceListTail_;

        // wave89 (sonnet:alchemy-wave89-lane1, 2026-09-15): backing store for
        // TickLifetime() above -- see IGenericFx::TickLifetime's own doc
        // comment (alchemy_types.h) for the full LAB_06240150 byte trace.
        // Retail this+0x84 (Age_) and this+0x88 (Duration_) sit immediately
        // BEFORE AppearanceOuterTime_'s own confirmed this+0x8c below --
        // i.e. all three are consecutive dwords in retail's real layout,
        // additional (not the original) evidence that Age_/Duration_
        // genuinely belong on this shared base class right next to it, not
        // on some unrelated per-leaf object.
        //
        // FUN_0623fb80 seeds Age_ = 0 and Duration_ = 100.0f (0x42c80000);
        // FUN_0623fc30 (InitNodeFromArchetype) then copies the archetype's
        // Node_LifeSpan (hash 0xf27fde7d; descriptor default 3.0f from FUN_0623f7b0,
        // FLT_MAX is only the table's max bound) into Duration_.
        float Age_;
        float Duration_;

        // Node +0x8c: the ramp Eval "OuterTime" every property evaluation passes
        // (emitter props in FUN_06245bf0, the compose, the mesh pose). FUN_0623fb80
        // zeroes it. CORRECTED 2026-09-26 (ALC-1): it does have a writer -- outer
        // +0x5C SetNodeOuterTime (cone LAB_0621c2c0: FSTP [outer+0x90]), which
        // IFxEffect Slot6 (LAB_0621f820) walks over every node.
        float AppearanceOuterTime_;

        // wave117 (sonnet:wave117:globals-final): backing store for
        // GetDustPrevCameraDistance/SetDustPrevCameraDistance above --
        // retail's FLDustAppearance-only [this+0x12C] previous-frame
        // camera-distance field (see the accessor's doc comment in
        // alchemy_types.h for the full FUN_0620fcb0 trace). Defaults to
        // 0.0f, matching retail's own zero-initialized object memory (no
        // explicit constructor write to this offset was found anywhere in
        // the module).
        float DustPrevCameraDistance_;

        // Retail node+0xb4 (the beam's +0xb8): the appearance's texture state,
        // FUN_06244b20 at construction, FUN_06244b50 in every appearance destructor.
        // The port keeps it on the shared base; a node that never resolves a texture
        // keeps Handle -1, for which ReleaseHandle is a no-op.
        FFxTextureWrapper Texture_;

        // Retail node+0xf0 (dust +0xe8): the billboard set, i.e. PropertySet_ typed, for
        // the six billboard appearances; nullptr for every other node. Set by
        // OnPropertySetChanged of those six leaves.
        FAppearanceRampDescriptor* Descriptor_;
    };

    // RegisterComponent #15 (FxNode). A plain node has no field/emitter/appearance
    // behaviour, so every inherited hook is the retail one. (#14 "FxNodePropertySet" is
    // not a node: its factory builds FxNode's own property set, FFxNodeOwnPropertySet --
    // wave 3's "same underlying object" reading was wrong, see alchemy_factory.cpp.)
    class FxNode final : public FGenericFxObjectBase
    {
    public:
        FxNode(const char* PrimaryIid, const char* FactoryName)
            : FGenericFxObjectBase(PrimaryIid, FactoryName)
        {
        }

        // LAB_06240840 (node +0x20), outer alias LAB_06202740: constant 0x190.
        int DACOM_API GetSortKey() override { return 0x190; }

        // FUN_0623fc90 (outer thunk 062026b0): the node (0xa0 bytes) built inline, then the
        // body of FUN_0623fc30 inline (0x0623fd77..0x0623fdbd); *Out = it, 0.
        GENRESULT DACOM_API CloneNode(IDAComponent** Out) override;

    protected:
        // FUN_0623ff00 (node slot +0x48): operator_new(0x54), FUN_0623f7b0, inner vtable
        // 0x0624f9e0, outer 0x0624f980, count 1, then the adoption tail.
        void CreatePropertySet() override;
    };

} // namespace AlchemyImpl
