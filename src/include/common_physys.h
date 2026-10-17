// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"

// Common.dll PhySys — full PHYS batch port (Wave 2).
// Listing RVAs from the Common.dll export table; RE from EXE/Disasm/Common.dll.c.
// Retail physics is IVP (Innovative Physics); we port observable Common-layer behavior.
// Deep IVP calls (ivp_* internals) are stubbed with FULLPORT-DEFERRED[IVP].
// See docs/re/common_physys.md.

#include "common_ini.h"
#include "common_csys.h"

#include <cstddef>
#include <list>

struct CObject;
struct CSimple;
struct CBeam;

struct IObjDB;

// Quaternion for RCBegin / RCSet. Retail uses 4 floats (W,X,Y,Z) matching FQuaternion.
class Quaternion
{
public:
    float w;
    float x;
    float y;
    float z;
};

static_assert(sizeof(Quaternion) == 0x10, "Quaternion 4 floats");

namespace PhyArch
{
    struct Part;
}

// The two IVP SDK types PhySys's collision filter is spelled in terms of. The SDK is not part
// of the port and neither type is ever dereferenced here; they exist so that
// ?m_pCollisionFilter@PhySys@@3P6A?AW4IVP_BOOL@@PAVIVP_Real_Object@@0@ZA mangles the way retail
// does. IVP_BOOL is IVP's own boolean enum, IVP_Real_Object its rigid body.
enum IVP_BOOL
{
    IVP_FALSE = 0,
    IVP_TRUE = 1
};

class IVP_Real_Object;

// PhySys::Controller is an IVP_Controller_Independent in retail, so three more SDK spellings
// leak into its mangled names:
//   ?core_is_going_to_be_deleted_event@Controller@PhySys@@MAEXPAVIVP_Core@@@Z
//   ?get_controller_priority@Controller@PhySys@@MAE?AW4IVP_CONTROLLER_PRIORITY@@XZ
//   ?get_associated_controlled_cores@Controller@PhySys@@MAEPAV?$IVP_U_Vector@VIVP_Core@@@@XZ
// All three are declared, none defined -- the ABI only needs the names.
class IVP_Core;
template <class T> class IVP_U_Vector;

// More IVP classes that retail's exported PhySys names spell at global scope:
//   ?GetCurrentTime@PhySys@@YA?AVIVP_Time@@I@Z                     (defined in ivp_core.h)
//   ?mindist_entered_volume@PhyCollisionStateManager@PhySys@@UAEXPAVIVP_Controller_Phantom@@PAVIVP_Mindist_Base@@@Z
//   ?get_other_object@PhyCollisionStateManager@PhySys@@IAEPAVIVP_Real_Object@@PAVIVP_Mindist_Base@@@Z
// Only the names are needed here: IVP_Mindist_Base is in src/IVP/ivp_physics/ivp_mindist.h,
// IVP_Controller_Phantom in src/IVP/ivp_physics/ivp_phantom.h.
class IVP_Time;
class IVP_Mindist_Base;
class IVP_Controller_Phantom;
// The event every controller's do_simulation_controller receives; defined in
// common_ivp_controller.h (only the name is needed here).
class IVP_Event_Sim;

// Retail's golem force controller, CObject+0x60. RTTI-confirmed global-scope class name (see
// PhySys::GolemSetTargetPosition below for the COL trail). Defined in common_controllergolem.cpp.
class FLControllerGolem;

// IVP_Controller -- PhySys::Controller's base (retail CHD: PhySys::Controller,
// IVP_Controller_Dependent, IVP_Controller) -- and the IVP_CONTROLLER_PRIORITY enum its
// slot 5 returns (moved there 2026-09-25; the value 0x5dc is still retail's, 06285c80).
#include "common_ivp_controller.h"
// IVP_Listener_Phantom -- PhyCollisionStateManager's base (RTTI COL 063a6948:
// PhyCollisionStateManager, IVP_Listener_Phantom at 0).
#include "../IVP/ivp_physics/ivp_phantom.h"
// CheapSet<CObject*> -- PhyCollisionStateManager::get_intruding_cobjs' out set.
#include "common_cheapset.h"

