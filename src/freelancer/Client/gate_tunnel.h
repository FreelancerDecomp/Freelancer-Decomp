// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail Client\Space\GateTunnel.cpp (path string .rdata 0x005da560) -- the jump tunnel a
// player ship flies through, loaded from the [Data] gate_tunnels INI and driven by the
// JumpShipEffect (Ship +0x188) that finds it by its jump_tunnel id.
//
// Class GateTunnel: operator new(0x2b8), constructor FUN_004fee30, vtable 0x005da71c
//   slot 0 FUN_004ff070  scalar deleting destructor (body FUN_004ff0d0)
//   slot 1 FUN_00501080  EnterState0 -- reset the clocks and the ship
//   slot 2 LAB_005010b0  EnterState1 -- rebuild every curve, create the tunnel light
//   slot 3 LAB_00501290  EnterState2 -- restart the speed clock
//   slot 4 FUN_005012a0  EnterState3 -- release the tunnel light
// and its members SetState FUN_004ff360, SetShip FUN_004ff3b0, Update FUN_004ff3c0, Draw
// FUN_004ffab0 (+ BuildFrame FUN_004ffbb0, EmitLayer FUN_004ffe50), RebuildPath FUN_00500a80,
// RebuildColorOffsets FUN_00500de0. The nine 0x28-byte spline curves are FGateTunnelCurve
// (FUN_00501720 / FUN_005012e0 / FUN_00501470 / FUN_00501750 / FUN_00501810, the solver
// FUN_005018a0 and the cubic FUN_00501c40). The list DAT_00674f9c is filled by GateTunnelIniLoad
// (FUN_004fe1c0, 0x0054ac92) and emptied by GateTunnelsClear (FUN_004fe180, 0x0054afbb).
//
// Layout note: retail's curves hold VC6 std::vectors (allocator byte, first, last, end); the
// port holds ours, so the class is not byte-compatible with retail past +0xc8 and nothing may
// address it by raw offset. Offsets below are retail's, for tracing.

#include "common_csys.h"
#include "common_id.h"
#include "texture_shapes.h" // FTextureRef
#include "x86math.h"

#include <vector>

class FClientShipGameobj; // client_gameobj.h
struct FGameLight;        // engine_light.h
struct FSpaceCamera;      // space_camera.h

// One cubic segment of a curve: value = ((c3 t + c2) t + c1) t + c0 (0x10 bytes).
struct FGateTunnelCoeff
{
    float C0; // +0x00
    float C1; // +0x04
    float C2; // +0x08
    float C3; // +0x0c
};

// FUN_00501c40 (thiscall on the segment, RET 4) -- the cubic at T, left on the FPU.
double GateTunnelCoeffEval(FGateTunnelCoeff const* Coeff, float T);

// FUN_005018a0 (cdecl) -- the natural-end cubic spline through Y[0..N] (tridiagonal solve for
// the tangents D, then Out[i] = {Y[i], D[i], 3(Y[i+1]-Y[i]) - 2D[i] - D[i+1],
// 2(Y[i]-Y[i+1]) + D[i] + D[i+1]}). Out is emptied and resized to N segments.
void GateTunnelSolveSpline(float const* Y, int N, std::vector<FGateTunnelCoeff>* Out);

// A random spline (0x28 bytes in retail): the control values, their segments and the range new
// control values are drawn from.
struct FGateTunnelCurve
{
    // FUN_00501720 + the ctor's `[+0x20] = 0, [+0x24] = 1.0f`.
    FGateTunnelCurve();

    // FUN_00501810 (thiscall, RET 4) -- the curve at T in [0, 1]: T == 1 gives the last control
    // value, anything else the segment floor(Count * T) at the fraction. Left on the FPU.
    double Sample(float T) const;

    // FUN_005012e0 (thiscall, RET 0x14) -- Count control values drawn uniformly from
    // [Minimum, Maximum] (which become the curve's range), the first ZeroHead and the last
    // ZeroTail forced to 0, then the spline.
    void Randomize(int Count, float Minimum, float Maximum, int ZeroHead, int ZeroTail);

