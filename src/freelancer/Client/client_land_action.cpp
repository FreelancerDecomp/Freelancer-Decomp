// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "client_land_action.h"

#include "client_gameobj.h"
#include "common_archetype.h"   // Archetype::EqObj::bDockingCamera
#include "common_cobject.h"     // CEqObj, CShip
#include "jump_effect.h"        // FJumpShipEffect (the Ship's +0x188)
#include "space_state.h"        // SetPendingLandBase (FUN_00548ef0)
#include "player_behavior.h"    // GetPlayerObjRW (FUN_0054baf0)

#include <cmath>
#include <cstring>

static_assert(sizeof(Action_Land) == 0x50, "Action_Land is IAction::operator new(0x50)");
static_assert(sizeof(Action_JumpOut) == 0x64, "Action_JumpOut is IAction::operator new(0x64)");

namespace
{
    // DAT_00678b68 -- ship.cpp's 3DMathEngine, FUN_005b73e0 on first use at every site
    // (0x00543034, 0x00543056, 0x00543078, 0x0054322f, 0x005432dc, 0x00546167).
    I3DMathEngine* G_ShipMath = nullptr;

    I3DMathEngine* ShipMath()
    {
        if (G_ShipMath == nullptr)
            G_ShipMath = Acquire3DMathEngine();
        return G_ShipMath;
    }

    // DAT_005c75dc: the eased fraction's cap (0x005431fa).
    constexpr float kJumpOutMaxFraction = 1.0f;

    // DAT_005dfb34: the landing run's length in seconds (0x00542dbc, 0x00542e3a; the Ship's
    // land request hands the same constant to the docking camera, 0x00543cc7).
    constexpr float kLandSeconds = 3.0f;
    // DAT_005d440c: the speed gained per second of the run (0x00542e4e).
    constexpr float kLandAcceleration = 50.0f;
    // DAT_005ca230: the speed cap (0x00542e57 / 0x00542e66).
    constexpr float kLandMaxSpeed = 100.0f;
}

I3DMathEngine* GetShipMathEngine()
{
    // DAT_00678b68, filled by FUN_005b73e0 on first use (the ship draw's site 0x00546167).
    return ShipMath();
}

Action_Land::Action_Land(FClientShipGameobj* InShip, CEqObj* Dock)
{
    // FUN_00542b70 (RET 8). IAction() stores +4 = 0 (0x00542b8f), PhySys::Controller() builds
    // the +8 face (0x00542ba6); +0x20 = 0, +0x18 = the Ship (0x00542bc3 / 0x00542bc6). With no
    // Ship nothing else is written.
    Elapsed = 0.0f;
    Gameobj = InShip;
    if (Gameobj == nullptr)
        return;

    // 0x00542bcf..0x00542bef: the Ship's object narrowed to a CShip ((class & 0x503) == 0x503),
    // null otherwise; none of the calls below tests it.
    CObject* const Object = Gameobj->Object;
    CShip* Ship = nullptr;
    if (Object != nullptr && (static_cast<unsigned>(Object->ObjectClass) & 0x503u) == 0x503u)
        Ship = static_cast<CShip*>(Object);

    // 0x00542bf1..0x00542c22: this controller on the ship, collisions off, and control exclusion
    // 7 -- with a set_control_exclusion(0) first when 7 is already excluded.
    PhySys::Controller::Register(this, Ship);
    Ship->enable_collisions_r(false);
    if (Ship->is_control_excluded(7))
        Ship->set_control_exclusion(0);
    Ship->set_control_exclusion(7);

    // 0x00542c24..0x00542c3e: the dock's base id, and its launch point 0 -- the position is
    // dropped, the orientation lands in +0x2c.
    BaseId = Dock->DockingPointBaseId;
    Vector LaunchPos;
    Dock->launch_pos(LaunchPos, Orient, 0);

    // 0x00542c44..0x00542ca7: columns 0 and 2 negated (elements 0, 3, 6 and 2, 5, 8) -- the
    // launch orientation turned half way round its Y axis, facing into the dock.
    Orient.E[0][0] = -Orient.E[0][0];
    Orient.E[1][0] = -Orient.E[1][0];
    Orient.E[2][0] = -Orient.E[2][0];
    Orient.E[0][2] = -Orient.E[0][2];
    Orient.E[1][2] = -Orient.E[1][2];
    Orient.E[2][2] = -Orient.E[2][2];

    // 0x00542caa..0x00542ccd: the Ship gameobj's speed (vt+4 get_velocity), x*x + y*y + z*z.
    Vector const Velocity = Gameobj->get_velocity();
    Speed = std::sqrt(Velocity.x * Velocity.x + Velocity.y * Velocity.y + Velocity.z * Velocity.z);

    // 0x00542cd0..0x00542ce7: the dock archetype's docking_camera byte (+0x88 -> +0x81), SETZ.
    bNoDockingCamera = !Dock->eqobjarch()->bDockingCamera;

    // 0x00542cea: the throttle to zero.
    Ship->set_throttle(0.0f);
}

