// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// LaneAction (trade-lane transit controller) -- split out of common_cequip.cpp (CMN-B2).
// Declared in common_cequip.h. Retail re-derives the watched traveller (`head ? head - 8 : 0`)
// at every use; TradelaneObj() is that expression, and nothing between two uses changes it.
#include <cmath>
#include "common_cequip.h"
#include "x86math.h"

#include "common_cobject.h"
#include "common_cobjdb.h"   // IObjDB::FindById (the ring hop)
#include "common_csys.h"     // Acquire3DMathEngine, MatrixSetIdentity
#include "common_iface.h"    // IObjRW / IObjInspect / IObjAffect
#include "common_ini.h"
#include "common_misc.h"     // SinglePlayer
#include "common_physys.h"
#include "common_universe.h"
#include "../IVP/ivp_physics/ivp_controller.h"  // IVP_Event_Sim (do_simulation_controller)

#include <math.h>
#include <string.h>

namespace
{
    // FUN_0628b070: Out = MatrixToQuaternion(In) through the 3D math engine (slot 0x50, no
    // null test); returns &Out.
    Quaternion* MatrixToQuaternionViaMathEngine(Quaternion& Out, Matrix const& In)
    {
        I3DMathEngine* Math = Acquire3DMathEngine();
        Math->MatrixToQuaternion((FQuaternion&)Out, (FMatrix const&)In); // +0x50
        return &Out;
    }
}

IObjRW* LaneAction::TradelaneObj() const
{
    return const_cast<IObjRW*>(static_cast<IObjRW const*>(TradelaneObjWatcher.target()));
}

LaneAction::LaneAction(IObjInspect const* InEntranceRing, IObjInspect const* InExitRing, IObjRW* InTradelaneObj,
                       bool bFastExit, float InSpeed)
{
    // 062c1220 (RET 0x14). Retail leaves +0xcc..+0xe7 and +0xf9..+0xfb unwritten here
    // (InitializeApproach fills +0xcc..+0xe7); the port zeroes them so they are deterministic.
    CapturedShipPos = Vector{ 0.0f, 0.0f, 0.0f };
    memset(&CapturedShipOrient, 0, sizeof(CapturedShipOrient));
    Opaque_F8[1] = 0;
    Opaque_F8[2] = 0;
    Opaque_F8[3] = 0;

    // 062c1257-062c1300: identity rotations and zero positions in both ring frames and the
    // lane pose (the ring slots are left for BuildRingTransform).
    MatrixSetIdentity(EntranceRingXform.Orient);
    EntranceRingXform.Pos = Vector{ 0.0f, 0.0f, 0.0f };
    MatrixSetIdentity(ExitRingXform.Orient);
    ExitRingXform.Pos = Vector{ 0.0f, 0.0f, 0.0f };
    MatrixSetIdentity(LanePose.Rotation);
    LanePose.Translation = Vector{ 0.0f, 0.0f, 0.0f };

    // 062c1318-062c1326: watch the traveller's Watchable (+8; 0 for none) when it differs.
    TradelaneObjWatcher.set(InTradelaneObj);

    EntranceRing = InEntranceRing;   // +0x20
    ExitRing = InExitRing;           // +0x24

    // 062c133f-062c1363: bHpLeftLane = 1, cleared unless the ring after the entrance ring
    // (traverse_rings forward, the id pre-zeroed) is the exit ring. No null tests.
    bHpLeftLane = 1;
    unsigned NextRingId = 0;
    EntranceRing->traverse_rings(NextRingId, true);
    if (NextRingId != ExitRing->get_id())
        bHpLeftLane = 0;
    Field95 = 0;
    Opaque_F8[0] = 0;

    BuildRingTransform(EntranceRing, EntranceRingXform);   // 062c137c
    BuildRingTransform(ExitRing, ExitRingXform);           // 062c1388

    bRingApproachValid = bFastExit ? 1 : 0;   // +0x97
    Speed = InSpeed;                          // +0x98
    ExitState = 0;
    bExitFlag = 0;
    TransitElapsed = 0.0f;
    TransitRamp = 0.0f;
    TransitRampTarget = 0.0f;
    ExitRampStart = 0.0f;

    InitializeApproach();   // 062c13c7
}

LaneAction::~LaneAction()
{
    // 062c1420: the TradelaneObjWatcher member dtor, then ~IAction (+0x10), then
    // ~Controller -- the implicit member and base order.
}

