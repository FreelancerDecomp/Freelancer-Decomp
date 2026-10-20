// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Thorn.h"
#include "SplineMath.h"
#include "ExtendedProgressCodec.h"
#include "ParamCurveMath.h"
#include "CurvePolynomialMath.h"
#include "HermiteRecord.h"
#include "ExtendedFloatStorage.h"
#include "PortableComparison.h"

#include <cmath>
#include <cstring>
#include <list>
#include <map>
#include <new>
#include <memory>

// IParamCurve — three 1D evaluators from docs/re/thorn.md.
// Slot 6 add-key (4 floats, t∈[0,1]) is thiscall RET 0x10 (LAB_06f2f560).

namespace
{
    enum ECurveEval
    {
        EvalCatmullRom,
        EvalLerp,
        EvalHermite,
    };

    struct FParamKey
    {
        float T;
        float Value;
        float Extra0;
        float Extra1;
        int Id;
    };

    typedef std::map<int, FParamKey*> FParamKeyMap;

    struct FParamKeyStorage
    {
        FParamKeyMap Items;
        bool bBaseConstructed = false;

        ~FParamKeyStorage()
        {
            // Base EH destroys nodes only until both endpoint adoptions finish.
            // A completed base owns payloads, including during derived unwind.
            if (bBaseConstructed)
            {
                for (FParamKeyMap::iterator Key = Items.begin(); Key != Items.end(); ++Key)
                    delete Key->second;
            }
        }
    };

    void StoreParamFloat(FThornExtendedProgress* Output, const float* Value)
    {
        ThornPortableMath::StoreFloatToExtended80(Output->Bytes, Value);
    }

    typedef std::list<FParamKey> FParamEvaluationList;

    bool ParamStrictLess(const float* Left, const float* Right)
    {
        return ThornPortableComparison::StrictLess(Left, Right);
    }

    void MergeEvaluationLists(FParamEvaluationList& Destination, FParamEvaluationList& Source)
    {
        // 310d0 and both inlined merges in 30d20: only a strictly lesser
        // source moves before the current destination. Never std::list::sort:
        // NaNs make the order dependent on the exact merge schedule.
        if (&Destination == &Source)
            return;
        FParamEvaluationList::iterator Into = Destination.begin();
        FParamEvaluationList::iterator From = Source.begin();
        while (Into != Destination.end())
        {
            if (From == Source.end())
                return;
            if (ParamStrictLess(&From->T, &Into->T))
            {
                FParamEvaluationList::iterator Next = From;
                ++Next;
                Destination.splice(Into, Source, From);
                From = Next;
            }
            else
            {
                ++Into;
            }
        }
        Destination.splice(Destination.end(), Source, From, Source.end());
    }

    void SortEvaluationList(FParamEvaluationList& Values)
    {
        // 30d20: carry is constructed before all sixteen buckets. The
        // saturation branch merges into bucket15 without extending Fill;
        // the final descending loop visits Fill-1..0 exactly as retail.
        if (Values.size() < 2)
            return;
        FParamEvaluationList Carry;
        FParamEvaluationList Buckets[16];
        unsigned int Fill = 0;
        while (!Values.empty())
        {
            Carry.splice(Carry.begin(), Values, Values.begin());
            unsigned int Bucket = 0;
            while (Bucket < Fill && !Buckets[Bucket].empty())
            {
                MergeEvaluationLists(Buckets[Bucket], Carry);
                Buckets[Bucket].swap(Carry);
                ++Bucket;
            }
            if (Bucket == 15)
            {
                MergeEvaluationLists(Buckets[15], Carry);
            }
            else
            {
                Buckets[Bucket].swap(Carry);
                if (Bucket == Fill)
                    ++Fill;
            }
        }
        while (Fill != 0)
        {
            --Fill;
            MergeEvaluationLists(Values, Buckets[Fill]);
        }
    }

    using ThornCurveDetail::FHermiteKnot;
    using ThornCurveDetail::AssignHermiteRecord;

    struct FCurveSpec
    {
        const char* FactoryName;
        const char* DisplayName;
        ECurveEval Eval;
        int HermiteType;
        bool bClampNonNegative;
    };

    const float G_MinDeltaT = 1.0e-4f;

    union FIntFloat
    {
        int AsInt;
        float AsFloat;
    };

    const float G_ScalarOne = 1.0f;
    const float G_ScalarHalfFloat = 0.5f;
    const float G_ScalarThree = 3.0f;
    const float G_ScalarFour = 4.0f;
    const float G_ScalarFive = 5.0f;
    const double G_ScalarHalf = 0.5;

