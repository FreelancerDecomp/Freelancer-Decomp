// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "alchemy_genericfx.h"

// alchemy.dll — DACOM particle FX scaffolding.
//
// The 7 "generic" field leaves (RegisterComponent names FxGravityField/
// FxAirField/FxRadialField/FxTurbulenceField/FxCollideField/FLDustField/
// FLBeamField). Each overrides FGenericFxObjectBase's IGenericFx
// BeginField/ApplyFieldToVel pair with its own logic and owns only the
// state that type actually needs — this used to be ONE class
// (FGenericFxObject) with every field type's state (FieldMagnitude_/
// FieldApproach_/FieldAtten_/FieldCache_/FieldPoint_/FieldReflect_) crammed
// onto the same object regardless of which type the instance actually was,
// dispatching via `_stricmp(FactoryName_, "TypeName") == 0` string branches
// in BeginField/ApplyFieldToVel. That shape hid a real bug: FLDustField's
// old BeginField branch never wrote FieldMagnitude_ (it only set
// FieldPoint_ via TransformPoint), so ApplyFieldToVel's
// `FieldMagnitude_ > 0.0f` guard was always false and the entire
// kill-by-radius block was silently dead code for every Dust field
// instance, in every level, since the type was first ported. See
// FLDustField below for the fix (and the honest state of what could and
// could not be re-derived from retail this wave).
//
// See alchemy_genericfx.h's file-header comment for the full class-
// hierarchy-split rationale (this is one of the 3 leaf families it lists).
namespace AlchemyImpl
{
    // ALC-4 / ALC-6 (2026-09-30): every field's properties are ramps held by its property
    // set (tables: gravity .data 0x06256240, air 0x06255af4, radial 0x062569c8,
    // turbulence 0x06256ccc, collide 0x06255d78, dust 0x06255aa8), evaluated once per
    // frame in the field's own node update (outer +0x14, the port's Pad5) into node floats
    // that Begin/Apply read. The sets below hold them (shared with clones); the reader
    // fills the ramps in place through the getters.

    // Gravity set (create FUN_06223340, 0x58 bytes with the outer).
    class FFxGravityFieldPropertySet final : public FFxNodePropertySet
    {
    public:
        FFxGravityFieldPropertySet();
        ~FFxGravityFieldPropertySet() override;
        GENRESULT DACOM_API GetPropAnimatedSingleRamp(unsigned long Hash,
                                                      INestedSingleRamp** Out) override;
        int DACOM_API EnumerateProps(FNodePropertyRowCallback Callback, void* Context) override;
        INestedSingleRamp* GravityRamp; // +0x4c GravityField_Gravity 0xE02B8BD4, 9.8
    };

    // Air set (create FUN_06214010, 0x5c bytes).
    class FFxAirFieldPropertySet final : public FFxNodePropertySet
    {
    public:
        FFxAirFieldPropertySet();
        ~FFxAirFieldPropertySet() override;
        GENRESULT DACOM_API GetPropAnimatedSingleRamp(unsigned long Hash,
                                                      INestedSingleRamp** Out) override;
        int DACOM_API EnumerateProps(FNodePropertyRowCallback Callback, void* Context) override;
        INestedSingleRamp* ApproachRamp;  // +0x4c AirField_Approach 0x10423CEB, 1.0
        INestedSingleRamp* MagnitudeRamp; // +0x50 AirField_Magnitude 0xE5E3524C, 1.0
    };

