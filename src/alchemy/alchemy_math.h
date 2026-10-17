// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include <cstdint>

namespace AlchemyImpl
{
    double ComputeAlchemyFpuSine(const double& Angle);
    double ComputeAlchemyFpuCosine(const double& Angle);
    double ComputeAlchemyFpuCosineAndClear(const double& Angle, float& ZeroOut);
    double ComputeAlchemyFpuSquareRoot(const double& Value);
    double ComputeAlchemyFpuWideInteger(const std::int64_t& Value);
    void StoreAlchemyFpuFloat(float& Out, const double& Value);
    std::int64_t ConvertAlchemyFpuInteger(const double& Value);
}
