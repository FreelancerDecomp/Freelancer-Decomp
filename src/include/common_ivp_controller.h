// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Public since 2026-10-14 (claude-code:cmn-s2-phy8; formerly src/IVP/ivp_physics/
// ivp_controller.h, which now only includes this file): Common exports classes built on these
// bases (CRemotePhysicsController : IVP_Controller_Golem, PhySys::Controller, the engine
// controllers) and Server.dll dynamic_casts CObject::GolemController (IVP_Controller*) at
// .text:06d4d672, so the hierarchy is a cross-DLL contract.

#include "../IVP/ivp_utility/ivp_u_vector.h"
#include "../IVP/ivp_utility/ivp_math.h"  // IVP_U_Point / IVP_U_Quat members of IVP_Controller_Motion
#include "../IVP/ivp_physics/ivp_core.h"  // IVP_Time, passed BY VALUE to IVP_Controller slot 3

// The third-party Ipion "IVP_Controller" family, as far as retail Common.dll / Server.dll
// carry it. RTTI (Class Hierarchy Descriptors, read from EXE\Common.dll bytes):
//   IVP_Controller               CHD 063a6848, 1 entry  -- vftable .rdata:0639b64c (COL 063a6858)
//   IVP_Controller_Dependent     CHD 063a67a0, 2 entries (+ IVP_Controller)
//                                               -- vftable .rdata:0639b62c (COL 063a67b0)
//   PhySys::Controller           CHD 063a681c: Controller, IVP_Controller_Dependent, IVP_Controller
//   FLControllerGolem            CHD 063a70d0: FLControllerGolem, IVP_Controller_Golem,
//                                IVP_Controller_Motion, IVP_Controller_Independent, IVP_Controller
//   CRemotePhysicsController     CHD 063a6af8: the same chain + CRemotePhysicsSimulation @0xd0
// So there are TWO branches under IVP_Controller: Dependent (PhySys::Controller and every
// Freelancer controller built on it -- CShip, the equipment controllers, Server's Mine and
// Guided) and Independent -> Motion -> Golem (the golem controllers). Until 2026-09-25 the port
// had neither intermediate class: PhySys::Controller derived from IVP_Controller directly and
// IVP_Controller_Golem folded Motion in and skipped Independent (claude-code:h-ivp restored all
// three; see each class below for its evidence).
//
// IVP_Core::~IVP_Core() (IVP_Core_Remove_Object in ivp_core.cpp) dispatches slot 0,
// core_is_going_to_be_deleted_event(IVP_Core*), on every attached controller when a
// core is torn down and (Flags & 0xC) != 4. Confirmed by the
// real destructor's raw dispatch (FUN_063477e0, Common.dll.txt:411293-411300:
// `MOV ECX,[array+i*4]; MOV EDX,[ECX]; PUSH Core; CALL [EDX]` -- i.e.
// controller->vtbl[0](Core), thiscall) matched 1:1 against common_physys.h's
// own confirmed slot-0 name/signature for the same base class.
//
// IVP_Controller has NO virtual destructor ahead of this slot (common_physys.h:
// "Retail's vftable has 7 slots, not 3+dtor") -- do not add one here, it would
// shift slot 0 and desync this class from retail's slot order.
//
// UPDATED 2026-09-17: all seven slots are now declared, at their real indices,
// after PhySys::Controller's vftable was dumped from .rdata:0639b60c (the dump
// is quoted at get_controller_priority below). The sentence that used to stand
// here -- "this port has no concrete IVP_Controller subclass yet, so
// Core->Controllers is always empty and nothing calls through them" -- is no
// longer true: FLControllerGolem (src/Common/common_physys.cpp, RTTI name from
// the COL trail at .rdata:0639c1c4) is a real one, and its constructor calls
// IVP_Core::register_controller(this) exactly as retail does.
class IVP_Core;
class IVP_Environment;

