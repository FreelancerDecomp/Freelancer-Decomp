// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include <cmath>
#include "common_physys.h"

#include "common_archetype.h"
#include "common_cobject.h"
#include "common_cobjdb.h"   // IObjDB::GetZoneDb (PhySys::GenerateCollisions)
#include "common_id.h"
#include "common_universe.h" // Universe::ISystem (PhySys::GenerateCollisions)
#include "common_net.h"  // IsMPServer -- CRemotePhysicsSimulation::tracef (.text:06291be6)
#include "common_phyarch.h"
#include "common_misc.h"     // SinglePlayer
#include "common_server.h"

#include "dacom.h"
#include "DALib.h"
#include "x86math.h"  // I3DMathEngine::MatrixToQuaternion, FQuaternion, FMatrix
#include "../IVP/ivp_collision/ivp_collision.h" // IVP_Collision::get_objects (PhyCollisionStateManager)
#include "../IVP/ivp_physics/ivp_core.h"
#include "../IVP/ivp_physics/ivp_real_object.h"
#include "../IVP/ivp_physics/ivp_environment.h"
#include "../IVP/ivp_physics/ivp_controller.h"
#include "../IVP/ivp_physics/ivp_material.h"
#include "../IVP/ivp_physics/ivp_listener.h"
#include "../IVP/ivp_physics/ivp_mindist_settings.h"
#include "../IVP/ivp_physics/ivp_mindist.h" // IVP_Synapse_Real (PhySys_CalcClosestPointOnSynapse)
#include "common_csys.h"  // Acquire3DMathEngine
// PhySys::CalcClosestPointOnSynapse (FUN_0628b7a0, near GetClosestPhantomIntruderPos
// below) walks retail's compact half-edge mesh directly -- IVP_Compact_Edge /
// IVP_Compact_Triangle / IVP_Compact_Ledge and IVP_U_Float_Point.
#include "../IVP/ivp_compact_builder/ivp_compact_ledge.h"
#include "../IVP/ivp_compact_builder/ivp_surface_builder_pointsoup.h" // Cleanup, the box phantom's hull
#include "../IVP/ivp_compact_builder/ivp_surface_builder_ledge_soup.h" // the box phantom's surface
#include "../IVP/ivp_physics/ivp_surman_polygon.h" // PhySys::PhySurfaceManager's base
#include "../IVP/ivp_collision/ivp_ray_solver.h"   // PhySys::RaySolver_MaskedSurface's base
#include "../IVP/ivp_collision/ivp_sphere_query.h" // PhysicalSystem::FindSphereCollisions (06350740)

#include <assert.h>
#include <float.h>   // FLT_MAX (PhySys::GetClosestPhantomIntruderPos, DAT_0639c39c)
#include <math.h>
#include <new>
#include <stdarg.h>  // va_list -- CRemotePhysicsSimulation::tracef is __cdecl varargs
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

#include <mmsystem.h>  // timeGetTime (PhysicalSystem::Update, .text:0628ca84); winmm is linked by common_playeraccount.cpp


// Win32 macro conflict: PhySys::GetCurrentTime vs windows.h GetCurrentTime()
#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

#include "common_physys_internal.h"

using namespace CmnPhySys;

// ---- Collision globals (defined here, declared in common_goods.h) ----
extern float COLLISION_DAMAGE_FACTOR;

namespace CmnPhySys
{
    // Retail DAT_063fc0bc: the game's single IVP material. PhySys::Startup creates it once
    // (operator_new(0x1c) + FUN_0634bec0, .text:06289537..0628957c); PhySys::Shutdown deletes it
    // when the world map is empty (.text:06289998..062899a9). Read as the construction
    // template's default material by FUN_0628c7b0 / FUN_0628c860 / PhySys::DetachPhantom.
    IVP_Material* G_PhySysMaterial = nullptr;
}

namespace
{
    // ---- Collision damage table ----
    // Retail: a std::map<unsigned, float> object at .data:DAT_063fcc58 (+4 head node
    // DAT_063fcc5c, +8 nil DAT_063fcc60, +0xc multi flag DAT_063fcc64 = 0, +0x10 size
    // DAT_063fcc68), constructed by the static initializer LAB_06302840 (FUN_06302ce0) with
    // the atexit destructor LAB_06302870. Key = DecodeType(name), value = damage (node +0x10).
    std::map<unsigned, float> G_CollisionDamage;
    // .data:DAT_063ed178, initial 0x437a0000 = 250.0f; written only by Collision::Load.
    float G_CollisionMaxImpactSpeed = 250.0f;

