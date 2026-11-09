// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll AI obstacle avoidance: ObstacleAvoidance (PathfindManager+0x268), the
// obstacle-candidate / heading-blend helpers (retail 0x062dbbe0-0x062dc840,
// 0x062de220-0x062df3a0, 0x062e74e0) and FindClosestObstacleToAvoid (0x062e7a30).
// See src/include/common_aisteer.h for the subsystem writeup.
//
// CMN-SPLIT stage 1 (2026-10-14): split out of common_aisteer.cpp, code unchanged.

#include "common_aisteer.h"
#include "common_aisteer_internal.h"
#include "x86math.h"
#include "common_csys.h"          // Acquire3DMathEngine, Matrix
#include "common_physys.h"        // PhySys::FindRayCollisions, RayHit, Quaternion
#include "common_misc.h"          // FastInvSqrt, SUBOBJ_ID_NONE
#include "common_cequip.h"
#include "common_cobject.h"       // CObject::ClassShip, CShip
#include "common_behavior.h"      // Behavior_* helpers
#include "common_systemmanager.h" // SystemManager, SystemManagerTimerTable
#include "common_pathfind.h"      // PathfindManager (embeds ObstacleAvoidance at +0x268)
#include "common_cobjdb.h"        // IObjDB
#include "common_spatial.h"       // ISpatialPartition, FSpatialQueryResult (ClassifyShipObstaclePhase)

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

using namespace CmnAiSteer;

namespace
{
    // DAT_063fc9a0 / DAT_063fca14. A 25-dword table in uninitialised .data (all zero) that
    // FUN_062de490 shuffles once (062de7c8-062de843) and nothing ever reads: the XREF list of
    // DAT_063fc9a0 holds only the eight shuffle instructions. Since every entry is zero the
    // XOR swaps leave it zero; the observable effect is the 50 rand() calls of the first tick.
    unsigned GAvoidIndexTable[25] = {};
    unsigned GAvoidIndexTableShuffled = 0;

    // DAT_063ec6a8, a writable .data float (2.0f) whose only reference is the read at
    // .text:062debe8.
    float GAvoidTurnGain = 2.0f;
}

// ============================================================================
// ObstacleAvoidance -- construction and owner bind (PathfindManager+0x268)
// ============================================================================

ObstacleAvoidance::ObstacleAvoidance()
{
    // Inlined into FUN_062db3b0: .text:062db4a6-062db4ca builds Slots with
    // `eh vector constructor iterator(this+0x14, 0x1c, 0x19, Watchable::Watchable)`
    // (0x062db770, `MOV [ECX],0` -- the slot's first dword, Timer), .text:062db4d5
    // CursorIndex = 0, .text:062db4db FUN_062db780 on this+0x2d4.
    for (int Index = 0; Index < 25; ++Index)
    {
        Slots[Index].Timer = 0.0f;
    }
    CursorIndex = 0;
    ResetContact();
}

void ObstacleAvoidance::ResetContact()
{
    // FUN_062db780 (Common.dll.txt 225749-225760), ECX = this+0x2d4, all 8 instructions:
    // byte [+0] (Active), byte [+0x38] (HasContact), dword [+0x1c] (ContactObjectId),
    // dword [+0x40] (ContactCloseRate), dword [+0x3c] (ContactTimer) -- all 0, in that order.
    Active = 0;
    HasContact = 0;
    ContactObjectId = 0;
    ContactCloseRate = 0.0f;
    ContactTimer = 0.0f;
}

void ObstacleAvoidance::AttachOwner(IBehaviorManager* NewOwner)
{
    // FUN_062de220 (Common.dll.txt 229711-229760), RET 4, all 41 instructions.
    //   062de22c  Owner = NewOwner; 062de22e ElapsedTime = 0
    //   062de231  Obj = [NewOwner+0xf0] - 8 (0 when unset), Obj->get_archetype_extents(Min,
    //             Max) (+0x28; last push &Min is the first argument), result dropped
    //   062de24d  +0x08 = -Min.z (FCHS)
    //   062de256  A = |Max.x - Min.x|, B = |Max.y - Min.y| (B spilled to the arg slot)
    //   062de26e  FCOM A,B / TEST AH,0x41 / JNZ: when A < B, A == B or unordered the
    //             spilled B is stored, otherwise A -- +0x0C = (A > B) ? A : B.
    Owner = NewOwner;
    ElapsedTime = 0.0f;

    Vector Min;
    Vector Max;
    PathfindOwnerObject(NewOwner)->get_archetype_extents(Min, Max);

    NoseDistance = -Min.z;

    float const SpanX = std::fabs(Max.x - Min.x);
    float const SpanY = std::fabs(Max.y - Min.y);
    Breadth = (SpanX > SpanY) ? SpanX : SpanY;
}

float ObstacleAvoidance::GetCloseRateScale() const
{
    // FUN_062df2d0 (Common.dll.txt 231006-231050), all 41 instructions traced.
    if (Active == 0)                                                // 062df2d1
    {
        return 1.0f;
    }
    // 062df2db-062df2ed: the owner's watched object, `W == 0 || W - 8 == 0` -> 1.0f.
    if (Owner->WatchablePtr == nullptr || ObjRwFromWatchable(Owner->WatchablePtr) == nullptr)
    {
        return 1.0f;
    }
    // 062df2ef-062df300: above 5.0 (double DAT_0639fa58; TEST AH,0x41 / JNZ) -> 0.8f.
    if (ContactCloseRate > 5.0)
    {
        return 0.8f;
    }
    // 062df30a-062df31b: below 1.0 (TEST AH,0x5 / JP leaves >=, unordered) -> 0.4f.
    if (ContactCloseRate < 1.0)
    {
        return 0.4f;
    }
    // 062df325-062df34c: ((rate - 1.0) * 0.25)^2 (_CIpow with the double 2.0) * 0.4f + 0.4f.
    double const Excess = ((double)(ContactCloseRate) - 1.0) * 0.25;
    return (float)(std::pow(Excess, 2.0) * 0.4f + 0.4f);
}

bool ObstacleAvoidance::GetAvoidDirection(Vector* OutDirection, unsigned* OutObjectId) const
{
    // FUN_062df360 (Common.dll.txt 231061-231080), all 20 instructions.
    if (Active == 0)
    {
        return false;
    }
    *OutDirection = AvoidDirection;
    *OutObjectId = ContactObjectId;
    return true;
}

bool ObstacleAvoidance::GetAvoidPoint(Vector* OutPoint, unsigned* OutObjectId) const
{
    // FUN_062df3a0 (Common.dll.txt 231093-231112), all 20 instructions.
    if (Active == 0)
    {
        return false;
    }
    *OutPoint = AvoidPoint;
    *OutObjectId = ContactObjectId;
    return true;
}