// IVP_Event_Sim -- the per-PSI simulation event handed to every controller's
// do_simulation_controller (slot 4 below). A class in retail: the exported
// overrides mangle it as PAVIVP_Event_Sim@@ (e.g.
// ?do_simulation_controller@CEEngine@@MAEXPAVIVP_Event_Sim@@PAV?$IVP_U_Vector@VIVP_Core@@@@@Z).
// Recovered 2026-09-17 (lane claude:w19-integrator, while porting LAB_06343150) and moved
// here from common_physys.cpp once slot 4 got its real signature.
//
// Layout proven from the one retail site that BUILDS one, FUN_063499c0
// (.text:063499c0-06349a57), which fills a 3-dword stack local from the environment and then
// hands its address to FUN_063496a0:
//     .text:063499c7  FLD  [EAX+0xac]   -> local+0x00      (EAX = the IVP_Environment*)
//     .text:063499d7  FLD  [EAX+0xb0]   -> local+0x04
//     .text:063499dd  MOV  [local+0x08],EAX
// IVP_Environment+0xAC is `delta_time` and +0xB0 is `inv_delta_time` (ivp_environment.h).
// Corroborated at the dispatch loop FUN_063496a0 (.text:063498fa-0634990d): `MOV ECX,[EAX]`
// (the controller), `PUSH EAX+4`, `PUSH EBX`, `CALL [EDX+0x10]` -- EBX, argument 1, is that
// function's own param_1. Every exported override reads [Sim+0] as its Dt (CEEngine
// .text:062964c5, CEThruster .text:0629d315, CStrafeEngine .text:062bb985), and
// FLControllerGolem's integrator uses +0x00/+0x04 as reciprocals (.text:0634333e-0634336a).
//
// +0x0c (2026-10-08, claude-code:cmn-phy3): the simulation unit whose controllers are being
// run. FUN_063499c0 reserves FOUR dwords (`SUB ESP,0x10`, .text:063499c0) and fills the first
// three; the fourth is written per unit by the dispatch loop itself, FUN_063496a0
// `MOV [ECX+0xc],EDI` (.text:063496b0, ECX = the event, EDI = the IVP_Simulation_Unit `this`).
// The friction controllers read it back: `MOV EAX,[EDI+0xc]; MOV ECX,[EAX]; TEST CH,0x30`
// (.text:06356533-06356538) tests IVP_Simulation_Unit::State, and .text:063565fd-0635660f
// rewrites that same State word.
class IVP_Simulation_Unit;
class IVP_Event_Sim
{
public:
    float delta_time;               // +0x00 <- IVP_Environment::delta_time     (+0xAC)
    float i_delta_time;             // +0x04 <- IVP_Environment::inv_delta_time (+0xB0)
    IVP_Environment* environment;   // +0x08
    IVP_Simulation_Unit* sim_unit;  // +0x0c <- set by FUN_063496a0 (.text:063496b0)
};

static_assert(sizeof(IVP_Event_Sim) == 0x10, "IVP_Event_Sim sizeof 0x10 (FUN_063499c0 SUB ESP,0x10)");

// IVP's controller-priority enum, the return type of slot 5 below. Retail spells it in
// ?get_controller_priority@Controller@PhySys@@MAE?AW4IVP_CONTROLLER_PRIORITY@@XZ, so it is a
// global-scope enum. MOVED HERE 2026-09-25 (claude-code:g-vt1) from common_physys.h, because
// PhySys::Controller now derives from IVP_Controller and its override must return the same
// type as the base slot. The only value recoverable from the binary is 0x5dc: PhySys::
// Controller's slot 5 (06285c80) is literally MOV EAX,0x5dc / RET. 1500 is IVP_CP_MOTION in
// the published IVP headers, so that name is the SDK's; IVP_CP_NONE is there to keep the enum
// from having a single enumerator. The VALUE is retail's and the NAMES are IVP convention.
//
// 2026-10-08 (claude-code:cmn-phy3): three more retail values, from the friction system's
// controllers (see IVP_ListManager in ivp_mindist.h): 0 for the static friction handle
// (06325390 `XOR EAX,EAX; RET`), 600 for the friction system itself (0634b6e0 `MOV EAX,0x258`)
// and 2000 for the energy handle (0634b690 `MOV EAX,0x7d0`). Values are retail's, names are
// the IVP SDK's for those roles.
enum IVP_CONTROLLER_PRIORITY
{
    IVP_CP_NONE = 0,
    IVP_CP_STATIC_FRICTION = 0,
    IVP_CP_DYNAMIC_FRICTION = 600,
    IVP_CP_GRAVITY = 1000,          // 0634c6c0 `MOV EAX,0x3e8` (IVP_Standard_Gravity_Controller, cmn-phy5)
    IVP_CP_MOTION = 1500,
    IVP_CP_ENERGY_FRICTION = 2000
};

