// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"
#include "common_ini.h"      // Vector, OrientedBox
#include "common_csys.h"     // Matrix, Transform, MatrixColumn0/1/2
#include "common_iface.h"    // IBehaviorManager
#include "common_cobject.h"  // CObject

#include <cstddef>

// ============================================================================
// Common.dll AI steering helpers, address range 0x062d6000-0x062ee000.
//
// This header declares the shared geometry and steering helpers the AI behaviors and
// ideas use: ObstacleAvoidance (PathfindManager+0x268), ICRSplineSegment, the ray / box /
// capsule tests, the intercept / clearance / heading-cone predicates on an FAiTarget,
// the steering-frame builders (BuildOrientationBasis, ComputeBankedSteeringFrame,
// TurnHelper) and GenericWeightedPick. The idea classes themselves (BaseIdea and its
// eleven subclasses) are in common_idea.h; SystemManager, its ThrustManager, FAiTarget
// and the timer table are in common_systemmanager.h. Per-function evidence lives in the
// decomp-tracker items.
//
// Definitions: src/Common/common_obstacleavoidance.cpp (ObstacleAvoidance, obstacle
// candidates / heading blend, FindClosestObstacleToAvoid), common_crspline.cpp
// (ICRSplineSegment), common_aiintercept.cpp (intercept / clearance / heading cones /
// GetIfacePositionOrZero), common_aifirecontrol.cpp (SustainedThresholdTimer, weapon fire
// control, the CM-dropper watcher), common_airaycast.cpp (ray / box / capsule tests,
// bearings), common_aisteer.cpp (steering frames, TurnHelper), common_aitarget.cpp
// (FAiTarget), common_gunconvergence.cpp (the SystemManager+0x1364 block), the idea
// Update bodies in common_faceidea.cpp / common_dodgeidea.cpp / common_splinemoveidea.cpp,
// and common_idea.cpp (GenericWeightedPick). Shared file-local helpers:
// src/Common/common_aisteer_internal.h.
// ============================================================================

// ----------------------------------------------------------------------------
// ObstacleAvoidance -- Common.dll.c 0x062de490 (Update) / 0x062de2a0 (average
// active direction helper). Per-ship forward-raycast obstacle steering.
//
// Ownership (settled 2026-09-24): PathfindManager embeds it at +0x268
// (common_pathfind.h) -- 0x268 + 0x318 == 0x580, PathfindManager's size. Its
// ctor pieces are inlined into PathfindManager's (FUN_062db3b0, .text:062db4a6-
// 062db4db), and AttachOwner (FUN_062de220) is called from there with the
// owning IBehaviorManager.
// ----------------------------------------------------------------------------
struct ObstacleAvoidance
{
    // Inlined into FUN_062db3b0: `eh vector constructor iterator(this+0x14, 0x1c,
    // 0x19, Watchable::Watchable)` -- the ICF-folded element ctor stores 0 into
    // the element's first dword, i.e. Slots[i].Timer = 0 for all 25 -- then
    // CursorIndex = 0 (.text:062db4d5) and ResetContact (FUN_062db780). Nothing
    // else is written; Owner/Opaque08/ElapsedTime come from AttachOwner and
    // PathfindManager's ctor zeroes PhysSystemId afterwards (.text:062db594).
    ObstacleAvoidance();

    // FUN_062de220 (item 1694), __thiscall, RET 4. Owner = NewOwner, ElapsedTime
    // = 0; then the owner object's get_archetype_extents(Min, Max) (IObjInspect
    // slot 10, +0x28) sets Opaque08's two floats: +0x08 = -Min.z, +0x0C =
    // max(|Max.x - Min.x|, |Max.y - Min.y|).
    void AttachOwner(IBehaviorManager* NewOwner);

    // FUN_062db780 (item 1669), called on this+0x2D4: Active = 0,
    // HasContact = 0, ContactObjectId = 0, ContactCloseRate = 0,
    // ContactTimer = 0 -- in that order.
    void ResetContact();

    // One candidate deflection direction. Timer counts down to 0 every tick
    // (Update's decay loop); a slot is only overwritten once its timer has
    // expired, so a heading that was just tried and failed isn't immediately
    // retried on the very next attempt.
    struct Slot
    {
        float Timer;        // +0x00  cooldown; > 0 while "reserved"
        Vector Point;        // +0x04  candidate deflection point (world space)
        Vector Direction;     // +0x10  candidate deflection direction (unit-ish)
    };

    static_assert(sizeof(Slot) == 0x1c, "ObstacleAvoidance::Slot stride 7 floats (Common.dll.c 0x062de490)");

    // Common.dll.c 0x062de490: `*(int*)(*param_1_00 + 0xf0)` / `*(int*)(*param_1_00 + 0xc)`.
    // Owner+0xf0 matches IBehaviorManager::WatchablePtr byte-for-byte (see
    // common_iface.h), so Owner is the ship's IBehaviorManager, and Owner+0xc is
    // IBehaviorManager::Pathfind -- the PathfindManager this object is embedded in.
    IBehaviorManager* Owner;          // +0x00
    unsigned PhysSystemId;             // +0x04  PhySys system id (FindRayCollisions)
    // Both written only by AttachOwner from the owner's archetype extents. Update moves the
    // ray start from the ship's position NoseDistance units along -Z (the nose), sizes the
    // look-ahead from NoseDistance * 50 and the probe spiral radius from Breadth.
    float NoseDistance;                  // +0x08  -Min.z
    float Breadth;                       // +0x0C  max(|Max.x - Min.x|, |Max.y - Min.y|)
    float ElapsedTime;                   // +0x10  running dt accumulator (drives the probe spiral angle)

    Slot Slots[25];                        // +0x14 .. +0x2CF

    int CursorIndex;                         // +0x2D0  round-robin slot cursor for replacement
    unsigned char Active;                     // +0x2D4  true while actively steering around a contact
    unsigned char PadD5[3];

    // Point and Normal of the PhySys::RayHit that triggered avoidance (062dea20-062dea53
    // copies hit+0x04 and hit+0x10, three dwords each; the hit's Object is not kept).
    Vector ContactPoint;                        // +0x2D8
    Vector ContactNormal;                        // +0x2E4
    unsigned ContactObjectId;                     // +0x2F0  hit CSimple::Id (062dea0d `[hit.Object+0xb0]`)
    Vector AvoidDirection;                          // +0x2F4
    Vector AvoidPoint;                                // +0x300
    unsigned char HasContact;                          // +0x30C
    unsigned char PadE[3];
    float ContactTimer;                                  // +0x310
    float ContactCloseRate;                                // +0x314

    // FUN_062de490, __thiscall, RET 4. Per-frame tick: ages the 25 slot timers; with a
    // physics system bound and the ship moving, casts a ray from the nose along a slowly
    // spiralling probe ahead of the ship. A close enough blocking hit sends three random
    // rays into a cone around -Z, every blocked one recording its hit point / normal into
    // a slot, and the active slots are averaged into the escape direction.
    void Update(float Dt);

    // FUN_062df2d0, __thiscall, RET 0, float on the x87 stack (its one caller FSTPs it).
    // 1.0f unless Active with a watched owner; then by ContactCloseRate: > 5.0 -> 0.8f,
    // < 1.0 -> 0.4f, otherwise ((rate - 1.0) * 0.25)^2 * 0.4f + 0.4f.
    float GetCloseRateScale() const;

    // FUN_062df360, __thiscall, RET 8. While Active: *OutDirection = AvoidDirection,
    // *OutObjectId = ContactObjectId, true; otherwise false with both untouched.
    bool GetAvoidDirection(Vector* OutDirection, unsigned* OutObjectId) const;

    // FUN_062df3a0, __thiscall, RET 8. GetAvoidDirection's twin over AvoidPoint.
    bool GetAvoidPoint(Vector* OutPoint, unsigned* OutObjectId) const;
};

static_assert(offsetof(ObstacleAvoidance, NoseDistance) == 0x08, "ObstacleAvoidance+0x08 NoseDistance");
static_assert(offsetof(ObstacleAvoidance, Breadth) == 0x0c, "ObstacleAvoidance+0x0c Breadth");
static_assert(offsetof(ObstacleAvoidance, ElapsedTime) == 0x10, "ObstacleAvoidance+0x10 ElapsedTime");
static_assert(offsetof(ObstacleAvoidance, Slots) == 0x14, "ObstacleAvoidance+0x14 Slots");
static_assert(offsetof(ObstacleAvoidance, CursorIndex) == 0x2d0, "ObstacleAvoidance+0x2d0 CursorIndex");
static_assert(offsetof(ObstacleAvoidance, ContactPoint) == 0x2d8, "ObstacleAvoidance+0x2d8 ContactPoint");
static_assert(offsetof(ObstacleAvoidance, ContactNormal) == 0x2e4, "ObstacleAvoidance+0x2e4 ContactNormal");
static_assert(offsetof(ObstacleAvoidance, ContactObjectId) == 0x2f0, "ObstacleAvoidance+0x2f0 ContactObjectId");
static_assert(offsetof(ObstacleAvoidance, AvoidDirection) == 0x2f4, "ObstacleAvoidance+0x2f4 AvoidDirection");
static_assert(offsetof(ObstacleAvoidance, AvoidPoint) == 0x300, "ObstacleAvoidance+0x300 AvoidPoint");
static_assert(offsetof(ObstacleAvoidance, ContactTimer) == 0x310, "ObstacleAvoidance+0x310 ContactTimer");
static_assert(sizeof(ObstacleAvoidance) == 0x318, "ObstacleAvoidance sizeof 0x318");

// FUN_062de2a0, __thiscall on &Slots[0], RET 8. For every active (Timer > 0) slot,
// accumulates `2 * normalize(Ship.Translation - Slot.Point) + Slot.Direction`, then
// averages; false when no slot is active. The average is normalised unless its squared
// length is <= 1e-5, and true is returned either way. Ship is the Transform the caller
// got from the owner's get_orientation (slot 4, folded with get_transform).
bool ObstacleAvoidance_AverageActiveDirection(ObstacleAvoidance::Slot const* Slots,
                                              Transform const& Ship,
                                              Vector& OutDirection);

// Common.dll.c 0x062dc440 (FUN_062dc440), raw disasm Common.dll.txt:227063-
// 227197 (115 insns), decomp Common.dll.c:110730-110781. Item 1679. Promoted
// this pass (d256:common-port) from `simplified` to a real standalone free
// function -- fully self-contained pure math, does not touch the
// unidentified "ship AI state" owner, so it doesn't need Field_0xNN raw-
// offset treatment like ClassifyShipObstaclePhase below.
//
// Quaternion-slerp-style rotation: rotates `From` toward `To` by a FIXED
// Angle (NOT the angle between From/To -- Angle is an independent caller-
// supplied step size), around the shortest-arc axis cross(From, To). If
// From/To are already within ~2.56 degrees of parallel (dot > 0.999), returns
// To verbatim (retail's own early-out, side-stepping a degenerate
// cross-product axis when one isn't needed). Re-verified this pass against
// the raw disassembly's actual push order at both math-engine call sites
// (Common.dll.txt:227167-227186) -- Ghidra's own decompile of the two calls
// is corrupted (shows zero and four bogus arguments respectively) exactly
// like the sibling call in UpdateShipHeadingBlend below; the real shape
// (engine, Dst=Matrix, Quat) / (engine, Dst=Vector, Matrix, Source=From) is
// identical to that already-verified sibling's QuaternionToMatrix (vtable
// +0x54) / TransformMatrix (vtable +0x2C) pair. Sole caller is
// FUN_062dbfb0/item 1676 (`NudgeObstaclePathPoint` below), with the literal
// step angle 0.3f (0x3e99999a) -- NOT ~0.2 rad as an earlier pass guessed.
Vector RotateTowardBySlerp(Vector const& From, Vector const& To, float Angle);

