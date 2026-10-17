// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "alchemy_types.h"

#include <vector>

// alchemy.dll -- the keyed ramps (see "Keyed ramps" in alchemy_types.h for the five
// retail classes and their interfaces), the IAnimatedSingle weight helper and the
// IAnimatedTransform composer.
//
// ALC-2 (2026-09-26): the single FFxRampObject that used to stand in for FxRampSingle,
// FxRampColor, FxRampSingleRamp and FxRampColorRamp (with a port-only ValueKind_ tag, a
// hermite sampler none of the four has, and a SetKey that copied raw bytes) is gone. Each
// retail vtable now has its own class below, ported from that vtable's own slot bodies.
namespace AlchemyImpl
{
    // GetKeyFactoryName on the flat ramps: `MOV EAX,0xfffffff9; RET 8` (06209130 single,
    // 062097b0 colour) -- a flat ramp has no key objects to create.
    static const GENRESULT kRampNoKeyFactory = static_cast<GENRESULT>(0xFFFFFFF9);

    // The key container every ramp embeds at +8: {vptr, Data (+4), ?, LastIndex (+0xC,
    // count - 1, -1 when empty), Capacity (+0x10)} over 8-byte {float Time; TValue} keys.
    // Value moves only; children are AddRef'd / Released by the owning ramp.
    template <typename TValue>
    class TRampKeyVector
    {
    public:
        struct FKey
        {
            float Time;
            TValue Value;
        };

        TRampKeyVector() : Data_(nullptr), LastIndex_(-1), Capacity_(0)
        {
        }

        ~TRampKeyVector()
        {
            delete[] Data_;
        }

        int Count() const
        {
            return LastIndex_ + 1;
        }

        int LastIndex() const
        {
            return LastIndex_;
        }

        FKey& At(int Index)
        {
            return Data_[Index];
        }

        // Insertion point of AddKey (06208c90, 062098e0, 06209310, 06207650, 06207e00,
        // 06206ea0): the first key whose time is greater than Time. `FLD Time; FCOMP key;
        // TEST AH,5; JNP` leaves the scan only on "less", so equal and unordered keep
        // scanning and a new key lands after keys of the same time.
        int UpperBound(float Time) const
        {
            int Index = 0;
            while (Index <= LastIndex_ && !(Time < Data_[Index].Time))
                ++Index;
            return Index;
        }

        // The append / insert helpers (FUN_0620c300 / FUN_0620c420 and their per-type
        // twins): grow when full, shift up, store, bump LastIndex.
        void InsertAt(int Index, float Time, TValue Value)
        {
            Grow();
            int Slot = LastIndex_ + 1;
            while (Slot > Index)
            {
                Data_[Slot] = Data_[Slot - 1];
                --Slot;
            }
            Data_[Index].Time = Time;
            Data_[Index].Value = Value;
            ++LastIndex_;
        }

        // RemoveKey's memmove of the tail (06208d80 and twins), then LastIndex - 1.
        void EraseAt(int Index)
        {
            int Slot = Index;
            while (Slot < LastIndex_)
            {
                Data_[Slot] = Data_[Slot + 1];
                ++Slot;
            }
            --LastIndex_;
        }

        // Reset's container half: resize to 0 (LastIndex = -1), then, when Capacity is
        // non-zero, free Data and zero Data and Capacity (06208c50 and twins).
        void Free()
        {
            LastIndex_ = -1;
            if (Capacity_ != 0)
            {
                delete[] Data_;
                Data_ = nullptr;
                Capacity_ = 0;
            }
        }