void ObstacleAvoidance::Update(float Dt)
{
    // FUN_062de490 (Common.dll.txt 229963-230987), all 978 instructions traced. Values the
    // listing keeps on the x87 stack are held in double; a float local is a `FSTP dword`.

    // 062de49a-062de4b3: the watched object (0 when nothing is watched), dereferenced
    // below without a test.
    IObjRW* const Ship = PathfindOwnerObject(Owner);

    // 062de4b3-062de4ed: age the slot timers. `Timer > 0` (TEST AH,0x41 / JNZ skips <=, ==
    // and unordered); the stored float is Timer - Dt, and the unrounded difference is
    // compared with 0.0f (TEST AH,0x5 / JP skips >=, unordered) to clamp it to 0.
    for (int Index = 0; Index < 25; ++Index)
    {
        Slot& Entry = Slots[Index];
        if (Entry.Timer > 0.0f)
        {
            double const Left = (double)(Entry.Timer) - Dt;
            Entry.Timer = (float)(Left);
            if (Left < 0.0)
            {
                Entry.Timer = 0.0f;
            }
        }
    }

    // 062de4ef-062de51c
    ElapsedTime = Dt + ElapsedTime;
    {
        double const Left = (double)(ContactTimer) - Dt;
        ContactTimer = (float)(Left);
        if (Left < 0.0)                              // double 0.0 (DAT_0639c430)
        {
            ContactTimer = 0.0f;
        }
    }

    // 062de522-062de551: no physics system bound.
    if (PhysSystemId == 0)
    {
        ContactObjectId = 0;
        ContactCloseRate = 0.0f;
        ContactTimer = 0.0f;
        Active = 0;
        HasContact = 0;
        return;
    }

    // 062de554-062de5fd: get_velocity (slot 1, +0x04); its fast length is kept as a float
    // (ESP+0x7c, read again at 062df079) and the unrounded length is compared with 1.0f:
    // TEST AH,0x5 / JP continues on >= and unordered, so only a length below 1 bails.
    Vector const Velocity = Ship->get_velocity();
    double const SpeedExt = InlineFastLength((double)(Velocity.x) * Velocity.x
                                             + (double)(Velocity.y) * Velocity.y
                                             + (double)(Velocity.z) * Velocity.z);
    float const Speed = (float)(SpeedExt);
    if (SpeedExt < 1.0)
    {
        ContactObjectId = 0;
        ContactCloseRate = 0.0f;
        ContactTimer = 0.0f;
        Active = 0;
        HasContact = 0;
        return;
    }

    // 062de600-062de614: fmod(6.89f * ElapsedTime, 2*pi (double DAT_0639c9e0)), stored as a
    // float (ESP+0x10). Only the probe spiral below reads it; the slot is reused from
    // 062de95e on.
    float const SpiralAngle = (float)(
        std::fmod((double)(6.89f) * ElapsedTime, 6.2831854820251465));

    // 062de618-062de6b6: retail calls slot 4 (+0x10, get_orientation), which shares its body
    // with slot 3 (get_transform, 0x062bf1f0) and reads 12 floats off the result -- the
    // Transform. The pointer is kept (ESP+0x8c) and handed to FUN_062de2a0 at the end.
    Transform const& ShipXform = Ship->get_transform();

    // Forward = -column 2 of the rotation, built twice in retail: once from the live
    // transform (ESP+0x70, 062de621-062de649) and once from the 9-dword copy (ESP+0x9c,
    // 062de685-062de6b6) -- equal values.
    Vector const Forward{ -ShipXform.Rotation.E[0][2], -ShipXform.Rotation.E[1][2],
                          -ShipXform.Rotation.E[2][2] };

    // 062de64e-062de672: Nose = Translation + Forward * NoseDistance (FUN_06288830, then
    // FUN_062887d0), each component a float.
    Vector const NoseOffset{ Forward.x * NoseDistance, Forward.y * NoseDistance, Forward.z * NoseDistance };
    Vector const Nose{ ShipXform.Translation.x + NoseOffset.x, ShipXform.Translation.y + NoseOffset.y,
                       ShipXform.Translation.z + NoseOffset.z };

    // 062de677-062de683: REP MOVSD of the 9 rotation dwords (ESP+0xe8).
    Matrix const Rotation = ShipXform.Rotation;

    // 062de6bb-062de6d3: LookAhead = 50.0f * NoseDistance (FST to ESP+0x24); the unrounded
    // product compared with 0.0f -- TEST AH,0x44 / JNP bails only on "equal".
    double const LookAheadExt = 50.0f * (double)(NoseDistance);
    float LookAhead = (float)(LookAheadExt);
    if (LookAheadExt == 0.0)
    {
        Active = 0;                                  // 062defb8
        HasContact = 0;
        ContactObjectId = 0;
        ContactCloseRate = 0.0f;
        ContactTimer = 0.0f;
        return;
    }

    // 062de6d9-062de6f0: TEST AH,0x41 / JNZ keeps <=, == and unordered; above 750 clamps.
    if (LookAhead > 750.0f)
    {
        LookAhead = 750.0f;
    }

    // 062de6f4-062de730: probe spiral in ship space -- radius 900.0f / LookAhead * Breadth
    // kept on the stack, angle SpiralAngle, depth -900.0f.
    double const SpiralRadius = 900.0f / (double)(LookAhead) * Breadth;
    Vector const ProbeLocal{ (float)(std::cos(static_cast<double>(SpiralAngle)) * SpiralRadius),
                             (float)(std::sin(static_cast<double>(SpiralAngle)) * SpiralRadius),
                             -900.0f };

    // 062de737-062de788: rotated into world space (3DMathEngine +0x2c, DAT_063fca10 is the
    // function's cached engine) and added to the nose.
    I3DMathEngine* const Math = Acquire3DMathEngine();
    Vector ProbeOffset;
    Math->TransformMatrix((FVector&)(ProbeOffset), (FMatrix const&)(Rotation),
                          (FVector const&)(ProbeLocal));
    Vector const ProbeEnd{ Nose.x + ProbeOffset.x, Nose.y + ProbeOffset.y, Nose.z + ProbeOffset.z };

    // 062de78d-062de7be: up to 8 hits along Nose -> ProbeEnd (hit buffer ESP+0x130).
    PhySys::RayHit Hits[8];
    int const HitCount = PhySys::FindRayCollisions(PhysSystemId, Nose, ProbeEnd, Hits, 8);

    // 062de7b4-062de7c2: get_type (slot 34, +0x88) into ESP+0xb4; hits of the ship's own
    // type are ignored below. Retail does not initialise the dword; IObjInspectImpl::get_type
    // stores 0 into it first on every path, so the zero here changes nothing.
    unsigned OwnType = 0;
    Ship->get_type(OwnType);

    // 062de7c8-062de843: the one-time XOR shuffle of the (all-zero, never read) table.
    if (GAvoidIndexTableShuffled == 0)
    {
        for (int Round = 0; Round < 25; ++Round)
        {
            int const A = (std::rand() * 25) >> 15;
            int const B = (std::rand() * 25) >> 15;
            GAvoidIndexTable[A] ^= GAvoidIndexTable[B];
            GAvoidIndexTable[B] ^= GAvoidIndexTable[A];
            GAvoidIndexTable[A] ^= GAvoidIndexTable[B];
        }
        GAvoidIndexTableShuffled = 1;
    }

    // 062de84d-062de8b5: the steering target cached at PathfindManager+0x200; it counts when
    // its squared length (z*z + y*y + x*x) is above 1.0f (TEST AH,0x41 / JNZ).
    Vector const Target = Owner->Pathfind->CachedTargetPosition;
    bool const bHaveTarget = ((double)(Target.z) * Target.z + (double)(Target.y) * Target.y
                              + (double)(Target.x) * Target.x) > 1.0;

    // 062de8ba-062dea68: walk the hits.
    bool bBlocked = false;
    HasContact = 0;
    for (int HitIndex = 0; HitIndex < HitCount; ++HitIndex)
    {
        PhySys::RayHit const& Hit = Hits[HitIndex];
        CObject* const HitObject = Hit.Object;
        if (HitObject == nullptr)
        {
            continue;
        }
        if (((unsigned)HitObject->ObjectClass & 3) != 3)
        {
            continue;
        }
        CSimple const* const HitSimple = (CSimple const*)HitObject;
        unsigned const HitType = HitSimple->TypeBits;
        if ((HitType & 0x20c150) == 0 || HitType == OwnType)
        {
            continue;
        }

        // 062de918-062de982: fast distance nose -> hit point, stored as a float (ESP+0x58).
        Vector const ToHit{ Hit.Point.x - Nose.x, Hit.Point.y - Nose.y, Hit.Point.z - Nose.z };
        float const HitDistance = (float)(InlineFastLength(
            (double)(ToHit.z) * ToHit.z + (double)(ToHit.y) * ToHit.y
            + (double)(ToHit.x) * ToHit.x));

        // 062de986-062de9f8: a target nearer than the hit (TEST AH,0x5 / JNP: "less") drops
        // the whole avoidance state and returns.
        if (bHaveTarget)
        {
            Vector const ToTarget{ Target.x - Nose.x, Target.y - Nose.y, Target.z - Nose.z };
            double const TargetDistance = InlineFastLength(
                (double)(ToTarget.z) * ToTarget.z + (double)(ToTarget.y) * ToTarget.y
                + (double)(ToTarget.x) * ToTarget.x);
            if (TargetDistance < HitDistance)
            {
                Active = 0;                          // 062defb8
                HasContact = 0;
                ContactObjectId = 0;
                ContactCloseRate = 0.0f;
                ContactTimer = 0.0f;
                return;
            }
        }

        // 062de9fe-062dea53: every surviving hit marks the contact; one nearer than LookAhead
        // (TEST AH,0x5 / JP skips >=) also records its point and normal.
        HasContact = 1;
        ContactObjectId = HitSimple->Id;
        if (HitDistance < LookAhead)
        {
            ContactPoint = Hit.Point;
            ContactNormal = Hit.Normal;
            bBlocked = true;
        }
    }

    if (!bBlocked)
    {
        // 062dea7a-062ded51. Entered only when ContactTimer < 0.0 (TEST AH,0x5 / JP leaves on
        // >=, unordered) -- the clamp above never leaves it negative, so retail never takes
        // this branch either; it is kept as written.
        if (!(ContactTimer < 0.0f) || !bHaveTarget)
        {
            Active = 0;                              // 062ded56
            return;
        }

        PhySys::RayHit TargetHit[1];
        if (PhySys::FindRayCollisions(PhysSystemId, Nose, Target, TargetHit, 1) <= 0)
        {
            Active = 0;
            return;
        }

        // 062deac8-062deb42: unit direction nose -> target (FSQRT, FDIVR 1.0f).
        Vector ToTarget{ Target.x - Nose.x, Target.y - Nose.y, Target.z - Nose.z };
        double const InvLength = 1.0f / std::sqrt((double)(ToTarget.z) * ToTarget.z
                                                  + (double)(ToTarget.y) * ToTarget.y
                                                  + (double)(ToTarget.x) * ToTarget.x);
        ToTarget.x = (float)(ToTarget.x * InvLength);
        ToTarget.y = (float)(ToTarget.y * InvLength);
        ToTarget.z = (float)(ToTarget.z * InvLength);

        // 062deb44-062debbb: Axis = Forward x ToTarget, Dot = Forward . ToTarget (unrounded).
        Vector const Axis{
            (float)((double)(ToTarget.z) * Forward.y - (double)(Forward.z) * ToTarget.y),
            (float)((double)(Forward.z) * ToTarget.x - (double)(ToTarget.z) * Forward.x),
            (float)((double)(ToTarget.y) * Forward.x - (double)(Forward.y) * ToTarget.x) };
        double const Dot = (double)(ToTarget.z) * Forward.z + (double)(Forward.y) * ToTarget.y
                           + (double)(Forward.x) * ToTarget.x;

        // 062debbd-062decab: |Dot| > 0.98 (double DAT_063a0610; TEST AH,0x41 / JNZ rotates on
        // <=, == and unordered) keeps ToTarget; otherwise Forward is turned about Axis by the
        // half angle (1.0 - Dot) * 2.0f (DAT_063ec6a8) * 0.5f.
        Vector Heading;
        if (std::fabs(Dot) > 0.98)
        {
            Heading = ToTarget;
        }
        else
        {
            double const HalfAngle = (1.0 - Dot) * GAvoidTurnGain * 0.5f;
            FQuaternion Turn;
            Turn.W = static_cast<float>(std::cos(static_cast<double>(HalfAngle)));
            float const AxisScale = (float)(std::sin(static_cast<double>(HalfAngle)) / std::sqrt(
                (double)(Axis.z) * Axis.z + (double)(Axis.y) * Axis.y
                + (double)(Axis.x) * Axis.x));
            Turn.X = Axis.x * AxisScale;                 // FUN_06288830
            Turn.Y = Axis.y * AxisScale;
            Turn.Z = Axis.z * AxisScale;
            FMatrix TurnMatrix;
            Math->QuaternionToMatrix(TurnMatrix, Turn);  // +0x54
            Math->TransformMatrix((FVector&)(Heading), TurnMatrix,
                                  (FVector const&)(Forward)); // +0x2c
        }

        // 062decaf-062ded51, 062df2a8: AvoidDirection = Heading, AvoidPoint = Nose +
        // Heading * 1000.0f (DAT_063a0608). Active is left as it is.
        AvoidDirection = Heading;
        Vector const Reach{ Heading.x * 1000.0f, Heading.y * 1000.0f, Heading.z * 1000.0f };
        AvoidPoint = Vector{ Reach.x + Nose.x, Reach.y + Nose.y, Reach.z + Nose.z };
        return;
    }

    // 062ded6a-062ded8d: blocked -- three random rays into a cone of radius LookAhead * 0.5
    // (double DAT_0639df88) at depth -LookAhead.
    Active = 1;
    float const ConeRadius = (float)(LookAhead * 0.5);
    float const ConeDepth = -LookAhead;
    for (int Attempt = 3; Attempt != 0; --Attempt)
    {
        // 062ded94-062dede6: radius rand() * (1/32768) * ConeRadius (float), angle rand() *
        // (1/32768) * 2*pi (double) kept on the stack.
        float const Radius = (float)((double)(std::rand()) * 3.0517578125e-05f * ConeRadius);
        double const Angle = (double)(std::rand()) * 3.0517578125e-05f * 6.2831854820251465;
        Vector const ConeLocal{ (float)(std::cos(static_cast<double>(Angle)) * Radius),
                                (float)(std::sin(static_cast<double>(Angle)) * Radius), ConeDepth };

        // 062dedf6-062dee7f: world end point (+0x2c) and the nose -> end offset.
        Vector ConeOffset;
        Math->TransformMatrix((FVector&)(ConeOffset), (FMatrix const&)(Rotation),
                              (FVector const&)(ConeLocal));
        Vector const ConeEnd{ ConeOffset.x + Nose.x, ConeOffset.y + Nose.y, ConeOffset.z + Nose.z };
        Vector ConeDirection{ ConeEnd.x - Nose.x, ConeEnd.y - Nose.y, ConeEnd.z - Nose.z };

        // 062dee84-062deed8: the offset is normalised ((y*y + z*z) + x*x, FSQRT, FDIVR 1.0f)
        // and never read again.
        double const InvConeLength = 1.0f / std::sqrt((double)(ConeDirection.y) * ConeDirection.y
                                                      + (double)(ConeDirection.z) * ConeDirection.z
                                                      + (double)(ConeDirection.x) * ConeDirection.x);
        ConeDirection.x = (float)(ConeDirection.x * InvConeLength);
        ConeDirection.y = (float)(ConeDirection.y * InvConeLength);
        ConeDirection.z = (float)(ConeDirection.z * InvConeLength);

        // 062deedc-062deee6: one hit along Nose -> ConeEnd (buffer ESP+0xc8).
        PhySys::RayHit ConeHit[1];
        if (PhySys::FindRayCollisions(PhysSystemId, Nose, ConeEnd, ConeHit, 1) <= 0)
        {
            continue;
        }

        // 062deeec-062def43: pick a slot. Walking K = 0..24 from Cursor = CursorIndex, the
        // slot (Cursor + K) % 25 (signed IDIV) is tested: an expired one (Timer <= 0, TEST
        // AH,0x41 / JNP) is taken at once and CursorIndex becomes K -- the step count, not the
        // slot index. Otherwise the lowest timer below 1e8f (DAT_063a0604) is tracked, but
        // what retail remembers is K, and after the loop it takes Slots[K] -- a slot that is
        // generally not the one it compared. Kept as retail, including the index -1 (an
        // address 0x1c bytes before Slots) left when no timer is below 1e8f, which the 1.0f
        // timers this function writes cannot produce.
        ObstacleAvoidance::Slot* Chosen = nullptr;
        {
            float Lowest = 100000000.0f;
            int LowestStep = -1;
            int Cursor = CursorIndex;
            int Step = 0;
            for (; Step < 25; ++Step, ++Cursor)
            {
                ObstacleAvoidance::Slot& Candidate = Slots[Cursor % 25];
                if (Candidate.Timer <= 0.0f)
                {
                    CursorIndex = Step;              // 062defe9 MOV [EBX+0x2bc],ECX
                    Chosen = &Candidate;
                    break;
                }
                if (Lowest > Candidate.Timer)        // FCOM / TEST AH,0x41 / JNZ
                {
                    Lowest = Candidate.Timer;
                    LowestStep = Step;
                }
            }
            if (Step == 25)
            {
                // Retail indexes by the loop step, not by the cursor-relative slot it compared.
                Chosen = Slots + LowestStep;
            }
        }

        // 062def45-062def8f: `TEST EDX,EDX` -- never zero here. The slot takes the hit's point
        // and normal and a 1.0f timer.
        if (Chosen != nullptr)
        {
            Chosen->Point = ConeHit[0].Point;
            Chosen->Direction = ConeHit[0].Normal;
            Chosen->Timer = 1.0f;
        }
    }

    // 062def9f-062defb6: average the active slots (FUN_062de2a0 on the saved transform).
    Vector Escape;
    if (!ObstacleAvoidance_AverageActiveDirection(Slots, ShipXform, Escape))
    {
        Active = 0;                                  // 062defb8
        HasContact = 0;
        ContactObjectId = 0;
        ContactCloseRate = 0.0f;
        ContactTimer = 0.0f;
        return;
    }

    // 062deff4-062df016
    AvoidDirection = Escape;
    ContactTimer = 1.0f;                             // DAT_063a05fc

    // 062df01c-062df0cb: ContactCloseRate = fast |ContactPoint - Nose| / Speed when the
    // stored float Speed is above 1.0f (TEST AH,0x41 / JNZ zeroes it on <=, == and
    // unordered).
    Vector const ToContact{ ContactPoint.x - Nose.x, ContactPoint.y - Nose.y, ContactPoint.z - Nose.z };
    double const ContactLengthSq = (double)(ToContact.z) * ToContact.z
                                   + (double)(ToContact.y) * ToContact.y
                                   + (double)(ToContact.x) * ToContact.x;
    if (Speed > 1.0f)
    {
        ContactCloseRate = (float)(InlineFastLength(ContactLengthSq) / Speed);
    }
    else
    {
        ContactCloseRate = 0.0f;
    }

    // 062df0cd-062df0f6: Dot = Forward . AvoidDirection, stored as a float (ESP+0x10); its
    // unrounded |Dot| <= 0.999 (double DAT_0639fd40; TEST AH,0x41 / JP skips > and
    // unordered) turns Forward toward the escape direction.
    double const EscapeDotExt = (double)(Forward.z) * AvoidDirection.z
                                + (double)(Forward.y) * AvoidDirection.y
                                + (double)(Forward.x) * AvoidDirection.x;
    float const EscapeDot = (float)(EscapeDotExt);
    Vector Heading;
    if (std::fabs(EscapeDotExt) <= 0.999)
    {
        // 062df0fc-062df132: Axis = Forward x AvoidDirection.
        Vector const Axis{
            (float)((double)(Forward.y) * AvoidDirection.z - (double)(Forward.z) * AvoidDirection.y),
            (float)((double)(Forward.z) * AvoidDirection.x - (double)(Forward.x) * AvoidDirection.z),
            (float)((double)(Forward.x) * AvoidDirection.y - (double)(Forward.y) * AvoidDirection.x) };

        // 062df136-062df15f: angle (1.0 - Dot) * 0.2f (DAT_063a0600), replaced by 0.0f when
        // Dot > 0.0 (double DAT_0639c430; TEST AH,0x41 / JNZ keeps it on <=, == and
        // unordered); then halved (0.5f).
        double TurnAngle = (1.0 - EscapeDot) * 0.2f;
        if (EscapeDot > 0.0)
        {
            TurnAngle = 0.0f;
        }
        double const HalfAngle = TurnAngle * 0.5f;

        // 062df165-062df212: quaternion (cos, Axis * sin / |Axis|), 3DMathEngine +0x54 and
        // +0x2c on Forward.
        FQuaternion Turn;
        Turn.W = static_cast<float>(std::cos(static_cast<double>(HalfAngle)));
        float const AxisScale = (float)(std::sin(static_cast<double>(HalfAngle)) / std::sqrt(
            (double)(Axis.z) * Axis.z + (double)(Axis.y) * Axis.y
            + (double)(Axis.x) * Axis.x));
        Turn.X = Axis.x * AxisScale;                     // FUN_06288830
        Turn.Y = Axis.y * AxisScale;
        Turn.Z = Axis.z * AxisScale;
        FMatrix TurnMatrix;
        Math->QuaternionToMatrix(TurnMatrix, Turn);
        Math->TransformMatrix((FVector&)(Heading), TurnMatrix,
                              (FVector const&)(Forward));
    }
    else
    {
        Heading = Forward;                           // 062df220
    }

    // 062df22c-062df2bb: AvoidDirection = Heading; AvoidPoint = Nose + Heading * LookAhead *
    // 10.0f (DAT_0639e884). Retail rounds x * LookAhead * 10 once, z * LookAhead before the
    // * 10, and keeps y unrounded until the sum.
    AvoidDirection = Heading;
    float const ReachX = (float)((double)(Heading.x) * LookAhead * 10.0f);
    double const ReachY = (double)(Heading.y) * LookAhead * 10.0f;
    float const ReachZScaled = (float)((double)(Heading.z) * LookAhead);
    AvoidPoint = Vector{ ReachX + Nose.x,
                         (float)(ReachY + Nose.y),
                         (float)((double)(ReachZScaled) * 10.0f + Nose.z) };
}