    // Radial set (inner ctor FUN_06235f50, create FUN_06236280, 0x64 bytes).
    class FFxRadialFieldPropertySet final : public FFxNodePropertySet
    {
    public:
        FFxRadialFieldPropertySet();
        ~FFxRadialFieldPropertySet() override;
        GENRESULT DACOM_API GetPropSingleRampRamp(unsigned long Hash,
                                                  INestedSingleRamp** Out) override;
        GENRESULT DACOM_API GetPropAnimatedSingleRamp(unsigned long Hash,
                                                      INestedSingleRamp** Out) override;
        int DACOM_API EnumerateProps(FNodePropertyRowCallback Callback, void* Context) override;
        INestedSingleRamp* MagnitudeRamp; // +0x4c RadialField_Magnitude 0x0064B3B9, 10
        INestedSingleRamp* ApproachRamp;  // +0x50 RadialField_Approach 0xF3CF5EA5, 0.3
        INestedSingleRamp* RadiusRamp;    // +0x54 RadialField_Radius 0xFF28F620, 50
        INestedSingleRamp* FalloffRamp;   // +0x58 RadialField_Attenuation 0x1A433168 (0x200)
    };

    // Turbulence set (create FUN_0623d4b0, 0x5c bytes).
    class FFxTurbulenceFieldPropertySet final : public FFxNodePropertySet
    {
    public:
        FFxTurbulenceFieldPropertySet();
        ~FFxTurbulenceFieldPropertySet() noexcept(false) override;
        GENRESULT DACOM_API GetPropAnimatedSingleRamp(unsigned long Hash,
                                                      INestedSingleRamp** Out) override;
        int DACOM_API EnumerateProps(FNodePropertyRowCallback Callback, void* Context) override;
        INestedSingleRamp* ApproachRamp;  // +0x4c TurbulenceField_Approach 0x1B970B9A, 1
        INestedSingleRamp* MagnitudeRamp; // +0x50 TurbulenceField_Magnitude 0x1CEAC6D1, 10
    };

    // Collide set (inner ctor FUN_06219a30, create FUN_06219d30, 0x60 bytes).
    class FFxCollideFieldPropertySet final : public FFxNodePropertySet
    {
    public:
        FFxCollideFieldPropertySet();
        ~FFxCollideFieldPropertySet() noexcept(false) override;
        GENRESULT DACOM_API GetPropAnimatedSingleRamp(unsigned long Hash,
                                                      INestedSingleRamp** Out) override;
        int DACOM_API EnumerateProps(FNodePropertyRowCallback Callback, void* Context) override;
        INestedSingleRamp* ReflectRamp; // +0x4c CollideField_Reflectivity 0xF2468210, 0.7
        INestedSingleRamp* WidthRamp;   // +0x50 CollideField_Width 0x138BDB51, 10
        INestedSingleRamp* HeightRamp;  // +0x54 CollideField_Height 0x11336C98, 10
    };

    // Dust field set (inner ctor FUN_06212810, create FUN_06212960, 0x58 bytes).
    class FFxDustFieldPropertySet final : public FFxNodePropertySet
    {
    public:
        FFxDustFieldPropertySet();
        ~FFxDustFieldPropertySet() override;
        GENRESULT DACOM_API GetPropAnimatedSingleRamp(unsigned long Hash,
                                                      INestedSingleRamp** Out) override;
        int DACOM_API EnumerateProps(FNodePropertyRowCallback Callback, void* Context) override;
        INestedSingleRamp* RadiusRamp; // +0x4c "SphereEmitter_MaxRadius" 0x1874DE74, 60.1
    };

    // retail update LAB_06223dd0 (outer +0x14; dead-table twin LAB_062235c0), Begin
    // LAB_062241a0, Apply LAB_06224160 (vel += cache).
    class FxGravityField final : public FGenericFxObjectBase
    {
    public:
        FxGravityField(const char* PrimaryIid, const char* FactoryName);
        ~FxGravityField() override;

        const char* DACOM_API GetFxFactoryName() override
        {
            return GravityPropertySet_->ClassName.Buffer;
        }

        GENRESULT DACOM_API Pad5(float Dt) override;
        // FUN_062234e0 (outer thunk 06223e50).
        GENRESULT DACOM_API CloneNode(IDAComponent** Out) override;
        void DACOM_API BeginField(IDAComponent* Appearance, float Dt) override;
        void DACOM_API ApplyFieldToVel(float* PosVel6, float* Age, float Dt) override;

    protected:
        IAnimatedTransform* FindNodeAnimatedTransform() const override
        {
            return GravityPropertySet_->Transform;
        }
        void CreatePropertySet() override;

