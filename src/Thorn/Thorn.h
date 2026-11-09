// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Thorn.dll — architecture: docs/re/thorn.md.
// Public contracts are declared in src/include/thorn_iface.h.
// Interface methods: DACOM_API stdcall unless noted. IScriptEngine slot 20 = thiscall.
// IParamCurve is declared once in thorn_iface.h using modern DACOM_API methods.

#include "dacom.h"
#include "thorn_iface.h"
#include "x86math.h"

static const char* const G_NameScriptEngine = "IScriptEngine";
static const char* const G_Name3DMathEngine = "3DMathEngine";

static const unsigned long G_ThornRegisterPriority = 0x40000000;
static const unsigned long G_FactoryIidCompareBytes = 0x17; // "1.11_IComponentFactory" + NUL
static const unsigned long G_AggDescSizeThorn = 0x14;
static const unsigned long G_AggDescSizeMath = 0xC;

// x86math.h's FVector, which Thorn and SoundManager each used to redeclare.
typedef FVector FVec3;

struct FQuat
{
    float W;
    float X;
    float Y;
    float Z;
};

// Retail FUN_06f57de0 stack desc (size + name + unused +8).
struct FAggDescC
{
    unsigned long Size;
    const char* NameAt4;
    void* Field8;
};

// Thorn factories: size 0x14, +4 name, +8 outer, +0xC inner-out (not FAGGDESC14 IID layout).
struct FThornAggDesc14
{
    unsigned long Size;
    const char* NameAt4;
    IDAComponent* Outer;
    IDAComponent** InnerOut;
    unsigned int Field_0x10; // TODO: meaning unknown; Lua producers store zero.
};
static_assert(sizeof(FThornAggDesc14) == 0x14, "Thorn creation descriptor size");

// Retail COM aggregation shape shared by the CV_CRSplinePath object
// (controlling unknown at +0x70, embedded inner unknown at +0x74) and the
// IScriptEngine object (+0x108 / +0x10c), built by the factories
// LAB_06f37400 / LAB_06f3f4c0:
//   * every public facet's QueryInterface/AddRef/Release is a pure forward to
//     the controlling unknown (FUN_06f378d0/e0/f0, FUN_06f3f7a0/c0/e0:
//     `MOV EAX,[this+0x70]; JMP [[EAX]+slot]`);
//   * the controlling unknown is Desc->Outer when the caller aggregates,
//     otherwise the object's own embedded {vtbl, RefCount=1, Owner} triple;
//   * that triple's vtable (PTR_LAB_06f5abc8 / PTR_LAB_06f5b004) is the
//     NON-delegating implementation: QI (LAB_06f377c0 / LAB_06f3f690) walks
//     the IID table and AddRefs the facet through the facet's own
//     (delegating) vtbl+4; AddRef (LAB_06f37880 / LAB_06f3f750) is
//     `return ++RefCount`; Release (LAB_06f37890 / LAB_06f3f760) is a
//     saturating decrement that, on reaching zero, parks RefCount at 1,
//     runs the owner's destructor + free through the Owner back-pointer
//     and returns 0.
// FThornInnerUnknown is that embedded triple, a member rather than a base
// class (retail keeps it at +0x10c / +0x74, after the data); the owner
// supplies the two callbacks. The owners' facets (+4/+8) are real bases.
typedef GENRESULT (*FThornInnerQueryFn)(void* Owner, const char* Iid, void** Out);
typedef void (*FThornInnerDestroyFn)(void* Owner);

class FThornInnerUnknown final : public IDAComponent
{
public:
    FThornInnerUnknown(void* Owner, FThornInnerQueryFn Query, FThornInnerDestroyFn Destroy)
        : RefCount_(1), Owner_(Owner), Query_(Query), Destroy_(Destroy)
    {
    }

    GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override
    {
        return Query_(Owner_, Iid, Out);
    }

    unsigned long DACOM_API AddRef() override { return ++RefCount_; }

    unsigned long DACOM_API Release() override
    {
        // LAB_06f37890: `if (count) --count; if (count) return count;`
        if (RefCount_ != 0)
            --RefCount_;
        if (RefCount_ != 0)
            return RefCount_;
        // Retail parks the count at 1 before destroying the owner so a
        // re-entrant Release during teardown cannot destroy it twice.
        RefCount_ = 1;
        void* Owner = Owner_;
        FThornInnerDestroyFn Destroy = Destroy_;
        if (Owner != nullptr && Destroy != nullptr)
            Destroy(Owner);
        return 0;
    }

    unsigned long RefCount() const { return RefCount_; }

private:
    unsigned long RefCount_;
    void* Owner_;
    FThornInnerQueryFn Query_;
    FThornInnerDestroyFn Destroy_;
};

// Raw I3DMathEngine* from x86math CreateInstance — do not QI (docs/re/thorn.md).
// Its ABI is x86math.dll's published one; FQuat above is layout-identical to FQuaternion.