namespace PhySys
{
    // ---- Constants (defined in common_constants.cpp ReadConstants) ----
    COMMON_API extern float PHYSICAL_SIM_RATE;                       // 0x18B318
    COMMON_API extern int MAX_SPAWNED_MINDIST_COUNT;                 // 0x18B31C
    COMMON_API extern float ANOM_LIMITS_MAX_VELOCITY;                // 0x18B320
    COMMON_API extern float ANOM_LIMITS_MAX_ANGULAR_VELOCITY_PER_PSI;// 0x18B324
    COMMON_API extern double MATERIAL_FRICTION;                      // 0x18B328
    COMMON_API extern double MATERIAL_ELASTICITY;                    // 0x18B330
    COMMON_API extern double MIN_TIME_BETWEEN_COLLISIONS;            // 0x18B338
    COMMON_API extern float RMGR_LOOK_AHEAD_TIME_WORLD;              // 0x18B340
    COMMON_API extern float RMGR_LOOK_AHEAD_MAX_RADIUS_WORLD;        // 0x18B344
    COMMON_API extern float RMGR_LOOK_AHEAD_MIN_DISTANCE_WORLD;      // 0x18B348
    COMMON_API extern float RMGR_LOOK_AHEAD_MAX_DISTANCE_WORLD;      // 0x18B34C
    COMMON_API extern float RMGR_LOOK_AHEAD_MIN_SECONDS_WORLD;       // 0x19C0F8
    COMMON_API extern float RMGR_LOOK_AHEAD_TIME_INTRA;              // 0x18B350
    COMMON_API extern float RMGR_LOOK_AHEAD_MAX_RADIUS_INTRA;        // 0x18B354
    COMMON_API extern float RMGR_LOOK_AHEAD_MIN_DISTANCE_INTRA;      // 0x18B358
    COMMON_API extern float RMGR_LOOK_AHEAD_MAX_DISTANCE_INTRA;      // 0x18B35C
    COMMON_API extern float RMGR_LOOK_AHEAD_MIN_SECONDS_INTRA;       // 0x19C0F4
    COMMON_API extern float DEFAULT_LINEAR_DAMPING;                  // 0x18B360
    COMMON_API extern Vector DEFAULT_ANGULAR_DAMPING;                // 0x19C0E8
    COMMON_API extern float GOLEM_CHILD_MASS;                        // 0x18B364
    COMMON_API extern float GOLEM_CHILD_LINEAR_DAMP;                 // 0x18B368
    COMMON_API extern Vector GOLEM_CHILD_ANGULAR_DAMP;               // 0x19C0DC
    COMMON_API extern float GOLEM_MAX_DELTA_POSITION;                // 0x18B36C
    COMMON_API extern float GOLEM_DELTA_ORIENTATION;                 // 0x18B370
    COMMON_API extern float GOLEM_MAX_TORQUE;                        // 0x18B374
    COMMON_API extern float GOLEM_FORCE_FACTOR;                      // 0x18B378
    COMMON_API extern float GOLEM_DAMP_FACTOR;                       // 0x18B37C
    COMMON_API extern float GOLEM_TORQUE_FACTOR;                     // 0x18B380
    COMMON_API extern float GOLEM_ANGULAR_DAMP_FACTOR;               // 0x18B384
    COMMON_API extern float RC_MAX_DELTA_POSITION;                   // 0x18B388
    COMMON_API extern float RC_MAX_DELTA_ORIENTATION;                // 0x18B38C
    COMMON_API extern Vector GOLEM_MAX_TRANSLATION_FORCE;            // 0x19C0D0
    COMMON_API extern float BULLET_LENGTH_FUDGE;                     // 0x19C0FC

    // Global collision filter function pointer (0x19C0C0).
    // ?m_pCollisionFilter@PhySys@@3P6A?AW4IVP_BOOL@@PAVIVP_Real_Object@@0@ZA. The IVP SDK is
    // not part of the port, so the two types are declared but never defined -- enough for the
    // pointer to have retail's exact type, which is all the ABI needs.
    typedef IVP_BOOL(__cdecl* FCollisionFilter)(IVP_Real_Object* A, IVP_Real_Object* B);
    COMMON_API extern FCollisionFilter m_pCollisionFilter;

    // ---- CreateParms ----
    // CShip::init_physics stack -> BuildIVP_Real. 13 dwords (Common.dll.c copy loop @ 0x28970).
    struct CreateParms
    {
        float Mass;                  // +0x00  arch Root+0x20
        float LinearDamping;         // +0x04  DEFAULT_LINEAR_DAMPING
        float AngularDamping[3];     // +0x08
        // +0x14. MISNOMER, kept only because renaming it sweeps src/Common/common_casteroid.*
        // and CAsteroid+0xf4 (which carries the same value under the same wrong name). This is
        // the STAR-SYSTEM id, not a collision group: BuildIVP_Sphere and ReMakePhysicalR copy
        // it straight onto CObject+0x50 (.text:06289471 / 0628a681, `MOV EDX,[EBP+0x14]` then
        // `MOV [EDI+0x50],EDX` right after the BodyPtr store at +0x54), and CObject+0x50 is
        // then handed to PhySys::GetCurrentTime(unsigned) at .text:0627e261 as the key into
        // the systems map DAT_063fc098 -- the same unsigned key PhySys::Startup(unsigned) and
        // Update(unsigned,float) take, compared unsigned at .text:06289a8e (`CMP EDX,[EAX+0xc]`
        // + JC). common_cobject.cpp already fills it from IObjDB's get-system accessor.
        // OPEN TODO: rename to SystemId across CreateParms, CAsteroid+0xf4 and CAsteroidCube.
        unsigned CollisionGroupId;   // +0x14
        int Type;                    // +0x18  CShip writes 2
        unsigned char bFlag1c;       // +0x1c
        unsigned char bEnableCollisions; // +0x1d  BuildIVP_Real reads this
        char Name[8];                // +0x1e  itoa(object id)
        unsigned char bHasInertia;   // +0x26  Root+0x38
        unsigned char Pad27;
        Vector Inertia;              // +0x28  Root+0x2c..+0x34
    };

    static_assert(sizeof(CreateParms) == 0x34, "PhySys::CreateParms 13 dwords");
    static_assert(offsetof(CreateParms, bEnableCollisions) == 0x1d, "CreateParms+0x1d");
    static_assert(offsetof(CreateParms, bHasInertia) == 0x26, "CreateParms+0x26");
    static_assert(offsetof(CreateParms, Inertia) == 0x28, "CreateParms+0x28");