// ----------------------------------------------------------------------------
// Ship heading-blend classifier pair -- Common.dll.c 0x062dc590 (item 1680) /
// 0x062dc840 (item 1681). Per-tick maneuver logic on the still-unidentified
// large "ship AI state" object first seen in item 1667's ctor (Common.dll.c
// 0x062db3b0) -- ported as raw Field_0xNN offsets against a `void* Self`,
// matching this file's "AI weapon fire-control cluster" convention below for
// objects whose owning class isn't independently identified.
//
// Confirmed field layout this pass (cross-checked against item 1667's ctor
// notes and against ObstacleAvoidance's own already-ported Slot shape above):
//   +0x068 unsigned                   NextDeflectionSlot -- round-robin cursor
//   +0x06C unsigned[8]                RecentObstacleIds  -- recent-candidate cache
//   +0x08C ObstacleAvoidance::Slot[8] DeflectionSlots -- CORRECTS item 1667's
//     ctor note, which guessed a "Csys sub-object" at this offset: this pass
//     traced the actual array stride directly from the raw disassembly
//     (Common.dll.txt:227727-227762, `IMUL EBP,EBP,0x1c` against an 8-bound
//     counter), matching Slot's own already-confirmed 28-byte size, and the
//     8 slots tile exactly up to +0x16C (8*0x1c = 0xE0, 0x8C+0xE0 = 0x16C)
//     with no gap before State below -- an 8-slot sibling of the
//     already-ported 25-slot ObstacleAvoidance::Slots, not a Csys.
//   +0x16C int    State       -- 0/1 while actively deflecting, 3 = idle
//                                (this classifier's own default each tick)
//   +0x170 unsigned  LastObstacleTag -- cached vtable+0x20 id of whichever
//                                obstacle drove the most recent decision
//   +0x174..+0x17C Vector Heading -- current computed heading direction,
//                                read+blended by UpdateShipHeadingBlend
//   +0x180 bool   InReaction
//   +0x181 bool   CloseEncounterFlag
//   +0x184..+0x19F ObstacleAvoidance::Slot ActiveDeflectionSlot -- cached
//                                copy of whichever DeflectionSlots[] entry
//                                drove the current reaction
//   +0x1A0 float  BlendTimer  -- owned by UpdateShipHeadingBlend, not
//                                touched by ClassifyShipObstaclePhase
//   +0x1B0 bool   BlendEnabled
//   +0x1B4..+0x1BC Vector TargetPosition -- effective steering target (see
//                                ClassifyShipObstaclePhase below)
//   +0x1C0 IBehaviorManager* Owner
//   +0x260 unsigned CachedObstacleId
//   +0x264 bool     HasCachedObstacle
//
// Independently confirms (from the opposite direction) common_obstacleavoidance.cpp's
// own already-documented ObstacleAvoidance::Update comment ("IBehaviorManager
// +0xc: ... pointer whose own +0x200 caches the ship's last resolved target
// world position"): ClassifyShipObstaclePhase is one of that field's writers
// (Common.dll.txt:227467-227480) -- Self->TargetPosition is refreshed every
// tick from *(Owner+0xc)+0x200.
// ----------------------------------------------------------------------------

// Common.dll.c 0x062dbfb0 (FUN_062dbfb0), raw disasm Common.dll.txt:226605-
// 227001 (348 insns), decomp Common.dll.c:110604-110702 (cleanly decompiled,
// no unaff_ artifacts). Item 1676, ported for real this pass
// (d256:common-port) now that both its dependencies (RotateTowardBySlerp/
// item 1679 and RaySegmentCapsuleFirstHit/item 1722, both above) are fully
// resolved.
//
// Candidate's own field shape (Radius/SegStart/SegDir, 0x1c bytes) is
// IDENTICAL to the already-ported ObstacleAvoidance::Slot (Timer/Point/
// Direction, also 0x1c) -- confirmed by tracing param_1's exact byte offsets
// against the raw disassembly (Common.dll.txt:226652/226659, param_1+0x10
// and param_1+4) and matching Slot's own already-confirmed field sizes
// field-for-field. This is ClassifyShipObstaclePhase's own DeflectionSlots[]
// entry type (its Timer field is reinterpreted here as a clearance Radius),
// confirming the two functions share the same candidate-slot data directly,
// not merely a coincidentally-same-sized struct.
//
// Obstacle's own field shape (param_3) is likewise identical to the
// already-ported Transform (Matrix Rotation @ +0x00, Vector Translation
// @ +0x24, sizeof 0x30) -- confirmed via the raw disassembly's obstacle-
// orientation read (Common.dll.txt:226826-226840, offsets +0x8/+0x14/+0x20,
// exactly MatrixColumn2's own already-established stride-0xC-per-row column
// pattern) and the obstacle-position read (+0x24, matching pfVar1).
//
// Algorithm: builds Midpoint = Candidate.SegStart + 0.5*Candidate.SegDir,
// then DirToObstacle = normalize(Obstacle.Translation - Midpoint) (a REAL
// reciprocal sqrt via the already-ported VectorLength-style FUN_06347e60,
// not the fast-invsqrt idiom used later); bails with 3 ("path irrelevant to
// this obstacle") if a ray from Midpoint along DirToObstacle doesn't even
// intersect the candidate's own swept-capsule volume
// (RaySegmentCapsuleFirstHit). Three more re-derived unit directions
// (ObstacleDir, PointDir, ObstacleToPointDir -- all bailing with 1, "too
// close/urgent", if their source distance is under 1 unit) feed a
// "roughly-aligned" gate (dot(ObstacleDir,PointDir) > 0.9, i.e. the obstacle
// sits within ~25.8 degrees of the straight line to Point) that returns 1
// UNLESS the caller opted into the extra orientation check (ExtraCheck) AND
// the obstacle's own -MatrixColumn2 (its "backward" direction) is more than
// ~31.8 degrees off ObstacleToPointDir (dot < 0.85) -- i.e. the obstacle is
// facing roughly away from Point, making the straight-line proximity less
// concerning. Past that gate, a fast-invsqrt-approximated-distance compare
// (RayCapsuleHitPoint-to-Midpoint * 1.2 vs Obstacle-to-Midpoint) returns 2
// ("close enough already, no reroute needed") if the capsule's own surface
// is closer than 1.2x the obstacle itself. Otherwise, if Point is already
// within a fast-invsqrt-approximated 100 units of the obstacle, *OutPoint =
// Point unchanged (0, "use Point as-is"); else RotateTowardBySlerp(From=
// ObstacleDir, To=PointDir, Angle=0.3f) computes a nudged direction, and a
// second RaySegmentCapsuleFirstHit along it produces the final *OutPoint (0
// on success, 3 if even the nudged direction misses the capsule).
int NudgeObstaclePathPoint(ObstacleAvoidance::Slot const& Candidate, Vector const& Point,
                                       Transform const& Obstacle, Vector* OutPoint, bool ExtraCheck);

// FUN_062dbbe0, item 1675, Common.dll.txt:226296-226595. A PathfindManager thiscall
// in retail that never reads `this`; fills a deflection slot (Timer = capsule radius,
// Point = segment start, Direction = segment vector) for an obstacle candidate:
// from get_physical_radius / get_atmosphere_range when get_archetype_extents
// reports a sphere, otherwise a capsule along the longest extent of the archetype
// box, transformed to world space. Always returns true.
bool ResolveObstacleCandidateSlot(IObjRW* Candidate, ObstacleAvoidance::Slot& OutSlot);

// FUN_062e74e0 (item 1799) is SystemManagerTimerTable::GetOwnScanList (formerly
// ObstacleCandidateSource_Resolve), declared in common_systemmanager.h and defined in
// common_obstacleavoidance.cpp.

// FUN_062dc840 / FUN_062dc590 (items 1681 / 1680) are PathfindManager::ClassifyObstaclePhase /
// UpdateHeadingBlend (common_pathfind.h), defined in common_obstacleavoidance.cpp.

// ----------------------------------------------------------------------------
// ICRSplineSegment -- Common.dll.c/.txt 0x063327e0-0x06332eab ("0xd27e0" ..
// "0xd2df0" in the per-symbol ordinal comments). A 4-point uniform Catmull-Rom
// spline segment: P1..P2 is the interpolated curve, P0/P3 are the neighboring
// control points that only shape the incoming/outgoing tangent.
//
// Retail embeds this as a member subobject at different byte offsets inside
// at least three distinct, otherwise-unrelated outer objects (Common.dll.c
// casts seen at outer+0xc6, outer+0xd2, and outer+0x348 respectively) --
// most likely path nodes for AI patrol/formation movement -- but every one of
// its own six methods only ever touches its own fields, so the class ports
// cleanly on its own without needing any of those outer layouts.
//
// Field layout: P0..SampleCount below (0x00-0x38) are directly confirmed --
// every FLD/FSUB/FADD/FMUL operand across all six methods' disassembly reads
// or writes one of these six fields at exactly these offsets, and no other
// offset is ever touched by any of them. There are, however, 8 further
// unaccounted-for bytes: ICRSplineSegment::operator=(ICRSplineSegment const&)
// (Common.dll.txt line 68585, address 0x062830a0) is COMDAT-folded byte-for-
// byte with pub::AI::Personality::GunUseStruct::operator=, i.e. the *same*
// machine code serves both -- `MOV ECX,0x10; REP MOVSD` -- a flat 16-dword
// (0x40-byte) memberwise copy. Since GunUseStruct is independently confirmed
// as exactly sizeof 0x40 (common_pub.h), the fold is only valid if
// ICRSplineSegment is *also* exactly 0x40 bytes, 8 bytes more than the six
// methods above account for. Kept as an explicit opaque tail rather than
// silently undersizing the type; see Reserved38 below.
struct COMMON_API ICRSplineSegment
{
    // User-declared so that no implicit move members are generated and exported:
    // VC6 had none, and retail exports exactly the copy assignment.
    ICRSplineSegment& operator=(ICRSplineSegment const&) = default;

    Vector P0;              // +0x00  neighbor control point before the segment
    Vector P1;              // +0x0C  segment start -- calculate_spline_vector(0) == P1
    Vector P2;              // +0x18  segment end   -- calculate_spline_vector(1) == P2
    Vector P3;              // +0x24  neighbor control point after the segment
    float ArcLength;        // +0x30  running accumulated length; written by calculate_arclength()
    unsigned SampleCount;    // +0x34  numerical-integration sample count; ctor sets it to 20

    // +0x38 .. +0x3F: proven to exist (see the operator= COMDAT-fold note
    // above) but never read or written by any of the six methods ported
    // here, so its real meaning (most plausibly intrusive list linkage,
    // given the three different embedding offsets noted above) is not
    // confirmed. Left opaque rather than guessed.
    unsigned char Reserved38[0x08];

    // Common.dll.c 0xd27e0 (Common.dll.txt 376247-376255). Retail only ever
    // sets SampleCount; P0..P3/ArcLength/Reserved38 are left uninitialized,
    // matched here -- callers must fill in P0..P3 before evaluating the curve.
    ICRSplineSegment() : SampleCount(20) {}

    // Common.dll.c 0xd27f0 (Common.dll.txt 376262-376562, ~230 instructions).
    // Evaluates the uniform Catmull-Rom basis at parameter T (0 == P1, 1 == P2):
    //   0.5 * ( 2*P1 + T*(P2-P0) + T^2*(2*P0-5*P1+4*P2-P3) + T^3*(-P0+3*P1-3*P2+P3) )
    // Every coefficient (2.0, the implicit +-1 in P2-P0, 2/-5/4/-1, -1/3/-3/1,
    // and the final 0.5) was individually confirmed against the literal FPU
    // immediates/constants in the disassembly (e.g. DAT_0639dfd4 == 3.0f) --
    // this is exactly the textbook Catmull-Rom basis, not an assumption. The
    // specific FPU instruction *order* (heavy x87/stack-slot reuse across the
    // full 230 instructions) was not re-derived op-for-op; this evaluates the
    // same basis with a normal left-to-right float evaluation order instead.
    Vector calculate_spline_vector(float T);

    // Common.dll.c 0xd2bb0 (Common.dll.txt 376578-376646). Central-difference
    // numerical tangent: normalize(calculate_spline_vector(T+eps) -
    // calculate_spline_vector(T-eps)), eps == 0.001 (verified from the raw
    // IEEE-754 double bit pattern at .rdata:0639c9f0, which decodes exactly to
    // 0.001). Not a closed-form derivative.
    Vector calculate_spline_tangent(float T);

    // Common.dll.c 0xd2c60 (Common.dll.txt 376649-376735). Numerically
    // integrates the segment's length over SampleCount steps (T = i/SampleCount
    // for i in [1, SampleCount]) as a sum of per-step chord lengths, storing
    // the result in ArcLength. Retail measures the very first chord from the
    // raw control point P0 (this+0x00), NOT from calculate_spline_vector(0)
    // (== P1) the way every subsequent chord is measured -- kept exactly as
    // found; see get_spline_length below for the same loop starting from P1
    // instead. This P0-vs-P1 asymmetry is in the retail binary itself.
    void calculate_arclength();