void ThornFdump(const char* Message);
void ThornLogStub(const char* What);
bool ThornNameEqual(const char* Left, const char* Right);
I3DMathEngine* ThornAcquire3DMathEngine();
GENRESULT ThornQuatMul(I3DMathEngine* Math, FQuat& Dst, const FQuat& A, const FQuat& B);
GENRESULT ThornQuatNlerp(I3DMathEngine* Math, FQuat& Dst, const FQuat& A, const FQuat& B, float T);
GENRESULT ThornQuatToMatrix(I3DMathEngine* Math, float* Dst9, const FQuat& Quat);
GENRESULT ThornMatrixToQuat(I3DMathEngine* Math, FQuat& Dst, const float* Src9);
GENRESULT ThornMathTransform(I3DMathEngine* Math, float* OutVec3, const float* Xform12,
                             const float* InVec3);
GENRESULT ThornMathMulMatrix(I3DMathEngine* Math, float* Out9, const float* Left9,
                             const float* Right9);
GENRESULT ThornMathMulTransform(I3DMathEngine* Math, float* Out12, const float* Left12,
                                const float* Right12);
GENRESULT ThornMathRotate(I3DMathEngine* Math, float* OutVec3, const float* Xform12,
                          const float* InVec3);
GENRESULT ThornMathTransposeTransform(I3DMathEngine* Math, float* OutVec3, const float* Matrix9,
                                      const float* InVec3);

IDAComponent* ThornCreateParamCurve(const char* FactoryName, bool bDeadSecondLinear);
// LAB_06f37400 object construction + aggregation step: the new object's
// controlling unknown becomes Outer (or its own inner unknown when Outer is
// null); *OutInnerUnknown receives the embedded non-delegating inner unknown.
IDAComponent* ThornCreateSplinePath(const char* FactoryName, bool bOrientation, IDAComponent* Outer,
                                    IDAComponent** OutInnerUnknown);

// Lua 3.2 (DllMain FUN_06f24c80 / slot 4 FUN_06f250e0). See ThornLua.cpp.
class FThornScene;

bool ThornLuaOpen();
void ThornLuaClose();
IThornLoadParser* ThornCreateLuaLoadParser(FThornScene* Scene);


// 16-slot ICRSpline (PTR_LAB_06f5ab88). Slot 4 = load points FUN_06f346a0.
struct ICRSpline : IDAComponent
{
    // Retail outer wrappers validate an index against the inner spline and
    // forward these two-argument operations (this, index, output).
    virtual GENRESULT DACOM_API Slot3(unsigned int Index, float* OutPoint) = 0;
    virtual GENRESULT DACOM_API Slot4LoadPoints(const float* Pts, unsigned long Count) = 0;
    virtual unsigned long DACOM_API Slot5() = 0;
    virtual GENRESULT DACOM_API Slot6(unsigned int Index, float* OutPoint) = 0;
    virtual GENRESULT DACOM_API Slot7(unsigned int Index, float* OutValue) = 0;
    virtual GENRESULT DACOM_API Slot8(unsigned char bClosed) = 0;
    virtual GENRESULT DACOM_API Slot9() = 0;
    // Orientation-spline extension slots (present on the orientation vtable).
    // Slot10/Slot11 are per-index / bulk ORIENTATION SETTERS (confirmed
    // 2026-09-10 via FUN_06f22a20's instruction-level trace: the [this+0x5d]
    // orientation-gate + -8 return and the 16-byte/4-float record stride
    // only match a Quats_ writer, never Points_ -- the earlier "indexed
    // point"/"OutRecords" names and getter framing were misattributions).
    virtual GENRESULT DACOM_API Slot10(unsigned int Index, float* Orientation) = 0;
    virtual GENRESULT DACOM_API Slot11(unsigned int Count, float* Orientations) = 0;
    virtual GENRESULT DACOM_API Slot12(unsigned int Index, float* OutOrientation) = 0;
    // Slot13/Slot14 widened 2026-09-10 from the 0-argument position-only
    // stub shape to the orientation variant's real, distinct implementations
    // (LAB_06f35270 / LAB_06f352f0, retraced against thorn.dll.txt). Neither
    // is called anywhere outside SplinePath.cpp (grepped across src/Thorn/),
    // so widening is safe.
    virtual GENRESULT DACOM_API Slot13(unsigned int Index, const float* MatrixRecords) = 0;
    virtual GENRESULT DACOM_API Slot14(unsigned int Count, const float* TransformRecords) = 0;
    // Slot15 (RET 0xc on both variants): position-only LAB_06f34820 is a
    // constant -7 stub; the orientation variant LAB_06f35410 (.rdata
    // 06f5ab2c / 06f5acc4 = vtable head + 0x3c) is the inverse of Slot13 --
    // it writes the indexed point's [3x3 rotation | position] 12-float
    // transform record into OutTransform and always returns -1.
    virtual GENRESULT DACOM_API Slot15(unsigned int Index, float* OutTransform) = 0;
};

// FUN_06f27bf0: diagnostic event aliases (separate from THN save names).
const char* ThornEventTypeDebugName(unsigned int Type);