bool ObstacleAvoidance_AverageActiveDirection(ObstacleAvoidance::Slot const* Slots,
                                              Transform const& Ship,
                                              Vector& OutDirection)
{
    // FUN_062de2a0 (Common.dll.txt 229775-229955), all 152 instructions traced.
    // 062de2a6-062de2c5: Sum and Count zeroed.
    Vector Sum{ 0.0f, 0.0f, 0.0f };
    int Count = 0;
    for (int Index = 0; Index < 25; ++Index)
    {
        ObstacleAvoidance::Slot const& Entry = Slots[Index];

        // 062de2d1-062de2df: Timer > 0.0f (TEST AH,0x41 / JNZ skips <=, == and unordered).
        if (!(Entry.Timer > 0.0f))
        {
            continue;
        }

        // 062de2e5-062de308: Away = Ship.Translation - Slot.Point.
        Vector Away{ Ship.Translation.x - Entry.Point.x, Ship.Translation.y - Entry.Point.y,
                     Ship.Translation.z - Entry.Point.z };

        // 062de30d-062de356: (z*z + y*y) + x*x, FSQRT, FDIVR 1.0f, each component rounded.
        double const InvLength = 1.0f / std::sqrt((double)(Away.z) * Away.z
                                                  + (double)(Away.y) * Away.y
                                                  + (double)(Away.x) * Away.x);
        Away.x = (float)(Away.x * InvLength);
        Away.y = (float)(Away.y * InvLength);
        Away.z = (float)(Away.z * InvLength);

        // 062de358-062de3c7: Term = (Away + Away) + Slot.Direction, Sum += Term (floats).
        Vector const Term{ (Away.x + Away.x) + Entry.Direction.x, (Away.y + Away.y) + Entry.Direction.y,
                           (Away.z + Away.z) + Entry.Direction.z };
        ++Count;
        Sum.x = Term.x + Sum.x;
        Sum.y = Term.y + Sum.y;
        Sum.z = Term.z + Sum.z;
    }

    // 062de3d8-062de3df, 062de482: no active slot -> false, OutDirection untouched.
    if (Count <= 0)
    {
        return false;
    }

    // 062de3e5-062de426: FILD Count / FDIVR 1.0f keeps 1/Count on the stack; each component
    // is rounded once.
    double const InvCount = 1.0f / (double)(Count);
    OutDirection.x = (float)(Sum.x * InvCount);
    OutDirection.y = (float)(Sum.y * InvCount);
    OutDirection.z = (float)(Sum.z * InvCount);

    // 062de429-062de452: (x*x + y*y) + z*z against double 1e-05 (DAT_0639f418); TEST AH,0x41
    // / JNZ returns true un-normalised on <=, == and unordered.
    double const LengthSq = (double)(OutDirection.x) * OutDirection.x
                            + (double)(OutDirection.y) * OutDirection.y
                            + (double)(OutDirection.z) * OutDirection.z;
    if (!(LengthSq > 1e-05))
    {
        return true;
    }

    // 062de454-062de475: FSQRT, FDIVR 1.0f, scale in place.
    double const InvLength = 1.0f / std::sqrt(LengthSq);
    OutDirection.x = (float)(InvLength * OutDirection.x);
    OutDirection.y = (float)(InvLength * OutDirection.y);
    OutDirection.z = (float)(InvLength * OutDirection.z);
    return true;
}

// ----------------------------------------------------------------------------
// Ship heading-blend classifier pair -- see common_aisteer.h for the field
// layout writeup (item 1667/1680/1681).
// ----------------------------------------------------------------------------

namespace
{
    // Shared "-8 from IBehaviorManager::WatchablePtr" resolution, used by both
    // ClassifyShipObstaclePhase and UpdateShipHeadingBlend to reach the ship's
    // current Transform (matches ObstacleAvoidance::Update's own identical
    // "ProviderBase" resolution above).
    // Retail (e.g. 062dc59f-062dc5bb) tests only the watched pointer for the -8 adjust and
    // then calls through the result unconditionally; there is no null return.
    Transform const* ResolveShipTransform(IBehaviorManager* Owner)
    {
        IObjRW* const ProviderBase = ObjRwFromWatchable(Owner->WatchablePtr);
        // See the get_transform note in this file's sensor block: retail's +0x10 is slot 4,
        // folded with slot 3, and the caller wants the Transform.
        return &ProviderBase->get_transform();
    }
}

// Common.dll.c 0x062dc440 (FUN_062dc440). See common_aisteer.h for the full
// algorithm writeup / the Ghidra call-argument-corruption note. Item 1679.
// Re-ported 2026-10-08 (cmn-ai9) from the full listing 062dc440-062dc587, cdecl
// (Out, From, To, Angle):
//   062dc44c-062dc46b  (F.x*T.x + T.z*F.z) + F.y*T.y FCOMP double 0.999 (DAT_0639fd40),
//                      TEST AH,0x41 + JNZ: only a greater dot returns To.
//   062dc488-062dc4ae  Axis = cross(From, To); x and y stay on the FPU, z is FSTP float.
//   062dc4b2-062dc4dc  Half = Angle * 0.5f (DAT_0639c13c); K = sin(Half) /
//                      sqrt((z*z + x*x) + y*y), FSTP float.
//   062dc4e0-062dc525  Quat = (cos(Half), K*x, K*y, z*K), each FSTP float.
//   062dc535-062dc580  QuaternionToMatrix (+0x54) then TransformMatrix (+0x2c) of From,
//                      both through the lazily created engine with no null test.
Vector RotateTowardBySlerp(Vector const& From, Vector const& To, float Angle)
{
    double const Dot = ((double)(From.x) * To.x + (double)(To.z) * From.z) +
                       (double)(From.y) * To.y;
    if (Dot > 0.999)
        return To;

    double const AxisX = (double)(From.y) * To.z - (double)(To.y) * From.z;
    double const AxisY = (double)(To.x) * From.z - (double)(To.z) * From.x;
    float const AxisZ = (float)((double)(To.y) * From.x - (double)(From.y) * To.x);
    double const HalfAngle = (double)(Angle) * 0.5f;
    double const AxisLength = std::sqrt(((double)(AxisZ) * AxisZ + AxisX * AxisX) + AxisY * AxisY);
    float const SinOverLength = (float)(std::sin(static_cast<double>(HalfAngle)) / AxisLength);

    FQuaternion Quat;
    Quat.W = static_cast<float>(std::cos(static_cast<double>(HalfAngle)));
    Quat.X = (float)(SinOverLength * AxisX);
    Quat.Y = (float)(SinOverLength * AxisY);
    Quat.Z = (float)((double)(AxisZ) * SinOverLength);

    Matrix RotMatrix;
    Acquire3DMathEngine()->QuaternionToMatrix((FMatrix&)(RotMatrix), Quat); // +0x54
    Vector Result;
    Acquire3DMathEngine()->TransformMatrix((FVector&)(Result),
                                           (FMatrix const&)(RotMatrix),
                                           (FVector const&)(From)); // +0x2C
    return Result;
}

// Common.dll.c 0x062dbfb0 (FUN_062dbfb0). See common_aisteer.h for the full
// algorithm writeup / field-shape identification. Item 1676.
namespace
{
    // FUN_06347e60 in double; defined with FindClosestObstacleToAvoid's helpers below.
    double VectorLengthX87(Vector const& V);

    // FUN_062dc3e0 (item 1677), cdecl, one float, result left in ST0 (062dc3e0-062dc40f):
    // y = bits(0x5f3759df - (bits(X) SAR 1)); result = (1.5f - ((y*y)*X)*0.5f) * y, unrounded.
    // Not the arithmetic of FUN_062df7f0 (1.5f*y - (0.5f*X*y)*(y*y)).
    double FastInvSqrtNewton(float X)
    {
        int Bits;
        std::memcpy(&Bits, &X, sizeof(Bits));
        Bits = 0x5f3759df - (Bits >> 1);
        float Y;
        std::memcpy(&Y, &Bits, sizeof(Y));
        return (1.5f - (((double)Y * Y) * X) * 0.5f) * Y;
    }

    // The |V|^2 = (z*z + y*y) + x*x order NudgeObstaclePathPoint uses for every vector
    // it measures inline, left unrounded.
    double LenSqZyx(Vector const& V)
    {
        return ((double)V.z * V.z + (double)V.y * V.y) + (double)V.x * V.x;
    }
}