    // Common.dll.c 0xd2d30 (Common.dll.txt 376747-376828). Same per-step
    // integration as calculate_arclength, but the running chord starts from
    // P1 (this+0x0C) and the sum is returned rather than stored.
    float get_spline_length();

    // Common.dll.c 0xd2df0 (Common.dll.txt 376834-376921). NOTE: in the
    // decomp-tracker DB this retail function's address range was merged into
    // the same item as get_spline_length above (no gap/alignment padding
    // separates the two in the listing) -- they are two distinct retail
    // functions with their own prologues/RET, ported separately here.
    //
    // A coarse one-directional line search, NOT a real closest-point solve:
    // starting at T = StartT and stepping by StepT, evaluate the squared
    // distance from the curve to Point at each step; stop as soon as a step
    // makes the distance worse (equal-or-better keeps going), and return the
    // LAST ACCEPTED T -- i.e. the local minimum's own T, not one step past
    // it. (Re-verified directly from the raw disassembly, Common.dll.txt
    // 376834-376921: the returned local slot is only ever written with
    // StartT at entry or with the new T on the accept branch at 06332e9a;
    // the reject branch jumps straight to the final FLD/RET without writing
    // it, so the failing candidate is never what gets returned.) Like
    // retail, this can loop indefinitely if the distance never stops
    // decreasing for the given StepT.
    float get_t_from_point(Vector const& Point, float StartT, float StepT);
};

// ----------------------------------------------------------------------------
// Ray-intersection cluster (decomp-tracker items 1712-1714, address range
// 0x062df830-0x062dfd80+). Three clean, self-contained pure-math functions
// with no dependency on any unidentified owner object -- ported directly as
// named free functions (not Field_0xNN raw-offset stubs), matching the
// existing OBBIntersect/OrientedBox convention in common_ini.h.
// ----------------------------------------------------------------------------

// Common.dll.c 0x062df830 (FUN_062df830), raw disasm Common.dll.txt around
// 112773 (.c) / cleanly decompiled, no frame-size failure. Classic analytic
// ray-sphere intersection: L = SphereCenter - RayOrigin; b = dot(L, RayDir)
// (RayDir assumed unit-length, per retail -- no normalization here);
// disc = Radius^2 + b^2 - dot(L,L); t0 = b - sqrt(disc), t1 = b + sqrt(disc).
// Returns false if disc < 0 (no real root) or both t0 and t1 are negative
// (sphere entirely behind the ray origin). Otherwise returns true and writes
// the nearest non-negative root to *OutT (t0 if t0 >= 0, else t1) -- OutT may
// be null (retail checks before writing). The `t0 >= 0 && t1 < 0` branch is
// algebraically unreachable in practice (t1 = t0 + 2*sqrt(disc) >= t0) but is
// transliterated verbatim from the raw disassembly's if-chain for byte-exact
// fidelity.
// ABI CORRECTION 2026-09-17: `Radius` is passed BY ADDRESS in retail, not by
// value, so it is a reference here. The callee dereferences it --
// .text:062df85c `MOV EAX,[ESP+param_4]` followed by .text:062df866
// `FLD [EAX]` -- and the call site at .text:062d083e pushes `LEA EDX,[ESP+0x30]`,
// an address, into that slot (five pushes in all: &[ESP+0x5c], &[ESP+0x28], EBP,
// &[ESP+0x30], &[ESP+0x48], so right-to-left this is param_4). The second call
// site is .text:062dd23c. The old by-value `float` computed the same answer, so
// nothing was visibly broken, but the argument's width and indirection were
// wrong, which is exactly the class of mismatch this port exists to get right.
bool RaySphereIntersect(Vector const& RayOrigin, Vector const& RayDir, Vector const& SphereCenter, float const& Radius, float* OutT);

// Common.dll.c 0x062df940 (FUN_062df940), raw disasm Common.dll.txt around
// 112818 (.c). Ray-vs-axis-aligned-box slab test in LOCAL space (box spans
// [-HalfExtent, +HalfExtent] around the local origin) -- the classic
// Kay/Kajiya slab-clipping algorithm, one half-space test per box face (6
// total: +-X, +-Y, +-Z), narrowing [TMin, TMax] in place. TMin/TMax are
// in/out by reference (caller seeds them, e.g. [0, FLT_MAX] for a plain ray).
// Returns false the instant any face test proves the interval empty or the
// ray is parallel to a slab and starts outside it; true otherwise, but ONLY
// if TMin or TMax actually changed from their input values (retail's final
// `local_out == in` guard -- an untouched interval means no face test ever
// tightened it, i.e. degenerate input, not a real hit).
bool RayAABBIntersect(Vector const& LocalOrigin, Vector const& LocalDir, Vector const& HalfExtent, float& TMin, float& TMax);

// Common.dll.c 0x062dfd80 (FUN_062dfd80). Ray-vs-ORIENTED-box: param_3 is
// mistyped `float` by Ghidra (calling-convention ambiguity -- immediately
// cast back to a pointer at entry) but is actually an `OrientedBox const*`;
// confirmed field-for-field identical to common_ini.h's OrientedBox (offsets
// +0xc/+0x18/+0x24 = AxisX/AxisY/AxisZ rows, +0x30 = HalfExtent, +0x0 =
// Center, read via FUN_06288800 as `RayOrigin - Box->Center`). Transforms
// RayOrigin (recentered on the box) and RayDir into box-local space via dot
// products against each axis row, then defers to RayAABBIntersect with
// TMin=0/TMax=FLT_MAX. On a hit, writes:
//   *OutHitType = 2 (entered from outside, TMin > 0): OutPoints[0] = entry
//     point (RayOrigin + TMin*RayDir, WORLD-space RayDir), OutPoints[1] =
//     exit point (RayOrigin + TMax*RayDir).
//   *OutHitType = 1 (ray origin already inside the box, TMin <= 0): retail
//     writes ONLY the exit point, into OutPoints[0] (OutPoints[1] is left
//     untouched/stale in this path -- reproduced here exactly, matched by
//     leaving that slot alone rather than "helpfully" zero-filling it).
// *OutHitType = 0 and false on a miss. OutPoints must have room for 2
// Vector3s (6 floats) even though only one is guaranteed written.
bool RayOrientedBoxIntersect(Vector const& RayOrigin, Vector const& RayDir, OrientedBox const& Box, int* OutHitType, Vector* OutPoints);

// FUN_062dffd0, item 1715, raw disasm Common.dll.txt:232432-232837
// (.text:062dffd0-062e0473, 393 instructions, no Ghidra decompile at all --
// read from the listing only).
//
// THE OLD `blocked` REASON WAS STALE, on both of its legs. It said the three
// helper calls "look like matrix-column extractors, likely related to the
// already-ported MatrixColumn0/1/2 family but under different names" -- they
// ARE that family, literally: FUN_062e0480/FUN_062e04a0/FUN_062d3af0 are the
// items already tracked as MatrixColumn0/1/2, each an eight-instruction
// FLD/FLD/FLD + FSTP/FSTP/FSTP over offsets {0,0xc,0x18}/{4,0x10,0x1c}/
// {8,0x14,0x20}. And it advised starting from the caller "inside FUN_062e47xx,
// also unassigned" -- that is item 1747/EstimateLeadInterceptDistance, which
// has been in the tracker (and half-ported) for days. The only genuinely hard
// part, the ~40-local frame, dissolves once you notice that local_64..local_2c
// are not 40 scalars but ONE 15-float OrientedBox plus a dozen dot products:
// FUN_062dfd80/RayOrientedBoxIntersect indexes that same stack run at
// +0/+0xc/+0x18/+0x24/+0x30, which is OrientedBox field for field.
//
// What it does: takes a ray, a Transform and a local-space AABB laid out
// {MinX,MaxX,MinY,MaxY,MinZ,MaxZ} (the same 6-float shape
// TransformedBoxesIntersect takes, and the call site at 0x062e47b0-062e47d8
// de-interleaves a get_bounding_box(Min,Max) pair into exactly it), builds the
// world-space OrientedBox, and runs Eberly's ray-vs-OBB separating-axis test
// (Wild Magic Ray3Box3): three box-axis tests of the form
// `|dot(Diff,A_i)| > Extent_i && dot(Diff,A_i)*dot(Dir,A_i) >= 0`, then three
// cross-axis tests `|dot(Dir x Diff, A_i)| > E_j*|dot(Dir,A_k)| +
// E_k*|dot(Dir,A_j)|`. Any one of them returns false. Note the box-axis tests
// are the RAY form (start outside the slab, heading away), not the segment
// form -- there is no `+ |dot(Dir,A_i)|` term, and the raw bytes have no
// instruction that could hide one.
//
// On a hit it defers to RayOrientedBoxIntersect for the real entry/exit points
// and returns the one nearer the ray origin; a hit-type of 1 (origin already
// inside the box) sets *OutStartsInside and returns that single point.
// *OutStartsInside is cleared up front, before any test, so it is written even
// when the function returns false. Retail null-checks that pointer and no
// other, and this port matches that -- OutPoint is written unconditionally.
bool RayTransformedBoxNearestHit(Vector const& RayOrigin, Vector const& RayDir,
                                            Transform const& Xform, float const* AabbMinMax,
                                            Vector* OutPoint, bool* OutStartsInside);

// Common.dll.c 0x062e0cf0 (FUN_062e0cf0), raw disasm Common.dll.txt:233702-
// 233819 (103 insns). Item 1720. Builds an orthonormal basis {Out1, Out2,
// Dir} from a single direction vector Dir (in/out -- normalized in place
// first unless AlreadyNormalized is set), using the classic "zero out
// whichever of X/Y has the LARGER absolute value" single-vector basis
// construction: Out1's other two components are built from
// FastInvSqrt(sum-of-squares) of Z plus whichever of X/Y was KEPT, making
// Out1 unit-length and perpendicular to Dir; Out2 = cross(Dir, Out1)
// completes the basis. Retail's "extraout_EDX"/"extraout_EDX_00" locals in
// the Ghidra decompile are both just Dir itself (EDX, never reassigned
// across the FUN_062df7f0/FastInvSqrt call, item 1711, already ported in
// common_misc.h/.cpp and now wired up here) -- a Ghidra register-tracking
// artifact, not real extra outputs. Only caller in the module is
// RaySegmentCapsuleIntersect/FUN_062e0e10 (item 1721, below -- now ported at
// `ready_unverified` confidence, this basis feeds its capsule-local
// coordinate frame).
void BuildPerpendicularBasis(Vector* Out1, Vector* Out2, Vector* Dir, bool AlreadyNormalized);

// Common.dll.c 0x062e0e10 (FUN_062e0e10), raw disasm Common.dll.txt:233834-
// 234318 (414 insns), decomp Common.dll.c:113490-113637 (cleanly decompiled,
// no unaff_/extraout corruption -- tracker metadata's decomp_line_start=null
// for this item is stale, see item 1721's own notes). Item 1721, now fully
// resolved and promoted from `simplified` to `ready_unverified` this pass
// (d256:common-port) -- the previously-flagged "genuine deep implementation
// gap" in the end-cap section is closed: re-derived algebraically term-for-
// term against the raw FPU disassembly (Common.dll.c:113607-113636) and
// confirmed to compute an exact SPHERE-vs-ray intersection at each end of the
// segment, not a flat-disk cap. Combined with the lateral quadratic (a
// constant-radius, i.e. NON-tapering, infinite-cylinder test -- confirmed by
// the coefficients using a fixed Radius with no per-height scaling, contrary
// to this cluster's earlier "cone" working title) restricted to the finite
// axis range [0, |SegDir|], the true retail shape is a CAPSULE (cylinder body
// + 2 hemispherical end-caps), matching this header's own already-ported
// PointInCapsule below field-for-field (Radius, SegStart, SegDir-not-unit).
//
// Builds an orthonormal (Basis1, Basis2, UnitAxis) frame via
// BuildPerpendicularBasis above, projects RayDir and (RayOrigin-SegStart)
// into it, and:
//   * If the ray is (near-)parallel to the axis (|dot(UnitAxis,RayDir)| >= 1):
//     degenerates to testing whether the ray's (fixed) lateral offset is
//     within Radius, writing both axis-crossing t's (0/2 hits only, matching
//     retail exactly).
//   * Otherwise solves the constant-radius lateral (infinite-cylinder)
//     quadratic; each root is kept only if its axis-coordinate falls in
//     [0, Height] (Height = the fast-invsqrt-approximated |SegDir|, DAT_
//     magic-number idiom matching FastInvSqrt's own formula, reused via
//     FastInvSqrt(x)*x rather than reimplementing it).
//   * Falls through (whether the lateral test produced 0 or 1 valid roots --
//     2 already returns immediately) to test both end-cap spheres (centered
//     at SegStart and SegStart+SegDir, both radius Radius), each root
//     accepted only on the correct outward-facing side (axis-coordinate <= 0
//     for the SegStart cap, >= Height for the far cap) so it doesn't overlap
//     the cylinder body's own already-covered range.
// Returns the total hit count (0/1/2), writing that many t-values (ray
// parameter, i.e. HitPoint = RayOrigin + t*RayDir) into OutT in the exact
// order retail produces them (lateral roots first, then end-cap roots) --
// this order matters, see RaySegmentCapsuleFirstHit below.
int RaySegmentCapsuleIntersect(Vector const& RayOrigin, Vector const& RayDir, float Radius,
                                           Vector const& SegStart, Vector const& SegDir, float* OutT);