    // ---- DetachParms ----
    // Detach (.text:06284910) passes Parms through unchanged to FUN_062847e0 (0x0628496b).
    // CORRECTED 2026-09-25 (exe-w3a): the block is 0x2c bytes, not an empty placeholder. Both
    // retail builders fill the same fields:
    //   Common FUN_06284690 (.text:06284732..06284784, the per-child default parms) and
    //   freelancer.exe FUN_00536c20 (.text:00536d93..00536dfd, the explosion's piece detach):
    //   +0x00 DEFAULT_LINEAR_DAMPING; +0x04 DEFAULT_ANGULAR_DAMPING; +0x10 byte 0; +0x18 1.0f
    //   (the exe then overwrites it with radius * 200); +0x1c byte 0; +0x20 Vector 0.
    // The consumer FUN_062847e0 reads them back: the byte at +0x10 is a NUL-terminated name
    // (a non-zero first byte -> FUN_06348720(&Parms+0x10), 0x0628481f..06284832, the same
    // helper CreatePhantom calls on PhantomParms+0x06); +0x18 (06284837); the +0x1c flag
    // gating +0x20..+0x28 (0628483e..06284846); +0x00..+0x0c (06284882..062848a8). The same
    // tail as CreateParms (+0x1e name, +0x26 flag, +0x28 Vector), so the names follow it.
    // The bytes +0x11..+0x17 and +0x1d..+0x1f are never written by either builder.
    struct DetachParms
    {
        float LinearDamping;         // +0x00
        Vector AngularDamping;       // +0x04
        char Name[8];                // +0x10  NUL-terminated; both builders store only [0] = 0
        float Mass;                  // +0x18
        unsigned char bHasInertia;   // +0x1c
        unsigned char Pad1d[3];
        Vector Inertia;              // +0x20
    };
    static_assert(sizeof(DetachParms) == 0x2c, "PhySys::DetachParms 0x2c");
    static_assert(offsetof(DetachParms, Name) == 0x10, "DetachParms+0x10");
    static_assert(offsetof(DetachParms, Mass) == 0x18, "DetachParms+0x18");
    static_assert(offsetof(DetachParms, bHasInertia) == 0x1c, "DetachParms+0x1c");
    static_assert(offsetof(DetachParms, Inertia) == 0x20, "DetachParms+0x20");

    // ---- CObjectCB ----
    // The phantom intruder callback that PhantomParms+0x40 carries. RTTI ".?AUCObjectCB@PhySys@@"
    // (Common .data:063eb71c, Server .data:06d8878c): a base class of Server's
    // SrvAsteroid::MedMine (its COL .rdata:06d6cf3c puts the sub-object at +0x60) and of
    // Common's CEShieldGenerator (.rdata:063a7d74) and SphereIntruderChecker (base-class
    // descriptor .rdata:063aa90c in its array 063aa93c).
    // CreatePhantom stores the pointer at phantom+0x10 (Common .text:0628b19b), and Common's
    // phantom listener (vtable .rdata:0639c30c) is its only caller:
    //   .text:0628aa70  CB = [this+0x10]; obj = [arg2+0xa4] (IVP_Real_Object client_data,
    //                   the CObject); both non-null -> CALL [CB vtable + 0x00](obj), RET 8
    //   .text:0628aa90  the same shape -> CALL [CB vtable + 0x04](obj)
    //   .text:0628aab0  [this+0x10] = 0 (the phantom is going away; CB is not called)
    // MedMine's bodies (.text:06d04fb0 counts a ship in, .text:06d05070 counts it out) say
    // what the two slots mean. The vtable has exactly these two entries (.rdata:06d67a54 /
    // .rdata:06d67a58, then the next table's COL) -- no destructor.
    // The names come from Common's exports: SphereIntruderChecker derives only from CObjectCB
    // and its vftable (.rdata:063a4964) has exactly two slots, `?added@SphereIntruderChecker@@MAE
    // XPAUCObject@@@Z` (06332420) and `?deleted@...` (062ed730) -- protected virtual overrides of
    // these two, so the base slots carry the same names.
    struct CObjectCB
    {
        virtual void added(CObject* Obj) = 0;     // +0x00  an intruder entered the phantom (RET 4)
        virtual void deleted(CObject* Obj) = 0;   // +0x04  an intruder left it (RET 4)
    };

    // ---- PhantomParms ----
    // CreatePhantom @ 0x2aac0 / 0x2b0a0 / 0x2b200 all take PhantomParms const&.
    // Fields extracted from ctor stores in Common.dll.c.
    //
    // CORRECTED 2026-09-24 (w5-C, Server.dll MedMine): the block is 0x44 bytes, not 0x28, and
    // +0x04 is two bytes, not a dword. Both stack builders fill every field:
    //   Server FUN_06d04d60 (.text:06d04d6a .. 06d04de9) and Common FUN_0629cb70
    //   (.text:0629cbb3 .. 0629cc5f): +0x00 system id; +0x04/+0x05 bytes; +0x06 a
    //   NUL-terminated name (Common strcpy's PhySys::GetCollisionGroup into it at
    //   .text:0629cc2f, Server stores only its first byte = 0); +0x10 Vector; +0x1c a Matrix
    //   (both builders store the identity: 1.0f at +0x1c/+0x2c/+0x3c); +0x40 a listener
    //   pointer (Server: the MedMine's own listener sub-object at +0x60).
    // The consumer, CreatePhantom(Parms, float) (Common .text:0628b0a0), reads +0x00 (the
    // system map key, 0628b0f0), MOVZX +0x04 (0628b0e2), +0x06 (FUN_06348720, 0628b0dd),
    // MOVZX +0x05 (0628b174), +0x10 / +0x1c (FUN_0628c860, 0628b163) and +0x40 (0628b191,
    // stored at phantom+0x10 when non-null).
    struct PhantomParms
    {
        unsigned SystemId;          // +0x00
        unsigned char Flag04;       // +0x04
        unsigned char Flag05;       // +0x05
        char Name[10];              // +0x06 .. +0x0f  collision-group name
        Vector Position;            // +0x10
        Matrix Orientation;         // +0x1c
        CObjectCB* Listener;        // +0x40  phantom enter/leave listener (see CObjectCB above)
    };