    private:
        FFxGravityFieldPropertySet* GravityProps() const
        {
            return GravityPropertySet_;
        }

        FFxGravityFieldPropertySet* GravityPropertySet_; // inner +0x9c, non-owning cache
        float Gravity_;  // node+0xa0: Eval(GravityField_Gravity) * Dt
        float Cache_[3]; // node+0xa4..+0xac
    };

    // retail update LAB_062147b0 (outer +0x14; dead-table twin LAB_062142c0), Begin
    // LAB_06214bf0, Apply LAB_06214b80 (lerp toward cache).
    class FxAirField final : public FGenericFxObjectBase
    {
    public:
        FxAirField(const char* PrimaryIid, const char* FactoryName);
        ~FxAirField() override;

        const char* DACOM_API GetFxFactoryName() override
        {
            return AirPropertySet_->ClassName.Buffer;
        }

        GENRESULT DACOM_API Pad5(float Dt) override;
        // FUN_062141c0 (outer thunk 06214870).
        GENRESULT DACOM_API CloneNode(IDAComponent** Out) override;
        void DACOM_API BeginField(IDAComponent* Appearance, float Dt) override;
        void DACOM_API ApplyFieldToVel(float* PosVel6, float* Age, float Dt) override;

    protected:
        IAnimatedTransform* FindNodeAnimatedTransform() const override
        {
            return AirPropertySet_->Transform;
        }
        void CreatePropertySet() override;

    private:
        FFxAirFieldPropertySet* AirProps() const
        {
            return AirPropertySet_;
        }

        FFxAirFieldPropertySet* AirPropertySet_; // inner +0x9c, non-owning cache
        float Approach_;  // node+0xa0
        float Magnitude_; // node+0xa4
        float Blend_;     // node+0xa8: min(Dt * Approach * 4, 1)
        float Cache_[3];  // node+0xac..+0xb4
    };