        // SetKeyTime's reorder, one algorithm in all five classes (06208e40, 062094c0,
        // 062078c0, 06208130, 06207110). Returns the key's new index.
        //   `FCOM Old; AND 0x4100; JNZ`: only NewTime > Old (ordered) goes forward.
        //   Forward: last key, or NewTime <= next (TEST AH,0x41 / JNP; unordered moves) ->
        //     write the time in place. Otherwise scan from Index + 1 while the key is not
        //     greater than NewTime (TEST AH,5 / JNP leaves on "less"), counting up.
        //   Backward: `TEST AH,5; JP` -- equal and unordered leave the array untouched.
        //     First key, or NewTime >= previous (AND 0x100 / JZ; unordered moves) -> in
        //     place. Otherwise scan down from Index - 1 while NewTime is not greater than
        //     the key (AND 0x4100 / JZ leaves on "greater"), counting down.
        //   Move: erase the key, insert {NewTime, value} at the new index (after equal
        //     times going forward, before them going backward). The insert never grows.
        int Reposition(int Index, float NewTime)
        {
            const float OldTime = Data_[Index].Time;
            int NewIndex = Index;
            if (NewTime > OldTime)
            {
                if (Index == LastIndex_ || NewTime <= Data_[Index + 1].Time)
                {
                    Data_[Index].Time = NewTime;
                    return NewIndex;
                }
                int Scan = Index + 1;
                while (Scan <= LastIndex_ && !(NewTime < Data_[Scan].Time))
                {
                    ++NewIndex;
                    ++Scan;
                }
            }
            else
            {
                if (!(NewTime < OldTime))
                    return NewIndex;
                if (Index == 0 || NewTime >= Data_[Index - 1].Time)
                {
                    Data_[Index].Time = NewTime;
                    return NewIndex;
                }
                int Scan = Index - 1;
                while (Scan >= 0 && !(NewTime > Data_[Scan].Time))
                {
                    --NewIndex;
                    --Scan;
                }
            }
            const TValue Moved = Data_[Index].Value;
            EraseAt(Index);
            InsertAt(NewIndex, NewTime, Moved);
            return NewIndex;
        }

    private:
        // FUN_0620c300's growth: when Count + 1 exceeds Capacity, the new capacity is
        // Count + max(1, ftol(Count * 0.1f)) (`FILD count; FMUL DAT_0624bdc0 (0.1f)` on
        // the x87 stack, hence the double product here), and the old buffer (Capacity
        // keys) is copied over and freed (FUN_0620c3d0).
        void Grow()
        {
            const int CurrentCount = LastIndex_ + 1;
            if (CurrentCount + 1 <= Capacity_)
                return;
            const double Scaled = static_cast<double>(CurrentCount) * static_cast<double>(0.1f);
            int Step = static_cast<int>(static_cast<long long>(Scaled));
            if (Step <= 1)
                Step = 1;
            const int NewCapacity = CurrentCount + Step;
            if (NewCapacity == Capacity_)
                return;
            FKey* NewData = new FKey[NewCapacity];
            if (Data_ != nullptr)
            {
                int Slot = 0;
                while (Slot < Capacity_)
                {
                    NewData[Slot] = Data_[Slot];
                    ++Slot;
                }
                delete[] Data_;
            }
            Data_ = NewData;
            Capacity_ = NewCapacity;
        }

        FKey* Data_;
        int LastIndex_;
        int Capacity_;
    };

    // FxRampSingle (vtable 0x0624bbf0). Constructed by FUN_06229eb0 / FUN_062446c0 and
    // inline in FUN_06208bc0: Mode 4, empty keys, RefCount 1; InvSpan, the cached segment
    // and the interpolator are left unset by retail until AddKey / SetMode write them (the
    // port zeroes them). The factory path (06242c90) then calls Initialize.
    class FRampSingle final : public IRampSingle
    {
    public:
        // Returns the x87 ST0 value (53-bit precision control), hence double.
        typedef double (*FInterpolator)(float A, float B, float T);

        FRampSingle();

        GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
        unsigned long DACOM_API AddRef() override;
        unsigned long DACOM_API Release() override;