    static_assert(sizeof(PhantomParms) == 0x44, "PhySys::PhantomParms is 0x44 bytes");

    // ---- RayHit ----
    // FindRayCollisions / FindRayIntersect output.
    struct RayHit
    {
        CObject* Object;           // +0x00
        Vector Point;              // +0x04
        Vector Normal;             // +0x10
        // +0x1c: a part id, not a distance. FUN_0628d1a0 stores the IVP hit's +0x14 object's
        // +4 dword here, or 0 when that pointer is null (.text:0628d3da..0628d3e8); the exe's
        // camera ray pick FUN_0051ddd0 matches it against the CObject PartMap's PartId words
        // (0x0051de62..0x0051de67 is_shield_part, 0x0051def4..0x0051defa part_to_inst).
        unsigned PartId;           // +0x1c
    };

    static_assert(sizeof(RayHit) == 0x20, "PhySys::RayHit sizeof 0x20");

    // ---- CollisionEvent ----
    // GetCollisions output range [Begin, End) -- the std::vector at world+0x20
    // (map node +0x30/+0x34, .text:06289a59 / 06289a62).
    //
    // 0x34 bytes, not 0x28. The world copy constructor sizes that vector with the
    // /0x34 idiom and `IMUL EAX,EAX,0x34` (.text:0628ec3f .. 0628ec58), its insert
    // is FUN_0628fa40 (same idiom, .text:0628fa55 ..), and Server.dll's consumer
    // steps by `ADD EAX,0x34` (.text:06ce3552). The producer at .text:0628c42b ..
    // 0628c547 builds the element from an IVP collision record R:
    //   +0x00 / +0x04  the two CObjects (core +0xa4 of R+0x30 / R+0x34)
    //   +0x08 / +0x0c  their part ids (hash lookups of R+0x38 / R+0x3c)
    //   +0x10          R+0x20 .. R+0x28
    //   +0x1c          R+0x00 .. R+0x08
    //   +0x28          R+0x10 .. R+0x18
    // Server.dll reads +0x10 as the shared contact point and negates +0x1c and
    // +0x28 for side B (.text:06ce3616 .. 06ce367b); their physical meaning is
    // not derived, hence the offset names.
    struct CollisionEvent
    {
        CObject* ObjectA;          // +0x00
        CObject* ObjectB;          // +0x04
        unsigned PartIdA;          // +0x08
        unsigned PartIdB;          // +0x0c
        Vector Point;              // +0x10
        // +0x1c: the IVP collision situation's surf_normal (GenerateCollisions) or the beam
        // contact's world normal (0628c410) (formerly Field_0x1c).
        Vector SurfaceNormal;      // +0x1c
        // +0x28: the situation's relative speed, or beam velocity minus surface speed
        // (formerly Field_0x28).
        Vector RelativeVelocity;   // +0x28
    };

    static_assert(sizeof(CollisionEvent) == 0x34, "PhySys::CollisionEvent sizeof 0x34");

    // ---- IVP_Time ----
    // There is no PhySys::IVP_Time. Retail's export ?GetCurrentTime@PhySys@@YA?AVIVP_Time@@I@Z
    // returns the GLOBAL class IVP_Time, IVP's split-precision stamp {float time_base; float
    // time_offset;} (src/IVP/ivp_physics/ivp_core.h, forward-declared above). The float-pair
    // layout is proven by FLControllerGolem's integrator (.text:06343170-06343185: FLD
    // [env+0xf8]; FSUB [this+0x64]; FADD [env+0xfc]; FSUB [this+0x68]) and by the golem storing
    // the pair at +0x64. GetCurrentTime itself (.text:06289aa7-06289ac4) copies the two dwords.
    // The port used to keep a private copy of the struct in this namespace; that duplicate
    // declaration is gone, and callers include ivp_core.h for the complete type.