    // FUN_00501470 (thiscall, RET 4) -- with Shift >= 8 and at least 8 control values: the
    // last Shift values move to the front, the rest are redrawn from the range, the spline is
    // rebuilt, and -((Count - Shift) / (Count - 1)) (integer division) comes back; otherwise 0.
    double Scroll(int Shift);

    // FUN_00501750 (thiscall, RET 0xc) -- Count control values read from Source with a Stride
    // of floats, then the spline.
    void SetFromSamples(int Count, float const* Source, int Stride);

    std::vector<float> Samples;              // +0x00 (VC6: first +0x04, last +0x08)
    std::vector<FGateTunnelCoeff> Coeffs;    // +0x10 (VC6: first +0x14, last +0x18)
    float Minimum;                           // +0x20
    float Maximum;                           // +0x24
};

// One render layer (0x34 bytes, two at +0xc). Constructor LAB_004ff090 and destructor
// FUN_00426de0 are FTextureRef's; the rest is set by the loader's [layer] section.
struct FGateTunnelLayer
{
    FTextureRef Texture;  // +0x00 texture (a G_EffectShapeDB shape's record)
    FVector Color;        // +0x08 color (defaults 0.5)
    float NearAlpha;      // +0x14 near_alpha (1.0)
    float FarAlpha;       // +0x18 far_alpha (0.0)
    float RadiusFactor;   // +0x1c radius_factor (1.0)
    float UOffset;        // +0x20 u_offset (0.0), scrolled by Du
    float VOffset;        // +0x24 v_offset (0.0), scrolled by Dv
    float Du;             // +0x28 du (0.09)
    float Dv;             // +0x2c dv (0.07)
    float VScale;         // +0x30 v_scale (10.0)

    FGateTunnelLayer();
};

// A struct (not a class) because jump_effect.h forward-declares it as one.
struct FGateTunnel
{
    // FUN_004fee30.
    FGateTunnel();
    // slot 0: FUN_004ff070 / FUN_004ff0d0.
    virtual ~FGateTunnel();
    // slots 1..4, reached through SetState.
    virtual void EnterState0();
    virtual void EnterState1();
    virtual void EnterState2();
    virtual void EnterState3();

    FGateTunnel(FGateTunnel const&) = delete;
    FGateTunnel& operator=(FGateTunnel const&) = delete;

    // FUN_004ff360 (thiscall, RET 4): State = NewState, then slot 1 + NewState for 0..3.
    // Callers: the JumpShipEffect methods 0x00503eb4 / 0x00504221 / 0x00504481 / 0x00504652.
    void SetState(int NewState);
    // FUN_004ff3c0 (thiscall, RET 4). Caller: FUN_00503cd0 (0x00503cf8).
    void Update(float Dt);
    // FUN_004ffab0 (thiscall, RET 4). Caller: FUN_00504890, the JumpShipEffect's tunnel draw.
    void Draw(FSpaceCamera* Camera);
    // FUN_004ffbb0 (thiscall, RET 8): the path frame at T -- columns (left, up, forward), the
    // point on the path as translation.
    void BuildFrame(FTransform* Out, float T) const;
    // FUN_004ffe50 (thiscall, RET 0x10): one layer's ring strips from T0 to T1.
    void EmitLayer(float T0, float T1, FGateTunnelLayer* Layer, int LayerIndex);
    // FUN_00500a80 (thiscall, RET 4).
    void RebuildPath(int Shift);
    // FUN_00500de0 (thiscall, RET 4).
    void RebuildColorOffsets(int Shift);