// IVP_Controller's OWN vftable is .rdata:0639b64c (COL 063a6858 -> CHD 063a6848, one entry):
//   +0x00 062ed730  `RET 0x4`            (ICF label MetaList<GoodDesc>::operator=)
//   +0x04 0627cd20  `FLD 1.0f; RET`
//   +0x08 06391fb0  _purecall
//   +0x0c 0628f050  `RET 0x8`            (ICF label JustifyNode::Execute)
//   +0x10 06391fb0  _purecall
//   +0x14 06391fb0  _purecall
//   +0x18 0627cdb0  scalar deleting destructor: stamp 0639b64c, delete on flag 1
// It is stamped by the out-of-line destructor FUN_0634b6a0 (.text:0634b6a0, `MOV [ECX],
// 0639b64c; RET`, called from the EH unwind funclets) and by every derived destructor's
// epilogue (.text:06285c1b PhySys::Controller, .text:0634300c IVP_Controller_Motion,
// .text:0634b7b2 another IVP controller). No data members: the first derived field sits at +4
// in every subclass (PhySys::Controller's core vector, Motion's target position).
class IVP_Controller
{
public:
    // Slot 0. NOT pure: 0639b64c[0] is .text:062ed730, a bare `RET 0x4` -- an empty inline
    // default (it was declared `= 0` here until 2026-09-25; a pure slot would be _purecall
    // 06391fb0, as slots 2/4/5 are). IVP_Core's teardown FUN_063477e0 dispatches it.
    virtual void core_is_going_to_be_deleted_event(IVP_Core* Core) { (void)Core; }

    // Slot 1. NOT get_controller_priority -- that claim is RETRACTED, see below.
    // Retail's slot 1 is .rdata:0639b610 -> LAB_0627cd20, `FLD float ptr [1.0f]; RET` --
    // an inline default of this base: every table that does not override it carries a copy
    // of that body (Common: PhySys::Controller 0639b610, the golem tables 0639c1cc /
    // 0639ba3c; Server.dll: its own copy .text:06cf33f0, `FLD [.rdata:06d65520 = 1.0f]`,
    // in Mine's and Guided's PhySys::Controller tables .rdata:06d66230 / 06d66704). So it
    // returns a float and is not pure. Named after Ipion's IVP_Controller, whose second
    // virtual is get_minimum_simulation_frequency (default 1.0); its other six slots line up
    // with retail's table one for one (slot 3 = reset_time, below).
    virtual float get_minimum_simulation_frequency() { return 1.0f; } // formerly slot_01

    // Slot 2: FUN_06348f60/FUN_06348fe0/FUN_06349040 (decomp-tracker items
    // 2926/2927/2930, D-cluster-06348f60 pass, 2026-09-10) all dispatch
    // `CALL dword ptr[vtbl+8]` (slot index 2) on an IVP_Controller* and use
    // the return value exactly as an `IVP_U_Vector<IVP_Core>*` (word count at
    // +2, elems array at +4) -- i.e. this class's OWN associated-cores list,
    // not a value borrowed from elsewhere. CONFIRMED, and now corroborated by
    // the raw table dump below rather than standing alone against it.
    virtual IVP_U_Vector<IVP_Core>* get_associated_controlled_cores() = 0;

    // Slot 3: .rdata:0639b618, reset_time. Named after Ipion's IVP_Controller::reset_time(
    // IVP_Time offset): the friction system's override (IVP_ListManager, LAB_06355bb0) is
    // Ipion's IVP_Friction_System::reset_time loop exactly -- pairs backward, dists backward,
    // each dist's own time stamp rebased by the offset. In PhySys::Controller's table it is the
    // shared bare `RET 0x8` body (labelled JustifyNode::Execute by ICF), and Server.dll's
    // Mine/Guided PhySys::Controller tables carry the same `RET 0x8` (.text:06d28880 at
    // .rdata:06d66238 / 06d6670c) -- an inline empty default of this base, not a pure slot.
    // ARGUMENT RECOVERED 2026-09-25 (claude-code:h-ivp) from the one retail override,
    // IVP_Controller_Golem's LAB_063435f0 (.text:063435f0-0634366b, RET 0x8): it reads
    // [ESP+4] and [ESP+8] as FLOATS and subtracts them from the two halves of its stored
    // IVP_Time (+0x64 time_base, +0x68 time_offset), then re-normalises the pair. The 8 bytes
    // are therefore one IVP_Time passed by value (two floats, same order as ivp_core.h's
    // IVP_Time) -- which is also what makes the port's default pop 8 bytes like retail's.
    virtual void reset_time(IVP_Time Delta) { (void)Delta; } // formerly slot_03