    // 2f2f0: preserve the complete extended x87 value in typed output, including
    // the caller-supplied nullable outside values. A C++ float polynomial
    // would round U²/U³ and the intermediate sums before the retail stores.
    __declspec(naked) void __cdecl CatmullRom1D(const float* P0, const float* P1,
                                               const float* P2, const float* P3, float U, FThornExtendedProgress* Output)
    {
        __asm
        {
            fld dword ptr [esp + 14h]
            mov eax, dword ptr [esp + 4]
            test eax, eax
            fmul dword ptr [esp + 14h]
            fld dword ptr [esp + 14h]
            push esi
            mov esi, dword ptr [esp + 0ch]
            fmul st(0), st(1)
            jnz ScalarLeftPresent
            mov eax, esi
        ScalarLeftPresent:
            mov edx, dword ptr [esp + 14h]
            test edx, edx
            mov ecx, dword ptr [esp + 10h]
            jnz ScalarRightPresent
            mov edx, ecx
        ScalarRightPresent:
            fld dword ptr [eax]
            fadd st(0), st(0)
            fld dword ptr [esi]
            fmul G_ScalarFive
            fsubp st(1), st(0)
            fld dword ptr [ecx]
            fmul G_ScalarFour
            faddp st(1), st(0)
            fsub dword ptr [edx]
            fmulp st(2), st(0)
            fld dword ptr [esi]
            fmul G_ScalarThree
            fsub dword ptr [eax]
            fld dword ptr [ecx]
            fmul G_ScalarThree
            fsubp st(1), st(0)
            fadd dword ptr [edx]
            fmul st(0), st(1)
            faddp st(2), st(0)
            fld dword ptr [ecx]
            fsub dword ptr [eax]
            fmul dword ptr [esp + 18h]
            faddp st(2), st(0)
            fld dword ptr [esi]
            pop esi
            fadd st(0), st(0)
            faddp st(2), st(0)
            fxch st(1)
            fmul G_ScalarHalf
            fstp st(1)
            mov eax, dword ptr [esp + 18h]
            fstp tbyte ptr [eax]
            ret
        }
    }

    __declspec(naked) void __cdecl ParamLerp(const FParamKey* Left, const FParamKey* Right, const float* T, FThornExtendedProgress* Output)
    {
        // 2f948..2f961: extended subtraction/divide/product/add to 80-bit output.
        __asm
        {
            // Copy input bits into private argument scratch, without an FP load.
            mov eax, dword ptr [esp + 0ch]
            mov eax, dword ptr [eax]
            mov dword ptr [esp + 0ch], eax
            mov eax, dword ptr [esp + 4]
            mov ecx, dword ptr [esp + 8]
            fld dword ptr [eax + 4]
            fld dword ptr [esp + 0ch]
            fsub dword ptr [eax]
            fld dword ptr [ecx]
            fsub dword ptr [eax]
            fdivp st(1), st(0)
            fld dword ptr [ecx + 4]
            fsub st(0), st(2)
            fmulp st(1), st(0)
            faddp st(1), st(0)
            mov eax, dword ptr [esp + 10h]
            fstp tbyte ptr [eax]
            ret
        }
    }

    // FUN_06f21000: shared exact x87 arithmetic including helper float stores.
    GENRESULT ThornEvalCatmullRom3D(const float* P0, const float* P1, const float* P2,
                                    const float* P3, float U, float* Out)
    {
        ThornSplineMath::CatmullRom3D(P0, P1, P2, P3, U, Out);
        return GR_OK;
    }

    struct FParamHermiteDeleter
    {
        void operator()(FHermiteKnot* Buffer) const
        {
            ::operator delete(Buffer);
        }
    };

    // FUN_06f33df0: construction copies Value/T raw, but tangent values pass
    // through FLD/FSTP in reverse load order. Assignment is a different path.
    using ThornCurveDetail::ConstructHermiteRecord;

    // FUN_06f30480: all 60 instructions; only normalized S is stored as float.
    __declspec(naked) void __cdecl Hermite1D(const FHermiteKnot& A, const FHermiteKnot& B, const float* T, FThornExtendedProgress* Output)
    {
        __asm
        {
            // Copy input bits into private argument scratch, without an FP load.
            mov eax, dword ptr [esp + 0ch]
            mov eax, dword ptr [eax]
            mov dword ptr [esp + 0ch], eax
            mov ecx, dword ptr [esp + 4]
            fld dword ptr [ecx + 4]
            fcomp dword ptr [esp + 0ch]
            fnstsw ax
            test ah, 44h
            jp CompareRight
            fld dword ptr [ecx]
            jmp HermiteStoreOutput
        CompareRight:
            mov edx, dword ptr [esp + 8]
            fld dword ptr [edx + 4]
            fcomp dword ptr [esp + 0ch]
            fnstsw ax
            test ah, 44h
            jp NormalizeTime
            fld dword ptr [edx]
            jmp HermiteStoreOutput
        NormalizeTime:
            fld dword ptr [edx + 4]
            mov eax, dword ptr [ecx + 0ch]
            cmp eax, -1
            fsub dword ptr [ecx + 4]
            fld dword ptr [esp + 0ch]
            fsub dword ptr [ecx + 4]
            fdiv st(0), st(1)
            fstp dword ptr [esp + 0ch]
            jnz EvaluatePolynomial
            fstp st(0)
            fld dword ptr [ecx]
            jmp HermiteStoreOutput
        EvaluatePolynomial:
            fld dword ptr [esp + 0ch]
            fmul dword ptr [esp + 0ch]
            fld dword ptr [esp + 0ch]
            fmul st(0), st(1)
            fld st(0)
            fadd st(0), st(1)
            fld st(2)
            fmul G_ScalarThree
            fld st(3)
            fadd st(0), st(0)
            fsubr st(0), st(3)
            fadd dword ptr [esp + 0ch]
            fmul dword ptr [ecx + 0ch]
            fmul st(0), st(5)
            fxch st(3)
            fsub st(0), st(4)
            fmul dword ptr [edx + 8]
            fmul st(0), st(5)
            faddp st(3), st(0)
            fld st(1)
            fsub st(0), st(1)
            fadd G_ScalarOne
            fmul dword ptr [ecx]
            faddp st(3), st(0)
            fsub st(0), st(1)
            fmul dword ptr [edx]
            faddp st(2), st(0)
            fxch st(1)
            fstp st(3)
            fstp st(0)
            fstp st(0)
        HermiteStoreOutput:
            mov eax, dword ptr [esp + 10h]
            fstp tbyte ptr [eax]
            ret
        }
    }

