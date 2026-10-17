// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// GraphLibrary -- E:\FL\Scratch\Source\Client\cmn\GraphLibrary.cpp.
//
// Named keyframe curves ("IGraph") loaded from the [IGraph] sections of the file DATA's
// freelancer.ini names under `igraph =` (FUN_005b0c30 @ 005b0ee1 -> FUN_004197e0). The library
// is a std::list<GraphData> (DAT_0061669c allocator byte, DAT_006166a0 sentinel node,
// DAT_006166a4 size) built by the static initializer LAB_00419050 and torn down twice: first by
// FUN_00419e50 from the client atexit chain (005b2286), then by the static destructor
// LAB_00419090.
//
// [LightSource] names its ambient_curve / atten_curve / color_curve by nickname; the engine
// light resolves them through GraphLibraryFind (FUN_00419760), which AddRefs the curve it
// returns.
//
// This is an ordinary C++ object, not a DACOM component: every slot takes `this` in ECX
// (__thiscall). The abstract IGraph vtable is PTR_LAB_005c8f4c (ten _purecall entries, so no
// virtual destructor); GraphData's is PTR_LAB_005c8eb4.

#include <string>

// The {flag, min, max} block at GraphData +0x20. Callers build one on the stack and pass it to
// Evaluate / EvaluateColor to map their own parameter range onto the curve's X range.
struct FGraphRange
{
    unsigned char bClamp; // +0x00 non-zero: clamp to [Min, Max - 0.001]; zero: wrap
    unsigned char Pad01[3];
    float Min;            // +0x04
    float Max;            // +0x08
};

// One key: X, then either a float (ElemType FLOAT) or a dword (INTEGER, or COLOR packed
// 0xAARRGGBB). 8 bytes, which is the stride every GraphData loop uses.
struct FGraphPoint
{
    float X;
    union
    {
        float Value;
        unsigned Bits;
    };
};

// The {ElemType, Count, Data} triple GetDesc copies out and SetData takes in (GraphData
// +0x14..+0x1C).
struct FGraphDesc
{
    int ElemType;
    unsigned Count;
    FGraphPoint* Data;
};

// FUN_004197e0 "type" values.
enum EGraphElemType
{
    GraphElem_Integer = 1,
    GraphElem_Float = 2,
    GraphElem_Color = 3,
};

class IGraph
{
public:
    virtual char const* GetName() = 0;                                        // 0  LAB_004191d0
    virtual int GetElemType() = 0;                                            // 1  FUN_00415070 (ICF)
    virtual unsigned GetCount() = 0;                                          // 2  LAB_004191e0
    virtual FGraphRange* GetRange() = 0;                                      // 3  LAB_00431680 (ICF)
    virtual FGraphDesc* GetDesc(FGraphDesc* Out) = 0;                         // 4  LAB_004191f0
    virtual void SetData(FGraphDesc const* Desc) = 0;                         // 5  FUN_00419210
    // Returned in ST0 unrounded (00419452): callers either FSTP it to a float (0041064e) or
    // multiply with it first (00410934), so the port keeps the double.
    virtual double Evaluate(float T, FGraphRange const* Range) = 0;           // 6  LAB_004192f0
    virtual unsigned EvaluateColor(float T, FGraphRange const* Range) = 0;    // 7  LAB_00419500
    virtual int AddRef() = 0;                                                 // 8  LAB_00419160
    virtual int Release() = 0;                                                // 9  LAB_00419170
};

// GraphData, 0x30 bytes in retail. Lives by value inside the library's list node (node + 8).
class FGraphData : public IGraph
{
public:
    // FUN_00419d20. ElemType is deliberately left unset, as in retail: SetData writes it before
    // anything reads it.
    FGraphData();
    // FUN_00419f50 (reached from FUN_00419ec0's node construct): memberwise copy -- the name,
    // then +0x14..+0x2C verbatim, so the key buffer is shared, not duplicated.
    FGraphData(FGraphData const& Other);
    // FUN_00419100: free the key buffer, then the name.
    ~FGraphData();

    char const* GetName() override;
    int GetElemType() override;
    unsigned GetCount() override;
    FGraphRange* GetRange() override;
    FGraphDesc* GetDesc(FGraphDesc* Out) override;
    void SetData(FGraphDesc const* Desc) override;
    double Evaluate(float T, FGraphRange const* Caller) override;
    unsigned EvaluateColor(float T, FGraphRange const* Caller) override;
    int AddRef() override;
    int Release() override;

    // FUN_00419d50: name a fresh graph and take the library's reference. Refused once the
    // graph has references or keys.
    bool SetName(char const* NewName);

private:
    FGraphData& operator=(FGraphData const&) = delete;

    std::string Name;     // +0x04
    int ElemType;         // +0x14
    unsigned Count;       // +0x18
    FGraphPoint* Data;    // +0x1C malloc/realloc'd, Count keys
    FGraphRange Range;    // +0x20 bClamp is always 0 here (wrap); Min/Max = key X extent
    int RefCount;         // +0x2C
};

// FUN_00419460 -- __thiscall on an FGraphRange: clamp T into [Min, Max - 0.001] or wrap it
// into [0, Max - Min).
double GraphRangeWrap(FGraphRange const* Range, float T);
// FUN_004194e0 -- __thiscall on an FGraphRange: (T - Min) / (Max - Min).
double GraphRangeNormalize(FGraphRange const* Range, float T);

// FUN_00419760 -- find by nickname (case-insensitive), AddRef and return; null plus an FDUMP
// warning when absent.
IGraph* GraphLibraryFind(char const* Name);
// FUN_004197e0 -- load every [IGraph] section of Path; returns the number of graphs created.
int GraphLibraryLoad(char const* Path);
// FUN_00419e50 -- release every graph until the library is empty.
void GraphLibraryShutdown();