    // ---- Default Part: retail .data:DAT_063fc02c (surface DAT_063fc030), see GetDefaultPart ----
    PhyArch::Part G_DefaultPartStorage;

    // DecodeType lookup is the global ::DecodeType export (see Collision::Load below) --
    // it is a fixed name table, not a hash, so it lives in common_archetype.cpp/.h.
}

// ============================================================================
// PhySys constants — defined in common_constants.cpp ReadConstants, declared extern above.
// BULLET_LENGTH_FUDGE is new for this batch.
// ============================================================================
namespace PhySys
{
    float BULLET_LENGTH_FUDGE = 0.0f;
    FCollisionFilter m_pCollisionFilter = nullptr;

    // PhySys constants — declared in common_physys.h, defined here. Retail defaults
    // Scalars come from the raw .data bytes (0x063eb364-0x063eb38c); the vectors
    // are seeded by startup initializers LAB_062908d0/LAB_062908f0/LAB_06290910.
    float DEFAULT_LINEAR_DAMPING = 1.0f;               // .data:063eb360 = 0000803f
    Vector DEFAULT_ANGULAR_DAMPING = {2.0f, 2.0f, 2.0f}; // LAB_062908d0: XYZ <- 0x40000000
    float GOLEM_CHILD_MASS = 100000.0f;
    float GOLEM_CHILD_LINEAR_DAMP = 1.0f;
    Vector GOLEM_CHILD_ANGULAR_DAMP = {1.0f, 1.0f, 1.0f}; // LAB_062908f0: XYZ <- 0x3f800000
    float GOLEM_MAX_DELTA_POSITION = 10.0f;
    float GOLEM_DELTA_ORIENTATION = 1.5707964f;
    Vector GOLEM_MAX_TRANSLATION_FORCE = {1.0e12f, 1.0e12f, 1.0e12f}; // LAB_06290910: XYZ <- 0x5368d4a5
    float GOLEM_MAX_TORQUE = 1.0E+12f;
    float GOLEM_FORCE_FACTOR = 0.8f;
    float GOLEM_DAMP_FACTOR = 1.0f;
    float GOLEM_TORQUE_FACTOR = 0.8f;
    float GOLEM_ANGULAR_DAMP_FACTOR = 1.0f;
    float RC_MAX_DELTA_POSITION = 50.0f;
    float RC_MAX_DELTA_ORIENTATION = 5.0f;
}

namespace CmnPhySys
{
    // .data:DAT_063fc098: the world map (VC6 _Tree object: head DAT_063fc09c, size
    // DAT_063fc0a8 -- PhySys::AnyActive tests the size).
    std::map<unsigned, PhysicalSystem> G_PhysicalSystems;
}

namespace
{
    // The engine's per-instance joint block (IEngine::GetInstanceRelative, +0xc4) as
    // FUN_062883c0 reads it. common_cattachment.cpp's file-local FRevoluteJoint is the same block.
    struct FPhySysJointBlock
    {
        unsigned long Type;  // +0x00  0, 1 or 2 (anything else: the pose is left as the parent's)
        FVector ParentPoint; // +0x04
        FVector ChildPoint;  // +0x10
        FMatrix Orient;      // +0x1c
        FVector Axis;        // +0x40
    };
    static_assert(offsetof(FPhySysJointBlock, ParentPoint) == 0x04, "joint block +0x04");
    static_assert(offsetof(FPhySysJointBlock, ChildPoint) == 0x10, "joint block +0x10");
    static_assert(offsetof(FPhySysJointBlock, Orient) == 0x1c, "joint block +0x1c");
    static_assert(offsetof(FPhySysJointBlock, Axis) == 0x40, "joint block +0x40");
    static_assert(sizeof(FTransform) == 0x30, "the engine instance transform is 12 floats");

    // FUN_06288860 (thiscall on M, RET 4): M = M * B through the math engine (+0x20), the
    // product copied back (REP MOVSD 9).
    void PhySys_MatrixMulInPlace(FMatrix& M, FMatrix const& B)
    {
        FMatrix Product;
        Acquire3DMathEngine()->MulMatrix(Product, M, B);
        M = Product;
    }