    // FUN_06f30520: exact endpoint/seed/walk arithmetic, followed by an
    // ordinary typed kernel call. No C++ function is invoked inside asm.
    __declspec(noinline) void HermiteEvaluate(const FHermiteKnot* Begin, int Count, const float* T, FThornExtendedProgress* Output)
    {
        // A named local operand can use the compiler's realigned frame register.
        // Clang may choose ESI for that register, which this asm also modifies.
        // Address all operands through one fixed EBX state pointer instead.
        struct FState
        {
            const FHermiteKnot* Begin;
            int Count;
            const float* T;
            FThornExtendedProgress* Output;
            const FHermiteKnot* Left;
            const FHermiteKnot* Right;
            int LastIndex;
            float FirstTime;
            float LastTime;
            unsigned short OldControl;
            unsigned short ChopControl;
            // FISTP still writes eight bytes, without imposing 8-byte C++
            // alignment and introducing another realigned frame register.
            unsigned int Seed[2];
            unsigned char bEndpoint;
        } State = {Begin, Count, T, Output};
        static_assert(offsetof(FState, T) == 8 && offsetof(FState, Left) == 0x10 &&
                      offsetof(FState, OldControl) == 0x24 && offsetof(FState, Seed) == 0x28 &&
                      offsetof(FState, bEndpoint) == 0x30 && alignof(FState) == 4, "Hermite asm state offsets");
        __asm
        {
            lea ebx, State
            mov esi, dword ptr [ebx]
            test esi, esi
            jnz HermiteReadCount
            xor eax, eax
            jmp HermiteCountReady
        HermiteReadCount:
            mov eax, dword ptr [ebx + 4]
        HermiteCountReady:
            cmp eax, 1
            jne HermiteCheckEndpoints
            fld dword ptr [esi]
            jmp HermiteEndpointOutput
        HermiteCheckEndpoints:
            mov edx, dword ptr [ebx + 8]
            fld dword ptr [edx]
            dec eax
            mov dword ptr [ebx + 18h], eax
            shl eax, 4
            add eax, esi
            mov edi, eax
            mov eax, dword ptr [esi + 4]
            mov ecx, dword ptr [edi + 4]
            mov dword ptr [ebx + 1ch], eax
            fcomp dword ptr [ebx + 1ch]
            mov dword ptr [ebx + 20h], ecx
            fnstsw ax
            test ah, 05h
            jp HermiteCheckLastEndpoint
            fld dword ptr [esi]
            jmp HermiteEndpointOutput
        HermiteCheckLastEndpoint:
            mov edx, dword ptr [ebx + 8]
            fld dword ptr [edx]
            fcomp dword ptr [ebx + 20h]
            fnstsw ax
            test ah, 41h
            jnz HermiteSeedSegment
            fld dword ptr [edi]
            jmp HermiteEndpointOutput
        HermiteSeedSegment:
            mov edx, dword ptr [ebx + 8]
            fld dword ptr [edx]
            fsub dword ptr [ebx + 1ch]
            fld dword ptr [ebx + 20h]
            fsub dword ptr [ebx + 1ch]
            fdivp st(1), st(0)
            fimul dword ptr [ebx + 18h]
            fstcw word ptr [ebx + 24h]
            fwait
            mov ax, word ptr [ebx + 24h]
            or ah, 0ch
            mov word ptr [ebx + 26h], ax
            fldcw word ptr [ebx + 26h]
            fistp qword ptr [ebx + 28h]
            fldcw word ptr [ebx + 24h]
            mov eax, dword ptr [ebx + 28h]
            mov ecx, eax
            shl ecx, 4
            fld dword ptr [ecx + esi + 4]
            add ecx, esi
            mov edx, dword ptr [ebx + 8]
            fcomp dword ptr [edx]
            fnstsw ax
            test ah, 41h
            jnz HermiteWalkForwardStart
            cmp ecx, esi
            jbe HermiteBackwardPair
        HermiteWalkBackward:
            fld dword ptr [ecx + 4]
            mov edx, dword ptr [ebx + 8]
            fcomp dword ptr [edx]
            fnstsw ax
            test ah, 05h
            jnp HermiteBackwardPair
            sub ecx, 10h
            cmp ecx, esi
            ja HermiteWalkBackward
        HermiteBackwardPair:
            lea eax, [ecx + 10h]
            jmp HermiteSelectedPair
        HermiteWalkForwardStart:
            cmp ecx, edi
            ja HermiteForwardPair
        HermiteWalkForward:
            fld dword ptr [ecx + 4]
            mov edx, dword ptr [ebx + 8]
            fcomp dword ptr [edx]
            fnstsw ax
            test ah, 41h
            jz HermiteForwardPair
            add ecx, 10h
            cmp ecx, edi
            jbe HermiteWalkForward
        HermiteForwardPair:
            mov eax, ecx
            add ecx, -10h
        HermiteSelectedPair:
            mov dword ptr [ebx + 10h], ecx
            mov dword ptr [ebx + 14h], eax
            jmp HermitePhaseDone
        HermiteEndpointOutput:
            mov ecx, dword ptr [ebx + 0ch]
            fstp tbyte ptr [ecx]
            mov byte ptr [ebx + 30h], 1
        HermitePhaseDone:
        }
        if (!State.bEndpoint)
            Hermite1D(*State.Left, *State.Right, T, Output);
    }