    // Slot 4: .rdata:0639b61c, do_simulation_controller -- named by the six
    // retail exports that override it (CEEngine 06296430, CEThruster 0629d2d0,
    // CNudgeEngine 062ad340, CShip 0628f050, CSteering 062bb7c0, CStrafeEngine
    // 062bb960; all `...do_simulation_controller@...@@?AEXPAVIVP_Event_Sim@@
    // PAV?$IVP_U_Vector@VIVP_Core@@@@@Z`, RET 0x8). _purecall in PhySys::Controller's
    // table, so every concrete controller overrides it; FLControllerGolem's force
    // integrator LAB_06343150 is its override (common_physys.cpp).
    // Retail's only dispatcher is the per-PSI controller loop FUN_063496a0
    // (.text:063498fa-0634990d: `PUSH element+4; PUSH EBX(event); CALL [EDX+0x10]`),
    // which this port has not ported -- nothing in src/IVP calls this slot yet.
    virtual void do_simulation_controller(IVP_Event_Sim* EventSim, IVP_U_Vector<IVP_Core>* CoreList) = 0;

    // Slot 5: get_controller_priority. RESOLVED 2026-09-17, and it resolves the
    // conflict this header used to carry unreconciled between its own slot-2
    // call-site evidence and common_physys.h's RTTI-derived ordering. BOTH were
    // partly right: slot 2 is get_associated_controlled_cores (call sites win),
    // and get_controller_priority is at slot 5 -- NOT slot 1, where this header
    // put it purely as a spacer.
    //
    // Settled by dumping the table rather than by argument. PhySys::Controller's
    // vftable starts at .rdata:0639b60c (its RTTI COL sits at 0639b608) and ends
    // before 0639b628, which is the next class's COL -- seven slots:
    //   +0x00 0639b60c  PhySys::Controller::core_is_going_to_be_deleted_event
    //   +0x04 0639b610  LAB_0627cd20
    //   +0x08 0639b614  CommReferrable::GetMsgIdPrefix      <- /OPT:ICF artifact
    //   +0x0c 0639b618  JustifyNode::Execute                <- /OPT:ICF artifact
    //   +0x10 0639b61c  LAB_06391fb0 == _purecall
    //   +0x14 0639b620  PhySys::Controller::get_controller_priority
    //   +0x18 0639b624  LAB_0627cd50
    // The two folded names are worthless as identification: JustifyNode::Execute
    // is a bare `RET 0x8` with hundreds of unrelated callers and is this module's
    // single biggest ICF magnet. Only the two names that carry PhySys::Controller's
    // own qualification mean anything, and one of them is at +0x14.
    //
    // Returns IVP_CONTROLLER_PRIORITY (above), the type retail's PhySys::Controller
    // override mangles with. PhySys::Controller's body is literally `MOV EAX,0x5dc / RET`
    // at .text:06285c80.
    virtual IVP_CONTROLLER_PRIORITY get_controller_priority() = 0;

    // Slot 6: .rdata:0639b624 -> LAB_0627cd50. That body is PhySys::Controller's
    // vector deleting destructor (flag test 2, `eh vector destructor iterator` with
    // PhySys::Controller::~Controller, then operator delete on flag 1), i.e. the
    // virtual destructor's slot.
    //
    // DECLARED AS THE REAL VIRTUAL DESTRUCTOR 2026-09-24 (claude-code:w6-rpc), replacing the
    // `slot_06()` placeholder. MSVC gives a virtual destructor ONE vftable entry (the
    // deleting destructor) at its declaration position, so declaring it here -- after the
    // six slots above and nowhere earlier -- keeps it at +0x18 exactly where retail has it.
    // Two retail sites dispatch it as `vtbl[6](1)` on an IVP_Controller*, i.e. `delete`:
    // the golem family's slot 0 (LAB_06342fc0: `MOV EAX,[ECX]; PUSH 1; CALL [EAX+0x18]`)
    // and PhySys::RCEnd (.text:06285b3c-06285b40, same three instructions on the
    // CRemotePhysicsController it owns). Both are `delete` through this base now.
    virtual ~IVP_Controller() {}
};

class IVP_Real_Object;
class Vector;
struct FLControllerGolemParms;  // the stack-built tuning block, src/Common/common_physys.cpp