    // FUN_06288900 (cdecl): Out = A * B (+0x20).
    FMatrix PhySys_MatrixMul(FMatrix const& A, FMatrix const& B)
    {
        FMatrix Product;
        Acquire3DMathEngine()->MulMatrix(Product, A, B);
        return Product;
    }

    // FUN_062888b0 (cdecl): Out = M * V (TransformMatrix, +0x2c).
    FVector PhySys_MatrixTransform(FMatrix const& M, FVector const& V)
    {
        FVector Out;
        Acquire3DMathEngine()->TransformMatrix(Out, M, V);
        return Out;
    }
}

namespace CmnPhySys
{
    // FUN_062883c0 (.text:062883c0..06288797, cdecl): re-pose every child instance of Instance
    // whose user data is Object from the child's joint block and joint parameter, then recurse
    // into it. Every vector helper (FUN_062887a0 / 062887d0 / 06288800 / 06288830 /
    // 0628b030) is one x87 op per component with a FSTP float, i.e. plain float arithmetic.
    void PhySys_UpdateJointedChildren(long Instance, CObject* Object)
    {
        // .text:062883c8..062883d2
        if (Instance == -1)
            return;

        IEngine* const Engine = DALib::Engine;

        // .text:062883d8..062883f3: the parent's world transform (IEngine +0xa0; engbase.h
        // types the result float*, it is the instance's 12-float FTransform).
        FTransform const* const ParentTransform =
            (FTransform const*)Engine->GetInstanceTransform(PhySys_InstanceHandle(Instance));

        // .text:062883e9..06288787: children through IEngine +0xd8 (flags 1), -1 ends.
        for (long Child = PhySys_RelatedInstance(Instance, 1, -1); Child != -1;
             Child = PhySys_RelatedInstance(Instance, 1, Child))
        {
            // .text:06288410..06288429: only children that belong to Object.
            if (PhySys_InstanceObject(Child) != Object)
                continue;

            // .text:0628842f..0628843b: IEngine +0xc4 once with the result unused.
            Engine->GetInstanceRelative(PhySys_InstanceHandle(Child));

            // .text:06288441..06288474: start from the parent's transform; keep its rotation.
            // IEngine +0xc4 hands back the child's joint block (typed unsigned long* there).
            FTransform Local = *ParentTransform;
            FPhySysJointBlock const* const Joint =
                (FPhySysJointBlock const*)Engine->GetInstanceRelative(PhySys_InstanceHandle(Child));
            FMatrix const ParentRotation = Local.Rotation;

            // .text:06288476..0628848a: SUB 0 / DEC / DEC on the joint type.
            if (Joint->Type == 0)
            {
                // .text:062886c5..062886fa: R = R * Orient; T += ParentR * ChildPoint.
                PhySys_MatrixMulInPlace(Local.Rotation, Joint->Orient);
                FVector const Offset = PhySys_MatrixTransform(ParentRotation, Joint->ChildPoint);
                Local.Translation.X = Local.Translation.X + Offset.X;
                Local.Translation.Y = Local.Translation.Y + Offset.Y;
                Local.Translation.Z = Local.Translation.Z + Offset.Z;
            }
            else if (Joint->Type == 1)
            {
                // .text:062885c7..062885d9: the joint parameter (IEngine +0xcc, index 0).
                float Parameter;
                Engine->GetJointParams(PhySys_InstanceHandle(Child), 0, &Parameter);

                // .text:062885df..06288600: R = R * Orient; ChildOffset = R * ChildPoint.
                PhySys_MatrixMulInPlace(Local.Rotation, Joint->Orient);
                FVector const ChildOffset = PhySys_MatrixTransform(Local.Rotation, Joint->ChildPoint);

                // .text:06288602..06288631: Slid = ParentPoint + Axis * Parameter, in two
                // helper calls: FUN_06288830 stores each product as a float (FMUL; FSTP
                // float), then FUN_062887d0 adds ParentPoint to the stored floats.
                FVector Scaled;
                Scaled.X = Joint->Axis.X * Parameter;
                Scaled.Y = Joint->Axis.Y * Parameter;
                Scaled.Z = Joint->Axis.Z * Parameter;
                FVector Slid;
                Slid.X = Joint->ParentPoint.X + Scaled.X;
                Slid.Y = Joint->ParentPoint.Y + Scaled.Y;
                Slid.Z = Joint->ParentPoint.Z + Scaled.Z;

                // .text:06288636..062886bf: T += (ParentR * Slid) - ChildOffset (the difference is
                // stored as floats first, FUN_0628b030).
                FVector const ParentOffset = PhySys_MatrixTransform(ParentRotation, Slid);
                FVector Delta;
                Delta.X = ParentOffset.X - ChildOffset.X;
                Delta.Y = ParentOffset.Y - ChildOffset.Y;
                Delta.Z = ParentOffset.Z - ChildOffset.Z;
                Local.Translation.X = Local.Translation.X + Delta.X;
                Local.Translation.Y = Local.Translation.Y + Delta.Y;
                Local.Translation.Z = Local.Translation.Z + Delta.Z;
            }
            else if (Joint->Type == 2)
            {
                // .text:06288490..062884a3: the joint angle (IEngine +0xcc, index 0).
                float Angle;
                Engine->GetJointParams(PhySys_InstanceHandle(Child), 0, &Angle);

                // .text:062884a9..062884e0: half = angle * 0.5f (DAT_0639c13c); FSIN, FSTP float;
                // divided (FDIVR) by the axis length FUN_06347e60 (sqrt(x*x + y*y + z*z) on the
                // x87 stack), FSTP float; FCOS of the half angle, FSTP float.
                float const HalfAngle = Angle * 0.5f;
                float const SinHalf = static_cast<float>(std::sin(static_cast<double>(HalfAngle)));
                double const AxisLength = sqrt((double)(Joint->Axis.X) * Joint->Axis.X +
                                               (double)(Joint->Axis.Y) * Joint->Axis.Y +
                                               (double)(Joint->Axis.Z) * Joint->Axis.Z);
                float const AxisScale = (float)((double)(SinHalf) / AxisLength);

                // .text:062884e7..06288516: the rotation quaternion (W = cos, XYZ = Axis * scale).
                FQuaternion Rotation;
                Rotation.W = static_cast<float>(std::cos(static_cast<double>(HalfAngle)));
                Rotation.X = Joint->Axis.X * AxisScale;
                Rotation.Y = Joint->Axis.Y * AxisScale;
                Rotation.Z = Joint->Axis.Z * AxisScale;

                // .text:06288505..06288567: JointR = matrix(quat) (+0x54); R = R * (JointR * Orient).
                FMatrix JointRotation;
                Acquire3DMathEngine()->QuaternionToMatrix(JointRotation, Rotation);
                PhySys_MatrixMulInPlace(Local.Rotation, PhySys_MatrixMul(JointRotation, Joint->Orient));

                // .text:0628856c..062885c2: T += (ParentR * ParentPoint) - (R * ChildPoint).
                FVector const ChildOffset = PhySys_MatrixTransform(Local.Rotation, Joint->ChildPoint);
                FVector const ParentOffset = PhySys_MatrixTransform(ParentRotation, Joint->ParentPoint);
                FVector Delta;
                Delta.X = ParentOffset.X - ChildOffset.X;
                Delta.Y = ParentOffset.Y - ChildOffset.Y;
                Delta.Z = ParentOffset.Z - ChildOffset.Z;
                Local.Translation.X = Local.Translation.X + Delta.X;
                Local.Translation.Y = Local.Translation.Y + Delta.Y;
                Local.Translation.Z = Local.Translation.Z + Delta.Z;
            }

            // .text:062886ff..06288715: IEngine +0x9c with the transform.
            Engine->SetInstanceMatrix(PhySys_InstanceHandle(Child), &Local);

            // .text:06288715..0628874f: read the instance extent (flags 1) and write it back.
            float Extent;
            FVector ExtentVector;
            Engine->GetInstanceExtent(PhySys_InstanceHandle(Child), 1, &Extent, &ExtentVector.X);
            Engine->SetInstanceExtent(PhySys_InstanceHandle(Child), 1, Extent, &ExtentVector.X);

            // .text:06288755..0628875e: recurse into the child.
            PhySys_UpdateJointedChildren(Child, Object);
        }
    }
}