bool LaneAction::update(float Dt)
{
    // 062c13f0: MOV EDX,[ECX+0x80] (ECX = the IAction sub-object, so +0x90); SETNZ; RET 4.
    (void)Dt;
    return ExitState != 0;
}

void LaneAction::request_exit()
{
    // 062c2300
    bExitFlag = 1;
    ExitState = 4;
}

void LaneAction::stop(char bSkipStopTradelane)
{
    // 062c2350
    if (ExitState != 0)
    {
        ExitState = 0;
        PhySys::Controller::UnRegister(this);

        // 062c2371-062c2384: both the watched head and head - 8 must be non-null.
        if (TradelaneObjWatcher.target() != nullptr)
        {
            IObjRW* const Rw = TradelaneObj();
            if (Rw != nullptr)
            {
                // 062c239b-062c23c9: cobject(), kept only as a CEqObj, else 0 -- used without
                // a further test.
                CObject* const Obj = (CObject*)Rw->cobject();
                CEqObj* const Eq = (Obj != nullptr && (Obj->ObjectClass & CObject::ClassEqObj) == CObject::ClassEqObj)
                    ? (CEqObj*)Obj : nullptr;
                Eq->set_control_exclusion(0);
                Eq->enable_collisions_r(true);

                if (bSkipStopTradelane == 0)
                    Rw->stop_tradelane();          // 062c23eb, IObjAffect slot 26

                (void)Rw->cobject();               // 062c23fe, result dropped
                Rw->set_throttle(1.0f);            // 062c241d, IObjAffect slot 0
            }
        }
        ExitState = 0;                             // 062c241f
    }
}

void LaneAction::BuildRingTransform(IObjInspect const* Ring, LaneRingTransform& OutXform)
{
    // 062c1880-062c189e: the traveller's slot 4 (get_orientation), result dropped.
    (void)TradelaneObj()->get_orientation();

    OutXform.Ring = Ring;

    // 062c18af-062c18e1: "HpRightLane" (slot 48, get_hardpoint(Name, Pos, Orient)); the
    // position goes straight to OutXform.Pos.
    Vector HpPos;
    Matrix HpRightOrient;
    Ring->get_hardpoint("HpRightLane", &HpPos, &HpRightOrient);
    OutXform.Pos = HpPos;

    // 062c18e8: CMP byte [this+0x96],0; JZ 062c1902.
    if (bHpLeftLane != 0)
    {
        OutXform.Orient = HpRightOrient;   // 062c18f1-062c18fd, 062c19aa
        return;
    }

    // 062c1902-062c1937: "HpLeftLane" into the same position scratch; its orientation is
    // never read.
    Matrix HpLeftOrient;
    Ring->get_hardpoint("HpLeftLane", &HpPos, &HpLeftOrient);
    OutXform.Pos = HpPos;

    // 062c191e-062c1981: Y rotation by DAT_0639e2d0, the double 3.1415927410125732 (float PI
    // widened, not M_PI), cos and sin each rounded to float.
    double const Angle = 3.1415927410125732;
    float const CosA = static_cast<float>(std::cos(static_cast<double>(Angle)));
    float const SinA = static_cast<float>(std::sin(static_cast<double>(Angle)));
    Matrix const RotY(CosA, 0.0f, SinA,
                      0.0f, 1.0f, 0.0f,
                      -SinA, 0.0f, CosA);

    // 062c1991-062c19b2: MulMatrix(Dst, RotY, HpRightOrient) (slot 0x20, no null test), Dst
    // copied into OutXform.Orient.
    Matrix Turned;
    Acquire3DMathEngine()->MulMatrix((FMatrix&)Turned, (FMatrix const&)RotY, (FMatrix const&)HpRightOrient);
    OutXform.Orient = Turned;
}