    // ---- Controller (MI base for CShip @ +0x1a4) ----
    // ctor 0x25b50, dtor 0x25b90. Vtable at .rdata 0x13B60C.
    // Retail's vftable has 7 slots, not 3+dtor -- confirmed via RTTI class hierarchy
    // (COL 0639b608 -> CHD 063a681c -> base array 063a680c): PhySys::Controller
    // derives from IVP_Controller_Dependent, which derives from IVP_Controller (the
    // third-party Ipion physics SDK base, declared in common_ivp_controller.h).
    // The 3 extra vtable slots between
    // core_is_going_to_be_deleted_event and get_controller_priority are IVP_Controller's:
    // slot 1 (LAB_0627cd20, `FLD 1.0; RET`) and slot 3 (a `RET 0x8` no-op) are its
    // un-overridden defaults, and slot 4 (.rdata:0639b61c, _purecall) is
    // do_simulation_controller, declared below as pure because every concrete controller
    // overrides it (CShip, CEEngine, CEThruster, CSteering, CNudgeEngine, CStrafeEngine,
    // JumpInAction, LaunchAction, LaneAction).
    //
    // DERIVES FROM IVP_Controller since 2026-09-25 (claude-code:g-vt1). Until then this struct
    // declared its own virtuals -- destructor, core_is_going_to_be_deleted_event,
    // do_simulation_controller, get_controller_priority, get_associated_controlled_cores --
    // i.e. FIVE slots with the destructor first, against retail's seven in IVP order. Every
    // controller subclass therefore had its overrides in the wrong slots (Server.dll's Mine /
    // Guided tables .rdata:06d6622c / 06d66700 put do_simulation_controller at +0x10 and the
    // deleting destructor at +0x18, exactly as .rdata:0639b60c here). The slot order now
    // comes from IVP_Controller (common_ivp_controller.h):
    //   +0x00 core_is_going_to_be_deleted_event  this class (06285c30)
    //   +0x04 get_minimum_simulation_frequency    IVP_Controller's default (0627cd20, 1.0f)
    //   +0x08 get_associated_controlled_cores     this class (0632e7a0 / ICF)
    //   +0x0c reset_time                          IVP_Controller's default (`RET 0x8`)
    //   +0x10 do_simulation_controller            pure (_purecall)
    //   +0x14 get_controller_priority             this class (06285c80)
    //   +0x18 destructor                          this class (0627cd50)
    // DIRECT BASE IS IVP_Controller_Dependent since 2026-09-25 (claude-code:h-ivp), as retail's
    // CHD 063a681c lists it (Controller, IVP_Controller_Dependent, IVP_Controller -- the same
    // three BCDs sit inside Server.dll's Mine BCA 06d6c6b8 and Guided BCA 06d6c808). That class
    // adds no slot and no field (ivp_controller.h has the destructor-chain proof), so the
    // seven slots above and the +0x04 field offset are unchanged; what changes is the RTTI
    // chain every Controller-derived class emits.
    // C4275: IVP_Controller_Dependent is not exported; retail Common.dll exports no IVP_*
    // symbol either, only PhySys::Controller's own members.
#pragma warning(push)
#pragma warning(disable : 4275)
    struct COMMON_API Controller : public IVP_Controller_Dependent
    {
    public:
        Controller();
        Controller(Controller const& Other);
        // +0x18 -- overrides IVP_Controller's virtual destructor (its last slot).
        virtual ~Controller();

        Controller& operator=(Controller const& Other);

        static void __cdecl Register(Controller* Ctrl, CObject* Object);
        static void __cdecl UnRegister(Controller* Ctrl);

        void push(Vector const& Force, float Dt);
        void rotate(Vector const& Torque, float Dt);

    protected:
        // All four are protected in retail (M = protected virtual, I = protected non-virtual).
        // get_associated_controlled_cores is COMDAT-folded with a pile of other one-line
        // accessors at 0632e7a0 whose whole body is LEA EAX,[this+4] -- it hands back the
        // IVP_U_Vector that the three fields below ARE: memsize, n_elems, elems, which is
        // exactly IVP_U_Vector's own layout (ivp_u_vector.h: memsize@+0, n_elems@+2,
        // elems@+4).
        // Declared in RETAIL SLOT ORDER (see the table above); all are IVP_Controller
        // overrides, so the declaration order here no longer decides the slots -- it only
        // mirrors them.
        void core_is_going_to_be_deleted_event(IVP_Core* Core) override;              // +0x00
        IVP_U_Vector<IVP_Core>* get_associated_controlled_cores() override;           // +0x08
        // IVP_Controller slot 4 (.rdata:0639b61c): _purecall in this class's own table, so
        // PhySys::Controller is abstract in retail and exports no body for it. Every
        // concrete controller overrides it (the six exported overrides are protected, CShip's
        // private). Nothing in the port dispatches this slot yet -- retail's dispatcher is
        // the per-PSI loop FUN_063496a0 (.text:063498fa-0634990d), not ported.
        void do_simulation_controller(IVP_Event_Sim* EventSim,
                                      IVP_U_Vector<IVP_Core>* CoreList) override = 0;  // +0x10
        IVP_CONTROLLER_PRIORITY get_controller_priority() override;                    // +0x14
        void wakeup();

    public:
        // +0x04: the controlled cores (memsize +0x04, n_elems +0x06, elems +0x08), the vector
        // get_associated_controlled_cores hands to IVP. Register adds the object's core
        // (.text:06285cb0, grown through FUN_063441a0); the destructor, UnRegister and
        // core_is_going_to_be_deleted_event detach the controller and empty it.
        IVP_U_Vector<IVP_Core> Cores; // +0x04
        unsigned PadC;                // +0x0c  never written by the ctor; copied as a dword
    };
#pragma warning(pop)

    static_assert(sizeof(Controller) == 0x10, "PhySys::Controller 0x10 with pad");