        GENRESULT DACOM_API Reset() override;
        GENRESULT DACOM_API CopyFrom(IRampSingle* Other) override;
        int DACOM_API AddKey(float Time, const float* Value) override;
        GENRESULT DACOM_API RemoveKey(int Index) override;
        double DACOM_API Eval(float Time) override;
        GENRESULT DACOM_API GetKeyTime(int Index, float* Out) override;
        GENRESULT DACOM_API GetKeyValue(int Index, float* Out) override;
        GENRESULT DACOM_API SetKeyTime(int Index, float Time, int* OutNewIndex) override;
        GENRESULT DACOM_API SetKeyValue(int Index, const float* Value) override;
        unsigned long DACOM_API GetMode() override;
        GENRESULT DACOM_API SetMode(unsigned long Mode) override;
        int DACOM_API GetKeyCount() override;
        GENRESULT DACOM_API GetKeyFactoryName(const char** Out) override;
        GENRESULT DACOM_API Initialize(AGGDESC* Desc) override;

    private:
        // The tail AddKey / RemoveKey / SetKeyTime share: cached segment 0, InvSpan 0, and
        // with two or more keys InvSpan = 1 / (t1 - t0) unless that span is exactly zero.
        void ResetEvalCache();

        unsigned long Mode_;          // +0x04
        TRampKeyVector<float> Keys_;  // +0x08
        float InvSpan_;               // +0x1C
        int Cache_;                   // +0x20
        FInterpolator Interpolator_;  // +0x24
        unsigned long RefCount_;      // +0x28
    };

    // FxRampColor (vtable 0x0624bc40): FxRampSingle's layout and search with packed
    // colours (constructed by FUN_06229f00 and inline in FUN_06209240; factory 06242630).
    class FRampColor final : public IRampColor
    {
    public:
        typedef unsigned long (*FInterpolator)(unsigned long A, unsigned long B, float T);

        FRampColor();

        GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
        unsigned long DACOM_API AddRef() override;
        unsigned long DACOM_API Release() override;

        GENRESULT DACOM_API Reset() override;
        GENRESULT DACOM_API CopyFrom(IRampColor* Other) override;
        int DACOM_API AddKey(float Time, const float* Rgb) override;
        GENRESULT DACOM_API RemoveKey(int Index) override;
        float* DACOM_API EvalRgb(float* Out, float Time) override;
        unsigned long DACOM_API EvalPacked(float Time) override;
        GENRESULT DACOM_API GetKeyTime(int Index, float* Out) override;
        GENRESULT DACOM_API GetKeyRgb(int Index, float* Out) override;
        GENRESULT DACOM_API SetKeyTime(int Index, float Time, int* OutNewIndex) override;
        GENRESULT DACOM_API SetKeyRgb(int Index, const float* Rgb) override;
        unsigned long DACOM_API GetMode() override;
        GENRESULT DACOM_API SetMode(unsigned long Mode) override;
        int DACOM_API GetKeyCount() override;
        GENRESULT DACOM_API GetKeyFactoryName(const char** Out) override;
        int DACOM_API AddKeyPacked(float Time, const unsigned long* Packed) override;
        GENRESULT DACOM_API GetKeyPacked(int Index, unsigned long* Out) override;
        GENRESULT DACOM_API SetKeyPacked(int Index, const unsigned long* Packed) override;
        GENRESULT DACOM_API Initialize(AGGDESC* Desc) override;

    private:
        void ResetEvalCache();

        unsigned long Mode_;                  // +0x04
        TRampKeyVector<unsigned long> Keys_;  // +0x08
        float InvSpan_;                       // +0x1C
        int Cache_;                           // +0x20
        FInterpolator Interpolator_;          // +0x24
        unsigned long RefCount_;              // +0x28
    };

    // FxAnimatedSingle (vtable 0x0624bca0; interface and object layout in alchemy_types.h,
    // IFxAnimatedSingle). Every body below is ported from its own slot; the key track's
    // helpers (FUN_06246550 .. FUN_06246950) are private statics.
    class FFxAnimatedSingle final : public IFxAnimatedSingle
    {
    public:
        // Retail's 16-byte key record. The value triple the key slots take and return is
        // {Value, InSlope, OutSlope}.
        struct FKey
        {
            float Value;     // +0x0
            float Time;      // +0x4
            float InSlope;   // +0x8
            float OutSlope;  // +0xC; the bit pattern 0xFFFFFFFF marks a step (06246987)
        };

