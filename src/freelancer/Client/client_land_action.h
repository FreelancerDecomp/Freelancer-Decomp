// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// freelancer.exe ship.cpp -- retail E:\FL\Scratch\Source\Client\space\ship.cpp,
// .text:00542b70-00542f03: Action_Land, the IAction a client Ship's action list runs after the
// Ship asked to land on a dock (FClientShipGameobj::RequestLand, FUN_00543b40). It also rides
// the ship's physics as a PhySys::Controller and flies the ship into the dock for three seconds.
//
//   struct Action_Land : IAction, PhySys::Controller   sizeof 0x50 (IAction::operator new(0x50)
//                                                          at 0x00543b7f), RTTI .?AUAction_Land@@
//   IAction face    .rdata:005dfc98 = {LAB_00542d10 get_id "Land", LAB_00542da0 update,
//                                      FUN_00542d20 deleting dtor}
//   Controller face .rdata:005dfc78 = {Controller thunks 005b734a / 005b7344 / 005b733e,
//                                      IVP defaults 0043d930 (1.0f) / 0053aaa0 (RET 8),
//                                      LAB_00542e10 do_simulation_controller,
//                                      LAB_00547400 (SUB ECX,8 -> FUN_00542d20)}

#include "common_cequip.h" // IAction
#include "common_csys.h"   // Matrix
#include "common_physys.h" // PhySys::Controller, IVP_Event_Sim
#include "x86math.h"       // FQuaternion

class FClientGameobj;
class FClientShipGameobj;
struct CEqObj;

struct Action_Land : public IAction, public PhySys::Controller
{
public:
    // FUN_00542b70 (thiscall, RET 8). Ship is the landing Ship gameobj, Dock the dock's object
    // narrowed to a CEqObj ((class & 0x303) == 0x303) by the caller.
    Action_Land(FClientShipGameobj* InShip, CEqObj* Dock);
    // FUN_00542d40 (body) / FUN_00542d20 (deleting): PhySys::Controller's destructor, then
    // IAction's; IAction::operator delete when bit 0 is set.
    ~Action_Land() override;

    // LAB_00542d10: "Land".
    char const* get_id() const override;
    // LAB_00542da0 (RET 4). Always true: the action list never drops it.
    bool update(float Dt) override;

protected:
    // LAB_00542e10 (RET 8) -- the Controller face's slot 4.
    void do_simulation_controller(IVP_Event_Sim* EventSim,
                                  IVP_U_Vector<IVP_Core>* CoreList) override;

private:
    FClientGameobj* Gameobj;   // +0x18  the landing Ship gameobj (the ctor's first argument)
    unsigned BaseId;           // +0x1c  the dock's CEqObj::DockingPointBaseId (+0x164)
    float Elapsed;             // +0x20  0 at construction, + Dt every update
    float Speed;               // +0x24  |velocity| at construction
    bool bNoDockingCamera;     // +0x28  !dock->eqobjarch()->bDockingCamera (+0x81)
    Matrix Orient;             // +0x2c  the dock's launch orientation, columns 0 and 2 negated
};

// freelancer.exe ship.cpp .text:00542f10-00543316: Action_JumpOut, the IAction the Ship's jump
// out (FClientShipGameobj::BeginJumpOut, FUN_00543530) puts on its action list. As a
// PhySys::Controller it swings the ship from where it was to the gate's jump-out hardpoint over
// the gate's JumpOutTime, easing in (t squared), then marks itself done; its update keeps the
// controller awake until then and drops the action after.
//
//   struct Action_JumpOut : IAction, PhySys::Controller   sizeof 0x64 (IAction::operator
//                                                            new(0x64) at 0x005435d2)
//   IAction face    .rdata:005dfcc8 = {LAB_00543100 get_id "JumpOut", FUN_00543190 update,
//                                      FUN_00543110 deleting dtor}
//   Controller face .rdata:005dfca8 = {Controller thunks 005b734a / 005b7344 / 005b733e,
//                                      IVP defaults 0043d930 / 0053aaa0,
//                                      LAB_005431c0 do_simulation_controller,
//                                      LAB_005473f0 (SUB ECX,8 -> FUN_00543110)}
struct Action_JumpOut : public IAction, public PhySys::Controller
{
public:
    // FUN_00542f10 (thiscall, RET 0xc). Duration is the ship effect's JumpOutTime (+4) and
    // GateType the gate solar's get_type (0 without a gate).
    Action_JumpOut(FClientShipGameobj* InShip, float InDuration, unsigned InGateType);
    // FUN_00543130 (body) / FUN_00543110 (deleting).
    ~Action_JumpOut() override;

    // LAB_00543100: "JumpOut".
    char const* get_id() const override;
    // FUN_00543190 (RET 4). Dt is not read.
    bool update(float Dt) override;

protected:
    // LAB_005431c0 (RET 8) -- the Controller face's slot 4.
    void do_simulation_controller(IVP_Event_Sim* EventSim,
                                  IVP_U_Vector<IVP_Core>* CoreList) override;

private:
    unsigned GateType;              // +0x18  bit 0x800: keep the start orientation (no slerp)
    float Elapsed;                  // +0x1c  0 at construction, + delta_time every sim step
    float Duration;                 // +0x20
    FClientShipGameobj* Ship;       // +0x24
    FQuaternion StartRotation;      // +0x28  the ship's orientation at construction
    Vector StartPosition;           // +0x38  the ship's position at construction
    FQuaternion EndRotation;        // +0x44  the jump effect's +0xa8 orientation (else the ship's)
    Vector EndPosition;             // +0x54  the jump effect's +0xa8 position (else the ship's)
    bool bDone;                     // +0x60  set once Elapsed passes Duration
};

// FClientShipGameobj::RequestLand is declared in client_gameobj.h (FUN_00543b40).

// DAT_00678b68 -- ship.cpp's 3DMathEngine, acquired (FUN_005b73e0) on first use; shared by the
// actions above and the Ship draw LAB_00546050 (0x00546167).
I3DMathEngine* GetShipMathEngine();