    // retail LAB_06236ea0 (Begin: cache = world origin) / LAB_06236d90 (Apply:
    // pull toward cache, attenuated, falloff-ramped). Owns the falloff ramp
    // (FUN_06235f50 / FxRampSingleRamp, wave 12 alchemy-wave12-lane2) that
    // used to be InitRadialFalloffRamp() on the shared base class; it is now
    // a private method directly on this leaf since only FxRadialField needs
    // it (see alchemy_genericfx.h's file-header comment).
    //
    // wave111:radial-field-a0 (2026-09-15, closes item 134549's this+0xa0
    // contradiction): retail's real Begin (LAB_06236450) and Apply
    // (LAB_06236d90) are reached through two DIFFERENT vtables of the same
    // multiply-inherited object -- Begin only via the secondary interface
    // at object+4 (proved by the LAB_06236a30 "ECX+=4; JMP LAB_06236450"
    // adjustor thunk sitting in the *primary* vtable next to Apply's own
    // direct slot; both of Begin's two call paths converge on identical
    // code that is therefore provably always invoked with this=object+4),
    // while Apply sits directly in the primary vtable at object+0. Every
    // prior wave on this pair (12/37/53/94/110's own first pass) read both
    // functions' literal "this+0xNN" offsets as if they shared one frame;
    // they don't -- Begin's need +4 added to land on Apply's (object-
    // relative) frame. Once corrected, Begin's own this+0x9c/+0xa0/+0xa4/
    // +0xa8/+0xac land on object+0xa0/+0xa4/+0xa8/+0xac/+0xb0 respectively,
    // which resolves the apparent pointer/float aliasing on "+0xa0" (they
    // are different bytes) and reassigns which Begin-computed value feeds
    // which Apply read:
    //   object+0xa0 = RampsObj (a real pointer, written once at
    //                 construction by FUN_06236280, also frame-shifted;
    //                 Apply's FalloffRamp_ wiring off this was already
    //                 correct and is unaffected).
    //   object+0xa8 (Apply's Target-magnitude multiply) = Begin's own
    //                 this+0xa4 = -Eval(RadialField_Magnitude ramp) --
    //                 i.e. retail unconditionally negates Magnitude before
    //                 ever multiplying it into Target.
    //   object+0xac (Apply's LerpVel3-equivalent blend factor) = Begin's
    //                 own this+0xa8 = min(Dt * RadialField_Attenuation *
    //                 4.0f, 1.0f) -- NOT the flat RadialField_Approach
    //                 property as previously ported.
    //   object+0xb0 (Apply's Dist normalizer) = Begin's own this+0xac =
    //                 1.0f / RadialField_Approach -- NOT
    //                 RadialField_Attenuation as previously ported.
    // Implemented below: Blend_/DistScale_ are the cached, Begin-time
    // equivalents of object+0xac/+0xb0; Magnitude_ is negated at point of
    // use in ApplyFieldToVel. (Superseded naming, see the ALC-4 note below.)
    //
    // ALC-4 (2026-09-30): the property names above were crossed. The descriptor's
    // property table (.data 0x062569c8, {name, hash, kind, ..., min, max}) names the
    // four ramps FUN_06235f50 builds (descriptor-inner offsets):
    //   +0x4c RadialField_Magnitude   0x0064B3B9 kind 0x202, default 10.0
    //   +0x50 RadialField_Approach    0xF3CF5EA5 kind 0x202, default 0.3
    //   +0x54 RadialField_Radius      0xFF28F620 kind 0x202, default 50.0
    //   +0x58 RadialField_Attenuation 0x1A433168 kind 0x200, keys (0, 1.0) (1.0, 0.001)
    // So Begin's 1/x is 1/Radius, its blend is Dt * Approach * 4, and the falloff ramp
    // Apply evaluates is the Attenuation ramp. They are ramps (kinds 0x200 / 0x202),
    // which the old port read as kind 2/3 floats from Props_ -- never present, so every
    // radial field ran on the defaults. The ramps live in the radial set (ALC-6), filled
    // through the set's getters (LAB_06236200 / LAB_06236250).
    class FxRadialField final : public FGenericFxObjectBase
    {
    public:
        FxRadialField(const char* PrimaryIid, const char* FactoryName);
        ~FxRadialField() override;

        const char* DACOM_API GetFxFactoryName() override
        {
            return RadialPropertySet_->ClassName.Buffer;
        }

        GENRESULT DACOM_API Pad5(float Dt) override;
        // FUN_06236360 (outer thunk 06236a80).
        GENRESULT DACOM_API CloneNode(IDAComponent** Out) override;
        void DACOM_API BeginField(IDAComponent* Appearance, float Dt) override;
        void DACOM_API ApplyFieldToVel(float* PosVel6, float* Age, float Dt) override;

    protected:
        IAnimatedTransform* FindNodeAnimatedTransform() const override
        {
            return RadialPropertySet_->Transform;
        }
        void CreatePropertySet() override;

    private:
        FFxRadialFieldPropertySet* RadialProps() const
        {
            return RadialPropertySet_;
        }

        FFxRadialFieldPropertySet* RadialPropertySet_; // inner +0x9c, non-owning cache
        float Cache_[3];
        // Begin-time values Apply consumes (retail node+0xa0..+0xac): Approach, the
        // magnitude (retail stores it negated), the blend and 1 / Radius.
        float Magnitude_;
        float Approach_;
        float Blend_;
        float DistScale_;
    };

    // retail update LAB_0623dfa0 (outer +0x14; dead-table twin LAB_0623d710), Begin
    // LAB_0623e4a0 (XOR EAX,EAX; RET 8), Apply LAB_0623e370 (random unit direction *
    // random * magnitude, lerp).
    class FxTurbulenceField final : public FGenericFxObjectBase
    {
    public:
        FxTurbulenceField(const char* PrimaryIid, const char* FactoryName);
        ~FxTurbulenceField() override;

        const char* DACOM_API GetFxFactoryName() override
        {
            return TurbulencePropertySet_->ClassName.Buffer;
        }