    void ApplyHermiteTangents(FHermiteKnot* Knots, int Count, int TangentMode)
    {
        if (Count == 0 || Count == 1)
            return;

        for (int Index = 0; Index < Count - 1; ++Index)
        {
            FHermiteKnot* Current = &Knots[Index];
            FHermiteKnot* Next = &Knots[Index + 1];
            float DeltaT;
            float DeltaV;
            // 300e5..30110: both differences are stored before the clamp,
            // even for a mode whose branch does not calculate a slope.
            __asm
            {
                mov edx, Next
                mov ecx, Current
                fld dword ptr [edx + 4]
                fsub dword ptr [ecx + 4]
                fstp DeltaT
                fld dword ptr [edx]
                fsub dword ptr [ecx]
                fstp DeltaV
                fld DeltaT
                fcomp G_MinDeltaT
                fnstsw ax
                test ah, 05h
                jp TangentDispatch
                mov dword ptr DeltaT, 38d1b717h
            TangentDispatch:
            }
            switch (TangentMode)
            {
            case 0:
                __asm
                {
                    mov ecx, Current
                    mov dword ptr [ecx + 0ch], 0ffffffffh
                }
                break;
            case 1:
                __asm
                {
                    mov edx, Next
                    mov ecx, Current
                    fld DeltaV
                    fdiv DeltaT
                    fst dword ptr [edx + 8]
                    fstp dword ptr [ecx + 0ch]
                }
                break;
            case 4:
                __asm
                {
                    mov ecx, Current
                    mov edx, Next
                    mov dword ptr [ecx + 0ch], 0
                    mov dword ptr [edx + 8], 0
                }
                break;
            case 5:
                // TEST AH,41/JNZ takes not-greater, including unordered.
                // FNSTSW follows the half-product exactly as in retail.
                __asm
                {
                    mov ecx, Current
                    mov edx, Next
                    fld dword ptr [ecx]
                    fcomp dword ptr [edx]
                    fld DeltaT
                    fmul G_ScalarHalfFloat
                    fnstsw ax
                    fdivr DeltaV
                    test ah, 41h
                    jnz StoreOutgoing
                    mov dword ptr [ecx + 0ch], 0
                    fstp dword ptr [edx + 8]
                    jmp TangentDone
                StoreOutgoing:
                    fstp dword ptr [ecx + 0ch]
                    mov dword ptr [edx + 8], 0
                TangentDone:
                }
                break;
            case 2:
            case 3:
            case 6:
                // Mode6 TEST AH,05/JP selects mode3 on equal, greater or
                // unordered; the strictly-less fall-through selects mode2.
                __asm
                {
                    mov ecx, Current
                    mov edx, Next
                    cmp TangentMode, 2
                    je IncomingTangent
                    cmp TangentMode, 3
                    je OutgoingTangent
                    fld dword ptr [ecx]
                    fcomp dword ptr [edx]
                    fnstsw ax
                    test ah, 05h
                    jp OutgoingTangent
                IncomingTangent:
                    fld DeltaT
                    mov dword ptr [ecx + 0ch], 0
                    fmul G_ScalarHalfFloat
                    fdivr DeltaV
                    fstp dword ptr [edx + 8]
                    jmp TangentPairDone
                OutgoingTangent:
                    fld DeltaT
                    fmul G_ScalarHalfFloat
                    fdivr DeltaV
                    fstp dword ptr [ecx + 0ch]
                    mov dword ptr [edx + 8], 0
                TangentPairDone:
                }
                break;
            default:
                break;
            }
        }
    }

    const FCurveSpec* SpecFor(const char* FactoryName, bool bDeadSecondLinear)
    {
        static const FCurveSpec Specs[] = {
            {"CatmullRomPCurve", "CatmullRom", EvalCatmullRom, -1, false},
            {"ThornParamCurve", "CatmullRom(non-negative)", EvalCatmullRom, -1, true},
            {"LinearPCurve", "Linear", EvalLerp, -1, false},
            {"ThornLPCurve", "Linear(non-negative)", EvalLerp, -1, true},
            {"SmoothPCurve", "Smooth", EvalHermite, 4, false},
            {"StepPCurve", "Step", EvalHermite, 0, false},
            {"LinearPCurve", "Linear", EvalHermite, 1, false},
            {"RampUpPCurve", "Ramp Up", EvalHermite, 2, false},
            {"RampDownPCurve", "Ramp Down", EvalHermite, 3, false},
            {"BumpOutPCurve", "Bump Out", EvalHermite, 5, false},
            {"BumpInPCurve", "Bump In", EvalHermite, 6, false},
            {"FreeFormPCurve", "Free Form", EvalHermite, 7, false},
        };

        if (FactoryName == nullptr)
            return nullptr;

        if (ThornNameEqual(FactoryName, "LinearPCurve"))
            return bDeadSecondLinear ? &Specs[6] : &Specs[2];

        const int Count = static_cast<int>(sizeof(Specs) / sizeof(Specs[0]));
        for (int Index = 0; Index < Count; ++Index)
        {
            if (ThornNameEqual(FactoryName, Specs[Index].FactoryName))
                return &Specs[Index];
        }
        return nullptr;
    }

    class FParamCurve final : public IParamCurve
    {
    public:
        FParamCurve(const FCurveSpec* Spec)
            : RefCount_(1), Spec_(Spec),
              Knots_(nullptr), KnotCount_(0), KnotCapacity_(0), NextId_(2)
        {
            const FCurveSpec* RequestedSpec = Spec_;
            const bool bDerivedHermiteConstruction = Spec_->Eval == EvalHermite && Spec_->HermiteType != 4;
            // Each derived Hermite ctor first calls Smooth2fa40/mode4/rebuild:
            // Step2fbf0, Linear2fc90, RampUp2fd30, RampDown2fdd0,
            // BumpOut2fe70, BumpIn2ff10 and FreeForm2ffb0. Then its own
            // mode is installed and the same buffers are rebuilt again.
            if (bDerivedHermiteConstruction)
                Spec_ = SpecFor("SmoothPCurve", false);
            InsertDefaultKeys();
            KeysOwner_.bBaseConstructed = true;
            Slot13Rebuild();
            if (bDerivedHermiteConstruction)
            {
                Spec_ = RequestedSpec;
                Slot13Rebuild();
            }
        }

