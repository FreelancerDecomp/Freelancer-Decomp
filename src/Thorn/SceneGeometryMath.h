#pragma once
#include <cmath>

namespace ThornSceneGeometry
{
    // Preserve the algorithms and publication order with portable double
    // expressions. Last-bit x87 rounding and exception timing are not emulated.
    inline void Cross(float* result, const float* a, const float* b)
    {
        const double z = static_cast<double>(a[0])*b[1] - static_cast<double>(a[1])*b[0];
        const double y = static_cast<double>(a[2])*b[0] - static_cast<double>(b[2])*a[0];
        const double x = static_cast<double>(a[1])*b[2] - static_cast<double>(a[2])*b[1];
        result[0] = static_cast<float>(x);
        result[1] = static_cast<float>(y);
        result[2] = static_cast<float>(z);
    }

    inline void Normalize(float& x, float& y, float& z, bool reverseSum = false)
    {
        const double dx = x, dy = y, dz = z;
        const double lengthSquared = reverseSum ? (dz*dz + dy*dy) + dx*dx : (dx*dx + dy*dy) + dz*dz;
        const double inverse = 1.0 / std::sqrt(lengthSquared);
        // Preserve the original zero-vector NaNs; do not introduce a fallback.
        x = static_cast<float>(inverse * dx);
        y = static_cast<float>(inverse * dy);
        z = static_cast<float>(inverse * dz);
    }

    inline void CrossSide(float* result, const float* up, const float* front, bool initial)
    {
        // These are sequential stores, unlike Cross's complete snapshot. Keep
        // source rereads so overlapping input/output has the existing behavior.
        result[2] = static_cast<float>(static_cast<double>(front[1])*up[0] - static_cast<double>(up[1])*front[0]);
        result[1] = static_cast<float>(static_cast<double>(up[2])*front[0] - static_cast<double>(front[2])*up[0]);
        const double last = initial ? static_cast<double>(front[1])*up[2] : static_cast<double>(up[2])*front[1];
        result[0] = static_cast<float>(static_cast<double>(front[2])*up[1] - last);
    }

    inline void CrossUp(float* result, const float* direction, const float* side)
    {
        CrossSide(result, direction, side, true);
    }

    inline bool AxisDotNotGreater(const float* up, const float* direction)
    {
        const double dot = (static_cast<double>(up[2])*direction[2] + static_cast<double>(up[1])*direction[1]) +
            static_cast<double>(up[0])*direction[0];
        return !(std::fabs(dot) > 0.99);
    }

    template<class Vector>
    inline void LookAtCross(Vector& result, const Vector& a, const Vector& b, bool reverse)
    {
        // The two original branches differ in sign as well as store order:
        // false computes B cross A; true computes A cross B. Snapshot B only.
        const double bx = b.X, by = b.Y, bz = b.Z;
        if (reverse)
        {
            result.Z = static_cast<float>(static_cast<double>(a.X)*by - static_cast<double>(a.Y)*bx);
            result.Y = static_cast<float>(static_cast<double>(a.Z)*bx - static_cast<double>(a.X)*bz);
            result.X = static_cast<float>(static_cast<double>(a.Y)*bz - static_cast<double>(a.Z)*by);
        }
        else
        {
            result.X = static_cast<float>(static_cast<double>(a.Z)*by - static_cast<double>(a.Y)*bz);
            result.Y = static_cast<float>(static_cast<double>(a.X)*bz - static_cast<double>(a.Z)*bx);
            result.Z = static_cast<float>(static_cast<double>(a.Y)*bx - static_cast<double>(a.X)*by);
        }
    }

    template<class Vector>
    inline void BuildLookAtFrame(const Vector& tangent, float* xform)
    {
        Vector forward = tangent;
        Normalize(forward.X,forward.Y,forward.Z,true);
        forward.Z = -forward.Z; forward.Y = -forward.Y; forward.X = -forward.X;
        const Vector upRef = {xform[1],xform[4],xform[7]};
        Vector rawY;
        LookAtCross(rawY,forward,upRef,false);
        Vector axisY = rawY;
        Normalize(axisY.X,axisY.Y,axisY.Z,true);
        const Vector existingX = {xform[0],xform[3],xform[6]};
        Vector axisZ;
        LookAtCross(axisZ,rawY,existingX,true);
        Normalize(axisZ.X,axisZ.Y,axisZ.Z);
        xform[0]=forward.X; xform[1]=axisY.X;
        xform[3]=forward.Y; xform[6]=forward.Z;
        xform[4]=axisY.Y; xform[7]=axisY.Z;
        xform[5]=-axisZ.Y; xform[2]=-axisZ.X; xform[8]=-axisZ.Z;
    }
}