// Common.dll.c 0x062e1330 (FUN_062e1330), raw disasm Common.dll.txt around
// 234320 (76 insns), decomp Common.dll.c:113641-113698. Item 1722, promoted
// from `simplified` to `ready_unverified` this pass now that its own
// dependency (RaySegmentCapsuleIntersect/item 1721 above) is fully resolved.
// Thin wrapper: calls RaySegmentCapsuleIntersect, then walks its OutT[]
// results in order and computes RayOrigin + t*RayDir for the FIRST t >= 0.0
// found -- CORRECTING this item's own earlier note (which guessed it kept
// the "last" hit point): re-traced directly against the raw disassembly this
// pass, the output pointer only advances past its initial slot on a
// successful write, so any hit AFTER the first valid one is computed but
// written to a dead, never-read stack slot. Returns true iff at least one
// t >= 0.0 was found (independent of which slot's point got kept).
bool RaySegmentCapsuleFirstHit(Vector const& RayOrigin, Vector const& RayDir, float Radius,
                                           Vector const& SegStart, Vector const& SegDir, Vector* OutPoint);

// Common.dll.c 0x062e1410 (FUN_062e1410), raw disasm Common.dll.txt around
// 234433 (91 insns). Item 1723. Classic point-vs-capsule test: Capsule is a
// 7-float record {Radius, SegStart(3), SegDir(3), SegDir NOT unit -- its own
// squared length is the segment's parameterization denominator}. Projects
// Point onto the segment, clamping to the near endpoint (t<0, i.e. the dot
// product gate fails) or far endpoint (t>1, i.e. dot >= SegDir's squared
// length) exactly like a standard closest-point-on-segment routine, then
// compares the squared distance from Point to that closest point against
// Radius^2.
bool PointInCapsule(Vector const& Point, float Radius, Vector const& SegStart, Vector const& SegDir);

// Common.dll.c 0x062e1630 (FUN_062e1630), raw disasm Common.dll.txt around
// 234782 (24 insns). Item 1727. Dot product of two Vector3s, clamped to
// [-1, 1] -- the standard "safe cosine" guard before an acos() call
// elsewhere (matches this cluster's other acos-based angle helpers, e.g.
// items 1731/1732 below). Returns double: retail leaves the value in ST0 unrounded
// (see the definition).
double ClampedDot3(Vector const& A, Vector const& B);

// Common.dll.c 0x062e18f0 (FUN_062e18f0), raw disasm Common.dll.txt:235066-
// 235227 (137 insns). Item 1730. Classic continuous (swept) sphere-vs-sphere
// earliest-time-of-impact test over a single linear time step: sphere A
// moves from A0 to A1 (radius RadiusA), sphere B moves from B0 to B1 (radius
// RadiusB). RelPos0 = B0-A0, RelVel = (B1-B0)-(A1-A0) (relative displacement
// over the step); solves |RelPos0 + t*RelVel|^2 = (RadiusA+RadiusB)^2 for the
// smaller root of the resulting quadratic. Already-overlapping-at-t=0 short-
// circuits to *OutT=0/true. A near-zero relative-velocity-squared (<0.01) or
// negative discriminant reports *OutT=-1/false (no collision this step).
// Returns false (with a valid *OutT) if the computed earliest-impact t itself
// is negative (the collision, if any, already happened before this step).
bool SweptSphereCollisionTime(float RadiusA, Vector const& A0, Vector const& A1,
                                          float RadiusB, Vector const& B0, Vector const& B1, float* OutT);

// ----------------------------------------------------------------------------
// BearingAngleToIface / BearingAngleToPoint -- FUN_062e1aa0 (item 1731) /
// FUN_062e1c90 (item 1732), Common.dll.txt 235241-235404 / 235417-235549.
//
// Both return fabs(acos(clamp(Dot, -1, 1))) where Dot is the normalised
// (Self centre - comparison point) dotted with column 2 of Self's get_transform()
// (+8/+0x14/+0x20, not negated). The centre is IObjInspect slot 0x2c's Vector
// out-parameter, inlined in the GetIfacePositionOrZero shape. A null Self (or, for
// the Iface variant, a null Target) returns 0.0 (DAT_06399408).
//
// Both return DOUBLE: the FABS result is left unrounded in ST0, and every retail
// caller FCOMPs it directly (062c8e96, 062c8fae, 062c9f3b, 062ca8dd, 062d5422,
// 062d55fe, 062d631e).
//
// The two inline normalisations are NOT the same and neither is VecNormalize3:
//   * Iface: inverse = 1.0f/Len kept unrounded; x and y FSTP'd to float, z left on
//     the stack.
//   * Point: inverse FSTP'd to float, all three products left on the stack.
// The dot is (z*m22 + y*m12) + x*m02 in both, FSTP'd to float before the clamp.
//
// BearingAngleToIface additionally queries Target's centre (first, before Self's).
double BearingAngleToIface(void* Self, void* Target);

// BearingAngleToPoint (item 1732). Ghidra counts one parameter; the second (the
// point) is read at 062e1d02 `MOV EAX,[ESP+0x34]` (entry ESP + 8), and all three
// callers (062d541d, 062d55f9, 062d6319) push two dwords and clean 8 bytes.
double BearingAngleToPoint(void* Self, Vector const& ComparisonPoint);

// ----------------------------------------------------------------------------
// BuildOrientationBasis / TransformedBoxesIntersect -- newly-reopened AISTEER/
// TurnHelper cluster, address range 0x062e0000-0x06301280 (see common_ini.h's
// "2026-09-10 re-verification pass" header note for the DO-NOT-TOUCH-list
// correction that opened this sub-range; OBBIntersect/OrientedBox/the Vec*3
// primitives these two functions use both live in common_ini.h next to that
// note). Both ported this pass, continuing directly from the prior lane's
// OBBIntersect (item 1718) / VecNormalizeInPlaceByLength3 (item 1678) work.
// ----------------------------------------------------------------------------

// Common.dll.c 0x062e1f40 (FUN_062e1f40), raw disasm Common.dll.txt:235665-236218
// (466 insns), decomp Common.dll.c:114207-114417 (cleanly decompiled, no frame-
// size decompiler failure). "Look-at" orthonormal basis builder: given a Forward
// direction and an optional Up hint, builds Out as [Right | Up' | Z] (column 0 =
// Right, column 1 = the re-orthogonalized Up, column 2 = Z), where Z =
// normalize(-Forward).
//
//   * If Forward is near-zero (|Forward|^2 < 0.0001, DAT_0639c2b8), Out is left
//     as the all-zero matrix (retail zero-fills it up front and only overwrites
//     it on the non-degenerate path) -- reproduced here as Out = {} rather than
//     leaving it uninitialized.
//   * If UpHint is supplied and is not within ~2.56 degrees of parallel to Z
//     (|dot(UpHint,Z)| < 0.999): Right = normalize(cross(UpHint, Z)), and
//     Up' = cross(Z, Right) (already unit-length, Z and Right being unit and
//     orthogonal -- retail does not re-normalize it, matched here).
//   * Otherwise (UpHint is null, or too close to parallel with Z): retail falls
//     back to Reference's own column 0/1 (C0/C1, via MatrixColumn0/1) to rebuild
//     an orthogonal basis via Gram-Schmidt, picking C0 or C1 depending on which
//     is further from parallel with Z, then sign-flipping the result to stay
//     consistent with Reference's own column 1 (near-parallel-to-C1 branch,
//     rebuilt from C0) or column 0 (otherwise, rebuilt from C1) -- see the raw
//     disassembly line-for-line in the .cpp for the exact per-branch operand
//     identities (this fallback pair of
//     degenerate-input branches only diverges from the primary path when the
//     caller's Up hint is itself nearly parallel to Forward, or absent).
//
// Reference is dereferenced unconditionally in the fallback path with no null
// check, matched here (retail assumption: always valid). UpHint is the only
// nullable input (retail's own `param_4 != 0` guard).
void BuildOrientationBasis(Matrix& Out, Matrix const& Reference, Vector const& Forward, Vector const* UpHint);

// Common.dll.c 0x062e2630 (FUN_062e2630), raw disasm Common.dll.txt:236333-236544
// (205 insns), item 1738. PORTED FOR REAL 2026-09-17
// (claude:common-unaff-rederive). This item had been `blocked` on three claimed
// "unaff_" corruptions, then `partial` on one remaining unresolved scratch slot;
// a complete 205/205 hand trace with byte-exact stack-depth tracking closes both.
//
// Signature, confirmed at all five call sites (each cleans exactly 0x10 bytes --
// e.g. Common.dll.txt:222328-222336, and note 062d9583's caller cleans 0x20
// because it also covers the immediately preceding BuildOrientationBasis call):
//   Matrix* (Matrix* Out /*hidden, E+4, returned in EAX*/,
//            Matrix const* Reference /*E+8, the old `unaff_EBX`*/,
//            float SpreadAngle      /*E+0xC, the old `unaff_retaddr`*/,
//            float BankAngle        /*E+0x10 -- a FOURTH parameter no previous
//                                     pass identified*/)
// Observed arguments: SpreadAngle is the literal 0x3e490fdb (pi/16) at three of
// the five sites and a caller variable at the other two; BankAngle is 0.0f at
// three sites and a caller value at two.
//
// Algorithm:
//   1. Quat0 = MatrixToQuaternion(*Reference)                    [+0x50]
//   2. Quat1 = {cos(SpreadAngle/2), RandomVector(1.0f) * (sin(SpreadAngle/2) /
//      |RandomVector|)} -- a rotation of SpreadAngle about a uniformly random
//      axis. The 0.5f is DAT_0639c13c; the magnitude uses a real FSQRT, not the
//      clearance cluster's FastInvSqrt idiom, and the divide is unguarded.
//   3. Frame = QuaternionToMatrix(MulQuat(Quat0, Quat1))         [+0x18, +0x54]
//   4. Forward = -column2(Frame). THIS IS THE SLOT the previous two passes could
//      not pin down ("what feeds Forward's Y-component, fStack_6c"): both
//      branches read the same three flat offsets E-0x50/E-0x44/E-0x38 of the
//      9-float Frame buffer, i.e. E[0][2]/E[1][2]/E[2][2], and negate each.
//      Nothing between MulQuat and QuaternionToMatrix contributes to it.
//   5. BuildOrientationBasis(Frame, WorkingCopyOfFrame, Forward, &UpHint), then
//      the returned frame is copied back over the working copy. Retail needs the
//      copy only because it aims Out at Frame's own storage.
//   6. Finally *Out = the working frame, and EAX = Out.
//
// TWO CORRECTIONS to the earlier `partial` analysis, both from the raw listing:
//   * The branch gate is `BankAngle > 0.0f` (236426/236427: `FLD [ESP+0xc0];
//     FCOMP DAT_06399408`, whose bytes at .rdata:06399408 are 00 00 00 00), NOT
//     "a value read from the MulQuat/QuaternionToMatrix output". When it holds,
//     UpHint is Reference's own column 1 and a random-signed roll of BankAngle
//     is applied afterwards; otherwise UpHint is the literal (0, 1, 0) and no
//     roll happens.
//   * BuildOrientationBasis never receives a null UpHint here -- both branches
//     pass a real vector.
// The post-basis roll (236478-236502) is a 3-iteration loop over Frame's ROWS
// rotating each row's (col0, col1) pair by the roll angle -- a roll about the
// local Z axis, leaving column 2 (the Forward direction) untouched.
//
// RETAIL DEAD CODE, reproduced literally: the roll's sign draw is
// `if (rand() * (1/32768) < 0.0f) BankAngle = -BankAngle` (236467-236476,
// DAT_063997d0 == 1/32768, compared against the same 0.0f constant). rand() is
// never negative, so retail's FCHS is unreachable and the roll is always
// positive. Almost certainly a source-level intent of "< 0.5f", but "fixed" it
// would change observable behaviour, so the port keeps retail's semantics and
// documents the discrepancy instead.
Matrix* BuildRandomSpreadFrame(Matrix& Out, Matrix const& Reference,
                                          float SpreadAngle, float BankAngle);