        // Owner members also unwind if construction/rebuild throws:
        // derived knots, ID payload storage, then evaluation list.
        ~FParamCurve()
        {
            if (Spec_->Eval == EvalHermite)
            {
                Spec_ = SpecFor("SmoothPCurve", false);
                KnotsOwner_.reset();
                Knots_ = nullptr;
                KnotCount_ = 0;
                KnotCapacity_ = 0;
            }
        }

        // Retail generates one byte-identical QueryInterface clone per curve
        // factory's own vtable (VC6 does not fold identical thunks across
        // separate classes/vtables): confirmed clones at LAB_06f33650,
        // LAB_06f33a10, LAB_06f33b00, LAB_06f33bf0, LAB_06f33ce0 (and more of
        // the same shape) all walk a 1-entry IID table for the literal
        // string "1.11_IParamCurve", then on a match adjust `this` and call
        // AddRef via vtable+4, else write *Out=0 and return -3
        // (GR_INTERFACE_UNSUPPORTED). This single method reproduces that
        // observable contract for every curve type via Spec_, so all of
        // those clones are covered here (not separate 1:1 ports).
        GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override
        {
            // The retail table compares bytes exactly, including case.
            if (std::strcmp(Iid, Iids::ParamCurve) != 0)
            {
                *Out = nullptr;
                return GR_INTERFACE_UNSUPPORTED;
            }
            AddRef();
            *Out = static_cast<IParamCurve*>(this);
            return GR_OK;
        }

        unsigned long DACOM_API AddRef() override { return ++RefCount_; }

        // Retail likewise clones Release per curve factory's vtable
        // (confirmed identical bodies at LAB_06f33710, LAB_06f33800,
        // LAB_06f338f0, LAB_06f339e0, LAB_06f33ad0, and more): decrement the
        // dword at +0x3c, and when it reaches zero, force it to 1 and call
        // vtable+0x38 (Slot14Dtor) with DeleteFlag=1 before returning 0. All
        // of those clones are covered by this one method + Slot14Dtor.
        unsigned long DACOM_API Release() override
        {
            // 33717/33719 only decrement a positive count; zero still tears down.
            if (RefCount_ != 0)
                --RefCount_;
            if (RefCount_ == 0)
            {
                RefCount_ = 1;
                Slot14Dtor(1);
                return 0;
            }
            return RefCount_;
        }

#pragma warning(push)
#pragma warning(disable: 4716) // Return value is explicitly left in ST(0).
        float DACOM_API Evaluate(float T) override
        {
            FThornExtendedProgress Result;
            EvaluateExtended(&T, &Result);
            return ThornExtendedProgress::LoadFloat(Result.Bytes);
        }
#pragma warning(pop)

        // Modern typed transport for callers that retain the original ST(0).
        // The only concrete IParamCurve producer is this final class.
        void DACOM_API EvaluateToExtended(const float* T, FThornExtendedProgress* Output) override
        {
            EvaluateExtended(T, Output);
        }

        void DACOM_API WrapEvaluateToExtended(int A, int B, FThornExtendedProgress* Output) override
        {
            const float Normalized = ThornParamMath::WrappedRatio(A, B);
            EvaluateToExtended(&Normalized, Output);
        }

        unsigned char DACOM_API Slot4Replace(int Id, float F0, float F1, float F2,
                                              float F3) override
        {
            // 2f3e7..2f41d: the tree is keyed by signed ID, not by time.
            FParamKey* Key = FindKeyById(Id);
            if (Key == nullptr)
                return 0;
            float TargetT;
            const bool bAccepted = ThornParamMath::ReplacementTime(Id, F0, &TargetT);
            if (!bAccepted)
                return 0;
            Key->T = TargetT;
            Key->Value = F1;
            Key->Extra1 = F3;
            Key->Extra0 = F2;
            Slot13Rebuild();
            return 1;
        }

        unsigned char DACOM_API Slot5ReadKey(int Id, float* Out0, float* Out1, float* Out2,
                                              float* Out3) override
        {
            FParamKey* Key = FindKeyById(Id);
            if (Key == nullptr)
                return 0;
            // Sequential raw reads, including output aliasing a later field.
            unsigned int Word;
            std::memcpy(&Word, &Key->T, sizeof(Word));
            std::memcpy(Out0, &Word, sizeof(Word));
            std::memcpy(&Word, &Key->Value, sizeof(Word));
            std::memcpy(Out1, &Word, sizeof(Word));
            std::memcpy(&Word, &Key->Extra0, sizeof(Word));
            std::memcpy(Out2, &Word, sizeof(Word));
            std::memcpy(&Word, &Key->Extra1, sizeof(Word));
            std::memcpy(Out3, &Word, sizeof(Word));
            return 1;
        }