int NudgeObstaclePathPoint(ObstacleAvoidance::Slot const& Candidate, Vector const& Point,
                                       Transform const& Obstacle, Vector* OutPoint, bool ExtraCheck)
{
    // 062dbfbb-062dbfdc: Midpoint = Candidate.Point + Candidate.Direction * 0.5f
    // (FUN_06288830 then FUN_062887d0, float per component).
    Vector const Midpoint = Add(Candidate.Point, Scale(Candidate.Direction, 0.5f));
    Vector const& ObstaclePos = Obstacle.Translation; // ESI = Obstacle + 0x24

    // 062dbfe1-062dc03b: DirToObstacle = (ObstaclePos - Midpoint) times the unrounded
    // reciprocal of its FUN_06347e60 length (FDIVR 1.0f), FSTP per component.
    Vector DirToObstacle = Sub(ObstaclePos, Midpoint);
    {
        double const Inv = 1.0 / VectorLengthX87(DirToObstacle);
        DirToObstacle.x = (float)(DirToObstacle.x * Inv);
        DirToObstacle.y = (float)(DirToObstacle.y * Inv);
        DirToObstacle.z = (float)(DirToObstacle.z * Inv);
    }

    // 062dc03d-062dc055: FUN_062e1330 with the candidate's capsule record.
    Vector HitPoint0;
    if (!RaySegmentCapsuleFirstHit(Midpoint, DirToObstacle, Candidate.Timer, Candidate.Point, Candidate.Direction, &HitPoint0))
        return 3;

    // 062dc058-062dc0b2: ObstacleDir = ObstaclePos - Midpoint again; its length is
    // FST'd, the UNROUNDED length FCOMP 1.0f (TEST AH,0x5 / JP: only less returns 1),
    // and FUN_062dc410 normalises by the float image.
    Vector ObstacleDir = Sub(ObstaclePos, Midpoint);
    double const DistToObstacle = std::sqrt(LenSqZyx(ObstacleDir));
    if (DistToObstacle < 1.0)
        return 1;
    VecNormalizeInPlaceByLength3(&ObstacleDir, (float)DistToObstacle);

    // 062dc0b7-062dc115: PointDir = Point - Midpoint, the same way.
    Vector PointDir = Sub(Point, Midpoint);
    double const DistPointToMid = std::sqrt(LenSqZyx(PointDir));
    if (DistPointToMid < 1.0)
        return 1;
    VecNormalizeInPlaceByLength3(&PointDir, (float)DistPointToMid);

    // 062dc11a-062dc189: ObstacleToPointDir = Point - ObstaclePos; the length is never
    // stored -- `FCOM 1.0f` then `FDIVR 1.0f` on the unrounded value.
    Vector ObstacleToPointDir = Sub(Point, ObstaclePos);
    double const DistPointToObstacle = std::sqrt(LenSqZyx(ObstacleToPointDir));
    if (DistPointToObstacle < 1.0)
        return 1;
    {
        double const Inv = 1.0 / DistPointToObstacle;
        ObstacleToPointDir.x = (float)(ObstacleToPointDir.x * Inv);
        ObstacleToPointDir.y = (float)(ObstacleToPointDir.y * Inv);
        ObstacleToPointDir.z = (float)(ObstacleToPointDir.z * Inv);
    }

    // 062dc18b-062dc1b2: (P.z*O.z + P.y*O.y) + O.x*P.x FCOMP 0.9f; TEST AH,0x41 / JNZ --
    // only greater goes on to the urgency checks.
    double const AlignDot = ((double)PointDir.z * ObstacleDir.z + (double)PointDir.y * ObstacleDir.y) +
                            (double)ObstacleDir.x * PointDir.x;
    if (AlignDot > 0.9f)
    {
        // 062dc1b4-062dc1ba: without ExtraCheck, return 1.
        if (!ExtraCheck)
            return 1;
        // 062dc1bc-062dc209: -column 2 of the obstacle's rotation (FUN_0628b030 from
        // +0x8/+0x14/+0x20), dot (z, y, x order) FCOMP 0.85f; TEST AH,0x5 / JNP -- only
        // less goes on, not-less (and NaN) returns 1.
        Vector const NegForward{ -Obstacle.Rotation.E[0][2], -Obstacle.Rotation.E[1][2], -Obstacle.Rotation.E[2][2] };
        double const FacingDot = ((double)NegForward.z * ObstacleToPointDir.z + (double)NegForward.y * ObstacleToPointDir.y) +
                                 (double)NegForward.x * ObstacleToPointDir.x;
        if (!(FacingDot < 0.85f))
            return 1;
    }

    // LAB_062dc21a: A = |HitPoint0 - Midpoint|^2 and B = |ObstaclePos - Midpoint|^2. B's
    // fast length runs on the unrounded B; A's on its FST float image, times 1.2f.
    // `FCOMPP; TEST AH,0x5; JP` -- only A' < B' returns 2.
    float const LenSqA = (float)LenSqZyx(Sub(HitPoint0, Midpoint));
    double const LenSqB = LenSqZyx(Sub(ObstaclePos, Midpoint));
    double const ApproxDistA = InlineFastLength((double)LenSqA) * 1.2f;
    double const ApproxDistB = InlineFastLength(LenSqB);
    if (ApproxDistA < ApproxDistB)
        return 2;

    // LAB_062dc30f: C = |Point - ObstaclePos|^2 FSTP float; FUN_062dc3e0(C) * C FCOMP
    // 100.0f; TEST AH,0x41 / JNZ -- not-greater (and NaN) hands Point straight back.
    Vector const PointToObstacle = Sub(Point, ObstaclePos);
    float const LenSqC = (float)LenSqZyx(PointToObstacle);
    if (!(FastInvSqrtNewton(LenSqC) * LenSqC > 100.0f))
    {
        *OutPoint = Point;
        return 0;
    }

    // 062dc374-062dc3b8: swing ObstacleDir 0.3 rad toward PointDir (FUN_062dc440) and
    // cast again, writing straight into the caller's OutPoint.
    Vector const RotatedDir = RotateTowardBySlerp(ObstacleDir, PointDir, 0.3f);
    if (!RaySegmentCapsuleFirstHit(Midpoint, RotatedDir, Candidate.Timer, Candidate.Point, Candidate.Direction, OutPoint))
        return 3;
    return 0;
}

// FUN_062e74e0, item 1799. RESOLVED this pass (2026-09-11, d282:common-port)
// -- previously re-confirmed `blocked` by an independent lane on the belief
// that Ghidra's own decompile (`unaff_ESI`/`unaff_retaddr`) reflected genuine
// unrecoverable register corruption. Direct hand ESP-delta tracing of the raw
// bytes (Common.dll.txt:243780-243845, all 61 instructions, completely
// linear, no loops) shows Ghidra's decompile is simply WRONG, not
// unresolvable:
//   - The function is an ordinary thiscall (ECX=this) taking exactly 2
//     explicit stack dwords -- confirmed by `RET 0x8` at both exits
//     (243839/243845) and independently by hand-tracing the prologue
//     (`SUB ESP,0x8; PUSH EBX; PUSH ESI` -> ESP_P = ESP_entry-0x10). Ghidra's
//     "unaff_retaddr" is this function's own SECOND stack parameter
//     ([ESP_P+0x18] = [ESP_entry+8]), and the value Ghidra folds into the
//     same expression as "local_4" is actually this function's own FIRST
//     stack parameter ([ESP_P+0x14] = [ESP_entry+4]) -- both simply
//     forwarded, unmodified, into a nested vtable[0x104] call
//     (Common.dll.txt:243825-243832) alongside a genuine local byte field
//     (this+0x1d).
//   - Ghidra's "unaff_ESI" gate (`if ((unaff_ESI & 2) == 0)`) is not a
//     register at all: the real instruction (Common.dll.txt:243812,
//     `TEST byte ptr [ESP+0x8],0x2`) tests the low byte of an 8-byte local
//     scratch buffer that the FIRST vtable call (slot 0x118,
//     Common.dll.txt:243806-243811) had just written into as its own
//     out-parameter one instruction earlier -- an ordinary read-after-write.
// Cross-checked against the real caller relevant to this cluster (retail
// 0x062dca60, ClassifyShipObstaclePhase's own obstacle-scan entry gate,
// Common.dll.txt:227604-227611): Self=`(Owner->8) + 0x34`, Arg1=a
// caller-local out-buffer (pre-zeroed immediately before the call and
// re-checked for non-zero immediately after, Common.dll.txt:227605/227612 --
// "if it doesn't populate a non-null candidate-source pointer, return 4"),
// Arg2=0xfd58 (a literal filter mask; the two OTHER call sites in this
// module, 062ce790/062ce948, pass different literal masks 0x7f0000/
// 0x10000000, confirming Arg2 is a genuine per-call filter constant, not an
// internal literal specific to this call site).
//
// The two internal vtable slots (0x118, 0x104) are not independently named
// this pass -- ported at the same raw-offset/vtable-slot-call confidence
// this file already uses for ClassifyShipObstaclePhase's own still-unnamed
// interface slots (+0x4c/+0x88 below); they are get_equipment_status (0x118)
// and get_scanlist (0x104). Past the first null check retail dereferences the
// re-derived "handle - 8" pointer unconditionally, and so does the port.
// Callers: ClassifyShipObstaclePhase (below) and LootBehavior.
bool SystemManagerTimerTable::GetOwnScanList(ScanList const*& OutList, unsigned FilterMask)
{
    // 062e74e7-062e74f8: `MOV EAX,[ESI+4]; MOV ECX,[EAX+8]` -- no test on Owner itself; the
    // watched own ship (raw and raw - 8) is the only gate.
    Watchable const* const ListHandle = Owner->Watcher.target();
    if (ListHandle == nullptr)
        return false;
    IObjRW* const ProviderBase = ObjRwFromWatchable(const_cast<Watchable*>(ListHandle));
    if (ProviderBase == nullptr)
        return false;

    // Retail reads+masks its own genuinely-uninitialized 8-byte scratch
    // buffer here (`local_8 &= 0xf8; local_4 &= 0xe0000000;`,
    // Common.dll.txt:243792-243797) before the very next call unconditionally
    // overwrites it as an out-parameter -- observably harmless (nothing ever
    // reads the pre-call garbage), so not reproduced.
    EquipStatus Scratch{};
    ProviderBase->get_equipment_status(Scratch, ScannerEquipId);

    if ((Scratch.Flags & 2) != 0)
        return false; // matches retail's shared early-out (`MOV AL,BL`, BL never set past its initial 0)

    // Retail re-derives the SAME ProviderBase chain from scratch here
    // (Common.dll.txt:243814-243824) rather than reusing the first
    // derivation -- reproduced faithfully (not just reused) since the
    // vtable[0x118] call above is not independently known to be free of
    // side effects on Inner/ListHandle.
    // 062e7539-062e754c: re-derived with the `raw ? raw - 8 : 0` arm and no early return
    // (a null owner faults on the vtable read in retail, as here).
    IObjRW* const ProviderBase2 = ObjRwFromWatchable(const_cast<Watchable*>(Owner->Watcher.target()));

    int const Result = ProviderBase2->get_scanlist(OutList, FilterMask, bKeepCache != 0);
    return Result == 0; // NEG/SBB/INC idiom (Common.dll.txt:243833-243836): true iff the dispatch returned 0
}

// FUN_062dbbe0, item 1675, Common.dll.txt:226296-226595. A PathfindManager thiscall in retail
// whose `this` is never read (ECX is overwritten at 062dbbec), RET 0x8 (Candidate, Slot).
// Re-ported 2026-10-08 (cmn-ai9) from a full trace of all 290 instructions.
bool ResolveObstacleCandidateSlot(IObjRW* Candidate, ObstacleAvoidance::Slot& OutSlot)
{
    IObjRW* const Obj = Candidate;

    // 062dbbe9-062dbc06: the candidate's transform (slot +0x10, folded with get_transform),
    // then get_archetype_extents (+0x28) into Min (S+0x1c) / Max (S+0x28).
    Transform const& CandXform = Obj->get_transform();
    Vector Min;
    Vector Max;
    if (Obj->get_archetype_extents(Min, Max) != 0)
    {
        // 062dbc0c-062dbc93: get_physical_radius (+0x2c) into a zeroed radius (S+0x50) and the
        // centre; Slot = {radius, centre, (0, 1, 0)}; get_atmosphere_range (+0x140) into a
        // zeroed float replaces the radius only when greater (TEST AH,0x41 + JNZ). True.
        float Radius = 0.0f;
        Vector Centre;
        Obj->get_physical_radius(Radius, Centre);
        OutSlot.Direction.x = 0.0f;
        OutSlot.Direction.y = 1.0f;
        OutSlot.Direction.z = 0.0f;
        OutSlot.Point = Centre;
        OutSlot.Timer = Radius;
        float Range = 0.0f;
        Obj->get_atmosphere_range(Range);
        if (Range > Radius)
            OutSlot.Timer = Range;
        return true;
    }

    // 062dbc96-062dbcf5: LocalCentre (S+0x10) = (Min + Max) * 0.5f, float ops.
    Vector Sum;
    Sum.x = Min.x + Max.x;
    Sum.y = Min.y + Max.y;
    Sum.z = Min.z + Max.z;
    Vector LocalCentre;
    LocalCentre.x = Sum.x * 0.5f;
    LocalCentre.y = Sum.y * 0.5f;
    LocalCentre.z = Sum.z * 0.5f;

    // 062dbcfa-062dbd1b: TransformXform (+0x28) of the local centre into S+0x40; the result
    // is never read again.
    Vector WorldCentre;
    Acquire3DMathEngine()->TransformXform((FVector&)(WorldCentre),
                                          (FTransform const&)(CandXform),
                                          (FVector const&)(LocalCentre));

    // 062dbd1e-062dbd3a: the extents; Ey (S+0x50) and Ez (S+0xc) are FSTP float, Ex stays on
    // the FPU unrounded.
    double const ExtentX = (double)(Max.x) - Min.x;
    float const ExtentY = (float)((double)(Max.y) - Min.y);
    float const ExtentZ = (float)((double)(Max.z) - Min.z);

    // The capsule runs along the strictly longest extent (X, then Y, else Z), starting at
    // Min on that axis and the centre on the other two; its radius is half the larger of
    // the other two extents (a tie takes the later-tested one). Every TEST AH,0x41 here
    // treats "not greater" as false.
    double Across;
    if (ExtentX > ExtentY && ExtentX > ExtentZ)
    {
        // 062dbd5c-062dbde9.
        OutSlot.Point.x = Min.x;
        OutSlot.Point.y = LocalCentre.y;
        OutSlot.Point.z = LocalCentre.z;
        OutSlot.Direction.x = Max.x - OutSlot.Point.x;
        OutSlot.Direction.y = LocalCentre.y - OutSlot.Point.y;
        OutSlot.Direction.z = LocalCentre.z - OutSlot.Point.z;
        Across = (ExtentY > ExtentZ) ? ExtentY : ExtentZ;
    }
    else if (ExtentY > ExtentX && ExtentY > ExtentZ)
    {
        // 062dbe12-062dbe9f.
        OutSlot.Point.x = LocalCentre.x;
        OutSlot.Point.y = Min.y;
        OutSlot.Point.z = LocalCentre.z;
        OutSlot.Direction.x = LocalCentre.x - OutSlot.Point.x;
        OutSlot.Direction.y = Max.y - OutSlot.Point.y;
        OutSlot.Direction.z = LocalCentre.z - OutSlot.Point.z;
        Across = (ExtentX > ExtentZ) ? ExtentX : (double)(ExtentZ);
    }
    else
    {
        // LAB_062dbea4.
        OutSlot.Point.x = LocalCentre.x;
        OutSlot.Point.y = LocalCentre.y;
        OutSlot.Point.z = Min.z;
        OutSlot.Direction.x = LocalCentre.x - OutSlot.Point.x;
        OutSlot.Direction.y = LocalCentre.y - OutSlot.Point.y;
        OutSlot.Direction.z = Max.z - OutSlot.Point.z;
        Across = (ExtentX > ExtentY) ? ExtentX : (double)(ExtentY);
    }

    // LAB_062dbf2d: Timer = Across * 0.5 (double DAT_0639df88), FSTP float.
    OutSlot.Timer = (float)(Across * 0.5);

    // 062dbf35-062dbf9a: Point through TransformXform (+0x28), Direction through Rotate
    // (+0x30), both by the candidate transform, no null tests. True.
    Vector WorldPoint;
    Acquire3DMathEngine()->TransformXform((FVector&)(WorldPoint),
                                          (FTransform const&)(CandXform),
                                          (FVector const&)(OutSlot.Point));
    OutSlot.Point = WorldPoint;
    Vector WorldDir;
    Acquire3DMathEngine()->Rotate((FVector&)(WorldDir),
                                  (FTransform const&)(CandXform),
                                  (FVector const&)(OutSlot.Direction));
    OutSlot.Direction = WorldDir;
    return true;
}