// Common.dll.c 0x062e0a60 (FUN_062e0a60), raw disasm Common.dll.txt:233457-233693
// (188 insns), decomp Common.dll.c:113337-113431. Independently re-traced this
// pass (see decomp-tracker item 1719/899 notes): builds two OrientedBox values,
// one per (Transform, local-space AABB) pair --
//   Center     = TransformXform(Xform, AabbMin + AabbHalfExtent)   (world space)
//   AxisX/Y/Z  = Xform.Rotation's raw columns 0/1/2 (MatrixColumn0/1/2, no flip)
//   HalfExtent = per-axis |max-min|/2 of the AABB
// -- then calls OBBIntersect on the pair. AabbMinMax is 6 floats laid out
// {MinX,MaxX,MinY,MaxY,MinZ,MaxZ} (retail reads exactly this pattern: fields
// 0/2/4 as the min corner, |field[2k+1]-field[2k]| as the half-extent).
//
// Verification note for item 899 (FUN_06299880/TransformXformVec): this call
// site was flagged by the prior lane as needing independent confirmation,
// since item 899's only previously-known caller used it from an unrelated
// context. Traced directly against FUN_06299880's own disassembly
// (Common.dll.txt:107975-108009) and both call sites here
// (Common.dll.txt:233590-233593 / 233676-233678): thiscall this=ECX=Transform
// const*, stack args (hidden Dst pointer, then Vector const& V) -- i.e. exactly
// item 899's already-documented `Vector FUN_06299880(Transform const*, Vector
// const&)` signature, called the same way both times. No overload, no
// different-arity call -- the two call sites here use the identical signature,
// just with a raw Transform* (Matrix @ +0, Translation @ +0x24) rather than the
// Transform reference item 899's other caller used.
bool TransformedBoxesIntersect(Transform const& XformA, float const* AabbA, Transform const& XformB, float const* AabbB);

// ---------------------------------------------------------------------------
// Intercept-clearance cluster (items 1745-1751, Common.dll.c 0x062e4350-0x062e4ea0),
// common_aiintercept.cpp. Every "Elem" is an FAiTarget (common_systemmanager.h): the
// own ship's SystemManager::SelfTarget or an element of the timer table's target
// vector. The clearance is the distance between two cached centres minus the sum of
// the cached radii, clamped to >= 0, through retail's inlined fast-length idiom; when
// the 0x30000 type bits are missing retail falls back to EstimateLeadInterceptDistance.
bool IsWithinInterceptRange(FAiTarget* Elem, FAiTarget* Target, float Threshold); // FUN_062e4350, item 1745
double GetInterceptClearance(FAiTarget* Elem, FAiTarget* Target); // FUN_062e4d00, item 1750 (same body as above minus the final threshold compare; ST0 return)

// Common.dll.c 0x062e4500 (FUN_062e4500), item 1746. RE-VERIFIED 2026-09-11
// (d264:common-port): NOT a duplicate of IsWithinInterceptRange above and NOT
// genuinely corrupted -- the previously-claimed unaff_ESI/unaff_retaddr were
// a false positive (Ghidra guessed the wrong parameter count for this
// function). Real signature: (Elem, TargetIface-already-resolved, Threshold)
// -- a sibling overload for call sites that only hold the Target's interface
// pointer, not a full Target record, so it can't use Target's own 0x7d cache
// byte and instead re-queries TargetIface's flags unconditionally every
// call. Gate condition also differs from the twin (TargetFlags&0x207 alone
// is independently sufficient, not just the twin's 0x30000-on-both check).
// See common_aiintercept.cpp's own doc comment for the complete per-instruction
// derivation.
bool IsWithinInterceptRangeOfIface(FAiTarget* Elem, IObjInspect const* TargetIface, float Threshold); // FUN_062e4500, item 1746

// Common.dll.c 0x062e4bb0 (FUN_062e4bb0), item 1749. PORTED 2026-09-17
// (claude:common-steering-close). Stands to IsWithinInterceptRangeOfIface above
// exactly as GetInterceptClearance stands to IsWithinInterceptRange: the same
// body, returning the clamped clearance distance instead of comparing it against
// a threshold. Ghidra reported both parameters "<UNASSIGNED>" because MSVC reuses
// both parameter home slots as float scratch inside the body -- not register
// corruption; see common_aiintercept.cpp's doc comment for the per-instruction
// derivation. This is the function MoveIdea::Update (item 1577) needs on its
// TargetKind48 == 1 arm, where it is now reached through NavInterceptClearanceOfIface
// (common_idea_internal.h; the 0x7effffff sentinel stand-in that once stood there is gone).
// Returns double: retail leaves the clearance in ST0 (FUN_062e4bb0 never stores it to a float).
double GetInterceptClearanceOfIface(FAiTarget* Elem, IObjInspect const* TargetIface); // FUN_062e4bb0, item 1749

// FUN_062e53e0, item 1755, raw disasm Common.dll.txt:240396-240560 (141 insns).
// PORTED IN FULL 2026-09-17 (claude:common-idea-updates), replacing the local
// always-true `RunCollisionProbeStub` this file used to carry. Nine call sites
// (062c4edd, 062c7c16, 062c7ca8, 062c7dbe, 062c8002, 062c806a, 062d9265,
// 062d931f, 062da2b5); plain __cdecl, three arguments, `ADD ESP,0xc` at the
// 062d9265 site.
//
// The whole body is one predicate: take the unit direction from `Iface`'s own
// physical centre to `Point`, dot it against `Iface`'s heading (the NEGATED
// column 2 of its get_transform(), i.e. the axis this module's LookMatrix calls
// "forward"), clamp the dot into [-1, 1], and return whether it is strictly
// below `CosThreshold`. In other words: is `Point` outside the cone of half-
// angle acos(CosThreshold) around where `Iface` is pointing.
//
// The item's old note called this "not ported verbatim ... the two branches
// disagree on which stack slot holds Z". That was a Ghidra artifact, not a real
// ambiguity: retail reuses its OWN `Iface` argument slot as the scratch
// `float& Radius` out-param (`LEA EDX,[ESP+0x34]` at 062e53f3, which Ghidra
// itself annotates `=>param_2`), and reuses the centre's three stack slots for
// the difference vector immediately afterwards. Read off the raw bytes every
// slot is unambiguous; the claim is retired.
//
// Two shapes worth keeping as read, because they look like bugs and are not:
//   * get_physical_radius returning NON-zero makes retail call get_id()
//     (slot 0x20) and DISCARD the result, then use a zero centre -- the filled
//     centre is used only on the ZERO return (062e53fd/062e5423).
//   * The near-zero guard is on the length AFTER the sqrt, against
//     .rdata:DAT_0639e6b8 == 0x3c23d70a == 0.01f, and a shorter separation
//     yields a zero direction (hence a zero dot), not an early out.
bool IsPointOutsideHeadingCone(Vector const& Point, IObjInspect const* Iface,
                                          float CosThreshold); // FUN_062e53e0, item 1755

// FUN_062e5190, item 1754, raw disasm Common.dll.txt:240181-240392 (191 insns).
// PORTED IN FULL 2026-09-17 (claude:common-idea-updates). Four call sites
// (062c61fe, 062cb00c, 062d2c79, 062d937f), all cleaning 0xc -- so it takes
// THREE arguments, not the two Ghidra's header lists; see the .cpp for where
// the third one is read.
//
// The two-object sibling of IsPointOutsideHeadingCone: `Elem` is the nav block
// (its owner at +0x84, its refresh byte at +0x7e and its cached centre/radius at
// +0x0c/+0x18, exactly as in IsWithinInterceptRangeOfIface), and the cone
// belongs to `TargetIface`. Returns true only when BOTH hold:
//   * the two objects' headings point the same way (their dot is > 0), and
//   * the unit direction from TargetIface's centre to Elem's centre, dotted
//     against TargetIface's heading and clamped into [-1, 1], is strictly below
//     CosThreshold.
// A null TargetIface degenerates both terms to zero, which fails the first test.
bool IsAlignedAndOutsideHeadingCone(FAiTarget* Elem, IObjInspect const* TargetIface,
                                    float CosThreshold); // FUN_062e5190, item 1754

// Common.dll.c 0x062e4660 (FUN_062e4660), raw disasm Common.dll.txt:239080-
// 239315 (225 insns). Item 1747. Elem is an FAiTarget, TargetIface the other
// object's IObjInspect. Refreshes Elem's bounding-box corner cache (+0x7f,
// FUN_062e4910) and its PosValid cache, casts a ray from Elem's surface point
// (frame translation + unit direction * cached radius) towards the target's
// position and intersects it with the target's oriented archetype box
// (FUN_062dffd0). Returns, in ST0 (double): 0 when the ray starts inside the
// box, the fast length to the nearest hit, or on a miss the fast length from
// Elem's frame translation to the target's physical centre (radius unused).
double EstimateLeadInterceptDistance(FAiTarget* Elem, IObjInspect const* TargetIface); // FUN_062e4660, item 1747

// Common.dll.c 0x062e4ea0 (FUN_062e4ea0), raw disasm Common.dll.txt:239876-
// 239932 (48 insns). Item 1751. Small, cleanly decompiled, no register
// corruption. Distance from Elem's cached position to an arbitrary world
// Point, minus Elem's cached radius -- UNCLAMPED (may be negative), and using
// a real sqrtf rather than the FastInvSqrt idiom the 1745/1750 pair above use.
// Retail re-checks the PosValid guard a second time after it was already set
// true by the first check (dead but harmless, likely an artifact of two
// separate inlined accessor calls in the original source); reproduced as-is.
// Returns the ST0 value unrounded (double); callers FCOMP / FSTP it themselves.
double GetSignedRadialClearanceToPoint(FAiTarget* Elem, Vector const& Point); // FUN_062e4ea0, item 1751

// Common.dll.c 0x062e4f60 (FUN_062e4f60), raw disasm Common.dll.txt:239984-
// 240173 (178 insns). Item 1753. FULLY RE-DERIVED 2026-09-17
// (claude:common-iustategraph) from the raw listing; the previous pass's
// "would need careful re-derivation of which Elem is which side to avoid a
// swapped-operand bug, judged not worth the risk" is now settled -- the
// operand order below is read straight off the two call sites' push order
// (0x062e78df: PUSH 0; PUSH SelfElem; PUSH CandidateElem -- so the FIRST
// cdecl argument is the candidate and the SECOND is the reference).
//
// Retail keeps the two arguments in ESI/EDI the other way round from the
// stack order (ESI = param_2 = Reference at 0x062e4f64, EDI = param_1 =
// Target at 0x062e4f97) and then REUSES param_2's own incoming stack slot as
// a scratch local (FSTP [ESP+0x40] at 0x062e4fea writes the heading dot
// product over it, and FLD [ESP+0x38] at 0x062e5160 reads it back). That
// aliasing is what made the function look self-contradictory to earlier
// passes; it is a plain compiler stack-slot reuse, nothing hidden.
//
// Returns true iff BOTH of:
//   1. dot(RefForward, TargetForward) > 0.0f -- the two objects' facings
//      point into the same half-space. Retail builds each forward vector as
//      MatrixColumn2 of the object's own get_transform (slot +0x0C) and runs
//      it through VecNegate3 (0x062c5a70) first; negating both sides leaves
//      the dot product unchanged, so the port skips the two negations.
//   2. dot(normalize(TargetPos - RefPos), -RefForward) < CosThreshold, with
//      the cosine clamped to [-1, +1] exactly as retail does (two FCOM/FLD
//      clamp branches against +/-1.0f). At the one live call site
//      CosThreshold is 0.0f, i.e. "the target is in the reference's forward
//      half-space". A direction shorter than 0.01f degenerates to the zero
//      vector (retail's own epsilon branch at 0x062e5079), which makes the
//      dot product 0.0f and therefore fails a 0.0f threshold.
// Both objects' cached positions are refreshed through the usual bPosValid /
// get_physical_radius idiom before step 2.
bool IsTargetCoAlignedAndAhead(FAiTarget* Target, FAiTarget* Reference, float CosThreshold); // FUN_062e4f60, item 1753