        GENRESULT DACOM_API Pad5(float Dt) override;
        // FUN_0623d640 (outer thunk 0623e060).
        GENRESULT DACOM_API CloneNode(IDAComponent** Out) override;
        void DACOM_API BeginField(IDAComponent* Appearance, float Dt) override;
        void DACOM_API ApplyFieldToVel(float* PosVel6, float* Age, float Dt) override;

    protected:
        IAnimatedTransform* FindNodeAnimatedTransform() const override
        {
            return TurbulencePropertySet_->Transform;
        }
        void CreatePropertySet() override;

    private:
        FFxTurbulenceFieldPropertySet* TurbulenceProps() const
        {
            return TurbulencePropertySet_;
        }

        FFxTurbulenceFieldPropertySet* TurbulencePropertySet_; // inner +0x9c, non-owning cache
        float Approach_;  // node+0xa0
        float Blend_;     // node+0xa4: min(Dt * Approach * 4, 1)
        float Magnitude_; // node+0xa8
    };

    // retail update LAB_0621a780 (outer +0x14), Begin LAB_0621ab20, Apply LAB_0621a0e0
    // (reached through the ECX-less thunk LAB_0621ab10, arg1 += 4, so it runs in the node
    // frame). Begin caches, in the node: +0xa8 the unit normal (the field's local +Y
    // rotated into the appearance's space and normalised), +0xb4 the local point
    // (1, 0, 1) transformed, +0xc0 the normal * 5, and +0xcc the slot-7 transform
    // itself. Apply is a one-frame sweep: signed distances against the +0xb4 point,
    // a 5-unit margin along the normal, the crossing fraction T, reflection of the
    // velocity direction scaled by -Speed * Reflectivity, and a final position step back
    // by (1 - T) * Dt along the new velocity. See the method bodies for the term-by-term
    // trace (ALC-5, 2026-09-30; the old "Point_" origin and the Point_-for-Corner_
    // substitution were a lookalike -- retail never transforms the origin here).
    class FxCollideField final : public FGenericFxObjectBase
    {
    public:
        FxCollideField(const char* PrimaryIid, const char* FactoryName);
        ~FxCollideField() override;

        // 0621a7e0 reads the published set view, which can precede its owning swap.
        const char* DACOM_API GetFxFactoryName() override
        {
            return CollidePropertySet_->ClassName.Buffer;
        }

        // ALC-4: retail update LAB_0621a780 (outer +0x14): TickLifetime, node+0xa0 =
        // Eval(CollideField_Reflectivity), node+0xa4 = Dt.
        GENRESULT DACOM_API Pad5(float Dt) override;
        // FUN_06219e10 (outer thunk 0621a800).
        GENRESULT DACOM_API CloneNode(IDAComponent** Out) override;
        void DACOM_API BeginField(IDAComponent* Appearance, float Dt) override;
        void DACOM_API ApplyFieldToVel(float* PosVel6, float* Age, float Dt) override;
        // Getter LAB_06219cb0: each ramp answers to its table hash and an alias
        // (0xE462B789 / 0x061AAF28 / 0x16FF3474).

    protected:
        IAnimatedTransform* FindNodeAnimatedTransform() const override
        {
            return CollidePropertySet_->Transform;
        }
        void CreatePropertySet() override;

    private:
        FFxCollideFieldPropertySet* CollideProps() const
        {
            return CollidePropertySet_;
        }

        FFxCollideFieldPropertySet* CollidePropertySet_; // inner +0x9c, non-owning cache
        float Reflect_;  // node+0xa0: Eval(CollideField_Reflectivity), this frame
        float UpdateDt_; // node+0xa4: the Dt of the last update (Apply's sweep time)
        float Normal_[3];       // node+0xa8: unit plane normal
        float Corner_[3];       // node+0xb4: TransformXform(slot 7, (1, 0, 1))
        float ScaledNormal_[3]; // node+0xc0: Normal_ * 5.0f (DAT_0624d160)
        float BeginXform_[12];  // node+0xcc: slot 7 output (no reader besides Begin)
    };