        // FUN_06243280's 0x14-byte allocation: +0 LoopType (low nibble: behaviour before
        // the first key, high nibble: after the last), +4 a VC6 std::vector of FKey
        // (allocator byte, begin, end, capacity end), held here as a std::vector.
        struct FTrack
        {
            unsigned long LoopType;
            std::vector<FKey> Keys;
        };

        // 06209ad0 / 06243390 / 06227cbd (inline): Weight 1.0f, no track, RefCount 1.
        FFxAnimatedSingle();
        // FUN_06242e70 (reached through 06209b10): when a track exists, its key buffer
        // (FUN_06242ea0) and the track itself are freed.
        ~FFxAnimatedSingle();

        GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
        unsigned long DACOM_API AddRef() override;
        unsigned long DACOM_API Release() override;

        GENRESULT DACOM_API Reset() override;
        GENRESULT DACOM_API CopyFrom(IFxAnimatedSingle* Other) override;
        int DACOM_API GetKeyCount() override;
        GENRESULT DACOM_API SetWeight(float Weight) override;
        GENRESULT DACOM_API GetWeight(float* Out) override;
        int DACOM_API AddKey(float Time, const float* Value) override;
        GENRESULT DACOM_API RemoveKey(int Index) override;
        double DACOM_API Eval(float Time) override;
        GENRESULT DACOM_API GetKeyTime(int Index, float* Out) override;
        GENRESULT DACOM_API GetKeyValue(int Index, float* Out) override;
        GENRESULT DACOM_API SetKeyTime(int Index, float Time, int* OutNewIndex) override;
        GENRESULT DACOM_API SetKeyValue(int Index, const float* Value) override;
        GENRESULT DACOM_API SetLoopType(unsigned long LoopType) override;
        GENRESULT DACOM_API GetLoopType(unsigned long* Out) override;
        GENRESULT DACOM_API ComputeSlopes(unsigned long Mode) override;
        GENRESULT DACOM_API Initialize(AGGDESC* Desc) override;

    private:
        // FUN_06243280: allocate the track on the first AddKey.
        void EnsureTrack();

        // The track's own thiscall helpers.
        static int InsertTrackKey(FTrack* Track, const FKey& Key);              // 06246780
        static int SetTrackKeyTime(FTrack* Track, int Index, float NewTime);    // 06246660
        static void ComputeTrackSlopes(FTrack* Track, unsigned long Mode);      // 06246550
        static double EvalTrack(FTrack* Track, float Time);                     // 062469f0
        static double EvalSegment(const FKey* A, const FKey* B, float Time);    // 06246950

        float Weight_;              // +0x04
        FTrack* Track_;             // +0x08
        unsigned long RefCount_;    // +0x0C
    };

    // One class template for the two scalar nested ramps (see INestedSingleRamp in
    // alchemy_types.h): FxRampSingleRamp (0x0624bb18) and FxAnimatedSingleRamp
    // (0x0624bac8) are the same slot bodies compiled twice over a different child class.
    // Both explicit instantiations live in alchemy_ramp.cpp. Object: +4 Mode (every ctor
    // stores 4 -- FUN_06207490, FUN_06210860, FUN_06210900, the factories 062414e0 /
    // 06241900), +8 keys of {float Time; child*}, +0x1C RefCount.
    template <typename TTraits>
    class TNestedSingleRamp final : public TTraits::FInterface
    {
    public:
        typedef typename TTraits::FChild FChild;

        TNestedSingleRamp();
        ~TNestedSingleRamp();

        GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
        unsigned long DACOM_API AddRef() override;
        unsigned long DACOM_API Release() override;