// ============================================================================
// Collision namespace
// ============================================================================

// 0x06302940 (.text:06302940-06302a93). Not [CollisionConsts] -> false. Otherwise, for each
// value: "damage" -> TypeId = DecodeType(string 0), Amount = float 1 (read before the TypeId
// test); a non-zero TypeId is inserted with std::map::insert semantics (the inlined _Tree
// insert at 063029a9-06302a4c with _Multi = DAT_063fcc64 = 0: an existing key keeps its first
// value). Else "max_impact_speed" -> DAT_063ed178 = float 0. Returns true.
bool __cdecl Collision::Load(INI_Reader& Reader)
{
    if (!Reader.is_header("CollisionConsts"))
        return false;

    while (Reader.read_value())
    {
        if (Reader.is_value("damage"))
        {
            // DecodeType: the fixed OBJECT_TYPE name -> bit table (common_archetype.h).
            unsigned TypeId = DecodeType(Reader.get_value_string(0));
            float Amount = Reader.get_value_float(1);
            if (TypeId != 0)
            {
                G_CollisionDamage.insert(std::pair<const unsigned, float>(TypeId, Amount));
            }
        }
        else if (Reader.is_value("max_impact_speed"))
        {
            G_CollisionMaxImpactSpeed = Reader.get_value_float(0);
        }
    }
    return true;
}