        int DACOM_API AddKey(float F0, float F1, float F2, float F3) override
        {
            const int TimeClass = ThornParamMath::ClassifyTime(F0);
            if (TimeClass < 0)
                return -1;

            if (TimeClass == 0)
            {
                // 2f5c3 overwrites the saved return ID before callbacks.
                Slot4Replace(0, F0, F1, F2, F3);
                Slot13Rebuild();
                return 0;
            }
            if (TimeClass == 1)
            {
                // 2f60c returns reserved endpoint ID 1, not NextId_.
                Slot4Replace(1, F0, F1, F2, F3);
                Slot13Rebuild();
                return 1;
            }

            const int ReturnedId = NextId_;
            FParamKey* Record = new FParamKey;
            // 2edc0 copies raw DWORDs, without x87 conversion.
            std::memcpy(&Record->T, &F0, sizeof(float));
            std::memcpy(&Record->Value, &F1, sizeof(float));
            std::memcpy(&Record->Extra0, &F2, sizeof(float));
            std::memcpy(&Record->Extra1, &F3, sizeof(float));
            Record->Id = ReturnedId;
            // No local payload cleanup in retail if list/map allocation throws.
            EvaluationKeys_.push_back(*Record);
            // A wrapped collision overwrites the pointer without freeing it.
            KeysOwner_.Items[ReturnedId] = Record;
            // 2f6c2 rereads the counter only after map publication.
            do
            {
                const unsigned int NextBits = static_cast<unsigned int>(NextId_) + 1u;
                std::memcpy(&NextId_, &NextBits, sizeof(NextId_));
            }
            while (NextId_ == -1 || NextId_ == 0 || NextId_ == 1);
            Slot13Rebuild();
            return ReturnedId;
        }

        unsigned char DACOM_API Slot7DeleteKey(int Id) override
        {
            if (Id == 0 || Id == 1)
                return 0;
            FParamKeyMap::iterator Key = KeysOwner_.Items.find(Id);
            if (Key == KeysOwner_.Items.end())
                return 0;
            FParamKey* Payload = Key->second;
            KeysOwner_.Items.erase(Key);
            delete Payload;
            // 2f76b..2f78d removes the evaluation tail, not the ID node.
            EvaluationKeys_.pop_back();
            Slot13Rebuild();
            return 1;
        }

        // LAB_06f2f7b0 returns the key-map count VALUE, not a field address.
        unsigned int DACOM_API GetKeyCount() override
        {
            return static_cast<unsigned int>(KeysOwner_.Items.size());
        }

        int DACOM_API Slot9IdByIndex(int Index) override
        {
            FParamEvaluationList::const_iterator Key = EvaluationKeys_.begin();
            unsigned int Remaining = static_cast<unsigned int>(Index);
            while (Remaining != 0 && Key != EvaluationKeys_.end())
            {
                --Remaining;
                ++Key;
            }
            return Key == EvaluationKeys_.end() ? -1 : Key->Id;
        }

        const char* DACOM_API Slot10Clsid() override
        {
            return Spec_ != nullptr ? Spec_->FactoryName : "";
        }

        const char* DACOM_API Slot11Display() override
        {
            return Spec_ != nullptr ? Spec_->DisplayName : "";
        }

#pragma warning(push)
#pragma warning(disable: 4716) // Virtual evaluator returns extended ST(0).
        float DACOM_API Slot12WrapEvaluate(int A, int B) override
        {
            int Remainder;
            float Normalized;
            // 2f810: signed IDIV (including both hardware fault cases), then
            // integer x87 normalization and ONE float store before slot3.
            __asm
            {
                mov eax, A
                cdq
                idiv B
                mov Remainder, edx
                fild Remainder
                fidiv B
                fstp dword ptr Normalized
            }
            return Evaluate(Normalized);
        }
#pragma warning(pop)

        // 2f370/2f840/2f970/2fac0 share ID-map -> persistent list
        // synchronization and the exact 30d20 sort. Hermite derived records
        // are rebuilt separately in signed ID order, with tangents after
        // EACH insertion (2fb56..2fb96), not once after the entire map.
        void DACOM_API Slot13Rebuild() override
        {
            FParamEvaluationList::iterator Key = EvaluationKeys_.begin();
            for (FParamKeyMap::const_iterator Mapped = KeysOwner_.Items.begin();
                 Mapped != KeysOwner_.Items.end(); ++Mapped)
            {
                std::memcpy(&*Key, Mapped->second, sizeof(FParamKey));
                ++Key;
            }
            SortEvaluationList(EvaluationKeys_);
            if (Spec_ != nullptr && Spec_->bClampNonNegative)
                ClampNonNegative();
            if (Spec_ == nullptr || Spec_->Eval != EvalHermite)
                return;

            // 2fb0a..2fb42 erases the vector range, retaining its capacity.
            KnotCount_ = 0;
            for (FParamKeyMap::const_iterator Mapped = KeysOwner_.Items.begin();
                 Mapped != KeysOwner_.Items.end(); ++Mapped)
            {
                const FParamKey* Key = Mapped->second;
                FHermiteKnot Record;
                // 2fb62..2fb87 packs raw DWORDs; tangent construction's x87
                // loads belong to 33df0, not this temporary-record assembly.
                std::memcpy(&Record.Value, &Key->Value, sizeof(float));
                std::memcpy(&Record.T, &Key->T, sizeof(float));
                std::memcpy(&Record.MIn, &Key->Extra0, sizeof(float));
                std::memcpy(&Record.MOut, &Key->Extra1, sizeof(float));
                InsertDerivedRecord(Record);
                ApplyHermiteTangents(Knots_, KnotCount_, Spec_->HermiteType);
            }
        }

        void* DACOM_API Slot14Dtor(unsigned char DeleteFlag) override
        {
            // 330e0 always runs the destructor; only bit0 releases storage.
            void* Result = this;
            this->~FParamCurve();
            if ((DeleteFlag & 1) != 0)
                ::operator delete(Result);
            return Result;
        }