// ============================================================================
// IVP_Controller_Dependent -- RTTI-CONFIRMED NAME, the direct base of PhySys::Controller.
// ============================================================================
// CHD .rdata:063a67a0 (2 entries: IVP_Controller_Dependent, IVP_Controller), own vftable
// .rdata:0639b62c (COL 063a67b0):
//   +0x00 062ed730  `RET 0x4`   -- IVP_Controller's default, not overridden
//   +0x04 0627cd20  `FLD 1.0f`  -- IVP_Controller's default
//   +0x08 06391fb0  _purecall
//   +0x0c 0628f050  `RET 0x8`   -- IVP_Controller's default
//   +0x10 06391fb0  _purecall
//   +0x14 06391fb0  _purecall
//   +0x18 0634b6b0  scalar deleting destructor
// The table is IVP_Controller's slot for slot: this class adds no virtual and overrides
// only the destructor. It has NO data members either -- proven by the destructor chain, not
// assumed: 0634b6b0 calls FUN_0634b6a0, which is IVP_Controller's destructor (stamp 0639b64c
// only). The compiler folded this class's destructor into its base's because its own vptr
// stamp was immediately overwritten (a dead store) -- which can only happen when there is
// nothing to destroy between the two stamps. PhySys::Controller's destructor shows the same
// thing from the derived side: after its own core-vector cleanup it stamps 0639b64c
// (.text:06285c1b) directly, with no 0639b62c stamp in between.
// The table is stamped by inlined PhySys::Controller construction sites (the copy ctor
// .text:0627ccf6, and 06286e76 / 06287816 / 062878f6 / 06287a06 / 06287d0b / 06295aa0 /
// 0629d078), each immediately overwritten by the derived table.
// Header-only on purpose: Server.dll derives Mine/Guided from PhySys::Controller and does
// not link the IVP sources (they are compiled into Common.dll), exactly as retail Server.dll
// carries its own copies of this family's inline defaults (.text:06cf33f0, .text:06d28880).
class IVP_Controller_Dependent : public IVP_Controller
{
public:
    virtual ~IVP_Controller_Dependent() {}
};

static_assert(sizeof(IVP_Controller_Dependent) == 0x4,
              "IVP_Controller_Dependent is vptr-only: PhySys::Controller's first field is at +4");

// ============================================================================
// IVP_Controller_Independent -- RTTI-CONFIRMED NAME, the direct base of IVP_Controller_Motion.
// ============================================================================
// Named by the CHDs of FLControllerGolem (063a70d0) and CRemotePhysicsController (063a6af8),
// TypeDescriptor .data:063eae68. Retail has NO vftable of its own for it: its constructor
// and destructor are inlined into IVP_Controller_Motion's (FUN_06342c80 stamps 063a53b4
// directly; FUN_06342fd0 goes from 063a53b4 straight to IVP_Controller's 0639b64c at
// .text:0634300c) and the dead stamps took the table with them (/OPT:REF). What it DOES
// contribute survives in its descendants' tables: slot 2 is LAB_06285a60 in EVERY table
// built on this class and in no other -- Motion 063a53bc, Golem 063a53e4, FLControllerGolem
// 0639c1d0, CRemotePhysicsController 0639ba40, and the three other IVP controllers at
// 063a54ec / 063a5508 / 063a5578 (whose slot 6 is IVP_Controller's own 0634b6b0 and whose
// slot 0 is IVP_Controller's 062ed730, i.e. they add nothing else at these levels).
// PhySys::Controller's branch (Dependent) returns its own per-object vector there instead.
// Like Dependent it has no fields: Motion's first field is at +4 (.text:06342ca5).
class IVP_Controller_Independent : public IVP_Controller
{
public:
    virtual ~IVP_Controller_Independent() {}

    // Slot 2, LAB_06285a60 (.text:06285a60-06285a65): `MOV EAX,.data:DAT_0658a9e8; RET` --
    // one process-wide IVP_U_Vector<IVP_Core> shared by every independent controller
    // (constructed empty by the static initializer LAB_06348760 -> FUN_06348770, which zeroes
    // its memsize/n_elems/elems at .text:06348772-0634877e; torn down at exit by LAB_063487a0).
    // Nothing in retail ever adds to it, so "an independent controller controls no cores
    // through this list" -- the controller registers itself with its core directly instead
    // (IVP_Core::register_controller, see IVP_Controller_Motion's constructor).
    // Defined in ivp_controller.cpp next to that vector.
    virtual IVP_U_Vector<IVP_Core>* get_associated_controlled_cores() override;
};