void LaneAction::InitializeApproach()
{
    // 062c1490
    if (bRingApproachValid != 0)
    {
        // 062c149d-062c1506: state 2, or 3 when the exit ring has no ring after it.
        ExitState = 2;
        unsigned NextRingId = 0;
        ExitRingXform.Ring->traverse_rings(NextRingId, bHpLeftLane != 0);
        if (NextRingId == 0)
        {
            ExitState = 3;
        }

        // DAT_0639f410 (80.0f) / DAT_0639f3cc (2500.0f).
        TransitRamp = 1.0f;
        TransitElapsed = 3.0f;
        TransitRampTarget = 80.0f / 2500.0f;
    }
    else
    {
        // 062c1508-062c1587: state 1; the traveller's speed / 2500 through the inlined
        // 0x5f3759df fast length.
        ExitState = 1;

        // 062c1512-062c1527: slot 1 get_velocity, no null test.
        Vector const Vel = TradelaneObj()->get_velocity();

        // 062c152a-062c1549: S = (x*x + y*y) + z*z on the FPU, never rounded; only its
        // `FST float` image (062c154d) seeds the guess (SAR 1, 0x5f3759df - bits).
        double const SpeedSq = ((double)Vel.x * Vel.x + (double)Vel.y * Vel.y) + (double)Vel.z * Vel.z;
        float const SpeedSqImage = (float)SpeedSq;
        int Bits;
        memcpy(&Bits, &SpeedSqImage, sizeof(Bits));
        Bits = 0x5f3759df - (Bits >> 1);
        float Guess;
        memcpy(&Guess, &Bits, sizeof(Guess));
        // 062c155d-062c157d: 1.5f (DAT_0639e6d0) - g*g*S*0.5f (DAT_0639c13c); S / 2500.0f
        // (DAT_0639f3cc) times that, times g -- all on the FPU, rounded once by FST +0xf0 and
        // FSTP +0xec.
        double const Refine = 1.5 - (((double)Guess * Guess) * SpeedSq) * 0.5;
        double const Ramp = ((SpeedSq / 2500.0) * Refine) * Guess;
        TransitRampTarget = (float)Ramp;
        TransitRamp = (float)Ramp;
    }

    // 062c158d-062c165d: shared tail.
    IObjRW* const Traveller = TradelaneObj();

    // 062c159b-062c15d1: enable_collisions_r(false) on the cobject() result with no null test;
    // set_control_exclusion(3) on it when it is a CEqObj, else on 0; then
    // Controller::Register(this, Dock).
    CObject* const Dock = (CObject*)Traveller->cobject();                 // slot 84 (+0x150)
    Dock->enable_collisions_r(false);
    CEqObj* const DockEq = (Dock != nullptr && (Dock->ObjectClass & CObject::ClassEqObj) == CObject::ClassEqObj)
        ? (CEqObj*)Dock : nullptr;
    DockEq->set_control_exclusion(3);
    PhySys::Controller::Register(this, Dock);

    // 062c15e7-062c162b: slot 3 rotation -> quaternion at +0xd8.
    Quaternion QuatOut{};
    MatrixToQuaternionViaMathEngine(QuatOut, Traveller->get_transform().Rotation);
    CapturedShipOrient = QuatOut;

    // 062c163d-062c1654: slot 0 position at +0xcc.
    CapturedShipPos = Traveller->get_position();
}

void LaneAction::ProjectOntoSegment(LaneRingTransform const& A, LaneRingTransform const& B, float* OutProgress, Transform* OutResult)
{
    // 062c1660-062c16ab: the traveller's position (slot 0, no null test on the result).
    Vector const ObjPos = TradelaneObj()->get_position();

    // 062c1680-062c16db: Delta = B.Pos - A.Pos (each component rounded by FSTP float), its
    // length summed z, y, x on the FPU and square-rooted unrounded. The float image is kept
    // (FST into the spent first-argument slot) for the progress divide below.
    Vector const Delta{ B.Pos.x - A.Pos.x, B.Pos.y - A.Pos.y, B.Pos.z - A.Pos.z };
    double const SegLenUnrounded =
        sqrt(((double)Delta.z * Delta.z + (double)Delta.y * Delta.y) + (double)Delta.x * Delta.x);
    float const SegLen = (float)SegLenUnrounded;

    // 062c16df-062c16ea: FCOMP double DAT_0639f418 (1e-05); TEST AH,0x41; JNZ -- taken when the
    // length is not greater than the epsilon, or unordered.
    if (!(SegLenUnrounded > 1e-05))
    {
        // 062c1840-062c186a: progress 0, the translation is A.Pos, the rotation A.Orient.
        *OutProgress = 0.0f;
        OutResult->Translation = A.Pos;
        OutResult->Rotation = A.Orient;
        return;
    }

    // 062c16f0-062c1762: Delta rebuilt and its length recomputed; Dir = Delta * (1.0f / length)
    // with the reciprocal kept on the FPU, each component rounded by FSTP float.
    double const InvLen =
        1.0 / sqrt(((double)Delta.z * Delta.z + (double)Delta.y * Delta.y) + (double)Delta.x * Delta.x);
    Vector const Dir{ (float)(Delta.x * InvLen), (float)(Delta.y * InvLen), (float)(Delta.z * InvLen) };

    // 062c1764-062c1784: Rel = ObjPos - A.Pos.
    Vector const Rel{ ObjPos.x - A.Pos.x, ObjPos.y - A.Pos.y, ObjPos.z - A.Pos.z };

    // 062c1789-062c17d0: Along = dot(Dir, Rel) / dot(Dir, Dir), both sums z, y, x, unrounded;
    // progress = Along / the float length image, rounded once.
    double const Dot = ((double)Dir.z * Rel.z + (double)Dir.y * Rel.y) + (double)Rel.x * Dir.x;
    double const DirLenSq = ((double)Dir.z * Dir.z + (double)Dir.y * Dir.y) + (double)Dir.x * Dir.x;
    double const Along = Dot / DirLenSq;
    *OutProgress = (float)(Along / SegLen);

    // 062c17d2-062c1821: Scaled = Dir * Along (rounded per component), then Scaled + A.Pos.
    Vector const Scaled{ (float)(Dir.x * Along), (float)(Dir.y * Along), (float)(Dir.z * Along) };
    OutResult->Translation = Vector{ Scaled.x + A.Pos.x, Scaled.y + A.Pos.y, Scaled.z + A.Pos.z };
    OutResult->Rotation = A.Orient;
}

