// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// EngineObject: the IEngineInstance base of every CObject (engine instance id, transform,
// radius, instance flags, velocities).
#include "common_cobject.h"
#include "common_cobject_internal.h" // CmnCobject::CopyMatrix9

#include "DALib.h"
#include "dacom.h"

#include <string.h>

using CmnCobject::CopyMatrix9;

namespace
{

    // EngineObject::update_tree .text:0627caf0..0627cb05: IEngine +0x6c UpdateInstance(
    // instance, 0, 0) -- no test on DALib::Engine.
    void UpdateEngineInstance(long Instance)
    {
        DALib::Engine->UpdateInstance((void*)Instance, 0, nullptr);
    }
}

EngineObject::EngineObject()
{
    // Common.dll.c 0xa98a0 вЂ” identity diagonal, instance -1.
    EngineInstanceId = -1;
    memset(&Orientation, 0, sizeof(Orientation));
    Orientation.E[0][0] = 1.0f;
    Orientation.E[1][1] = 1.0f;
    Orientation.E[2][2] = 1.0f;
    WorldPosition.x = 0.0f;
    WorldPosition.y = 0.0f;
    WorldPosition.z = 0.0f;
    Radius = 0.0f;
    RadiusCenter.x = 0.0f;
    RadiusCenter.y = 0.0f;
    RadiusCenter.z = 0.0f;
    InstanceFlags = 0;
}

EngineObject::~EngineObject()
{
}

void DACOM_API EngineObject::initialize_instance(long Inst)
{
    // Listing 0xa9910. this+4 = FEngineInstance*.
    EngineInstanceId = (int)Inst;
}

void DACOM_API EngineObject::create_instance(long Inst)
{
    // Listing 0x2f050 RET. EngBase slot 1 = AddRef stub.
    (void)Inst;
}

void DACOM_API EngineObject::destroy_instance(long Inst)
{
    // Listing 0xa9920. Clears this+4 only.
    (void)Inst;
    EngineInstanceId = -1;
}

void DACOM_API EngineObject::set_position(long Inst, Vector const& Pos)
{
    (void)Inst;
    set_position(Pos);
}

Vector const& DACOM_API EngineObject::get_position(long Inst) const
{
    (void)Inst;
    return WorldPosition;
}

void DACOM_API EngineObject::set_orientation(long Inst, Matrix const& Orient)
{
    (void)Inst;
    set_orientation(Orient);
}

Matrix const& DACOM_API EngineObject::get_orientation(long Inst) const
{
    (void)Inst;
    return Orientation;
}

void DACOM_API EngineObject::set_transform(long Inst, Transform const& Xform)
{
    (void)Inst;
    set_transform(Xform);
}

Transform const& DACOM_API EngineObject::get_transform(long Inst) const
{
    (void)Inst;
    return get_transform();
}

void DACOM_API EngineObject::get_centered_radius(long Inst, float* OutRadius,
                                                 Vector* OutCenter) const
{
    // .text:06309a30..06309a54: *OutRadius = [this+0x38], *OutCenter = [this+0x3c..+0x44];
    // Inst unused, no null tests on the outputs.
    (void)Inst;
    *OutRadius = Radius;
    *OutCenter = RadiusCenter;
}

void DACOM_API EngineObject::set_centered_radius(long Inst, float NewRadius,
                                                 Vector const& Center)
{
    (void)Inst;
    Radius = NewRadius;
    RadiusCenter = Center;
}

void DACOM_API EngineObject::set_instance_flags(long Inst, unsigned long Flags)
{
    (void)Inst;
    InstanceFlags = Flags;
}

unsigned long DACOM_API EngineObject::get_instance_flags(long Inst) const
{
    (void)Inst;
    return InstanceFlags;
}

bool DACOM_API EngineObject::joint_changed(long Inst)
{
    (void)Inst;
    return false;
}

Vector const& DACOM_API EngineObject::get_velocity(long Inst) const
{
    // Listing 0xa99b0. Always the zero vector at this level.
    (void)Inst;
    static const Vector Zero = {0.0f, 0.0f, 0.0f};
    return Zero;
}

void DACOM_API EngineObject::set_velocity(long Inst, Vector const& Vel)
{
    // .text:062a8910: RET 0xc.
    (void)Inst;
    (void)Vel;
}

Vector const& DACOM_API EngineObject::get_angular_velocity(long Inst) const
{
    // Listing 0xa99f0. Always the zero vector at this level.
    (void)Inst;
    static const Vector Zero = {0.0f, 0.0f, 0.0f};
    return Zero;
}

void DACOM_API EngineObject::set_angular_velocity(long Inst, Vector const& AngVel)
{
    // .text:062a8910: RET 0xc (the same body as set_velocity).
    (void)Inst;
    (void)AngVel;
}

void EngineObject::set_position(Vector const& Pos)
{
    // Listing 0x1cb10.
    WorldPosition = Pos;
}

void EngineObject::set_orientation(Matrix const& Orient)
{
    // Listing 0x1cb30. 9 floats to this+8.
    CopyMatrix9(Orientation, Orient);
}

void EngineObject::set_transform(Transform const& Xform)
{
    // Listing 0x1cb50. 12 floats to this+8 (3Г—3 then pos).
    CopyMatrix9(Orientation, Xform.Rotation);
    WorldPosition = Xform.Translation;
}

Transform const& EngineObject::get_transform() const
{
    // this+0x08: Orientation followed by WorldPosition, one Transform.
    return *(Transform const*)&Orientation;
}

Matrix const& EngineObject::get_orientation() const
{
    // .text:06263b80: LEA EAX,[this+0x8]; RET.
    return Orientation;
}

Vector const& EngineObject::get_position() const
{
    // .text:06356500: LEA EAX,[this+0x2c]; RET.
    return WorldPosition;
}

long EngineObject::get_index() const
{
    // .text:062d1ad0: MOV EAX,[this+0x4]; RET.
    return EngineInstanceId;
}

float const EngineObject::get_radius() const
{
    return Radius;
}

void EngineObject::update_tree() const
{
    // Listing 0x1caf0. IEngine+0x6c(this+4, 0, 0).
    UpdateEngineInstance(EngineInstanceId);
}

void EngineObject::set_centered_radius(float NewRadius, Vector const& Center)
{
    // Listing 0x1cb80: (**(this->vtbl + 0x28))(this, EngineInstanceId, NewRadius, Center).
    // Vtable slot 0x28 is IEngineInstance::set_centered_radius(Inst, Radius, Center),
    // so this is exactly a polymorphic call to that overload with EngineInstanceId.
    set_centered_radius(EngineInstanceId, NewRadius, Center);
}