static_assert(sizeof(IVP_Controller_Independent) == 0x4,
              "IVP_Controller_Independent is vptr-only: Motion's first field is at +4");

// The first 0x24 bytes of the golem tuning block -- the part IVP_Controller_Motion's
// constructor FUN_06342c80 reads (+0x00..+0x0c at .text:06342cf0-06342d08, +0x10/+0x14/+0x18
// at .text:06342cc6-06342cd7, +0x20 three times at .text:06342cde-06342ce4; +0x1c is never
// read). PORT-LOCAL NAME: retail's type name is not in any binary. The split mirrors retail's
// own construction of the block: FUN_06343020 (the whole 0x38-byte block) first calls
// FUN_06342c40, which zero-fills exactly these nine dwords and sets their defaults, then adds
// its own five (.text:06343028-06343041). FLControllerGolemParms (src/Common/common_physys.cpp)
// derives from this.
struct IVP_Controller_Motion_Parms
{
    float ForceFactor;             // +0x00  default 0.8f  (0x3f4ccccd)
    float DampFactor;              // +0x04  default 1.0f
    float TorqueFactor;            // +0x08  default 0.8f
    float AngularDampFactor;       // +0x0c  default 1.0f
    float MaxTranslationForce[3];  // +0x10  default 1e6f each (0x49742400)
    float Field_0x1c;              // +0x1c  default 0.0f; never read by FUN_06342c80
    float MaxTorque;               // +0x20  default 1e6f
};

static_assert(sizeof(IVP_Controller_Motion_Parms) == 0x24, "IVP_Controller_Motion_Parms 0x24");

// ============================================================================
// IVP_Controller_Motion -- RTTI-CONFIRMED NAME, the direct base of IVP_Controller_Golem.
// ============================================================================
// Named by the same two CHDs (TypeDescriptor .data:063eae94). No COL precedes its vftable
// (the IVP sources were built without RTTI; only Freelancer's own leaves carry COLs), so the
// table is identified by its stamps: .rdata:063a53b4, written by the constructor
// FUN_06342c80 (.text:06342c9d) and the destructor FUN_06342fd0 (.text:06342fed). Seven slots
// (+0x1c is the float 1.0f DAT_063a53d0, not a slot), so it is CONCRETE:
//   +0x00 06342fc0  core_is_going_to_be_deleted_event -> delete this        (this class)
//   +0x04 0627cd20  IVP_Controller's 1.0f default
//   +0x08 06285a60  get_associated_controlled_cores                         (Independent's)
//   +0x0c 0628f050  IVP_Controller's `RET 0x8` default
//   +0x10 06342d60  do_simulation_controller -- a plain PD servo             (this class)
//   +0x14 06285a70  get_controller_priority -> 500                          (this class)
//   +0x18 06342d40  scalar deleting destructor around FUN_06342fd0
// Fields +0x04..+0x5c are this class's: FUN_06342c80 writes every one of them and
// FUN_06343050 (the golem layer) none of them.
class IVP_Controller_Motion : public IVP_Controller_Independent
{
public:
    // FUN_06342c80 (.text:06342c80-06342d3a, RET 0x8). The first argument is the
    // IVP_Real_Object, not the CObject: both call sites (through FUN_06343050) push
    // CObject+0x54 (.text:062859bd/062859c6 in RCBegin, .text:06288f9e-06288fa6 in the golem
    // attach).
    IVP_Controller_Motion(IVP_Real_Object* InBody, IVP_Controller_Motion_Parms const& Parms);

    // FUN_06342fd0 (.text:06342fd0-0634301d); scalar deleting FUN_06342d40.
    virtual ~IVP_Controller_Motion();

    // ---- IVP_Controller ----
    // Slot 0, LAB_06342fc0 -- `delete this`.
    virtual void core_is_going_to_be_deleted_event(IVP_Core* DeletedCore) override;
    // Slot 4, LAB_06342d60 (.text:06342d60-06342fb0, RET 0x8).
    virtual void do_simulation_controller(IVP_Event_Sim* EventSim,
                                          IVP_U_Vector<IVP_Core>* AffectedCores) override;
    // Slot 5, LAB_06285a70 -- 500.
    virtual IVP_CONTROLLER_PRIORITY get_controller_priority() override;