    // ---- PhyCollisionStateManager ----
    // Manages per-object collision enable/disable via IVP mindist callbacks.
    // sizeof 0x14 from operator_new(0x14) in BuildIVP_Sphere @ 0x29370.
    // vtable .rdata:0639b864 (COL 063a6948), the three IVP_Listener_Phantom slots
    // 062816e0 / 06281740 / 062ed730 and no destructor slot.
#pragma warning(push)
#pragma warning(disable : 4275)
    class COMMON_API PhyCollisionStateManager : public IVP_Listener_Phantom
    {
    public:
        explicit PhyCollisionStateManager(CObject* Object);
        PhyCollisionStateManager(PhyCollisionStateManager const& Other);
        ~PhyCollisionStateManager();

        PhyCollisionStateManager& operator=(PhyCollisionStateManager const& Other);

        void enable_collisions(bool bEnable, bool bImmediate);
        bool get_desired_collision_state() const;
        bool get_actual_collision_state() const;
        void update(float Dt);
        // .text:062817c0 (thiscall, RET 8): the CObjects (or, with bRoot, their roots) behind
        // every mindist inside this object's phantom, added to Out; returns Out's count.
        int get_intruding_cobjs(bool bRoot, CheapSet<CObject*, std::less<CObject*>>& Out);

        // IVP_Listener_Phantom slots 0 / 1 / 2.
        void mindist_entered_volume(IVP_Controller_Phantom* PhantomCtrl, IVP_Mindist_Base* MindistBase) override;
        void mindist_left_volume(IVP_Controller_Phantom* PhantomCtrl, IVP_Mindist_Base* MindistBase) override;
        void phantom_is_going_to_be_deleted_event(IVP_Controller_Phantom* PhantomCtrl) override;

    protected:
        IVP_Real_Object* get_other_object(IVP_Mindist_Base* MindistBase);
        CObject* mindist_to_cobject(IVP_Mindist_Base* MindistBase);

    private:
        CObject* OwnerObject;         // +0x04
        unsigned IntruderCount;       // +0x08  phantom intruders inside
        // +0x0c: a float, not a count -- update() (.text:062816c3..062816ca) adds Dt to it
        // while IntruderCount != 0 and stores dword 0 when the phantom is torn down.
        float IntruderTime;           // +0x0c
        unsigned char CollisionType;  // +0x10  from Archetype::Root+0x39
        unsigned char Pad11[3];
    };
#pragma warning(pop)

    static_assert(sizeof(PhyCollisionStateManager) == 0x14, "PhyCollisionStateManager 0x14");

    // ---- Startup / Shutdown ----
    COMMON_API void __cdecl Startup(unsigned SystemId);
    COMMON_API void __cdecl Shutdown(unsigned SystemId);

    // ---- Update ----
    // 0x299c0: world lookup, FUN_0628ca80(Dt) = IVP environment simulate step.
    COMMON_API void __cdecl Update(unsigned SystemId, float Dt);

    // ---- Build hulls ----
    COMMON_API void __cdecl BuildIVP_Real(CObject* Object, CreateParms const& Parms);
    COMMON_API void __cdecl BuildIVP_Sphere(CObject* Object, float Radius, CreateParms const& Parms);

    // ---- FLControllerGolem (CObject+0x60) ----
    // Retail class name is RTTI-confirmed, not a guess: the leaf vftable installed at
    // .text:06288fae is .rdata:0639c1c8, its Complete Object Locator sits at [0639c1c8-4] =
    // 0639c1c4 -> 063a70e0, whose +0xC type-descriptor pointer (.rdata:063a70ec) is
    // .data:063eb0f8 = ".?AVFLControllerGolem@@". Earlier tracker notes on items 2729-2743
    // called this cluster "PhySys::Golem" -- that name was invented before anyone read the
    // COL, and is wrong.
    //
    // The class is a genuine IVP_Controller: its constructor registers itself into the body's
    // core via IVP_Core::register_controller (FUN_06349020 -> FUN_06349180) and its destructor
    // detaches via FUN_06349030 -> FUN_06349110. It therefore derives from IVP_Controller, and
    // its full definition lives in common_controllergolem.cpp rather than here: this header is pulled
    // into Server.dll and Freelancer.exe translation units (common_cobject.h, common_server.h,
    // space_scene_load.cpp) that have no src/IVP include path, and dragging ivp_controller.h
    // across that boundary is not worth it for three call sites. The three entry points below
    // are port-local free functions standing in for retail's __thiscall methods. None of the
    // four functions below is a retail export (all are FUN_ addresses), so none is COMMON_API.
    // (The class itself is declared at global scope above -- ".?AVFLControllerGolem@@" has no
    // namespace qualifier, so it is NOT PhySys::FLControllerGolem.)

    // FUN_06343580 (.text:06343580-063435e9, __thiscall, RET 0xc).
    // set_target_position(TargetPosition, TargetSpeed, Time).
    void __cdecl GolemSetTargetPosition(::FLControllerGolem* Golem,
                                                  Vector const& TargetPosition,
                                                  Vector const& TargetSpeed,
                                                  IVP_Time const& Time);

    // FUN_06343670 (.text:06343670-063436f1, __thiscall, RET 0x10).
    // set_target_quaternion(TargetQuat, Time, TargetRotQuatOrNull, DeltaTime). Both quaternion
    // arguments are 4 floats in IVP's (X,Y,Z,W) order, NOT FQuaternion's (W,X,Y,Z).
    void __cdecl GolemSetTargetQuaternion(::FLControllerGolem* Golem,
                                                    float const TargetQuat[4],
                                                    IVP_Time const& Time,
                                                    float const* TargetRotQuatOrNull,
                                                    float DeltaTime);