    // retail LAB_0620ed20 (Begin) / LAB_0620ed30 (Apply): both are genuinely
    // `XOR EAX,EAX RET` no-ops in retail — confirmed by direct disasm read.
    // The base class's inherited no-op BeginField/ApplyFieldToVel is already
    // byte-correct for this type, so this leaf adds nothing beyond the
    // trivial forwarding constructor.
    // Its property set is the base set (create FUN_06244fb0: FUN_0623f7b0 under the field
    // vtables 0x0624fda8 / 0x0624fcf0, bodies identical to the base's), which the
    // inherited CreatePropertySet builds.
    class FLBeamField final : public FGenericFxObjectBase
    {
    public:
        FLBeamField(const char* PrimaryIid, const char* FactoryName)
            : FGenericFxObjectBase(PrimaryIid, FactoryName)
        {
        }

        // FUN_0620e510 (outer thunk 0620ea10).
        GENRESULT DACOM_API CloneNode(IDAComponent** Out) override;
    };

    // FLDustField (ALC-5, 2026-09-30; the earlier "no property exists, Radius_ stays 0"
    // conclusion was wrong). Its descriptor (inner ctor FUN_06212810, create-descriptor
    // FUN_06212960 -- node slot +0x48, which FUN_0623fde0 calls on every factory create,
    // so node+0x9c is always set) holds ONE kind-0x202 ramp at inner +0x4c, keyed
    // FxAnimatedSingle 60.1 (0x42706666). Its property table (.data 0x06255aa8) names it
    // "SphereEmitter_MaxRadius", hash 0x1874DE74, min 60.1 -- the dust field reuses the
    // sphere emitter's property name; the getter is LAB_06212930.
    //   update LAB_06212c20 (outer +0x14): node+0xd8 = Dt, TickLifetime, R = Eval ->
    //     node+0xa0 (float), R*R -> node+0xa4 (float).
    //   Begin LAB_06212c70: slot 7 with the appearance into node+0xa8 (nothing else).
    //   Apply LAB_06212c90: the particle position in the field's frame (math +0x3c
    //     InverseTransform by node+0x4c); outside R^2 the particle's age is set to
    //     FLT_MAX (killed); inside, the age becomes 10 * (1 - (R - z) / 2R) * fade, where
    //     fade drops from 1 to 0 across the outer 20% of R^2 on the z < 0 side.
    class FLDustField final : public FGenericFxObjectBase
    {
    public:
        FLDustField(const char* PrimaryIid, const char* FactoryName);
        ~FLDustField() override;

        // 062137d0 reads the view published before the owning set swap.
        const char* DACOM_API GetFxFactoryName() override
        {
            return DustPropertySet_->ClassName.Buffer;
        }

        GENRESULT DACOM_API Pad5(float Dt) override;
        // FUN_06212b00 (outer thunk 062137f0).
        GENRESULT DACOM_API CloneNode(IDAComponent** Out) override;
        void DACOM_API BeginField(IDAComponent* Appearance, float Dt) override;
        void DACOM_API ApplyFieldToVel(float* PosVel6, float* Age, float Dt) override;
        // LAB_06212930 (RET 0xc): 0x1874DE74 -> set +0x4c, AddRef'd; else -8.

    protected:
        IAnimatedTransform* FindNodeAnimatedTransform() const override
        {
            return DustPropertySet_->Transform;
        }
        void CreatePropertySet() override;

    private:
        FFxDustFieldPropertySet* DustProps() const
        {
            return DustPropertySet_;
        }

        FFxDustFieldPropertySet* DustPropertySet_; // inner +0x9c, non-owning cache
        float Radius_;          // node+0xa0: Eval(radius ramp), this frame
        float RadiusSq_;        // node+0xa4: Radius^2 (float)
        float BeginXform_[12];  // node+0xa8: slot 7 output (no reader besides Begin)
        float UpdateDt_;        // node+0xd8: the Dt of the last update
    };

} // namespace AlchemyImpl