    // Members are public so the offsetof static_asserts in common_physys.cpp can reach them.
    // Offsets are retail's; the field notes are in common_physys.cpp (FIELD NOTES).
public:
    IVP_U_Point CurrentTargetPosition;  // +0x04  the pose slot 4 servoes to
    IVP_U_Quat CurrentTargetQuat;       // +0x14
    IVP_U_Point MaxTranslationForce;    // +0x24
    IVP_U_Point MaxTorque;              // +0x34
    float ForceFactor;                  // +0x44
    float DampFactor;                   // +0x48
    float TorqueFactor;                 // +0x4c
    float AngularDampFactor;            // +0x50
    IVP_Environment* Environment;       // +0x54
    IVP_Real_Object* Body;              // +0x58
    IVP_Core* Core;                     // +0x5c
};

static_assert(sizeof(IVP_Controller_Motion) == 0x60,
              "IVP_Controller_Motion is 0x60: FUN_06342c80's last field is +0x5c and "
              "FUN_06343050 starts the golem layer at +0x60 (.text:0634306b)");

// ============================================================================
// IVP_Controller_Golem -- the Ipion golem controller. RTTI-CONFIRMED NAME.
// ============================================================================
// Class Hierarchy Descriptor of CRemotePhysicsController (.rdata:063a6af8, six bases,
// array .rdata:063a6adc) lists, in order: CRemotePhysicsController (063eadf4),
// IVP_Controller_Golem (063eaeb8), IVP_Controller_Motion (063eae94),
// IVP_Controller_Independent (063eae68), the IVP_Controller root (063eacf0) and, with
// mdisp 0xd0, CRemotePhysicsSimulation (063eae40). FLControllerGolem (the leaf at
// .rdata:0639c1c8, src/Common/common_physys.cpp) is the other class built on it.
//
// Retail's own tables and code for this class, all shared by both leaves:
//   vftable .rdata:063a53dc, stamped by FUN_06343050 (.text:06343063) and by the
//   destructor FUN_06343120 (.text:06343120). Slot 7 of that table is _purecall
//   (LAB_06391fb0), so the class is abstract; each leaf supplies slot 7.
//   ctor    FUN_06343050 (.text:06343050-063430fb, RET 0x8): calls the
//           IVP_Controller_Motion constructor FUN_06342c80 with its own two arguments
//           (.text:0634305a-0634305e), then fills +0x60..+0xcc.
//   dtor    FUN_06343120 (.text:06343120-06343126): stamp 063a53dc, JMP FUN_06342fd0 (the
//           Motion destructor) -- the golem layer destroys nothing of its own.
//   slot 0  LAB_06342fc0  inherited from IVP_Controller_Motion
//   slot 1  LAB_0627cd20  inherited from IVP_Controller
//   slot 2  LAB_06285a60  inherited from IVP_Controller_Independent
//   slot 3  LAB_063435f0  this class: rebase TargetPositionTime by an IVP_Time
//   slot 4  LAB_06343150  do_simulation_controller -- the extrapolating PD servo
//                         (FLControllerGolem keeps it; CRemotePhysicsController overrides it)
//   slot 5  LAB_06285a70  inherited from IVP_Controller_Motion (500)
//   slot 6  virtual destructor
//   slot 7  pure: the leaf's "beam to the target" hook, called by slot 4 with 0/1.
//
// The layout is RETAIL's and is asserted offset by offset in common_physys.cpp.
class IVP_Controller_Golem : public IVP_Controller_Motion
{
public:
    // FUN_06343050.
    IVP_Controller_Golem(IVP_Real_Object* InBody, FLControllerGolemParms const& Parms);

    // FUN_06343120 (-> FUN_06342fd0 through the base destructor).
    virtual ~IVP_Controller_Golem();

    // FUN_06343580 (.text:06343580-063435e9).
    void set_target_position(Vector const& InTargetPosition, Vector const& InTargetSpeed,
                             IVP_Time const& Time);

    // FUN_06343670 (.text:06343670-063436f1).
    void set_target_quaternion(float const InTargetQuat[4], IVP_Time const& Time,
                               float const* InTargetRotQuatOrNull, float DeltaTime);

