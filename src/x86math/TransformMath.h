#pragma once

#include "MatrixMath.h"

namespace X86TransformMath
{
    template<class Vector, class Transform>
    inline void Apply(Vector& dst, const Transform& transform, const Vector& source)
    {
        X86BatchMath::TransformList(&dst, transform.Rotation, &source, 1, &transform.Translation);
    }

    template<class Transform>
    inline void Compose(Transform& dst, const Transform& a, const Transform& b)
    {
        Transform result;
        X86MatrixMath::Multiply(result.Rotation, a.Rotation, b.Rotation);
        Apply(result.Translation, a, b.Translation);
        dst = result;
    }
}