// Common.dll.c 0x062dc840 (FUN_062dc840), item 1681, Common.dll.txt:227424-228374. A
// PathfindManager method (ECX = the PathfindManager, Owner at +0x1C0). Re-ported 2026-10-08
// (cmn-ai9) from a full trace of all 872 instructions; stack slots are S+N with S = ESP after
// the 4 pushes (frame 0x20c0). Returns 4 for "no reaction", 0 after committing a reaction, or
// NudgeObstaclePathPoint's own non-zero result.
int PathfindManager::ClassifyObstaclePhase()
{
    PathfindManager* const This = this;

    // 062dc84b-062dc888: Reaction.State = 3, LastObstacleTag = 0, Heading = 0 (z, y, x),
    // bInReaction = bCloseEncounter = 0, CachedObstacleId = 0, bHasCachedObstacle = 0.
    This->Reaction.State = 3;
    This->Reaction.LastObstacleTag = 0;
    This->Reaction.Heading.z = 0.0f;
    This->Reaction.Heading.y = 0.0f;
    This->Reaction.Heading.x = 0.0f;
    This->Reaction.bInReaction = false;
    This->Reaction.bCloseEncounter = false;
    This->CachedObstacleId = 0;
    This->bHasCachedObstacle = false;

    // 062dc88f-062dc8c0: WorkingTarget (S+0x28) and TargetPosition = Owner->Pathfind's
    // CachedTargetPosition.
    IBehaviorManager* const Owner = This->Owner;
    Vector WorkingTarget = Owner->Pathfind->CachedTargetPosition;
    This->TargetPosition = WorkingTarget;

    // 062dc8c3-062dc8e1: behaviour 10 never reacts.
    int const BehaviorIndex = Owner->CurrentBehaviorIndex; // S+0x38
    if (BehaviorIndex == 10)
        return 4;

    // 062dc8e2-062dc92e: FlagA (S+0x16) = behaviour in {1, 3, 5, 0xb, 0xd, 0x12}.
    int const LookaheadBehaviors[6] = { 1, 3, 5, 0xb, 0xd, 0x12 };
    bool bFlagA = false;
    for (int Index = 0; Index < 6; ++Index)
    {
        if (LookaheadBehaviors[Index] == BehaviorIndex)
        {
            bFlagA = true;
            break;
        }
    }

    // 062dc933-062dc957: the ship's transform through the watched object's slot +0x10
    // (S+0x48), no null test.
    Transform const* const ShipXform = ResolveShipTransform(Owner);

    // 062dc949-062dc9f4: (x*x + y*y) + z*z of WorkingTarget FCOMP 1.0f, TEST AH,0x5 + JP:
    // greater, equal or unordered keeps it. Below 1: without FlagA bBlendEnabled = 0 and 4;
    // with it WorkingTarget = TransformXform (+0x28) of (0, 0, -2000.0f) by the ship transform.
    double const TargetLenSq = ((double)(WorkingTarget.x) * WorkingTarget.x +
                                (double)(WorkingTarget.y) * WorkingTarget.y) +
                               (double)(WorkingTarget.z) * WorkingTarget.z;
    if (TargetLenSq < 1.0)
    {
        if (!bFlagA)
        {
            This->bBlendEnabled = false;
            return 4;
        }
        This->bBlendEnabled = true;
        Vector const LocalAhead{ 0.0f, 0.0f, -2000.0f };
        Vector Ahead;
        Acquire3DMathEngine()->TransformXform((FVector&)(Ahead),
                                              (FTransform const&)(*ShipXform),
                                              (FVector const&)(LocalAhead));
        WorkingTarget = Ahead;
    }
    else
    {
        This->bBlendEnabled = true;
    }

    // 062dc9fb-062dca45: get_throttle (+0x4c) of the watched object into a 1.0f-seeded float,
    // FCOMP double 0.1 (DAT_063a0460), TEST AH,0x5 + JP: only "less" returns 4.
    float Throttle = 1.0f;
    ObjRwFromWatchable(Owner->WatchablePtr)->get_throttle(Throttle);
    if ((double)(Throttle) < 0.1)
        return 4;

    // 062dca46-062dca7a: FUN_062e74e0 on Owner->Sys + 0x34 with mask 0xfd58 fills the
    // candidate list pointer (S+0x70); its return value is ignored, a null list returns 4.
    ScanList const* CandidateList = nullptr;
    Owner->Sys->Timers.GetOwnScanList(CandidateList, 0xfd58u);
    if (CandidateList == nullptr)
        return 4;

    // 062dca7b-062dcad4: the {0x800, 0} result block (S+0xc8); ObjectDb->GetBucket(7) and,
    // when it is non-null, its QuerySphere (+0x1c) around the ship position with radius
    // 20000.0f (S+0x4c).
    FSpatialQueryResult Nearby;
    Nearby.Count = 0;
    Nearby.Capacity = 0x800;
    ISpatialPartition* const Bucket = (ISpatialPartition*)Owner->ObjectDb->GetBucket(7);
    if (Bucket != nullptr)
    {
        Geometry::Sphere Query;
        Query.Center = ShipXform->Translation;
        Query.Radius = 20000.0f;
        Bucket->QuerySphere(&Query, &Nearby);
    }

    // 062dcad7-062dcda3: two passes. Pass 0 walks the candidate list (8-byte entries, a
    // watched pointer -8 each, count at +0x800, type mask 0xfd58); pass 1 the query result
    // (mask 7). Per candidate: get_type (+0x88) into a zeroed word, mask test; S+0x17 =
    // type & 7; the get_id (+0x20) looked up in RecentObstacleIds, a miss advancing
    // NextDeflectionSlot ((n + 1) % 8, signed) and refilling that slot via FUN_062dbbe0.
    float BestDistance = 3.40282347e+38f; // S+0x44, 0x7f7fffff
    IObjRW* BestCandidate = nullptr;      // S+0x6c
    ObstacleAvoidance::Slot BestSlot{};   // S+0x8c, read only after a candidate set it
    for (int Pass = 0; Pass < 2; ++Pass)
    {
        unsigned const Count = (Pass == 0) ? (unsigned)CandidateList->Count : Nearby.Count;
        for (unsigned Index = 0; Index < Count; ++Index)
        {
            IObjRW* Candidate;
            unsigned TypeMask;
            if (Pass == 0)
            {
                Candidate = ObjRwFromWatchable(const_cast<Watchable*>(CandidateList->Entries[Index].target()));
                TypeMask = 0xfd58u;
            }
            else
            {
                Candidate = (IObjRW*)Nearby.Items[Index];
                TypeMask = 7u;
            }

            unsigned Type = 0;
            Candidate->get_type(Type);
            if ((Type & TypeMask) == 0)
                continue;
            bool const bCandidateClose = (Type & 7u) != 0;

            unsigned const CandidateId = Candidate->get_id();
            ObstacleAvoidance::Slot* SlotPtr = nullptr;
            for (int Cached = 0; Cached < 8; ++Cached)
            {
                if (This->RecentObstacleIds[Cached] == CandidateId)
                {
                    SlotPtr = &This->DeflectionSlots[Cached];
                    break;
                }
            }
            if (SlotPtr == nullptr)
            {
                int const Next = ((int)This->NextDeflectionSlot + 1) % 8;   // signed remainder (CDQ; IDIV)
                This->NextDeflectionSlot = (unsigned)Next;
                SlotPtr = &This->DeflectionSlots[Next];
                ResolveObstacleCandidateSlot(Candidate, *SlotPtr);
            }

            // 062dcbd8-062dcbfa: Slot.Timer FCOMP 100.0f (TEST AH,0x5 + JNP: less skips) and
            // FCOMP 20000.0f (TEST AH,0x41 + JZ: greater skips); NaN passes both.
            if (SlotPtr->Timer < 100.0f || SlotPtr->Timer > 20000.0f)
                continue;

            // 062dcc00-062dcc5f: Margin (S+0x18) = Timer * 0.15f on the FPU, replaced by the floor
            // 750.0f (candidate close and FlagA) or 250.0f whenever the floor is greater; else
            // the product FSTP float.
            double const Scaled = (double)(SlotPtr->Timer) * 0.15f;
            float const Floor = (bCandidateClose && bFlagA) ? 750.0f : 250.0f;
            float const Margin = ((double)(Floor) > Scaled) ? Floor : (float)(Scaled);

            // 062dcc5f-062dcc80: Timer = (Margin + Timer) + Field_1FC * 0.5 on the FPU (the FST
            // in between does not round the running sum).
            SlotPtr->Timer = (float)(((double)(Margin) + SlotPtr->Timer) +
                                                (double)(This->Field_1FC) * 0.5);

            // 062dcc83-062dcd7b: FUN_062e1410(ship position, slot); on a hit the slot midpoint
            // D*0.5f + P (float ops) against WorkingTarget, (x*x + z*z) + y*y unrounded, the
            // inlined fast length, FCOM Best + TEST AH,0x5 + JP: only "less" takes it (Best FSTP
            // float, slot copied, candidate remembered).
            if (PointInCapsule(ShipXform->Translation, SlotPtr->Timer, SlotPtr->Point, SlotPtr->Direction))
            {
                Vector Mid;
                Mid.x = SlotPtr->Direction.x * 0.5f + SlotPtr->Point.x;
                Mid.y = SlotPtr->Direction.y * 0.5f + SlotPtr->Point.y;
                Mid.z = SlotPtr->Direction.z * 0.5f + SlotPtr->Point.z;
                double const DX = (double)(Mid.x) - WorkingTarget.x;
                double const DY = (double)(Mid.y) - WorkingTarget.y;
                double const DZ = (double)(Mid.z) - WorkingTarget.z;
                double const Distance = InlineFastLength((DX * DX + DZ * DZ) + DY * DY);
                if (Distance < (double)(BestDistance))
                {
                    BestDistance = (float)(Distance);
                    BestSlot = *SlotPtr;
                    BestCandidate = Candidate;
                }
            }

            // 062dcd7d-062dcd84: Timer -= Margin.
            SlotPtr->Timer = (float)((double)(SlotPtr->Timer) - Margin);
        }
    }

    // 062dcdb5-062dcdce.
    if (BestCandidate == nullptr)
        return 4;

    // 062dcdcf-062dce53: behaviour 2: the winner's get_position against WorkingTarget,
    // (x*x + z*z) + y*y unrounded, fast length FCOMP 1.0f + TEST AH,0x41 + JP: greater (or
    // unordered) continues, less or equal returns 4.
    if (BehaviorIndex == 2)
    {
        Vector const& WinnerPos = BestCandidate->get_position();
        double const DX = (double)(WinnerPos.x) - WorkingTarget.x;
        double const DY = (double)(WinnerPos.y) - WorkingTarget.y;
        double const DZ = (double)(WinnerPos.z) - WorkingTarget.z;
        double const Distance = InlineFastLength((DX * DX + DZ * DZ) + DY * DY);
        if (Distance <= 1.0)
            return 4;
    }

    // 062dce54-062dce89: CachedObstacleId = get_id, bHasCachedObstacle = 1; get_type into a
    // zeroed word, HasCloseFlags (S+0x17) = type & 7.
    This->CachedObstacleId = BestCandidate->get_id();
    This->bHasCachedObstacle = true;
    unsigned WinnerType = 0;
    BestCandidate->get_type(WinnerType);
    bool const bHasCloseFlags = (WinnerType & 7u) != 0;

    if (bHasCloseFlags)
    {
        // 062dce8f-062dcf1b: winner position minus the ship position, (z*z + y*y) + x*x
        // rounded to float (S+0x34) before the fast length; get_atmosphere_range (+0x140)
        // into a zeroed float; TEST AH,0x41 + JP: less or equal sets bCloseEncounter.
        Vector const& WinnerPos = BestCandidate->get_position();
        double const DX = (double)(WinnerPos.x) - ShipXform->Translation.x;
        double const DY = (double)(WinnerPos.y) - ShipXform->Translation.y;
        double const DZ = (double)(WinnerPos.z) - ShipXform->Translation.z;
        float const LenSqF = (float)((DZ * DZ + DY * DY) + DX * DX);
        float Range = 0.0f;
        BestCandidate->get_atmosphere_range(Range);
        if (InlineFastLength(LenSqF) <= (double)(Range))
            This->Reaction.bCloseEncounter = true;

        // 062dcf22-062dcfe1: with FlagA, the watched object's get_target (+0x74) into a
        // zeroed pointer; a target farther than 500.0f (unrounded (z*z + y*y) + x*x, fast
        // length, TEST AH,0x41 + JNZ) replaces WorkingTarget with its position.
        if (bFlagA)
        {
            IObjRW* Target = nullptr;
            ObjRwFromWatchable(Owner->WatchablePtr)->get_target(Target);
            if (Target != nullptr)
            {
                Vector const& TargetPos = Target->get_position();
                double const TX = (double)(TargetPos.x) - ShipXform->Translation.x;
                double const TY = (double)(TargetPos.y) - ShipXform->Translation.y;
                double const TZ = (double)(TargetPos.z) - ShipXform->Translation.z;
                if (InlineFastLength((TZ * TZ + TY * TY) + TX * TX) > 500.0)
                {
                    WorkingTarget = TargetPos;
                }
                else
                {
                    // 062dcfe6-062dd11d: the target is within 500 (or unordered): ActiveSlot =
                    // BestSlot, State = 1, LastObstacleTag = the winner's get_id, bInReaction =
                    // 1, Heading = -(normalized (BestSlot midpoint - ship position)): midpoint
                    // D*0.5f + P in float ops, z FST float and squared as z * float(z), Inv =
                    // 1.0f / sqrt((z*zf + y*y) + x*x) FSTP float, each component FSTP float and
                    // negated. Returns 0.
                    This->Reaction.ActiveSlot = BestSlot;
                    This->Reaction.State = 1;
                    This->Reaction.LastObstacleTag = BestCandidate->get_id();
                    This->Reaction.bInReaction = true;
                    Vector Mid;
                    Mid.x = BestSlot.Direction.x * 0.5f + BestSlot.Point.x;
                    Mid.y = BestSlot.Direction.y * 0.5f + BestSlot.Point.y;
                    Mid.z = BestSlot.Direction.z * 0.5f + BestSlot.Point.z;
                    double const MX = (double)(Mid.x) - ShipXform->Translation.x;
                    double const MY = (double)(Mid.y) - ShipXform->Translation.y;
                    double const MZ = (double)(Mid.z) - ShipXform->Translation.z;
                    float const MZf = (float)(MZ);
                    float const Inv = (float)(1.0 / std::sqrt((MZ * MZf + MY * MY) + MX * MX));
                    This->Reaction.Heading.x = -(float)(MX * Inv);
                    This->Reaction.Heading.y = -(float)(MY * Inv);
                    This->Reaction.Heading.z = -(float)((double)(MZf) * Inv);
                    return 0;
                }
            }
        }
    }

    // The shared commit (LAB_062dd273): ActiveSlot = BestSlot, State = 0, LastObstacleTag =
    // the winner's get_id, bInReaction = 0, Heading = OutPoint - ship position (FSTP float
    // each) normalized by 1 / sqrt((x*x + y*y) + z*z) kept on the FPU (no zero test), and
    // bInReaction = 1 when ((-E22*H.z) + (-E12*H.y)) + (-E02*H.x) is below 0.5f (TEST AH,0x5
    // + JP). Returns 0.
    auto CommitReaction = [&](Vector const& OutPoint) -> int
    {
        This->Reaction.ActiveSlot = BestSlot;
        This->Reaction.State = 0;
        This->Reaction.LastObstacleTag = BestCandidate->get_id();
        This->Reaction.bInReaction = false;
        Vector& Heading = This->Reaction.Heading;
        Heading.x = OutPoint.x - ShipXform->Translation.x;
        Heading.y = OutPoint.y - ShipXform->Translation.y;
        Heading.z = OutPoint.z - ShipXform->Translation.z;
        double const Inv = 1.0 / std::sqrt(((double)(Heading.x) * Heading.x +
                                            (double)(Heading.y) * Heading.y) +
                                           (double)(Heading.z) * Heading.z);
        Heading.x = (float)(Inv * Heading.x);
        Heading.y = (float)(Inv * Heading.y);
        Heading.z = (float)(Inv * Heading.z);
        Matrix const& Rotation = ShipXform->Rotation;
        double const Facing = (-(double)(Rotation.E[2][2]) * Heading.z +
                               -(double)(Rotation.E[1][2]) * Heading.y) +
                              -(double)(Rotation.E[0][2]) * Heading.x;
        if (Facing < 0.5)
            This->Reaction.bInReaction = true;
        return 0;
    };

    // LAB_062dd122: FUN_062e1410(WorkingTarget, BestSlot) == 1 -> NudgeObstaclePathPoint
    // (BestSlot, WorkingTarget, ship transform, S+0x80, bCloseEncounter); non-zero returns.
    Vector OutPoint;
    if (PointInCapsule(WorkingTarget, BestSlot.Timer, BestSlot.Point, BestSlot.Direction))
    {
        int const Nudge = NudgeObstaclePathPoint(BestSlot, WorkingTarget, *ShipXform, &OutPoint,
                                                 This->Reaction.bCloseEncounter);
        return Nudge == 0 ? CommitReaction(OutPoint) : Nudge;
    }

    if (bHasCloseFlags)
    {
        // 062dd181-062dd247: get_physical_radius (+0x2c) and get_atmosphere_range (+0x140)
        // into zeroed floats, the larger (TEST AH,0x41 + JNZ keeps the first unless the
        // second is greater) plus 200.0f FSTP float; the ray from the ship toward
        // WorkingTarget (z FST float, Inv = 1.0f / sqrt((z*zf + y*y) + x*x) FSTP float) into
        // FUN_062df830; anything but 1 returns 4.
        float Radius = 0.0f;
        Vector Center;
        BestCandidate->get_physical_radius(Radius, Center);
        float Range = 0.0f;
        BestCandidate->get_atmosphere_range(Range);
        if (Range > Radius)
            Radius = Range;
        float const SphereRadius = (float)((double)(Radius) + 200.0f);
        double const RX = (double)(WorkingTarget.x) - ShipXform->Translation.x;
        double const RY = (double)(WorkingTarget.y) - ShipXform->Translation.y;
        double const RZ = (double)(WorkingTarget.z) - ShipXform->Translation.z;
        float const RZf = (float)(RZ);
        float const Inv = (float)(1.0 / std::sqrt((RZ * RZf + RY * RY) + RX * RX));
        Vector RayDir;
        RayDir.x = (float)(RX * Inv);
        RayDir.y = (float)(RY * Inv);
        RayDir.z = (float)((double)(RZf) * Inv);
        if (!RaySphereIntersect(ShipXform->Translation, RayDir, Center, SphereRadius, nullptr))
            return 4;
    }
    else
    {
        // LAB_062dd381: cobject (+0x150) narrowed to class & 3 == 3 (else null), then
        // PhySys::FindRayIntersect(it, ship position, WorkingTarget, S+0xa8, 1); zero returns 4.
        CObject const* const Cobj = BestCandidate->cobject();
        CObject* const Exclude = (Cobj != nullptr && ((unsigned)Cobj->ObjectClass & 3u) == 3u)
            ? const_cast<CObject*>(Cobj)
            : nullptr;
        PhySys::RayHit Hit[1];
        if (PhySys::FindRayIntersect(Exclude, ShipXform->Translation, WorkingTarget, Hit, 1) == 0)
            return 4;
    }

    // LAB_062dd24d: NudgeObstaclePathPoint(BestSlot, WorkingTarget, ship transform, S+0x80, 0).
    int const Nudge = NudgeObstaclePathPoint(BestSlot, WorkingTarget, *ShipXform, &OutPoint, false);
    return Nudge == 0 ? CommitReaction(OutPoint) : Nudge;
}