        GENRESULT DACOM_API Reset() override;
        GENRESULT DACOM_API CopyFrom(INestedSingleRamp* Other) override;
        int DACOM_API AddKey(float Time, IDAComponent* Child) override;
        GENRESULT DACOM_API RemoveKey(int Index) override;
        double DACOM_API Eval(float OuterTime, float InnerTime) override;
        GENRESULT DACOM_API GetKeyTime(int Index, float* Out) override;
        GENRESULT DACOM_API GetKeyValue(int Index, IDAComponent** Out) override;
        GENRESULT DACOM_API SetKeyTime(int Index, float Time, int* OutNewIndex) override;
        GENRESULT DACOM_API SetKeyValue(int Index, IDAComponent* Child) override;
        unsigned long DACOM_API GetMode() override;
        GENRESULT DACOM_API SetMode(unsigned long Mode) override;
        int DACOM_API GetKeyCount() override;
        GENRESULT DACOM_API GetKeyFactoryName(const char** Out) override;
        GENRESULT DACOM_API Initialize(AGGDESC* Desc) override;
        GENRESULT DACOM_API CreateKeyValue(IDAComponent** Out) override;

    private:
        // Release every stored child (the resize-to-0 half of Reset, and the teardown of
        // Release at zero -- FUN_0620cf20 / FUN_0620cde0).
        void ReleaseChildren();

        unsigned long Mode_;              // +0x04
        TRampKeyVector<FChild*> Keys_;    // +0x08
        unsigned long RefCount_;          // +0x1C
    };

    struct FRampSingleRampTraits
    {
        typedef IRampSingleRamp FInterface;
        typedef IRampSingle FChild;
        static const char* Iid() { return Iids::RampSingleRamp; }
        // 06209b40: *Out = "FxRampSingle".
        static const char* KeyFactoryName() { return "FxRampSingle"; }
        // 06207740: `CALL [child_vtable + 0x1c]` with (child, InnerTime).
        static double EvalChild(IRampSingle* Child, float Time) { return Child->Eval(Time); }
        // 06207530: `CALL [copy_vtable + 0x10]` with (copy, source).
        static void CopyChild(IRampSingle* Dst, IRampSingle* Src) { Dst->CopyFrom(Src); }
    };

    struct FAnimatedSingleRampTraits
    {
        typedef IAnimatedSingleRamp FInterface;
        typedef IFxAnimatedSingle FChild;
        static const char* Iid() { return Iids::AnimatedSingleRamp; }
        // 06209b60: *Out = "FxAnimatedSingle".
        static const char* KeyFactoryName() { return "FxAnimatedSingle"; }
        // 06206f90: `CALL [child_vtable + 0x28]` with (child, InnerTime).
        static double EvalChild(IFxAnimatedSingle* Child, float Time) { return Child->Eval(Time); }
        // 06206d80: `CALL [copy_vtable + 0x10]` with (copy, source).
        static void CopyChild(IFxAnimatedSingle* Dst, IFxAnimatedSingle* Src) { Dst->CopyFrom(Src); }
    };

    typedef TNestedSingleRamp<FRampSingleRampTraits> FRampSingleRamp;
    typedef TNestedSingleRamp<FAnimatedSingleRampTraits> FAnimatedSingleRamp;
    extern template class TNestedSingleRamp<FRampSingleRampTraits>;
    extern template class TNestedSingleRamp<FAnimatedSingleRampTraits>;

    // FxRampColorRamp (vtable 0x0624bb68): the FxRampSingleRamp layout over IRampColor*
    // children (ctor FUN_062108b0 / factory 062416f0: Mode 4, RefCount 1).
    class FRampColorRamp final : public IRampColorRamp
    {
    public:
        FRampColorRamp();
        ~FRampColorRamp();

        GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
        unsigned long DACOM_API AddRef() override;
        unsigned long DACOM_API Release() override;

