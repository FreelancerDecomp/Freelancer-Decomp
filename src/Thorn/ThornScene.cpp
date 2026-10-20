// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "ThornScene.h"
#include "readfile.h"
#include "ThornModernAdapters.h"
#include "ThornControlCalls.h"
#include "ExtendedProgressCodec.h"
#include "PortableSplineMath.h"
#include "PortableSceneMath.h"
#include "SceneInterpolationMath.h"
#include "SceneIntegerMath.h"
#include "CompactLightMath.h"
#include "PortableComparison.h"
#include "SceneGeometryMath.h"
#include "portable_vector_load.h"

#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <malloc.h>
#include <new>
#include <memory>
#include <string>
#include <stdexcept>
#include <vector>
#include <windows.h>

// Playback: slots 19/22/23/26 + Start types 1РІР‚вЂњ19. docs/re/thorn.md.

namespace
{
    unsigned int G_NextEventId = 0; // DAT_06f61e24: module lifetime, wrapping counter.
    I3DMathEngine* G_SpatialWriterMathEngine = nullptr; // DAT_06f61df0: retained, no Release.
    I3DMathEngine* G_SceneEventMathEngine = nullptr; // DAT_06f61e20: retained, no Release.
    I3DMathEngine* G_SceneCsysMathEngine = nullptr; // DAT_06f61e1c: retained, no Release.

    I3DMathEngine* AcquireSceneCsysMathEngine()
    {
        if (G_SceneCsysMathEngine == nullptr)
            G_SceneCsysMathEngine = ThornAcquire3DMathEngine();
        return G_SceneCsysMathEngine;
    }

    I3DMathEngine* AcquireSceneEventMathEngine()
    {
        if (G_SceneEventMathEngine == nullptr)
            G_SceneEventMathEngine = ThornAcquire3DMathEngine();
        return G_SceneEventMathEngine;
    }
    // FUN_06f400b0: signed event-type predicate; either null means false.
    bool ActiveEventTypePrecedes(const FThornEvent* Left, const FThornEvent* Right)
    {
        return Left != nullptr && Right != nullptr && Left->Type < Right->Type;
    }

    // FUN_06f400e0: the sorted first sixteen entries provide the sentinel.
    void InsertActiveEventType(FThornEvent** Slot, FThornEvent* Event)
    {
        while (Event != nullptr)
        {
            FThornEvent* Previous = Slot[-1];
            if (Previous == nullptr || Event->Type >= Previous->Type)
                break;
            *Slot = Previous;
            --Slot;
        }
        *Slot = Event;
    }

    // FUN_06f40290: signed Type insertion sort, including the front shift.
    void SortActiveEventTypes(FThornEvent** Begin, FThornEvent** End)
    {
        if (Begin == End)
            return;
        for (FThornEvent** Slot = Begin + 1; Slot != End; ++Slot)
        {
            FThornEvent* Event = *Slot;
            if (Event != nullptr && *Begin != nullptr && Event->Type < (*Begin)->Type)
            {
                FThornEvent** Write = Slot;
                while (Write != Begin)
                {
                    *Write = Write[-1];
                    --Write;
                }
                *Begin = Event;
            }
            else
                InsertActiveEventType(Slot, Event);
        }
    }

    // FUN_06f3ff10: the already sorted prefix supplies the first sentinel.
    void InsertMasterEvent(FThornEvent** Slot, FThornEvent* Event)
    {
        while (Event != nullptr && Slot[-1] != nullptr &&
               static_cast<unsigned int>(Event->StartTimeMs) <
                   static_cast<unsigned int>(Slot[-1]->StartTimeMs))
        {
            *Slot = Slot[-1];
            --Slot;
        }
        *Slot = Event;
    }

    // FUN_06f40120: unsigned-time insertion sort, including the front shift.
    void SortRetailNodesByKey(FThornEvent** Nodes, int Count)
    {
        if (Count == 0)
            return;
        for (int Index = 1; Index < Count; ++Index)
        {
            FThornEvent* Event = Nodes[Index];
            FThornEvent** Slot = Nodes + Index;
            if (Event == nullptr || Nodes[0] == nullptr ||
                static_cast<unsigned int>(Nodes[0]->StartTimeMs) <=
                    static_cast<unsigned int>(Event->StartTimeMs))
            {
                InsertMasterEvent(Slot, Event);
            }
            else
            {
                for (; Slot != Nodes; --Slot)
                    *Slot = Slot[-1];
                *Nodes = Event;
            }
        }
    }

    // FUN_06f3fee0: AL-only unsigned event-time predicate.
    bool MasterEventPrecedes(const FThornEvent* Left, const FThornEvent* Right)
    {
        return Left != nullptr && Right != nullptr &&
               static_cast<unsigned int>(Left->StartTimeMs) <
                   static_cast<unsigned int>(Right->StartTimeMs);
    }

    // Retail FUN_06f401b0: select the median node by the unsigned key at
    // +0x08. The branch order is intentional; it also preserves retail's
    // null fallbacks instead of imposing a modern total-order policy.
    FThornEvent* SelectRetailMedianNode(FThornEvent* First, FThornEvent* Second, FThornEvent* Third)
    {
        if (First != nullptr)
        {
            if (Second != nullptr)
            {
                const unsigned int FirstKey = static_cast<unsigned int>(First->StartTimeMs);
                const unsigned int SecondKey = static_cast<unsigned int>(Second->StartTimeMs);
                if (FirstKey < SecondKey)
                {
                    if (Third == nullptr)
                        return First;
                    const unsigned int ThirdKey = static_cast<unsigned int>(Third->StartTimeMs);
                    if (SecondKey < ThirdKey)
                        return Second;
                    if (ThirdKey <= FirstKey)
                        return First;
                    return Third;
                }
            }
            if (Third != nullptr)
            {
                if (MasterEventPrecedes(First, Third))
                    return First;
            }
        }
        if (Second != nullptr && Third != nullptr)
        {
            if (MasterEventPrecedes(Second, Third))
                return Third;
        }
        return Second;
    }

    // Retail FUN_06f40230: Hoare-style partition over [Begin, End), using
    // the unsigned +0x08 key and retaining the retail null-stop branches.
    FThornEvent** PartitionRetailNodes(FThornEvent** Begin, FThornEvent** End, FThornEvent* Pivot)
    {
        FThornEvent** Left = Begin;
        FThornEvent** Right = End;
        while (true)
        {
            FThornEvent* LeftNode = *Left;
            while (LeftNode != nullptr && Pivot != nullptr &&
                   static_cast<unsigned int>(LeftNode->StartTimeMs) <
                   static_cast<unsigned int>(Pivot->StartTimeMs))
            {
                Left = Left + 1;
                LeftNode = *Left;
            }

            do
            {
                Right = Right - 1;
                if (Pivot == nullptr || *Right == nullptr)
                    break;
            }
            while (static_cast<unsigned int>(Pivot->StartTimeMs) <
                   static_cast<unsigned int>((*Right)->StartTimeMs));

            if (Right <= Left)
                return Left;
            *Left = *Right;
            *Right = LeftNode;
            Left = Left + 1;
        }
    }

    // FUN_06f3fd80: partition only; insertion follows in the load caller.
    void SortMasterPartitions(FThornEvent** Begin, FThornEvent** End)
    {
        while (End - Begin > 16)
        {
            const int Count = static_cast<int>(End - Begin);
            FThornEvent* Pivot = SelectRetailMedianNode(Begin[0], Begin[Count / 2], End[-1]);
            FThornEvent** Middle = PartitionRetailNodes(Begin, End, Pivot);
            if (End - Middle <= Middle - Begin)
            {
                SortMasterPartitions(Middle, End);
                End = Middle;
            }
            else
            {
                SortMasterPartitions(Begin, Middle);
                Begin = Middle;
            }
        }
    }

    // Slot4_Load, 06f3833f-06f38414: restore the complete original range
    // before sorting the first 16 entries and inserting the remaining entries.
    void SortMasterEvents(FThornEvent** Events, int Count)
    {
        if (Count <= 16)
        {
            SortRetailNodesByKey(Events, Count);
            return;
        }
        SortMasterPartitions(Events, Events + Count);
        SortRetailNodesByKey(Events, 16);
        for (int Index = 16; Index < Count; ++Index)
            InsertMasterEvent(Events + Index, Events[Index]);
    }

    // FUN_06f3ff50: literal signed-Type median/Hoare partitions.
    void PartitionActiveEventTypes(FThornEvent** Begin, FThornEvent** End)
    {
        while (End - Begin > 16)
        {
            FThornEvent* First = Begin[0];
            FThornEvent* Middle = Begin[(End - Begin) / 2];
            FThornEvent* Last = End[-1];
            FThornEvent* Pivot;
            if (ActiveEventTypePrecedes(First, Middle))
            {
                if (Last == nullptr)
                    Pivot = First;
                else if (Middle->Type < Last->Type)
                    Pivot = Middle;
                else if (First->Type < Last->Type)
                    Pivot = Last;
                else
                    Pivot = First;
            }
            else if (ActiveEventTypePrecedes(First, Last))
                Pivot = First;
            else if (ActiveEventTypePrecedes(Middle, Last))
                Pivot = Last;
            else
                Pivot = Middle;

            FThornEvent** Left = Begin;
            FThornEvent** Right = End;
            while (true)
            {
                FThornEvent* Event = *Left;
                while (ActiveEventTypePrecedes(Event, Pivot))
                {
                    ++Left;
                    Event = *Left;
                }
                do
                {
                    --Right;
                }
                while (Pivot != nullptr && *Right != nullptr && Pivot->Type < (*Right)->Type);
                if (Right <= Left)
                    break;
                *Left = *Right;
                *Right = Event;
                ++Left;
            }
            if (End - Left <= Left - Begin)
            {
                PartitionActiveEventTypes(Left, End);
                End = Left;
            }
            else
            {
                PartitionActiveEventTypes(Begin, Left);
                Begin = Left;
            }
        }
    }

    void IdentityOrient(float* Out9)
    {
        std::memset(Out9, 0, sizeof(float) * 9);
        Out9[0] = 1.0f;
        Out9[4] = 1.0f;
        Out9[8] = 1.0f;
    }

    void Copy8(float* Dst, const float* Src)
    {
        if (Dst == nullptr || Src == nullptr)
            return;
        std::memcpy(Dst, Src, 8 * sizeof(float));
    }

    int BitsAsInt(const float* Slot)
    {
        int Value = 0;
        if (Slot != nullptr)
            std::memcpy(&Value, Slot, sizeof(Value));
        return Value;
    }

    void StoreIntBits(float* Slot, int Value)
    {
        if (Slot != nullptr)
            std::memcpy(Slot, &Value, sizeof(Value));
    }

    unsigned int BitsAsUInt(const float* Slot)
    {
        unsigned int Value = 0;
        if (Slot != nullptr)
            std::memcpy(&Value, Slot, sizeof(Value));
        return Value;
    }

    // Literal DWORD pose transport for 38950/38a80. Forward reads/stores
    // preserve overlapping initial outputs without x87 conversion.
    void CopyPoseWordsForward(float* Dst, const float* Src, int Count)
    {
        for (int Index = 0; Index < Count; ++Index)
        {
            unsigned int Word;
            std::memcpy(&Word, Src + Index, sizeof(Word));
            std::memcpy(Dst + Index, &Word, sizeof(Word));
        }
    }

    void Copy3(float* Dst, const float* Src)
    {
        Dst[0] = Src[0];
        Dst[1] = Src[1];
        Dst[2] = Src[2];
    }

    void Copy9(float* Dst, const float* Src)
    {
        for (int Index = 0; Index < 9; ++Index)
            Dst[Index] = Src[Index];
    }

    void Copy12(float* Dst, const float* Src)
    {
        for (int Index = 0; Index < 12; ++Index)
            Dst[Index] = Src[Index];
    }

    void PackXform(float* Out12, const float* Orient9, const float* Pos3)
    {
        Copy9(Out12, Orient9);
        Copy3(&Out12[9], Pos3);
    }

    void UnpackXform(const float* Xform12, float* Orient9, float* Pos3)
    {
        Copy9(Orient9, Xform12);
        Copy3(Pos3, &Xform12[9]);
    }

    // FUN_06f2ecc0: snapshot the cross product before publishing X/Y/Z.
    void Cross3(float* Result, const float* A, const float* B)
    {
        ThornSceneGeometry::Cross(Result, A, B);
    }

    // FUN_06f2ec80: (X*X + Y*Y) + Z*Z, then reciprocal length.
    void Normalize3(float* V)
    {
        ThornSceneGeometry::Normalize(V[0], V[1], V[2]);
    }

    // FUN_06f23090: separate input/output, all FLD/FCHS before stores.
    void NegateVector3(float* Result, const float* Source)
    {
        ThornPortableSplineMath::NegateVector(Result, Source);
    }

    char* DupHeapString(const char* Src)
    {
        if (Src == nullptr)
            return nullptr;
        const unsigned int Len = static_cast<unsigned int>(std::strlen(Src));
        char* Dst = new (std::nothrow) char[Len + 1];
        if (Dst == nullptr)
            return nullptr;
        std::memcpy(Dst, Src, Len + 1);
        return Dst;
    }

    template<typename TChar>
    void ReplaceOwnedString(TChar*& Target, const char* Source)
    {
        char* Replacement = DupHeapString(Source);
        if (Source != nullptr && Replacement == nullptr)
            return;
        delete[] Target;
        Target = Replacement;
    }

    // LAB_06f38d20: the Count gate precedes signed-map selection and traversal.
    const char* NthSortedUserPropKey(const FThornEntity* Entity, int N)
    {
        if (static_cast<unsigned int>(N) >= Entity->UserProps->size())
            return nullptr;
        FThornUserProps::const_iterator Prop = Entity->UserProps->begin();
        for (int Step = 0; Step < N; ++Step)
            ++Prop;
        return Prop->first.c_str();
    }

    void WriteColumn(float* M9, int Col, const float* V)
    {
        // 2ed60/2ed80/2eda0 interleave raw word reads and stores. Avoid
        // floating loads, which quiet signalling NaNs in the Debug build.
        for (int Index = 0; Index != 3; ++Index)
        {
            unsigned int Word;
            std::memcpy(&Word, V + Index, sizeof(Word));
            std::memcpy(M9 + Col + Index * 3, &Word, sizeof(Word));
        }
    }

    // FUN_06f2ed00/20/40: column getters convert via x87, Z/Y/X load order.
    void ReadColumn(const float* M9, int Col, float* V)
    {
        PortableVectorLoad::Copy(V, M9 + Col, 3);
    }

    // FUN_06f3b670: load all eight FP words before output; word3 is raw.
    // Preserve x87 sNaN conversion and input/output overlap ordering.
    void Transpose9(float* Out9, const float* In9)
    {
        ThornPortableSceneMath::Transpose9(Out9, In9);
    }

    // FUN_06f3b9a0: math +0x38 transpose_transform(out, csys.orient, csys.pos).
    // Retail (thorn.dll.txt:53057-53098) calls I3DMathEngine vtbl+0x38 and
    // writes the 3 result dwords into the destination unconditionally -- it
    // never checks the returned GENRESULT before consuming the buffer, and
    // there is no manual-math fallback path in retail at all.
    void Fun06f3b9a0(float* Out3, const FThornCsys* Csys)
    {
        I3DMathEngine* Math = AcquireSceneCsysMathEngine();
        // Snapshot the live input only after factory acquisition, as retail does.
        FMatrix Orientation;
        FVector Position;
        FVector Result;
        std::memcpy(&Orientation, Csys->Orient, sizeof(Orientation));
        std::memcpy(&Position, Csys->Pos, sizeof(Position));
        Math->TransposeTransform(Result, Orientation, Position);
        std::memcpy(Out3, &Result, sizeof(Result));
    }

    // FUN_06f23090 РІР‚вЂќ negate vec3.
    void Fun06f23090(float* Out3, const float* In3)
    {
        NegateVector3(Out3, In3);
    }

    // FUN_06f3b970: REP9, then three forward DWORD reads/stores.
    // Byte transport accepts actual float/unsigned objects without overlays;
    // reading each word immediately before its store preserves overlap.
    void PackCsysWords(void* Destination, const void* Orientation, const void* Position)
    {
        unsigned char* Output = static_cast<unsigned char*>(Destination);
        const unsigned char* Input = static_cast<const unsigned char*>(Orientation);
        for (int Index = 0; Index < 9; ++Index)
        {
            unsigned int Word;
            std::memcpy(&Word, Input + Index * 4, sizeof(Word));
            std::memcpy(Output + Index * 4, &Word, sizeof(Word));
        }
        Input = static_cast<const unsigned char*>(Position);
        for (int Index = 0; Index < 3; ++Index)
        {
            unsigned int Word;
            std::memcpy(&Word, Input + Index * 4, sizeof(Word));
            std::memcpy(Output + 36 + Index * 4, &Word, sizeof(Word));
        }
    }

    // FUN_06f3b9a0 + FUN_06f23090 + FUN_06f3b670 + pack. Inverse of parent Csys.
    void InvertParentCsys(float* Out12, const FThornCsys* Csys)
    {
        float Tmp[3];
        Fun06f3b9a0(Tmp, Csys);
        float Neg[3];
        Fun06f23090(Neg, Tmp);
        float Transposed[9];
        Transpose9(Transposed, Csys->Orient);
        PackCsysWords(Out12, Transposed, Neg);
    }

    enum class EEventInverseNegateOrder
    {
        Shared,
        Position,
        Orientation
    };

    // 409ef..40a2b: each component is negated and stored before the next.
    void NegateInversePositionWords(float* Words)
    {
        Words[0] = -Words[0];
        Words[1] = -Words[1];
        Words[2] = -Words[2];
    }

    // 40b7f..40ba0: the orientation getter's local negate runs Z/Y/X.
    void NegateInverseOrientationWords(float* Words)
    {
        Words[2] = -Words[2];
        Words[1] = -Words[1];
        Words[0] = -Words[0];
    }

    // Inline inverse in entity/event consumers uses DAT_06f61e20, distinct
    // from FUN_06f3b9a0's DAT_06f61e1c. The foreign output is consumed even
    // when its GENRESULT reports failure.
    void InvertEventParentCsys(float* Out12, const FThornCsys* Csys,
                               EEventInverseNegateOrder NegateOrder = EEventInverseNegateOrder::Shared)
    {
        I3DMathEngine* Math = AcquireSceneEventMathEngine();
        FMatrix OrientationInput;
        FVector PositionInput;
        FVector TransformedPosition;
        std::memcpy(&OrientationInput, Csys->Orient, sizeof(OrientationInput));
        std::memcpy(&PositionInput, Csys->Pos, sizeof(PositionInput));
        Math->TransposeTransform(TransformedPosition, OrientationInput, PositionInput);
        float Tmp[3];
        std::memcpy(Tmp, &TransformedPosition, sizeof(Tmp));
        float Neg[3];
        if (NegateOrder == EEventInverseNegateOrder::Shared)
            NegateVector3(Neg, Tmp);
        else
        {
            std::memcpy(Neg, Tmp, sizeof(Neg));
            if (NegateOrder == EEventInverseNegateOrder::Position)
                NegateInversePositionWords(Neg);
            else
                NegateInverseOrientationWords(Neg);
        }
        // 409a0/40b30 inline inverse: raw constructor transpose, then REP9.
        unsigned int Transposed[9];
        const int SourceIndices[9] = {0, 3, 6, 1, 4, 7, 2, 5, 8};
        for (int Index = 0; Index < 9; ++Index)
            std::memcpy(&Transposed[Index], &Csys->Orient[SourceIndices[Index]], sizeof(Transposed[Index]));
        PackCsysWords(Out12, Transposed, Neg);
    }

    // Retail calls the typed foreign slots without native wrapper guards.
    GENRESULT SceneMathTransform(I3DMathEngine* Math, float* Out, const float* Xform, const float* Source)
    {
        // All actual callers provide a distinct output buffer. The sole math
        // producer is a callback-free kernel; preserve its input bits until entry.
        FTransform TransformInput;
        FVector SourceInput;
        FVector Result;
        std::memcpy(&TransformInput, Xform, sizeof(TransformInput));
        std::memcpy(&SourceInput, Source, sizeof(SourceInput));
        const GENRESULT Status = Math->TransformXform(Result, TransformInput, SourceInput);
        std::memcpy(Out, &Result, sizeof(Result));
        return Status;
    }

    GENRESULT SceneMathMulMatrix(I3DMathEngine* Math, float* Out, const float* A, const float* B)
    {
        FMatrix LeftInput;
        FMatrix RightInput;
        FMatrix Result;
        std::memcpy(&LeftInput, A, sizeof(LeftInput));
        std::memcpy(&RightInput, B, sizeof(RightInput));
        const GENRESULT Status = Math->MulMatrix(Result, LeftInput, RightInput);
        std::memcpy(Out, &Result, sizeof(Result));
        return Status;
    }

    GENRESULT SceneMathMulTransform(I3DMathEngine* Math, float* Out, const float* A, const float* B)
    {
        FTransform LeftInput;
        FTransform RightInput;
        FTransform Result;
        std::memcpy(&LeftInput, A, sizeof(LeftInput));
        std::memcpy(&RightInput, B, sizeof(RightInput));
        const GENRESULT Status = Math->MulTransform(Result, LeftInput, RightInput);
        std::memcpy(Out, &Result, sizeof(Result));
        return Status;
    }

    GENRESULT SceneMathRotate(I3DMathEngine* Math, float* Out, const float* Xform, const float* Source)
    {
        FTransform TransformInput;
        FVector SourceInput;
        FVector Result;
        std::memcpy(&TransformInput, Xform, sizeof(TransformInput));
        std::memcpy(&SourceInput, Source, sizeof(SourceInput));
        const GENRESULT Status = Math->Rotate(Result, TransformInput, SourceInput);
        std::memcpy(Out, &Result, sizeof(Result));
        return Status;
    }

    GENRESULT SceneQuatToMatrix(I3DMathEngine* Math, float* Out, const FQuat& Source)
    {
        FQuaternion QuaternionInput;
        FMatrix MatrixResult;
        std::memcpy(&QuaternionInput, &Source, sizeof(QuaternionInput));
        const GENRESULT Status = Math->QuaternionToMatrix(MatrixResult, QuaternionInput);
        std::memcpy(Out, &MatrixResult, sizeof(MatrixResult));
        return Status;
    }

    // FUN_06f44ce0 receives an already prepared inverse in ECX, a distinct
    // destination and source. It neither builds an inverse nor checks pointers.
    void MulTransformByPreparedInverse(const float* Inverse, float* Destination, const float* Source)
    {
        I3DMathEngine* Math = AcquireSceneEventMathEngine();
        float Result[12];
        SceneMathMulTransform(Math, Result, Inverse, Source);
        // 44d1a copies twelve result DWORDs without re-evaluating them.
        std::memcpy(Destination, Result, sizeof(Result));
    }

    // Hardpoint predecessors CALL FUN_06f3b9a0 (DAT_06f61e1c), then pass the
    // resulting inverse into FUN_06f44ce0 (DAT_06f61e20). Keep both owners.
    void MulXformByInverseCsys(const FThornCsys* Csys, float* Xform12)
    {
        float Inverse[12];
        InvertParentCsys(Inverse, Csys);
        MulTransformByPreparedInverse(Inverse, Xform12, Xform12);
    }

    // 49a64..49b1f: captured parent, Csys math cache, Z/Y/X negate,
    // raw transpose, then the independent event transform cache.
    void TransformFloorHardpoint(const FThornCsys* Parent, float* Hardpoint)
    {
        float NegativePosition[3];
        Fun06f3b9a0(NegativePosition, Parent);
        NegateInverseOrientationWords(NegativePosition);
        unsigned int Transposed[9];
        const int SourceIndices[9] = {0, 3, 6, 1, 4, 7, 2, 5, 8};
        for (int Index = 0; Index < 9; ++Index)
            std::memcpy(Transposed + Index, Parent->Orient + SourceIndices[Index], sizeof(unsigned int));
        float Inverse[12];
        PackCsysWords(Inverse, Transposed, NegativePosition);
        MulTransformByPreparedInverse(Inverse, Hardpoint, Hardpoint);
    }

    // Type 11 after FUN_06f2e440: math +0x28 Transform pos, +0x20 MulMatrix orient.
    // Retail (thorn.dll.txt:69900-69965, inside LAB_06f44140) never tests
    // either vtbl call's GENRESULT -- both results are copied into Pos/Orient
    // unconditionally.
    void ApplyForwardCsys(const FThornCsys* Csys, float* Pos, float* Orient)
    {
        if (Csys == nullptr || Pos == nullptr || Orient == nullptr)
            return;
        I3DMathEngine* Math = AcquireSceneEventMathEngine();
        float WorldPos[3];
        SceneMathTransform(Math, WorldPos, Csys->Orient, Pos);
        Copy3(Pos, WorldPos);
        float WorldOrient[9];
        SceneMathMulMatrix(Math, WorldOrient, Csys->Orient, Orient);
        Copy9(Orient, WorldOrient);
    }

    // 2e5b2 and the inlined axis normalizations use the same sum order.
    void NormalizeAxis3(float* V)
    {
        ThornSceneGeometry::Normalize(V[0], V[1], V[2]);
    }

    // Inlined 2e53d/2e7c7/2e737: Up cross Front, Z/Y/X float stores.
    // Initial basis differs only in the operand order of the final product.
    void CrossAxisSide3(float* Result, const float* Up, const float* Front, bool bInitial)
    {
        ThornSceneGeometry::CrossSide(Result, Up, Front, bInitial);
    }

    // Inlined 2e687/2e85f: Dir cross Side, Z/Y/X float stores.
    void CrossAxisUp3(float* Result, const float* Dir, const float* Side)
    {
        ThornSceneGeometry::CrossUp(Result, Dir, Side);
    }

    bool AxisDotNotGreater(const float* Up, const float* Dir)
    {
        return ThornSceneGeometry::AxisDotNotGreater(Up, Dir);
    }

    // FUN_06f2e440 РІР‚вЂќ look-at frame from direction + Lua up/front axis IDs (0..5).
    void BuildAxisFrame(float* Out9, const float* Direction, unsigned FrontAxis, unsigned UpAxis,
                        const unsigned int* SceneFrontAxis, const unsigned int* SceneUpAxis)
    {
        static const float kAxisTable[6][3] = {
            {1.0f, 0.0f, 0.0f},  {0.0f, 1.0f, 0.0f},  {0.0f, 0.0f, 1.0f},
            {-1.0f, 0.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}};

        int UpIdx = 1;
        int FrontIdx = 2;
        // FUN_06f2e440: null/sentinel shared-up skips BOTH shared selections.
        if (SceneUpAxis != nullptr && *SceneUpAxis != 6)
        {
            UpIdx = static_cast<int>(*SceneUpAxis);
            if (SceneFrontAxis != nullptr && *SceneFrontAxis != 6)
                FrontIdx = static_cast<int>(*SceneFrontAxis);
        }

        float TableFront[3];
        Copy3(TableFront, kAxisTable[FrontIdx]);
        float TableUp[3];
        Copy3(TableUp, kAxisTable[UpIdx]);
        float TableSide[3];
        CrossAxisSide3(TableSide, TableUp, TableFront, true);

        float Dir[3];
        Copy3(Dir, Direction);
        NormalizeAxis3(Dir);

        float UpVec[3];
        Copy3(UpVec, TableUp);

        if (FrontAxis > 2)
            NegateVector3(Dir, Dir);
        if (UpAxis > 2)
            NegateVector3(UpVec, UpVec);

        float OrthoUp[3];
        float Side[3];
        if (AxisDotNotGreater(UpVec, Dir))
        {
            CrossAxisSide3(Side, UpVec, Dir, false);
            NormalizeAxis3(Side);
            CrossAxisUp3(OrthoUp, Dir, Side);
            NormalizeAxis3(OrthoUp);
        }
        else
        {
            // 2e687..2e731 builds the final up directly from Dir x TableSide.
            CrossAxisUp3(OrthoUp, Dir, TableSide);
            NormalizeAxis3(OrthoUp);
            CrossAxisSide3(Side, OrthoUp, Dir, false);
            NormalizeAxis3(Side);
        }

        bool bCol0 = false;
        bool bCol1 = false;
        bool bCol2 = false;
        bool bUpColumn0 = false;
        switch (FrontAxis)
        {
        case 0:
        case 3:
            WriteColumn(Out9, 0, Dir);
            bCol0 = true;
            break;
        case 1:
        case 4:
            WriteColumn(Out9, 1, Dir);
            bCol1 = true;
            break;
        case 2:
        case 5:
            WriteColumn(Out9, 2, Dir);
            bCol2 = true;
            break;
        }
        switch (UpAxis)
        {
        case 0:
        case 3:
            WriteColumn(Out9, 0, OrthoUp);
            bUpColumn0 = true;
            break;
        case 1:
        case 4:
            WriteColumn(Out9, 1, OrthoUp);
            bCol1 = true;
            break;
        case 2:
        case 5:
            WriteColumn(Out9, 2, OrthoUp);
            bCol2 = true;
            break;
        }

        // Up column 0 jumps directly to 2e971, skipping missing-X handling.
        if (!bUpColumn0 && !bCol0)
        {
            float ColY[3];
            float ColZ[3];
            float ColX[3];
            // 2ea11 uses raw DWORDs for Z before the x87 Y getter.
            std::memcpy(ColZ, Out9 + 2, 4);
            std::memcpy(ColZ + 1, Out9 + 5, 4);
            std::memcpy(ColZ + 2, Out9 + 8, 4);
            ReadColumn(Out9, 1, ColY);
            Cross3(ColX, ColY, ColZ);
            Normalize3(ColX);
            WriteColumn(Out9, 0, ColX);
            return;
        }
        if (bCol1)
        {
            if (!bCol2)
            {
                float ColX[3];
                float ColY[3];
                float ColZ[3];
                ReadColumn(Out9, 1, ColY);
                ReadColumn(Out9, 0, ColX);
                Cross3(ColZ, ColX, ColY);
                Normalize3(ColZ);
                WriteColumn(Out9, 2, ColZ);
            }
            return;
        }
        float ColX[3];
        float ColZ[3];
        float ColY[3];
        ReadColumn(Out9, 0, ColX);
        ReadColumn(Out9, 2, ColZ);
        Cross3(ColY, ColZ, ColX);
        Normalize3(ColY);
        WriteColumn(Out9, 1, ColY);
    }

    void Lerp3(float* Out, const float* A, const float* B, float T)
    {
        Out[0] = A[0] + (B[0] - A[0]) * T;
        Out[1] = A[1] + (B[1] - A[1]) * T;
        Out[2] = A[2] + (B[2] - A[2]) * T;
    }

    float Lerp1(float A, float B, float T) { return A + (B - A) * T; }

    void AudioLerpFloat(const float* From, const float* To, float* Output, double Progress)
    {
        ThornSceneInterpolation::Audio(Output, From, To, Progress);
    }

    // Audio's wrapped difference is unsigned; capture remains signed.
    int ComputeAudioLerpInteger(int From, int To, double Progress)
    {
        return ThornSceneInteger::Audio(From, To, Progress);
    }

    // Signed wrapping difference and a final truncation to the low integer word.
    int ComputeLightColor(int From, int To, const float* U)
    {
        return ThornSceneInteger::Color(From, To, U);
    }

    // FOG 48eb3..48f0c: snapshot both scalar endpoints before publication.
    void FogLerpFloat(const float* From, const float* To, const float* U, float* ResultValue)
    {
        ThornSceneInterpolation::Lerp(From, To, U, ResultValue);
    }

    // FOG 48f17..48f98 wraps SUB as an int32, FILDs that value, then
    // FMUL/FIADD and _ftol's chop-to-qword with low32 return. No float store.
    int FogLerpInteger(int From, int To, const float* U)
    {
        return ThornSceneInteger::Color(From, To, U);
    }

    void CopyLightProps(FThornLightProps* Dst, const FThornLightProps* Src)
    {
        if (Dst == nullptr || Src == nullptr)
            return;
        std::memcpy(Dst, Src, sizeof(FThornLightProps));
    }

    struct FThornRetailLightTarget
    {
        unsigned char Bytes[0x68];
    };

    static_assert(sizeof(FThornRetailLightTarget) == 0x68, "retail compact light target size");

    float ReadRetailFloat(const unsigned char* Blob, unsigned int Offset)
    {
        float Value = 0.0f;
        std::memcpy(&Value, Blob + Offset, sizeof(Value));
        return Value;
    }

    int ReadRetailInt(const unsigned char* Blob, unsigned int Offset)
    {
        int Value = 0;
        std::memcpy(&Value, Blob + Offset, sizeof(Value));
        return Value;
    }

    void WriteRetailFloat(unsigned char* Blob, unsigned int Offset, float Value)
    {
        std::memcpy(Blob + Offset, &Value, sizeof(Value));
    }

    void WriteRetailInt(unsigned char* Blob, unsigned int Offset, int Value)
    {
        std::memcpy(Blob + Offset, &Value, sizeof(Value));
    }

    // FUN_06f2eb20: exact compact source (+0x00..+0x24) to the retail target
    // light record (+0x00..+0x64).  The public FThornLightProps type is a
    // normalized port-side record, so the adapter below is deliberately kept
    // separate from this raw-layout implementation.
    void ConvertCompactLightRetail(const unsigned char* Source, unsigned char* Target)
    {
        ThornCompactLight::Convert(Source, Target);
    }

    void ConvertCompactLight(FThornLightProps* Blob)
    {
        unsigned char Source[0x28];
        std::memcpy(Source, Blob, sizeof(Source));
        FThornRetailLightTarget Target;
        ConvertCompactLightRetail(Source, Target.Bytes);
        Blob->Type = ReadRetailInt(Target.Bytes, 0x00);
        for (int Index = 0; Index < 4; ++Index)
            Blob->Diffuse[Index] = ReadRetailFloat(Target.Bytes, static_cast<unsigned int>(0x04 + Index * 4));
        Blob->Range = ReadRetailFloat(Target.Bytes, 0x4c);
        for (int Index = 0; Index < 3; ++Index)
            Blob->Atten[Index] = ReadRetailFloat(Target.Bytes, static_cast<unsigned int>(0x54 + Index * 4));
        // Type3 leaves these two output words untouched in retail.
        if (ReadRetailInt(Source, 0x24) == 0)
        {
            std::memcpy(&Blob->Theta, Target.Bytes + 0x60, sizeof(Blob->Theta));
            std::memcpy(&Blob->Cutoff, Target.Bytes + 0x64, sizeof(Blob->Cutoff));
        }
        Blob->Specular[0] = 0.0f;
        Blob->Specular[1] = 0.0f;
        Blob->Specular[2] = 0.0f;
        Blob->Specular[3] = 0.0f;
        Blob->Ambient[0] = 0.0f;
        Blob->Ambient[1] = 0.0f;
        Blob->Ambient[2] = 0.0f;
        Blob->Ambient[3] = 0.0f;
    }

    void PackEntityLightProps(FThornLightProps* Out, const FThornEntity* Entity)
    {
        std::memset(Out, 0, sizeof(*Out));
        if (Entity == nullptr)
            return;
        if (Entity->Snap.Type == kThornEntityScene || Entity->Snap.Type == kThornEntitySubScene)
        {
            Out->Color[0] = Entity->SceneAmbient[0];
            Out->Color[1] = Entity->SceneAmbient[1];
            Out->Color[2] = Entity->SceneAmbient[2];
            return;
        }
        Out->Color[0] = static_cast<int>(Entity->LightColor[0]);
        Out->Color[1] = static_cast<int>(Entity->LightColor[1]);
        Out->Color[2] = static_cast<int>(Entity->LightColor[2]);
        Copy3(Out->Direction, Entity->LightDirection);
        Out->RangeTemp = Entity->LightRange;
        Out->Range = Entity->LightRange;
        Out->CutoffTemp = Entity->LightCutoff;
        Out->Cutoff = Entity->LightCutoff;
        Out->On = Entity->LightOn;
        Out->Infinite = Entity->LightInfinite;
        Out->Type = Entity->LightType;
        Copy3(Out->Diffuse, Entity->LightDiffuse);
        Out->Diffuse[3] = 1.0f;
        Copy3(Out->Specular, Entity->LightSpecular);
        Out->Specular[3] = 1.0f;
        Copy3(Out->Ambient, Entity->LightAmbient);
        Out->Ambient[3] = 1.0f;
        Copy3(Out->Atten, Entity->LightAtten);
        Out->Theta = Entity->LightTheta;
    }

    void UnpackEntityLightProps(FThornEntity* Entity, const FThornLightProps* In)
    {
        if (Entity == nullptr || In == nullptr)
            return;
        if (Entity->Snap.Type == kThornEntityScene || Entity->Snap.Type == kThornEntitySubScene)
        {
            Entity->SceneAmbient[0] = In->Color[0];
            Entity->SceneAmbient[1] = In->Color[1];
            Entity->SceneAmbient[2] = In->Color[2];
            return;
        }
        Entity->LightOn = In->On;
        Entity->LightInfinite = In->Infinite;
        Entity->LightType = In->Type;
        Entity->LightColor[0] = static_cast<float>(In->Color[0]);
        Entity->LightColor[1] = static_cast<float>(In->Color[1]);
        Entity->LightColor[2] = static_cast<float>(In->Color[2]);
        Copy3(Entity->LightDirection, In->Direction);
        Entity->LightRange = In->Range;
        Entity->LightCutoff = In->Cutoff;
        Entity->LightTheta = In->Theta;
        Copy3(Entity->LightDiffuse, In->Diffuse);
        Copy3(Entity->LightSpecular, In->Specular);
        Copy3(Entity->LightAmbient, In->Ambient);
        Copy3(Entity->LightAtten, In->Atten);
    }

    // 45b2a/45d5c/45e17: input-first signaling comparison. Actual ordinary
    // C++ reverses the operands or changes the waiting instruction boundary.
    bool IsLightInterpolationComplete(const float* U)
    {
        return ThornPortableComparison::AtLeastOne(U);
    }

    // 45d81..45e0e: preserve the float Z difference and X product boundaries.
    void InterpolateLightDirection(float* Out, const float* From, const float* To, const float* U)
    {
        ThornSceneInterpolation::LightDirection(Out, From, To, U);
    }

    // LAB_06f45890 FPU mask loop. Out starts as live primary; To is target (or secondary live).
    void EvalLightProps(FThornLightProps* Out, const FThornLightProps* Capture,
                        const FThornLightProps* Target, const volatile int* MaskOwner, const float* U)
    {
        int Mask = *MaskOwner;
        if ((Mask & kThornLightBitRange) != 0)
            FogLerpFloat(&Capture->Range, &Target->Range, U, &Out->Range);
        if ((Mask & kThornLightBitCutoff) != 0)
            FogLerpFloat(&Capture->Cutoff, &Target->Cutoff, U, &Out->Cutoff);
        if ((Mask & kThornLightBitType) != 0 && IsLightInterpolationComplete(U))
            Out->Type = Target->Type;
        if ((Mask & kThornLightBitTheta) != 0)
            FogLerpFloat(&Capture->Theta, &Target->Theta, U, &Out->Theta);
        if ((Mask & kThornLightBitSpecular) != 0)
        {
            for (int Index = 0; Index < 3; ++Index)
                FogLerpFloat(Capture->Specular + Index, Target->Specular + Index, U, Out->Specular + Index);
        }
        if ((Mask & kThornLightBitAmbient) != 0)
        {
            for (int Index = 0; Index < 3; ++Index)
                FogLerpFloat(Capture->Ambient + Index, Target->Ambient + Index, U, Out->Ambient + Index);
        }
        if ((Mask & kThornLightBitDiffuse) != 0)
        {
            for (int Index = 0; Index < 3; ++Index)
                FogLerpFloat(Capture->Diffuse + Index, Target->Diffuse + Index, U, Out->Diffuse + Index);
        }
        if ((Mask & kThornLightBitAtten) != 0)
        {
            for (int Index = 0; Index < 3; ++Index)
                FogLerpFloat(Capture->Atten + Index, Target->Atten + Index, U, Out->Atten + Index);
        }
        if ((Mask & kThornLightBitColor) != 0)
        {
            Out->Color[0] = static_cast<int>(static_cast<unsigned int>(static_cast<__int64>(
                ComputeLightColor(Capture->Color[0], Target->Color[0], U))));
            Out->Color[1] = static_cast<int>(static_cast<unsigned int>(static_cast<__int64>(
                ComputeLightColor(Capture->Color[1], Target->Color[1], U))));
            Out->Color[2] = static_cast<int>(static_cast<unsigned int>(static_cast<__int64>(
                ComputeLightColor(Capture->Color[2], Target->Color[2], U))));
        }
        Mask = *MaskOwner; // 45d54 rereads the mask after the color conversion calls.
        if ((Mask & kThornLightBitInfinite) != 0 && IsLightInterpolationComplete(U))
            Out->Infinite = Target->Infinite;
        if ((Mask & kThornLightBitDirection) != 0)
            InterpolateLightDirection(Out->Direction, Capture->Direction, Target->Direction, U);
        if ((Mask & kThornLightBitOn) != 0 && IsLightInterpolationComplete(U))
            Out->On = Target->On;
    }

    void Lerp9(float* Out, const float* A, const float* B, float T)
    {
        for (int Index = 0; Index < 9; ++Index)
            Out[Index] = A[Index] + (B[Index] - A[Index]) * T;
    }

    template <class TControl>
    void ReleaseControl(TControl*& Control)
    {
        IDAComponent* AsComponent = Control;
        FThornModernAdapters::ReleaseOwnedComponent(AsComponent);
        Control = nullptr;
    }

    template <class TControl>
    class FThornControlReleaseStage
    {
    public:
        explicit FThornControlReleaseStage(TControl*& InControl) : Control(InControl) {}
        ~FThornControlReleaseStage() noexcept(false) { ReleaseControl(Control); }
        FThornControlReleaseStage(const FThornControlReleaseStage&) = delete;
        FThornControlReleaseStage& operator=(const FThornControlReleaseStage&) = delete;
    private:
        TControl*& Control;
    };

    // The generic `CallXxx(Ctrl, Slot, ...)` dispatchers that used to live here are gone.
    // They took the slot number as a runtime argument, so one helper served a dozen unrelated
    // interfaces and no call could be checked against any declaration. Everything now goes
    // through src/include/thorn_controls.h.

    void IdentityQuat4(FQuaternion& Output)
    {
        Output.W = 1.0f;
        Output.X = 0.0f;
        Output.Y = 0.0f;
        Output.Z = 0.0f;
    }

    void CopyQuat4(float* Dst, const float* Src)
    {
        // 46e09..46e2c copies the four result words without FP evaluation.
        std::memcpy(Dst, Src, sizeof(float) * 4);
    }

    // SPATIAL slot +50 writes the actual event owner throughout the call.
    // A later arithmetic exception must retain already published components.
    void SpatialMatrixToQuaternion(I3DMathEngine* Math, FQuaternion& Destination, const void* Source)
    {
        FMatrix MatrixInput;
        std::memcpy(&MatrixInput, Source, sizeof(MatrixInput));
        Math->MatrixToQuaternion(Destination, MatrixInput);
    }

    // Retail (LAB_06f46a40, thorn.dll.txt:74369-74375, CALL[ECX+0x54]) never
    // tests the QuatToMatrix GENRESULT and has no identity fallback -- the
    // matrix is consumed unconditionally.
    void Quat4ToMat9(float* Out9, const float* Src4)
    {
        FQuat Quat;
        std::memcpy(&Quat, Src4, sizeof(Quat));
        I3DMathEngine* Math = AcquireSceneEventMathEngine();
        SceneQuatToMatrix(Math, Out9, Quat);
    }

    void NlerpQuat4(float* Out4, const FQuaternion& From4, const FQuaternion& To4, const float* U)
    {
        I3DMathEngine* Math = AcquireSceneEventMathEngine();
        FQuaternion FromQuat;
        FQuaternion ToQuat;
        FQuaternion ResultQuat;
        // 46df2/46df9 reads the current owners after cache acquisition.
        // The sole kernel is pure; U stays a pointer for its actual load stage.
        std::memcpy(&FromQuat, &From4, sizeof(FromQuat));
        std::memcpy(&ToQuat, &To4, sizeof(ToQuat));
        Math->QuatNlerpValue(ResultQuat, FromQuat, ToQuat, U);
        std::memcpy(Out4, &ResultQuat, sizeof(ResultQuat));
    }

    // 46d5a..46db5: publish one component before rereading the next inputs.
    void LerpSpatialPosition(float* ResultPosition, const float* From, const float* To, const float* Scale)
    {
        ThornSceneInterpolation::SpatialPosition(ResultPosition, From, To, Scale);
    }

    // 46e6d..46ed8: publish float Delta, then reread Applied using the unrounded change.
    void AccumulateAxisDelta(const float* AnimT, const float* Angle, float* Applied, float* Delta)
    {
        ThornSceneInterpolation::AxisDelta(AnimT, Angle, Applied, Delta);
    }

    // 46eef..46fd7: FCOS is stored to float, FSIN remains on x87 through
    // all three columns. The radian product has no intermediate float store.
    // Jump table0/3=X,1/4=Y,2/5=Z; unsigned>5 keeps identity without trig.
    void InitializeEventFrameWords(float* Frame);

    void ApplyAxisRotFpu(float* Orient9, const FQuaternion& BaseQuat4, int AxisId, const float* DeltaDeg)
    {
        const unsigned int Kind = static_cast<unsigned int>(AxisId);
        float Rot[9];
        ThornSceneInterpolation::AxisRotation(Rot, AxisId, DeltaDeg);

        I3DMathEngine* Math = AcquireSceneEventMathEngine();
        FQuaternion BaseQuaternion;
        FMatrix BaseMatrix;
        std::memcpy(&BaseQuaternion, &BaseQuat4, sizeof(BaseQuaternion));
        Math->QuaternionToMatrix(BaseMatrix, BaseQuaternion);
        float Base9[9];
        std::memcpy(Base9, &BaseMatrix, sizeof(BaseMatrix));
        Math = AcquireSceneEventMathEngine();
        float ResultMatrix[9];
        if (Kind <= 2u)
            SceneMathMulMatrix(Math, ResultMatrix, Rot, Base9);
        else
            SceneMathMulMatrix(Math, ResultMatrix, Base9, Rot);
        // 47098..470a8 raw REP9 precedes the primary orientation callback.
        std::memcpy(Orient9, ResultMatrix, sizeof(ResultMatrix));
    }

    struct FThornSceneFogSnapshot
    {
        unsigned int Prefix[7];
        FThornFog8 Fog;
    };
    static_assert(sizeof(FThornSceneFogSnapshot) == 15 * sizeof(unsigned int), "packed scene snapshot");
    static_assert(offsetof(FThornSceneFogSnapshot, Fog) == 7 * sizeof(unsigned int), "packed fog offset");

    // FUN_06f431d0(blob, mask): copy 15 dword, mirror words 5/6 through the
    // optional native output pointers, and notify the typed light control when
    // the caller requests the retail bit-1 side effect.
    void Fun06f431d0(FThornEntity* Entity, const void* Blob15, unsigned char Mask)
    {
        const unsigned char* Source = static_cast<const unsigned char*>(Blob15);
        for (int Index = 0; Index < 15; ++Index)
        {
            unsigned int Word;
            std::memcpy(&Word, Source + Index * sizeof(Word), sizeof(Word));
            std::memcpy(&Entity->SceneFogPacked[Index], &Word, sizeof(Word));
        }
        if (Entity->SceneFogOutputA != nullptr)
            *Entity->SceneFogOutputA = Entity->SceneFogPacked[5];
        if (Entity->SceneFogOutputB != nullptr)
            *Entity->SceneFogOutputB = Entity->SceneFogPacked[6];
        if (Entity->LightControl != nullptr && (Mask & 1) != 0)
        {
            // QI 43283 identifies +0x104 as ILightControl; slot 3 reads RGB words 0..2.
            Entity->LightControl->SetColor(reinterpret_cast<int*>(Entity->SceneFogPacked));
        }
    }

    // FUN_06f43140(mask): IFogControl from packed +0x138. Bits 1 / 0xA / 0x70 / 4.
    void Fun06f43140(FThornEntity* Entity, unsigned char Mask)
    {
        if (Entity->FogControl == nullptr)
            return;
        const unsigned char* FogBytes = reinterpret_cast<const unsigned char*>(&Entity->SceneFogPacked[7]);
        if ((Mask & 1) != 0)
            Entity->FogControl->SetOn(FogBytes[0]);
        if ((Mask & 0xA) != 0)
            Entity->FogControl->SetTableMode(FogBytes[1], Entity->SceneFogPacked[11]);
        if ((Mask & 0x70) != 0)
        {
            float RangeValues[3];
            std::memcpy(RangeValues, &Entity->SceneFogPacked[12], sizeof(RangeValues));
            Entity->FogControl->SetRangeValue(&RangeValues[0], &RangeValues[1], &RangeValues[2]);
        }
        if ((Mask & 4) != 0)
        {
            // 0x06f431bb: the packed block's int colour, not a float copy.
            Entity->FogControl->SetColor(reinterpret_cast<const int*>(&Entity->SceneFogPacked[8]));
        }
    }

    // LAB_06f456e0 / LAB_06f45890 GET into the 0x24-dword blob, then copy.
    void CaptureLiveLightProps(FThornEntity* Entity, FThornLightProps* Out)
    {
        if (Entity == nullptr || Out == nullptr)
            return;
        if (Entity->Snap.Type == kThornEntityLight)
        {
            // LAB_06f456e0 (thorn.dll.txt:72079-72148): the GETs write in
            // place into the entity's persistent +0x160 blob and all 0x24
            // dwords are copied out afterwards; with neither control bound
            // the blob is copied untouched.  Seed from LightEntityDescriptor
            // so the RangeTemp/CutoffTemp/Unused words that have no
            // normalized-field equivalent survive exactly as retail keeps
            // them, instead of being rebuilt by PackEntityLightProps.
            std::memcpy(Out, &Entity->LightEntityDescriptor, sizeof(*Out));
            char* Blob = reinterpret_cast<char*>(Out);
            if (Entity->D3dLightControl != nullptr)
            {
                Entity->D3dLightControl->GetLight(Blob + 0x28);
                Entity->D3dLightControl->GetOn(reinterpret_cast<int*>(Blob + 0x20));
                Entity->D3dLightControl->GetDirection(reinterpret_cast<float*>(Blob + 0x0C));
            }
            else if (Entity->LightControl != nullptr)
            {
                Entity->LightControl->GetColor(reinterpret_cast<float*>(Blob + 0x00));
                Entity->LightControl->GetDirection(reinterpret_cast<float*>(Blob + 0x0C));
                Entity->LightControl->GetRange(reinterpret_cast<float*>(Blob + 0x18));
                Entity->LightControl->GetCutoff(reinterpret_cast<float*>(Blob + 0x1C));
                Entity->LightControl->GetSlot14(reinterpret_cast<float*>(Blob + 0x24));
                Entity->LightControl->GetSlot13(reinterpret_cast<float*>(Blob + 0x20));
                ConvertCompactLight(Out);
            }
            std::memcpy(&Entity->LightEntityDescriptor, Out, sizeof(Entity->LightEntityDescriptor));
            return;
        }
        if (Entity->Snap.Type == kThornEntityScene)
        {
            // LAB_06f456e0 (thorn.dll.txt:72149-72177): ILightControl slot 9
            // GETs in place at scene +0x11c, then exactly three dwords
            // (+0x11c..+0x128, the ambient) are staged and the 0x24-dword
            // copy takes those three plus 33 uninitialized stack words.  The
            // modern owner keeps the scene ambient in SceneAmbient. The tail
            // is deterministic: tick's SCENE sink (06f45e68..06f45eb2) copies
            // only Color[0..2] into the existing scene blob; getter/save/Stop
            // never expose the private capture tail. Recreating stack garbage
            // would add undefined behavior without preserving any scene output.
            PackEntityLightProps(Out, Entity);
            if (Entity->LightControl != nullptr)
            {
                FThornLightProps Live;
                CopyLightProps(&Live, Out);
                Entity->LightControl->GetColor(reinterpret_cast<float*>(&Live));
                Entity->SceneAmbient[0] = Live.Color[0];
                Entity->SceneAmbient[1] = Live.Color[1];
                Entity->SceneAmbient[2] = Live.Color[2];
                Out->Color[0] = Live.Color[0];
                Out->Color[1] = Live.Color[1];
                Out->Color[2] = Live.Color[2];
            }
            return;
        }
        // Any other entity type: retail copies nothing (jumps straight to
        // the end-time tail).  Zero the output so callers stay deterministic.
        std::memset(Out, 0, sizeof(*Out));
    }

    int LightColorToInteger(const float* Value)
    {
        // 41b64..41b97: FLD32/FMUL255 followed by qword ftol, consuming low32.
        const __int64 Converted = static_cast<__int64>(static_cast<double>(*Value) * 255.0);
        const unsigned int Word = static_cast<unsigned int>(Converted);
        int Result;
        std::memcpy(&Result, &Word, sizeof(Result));
        return Result;
    }

    void ApplyEntityLightRuntimeProps(FThornEntity* Entity, const FThornLightProps* Props)
    {
        static const float PiOver180 = 0.017453292f; // Exact retail 0x3c8efa35.
        ID3DLightControl* D3dLight = Entity->D3dLightControl;
        FThornLightProps& Live = Entity->LightEntityDescriptor;
        for (int Index = 0; Index < 36; ++Index)
        {
            unsigned int Word;
            std::memcpy(&Word, reinterpret_cast<const unsigned char*>(Props) +
                                   Index * sizeof(Word), sizeof(Word));
            std::memcpy(reinterpret_cast<unsigned char*>(&Live) +
                            Index * sizeof(Word), &Word, sizeof(Word));
        }
        if (D3dLight != nullptr)
        {
            D3dLight->SetOn(Live.On);
            Entity->D3dLightControl->SetDirection(Live.Direction);
            Entity->D3dLightControl->SetLight(&Live.Type);
        }
        ILightControl* Light = Entity->LightControl;
        if (Light != nullptr)
        {
            int Color[3];
            Color[0] = LightColorToInteger(&Live.Diffuse[0]);
            Color[1] = LightColorToInteger(&Live.Diffuse[1]);
            Color[2] = LightColorToInteger(&Live.Diffuse[2]);
            Light->SetColor(Color);
            Entity->LightControl->SetDirection(Live.Direction);
            Entity->LightControl->SetRange(Live.Range);
            if (Live.Type == 1)
                Entity->LightControl->SetCutoff(-1.0f);
            else
                Entity->LightControl->SetCutoff(Live.Cutoff * PiOver180);
            Entity->LightControl->SetIsSpot((Live.Type == 3) ? 1 : 0);
            Entity->LightControl->SetOn(Live.On);
        }
    }

    void ApplyLightProps(FThornEntity* Entity, const FThornLightProps* Props)
    {
        if (Entity == nullptr || Props == nullptr)
            return;
        if (Entity->Snap.Type == kThornEntityScene || Entity->Snap.Type == kThornEntitySubScene)
        {
            std::memcpy(&Entity->LightEntityDescriptor, Props, sizeof(Entity->LightEntityDescriptor));
            UnpackEntityLightProps(Entity, Props);
            if (Entity->LightControl != nullptr)
                Entity->LightControl->SetColor(const_cast<int*>(Props->Color));
            return;
        }
        ApplyEntityLightRuntimeProps(Entity, Props);
    }

    void GetLivePose(FThornEntity* Entity, float* Pos, float* Orient)
    {
        Copy3(Pos, Entity->Snap.Pos);
        Copy9(Orient, Entity->Snap.Orient);
        if (Entity->GeoControl != nullptr)
        {
            Entity->GeoControl->GetPosition(Pos);
            Entity->GeoControl->GetOrientation(Orient);
        }
    }

    void CopyMotionPathBytesForward(char* Destination, const char* Source, std::size_t Length)
    {
        std::size_t Offset = 0;
        for (; Offset + sizeof(unsigned int) <= Length; Offset += sizeof(unsigned int))
        {
            unsigned int Word;
            std::memcpy(&Word, Source + Offset, sizeof(Word));
            std::memcpy(Destination + Offset, &Word, sizeof(Word));
        }
        for (; Offset < Length; ++Offset)
            Destination[Offset] = Source[Offset];
    }

    // Sole modern string owner; no VC6 object layout or shared-buffer header.
    const char* AssignMotionPathType(FThornEntity* Entity, const char* Source, bool bConstructionCopy)
    {
        static const char Empty[] = "";
        const std::size_t Length = std::strlen(Source);
        if (Length > 0xfffffffdu)
            throw std::length_error("Thorn descriptor string too long");
        if (Length == 0)
        {
            delete[] Entity->PathType;
            Entity->PathType = nullptr;
            Entity->PathTypeCapacity = 0;
            return Empty;
        }
        // 42973..4297d reuses sufficient capacity only while it is <=31.
        // The binder's Grow policy is delegated to the current private owner.
        if (Entity->PathTypeCapacity < Length ||
            (!bConstructionCopy && Entity->PathTypeCapacity > 31))
        {
            std::unique_ptr<char[]> AliasedData;
            const UINT_PTR SourceAddress = reinterpret_cast<UINT_PTR>(Source);
            const UINT_PTR DataAddress = reinterpret_cast<UINT_PTR>(Entity->PathType);
            if (Entity->PathType != nullptr && SourceAddress >= DataAddress &&
                SourceAddress - DataAddress <= Entity->PathTypeCapacity)
                AliasedData.reset(Entity->PathType);
            else
                delete[] Entity->PathType;
            Entity->PathType = nullptr;
            Entity->PathTypeCapacity = 0;
            // Preserve only a modern defined aliased copy, not freed-source bytes.
            const std::size_t Capacity = Length | std::size_t(31);
            char* Replacement = new char[Capacity + 1];
            CopyMotionPathBytesForward(Replacement, Source, Length);
            Replacement[Length] = 0;
            Entity->PathType = Replacement;
            Entity->PathTypeCapacity = Capacity;
        }
        else
        {
            CopyMotionPathBytesForward(Entity->PathType, Source, Length);
            Entity->PathType[Length] = 0;
        }
        return Entity->PathType;
    }

    int ReadEventWord(const void* Source, std::size_t Index)
    {
        int Word;
        std::memcpy(&Word, static_cast<const unsigned char*>(Source) + Index * sizeof(Word), sizeof(Word));
        return Word;
    }

    void ApplyMotionPathDesc(FThornEntity* Entity, const FThornMotionPathDesc* Desc)
    {
        // 42900 has one string owner (+d4) and a separate descriptor pointer
        // (+c8). A null input clears only that pointer, preserving the owner.
        const char* Source = Desc->PathType;
        if (Source != nullptr)
        {
            Entity->Snap.MotionPath.PathType = AssignMotionPathType(Entity, Source, false);
        }
        else
            Entity->Snap.MotionPath.PathType = nullptr;

        // Publish the string pointer before Release. The input object's word
        // is reread after that callback (429e8..429f2), then published before
        // AddRef. In particular, Desc may alias the entity's public packet.
        if (Entity->Snap.MotionPath.MotionPath != nullptr)
            Entity->Snap.MotionPath.MotionPath->Release();
        Entity->Snap.MotionPath.MotionPath = Desc->MotionPath;
        if (Entity->Snap.MotionPath.MotionPath != nullptr)
            Entity->Snap.MotionPath.MotionPath->AddRef();
    }

    // FUN_06f409a0: GeoControl GetPosition, then the inline parent inverse
    // and TransformXform through the shared DAT_06f61e20 math owner. No
    // orientation getter or full-pose MulTransform occurs in this helper.
    void GetRetailRelativePosition(FThornEntity* Entity, float* Pos)
    {
        if (Entity->GeoControl == nullptr)
            return;
        Entity->GeoControl->GetPosition(Pos);
        if (Entity->ParentCsys != nullptr)
        {
            const FThornCsys* Parent = Entity->ParentCsys;
            float Inv[12];
            InvertEventParentCsys(Inv, Parent, EEventInverseNegateOrder::Position);
            I3DMathEngine* Math = AcquireSceneEventMathEngine();
            float Result[3];
            SceneMathTransform(Math, Result, Inv, Pos);
            std::memcpy(Pos, Result, sizeof(Result));
        }
    }

    // FUN_06f40b30: GeoControl GetOrientation, then the inline parent
    // inverse and MulMatrix through the same DAT_06f61e20 owner. Retail
    // still computes the inverse translation with TransposeTransform even
    // though this final matrix operation consumes only the rotation.
    void GetRetailRelativeOrientation(FThornEntity* Entity, float* Orient)
    {
        if (Entity->GeoControl == nullptr)
            return;
        Entity->GeoControl->GetOrientation(Orient);
        if (Entity->ParentCsys != nullptr)
        {
            const FThornCsys* Parent = Entity->ParentCsys;
            float Inv[12];
            InvertEventParentCsys(Inv, Parent, EEventInverseNegateOrder::Orientation);
            I3DMathEngine* Math = AcquireSceneEventMathEngine();
            float Result[9];
            // Retail FUN_06f40b30 (thorn.dll.txt:63575-63636) calls vtbl+0x20
            // and REP MOVSD-copies the 9 result dwords unconditionally; the
            // GENRESULT is never tested.
            SceneMathMulMatrix(Math, Result, Inv, Orient);
            std::memcpy(Orient, Result, sizeof(Result));
        }
    }

    // FUN_06f49f60 (and its per-type copies FUN_06f4a070/0d0/170/210/2b0/350):
    // entity slot 5 refreshes Snap.Pos in place through FUN_06f409a0
    // (thorn.dll.txt:63398 LEA EBX,[Snap+8]; 63478-63480 stores) and returns
    // &Snap.Pos; the caller reads Y at +4.
    float GetLocalPosY(FThornEntity* Entity)
    {
        if (Entity == nullptr)
            return 0.0f;
        return Entity->InnerGetPos()[1];
    }

    // ICompoundControl +0x24: (ctrl, target_type int, name, xform12).
    bool ResolveCompoundHardpoint(FThornEntity* Entity, int TypeId, const char* Name, float* Xform12,
                                  bool bRequireType12)
    {
        if (Entity == nullptr || Entity->CompoundControl == nullptr || Xform12 == nullptr)
            return false;
        if (bRequireType12)
        {
            const int EntType = Entity->Snap.Type;
            if (EntType != kThornEntityCompound && EntType != kThornEntityDeformable)
                return false;
        }
        return Entity->CompoundControl->GetHardpointXform(TypeId, Name, Xform12) == GR_OK;
    }

    // 47e6c..47e93 stores position Z/Y/X; 47f6a..47f93 stores target X/Y/Z.
    // Each original child word is the first x87 operand; no SSE add substitutes.
    void AddAttachOffset(float* Result, const float* ChildPosition, const float* OffsetVector, bool bReverseStores)
    {
        if (bReverseStores)
        {
            Result[2] = ChildPosition[2] + OffsetVector[2];
            Result[1] = ChildPosition[1] + OffsetVector[1];
            Result[0] = ChildPosition[0] + OffsetVector[0];
        }
        else
        {
            Result[0] = ChildPosition[0] + OffsetVector[0];
            Result[1] = ChildPosition[1] + OffsetVector[1];
            Result[2] = ChildPosition[2] + OffsetVector[2];
        }
    }

    // 47f9a..47fdf: rounded X and retained Y/Z differences form the zero-direction test.
    bool BuildAttachLookDirection(float* DirectionOut, const float* Target, const float* Origin)
    {
        return ThornSceneInterpolation::AttachDirection(DirectionOut, Target, Origin);
    }

    // These local frames are raw retail MOVs, even with a pending x87 trap.
    void InitializeEventFrameWords(float* Frame)
    {
        static const unsigned int Words[9] = {
            0x3f800000u, 0u, 0u, 0u, 0x3f800000u, 0u, 0u, 0u, 0x3f800000u};
        std::memcpy(Frame, Words, sizeof(Words));
    }
    // 441c5/44567 retain this event-owned effector after the first hardpoint write.
    void BuildIkPose(FThornEvent* Event, FThornEntity* Primary, FThornEntity** SecondaryEntry,
                     float* Pos, float* Orient)
    {
        if (Primary->CompoundControl != nullptr)
        {
            Primary->CompoundControl->GetHardpointXform(kThornTargetPart, Event->EndEffector,
                                                      Event->IkEffectorXform);
            const FThornCsys* Parent = Event->ParentCsys;
            if (Parent != nullptr)
                MulXformByInverseCsys(Parent, Event->IkEffectorXform);
        }

        // 44235/445d7 initializes only the local frame. Slot5 returns position;
        // there is no secondary orientation getter on either concrete IK body.
        InitializeEventFrameWords(Orient);
        std::memset(Pos, 0, sizeof(float) * 3);
        const float* SecondaryPosition = (*SecondaryEntry)->InnerGetPos();
        std::memcpy(Pos, SecondaryPosition, sizeof(float) * 3);

        if (Event->TargetPart != nullptr && Event->TargetType != 0)
        {
            const int SecondaryType = (*SecondaryEntry)->Snap.Type;
            if (SecondaryType == kThornEntityCompound || SecondaryType == kThornEntityDeformable)
            {
                ICompoundControl* Compound = (*SecondaryEntry)->CompoundControl;
                if (Compound != nullptr)
                {
                    float Hardpoint[12];
                    PackCsysWords(Hardpoint, Orient, Pos);
                    Compound->GetHardpointXform(Event->TargetType, Event->TargetPart, Hardpoint);
                    const FThornCsys* Parent = Event->ParentCsys;
                    // ParentNULL ignores the hardpoint result and keeps the
                    // original secondary position and identity local frame.
                    if (Parent != nullptr)
                    {
                        MulXformByInverseCsys(Parent, Hardpoint);
                        std::memcpy(Orient, Hardpoint, sizeof(float) * 9);
                        std::memcpy(Pos, Hardpoint + 9, sizeof(float) * 3);
                    }
                }
            }
        }

        float StablePosition[3];
        std::memcpy(StablePosition, Pos, sizeof(StablePosition));
        float Direction[3];
        Direction[0] = Pos[0] - Event->IkEffectorXform[9];
        Direction[1] = Pos[1] - Event->IkEffectorXform[10];
        Direction[2] = Pos[2] - Event->IkEffectorXform[11];
        BuildAxisFrame(Orient, Direction, static_cast<unsigned>(Event->LookAtFrontAxis),
                       static_cast<unsigned>(Event->LookAtUpAxis), Event->SceneFrontAxis,
                       Event->SceneUpAxis);
        const FThornCsys* Parent = Event->ParentCsys;
        if (Parent != nullptr)
        {
            I3DMathEngine* Math = AcquireSceneEventMathEngine();
            float WorldPosition[3];
            SceneMathTransform(Math, WorldPosition, Parent->Orient, StablePosition);
            std::memcpy(Pos, WorldPosition, sizeof(WorldPosition));
            // 44433/447bf reloads the event parent after the point callback,
            // then independently acquires the live math owner for the matrix.
            Parent = Event->ParentCsys;
            Math = AcquireSceneEventMathEngine();
            float WorldOrientation[9];
            SceneMathMulMatrix(Math, WorldOrientation, Parent->Orient, Orient);
            std::memcpy(Orient, WorldOrientation, sizeof(WorldOrientation));
        }
    }

    void ApplyGeoPose(FThornEntity* Entity, const float* Pos, const float* Orient)
    {
        if (Entity == nullptr)
            return;
        Entity->InnerSetPos(Pos);
        Entity->InnerSetOrient(Orient);
    }

    // Bind callbacks observe and write the actual typed owner, including failed QI.
    template <class TControl>
    void BindQi(void* EngineObj, const char* Iid, TControl*& Slot, int& Result)
    {
        if (Slot != nullptr)
        {
            TControl* Previous = Slot;
            Previous->Release();
            Slot = nullptr;
        }
        IDAComponent* EngineObject = static_cast<IDAComponent*>(EngineObj);
        const GENRESULT Hr = EngineObject->QueryInterface(Iid, reinterpret_cast<void**>(&Slot));
        if (Hr >= 0)
            Result = 0;
    }

    // PSYS43730: actual owner slot is cleared after Release, then is the QI out.
    template <class TControl>
    void BindLiveEntityControl(IDAComponent* EngineObject, const char* Iid,
                         TControl*& Slot, int& Result)
    {
        if (Slot != nullptr)
        {
            TControl* Previous = Slot;
            Previous->Release();
            Slot = nullptr;
        }
        const GENRESULT Hr = EngineObject->QueryInterface(Iid, reinterpret_cast<void**>(&Slot));
        if (Hr >= 0)
            Result = 0;
    }

    unsigned char HrToAl(GENRESULT Hr)
    {
        return (Hr == GR_OK) ? 1 : 0;
    }

    unsigned char RetailStillRunningPredicate(int TimeMs, int EndTimeMs)
    {
        // LAB_06f483a0 / LAB_06f485e0 use CMP followed by JC, then retain
        // negative end times as unbounded.  Preserve the unsigned compare
        // explicitly; a signed C++ comparison is not equivalent for a
        // negative clock or a high-bit end value.
        const unsigned int TimeBits = static_cast<unsigned int>(TimeMs);
        const unsigned int EndBits = static_cast<unsigned int>(EndTimeMs);
        if (TimeBits < EndBits)
            return 1;
        if (EndTimeMs < 0)
            return 1;
        return 0;
    }

    void ApplyEndTime(FThornEvent* Event)
    {
        if ((Event->Desc.Flags & kThornFlagUseScriptDuration) != 0)
            Event->EndTimeMs = -1;
        else
        {
            const unsigned int EndBits = static_cast<unsigned int>(Event->Desc.DurationMs) +
                                         static_cast<unsigned int>(Event->Desc.StartMs);
            std::memcpy(&Event->EndTimeMs, &EndBits, sizeof(Event->EndTimeMs));
        }
    }

    // FUN_06f423d0: GET slot 10 РІвЂ вЂ™ entity+0xf0, copy 8 dword out.
    void CaptureAudioFromEntity(FThornEntity* Entity, float* Out8)
    {
        Entity->CaptureAudioProps(Out8);
    }

    // FUN_06f423a0: copy 8 dword РІвЂ вЂ™ entity+0xf0, SET slot 9.
    void ApplyAudioProps(FThornEntity* Entity, const float* Props8)
    {
        // 423ae..423c9 retains the control before the forward eight-word copy.
        ISoundControl* Control = Entity->SoundControl;
        CopyPoseWordsForward(Entity->Audio, Props8, 8);
        if (Control != nullptr)
            Control->SetAudioProps(Entity->Audio, -1);
    }

    // FUN_06f43560: descriptor+c8 is untouched; +d0 is runtime state.
    void ApplyPsysSparam(FThornEntity* Entity, const float* Value)
    {
        const float Argument = ThornSceneInterpolation::PublishParticle(&Entity->PsysRuntimeSparam, Value);
        IPSysControl* Control = Entity->PsysControl;
        if (Control != nullptr)
            Control->SetSparam(Argument);
    }

    // Spatial wrapping compares rounded progress; floor wrapping compares the wide value.
    void SpatialWrapScale(float* Last, const FThornExtendedProgress* Progress, float* RoundedProgress, float* ResultScale)
    {
        ThornSceneInterpolation::SpatialWrap(Last, Progress->Bytes, RoundedProgress, ResultScale);
    }

    void FloorWrapScale(FThornEvent* Event, const FThornExtendedProgress* Progress, float* ScaleOutput)
    {
        ThornSceneInterpolation::FloorWrap(&Event->FloorWrapLast, Progress->Bytes, ScaleOutput);
    }

    void InterpolateFloorHeight(const float* Capture, const float* Target, const float* ScaleSource, float* HeightOutput)
    {
        ThornSceneInterpolation::Lerp(Capture, Target, ScaleSource, HeightOutput);
    }

    int EventElapsedTime(int TimeMs, int StartTimeMs)
    {
        // LAB_06f45ef0 subtracts two 32-bit retail integers before FILD.
        // Spell the wraparound out so the modern compiler cannot introduce
        // undefined signed-overflow behavior on the same input domain.
        const unsigned int ElapsedBits = static_cast<unsigned int>(TimeMs) -
                                         static_cast<unsigned int>(StartTimeMs);
        return static_cast<int>(ElapsedBits);
    }

    void EvaluateAnimT(FThornEvent* Event, int TimeMs, FThornExtendedProgress* Result)
    {
        // The typed output worker retains the concrete progress result in 80 bits.
        Event->EvaluateProgressExtended(TimeMs, Result, Event->Type == kThornTypeReverbPropAnim);
    }

    // Write the final float directly; returning it would add a reload/store.
    void StoreAnimTValue(const FThornExtendedProgress* Progress, float* Output)
    {
        const float Value = ThornExtendedProgress::LoadFloat(Progress->Bytes);
        std::memcpy(Output, &Value, sizeof(Value));
    }

    float StoreAnimT(const FThornExtendedProgress* Progress)
    {
        float Value;
        StoreAnimTValue(Progress, &Value);
        return Value;
    }

    void ApplyCameraProps(FThornEntity* Entity, const float* FovNearFarAspect)
    {
        // 41548..41570: raw writes in near/far/fov/aspect order, before SET.
        const int StoreOrder[4] = {1, 2, 0, 3};
        for (int Index = 0; Index < 4; ++Index)
        {
            const int Field = StoreOrder[Index];
            unsigned int Word;
            std::memcpy(&Word, &FovNearFarAspect[Field], sizeof(Word));
            std::memcpy(&Entity->CameraRuntimeProps[Field], &Word, sizeof(Word));
        }
        if (Entity->CameraControl != nullptr)
        {
            // Every argument and receiver is reread after the preceding callback.
            Entity->CameraControl->SetFov(&Entity->CameraRuntimeProps[0]);
            Entity->CameraControl->SetAspect(&Entity->CameraRuntimeProps[3]);
            Entity->CameraControl->SetNear(&Entity->CameraRuntimeProps[1]);
            Entity->CameraControl->SetFar(&Entity->CameraRuntimeProps[2]);
        }
        if (Entity->Snap.Type != kThornEntityMonitor)
        {
            FThornMonitorLinkNode* Node = Entity->MonitorLinks.Sentinel->Next;
            while (Node != Entity->MonitorLinks.Sentinel)
            {
                ApplyCameraProps(Node->Entity, Entity->CameraRuntimeProps);
                // 415d9: fetch live Next after the child callback, not before.
                Node = Node->Next;
            }
        }
    }

    void InterpolateCameraProps(float* ResultValues, const float* TargetValues,
                                const float* CapturedValues,
                                const FThornExtendedProgress* Progress, int Mask)
    {
        const double value = ThornExtendedProgress::LoadDouble(Progress->Bytes);
        ThornSceneInterpolation::Camera(ResultValues, TargetValues, CapturedValues, value, Mask);
    }

    void CaptureCameraProps(FThornEntity* Entity, float* Out4)
    {
        if (Entity->CameraControl != nullptr)
        {
            // FUN_06f460f0 writes GET results into the runtime cache.
            Entity->CameraControl->GetFov(&Entity->CameraRuntimeProps[0]);
            Entity->CameraControl->GetNear(&Entity->CameraRuntimeProps[1]);
            Entity->CameraControl->GetFar(&Entity->CameraRuntimeProps[2]);
            Entity->CameraControl->GetAspect(&Entity->CameraRuntimeProps[3]);
        }
        // 4617f..46195 copies forward one raw DWORD at a time.
        for (int Index = 0; Index < 4; ++Index)
        {
            unsigned int Word;
            std::memcpy(&Word, &Entity->CameraRuntimeProps[Index], sizeof(Word));
            std::memcpy(&Out4[Index], &Word, sizeof(Word));
        }
    }

    ISubtitleControl* SubtitleOf(FThornEntity* Entity)
    {
        if (Entity == nullptr)
            return nullptr;
        // FUN_06f49d70/06f49e20 select the subtitle facet by the resolved
        // entity type.  Marker, hardpoint, motion-path, psys and compound
        // type 1 do not enter those switch cases even if a stale native
        // pointer happens to exist.
        const int Type = Entity->Snap.Type;
        if (Type != kThornEntityDeformable &&
            Type != kThornEntityCamera && Type != kThornEntityMonitor &&
            Type != kThornEntitySound && Type != kThornEntityScene)
            return nullptr;
        return Entity->SubtitleControl;
    }

    bool GrowPointerArray(void*** Array, int* Cap, int Needed, int ElemSize)
    {
        if (Needed <= *Cap)
            return true;
        int NewCap = (*Cap == 0) ? 8 : (*Cap * 2);
        while (NewCap < Needed)
            NewCap = NewCap * 2;
        unsigned char* NewBuf = new (std::nothrow) unsigned char[static_cast<unsigned int>(NewCap) *
                                                                 static_cast<unsigned int>(ElemSize)];
        if (NewBuf == nullptr)
            return false;
        if (*Array != nullptr)
        {
            std::memcpy(NewBuf, *Array,
                        static_cast<unsigned int>(*Cap) * static_cast<unsigned int>(ElemSize));
            delete[] reinterpret_cast<unsigned char*>(*Array);
        }
        *Array = reinterpret_cast<void**>(NewBuf);
        *Cap = NewCap;
        return true;
    }

    // 415f0: identity scan, one node allocation, tail append, then count/AL1.
    unsigned char AddMonitorLink(FThornEntity* Self, FThornEntity* Other)
    {
        FThornMonitorLinkNode* Sentinel = Self->MonitorLinks.Sentinel;
        FThornMonitorLinkNode* Node = Sentinel->Next;
        while (Node != Sentinel)
        {
            if (Node->Entity == Other)
                return 0;
            Node = Node->Next;
        }
        FThornMonitorLinkNode* Previous = Sentinel->Previous;
        Node = new FThornMonitorLinkNode;
        Node->Next = Sentinel;
        // 41624 TEST saved tail / JNZ: null falls through to Node itself.
        if (Previous == nullptr)
            Previous = Node;
        Node->Previous = Previous;
        Sentinel->Previous = Node;
        Previous->Next = Node;
        Node->Entity = Other;
        ++Self->MonitorLinks.Count;
        return 1;
    }

    // 41660: erase the first identity match, free its node, then decrement.
    unsigned char RemoveMonitorLink(FThornEntity* Self, FThornEntity* Other)
    {
        FThornMonitorLinkNode* Sentinel = Self->MonitorLinks.Sentinel;
        FThornMonitorLinkNode* Node = Sentinel->Next;
        while (Node != Sentinel)
        {
            if (Node->Entity == Other)
            {
                Self->MonitorLinks.Erase(Node);
                return 1;
            }
            Node = Node->Next;
        }
        return 0;
    }

    unsigned char UnbindMonitorChildren(FThornEntity* Self)
    {
        FThornMonitorLinkNode* Node = Self->MonitorLinks.Sentinel->Next;
        while (Node != Self->MonitorLinks.Sentinel)
        {
            RemoveMonitorLink(Node->Entity, Self);
            // 416ca reads current.Next only AFTER reciprocal removal.
            // 416ef then visits savedNext.Next, deliberately skipping a node.
            // A self-link makes current dangling in retail too: no invented guard.
            FThornMonitorLinkNode* SavedNext = Node->Next;
            Self->MonitorLinks.Erase(Node);
            Node = SavedNext->Next;
        }
        return 1; // 416fb AL=1, including an initially empty list.
    }
}

void ThornInitLightEntityDefaults(FThornLightProps* Props)
{
    // FUN_06f41840: execute the retail compact/D3D initialization against a
    // raw adapter first, then expose the values through the port-side record.
    // This keeps the retail offsets explicit without importing the retail ABI.
    static const unsigned int kOneBits = 0x3f800000;
    if (Props == nullptr)
        return;

    unsigned char Retail[0xd4];
    std::memset(Retail, 0, sizeof(Retail));
    WriteRetailFloat(Retail, 0x5c, 1000.0f);
    WriteRetailFloat(Retail, 0x60, 180.0f);
    WriteRetailInt(Retail, 0x64, 1);
    WriteRetailFloat(Retail, 0x58, 1.0f);
    WriteRetailFloat(Retail, 0x68, 0.0f);
    WriteRetailInt(Retail, 0x44, 0xff);
    WriteRetailInt(Retail, 0x48, 0xff);
    WriteRetailInt(Retail, 0x4c, 0xff);
    WriteRetailInt(Retail, 0x6c, 2);
    WriteRetailFloat(Retail, 0x70, 1.0f);
    WriteRetailFloat(Retail, 0x74, 1.0f);
    WriteRetailFloat(Retail, 0x78, 1.0f);
    WriteRetailFloat(Retail, 0x7c, 1.0f);
    WriteRetailFloat(Retail, 0xb8, 1000.0f);
    WriteRetailFloat(Retail, 0xbc, 1.0f);
    WriteRetailFloat(Retail, 0xc0, 0.0f);
    WriteRetailFloat(Retail, 0xc4, 0.0f);
    WriteRetailFloat(Retail, 0xc8, 0.0001f);
    WriteRetailInt(Retail, 0xcc, 0x4020d97c);
    WriteRetailInt(Retail, 0xd0, 0x40490fdb);

    std::memset(Props, 0, sizeof(*Props));
    Props->Color[0] = ReadRetailInt(Retail, 0x44);
    Props->Color[1] = ReadRetailInt(Retail, 0x48);
    Props->Color[2] = ReadRetailInt(Retail, 0x4c);
    Props->Direction[0] = ReadRetailFloat(Retail, 0x50);
    Props->Direction[1] = ReadRetailFloat(Retail, 0x54);
    Props->Direction[2] = ReadRetailFloat(Retail, 0x58);
    Props->RangeTemp = ReadRetailFloat(Retail, 0x5c);
    Props->CutoffTemp = ReadRetailFloat(Retail, 0x60);
    Props->On = ReadRetailInt(Retail, 0x64);
    Props->Infinite = ReadRetailInt(Retail, 0x68);
    Props->Type = ReadRetailInt(Retail, 0x6c);
    Props->Diffuse[0] = ReadRetailFloat(Retail, 0x70);
    Props->Diffuse[1] = ReadRetailFloat(Retail, 0x74);
    Props->Diffuse[2] = ReadRetailFloat(Retail, 0x78);
    Props->Diffuse[3] = ReadRetailFloat(Retail, 0x7c);
    Props->Range = ReadRetailFloat(Retail, 0xb8);
    std::memcpy(&Props->Unused1e, &kOneBits, sizeof(Props->Unused1e));
    Props->Atten[0] = ReadRetailFloat(Retail, 0xc0);
    Props->Atten[1] = ReadRetailFloat(Retail, 0xc4);
    Props->Atten[2] = ReadRetailFloat(Retail, 0xc8);
    Props->Theta = ReadRetailFloat(Retail, 0xcc);
    Props->Cutoff = ReadRetailFloat(Retail, 0xcc + 4);
}

void ThornConvertCompactLight(FThornLightProps* Blob)
{
    ConvertCompactLight(Blob);
}

void ThornCopyLightPropsToEntity(FThornEntity* Entity, const FThornLightProps* Props)
{
    if (Entity == nullptr || Props == nullptr)
        return;
    // The retail loader (FUN_06f28a60 case 5) fills the entity's own +0x160
    // blob; keep LightEntityDescriptor in step with the normalized fields so
    // the START_LIGHT_PROP_ANIM capture can seed from it like retail does.
    std::memcpy(&Entity->LightEntityDescriptor, Props, sizeof(Entity->LightEntityDescriptor));
    UnpackEntityLightProps(Entity, Props);
}

static void InitializeLightEntityPortState(FThornEntity* Entity)
{
    if (Entity == nullptr)
        return;
    FThornLightProps Defaults;
    ThornInitLightEntityDefaults(&Defaults);
    // FUN_06f41840 writes these defaults into the +0x160 blob itself.
    std::memcpy(&Entity->LightEntityDescriptor, &Defaults, sizeof(Entity->LightEntityDescriptor));
    UnpackEntityLightProps(Entity, &Defaults);
    Entity->LightRuntimeState[0] = 0;
    Entity->LightRuntimeState[1] = 0;
    Entity->bLightStateInitialized = 1;
}

void ThornPublishEntityDescTail(FThornEntity* Entity)
{
    // Retail's loader builds each entity's descriptor, tail included, and hands it to the
    // type's constructor, whose ApplyDesc copies the whole descriptor into the entity (the
    // LIGHT entity keeps it at +0x84, LAB_06f4a110); IScriptEngine slots 8 / 9 hand that copy
    // out. The port's Lua loader writes the native fields instead, so the descriptor tail
    // (thorn_iface.h FThornEntitySnap) is filled from them here, once per loaded entity.
    if (Entity == nullptr)
        return;
    const int Type = Entity->Snap.Type;
    Entity->DescriptorOwnerType = Type;
    if (Type == kThornEntityCamera || Type == kThornEntityMonitor)
        ApplyCameraProps(Entity, &Entity->Snap.Fovh);
    if (Type == kThornEntityCompound || Type == kThornEntityDeformable)
        Entity->Snap.FloorHeight = Entity->FloorHeight;
    else if (Type == kThornEntityLight)
    {
        // Lua parses into runtime fields; a fresh reference owner discards that tail.
        if ((Entity->Snap.Flags & 1) != 0)
        {
            ThornInitLightEntityDefaults(&Entity->Snap.Light);
            ThornCopyLightPropsToEntity(Entity, &Entity->Snap.Light);
        }
        else
            std::memcpy(&Entity->Snap.Light, &Entity->LightEntityDescriptor, sizeof(Entity->Snap.Light));
    }
    else if (Type == kThornEntitySound)
    {
        // Lua's parse owner passes all eight words, including reference inputs.
        // The actual Slot35 constructor owns the later reference-tail gate.
        std::memcpy(Entity->Snap.Audio, Entity->Audio, sizeof(Entity->Snap.Audio));
    }
    else if (Type == kThornEntityScene || Type == kThornEntitySubScene)
    {
        if (Type == kThornEntityScene && (Entity->Snap.Flags & 1) == 0)
            std::memcpy(Entity->Snap.SceneBlob, Entity->SceneFogPacked, sizeof(Entity->Snap.SceneBlob));
        else
        {
            std::memset(Entity->Snap.SceneBlob, 0, sizeof(Entity->Snap.SceneBlob));
            Entity->Snap.SceneBlob[5] = 6;
            Entity->Snap.SceneBlob[6] = 6;
        }
        Fun06f431d0(Entity, Entity->Snap.SceneBlob, 0xFF);
    }
    else if (Type == kThornEntityMotionPath)
    {
        if ((Entity->Snap.Flags & 1) == 0)
        {
            Entity->Snap.MotionPath.PathType = Entity->PathType;
            Entity->Snap.MotionPath.MotionPath = Entity->ParsedMotionPathDescObject;
        }
        else
            Entity->Snap.MotionPath = {nullptr, nullptr};
    }
    else if (Type == kThornEntityPsys && (Entity->Snap.Flags & 1) != 0)
        std::memset(&Entity->Snap.PsysSparam, 0, sizeof(Entity->Snap.PsysSparam));
}

bool FThornConnectString::Assign(const char* Source)
{
    const size_t SourceLength = std::strlen(Source);
    // Inline max_size/_Xlen branches at 48255/495d4. The private owner has
    // current-CRT allocation, not the imported VC6 reference-count header.
    if (SourceLength > 0xfffffffdu)
        throw std::length_error("Thorn descriptor string too long");
    if (SourceLength == 0)
    {
        Reset();
        return false;
    }

    if (Capacity > 31u || Capacity < SourceLength)
    {
        // trim=true clears the old owner before growth. Keep an aliased old
        // allocation alive only for the modern defined copy; retail's dangling
        // source after this reset has no defined bytes to reproduce.
        std::unique_ptr<char[]> AliasedData;
        const UINT_PTR SourceAddress = reinterpret_cast<UINT_PTR>(Source);
        const UINT_PTR DataAddress = reinterpret_cast<UINT_PTR>(Data.get());
        if (Data && SourceAddress >= DataAddress && SourceAddress - DataAddress <= Length)
            AliasedData = std::move(Data);
        Reset();
        size_t NewCapacity = SourceLength | 31u;
        if (NewCapacity > 0xfffffffdu)
            NewCapacity = SourceLength;
        try
        {
            Data.reset(new char[NewCapacity + 1u]);
        }
        catch (const std::bad_alloc&)
        {
            // Imported _Copy reference retries the exact length. Its precise
            // 2003 CRT/new-handler/COW exception protocol remains unverified.
            NewCapacity = SourceLength;
            Data.reset(new char[NewCapacity + 1u]);
        }
        Capacity = NewCapacity;
        std::memcpy(Data.get(), Source, SourceLength);
    }
    else
    {
        // Own-buffer substrings move toward the beginning, matching forward
        // retail REP copies; identical source is also defined here.
        std::memmove(Data.get(), Source, SourceLength);
    }
    Length = SourceLength;
    Data[Length] = '\0';
    return true;
}

char* FThornConnectString::GetPublishedData()
{
    // 48223/482d8 publish owner data, or the imported static empty byte.
    static char Empty[1] = {'\0'};
    return Data ? Data.get() : Empty;
}

void FThornConnectString::Reset()
{
    Data.reset();
    Length = 0;
    Capacity = 0;
}

void FThornEvent::AdoptConnectStringStorage()
{
    // All native Lua producers use the setters; exposure is borrowed, never
    // adopted. Retain this activation flag for the existing cleanup routing.
    bConnectStorageActive = true;
}

void FThornEvent::ReleaseConnectStringStorage()
{
    AdoptConnectStringStorage();
    // 4811a..48188: parent owner first, then hardpoint, then common base (no curve).
    ConnectParentHardpointStorage.Reset();
    ConnectHardpointStorage.Reset();
}

void FThornEvent::SetConnectHardpoint(const char* Name)
{
    AdoptConnectStringStorage();
    Hardpoint = nullptr;
    if (Name != nullptr)
    {
        ConnectHardpointStorage.Assign(Name);
        Hardpoint = ConnectHardpointStorage.GetPublishedData();
    }
}

void FThornEvent::SetConnectParentHardpoint(const char* Name)
{
    AdoptConnectStringStorage();
    ParentHardpoint = nullptr;
    if (Name != nullptr)
    {
        ConnectParentHardpointStorage.Assign(Name);
        ParentHardpoint = ConnectParentHardpointStorage.GetPublishedData();
    }
}

void FThornEvent::AdoptSpatialStringStorage()
{
    bSpatialStorageActive = true;
}

void FThornEvent::SetSpatialTargetPart(const char* Name)
{
    AdoptSpatialStringStorage();
    TargetPart = nullptr;
    // 466b0..46769 skips the private assignment for null input. Otherwise
    // publish the retained owner after copying, including its empty-string byte.
    // Exact imported CRT capacity/COW/allocation failure remains a named gap.
    if (Name != nullptr)
    {
        SpatialTargetPartStorage.Assign(Name);
        TargetPart = SpatialTargetPartStorage.GetPublishedData();
    }
}

void FThornEvent::AdoptFloorStringStorage()
{
    bFloorStorageActive = true;
}

void FThornEvent::SetFloorTargetPart(const char* Name)
{
    AdoptFloorStringStorage();
    TargetPart = nullptr;
    // 495c2 skips assignment for null; the private owner survives while the
    // descriptor pointer stays null. Imported CRT growth/failure remains open.
    if (Name != nullptr)
    {
        FloorTargetPartStorage.Assign(Name);
        TargetPart = FloorTargetPartStorage.GetPublishedData();
    }
}

void FThornEvent::ApplyFloorHeightDesc(const void* DescIn)
{
    AdoptFloorStringStorage();
    ApplyCommonDesc(DescIn);
    // 4959c..495b1: callback-completed common descriptor, forced owner type,
    // cleared exposed pointer/type, then reread the current incoming type.
    Desc.Type = kThornTypeFloorHeightAnim;
    TargetPart = nullptr;
    TargetType = 0;
    if (ReadEventWord(DescIn, 0) == kThornTypeFloorHeightAnim)
    {
        std::memcpy(&FloorTarget, static_cast<const unsigned char*>(DescIn) + 44, sizeof(FloorTarget));
        SetFloorTargetPart(reinterpret_cast<const char*>(static_cast<INT_PTR>(ReadEventWord(DescIn, 12))));
        TargetType = ReadEventWord(DescIn, 13);
    }
    // 49677..49696 mirrors common timing/endpoints after the payload work.
    FinalizeDesc();
}

void FThornEvent::ReleaseFloorOwnedState()
{
    AdoptFloorStringStorage();
    try
    {
        if (ParamCurve != nullptr)
        {
            ParamCurve->Release();
        }
    }
    catch (...)
    {
        // 59638 unwinds the private owner after a curve Release exception.
        FloorTargetPartStorage.Reset();
        throw;
    }
    FloorTargetPartStorage.Reset();
}

FThornEvent::FThornEvent()
    : FThornEvent(0)
{
}

FThornEvent::FThornEvent(int DescriptorStorageType)
    : ParentCsys(nullptr), bOwnsParentCsys(0), SceneUpAxis(nullptr), SceneFrontAxis(nullptr), StartTimeMs(0), UniqueId(static_cast<int>(G_NextEventId++)), Type(0), PrimaryIndex(-1), SecondaryIndex(-1),
      EndTimeMs(-1), Unused20(0), bFloorEventOwner(DescriptorStorageType == kThornTypeFloorHeightAnim), NativeEventType(DescriptorStorageType), UserEventDesc{}, FogDesc{}, PsysPropDesc{}, AudioPropDesc{}, LightDesc{}, ReverbDesc{}, CameraDesc{}, SetCameraDesc{}, FloorDesc{}, PathDesc{},
      ParamCurve(DescriptorStorageType == kThornTypeFogPropAnim
          ? FogDesc.Common.ParamCurve : DescriptorStorageType == kThornTypePsysPropAnim
              ? PsysPropDesc.Common.ParamCurve : DescriptorStorageType == kThornTypeAudioPropAnim
                  ? AudioPropDesc.Common.ParamCurve : DescriptorStorageType == kThornTypeLightPropAnim
                      ? LightDesc.Common.ParamCurve : DescriptorStorageType == kThornTypeReverbPropAnim
                          ? ReverbDesc.Common.ParamCurve : DescriptorStorageType == kThornTypeCameraPropAnim
                              ? CameraDesc.Common.ParamCurve : DescriptorStorageType == kThornTypeSetCamera
                                  ? SetCameraDesc.ParamCurve : DescriptorStorageType == kThornTypeFloorHeightAnim
                                      ? FloorDesc.Common.ParamCurve : DescriptorStorageType == kThornTypePathAnim
                                          ? PathDesc.Common.ParamCurve : UserEventDesc.Common.ParamCurve),
      Desc(DescriptorStorageType == kThornTypeFogPropAnim
          ? FogDesc.Common : DescriptorStorageType == kThornTypePsysPropAnim
              ? PsysPropDesc.Common : DescriptorStorageType == kThornTypeAudioPropAnim
                  ? AudioPropDesc.Common : DescriptorStorageType == kThornTypeLightPropAnim
                      ? LightDesc.Common : DescriptorStorageType == kThornTypeReverbPropAnim
                          ? ReverbDesc.Common : DescriptorStorageType == kThornTypeCameraPropAnim
                              ? CameraDesc.Common : DescriptorStorageType == kThornTypeSetCamera
                                  ? SetCameraDesc : DescriptorStorageType == kThornTypeFloorHeightAnim
                                      ? FloorDesc.Common : DescriptorStorageType == kThornTypePathAnim
                                          ? PathDesc.Common : UserEventDesc.Common),
      PcurvePeriod(DescriptorStorageType == kThornTypeFogPropAnim
          ? FogDesc.Common.Field28 : DescriptorStorageType == kThornTypePsysPropAnim
              ? PsysPropDesc.Common.Field28 : DescriptorStorageType == kThornTypeAudioPropAnim
                  ? AudioPropDesc.Common.Field28 : DescriptorStorageType == kThornTypeLightPropAnim
                      ? LightDesc.Common.Field28 : DescriptorStorageType == kThornTypeReverbPropAnim
                          ? ReverbDesc.Common.Field28 : DescriptorStorageType == kThornTypeCameraPropAnim
                              ? CameraDesc.Common.Field28 : DescriptorStorageType == kThornTypeSetCamera
                                  ? SetCameraDesc.Field28 : DescriptorStorageType == kThornTypeFloorHeightAnim
                                      ? FloorDesc.Common.Field28 : DescriptorStorageType == kThornTypePathAnim
                                          ? PathDesc.Common.Field28 : UserEventDesc.Common.Field28), SpatialMask(0),
      AxisRotAngle(0.0f), AxisRotAxis(1), AxisRotApplied(0.0f), SpatialWrapLast(0.0f),
      bHasTargetPos(false),
      bHasTargetOrient(false), bCaptured(false), UserEventString(nullptr), CamMask(CameraDesc.Mask), CamTarget(CameraDesc.Target), LightMask(LightDesc.Mask), LightTarget(LightDesc.Target),
      StartPercent(PathDesc.StartPercent), StopPercent(PathDesc.StopPercent),
      PercentDelta(PathDesc.PercentDelta), PathOffset(PathDesc.Offset), Animation(nullptr),
      LockedBone(nullptr), TransTime(0.0f), TimeScale(1.0f), Weight(1.0f), Heading(0.0f),
      TransScale(1.0f), MotionStartTime(0.0f), Field_0x40(0), Field_0x50(0), SubSceneStartTime(0.0f), Hardpoint(nullptr),
      ParentHardpoint(nullptr), EndEffector(nullptr), LegacyTargetPart(nullptr),
      TargetPart(DescriptorStorageType == kThornTypeFloorHeightAnim ? FloorDesc.TargetPart : LegacyTargetPart),
      LegacyTargetType(0), TargetType(DescriptorStorageType == kThornTypeFloorHeightAnim ? FloorDesc.TargetType : LegacyTargetType),
      LookAtUpAxis(PathDesc.LookAtUpAxis), LookAtFrontAxis(PathDesc.LookAtFrontAxis),
      bPointAt(0), bMoveTo(0), IkId(0),
      CountToRoot(0),
      Damping(DescriptorStorageType == kThornTypeStartIk ? 1.0f : 0.0f),
      TransitionDuration(DescriptorStorageType == kThornTypeStartIk ? 1.0f : 0.0f), SparamTarget(PsysPropDesc.Sparam), SparamCapture(0.0f), PsysMask(PsysPropDesc.Mask),
      AudioMask(AudioPropDesc.Mask), AudioTarget(AudioPropDesc.Target), AudioCapture{}, FogMask(FogDesc.Mask), FogTarget(FogDesc.Target), FogCaptureOn(0), FogCaptureStart(0.0f), FogCaptureEnd(0.0f),
      FogCaptureDensity(0.0f), ReverbMask(ReverbDesc.Mask), ReverbTarget(ReverbDesc.Target), FloorTarget(FloorDesc.FloorHeight), FloorCapture(0.0f),
      FloorWrapLast(0.0f), Strid(0),
      SoundStartTime(0)
{
    bIkStorageActive = NativeEventType == kThornTypeStartIk;
    UserEventDesc.ExternalPayloadWord = 0;
    std::memset(&Desc, 0, sizeof(Desc));
    Desc.UniqueId = -1;
    Desc.PrimaryIndex = -1;
    Desc.SecondaryIndex = -1;
    Desc.Field28 = -1;
    StartPercent = 0.0f;
    StopPercent = 1.0f;
    PercentDelta = 1.0f;
    LookAtUpAxis = 1;
    LookAtFrontAxis = 5;
    std::memset(TargetPos, 0, sizeof(TargetPos));
    IdentityOrient(TargetOrient);
    std::memset(CapturePos, 0, sizeof(CapturePos));
    IdentityOrient(CaptureOrient);
    IdentityQuat4(TargetQuat);
    IdentityQuat4(CaptureQuat);
    std::memset(CamTarget, 0, sizeof(CamTarget));
    std::memset(CamCapture, 0, sizeof(CamCapture));
    std::memset(&LightTarget, 0, sizeof(LightTarget));
    std::memset(&LightCapture, 0, sizeof(LightCapture));
    std::memset(PathOffset, 0, sizeof(PathOffset));
    std::memset(PointAt, 0, sizeof(PointAt));
    std::memset(AttachXform, 0, sizeof(AttachXform));
    AttachXform[0] = 1.0f;
    AttachXform[4] = 1.0f;
    AttachXform[8] = 1.0f;
    // 47291..472b6 defaults; the actual owning target is the public packet.
    AudioTarget.Dmin = 50.0f;
    AudioTarget.Dmax = 1000.0f;
    AudioTarget.Ain = 0x168;
    AudioTarget.Aout = 0x168;
    std::memset(&FogTarget, 0, sizeof(FogTarget));
    std::memset(FogCaptureColor, 0, sizeof(FogCaptureColor));
    std::memset(&FogCapturePacked, 0, sizeof(FogCapturePacked));
    // TODO(49070): retail leaves the four target words unwritten on mismatch;
    // the defined native packet defaults remain a separately tracked gap.
    std::memset(ReverbCapture, 0, sizeof(ReverbCapture));
}

// Descriptor callbacks run inside native construction, before MASTER publication.
// If they throw, C++ unwinds the delegated owner and the new-expression storage.
// Exact retail typed-constructor subobject paths remain tracked separately.
FThornEvent::FThornEvent(int Type, const void* Desc)
    : FThornEvent(Type)
{
    FThornEvent* Event = this;
    struct FConstructorUnwindStage
    {
        FThornEvent* Event;
        bool bClearCurveOnFailure;
        bool bCompleted;

        ~FConstructorUnwindStage()
        {
            // The traced typed constructor unwind maps destroy only the
            // common base. This local runs before the delegated destructor.
            if (!bCompleted && bClearCurveOnFailure)
                Event->ParamCurve = nullptr;
        }
    };
    const bool bClearCurveOnFailure =
        Type == kThornTypeLightPropAnim ||
        Type == kThornTypeReverbPropAnim ||
        Type == kThornTypePsysPropAnim ||
        Type == kThornTypeFogPropAnim ||
        Type == kThornTypeAudioPropAnim ||
        Type == kThornTypeCameraPropAnim ||
        Type == kThornTypeSubtitle ||
        Type == kThornTypePathAnim ||
        Type == kThornTypeFloorHeightAnim ||
        Type == kThornTypeSpatialPropAnim ||
        Type == kThornTypeAttachEntity;
    FConstructorUnwindStage ConstructorUnwind = {Event, bClearCurveOnFailure, false};
    if (Type == kThornTypeLightPropAnim)
    {
        Event->ApplyLightEventDesc(Desc);
        // 455c8/455cf publishes the independent ID only after the setter.
        Event->Desc.UniqueId = Event->UniqueId;
    }
    else if (Type == kThornTypeReverbPropAnim)
    {
        Event->ApplyReverbPropAnimDesc(Desc);
        // 490b8/490bf: publish the independent ID after setter callbacks.
        Event->Desc.UniqueId = Event->UniqueId;
    }
    else if (Type == kThornTypePsysPropAnim)
    {
        Event->ApplyPsysPropAnimDesc(Desc);
        // 48888/4888f publishes the independent ID after setter callbacks.
        Event->Desc.UniqueId = Event->UniqueId;
    }
    else if (Type == kThornTypeFogPropAnim)
    {
        Event->ApplyFogPropAnimDesc(Desc);
        // 48c28..48c2f publishes the independent ID after setter callbacks.
        Event->Desc.UniqueId = Event->UniqueId;
    }
    else if (Type == kThornTypeAudioPropAnim)
    {
        Event->ApplyAudioPropAnimDesc(Desc);
        // 471e8/471ef publishes the independent ID after setter callbacks.
        Event->Desc.UniqueId = Event->UniqueId;
    }
    else if (Type == kThornTypeSetCamera)
    {
        Event->ApplySetCameraDesc(Desc);
        // 44e08..44e0f publishes the independent ID after common callbacks.
        Event->Desc.UniqueId = Event->UniqueId;
    }
    else if (Type == kThornTypeCameraPropAnim)
    {
        Event->ApplyCameraPropAnimDesc(Desc);
        // 45fc8..45fcf publishes the independent ID after setter callbacks.
        Event->Desc.UniqueId = Event->UniqueId;
    }
    else if (Type == kThornTypeStartSubScene)
    {
        float StartTimeValue = 0.0f;
        std::memcpy(&StartTimeValue, static_cast<const unsigned char*>(Desc) + 44, sizeof(StartTimeValue));
        Event->ApplySubSceneDesc(Desc, StartTimeValue);
        // Own constructor publishes the independent ID after the setter.
        Event->Desc.UniqueId = Event->UniqueId;
    }
    else if (Type == kThornTypeSubtitle)
    {
        Event->ApplySubtitleDesc(Desc);
        // Own constructor publishes the independent ID after the setter.
        Event->Desc.UniqueId = Event->UniqueId;
    }
    else if (Type == kThornTypeUserEvent)
    {
        Event->bUserDescriptorOwner = true;
        Event->ApplyUserEventDesc(Desc);
        // 44b98..44b9f: overwrite the incoming descriptor ID after setter callbacks.
        Event->Desc.UniqueId = Event->UniqueId;
    }
    else if (Type == kThornTypeAttachEntity)
    {
        Event->ApplyAttachDesc(Desc);
        // 47708/4770b publishes the independent ID after setter callbacks.
        Event->Desc.UniqueId = Event->UniqueId;
    }
    else if (Type == kThornTypeConnectHardpoints)
        Event->InitializeConnectDesc(Desc);
    else if (Type == kThornTypePathAnim)
    {
        Event->ApplyPathAnimDesc(Desc);
        // Own constructor publishes the independent ID after the setter.
        Event->Desc.UniqueId = Event->UniqueId;
    }
    else if (Type == kThornTypeSpatialPropAnim)
    {
        Event->ApplySpatialPropAnimDesc(Desc);
        // 46509/4650c: typed construction publishes its id after the setter.
        Event->Desc.UniqueId = Event->UniqueId;
    }
    else if (Type == kThornTypeStartMotion)
    {
        Event->ApplyMotionDesc(Desc);
        // 43a30/43a37 publishes the independent ID after setter callbacks.
        Event->Desc.UniqueId = Event->UniqueId;
    }
    else if (Type == kThornTypeStartIk)
    {
        Event->ApplyStartIkDesc(Desc);
        // 43e7c/43e83: publish the independent ID after the specialized setter.
        Event->Desc.UniqueId = Event->UniqueId;
    }
    else if (Type == kThornTypeStartPsys)
    {
        Event->ApplyStartPsysDesc(Desc);
        // Own constructor publishes the independent ID after the setter.
        Event->Desc.UniqueId = Event->UniqueId;
    }
    else if (Type == kThornTypeStartSound)
    {
        Event->ApplyStartSoundDesc(Desc);
        // 44958/4495f: publish the independent ID after setter callbacks.
        Event->Desc.UniqueId = Event->UniqueId;
    }
    else if (Type == kThornTypeFloorHeightAnim)
    {
        Event->ApplyFloorHeightDesc(Desc);
        // 4949b/4949e publishes the independent event ID after the setter.
        Event->Desc.UniqueId = Event->UniqueId;
    }
    else
        Event->ApplyDesc(Desc);


    ConstructorUnwind.bCompleted = true;
}

void FThornEvent::DestroyNative(FThornEvent*& Event)
{
    if (Event == nullptr)
        return;
    // The extended retail wrappers (FUN_06f43a50/FUN_06f43ea0) reach the same
    // native virtual owner; type-specific string cleanup is selected by the
    // destructor before the current-CRT delete.
    delete Event;
    Event = nullptr;
}

FThornEvent::~FThornEvent() noexcept(false)
{
    struct FCommonCleanupStage
    {
        FThornEvent* Event;
        ~FCommonCleanupStage() { Event->ParentCsys = nullptr; }
    } CommonCleanup{this};
    const int CleanupType = NativeEventType != 0 ? NativeEventType : Type;
    if (NativeEventType == kThornTypeConnectHardpoints || bConnectStorageActive)
    {
        ReleaseConnectStringStorage();
        return;
    }
    if (NativeEventType == kThornTypeStartMotion || bMotionStorageActive)
    {
        ReleaseMotionOwnedState();
        return;
    }
    if (CleanupType == kThornTypeStartSound || CleanupType == kThornTypeStartSubScene ||
        CleanupType == kThornTypeStartPsys)
        return;

    if (NativeEventType == kThornTypeStartIk || bIkStorageActive)
    {
        // 43ec0/592c7/592b8: target owner, endpoint owner, then common base.
        // The IK class never releases the copied curve.
        ReleaseIkOwnedState();
        ParentCsys = nullptr;
        return;
    }
    if (&Desc == &FloorDesc.Common)
    {
        // 494e0: live curve Release, private string teardown, base parent clear.
        ReleaseFloorOwnedState();
        ParentCsys = nullptr;
        return;
    }
    if (&Desc == &SetCameraDesc)
    {
        // 44e30 -> 44e50 -> 439a0 clears borrowed parent, without curve Release.
        ParentCsys = nullptr;
        ParamCurve = nullptr;
        return;
    }
    if (bUserDescriptorOwner)
    {
        // 44be0 -> 439a0 clears the borrowed parent. USER never releases
        // its copied curve, including delegated-constructor failure cleanup.
        ParentCsys = nullptr;
        ParamCurve = nullptr;
        return;
    }
    if (CleanupType == kThornTypeConnectHardpoints || bConnectStorageActive)
        ReleaseConnectStringStorage();
    if (bFloorStorageActive || CleanupType == kThornTypeFloorHeightAnim)
        ReleaseFloorOwnedState();
    else if (bSpatialStorageActive)
        ReleaseSpatialOwnedState();
    else if (&Desc == &CameraDesc.Common || CleanupType == kThornTypeCameraPropAnim)
        ReleaseCameraOwnedState();
    else if (CleanupType == kThornTypeAudioPropAnim)
        ReleaseAudioOwnedState();
    else if (CleanupType == kThornTypeSpatialPropAnim)
        ReleaseSpatialOwnedState();
    else if (bAttachStorageActive || CleanupType == kThornTypeAttachEntity)
        ReleaseAttachOwnedState();
    else if (ParamCurve != nullptr)
    {
        ParamCurve->Release();
        ParamCurve = nullptr;
    }
    delete[] UserEventString;
    UserEventString = nullptr;
    delete[] Animation;
    Animation = nullptr;
    delete[] LockedBone;
    LockedBone = nullptr;
    if (!bConnectStorageActive)
        delete[] Hardpoint;
    Hardpoint = nullptr;
    if (!bConnectStorageActive)
        delete[] ParentHardpoint;
    ParentHardpoint = nullptr;
    if (CleanupType == kThornTypeStartMotion)
        ReleaseMotionOwnedState();
    if (!bSpatialStorageActive && CleanupType == kThornTypeStartIk)
        ReleaseIkOwnedState();
    else
    {
        delete[] EndEffector;
        EndEffector = nullptr;
    }
    if (!bFloorStorageActive && !bSpatialStorageActive && !bAttachStorageActive && CleanupType != kThornTypeStartIk && CleanupType != kThornTypeSpatialPropAnim &&
        CleanupType != kThornTypeAttachEntity)
    {
        delete[] TargetPart;
        TargetPart = nullptr;
    }
    if (bOwnsParentCsys)
        delete ParentCsys;
    ParentCsys = nullptr;
    bOwnsParentCsys = 0;
}

void FThornEvent::ApplyDesc(const void* DescIn)
{
    if (&Desc == &FloorDesc.Common)
    {
        ApplyFloorHeightDesc(DescIn);
        return;
    }
    if (&Desc == &SetCameraDesc)
    {
        ApplySetCameraDesc(DescIn);
        return;
    }
    if (&Desc == &ReverbDesc.Common)
    {
        ApplyReverbPropAnimDesc(DescIn);
        return;
    }
    if (&Desc == &LightDesc.Common)
    {
        ApplyLightEventDesc(DescIn);
        return;
    }
    if (&Desc == &AudioPropDesc.Common)
    {
        ApplyAudioPropAnimDesc(DescIn);
        return;
    }
    // Fallback for wrappers not yet split: their payload/tail ordering remains
    // a tracked gap. Specialized wrappers call the two operations explicitly.
    ApplyCommonDesc(DescIn);
    FinalizeDesc();
}

void FThornEvent::ApplyCommonDesc(const void* DescIn)
{
    // Every retail caller snapshots the common record before 438c0 callbacks.
    FThornEventDesc Incoming;
    std::memcpy(&Incoming, DescIn, sizeof(Incoming));
    Desc.Type = Incoming.Type;
    Desc.StartMs = Incoming.StartMs;
    Desc.Flags = Incoming.Flags;
    Desc.EventFlags = Incoming.EventFlags;
    Desc.Field10 = Incoming.Field10;
    Desc.UniqueId = Incoming.UniqueId;
    Desc.DurationMs = Incoming.DurationMs;
    Desc.PrimaryIndex = Incoming.PrimaryIndex;
    Desc.SecondaryIndex = Incoming.SecondaryIndex;
    if (ParamCurve != nullptr)
        ParamCurve->Release();
    ParamCurve = Incoming.ParamCurve;
    if (ParamCurve != nullptr)
        ParamCurve->AddRef();
    PcurvePeriod = Incoming.Field28;
}

void FThornEvent::FinalizeDesc()
{
    // Native mirrors belong to the per-type wrapper tail after common438c0.
    Type = Desc.Type;
    StartTimeMs = Desc.StartMs;
    PrimaryIndex = Desc.PrimaryIndex;
    SecondaryIndex = Desc.SecondaryIndex;
    // Every retail per-type ApplyDesc tail (FUN_06f465f0 thorn.dll.c:28066-
    // 28070, FUN_06f48700 28799-28802, ...) sets +0x1c = Desc.DurationMs + Desc.StartMs
    // unconditionally.  The USE_SCRIPT_DURATION -> -1 selection is the
    // Start-time prologue (ApplyEndTime), which START_PSYS alone lacks.
    const unsigned int EndBits = static_cast<unsigned int>(StartTimeMs) +
                                 static_cast<unsigned int>(Desc.DurationMs);
    std::memcpy(&EndTimeMs, &EndBits, sizeof(EndTimeMs));
}

void FThornEvent::ApplyUserEventDesc(const void* DescIn)
{
    ApplyCommonDesc(DescIn);
    Desc.Type = kThornTypeUserEvent;
    UserEventDesc.ExternalPayloadWord = ReadEventWord(DescIn, 0) == kThornTypeUserEvent
        ? static_cast<unsigned int>(ReadEventWord(DescIn, 11)) : 0u;
    FinalizeDesc();
}

void FThornEvent::ApplySubtitleDesc(const void* DescIn)
{
    ApplyCommonDesc(DescIn);
    Desc.Type = kThornTypeSubtitle;
    // FUN_06f49d00 preserves the existing subtitle id on a mismatched
    // descriptor, while the matching type copies dword 11.
    if (ReadEventWord(DescIn, 0) == kThornTypeSubtitle)
        std::memcpy(&Strid, static_cast<const unsigned char*>(DescIn) + 44, sizeof(Strid));
    FinalizeDesc();
}

void FThornEvent::ApplySubSceneDesc(const void* DescIn, float StartTimeValue)
{
    (void)StartTimeValue;
    ApplyCommonDesc(DescIn);
    Desc.Type = kThornTypeStartSubScene;
    if (ReadEventWord(DescIn, 0) == kThornTypeStartSubScene)
        std::memcpy(&SubSceneStartTime, static_cast<const unsigned char*>(DescIn) + 44, sizeof(SubSceneStartTime));
    else
        SubSceneStartTime = 0.0f;
    FinalizeDesc();
}

void FThornEvent::InitializeConnectDesc(const void* DescIn)
{
    // 48050 has already initialized the common header, eleven descriptor
    // defaults and both string owners. Keep both calls on the original input:
    // callbacks from the first call may change it before the second snapshot.
    ApplyConnectDesc(DescIn); // 480b8
    Hardpoint = nullptr;     // 480c0: exposed pointers only
    ParentHardpoint = nullptr; // 480c3
    ApplyConnectDesc(DescIn); // 480c6; private string owners survive
    Desc.UniqueId = UniqueId;       // 480d2, only after both setter calls complete
}

void FThornEvent::ApplyConnectDesc(const void* DescIn)
{
    // FUN_06f48190: common eleven words, force CONNECT, two owned strings.
    ApplyCommonDesc(DescIn);
    Desc.Type = kThornTypeConnectHardpoints;
    AdoptConnectStringStorage();
    // 481c9/481cc clear exposed pointers without destroying either owner.
    Hardpoint = nullptr;
    ParentHardpoint = nullptr;
    const bool bMatchingType = ReadEventWord(DescIn, 0) == kThornTypeConnectHardpoints;
    if (bMatchingType)
    {
        const char* HardpointName =
            reinterpret_cast<const char*>(static_cast<INT_PTR>(ReadEventWord(DescIn, 11)));
        if (HardpointName != nullptr)
            SetConnectHardpoint(HardpointName);
        // 48237 reads the second argument after the first string operation.
        const char* ParentHardpointName =
            reinterpret_cast<const char*>(static_cast<INT_PTR>(ReadEventWord(DescIn, 12)));
        if (ParentHardpointName != nullptr)
            SetConnectParentHardpoint(ParentHardpointName);
    }
    FinalizeDesc();
}

void FThornEvent::ApplyAttachDesc(const void* DescIn)
{
    bAttachStorageActive = true;
    ApplyCommonDesc(DescIn);
    Desc.Type = kThornTypeAttachEntity;
    TargetPart = nullptr;
    if (ReadEventWord(DescIn, 0) == kThornTypeAttachEntity)
    {
        std::memcpy(PathOffset, static_cast<const unsigned char*>(DescIn) + 44, sizeof(PathOffset));
        TargetType = ReadEventWord(DescIn, 14);
        LookAtUpAxis = ReadEventWord(DescIn, 15);
        LookAtFrontAxis = ReadEventWord(DescIn, 16);
        const char* TargetPartName = reinterpret_cast<const char*>(static_cast<INT_PTR>(ReadEventWord(DescIn, 17)));
        if (TargetPartName != nullptr)
        {
            AttachTargetPartStorage.Assign(TargetPartName);
            TargetPart = AttachTargetPartStorage.GetPublishedData();
        }
    }
    else
    {
        std::memset(PathOffset, 0, sizeof(PathOffset));
        TargetType = 0;
        LookAtUpAxis = 1;
        LookAtFrontAxis = 5;
    }
    FinalizeDesc();
}

void FThornEvent::ApplyPathAnimDesc(const void* DescIn)
{
    // Retail FUN_06f450a0: common eleven-word copy, force type 6, then either
    // (input type == 6) copy StartPercent/StopPercent/PercentDelta/PathOffset
    // /LookAtUpAxis/LookAtFrontAxis from dwords 11..18, or (mismatch) reset
    // them to the construction-time defaults (0.0, 1.0, 1.0, {0,0,0}, 1, 5).
    ApplyCommonDesc(DescIn);
    Desc.Type = kThornTypePathAnim;
    if (ReadEventWord(DescIn, 0) == kThornTypePathAnim)
    {
        // DescIn[11..16] are raw IEEE-754 float bit patterns, not integers to
        // convert -- memcpy preserves the bits (same convention as the other
        // float payloads carried past dword 11 in this descriptor format).
        std::memcpy(&StartPercent, static_cast<const unsigned char*>(DescIn) + 44, sizeof(StartPercent));
        std::memcpy(&StopPercent, static_cast<const unsigned char*>(DescIn) + 48, sizeof(StopPercent));
        std::memcpy(&PercentDelta, static_cast<const unsigned char*>(DescIn) + 52, sizeof(PercentDelta));
        std::memcpy(PathOffset, static_cast<const unsigned char*>(DescIn) + 56, sizeof(PathOffset));
        LookAtUpAxis = ReadEventWord(DescIn, 17);
        LookAtFrontAxis = ReadEventWord(DescIn, 18);
    }
    else
    {
        StartPercent = 0.0f;
        StopPercent = 1.0f;
        PercentDelta = 1.0f;
        PathOffset[0] = 0.0f;
        PathOffset[1] = 0.0f;
        PathOffset[2] = 0.0f;
        LookAtUpAxis = 1;
        LookAtFrontAxis = 5;
    }
    FinalizeDesc();
}

void FThornEvent::ApplyLightEventDesc(const void* DescIn)
{
    ApplyCommonDesc(DescIn);
    // 4568e forces the owner type before rereading input after callbacks.
    Desc.Type = kThornTypeLightPropAnim;
    if (ReadEventWord(DescIn, 0) == kThornTypeLightPropAnim)
    {
        LightMask = ReadEventWord(DescIn, 11);
        // 456ac forward REP MOVSD: preserve each DWORD, including any alias
        // with the destination rather than imposing memcpy's non-overlap rule.
        unsigned char* Destination = reinterpret_cast<unsigned char*>(&LightTarget);
        const unsigned char* Source = reinterpret_cast<const unsigned char*>(static_cast<const unsigned char*>(DescIn) + 48);
        for (int Index = 0; Index < 0x24; ++Index)
        {
            unsigned int Word;
            std::memcpy(&Word, Source + Index * sizeof(Word), sizeof(Word));
            std::memcpy(Destination + Index * sizeof(Word), &Word, sizeof(Word));
        }
    }
    else
    {
        // 456b0 clears only the mask. The previous target payload survives.
        LightMask = 0;
    }
    FinalizeDesc();
}

void FThornEvent::ApplyMotionDesc(const void* DescIn)
{
    // FUN_06f43af0: input descriptor -> owned event state, not serialization.
    bMotionStorageActive = true;
    ApplyCommonDesc(DescIn);
    Desc.Type = kThornTypeStartMotion;
    Animation = nullptr;
    LockedBone = nullptr;
    if (ReadEventWord(DescIn, 0) == kThornTypeStartMotion)
    {
        // 43b36..43b63 stores every scalar before either string operation.
        std::memcpy(&Heading, static_cast<const unsigned char*>(DescIn) + 68, sizeof(Heading));
        Field_0x40 = static_cast<unsigned char>(ReadEventWord(DescIn, 16));
        std::memcpy(&MotionStartTime, static_cast<const unsigned char*>(DescIn) + 44, sizeof(MotionStartTime));
        std::memcpy(&TimeScale, static_cast<const unsigned char*>(DescIn) + 52, sizeof(TimeScale));
        Field_0x50 = ReadEventWord(DescIn, 20);
        std::memcpy(&TransTime, static_cast<const unsigned char*>(DescIn) + 48, sizeof(TransTime));
        std::memcpy(&TransScale, static_cast<const unsigned char*>(DescIn) + 76, sizeof(TransScale));
        std::memcpy(&Weight, static_cast<const unsigned char*>(DescIn) + 56, sizeof(Weight));
        const char* AnimationName = reinterpret_cast<const char*>(static_cast<INT_PTR>(ReadEventWord(DescIn, 15)));
        if (AnimationName != nullptr)
        {
            MotionAnimationStorage.Assign(AnimationName);
            Animation = MotionAnimationStorage.GetPublishedData();
        }
        const char* BoneName = reinterpret_cast<const char*>(static_cast<INT_PTR>(ReadEventWord(DescIn, 18)));
        if (BoneName != nullptr)
        {
            MotionLockedBoneStorage.Assign(BoneName);
            LockedBone = MotionLockedBoneStorage.GetPublishedData();
        }
    }
    else
    {
        MotionStartTime = 0.0f;
        TransTime = 0.0f;
        TimeScale = 1.0f;
        Weight = 1.0f;
        Field_0x40 = 0;
        Heading = -1.0f;
        TransScale = 1.0f;
        Field_0x50 = 0;
    }
    FinalizeDesc();
}

void FThornEvent::ApplySpatialPropAnimDesc(const void* DescIn)
{
    // FUN_06f465f0 (thorn.dll.c:27941-28072).  Pre-reset: +0x98 axis-rot
    // angle = 0, +0x9c axis id = 1, +0x104 applied rotation = 0; common
    // eleven-word copy; forced Type 7; +0x54 mask, +0xa0 target-part c_str
    // and +0xa4 target type cleared.  Only an input type 7 carries the
    // payload: Desc[11] mask, Desc[12..20] target orientation (+0x58),
    // Desc[21..23] target position (+0x7c), Desc[24..27] quaternion (+0x88)
    // when kThornFlagQuatOrient is set in the copied flags -- otherwise the
    // quaternion is derived from the orientation through math vtbl+0x50
    // (SceneMatrixToQuat) -- Desc[28] angle, Desc[29] axis, Desc[30] target
    // part string (VC6 basic_string at +0xa8), Desc[31] target type.  The
    // retail Lua loader (FUN_06f25340 case 7, thorn.dll.c:4897-5011) writes
    // the same slots: [0x26]/[0x27] axisrot, [0x1f..0x21] pos,
    // [0x22..0x25] q_orient, and the tick LAB_06f46a40 reads +0x98/+0x9c
    // (thorn.dll.txt:74386-74388) and +0xa0/+0xa4 (74199-74224).
    AxisRotAngle = 0.0f;
    AxisRotApplied = 0.0f;
    AxisRotAxis = 1;
    ApplyCommonDesc(DescIn);
    Desc.Type = kThornTypeSpatialPropAnim;
    SpatialMask = 0;
    AdoptSpatialStringStorage();
    TargetPart = nullptr; // 46636 clears exposure, not the private owner.
    TargetType = 0;
    if (ReadEventWord(DescIn, 0) != kThornTypeSpatialPropAnim)
    {
        FinalizeDesc();
        return;
    }
    SpatialMask = ReadEventWord(DescIn, 11);
    const bool bQuaternionOrientation = (Desc.Flags & kThornFlagQuatOrient) != 0;
    // 46665 copies twelve words forwards; retain the store boundaries if the
    // incoming descriptor aliases the owner's orientation/position storage.
    for (int Index = 0; Index < 9; ++Index)
        std::memcpy(TargetOrient + Index, static_cast<const unsigned char*>(DescIn) + 48 + Index * 4, 4);
    for (int Index = 0; Index < 3; ++Index)
        std::memcpy(TargetPos + Index, static_cast<const unsigned char*>(DescIn) + 84 + Index * 4, 4);
    if (bQuaternionOrientation)
    {
        for (int Index = 0; Index < 4; ++Index)
            std::memcpy(reinterpret_cast<unsigned char*>(&TargetQuat) + Index * 4,
                        static_cast<const unsigned char*>(DescIn) + 96 + Index * 4, 4);
    }
    else
    {
        // 4668a..466ab: acquire before reading the original descriptor matrix.
        // Do not substitute the earlier TargetOrient snapshot for this input.
        if (G_SceneEventMathEngine == nullptr)
            G_SceneEventMathEngine = ThornAcquire3DMathEngine();
        SpatialMatrixToQuaternion(G_SceneEventMathEngine, TargetQuat,
            static_cast<const unsigned char*>(DescIn) + 48);
    }
    const char* TargetPartName = reinterpret_cast<const char*>(static_cast<INT_PTR>(ReadEventWord(DescIn, 30)));
    // 46636 already cleared exposure before the math callback. Null input
    // skips assignment entirely; do not clear a callback's later publication.
    if (TargetPartName != nullptr)
    {
        SpatialTargetPartStorage.Assign(TargetPartName);
        TargetPart = SpatialTargetPartStorage.GetPublishedData();
    }
    // Read these only after the string call, matching 46773..46788.
    TargetType = ReadEventWord(DescIn, 31);
    std::memcpy(&AxisRotAngle, static_cast<const unsigned char*>(DescIn) + 112, sizeof(AxisRotAngle));
    AxisRotAxis = ReadEventWord(DescIn, 29);
    FinalizeDesc();
}

void FThornEvent::ApplyStartSoundDesc(const void* DescIn)
{
    ApplyCommonDesc(DescIn);
    Desc.Type = kThornTypeStartSound;
    // 449e1 rereads the input after common callbacks and the forced store.
    SoundStartTime = ReadEventWord(DescIn, 0) == kThornTypeStartSound ? ReadEventWord(DescIn, 11) : 0;
    FinalizeDesc();
}

void FThornEvent::ApplySetCameraDesc(const void* DescIn)
{
    // Full33-instruction44e60: common snapshot/callbacks, force2, cached tail.
    ApplyCommonDesc(DescIn);
    Desc.Type = kThornTypeSetCamera;
    FinalizeDesc();
}

void FThornEvent::ApplyCameraPropAnimDesc(const void* DescIn)
{
    ApplyCommonDesc(DescIn);
    Desc.Type = kThornTypeCameraPropAnim;
    // 46091 reads the current input type after the common curve callbacks.
    if (ReadEventWord(DescIn, 0) == kThornTypeCameraPropAnim)
    {
        CamMask = ReadEventWord(DescIn, 11);
        for (int Index = 0; Index < 4; ++Index)
            std::memcpy(CamTarget + Index, static_cast<const unsigned char*>(DescIn) + 48 + Index * 4, 4);
    }
    else
        CamMask = 0; // 460c0 retains the four target words on mismatch.
    FinalizeDesc();
}

void FThornEvent::ApplyCameraPropAnimPayload(int DescriptorType, int Mask,
                                             const float* Target4)
{
    // FUN_06f46060: the common descriptor is applied first, then the
    // specialized owner is forced to type 5.  Only an input type 5 copies
    // the mask and four camera values; mismatch clears the mask.
    Type = kThornTypeCameraPropAnim;
    if (DescriptorType == kThornTypeCameraPropAnim && Target4 != nullptr)
    {
        CamMask = Mask;
        std::memcpy(CamTarget, Target4, sizeof(CamTarget));
    }
    else
    {
        CamMask = 0;
        std::memset(CamTarget, 0, sizeof(CamTarget));
    }
}

void FThornEvent::ApplyStartPsysDesc(const void* DescIn)
{
    // 48721 common callbacks precede the unconditional type-13 stores.
    ApplyCommonDesc(DescIn);
    Desc.Type = kThornTypeStartPsys;
    FinalizeDesc();
}

void FThornEvent::ApplyPsysPropAnimDesc(const void* DescIn)
{
    ApplyCommonDesc(DescIn);
    Desc.Type = kThornTypePsysPropAnim;
    // 48951 rereads the input after common callbacks and the forced store.
    if (ReadEventWord(DescIn, 0) == kThornTypePsysPropAnim)
    {
        PsysMask = ReadEventWord(DescIn, 11);
        // 48961/48964 reads a DWORD before its store, including self input.
        unsigned int TargetBits;
        std::memcpy(&TargetBits, static_cast<const unsigned char*>(DescIn) + 48, sizeof(TargetBits));
        std::memcpy(&SparamTarget, &TargetBits, sizeof(TargetBits));
    }
    else
    {
        PsysMask = 0;
        const unsigned int ZeroBits = 0;
        std::memcpy(&SparamTarget, &ZeroBits, sizeof(SparamTarget));
    }
    FinalizeDesc();
}

void FThornEvent::ApplyPsysPropAnimPayload(int DescriptorType, int Mask, float Target)
{
    // FUN_06f48920: type 0xe keeps the descriptor mask and sparam; a
    // mismatched source clears both specialized payload words.
    Type = kThornTypePsysPropAnim;
    if (DescriptorType == kThornTypePsysPropAnim)
    {
        PsysMask = Mask;
        SparamTarget = Target;
    }
    else
    {
        PsysMask = 0;
        SparamTarget = 0.0f;
    }
}

void FThornEvent::ApplyFogPropAnimDesc(const void* DescIn)
{
    ApplyCommonDesc(DescIn);
    Desc.Type = kThornTypeFogPropAnim;
    // 48cf1 reads the input type only after common curve callbacks return.
    if (ReadEventWord(DescIn, 0) == kThornTypeFogPropAnim)
    {
        FogMask = ReadEventWord(DescIn, 11);
        // 48d0c is a forward DWORD copy, preserving flags and padding.
        for (int Index = 0; Index < 8; ++Index)
        {
            unsigned int Word;
            std::memcpy(&Word, static_cast<const unsigned char*>(DescIn) + 48 + Index * 4, sizeof(Word));
            std::memcpy(reinterpret_cast<unsigned char*>(&FogTarget) + Index * 4, &Word, sizeof(Word));
        }
    }
    else
        FogMask = 0;
    FinalizeDesc();
}

void FThornEvent::ApplyReverbPropAnimDesc(const void* DescIn)
{
    ApplyCommonDesc(DescIn);
    Desc.Type = kThornTypeReverbPropAnim;
    // 49181 rereads type; 49194..491ab copies four raw words in order.
    if (ReadEventWord(DescIn, 0) == kThornTypeReverbPropAnim)
    {
        ReverbMask = ReadEventWord(DescIn, 11);
        for (int Index = 0; Index < 4; ++Index)
            std::memcpy(reinterpret_cast<unsigned char*>(&ReverbTarget) + Index * 4,
                        static_cast<const unsigned char*>(DescIn) + 48 + Index * 4, 4);
    }
    else
        ReverbMask = 0;
    FinalizeDesc();
}

void FThornEvent::ApplyFogPropAnimPayload(int DescriptorType, int Mask,
                                          const FThornFog8* Target)
{
    // 48cfb..48d0c copies all eight words, including both flag bytes and padding.
    // 48d10 clears only the mask on a descriptor type mismatch.
    Type = kThornTypeFogPropAnim;
    Desc.Type = kThornTypeFogPropAnim;
    if (DescriptorType == kThornTypeFogPropAnim)
    {
        FogMask = Mask;
        std::memcpy(&FogTarget, Target, sizeof(FogTarget));
    }
    else
        FogMask = 0;
}

void FThornEvent::ApplyReverbPropAnimPayload(int DescriptorType, int Mask,
                                             const FThornReverb4* Target4)
{
    // FUN_06f49150 first applies the common eleven-word descriptor through
    // FUN_06f438c0, then forces the specialized owner type to 0x11.  For a
    // matching input it copies the mask and exactly four reverb words;
    // otherwise only the specialized mask is cleared.  FThornEvent's
    // native zero defaults on unwritten retail target words remain a named
    // constructor gap; the target itself has one public owning packet.
    Type = kThornTypeReverbPropAnim;
    Desc.Type = kThornTypeReverbPropAnim;
    ReverbMask = 0;
    if (DescriptorType != kThornTypeReverbPropAnim)
        return;
    ReverbMask = Mask;
    std::memcpy(&ReverbTarget, Target4, sizeof(ReverbTarget));
}

void FThornEvent::ApplyAudioPropAnimDesc(const void* DescIn)
{
    // 47291..472b6 ordered raw defaults precede the common curve callbacks.
    AudioTarget.Ain = 0x168;
    AudioTarget.Aout = 0x168;
    const unsigned int ZeroBits = 0;
    const unsigned int MinBits = 0x42480000;
    const unsigned int MaxBits = 0x447a0000;
    std::memcpy(&AudioTarget.Attenuation, &ZeroBits, 4);
    std::memcpy(&AudioTarget.Pan, &ZeroBits, 4);
    std::memcpy(&AudioTarget.Dmin, &MinBits, 4);
    std::memcpy(&AudioTarget.Dmax, &MaxBits, 4);
    std::memcpy(&AudioTarget.Atout, &ZeroBits, 4);
    std::memcpy(&AudioTarget.Rmix, &ZeroBits, 4);
    ApplyCommonDesc(DescIn);
    Desc.Type = kThornTypeAudioPropAnim;
    // 472d1 rereads input type after callbacks; mismatch clears only mask.
    if (ReadEventWord(DescIn, 0) == kThornTypeAudioPropAnim)
    {
        AudioMask = ReadEventWord(DescIn, 11);
        // Exact DWORD-forward transport, including self/overlapping input.
        for (int Index = 0; Index < 8; ++Index)
        {
            unsigned int Word;
            std::memcpy(&Word, static_cast<const unsigned char*>(DescIn) + 48 + Index * 4, 4);
            std::memcpy(reinterpret_cast<unsigned char*>(&AudioTarget) + Index * 4, &Word, 4);
        }
    }
    else
        AudioMask = 0;
    FinalizeDesc();
}

void FThornEvent::ReleaseAudioOwnedState()
{
    // FUN_06f47230 releases the specialized object's +0x4c pointer before
    // invoking common event cleanup. In the portable owner that pointer is
    // the optional parameter curve kept in ParamCurve; release it once and do
    // not reproduce the retail subobject/vtable layout.
    if (ParamCurve != nullptr)
    {
        ParamCurve->Release();
        ParamCurve = nullptr;
    }
}

void FThornEvent::ReleaseCameraOwnedState()
{
    // FUN_06f46010 releases the camera event's optional +0x4c curve and then
    // delegates to FUN_06f439a0.  ParamCurve is the modern owner-side form;
    // the retail vtable reset/deleting-destructor wrapper is intentionally
    // not part of the current-CRT object contract.
    if (ParamCurve != nullptr)
    {
        ParamCurve->Release();
        ParamCurve = nullptr;
    }
}

void FThornEvent::ReleaseSpatialOwnedState()
{
    // FUN_06f46550 releases +0x4c, then decrements/frees the ref-counted
    // target-part string at +0xac and clears its three words.  The modern
    // owner uses the already-owned curve and native string independently of
    // the retail VC6 basic_string representation.
    AdoptSpatialStringStorage();
    try
    {
        if (ParamCurve != nullptr)
        {
            ParamCurve->Release();
        }
    }
    catch (...)
    {
        // State 1 -> 0 invokes private-string destruction (59438) even if
        // the curve Release throws; common-tail cleanup follows during unwind.
        SpatialTargetPartStorage.Reset();
        throw;
    }
    SpatialTargetPartStorage.Reset();
    // Retail only clears the private owner words; the exposed pointer remains
    // stale during final common cleanup. No subsequent consumer owns it.
}

void FThornEvent::ReleaseAttachOwnedState()
{
    try
    {
        if (ParamCurve != nullptr)
            ParamCurve->Release();
    }
    catch (...)
    {
        // 594c8 destroys the private target owner before common-base unwind.
        AttachTargetPartStorage.Reset();
        throw;
    }
    AttachTargetPartStorage.Reset();
}

void FThornEvent::ReleaseIkOwnedState()
{
    // 43ec0 retains the exposed descriptor pointers during private teardown.
    IkTargetPartStorage.Reset();
    IkEndEffectorStorage.Reset();
}

void FThornEvent::SetIkEndEffector(const char* Name)
{
    bIkStorageActive = true;
    EndEffector = nullptr;
    if (Name != nullptr)
    {
        IkEndEffectorStorage.Assign(Name);
        EndEffector = IkEndEffectorStorage.GetPublishedData();
    }
}

void FThornEvent::SetIkTargetPart(const char* Name)
{
    bIkStorageActive = true;
    TargetPart = nullptr;
    if (Name != nullptr)
    {
        IkTargetPartStorage.Assign(Name);
        TargetPart = IkTargetPartStorage.GetPublishedData();
    }
}

void FThornEvent::ReleaseMotionOwnedState()
{
    // 43a70: locked-bone owner, animation owner, then common base; no curve Release.
    MotionLockedBoneStorage.Reset();
    MotionAnimationStorage.Reset();
}

float FThornEvent::EvaluateControlBackedCurve(float Scale, int TimeMs)
{
    if (Scale == 0.0f) return 1.0f;
    const unsigned Elapsed = static_cast<unsigned>(TimeMs) - static_cast<unsigned>(Desc.StartMs);
    const int Duration = Desc.DurationMs;
    IParamCurve* Curve = ParamCurve;
    const double Quotient = ThornSceneInterpolation::WrapIntermediate(static_cast<double>(Elapsed) / Duration);
    const float Ratio = static_cast<float>(Quotient);
    double Value = Quotient;
    if (Curve != nullptr)
    {
        const int Period = PcurvePeriod;
        int SignedElapsed;
        std::memcpy(&SignedElapsed, &Elapsed, sizeof(SignedElapsed));
        const float CurveValue = Period > 0 ? Curve->Slot12WrapEvaluate(SignedElapsed, Period) : Curve->Evaluate(Ratio);
        Value = CurveValue;
    }
    // StartPercent is read only after the curve callback has returned.
    const double Product = ThornSceneInterpolation::WrapIntermediate(Value * static_cast<double>(Scale));
    return static_cast<float>(ThornSceneInterpolation::WrapIntermediate(Product + static_cast<double>(StartPercent)));
}

namespace
{
    int GetIkDescriptorType(const FThornIkEventDesc* Descriptor)
    {
        return Descriptor->Desc[0];
    }

    int GetIkDescriptorType(const FThornStartIkDesc* Descriptor)
    {
        return Descriptor->Common.Type;
    }

    template <typename FDescriptor>
    void ApplyIkPayload(FThornEvent* Event, const FDescriptor* Descriptor)
    {
        Event->Desc.Type = kThornTypeStartIk;
        Event->bIkStorageActive = true;
        // 43f76/43f79 precede the type gate and either string assignment.
        Event->EndEffector = nullptr;
        Event->TargetPart = nullptr;
        if (GetIkDescriptorType(Descriptor) == kThornTypeStartIk)
        {
            Event->CountToRoot = Descriptor->CountToRoot;
            Event->LookAtUpAxis = Descriptor->LookAtUpAxis;
            Event->LookAtFrontAxis = Descriptor->LookAtFrontAxis;
            CopyPoseWordsForward(&Event->Damping, &Descriptor->Damping, 1);
            Event->bPointAt = Descriptor->bPointAt;
            Event->bMoveTo = Descriptor->bMoveTo;
            CopyPoseWordsForward(&Event->TransitionDuration, &Descriptor->TransitionDuration, 1);
            if (Descriptor->EndEffector != nullptr)
            {
                Event->IkEndEffectorStorage.Assign(Descriptor->EndEffector);
                Event->EndEffector = Event->IkEndEffectorStorage.GetPublishedData();
            }
            if (Descriptor->TargetPart != nullptr)
            {
                Event->IkTargetPartStorage.Assign(Descriptor->TargetPart);
                Event->TargetPart = Event->IkTargetPartStorage.GetPublishedData();
            }
            Event->TargetType = Descriptor->TargetType;
        }
        else
        {
            Event->CountToRoot = 0;
            Event->LookAtUpAxis = 1;
            Event->LookAtFrontAxis = 5;
            Event->Damping = 1.0f;
            Event->bPointAt = 0;
            Event->bMoveTo = 0;
            Event->TransitionDuration = 1.0f;
            Event->TargetType = 0;
        }
        Event->FinalizeDesc();
    }
}

void FThornEvent::ApplyIkExtendedDesc(const FThornIkEventDesc* DescIn)
{
    ApplyCommonDesc(DescIn->Desc);
    ApplyIkPayload(this, DescIn);
}

void FThornEvent::ApplyStartIkDesc(const void* DescIn)
{
    // The canonical native packet has Key before CountToRoot. Read its payload
    // after the common callback; the parser's private aggregate has no Key.
    const FThornStartIkDesc* Descriptor = static_cast<const FThornStartIkDesc*>(DescIn);
    ApplyCommonDesc(&Descriptor->Common);
    ApplyIkPayload(this, Descriptor);
}

static FThornPathEventDesc* GetPathEventDesc(FThornEvent* Event)
{
    // 4a4b0: the selected PATH descriptor remains a live, nullable view.
    return Event != nullptr ? &Event->PathDesc : nullptr;
}

void* FThornEvent::GetDesc()
{
    if (&Desc == &PathDesc.Common)
        return GetPathEventDesc(this);
    if (&Desc == &FloorDesc.Common)
        return &FloorDesc.Common;
    if (&Desc == &SetCameraDesc)
        return &SetCameraDesc;
    // 4a6d0 returns the live descriptor even after cached Type changes.
    if (&Desc == &CameraDesc.Common)
        return &CameraDesc.Common;
    if (&Desc == &ReverbDesc.Common)
        return &ReverbDesc.Common;
    if (&Desc == &LightDesc.Common)
        return &LightDesc.Common;
    if (&Desc == &AudioPropDesc.Common)
        return &AudioPropDesc.Common;
    if (Type == kThornTypeStartSubScene)
    {
        std::memcpy(&SubSceneDesc.Common, &Desc, sizeof(SubSceneDesc.Common));
        std::memcpy(&SubSceneDesc.StartTime, &SubSceneStartTime, sizeof(SubSceneStartTime));
        return &SubSceneDesc.Common;
    }
    if (Type == kThornTypeAttachEntity)
    {
        PackAttachDesc();
        return &AttachDesc.Common;
    }
    if (Type == kThornTypeUserEvent)
    {
        return &UserEventDesc.Common;
    }
    if (Type == kThornTypeStartSound)
    {
        PackSoundDesc();
        return &SoundDesc.Common;
    }
    // LAB_06f4a470: event +0x28. For START_MOTION the retail descriptor continues past the
    // common eleven words (FUN_06f43af0 layout); hand out the packed copy.
    if (Type == kThornTypeStartMotion)
    {
        PackMotionDesc();
        return &MotionDesc.Common;
    }
    // START_IK's descriptor continues the same way (FUN_06f43f40 layout).
    if (Type == kThornTypeStartIk)
    {
        PackIkDesc();
        return &IkDesc.Common;
    }
    // CONNECT_HARDPOINTS' descriptor continues with the two hardpoint names.
    if (Type == kThornTypeConnectHardpoints)
    {
        PackConnectDesc();
        return &ConnectDesc.Common;
    }
    if (Type == kThornTypeSpatialPropAnim)
    {
        std::memcpy(&SpatialDesc.Common, &Desc, sizeof(SpatialDesc.Common));
        SpatialDesc.Mask = SpatialMask;
        std::memcpy(SpatialDesc.Orient, TargetOrient, sizeof(TargetOrient));
        std::memcpy(SpatialDesc.Position, TargetPos, sizeof(TargetPos));
        std::memcpy(SpatialDesc.Quaternion, &TargetQuat, sizeof(TargetQuat));
        std::memcpy(&SpatialDesc.AxisRotAngle, &AxisRotAngle, sizeof(AxisRotAngle));
        SpatialDesc.AxisRotAxis = AxisRotAxis;
        SpatialDesc.TargetPart = TargetPart;
        SpatialDesc.TargetType = TargetType;
        return &SpatialDesc.Common;
    }
    if (Type == kThornTypeReverbPropAnim)
        return &ReverbDesc.Common;
    if (Type == kThornTypeAudioPropAnim)
    {
        return &AudioPropDesc.Common;
    }
    if (Type == kThornTypePsysPropAnim)
    {
        // 4a670 returns the actual live 13-word packet, without repacking.
        return &PsysPropDesc.Common;
    }
    if (Type == kThornTypeFogPropAnim)
    {
        return &FogDesc.Common;
    }
    if (Type == kThornTypeSubtitle)
    {
        std::memcpy(&SubtitleDesc.Common, &Desc, sizeof(SubtitleDesc.Common));
        std::memcpy(&SubtitleDesc.Strid, &Strid, sizeof(Strid));
        return &SubtitleDesc.Common;
    }
    return &Desc;
}

void FThornEvent::PackAttachDesc()
{
    std::memcpy(&AttachDesc.Common, &Desc, sizeof(AttachDesc.Common));
    std::memcpy(AttachDesc.Offset, PathOffset, sizeof(AttachDesc.Offset));
    AttachDesc.TargetType = TargetType;
    AttachDesc.UpAxis = LookAtUpAxis;
    AttachDesc.FrontAxis = LookAtFrontAxis;
    AttachDesc.TargetPart = TargetPart;
}

void FThornEvent::PackSoundDesc()
{
    std::memcpy(&SoundDesc.Common, &Desc, sizeof(SoundDesc.Common));
    SoundDesc.SoundStartTime = SoundStartTime;
}

void FThornEvent::PackConnectDesc()
{
    // Event +0x28: the common words, then hardpoint (+0x54 = desc +0x2c) and parent_hardpoint
    // (+0x58 = desc +0x30), the name pointers themselves (not copies).
    std::memcpy(&ConnectDesc.Common, &Desc, sizeof(ConnectDesc.Common));
    ConnectDesc.Hardpoint = Hardpoint;
    ConnectDesc.ParentHardpoint = ParentHardpoint;
}

void FThornEvent::PackIkDesc()
{
    // FUN_06f43f40 (thorn_iface.h FThornStartIkDesc): the common words, then the event's own
    // address at +0x2c (0x06f43f73), count_to_root +0x30, up +0x34, front +0x38, damping +0x3c,
    // point_at +0x40, move_to +0x41, transition_duration +0x44, the two name strings' c_str at
    // +0x48 / +0x4c (null without a name, 0x06f43f76 / 0x06f43f79) and target_type +0x50.
    std::memcpy(&IkDesc.Common, &Desc, sizeof(IkDesc.Common));
    IkDesc.Key = this;
    IkDesc.CountToRoot = CountToRoot;
    IkDesc.LookAtUpAxis = LookAtUpAxis;
    IkDesc.LookAtFrontAxis = LookAtFrontAxis;
    std::memcpy(&IkDesc.Damping, &Damping, sizeof(Damping));
    IkDesc.bPointAt = bPointAt;
    IkDesc.bMoveTo = bMoveTo;
    IkDesc.Pad42[0] = 0;
    IkDesc.Pad42[1] = 0;
    std::memcpy(&IkDesc.TransitionDuration, &TransitionDuration, sizeof(TransitionDuration));
    IkDesc.EndEffector = EndEffector;
    IkDesc.TargetPart = TargetPart;
    IkDesc.TargetType = TargetType;
}

void FThornEvent::PackMotionDesc()
{
    // FUN_06f43af0's layout (thorn_iface.h FThornMotionEventDesc): the common words, then
    // start_time +0x2c, trans_time +0x30, time_scale +0x34, weight +0x38, animation +0x3c,
    // the byte +0x40, heading +0x44, locked_bone +0x48, trans_scale +0x4c, +0x50.
    std::memcpy(&MotionDesc.Common, &Desc, sizeof(MotionDesc.Common));
    std::memcpy(&MotionDesc.StartTime, &MotionStartTime, sizeof(MotionStartTime));
    std::memcpy(&MotionDesc.TransTime, &TransTime, sizeof(TransTime));
    std::memcpy(&MotionDesc.TimeScale, &TimeScale, sizeof(TimeScale));
    std::memcpy(&MotionDesc.Weight, &Weight, sizeof(Weight));
    MotionDesc.Animation = Animation;
    // Raw-descriptor fields are retained even while their semantic names are unknown.
    MotionDesc.Field40 = Field_0x40;
    MotionDesc.Pad41[0] = 0;
    MotionDesc.Pad41[1] = 0;
    MotionDesc.Pad41[2] = 0;
    std::memcpy(&MotionDesc.Heading, &Heading, sizeof(Heading));
    MotionDesc.LockedBone = LockedBone;
    std::memcpy(&MotionDesc.TransScale, &TransScale, sizeof(TransScale));
    MotionDesc.Field50 = Field_0x50;
}

float FThornEvent::EvaluateProgress(int TimeMs) const
{
    if (bUserDescriptorOwner || &Desc == &SetCameraDesc) return 0.0f;
    const int Duration = Desc.DurationMs;
    if (Duration == 0) return 1.0f;
    const unsigned Elapsed = static_cast<unsigned>(TimeMs) - static_cast<unsigned>(Desc.StartMs);
    IParamCurve* Curve = ParamCurve;
    if (Curve != nullptr && PcurvePeriod > 0)
    {
        int SignedElapsed;
        std::memcpy(&SignedElapsed, &Elapsed, sizeof(SignedElapsed));
        return Curve->Slot12WrapEvaluate(SignedElapsed, PcurvePeriod);
    }
    const float Ratio = static_cast<float>(ThornSceneInterpolation::WrapIntermediate(static_cast<double>(Elapsed) / Duration));
    return Curve != nullptr ? Curve->Evaluate(Ratio) : Ratio;
}

float FThornEvent::EvaluateProgressNoPeriodWrap(int TimeMs) const
{
    const int Duration = Desc.DurationMs;
    if (Duration == 0) return 1.0f;
    const unsigned Elapsed = static_cast<unsigned>(TimeMs) - static_cast<unsigned>(Desc.StartMs);
    IParamCurve* Curve = ParamCurve;
    const float Ratio = static_cast<float>(ThornSceneInterpolation::WrapIntermediate(static_cast<double>(Elapsed) / Duration));
    return Curve != nullptr ? Curve->Evaluate(Ratio) : Ratio;
}

void FThornEvent::EvaluateProgressExtended(int TimeMs, FThornExtendedProgress* Result,
                                           bool bNoPeriodWrap) const
{
    const int Duration = Desc.DurationMs;
    if (Duration == 0)
    {
        ThornExtendedProgress::StoreDouble(Result->Bytes, 1.0);
        return;
    }
    IParamCurve* Curve = ParamCurve;
    const unsigned Elapsed = static_cast<unsigned>(TimeMs) - static_cast<unsigned>(Desc.StartMs);
    if (!bNoPeriodWrap && Curve != nullptr)
    {
        const int Period = PcurvePeriod;
        if (Period > 0)
        {
            int SignedElapsed;
            std::memcpy(&SignedElapsed, &Elapsed, sizeof(SignedElapsed));
            Curve->WrapEvaluateToExtended(SignedElapsed, Period, Result);
            return;
        }
    }
    const double Quotient = ThornSceneInterpolation::WrapIntermediate(static_cast<double>(Elapsed) / Duration);
    if (!bNoPeriodWrap && Curve == nullptr)
    {
        ThornExtendedProgress::StoreDouble(Result->Bytes, Quotient);
        return;
    }
    const float Ratio = static_cast<float>(Quotient);
    if (Curve != nullptr) Curve->EvaluateToExtended(&Ratio, Result);
    else ThornExtendedProgress::StoreDouble(Result->Bytes, static_cast<double>(Ratio));
}

const char* FThornEvent::TypeName() const
{
    // Retail Family-B event vtables (0x06f5b1cc .. 0x06f5b40c, 0x20 stride)
    // carry a slot-7 getter that is just `MOV EAX,"NAME"; RET`:
    //   LAB_06f4a3d0 "MOTION"            LAB_06f4a400 "IK"
    //   LAB_06f4a430 "SOUND"             LAB_06f4a460 "USER_EVENT"
    //   LAB_06f4a4a0 "SET MONITOR"       LAB_06f4a4d0 "PATH MOTION"
    //   LAB_06f4a500 "LIGHT PROP ANIM"   LAB_06f4a530 "CAMERA PROP ANIM"
    //   LAB_06f4a590 "SPATIAL PROP ANIM" LAB_06f4a560 "AUDIO PROP ANIM"
    //   LAB_06f4a5d0 "ATTACH"            LAB_06f4a600 "CONNECT"
    //   LAB_06f4a630 "START SUB-SCENE"   LAB_06f4a660 "START PSYS"
    //   LAB_06f4a690 "PSYS PROP ANIM"    LAB_06f4a6c0 "FOG PROP ANIM"
    //   LAB_06f4a6f0 "REVERB PROP ANIM"  LAB_06f4a720 "SPATIAL PROP ANIM"
    //   LAB_06f4a750 "SUBTITLE"
    // Type 18 (floor-height) reuses the "SPATIAL PROP ANIM" literal in retail.
    switch (NativeEventType != 0 ? NativeEventType : Type)
    {
    case kThornTypeUserEvent:         return "USER_EVENT";
    case kThornTypeSetCamera:         return "SET MONITOR";
    case kThornTypeStartSound:        return "SOUND";
    case kThornTypeLightPropAnim:     return "LIGHT PROP ANIM";
    case kThornTypeCameraPropAnim:    return "CAMERA PROP ANIM";
    case kThornTypePathAnim:          return "PATH MOTION";
    case kThornTypeSpatialPropAnim:   return "SPATIAL PROP ANIM";
    case kThornTypeAttachEntity:      return "ATTACH";
    case kThornTypeConnectHardpoints: return "CONNECT";
    case kThornTypeStartMotion:       return "MOTION";
    case kThornTypeStartIk:           return "IK";
    case kThornTypeStartSubScene:     return "START SUB-SCENE";
    case kThornTypeStartPsys:         return "START PSYS";
    case kThornTypePsysPropAnim:      return "PSYS PROP ANIM";
    case kThornTypeAudioPropAnim:     return "AUDIO PROP ANIM";
    case kThornTypeFogPropAnim:       return "FOG PROP ANIM";
    case kThornTypeReverbPropAnim:    return "REVERB PROP ANIM";
    case kThornTypeFloorHeightAnim:   return "SPATIAL PROP ANIM";
    case kThornTypeSubtitle:          return "SUBTITLE";
    default:
        // Unreachable in retail: every concrete event vtable has a getter.
        return "";
    }
}

unsigned char FThornEvent::StartSetCamera(FThornScene* Scene)
{
    if (Scene == nullptr)
        return 0;
    FThornEntity* DestEnt = Scene->EntityAt(PrimaryIndex);
    FThornEntity* SourceEnt = nullptr;
    if (SecondaryIndex >= 0)
        SourceEnt = Scene->EntityAt(SecondaryIndex);
    // LAB_06f44ec0: either FUN_06f4aa40 lookup may return the scene sentinel;
    // the modern index lookup represents that failure as nullptr.
    if (DestEnt == nullptr || SourceEnt == nullptr)
        return 0;

    if (AddMonitorLink(SourceEnt, DestEnt) != 0)
    {
        UnbindMonitorChildren(DestEnt);
        AddMonitorLink(DestEnt, SourceEnt);
    }

    float Props[4];
    CaptureCameraProps(SourceEnt, Props);
    ApplyCameraProps(DestEnt, Props);

    // The retail tail calls the source vtable getters (+0x14/+0x18), not a
    // cached descriptor copy.  GeoControl is the modern live-pose owner.
    float Position[3];
    float Orient[9];
    GetLivePose(SourceEnt, Position, Orient);
    DestEnt->InnerSetPos(Position);
    DestEnt->InnerSetOrient(Orient);
    return 0;
}

unsigned char FThornEvent::StartCameraPropAnim(FThornScene* Scene)
{
    // 460f8..46120: one primary lookup and only concrete type CAMERA (3).
    // Missing/non-camera receivers leave the prior captured four words intact.
    FThornEntity* Primary = Scene->EntityAt(PrimaryIndex);
    if (Primary != nullptr && Primary->Snap.Type == kThornEntityCamera)
        CaptureCameraProps(Primary, CamCapture);
    // End-time selection follows every capture/lookup branch; return is 1.
    ApplyEndTime(this);
    return 1;
}

unsigned char FThornEvent::StartSpatialPropAnim(FThornScene* Scene)
{
    // LAB_06f46910: each getter selects the current payload of one stable entry.
    AxisRotApplied = 0.0f;
    FThornEntity** PrimaryEntry = Scene->FindEntityEntry(PrimaryIndex);
    if (PrimaryEntry != nullptr)
    {
        FThornEntity* Primary = *PrimaryEntry;
        GetRetailRelativePosition(Primary, Primary->Snap.Pos);
        CopyPoseWordsForward(CapturePos, Primary->Snap.Pos, 3);
        Primary = *PrimaryEntry;
        GetRetailRelativeOrientation(Primary, Primary->Snap.Orient);
        CopyPoseWordsForward(CaptureOrient, Primary->Snap.Orient, 9);
        Primary = *PrimaryEntry;
        GetRetailRelativeOrientation(Primary, Primary->Snap.Orient);
        // 46983 retains the third getter's returned storage across the factory.
        const float* QuaternionMatrix = Primary->Snap.Orient;
        I3DMathEngine* Math = AcquireSceneEventMathEngine();
        SpatialMatrixToQuaternion(Math, CaptureQuat, QuaternionMatrix);
    }
    // Re-read the secondary ID after all primary getter and math callbacks.
    if (SecondaryIndex != -1)
    {
        FThornEntity** SecondaryEntry = Scene->FindEntityEntry(SecondaryIndex);
        if (SecondaryEntry != nullptr)
        {
            FThornEntity* Secondary = *SecondaryEntry;
            GetRetailRelativePosition(Secondary, Secondary->Snap.Pos);
            CopyPoseWordsForward(TargetPos, Secondary->Snap.Pos, 3);
        }
    }
    SpatialWrapLast = 0.0f;
    ApplyEndTime(this);
    return 1;
}

unsigned char FThornEvent::StartAttachEntity(FThornScene* Scene)
{
    // LAB_06f478f0: the OR AL,80h makes the apparent flags gate unconditional.
    ApplyEndTime(this);
    FThornEntity** PrimaryEntry = Scene->FindEntityEntry(PrimaryIndex);
    if (PrimaryEntry == nullptr)
        return 1;
    FThornEntity** SecondaryEntry = Scene->FindEntityEntry(SecondaryIndex);
    if (SecondaryEntry == nullptr)
        return 1;

    float Child[12];
    IdentityOrient(Child);
    Child[9] = Child[10] = Child[11] = 0.0f;
    FThornEntity* Secondary = *SecondaryEntry;
    GetRetailRelativePosition(Secondary, Secondary->Snap.Pos);
    CopyPoseWordsForward(Child + 9, Secondary->Snap.Pos, 3);
    Secondary = *SecondaryEntry;
    GetRetailRelativeOrientation(Secondary, Secondary->Snap.Orient);
    CopyPoseWordsForward(Child, Secondary->Snap.Orient, 9);
    if (TargetPart != nullptr && TargetType != 0)
    {
        const int SecondaryType = (*SecondaryEntry)->Snap.Type;
        if (SecondaryType == kThornEntityCompound || SecondaryType == kThornEntityDeformable)
        {
            ICompoundControl* Compound = (*SecondaryEntry)->CompoundControl;
            if (Compound != nullptr)
            {
                Compound->GetHardpointXform(TargetType, TargetPart, Child);
                const FThornCsys* Parent = ParentCsys;
                if (Parent != nullptr)
                    MulXformByInverseCsys(Parent, Child);
            }
        }
    }

    float PrimaryXform[12];
    IdentityOrient(PrimaryXform);
    PrimaryXform[9] = PrimaryXform[10] = PrimaryXform[11] = 0.0f;
    FThornEntity* Primary = *PrimaryEntry;
    GetRetailRelativePosition(Primary, Primary->Snap.Pos);
    CopyPoseWordsForward(PrimaryXform + 9, Primary->Snap.Pos, 3);
    Primary = *PrimaryEntry;
    GetRetailRelativeOrientation(Primary, Primary->Snap.Orient);
    CopyPoseWordsForward(PrimaryXform, Primary->Snap.Orient, 9);

    // 47b60: read the completed child only AFTER cache acquisition. The
    // sole math producer has no callbacks; its output is distinct from both
    // input objects, so raw typed snapshots preserve every operand read.
    float TransformedPosition[3];
    I3DMathEngine* Math = AcquireSceneEventMathEngine();
    FMatrix ChildOrientation;
    FVector ChildPosition;
    FVector ChildInversePosition;
    std::memcpy(&ChildOrientation, Child, sizeof(ChildOrientation));
    std::memcpy(&ChildPosition, Child + 9, sizeof(ChildPosition));
    Math->TransposeTransform(ChildInversePosition, ChildOrientation, ChildPosition);
    std::memcpy(TransformedPosition, &ChildInversePosition, sizeof(TransformedPosition));
    float NegativePosition[3];
    const float* PositionSource = TransformedPosition;
    float* PositionResult = NegativePosition;
    // 47b63..47b84: each Z/Y/X word is rounded immediately after FCHS.
    ThornPortableSplineMath::NegateStored(PositionResult, PositionSource);
    // 47b8c..47bc0/428b0 pass raw DWORDs in transpose order. In contrast to
    // FUN_3b670, this path has no x87 load/store of the nine matrix words.
    float InverseChild[12];
    for (int Row = 0; Row < 3; ++Row)
    {
        for (int Column = 0; Column < 3; ++Column)
        {
            // Raw words preserve NaN payloads and the retail read order.
            unsigned int Word;
            std::memcpy(&Word, Child + Row + Column * 3, sizeof(Word));
            std::memcpy(InverseChild + Row * 3 + Column, &Word, sizeof(Word));
        }
    }
    PackCsysWords(InverseChild, InverseChild, NegativePosition);
    float Result[12];
    Math = AcquireSceneEventMathEngine();
    SceneMathMulTransform(Math, Result, InverseChild, PrimaryXform);
    std::memcpy(AttachXform, Result, sizeof(Result));
    return 1;
}

// LAB_06f48d40: concrete FOG owner, exact signed key selection and raw GET capture.
unsigned char FThornEvent::StartFogPropAnim(FThornScene* Scene)
{
    FThornEntity** PrimaryEntry = Scene->FindEntityEntry(PrimaryIndex);
    if (PrimaryEntry != nullptr && (*PrimaryEntry)->Snap.Type == kThornEntityScene)
    {
        // SCENE slot1 is 4a300: MOV EAX,[ECX+84]; RET, with no callback.
        FThornEntity* Primary = *PrimaryEntry;
        IFogControl* Control = Primary->FogControl;
        if (Control != nullptr)
            Control->GetPacked(reinterpret_cast<unsigned char*>(&Primary->SceneFogPacked[7]));
        // GET may mutate the registry; retail retains the captured entity here.
        std::memcpy(&FogCapturePacked, &Primary->SceneFogPacked[7], sizeof(FogCapturePacked));
    }
    ApplyEndTime(this);
    return 1;
}

// FUN_06f48de0: full150-instruction concrete FOG tick; immutable owner dispatch.
unsigned char FThornEvent::TickFogPropAnim(int TimeMs, FThornScene* Scene)
{
    if (FogMask != 0)
    {
        FThornFog8 Target;
        std::memcpy(&Target, &FogTarget, sizeof(Target));
        FThornEntity** PrimaryEntry = Scene->FindEntityEntry(PrimaryIndex);
        if (PrimaryEntry != nullptr)
        {
            FThornExtendedProgress Progress;
            EvaluateProgressExtended(TimeMs, &Progress, false);
            float U;
            StoreAnimTValue(&Progress, &U);
            if ((*PrimaryEntry)->Snap.Type == kThornEntityScene)
            {
                // FLD U/FCOMP 1; TEST AH,01 + JNZ skips for less/unordered.
                const bool bSnapDiscrete = U >= 1.0f;
                FThornEntity* Primary = *PrimaryEntry;
                FThornSceneFogSnapshot Out;
                std::memcpy(&Out, Primary->SceneFogPacked, sizeof(Out));
                FThornFog8* ResultFog = &Out.Fog;
                const FThornFog8* Capture = &FogCapturePacked;
                if (bSnapDiscrete)
                {
                    const unsigned int SnapMask = FogMask;
                    if ((SnapMask & 1) != 0) ResultFog->On = Target.On;
                    if ((SnapMask & 2) != 0) ResultFog->Table = Target.Table;
                    if ((SnapMask & 8) != 0) ResultFog->Mode = Target.Mode;
                }
                const unsigned int InterpolationMask = FogMask;
                if ((InterpolationMask & 0x10) != 0)
                    FogLerpFloat(&Capture->Start, &Target.Start, &U, &ResultFog->Start);
                if ((InterpolationMask & 0x20) != 0)
                    FogLerpFloat(&Capture->End, &Target.End, &U, &ResultFog->End);
                if ((InterpolationMask & 0x40) != 0)
                    FogLerpFloat(&Capture->Density, &Target.Density, &U, &ResultFog->Density);
                if ((InterpolationMask & 4) != 0)
                {
                    ResultFog->Color[0] = FogLerpInteger(Capture->Color[0], Target.Color[0], &U);
                    ResultFog->Color[1] = FogLerpInteger(Capture->Color[1], Target.Color[1], &U);
                    ResultFog->Color[2] = FogLerpInteger(Capture->Color[2], Target.Color[2], &U);
                }
                // 431d0 reads middle argument zero; third pushed mask is unused.
                Fun06f431d0(Primary, &Out, 0);
                Fun06f43140(Primary, static_cast<unsigned char>(FogMask));
            }
        }
    }
    return RetailStillRunningPredicate(TimeMs, EndTimeMs);
}

unsigned char FThornEvent::StartPsysPropAnim(FThornScene* Scene)
{
    // Full60 LAB_06f489a0: only the primary keyed lookup occurs here.
    FThornEntity* Primary = Scene->EntityAt(PrimaryIndex);
    if (Primary != nullptr && Primary->Snap.Type == kThornEntityPsys)
    {
        // All concrete entity slot1 getters are pure published Type reads.
        // 489ed captures this payload before the foreign GetSparam callback.
        IPSysControl* Control = Primary->PsysControl;
        if (Control != nullptr)
            Control->GetSparam(&Primary->PsysRuntimeSparam);
        std::memcpy(&SparamCapture, &Primary->PsysRuntimeSparam, sizeof(SparamCapture));
    }
    // Read the current canonical flags/duration/start after capture callbacks.
    ApplyEndTime(this);
    return 1;
}

unsigned char FThornEvent::StartReverbPropAnim(FThornScene* Scene)
{
    // 491e0..49265: primary-only lookup; the concrete type leaf has no callback.
    FThornEntity** PrimaryEntry = Scene->FindEntityEntry(PrimaryIndex);
    if (PrimaryEntry != nullptr && (*PrimaryEntry)->Snap.Type == kThornEntityScene)
    {
        IReverbControl* Control = (*PrimaryEntry)->ReverbControl;
        if (Control != nullptr)
            Control->GetReverb(&ReverbCapture[0], &ReverbCapture[1],
                              &ReverbCapture[2], &ReverbCapture[3]);
        else
        {
            const unsigned int Defaults[4] = {0, 0, 0x3f800000, 0x3f800000};
            std::memcpy(ReverbCapture, Defaults, sizeof(ReverbCapture));
        }
    }
    // 49265 reads current flags/start/duration only after GET returns.
    ApplyEndTime(this);
    return 1;
}

unsigned char FThornEvent::TickReverbPropAnim(int TimeMs, FThornScene* Scene)
{
    // 49298 zero mask skips target reads, lookup and the progress callback.
    if (ReverbMask != 0)
    {
        FThornReverb4 Target;
        std::memcpy(&Target, &ReverbTarget, sizeof(Target));
        FThornEntity** PrimaryEntry = Scene->FindEntityEntry(PrimaryIndex);
        if (PrimaryEntry != nullptr)
        {
            FThornExtendedProgress Progress;
            EvaluateProgressExtended(TimeMs, &Progress, true);
            float U;
            StoreAnimTValue(&Progress, &U);
            // 492f8 rereads the saved map entry AFTER the progress callback.
            FThornEntity* Primary = *PrimaryEntry;
            FThornReverb4 Output;
            std::memcpy(&Output, ReverbCapture, sizeof(Output));
            if (Primary->Snap.Type == kThornEntityScene)
            {
                IReverbControl* Control = (*PrimaryEntry)->ReverbControl;
                if (Control != nullptr)
                {
                    // FCOMP 1 / TEST AH,1 / JNZ skips ordered-less and
                    // unordered. Equal/greater alone can replace environment.
                    if (U >= 1.0f && (ReverbMask & 1) != 0)
                        Output.Environment = Target.Environment;
                    const int Mask = ReverbMask;
                    // 49352/4936b/49388 share the existing seven-operation
                    // retained-capture x87 kernel; calls stay outside asm.
                    if ((Mask & 2) != 0)
                        FogLerpFloat(&ReverbCapture[1], &Target.Volume, &U, &Output.Volume);
                    if ((Mask & 4) != 0)
                        FogLerpFloat(&ReverbCapture[2], &Target.Decay, &U, &Output.Decay);
                    if ((Mask & 8) != 0)
                        FogLerpFloat(&ReverbCapture[3], &Target.Damping, &U, &Output.Damping);
                    ThornSetReverbProperties(Control, &Output);
                }
            }
        }
    }
    return RetailStillRunningPredicate(TimeMs, EndTimeMs);
}

unsigned char FThornEvent::StartAudioPropAnim(FThornScene* Scene)
{
    // Full51 LAB_06f47320: primary lookup, pure type6 gate, captured receiver.
    FThornEntity* Primary = Scene->EntityAt(PrimaryIndex);
    if (Primary != nullptr && Primary->Snap.Type == kThornEntitySound)
    {
        float CapturedWords[8];
        CaptureAudioFromEntity(Primary, CapturedWords);
        std::memcpy(&AudioCapture, CapturedWords, sizeof(AudioCapture));
    }
    ApplyEndTime(this);
    return 1;
}

unsigned char FThornEvent::TickAudioPropAnim(int TimeMs, FThornScene* Scene)
{
    // Full186 LAB_06f473a0: snapshot target before either lookup or curve.
    if (AudioMask != 0)
    {
        FThornAudio8 To;
        std::memcpy(&To, &AudioTarget, sizeof(To));
        if (SecondaryIndex != -1)
        {
            FThornEntity* Secondary = Scene->EntityAt(SecondaryIndex);
            if (Secondary != nullptr && Secondary->Snap.Type == kThornEntitySound)
                std::memcpy(&To, Secondary->Audio, sizeof(To)); // no foreign GET
        }
        FThornEntity** PrimaryEntry = Scene->FindEntityEntry(PrimaryIndex);
        if (PrimaryEntry != nullptr)
        {
            FThornExtendedProgress Progress;
            EvaluateProgressExtended(TimeMs, &Progress, false);
            const double ProgressValue = ThornExtendedProgress::LoadDouble(Progress.Bytes);
            // 47438..47448 selects the current payload AFTER the curve callback.
            FThornEntity* Primary = *PrimaryEntry;
            const int Mask = AudioMask;
            FThornAudio8 Out;
            std::memcpy(&Out, Primary->Audio, sizeof(Out));
            if ((Mask & 1) != 0)
                AudioLerpFloat(&AudioCapture.Attenuation, &To.Attenuation, &Out.Attenuation, ProgressValue);
            if ((Mask & 2) != 0)
                AudioLerpFloat(&AudioCapture.Pan, &To.Pan, &Out.Pan, ProgressValue);
            // 4748d..474b9 repeats these two stores, including exceptional FP effects.
            if ((Mask & 1) != 0)
                AudioLerpFloat(&AudioCapture.Attenuation, &To.Attenuation, &Out.Attenuation, ProgressValue);
            if ((Mask & 2) != 0)
                AudioLerpFloat(&AudioCapture.Pan, &To.Pan, &Out.Pan, ProgressValue);
            if ((Mask & 4) != 0)
                AudioLerpFloat(&AudioCapture.Dmin, &To.Dmin, &Out.Dmin, ProgressValue);
            if ((Mask & 8) != 0)
                AudioLerpFloat(&AudioCapture.Dmax, &To.Dmax, &Out.Dmax, ProgressValue);
            if ((Mask & 0x10) != 0)
                Out.Ain = ComputeAudioLerpInteger(AudioCapture.Ain, To.Ain, ProgressValue);
            if ((Mask & 0x20) != 0)
                Out.Aout = ComputeAudioLerpInteger(AudioCapture.Aout, To.Aout, ProgressValue);
            if ((Mask & 0x40) != 0)
                AudioLerpFloat(&AudioCapture.Atout, &To.Atout, &Out.Atout, ProgressValue);
            if ((Mask & 0x80) != 0)
                AudioLerpFloat(&AudioCapture.Rmix, &To.Rmix, &Out.Rmix, ProgressValue);
            float OutWords[8];
            std::memcpy(OutWords, &Out, sizeof(Out));
            ApplyAudioProps(Primary, OutWords);
        }
    }
    return RetailStillRunningPredicate(TimeMs, EndTimeMs);
}

unsigned char FThornEvent::StartFloorHeightAnim(FThornScene* Scene)
{
    // Full79-instruction497b0: primary capture precedes live secondary lookup.
    FThornEntity** PrimaryEntry = Scene->FindEntityEntry(PrimaryIndex);
    if (PrimaryEntry != nullptr && (*PrimaryEntry)->Snap.Type == kThornEntityDeformable)
    {
        FThornEntity* Primary = *PrimaryEntry;
        ICompoundControl* Control = Primary->CompoundControl;
        if (Control != nullptr)
            Control->GetFloorHeight(&FloorCapture);
        else
        {
            // Keep the widened source across both publications; reloading the
            // runtime float would add a denormal exception between the stores.
            const double Height = static_cast<double>(Primary->Snap.FloorHeight);
            Primary->FloorHeight = static_cast<float>(Height);
            FloorCapture = static_cast<float>(Height);
        }
    }
    if (SecondaryIndex != -1)
    {
        FThornEntity* Secondary = Scene->EntityAt(SecondaryIndex);
        if (Secondary != nullptr)
        {
            GetRetailRelativePosition(Secondary, Secondary->Snap.Pos);
            std::memcpy(&FloorTarget, Secondary->Snap.Pos + 1, sizeof(FloorTarget));
        }
    }
    const unsigned int Zero = 0;
    std::memcpy(&FloorWrapLast, &Zero, sizeof(Zero));
    ApplyEndTime(this);
    return 1;
}

unsigned char FThornEvent::Start(FThornScene* Scene, int TimeMs)
{
    if (NativeEventType == kThornTypeStartIk)
    {
        ApplyEndTime(this);
        FThornEntity* Primary = Scene->EntityAt(PrimaryIndex);
        if (Primary == nullptr)
            return 0;
        FThornEntity** SecondaryEntry = Scene->FindEntityEntry(SecondaryIndex);
        if (SecondaryEntry == nullptr || Primary->IkControl == nullptr)
            return 0;
        float Pos[3];
        float Orient[9];
        BuildIkPose(this, Primary, SecondaryEntry, Pos, Orient);
        PackIkDesc();
        return HrToAl(Primary->IkControl->StartIk(&IkDesc.Common, Pos, Orient));
    }
    if (NativeEventType == kThornTypeStartSubScene)
    {
        ApplyEndTime(this);
        FThornEntity* Primary = Scene->EntityAt(PrimaryIndex);
        if (Primary == nullptr || Primary->ScriptControl == nullptr)
            return 0;
        IScriptControl* Control = Primary->ScriptControl;
        const __int64 Converted = static_cast<__int64>(SubSceneStartTime);
        const unsigned int LowWord = static_cast<unsigned int>(Converted);
        int StartValue;
        std::memcpy(&StartValue, &LowWord, sizeof(StartValue));
        return HrToAl(Control->Slot3Start(StartValue));
    }
    if (NativeEventType == kThornTypeStartPsys)
    {
        FThornEntity* Primary = Scene->EntityAt(PrimaryIndex);
        if (Primary == nullptr || Primary->PsysControl == nullptr)
            return 0;
        return HrToAl(Primary->PsysControl->Start());
    }
    if (&Desc == &FloorDesc.Common)
        return StartFloorHeightAnim(Scene);
    if (&Desc == &ReverbDesc.Common)
        return StartReverbPropAnim(Scene);
    if (&Desc == &FogDesc.Common)
        return StartFogPropAnim(Scene);
    if (&Desc == &PsysPropDesc.Common)
        return StartPsysPropAnim(Scene);
    if (&Desc == &AudioPropDesc.Common)
        return StartAudioPropAnim(Scene);
    if (Type < kThornEventTypeMin || Type > kThornEventTypeMax)
    {
        ThornFdump("Start attempted on event with unknown type\n");
        return 0;
    }
    if (Type == kThornTypeUserEvent)
    {
        // 44c64..44c88: end-time publication precedes the primary lookup.
        ApplyEndTime(this);
        FThornEntity* Primary = Scene->EntityAt(PrimaryIndex);
        if (Primary == nullptr || Primary->UserEventControl == nullptr)
            return 0;
        IUserEventControl* Control = Primary->UserEventControl;
        // Retail passes owner+28 directly, without an event virtual getter.
        return HrToAl(Control->Slot3(&UserEventDesc));
    }
    if (Type == kThornTypeCameraPropAnim)
        return StartCameraPropAnim(Scene);
    if (Type == kThornTypeSpatialPropAnim)
        return StartSpatialPropAnim(Scene);
    if (Type == kThornTypeAttachEntity)
        return StartAttachEntity(Scene);
    if (Scene == nullptr)
        return 0;


    FThornEntity* Primary = Scene->EntityAt(PrimaryIndex);
    FThornEntity* Secondary = nullptr;
    if (SecondaryIndex >= 0)
        Secondary = Scene->EntityAt(SecondaryIndex);

    if (Type == kThornTypeSetCamera)
        return StartSetCamera(Scene);

    if (Type == kThornTypePathAnim)
    {
        // LAB_06f45160: the retail callback only derives the percent delta and
        // end time. It does not bind or mutate the event parent coordinate
        // system on this path.
        PercentDelta = StopPercent - StartPercent;
        ApplyEndTime(this);
        return 1;
    }

    if (Type == kThornTypeStartMotion)
    {
        ApplyEndTime(this);
        if (Primary == nullptr || Primary->CompoundControl == nullptr)
            return 0;
        // LAB_06f43cd0: slot 3 with event +0x28, the whole motion descriptor.
        return HrToAl(Primary->CompoundControl->StartMotion(GetDesc()));
    }

    if (Type == kThornTypeStartSound)
    {
        ApplyEndTime(this);
        if (Primary == nullptr || Primary->SoundControl == nullptr)
            return 0;
        return HrToAl(Primary->SoundControl->StartSound(GetDesc(), TimeMs));
    }


    if (Type == kThornTypeLightPropAnim)
    {
        // LAB_06f456e0: GET into entity blob, REP MOVSD 0x24 РІвЂ вЂ™ event+0xE8.
        if (Primary != nullptr && (Primary->Snap.Type == kThornEntityLight ||
                                   Primary->Snap.Type == kThornEntityScene))
        {
            CaptureLiveLightProps(Primary, &LightCapture);
            UnpackEntityLightProps(Primary, &LightCapture);
        }
        ApplyEndTime(this);
        return 1;
    }


    if (Type == kThornTypeSubtitle)
    {
        ISubtitleControl* Sub = SubtitleOf(Primary);
        if (Sub != nullptr)
            Sub->ShowSubtitle(Strid);
        ApplyEndTime(this);
        return 1;
    }

    if (Type == kThornTypeConnectHardpoints)
    {
        ApplyEndTime(this);
        if (Primary == nullptr || Primary->CompoundControl == nullptr)
            return 0;
        return HrToAl(Primary->CompoundControl->ConnectHardpoints(GetDesc()));
    }

    if (Type == kThornTypeStartIk)
    {
        // LAB_06f44140: both lookups + IIKControl; invert after hp; FUN_06f2e440;
        // forward Csys; slot 3. AL=0 on miss.
        ApplyEndTime(this);
        if (Primary == nullptr || Secondary == nullptr || Primary->IkControl == nullptr)
            return 0;
        float Pos[3];
        float Orient[9];
        BuildIkPose(this, Primary, &Secondary, Pos, Orient);
        // 0x06f44496 `ADD EBX,0x28; PUSH EBX`: the event's descriptor, IK fields included.
        return HrToAl(Primary->IkControl->StartIk(GetDesc(), Pos, Orient));
    }

    if (Type == kThornTypeStartPsys)
    {
        // LAB_06f48760 (thorn.dll.txt:76954-76996) is the one retail Start
        // without the end-time prologue (compare LAB_06f48320 at 76375):
        // it goes straight to the FUN_06f4af00 lookup, and EndTimeMs stays
        // whatever ApplyDesc (FUN_06f48700: StartTime + Desc.DurationMs,
        // unconditionally) left there -- so a START_PSYS with
        // USE_SCRIPT_DURATION ends at its own start time in retail rather
        // than running unbounded.  Do not recompute it here.
        if (Primary == nullptr || Primary->PsysControl == nullptr)
            return 0;
        return HrToAl(Primary->PsysControl->Start());
    }

    if (Type == kThornTypeReverbPropAnim)
    {
        // LAB_06f491e0: type 9 GET slot 4; NULL control РІвЂ вЂ™ 0, 0, 1.0, 1.0.
        if (Primary != nullptr && Primary->Snap.Type == kThornEntityScene)
        {
            if (Primary->ReverbControl != nullptr)
            {
                Primary->ReverbControl->GetReverb(&ReverbCapture[0], &ReverbCapture[1],
                                                  &ReverbCapture[2], &ReverbCapture[3]);
            }
            else
            {
                StoreIntBits(&ReverbCapture[0], 0);
                ReverbCapture[1] = 0.0f;
                ReverbCapture[2] = 1.0f;
                ReverbCapture[3] = 1.0f;
            }
        }
        ApplyEndTime(this);
        return 1;
    }

    return 0;
}

void FThornEvent::DispatchSubtitleControl(FThornScene* Scene, bool bShow)
{
    FThornEntity* Primary = Scene->EntityAt(PrimaryIndex);
    ISubtitleControl* Control = SubtitleOf(Primary);
    if (Control != nullptr)
    {
        if (bShow)
            Control->ShowSubtitle(Strid);
        else
            Control->HideSubtitle(Strid);
    }
}

unsigned char FThornEvent::Stop(FThornScene* Scene)
{
    // Concrete virtual slots remain fixed when common Type is changed.
    switch (NativeEventType)
    {
    case kThornTypeStartMotion:
    case kThornTypeStartSound:
    case kThornTypeStartIk:
    case kThornTypeConnectHardpoints:
    case kThornTypeStartSubScene:
    case kThornTypeStartPsys:
    {
        FThornEntity* Primary = Scene->EntityAt(PrimaryIndex);
        if (Primary == nullptr)
            return 0;
        switch (NativeEventType)
        {
        case kThornTypeStartMotion:
            if (Primary->CompoundControl != nullptr)
            {
                ICompoundControl* Control = Primary->CompoundControl;
                PackMotionDesc();
                Control->StopMotion(&MotionDesc.Common);
            }
            break;
        case kThornTypeStartSound:
            if (Primary->SoundControl != nullptr)
                Primary->SoundControl->StopSound(&Desc);
            break;
        case kThornTypeStartIk:
            // Secondary lookup precedes the live control read, even for negative keys.
            if (Scene->EntityAt(SecondaryIndex) != nullptr && Primary->IkControl != nullptr)
            {
                IIKControl* Control = Primary->IkControl;
                PackIkDesc();
                Control->StopIk(&IkDesc.Common);
            }
            break;
        case kThornTypeConnectHardpoints:
            if (Primary->CompoundControl != nullptr)
            {
                ICompoundControl* Control = Primary->CompoundControl;
                PackConnectDesc();
                Control->DisconnectHardpoints(&ConnectDesc.Common);
            }
            break;
        case kThornTypeStartSubScene:
            if (Primary->ScriptControl != nullptr)
                Primary->ScriptControl->Slot5Stop();
            break;
        case kThornTypeStartPsys:
            if (Primary->PsysControl != nullptr)
                Primary->PsysControl->StopEffect();
            break;
        }
        return 0;
    }
    case kThornTypeSubtitle:
        DispatchSubtitleControl(Scene, false);
        return 0;
    }

    // 06f4a6b0: FOG PROP ANIM stops without scene access.
    if (&Desc == &FogDesc.Common)
        return 0;
    // 06f4a6e0: REVERB PROP ANIM stops without scene access.
    if (&Desc == &ReverbDesc.Common)
        return 0;
    // 06f4a520: CAMERA PROP ANIM's stop slot ignores the scene.
    if (&Desc == &CameraDesc.Common)
        return 0;
    // 06f4a4f0: LIGHT PROP ANIM's fixed stop slot returns before lookups.
    if (&Desc == &LightDesc.Common)
        return 0;
    // 06f4a480: the SET MONITOR stop slot does not perform a scene lookup.
    if (&Desc == &SetCameraDesc)
        return 0;
    if (&Desc == &AudioPropDesc.Common)
        return 0;
    if (Type == kThornTypeUserEvent)
    {
        FThornEntity* Primary = Scene->EntityAt(PrimaryIndex);
        if (Primary != nullptr && Primary->UserEventControl != nullptr)
        {
            IUserEventControl* Control = Primary->UserEventControl;
                Control->Slot4(&UserEventDesc);
        }
        return 0;
    }
    if (Scene == nullptr)
        return 0;
    FThornEntity* Primary = Scene->EntityAt(PrimaryIndex);

    if (Type == kThornTypeStartMotion && Primary != nullptr && Primary->CompoundControl != nullptr)
        Primary->CompoundControl->StopMotion(GetDesc());
    else if (Type == kThornTypeStartSound && Primary != nullptr && Primary->SoundControl != nullptr)
        Primary->SoundControl->StopSound(&Desc);
    else if (Type == kThornTypeStartSubScene && Primary != nullptr &&
             Primary->ScriptControl != nullptr)
        Primary->ScriptControl->Slot5Stop();
    else if (Type == kThornTypeStartPsys && Primary != nullptr && Primary->PsysControl != nullptr)
        Primary->PsysControl->StopEffect();
    else if (Type == kThornTypeConnectHardpoints && Primary != nullptr &&
             Primary->CompoundControl != nullptr)
    {
        Primary->CompoundControl->DisconnectHardpoints(GetDesc());
    }
    else if (Type == kThornTypeStartIk && Primary != nullptr && Primary->IkControl != nullptr)
    {
        // LAB_06f44860 (thorn.dll.txt:70358-70397): both PrimaryIndex and
        // SecondaryIndex must resolve in the entity tree before IIKControl
        // slot 4 is issued; a missing secondary silently skips the stop.
        if (SecondaryIndex >= 0 && Scene->EntityAt(SecondaryIndex) != nullptr)
            Primary->IkControl->StopIk(GetDesc());
    }
    else if (Type == kThornTypeSubtitle)
    {
        ISubtitleControl* Sub = SubtitleOf(Primary);
        if (Sub != nullptr)
            Sub->HideSubtitle(Strid);
    }
    return 0;
}

unsigned char FThornEvent::TickCameraPropAnim(int TimeMs, FThornScene* Scene)
{
    // Mask zero skips all lookup, GET, curve and SET work.
    if (CamMask != 0)
    {
        float TargetValues[4];
        for (int Index = 0; Index < 4; ++Index)
            std::memcpy(&TargetValues[Index], &CamTarget[Index], sizeof(float));
        // 461f4 tests only -1; all other signed IDs go through the lookup.
        if (SecondaryIndex != -1)
        {
            FThornEntity* Secondary = Scene->EntityAt(SecondaryIndex);
            if (Secondary != nullptr && Secondary->Snap.Type == kThornEntityCamera)
                CaptureCameraProps(Secondary, TargetValues);
        }
        // Primary lookup follows secondary GET callbacks and reads the current ID.
        FThornEntity** PrimaryEntry = Scene->FindEntityEntry(PrimaryIndex);
        if (PrimaryEntry != nullptr)
        {
            FThornExtendedProgress Progress;
            // Camera's fixed slot6 uses the period-aware progress accessor even
            // when an earlier callback has changed the mirrored event type.
            EvaluateProgressExtended(TimeMs, &Progress, false);
            // 462d6 retains the selected node across slot6; 462ed reads
            // its live payload AFTER that callback, not a fresh ID lookup.
            FThornEntity* Primary = *PrimaryEntry;
            float ResultValues[4];
            // 462ed..46312 uses the primary runtime cache AFTER the curve callback;
            // there is no primary GET call on this tick path.
            for (int Index = 0; Index < 4; ++Index)
                std::memcpy(&ResultValues[Index], &Primary->CameraRuntimeProps[Index], sizeof(float));
            InterpolateCameraProps(ResultValues, TargetValues, CamCapture, &Progress, CamMask);
            ApplyCameraProps(Primary, ResultValues);
        }
    }
    return RetailStillRunningPredicate(TimeMs, EndTimeMs);
}

unsigned char FThornEvent::TickPsysPropAnim(int TimeMs, FThornScene* Scene)
{
    // LAB_06f48a40: secondary runtime is captured before the primary lookup
    // and before the curve callback. No secondary GetSparam call occurs here.
    if (PsysMask != 0)
    {
        float Target;
        std::memcpy(&Target, &SparamTarget, sizeof(Target));
        if (SecondaryIndex != -1)
        {
            FThornEntity* Secondary = Scene->EntityAt(SecondaryIndex);
            if (Secondary != nullptr && Secondary->Snap.Type == kThornEntityPsys)
                std::memcpy(&Target, &Secondary->PsysRuntimeSparam, sizeof(Target));
        }
        FThornEntity** PrimaryEntry = Scene->FindEntityEntry(PrimaryIndex);
        if (PrimaryEntry != nullptr)
        {
            FThornExtendedProgress Progress;
            EvaluateProgressExtended(TimeMs, &Progress, false);
            float Scale;
            StoreAnimTValue(&Progress, &Scale);
            // The primary type and live cache are consumed after the curve call.
            if ((*PrimaryEntry)->Snap.Type == kThornEntityPsys)
            {
                // 48ae3/48af0 reload the selected node payload after the
                // curve and the concrete descriptor-type getter, respectively.
                FThornEntity* Primary = *PrimaryEntry;
                float Result;
                std::memcpy(&Result, &Primary->PsysRuntimeSparam, sizeof(Result));
                if ((PsysMask & 1) != 0)
                {
                    const float* Capture = &SparamCapture;
                    ThornSceneInterpolation::Lerp(Capture, &Target, &Scale, &Result);
                }
                ApplyPsysSparam(Primary, &Result);
            }
        }
    }
    return RetailStillRunningPredicate(TimeMs, EndTimeMs);
}

unsigned char FThornEvent::TickLightPropAnim(int TimeMs, FThornScene* Scene)
{
    // 45890: a zero initial mask skips all registry and curve callbacks.
    if (LightMask == 0)
        return RetailStillRunningPredicate(TimeMs, EndTimeMs);

    FThornLightProps Target;
    std::memcpy(&Target, &LightTarget, sizeof(Target));
    if (SecondaryIndex != -1)
    {
        FThornEntity** SecondaryEntry = Scene->FindEntityEntry(SecondaryIndex);
        if (SecondaryEntry != nullptr)
        {
            if ((*SecondaryEntry)->Snap.Type == kThornEntityLight)
            {
                FThornEntity* Secondary = *SecondaryEntry;
                FThornLightProps& Live = Secondary->LightEntityDescriptor;
                if (Secondary->D3dLightControl != nullptr)
                {
                    // Each later call rereads the facet after the previous callback.
                    Secondary->D3dLightControl->GetLight(&Live.Type);
                    Secondary->D3dLightControl->GetOn(&Live.On);
                    Secondary->D3dLightControl->GetDirection(Live.Direction);
                }
                else if (Secondary->LightControl != nullptr)
                {
                    Secondary->LightControl->GetColor(reinterpret_cast<float*>(Live.Color));
                    Secondary->LightControl->GetDirection(Live.Direction);
                    Secondary->LightControl->GetRange(&Live.RangeTemp);
                    Secondary->LightControl->GetCutoff(&Live.CutoffTemp);
                    Secondary->LightControl->GetSlot14(reinterpret_cast<float*>(&Live.Infinite));
                    Secondary->LightControl->GetSlot13(reinterpret_cast<float*>(&Live.On));
                    ConvertCompactLightRetail(reinterpret_cast<const unsigned char*>(&Live),
                                              reinterpret_cast<unsigned char*>(&Live.Type));
                }
                std::memcpy(&Target, &Live, sizeof(Target));
            }
            else if ((*SecondaryEntry)->Snap.Type == kThornEntityScene)
            {
                FThornEntity* Secondary = *SecondaryEntry;
                if (Secondary->LightControl != nullptr)
                    Secondary->LightControl->GetColor(reinterpret_cast<float*>(Secondary->SceneFogPacked));
                // Retail keeps color 2 in EBX while color 0/1 replace the target snapshot.
                // All three values are consumed by the later color interpolation.
                std::memcpy(Target.Color, Secondary->SceneFogPacked, sizeof(Target.Color));
            }
        }
    }

    FThornEntity** PrimaryEntry = Scene->FindEntityEntry(PrimaryIndex);
    if (PrimaryEntry != nullptr)
    {
        FThornExtendedProgress Progress;
        EvaluateProgressExtended(TimeMs, &Progress, false);
        float U;
        StoreAnimTValue(&Progress, &U);

        FThornLightProps Output = {};
        if ((*PrimaryEntry)->Snap.Type == kThornEntityLight)
            std::memcpy(&Output, &(*PrimaryEntry)->LightEntityDescriptor, sizeof(Output));
        else if ((*PrimaryEntry)->Snap.Type == kThornEntityScene)
        {
            // Only the first three words of the scene baseline reach its output sink.
            // The unused private tail is initialized without reproducing stack garbage.
            std::memcpy(Output.Color, (*PrimaryEntry)->SceneFogPacked, sizeof(Output.Color));
        }
        EvalLightProps(&Output, &LightCapture, &Target, &LightMask, &U);
        if ((*PrimaryEntry)->Snap.Type == kThornEntityLight)
            ApplyEntityLightRuntimeProps(*PrimaryEntry, &Output);
        else if ((*PrimaryEntry)->Snap.Type == kThornEntityScene)
        {
            unsigned int SceneSnapshot[15];
            std::memcpy(SceneSnapshot, (*PrimaryEntry)->SceneFogPacked, sizeof(SceneSnapshot));
            std::memcpy(SceneSnapshot, Output.Color, sizeof(Output.Color));
            // 45e96/45e9f use -1 for both remaining arguments; the third is unused.
            Fun06f431d0(*PrimaryEntry, SceneSnapshot, 0xff);
        }
    }
    return RetailStillRunningPredicate(TimeMs, EndTimeMs);
}

unsigned char FThornEvent::TickSpatialPropAnim(int TimeMs, FThornScene* Scene)
{
    // LAB_06f46a40: zero mask skips both slot6 and registry access. The
    // concrete SPATIAL slot6 is 47110, independent of mutable descriptor Type.
    if (SpatialMask == 0)
        return RetailStillRunningPredicate(TimeMs, EndTimeMs);
    FThornExtendedProgress Progress;
    EvaluateProgressExtended(TimeMs, &Progress, false);
    float U;
    float Scale;
    SpatialWrapScale(&SpatialWrapLast, &Progress, &U, &Scale);
    FThornEntity** PrimaryEntry = Scene->FindEntityEntry(PrimaryIndex);
    if (PrimaryEntry != nullptr)
    {
        // 46b19 writes the descriptor position, copied into +dc before
        // the orientation getter and direct owner-output math call.
        FThornEntity* Primary = *PrimaryEntry;
        GetRetailRelativePosition(Primary, Primary->Snap.Pos);
        std::memcpy(CapturePos, Primary->Snap.Pos, sizeof(CapturePos));
        Primary = *PrimaryEntry;
        GetRetailRelativeOrientation(Primary, Primary->Snap.Orient);
        I3DMathEngine* Math = AcquireSceneEventMathEngine();
        SpatialMatrixToQuaternion(Math, CaptureQuat, Primary->Snap.Orient);

        // 46b61 rereads the index after the primary math callback.
        FThornEntity** SecondaryEntry = SecondaryIndex != -1 ? Scene->FindEntityEntry(SecondaryIndex) : nullptr;
        if (SecondaryEntry != nullptr)
        {
            FThornEntity* Secondary = *SecondaryEntry;
            GetRetailRelativePosition(Secondary, Secondary->Snap.Pos);
            std::memcpy(TargetPos, Secondary->Snap.Pos, sizeof(TargetPos));
            Secondary = *SecondaryEntry;
            GetRetailRelativeOrientation(Secondary, Secondary->Snap.Orient);
            Math = AcquireSceneEventMathEngine();
            SpatialMatrixToQuaternion(Math, TargetQuat, Secondary->Snap.Orient);
            if (TargetPart != nullptr && TargetType != 0)
            {
                const int SecondaryType = (*SecondaryEntry)->Snap.Type;
                // 46bfc rereads the name after the type getter.
                const char* PartName = TargetPart;
                if (PartName != nullptr &&
                    (SecondaryType == kThornEntityCompound || SecondaryType == kThornEntityDeformable))
                {
                    ICompoundControl* Compound = (*SecondaryEntry)->CompoundControl;
                    if (Compound != nullptr)
                    {
                        float HpXform[12];
                        InitializeEventFrameWords(HpXform);
                        std::memset(HpXform + 9, 0, sizeof(float) * 3);
                        Compound->GetHardpointXform(TargetType, PartName, HpXform);
                        const FThornCsys* Parent = ParentCsys;
                        if (Parent != nullptr)
                            MulXformByInverseCsys(Parent, HpXform);
                        // 46d23 copies all12 words and requats even with
                        // Parent==NULL. Foreign GENRESULT is ignored.
                        std::memcpy(TargetOrient, HpXform, sizeof(TargetOrient));
                        std::memcpy(TargetPos, HpXform + 9, sizeof(TargetPos));
                        Math = AcquireSceneEventMathEngine();
                        SpatialMatrixToQuaternion(Math, TargetQuat, TargetOrient);
                    }
                }
            }
        }

        if ((SpatialMask & kThornSpatialBitPos) != 0)
        {
            float Pos[3];
            LerpSpatialPosition(Pos, CapturePos, TargetPos, &Scale);
            (*PrimaryEntry)->InnerSetPos(Pos);
        }

        // 46dce and46e63 are independent tests. Ordinary orientation
        // publication runs before axis rotation when both bits are set.
        if ((SpatialMask & kThornSpatialBitOrient) != 0)
        {
            float Quat[4];
            float Orient[9];
            NlerpQuat4(Quat, CaptureQuat, TargetQuat, &Scale);
            Quat4ToMat9(Orient, Quat);
            (*PrimaryEntry)->InnerSetOrient(Orient);
        }
        if ((SpatialMask & kThornSpatialBitAxisRot) != 0)
        {
            float Orient[9];
            const int AxisId = AxisRotAxis;
            float DeltaDeg;
            AccumulateAxisDelta(&U, &AxisRotAngle, &AxisRotApplied, &DeltaDeg);
            ApplyAxisRotFpu(Orient, CaptureQuat, AxisId, &DeltaDeg);
            (*PrimaryEntry)->InnerSetOrient(Orient);
        }
    }
    return RetailStillRunningPredicate(TimeMs, EndTimeMs);
}

unsigned char FThornEvent::TickAttachEntity(int TimeMs, FThornScene* Scene)
{
    // LAB_06f47c30: no event slot6/curve call on this concrete tick.
    FThornEntity** PrimaryEntry = Scene->FindEntityEntry(PrimaryIndex);
    if (PrimaryEntry == nullptr)
        return RetailStillRunningPredicate(TimeMs, EndTimeMs);
    FThornEntity** SecondaryEntry = Scene->FindEntityEntry(SecondaryIndex);
    if (SecondaryEntry == nullptr)
        return RetailStillRunningPredicate(TimeMs, EndTimeMs);

    float Child[12];
    IdentityOrient(Child);
    Child[9] = Child[10] = Child[11] = 0.0f;
    FThornEntity* Secondary = *SecondaryEntry;
    GetRetailRelativePosition(Secondary, Secondary->Snap.Pos);
    Copy3(Child + 9, Secondary->Snap.Pos);
    Secondary = *SecondaryEntry;
    GetRetailRelativeOrientation(Secondary, Secondary->Snap.Orient);
    Copy9(Child, Secondary->Snap.Orient);
    if (TargetPart != nullptr && TargetType != 0)
    {
        const int SecondaryType = (*SecondaryEntry)->Snap.Type;
        if (SecondaryType == kThornEntityCompound || SecondaryType == kThornEntityDeformable)
        {
            ICompoundControl* Compound = (*SecondaryEntry)->CompoundControl;
            if (Compound != nullptr)
            {
                // Unlike SPATIAL there is no second part-name null test after
                // the type getter. The live arguments feed the captured control.
                Compound->GetHardpointXform(TargetType, TargetPart, Child);
                const FThornCsys* Parent = ParentCsys;
                if (Parent != nullptr)
                    MulXformByInverseCsys(Parent, Child);
            }
        }
    }
    if ((Desc.Flags & kThornFlagParentChild) != 0)
    {
        float Result[12];
        SceneMathMulTransform(AcquireSceneEventMathEngine(), Result, Child, AttachXform);
        if ((Desc.Flags & kThornFlagPosition) != 0)
            (*PrimaryEntry)->InnerSetPos(Result + 9);
        if ((Desc.Flags & kThornFlagOrientation) != 0)
            (*PrimaryEntry)->InnerSetOrient(Result);
    }
    else
    {
        if ((Desc.Flags & kThornFlagPosition) != 0)
        {
            float Offset[3];
            Copy3(Offset, PathOffset);
            if ((Desc.Flags & kThornFlagEntityRelative) != 0)
            {
                float Rotated[3];
                SceneMathRotate(AcquireSceneEventMathEngine(), Rotated, Child, Offset);
                Copy3(Offset, Rotated);
            }
            float Position[3];
            AddAttachOffset(Position, Child + 9, Offset, true);
            (*PrimaryEntry)->InnerSetPos(Position);
        }
        if ((Desc.Flags & kThornFlagOrientation) != 0)
        {
            if ((Desc.Flags & kThornFlagOrientRelative) != 0)
            {
                float Result[12];
                SceneMathMulTransform(AcquireSceneEventMathEngine(), Result, Child, AttachXform);
                (*PrimaryEntry)->InnerSetOrient(Result);
            }
            else
                (*PrimaryEntry)->InnerSetOrient(Child);
        }
        else if ((Desc.Flags & kThornFlagLookAt) != 0)
        {
            float Offset[3];
            Copy3(Offset, PathOffset);
            if ((Desc.Flags & kThornFlagEntityRelative) != 0)
            {
                float Rotated[3];
                SceneMathRotate(AcquireSceneEventMathEngine(), Rotated, Child, Offset);
                Copy3(Offset, Rotated);
            }
            float Target[3];
            AddAttachOffset(Target, Child + 9, Offset, false);
            // 47f97 calls the entity relative position getter, including its
            // parent inverse. Keep that selected receiver's returned buffer.
            FThornEntity* Primary = *PrimaryEntry;
            GetRetailRelativePosition(Primary, Primary->Snap.Pos);
            float Direction[3];
            if (BuildAttachLookDirection(Direction, Target, Primary->Snap.Pos))
            {
                float Orientation[9];
                BuildAxisFrame(Orientation, Direction, static_cast<unsigned>(LookAtFrontAxis),
                               static_cast<unsigned>(LookAtUpAxis), SceneFrontAxis, SceneUpAxis);
                (*PrimaryEntry)->InnerSetOrient(Orientation);
            }
        }
    }
    return RetailStillRunningPredicate(TimeMs, EndTimeMs);
}

// LAB_06f451a0, all 260 instructions: secondary payload is captured before
// progress; primary selected-entry payload is reread after each callback.
unsigned char FThornEvent::TickPathAnim(int TimeMs, FThornScene* Scene)
{
    FThornEntity** PrimaryEntry = Scene->FindEntityEntry(PrimaryIndex);
    if (PrimaryEntry == nullptr)
        return RetailStillRunningPredicate(TimeMs, EndTimeMs);
    FThornEntity** SecondaryEntry = Scene->FindEntityEntry(SecondaryIndex);
    if (SecondaryEntry == nullptr)
        return RetailStillRunningPredicate(TimeMs, EndTimeMs);
    FThornEntity* Secondary = *SecondaryEntry;
    if (Secondary->MotionPath == nullptr)
        return RetailStillRunningPredicate(TimeMs, EndTimeMs);

    // Concrete PATH slot 6 is 454f0. Its extended return is stored once.
    const float Percent = EvaluateControlBackedCurve(PercentDelta, TimeMs);
    float Position[3];
    float Orientation[9];
    FThornEntity* Primary = *PrimaryEntry;
    GetRetailRelativePosition(Primary, Primary->Snap.Pos);
    Copy3(Position, Primary->Snap.Pos);
    Primary = *PrimaryEntry;
    GetRetailRelativeOrientation(Primary, Primary->Snap.Orient);
    Copy9(Orientation, Primary->Snap.Orient);

    float PathVector[3];
    if ((Desc.Flags & kThornFlagPosition) != 0 &&
        Secondary->MotionPath->Slot4Evaluate(Percent, PathVector) == GR_OK)
    {
        float PathXform[12];
        IdentityOrient(PathXform);
        PathXform[9] = 0.0f;
        PathXform[10] = 0.0f;
        PathXform[11] = 0.0f;
        GetRetailRelativeOrientation(Secondary, Secondary->Snap.Orient);
        Copy9(PathXform, Secondary->Snap.Orient);
        GetRetailRelativePosition(Secondary, Secondary->Snap.Pos);
        Copy3(PathXform + 9, Secondary->Snap.Pos);
        float OffsetPosition[3];
        // 452b9..452ef: live offsets read AFTER both getters, Z/Y/X stores.
        AddAttachOffset(OffsetPosition, PathVector, PathOffset, true);
        float WorldPosition[3];
        I3DMathEngine* Math = AcquireSceneEventMathEngine(); // 452f7
        SceneMathTransform(Math, WorldPosition, PathXform, OffsetPosition);
        Copy3(Position, WorldPosition);
    }
    if ((Desc.Flags & kThornFlagOrientation) != 0)
    {
        if ((Desc.Flags & kThornFlagLookAt) != 0)
        {
            if (Secondary->MotionPath->Slot6LookAt(Percent, PathVector) == GR_OK)
            {
                float PathXform[12];
                IdentityOrient(PathXform);
                PathXform[9] = 0.0f;
                PathXform[10] = 0.0f;
                PathXform[11] = 0.0f;
                GetRetailRelativeOrientation(Secondary, Secondary->Snap.Orient);
                Copy9(PathXform, Secondary->Snap.Orient);
                float Rotated[3];
                I3DMathEngine* Math = AcquireSceneEventMathEngine(); // 453ae
                SceneMathRotate(Math, Rotated, PathXform, PathVector);
                Copy3(PathVector, Rotated);
                BuildAxisFrame(Orientation, PathVector,
                               static_cast<unsigned>(LookAtFrontAxis),
                               static_cast<unsigned>(LookAtUpAxis), SceneFrontAxis, SceneUpAxis);
            }
        }
        else
        {
            float QuaternionWords[4];
            if (Secondary->MotionPath->Slot5(Percent, QuaternionWords) == GR_OK)
            {
                FQuaternion Quaternion;
                std::memcpy(&Quaternion, QuaternionWords, sizeof(Quaternion));
                FMatrix PathMatrix;
                I3DMathEngine* Math = AcquireSceneEventMathEngine(); // 45432
                Math->QuaternionToMatrix(PathMatrix, Quaternion);
                GetRetailRelativeOrientation(Secondary, Secondary->Snap.Orient);
                // Preserve the getter's original storage through the next acquisition.
                const float* SecondaryMatrix = Secondary->Snap.Orient;
                Math = AcquireSceneEventMathEngine(); // 45462
                float Combined[9];
                SceneMathMulMatrix(Math, Combined, SecondaryMatrix, &PathMatrix.D[0][0]);
                Copy9(Orientation, Combined);
            }
        }
    }
    (*PrimaryEntry)->InnerSetOrient(Orientation);
    (*PrimaryEntry)->InnerSetPos(Position);
    return RetailStillRunningPredicate(TimeMs, EndTimeMs);
}

unsigned char FThornEvent::TickFloorHeightAnim(int TimeMs, FThornScene* Scene)
{
    // The concrete FLOOR slot6 is fixed even if callbacks mutate common Type.
    FThornExtendedProgress Progress;
    EvaluateProgressExtended(TimeMs, &Progress, false);
    float Scale;
    FloorWrapScale(this, &Progress, &Scale);
    FThornEntity* Primary = Scene->EntityAt(PrimaryIndex);
    if (Primary != nullptr)
    {
        ICompoundControl* CaptureControl = Primary->CompoundControl;
        if (CaptureControl != nullptr)
            CaptureControl->GetFloorHeight(&FloorCapture);
        else
            std::memcpy(&FloorCapture, &Primary->FloorHeight, sizeof(FloorCapture));
        // 49962 reads the live ID only after the primary GET callback.
        if (SecondaryIndex != -1)
        {
            FThornEntity** SecondaryEntry = Scene->FindEntityEntry(SecondaryIndex);
            if (SecondaryEntry != nullptr)
            {
                FThornEntity* Secondary = *SecondaryEntry;
                GetRetailRelativePosition(Secondary, Secondary->Snap.Pos);
                std::memcpy(&FloorTarget, Secondary->Snap.Pos + 1, sizeof(FloorTarget));
                if (TargetPart != nullptr && TargetType != 0)
                {
                    const int SecondaryType = (*SecondaryEntry)->Snap.Type;
                    if (TargetPart != nullptr &&
                        (SecondaryType == kThornEntityCompound || SecondaryType == kThornEntityDeformable))
                    {
                        ICompoundControl* HardpointControl = (*SecondaryEntry)->CompoundControl;
                        if (HardpointControl != nullptr)
                        {
                            float Hardpoint[12];
                            IdentityOrient(Hardpoint);
                            std::memset(Hardpoint + 9, 0, 3 * sizeof(float));
                            HardpointControl->GetHardpointXform(TargetType, TargetPart, Hardpoint);
                            // Keep the same parent through both math/cache callbacks.
                            const FThornCsys* Parent = ParentCsys;
                            if (Parent != nullptr)
                                TransformFloorHardpoint(Parent, Hardpoint);
                            std::memcpy(&FloorTarget, Hardpoint + 10, sizeof(FloorTarget));
                        }
                    }
                }
            }
        }
        // Retail retains the originally selected primary through every callback.
        ICompoundControl* OutputControl = Primary->CompoundControl;
        if (OutputControl != nullptr)
        {
            float Height;
            InterpolateFloorHeight(&FloorCapture, &FloorTarget, &Scale, &Height);
            ThornSetFloorHeight(OutputControl, &Height);
            std::memcpy(&Primary->FloorHeight, &Height, sizeof(Height));
        }
    }
    return RetailStillRunningPredicate(TimeMs, EndTimeMs);
}

unsigned char FThornEvent::StillRunning(int TimeMs, FThornScene* Scene)
{
    const int EventType = NativeEventType != 0 ? NativeEventType : Type;
    if (EventType == kThornTypeLightPropAnim)
        return TickLightPropAnim(TimeMs, Scene);
    if (EventType == kThornTypeStartIk)
    {
        FThornEntity* Primary = Scene->EntityAt(PrimaryIndex);
        if (Primary != nullptr)
        {
            FThornEntity** SecondaryEntry = Scene->FindEntityEntry(SecondaryIndex);
            if (SecondaryEntry != nullptr && Primary->IkControl != nullptr)
            {
                float Pos[3];
                float Orient[9];
                BuildIkPose(this, Primary, SecondaryEntry, Pos, Orient);
                PackIkDesc();
                Primary->IkControl->UpdateIk(&IkDesc.Common, Pos, Orient);
            }
        }
        return RetailStillRunningPredicate(TimeMs, EndTimeMs);
    }
    // These concrete tick slots are pure end-time predicates (43d50 etc.).
    // They perform neither entity lookup nor parameter-curve callbacks.
    if (EventType == kThornTypeStartMotion || EventType == kThornTypeStartSound ||
        EventType == kThornTypeConnectHardpoints || EventType == kThornTypeStartSubScene ||
        EventType == kThornTypeStartPsys || EventType == kThornTypeSubtitle)
        return RetailStillRunningPredicate(TimeMs, EndTimeMs);
    // 06f44d30: USER_EVENT's tick only compares the clock and end time.
    if (bUserDescriptorOwner)
        return RetailStillRunningPredicate(TimeMs, EndTimeMs);
    // 06f44fb0: this one-shot event has a constant-false tick slot.
    if (&Desc == &SetCameraDesc)
        return 0;
    if (bFloorEventOwner)
        return TickFloorHeightAnim(TimeMs, Scene);
    if (&Desc == &ReverbDesc.Common)
        return TickReverbPropAnim(TimeMs, Scene);
    if (&Desc == &FogDesc.Common)
        return TickFogPropAnim(TimeMs, Scene);
    if (&Desc == &PsysPropDesc.Common)
        return TickPsysPropAnim(TimeMs, Scene);
    if (&Desc == &AudioPropDesc.Common)
        return TickAudioPropAnim(TimeMs, Scene);
    if (Type == kThornTypeCameraPropAnim)
        return TickCameraPropAnim(TimeMs, Scene);
    if (Type == kThornTypePsysPropAnim)
        return TickPsysPropAnim(TimeMs, Scene);
    if (Type == kThornTypeSpatialPropAnim)
        return TickSpatialPropAnim(TimeMs, Scene);
    if (Type == kThornTypeAttachEntity)
        return TickAttachEntity(TimeMs, Scene);
    if (Type == kThornTypePathAnim)
        return TickPathAnim(TimeMs, Scene);
    if (Scene != nullptr)
    {
        FThornEntity* Primary = Scene->EntityAt(PrimaryIndex);
        FThornEntity* Secondary = SecondaryIndex >= 0 ? Scene->EntityAt(SecondaryIndex) : nullptr;
        // FOG skips the curve callback when its mask is zero.
        const bool bSkipCurve = Type == kThornTypeFogPropAnim &&
            (FogMask == 0 || Primary == nullptr);
        FThornExtendedProgress Progress;
        float U = 0.0f;
        if (!bSkipCurve)
        {
            EvaluateAnimT(this, TimeMs, &Progress);
            U = StoreAnimT(&Progress);
        }

        if (Type == kThornTypeStartIk && Primary != nullptr && Secondary != nullptr &&
                 Primary->IkControl != nullptr)
        {
            // LAB_06f444f0: same invert / look-at / forward Csys as Start; IIKControl slot 5.
            float Pos[3];
            float Orient[9];
            BuildIkPose(this, Primary, &Secondary, Pos, Orient);
            Primary->IkControl->UpdateIk(GetDesc(), Pos, Orient);
        }
        else if (Type == kThornTypeReverbPropAnim && ReverbMask != 0 && Primary != nullptr &&
                 Primary->ReverbControl != nullptr && Primary->Snap.Type == kThornEntityScene)
        {
            // LAB_06f49290: TO = load (no secondary); Out = capture; env snap U>=1; bits 2/4/8 lerp.
            float To[4];
            std::memcpy(To, &ReverbTarget, sizeof(To));
            float Out[4];
            Out[0] = ReverbCapture[0];
            Out[1] = ReverbCapture[1];
            Out[2] = ReverbCapture[2];
            Out[3] = ReverbCapture[3];
            if (U >= 1.0f && (ReverbMask & 0x1) != 0)
                Out[0] = To[0];
            if ((ReverbMask & 0x2) != 0)
                Out[1] = ReverbCapture[1] + (To[1] - ReverbCapture[1]) * U;
            if ((ReverbMask & 0x4) != 0)
                Out[2] = ReverbCapture[2] + (To[2] - ReverbCapture[2]) * U;
            if ((ReverbMask & 0x8) != 0)
                Out[3] = ReverbCapture[3] + (To[3] - ReverbCapture[3]) * U;
            Primary->ReverbControl->SetReverb(BitsAsUInt(&Out[0]), Out[1], Out[2], Out[3]);
        }

    }

    return RetailStillRunningPredicate(TimeMs, EndTimeMs);
}

FThornScene::FThornScene()
    : EntityCount_(0), Master_(nullptr), MasterCount_(0),
      MasterCap_(0), Pending_(nullptr), PendingCount_(0), PendingCap_(0), PendingBegin_(0),
      Active_(nullptr), ActiveCount_(0), ActiveCap_(0), StopTimes_(std::in_place), FrameEntities_(std::in_place),
      DurationMs_(0), ClockMs_(0), ClockAfterDt_(0), PlaybackState_(0), bLoop_(0),
      LoopCarryMs_(0), SceneUpAxis_(1), SceneFrontAxis_(2)
{
    IdentityOrient(SceneForwardCsys_.Orient);
    SceneForwardCsys_.Pos[0] = 0.0f;
    SceneForwardCsys_.Pos[1] = 0.0f;
    SceneForwardCsys_.Pos[2] = 0.0f;
}

// Retail 3a5a3/3a5af seeds outer +98/+94 only for the SCENE constructor.
// Common post-constructor stores 3a6a2..3a6b1 bind every entity's output pointers.
// Retail defines the outer axes only when the first SCENE writes them.
// Initial 1/2 values do not reconstruct its unwritten heap words.
void FThornScene::BuildInverseCsys(float* Out12) const
{
    InvertParentCsys(Out12, &SceneForwardCsys_);
}

void FThornScene::BindEntitySceneAxes(FThornEntity* Entity)
{
    if (Entity->DescriptorOwnerType == kThornEntityScene)
    {
        SceneUpAxis_ = Entity->Snap.SceneBlob[5];
        SceneFrontAxis_ = Entity->Snap.SceneBlob[6];
    }
    // 3a69c/3a69f: borrowed forward frame, not the inverse cache.
    Entity->ParentCsys = &SceneForwardCsys_;
    Entity->SceneFogOutputA = &SceneUpAxis_;
    Entity->SceneFogOutputB = &SceneFrontAxis_;
}

void FThornScene::RegisterFrameEntity(FThornEntity* Entity)
{
    const int Type = Entity->DescriptorOwnerType;
    if (Type == kThornEntityCompound || Type == kThornEntityDeformable ||
        Type == kThornEntitySubScene || Type == kThornEntityPsys)
        FrameEntities_->push_back(Entity);
}

void FThornScene::BindEventSceneAxes(FThornEvent* Event)
{
    // 39fbb..39fca: event +20 up and +24 front point at the same owner fields.
    // 39fb1/39fb4: every event borrows the same forward scene frame.
    Event->ParentCsys = &SceneForwardCsys_;
    Event->bOwnsParentCsys = 0;
    Event->SceneUpAxis = &SceneUpAxis_;
    Event->SceneFrontAxis = &SceneFrontAxis_;
}

FThornScene::~FThornScene()
{
    // ~FScriptEngine performs Clear on the normal path, before this cleanup.
    // 58bed..58b90 unwind only storage if a preceding callback throws.
    FrameEntities_.reset();
    StopTimes_.reset();
    delete[] reinterpret_cast<unsigned char*>(Active_);
    Active_ = nullptr;
    ActiveCount_ = 0;
    ActiveCap_ = 0;
    delete[] reinterpret_cast<unsigned char*>(Pending_);
    Pending_ = nullptr;
    PendingCount_ = 0;
    PendingCap_ = 0;
    PendingBegin_ = 0;
    delete[] reinterpret_cast<unsigned char*>(Master_);
    Master_ = nullptr;
    MasterCount_ = 0;
    MasterCap_ = 0;
}

void FThornScene::Clear()
{
    // 37f70 clears ranges but retains vector storage and the alias-list sentinel.
    StopAll();
    FrameEntities_->clear();
    // 37f98..37fc3: clear pending aliases before event deleting callbacks.
    PendingCount_ = 0;
    PendingBegin_ = 0;
    if (MasterCount_ != 0)
    {
        FThornEvent** EventSlot = Master_;
        do
        {
            // 37fda..37ff1 retains the selected slot through deleting-dtor.
            FThornEvent::DestroyNative(*EventSlot);
            *EventSlot = nullptr;
            ++EventSlot;
        } while (EventSlot != Master_ + MasterCount_);
    }
    MasterCount_ = 0;
    for (std::map<int, FThornEntity*>::iterator Entry = Entities_.begin();
         Entry != Entities_.end(); ++Entry)
    {
        DestroyEntity(Entry->second);
        Entry->second = nullptr;
    }
    Entities_.clear();
    EntityCount_ = 0;
    DurationMs_ = 0;
    // 37f70..38105 does not reset outer+24/+28: retain both clocks.
    PlaybackState_ = 0;
}

void FThornScene::GrowMaster()
{
    GrowPointerArray(reinterpret_cast<void***>(&Master_), &MasterCap_, MasterCount_ + 1,
                     static_cast<int>(sizeof(FThornEvent*)));
}

void FThornScene::GrowPending()
{
    GrowPointerArray(reinterpret_cast<void***>(&Pending_), &PendingCap_, PendingCount_ + 1,
                     static_cast<int>(sizeof(FThornEvent*)));
}

void FThornScene::GrowActive()
{
    GrowPointerArray(reinterpret_cast<void***>(&Active_), &ActiveCap_, ActiveCount_ + 1,
                     static_cast<int>(sizeof(FThornEvent*)));
}

FThornMonitorLinks::FThornMonitorLinks()
    : Sentinel(&InactiveSentinel), Count(0)
{
    Sentinel->Next = Sentinel;
    Sentinel->Previous = Sentinel;
    Sentinel->Entity = nullptr;
}

FThornMonitorLinks::~FThornMonitorLinks()
{
    Reset();
}

void FThornMonitorLinks::Initialize()
{
    // 41144 allocates this owner only after common entity construction.
    OwnedSentinel.reset(new FThornMonitorLinkNode);
    Sentinel = OwnedSentinel.get();
    Sentinel->Next = Sentinel;
    Sentinel->Previous = Sentinel;
    Sentinel->Entity = nullptr;
    Count = 0;
}

void FThornMonitorLinks::Reset()
{
    Clear();
    OwnedSentinel.reset();
    Sentinel = &InactiveSentinel;
    Sentinel->Next = Sentinel;
    Sentinel->Previous = Sentinel;
    Sentinel->Entity = nullptr;
    Count = 0;
}

void FThornMonitorLinks::Erase(FThornMonitorLinkNode* Node)
{
    Node->Previous->Next = Node->Next;
    Node->Next->Previous = Node->Previous;
    delete Node;
    --Count;
}

void FThornMonitorLinks::Clear()
{
    FThornMonitorLinkNode* Node = Sentinel->Next;
    while (Node != Sentinel)
    {
        FThornMonitorLinkNode* Next = Node->Next;
        Erase(Node);
        Node = Next;
    }
}

// FUN_06f3fcc0: unsigned minimum length, _memicmp, then length tie-break.
bool FThornUserPropLess::operator()(const std::string& Left, const std::string& Right) const
{
    const size_t Length = Left.size() < Right.size() ? Left.size() : Right.size();
    const int Comparison = _memicmp(Left.data(), Right.data(), Length);
    if (Comparison != 0)
        return Comparison < 0;
    return Left.size() < Right.size();
}

FThornEntity::FThornEntity()
    : NameStorage(std::in_place), TemplateStorage(std::in_place), UserProps(std::in_place)
{
    std::memset(&Snap, 0, sizeof(FThornEntity) - offsetof(FThornEntity, Snap));
    bMotionPathDescriptorOwner = false;
    bMotionPathConstructionComplete = false;
    IdentityOrient(Snap.Orient);
    Snap.Fovh = 60.0f;
    Snap.NearPlane = 0.1f;
    Snap.FarPlane = 100000.0f;
    Snap.Aspect = 4.0f / 3.0f;
    // Retail FUN_06f42d70 (the SCENE/SUBSCENE entity constructor) sets both
    // axis fields to the sentinel value 6 at construction; FThornScene::Attach
    // (LAB_06f43230 tail) only remaps that sentinel to the real defaults
    // (front=5, up=1) when the field is still 6.  Nothing else in the
    // descriptor-load path assigns these fields, so without this initializer
    // the memset above would leave them at 0 and the Attach-time remap would
    // never fire.  Harmless for non-scene entity types (unused fields).
    SceneUpAxis = 6;
    SceneFrontAxis = 6;
    BoundControlType = -1;
    InitializeLightEntityPortState(this);
}

// Typed construction completes before LAB_06f3a3f0 binds or publishes this object.
FThornEntity::FThornEntity(int Type, const FThornEntitySnap* Desc, FThornScene* OwnerScene)
    : FThornEntity()
{
    DescriptorOwnerType = Type;
    // Common construction reads strings before the original descriptor pose.
    AssignEntityName(Desc->Name);
    AssignEntityTemplate(Desc->TemplateName);
    std::memcpy(LoadPos, Desc->Pos, sizeof(LoadPos));
    std::memcpy(LoadOrient, Desc->Orient, sizeof(LoadOrient));
    if (Type == kThornEntityCompound || Type == kThornEntityDeformable)
        Snap.FloorHeight = 0.0f;
    else if (Type == kThornEntityCamera || Type == kThornEntityMonitor)
    {
        MonitorLinks.Initialize();
        Snap.Fovh = 60.0f;
        Snap.NearPlane = 0.1f;
        Snap.FarPlane = 100000.0f;
        Snap.Aspect = 4.0f / 3.0f;
    }
    else if (Type == kThornEntityLight)
        ThornInitLightEntityDefaults(&Snap.Light);
    else if (Type == kThornEntitySound)
        InitializeSoundDefaults();
    else if (Type == kThornEntityScene || Type == kThornEntitySubScene)
    {
        std::memset(Snap.SceneBlob, 0, sizeof(Snap.SceneBlob));
        Snap.SceneBlob[5] = 6;
        Snap.SceneBlob[6] = 6;
    }
    else if (Type == kThornEntityMotionPath)
    {
        Snap.MotionPath = {nullptr, nullptr};
        bMotionPathDescriptorOwner = true;
    }
    else if (Type == kThornEntityPsys)
        Snap.PsysSparam = 0.0f;
    OwnerScene->ApplyEntitySnap(this, Desc);
    if (Type == kThornEntityCompound || Type == kThornEntityDeformable)
    {
        // Concrete construction replays the live floor and pose after ApplyDesc.
        std::memcpy(&FloorHeight, &Snap.FloorHeight, sizeof(FloorHeight));
        InnerSetPos(LoadPos);
        InnerSetOrient(LoadOrient);
    }
    else if (Type == kThornEntityCamera || Type == kThornEntityMonitor)
    {
        ApplyCameraProps(this, &Snap.Fovh);
        // 411bb/411c6 consume the saved pose after the property callback.
        InnerSetPos(LoadPos);
        InnerSetOrient(LoadOrient);
    }
    else if (Type == kThornEntityLight)
    {
        ApplyEntityLightRuntimeProps(this, &Snap.Light);
        InnerSetPos(LoadPos);
        InnerSetOrient(LoadOrient);
    }
    else if (Type == kThornEntityMarker || Type == kThornEntityHardpoint)
    {
        InnerSetPos(LoadPos);
        InnerSetOrient(LoadOrient);
    }
    else if (Type == kThornEntitySound)
    {
        ApplyAudioProps(this, Snap.Audio);
        InnerSetPos(LoadPos);
        InnerSetOrient(LoadOrient);
    }
    else if (Type == kThornEntityScene || Type == kThornEntitySubScene)
    {
        Fun06f431d0(this, Snap.SceneBlob, 0xFF);
        InnerSetPos(LoadPos);
        InnerSetOrient(LoadOrient);
    }
    else if (Type == kThornEntityPsys)
    {
        // 434aa/434b6 replay the live saved pose only after concrete ApplyDesc.
        InnerSetPos(LoadPos);
        InnerSetOrient(LoadOrient);
    }
    // The MOTION_PATH constructor FUN_06f426f0 follows its ApplyDesc with
    // FUN_06f427d0; the update slot (LAB_06f38bd0) only re-applies the
    // descriptor, so this binding belongs to construction alone.
    if (Type == kThornEntityMotionPath)
    {
        BindMotionPathFromDescObject();
        InnerSetPos(LoadPos);
        InnerSetOrient(LoadOrient);
        bMotionPathConstructionComplete = true;
    }
}

FThornEntity::~FThornEntity() noexcept(false)
{
    struct FCommonTeardownStage
    {
        FThornEntity* Entity;
        ~FCommonTeardownStage() noexcept(false) { Entity->ReleaseCommonEntityState(); }
    };
    FCommonTeardownStage CommonTeardown{this};
    // Each retail deleting wrapper enters its concrete destructor, which
    // releases specialized controls before the common entity destructor.
    // The scene owner performs no extra control release before this body.
    // Retail MOTION_PATH destructor FUN_06f42cb0 (thorn.dll.c:25902-25929)
    // releases +0xe8 (constructor-retained path object) and +0xcc (the
    // descriptor's path object) before the path-type string and +0xd0.
    if (bMotionPathDescriptorOwner)
    {
        FThornControlReleaseStage<IMotionPath> RuntimeTeardown(MotionPath);
        struct FPathTypeTeardownStage
        {
            FThornEntity* Entity;
            ~FPathTypeTeardownStage()
            {
                delete[] Entity->PathType;
                Entity->PathType = nullptr;
                Entity->PathTypeCapacity = 0;
            }
        };
        FPathTypeTeardownStage PathTypeTeardown{this};
        // 42ce1/42cee read each live owner after the preceding Release callback.
        // 426f0 construction unwind has only string/runtime/common stages.
        if (bMotionPathConstructionComplete)
        {
            if (MotionPathBoundObject != nullptr)
                MotionPathBoundObject->Release();
            if (Snap.MotionPath.MotionPath != nullptr)
                Snap.MotionPath.MotionPath->Release();
        }
        // RAII also unwinds sole string then live runtime +d0 after either throw.
    }
    else
        ReleaseControl(MotionPathBoundObject);
    ReleaseControl(ParsedMotionPathDescObject);
    const bool bCameraOwner = DescriptorOwnerType == kThornEntityCamera ||
                              DescriptorOwnerType == kThornEntityMonitor;
    const bool bPsysOwner = DescriptorOwnerType == kThornEntityPsys;
    const bool bSceneOwner = DescriptorOwnerType == kThornEntityScene ||
                             DescriptorOwnerType == kThornEntitySubScene;
    if (bSceneOwner)
    {
        FThornControlReleaseStage<ILightControl> LightTeardown(LightControl);
        FThornControlReleaseStage<IScriptControl> ScriptTeardown(ScriptControl);
        FThornControlReleaseStage<IFogControl> FogTeardown(FogControl);
        FThornControlReleaseStage<IReverbControl> ReverbTeardown(ReverbControl);
        ReleaseControl(SubtitleControl);
    }
    else if (bPsysOwner)
    {
        // 434f0 specializes only +cc; base strings/UserProps precede +14/+10.
        ReleaseControl(PsysControl);
    }
    else if (bCameraOwner)
    {
        // 41210 destruction is distinct from 417e0 Detach: no reciprocal
        // UnbindMonitorChildren, and +e8 is released BEFORE +e4.
        struct FMonitorTeardownStage
        {
            FThornMonitorLinks& Links;
            ~FMonitorTeardownStage() { Links.Reset(); }
        };
        FMonitorTeardownStage MonitorTeardown{MonitorLinks};
        FThornControlReleaseStage<ICameraControl> CameraTeardown(CameraControl);
        ReleaseControl(SubtitleControl);
    }
    else
        ReleaseDerivedControls();
    // 41277..412bb deletes each local node without touching its child owner.
    // The member's RAII destructor also clears nodes if a control throws.
    MonitorLinks.Clear();

}

void FThornEntity::ReleaseCommonEntityState()
{
    // Retail FUN_06f405e0 (the common-base entity destructor) owns this
    // teardown directly: it walks the type-3 refcounted strings and the
    // UserProps tree/sentinel pair before releasing the two bound controls.
    // The modern equivalents are owned here rather than by the scene-level
    // FThornScene::DestroyEntity caller so that any direct destruction of an
    // FThornEntity observes the same cleanup retail's destructor performs
    // unconditionally.
    UserProps.reset();
    TemplateStorage.reset();
    NameStorage.reset();
    delete[] PathType;
    PathType = nullptr;
    delete[] PathData;
    PathData = nullptr;
    // Common405e0: all concrete owners reach live User before live Geo.
    {
        // Geo is read when this stage runs, including User Release unwinding.
        FThornControlReleaseStage<IGeoTransformControl> GeoTeardown(GeoControl);
        ReleaseControl(UserEventControl);
    }
    // Common retail destructor 405e0 does not delete borrowed inner+4.
    ParentCsys = nullptr;
}

char* FThornEntity::AssignEntityName(const char* Source)
{
    if (Source == nullptr)
        return nullptr;
    const std::size_t Length = std::strlen(Source);
    const std::size_t SnapshotSize = (Length + 4u) & ~std::size_t(3u);
    char* Snapshot = static_cast<char*>(_alloca(SnapshotSize));
    std::memcpy(Snapshot, Source, Length + 1u);
    NameStorage->assign(Snapshot, Length);
    return NameStorage->data();
}

char* FThornEntity::AssignEntityTemplate(const char* Source)
{
    if (Source == nullptr)
        return nullptr;
    const std::size_t Length = std::strlen(Source);
    const std::size_t SnapshotSize = (Length + 4u) & ~std::size_t(3u);
    char* Snapshot = static_cast<char*>(_alloca(SnapshotSize));
    std::memcpy(Snapshot, Source, Length + 1u);
    TemplateStorage->assign(Snapshot, Length);
    return TemplateStorage->data();
}

void FThornEntity::InnerUnused1()
{
}

void FThornEntity::InnerApplyDesc(const FThornEntitySnap* SnapIn)
{
    // FUN_06f41c50 / FUN_06f41fe0 / FUN_06f42410 / FUN_06f42a10 / FUN_06f43590 are the
    // per-type implementations of this operation in retail.  The common
    // descriptor has already been copied by FThornScene::ApplyEntitySnap;
    // this method owns the type-specific tail that can be represented by the
    // native snapshot.  The retail engine-resource callback chain is
    // intentionally not fabricated here: it is supplied by the object bound
    // through Attach and has a different VC6/vtable ABI.
    // FUN_06f41fe0 (thorn.dll.txt:65968-66062) and its per-type siblings:
    // right after the snapshot/name copy, when a parent Csys is bound, the
    // incoming world-space pose is converted into the parent frame in
    // place -- math vtbl+0x38 TransposeTransform of the parent position,
    // FUN_06f22d50 negate, FUN_06f428b0 transpose of the parent 3x3 and
    // FUN_06f3b970 pack (= InvertParentCsys), then vtbl+0x28 Transform for
    // Snap.Pos and vtbl+0x20 MulMatrix for Snap.Orient.  Both results are
    // copied back unconditionally; the GENRESULTs are never tested.
    if (ParentCsys != nullptr)
    {
        float Inv[12];
        InvertEventParentCsys(Inv, ParentCsys);
        I3DMathEngine* Math = AcquireSceneEventMathEngine();
        float LocalPos[3];
        SceneMathTransform(Math, LocalPos, Inv, Snap.Pos);
        CopyPoseWordsForward(Snap.Pos, LocalPos, 3);
        float LocalOrient[9];
        SceneMathMulMatrix(Math, LocalOrient, Inv, Snap.Orient);
        CopyPoseWordsForward(Snap.Orient, LocalOrient, 9);
    }

    // Every retail per-type ApplyDesc ends with FUN_06f40c40 / FUN_06f40c60
    // (thorn.dll.txt:66063-66071 for MARKER/HARDPOINT): a plain copy of the
    // (parent-converted) Snap pose into entity +0x38 / +0x44.  No control
    // publish happens on this path -- the non-virtual FUN_06f40900 /
    // FUN_06f40ab0 calls are issued by the constructors only, with a null
    // GeoControl, and UpdateEntityFromDesc (LAB_06f38bd0) invokes slot 2
    // alone.  LoadPos/LoadOrient are what IScriptEngine slots 10/11 report.
    CopyPoseWordsForward(LoadPos, Snap.Pos, 3);
    CopyPoseWordsForward(LoadOrient, Snap.Orient, 9);

    if (DescriptorOwnerType == kThornEntityPsys &&
        (SnapIn->Flags & 1) == 0 && SnapIn->Type == kThornEntityPsys)
        std::memcpy(&Snap.PsysSparam, &SnapIn->PsysSparam, sizeof(Snap.PsysSparam));

    if ((DescriptorOwnerType == kThornEntityScene || DescriptorOwnerType == kThornEntitySubScene) &&
        (SnapIn->Flags & 1) == 0 && SnapIn->Type == kThornEntityScene)
    {
        for (int Index = 0; Index < 15; ++Index)
        {
            unsigned int Word;
            std::memcpy(&Word, &SnapIn->SceneBlob[Index], sizeof(Word));
            std::memcpy(&Snap.SceneBlob[Index], &Word, sizeof(Word));
        }
        Fun06f431d0(this, Snap.SceneBlob, 0xFF);
    }

    if (DescriptorOwnerType == kThornEntitySound)
    {
        if ((SnapIn->Flags & 1) == 0 && SnapIn->Type == kThornEntitySound)
            CopyPoseWordsForward(Snap.Audio, SnapIn->Audio, 8);
        // 42576..42595 reads Name/Template only after the saved pose and tail.
        Snap.Name = AssignEntityName(SnapIn->Name);
        Snap.TemplateName = AssignEntityTemplate(SnapIn->TemplateName);
    }

    if (DescriptorOwnerType == kThornEntityMotionPath &&
        (SnapIn->Flags & 1) == 0 && SnapIn->Type == kThornEntityMotionPath)
        ApplyMotionPathDesc(this, &SnapIn->MotionPath);

    if (DescriptorOwnerType == kThornEntityLight &&
        (SnapIn->Flags & 1) == 0 && SnapIn->Type == kThornEntityLight)
    {
        // 41dc1..41dd3: saved pose precedes live Flags/Type and raw36 tail.
        for (int Index = 0; Index < 36; ++Index)
        {
            unsigned int Word;
            std::memcpy(&Word, reinterpret_cast<const unsigned char*>(&SnapIn->Light) +
                                   Index * sizeof(Word), sizeof(Word));
            std::memcpy(reinterpret_cast<unsigned char*>(&Snap.Light) +
                            Index * sizeof(Word), &Word, sizeof(Word));
        }
    }

    if ((DescriptorOwnerType == kThornEntityCompound || DescriptorOwnerType == kThornEntityDeformable) &&
        (SnapIn->Flags & 1) == 0 &&
        (SnapIn->Type == kThornEntityCompound || SnapIn->Type == kThornEntityDeformable))
    {
        // 40f80 changes the retained descriptor; 40f90 owns runtime publication.
        std::memcpy(&Snap.FloorHeight, &SnapIn->FloorHeight, sizeof(Snap.FloorHeight));
    }

    if (DescriptorOwnerType == kThornEntityCamera || DescriptorOwnerType == kThornEntityMonitor)
    {
        // 4144e/4145c load pose BEFORE 41463 reciprocal unlink. The input
        // flags/type/tail are read only after that operation has completed.
        UnbindMonitorChildren(this);
        if ((SnapIn->Flags & 1) == 0 && SnapIn->Type == kThornEntityCamera)
        {
            // 41510 raw Near/Far/Fov/Aspect stores preserve partial aliases.
            const int StoreOrder[4] = {1, 2, 0, 3};
            for (int Index = 0; Index < 4; ++Index)
            {
                const int Field = StoreOrder[Index];
                unsigned int Word;
                std::memcpy(&Word, reinterpret_cast<const unsigned char*>(&SnapIn->Fovh) +
                                   Field * sizeof(Word), sizeof(Word));
                std::memcpy(reinterpret_cast<unsigned char*>(&Snap.Fovh) +
                            Field * sizeof(Word), &Word, sizeof(Word));
            }
        }
    }
}

float* FThornEntity::InnerGetPos()
{
    GetRetailRelativePosition(this, Snap.Pos);
    return Snap.Pos;
}

float* FThornEntity::InnerGetOrient()
{
    GetRetailRelativeOrientation(this, Snap.Orient);
    return Snap.Orient;
}

void FThornEntity::InnerSetPos(const float* Pos)
{
    // FUN_06f40900 / LAB_06f49f00 thiscall vtable+0x0C RET 4.
    // CAMERA: FUN_06f41490 РІР‚вЂќ after store, typeРІвЂ°В 4 fans out list +0x88 via child +0x0C.
    // 40914..40921 are raw forward word transfers, including overlap.
    for (int Index = 0; Index < 3; ++Index)
    {
        unsigned int Word;
        std::memcpy(&Word, &Pos[Index], sizeof(Word));
        std::memcpy(&Snap.Pos[Index], &Word, sizeof(Word));
    }
    if (GeoControl != nullptr)
    {
        float World[3];
        std::memcpy(World, Pos, sizeof(World));
        if (ParentCsys != nullptr)
        {
            const FThornCsys* Parent = ParentCsys;
            I3DMathEngine* Math = AcquireSceneEventMathEngine();
            float Out[3];
            // Retail FUN_06f40900 copies the transform result into World
            // unconditionally after the call -- it never inspects the
            // returned GENRESULT before consuming the output buffer.
            SceneMathTransform(Math, Out, Parent->Orient, Pos);
            std::memcpy(World, Out, sizeof(World));
        }
        GeoControl->SetPosition(const_cast<float*>(World));
    }
    // Only concrete CAMERA/MONITOR dispatch enters 41490's late fanout.
    if ((DescriptorOwnerType == kThornEntityCamera || DescriptorOwnerType == kThornEntityMonitor) &&
        Snap.Type != kThornEntityMonitor)
    {
        FThornMonitorLinkNode* Node = MonitorLinks.Sentinel->Next;
        while (Node != MonitorLinks.Sentinel)
        {
            Node->Entity->InnerSetPos(Pos);
            Node = Node->Next; // 414c0: AFTER child virtual callback.
        }
    }
}

void FThornEntity::InnerSetOrient(const float* Mat9)
{
    // FUN_06f40ab0 / LAB_06f49f20 thiscall vtable+0x10 RET 4.
    // CAMERA: FUN_06f414d0 РІР‚вЂќ after store, the camera owner fans out its
    // linked list through child +0x10; type-4 monitor nodes are terminal.
    // 40acb REP MOVSD copies forward; do not collapse overlapping input.
    for (int Index = 0; Index < 9; ++Index)
    {
        unsigned int Word;
        std::memcpy(&Word, &Mat9[Index], sizeof(Word));
        std::memcpy(&Snap.Orient[Index], &Word, sizeof(Word));
    }
    if (GeoControl != nullptr)
    {
        float World[9];
        std::memcpy(World, Mat9, sizeof(World));
        if (ParentCsys != nullptr)
        {
            const FThornCsys* Parent = ParentCsys;
            I3DMathEngine* Math = AcquireSceneEventMathEngine();
            float Out9[9];
            // Retail FUN_06f40ab0 copies the mul-matrix result into World
            // unconditionally after the call -- it never inspects the
            // returned GENRESULT before consuming the output buffer.
            SceneMathMulMatrix(Math, Out9, Parent->Orient, Mat9);
            std::memcpy(World, Out9, sizeof(World));
        }
        GeoControl->SetOrientation(const_cast<float*>(World));
    }
    // Only concrete CAMERA/MONITOR dispatch enters 414d0's late fanout.
    if ((DescriptorOwnerType == kThornEntityCamera || DescriptorOwnerType == kThornEntityMonitor) &&
        Snap.Type != kThornEntityMonitor)
    {
        FThornMonitorLinkNode* Node = MonitorLinks.Sentinel->Next;
        while (Node != MonitorLinks.Sentinel)
        {
            Node->Entity->InnerSetOrient(Mat9);
            Node = Node->Next; // 41500: AFTER child virtual callback.
        }
    }
}

void FThornEntity::CaptureAudioProps(float* Out8)
{
    // 423d3..42400: optional GET, then raw forward REP8 with ignored HRESULT.
    ISoundControl* Control = SoundControl;
    if (Control != nullptr)
        Control->GetAudioProps(Audio, -1);
    CopyPoseWordsForward(Out8, Audio, 8);
}

void FThornEntity::InitializeSoundDefaults()
{
    // FUN_06f42210 descriptor defaults: dmin=1, dmax=1e9, raw integer cones=360.
    // The constructor copies these retained words to the runtime cache after ApplyDesc.
    std::memset(Audio, 0, sizeof(Audio));
    AudioMask = 0;
    Audio[2] = 1.0f;
    Audio[3] = 1000000000.0f;
    const int DefaultAinAout = 0x168;
    std::memcpy(&Audio[4], &DefaultAinAout, sizeof(DefaultAinAout));
    std::memcpy(&Audio[5], &DefaultAinAout, sizeof(DefaultAinAout));
    std::memcpy(Snap.Audio, Audio, sizeof(Snap.Audio));
}

void FThornEntity::ReleaseLightControls()
{
    // FUN_06f41a20: +0x15c (ID3DLightControl), then +0x158 (ILightControl).
    // The modern owner stores these as typed interface members, so each slot
    // is released and cleared without exposing the retail object offsets.
    FThornControlReleaseStage<ILightControl> LightTeardown(LightControl);
    ReleaseControl(D3dLightControl);
}

void FThornEntity::InnerUnbindControls()
{
    // These standalone slot+24 sequences differ from deleting destructors.
    // Concrete owner selects the slot even if ApplyDesc changes Snap.Type.
    const int Type = DescriptorOwnerType;
    if (Type == kThornEntityCompound || Type == kThornEntityDeformable)
    {
        // 410a0: +10, +d0, +cc, +14, +d4.
        ReleaseControl(GeoControl);
        ReleaseControl(IkControl);
        ReleaseControl(CompoundControl);
        ReleaseControl(UserEventControl);
        ReleaseControl(SubtitleControl);
    }
    else if (Type == kThornEntityCamera || Type == kThornEntityMonitor)
    {
        // 417e0: child unlink, +e4, +10, +14, +e8.
        UnbindMonitorChildren(this);
        ReleaseControl(CameraControl);
        ReleaseControl(GeoControl);
        ReleaseControl(UserEventControl);
        ReleaseControl(SubtitleControl);
    }
    else if (Type == kThornEntityLight)
    {
        // 41ed0: +158 BEFORE +15c, unlike deleting-dtor 41a20.
        ReleaseControl(LightControl);
        ReleaseControl(D3dLightControl);
        ReleaseControl(GeoControl);
        ReleaseControl(UserEventControl);
    }
    else if (Type == kThornEntitySound)
    {
        // 42690: +10, +e8, +14, +ec.
        ReleaseControl(GeoControl);
        ReleaseControl(SoundControl);
        ReleaseControl(UserEventControl);
        ReleaseControl(SubtitleControl);
    }
    else if (Type == kThornEntityScene || Type == kThornEntitySubScene)
    {
        ReleaseSceneBoundControls();
    }
    else if (Type == kThornEntityMotionPath)
    {
        ReleaseMotionPathControls();
    }
    else if (Type == kThornEntityPsys)
    {
        // 437e0: +10, +cc, +14; this is PSYS, not MOTION_PATH.
        ReleaseControl(GeoControl);
        ReleaseControl(PsysControl);
        ReleaseControl(UserEventControl);
    }
    else
    {
        // 421e0: +10, +14.
        ReleaseControl(GeoControl);
        ReleaseControl(UserEventControl);
    }
    bBound = 0;
}

void FThornEntity::ReleaseDerivedControls()
{
    // Concrete destructor identity stays fixed when binding or Snap.Type changes.
    // Common User/Geo owners are released after the common owned-data teardown.
    const int Type = DescriptorOwnerType;
    if (Type == kThornEntityCompound || Type == kThornEntityDeformable)
    {
        // Reverse destruction reads each live field only at its retail stage.
        FThornControlReleaseStage<ICompoundControl> CompoundTeardown(CompoundControl);
        FThornControlReleaseStage<IIKControl> IkTeardown(IkControl);
        ReleaseControl(SubtitleControl);
        return;
    }
    else if (Type == kThornEntityLight)
        ReleaseLightControls();
    else if (Type == kThornEntitySound)
    {
        FThornControlReleaseStage<ISoundControl> SoundTeardown(SoundControl);
        ReleaseControl(SubtitleControl);
    }
    else if (Type == kThornEntityMotionPath)
        ReleaseControl(MotionPath);
    bBound = 0;
}

void FThornEntity::ReleaseSceneBoundControls()
{
    // FUN_06f433c0: +0x10 Geo, +0x104 Light, +0x14 UserEvent,
    // +0x108 Script, +0x10c Fog, +0x114 Subtitle. Retail does not
    // release +0x110 Reverb on this unbind path; the scene destructor
    // releases it later through ReleaseDerivedControls().
    ReleaseControl(GeoControl);
    ReleaseControl(LightControl);
    ReleaseControl(UserEventControl);
    ReleaseControl(ScriptControl);
    ReleaseControl(FogControl);
    ReleaseControl(SubtitleControl);
    bBound = 0;
}

void FThornEntity::ReleaseMotionPathControls()
{
    // FUN_06f42c60: release +0xd0 (IMotionPath), then +0x10 (IGeo), then
    // +0x14 (IUserEvent), and clear the bound state. This is the observed
    // retail order; it is not the reverse of the bind QI sequence.
    ReleaseControl(MotionPath);
    ReleaseControl(GeoControl);
    ReleaseControl(UserEventControl);
    bBound = 0;
}

void FThornEntity::BindMotionPathFromDescObject()
{
    // FUN_06f427d0 (thorn.dll.txt:66842-66935), called by the MOTION_PATH
    // constructor FUN_06f426f0 (66792-66794) with &this+0xc8, i.e. the
    // entity's own mutable Snap.MotionPath descriptor pair.
    // Both 427f9 and 4292e address the SAME +d4 string owner. The +e4
    // pointer refreshed at 4283f is a second cache, never a second owner;
    // its private self-assignment/allocation contract remains unverified.
    // Publish +e4 before any interface callbacks. Null clears only the cache.
    const char* Source = Snap.MotionPath.PathType;
    MotionPathBoundType = Source != nullptr ? AssignMotionPathType(this, Source, true) : nullptr;
    // The object part: Release the old +0xe8, +0xe8 = +0xcc,
    // and if non-null AddRef it, Release +0xd0, then QueryInterface
    // "1.11_IMotionPath" from +0xe8 straight into +0xd0.
    if (MotionPathBoundObject != nullptr)
        MotionPathBoundObject->Release();
    // 42862/42866 reread the public owning field AFTER the Release callback.
    MotionPathBoundObject = Snap.MotionPath.MotionPath;
    if (MotionPathBoundObject == nullptr)
        return;
    MotionPathBoundObject->AddRef();
    ReleaseControl(MotionPath);
    MotionPathBoundObject->QueryInterface(Iids::MotionPath,
                                          reinterpret_cast<void**>(&MotionPath));
}

FThornEntity* FThornScene::AddEntity()
{
    FThornEntity* Entity = new (std::nothrow) FThornEntity();
    if (Entity == nullptr)
        return nullptr;
    // Unique signed-key insertion retains the existing payload on collision.
    Entities_.insert(std::make_pair(EntityCount_, Entity));
    EntityCount_ = static_cast<int>(static_cast<unsigned int>(EntityCount_) + 1u);
    return Entity;
}

FThornEvent* FThornScene::AddEvent(int DescriptorStorageType)
{
    GrowMaster();
    if (Master_ == nullptr || MasterCount_ >= MasterCap_)
        return nullptr;
    FThornEvent* Event = new (std::nothrow) FThornEvent(DescriptorStorageType);
    if (Event == nullptr)
        return nullptr;
    BindEventSceneAxes(Event);
    Master_[MasterCount_] = Event;
    MasterCount_ = MasterCount_ + 1;
    return Event;
}

unsigned long FThornScene::EntityCount() const
{
    return static_cast<unsigned long>(EntityCount_);
}

unsigned long FThornScene::LiveEntityCount() const
{
    return static_cast<unsigned long>(Entities_.size());
}

bool FThornScene::EntityExists(int Index) const
{
    if (static_cast<unsigned int>(Index) >= static_cast<unsigned int>(EntityCount_))
        return false;
    return Entities_.find(Index) != Entities_.end();
}

void FThornScene::DestroyEntity(FThornEntity* Entity)
{
    // Clear calls the concrete deleting destructor directly. RemoveEntityAt
    // performs its explicit inner unbind before reaching this owner. Derived
    // controls and common data are released only in ~FThornEntity().
    delete Entity;
}

namespace
{
    // FUN_06f40380 receives the complete common packet by value at its callers.
    void PackEntitySnapCommon(FThornEntitySnap* Destination,
                              const FThornEntitySnap& Snapshot)
    {
        Destination->Type = Snapshot.Type;
        std::memcpy(Destination->Orient, Snapshot.Orient, sizeof(Destination->Orient));
        std::memcpy(Destination->Pos, Snapshot.Pos, sizeof(Destination->Pos));
        Destination->LtGrp = Snapshot.LtGrp;
        Destination->SrtGrp = Snapshot.SrtGrp;
        Destination->Flags = Snapshot.Flags;
        Destination->UsrFlg = Snapshot.UsrFlg;
        Destination->Name = nullptr;
        Destination->TemplateName = nullptr;
    }
}

void FThornScene::ApplyEntitySnap(FThornEntity* Entity, const FThornEntitySnap* Snap)
{
    // All concrete ApplyDesc callers pass a 17-DWORD common snapshot before
    // the leaf publishes it. Name/Template inputs are reread afterwards.
    FThornEntitySnap CommonSnapshot;
    std::memcpy(&CommonSnapshot, Snap, offsetof(FThornEntitySnap, Fovh));
    PackEntitySnapCommon(&Entity->Snap, CommonSnapshot);
    if (Entity->DescriptorOwnerType != kThornEntitySound)
    {
        Entity->Snap.Name = Entity->AssignEntityName(Snap->Name);
        Entity->Snap.TemplateName = Entity->AssignEntityTemplate(Snap->TemplateName);
    }
    // Concrete CAMERA replay owns its LoadPose/unlink/late-tail sequence.
    Entity->InnerApplyDesc(Snap);
}

bool FThornScene::EventAllowsMissingSecondary(int Type)
{
    // Table 06f3a1e8 indexed by type-2: 0 РІвЂ вЂ™ fail, 1 РІвЂ вЂ™ continue.
    // Types 2/6/8/9/11 fail when secondary < 0; others continue (incl. type 1 via JA).
    if (Type == kThornTypeSetCamera || Type == kThornTypePathAnim ||
        Type == kThornTypeAttachEntity || Type == kThornTypeConnectHardpoints ||
        Type == kThornTypeStartIk)
        return false;
    return true;
}

FThornEvent** FThornScene::EraseMasterEventAt(FThornEvent** Slot)
{
    // 3a249..3a26e removes MASTER publication before the first Stop callback.
    FThornEvent* Event = *Slot;
    FThornEvent** MasterEnd = Master_ + MasterCount_;
    for (FThornEvent** Read = Slot + 1; Read != MasterEnd; ++Read)
        *(Read - 1) = *Read;
    MasterCount_ = MasterCount_ - 1;

    if (ActiveCount_ != 0)
    {
        // Retail keeps these cursors through Stop, but rereads the live end.
        FThornEvent** ActiveSlot = Active_;
        FThornEvent** Successor = ActiveSlot + 1;
        do
        {
            if (*ActiveSlot == Event)
            {
                Event->Stop(this);
                FThornEvent** ActiveEnd = Active_ + ActiveCount_;
                for (FThornEvent** Read = Successor; Read != ActiveEnd; ++Read)
                    *(Read - 1) = *Read;
                ActiveCount_ = ActiveCount_ - 1;
            }
            else
            {
                ++ActiveSlot;
                ++Successor;
            }
        } while (ActiveSlot != Active_ + ActiveCount_);
    }

    // 3a2d3..3a30c erases only the first PENDING match. STOPLIST is untouched.
    for (int PendingIndex = 0; PendingIndex != PendingCount_; ++PendingIndex)
    {
        if (Pending_[PendingIndex] != Event)
            continue;
        for (int Shift = PendingIndex + 1; Shift != PendingCount_; ++Shift)
            Pending_[Shift - 1] = Pending_[Shift];
        PendingCount_ = PendingCount_ - 1;
        break;
    }
    FThornEvent::DestroyNative(Event);
    return Slot;
}

void FThornScene::FinalizeLoad(int DurationMs)
{
    // Slot4_Load 3831d sets state before the parser's second GetDuration.
    // 38335 stores that duration, then 3833f..38414 sorts only MASTER.
    DurationMs_ = DurationMs;
    SortMasterEvents(Master_, MasterCount_);
}

int FThornScene::FindEntityByName(const char* Name) const
{
    if (Name == nullptr)
        return -1;
    for (std::map<int, FThornEntity*>::const_iterator Entry = Entities_.begin();
         Entry != Entities_.end(); ++Entry)
    {
        FThornEntity* Entity = Entry->second;
        if (Entity == nullptr)
            continue;
        const char* EntityName = Entity->Snap.Name;
        if (EntityName != nullptr && std::strcmp(EntityName, Name) == 0)
            return Entry->first;
    }
    return -1;
}

FThornEntity** FThornScene::FindEntityEntry(int Index)
{
    // 4aa40 exact-find / 4af00 signed lower_bound plus caller equality gate.
    // Insertion preserves this selected payload address. Erasing that entry
    // invalidates it, as the retail node does; no guessed lifetime guard.
    std::map<int, FThornEntity*>::iterator Entry = Entities_.lower_bound(Index);
    if (Entry == Entities_.end() || Index < Entry->first)
        return nullptr;
    return &Entry->second;
}

FThornEntity* FThornScene::EntityAt(int Index)
{
    FThornEntity** Entry = FindEntityEntry(Index);
    return Entry != nullptr ? *Entry : nullptr;
}

const FThornEntity* FThornScene::EntityAt(int Index) const
{
    std::map<int, FThornEntity*>::const_iterator Entry = Entities_.find(Index);
    return Entry != Entities_.end() ? Entry->second : nullptr;
}

void* FThornScene::EntityHandle(int Index)
{
    // Slot 7 enumerates signed keys in order; Index is an unsigned ordinal.
    std::map<int, FThornEntity*>::iterator Entry = Entities_.begin();
    unsigned int Seen = 0;
    const unsigned int Ordinal = static_cast<unsigned int>(Index);
    while (Seen < Ordinal && Entry != Entities_.end())
    {
        ++Entry;
        ++Seen;
    }
    if (Entry == Entities_.end())
        return reinterpret_cast<void*>(static_cast<INT_PTR>(-1));
    return reinterpret_cast<void*>(static_cast<INT_PTR>(Entry->first));
}

FThornEntitySnap* FThornScene::EntitySnap(int Index)
{
    if (!EntityExists(Index))
        return nullptr;
    return &EntityAt(Index)->Snap;
}

FThornEntitySnap* FThornScene::EntitySnapInorder(int Index)
{
    if (Index < 0)
        return nullptr;
    int Seen = 0;
    for (std::map<int, FThornEntity*>::iterator Entry = Entities_.begin();
         Entry != Entities_.end(); ++Entry)
    {
        if (Seen == Index)
            return &Entry->second->Snap;
        ++Seen;
    }
    return nullptr;
}

unsigned long FThornScene::UserPropCount(int Index) const
{
    // LAB_06f38ca0: lookup by key; *(inner+0x80). Miss РІвЂ вЂ™ 0.
    if (!EntityExists(Index))
        return 0;
    return static_cast<unsigned long>(EntityAt(Index)->UserProps->size());
}

const char* FThornScene::UserPropKey(int Index, int N) const
{
    // LAB_06f38d20: slot 31 then n inorder steps from inner+0x74; node+0x10 or "".
    if (!EntityExists(Index))
        return nullptr;
    return NthSortedUserPropKey(EntityAt(Index), N);
}

GENRESULT FThornScene::GetEntityPos(int Index, float* OutVec3)
{
    if (!EntityExists(Index))
        return GR_GENERIC;
    // LAB_06f38950 (thorn.dll.txt:48024-48120): Index >= this+0xa0 or a
    // missing tree entry -> -1; else copy entity +0x38 (the ApplyDesc-time
    // pose, not the live Snap.Pos) and, with a parent Csys, math vtbl+0x28
    // Transform through it.  AL/EAX = 0 on success.
    FThornEntity* Entity = EntityAt(Index);
    CopyPoseWordsForward(OutVec3, Entity->LoadPos, 3);
    const FThornCsys* Parent = Entity->ParentCsys;
    if (Parent != nullptr)
    {
        I3DMathEngine* Math = AcquireSceneCsysMathEngine();
        FTransform ParentInput;
        FVector PositionInput;
        FVector World;
        // The captured parent and output pose remain live until acquisition returns.
        std::memcpy(&ParentInput.Rotation, Parent->Orient, sizeof(ParentInput.Rotation));
        std::memcpy(&ParentInput.Translation, Parent->Pos, sizeof(ParentInput.Translation));
        std::memcpy(&PositionInput, OutVec3, sizeof(PositionInput));
        Math->TransformXform(World, ParentInput, PositionInput);
        // 38a46..38a57 loads all three result words before publishing them.
        unsigned int ResultWords[3];
        std::memcpy(ResultWords, &World, sizeof(ResultWords));
        std::memcpy(OutVec3, ResultWords, sizeof(ResultWords));
    }
    return GR_OK;
}

GENRESULT FThornScene::GetEntityOrient(int Index, float* OutMat9)
{
    if (!EntityExists(Index))
        return GR_GENERIC;
    // LAB_06f38a80 (thorn.dll.txt:48136-48244): same lookup as slot 10, then
    // REP MOVSD 9 dwords from entity +0x44 and, with a parent Csys, math
    // vtbl+0x20 MulMatrix by the parent orientation.
    FThornEntity* Entity = EntityAt(Index);
    CopyPoseWordsForward(OutMat9, Entity->LoadOrient, 9);
    const FThornCsys* Parent = Entity->ParentCsys;
    if (Parent != nullptr)
    {
        I3DMathEngine* Math = AcquireSceneCsysMathEngine();
        FMatrix ParentOrientation;
        FMatrix OrientationInput;
        FMatrix World;
        std::memcpy(&ParentOrientation, Parent->Orient, sizeof(ParentOrientation));
        std::memcpy(&OrientationInput, OutMat9, sizeof(OrientationInput));
        Math->MulMatrix(World, ParentOrientation, OrientationInput);
        float WorldWords[9];
        std::memcpy(WorldWords, &World, sizeof(WorldWords));
        CopyPoseWordsForward(OutMat9, WorldWords, 9);
    }
    return GR_OK;
}

GENRESULT FThornScene::Attach(int Index, void* EngineObj)
{
    // Slot 12: lookup + inner thiscall vtable+0x20. Does not AddRef engineObj.
    // Bind(NULL) AVs in retail QI РІР‚вЂќ callers must use Detach (slot 13 / +0x24).
    FThornEntity* Entity = EntityAt(Index);
    if (Entity == nullptr)
        return GR_GENERIC;
    if (EngineObj == nullptr && Entity->DescriptorOwnerType != kThornEntityPsys &&
        Entity->DescriptorOwnerType != kThornEntityScene &&
        Entity->DescriptorOwnerType != kThornEntitySubScene)
        return GR_GENERIC;

    // The constructor's concrete vtable selects this bind sequence even when
    // slot 2 replaces the published descriptor Type.
    Entity->EngineObj = EngineObj;
    const int EntType = Entity->DescriptorOwnerType;
    Entity->BoundControlType = EntType;

    int Result = -1;
    if (EntType == kThornEntityCompound || EntType == kThornEntityDeformable)
    {
        BindQi(EngineObj, Iids::CompoundControl, Entity->CompoundControl, Result);
        BindQi(EngineObj, Iids::IKControl, Entity->IkControl, Result);
        BindQi(EngineObj, Iids::GeoTransformControl, Entity->GeoControl, Result);
        BindQi(EngineObj, Iids::UserEventControl, Entity->UserEventControl, Result);
        BindQi(EngineObj, Iids::SubtitleControl, Entity->SubtitleControl, Result);
    }
    else if (EntType == kThornEntityCamera || EntType == kThornEntityMonitor)
    {
        // FUN_06f41700: Geo, Camera, UserEvent, Subtitle, in this order.
        BindQi(EngineObj, Iids::GeoTransformControl, Entity->GeoControl, Result);
        BindQi(EngineObj, Iids::CameraControl, Entity->CameraControl, Result);
        BindQi(EngineObj, Iids::UserEventControl, Entity->UserEventControl, Result);
        BindQi(EngineObj, Iids::SubtitleControl, Entity->SubtitleControl, Result);
    }
    else if (EntType == kThornEntityLight)
    {
        BindQi(EngineObj, Iids::GeoTransformControl, Entity->GeoControl, Result);
        BindQi(EngineObj, Iids::LightControl, Entity->LightControl, Result);
        BindQi(EngineObj, Iids::D3DLightControl, Entity->D3dLightControl, Result);
        BindQi(EngineObj, Iids::UserEventControl, Entity->UserEventControl, Result);
    }
    else if (EntType == kThornEntitySound)
    {
        BindQi(EngineObj, Iids::SoundControl, Entity->SoundControl, Result);
        BindQi(EngineObj, Iids::GeoTransformControl, Entity->GeoControl, Result);
        BindQi(EngineObj, Iids::UserEventControl, Entity->UserEventControl, Result);
        BindQi(EngineObj, Iids::SubtitleControl, Entity->SubtitleControl, Result);
    }
    else if (EntType == kThornEntityMarker || EntType == kThornEntityHardpoint)
    {
        BindQi(EngineObj, Iids::GeoTransformControl, Entity->GeoControl, Result);
        BindQi(EngineObj, Iids::UserEventControl, Entity->UserEventControl, Result);
    }
    else if (EntType == kThornEntityScene || EntType == kThornEntitySubScene)
    {
        IDAComponent* EngineObject = static_cast<IDAComponent*>(EngineObj);
        BindLiveEntityControl(EngineObject, Iids::GeoTransformControl, Entity->GeoControl, Result);
        BindLiveEntityControl(EngineObject, Iids::LightControl, Entity->LightControl, Result);
        BindLiveEntityControl(EngineObject, Iids::UserEventControl, Entity->UserEventControl, Result);
        BindLiveEntityControl(EngineObject, Iids::ScriptControl, Entity->ScriptControl, Result);
        BindLiveEntityControl(EngineObject, Iids::FogControl, Entity->FogControl, Result);
        BindLiveEntityControl(EngineObject, Iids::ReverbControl, Entity->ReverbControl, Result);
        BindLiveEntityControl(EngineObject, Iids::SubtitleControl, Entity->SubtitleControl, Result);
        // 43375..433af: test retained descriptor, update descriptor and runtime together.
        if (Entity->Snap.SceneBlob[6] == 6)
        {
            Entity->Snap.SceneBlob[6] = 5;
            Entity->SceneFogPacked[6] = 5;
            Entity->SceneFrontAxis = 5;
        }
        if (Entity->Snap.SceneBlob[5] == 6)
        {
            Entity->Snap.SceneBlob[5] = 1;
            Entity->SceneFogPacked[5] = 1;
            Entity->SceneUpAxis = 1;
        }
    }
    else if (EntType == kThornEntityMotionPath)
    {
        BindQi(EngineObj, Iids::GeoTransformControl, Entity->GeoControl, Result);
        BindQi(EngineObj, Iids::MotionPath, Entity->MotionPath, Result);
        BindQi(EngineObj, Iids::UserEventControl, Entity->UserEventControl, Result);
    }
    else if (EntType == kThornEntityPsys)
    {
        IDAComponent* EngineObject = static_cast<IDAComponent*>(EngineObj);
        BindLiveEntityControl(EngineObject, Iids::GeoTransformControl, Entity->GeoControl, Result);
        BindLiveEntityControl(EngineObject, Iids::PSysControl, Entity->PsysControl, Result);
        BindLiveEntityControl(EngineObject, Iids::UserEventControl, Entity->UserEventControl, Result);
    }
    else
    {
        BindQi(EngineObj, Iids::GeoTransformControl, Entity->GeoControl, Result);
        BindQi(EngineObj, Iids::UserEventControl, Entity->UserEventControl, Result);
    }

    Entity->bBound = (Result == 0) ? 1 : 0;
    Entity->BindState = 2;
    return (Result == 0) ? GR_OK : GR_GENERIC;
}

GENRESULT FThornScene::Detach(int Index)
{
    // Slot 13: inner vtable+0x24 unbind. Not Attach(index, NULL).
    // CAMERA FUN_06f417e0: FUN_06f416b0 (drop child monitors) then Release.
    FThornEntity* Entity = EntityAt(Index);
    if (Entity == nullptr)
        return GR_GENERIC;
    Entity->InnerUnbindControls();
    Entity->EngineObj = nullptr;
    Entity->bBound = 0;
    Entity->BindState = 0;
    return GR_OK;
}


void FThornScene::PushBoundGeoFromScene(const FTransform& OldInverse, bool bOrient)
{
    // 3b530/3b6c0 retain each inner across both getters and setters.
    // Advance the live signed-key iterator only after that inner's callbacks.
    for (std::map<int, FThornEntity*>::iterator Entry = Entities_.begin();
         Entry != Entities_.end(); ++Entry)
    {
        FThornEntity* Entity = Entry->second;
        if (Entity == nullptr || Entity->GeoControl == nullptr)
            continue;
        float PositionWords[3];
        float OrientationWords[9];
        Entity->GeoControl->GetPosition(PositionWords);
        // Reload this retained inner's GeoControl after GetPosition; retail
        // performs no second null check and gets orientation before any math.
        if (bOrient)
            Entity->GeoControl->GetOrientation(OrientationWords);

        I3DMathEngine* Math = AcquireSceneCsysMathEngine();
        FVector Position;
        FVector PositionResult;
        std::memcpy(&Position, PositionWords, sizeof(Position));
        Math->TransformXform(PositionResult, OldInverse, Position);
        std::memcpy(PositionWords, &PositionResult, sizeof(PositionWords));
        Entity->InnerSetPos(PositionWords);

        if (bOrient)
        {
            // Reacquire the shared cache after InnerSetPos, then read the
            // stable orientation captured before that callback.
            Math = AcquireSceneCsysMathEngine();
            FMatrix Orientation;
            FMatrix OrientationResult;
            std::memcpy(&Orientation, OrientationWords, sizeof(Orientation));
            Math->MulMatrix(OrientationResult, OldInverse.Rotation, Orientation);
            std::memcpy(OrientationWords, &OrientationResult, sizeof(OrientationWords));
            Entity->InnerSetOrient(OrientationWords);
        }
    }
}

GENRESULT FThornScene::UserProp(void* Handle, const char* Key, char** Out)
{
    const int Index = static_cast<int>(reinterpret_cast<INT_PTR>(Handle));
    if (!EntityExists(Index))
        return GR_GENERIC;
    FThornUserProps::iterator Prop;
    {
        // 38e97 strlen and owning key construction occur only after entity gates.
        const std::string KeyOwner(Key, std::strlen(Key));
        Prop = EntityAt(Index)->UserProps->find(KeyOwner);
    }
    // Retail destroys the temporary key before the final entity/end reread.
    if (Prop == EntityAt(Index)->UserProps->end())
        return GR_GENERIC;
    *Out = Prop->second.data();
    return GR_OK;
}

GENRESULT FThornScene::SetUserProp(int Index, const char* Key, const char* Value)
{
    if (!EntityExists(Index))
        return GR_GENERIC;
    FThornUserProps::iterator Prop;
    {
        // 39035..3908f: own the lookup key, then destroy it after lookup.
        const std::string KeyOwner(Key, std::strlen(Key));
        Prop = EntityAt(Index)->UserProps->find(KeyOwner);
    }
    if (Prop != EntityAt(Index)->UserProps->end())
    {
        // 390ce/390d8: length before assignment; std::string supports self-alias.
        Prop->second.assign(Value, std::strlen(Value));
        return GR_OK;
    }
    // 390f3..391b4: value owner, key owner, pair copies, then unique insertion.
    const std::string ValueOwner(Value, std::strlen(Value));
    const std::string KeyOwner(Key, std::strlen(Key));
    const std::pair<std::string, std::string> Property(KeyOwner, ValueOwner);
    EntityAt(Index)->UserProps->insert(Property);
    return GR_OK;
}

GENRESULT FThornScene::AddEntityFromDesc(void* Desc, int* OutIndex)
{
    // LAB_06f3a3f0: write -1, construct completely, bind axes, publish, then
    // write the current index and increment the bound count (3a766..3a77e).
    *OutIndex = -1;
    const FThornEntitySnap* Snap = static_cast<const FThornEntitySnap*>(Desc);
    const int Type = Snap->Type;
    if (Type < 1 || Type > 13 || Type == 12)
        return GR_GENERIC;
    // Use the required current throwing allocator. The retail import's obsolete
    // null result and subsequent null payload/AV paths are not a modern CRT ABI.
    FThornEntity* Entity = new FThornEntity(Type, Snap, this);
    // 3a455/3a5ef/3a643..3a689: append after the typed constructor,
    // before owner bindings and before main entity-map publication.
    RegisterFrameEntity(Entity);
    BindEntitySceneAxes(Entity);
    // Retail clears the raw allocation unwind state before tree insertion.
    // No guessed delete/RAII ownership of a completed object is added here.
    // 3a6b4..3a761 unique signed tree insertion; collision keeps old payload.
    // Completed-object ownership is not guessed on insertion failure/collision.
    Entities_.insert(std::make_pair(EntityCount_, Entity));
    *OutIndex = EntityCount_;
    EntityCount_ = static_cast<int>(static_cast<unsigned int>(EntityCount_) + 1u);
    if (PlaybackState_ == 0)
        PlaybackState_ = kThornPlaybackLoaded;
    return GR_OK;
}

GENRESULT FThornScene::RemoveEntityAt(int Index)
{
    // FUN_06f3a7e0: erase events that bind this inner; dtor entity; map erase. Bound not decremented.
    if (!EntityExists(Index))
        return GR_GENERIC;
    // 3a834 keeps the selected node through all event/unbind callbacks.
    std::map<int, FThornEntity*>::iterator Entry = Entities_.find(Index);
    if (MasterCount_ != 0)
    {
        FThornEvent** EventSlot = Master_;
        do
        {
            FThornEvent* Event = *EventSlot;
            if (Event->PrimaryIndex == Index || Event->SecondaryIndex == Index)
                EventSlot = EraseMasterEventAt(EventSlot);
            else
                ++EventSlot;
        } while (EventSlot != Master_ + MasterCount_);
    }
    // 3a885..3a8c8 removes every pointer match before the entity callback/dtor.
    std::list<FThornEntity*>::iterator FrameIt = FrameEntities_->begin();
    while (FrameIt != FrameEntities_->end())
    {
        if (*FrameIt == Entry->second)
            FrameIt = FrameEntities_->erase(FrameIt);
        else
            ++FrameIt;
    }
    // 3a8d9 invokes the concrete inner+24 unbind BEFORE deleting-dtor/map erase.
    Entry->second->InnerUnbindControls();
    DestroyEntity(Entry->second);
    Entities_.erase(Entry);
    return GR_OK;
}

GENRESULT FThornScene::UpdateEntityFromDesc(int Index, void* Desc)
{
    // LAB_06f38bd0: lookup; inner vtable+0x08 apply-desc.
    if (!EntityExists(Index))
        return GR_GENERIC;
    ApplyEntitySnap(EntityAt(Index), static_cast<const FThornEntitySnap*>(Desc));
    return GR_OK;
}

GENRESULT FThornScene::AddEventFromDesc(const void* Desc, int* OutId)
{
    // LAB_06f39b50: *out=-1; primary in map; secondary in map unless <0 and table allows.
    *OutId = -1;
    const int Primary = ReadEventWord(Desc, 7);
    if (!EntityExists(Primary))
        return GR_GENERIC;
    const int Secondary = ReadEventWord(Desc, 8);
    if (Secondary < 0)
    {
        if (!EventAllowsMissingSecondary(ReadEventWord(Desc, 0)))
            return GR_GENERIC;
    }
    else if (!EntityExists(Secondary))
        return GR_GENERIC;

    // 39c5a rereads the type after validation. An unknown type returns
    // success with the initial -1 output; allocation uses our throwing CRT.
    const int Type = ReadEventWord(Desc, 0);
    if (Type < kThornEventTypeMin || Type > kThornEventTypeMax)
        return GR_OK;
    FThornEvent* Event = new FThornEvent(Type, Desc);

    BindEventSceneAxes(Event);
    // 39fe6 CMP / 39fe9 JA: scan past equal unsigned start-time keys.
    int Pos = 0;
    const unsigned int StartKey = static_cast<unsigned int>(Event->StartTimeMs);
    while (Pos < MasterCount_ &&
           static_cast<unsigned int>(Master_[Pos]->StartTimeMs) <= StartKey)
        ++Pos;

    // Retail drops its allocation unwind state at 39fa3, before growing
    // MASTER. Do not add an event owner that deletes it on insertion failure.
    if (MasterCount_ == MasterCap_)
    {
        // 3a012..3a047: grow by max(size, 1), without a minimum capacity.
        const unsigned int Count = Master_ != nullptr ? static_cast<unsigned int>(MasterCount_) : 0u;
        const unsigned int Growth = Count > 1u ? Count : 1u;
        const unsigned int CapacityBits = Count + Growth;
        const int NewCapacity = static_cast<int>(CapacityBits);
        const unsigned int AllocationCount = NewCapacity < 0 ? 0u : CapacityBits;
        FThornEvent** NewMaster = reinterpret_cast<FThornEvent**>(
            new unsigned char[AllocationCount * static_cast<unsigned int>(sizeof(FThornEvent*))]);
        for (int Index = 0; Index < Pos; ++Index)
            NewMaster[Index] = Master_[Index];
        NewMaster[Pos] = Event;
        for (int Index = Pos; Index < MasterCount_; ++Index)
            NewMaster[Index + 1] = Master_[Index];
        // 3a0ae precedes capacity, end and begin publication, in that order.
        delete[] reinterpret_cast<unsigned char*>(Master_);
        MasterCap_ = NewCapacity;
        MasterCount_ = static_cast<int>((Master_ != nullptr ? static_cast<unsigned int>(MasterCount_) : 0u) + 1u);
        Master_ = NewMaster;
    }
    else
    {
        // 3a0fc..3a1ca: append or shift the occupied tail backwards.
        for (int Index = MasterCount_; Index > Pos; --Index)
            Master_[Index] = Master_[Index - 1];
        Master_[Pos] = Event;
        MasterCount_ = static_cast<int>(static_cast<unsigned int>(MasterCount_) + 1u);
    }
    *OutId = Event->UniqueId;
    return GR_OK;
}

GENRESULT FThornScene::RemoveEventById(int Id)
{
    // LAB_06f3a330: linear find event+0xC; FUN_06f3a240 erase.
    for (int Index = 0; Index < MasterCount_; ++Index)
    {
        FThornEvent* Event = Master_[Index];
        if (Event->UniqueId != Id)
            continue;
        EraseMasterEventAt(Master_ + Index);
        return GR_OK;
    }
    return GR_GENERIC;
}

void* FThornScene::EventDescByIndex(int Index)
{
    if (Index < 0 || Index >= MasterCount_)
        return nullptr;
    FThornEvent* Event = Master_[Index];
    return Event->GetDesc();
}

void* FThornScene::EventDescById(int Id)
{
    for (int Index = 0; Index < MasterCount_; ++Index)
    {
        if (Master_[Index]->UniqueId == Id)
            return Master_[Index]->GetDesc();
    }
    return nullptr;
}

bool FThornScene::HasEventType(int Type) const
{
    for (int Index = 0; Index < MasterCount_; ++Index)
    {
        if (Master_[Index]->Type == Type)
            return true;
    }
    return false;
}

int FThornScene::CompareEventStart(const void* Left, const void* Right)
{
    const FThornEvent* EventLeft = *reinterpret_cast<FThornEvent* const*>(Left);
    const FThornEvent* EventRight = *reinterpret_cast<FThornEvent* const*>(Right);
    if (EventLeft->StartTimeMs < EventRight->StartTimeMs)
        return -1;
    if (EventLeft->StartTimeMs > EventRight->StartTimeMs)
        return 1;
    return 0;
}

void FThornScene::RebuildPending()
{
    // 39485/39672/39871 clear PENDING, then assign MASTER without sorting.
    PendingCount_ = 0;
    PendingBegin_ = 0;
    const int SourceCount = Master_ != nullptr ? MasterCount_ : 0;
    const int Capacity = Pending_ != nullptr ? PendingCap_ : 0;
    if (static_cast<unsigned int>(SourceCount) > static_cast<unsigned int>(Capacity))
    {
        // Retail releases old storage before rereading the allocation count.
        delete[] Pending_;
        const int LiveCount = Master_ != nullptr ? MasterCount_ : 0;
        const unsigned int AllocationCount = LiveCount < 0 ? 0u : static_cast<unsigned int>(LiveCount);
        Pending_ = new FThornEvent*[AllocationCount];
        FThornEvent** Read = Master_;
        FThornEvent** End = MasterCount_ == 0 ? Master_ : Master_ + MasterCount_;
        FThornEvent** Write = Pending_;
        for (; Read != End; ++Read, ++Write)
            *Write = *Read;
        PendingCount_ = static_cast<int>(Write - Pending_);
        PendingCap_ = PendingCount_;
        return;
    }
    FThornEvent** Read = Master_;
    FThornEvent** End = MasterCount_ == 0 ? Master_ : Master_ + MasterCount_;
    FThornEvent** Write = Pending_;
    for (; Read != End; ++Read, ++Write)
        *Write = *Read;
    PendingCount_ = Master_ != nullptr ? MasterCount_ : 0;
}

void FThornScene::StopAll()
{
    // 3b497..3b4c3 clears stop aliases before any virtual Stop callback.
    StopTimes_->clear();
    if (ActiveCount_ == 0)
        return;
    FThornEvent** ActiveBegin = Active_;
    FThornEvent** Successor = ActiveBegin + 1;
    do
    {
        // 3b4d6 retains Begin; 3b4e4 rereads the live end after Stop.
        (*ActiveBegin)->Stop(this);
        FThornEvent** ActiveEnd = Active_ + ActiveCount_;
        for (FThornEvent** Read = Successor; Read != ActiveEnd; ++Read)
            *(Read - 1) = *Read;
        ActiveCount_ = ActiveCount_ - 1;
    } while (ActiveBegin != Active_ + ActiveCount_);
    // Reallocation or recursive clear can invalidate the retail cursors too.
    // Do not invent a relocated cursor, iteration cap or post-Stop empty guard.

}

void FThornScene::CompactPendingFrom(FThornEvent** SelectedBegin, FThornEvent** SelectedEnd)
{
    // 3a9c8 reads the live end after callbacks, preserving the selected slots.
    FThornEvent** LiveEnd = Pending_ + PendingCount_;
    FThornEvent** Write = SelectedBegin;
    for (FThornEvent** Read = SelectedEnd; Read != LiveEnd; ++Read)
        *Write++ = *Read;
    PendingCount_ = static_cast<int>(Write - Pending_);
}

void FThornScene::DrainTimedStops(const int* TimeMs, bool bBeforeStartTime,
                                 std::set<int>* UpdatedTimes)
{
    std::list<float>::iterator Stop = StopTimes_->begin();
    if (bBeforeStartTime)
    {
        // 3b000..3b01d rereads the cached event's unsigned start each iteration.
        while (Stop != StopTimes_->end())
        {
            if (!(static_cast<double>(*Stop) <
                  static_cast<double>(static_cast<unsigned int>(*TimeMs))))
                break;
            UpdateActive(static_cast<int>(static_cast<unsigned int>(
                static_cast<long long>(*Stop))));
            // 3b02f reads this same live node again after the callbacks.
            UpdatedTimes->insert(static_cast<int>(static_cast<unsigned int>(
                static_cast<long long>(*Stop))));
            Stop = StopTimes_->erase(Stop);
        }
    }
    else if (Stop != StopTimes_->end())
    {
        // Tick 399ca/ce stores its signed clock to float once before draining.
        const float RoundedTime = static_cast<float>(*TimeMs);
        do
        {
            if (!(*Stop < RoundedTime))
                break;
            UpdateActive(static_cast<int>(static_cast<unsigned int>(
                static_cast<long long>(*Stop))));
            // The successor is read and the selected node freed AFTER UpdateActive.
            Stop = StopTimes_->erase(Stop);
        } while (Stop != StopTimes_->end());
    }
}

void FThornScene::DispatchWindow(int OldTime, int NewTime)
{
    // FUN_06f3a960 selects an inclusive unsigned window without erasing the prefix.
    int TakeFrom = 0;
    while (TakeFrom < PendingCount_ &&
           static_cast<unsigned int>(Pending_[TakeFrom]->StartTimeMs) <
               static_cast<unsigned int>(OldTime))
        ++TakeFrom;
    if (TakeFrom >= PendingCount_ ||
        static_cast<unsigned int>(Pending_[TakeFrom]->StartTimeMs) >
            static_cast<unsigned int>(NewTime))
        return;
    int TakeTo = TakeFrom;
    while (TakeTo < PendingCount_ &&
           static_cast<unsigned int>(Pending_[TakeTo]->StartTimeMs) <=
               static_cast<unsigned int>(NewTime))
        ++TakeTo;
    // 3a960 saves this range before 3af50 constructs its local set.
    FThornEvent** SelectedBegin = Pending_ + TakeFrom;
    FThornEvent** SelectedEnd = Pending_ + TakeTo;
    {
        // FUN_06f3af50's local set records each start time that was already updated.
        std::set<int> UpdatedTimes;
        for (FThornEvent** PendingSlot = SelectedBegin; PendingSlot != SelectedEnd; ++PendingSlot)
        {
            // 3afb0..3afdb: unique signed-key insertion of a null placeholder.
            // A miss is observable in the registry even though this event is skipped.
            const int PrimaryId = (*PendingSlot)->PrimaryIndex;
            const std::pair<std::map<int, FThornEntity*>::iterator, bool> Registration =
                Entities_.insert(std::make_pair(PrimaryId, static_cast<FThornEntity*>(nullptr)));
            if (Registration.first->second == nullptr)
                continue;
            // 3afe1 caches the event only after insertion and the payload gate.
            FThornEvent* Event = *PendingSlot;

            DrainTimedStops(&Event->StartTimeMs, true, &UpdatedTimes);
            if (UpdatedTimes.find(Event->StartTimeMs) == UpdatedTimes.end())
            {
                UpdateActive(Event->StartTimeMs);
                UpdatedTimes.insert(Event->StartTimeMs);
            }

            // The retail function rejects types outside 1..19 before invoking the
            // registered event handler.  FThornEvent::Start performs the same
            // dispatch, but retain the explicit gate here so the range walker has
            // the same no-start/no-registration side effect for bad descriptors.
            // 3b0c8 reloads Type from the selected slot after UpdateActive;
            // Start still receives the earlier cached event at 3b0e2.
            const int CurrentType = (*PendingSlot)->Type;
            if (CurrentType < kThornEventTypeMin || CurrentType > kThornEventTypeMax)
            {
                char Diagnostic[128];
                std::sprintf(Diagnostic,
                    "Start attempted on event with unknown event type %d.\n", CurrentType);
                FDUMP(0x100002, "%s(%d) : WARNING:General:%s",
                    "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\THORN\\Thorn.cpp",
                    0x5cf, Diagnostic);
            }
            else
            {
                const unsigned char Ok = Event->Start(this, ClockAfterDt_);
                if (Ok != 0)
                {
                    // 3b0fb..3b10f inserts the cached event into ACTIVE before
                    // reading its live EndTime; allocation failure is not skipped.
                    AppendActiveGroupEvent(Event);
                    if (Event->EndTimeMs >= 0)
                    {
                        const float StopTime = static_cast<float>(Event->EndTimeMs);
                        std::list<float>::iterator Stop = StopTimes_->begin();
                        while (Stop != StopTimes_->end())
                        {
                            // TEST AH,44 / JP advances unless ordered equal.
                            if (*Stop == StopTime)
                                Stop = StopTimes_->erase(Stop);
                            else
                                ++Stop;
                        }
                        StopTimes_->push_back(static_cast<float>(Event->EndTimeMs));
                        StopTimes_->sort();
                    }
                }
            }
            // Retail rebuilds after every live-primary event, including failed Start.
            RebuildActiveGroups();
        }
    }
    CompactPendingFrom(SelectedBegin, SelectedEnd);
}

void FThornScene::AppendActiveGroupEvent(FThornEvent* Event)
{
    // 3accb..3ae1d: append to the actual ACTIVE owner, publishing each event.
    if (ActiveCap_ - ActiveCount_ >= 1)
    {
        Active_[ActiveCount_] = Event;
        ++ActiveCount_;
        return;
    }

    FThornEvent** Position = ActiveCount_ == 0 ? Active_ : Active_ + ActiveCount_;
    const int Growth = Active_ != nullptr && static_cast<unsigned int>(ActiveCount_) > 1
                           ? ActiveCount_ : 1;
    const int Capacity = static_cast<int>(static_cast<unsigned int>(ActiveCount_) +
                                         static_cast<unsigned int>(Growth));
    const unsigned int AllocationCount = Capacity < 0 ? 0 : static_cast<unsigned int>(Capacity);
    std::unique_ptr<FThornEvent*[]> Storage(new FThornEvent*[AllocationCount]);
    FThornEvent** Write = Storage.get();
    // The old begin is read AFTER allocation, while Position was saved before.
    for (FThornEvent** Read = Active_; Read != Position; ++Read)
        *Write++ = *Read;
    *Write++ = Event;
    FThornEvent** End = ActiveCount_ == 0 ? Active_ : Active_ + ActiveCount_;
    for (FThornEvent** Read = Position; Read != End; ++Read)
        *Write++ = *Read;
    delete[] Active_;
    ActiveCap_ = Capacity;
    ++ActiveCount_;
    Active_ = Storage.release();
}

void FThornScene::EmitActiveGroup(std::map<int, FActiveGroup>& Groups,
                                 std::map<int, FActiveGroup>::iterator Group)
{
    // 3abec..3ac13: visited belongs to the group, and is set before sorting.
    if (Group->second.bVisited)
        return;
    std::vector<FThornEvent*>& Events = Group->second.Events;
    FThornEvent** Begin = Events.data();
    FThornEvent** End = Events.empty() ? Begin : Begin + Events.size();
    Group->second.bVisited = true;
    if (Events.size() <= 16)
        SortActiveEventTypes(Begin, End);
    else
    {
        PartitionActiveEventTypes(Begin, End);
        SortActiveEventTypes(Begin, Begin + 16);
        for (FThornEvent** Slot = Begin + 16; Slot != End; ++Slot)
            InsertActiveEventType(Slot, *Slot);
    }

    // 3ac74 retains Event across dependency recursion and output allocation.
    FThornEvent** Slot = Events.data();
    while (Slot != (Events.empty() ? Events.data() : Events.data() + Events.size()))
    {
        FThornEvent* Event = *Slot;
        if (Event->SecondaryIndex != -1)
        {
            std::map<int, FActiveGroup>::iterator Dependency =
                Groups.lower_bound(Event->SecondaryIndex);
            if (Dependency != Groups.end() && Event->SecondaryIndex >= Dependency->first)
                EmitActiveGroup(Groups, Dependency);
        }
        AppendActiveGroupEvent(Event);
        ++Slot;
    }
}

void FThornScene::RebuildActiveGroups()
{
    // 3aa00: local signed-primary map owns each vector and visited flag.
    std::map<int, FActiveGroup> Groups;
    FThornEvent** Slot = Active_;
    while (Slot != (ActiveCount_ == 0 ? Active_ : Active_ + ActiveCount_))
    {
        FThornEvent* Event = *Slot;
        const int Primary = Event->PrimaryIndex;
        const std::pair<std::map<int, FActiveGroup>::iterator, bool> Registration =
            Groups.insert(std::make_pair(Primary, FActiveGroup()));
        if (Registration.second)
            Registration.first->second.bVisited = false;
        Registration.first->second.Events.push_back(Event);
        ++Slot;
    }
    // 3aaff..3ab19 clears ACTIVE only after all group collection succeeds.
    ActiveCount_ = 0;
    for (std::map<int, FActiveGroup>::iterator Group = Groups.begin();
         Group != Groups.end(); ++Group)
        EmitActiveGroup(Groups, Group);
}

void FThornScene::FrameUpdateEntities(int TimeMs)
{
    // FUN_06f3b330: entity GetType; case 1|2 compound+0x18, case 10 IScriptControl+0x10,
    // case 0xD psys+0x14. dt = time РІв‚¬вЂ™ clock, then clock := time.
    // 3b3bb loads the live next node AFTER its callback: appends stay visible.
    std::list<FThornEntity*>::iterator FrameIt = FrameEntities_->begin();
    while (FrameIt != FrameEntities_->end())
    {
        FThornEntity* Entity = *FrameIt;
        const int Type = Entity->Snap.Type;
        if ((Type == kThornEntityCompound || Type == kThornEntityDeformable) &&
            Entity->CompoundControl != nullptr)
            Entity->CompoundControl->AdvanceTime(static_cast<int>(static_cast<unsigned int>(TimeMs) -
                            static_cast<unsigned int>(ClockMs_)));
        else if (Type == kThornEntitySubScene && Entity->ScriptControl != nullptr)
            Entity->ScriptControl->Slot4Tick(static_cast<int>(static_cast<unsigned int>(TimeMs) -
                            static_cast<unsigned int>(ClockMs_)));
        else if (Type == kThornEntityPsys && Entity->PsysControl != nullptr)
            Entity->PsysControl->Tick(static_cast<int>(static_cast<unsigned int>(TimeMs) -
                            static_cast<unsigned int>(ClockMs_)));
        ++FrameIt;
    }
    ClockMs_ = TimeMs;
}

void FThornScene::UpdateActive(int TimeMs)
{
    FrameUpdateEntities(TimeMs);
    if (ActiveCount_ != 0)
    {
        FThornEvent** ActiveSlot = Active_;
        FThornEvent** Successor = ActiveSlot + 1;
        do
        {
            if ((*ActiveSlot)->StillRunning(TimeMs, this) != 0)
            {
                ++ActiveSlot;
                ++Successor;
            }
            else
            {
                // 3b438 rereads the selected payload after StillRunning.
                (*ActiveSlot)->Stop(this);
                FThornEvent** ActiveEnd = Active_ + ActiveCount_;
                for (FThornEvent** Read = Successor; Read != ActiveEnd; ++Read)
                    *(Read - 1) = *Read;
                ActiveCount_ = ActiveCount_ - 1;
            }
        } while (ActiveSlot != Active_ + ActiveCount_);
    }
}

GENRESULT FThornScene::StartPlayback(int Arg)
{
    // LAB_06f39460: EAX = state-2; cases 2..7.
    const int State = PlaybackState_;
    if (State == kThornPlaybackLoaded || State == kThornPlaybackReset ||
        State == kThornPlaybackDone)
    {
        ClockMs_ = 0;
        RebuildPending();
    }
    else if (State == kThornPlaybackLoop)
    {
        RebuildPending();
    }
    else if (State == kThornPlaybackPaused)
    {
        PlaybackState_ = kThornPlaybackPlay;
        Tick(Arg);
        return GR_OK;
    }

    if (bLoop_ != 0)
    {
        const int Delta = static_cast<int>(static_cast<unsigned int>(Arg) -
                                           static_cast<unsigned int>(LoopCarryMs_));
        PlaybackState_ = kThornPlaybackLoop;
        Tick(Delta);
        return GR_OK;
    }
    PlaybackState_ = kThornPlaybackPlay;
    Tick(Arg);
    return GR_OK;
}

GENRESULT FThornScene::StopPlayback()
{
    ClockMs_ = 0;
    PendingCount_ = 0;
    PendingBegin_ = 0;
    RebuildPending();
    if (PlaybackState_ == kThornPlaybackLoop)
        LoopCarryMs_ = 0;
    StopAll();
    PlaybackState_ = kThornPlaybackReset;
    return GR_OK;
}

GENRESULT FThornScene::PausePlayback()
{
    // LAB_06f397f0: if this+0x2c РІв‚¬в‚¬ {6, 7} РІвЂ вЂ™ 5, EAX=0; else РІв‚¬вЂ™1.
    // Does not Stop, does not touch clock. Slot 26 still ticks state 5.
    if (PlaybackState_ == kThornPlaybackPlay || PlaybackState_ == kThornPlaybackLoop)
    {
        PlaybackState_ = kThornPlaybackPaused;
        return GR_OK;
    }
    return GR_GENERIC;
}

GENRESULT FThornScene::Tick(int DtMs)
{
    const int OldTime = ClockMs_;
    int NewTime = static_cast<int>(static_cast<unsigned int>(OldTime) +
                                   static_cast<unsigned int>(DtMs));

    if (DtMs < 0)
    {
        if (NewTime < 0)
            NewTime = 0;
        StopAll();
        ClockMs_ = 0;
        RebuildPending();
    }
    // 39992 reads the live clock after reset/allocation callbacks; 39997
    // publishes the new clock even when the unsigned backwards gate exits.
    const int WindowOld = ClockMs_;
    ClockAfterDt_ = NewTime;
    if (static_cast<unsigned int>(NewTime) < static_cast<unsigned int>(WindowOld))
    {
        return GR_OK;
    }

    const int State = PlaybackState_;
    if (State != kThornPlaybackPaused && State != kThornPlaybackPlay &&
        State != kThornPlaybackLoop)
        return GR_OK;

    DispatchWindow(WindowOld, NewTime);

    DrainTimedStops(&NewTime, false, nullptr);

    UpdateActive(NewTime);

    if (ClockMs_ != 0)
        FrameUpdateEntities(ClockMs_);

    if (static_cast<unsigned int>(NewTime) > static_cast<unsigned int>(DurationMs_))
    {
        if (bLoop_ != 0)
        {
            StopPlayback();
            const int Carry = ClockMs_;
            LoopCarryMs_ = Carry;
            StartPlayback(Carry);
            return GR_OK;
        }
        StopAll();
        PlaybackState_ = kThornPlaybackDone;
    }
    return GR_OK;
}

namespace
{
    static const char* const kEntityTypeNames[] = {
        "UNKNOWN_ENTITY", "COMPOUND",    "DEFORMABLE", "CAMERA",     "MONITOR",
        "LIGHT",          "SOUND",       "MARKER",     "HARDPOINT",  "SCENE",
        "SUB_SCENE",      "MOTION_PATH", "DELETED",    "PSYS"};
    // FUN_06f27bf0 indexes PTR_s_UNDEFINED_EVENT_06f5e090 directly.  Preserve
    // the retail spellings and ordering used by the serializer/parser ABI.
    static const char* const kEventTypeNames[] = {
        "UNDEFINED_EVENT",          "START_MOTION",
        "START_SOUND",              "START_IK",
        "START_PATH_ANIMATION",     "ATTACH_ENTITY",
        "START_LIGHT_PROP_ANIM",    "START_CAMERA_PROP_ANIM",
        "START_SPATIAL_PROP_ANIM",  "SET_CAMERA",
        "CONNECT_HARDPOINTS",       "START_SUB_SCENE",
        "USER_EVENT",               "START_PSYS",
        "START_PSYS_PROP_ANIM",     "START_AUDIO_PROP_ANIM",
        "START_FOG_PROP_ANIM",      "START_REVERB_PROP_ANIM",
        "START_FLR_HEIGHT_ANIM",    "SUBTITLE"};
    static const char* const kAxisNames[] = {"X_AXIS",     "Y_AXIS",     "Z_AXIS",
                                             "NEG_X_AXIS", "NEG_Y_AXIS", "NEG_Z_AXIS"};

    const char* EntityTypeName(int Type)
    {
        if (Type < 0 || Type > 13)
            return kEntityTypeNames[0];
        return kEntityTypeNames[Type];
    }

    const char* EventTypeName(int Type)
    {
        if (Type < 0 || Type > 19)
            return kEventTypeNames[0];
        // 06f2b1f1 searches the numeric IDs before indexing the registration names.
        static const int EventIds[] = {0, 10, 3, 11, 6, 8, 4, 5, 7, 2, 9, 12, 1,
                                        13, 14, 15, 16, 17, 18, 19};
        for (int Index = 0; Index < 20; ++Index)
        {
            if (EventIds[Index] == Type)
                return kEventTypeNames[Index];
        }
        return kEventTypeNames[0];
    }

    bool SpatialInSave(int Type)
    {
        return Type == 1 || Type == 2 || Type == 3 || Type == 5 || Type == 6 || Type == 7 ||
               Type == 9 || Type == 10 || Type == 11 || Type == 13;
    }

    // 2d2c0 / Unwind58640/58660: independently constructed FS owner.
    struct FThornSaveFileSystemOwner
    {
        IFileSystem* FileSystem;

        explicit FThornSaveFileSystemOwner(IFileSystem* InFileSystem)
            : FileSystem(InFileSystem)
        {
        }

        void Release()
        {
            if (FileSystem != nullptr)
            {
                FileSystem->Release();
                FileSystem = nullptr;
            }
        }

        ~FThornSaveFileSystemOwner() noexcept(false)
        {
            Release();
        }
    };

    struct FThornSaveBuf
    {
        FThornSaveFileSystemOwner FileSystemOwner;
        char* Data;
        int Len;
        int Cap;
        int StorageCapacity;

        explicit FThornSaveBuf(IFileSystem* InFileSystem, int Capacity = 512)
            : FileSystemOwner(InFileSystem), Data(nullptr), Len(0), Cap(Capacity),
              StorageCapacity(Capacity + 1)
        {
            // Retain before allocation; the owner releases on constructor unwind.
            if (FileSystemOwner.FileSystem != nullptr)
                FileSystemOwner.FileSystem->AddRef();
            Data = new (std::nothrow) char[StorageCapacity];
        }

        ~FThornSaveBuf() noexcept(false)
        {
            // 2ca00: no flush. Owner retries Release if this call throws;
            // a successful Release clears it before the owner's destructor.
            delete[] Data;
            FileSystemOwner.Release();
        }

        FThornSaveBuf(const FThornSaveBuf&) = delete;
        FThornSaveBuf& operator=(const FThornSaveBuf&) = delete;
    };

    int SaveBufFlush(FThornSaveBuf* Buf)
    {
        // 2ca80: one write; its return value is ignored. A short write still
        // discards the entire pending buffer, and is never retried.
        if (Buf->Len == 0)
            return 0;
        unsigned long Written = 0;
        Buf->FileSystemOwner.FileSystem->Write(0, Buf->Data, static_cast<unsigned long>(Buf->Len),
                               &Written, nullptr);
        const int Result = Written == static_cast<unsigned long>(Buf->Len) ? 0 : -1;
        Buf->Len = 0;
        return Result;
    }

    int SaveBufSetCapacity(FThornSaveBuf* Buf, int Capacity)
    {
        const int Result = SaveBufFlush(Buf);
        if (Result != 0)
            return Result;
        delete[] Buf->Data;
        Buf->Data = nullptr;
        Buf->Cap = Capacity;
        Buf->StorageCapacity = Capacity;
        Buf->Data = new (std::nothrow) char[static_cast<unsigned int>(Capacity)];
        return 0;
    }

    int SaveBufAppendText(FThornSaveBuf* Buf, const char* Text)
    {
        // Full FUN_06f2cda0: cache strlen before the single optional flush.
        const unsigned int Length = static_cast<unsigned int>(std::strlen(Text));
        int Result = 0;
        if (static_cast<unsigned int>(Buf->Len) + Length > static_cast<unsigned int>(Buf->Cap))
            Result = SaveBufFlush(Buf);

        // After the callback, read the current destination and copy forwards
        // through the current NUL, including it, even when flushing failed.
        char* Destination = Buf->Data + static_cast<unsigned int>(Buf->Len);
        const char* Cursor = Text;
        char Byte;
        do
        {
            Byte = *Cursor;
            ++Cursor;
            *Destination = Byte;
            ++Destination;
        }
        while (Byte != '\0');
        Buf->Len = static_cast<int>(static_cast<unsigned int>(Buf->Len) + Length);
        return Result;
    }

    // FUN_06f2cbb0: direct bounded formatting, flush, retry, then grow.
    int SaveBufAppendVariadic(FThornSaveBuf* Buf, const char* Format, ...)
    {
        va_list Args;
        va_list Attempt;
        va_start(Args, Format);
        va_copy(Attempt, Args);
        int Written = _vsnprintf(Buf->Data + static_cast<unsigned int>(Buf->Len),
            static_cast<unsigned int>(Buf->Cap) - static_cast<unsigned int>(Buf->Len),
            Format, Attempt);
        va_end(Attempt);
        if (Written < 0)
        {
            const int Result = SaveBufFlush(Buf);
            if (Result != 0)
            {
                va_end(Args);
                return Result;
            }
            va_copy(Attempt, Args);
            Written = _vsnprintf(Buf->Data + static_cast<unsigned int>(Buf->Len),
                static_cast<unsigned int>(Buf->Cap) - static_cast<unsigned int>(Buf->Len),
                Format, Attempt);
            va_end(Attempt);
            while (Written < 0)
            {
                // 2cc18 ignores the resize return; no retry bound or abort latch.
                SaveBufSetCapacity(Buf, static_cast<int>(static_cast<unsigned int>(Buf->Cap) * 2u));
                va_copy(Attempt, Args);
                Written = _vsnprintf(Buf->Data + static_cast<unsigned int>(Buf->Len),
                    static_cast<unsigned int>(Buf->Cap) - static_cast<unsigned int>(Buf->Len),
                    Format, Attempt);
                va_end(Attempt);
            }
        }
        va_end(Args);
        Buf->Len = static_cast<int>(static_cast<unsigned int>(Buf->Len) +
                                   static_cast<unsigned int>(Written));
        return 0;
    }

    // 2ae63/82/a1/c1 and2af15/34 promote each retained raw float once.
    int SaveBufAppendNamedFloat(FThornSaveBuf* Buf, const char* Format,
                                const char* Key, const float* Value)
    {
        const double Promoted = static_cast<double>(*Value);
        return SaveBufAppendVariadic(Buf, Format, Key, Promoted);
    }

    // FUN_06f2ce00 / 2ce80: one raw integer argument, fixed scratch.
    int SaveBufAppendInteger(FThornSaveBuf* Buf, const char* Format, int Value)
    {
        char Temporary[512];
        std::sprintf(Temporary, Format, Value);
        return SaveBufAppendText(Buf, Temporary);
    }

    int SaveBufAppendUnsigned(FThornSaveBuf* Buf, const char* Format, unsigned int Value)
    {
        char Temporary[512];
        std::sprintf(Temporary, Format, Value);
        return SaveBufAppendText(Buf, Temporary);
    }

    int SaveBufAppendLiteral(FThornSaveBuf* Buf, const char* Text)
    {
        char Temporary[512];
        std::sprintf(Temporary, "%s", Text);
        return SaveBufAppendText(Buf, Temporary);
    }

    // Keep source bits untouched until retail2ce40's first float load.
    int SaveBufAppendFloat(FThornSaveBuf* Buf, const char* Format, const float* Value)
    {
        char Temporary[512];
        std::sprintf(Temporary, Format, static_cast<double>(*Value));
        return SaveBufAppendText(Buf, Temporary);
    }

    // Writer2aa6b..2aa94: zero-extend milliseconds, then one float store.
    int SaveBufAppendUnsignedMilliseconds(FThornSaveBuf* Buf, const char* Format,
                                          unsigned int Milliseconds)
    {
        // Every uint32 value is exact in both signed int64 and double.
        const __int64 ExtendedMilliseconds = Milliseconds;
        static const double GMillisecondsToSeconds = 0.001;
        const float Seconds = static_cast<float>(
            static_cast<double>(ExtendedMilliseconds) * GMillisecondsToSeconds);
        return SaveBufAppendFloat(Buf, Format, &Seconds);
    }

    // Event duration callers: signed descriptor DWORD, exact double scale.
    int SaveBufAppendSignedMilliseconds(FThornSaveBuf* Buf, const char* Format,
                                        const int* Milliseconds)
    {
        static const double GMillisecondsToSeconds = 0.001;
        const float Seconds = static_cast<float>(
            static_cast<double>(*Milliseconds) * GMillisecondsToSeconds);
        return SaveBufAppendFloat(Buf, Format, &Seconds);
    }

    int SaveBufAppendRawFloat(FThornSaveBuf* Buf, const char* Format, const float* Value)
    {
        return SaveBufAppendFloat(Buf, Format, Value);
    }

    // Full FUN_06f2cf10 / FUN_06f2d080: reverse loads, direct double args.
    int SaveBufAppendVector(FThornSaveBuf* Buf, const char* Format, const float* Values)
    {
        char Temporary[512];
        const double Z = static_cast<double>(Values[2]);
        const double Y = static_cast<double>(Values[1]);
        const double X = static_cast<double>(Values[0]);
        std::sprintf(Temporary, Format, X, Y, Z);
        return SaveBufAppendText(Buf, Temporary);
    }

    // Full FUN_06f2cec0: integer reads [1], [2], [0], then sprintf.
    int SaveBufAppendColor(FThornSaveBuf* Buf, const char* Format, const int* Values)
    {
        char Temporary[512];
        const int Y = Values[1];
        const int Z = Values[2];
        const int X = Values[0];
        std::sprintf(Temporary, Format, X, Y, Z);
        return SaveBufAppendText(Buf, Temporary);
    }

    bool ShouldSaveNonzeroFloat(const float* Value)
    {
        return ThornPortableComparison::Nonzero(Value);
    }

    int SaveBufAppendPromotedFloatAsInteger(FThornSaveBuf* Buf, const char* Format, const float* Value)
    {
        // 2bf31 -> 2ce40: FLD float / FSTP double precedes sprintf("%d").
        // The integer conversion reads the first four bytes of that double.
        // Pass the recovered word with its proper type instead of variadic UB.
        const double PromotedValue = static_cast<double>(*Value);
        int IntegerWord;
        static_assert(sizeof(PromotedValue) == 8 && sizeof(IntegerWord) == 4,
                      "Thorn writer requires an eight-byte double and four-byte int");
        std::memcpy(&IntegerWord, &PromotedValue, sizeof(IntegerWord));
        return SaveBufAppendInteger(Buf, Format, IntegerWord);
    }

    int SaveBufAppendIntegerAsDouble(FThornSaveBuf* Buf, unsigned int IntegerWord,
                                    const FThornEntitySnap* SecondaryDesc)
    {
        static_assert(sizeof(SecondaryDesc) == 4, "Thorn writer requires Win32 pointers");
        const unsigned int Words[2] = {IntegerWord,
            static_cast<unsigned int>(reinterpret_cast<ULONG_PTR>(SecondaryDesc))};
        double Payload;
        std::memcpy(&Payload, Words, sizeof(Payload));
        char Temporary[512];
        // Native current-MSVC Win32 va_list reads the actual double object.
        // Passing it by value would add FLD64/FSTP64 before CRT consumption.
        va_list Args = reinterpret_cast<va_list>(&Payload);
        std::vsprintf(Temporary, "%.6f", Args);
        va_end(Args);
        return SaveBufAppendText(Buf, Temporary);
    }

    int SaveBufAppendStringValue(FThornSaveBuf* Buf, const char* Format, const char* Value)
    {
        // 2cc90: dynamic scalar scratch; format and value lengths are DWORDs.
        if (Value == nullptr)
            Value = "NULL";
        const unsigned int AllocationSize = static_cast<unsigned int>(std::strlen(Format)) +
                                            static_cast<unsigned int>(std::strlen(Value)) + 2u;
        char* Temporary = static_cast<char*>(::operator new(AllocationSize));
        std::sprintf(Temporary, Format, Value);
        const unsigned int Pending = static_cast<unsigned int>(Buf->Len);
        const unsigned int FormattedLength = static_cast<unsigned int>(std::strlen(Temporary));
        unsigned int Remaining = AllocationSize;
        char* Cursor = Temporary;
        int Result = 0;
        if (Pending + FormattedLength > static_cast<unsigned int>(Buf->Cap))
            Result = SaveBufFlush(Buf);

        // Remaining is allocation size only on the first test (2cd17).
        while (Remaining > static_cast<unsigned int>(Buf->Cap))
        {
            const unsigned int ChunkLength = static_cast<unsigned int>(Buf->Cap);
            char* Destination = Buf->Data;
            Buf->Len = static_cast<int>(ChunkLength);
            std::strncpy(Destination, Cursor, ChunkLength);
            Cursor += static_cast<unsigned int>(Buf->Len);
            Remaining = static_cast<unsigned int>(std::strlen(Cursor));
            Result = SaveBufFlush(Buf);
        }
        if (Remaining != 0)
        {
            const int Written = std::sprintf(Buf->Data + static_cast<unsigned int>(Buf->Len), "%s", Cursor);
            Buf->Len = static_cast<int>(static_cast<unsigned int>(Buf->Len) +
                                       static_cast<unsigned int>(Written));
        }
        ::operator delete(Temporary);
        return Result;
    }

    // Retail FUN_06f2c310.  Copy at most Capacity bytes, expanding the four
    // escaped control characters when there is room for both bytes.  The
    // serializer uses this helper for strings embedded in its Lua text.
    char* EscapeThornString(char* Destination, const char* Source, int Capacity)
    {
        // 2c317 tests the source, before touching the destination.
        if (Source == nullptr)
            return nullptr;
        // Actual retail producer passes a nonnull 1024-byte stack buffer.
        // Zero/negative capacity would write before/outside that buffer.
        if (Destination == nullptr || Capacity <= 0)
            return nullptr;

        char* Out = Destination;
        int Remaining = Capacity;
        const char* In = Source;
        while (*In != '\0' && Remaining > 0)
        {
            char Escape = '\0';
            if (*In == '\\')
                Escape = '\\';
            else if (*In == '\r')
                Escape = 'r';
            else if (*In == '\n')
                Escape = 'n';
            else if (*In == '\b')
                Escape = 'b';

            if (Escape != '\0' && Remaining > 2)
            {
                *Out++ = '\\';
                *Out++ = Escape;
                Remaining -= 2;
            }
            else
            {
                *Out++ = *In;
                --Remaining;
            }
            ++In;
        }
        if (Remaining != 0)
            *Out = '\0';
        else
            *(Out - 1) = '\0';
        return Destination;
    }

    // FUN_06f2cb00: fixed scratch; first token is the format itself.
    int SaveBufAppendStringFormat(FThornSaveBuf* Buf, const char* Token, char* Started)
    {
        char Temporary[512];
        if (*Started != '\0')
            std::sprintf(Temporary, " +%s", Token);
        else
            std::sprintf(Temporary, Token);
        *Started = '\x01'; // 2cb4c: publication follows the first formatting call.
        const unsigned int Pending = static_cast<unsigned int>(Buf->Len);
        const unsigned int Length = static_cast<unsigned int>(std::strlen(Temporary));
        int Result = 0;
        if (Pending + Length > static_cast<unsigned int>(Buf->Cap))
            Result = SaveBufFlush(Buf);
        const int Written = std::sprintf(Buf->Data + static_cast<unsigned int>(Buf->Len),
                                         "%s", Temporary);
        Buf->Len = static_cast<int>(static_cast<unsigned int>(Buf->Len) +
                                   static_cast<unsigned int>(Written));
        return Result;
    }

    const char* LightTypeName(int Type)
    {
        if (Type == 2)
            return "L_SPOT";
        if (Type == 3)
            return "L_DIRECT";
        return "L_POINT";
    }

    const char* FogModeName(int Mode)
    {
        if (Mode == 1)
            return "F_EXP";
        if (Mode == 2)
            return "F_EXP2";
        if (Mode == 3)
            return "F_LINEAR";
        return "F_NONE";
    }

    // Full104-instruction FUN_06f2c3e0: mask is captured as one byte;
    // each payload read follows its separate key-format callback.
    void SaveFogProps(FThornSaveBuf* Buf, const FThornFog8* Props, unsigned char Mask)
    {
        if ((Mask & 0x1) != 0)
        {
            SaveBufAppendStringValue(Buf, "%s = ", "fogon");
            SaveBufAppendStringValue(Buf, "%s, ", Props->On != 0 ? "Y" : "N");
        }
        if ((Mask & 0x2) != 0)
        {
            SaveBufAppendStringValue(Buf, "%s = ", "fogtable");
            SaveBufAppendStringValue(Buf, "%s, ", Props->Table != 0 ? "Y" : "N");
        }
        if ((Mask & 0x4) != 0)
        {
            SaveBufAppendStringValue(Buf, "%s = ", "fogcolor");
            SaveBufAppendColor(Buf, "{%d, %d, %d}, ", Props->Color);
        }
        if ((Mask & 0x8) != 0)
        {
            SaveBufAppendStringValue(Buf, "%s = ", "fogmode");
            SaveBufAppendStringValue(Buf, "%s, ", FogModeName(Props->Mode));
        }
        if ((Mask & 0x10) != 0)
        {
            SaveBufAppendStringValue(Buf, "%s = ", "fogstart");
            SaveBufAppendRawFloat(Buf, "%.6f, ", &Props->Start);
        }
        if ((Mask & 0x20) != 0)
        {
            SaveBufAppendStringValue(Buf, "%s = ", "fogend");
            SaveBufAppendRawFloat(Buf, "%.6f, ", &Props->End);
        }
        if ((Mask & 0x40) != 0)
        {
            SaveBufAppendStringValue(Buf, "%s = ", "fogdensity");
            SaveBufAppendRawFloat(Buf, "%.6f, ", &Props->Density);
        }
    }

    // FUN_06f2c8f0: keep the captured byte mask across writer callbacks,
    // while reading each payload only after its key has been written.
    void SaveReverbProps(FThornSaveBuf* Buf, const FThornReverb4* Props, unsigned char Mask)
    {
        if ((Mask & 0x1) != 0)
        {
            SaveBufAppendStringValue(Buf, "%s = ", "env");
            int Environment;
            std::memcpy(&Environment, &Props->Environment, sizeof(Environment));
            SaveBufAppendInteger(Buf, "%d, ", Environment);
        }
        if ((Mask & 0x2) != 0)
        {
            SaveBufAppendStringValue(Buf, "%s = ", "vol");
            SaveBufAppendRawFloat(Buf, "%.6f, ", &Props->Volume);
        }
        if ((Mask & 0x4) != 0)
        {
            SaveBufAppendStringValue(Buf, "%s = ", "decay");
            SaveBufAppendRawFloat(Buf, "%.6f, ", &Props->Decay);
        }
        if ((Mask & 0x8) != 0)
        {
            SaveBufAppendStringValue(Buf, "%s = ", "damping");
            SaveBufAppendRawFloat(Buf, "%.6f, ", &Props->Damping);
        }
    }

    const char* YnToken(int Value) { return (Value != 0) ? "Y" : "N"; }

    void SaveLightKey(FThornSaveBuf* Buf, const char* Prefix, const char* Key)
    {
        SaveBufAppendStringValue(Buf, "%s", Prefix);
        SaveBufAppendStringValue(Buf, "%s = ", Key);
    }

    int SaveLightAngle(FThornSaveBuf* Buf, const float* Value)
    {
        const unsigned int ScaleBits = 0x42652ee0u;
        float Scale;
        std::memcpy(&Scale, &ScaleBits, sizeof(Scale));
        const float Scaled = *Value * Scale;
        return SaveBufAppendFloat(Buf, "%.6f, ", &Scaled);
    }

    // Full FUN_06f2c560..06f2c8b8: each selected field has distinct
    // prefix, key, value and suffix calls. Both retail callers ignore EAX.
    void SaveLightProps(FThornSaveBuf* Buf, const char* Prefix, const char* Suffix,
                        const FThornLightProps* Props, unsigned int Mask)
    {
        if ((Mask & kThornLightBitOn) != 0)
        {
            SaveLightKey(Buf, Prefix, "on");
            SaveBufAppendStringValue(Buf, "%s, ", YnToken(Props->On));
            SaveBufAppendStringValue(Buf, "%s", Suffix);
        }
        if ((Mask & kThornLightBitColor) != 0)
        {
            SaveLightKey(Buf, Prefix, "color");
            SaveBufAppendColor(Buf, "{%d, %d, %d}, ", Props->Color);
            SaveBufAppendStringValue(Buf, "%s", Suffix);
        }
        if ((Mask & kThornLightBitDiffuse) != 0)
        {
            SaveLightKey(Buf, Prefix, "diffuse");
            SaveBufAppendVector(Buf, "{ %.6f, %.6f, %.6f }, ", Props->Diffuse);
            SaveBufAppendStringValue(Buf, "%s", Suffix);
        }
        if ((Mask & kThornLightBitSpecular) != 0)
        {
            SaveLightKey(Buf, Prefix, "specular");
            SaveBufAppendVector(Buf, "{ %.6f, %.6f, %.6f }, ", Props->Specular);
            SaveBufAppendStringValue(Buf, "%s", Suffix);
        }
        if ((Mask & kThornLightBitAmbient) != 0)
        {
            SaveLightKey(Buf, Prefix, "ambient");
            SaveBufAppendVector(Buf, "{ %.6f, %.6f, %.6f }, ", Props->Ambient);
            SaveBufAppendStringValue(Buf, "%s", Suffix);
        }
        if ((Mask & kThornLightBitDirection) != 0)
        {
            SaveLightKey(Buf, Prefix, "direction");
            SaveBufAppendVector(Buf, "{ %.6f, %.6f, %.6f }, ", Props->Direction);
            SaveBufAppendStringValue(Buf, "%s", Suffix);
        }
        if ((Mask & kThornLightBitRange) != 0)
        {
            SaveLightKey(Buf, Prefix, "range");
            SaveBufAppendRawFloat(Buf, "%.6f, ", &Props->Range);
            SaveBufAppendStringValue(Buf, "%s", Suffix);
        }
        if ((Mask & kThornLightBitCutoff) != 0)
        {
            SaveLightKey(Buf, Prefix, "cutoff");
            SaveLightAngle(Buf, &Props->Cutoff);
            SaveBufAppendStringValue(Buf, "%s", Suffix);
        }
        if ((Mask & kThornLightBitType) != 0)
        {
            SaveLightKey(Buf, Prefix, "type");
            SaveBufAppendStringValue(Buf, "%s, ", LightTypeName(Props->Type));
            SaveBufAppendStringValue(Buf, "%s", Suffix);
        }
        if ((Mask & kThornLightBitTheta) != 0)
        {
            SaveLightKey(Buf, Prefix, "theta");
            SaveLightAngle(Buf, &Props->Theta);
            SaveBufAppendStringValue(Buf, "%s", Suffix);
        }
        if ((Mask & kThornLightBitAtten) != 0)
        {
            // 2c858..2c877 snapshots these words before the prefix append.
            float Atten[3];
            const float* Source = Props->Atten;
            Atten[0] = Source[0];
            Atten[1] = Source[1];
            Atten[2] = Source[2];
            SaveLightKey(Buf, Prefix, "atten");
            SaveBufAppendVector(Buf, "{ %.10f, %.10f, %.10f }, ", Atten);
            SaveBufAppendStringValue(Buf, "%s", Suffix);
        }
    }

    int SaveParamCurve(FThornSaveBuf* Buf, IParamCurve* Curve)
    {
        // 2d120: stage the header before key callbacks and append it together
        // with the first point. Later points overwrite scratch from offset zero.
        const unsigned int Count = Curve->GetKeyCount();
        if (Count == 0)
            return -1;
        char Temporary[512];
        int HeaderLength = std::sprintf(Temporary,
            ", param_curve = {CLSID = \"%s\", points = {", Curve->Slot10Clsid());
        int Result = 0;
        for (unsigned int Index = 0; Index < Count; ++Index)
        {
            const int Id = Curve->Slot9IdByIndex(static_cast<int>(Index));
            if (Id == -1)
                continue;
            float Values[4];
            if (Curve->Slot5ReadKey(Id, &Values[0], &Values[1], &Values[2], &Values[3]) == 0)
                continue;
            char* PointOutput = Temporary + HeaderLength;
            const char* PointFormat = "{ %.6f, %.6f, %.6f, %.6f },";
            const double W = static_cast<double>(Values[3]);
            const double Z = static_cast<double>(Values[2]);
            const double Y = static_cast<double>(Values[1]);
            const double X = static_cast<double>(Values[0]);
            std::sprintf(PointOutput, PointFormat, X, Y, Z, W);
            const unsigned int Length = static_cast<unsigned int>(std::strlen(Temporary));
            const bool bFlush = static_cast<unsigned int>(Buf->Len) + Length >
                static_cast<unsigned int>(Buf->Cap);
            const int AppendResult = SaveBufAppendText(Buf, Temporary);
            if (bFlush)
                Result = AppendResult; // Last flush replaces earlier results.
            HeaderLength = 0;
        }
        std::sprintf(Temporary, "}}");
        const bool bFlush = static_cast<unsigned int>(Buf->Len) + 2u >
            static_cast<unsigned int>(Buf->Cap);
        const int AppendResult = SaveBufAppendText(Buf, Temporary);
        return bFlush ? AppendResult : Result;
    }

    void SaveEventParamCurve(FThornSaveBuf* Buf, const FThornEventDesc* Descriptor)
    {
        if (Descriptor->ParamCurve == nullptr)
            return;
        SaveParamCurve(Buf, Descriptor->ParamCurve);
        // 2c1bc reads the period AFTER all curve and flush callbacks.
        SaveBufAppendInteger(Buf, ", pcurve_period = %d", Descriptor->Field28);
    }

    // 2cf60 promotes the matrix by columns: 0,3,6,1,4,7,2,5,8.
    // A four-conversion format consumes only the first four arguments.
    int SaveMatrixValues(FThornSaveBuf* Buf, const char* Format, const float* Matrix)
    {
        char Temporary[512];
        // Explicit column order; argument evaluation cannot reorder source reads.
        const double M22 = static_cast<double>(Matrix[8]);
        const double M12 = static_cast<double>(Matrix[5]);
        // Retail keeps these seven float stores before argument promotion.
        const volatile float M02 = Matrix[2];
        const volatile float M21 = Matrix[7];
        const volatile float M11 = Matrix[4];
        const volatile float M01 = Matrix[1];
        const volatile float M20 = Matrix[6];
        const volatile float M10 = Matrix[3];
        const volatile float M00 = Matrix[0];
        // Publish each promoted argument before loading the next staged float.
        volatile double Arguments[9];
        Arguments[8] = M22;
        Arguments[7] = M12;
        Arguments[6] = static_cast<double>(M02);
        Arguments[5] = static_cast<double>(M21);
        Arguments[4] = static_cast<double>(M11);
        Arguments[3] = static_cast<double>(M01);
        Arguments[2] = static_cast<double>(M20);
        Arguments[1] = static_cast<double>(M10);
        Arguments[0] = static_cast<double>(M00);
        // Current x86 va_list reads these nine actual double objects in order.
        va_list Args = reinterpret_cast<va_list>(const_cast<double*>(Arguments));
        std::vsprintf(Temporary, Format, Args);
        va_end(Args);
        return SaveBufAppendText(Buf, Temporary);
    }

    void SaveUseScriptDuration(FThornSaveBuf* Buf, const FThornEventDesc* Descriptor)
    {
        if ((Descriptor->Flags & kThornFlagUseScriptDuration) != 0)
            SaveBufAppendLiteral(Buf, ", flags = USE_SCRIPT_DURATION");
    }

    void SavePathAttachFlags(FThornSaveBuf* Buf, const FThornEventDesc* Descriptor, bool bAttach)
    {
        if ((Descriptor->Flags & 0xf) == 0)
            return;
        SaveBufAppendLiteral(Buf, ", flags =");
        char Started = '\0';
        if ((Descriptor->Flags & kThornFlagUseScriptDuration) != 0)
            SaveBufAppendStringFormat(Buf, " USE_SCRIPT_DURATION", &Started);
        if ((Descriptor->Flags & kThornFlagPosition) != 0)
            SaveBufAppendStringFormat(Buf, " POSITION", &Started);
        if ((Descriptor->Flags & kThornFlagOrientation) != 0)
            SaveBufAppendStringFormat(Buf, " ORIENTATION", &Started);
        if ((Descriptor->Flags & kThornFlagLookAt) != 0)
            SaveBufAppendStringFormat(Buf, " LOOK_AT", &Started);
        if ((Descriptor->Flags & kThornFlagEntityRelative) != 0)
            SaveBufAppendStringFormat(Buf, " ENTITY_RELATIVE", &Started);
        if (bAttach && (Descriptor->Flags & kThornFlagOrientRelative) != 0)
            SaveBufAppendStringFormat(Buf, " ORIENTATION_RELATIVE", &Started);
        if (bAttach && (Descriptor->Flags & kThornFlagParentChild) != 0)
            SaveBufAppendStringFormat(Buf, " PARENT_CHILD", &Started);
    }


}

GENRESULT FThornScene::SaveToFileSystem(void* FileSystem, IThornComContext* ComContext,
                                       unsigned int SavedDurationMs)
{
    if (FileSystem == nullptr)
        return GR_GENERIC;

    FThornSaveBuf Buf(static_cast<IFileSystem*>(FileSystem));

    SaveBufAppendUnsignedMilliseconds(&Buf, "duration = %.3f;\r\n\r\n",
                                      SavedDurationMs);
    SaveBufAppendLiteral(&Buf, "entities =\r\n{\r\n");

    bool bWroteEntity = false;
    for (unsigned int Ordinal = 0; Ordinal < LiveEntityCount(); ++Ordinal)
    {
        const int Index = static_cast<int>(reinterpret_cast<INT_PTR>(
            EntityHandle(static_cast<int>(Ordinal))));
        if (Index == -1)
            continue;
        FThornEntitySnap* Entity = EntitySnap(Index);
        // 2aae9/2aaf5 capture both outputs before any entity text callbacks.
        // Their GENRESULT values are discarded by the retail writer.
        float Pos[3];
        float Orient[9];
        GetEntityPos(Index, Pos);
        GetEntityOrient(Index, Orient);
        if (Entity == nullptr)
            continue;
        const int RegisteredType = Entity->Type; // 2ab00 lookup precedes text callbacks.
        if (!bWroteEntity)
            SaveBufAppendLiteral(&Buf, "\r\n\t{\r\n");
        else
            SaveBufAppendLiteral(&Buf, ",\r\n\r\n\t{\r\n");
        bWroteEntity = true;

        const char* Name = Entity->Name;
        // 2ab40..2ab60: entity_name is raw; template_name below alone has
        // the separate escape producer at 2ab73..2ab81.
        SaveBufAppendStringValue(&Buf, "\t\tentity_name = \"%s\",\r\n", Name);
        SaveBufAppendStringValue(&Buf, "\t\ttype = %s,\r\n", EntityTypeName(RegisteredType));
        // 2ab65 reads the live template only after the name/type format callbacks.
        char TemplateScratch[1024];
        const char* TemplateName = "";
        const char* TemplateSource = Entity->TemplateName;
        if (TemplateSource != nullptr)
            TemplateName = EscapeThornString(TemplateScratch, TemplateSource,
                                             sizeof(TemplateScratch));
        SaveBufAppendStringValue(&Buf, "\t\ttemplate_name = \"%s\",\r\n", TemplateName);
        // 2aba6/2abb9/2abcc: distinct writes and late descriptor reads.
        SaveBufAppendInteger(&Buf, "\t\tlt_grp = %d,", Entity->LtGrp);
        SaveBufAppendInteger(&Buf, " srt_grp = %d,", static_cast<signed char>(Entity->SrtGrp));
        SaveBufAppendInteger(&Buf, " usr_flg = %d", Entity->UsrFlg);

        if (Entity->Flags != 0)
        {
            char FlagStarted = '\0';
            unsigned int KnownFlags = 0x11;
            SaveBufAppendLiteral(&Buf, ",\r\n\t\tflags =");
            if ((Entity->Flags & 1) != 0)
                SaveBufAppendStringFormat(&Buf, " REFERENCE", &FlagStarted);
            if ((Entity->Flags & 0x10) != 0)
                SaveBufAppendStringFormat(&Buf, " HIDDEN", &FlagStarted);
            const int Type = Entity->Type;
            if (Type == 1 || Type == 2 || Type == 13)
            {
                KnownFlags = 0x17;
                if ((Entity->Flags & 4) != 0)
                    SaveBufAppendStringFormat(&Buf, " LIT_DYNAMIC", &FlagStarted);
                if ((Entity->Flags & 2) != 0)
                    SaveBufAppendStringFormat(&Buf, " LIT_AMBIENT", &FlagStarted);
            }
            else if (Type == 6)
            {
                KnownFlags = 0x1f;
                if ((Entity->Flags & 2) != 0)
                    SaveBufAppendStringFormat(&Buf, " SPATIAL", &FlagStarted);
                if ((Entity->Flags & 8) != 0)
                    SaveBufAppendStringFormat(&Buf, " LOOP", &FlagStarted);
                if ((Entity->Flags & 4) != 0)
                    SaveBufAppendStringFormat(&Buf, " STREAM", &FlagStarted);
            }
            else if (Type == 9)
            {
                KnownFlags = 0x31;
                if ((Entity->Flags & 0x20) != 0)
                    SaveBufAppendStringFormat(&Buf, " FOG_PROPS_REMOVED", &FlagStarted);
            }
            const unsigned int UnknownFlags = static_cast<unsigned int>(Entity->Flags) & ~KnownFlags;
            if (UnknownFlags != 0)
            {
                FDUMP(0x100003, "%s(%d) : NOTICE:General:%s",
                      "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\THORN\\LuaScriptParser.cpp",
                      0xa91, "Unknown flag value detected on entity\n");
                if (FlagStarted)
                    SaveBufAppendInteger(&Buf, "+", 0);
                SaveBufAppendInteger(&Buf, " %d", static_cast<int>(
                    static_cast<unsigned int>(Entity->Flags) & ~KnownFlags));
            }
        }

        if (SpatialInSave(Entity->Type))
        {
            SaveBufAppendLiteral(&Buf, ",\r\n\t\tspatialprops =\r\n\t\t{\r\n");
            SaveBufAppendVector(&Buf, "\t\t\tpos = { %.6f, %.6f, %.6f },\r\n", Pos);
            SaveMatrixValues(&Buf,
                             "\t\t\torient = { { %.6f, %.6f, %.6f },{ %.6f, %.6f, %.6f },"
                             "{ %.6f, %.6f, %.6f } }\r\n", Orient);
            SaveBufAppendLiteral(&Buf, "\t\t}");
        }

        if (Entity->Type == 3)
        {
            SaveBufAppendLiteral(&Buf, ",\r\n\t\tcameraprops =\r\n\t\t{\r\n");
            SaveBufAppendRawFloat(&Buf, "\t\t\tfovh = %.6f,\r\n", &Entity->Fovh);
            SaveBufAppendRawFloat(&Buf, "\t\t\thvaspect = %.6f,\r\n", &Entity->Aspect);
            SaveBufAppendRawFloat(&Buf, "\t\t\tnearplane = %.6f,\r\n", &Entity->NearPlane);
            SaveBufAppendRawFloat(&Buf, "\t\t\tfarplane = %.6f\r\n", &Entity->FarPlane);
            SaveBufAppendLiteral(&Buf, "\t\t}");
        }
        else if (Entity->Type == 2)
        {
            SaveBufAppendLiteral(&Buf, ",\r\n\t\tcompoundprops =\r\n\t\t{\r\n");
            SaveBufAppendRawFloat(&Buf, "\t\t\tfloor_height = %.6f\r\n", &Entity->FloorHeight);
            SaveBufAppendLiteral(&Buf, "\t\t}");
        }
        else if (Entity->Type == 5)
        {
            SaveBufAppendLiteral(&Buf, ",\r\n\t\tlightprops =\r\n\t\t{\r\n");
            SaveLightProps(&Buf, "\t\t\t", "\r\n", &Entity->Light, 0xffffffffu);
            SaveBufAppendLiteral(&Buf, "\t\t}");
        }
        else if (Entity->Type == 6)
        {
            // All8 retail2cbb0 callers: key+value stay in one formatting attempt.
            SaveBufAppendLiteral(&Buf, ",\r\n\t\taudioprops =\r\n\t\t{\r\n");
            SaveBufAppendNamedFloat(&Buf, "\t\t\t%s = %.6f,\r\n", "attenuation", &Entity->Audio[0]);
            SaveBufAppendNamedFloat(&Buf, "\t\t\t%s = %.6f,\r\n", "pan", &Entity->Audio[1]);
            SaveBufAppendNamedFloat(&Buf, "\t\t\t%s = %.6f,\r\n", "dmin", &Entity->Audio[2]);
            SaveBufAppendNamedFloat(&Buf, "\t\t\t%s = %.6f,\r\n", "dmax", &Entity->Audio[3]);
            SaveBufAppendVariadic(&Buf, "\t\t\t%s = %u,\r\n", "ain", BitsAsUInt(&Entity->Audio[4]));
            SaveBufAppendVariadic(&Buf, "\t\t\t%s = %u,\r\n", "aout", BitsAsUInt(&Entity->Audio[5]));
            SaveBufAppendNamedFloat(&Buf, "\t\t\t%s = %.6f,\r\n", "atout", &Entity->Audio[6]);
            SaveBufAppendNamedFloat(&Buf, "\t\t\t%s = %.6f,\r\n", "rmix", &Entity->Audio[7]);
            SaveBufAppendLiteral(&Buf, "\t\t}");
        }
        else if (Entity->Type == 9 || Entity->Type == 10)
        {
            SaveBufAppendStringValue(&Buf, ",\r\n\t\tup = %s", kAxisNames[Entity->SceneBlob[5]]);
            SaveBufAppendStringValue(&Buf, ",\r\n\t\tfront = %s", kAxisNames[Entity->SceneBlob[6]]);
            // Signed/unsigned corresponding types may alias the same DWORDs.
            SaveBufAppendColor(&Buf, ",\r\n\t\tambient = {%d,%d,%d}",
                               reinterpret_cast<const int*>(Entity->SceneBlob));
        }
        else if (Entity->Type == 11)
        {
            SaveBufAppendLiteral(&Buf, ",\r\n\t\tpathprops =\r\n\t\t{\r\n");
            SaveBufAppendStringValue(&Buf, "\r\n\t\t\tpath_type = \"%s\"", Entity->MotionPath.PathType);
            IPersistString* Persist = Entity->MotionPath.MotionPath;
            if (Persist != nullptr)
            {
                const unsigned long Size = Persist->Slot5EstimateSize();
                if (Size != 0)
                {
                    char* Data = new char[Size + 1u];
                    // 2b00e rereads the descriptor after Slot5 and allocation.
                    // Preserve Size while selecting the current receiver/vtable.
                    Entity->MotionPath.MotionPath->Slot4Save(Data, Size);
                    SaveBufAppendStringValue(&Buf, ",\r\n\t\t\tpath_data = \"%s\"\r\n", Data);
                    delete[] Data;
                }
            }
            SaveBufAppendLiteral(&Buf, "\t\t}");
        }
        else if (Entity->Type == 13)
        {
            SaveBufAppendLiteral(&Buf, ",\r\n\t\tpsysprops =\r\n\t\t{\r\n");
            SaveBufAppendRawFloat(&Buf, "\t\t\tsparam = %.6f\r\n", &Entity->PsysSparam);
            SaveBufAppendLiteral(&Buf, "\t\t}");
        }

        const unsigned long PropCount = UserPropCount(Index);
        if (PropCount > 0)
        {
            SaveBufAppendLiteral(&Buf, ",\r\n\t\tuserprops =\r\n\t\t{\r\n");
            for (unsigned long Prop = 0; Prop < PropCount; ++Prop)
            {
                char* Value = nullptr;
                const char* Key = UserPropKey(Index, static_cast<int>(Prop));
                if (Key == nullptr)
                    continue;
                if (UserProp(reinterpret_cast<void*>(static_cast<INT_PTR>(Index)), Key, &Value) != GR_OK)
                    continue;
                SaveBufAppendStringValue(&Buf, "\t\t\t%s = ", Key);
                SaveBufAppendStringValue(&Buf, "\"%s\",\r\n", Value);
            }
            SaveBufAppendLiteral(&Buf, "\t\t}");
        }

        SaveBufAppendLiteral(&Buf, "\r\n\t}");
    }

    SaveBufAppendLiteral(&Buf, "\r\n};\r\n");
    SaveBufAppendLiteral(&Buf, "\r\nevents =\r\n{");

    for (unsigned int Index = 0; Index < static_cast<unsigned int>(MasterCount_); ++Index)
    {
        // 2b1c1 retains the selected packet through every formatting callback.
        FThornEventDesc* Descriptor = static_cast<FThornEventDesc*>(
            EventDescByIndex(static_cast<int>(Index)));
        FThornEntitySnap* Primary = EntitySnap(Descriptor->PrimaryIndex);
        FThornEntitySnap* Secondary = Descriptor->SecondaryIndex >= 0
            ? EntitySnap(Descriptor->SecondaryIndex) : nullptr;
        // 2b1ec..2b269 selects the type name before any output callbacks.
        const char* EventName = EventTypeName(Descriptor->Type);
        SaveBufAppendStringValue(&Buf, "%s\r\n\t", Index == 0 ? "" : ",");
        SaveBufAppendUnsignedMilliseconds(&Buf, "{%.3f",
                                          static_cast<unsigned int>(Descriptor->StartMs));
        SaveBufAppendStringValue(&Buf, ", %s", EventName);
        SaveBufAppendStringValue(&Buf, ", {\"%s\"", Primary->Name);
        if (Secondary != nullptr)
            SaveBufAppendStringValue(&Buf, ", \"%s\"}", Secondary->Name);
        else
            SaveBufAppendLiteral(&Buf, "}");

        const int EvType = Descriptor->Type;
        if (EvType == kThornTypeSetCamera || EvType < kThornEventTypeMin ||
            EvType > kThornEventTypeMax)
        {
            SaveBufAppendLiteral(&Buf, "}");
            continue;
        }
        if (EvType == kThornTypeUserEvent)
        {
            SaveBufAppendLiteral(&Buf, ",  {");
            SaveBufAppendSignedMilliseconds(&Buf, "duration = %.3f", &Descriptor->DurationMs);
        }
        else if (EvType != kThornTypeStartMotion)
            SaveBufAppendSignedMilliseconds(&Buf, ", {duration = %.3f", &Descriptor->DurationMs);

        if (EvType == kThornTypeUserEvent)
        {
            if (Descriptor->EventFlags != 0)
                SaveBufAppendInteger(&Buf, ", event_flags = %d", Descriptor->EventFlags);
            // 2b4ce..2b525: size+1 scalar allocation; Slot4 receives Size too.
            // Delete is on the normal path, including a nonzero Slot4 HRESULT.
            if (ComContext != nullptr)
            {
                FThornUserEventDesc* UserDescriptor = reinterpret_cast<FThornUserEventDesc*>(Descriptor);
                const unsigned int Size = ComContext->Slot5(UserDescriptor);
                if (Size != 0)
                {
                    char* Buffer = static_cast<char*>(::operator new(Size + 1u));
                    const GENRESULT Hr = ComContext->Slot4(UserDescriptor, Buffer, Size);
                    if (Hr == GR_OK)
                        SaveBufAppendStringValue(&Buf, ", user_event_string = \"%s\"", Buffer);
                    ::operator delete(Buffer);
                }
            }
        }
        else if (EvType == kThornTypeStartSound)
        {
            const FThornSoundEventDesc* Packet = reinterpret_cast<const FThornSoundEventDesc*>(Descriptor);
            if (Packet->SoundStartTime != 0)
                SaveBufAppendInteger(&Buf, ", start_time = %d", Packet->SoundStartTime);
            if ((Descriptor->Flags & 0xf) != 0)
            {
                SaveBufAppendLiteral(&Buf, ", flags =");
                char Started = '\0';
                if ((Descriptor->Flags & kThornFlagUseScriptDuration) != 0)
                    SaveBufAppendStringFormat(&Buf, " USE_SCRIPT_DURATION", &Started);
                if ((Descriptor->Flags & 2) != 0)
                    SaveBufAppendStringFormat(&Buf, " SPATIAL", &Started);
                if ((Descriptor->Flags & 8) != 0)
                    SaveBufAppendStringFormat(&Buf, " LOOP", &Started);
                if ((Descriptor->Flags & 4) != 0)
                    SaveBufAppendStringFormat(&Buf, " STREAM", &Started);
            }
        }
        else if (EvType == kThornTypeLightPropAnim)
        {
            const FThornLightEventDesc* Packet = reinterpret_cast<const FThornLightEventDesc*>(Descriptor);
            SaveBufAppendLiteral(&Buf, ", lightprops = {");
            SaveLightProps(&Buf, "", "", &Packet->Target,
                           static_cast<unsigned int>(Packet->Mask));
            SaveBufAppendLiteral(&Buf, "}");
            SaveUseScriptDuration(&Buf, Descriptor);
            SaveEventParamCurve(&Buf, Descriptor);
        }
        else if (EvType == kThornTypeCameraPropAnim)
        {
            const FThornCameraEventDesc* Packet = reinterpret_cast<const FThornCameraEventDesc*>(Descriptor);
            static const char* const CameraKeys[] = {"fovh", "aspect", "near", "far"};
            static const int CameraBits[] = {
                kThornCamBitFovh, kThornCamBitAspect, kThornCamBitNear, kThornCamBitFar};
            SaveBufAppendLiteral(&Buf, ", cameraprops = {");
            bool bWroteCameraField = false;
            for (int Camera = 0; Camera < 4; ++Camera)
            {
                if ((Packet->Mask & CameraBits[Camera]) == 0)
                    continue;
                if (bWroteCameraField)
                    SaveBufAppendLiteral(&Buf, ", ");
                SaveBufAppendStringValue(&Buf, "%s = ", CameraKeys[Camera]);
                SaveBufAppendRawFloat(&Buf, "%.6f", &Packet->Target[Camera]);
                bWroteCameraField = true;
            }
            SaveBufAppendLiteral(&Buf, "}");
            SaveUseScriptDuration(&Buf, Descriptor);
            if (Descriptor->EventFlags != 0)
                SaveBufAppendInteger(&Buf, ", event_flags = %d", Descriptor->EventFlags);
            SaveEventParamCurve(&Buf, Descriptor);
        }
        else if (EvType == kThornTypePathAnim)
        {
            const FThornPathEventDesc* Packet = static_cast<const FThornPathEventDesc*>(
                static_cast<const void*>(Descriptor));
            // 2b86e/2b880 are separate scalar calls; reread stop after the first append.
            SaveBufAppendRawFloat(&Buf, ", start_percent = %.6f", &Packet->StartPercent);
            SaveBufAppendRawFloat(&Buf, ", stop_percent = %.6f", &Packet->StopPercent);
            SaveBufAppendVector(&Buf, ", offset = { %.6f, %.6f, %.6f }", Packet->Offset);
            // Separate string calls preserve the late front-axis read.
            SaveBufAppendStringValue(&Buf, ", up = %s", kAxisNames[Packet->LookAtUpAxis]);
            SaveBufAppendStringValue(&Buf, ", front = %s", kAxisNames[Packet->LookAtFrontAxis]);
            SavePathAttachFlags(&Buf, Descriptor, false);
            if (Descriptor->EventFlags != 0)
                SaveBufAppendInteger(&Buf, ", event_flags = %d", Descriptor->EventFlags);
            SaveEventParamCurve(&Buf, Descriptor);
        }
        else if (EvType == kThornTypeSpatialPropAnim)
        {
            const FThornSpatialEventDesc* Packet = reinterpret_cast<const FThornSpatialEventDesc*>(Descriptor);
            if (Packet->TargetPart != nullptr)
                SaveBufAppendStringValue(&Buf, ", target_part = \"%s\"", Packet->TargetPart);
            SaveBufAppendLiteral(&Buf, Packet->TargetType == 1 ? ", target_type = HARDPOINT"
                : Packet->TargetType == 2 ? ", target_type = PART" : ", target_type = ROOT");
            SaveBufAppendLiteral(&Buf, ", spatialprops = {");
            // 2bb74 clears BL; only the position branch sets it (2bbc0).
            // Orientation does not set it before the axisrot comma test.
            const bool bWrotePosition = (Packet->Mask & kThornSpatialBitPos) != 0;
            if (bWrotePosition)
            {
                // 2bb81..2bba4 captures all three raw words before the key callback.
                float Position[3];
                std::memcpy(Position, Packet->Position, sizeof(Position));
                SaveBufAppendStringValue(&Buf, "%s = ", "pos");
                SaveBufAppendVector(&Buf, "{ %.6f, %.6f, %.6f }", Position);
            }
            if ((Packet->Mask & kThornSpatialBitOrient) != 0)
            {
                if (bWrotePosition)
                    SaveBufAppendLiteral(&Buf, ", ");
                if ((Descriptor->Flags & kThornFlagQuatOrient) != 0)
                {
                    // 2bbff calls math +0x54, then formats the resulting
                    // matrix with q_orient's four conversions at 2bc07.
                    if (G_SpatialWriterMathEngine == nullptr)
                        G_SpatialWriterMathEngine = ThornAcquire3DMathEngine();
                    FQuaternion Quaternion;
                    // Raw W/X/Y/Z input, read after the lazy factory callback.
                    std::memcpy(&Quaternion, Packet->Quaternion, sizeof(Quaternion));
                    FMatrix Orientation;
                    G_SpatialWriterMathEngine->QuaternionToMatrix(Orientation, Quaternion);
                    float Matrix[9];
                    std::memcpy(Matrix, Orientation.D, sizeof(Matrix));
                    SaveMatrixValues(&Buf, "q_orient = { %.6f, %.6f, %.6f, %.6f }", Matrix);
                }
                else
                {
                    // 2bc0e..2bc1a snapshots nine words before entering the formatter.
                    float Matrix[9];
                    std::memcpy(Matrix, Packet->Orient, sizeof(Matrix));
                    SaveMatrixValues(&Buf,
                                     "orient = { { %.6f, %.6f, %.6f },{ %.6f, %.6f, %.6f },"
                                     "{ %.6f, %.6f, %.6f } }", Matrix);
                }
            }
            if ((Packet->Mask & kThornSpatialBitAxisRot) != 0)
            {
                if (bWrotePosition)
                    SaveBufAppendLiteral(&Buf, ", ");
                SaveBufAppendStringValue(&Buf, "%s = ", "axisrot");
                SaveBufAppendRawFloat(&Buf, "{%.6f, ", &Packet->AxisRotAngle);
                SaveBufAppendStringValue(&Buf, "%s}", kAxisNames[Packet->AxisRotAxis]);
            }
            SaveBufAppendLiteral(&Buf, "}");
            SaveUseScriptDuration(&Buf, Descriptor);
            if (Descriptor->EventFlags != 0)
                SaveBufAppendInteger(&Buf, ", event_flags = %d", Descriptor->EventFlags);
            SaveEventParamCurve(&Buf, Descriptor);
        }
        else if (EvType == kThornTypeAttachEntity)
        {
            const FThornAttachEventDesc* Packet = reinterpret_cast<const FThornAttachEventDesc*>(Descriptor);
            SaveBufAppendVector(&Buf, ", offset = { %.6f, %.6f, %.6f }", Packet->Offset);
            // Separate string calls preserve the late front-axis read.
            SaveBufAppendStringValue(&Buf, ", up = %s", kAxisNames[Packet->UpAxis]);
            SaveBufAppendStringValue(&Buf, ", front = %s", kAxisNames[Packet->FrontAxis]);
            if (Packet->TargetPart != nullptr)
                SaveBufAppendStringValue(&Buf, ", target_part = \"%s\"", Packet->TargetPart);
            SaveBufAppendLiteral(&Buf, Packet->TargetType == 1 ? ", target_type = HARDPOINT"
                : Packet->TargetType == 2 ? ", target_type = PART" : ", target_type = ROOT");
            SavePathAttachFlags(&Buf, Descriptor, true);
        }
        else if (EvType == kThornTypeConnectHardpoints)
        {
            const FThornConnectEventDesc* Packet = reinterpret_cast<const FThornConnectEventDesc*>(Descriptor);
            SaveBufAppendStringValue(&Buf, ", hardpoint = \"%s\"", Packet->Hardpoint);
            SaveBufAppendStringValue(&Buf, ", parent_hardpoint = \"%s\"", Packet->ParentHardpoint);
            SaveUseScriptDuration(&Buf, Descriptor);
        }
        else if (EvType == kThornTypeStartMotion)
        {
            const FThornMotionEventDesc* Packet = reinterpret_cast<const FThornMotionEventDesc*>(Descriptor);
            if (Packet->Animation != nullptr)
                SaveBufAppendStringValue(&Buf, ", {animation = \"%s\"", Packet->Animation);
            if (Packet->LockedBone != nullptr)
                SaveBufAppendStringValue(&Buf, ", locked_bone = \"%s\"", Packet->LockedBone);
            SaveBufAppendSignedMilliseconds(&Buf, ", duration = %.3f", &Descriptor->DurationMs);
            if (ShouldSaveNonzeroFloat(&Packet->StartTime))
                SaveBufAppendRawFloat(&Buf, ", start_time = %.3f", &Packet->StartTime);
            if (ShouldSaveNonzeroFloat(&Packet->TransTime))
                SaveBufAppendRawFloat(&Buf, ", trans_time = %.3f", &Packet->TransTime);
            if (ShouldSaveNonzeroFloat(&Packet->TimeScale))
                SaveBufAppendRawFloat(&Buf, ", time_scale = %.6f", &Packet->TimeScale);
            if (ShouldSaveNonzeroFloat(&Packet->Weight))
                SaveBufAppendRawFloat(&Buf, ", weight = %.6f", &Packet->Weight);
            // 2b385 tests the captured entity descriptor, after weight output.
            if (Primary->Type == 2)
                SaveBufAppendRawFloat(&Buf, ", heading = %.6f", &Packet->Heading);
            else if (ShouldSaveNonzeroFloat(&Packet->TransScale))
                SaveBufAppendRawFloat(&Buf, ", trans_scale = %.6f", &Packet->TransScale);
            SaveUseScriptDuration(&Buf, Descriptor);
        }
        else if (EvType == kThornTypeStartIk)
        {
            const FThornStartIkDesc* Packet = reinterpret_cast<const FThornStartIkDesc*>(Descriptor);
            SaveBufAppendStringValue(&Buf, ", end_effector = \"%s\"", Packet->EndEffector);
            SaveBufAppendInteger(&Buf, ", count_to_root = %d", Packet->CountToRoot);
            if (Packet->TargetPart != nullptr)
                SaveBufAppendStringValue(&Buf, ", target_part = \"%s\"", Packet->TargetPart);
            SaveBufAppendLiteral(&Buf, Packet->TargetType == 1 ? ", target_type = HARDPOINT"
                : Packet->TargetType == 2 ? ", target_type = PART" : ", target_type = ROOT");
            SaveBufAppendRawFloat(&Buf, ", damping = %.6f", &Packet->Damping);
            // Separate string calls preserve the late front-axis read.
            SaveBufAppendStringValue(&Buf, ", up = %s", kAxisNames[Packet->LookAtUpAxis]);
            SaveBufAppendStringValue(&Buf, ", front = %s", kAxisNames[Packet->LookAtFrontAxis]);
            SaveBufAppendInteger(&Buf, ", point_at = %d", static_cast<int>(Packet->bPointAt));
            SaveBufAppendInteger(&Buf, ", move_to = %d", static_cast<int>(Packet->bMoveTo));
            SaveBufAppendRawFloat(&Buf, ", transition_duration = %.3f", &Packet->TransitionDuration);
            SaveUseScriptDuration(&Buf, Descriptor);
        }
        else if (EvType == kThornTypeStartSubScene)
        {
            const FThornSubSceneEventDesc* Packet = reinterpret_cast<const FThornSubSceneEventDesc*>(Descriptor);
            if (ShouldSaveNonzeroFloat(&Packet->StartTime))
                SaveBufAppendPromotedFloatAsInteger(&Buf, ", start_time = %d", &Packet->StartTime);
            // 2bf36/2bf4a independently reread flags around the prefix append.
            if ((Descriptor->Flags & kThornFlagUseScriptDuration) != 0)
            {
                SaveBufAppendLiteral(&Buf, ", flags =");
                char Started = '\0';
                if ((Descriptor->Flags & kThornFlagUseScriptDuration) != 0)
                    SaveBufAppendStringFormat(&Buf, " USE_SCRIPT_DURATION", &Started);
            }
        }
        else if (EvType == kThornTypePsysPropAnim)
        {
            const FThornPsysPropEventDesc* Packet = reinterpret_cast<const FThornPsysPropEventDesc*>(Descriptor);
            if (Descriptor->EventFlags != 0)
                SaveBufAppendInteger(&Buf, ", event_flags = %d", Descriptor->EventFlags);
            SaveBufAppendLiteral(&Buf, ", psysprops = {");
            if ((Packet->Mask & 1) != 0)
            {
                SaveBufAppendStringValue(&Buf, "%s = ", "sparam");
                SaveBufAppendRawFloat(&Buf, "%.6f", &Packet->Sparam);
            }
            SaveBufAppendLiteral(&Buf, "}");
            SaveEventParamCurve(&Buf, Descriptor);
        }
        else if (EvType == kThornTypeAudioPropAnim)
        {
            const FThornAudioPropEventDesc* Packet = reinterpret_cast<const FThornAudioPropEventDesc*>(Descriptor);
            static const char* const AudioKeys[] = {
                "attenuation", "pan", "dmin", "dmax", "ain", "aout", "atout", "rmix"};
            static const int AudioBits[] = {0x1, 0x2, 0x4, 0x8, 0x10, 0x20, 0x40, 0x80};
            SaveBufAppendLiteral(&Buf, ", audioprops = {");
            bool bWroteAudioField = false;
            for (int Audio = 0; Audio < 8; ++Audio)
            {
                if ((Packet->Mask & AudioBits[Audio]) == 0)
                    continue;
                if (bWroteAudioField)
                    SaveBufAppendLiteral(&Buf, ", ");
                SaveBufAppendStringValue(&Buf, "%s = ", AudioKeys[Audio]);
                if (Audio == 4 || Audio == 5)
                {
                    const FThornEntitySnap* SecondaryDesc = Secondary != nullptr
                        ? Secondary : nullptr;
                    SaveBufAppendIntegerAsDouble(&Buf, static_cast<unsigned int>(Audio == 4 ? Packet->Target.Ain : Packet->Target.Aout),
                                                 SecondaryDesc);
                }
                else
                {
                    const float* Fields[8] = {&Packet->Target.Attenuation, &Packet->Target.Pan,
                        &Packet->Target.Dmin, &Packet->Target.Dmax, nullptr, nullptr,
                        &Packet->Target.Atout, &Packet->Target.Rmix};
                    SaveBufAppendRawFloat(&Buf, "%.6f", Fields[Audio]);
                }
                bWroteAudioField = true;
            }
            SaveBufAppendLiteral(&Buf, "}");
            SaveUseScriptDuration(&Buf, Descriptor);
            SaveEventParamCurve(&Buf, Descriptor);
        }
        else if (EvType == kThornTypeFogPropAnim)
        {
            const FThornFogEventDesc* Packet = reinterpret_cast<const FThornFogEventDesc*>(Descriptor);
            if (Descriptor->EventFlags != 0)
                SaveBufAppendInteger(&Buf, ", event_flags = %d", Descriptor->EventFlags);
            SaveBufAppendLiteral(&Buf, ", fogprops = {");
            SaveFogProps(&Buf, &Packet->Target, static_cast<unsigned char>(Packet->Mask));
            SaveBufAppendLiteral(&Buf, "}");
            SaveEventParamCurve(&Buf, Descriptor);
        }
        else if (EvType == kThornTypeReverbPropAnim)
        {
            const FThornReverbEventDesc* Packet = reinterpret_cast<const FThornReverbEventDesc*>(Descriptor);
            if (Descriptor->EventFlags != 0)
                SaveBufAppendInteger(&Buf, ", event_flags = %d", Descriptor->EventFlags);
            SaveBufAppendLiteral(&Buf, ", reverbprops = {");
            SaveReverbProps(&Buf, &Packet->Target, static_cast<unsigned char>(Packet->Mask));
            SaveBufAppendLiteral(&Buf, "}");
            SaveEventParamCurve(&Buf, Descriptor);
        }
        else if (EvType == kThornTypeFloorHeightAnim)
        {
            const FThornFloorEventDesc* Packet = reinterpret_cast<const FThornFloorEventDesc*>(Descriptor);
            if (Packet->TargetPart != nullptr)
                SaveBufAppendStringValue(&Buf, ", target_part = \"%s\"", Packet->TargetPart);
            SaveBufAppendLiteral(&Buf, Packet->TargetType == 1 ? ", target_type = HARDPOINT"
                : Packet->TargetType == 2 ? ", target_type = PART" : ", target_type = ROOT");
            SaveBufAppendRawFloat(&Buf, ", floor_height = %.6f", &Packet->FloorHeight);
            SaveUseScriptDuration(&Buf, Descriptor);
            if (Descriptor->EventFlags != 0)
                SaveBufAppendInteger(&Buf, ", event_flags = %d", Descriptor->EventFlags);
            SaveEventParamCurve(&Buf, Descriptor);
        }
        else if (EvType == kThornTypeSubtitle)
        {
            const FThornSubtitleEventDesc* Packet = reinterpret_cast<const FThornSubtitleEventDesc*>(Descriptor);
            if (Packet->Strid != 0)
                SaveBufAppendUnsigned(&Buf, ", strid = %u", Packet->Strid);
        }
        // 2bf85 is the shared tail of these eight branches.
        if (EvType == kThornTypeStartMotion || EvType == kThornTypeStartSound ||
            EvType == kThornTypeStartIk || EvType == kThornTypeAttachEntity ||
            EvType == kThornTypeConnectHardpoints || EvType == kThornTypeStartSubScene ||
            EvType == kThornTypeSubtitle || EvType == kThornTypeStartPsys)
        {
            if (Descriptor->EventFlags != 0)
                SaveBufAppendInteger(&Buf, ", event_flags = %d", Descriptor->EventFlags);
        }

        // 2c1d7 and 2c1e5 remain separate flush/callback stages.
        SaveBufAppendLiteral(&Buf, "}");
        SaveBufAppendLiteral(&Buf, "}");
    }

    SaveBufAppendLiteral(&Buf, "\r\n};\r\n");

    // 2c216..2c233: final flush status is discarded, then return zero.
    SaveBufFlush(&Buf);
    return GR_OK;
}

const char* ThornEventTypeDebugName(unsigned int Type)
{
    static const char* const Names[] = {
        "UNDEFINED",
        "USER_EVENT",
        "SET_MONITOR",
        "START_SOUND",
        "START_LIGHT_PROPERTY_ANIM",
        "START_CAMERA_PROPERTY_ANIM",
        "START_SPATIAL_PROPERTY_ANIM",
        "START_IK",
        "START_PATH_MOTION",
        "ATTACH_ENTITY",
        "CONNECT_ENTITY",
        "START_MOTION",
        "USER",
        "START_PSYS",
        "START_PSYS_PROPERTY_ANIM",
        "START_AUDIO_PROPERTY_ANIM",
        "START_FOG_PROPERTY_ANIM",
        "START_REVERB_PROP_ANIM",
        "START_FLR_HEIGHT_ANIM",
        "SUBTITLE",
    };
    if (Type >= 20)
        return "<out of range>";
    return Names[Type];
}