        int DACOM_API Slot15PersistStub(void* /*Unused*/) override { return 0; }
        int DACOM_API Slot16PersistStub(void* /*Unused*/) override { return 0; }

    private:
        // FUN_06f302b0: insert one 16-byte derived Hermite record into the
        // sorted contiguous array. Retail inserts before equal T values,
        // grows 0→1, 1→2, then doubles, and shifts the suffix in place.
        int InsertDerivedRecord(const FHermiteKnot& Record)
        {
            int InsertAt = 0;
            while (InsertAt < KnotCount_)
            {
                const float* ExistingTime = &Knots_[InsertAt].T;
                const float* NewTime = &Record.T;
                unsigned char bInsertBefore;
                // TEST AH,41/JNP breaks on less/equal. Greater/unordered
                // have even parity and advance; no C++ float comparison.
                __asm
                {
                    mov ecx, NewTime
                    mov edx, ExistingTime
                    fld dword ptr [ecx]
                    fcomp dword ptr [edx]
                    fnstsw ax
                    test ah, 41h
                    setnp bInsertBefore
                }
                if (bInsertBefore)
                    break;
                InsertAt = InsertAt + 1;
            }

            const unsigned int Available = static_cast<unsigned int>(KnotCapacity_) -
                                           static_cast<unsigned int>(KnotCount_);
            if (Available < 1u)
            {
                const unsigned int CountBits = Knots_ == nullptr ? 0u : static_cast<unsigned int>(KnotCount_);
                const unsigned int Growth = CountBits <= 1u ? 1u : CountBits;
                const unsigned int NewCapacityBits = CountBits + Growth;
                int NewCapacity;
                std::memcpy(&NewCapacity, &NewCapacityBits, sizeof(NewCapacity));
                // 3033f..3034e: signed-negative -> zero, then wrapping SHL4.
                const unsigned int AllocationBytes = (NewCapacity < 0 ? 0u : NewCapacityBits) << 4;
                std::unique_ptr<FHermiteKnot, FParamHermiteDeleter> Grown(
                    static_cast<FHermiteKnot*>(::operator new(AllocationBytes)));
                FHermiteKnot* NewKnots = Grown.get();
                for (int Index = 0; Index < InsertAt; ++Index)
                    ConstructHermiteRecord(&NewKnots[Index], &Knots_[Index]);
                ConstructHermiteRecord(&NewKnots[InsertAt], &Record);
                for (int Index = InsertAt; Index < KnotCount_; ++Index)
                    ConstructHermiteRecord(&NewKnots[Index + 1], &Knots_[Index]);

                // Old records have no dtor (31600); delete old storage before
                // publishing capacity/begin/end, as in 303be..303e7.
                KnotsOwner_ = std::move(Grown);
                KnotCapacity_ = NewCapacity;
                Knots_ = NewKnots;
            }
            else if (InsertAt == KnotCount_)
            {
                // Empty suffix: 31a60 does nothing; 31aa0 constructs one.
                ConstructHermiteRecord(&Knots_[KnotCount_], &Record);
            }
            else
            {
                // Nonempty suffix: construct last, raw-assign backward, then
                // raw-assign the insertion slot. Constructors can quiet SNaNs.
                ConstructHermiteRecord(&Knots_[KnotCount_], &Knots_[KnotCount_ - 1]);
                for (int Index = KnotCount_ - 1; Index > InsertAt; --Index)
                    AssignHermiteRecord(&Knots_[Index], &Knots_[Index - 1]);
                AssignHermiteRecord(&Knots_[InsertAt], &Record);
            }

            const unsigned int CountAfterBits = static_cast<unsigned int>(KnotCount_) + 1u;
            std::memcpy(&KnotCount_, &CountAfterBits, sizeof(KnotCount_));
            return InsertAt + 1;
        }

        void InsertDefaultKeys()
        {
            FParamKey* First = new FParamKey;
            First->T = 0.0f;
            First->Value = 0.0f;
            First->Extra0 = 0.0f;
            First->Extra1 = 0.0f;
            FParamKey* Second = new FParamKey;
            Second->T = 1.0f;
            Second->Value = 1.0f;
            Second->Extra0 = 0.0f;
            Second->Extra1 = 0.0f;
            First->Id = 0;
            Second->Id = 1;
            EvaluationKeys_.push_back(*First);
            EvaluationKeys_.push_back(*Second);
            KeysOwner_.Items[0] = First;
            KeysOwner_.Items[1] = Second;
        }

        FParamKey* FindKeyById(int Id) const
        {
            FParamKeyMap::const_iterator Key = KeysOwner_.Items.find(Id);
            return Key == KeysOwner_.Items.end() ? nullptr : Key->second;
        }

        void ClampNonNegative()
        {
            // 2f8a0..2f901 / 2f9d0..2fa31: list time order; a strict
            // decrease writes BOTH evaluation value and the ID-map payload.
            float Tracker = 0.0f;
            FParamEvaluationList::iterator Key = EvaluationKeys_.begin();
            while (Key != EvaluationKeys_.end())
            {
                if (ParamStrictLess(&Key->Value, &Tracker))
                {
                    std::memcpy(&Key->Value, &Tracker, sizeof(float));
                    FParamKey* Payload = FindKeyById(Key->Id);
                    if (Payload != nullptr)
                        std::memcpy(&Payload->Value, &Tracker, sizeof(float));
                }
                std::memcpy(&Tracker, &Key->Value, sizeof(float));
                ++Key;
            }
        }