// Common.dll.c 0x062dc590 (FUN_062dc590), item 1680. Re-ported 2026-10-08 (cmn-ai9) from
// the full listing 062dc590-062dc837 (thiscall, RET 0x4).
void PathfindManager::UpdateHeadingBlend(float Dt)
{
    // 062dc598-062dc5bb: the phase, then the owner ship's transform (+0x10) fetched before
    // any branch.
    int const Phase = ClassifyObstaclePhase();
    Transform const* const ShipXform = ResolveShipTransform(Owner);

    Vector& Heading = Reaction.Heading;
    float& Timer = BlendTimer;

    if (Phase == 0)
    {
        // 062dc5c4-062dc5f0.
        BlendStart = Heading;
        Timer = 4.0f;
        return;
    }

    if (Phase != 1)
    {
        // 062dc5fc-062dc627: Timer -= Dt (FST to a local and to +0x1a0), FCOMP 0.0f +
        // TEST AH,0x5 + JNP resets only on "less" (NaN continues), then the +0x1b0 byte.
        double const NewTimer = (double)(Timer) - Dt;
        float const LocalTimer = (float)(NewTimer);
        Timer = LocalTimer;
        if (!(NewTimer < 0.0) && bBlendEnabled)
        {
            // 062dc62d-062dc688: D = target(+0x1b4) - ship position, x / y on the FPU, z
            // FST float and squared as z * float(z); Inv = 1.0f / sqrt((z*zf + x*x) + y*y)
            // FSTP float; Dir = (x*Inv, y*Inv, zf*Inv), each FSTP float.
            Vector const& TargetPos = TargetPosition;
            double const DX = (double)(TargetPos.x) - ShipXform->Translation.x;
            double const DY = (double)(TargetPos.y) - ShipXform->Translation.y;
            double const DZ = (double)(TargetPos.z) - ShipXform->Translation.z;
            float const DZf = (float)(DZ);
            float const Inv = (float)(1.0 / std::sqrt((DZ * DZf + DX * DX) + DY * DY));
            Vector Dir;
            Dir.x = (float)(DX * Inv);
            Dir.y = (float)(DY * Inv);
            Dir.z = (float)((double)(DZf) * Inv);

            // 062dc68c-062dc6b3: Dot = (D.x*S.x + D.z*S.z) + D.y*S.y, FST float; the unrounded
            // value FCOMP double 0.999 + TEST AH,0x41 + JNZ: only greater snaps to Dir.
            double const Dot = ((double)(Dir.x) * BlendStart.x + (double)(Dir.z) * BlendStart.z) +
                               (double)(Dir.y) * BlendStart.y;
            float const DotF = (float)(Dot);
            Vector NewHeading;
            if (Dot > 0.999)
            {
                NewHeading = Dir;
            }
            else
            {
                // 062dc6c6-062dc6fc: Axis = cross(BlendStart, Dir), each FSTP float.
                Vector Axis;
                Axis.x = (float)((double)(Dir.z) * BlendStart.y - (double)(Dir.y) * BlendStart.z);
                Axis.y = (float)((double)(Dir.x) * BlendStart.z - (double)(Dir.z) * BlendStart.x);
                Axis.z = (float)((double)(Dir.y) * BlendStart.x - (double)(Dir.x) * BlendStart.y);

                // 062dc700-062dc72e: Half = acos(DotF) * ((4.0f - LocalTimer) * 0.25f) * 0.5f
                // on the FPU; W = FCOS(Half) FSTP float.
                double const HalfAngle = std::acos((double)(DotF)) *
                                         (((double)(4.0f) - LocalTimer) * 0.25f) * 0.5f;
                FQuaternion Quat;
                Quat.W = static_cast<float>(std::cos(static_cast<double>(HalfAngle)));

                // 062dc732-062dc780: K = sin(Half) / sqrt((z*z + x*x) + y*y) FSTP float, then
                // FUN_06288830 scales Axis by K (one float product per component).
                double const AxisLength = std::sqrt(((double)(Axis.z) * Axis.z +
                                                     (double)(Axis.x) * Axis.x) +
                                                    (double)(Axis.y) * Axis.y);
                float const K = (float)(std::sin(static_cast<double>(HalfAngle)) / AxisLength);
                Quat.X = Axis.x * K;
                Quat.Y = Axis.y * K;
                Quat.Z = Axis.z * K;

                // 062dc784-062dc7cc: QuaternionToMatrix (+0x54), then TransformMatrix (+0x2c)
                // of BlendStart, the engine (DAT_063fc994) created lazily with no null test.
                Matrix RotMatrix;
                Acquire3DMathEngine()->QuaternionToMatrix((FMatrix&)(RotMatrix), Quat);
                Acquire3DMathEngine()->TransformMatrix((FVector&)(NewHeading),
                                                       (FMatrix const&)(RotMatrix),
                                                       (FVector const&)(BlendStart));
            }

            // 062dc7d0-062dc7fa.
            Reaction.LastObstacleTag = 0;
            Reaction.bInReaction = false;
            Reaction.State = 2;
            Heading = NewHeading;
            return;
        }
    }

    // Common.dll.txt:227403-227416: full reset (phase==1, timer expired, or
    // BlendEnabled clear).
    Timer = 0.0f;
    Reaction.LastObstacleTag = 0;
    Reaction.State = 3;
    Heading = Vector{ 0.0f, 0.0f, 0.0f };
    Reaction.bInReaction = false;
    Reaction.bCloseEncounter = false;
}