        GENRESULT DACOM_API Reset() override;
        GENRESULT DACOM_API CopyFrom(IRampColorRamp* Other) override;
        int DACOM_API AddKey(float Time, IRampColor* Child) override;
        GENRESULT DACOM_API RemoveKey(int Index) override;
        float* DACOM_API EvalRgb(float* Out, float OuterTime, float InnerTime) override;
        unsigned long DACOM_API EvalPacked(float OuterTime, float InnerTime) override;
        GENRESULT DACOM_API GetKeyTime(int Index, float* Out) override;
        GENRESULT DACOM_API GetKeyValue(int Index, IRampColor** Out) override;
        GENRESULT DACOM_API SetKeyTime(int Index, float Time, int* OutNewIndex) override;
        GENRESULT DACOM_API SetKeyValue(int Index, IRampColor* Child) override;
        unsigned long DACOM_API GetMode() override;
        GENRESULT DACOM_API SetMode(unsigned long Mode) override;
        int DACOM_API GetKeyCount() override;
        GENRESULT DACOM_API GetKeyFactoryName(const char** Out) override;
        GENRESULT DACOM_API Initialize(AGGDESC* Desc) override;
        GENRESULT DACOM_API CreateKeyValue(IDAComponent** Out) override;

    private:
        void ReleaseChildren();

        unsigned long Mode_;                  // +0x04
        TRampKeyVector<IRampColor*> Keys_;    // +0x08
        unsigned long RefCount_;              // +0x1C
    };

    // The nested ramps' CreateKeyValue tail (06207be0, 06208450, 06207430): DACOM_Acquire()
    // then ICOManager +0x0C CreateInstance({8, Name}, Out); its result is returned as-is.
    GENRESULT CreateRampKeyValue(const char* Name, IDAComponent** Out);

    // The descriptor / property builders' ramp helpers:
    //   FUN_06208bc0(Value, Out): new FxRampSingle, SetMode(4), AddKey(0, &Value).
    //   FUN_06209240(Rgb, Out):   new FxRampColor,  SetMode(4), AddKey(0, Rgb).
    //   FUN_06210900(Weight, Out): new FxAnimatedSingleRamp holding one FxAnimatedSingle
    //     (SetWeight(Weight)) at time 0; the local child reference is released.
    // Each hands its construction reference to *Out, as retail does.
    void MakeRampSingle(float Value, IRampSingle** Out);
    void MakeRampColor(const float* Rgb, IRampColor** Out);
    void MakeAnimatedSingleRamp(float Weight, INestedSingleRamp** Out);
    // FUN_06208bc0 + a fresh FxRampSingleRamp (FUN_06207490 / FUN_06210860 + vtable
    // 0x0624bb18) + AddKey(0, child) + release of the local child: the one-key nested
    // scalar ramp every descriptor builder repeats inline (062324a3..06232516 and twins).
    void MakeRampSingleRamp(float Value, INestedSingleRamp** Out);
    // The same with FUN_06209240 under a fresh FxRampColorRamp (06232408..06232493).
    void MakeRampColorRamp(const float* Rgb, IRampColorRamp** Out);

    // FFxAnimatedTransform: the IAnimatedTransform component (alchemy_types.h
    // has the full disassembly trace for the interface shape/vtable
    // 0x0624bbb8). Retail composes an animated Translate(3)+Rotate(3)+Scale(3)
    // from 9 fixed per-channel FxAnimatedSingleRamp children.
    class FFxAnimatedTransform final : public IAnimatedTransform
    {
    public:
        // FUN_06208620 -> FUN_062434c0, then vtable 0x0624bbb8 and RefCount (+0x30) = 1.
        FFxAnimatedTransform();

        // FUN_0620d310 -> 06246194: children 8..0, Release then clear each slot.
        // The remaining earlier children also unwind through 062461fc/06246214.
        ~FFxAnimatedTransform() noexcept(false)
        {
            struct FChildCleanup
            {
                FAnimatedSingleRamp** Children;
                unsigned long Remaining;

                ~FChildCleanup() noexcept(false) { ReleaseRemaining(); }

                void ReleaseRemaining()
                {
                    while (Remaining != 0)
                    {
                        --Remaining;
                        FAnimatedSingleRamp*& Child = Children[Remaining];
                        if (Child != nullptr)
                        {
                            Child->Release();
                            Child = nullptr;
                        }
                    }
                }
            };

            FChildCleanup Cleanup = {Children_, kChannelCount};
            Cleanup.ReleaseRemaining();
        }

        GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override;
        unsigned long DACOM_API AddRef() override { return ++RefCount_; }
        unsigned long DACOM_API Release() override;

        // +0xC FUN_062436a0: see IAnimatedTransform's class comment for the
        // full trace. Rebuilds every channel to a fresh single default key
        // (Weight = kChannelDefaults[i]) and force-sets ChannelFlags=0x1FF.
        GENRESULT DACOM_API Reset() override;

        // +0x10 LAB_06243770: see IAnimatedTransform's class comment for the
        // full trace: per channel Children_[i]->CopyFrom(Other's child), then
        // PackedModes/ChannelFlags.
        GENRESULT DACOM_API CopyFrom(IAnimatedTransform* Other) override;

        // +0x14 LAB_06243830: `*Out = Children_[Index]; (*Out)->AddRef();`
        GENRESULT DACOM_API GetChild(unsigned long Index, IDAComponent** Out) override;

        // +0x18 FUN_06243850: see IAnimatedTransform's class comment for the
        // full byte trace of this genuinely subtle algorithm.
        GENRESULT DACOM_API UpdateChannelFlags() override;

        // +0x1C LAB_062439b0: *Out = [+8], returns 0.
        GENRESULT DACOM_API GetChannelFlags(unsigned long* Out) override
        {
            *Out = ChannelFlags_;
            return GR_OK;
        }

        // +0x20 LAB_062439d0: `return ChannelFlags_ == 0x1FF;`
        unsigned char DACOM_API IsIdentity() override
        {
            return ChannelFlags_ == 0x1FFu ? 1 : 0;
        }

        // +0x24 LAB_062439f0: packs 3 nibbles, no masking on write (matches
        // retail -- a caller value >0xF bleeds into the adjacent nibble).
        GENRESULT DACOM_API SetPackedModes(unsigned long A, unsigned long B,
                                           unsigned long C) override
        {
            PackedModes_ = (((A << 4) | B) << 4) | C;
            return GR_OK;
        }

        // +0x28 LAB_06243a20: unpacks the same 3 nibbles (top/mid/low),
        // masked 0xF on read; no null tests.
        GENRESULT DACOM_API GetPackedModes(unsigned long* A, unsigned long* B,
                                           unsigned long* C) override
        {
            *A = (PackedModes_ >> 8) & 0xF;
            *B = (PackedModes_ >> 4) & 0xF;
            *C = PackedModes_ & 0xF;
            return GR_OK;
        }

        // +0x2C LAB_06243a60: see the body in alchemy_ramp.cpp.
        void DACOM_API Evaluate(unsigned long TimeA, unsigned long TimeB, float* Out12) override;

        // +0x30 LAB_06243690: always-success 1-arg stub, no observable
        // retail behavior beyond returning GR_OK.
        GENRESULT DACOM_API Slot0x30(unsigned long /*Arg*/) override { return GR_OK; }

    private:
        static const unsigned long kChannelCount = 9;
        // Translate X/Y/Z, Rotate X/Y/Z (degrees), Scale X/Y/Z -- read
        // directly from retail's DAT_062575e8 (alchemy.dll.txt:180638-180672,
        // 9 dwords: 0,0,0,0,0,0,0x3f800000,0x3f800000,0x3f800000).
        static const float kChannelDefaults[kChannelCount];

        // FUN_06243c50 (thiscall on the 3x3 block of Out12): the rotation from three
        // angles in degrees (Evaluate passes child 3, child 5, child 4).
        static void BuildEulerRotation3x3(float* Out9, const float& AngleA,
                                         const float& AngleB, const float& AngleC);

        // Retail keeps RefCount at +0x30, after the children; the order here is the
        // port's own (no raw offset reaches this object).
        unsigned long RefCount_;
        unsigned long PackedModes_;              // +0x04
        unsigned long ChannelFlags_;             // +0x08
        FAnimatedSingleRamp* Children_[kChannelCount]; // +0x0C
    };

} // namespace AlchemyImpl