// Common.dll.c 0x062e6a80 (FUN_062e6a80), raw disasm Common.dll.txt:242775-
// 242823 (39 insns). Item 1791. Cleanly disassembled by hand (no Ghidra
// decompile issues -- small, single-path function). Catalogued under the
// separate "IUStateGraph" AI-wingman cluster since
// its 7 call sites are all inside that still-unbuilt subsystem, but it
// operates on the exact same Elem record layout as the Intercept-clearance
// cluster above (see that block's own "CROSS-CLUSTER IDENTITY" note) --
// ported here, beside Elem's other accessors, rather than duplicating the
// struct-layout writeup in a second file.
//
// Lazily-caches a {Low, High} float pair (Elem+0x00/+0x04) behind a RangeValid
// guard (Elem+0x7C), refreshed through a SECOND, independent vtable pointer
// (Elem+0x80, distinct from the Intercept-clearance cluster's Elem+0x84
// Iface) at slot 0x6C: `bool __thiscall RangeIface->vt[0x6C](RangeIface,
// float* OutLow, void* Iface, float* OutHigh)` -- Iface (Elem+0x84) is passed
// through unchanged as the query's own middle argument. If RangeIface or
// Iface is null, or the vtable call returns false, CachedLow/CachedHigh are
// forced to 0.0f/0.0f instead (matches the zero-fallback idiom used
// throughout this file, e.g. GetIfacePositionOrZero above).
void GetCachedRange(FAiTarget* Elem, float* OutLow, float* OutHigh); // FUN_062e6a80, item 1791

// Common.dll.c 0x062e6070 (FUN_062e6070), item 1770, raw disasm
// Common.dll.txt:241636-241677 (34 insns). PORTED 2026-09-17
// (claude:common-scan).
//
// That item's standing blocker -- "BOTH final compare operands (`unaff_ESI`/
// `unaff_retaddr`) are Ghidra register-corruption artifacts with no visible
// defining write" -- is FALSE, the tenth such claim disproved in this module.
// Tracking ESP by hand (E = entry ESP; `PUSH ECX; PUSH ESI` puts ESP at E-8)
// resolves both operands to ordinary stack slots:
//   * `FCOMP [ESP+0x4]` at 062e60ba reads E-4, the slot `PUSH ECX` reserved
//     and `MOV [ESP+0x4],EAX` at 062e609d filled with Target->CachedRadius;
//   * `FLD [ESP+0xc]` at 062e60b6 reads E+4 -- retail REUSES its own incoming
//     `FAiTarget*` parameter slot as get_atmosphere_range's out-parameter
//     (`LEA EAX,[ESP+0xc]` at 062e60a1, zeroed at 062e60a5, pushed at
//     062e60af). Ghidra names a read of E+4 after that overwrite
//     `unaff_retaddr` because it no longer matches the declared parameter.
// The exact same argument-slot-reuse artifact was already retired on items
// 1746, 1749 and 1755 in this file.
//
// The FAiTarget sibling of Behavior_GetIfaceMaxScalarAttribute (FUN_062e6010,
// item 1769, common_behavior.cpp): where that one re-queries the interface
// every call, this one goes through the record's bPosValid cache. Returns the
// larger of the object's physical radius (+0x18, refreshed through the same
// slot-11 get_physical_radius guard RefreshPos uses) and its atmosphere range
// (slot 80, +0x140). `TEST AH,0x41; JNZ` at 062e60c1 makes the compare
// strictly-greater, so a tie -- and an unordered compare -- yields the radius.
// __cdecl, one pointer argument (`ADD ESP,4` at the 062e8089 call site).
float GetAiTargetEffectiveRadius(FAiTarget* Target); // FUN_062e6070, item 1770

// FAiTargetList, a.k.a. SystemManagerTimerTable (common_systemmanager.h) --
// SystemManager+0x34. Forward-declared rather than included: the type carries
// an 0xB0-byte blob that is really this file's own SustainedThresholdTimer, so
// common_systemmanager.h deliberately does not include common_aisteer.h and
// the dependency must not be closed in the other direction either.
struct SystemManagerTimerTable;

// Common.dll.c 0x062e7a30 (FUN_062e7a30), item 1803, raw disasm
// Common.dll.txt:244315-244935 (577 insns). PORTED 2026-09-17
// (claude:common-scan) on top of the complete structural decode the
// claude:common-guide pass left in the tracker note.
//
// "Pick the obstacle worth dodging": the owner sweeps its own bounding volume
// forward along its heading for a look-ahead time T and returns the NEAREST
// scanner contact that volume would run into. The result feeds straight into
// ComputeShapeBiasedAvoidDirection (FUN_062e55b0, item 1756) as its `Target`
// -- that one works out which way to dodge.
//
// RECEIVER, settled: retail is `__thiscall` with `RET 0x8` and the sole call
// site (.text:062de098) passes `LEA ECX,[EBX+0x34]`. The prior note guessed
// "NavHelper+0x34 ... { +0x00 IObjDB* Db; +0x04 SystemManager-ish* Mgr; }";
// it is in fact SystemManagerTimerTable, already fully typed, and all three
// fields this function touches are named members of it:
//   this+0x04 Owner          -- the SystemManager (its +0x14FC is the owner's
//                               own FAiTarget, its +0x08 the owner's Watcher)
//   this+0x08 ScannerEquipId -- get_equipment_status's `unsigned short const&`
//   this+0x1D bKeepCache     -- forwarded verbatim as get_scanlist's `bFlag`
// which is exactly the trio BuildAiTargetList (item 1800) reads from the same
// object. Exposed here as a free function taking the receiver explicitly, the
// same convention FindObjectDockingAtBase uses below.
//
// ExcludeId is compared against each candidate's get_id() and rejects on a
// match; TypeMask is ANDed with the candidate's cached get_type() flags.
// Returns the winning candidate's IObjInspect*, or null.
//
// Three polarity corrections to the inherited reject chain, all read off the
// raw FNSTSW/Jcc pairs (see the .cpp for the per-test derivation): the
// heading dot-product test keeps `dot >= 0` (ahead), the range test keeps
// `LenSq <= Reach*Reach` (within the look-ahead), and the size test keeps
// `CachedRadius <= 1000.0f` (small enough to be worth dodging). The note's
// own summary had all three inverted. The two geometry arms were likewise
// swapped in that summary: bit 29 of the candidate's type flags SET selects
// TransformedBoxesIntersect (item 1719), CLEAR selects the swept-sphere path
// through SweptSphereCollisionTime (item 1730).
IObjInspect* FindClosestObstacleToAvoid(SystemManagerTimerTable* Self,
                                                   unsigned ExcludeId,
                                                   unsigned TypeMask); // FUN_062e7a30, item 1803

// Common.dll.c 0x062e6150 (FUN_062e6150), item 1772. Small, cleanly decompiled.
// Negated direction/basis-row vector fetched through an unidentified
// interface's vtable slot 0xc; zero-fills on null Iface.
void GetNegatedIfaceDirection(Vector* OutDir, IObjInspect const* Iface);

// FUN_062e61a0, item 1773: |IObjInspect::get_velocity()| of Iface, 0.0f on null Iface.
// Returns double: retail leaves the FSQRT result on the x87 stack (no FSTP to a float
// before the RET), and every caller compares that unrounded value.
double GetIfaceVectorMagnitude(IObjInspect const* Iface);

// Common.dll.c 0x062e4910 (FUN_062e4910), raw disasm Common.dll.txt:239332-239562
// (223 insns), item 1748. RE-DERIVED AND PORTED 2026-09-17
// (claude:common-unaff-rederive). Was `blocked` on "pervasive register-allocation
// corruption across the ENTIRE body" -- unaff_EBX read as a value and
// unaff_retaddr written through as an array, with no visible definition. Both are
// simply PARAMETERS Ghidra never counted (it recovered 2 of 4):
//   * unaff_EBX     = the FIRST parameter, loaded into EBX at listing 239350 from
//                     E+4. It is the Transform every one of the eight
//                     TransformXform calls uses as its Xform argument.
//   * unaff_retaddr = the FOURTH parameter, loaded into EBP at 239359 from E+0x10.
//                     It really is an output array -- which is why Ghidra's
//                     "written through as an 0x18-element array" observation was
//                     half right -- but it is a parameter, not a stray register.
// The "8 near-identical repeated blocks threading results through aliased locals"
// are eight copies of one nine-instruction block, each with its own inlined lazy
// init of the DAT_063fca48 singleton; the aliasing Ghidra could not keep straight
// is just retail reusing two scratch slots (E-0x18 and E-0xc) as alternating
// source/destination between blocks.
//
// Signature: void __cdecl (Transform const* Xform /*E+4*/, Vector const* Min
//                          /*E+8*/, Vector const* Max /*E+0xC*/,
//                          Vector* OutCorners /*E+0x10, eight Vectors*/)
// Confirmed at its one call site (Common.dll.txt:239105-239113, inside
// FUN_062e4660/item 1747), which cleans exactly 0x10 bytes and passes the
// Transform straight out of the Elem's own iface slot +0x10. EAX at the return is
// the last corner's Y dword -- incidental, not a return value.
//
// It writes the eight corners of the axis-aligned box [Min, Max], each pushed
// through I3DMathEngine::TransformXform (slot +0x28), in a plain 3-bit order with
// X as the high bit and Z as the low one.
//
// ELEM RECORD: this FILLS IN part of the "bytes 0x1C-0x7B (opaque -- no reader
// found)" gap this header records for the Elem record above. The call site passes
// `ESI + 0x1c` as OutCorners, so Elem+0x1C..+0x7C is an eight-Vector (0x60-byte)
// cache of the object's world-space bounding-box corners. Its guard byte is
// Elem+0x7F (set to 1 at Common.dll.txt:239099, immediately before the call) --
// a FOURTH cache byte alongside the already-known +0x7C RangeValid, +0x7D
// FlagsValid and +0x7E PosValid.
void TransformBoundingBoxCorners(Transform const& Xform, Vector const& Min,
                                            Vector const& Max, Vector* OutCorners); // FUN_062e4910, item 1748

// Common.dll.c 0x062e55b0 (FUN_062e55b0), raw disasm Common.dll.txt:240578-240800
// (212 insns), item 1756. RE-DERIVED AND PORTED 2026-09-17
// (claude:common-unaff-rederive). Was `blocked` on unaff_EBP / unaff_ESI /
// unaff_retaddr "used as live values with no visible defining write", plus a
// tail that appeared to store the ADDRESS of a stack local into an output field.
// A complete 212/212 hand trace resolves all four:
//   * unaff_EBP     = the real SECOND parameter, Elem, loaded into EBP at listing
//                     240608 from E+8 and used only as `*(Elem+0x84)` -- the
//                     same "Elem's Iface" field the rest of this cluster uses.
//   * unaff_ESI     = the real THIRD parameter, the target IObjInspect, loaded
//                     into ESI at 240581 from E+0xC and null-tested immediately.
//   * unaff_retaddr = the E+0xC parameter slot REUSED as floating-point scratch
//                     once Target is no longer needed in memory (240643/240647/
//                     240720/240763 all write `[ESP+0x60]`). Ghidra's frame
//                     model has that address past the parameter area, hence the
//                     "retaddr" label.
//   * `*local_2c = (int)&fStack_44` was just the tail copy of
//                     I3DMathEngine::Rotate's output into the caller's buffer
//                     (240789-240797), mis-rendered by the same broken frame model.
// Ghidra recovered only one of the three parameters and shifted its frame, which
// is the single root cause of all of it.
//
// Signature: Vector* __cdecl (Vector* Out /*hidden struct return, E+4*/,
//                            void* Elem /*E+8*/, IObjInspect* Target /*E+0xC*/)
// Confirmed at its one call site, Common.dll.txt:229542-229547, which pushes
// (&local, EBX+0x14fc, FUN_062e7a30's return) -- i.e. the Elem argument is the
// familiar `SystemManager + 0x14FC`, and the Target comes straight out of item
// 1803.
//
// What it computes -- a lateral dodge direction, in world space:
//   1. Target->get_physical_radius(Radius, Centre) [+0x2c]. The radius is never
//      read again: retail dead code.
//   2. LocalPos = InverseTransform(Elem's own Transform, Centre) [+0x3c], i.e.
//      the obstacle's centre in the Elem's local frame.
//   3. If |LocalPos| < 1e-5 (the DOUBLE at .rdata:0639f418), give up and answer
//      with the local +X axis, (1,0,0).
//   4. Target->get_archetype_extents(Min, Max) [+0x28], then classify the box:
//      HorizontalHalf = ((Max.x-Min.x) + (Max.z-Min.z)) * 0.5 in DOUBLE
//      precision (.rdata:0639df88), VerticalExtent = Max.y-Min.y.
//      bFlatObstacle = HorizontalHalf/VerticalExtent > 2.0; bTallObstacle is the
//      reciprocal test. Mutually exclusive by construction.
//   5. bFlatObstacle squashes the local X (right) component by 0.3
//      (.rdata:063a0788) before renormalising, which biases the dodge towards
//      going over/under a wide flat obstacle; bTallObstacle squashes the local Y
//      (up) component instead, biasing towards going around a tall one. On the
//      tall path retail rescales only Y and X and leaves Z alone -- an asymmetry
//      that is reproduced as-is (it cannot change the answer, since Z is
//      discarded in step 6).
//   6. Direction = normalize2D(-X, -Y) with Z forced to exactly zero (retail
//      multiplies the reciprocal by .rdata:06399408 == 0.0f to get it).
//   7. Result = Rotate(Elem's Transform, Direction) [+0x30] -- rotation only, no
//      translation, so this stays a pure direction.
// Freelancer's local frame is X=right, Y=up, Z=forward, so step 6 is a purely
// lateral sidestep away from the obstacle -- consistent with the caller being on
// the obstacle-avoidance path.
Vector* ComputeShapeBiasedAvoidDirection(Vector* Out, FAiTarget* Elem, IObjInspect* Target); // FUN_062e55b0, item 1756