        void __thiscall EvaluateExtended(const float* T, FThornExtendedProgress* Output) const
        {
            if (Spec_ != nullptr && Spec_->Eval == EvalHermite)
            {
                EvaluateHermite(T, Output);
                return;
            }
            if (Spec_ != nullptr && Spec_->Eval == EvalLerp)
            {
                EvaluateLerp(T, Output);
                return;
            }

            // 2f220: TEST AH,41/JZ breaks only for keyT > T. If that
            // branch is not taken, TEST AH,44/JNP returns this node's value
            // for ordered equality. Unordered continues the live list walk.
            FParamEvaluationList::const_iterator Key = EvaluationKeys_.begin();
            FParamEvaluationList::const_iterator Previous = EvaluationKeys_.end();
            while (Key != EvaluationKeys_.end())
            {
                const float* KeyTime = &Key->T;
                unsigned char bGreater;
                unsigned char bEqual;
                __asm
                {
                    mov ecx, KeyTime
                    fld dword ptr [ecx]
                    mov edx, T
                    fcomp dword ptr [edx]
                    fnstsw ax
                    test ah, 41h
                    setz bGreater
                }
                if (bGreater)
                {
                    if (Previous == EvaluationKeys_.end())
                        break;
                    EvaluateCatmull(Previous, Key, T, Output);
                    return;
                }
                __asm
                {
                    mov ecx, KeyTime
                    fld dword ptr [ecx]
                    mov edx, T
                    fcomp dword ptr [edx]
                    fnstsw ax
                    test ah, 44h
                    setnp bEqual
                }
                if (bEqual)
                {
                    StoreParamFloat(Output, &Key->Value);
                    return;
                }
                Previous = Key;
                ++Key;
            }
            const float Zero = 0.0f;
            StoreParamFloat(Output, &Zero);
        }

        void EvaluateLerp(const float* T, FThornExtendedProgress* Output) const
        {
            // 2f920 TEST AH,41/JZ finds FIRST STRICTGREATER key. Equal and
            // unordered continue; therefore even T==last returns zero.
            FParamEvaluationList::const_iterator Key = EvaluationKeys_.begin();
            FParamEvaluationList::const_iterator Previous = EvaluationKeys_.end();
            while (Key != EvaluationKeys_.end())
            {
                const float* KeyTime = &Key->T;
                unsigned char bGreater;
                __asm
                {
                    mov ecx, KeyTime
                    fld dword ptr [ecx]
                    mov edx, T
                    fcomp dword ptr [edx]
                    fnstsw ax
                    test ah, 41h
                    setz bGreater
                }
                if (bGreater)
                {
                    if (Previous == EvaluationKeys_.end())
                        break;
                    const FParamKey* Left = &*Previous;
                    const FParamKey* Right = &*Key;
                    ParamLerp(Left, Right, T, Output);
                    return;
                }
                Previous = Key;
                ++Key;
            }
            const float Zero = 0.0f;
            StoreParamFloat(Output, &Zero);
        }

        void EvaluateCatmull(FParamEvaluationList::const_iterator Left,
                             FParamEvaluationList::const_iterator Right, const float* T,
                             FThornExtendedProgress* Output) const
        {
            const float* T0 = &Left->T;
            const float* T1 = &Right->T;
            float U;
            // 2f266..2f285: extended subtracts/divide, then one FSTP DWORD.
            __asm
            {
                mov eax, T0
                mov ecx, T1
                mov edx, T
                fld dword ptr [edx]
                fsub dword ptr [eax]
                fld dword ptr [ecx]
                fsub dword ptr [eax]
                fdivp st(1), st(0)
                fstp U
            }
            const float* P0 = nullptr;
            const float* P3 = nullptr;
            FParamEvaluationList::const_iterator Outside = Left;
            if (Outside != EvaluationKeys_.begin())
            {
                --Outside;
                P0 = &Outside->Value;
            }
            Outside = Right;
            ++Outside;
            if (Outside != EvaluationKeys_.end())
                P3 = &Outside->Value;
            const float* P1 = &Left->Value;
            const float* P2 = &Right->Value;
            CatmullRom1D(P0, P1, P2, P3, U, Output);
        }

        void EvaluateHermite(const float* T, FThornExtendedProgress* Output) const
        {
            const FHermiteKnot* Begin = Knots_;
            const int Count = KnotCount_;
            HermiteEvaluate(Begin, Count, T, Output);
        }

        unsigned long RefCount_;
        const FCurveSpec* Spec_;
        // Base2f090 destroys ID payloads/map before its evaluation list.
        // Declaration order also constructs the list before ID storage.
        FParamEvaluationList EvaluationKeys_;
        FParamKeyStorage KeysOwner_;
        FHermiteKnot* Knots_;
        std::unique_ptr<FHermiteKnot, FParamHermiteDeleter> KnotsOwner_;
        int KnotCount_;
        int KnotCapacity_;
        int NextId_;
    };

    IDAComponent* CreateParamCurveObject(const char* FactoryName, bool bDeadSecondLinear)
    {
        const FCurveSpec* Spec = SpecFor(FactoryName, bDeadSecondLinear);
        if (Spec == nullptr)
            return nullptr;
        FParamCurve* Object = new (std::nothrow) FParamCurve(Spec);
        if (Object == nullptr)
            return nullptr;
        return static_cast<IParamCurve*>(Object);
    }
}

IDAComponent* ThornCreateParamCurve(const char* FactoryName, bool bDeadSecondLinear)
{
    return CreateParamCurveObject(FactoryName, bDeadSecondLinear);
}