// 0x06302aa0 (.text:06302aa0-06302b49): erase(begin(), end()) on the damage map, which the
// _Tree code turns into a whole-tree clear. max_impact_speed is not touched.
void __cdecl Collision::Free()
{
    G_CollisionDamage.clear();
}

// 0x06302b50 (.text:06302b50-06302b9d): lower_bound (FUN_062fcfb0); end, or key < node key,
// -> miss. Hit -> node value (+0x10); miss -> DAT_06399408 (0.0f).
float __cdecl Collision::GetImpactDamageFromType(unsigned TypeId)
{
    std::map<unsigned, float>::const_iterator Found = G_CollisionDamage.find(TypeId);
    if (Found != G_CollisionDamage.end())
    {
        return Found->second;
    }
    return 0.0f;
}

// 0x06302ba0 (.text:06302ba0-06302ca2), x87 evaluation reproduced in double:
//   Len = fsqrt((x*x + y*y) + z*z) of Normal, kept unrounded;
//   FCOM 0.0f; TEST AH,0x41; JNZ -> return 0.0f (taken for Len <= 0 and for unordered);
//   Inv = 1.0f / Len;
//   AbsDot = (float)|((Nz*Inv)*Rv.z + (Ny*Inv)*Rv.y) + (Nx*Inv)*Rv.x|   (FSTP float);
//   Ratio  = (float)(Len / max_impact_speed)                           (FSTP float);
//   map lookup as GetImpactDamageFromType (FUN_062fd030 lower_bound): hit ->
//   Damage * (Ratio * AbsDot), miss -> 0.0f * (Ratio * AbsDot).
float __cdecl Collision::CalcCollisionDamage(unsigned TypeId, Vector const& RelVel,
                                              Vector const& Normal)
{
    const double NormalX = (double)(Normal.x);
    const double NormalY = (double)(Normal.y);
    const double NormalZ = (double)(Normal.z);
    const double Len = sqrt((NormalX * NormalX + NormalY * NormalY) + NormalZ * NormalZ);
    if (!(Len > 0.0))
    {
        return 0.0f;
    }

    const double Inv = 1.0 / Len;
    const double Dot = ((NormalZ * Inv) * (double)(RelVel.z) +
                        (NormalY * Inv) * (double)(RelVel.y)) +
                       (NormalX * Inv) * (double)(RelVel.x);
    const float AbsDot = (float)(fabs(Dot));
    const float Ratio = (float)(Len / (double)(G_CollisionMaxImpactSpeed));

    double Damage = 0.0;
    std::map<unsigned, float>::const_iterator Found = G_CollisionDamage.find(TypeId);
    if (Found != G_CollisionDamage.end())
    {
        Damage = (double)(Found->second);
    }
    return (float)(Damage * ((double)(Ratio) * (double)(AbsDot)));
}