Action_Land::~Action_Land()
{
    // FUN_00542d40: both vtables restamped, PhySys::Controller::~Controller on the +8 face, then
    // IAction::~IAction -- the compiler's own sequence for this class.
}

char const* Action_Land::get_id() const
{
    // LAB_00542d10.
    return "Land";
}

bool Action_Land::update(float Dt)
{
    // LAB_00542da0 (RET 4). 0x00542da0..0x00542db0: the clock advances even with no Ship.
    Elapsed += Dt;
    if (Gameobj == nullptr)
        return true;

    // 0x00542db5..0x00542dc9: without a docking camera the landing ends at once; with one it
    // waits for the run (FCOMP 3.0 / TEST AH,0x1 / JNZ: below 3 or unordered keeps waiting).
    if (!bNoDockingCamera && !(Elapsed >= kLandSeconds))
        return true;

    // 0x00542dcd..0x00542dfc: the player's Ship asks the space frame for the base
    // (FUN_00548ef0); any other Ship goes through slot 87 (vt+0x15c) with true. The action stays
    // on the list either way.
    if (static_cast<void*>(static_cast<IObjRW*>(Gameobj)) == GetPlayerObjRW())
        SetPendingLandBase(&BaseId);
    else
        Gameobj->Slot15c(true);
    return true;
}

void Action_Land::do_simulation_controller(IVP_Event_Sim* EventSim, IVP_U_Vector<IVP_Core>* CoreList)
{
    // LAB_00542e10 (RET 8); CoreList is never read.
    (void)CoreList;
    if (Gameobj == nullptr)
        return;

    // 0x00542e22: the Ship gameobj's object (slot 84, vt+0x150), fetched before the tests.
    CObject* const Object = const_cast<CObject*>(Gameobj->cobject());

    // 0x00542e2c..0x00542e45: only a docking-camera landing, and only during the run
    // (FCOMP 3.0 / TEST AH,0x5 / JP: at or past 3, or unordered, stops).
    if (bNoDockingCamera)
        return;
    if (!(Elapsed < kLandSeconds))
        return;

    // 0x00542e4b..0x00542e6c: speed = elapsed * 50 + the speed at the start, at most 100
    // (TEST AH,0x41 / JNZ keeps the value unless it is greater).
    float RunSpeed = Elapsed * kLandAcceleration + Speed;
    if (RunSpeed > kLandMaxSpeed)
        RunSpeed = kLandMaxSpeed;

    // 0x00542e6c..0x00542ec8: this step's move, along the negated column 2 of +0x2c, times the
    // speed and the event's delta_time.
    float const Dt = EventSim->delta_time;
    float const StepX = (-Orient.E[0][2] * RunSpeed) * Dt;
    float const StepY = (-Orient.E[1][2] * RunSpeed) * Dt;
    float const StepZ = (RunSpeed * -Orient.E[2][2]) * Dt;

    // 0x00542eca..0x00542efb: added to the gameobj's position (vt+0), and the object beamed
    // there with its own orientation (+0x08), not attached (CObject vt+0x7c beam_object).
    Vector const& Position = Gameobj->get_position();
    Vector NewPosition;
    NewPosition.z = StepZ + Position.z;
    NewPosition.y = StepY + Position.y;
    NewPosition.x = StepX + Position.x;
    Object->beam_object(NewPosition, Object->Orientation, false);
}