void LaneAction::do_simulation_controller(IVP_Event_Sim* EventSim, IVP_U_Vector<IVP_Core>* CoreList)
{
    (void)CoreList;

    // 062c19da-062c19f9: no traveller (head or head - 8 null) or state 5 -> stop(0).
    if (TradelaneObjWatcher.target() == nullptr || TradelaneObj() == nullptr || ExitState == 5)
    {
        stop(0);
        return;
    }

    // 062c19ff-062c1a1d: the traveller's slot 4 result, all 0x30 bytes copied: slots 3 and 4
    // share one body (common_iface.h), so this is its transform. Read by the formation-leader
    // check below.
    Transform const Snapshot = TradelaneObj()->get_transform();

    // 062c1a1f-062c1a3d: Dt = EventSim->delta_time; TransitElapsed += Dt.
    float const Dt = EventSim->delta_time;
    TransitElapsed = TransitElapsed + Dt;

    // 062c1a2c-062c1a65: FUN_062748a0 (MatrixSetIdentity) on a stack transform at ESP+0xd0,
    // translation zeroed; nothing reads it.

    // 062c1a70: the progress scratch starts at 0; only ProjectOntoSegment writes it.
    float Progress = 0.0f;

    if (ExitState == 4)
    {
        // 062c1a7e-062c1ab2: the lane pose becomes the traveller's own transform (slot 4).
        LanePose = TradelaneObj()->get_transform();

        // 062c1a91-062c1abf: FCOMP 0.0f (DAT_06399408); TEST AH,0x44; JP skips unless equal.
        if (ExitRampStart == 0.0f)
        {
            ExitRampStart = TransitRamp;
        }

        if (bExitFlag != 0)
        {
            // 062c1ad3-062c1b5d: Angle = Dt * -0.56 (double DAT_0639f460) on the FPU; cos
            // rounded to float, sin kept unrounded. Each row (c0, c2) of an identity matrix is
            // turned in place: c0 = cos*c0 - sin*c2, c2 = c0*sin + cos*c2.
            double const Angle = (double)Dt * -0.56;
            float const CosA = static_cast<float>(std::cos(static_cast<double>(Angle)));
            double const SinA = std::sin(static_cast<double>(Angle));
            Matrix Turn(1.0f, 0.0f, 0.0f,
                        0.0f, 1.0f, 0.0f,
                        0.0f, 0.0f, 1.0f);
            for (int Row = 0; Row < 3; ++Row)
            {
                float const C0 = Turn.E[Row][0];
                float const C2 = Turn.E[Row][2];
                Turn.E[Row][0] = (float)((double)CosA * C0 - SinA * C2);
                Turn.E[Row][2] = (float)((double)C0 * SinA + (double)CosA * C2);
            }

            // 062c1b5f-062c1b96: MulMatrix(Dst, Turn, LanePose.Rotation) (slot 0x20, no null
            // test), Dst copied back into the rotation.
            Matrix Turned;
            Acquire3DMathEngine()->MulMatrix((FMatrix&)Turned, (FMatrix const&)Turn,
                                             (FMatrix const&)LanePose.Rotation);
            LanePose.Rotation = Turned;
        }

        // 062c1b98-062c1bb6: TransitRamp -= (ExitRampStart - TransitRampTarget * 0.4) * Dt,
        // on the FPU (DAT_0639f458 is the double 0.4), stored rounded.
        double const NewRamp =
            (double)TransitRamp - ((double)ExitRampStart - (double)TransitRampTarget * 0.4) * Dt;
        TransitRamp = (float)NewRamp;

        // 062c1bbc-062c1bc7: FCOMP TransitRampTarget on the unrounded value; TEST AH,0x41; JP
        // goes on (greater or unordered). Not greater: clamp, state 5, stop(0).
        if (NewRamp <= (double)TransitRampTarget)
        {
            TransitRamp = TransitRampTarget;
            ExitState = 5;
            stop(0);
            return;
        }
    }
    else
    {
        // 062c1bf5-062c1c0f: project onto the current leg; the pose lands in LanePose.
        ProjectOntoSegment(EntranceRingXform, ExitRingXform, &Progress, &LanePose);
    }

    // 062c1c14-062c1c9b: the exit ring reports damage (get_status health != 1.0f, NaN
    // included: TEST AH,0x44; JNP) and the traveller is half way (Progress >= 0.5, C0 clear) ->
    // a player gets IObjAffect slot 27 (1, 0); state 4 with bExitFlag; done for this tick.
    if (ExitRingXform.Ring != nullptr)
    {
        float Health = 1.0f;
        ExitRingXform.Ring->get_status(Health);

        if (Health != 1.0f && (double)Progress >= 0.5)
        {
            if (TradelaneObj()->is_player())
            {
                // IObjAffect slot 27, unnamed (see common_iface.h).
                TradelaneObj()->slot_6c(1, 0);
            }

            bExitFlag = 1;
            ExitState = 4;
            return;
        }
    }

    // 062c1ca6-062c1cb9: states 1-3 only.
    if (ExitState == 1 || ExitState == 2 || ExitState == 3)
    {
        // 062c1cbf-062c1cce: FCOMP 1.0f; TEST AH,0x1; JNZ -- the hop needs Progress >= 1.0
        // (less or unordered skips).
        if (Progress >= 1.0f)
        {
            // 062c1cd4-062c1d03: the object database of the traveller's CSimple (+0xac). When
            // cobject() is not a CSimple retail reads +0xac of 0.
            CObject const* const Resolved = TradelaneObj()->cobject();
            CSimple const* const Simple =
                (Resolved != nullptr && (Resolved->ObjectClass & CObject::ClassSimple) == CObject::ClassSimple)
                    ? (CSimple const*)Resolved
                    : nullptr;
            IObjDB* const ObjDb = Simple->ObjectDb;

            if (ObjDb != nullptr)
            {
                // 062c1d09-062c1d3b: the ring after the exit ring; the exit frame becomes the
                // entrance frame whatever the answer.
                unsigned NextRingId = 0;
                ExitRingXform.Ring->traverse_rings(NextRingId, bHpLeftLane != 0);
                EntranceRingXform = ExitRingXform;

                if (NextRingId != 0)
                {
                    // 062c1d3f-062c1d52: IObjDB slot 1 for the ring object, then the new exit
                    // frame.
                    BuildRingTransform(ObjDb->FindById(&NextRingId), ExitRingXform);

                    // 062c1d57-062c1d66: Progress = fmod(Progress, 1.0) (_CIfmod, DAT_06399410).
                    Progress = (float)fmod((double)Progress, 1.0);

                    // 062c1d6a-062c1da7: unless already on the last leg, a ring with nothing
                    // after it makes this the last leg.
                    if (ExitState != 3)
                    {
                        unsigned AfterNextId = NextRingId;
                        ExitRingXform.Ring->traverse_rings(AfterNextId, bHpLeftLane != 0);
                        if (AfterNextId == 0)
                            ExitState = 3;
                    }
                }
                else
                {
                    // 062c1da9-062c1db0: no ring left.
                    ExitRingXform.Ring = nullptr;
                    ExitState = 4;
                }
            }
        }

        // 062c1dba-062c1e78: a formation leader other than the traveller gives the formation
        // offset; otherwise a non-player with a follow leader gives the follow offset.
        IObjRW* FormationLeader = nullptr;
        TradelaneObj()->get_formation_leader(FormationLeader);

        bool bHaveOffset = false;
        Vector Offset{ 0.0f, 0.0f, 0.0f };
        if (FormationLeader != nullptr && FormationLeader != TradelaneObj())
        {
            TradelaneObj()->get_formation_offset(Offset);
            bHaveOffset = true;
        }
        else if (!TradelaneObj()->is_player())
        {
            IObjRW* FollowLeader = nullptr;
            TradelaneObj()->get_follow_leader(FollowLeader);
            if (FollowLeader != nullptr)
            {
                TradelaneObj()->get_follow_offset(Offset);
                bHaveOffset = true;
            }
        }

        if (bHaveOffset)
        {
            // 062c1e7e-062c1ed8: the offset's z is zeroed, rotated by the lane pose (math engine
            // slot 0x30, no null test) and added to its translation.
            Offset.z = 0.0f;
            Vector Rotated{ 0.0f, 0.0f, 0.0f };
            Acquire3DMathEngine()->Rotate((FVector&)Rotated, (FTransform const&)LanePose, (FVector const&)Offset);
            LanePose.Translation.x = Rotated.x + LanePose.Translation.x;
            LanePose.Translation.y = Rotated.y + LanePose.Translation.y;
            LanePose.Translation.z = Rotated.z + LanePose.Translation.z;
        }

        // 062c1ede-062c2031: multiplayer only -- a formation leader (not the traveller) that has
        // left the lane and is no more than 500 behind along the traveller's backward axis
        // starts the leaving ramp.
        if (!SinglePlayer() && FormationLeader != nullptr && FormationLeader != TradelaneObj())
        {
            // 062c1f0d-062c1f25: the answer is pre-seeded true.
            bool bLeaderInLane = true;
            FormationLeader->is_using_tradelane(&bLeaderInLane);

            if (!bLeaderInLane)
            {
                // 062c1f2b-062c1f7e: the snapshot rotation's third column, negated.
                Vector Backward{ -Snapshot.Rotation.E[0][2], -Snapshot.Rotation.E[1][2], -Snapshot.Rotation.E[2][2] };
                Vector const& LeaderPos = FormationLeader->get_position();   // 062c1f82, slot 0

                // 062c1fa2-062c1fcb: FUN_06347e60 length ((x*x + y*y) + z*z, unrounded), 1.0f
                // over it on the FPU, each component rounded.
                double const InvLen = 1.0 / sqrt(((double)Backward.x * Backward.x + (double)Backward.y * Backward.y)
                                                 + (double)Backward.z * Backward.z);
                Backward.x = (float)(Backward.x * InvLen);
                Backward.y = (float)(Backward.y * InvLen);
                Backward.z = (float)(Backward.z * InvLen);

                // 062c1fcd-062c200c: (Tx*Bx + Bz*Tz) + By*Ty minus (By*Ly + Bz*Lz) + Bx*Lx,
                // unrounded.
                double const LeaderAlong = ((double)Backward.y * LeaderPos.y + (double)Backward.z * LeaderPos.z)
                                           + (double)Backward.x * LeaderPos.x;
                double const SelfAlong = ((double)Snapshot.Translation.x * Backward.x
                                          + (double)Backward.z * Snapshot.Translation.z)
                                         + (double)Backward.y * Snapshot.Translation.y;

                // 062c200e-062c201b: FCOMP double -500 (DAT_0639f450); TEST AH,0x41; JNZ skips
                // not-greater and unordered.
                if (SelfAlong + -LeaderAlong > -500.0)
                {
                    ExitState = 4;
                    return;
                }
            }
        }
    }

    // 062c2034-062c2094: the speed schedule.
    if (ExitState == 1)
    {
        // 062c2067-062c2088: TransitRamp += Dt * 0.125 (double DAT_0639f440), stored rounded;
        // the unrounded sum against 1.0f, TEST AH,0x1; JNZ (less or unordered) stays in state 1.
        double const Ramped = (double)Dt * 0.125 + TransitRamp;
        TransitRamp = (float)Ramped;
        if (Ramped >= 1.0)
        {
            // Falls into the state-2 store (062c2094).
            ExitState = 2;
            TransitRamp = 1.0f;
        }
    }
    else if (ExitState == 2)
    {
        TransitRamp = 1.0f;   // 062c2094
    }
    else if (ExitState == 3)
    {
        // 062c2043-062c205f: 1.0f - (1.0f - 400.0 / 2500.0f) * Progress, on the FPU
        // (DAT_0639f448 double 400, DAT_0639f3cc 2500.0f).
        TransitRamp = (float)(1.0 - (1.0 - 400.0 / 2500.0) * Progress);
    }

    // 062c209e-062c2135: the lane speed (2500 * TransitRamp, rounded) times Dt, unrounded,
    // along the lane pose's backward axis. x and y are rounded before they are added; z is
    // added unrounded to both positions.
    float const LaneSpeed = 2500.0f * TransitRamp;
    double const Advance = (double)LaneSpeed * Dt;
    float const Dx = (float)(-LanePose.Rotation.E[0][2] * Advance);
    float const Dy = (float)(-LanePose.Rotation.E[1][2] * Advance);
    double const Dz = -LanePose.Rotation.E[2][2] * Advance;

    LanePose.Translation.x = Dx + LanePose.Translation.x;
    LanePose.Translation.y = Dy + LanePose.Translation.y;
    LanePose.Translation.z = (float)(Dz + LanePose.Translation.z);
    CapturedShipPos.x = Dx + CapturedShipPos.x;
    CapturedShipPos.y = Dy + CapturedShipPos.y;
    CapturedShipPos.z = (float)(Dz + CapturedShipPos.z);

    // 062c213b-062c215b: state 1 and TransitElapsed < 3.0 (double DAT_0639f438; TEST AH,0x5;
    // JP skips greater, equal and unordered) -> blend from the captured pose.
    if (ExitState == 1 && TransitElapsed < 3.0)
    {
        I3DMathEngine* const Math = Acquire3DMathEngine();

        // 062c2174-062c217d: the lane rotation as a quaternion (slot 0x50).
        Quaternion LaneQuat;
        Math->MatrixToQuaternion((FQuaternion&)LaneQuat, (FMatrix const&)LanePose.Rotation);

        // 062c2180-062c2193: Blend = TransitElapsed * (1/3) (double DAT_0639e0e8), rounded.
        float const Blend = (float)((double)TransitElapsed * (1.0 / 3.0));

        // 062c21a3-062c21bc: QuatNlerp(Dst, CapturedShipOrient, LaneQuat, Blend) (slot 0x58).
        Quaternion Blended;
        Math->QuatNlerp((FQuaternion&)Blended, (FQuaternion const&)CapturedShipOrient,
                        (FQuaternion const&)LaneQuat, Blend);

        // 062c21f2-062c2219: back to a matrix (slot 0x54), copied into the lane rotation.
        Matrix BlendedRotation;
        Math->QuaternionToMatrix((FMatrix&)BlendedRotation, (FQuaternion const&)Blended);
        LanePose.Rotation = BlendedRotation;

        // 062c221b-062c2290: Translation = (1 - Blend) * Captured + Blend * Translation. x keeps
        // both products unrounded; y rounds the captured product; z rounds both.
        double const Keep = 1.0 - Blend;
        double const LaneX = (double)Blend * LanePose.Translation.x;
        double const LaneY = (double)Blend * LanePose.Translation.y;
        float const LaneZ = (float)((double)Blend * LanePose.Translation.z);
        float const KeepY = (float)(Keep * CapturedShipPos.y);
        float const KeepZ = (float)(Keep * CapturedShipPos.z);
        LanePose.Translation.x = (float)(Keep * CapturedShipPos.x + LaneX);
        LanePose.Translation.y = (float)(KeepY + LaneY);
        LanePose.Translation.z = KeepZ + LaneZ;
    }

    // 062c2293-062c22c5: a ship gets the lane speed (CShip::set_tradelane_speed).
    CObject* const Committed = (CObject*)TradelaneObj()->cobject();
    if (Committed != nullptr && (Committed->ObjectClass & CObject::ClassShip) == CObject::ClassShip)
    {
        ((CShip*)Committed)->set_tradelane_speed(LaneSpeed);
    }

    // 062c22ca-062c22e8: cobject() again, no null test, beam_object(Translation, Rotation, true)
    // (CObject slot 0x7c).
    ((CObject*)TradelaneObj()->cobject())->beam_object(LanePose.Translation, LanePose.Rotation, true);
}
