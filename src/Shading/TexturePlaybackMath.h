#pragma once

#include <cmath>
#include <cstdint>

namespace TexturePlaybackMath
{
    inline double Duration(std::uint32_t frameCount, float rate)
    {
        return static_cast<double>(frameCount) / static_cast<double>(rate) - 1e-6;
    }

    // Timing uses C++ double expressions rather than explicit x87 registers.
    // The caller publishes the float time through SetPlaybackElapsed, which also
    // selects the frame. Keep the 0.001f constant and the strict reflection tests.
    inline float Advance(double elapsed, double duration, float& direction,
                         int wrapMode, float milliseconds)
    {
        const double delta = static_cast<double>(milliseconds) * static_cast<double>(0.001f);
        switch (wrapMode)
        {
        case 0:
        {
            double next = elapsed + delta;
            // Ordered tests also retain the original unordered/NaN branches.
            if (!(next < duration)) next = duration;
            if (!(next > 0.0)) next = 0.0;
            return static_cast<float>(next);
        }
        case 1:
            return static_cast<float>(std::fmod(elapsed + delta, duration));
        case 2:
        {
            const double next = elapsed + static_cast<double>(direction) * delta;
            if (direction < 0.0f && next < 0.0)
            {
                // Negate before narrowing: commuting these operations changes
                // the selected frame under directed floating-point rounding.
                const volatile double reflected = -std::fmod(next, duration);
                direction = -direction;
                return static_cast<float>(reflected);
            }
            if (direction > 0.0f && next > duration)
            {
                direction = -direction;
                return static_cast<float>(duration - std::fmod(next, duration));
            }
            return static_cast<float>(next);
        }
        default:
            return 0.0f;
        }
    }
}