    // FUN_06343130 (.text:06343130-06343142, RET 0x4). One stack argument that the body
    // never reads; the body is FUN_06345f90(Body, &CurrentTargetQuat,
    // &CurrentTargetPosition, 1) -- IVP_Real_Object::beam_object_to_new_position. Called by
    // FLControllerGolem's slot 7 (.text:06288955) and by CRemotePhysicsController's slots 4
    // and 7 (.text:06292557, .text:06292598, .text:062925aa).
    void beam_to_current_target(IVP_Event_Sim const* EventSim);

    // ---- IVP_Controller ----
    // Slot 3, LAB_063435f0 (.text:063435f0-0634366b, RET 0x8).
    virtual void reset_time(IVP_Time Delta) override; // formerly slot_03
    virtual void do_simulation_controller(IVP_Event_Sim* EventSim,
                                          IVP_U_Vector<IVP_Core>* AffectedCores) override;

    // Slot 7 (+0x1c), _purecall in .rdata:063a53dc. Called from slot 4 at .text:063432ae
    // (argument 0, the position gate) and .text:06343425 (argument 1, the orientation gate).
    virtual int beam_to_target(IVP_Event_Sim const* EventSim, int Reason) = 0;

    // Members are public so the offsetof static_asserts in common_physys.cpp can reach them;
    // see the field notes there. Offsets are retail's.
public:
    float SmoothedPositionErrorSq;      // +0x60
    float TargetPositionTime[2];        // +0x64  IVP_Time halves (time_base, time_offset)
    float TargetQuatTime[2];            // +0x6c
    float InvRotDeltaTime;              // +0x74
    int bHasTargetRotQuat;              // +0x78
    IVP_U_Quat TargetQuat;              // +0x7c
    IVP_U_Quat TargetRotQuat;           // +0x8c
    IVP_U_Point TargetPosition;         // +0x9c
    IVP_U_Point TargetSpeed;            // +0xac
    float MaxDeltaPosition;             // +0xbc
    float DeltaOrientation;             // +0xc0
    float Field_0xc4;                   // +0xc4
    float Field_0xc8;                   // +0xc8
    float Field_0xcc;                   // +0xcc
};

static_assert(sizeof(IVP_Controller_Golem) == 0xd0,
              "IVP_Controller_Golem is 0xd0: FLControllerGolem is operator_new(0xd0) "
              "(.text:06288f67) and CRemotePhysicsSimulation sits at +0xd0 of "
              "CRemotePhysicsController (.text:062859ce)");

// ============================================================================
// D-cluster-06348f60 pass (2026-09-10): free functions driving an
// IVP_Controller's own associated-cores list to keep the affected
// IVP_Simulation_Unit(s) in sync. Implemented in ivp_controller.cpp -- see
// each function's own header comment there for the full evidence trail.
// Ported from Common.dll.txt:414165-415002 (decomp-tracker items 2926/2927/
// 2930/2945). Callers of these 4 functions (Common.dll.txt:06285bc7-1f,
// 06285ca6-cff, 06285cf0, 063a5580) all sit outside this cluster's own
// assigned address range and were not traced this pass.
// ============================================================================

// FUN_06348f60 (item 2926, 48 insns).
void IVP_Controller_Detach_From_All_Cores(IVP_Controller* Controller, int SkipFinalWakeCheck);

// FUN_06348fe0 (item 2927, 20 insns).
void IVP_Controller_Wake_Or_Refresh_Associated_Sim_Unit(IVP_Controller* Controller);

// FUN_06349040 (item 2930, 71 insns). See ivp_controller.cpp for the full
// discussion of EnvironmentPtrHolder's unconfirmed provenance.
void IVP_Controller_Reconcile_Associated_Cores(IVP_Environment* const* EnvironmentPtrHolder, IVP_Controller* Controller);

// FUN_06349020 / FUN_06349030 (cdecl): IVP_Controller_Manager::add_controller_to_core /
// remove_controller_from_core -- forwarders to IVP_Core::register_controller (FUN_06349180) /
// detach_controller (FUN_06349110).
void IVP_Controller_Manager_Add_Controller_To_Core(IVP_Controller* Controller, IVP_Core* Core);
void IVP_Controller_Manager_Remove_Controller_From_Core(IVP_Controller* Controller, IVP_Core* Core);

// LAB_06349530 (item 2945) is IVP_Standard_Gravity_Controller::do_simulation_controller
// (ported by CMN-PHY5 next to the environment); the raw free-function copy that stood here
// had no callers and was removed 2026-10-08 (cmn-ivp3).