    // FUN_06343cc0 (.text:06343cc0-06343f63, __cdecl, 191 insns / 676 bytes; decomp-tracker
    // Common.dll item 2746). "Beam this body onto an absolute world pose by re-seating its
    // fixed offset inside its own IVP_Core" -- see the definition in common_controllergolem.cpp for the
    // full instruction-level derivation.
    //
    // Retail's argument list is (ReferenceBody, Body, Quat, Position, Unused). The fifth
    // argument is pushed by BOTH of the DLL's two call sites (.text:0627e1ec inside
    // AnimDB::Update's TransformState==2 arm, and .text:06343926 inside FUN_06343700) and read
    // by NOTHING in the body -- the callee's frame only ever touches [ESP+param_1..param_4] --
    // so it is dropped here rather than carried as a dead parameter.
    //
    // ReferenceBodyOrNull is retail's param_1, an IVP_Real_Object* whose cached narrow-phase
    // world transform pre-multiplies the requested pose. It is the literal constant 0 at both
    // call sites (verified instruction-by-instruction, not inferred) -- see the definition.
    //
    // Quat is 4 floats in IVP's (X,Y,Z,W) order, same convention as GolemSetTargetQuaternion.
    // Retail always returns 1 and neither call site inspects the result.
    bool __cdecl BeamBodyToWorldPose(IVP_Real_Object* ReferenceBodyOrNull,
                                     IVP_Real_Object* Body,
                                     float const WorldQuat[4],
                                     Vector const& WorldPosition);

    // FUN_06288e60 (.text:06288e60-062890cd, __cdecl). The per-child-part physics finalize:
    // switches on CreateParms::Type (+0x18), builds and primes the golem for mode 1, and for
    // mode 2 welds the child body into its root's core. See its definition in
    // common_controllergolem.cpp for what is and is not ported.
    void __cdecl AttachGolemController(CObject* Object, CreateParms const& Parms);

    // ---- Attach / Detach ----
    COMMON_API void __cdecl Attach(CObject* Parent, CObject* Child);
    COMMON_API void __cdecl Detach(CObject* Object, DetachParms const& Parms);

    // ---- Mass / inertia ----
    COMMON_API void __cdecl SetMass(CObject* Object, float Mass);
    COMMON_API float __cdecl GetMass(CObject const* Object);
    COMMON_API void __cdecl SetMomentOfInertia(CObject* Object, Vector const& Inertia);
    COMMON_API Vector __cdecl GetMomentOfInertia(CObject const* Object);

    // ---- Damping ----
    COMMON_API void __cdecl GetDamping(CObject const* Object, float& OutLinear, Vector& OutAngular);
    COMMON_API void __cdecl SetDamping(CObject* Object, float Linear, Vector const& Angular);

    // ---- Velocity / angular velocity ----
    COMMON_API Vector __cdecl GetVelocity(CObject const* Object);
    COMMON_API void __cdecl AddToVelocity(CObject* Object, Vector const& Delta);
    COMMON_API Vector __cdecl GetAngularVelocityOS(CObject const* Object);
    COMMON_API void __cdecl AddToAngularVelocityOS(CObject* Object, Vector const& Delta);

    // ---- Impulses ----
    COMMON_API void __cdecl AddImpulseAtPoint(CObject* Object, Vector const& Impulse, Vector const& Point);
    COMMON_API void __cdecl LinearImpulse(CObject* Object, Vector const& Direction, float Magnitude);
    COMMON_API void __cdecl AngularImpulse(CObject* Object, Vector const& Axis, float Magnitude);
    COMMON_API void __cdecl AngularImpulseOS(CObject* Object, Vector const& Axis, float Magnitude);

    // ---- Activation ----
    COMMON_API void __cdecl Wakeup(CObject* Object);
    COMMON_API void __cdecl DeactivatePhysics(CObject* Object);
    COMMON_API void __cdecl DeactivatePhysicsR(CObject* Object);

    // ---- Collision state ----
    COMMON_API bool __cdecl GetCollisionState(CObject const* Object);
    COMMON_API void __cdecl SetCollisionState(CObject* Object, bool bEnable);
    COMMON_API char const* __cdecl GetCollisionGroup(CObject const* Object);
    COMMON_API void __cdecl SetCollisionGroup(CObject* Object, char const* const Group);

    // ---- Center of mass / radius ----
    COMMON_API Vector __cdecl GetCenterOfMass(CObject const* Object);
    COMMON_API float __cdecl GetRadiusR(CObject const* Object, bool bIncludeChildren, Vector& OutCenter);

    // ---- BeamR ----
    COMMON_API void __cdecl BeamR(CObject* Object, Vector const& Pos, Matrix const& Orient, bool bFlag);

    // ---- Remote correction (RC) ----
    COMMON_API void __cdecl RCBegin(CSimple* Object, Vector const& Pos, Vector const& Vel,
                          Quaternion const& Orient, Vector const& AngVel);
    COMMON_API void __cdecl RCSet(CSimple* Object, Vector const& Pos, Quaternion const& Orient, double Time);
    COMMON_API void __cdecl RCEnd(CSimple* Object);

    // ---- ReMake / UnMake ----
    COMMON_API void __cdecl ReMakePhysicalR(CObject* Object, CreateParms const& Parms, float Radius);
    COMMON_API void __cdecl UnMakePhysicalR(CObject* Object);

