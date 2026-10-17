// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Common-internal helpers shared by the CObject-family TUs (common_cobject.cpp,
// common_engineobject.cpp, common_csimple.cpp, common_cprojectile.cpp, common_cship.cpp,
// common_csolar.cpp, common_cmine.cpp, common_cloot.cpp). Defined in common_cobject.cpp.
// Not exported.

#include "common_cobject.h"

struct IObjDB;

namespace CmnCobject
{
    // World-space vector -> object space through the transposed orientation and the cached
    // 3DMathEngine's TransformMatrix (slot +0x2c); used by the init_physics of CGuided, CMine,
    // CCounterMeasure, CLoot and CShip.
    Vector RotateWorldToObjectSpace(Matrix const& Orient, Vector const& World);

    // IObjDB slot 2 (GetZoneDb) -> Universe::ISystem slot 0: the system id used as the
    // physics collision group in the init_physics family.
    unsigned GetObjectDbCollisionGroup(IObjDB* Db);

    // Element-wise copy of the 3x3 rotation.
    void CopyMatrix9(Matrix& Dst, Matrix const& Src);
}