    ID_String Id;                  // +0x004 nickname ("default")
    int LayerCount;                // +0x008
    FGateTunnelLayer Layers[2];    // +0x00c
    unsigned char bWriteDepthBuffer; // +0x074 write_depth_buffer (1)
    int NumSplineControlPoints;    // +0x078 num_spline_control_points (11)
    float XRange;                  // +0x07c x_range (0)
    float YRange;                  // +0x080 y_range (0)
    float ZRange;                  // +0x084 z_range (0)
    float MinRadius;               // +0x088 min_radius (50)
    float MaxRadius;               // +0x08c max_radius (50)
    float FarRadiusFactor;         // +0x090 far_radius_factor (1.0)
    float MinSpeed;                // +0x094 min_speed (3e-5)
    float MaxSpeed;                // +0x098 max_speed (0.06)
    float TimeToMaxSpeed;          // +0x09c time_to_max_speed (8)
    float NumTSteps;               // +0x0a0 num_t_steps (200); Draw negates it around a layer
    float NumSSteps;               // +0x0a4 num_s_steps (12)
    float MinRotation;             // +0x0a8 min_rotation (-pi)
    float MaxRotation;             // +0x0ac max_rotation (pi)
    FVector MinRgb;                // +0x0b0 min_rgb (0)
    FVector MaxRgb;                // +0x0bc max_rgb (0)
    FGateTunnelCurve PathX;        // +0x0c8
    FGateTunnelCurve PathY;        // +0x0f0
    FGateTunnelCurve PathZ;        // +0x118
    FGateTunnelCurve Radius;       // +0x140 [min_radius, max_radius]
    FGateTunnelCurve Rotation;     // +0x168 [min_rotation, max_rotation]; no reader traced
    FGateTunnelCurve Brightness;   // +0x190 [0.5, 1.0], scales the layer colour
    FGateTunnelCurve ColorOffsetR; // +0x1b8 [min_rgb.x, max_rgb.x]
    FGateTunnelCurve ColorOffsetG; // +0x1e0 [min_rgb.y, max_rgb.y]
    FGateTunnelCurve ColorOffsetB; // +0x208 [min_rgb.z, max_rgb.z]; EmitLayer reads R for B
    Transform Frame;               // +0x230 the path's world frame (FUN_00503440 sets it)
    FClientShipGameobj* Ship;      // +0x260 FUN_004ff3b0
    float SpeedClock;              // +0x264
    unsigned Field_0x268;          // +0x268 TODO: no reader or writer found yet
    int SegmentIndex;              // +0x26c
    float Phase;                   // +0x270 the path position (0..1) the ship is at
    Vector ShipPosition;           // +0x274
    Matrix ShipOrientation;        // +0x280
    int State;                     // +0x2a4
    FGameLight* Light;             // +0x2a8
    Vector LightDirection;         // +0x2ac
};

// DAT_00674f98 (allocator byte; first DAT_00674f9c, last DAT_00674fa0, end DAT_00674fa4) -- the
// loaded tunnels, a VC6 vector of pointers built by the static initializer LAB_004fe0c0
// (FUN_0059ec40). Its atexit destructor (LAB_004fe0e0 -> FUN_004fe0f0) owns the elements: every
// non-null tunnel still listed is deleted through slot 0 before the storage is freed.
struct FGateTunnelList : std::vector<FGateTunnel*>
{
    FGateTunnelList() = default;
    FGateTunnelList(FGateTunnelList const&) = delete;
    FGateTunnelList& operator=(FGateTunnelList const&) = delete;
    // FUN_004fe0f0.
    ~FGateTunnelList();
};
extern FGateTunnelList G_GateTunnels;

// FUN_004fedd0 (cdecl) -- the tunnel whose +4 id equals *Id, else FDUMP 0x100002 (line 0xfc)
// and null. Caller: FUN_00503440 (0x0050377f).
FGateTunnel* GateTunnelFind(ID_String const* Id);

// FUN_004ff3b0 (thiscall, RET 4) -- +0x260 = Ship. Caller: FUN_00503440 (0x005037c0).
void GateTunnelSetShip(FGateTunnel* Tunnel, FClientShipGameobj* Ship);

// FUN_004fe1c0 (cdecl, the resolved path) -- [gate_tunnel] / [layer] sections into
// G_GateTunnels. Caller: the space init at 0x0054ac92.
void GateTunnelIniLoad(char const* Path);

// FUN_004fe180 (cdecl) -- every tunnel deleted through its slot 0, then end = begin. Caller:
// the space teardown at 0x0054afbb.
void GateTunnelsClear();