// Common.dll.c 0x062e5c00 (FUN_062e5c00), raw disasm Common.dll.txt:241288-241584
// (287 insns), item 1768. RE-DERIVED AND PORTED 2026-09-17
// (claude:common-unaff-rederive). This item was `blocked` on "severe Ghidra
// register-allocation corruption": unaff_EBX, unaff_ESI, unaff_retaddr, a
// pointer-reinterpreted-as-float, and `(float)param_1`. A complete 287/287 hand
// trace with byte-exact stack-depth tracking resolves ALL FIVE as ordinary
// artifacts -- Ghidra recovered only one of this function's four parameters, and
// its frame model was shifted, which cascaded into everything else:
//   * unaff_ESI    = the stack slot E-0x7c, written once with 1.0f at listing
//                    241297 and read only by the four early-out paths
//                    (`FLD [ESP+0x10]` at LAB_062e6000, 241579). The default
//                    return value, not a register.
//   * unaff_EBX    = E-0x78, the `float& Radius` out-parameter of
//                    get_physical_radius (zeroed at 241306, filled at 241307,
//                    read at 241351).
//   * unaff_retaddr= the THIRD parameter at E+0xC (`FLD [ESP+0x98]` at depth
//                    0x8c). Ghidra never counted it.
//   * (float)param_1 = the FOURTH parameter at E+0x10. Ghidra reused its single
//                    recovered parameter's variable for a different slot.
//   * `-(float)pfVar2` = simply `-TurnRadius`, reloaded from the scratch slot
//                    E-0x80 at 241434-241438. The same decompiler
//                    pointer-as-float bug item 1769 exhibits in miniature; that
//                    sibling was already disproven and ported, which is what
//                    made it the calibration case for this one.
// All four parameters confirmed at both call sites, each cleaning exactly 0x10
// bytes: 062d563f (Common.dll.txt:216309-216317, which passes cached
// *(Obj+0x60) and *(Obj+0x5c) as the two speeds) and 062ddb91 (229051).
//
// Signature: float __cdecl (IObjInspect* Iface, Vector const* Point,
//                          float AngularSpeed, float LinearSpeed)
//
// What it computes -- a steering cost in radians for reaching Point:
//   1. Frame = Iface->get_transform(), with its translation REPLACED by the
//      object's centre of mass from get_physical_radius. (Retail dispatches slot
//      +0x10, declared get_orientation, but copies a full 0x30-byte Transform;
//      that is only sound because retail folds slots 3 and 4 onto one
//      implementation -- see this header's get_transform/get_orientation note.
//      The port calls get_transform() so the types are honest.)
//   2. Bail (returning 1.0f) unless 0.01f < |Point - Centre| < max(Radius*5,100).
//      Constants verified: .rdata:0639e6b8 = 0.01f, 063a078c = 5.0f,
//      0639fb58 = 100.0f.
//   3. TurnRadius = LinearSpeed / AngularSpeed, where a zero argument means "not
//      cached" and is refilled from get_angular_speed_XY(0,1) [+0xf0] and
//      get_speed(1.0f, ThrustEquipEngine) [+0xd8] respectively. Bail unless
//      TurnRadius > 0.01f.
//   4. The two turn-circle centres: TransformXform(Frame, (+/-TurnRadius,0,0))
//      through the 3DMathEngine singleton (slot +0x28).
//   5. Bail unless Point lies INSIDE the nearer circle (min centre distance <
//      TurnRadius) -- i.e. only unreachable-without-turning-away targets cost
//      anything.
//   6. Result = max(dot(normalize(Point-Centre), -column2(Frame)), 0)^2
//               * MinCentreDist * AngularSpeed / LinearSpeed.
//      The exponent at .rdata:0639c420 is the double 2.0, so retail's _CIpow is
//      a square. The trailing factor has units of radians.
// RETAIL DEAD CODE, reproduced: get_throttle's out-parameter (E-0x70, listing
// 241294-241299) is written and never read by any later instruction.
// The result stays in ST0 unrounded in retail (062e5fd8-062e5ffb) and the MoveIdea caller
// compares it before rounding (062d5644), so the port returns double.
double EstimateTurnCircleApproachCost(IObjInspect* Iface, Vector const& Point,
                                      float AngularSpeed, float LinearSpeed); // FUN_062e5c00, item 1768

// Common.dll.c 0x062e6200 (FUN_062e6200), item 1775. RE-DERIVED 2026-09-17
// (claude:common-unaff-rederive): the "`unaff_retaddr` is the whole point of
// this function and cannot be recovered" blocker was a FALSE POSITIVE -- the
// fourth such false positive in this very cluster (compare items 1786, 1731/
// 1732, 1746). All 13 instructions (Common.dll.txt:241871-241884) are trivial
// once read as raw x86:
//     MOV  ECX,[ESP+4]          ; Iface
//     TEST ECX,ECX / JZ  fail
//     MOV  EAX,[ECX]            ; Iface's vtable
//     LEA  EDX,[ESP+4]          ; &(the caller's own argument slot!)
//     PUSH EDX
//     CALL [EAX+0x88]           ; __thiscall, ECX=Iface, callee pops the arg
//     TEST EAX,EAX / JNZ fail   ; nonzero == failure
//     MOV  EAX,[ESP+4]          ; re-read the slot the out-param just filled
//     RET
// So `unaff_retaddr` is Ghidra frame-tracking desync, cause (c): retail's
// callee pops its one argument (__thiscall/stdcall cleanup), Ghidra modelled
// it as caller-cleaned, so its view of ESP was 4 bytes low after the CALL and
// `[ESP+4]` looked like the return address instead of the argument slot.
// Retail deliberately reuses its OWN incoming parameter slot as the vtable
// call's out-param buffer, which is why there is "no defining write".
// Slot +0x88 is IObjInspect::get_type(uint& OutType) (common_iface.h slot 34,
// retail-ordered from ??_7IObjInspectImpl@@6BIObjInspect@@@), and it returns 0
// on success like every other IObjInspect getter -- so this is simply
// "Iface->get_type() or 0". That matches every known call site exactly: each
// one masks the result against a type-flag mask (0x3 in
// FindObjectDockingAtBase below, 0x30000 in BuzzBehavior::EvaluateReadiness,
// 0x7f0000 in DockBehavior/FollowBehavior::CheckClearanceProgress -- the same
// masks the already-ported IsWithinInterceptRange family uses on its own
// get_type results).
unsigned GetObjectTypeOrZero(IObjInspect const* Iface); // FUN_062e6200, item 1775

// Common.dll.c 0x062e7400 (FUN_062e7400), item 1797. RE-DERIVED 2026-09-17
// (claude:common-unaff-rederive): both halves of this item's blocker were
// FALSE POSITIVES.
//   * The dependency on item 1775 is resolved -- see GetObjectTypeOrZero above.
//   * `unaff_EDI` is not a loop-termination target at all. Retail's EDI is
//     just `MOV EDI,ECX` at Common.dll.txt:243660: this is a __thiscall method
//     (`RET 0x4`, one real stack parameter) and EDI is the cached `this`.
//     Ghidra typed the function __thiscall but then lost the ECX->EDI copy, so
//     `this` came back as an "unaffected incoming register". Ghidra ALSO
//     collapsed four distinct stack slots into one `local_10` (entry-0x10 for
//     the base id vs entry-0xC..entry-0x4 for the iterator state), which is
//     what made its rendered loop condition (`iStack_8 == unaff_EDI`)
//     nonsense; the raw listing's real compare is
//     `CMP ECX,[ESP+0xc]` at 243692, i.e. DockTarget == BaseId.
// `this` is NOT a class instance: every call site passes `NavHelper + 0x34`
// (Common.dll.txt:187926-187929 and 188234-188237, both inside DockBehavior --
// FUN_062c3370 and DockBehavior::CheckClearanceProgress), and the only thing
// this function reads through it is `*(IObjDB**)this`. So the parameter is
// "the address of the NavHelper field that holds the system's IObjDB".
// The enumeration is byte-for-byte the SAME IObjDB candidate walk already
// ported in ScanList::add_in_range (common_cescanner.cpp) and CAsteroidSystem::update
// (common_casteroid.cpp): IObjDB vtable slot 0, taking a 3-dword
// {cursor, cursor, TypeMask} state struct and returning the next matching
// IObjRW* (null when exhausted). TypeMask here is 0x20 and the entry guard
// masks the base's own type against 0x3, so in Freelancer's OBJECT_TYPE bit
// vocabulary (the same one the 0x30000/0x7f0000 ship masks come from) this is
// "given a moon/planet, walk the docking rings and return the one whose
// get_dock_target() equals that base's get_base() id" -- exactly the fallback
// DockBehavior::RecomputeApproach needs when a planet has no dock hardpoints
// of its own. Slots +0x8c/+0x90 are IObjInspect::get_base(uint&) /
// get_dock_target(uint&) (common_iface.h slots 35/36).
// Now SystemManagerTimerTable::FindObjectDockingAtBase (common_systemmanager.h).

// Common.dll.c 0x062e3460 (FUN_062e3460), item 1741. Raw disasm
// Common.dll.txt:237495-237739 (203 insns). cdecl, four arguments, returns
// bool in AL. "Build a perpendicular frame with angle-threshold rejection":
// false if |Direction|^2 < 1e-5 (double DAT_0639f418); NegDir =
// normalize(-Direction) (zero when its length is not above 0.01f); false if
// OutFrame is null; Reference defaults to (0,1,0); false if
// |clamp(dot(NegDir, Reference), -1, 1)| is not below DotThreshold. Otherwise
// Right = normalize(cross(Reference, NegDir)), Up = cross(NegDir, Right), and
// OutFrame = [Right | Up | NegDir] (column 0 at +0/+0xc/+0x18, column 1 at
// +4/+0x10/+0x1c, column 2 through MatrixSetColumn2 / FUN_062e3710).
// OutFrame is left untouched on every rejection.
bool BuildPerpendicularFrame(Vector const& Direction, Matrix* OutFrame, float DotThreshold, Vector const* Reference);

// Common.dll.c 0x062e2c00 (FUN_062e2c00), item 1739. Raw disasm
// Common.dll.txt:236827-236872 (31 insns, cleanly decompiled, no
// corruption). Small 3DMathEngine-singleton wrapper (Acquire3DMathEngine/
// DAT_063fca48 lazy-init, same convention as TransformInversePoint/item
// 1842): calls MathEngine->vtable[0x2c](this=Math, &Result, &M, &V) and
// returns Result.
//
// RESOLVED 2026-09-11 (d258:common-port): the item's previously-open
// question ("SrcArg's real type") is `Matrix const*` -- confirmed via
// ComputeBankedSteeringFrame's own call site below (Common.dll.txt:
// 238724-238733), where SrcArg is the very Matrix* ExternalIface's vtable
// slot 0xc returned moments earlier. Vtable slot 0x2c's (Dst, Matrix, Vector)
// call shape and behaviour (transform a Vector by a Matrix) match the
// already-identified FnTransformMatrix used by UpdateShipHeadingBlend above
// (same slot, same shape, same math-engine singleton) -- not given its own
// named function pointer typedef here since this is a one-call-site wrapper.
Vector TransformVectorByMatrix(Matrix const& M, Vector const& V);