// 0x06302cb0 (.text:06302cb0-06302cdc): Scaled = COLLISION_DAMAGE_FACTOR * DamageFactor (kept
// on the x87 stack); FLD MaxSpeed; FCOMP ActualSpeed; TEST AH,0x41; JNZ is taken for
// MaxSpeed <= ActualSpeed and for unordered -> Scaled * 1.0 (double DAT_06399410); the
// fall-through (MaxSpeed > ActualSpeed) -> Scaled * (ActualSpeed / MaxSpeed).
float __cdecl Collision::CalcCollisionDamage(float DamageFactor, float MaxSpeed, float ActualSpeed)
{
    const double Scaled = (double)(COLLISION_DAMAGE_FACTOR) * (double)(DamageFactor);
    if (MaxSpeed > ActualSpeed)
    {
        return (float)(Scaled * ((double)(ActualSpeed) / (double)(MaxSpeed)));
    }
    return (float)(Scaled * 1.0);
}

// ============================================================================
// CollisionGroupDesc operators (already declared in common_cobject.h)
// Implementations here since they belong to the PHYS batch.
// ============================================================================

// Note: CollisionGroupDesc is defined in common_cobject.h. These operators
// are listed under the CollisionGroupDesc class key in the Common.dll export table.
// They compare by Id field only (ushort at +0x00).

// These are already implicitly handled by the struct definition.
// Retail implementations at 0xc550/0xc570/0xc590/0xc5b0:
//   operator<: this->Id < other.Id
//   operator>: other.Id < this->Id
//   operator==: this->Id == other.Id
//   operator!=: this->Id != other.Id
// operator= is ICF-folded with option_spec::operator= @ 0x20ee0 (trivial memcpy 8 bytes).

// ============================================================================
// PhyArch additions
// ============================================================================

// .text:062802d0..06280532. The default part (.data:DAT_063fc02c, constructed by the static
// initializer at .text:062802a0) gets its surface on the first call: the test is the surface
// pointer itself (DAT_063fc030), there is no separate "initialised" flag.
PhyArch::Part const* __cdecl PhyArch::GetDefaultPart()
{
    if (G_DefaultPartStorage.Surface == nullptr)
    {
        // .text:062802fb..06280315: a ledge soup (FUN_06369e70) and an empty point vector.
        IVP_SurfaceBuilder_Ledge_Soup Soup;
        IVP_U_Vector<IVP_U_Float_Point> Points;

        // .text:06280325..06280446: five points appended in this order -- the four corners of
        // the z = +0.5 face and the apex (0, 0, -0.5). Their fourth floats are never written
        // in retail (stack garbage); the port stores 0 (the pointsoup builder reads x / y / z).
        IVP_U_Float_Point Pyramid[5] = {
            { -0.5f, -0.5f,  0.5f, 0.0f },   // +0x28 local_f0
            {  0.5f, -0.5f,  0.5f, 0.0f },   // +0x38 local_e0
            {  0.5f,  0.5f,  0.5f, 0.0f },   // +0x48 local_d0
            { -0.5f,  0.5f,  0.5f, 0.0f },   // +0x58 local_c0
            {  0.0f,  0.0f, -0.5f, 0.0f },   // +0x68 local_b0
        };
        for (int Index = 0; Index < 5; ++Index)
            Points.Add(&Pyramid[Index]);

        // .text:0628044b..06280491: hull (FUN_0636c9a0) -> soup (FUN_0636a050) -> compile with
        // {0, 1, 0, 1} (FUN_0636a0b0) -> the part's surface (+0x04).
        IVP_Compact_Ledge* const Hull = IVP_SurfaceBuilder_Pointsoup::convert_pointsoup_to_compact_ledge(&Points);
        Soup.InsertLedge(Hull);
        const int32_t CompileOptions[4] = { 0, 1, 0, 1 };
        G_DefaultPartStorage.Surface = (IVP_Compact_Surface*)Soup.Compile(CompileOptions);

        // .text:06280485..062804f3: extents (-0.5, -0.5, -0.5) .. (0.5, 0.5, 0.5) through stack
        // copies. The byte at +0x00 keeps the constructor's 1 and the hardpoint list stays
        // empty; the vector and the soup are destroyed at 062804ff / 06280513.
        G_DefaultPartStorage.ExtentsMin.x = -0.5f;
        G_DefaultPartStorage.ExtentsMin.y = -0.5f;
        G_DefaultPartStorage.ExtentsMin.z = -0.5f;
        G_DefaultPartStorage.ExtentsMax.x = 0.5f;
        G_DefaultPartStorage.ExtentsMax.y = 0.5f;
        G_DefaultPartStorage.ExtentsMax.z = 0.5f;
    }
    return &G_DefaultPartStorage;
}