// ============================================================================
// FAiTarget's two out-of-line methods, and the "pick the obstacle worth
// dodging" scan that uses them (items 1470 / 1283 / 1770 / 1803).
//
// Everything below was derived from Common.dll.txt with ESP tracked by hand,
// not from the .c pseudo-decompile. The one trap the whole 0x062e7a30 body
// hinges on is that `PUSH EBX` at 062e7cb1 sits between the prologue and the
// loop, so a given `[ESP+N]` text names two different addresses either side of
// it (and a third inside any pending argument push). Where a stack slot is
// cited in a comment below it is given as F-relative (F = entry ESP - 0x190,
// i.e. after `SUB ESP,0x184; PUSH EBP; PUSH ESI; PUSH EDI`) outside the loop
// and G-relative (G = F - 4) inside it.
// ============================================================================


namespace
{
    // .data:DAT_063ec6b0 (item 6666) == 0x3f800000 == 1.0f. A genuine .data
    // slot, not .rdata, but it has exactly two cross-references in the whole
    // module (062e7ac1 and 062e808e) and both are `FMUL float ptr [...]` reads
    // -- nothing anywhere writes it -- so it is a tuning knob that shipped at
    // its default. Both uses scale a radius before a collision test.
    float const g_AiObstacleRadiusScale = 1.0f;

    // The literal pushed as get_scanlist's `Type` at 062e7c77 and re-tested
    // against each candidate's own cached get_type flags at 062e7e2b. Retail
    // spells the same constant out twice; kept as one name here.
    unsigned const kAvoidScanTypeMask = 0x207f8010u;

    // SystemManager+0x14FC -- the owner's own FAiTarget. Same address
    // arithmetic common_iustategraph.cpp's OwnerElementOf() does; duplicated
    // rather than shared because that helper has internal linkage there.
    FAiTarget* AiTargetOwnerElementOf(SystemManager* Sys)
    {
        return &Sys->SelfTarget;
    }

    // `MOV EAX,[EDI+4]; ADD EAX,8; MOV EAX,[EAX]` then the usual `-8` Watchable
    // adjust. FindClosestObstacleToAvoid re-runs this NINE times rather than
    // caching it (062e7ab7, 062e7adf, 062e7b26, 062e7b46, 062e7bed, 062e7c11,
    // 062e7c31, 062e7c5c, 062e7cd6 -- the last one once per candidate) --
    // reproduced, since a watcher target can in principle go null between two
    // of them.
    IObjRW* AiOwnerObjectOf(SystemManager* Sys)
    {
        return ObjRwFromWatchable(const_cast<Watchable*>(Sys->Watcher.target()));
    }

    // The lazily-cached type-flags fetch retail inlines at three sites inside
    // FindClosestObstacleToAvoid (062e7cfe, 062e7e01, 062e7e83). Unlike
    // FAiTarget::RefreshPos, the guard is raised AFTER the vcall here
    // (062e7d1a then 062e7d20) -- the same split common_largeshipbehavior.cpp
    // and common_iustategraph.cpp already document on their own copies.
    void RefreshAiTargetFlags(FAiTarget& Self)
    {
        if (Self.bFlagsValid != 0)
            return;
        // 062e7d09-062e7d1a: `CALL [EDX+0x88]` on +0x84 with no null test.
        Self.Iface->get_type(Self.CachedFlags); // slot 34, +0x88
        Self.bFlagsValid = 1;
    }

    // FUN_06347e60 (item 2879) as retail leaves it in ST0: x, y, z loaded,
    // sqrt((x*x + y*y) + z*z), never rounded by the helper itself.
    double VectorLengthX87(Vector const& V)
    {
        return std::sqrt(((double)(V.x) * V.x + (double)(V.y) * V.y) +
                         (double)(V.z) * V.z);
    }
}