// Common.dll.txt:236889-237493 (FUN_062e2c70), item 1740. 571 instructions and
// NO Ghidra pseudocode whatsoever -- the implementation in common_aisteer.cpp
// is a hand trace of the raw bytes and carries the full derivation, including
// the .rdata constants decoded from their bytes and the ESP-delta accounting
// that fixes the nine __cdecl arguments.
//
// Out = (P0 + P1) * Ratio + P2 * ZFactor, where P0/P1/P2 are Command's scalar
// projections onto Obj's own orientation columns, Ratio scales the pitch/yaw
// plane by how far Obj still has to turn (clamped by the throttle it can
// actually deliver there), and ZFactor scales the roll axis. With Obj == null
// the whole body collapses to Out = Command. Returns &Out, matching retail's
// EAX-on-exit, which every call site consumes.
//
// Obj is the caller's own bound Object (this->0x4), an IObjInspect; the six slots it
// touches (0x8, 0xc, 0x50, 0xe4, 0xec, 0xf8) are ordinary IObjInspect getters.
//
// ReferenceAngle and the bank flag come straight from the call sites (0.35f and
// true at 062d72ee, this->0x98 and true at 062d731b). TurnAngle is an acos
// result on the angle path and a literal 0.0f on the other, and the body
// special-cases exactly that 0.0f.
Vector* LimitAxisThrottleForTurn(Vector& Out, IObjInspect const* Obj, Vector Command,
                                            Matrix const* TargetFrame, float TurnAngle,
                                            float ReferenceAngle, bool bBankBlend);

// ----------------------------------------------------------------------------
// FUN_062e8e60 (item 1822, formerly BlendNudgeIntoPredictedDirection) is
// ThrustManager::BlendNudgeDirection (common_systemmanager.h, common_thrustmanager.cpp).
// Retail callers: DodgeIdea::Update (062d849e) and PathfindManager (062dd8f7).

// ----------------------------------------------------------------------------
// ComputeBankedSteeringFrame -- Common.dll.c 0x062e37e0 (FUN_062e37e0), raw
// disasm Common.dll.txt:237847-238755 (866 insns), cdecl, caller cleans 0x14.
//
// Elem is the SystemManager+0x14FC view (IObjInspect at Elem+0x84, cached
// get_type flags at Elem+8, their valid byte at Elem+0x7d). TargetDir is the
// desired facing, WorkingFrame (in/out) the ship's orientation, BankVector
// (in/out) the bank state, Mode selects whether BankVector is first taken
// into the iface's local frame (TransposeTransform) or used as is.
//
// Returns false without touching WorkingFrame / BankVector when the flags
// lack bit 0x30000 or (Mode != 0) the frame is already within 0.98 of the
// iface forward. Otherwise WorkingFrame is always rebuilt by
// BuildOrientationBasis and BankVector re-expressed through the iface
// transform; the return value says whether the steer branch ran (TargetDir
// not within 0.999 of the iface forward, and not behind the iface up axis
// unless the object is the player). See the .cpp for the section-by-section
// listing addresses.
bool ComputeBankedSteeringFrame(FAiTarget* Elem, Vector const& TargetDir, Matrix& WorkingFrame, Vector& BankVector, unsigned char Mode);

// LAB_062d9d00 (item 1649, formerly the free UpdateSplineSteeringTask(void* Self, float Dt))
// is SplineMoveIdea::Update (common_idea.h, common_splinemoveidea.cpp).

// SustainedThresholdTimer (FUN_062e6a20) is declared in common_systemmanager.h, next to the
// timer table that holds eleven of them.

// Common.dll.c 0x062e60e0 (FUN_062e60e0), raw disasm Common.dll.txt:241693-
//241744 (35 insns). Item 1771. Ghidra's own decompile of this one is
// corrupted (reads a bogus "unaff_ESI" -- a plain register-allocation
// artifact); fully re-derived here from the raw disassembly directly. Widely
// used leaf helper (21 call sites across the module, one of them right here
// in this cluster -- CanFireLauncherAtTarget's fast path, common_aifirecontrol.cpp,
// where it is currently stubbed as a documented no-op TODO for item 1771; now
// resolved, that stub can be wired up to this real port): fetches a position
// Vector3 through an unidentified interface pointer's vtable slot 0x2c
// (Method(this, void** UnusedOut, Vector* Out) -- the middle out-param is
// written back into a stack copy of the Iface pointer itself and never read
// again by any caller found in this pass, kept only for byte-exact call-shape
// fidelity) with a zero-fallback: if Iface is null, OR the vtable+0x2c call
// returns non-zero (failure, after which vtable slot 0x20 -- presumably a
// release/cleanup -- is also called), *OutPos is zero-filled instead of being
// written from the call.
void GetIfacePositionOrZero(Vector* OutPos, IObjInspect const* Iface);

// The AI fire-control functions FUN_062ea050..FUN_062ebb10 are members of
// SystemManagerGunGroupManager / FCmDropperWatcher (common_systemmanager.h); their
// definitions are in common_aifirecontrol.cpp. FUN_062e8400 is
// SystemManagerTimerTable::HasTargetInForwardArc.

// Common.dll.c 0x062eb020 (FUN_062eb020), Common.dll.txt 062eb020-062eb068 (27 insns).
// Item 1842. A thiscall helper on a Transform, RET 8: *Out = the math engine's
// InverseTransform (+0x3c) of *In by *Self (through a 12-byte local); returns Out.
Vector* TransformInversePoint(Transform const* Self, Vector* Out, Vector const* In);

// The SystemManager+0x1364 block's ctor / Update / SetFormationLeader / SetFollowLeader /
// SyncFormationLeader (FUN_062ebbc0 / 062ebcb0 / 062ec870 / 062ec970 / 062ec6b0) are
// FGunConvergenceStateBlock members now (common_systemmanager.h,
// common_gunconvergence.cpp).

static_assert(offsetof(ICRSplineSegment, P0) == 0x00, "ICRSplineSegment+0x00 P0");
static_assert(offsetof(ICRSplineSegment, P1) == 0x0c, "ICRSplineSegment+0x0c P1");
static_assert(offsetof(ICRSplineSegment, P2) == 0x18, "ICRSplineSegment+0x18 P2");
static_assert(offsetof(ICRSplineSegment, P3) == 0x24, "ICRSplineSegment+0x24 P3");
static_assert(offsetof(ICRSplineSegment, ArcLength) == 0x30, "ICRSplineSegment+0x30 ArcLength");
static_assert(offsetof(ICRSplineSegment, SampleCount) == 0x34, "ICRSplineSegment+0x34 SampleCount");
static_assert(offsetof(ICRSplineSegment, Reserved38) == 0x38, "ICRSplineSegment+0x38 Reserved38");
static_assert(sizeof(ICRSplineSegment) == 0x40, "ICRSplineSegment sizeof 0x40 (proven by operator= COMDAT fold with GunUseStruct)");

// LAB_062d68b0 (item 1603) and LAB_062d7950 (item 1618) are FaceIdea::Update and
// DodgeIdea::Update (common_faceidea.cpp / common_dodgeidea.cpp).

// ----------------------------------------------------------------------------
// TurnHelper::get_angular_throttle -- retail 0x062e28f0, decomp-tracker item
// 71665, mangled `?get_angular_throttle@TurnHelper@@YA?AVVector@@ABVMatrix@@0PAM@Z`
// (a STATIC member of a retail `TurnHelper` class -- no other TurnHelper
// members were found anywhere else in this module, so this stub struct only
// carries the one method actually needed by the SteeringTask family below).
//
// Ghidra's own signature guess (Matrix*,Matrix*,float*) is WRONG for the 3rd
// argument's true role: the demangled name proves the real parameter list is
// (const Matrix& A, const Matrix& B, float* OutAngle) behind a hidden
// Vector-by-value return pointer -- FOUR incoming stack dwords total, one
// more than Ghidra auto-detected (it silently dropped OutAngle, which is read
// directly via raw ESP math at the tail instead of through a named param).
// This resolves the item's long-standing "param_3's true size/type" blocker.
//
// Full pipeline, hand-traced end to end against Common.dll.txt:236552-236812
// (all cited .rdata FPU constants decoded from raw bytes this pass):
//   1. TransposedA = Transpose(A)                       [Matrix ctor, retail FUN_06284f70]
//   2. RelativeMat = MulMatrix(A=B, B=TransposedA)       [Acquire3DMathEngine +0x20]
//   3. Quat(W,X,Y,Z) = MatrixToQuaternion(RelativeMat)   [+0x50]
//   4. Angle2 = 2*acos(clamp(W,-1,1))                    (candidate *OutAngle)
//   5. Axis = (asin(X),asin(Y),asin(Z)), normalized when |Axis|^2 > 1e-5f
//   6. LocalAxis = TransposeTransform(A, Axis)           [+0x38] (world->local)
//   7. dominant-plane scale: if max(|LocalAxis.x|,|LocalAxis.y|) > 0.001,
//      scale both by 1/max (FDIVR double 1.0, no square root); Z snapped to
//      +1 (z > 0) or -1 when |Z| > 0.1, otherwise kept
//   8. WorldThrottle = TransformMatrix(A, ScaledLocalAxis) [+0x2c] (local->world)
//   9. if Angle2 > +pi:      WorldThrottle = -WorldThrottle; Angle2 = 2*pi - Angle2
//      else if Angle2 < -pi: WorldThrottle = -WorldThrottle; Angle2 = Angle2 + 2*pi
//  10. if OutAngle != null: *OutAngle = Angle2 (post step-9 adjustment)
//  return WorldThrottle
//
// Confidence: every math-engine vtable slot (+0x20/+0x38/+0x2c/+0x50) and
// every .rdata constant (DAT_06399404=-1.0f, DAT_0639a1d0=1.0f,
// DAT_0639b984=1e-5f (0x3727c5ac), DAT_06399410=1.0(double), DAT_0639c9f0=0.001(double,
// byte-exact IEEE-754 match), DAT_063a0460=0.1(double, byte-exact),
// DAT_0639c430=0.0(double), DAT_0639d208=+pi(float, byte-exact
// 0x40490FDB), DAT_0639d2a0=-pi(float, byte-exact 0xC0490FDB),
// DAT_0639c9e8=2*pi(float, byte-exact 0x40C90FDB)) were independently
// decoded from the raw bytes and the push-order convention was cross-checked
// against 3 already-ready call sites elsewhere in this module
// (common_cobject.cpp's MulMatrix/TransformMatrix uses, common_cequip.cpp's
// MatrixToQuaternionViaMathEngine). Residual risk is limited to manual
// ESP-relative offset bookkeeping across ~30 chained stack ops -- the
// high-level data flow (which buffer feeds which call) was independently
// re-derived twice and both derivations agreed.
// A namespace in retail, not a class: ?get_angular_throttle@TurnHelper@@YA?AVVector@@ABVMatrix@@0PAM@Z
// is a free __cdecl function (YA), and nothing else of TurnHelper is exported.
namespace TurnHelper
{
    COMMON_API Vector __cdecl get_angular_throttle(Matrix const& A, Matrix const& B, float* OutAngle);
}

// FUN_062d76d0, item 1615. rand()-weighted 4-way state picker: State[0] is
// the current 1..4 tag (the entry matching it is treated as 0 rather than
// re-read -- "don't re-pick the current state"), State[1..4] are 4 float
// weights. Draws `rand() * (1/32768) * (sum of the masked weights)` and walks
// a cumulative-threshold ladder to land on a new 1..4 tag, writes it back to
// State[0] and returns it. Ported for real this pass (previously understood
// but not coded) -- used directly by DodgeIdea::Update's case-0 axis
// picker below.
int GenericWeightedPick(int* State);

// LAB_062d68b0, item 1603, is FaceIdea::Update (common_idea.h), defined in
// common_faceidea.cpp; the steering helpers it shares with DodgeIdea::Update are in
// common_aisteer.cpp (common_aisteer_internal.h).

// LAB_062d7950, item 1618, is DodgeIdea::Update (common_idea.h), defined in
// common_dodgeidea.cpp; the raw-offset free function SteeringTaskLarge_Update is gone.