Action_JumpOut::Action_JumpOut(FClientShipGameobj* InShip, float InDuration, unsigned InGateType)
{
    // FUN_00542f10 (RET 0xc). IAction() stores +4 = 0 (0x00542f33), PhySys::Controller() builds
    // the +8 face (0x00542f4e); then +0x24 = Ship, +0x60 = 0, +0x1c = 0, +0x20 = Duration,
    // +0x18 = GateType (0x00542f73..0x00542f8d).
    Ship = InShip;
    bDone = false;
    Elapsed = 0.0f;
    Duration = InDuration;
    GateType = InGateType;

    // 0x00542f90..0x00542fb9: only a ship whose object has all of class 0x103; otherwise the
    // rest of the object stays unwritten, as retail leaves it.
    if (Ship == nullptr)
        return;
    CObject* const Object = Ship->GetObject();
    if (Object == nullptr || (static_cast<unsigned>(Object->ObjectClass) & 0x103u) != 0x103u)
        return;

    // 0x00542fbf..0x00542ff1: collisions off; the IObjAffect face (+4) slot 0 set_throttle(1.0f)
    // and slot 1 set_axis_throttle({0,0,0}).
    Object->enable_collisions_r(false);
    static_cast<IObjAffect*>(Ship)->set_throttle(1.0f);
    Vector ZeroAxis;
    ZeroAxis.x = 0.0f;
    ZeroAxis.y = 0.0f;
    ZeroAxis.z = 0.0f;
    static_cast<IObjAffect*>(Ship)->set_axis_throttle(ZeroAxis);

    // 0x00542ff4..0x00543032: the ship's slot 4 (vt+0x10 get_orientation, ICF-folded with
    // get_transform: the pointer is the whole Transform, 12 dwords copied) into both the start
    // and the end frame; with a jump effect (+0x188) the end frame is its +0xa8 instead.
    Transform StartXform;
    Transform EndXform;
    Transform const& ShipXform =
        reinterpret_cast<Transform const&>(Ship->get_orientation());
    std::memcpy(&StartXform, &ShipXform, sizeof(Transform));
    std::memcpy(&EndXform, &ShipXform, sizeof(Transform));
    FJumpShipEffect* const Effect = static_cast<FJumpShipEffect*>(Ship->Field_0x188);
    if (Effect != nullptr)
        std::memcpy(&EndXform, &Effect->Field_0xa8, sizeof(Transform));

    // 0x00543034..0x00543098: MatrixToQuaternion (vt+0x50) on the start frame into +0x28, on
    // the end frame into +0x44, and once more on the start frame into a local nothing reads.
    ShipMath()->MatrixToQuaternion(StartRotation,
                                   reinterpret_cast<FMatrix const&>(StartXform.Rotation));
    ShipMath()->MatrixToQuaternion(EndRotation,
                                   reinterpret_cast<FMatrix const&>(EndXform.Rotation));
    FQuaternion Unused;
    ShipMath()->MatrixToQuaternion(Unused, reinterpret_cast<FMatrix const&>(StartXform.Rotation));

    // 0x0054309b..0x005430d4: the two positions.
    StartPosition = StartXform.Translation;
    EndPosition = EndXform.Translation;

    // 0x005430cf..0x005430d7: this controller registered on the object.
    PhySys::Controller::Register(this, Object);
}

Action_JumpOut::~Action_JumpOut()
{
    // FUN_00543130: both vtables restamped, PhySys::Controller::~Controller on the +8 face, then
    // IAction::~IAction -- the compiler's own sequence for this class.
}

char const* Action_JumpOut::get_id() const
{
    // LAB_00543100.
    return "JumpOut";
}

bool Action_JumpOut::update(float Dt)
{
    // FUN_00543190 (RET 4): while not done, Controller::wakeup on the +8 face; the action stays
    // until the controller has marked it done (SETZ on +0x60, re-read after the call).
    (void)Dt;
    if (!bDone)
        PhySys::Controller::wakeup();
    return !bDone;
}

void Action_JumpOut::do_simulation_controller(IVP_Event_Sim* EventSim,
                                              IVP_U_Vector<IVP_Core>* CoreList)
{
    // LAB_005431c0 (RET 8); CoreList is never read.
    (void)CoreList;

    // 0x005431c0..0x005431da: the clock advances by the step's delta_time (+0); past the
    // duration (FCOM / TEST AH,0x41 / JNZ: only strictly greater falls through) the action is
    // done and nothing moves.
    double const Clock = static_cast<double>(EventSim->delta_time) + static_cast<double>(Elapsed);
    Elapsed = static_cast<float>(Clock);
    if (Clock > static_cast<double>(Duration))
    {
        bDone = true;
        return;
    }

    // 0x005431e9..0x0054320f: t = clock / duration, eased to t * t (stored as a float), capped at
    // 1.0f (TEST AH,0x41 / JNZ keeps it unless it is greater).
    double const Fraction = Clock / static_cast<double>(Duration);
    float Eased = static_cast<float>(Fraction * Fraction);
    if (Eased > kJumpOutMaxFraction)
        Eased = kJumpOutMaxFraction;

    // 0x0054320f..0x0054326e: a gate type with bit 0x800 keeps the start orientation;
    // otherwise QuatNlerp (vt+0x58) from +0x28 to +0x44 by the eased fraction.
    FQuaternion Rotation;
    if ((GateType & 0x800u) != 0)
    {
        Rotation = StartRotation;
    }
    else
    {
        FQuaternion Nlerped;
        ShipMath()->QuatNlerp(Nlerped, StartRotation, EndRotation, Eased);
        Rotation = Nlerped;
    }

    // 0x00543272..0x005432b9: position = start + (end - start) * eased, per axis.
    Vector Position;
    Position.x = (EndPosition.x - StartPosition.x) * Eased + StartPosition.x;
    Position.y = (EndPosition.y - StartPosition.y) * Eased + StartPosition.y;
    Position.z = (EndPosition.z - StartPosition.z) * Eased + StartPosition.z;

    // 0x005432a8..0x00543312: the ship's object, when it has all of class 0x503: the rotation
    // back to a matrix (vt+0x54) and the object beamed there, not attached (vt+0x7c).
    if (Ship == nullptr)
        return;
    CObject* const Object = Ship->GetObject();
    if (Object == nullptr || (static_cast<unsigned>(Object->ObjectClass) & 0x503u) != 0x503u)
        return;
    Matrix Orientation;
    ShipMath()->QuaternionToMatrix(reinterpret_cast<FMatrix&>(Orientation), Rotation);
    Object->beam_object(Position, Orientation, false);
}