    // ---- Phantoms ----
    COMMON_API int __cdecl CreatePhantom(PhantomParms const& Parms, Vector const& Pos, void*& OutHandle);
    COMMON_API int __cdecl CreatePhantom(PhantomParms const& Parms, float Radius, void*& OutHandle);
    COMMON_API int __cdecl CreatePhantom(PhantomParms const& Parms, PhyArch::Part const& Part, void*& OutHandle);
    COMMON_API void __cdecl DestroyPhantom(void* Handle);
    COMMON_API int __cdecl AttachPhantom(CObject* Object, void* Handle);
    COMMON_API int __cdecl DetachPhantom(void* Handle);
    COMMON_API bool __cdecl GetCollisionStatePhantom(void* const Handle);
    COMMON_API void __cdecl SetCollisionStatePhantom(void* Handle, bool bEnable);
    COMMON_API bool __cdecl GetClosestPhantomIntruderPos(void* Handle, unsigned FilterA, unsigned FilterB,
                                               CSimple*& OutObject, Vector& OutPos);

    // ---- Queries ----
    COMMON_API int __cdecl FindRayCollisions(unsigned SystemId, Vector const& From, Vector const& To,
                                   RayHit* Hits, int MaxHits);
    COMMON_API int __cdecl FindRayIntersect(CObject* Object, Vector const& From, Vector const& To,
                                  RayHit* Hits, int MaxHits);
    COMMON_API int __cdecl FindSphereCollisions(unsigned SystemId, Vector const& Center, float Radius,
                                      CObject** OutObjects, int MaxObjects);
    COMMON_API void __cdecl GenerateCollisions(CBeam* Beam);
    COMMON_API void __cdecl GetCollisions(unsigned SystemId, CollisionEvent const*& Begin,
                                CollisionEvent const*& End);

    // ---- Misc ----
    COMMON_API bool __cdecl AnyActive();
    COMMON_API unsigned __cdecl MovingObjectCount(unsigned SystemId);
#ifdef GetCurrentTime
#undef GetCurrentTime
#endif
    COMMON_API IVP_Time __cdecl GetCurrentTime(unsigned SystemId);
}

// ---- Collision (free functions, namespace Collision) ----
namespace Collision
{
    COMMON_API bool __cdecl Load(class INI_Reader& Reader);
    COMMON_API void __cdecl Free();
    COMMON_API float __cdecl GetImpactDamageFromType(unsigned TypeId);
    COMMON_API float __cdecl CalcCollisionDamage(unsigned TypeId, Vector const& RelVel, Vector const& Normal);
    COMMON_API float __cdecl CalcCollisionDamage(float DamageFactor, float MaxSpeed, float ActualSpeed);
}

// ---- SphereIntruderChecker ----
// A phantom listener that collects the CObjects a probe sphere touches. RTTI
// ".?AVSphereIntruderChecker@@" (.data:063eda28): class hierarchy 063aa948 has two bases --
// itself and PhySys::CObjectCB at offset 0 (base-class array 063aa93c). Retail layout: +0x00
// the vftable .rdata:063a4964 (added / deleted), +0x04 a VC6 std::list<CObject*> (allocator
// byte +0x04, head +0x08, size +0x0c), sizeof 0x10. The port holds a current std::list (No
// retail ABI); no other module allocates the class.
class COMMON_API SphereIntruderChecker : public PhySys::CObjectCB
{
public:
    // 06332470: the list's default constructor, then the vftable.
    SphereIntruderChecker();
    // 063324a0: the list's copy constructor (a fresh head, every element appended in order).
    SphereIntruderChecker(SphereIntruderChecker const& Other);
    // 06332610: the list's destructor (no vftable store; CObjectCB has no destructor).
    ~SphereIntruderChecker();

    // 06332530: `if (&Intruders != &Other.Intruders) Intruders = Other.Intruders` (VC6
    // list::operator=: overwrite in place, erase the excess, append the rest).
    SphereIntruderChecker& operator=(SphereIntruderChecker const& Other);

    // 06332660 (RET 0x10): clear the list, probe a sphere phantom of Radius at Center in system
    // SystemId with this as its listener, drop adjacent duplicates, copy the result into Out.
    void check_sphere(unsigned SystemId, Vector const& Center, float Radius, std::list<CObject*>& Out);

protected:
    // Retail: ?added@SphereIntruderChecker@@MAEXPAUCObject@@@Z / ?deleted@... (M = protected
    // virtual), CObjectCB's two slots.
    virtual void added(CObject* Object) override;
    virtual void deleted(CObject* Object) override;

private:
    std::list<CObject*> Intruders;  // +0x04
};

// ---- Instance detach helpers (retail FUN_0628a410 / FUN_0628a050, not exported) ----
// Both unlink InstanceId's joint from its parent instance. When the instance is its owning
// CObject's own root: DetachInstanceInPlace releases that object, DetachInstance makes it a free
// physics body (PhySys::Detach) and returns it. Otherwise the instance is a sub-part of the owner:
// DetachInstanceInPlace erases the PartMap entries below it and destroys the instance,
// DetachInstance splits it off into a new object (FUN_0628a0c0, PhySys_SplitInstance).
// Bodies: src/Common/common_physys_build.cpp (end of file).
void __cdecl DetachInstanceInPlace(long InstanceId);
CObject* __cdecl DetachInstance(long InstanceId, PhySys::DetachParms const& Parms);