// FUN_062e7a30, item 1803. See common_aisteer.h for the receiver
// identification, the three inverted reject tests this pass corrected, and the
// swapped geometry arms.
IObjInspect* FindClosestObstacleToAvoid(SystemManagerTimerTable* Self,
                                                   unsigned ExcludeId,
                                                   unsigned TypeMask)
{
    SystemManager* const Sys = Self->Owner;
    FAiTarget* const OwnerElem = AiTargetOwnerElementOf(Sys);

    // ---- 062e7a3b-062e7a7a: too slow to need to dodge anything -------------
    // Slot 1 get_velocity() returns a Vector by value; retail's `LEA ECX,
    // [ESP+0x68]; PUSH ECX` is MSVC's hidden return buffer, not an argument.
    // FUN_06347e60 is the plain magnitude helper (item 2879), not FastInvSqrt.
    Vector const OwnerVelocity = OwnerElem->Iface->get_velocity();
    // 062e7a5e `FST float [F+0x30]` keeps the rounded Speed for every later
    // use, but the FCOMP right after it compares the UNROUNDED length.
    double const SpeedExact = VectorLengthX87(OwnerVelocity);
    float const Speed = (float)(SpeedExact);
    // 062e7a6a `TEST AH,0x5; JP` continues on >= and on unordered, so the
    // early-out below fires only on a strict less-than.
    if (SpeedExact < 5.0) // .rdata:063a078c == 0x40a00000
        return nullptr;

    // ---- 062e7a7d-062e7ac7: the owner's frame and swept radius -------------
    // Retail calls slot 4 (+0x10, get_orientation) and then reads +0x24 off
    // the result as a Vector, which only works because slots 3 and 4 are an
    // /OPT:ICF fold of one another (see common_iface.h). The caller wants the
    // Transform, so the port calls slot 3 -- same address, correct type. This
    // file's own ResolveShipTransform() already resolves the identical idiom
    // the same way.
    Transform const& OwnerXform = OwnerElem->Iface->get_transform();
    OwnerElem->RefreshPos(); // FUN_062c47c0 at 062e7aaf
    float const OwnerRadiusScaled = OwnerElem->CachedRadius * g_AiObstacleRadiusScale;

    // 062e7ad4-062e7ada. GetNegatedIfaceDirection (FUN_062e6150, item 1772)
    // returns the NEGATED column 2 of the object's rotation, which is what
    // this module calls "forward" -- so Heading points where the ship is
    // going, and the dot test further down reads as "is the candidate ahead".
    Vector Heading{ 0.0f, 0.0f, 0.0f };
    GetNegatedIfaceDirection(&Heading, AiOwnerObjectOf(Sys));

    // ---- 062e7adf-062e7b23: engine-readiness gate --------------------------
    // 062e7b02 multiplies by the DOUBLE at .rdata:0639df88 (0x3fe0000000000000
    // == 0.5), not a float, so the product is evaluated in double precision.
    // 062e7b13 `TEST AH,0x41; JP` continues on a strict greater-than AND on
    // unordered (AH & 0x41 == 0x41 has even parity); less and equal fall into
    // the early-out, so a NaN readiness goes on.
    double const Readiness =
        (double)(Behavior_InvokeManagerSlotD8(AiOwnerObjectOf(Sys), 1.0f,
                                                         ThrustEquipTradelane)) * 0.5;
    if (Readiness <= 1.0) // .rdata:0639a1d0 == 1.0f
        return nullptr;

    // ---- 062e7b26-062e7b69: the look-ahead horizon -------------------------
    // Behavior_QueryChainedRadiusEffect is FUN_062e59b0 (item 1760); the two
    // literals are the 0.0f from `PUSH EBP` (EBP was zeroed at 062e7a8d) and
    // the raw 3 that this family's ports spell ThrustEquipTradelane.
    // `stopping distance / speed + 6 seconds` is a time; Reach is how far the
    // ship travels in it.
    // 062e7b42-062e7b69: `FDIV Speed; FADD 6.0f; FST LookAheadTime(F+0x48);
    // FMUL Speed; FLD ST0; FMUL ST1; FSTP ReachSq(F+0x78)` -- LookAheadTime is
    // the rounded image, but the squared reach is built from the UNROUNDED
    // time and rounded once.
    double const LookAheadTimeExact =
        (double)(Behavior_QueryChainedRadiusEffect(AiOwnerObjectOf(Sys), 0.0f,
                                                              ThrustEquipTradelane)) / Speed
        + 6.0; // .rdata:063a07e0 == 0x40c00000
    float const LookAheadTime = (float)(LookAheadTimeExact);
    double const Reach = LookAheadTimeExact * Speed;
    float const ReachSq = (float)(Reach * Reach);

    // ---- 062e7b74-062e7be6: the owner's own archetype box, inflated --------
    // Slot 10 get_archetype_extents(Min, Max) -- the Min out-param is pushed
    // LAST (062e7b7f) so it is argument one. The six results are then
    // INTERLEAVED into {MinX, MaxX, MinY, MaxY, MinZ, MaxZ}, which is exactly
    // the `float const* Aabb` layout TransformedBoxesIntersect (item 1719)
    // documents. .rdata:0639e6d0 == 1.5f inflates it by half again.
    //
    // RETAIL UB: retail leaves all six slots (F+0x84..F+0x98) uninitialised
    // when get_archetype_extents fails (IObjInspectImpl returns -5 when the
    // owner CObject has no SurfaceExtentsPtr) and still passes them to the
    // box test; the port cannot reproduce stack garbage and zero-fills them.
    // The watcher target is not null-tested: retail's own `XOR ECX,ECX` arm at
    // 062e7b72 falls straight into `MOV EDX,[ECX]`.
    float OwnerBox[6] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    {
        Vector OwnerMin{ 0.0f, 0.0f, 0.0f };
        Vector OwnerMax{ 0.0f, 0.0f, 0.0f };
        if (AiOwnerObjectOf(Sys)->get_archetype_extents(OwnerMin, OwnerMax) == 0)
        {
            OwnerBox[0] = OwnerMin.x * 1.5f;
            OwnerBox[1] = OwnerMax.x * 1.5f;
            OwnerBox[2] = OwnerMin.y * 1.5f;
            OwnerBox[3] = OwnerMax.y * 1.5f;
            OwnerBox[4] = OwnerMin.z * 1.5f;
            OwnerBox[5] = OwnerMax.z * 1.5f;
        }
    }

    // ---- 062e7bed-062e7c0b: who the owner is flying formation with ---------
    // F+0x38 zeroed at 062e7bf3, then slot 44 (+0xb0) with no null test on the
    // watcher target (062e7c02 `XOR ECX,ECX` arm dereferences).
    IObjRW* OwnerFormationLeader = nullptr;
    AiOwnerObjectOf(Sys)->get_formation_leader(OwnerFormationLeader);

    // ---- 062e7c11-062e7c8d: the scanner, and its scan list -----------------
    // This watcher read is the one retail actually guards (062e7c1d/062e7c26,
    // both jumping to the null return).
    IObjRW* const Scanner = AiOwnerObjectOf(Sys);
    if (Scanner == nullptr)
        return nullptr;

    // 062e7c28 calls FUN_062e8270, which is EquipStatus's inlined default
    // constructor clearing its bitfields in place (`Flags &= 0xF8;
    // EquipFlags &= 0xE0000000`) over an UNINITIALISED stack local. Only bit
    // 0x02 of Flags is ever read here and that mask clears it, so a full
    // zero-init is equivalent -- the same call BuildAiTargetList (item 1800)
    // already documents and handles this way.
    EquipStatus Status;
    Status.Flags = 0;
    Status.Pad01 = 0;
    Status.Pad02 = 0;
    Status.Pad03 = 0;
    Status.EquipFlags = 0;

    ScanList const* List = nullptr; // pre-zeroed at 062e7c17
    Scanner->get_equipment_status(Status, Self->ScannerEquipId); // slot 70, +0x118
    if ((Status.Flags & EquipStatusFlagDestroyed) == 0)
    {
        // A destroyed scanner skips the query entirely and leaves List null,
        // which the check below turns into "no obstacle". Slot 65, +0x104,
        // with no null test on the watcher target (062e7c6d `XOR ECX,ECX` arm
        // dereferences). bKeepCache is forwarded as `bFlag`.
        AiOwnerObjectOf(Sys)->get_scanlist(List, kAvoidScanTypeMask, Self->bKeepCache != 0);
    }
    if (List == nullptr)
        return nullptr;

    // ---- 062e7c9d-062e8267: one pass over the scanner's contacts -----------
    // 062e7cab is `CMP Count,0; JBE` and 062e824d is `CMP idx,Count; JC`, both
    // UNSIGNED, so a negative Count would wrap rather than end the loop.
    // Retail re-reads Count every iteration (062e8246); so does this.
    IObjInspect* Result = nullptr;                  // F+0x44, zeroed at 062e7a91
    float BestDistance = 1.0e12f;                   // F+0x54, 0x5368d4a5

    // Retail keeps ONE 0x88-byte scratch record on its frame (G+0xc4) and
    // re-Init()s it per candidate; the zero-fill here is once, outside the
    // loop, for the same reason BuildAiTargetList zero-fills its own.
    FAiTarget Candidate;
    std::memset(&Candidate, 0, sizeof(Candidate));

    for (unsigned Index = 0; Index < (unsigned)List->Count; ++Index)
    {
        // 062e7cb2-062e7cc2: null entry, then retail's dead re-test of the
        // adjusted pointer (only a literal 0x8 sentinel could satisfy it).
        IObjRW* const CandidateObj =
            ObjRwFromWatchable(const_cast<Watchable*>(List->Entries[Index].target()));
        if (CandidateObj == nullptr)
            continue;

        // 062e7cd6-062e7cf9: `PUSH candidate; PUSH owner` -- the first stack
        // slot is the attitude reference, so the owner goes in +0x80 and the
        // contact in +0x84.
        Candidate.Init(AiOwnerObjectOf(Sys), CandidateObj);

        // (2) 062e7cfe-062e7d36: the caller's own type filter.
        RefreshAiTargetFlags(Candidate);
        if ((Candidate.CachedFlags & TypeMask) == 0)
            continue;

        Candidate.RefreshPos(); // 062e7d43

        // (3) 062e7d48-062e7d8c: is it ahead? FUN_06288800 is Vec3Sub (item
        // 729) called as (Out, Candidate.CachedPos, OwnerXform.Translation),
        // and the three FMULs that follow pair x*x, z*z, y*y -- a dot product
        // written out of order by the scheduler, nothing more.
        //
        // POLARITY, because the inherited note had this backwards: 062e7d89 is
        // `TEST AH,0x5; JNP <next candidate>`, and AH & 0x5 has ODD parity
        // only for C0 alone, i.e. only for a strict ST0 < src. So the reject
        // fires on dot < 0 and the candidate is KEPT when it is in the
        // forward half-space.
        // The sum stays on the x87 stack: (hx*tx + hz*tz) + hy*ty, unrounded.
        Vector const ToCandidate = Sub(Candidate.CachedPos, OwnerXform.Translation);
        double const AheadDot =
            ((double)(Heading.x) * ToCandidate.x + (double)(Heading.z) * ToCandidate.z) +
            (double)(Heading.y) * ToCandidate.y;
        if (AheadDot < 0.0) // .rdata:06399408 == 0.0f
            continue;

        // (4) 062e7d92-062e7db7: is it inside the look-ahead? 062e7db4 is
        // `TEST AH,0x41; JZ <next candidate>`, and AH & 0x41 is zero only for
        // a strict ST0 > src -- so the reject fires on LenSq > ReachSq. Also
        // inverted in the inherited note. Unrounded (x*x + z*z) + y*y against
        // the float ReachSq (F+0x78).
        double const ToCandidateLenSq =
            ((double)(ToCandidate.x) * ToCandidate.x + (double)(ToCandidate.z) * ToCandidate.z) +
            (double)(ToCandidate.y) * ToCandidate.y;
        if (ToCandidateLenSq > ReachSq)
            continue;

        // (5) 062e7dbd-062e7dd0: the caller's excluded object.
        if (Candidate.Iface->get_id() == ExcludeId) // slot 8, +0x20
            continue;

        // (6) 062e7dd6-062e7dfb: never dodge yourself. Retail asks the OWNER
        // first (062e7de8) and the candidate second (062e7df6); kept in that
        // order because both are virtual calls with unknown side effects.
        {
            unsigned const OwnerId = OwnerElem->Iface->get_id();
            unsigned const CandidateId = Candidate.Iface->get_id();
            if (CandidateId == OwnerId)
                continue;
        }

        // (7) 062e7e01-062e7e36: the same mask the scan list was requested
        // with, re-applied. The scanner can return contacts outside its own
        // requested mask, which is presumably why retail bothers.
        RefreshAiTargetFlags(Candidate);
        if ((Candidate.CachedFlags & kAvoidScanTypeMask) == 0)
            continue;

        // (8) 062e7e3c-062e7e7d: too big to dodge -- planets, stations and
        // the like are somebody else's problem. 062e7e7a is `TEST AH,0x41;
        // JZ`, i.e. reject on a strict CachedRadius > 1000. Third and last of
        // the inherited note's inverted tests.
        Candidate.RefreshPos();
        if (Candidate.CachedRadius > 1000.0f) // .rdata:063a0608 == 0x447a0000
            continue;

        // ---- 062e7e83-062e7f02: pick the geometry arm and the horizon ------
        RefreshAiTargetFlags(Candidate);
        float EffectiveTime = LookAheadTime;
        // 062e7ecd `SHR EBX,0x1d; AND BL,1` -- bit 29 of the type flags.
        bool bUseBoxTest = ((Candidate.CachedFlags >> 29) & 1u) != 0;

        IObjRW* CandidateLeader = nullptr;
        Candidate.Iface->get_formation_leader(CandidateLeader);
        if (CandidateLeader != nullptr && OwnerFormationLeader != nullptr)
        {
            // Retail asks the CANDIDATE's leader first (062e7eea).
            unsigned const CandidateLeaderId = CandidateLeader->get_id();
            unsigned const OwnerLeaderId = OwnerFormationLeader->get_id();
            if (CandidateLeaderId == OwnerLeaderId)
            {
                // A wingman in the same formation gets the tight box test over
                // a much shorter horizon -- you are supposed to fly close to it.
                bUseBoxTest = true;
                EffectiveTime = 0.3f; // 0x3e99999a
            }
        }

        // ---- 062e7f06-062e7f8c: both bodies' travel over the horizon -------
        Candidate.RefreshPos();
        Vector const CandidatePos = Candidate.CachedPos; // copied to G+0x1c at 062e7f4a
        Vector const CandidateVelocity = Candidate.Iface->get_velocity();
        // 062e7f6a-062e7f7b: EffT * Speed(float), one rounding (FSTP).
        float const OwnerTravel = EffectiveTime * Speed;
        // 062e7f7f-062e7f8c: the unrounded FUN_06347e60 length times EffT,
        // rounded once by the FSTP.
        float const CandidateTravel =
            (float)(VectorLengthX87(CandidateVelocity) * EffectiveTime);
        // Retail stores both of these NEGATED (FCHS at 062e7f79 and 062e7f8a)
        // because every use below subtracts them; kept positive here with the
        // sign moved to the use sites.

        bool bWouldCollide = false;
        if (bUseBoxTest)
        {
            // ---- 062e7f96-062e8071: oriented-box sweep ---------------------
            Vector CandidateMin{ 0.0f, 0.0f, 0.0f };
            Vector CandidateMax{ 0.0f, 0.0f, 0.0f };
            // (9a) Unlike the owner's own extents above, a FAILING query here
            // rejects the candidate outright (062e7fb4).
            if (Candidate.Iface->get_archetype_extents(CandidateMin, CandidateMax) != 0)
                continue;

            // Same interleave as OwnerBox, with the Z minimum pushed back by
            // how far the candidate travels -- i.e. the box swept along its
            // own local -Z. Note retail does NOT apply the 1.5f inflation to
            // the candidate, only to the owner.
            float const CandidateBox[6] = {
                CandidateMin.x, CandidateMax.x,
                CandidateMin.y, CandidateMax.y,
                CandidateMin.z - CandidateTravel, CandidateMax.z
            };

            // The owner's box is built once, outside the loop, but its swept
            // Z minimum depends on EffectiveTime and so changes per candidate.
            // Retail saves the original into a scratch slot at 062e803c and
            // puts it back at 062e806a; the same save/restore is what keeps
            // the next iteration correct.
            float const SavedOwnerMinZ = OwnerBox[4];
            OwnerBox[4] = SavedOwnerMinZ - OwnerTravel;

            // Slot 4 again -- see the get_transform note at the top.
            Transform const& CandidateXform = Candidate.Iface->get_transform();
            // 062e8063 `DEC EAX; NEG EAX; SBB AL,AL; INC AL` is "== 1", not
            // "!= 0"; TransformedBoxesIntersect returns bool, so the two agree.
            bWouldCollide = TransformedBoxesIntersect(OwnerXform, OwnerBox,
                                                      CandidateXform, CandidateBox);

            OwnerBox[4] = SavedOwnerMinZ;
        }
        else
        {
            // ---- 062e8076-062e81b2: swept-sphere time of impact ------------
            // GetAiTargetEffectiveRadius is FUN_062e6070 (item 1770), the only
            // call site it has in the module.
            float const CandidateRadiusScaled =
                GetAiTargetEffectiveRadius(&Candidate) * g_AiObstacleRadiusScale;

            // 062e80a4-062e80ce: slot 3 get_transform(), then the +0x08/+0x14/
            // +0x20 reads are column 2 of the rotation, then FUN_062c5a70
            // negates it -- so this is the candidate's forward heading, built
            // exactly the way GetNegatedIfaceDirection builds the owner's.
            Transform const& CandidateXform = Candidate.Iface->get_transform();
            Vector const CandidateForward{ -CandidateXform.Rotation.E[0][2],
                                           -CandidateXform.Rotation.E[1][2],
                                           -CandidateXform.Rotation.E[2][2] };

            // 062e810d-062e813a and 062e813f-062e817d: where each body ends up
            // after EffectiveTime, assuming both hold their current heading.
            // CandidateEnd rounds twice (the scaled forward is built into a
            // Vector by FUN_0628b030 first, 062e80d3-062e8108, then summed,
            // 062e810d-062e813a); OwnerEnd keeps each H*OwnerTravel product on
            // the x87 stack and rounds the sum once (062e8147-062e8178).
            Vector const CandidateEnd = Add(CandidatePos, Scale(CandidateForward, CandidateTravel));
            Vector const OwnerEnd{
                (float)((double)(Heading.x) * OwnerTravel + OwnerXform.Translation.x),
                (float)((double)(Heading.y) * OwnerTravel + OwnerXform.Translation.y),
                (float)((double)(Heading.z) * OwnerTravel + OwnerXform.Translation.z) };

            // Seven cdecl arguments (`ADD ESP,0x1c` at 062e81b2). ImpactTime
            // is zeroed at 062e807e and then never read -- retail wants only
            // the boolean.
            float ImpactTime = 0.0f;
            bWouldCollide = SweptSphereCollisionTime(OwnerRadiusScaled,
                                                     OwnerXform.Translation, OwnerEnd,
                                                     CandidateRadiusScaled,
                                                     CandidatePos, CandidateEnd,
                                                     &ImpactTime);
        }

        // (9b) 062e81b5
        if (!bWouldCollide)
            continue;

        // ---- (10) 062e81bd-062e823a: nearest wins ---------------------------
        // Retail's own fast-length idiom, inlined: the three differences stay
        // on the x87 stack, S = (dz*dz + dy*dy) + dx*dx is unrounded, only the
        // FST float image of S (062e81f4) feeds the 0x5f3759df magic (SAR, so
        // a signed halving), and the Newton step (0.5f at .rdata:0639c13c,
        // 1.5f at .rdata:0639e6d0) plus the final multiply run on the
        // unrounded S -- exactly CmnAiSteer::InlineFastLength. 062e8226 is
        // `TEST AH,0x5; JP <discard>`, so the new best must be STRICTLY
        // nearer -- ties and NaN keep the earlier candidate; the FSTP at
        // 062e8232 stores the rounded distance.
        double const Dx = (double)(CandidatePos.x) - OwnerXform.Translation.x;
        double const Dy = (double)(CandidatePos.y) - OwnerXform.Translation.y;
        double const Dz = (double)(CandidatePos.z) - OwnerXform.Translation.z;
        double const SeparationSq = (Dz * Dz + Dy * Dy) + Dx * Dx;
        double const Distance = InlineFastLength(SeparationSq);
        if (Distance < BestDistance)
        {
            BestDistance = (float)(Distance);
            Result = Candidate.Iface;
        }
    }

    return Result; // 062e825a reads the same slot 062e7a91 zeroed
}
