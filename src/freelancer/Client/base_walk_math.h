// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include <cmath>

enum class EBaseWalkLookMode
{
    None,
    Orbit,
    Steer
};

struct FBaseWalkMouseLook
{
    unsigned PreviousButtons = 0;
    bool bLeftBlocked = false;

    EBaseWalkLookMode Update(unsigned Buttons, bool bAvailable, bool bInteractiveTarget)
    {
        bool const bLeft = (Buttons & 1u) != 0;
        if (!bLeft)
            bLeftBlocked = false;
        else if ((PreviousButtons & 1u) == 0)
            bLeftBlocked = !bAvailable || bInteractiveTarget;
        PreviousButtons = Buttons;
        if (!bAvailable)
            return EBaseWalkLookMode::None;
        // RMB takes priority, including when both buttons are held.
        if ((Buttons & 2u) != 0)
            return EBaseWalkLookMode::Steer;
        return bLeft && !bLeftBlocked ? EBaseWalkLookMode::Orbit : EBaseWalkLookMode::None;
    }
};

struct FBaseWalkInterfaceFade
{
    float Elapsed = 0.0f;

    void Tick(float DeltaSeconds, bool bHidden)
    {
        Elapsed = bHidden ? std::fmin(1.0f, Elapsed + std::fmax(0.0f, DeltaSeconds)) : 0.0f;
    }

    float Opacity() const
    {
        // Smoothstep reaches fully transparent after one second, with soft endpoints.
        return 1.0f - Elapsed * Elapsed * (3.0f - 2.0f * Elapsed);
    }

    static unsigned long FadeColor(unsigned long Argb, float Opacity, bool bAdditive = false)
    {
        unsigned long Result = Argb & 0x00fffffful;
        if (bAdditive)
        {
            Result = 0;
            for (int Shift = 0; Shift < 24; Shift += 8)
                Result |= static_cast<unsigned long>(((Argb >> Shift) & 0xfful) * Opacity) << Shift;
        }
        return Result | (static_cast<unsigned long>((Argb >> 24) * Opacity) << 24);
    }
};

// Project beside the native room toolbar, then return to the Win3D z=-1 plane.
struct FBaseWalkIconLayout
{
    static constexpr float IconScale = 0.022f;
    static constexpr float IconHalfExtent = IconScale * 0.70710677f;
    float ButtonPosition[3];
    float HalfExtents[3];

    static FBaseWalkIconLayout Calculate(float HalfWidth, float HalfHeight,
                                         float XScale, float YScale,
                                         float ToolbarLeft, float ToolbarCenterY)
    {
        // Montage uses a diameter of IconScale * sqrt(2), so pick bounds use half of it.
        float const Radius = IconHalfExtent;
        float const HalfSizeX = Radius * XScale;
        float const HalfSizeY = Radius * -YScale;
        float const Gap = 0.008f * XScale;
        float const CenterX = std::fmax(HalfSizeX + Gap, ToolbarLeft - Gap - HalfSizeX);
        float const CenterY = std::fmax(HalfSizeY + Gap,
            std::fmin(HalfHeight * 2.0f - HalfSizeY - Gap, ToolbarCenterY));
        FBaseWalkIconLayout Layout;
        Layout.ButtonPosition[0] = (CenterX - HalfWidth) / XScale;
        Layout.ButtonPosition[1] = (CenterY - HalfHeight) / YScale;
        Layout.ButtonPosition[2] = -1.0f;
        Layout.HalfExtents[0] = Radius;
        Layout.HalfExtents[1] = Radius;
        Layout.HalfExtents[2] = 0.0f;
        return Layout;
    }
};

// Controller forward is +Z. Trent's model and cameras face along their local -Z.
struct FBaseWalkPose
{
    float Position[3] = {};
    float Yaw = 0.0f;
    float CameraYaw = 0.0f;
    float Pitch = 0.18f;

    void Look(float DeltaX, float DeltaY, bool bTurnCharacter)
    {
        CameraYaw = std::remainder(CameraYaw - DeltaX * 0.004f, 6.28318530718f);
        if (bTurnCharacter)
            Yaw = CameraYaw;
        Pitch += DeltaY * 0.004f;
        if (Pitch < -0.45f)
            Pitch = -0.45f;
        if (Pitch > 1.1f)
            Pitch = 1.1f;
    }

    bool Step(float Forward, float Strafe, bool bRunning, float DeltaSeconds)
    {
        float const Length = std::sqrt(Forward * Forward + Strafe * Strafe);
        if (Length == 0.0f || DeltaSeconds <= 0.0f)
            return false;
        float const Distance = (bRunning ? 4.2f : 1.65f) * DeltaSeconds / Length;
        float const Sin = std::sin(Yaw);
        float const Cos = std::cos(Yaw);
        Position[0] += (Sin * Forward - Cos * Strafe) * Distance;
        Position[2] += (Cos * Forward + Sin * Strafe) * Distance;
        return true;
    }

    void CharacterOrientation(float* Out) const
    {
        float const Sin = std::sin(Yaw);
        float const Cos = std::cos(Yaw);
        float const Matrix[9] = {-Cos, 0.0f, -Sin, 0.0f, 1.0f, 0.0f, Sin, 0.0f, -Cos};
        for (int Index = 0; Index < 9; ++Index)
            Out[Index] = Matrix[Index];
    }

    void CameraTransform(float* Out, float Distance = 3.0f) const
    {
        float const Sin = std::sin(CameraYaw);
        float const Cos = std::cos(CameraYaw);
        float const SinPitch = std::sin(Pitch);
        float const CosPitch = std::cos(Pitch);
        float const Matrix[9] = {
            -Cos, Sin * SinPitch, -Sin * CosPitch,
            0.0f, CosPitch, SinPitch,
            Sin, Cos * SinPitch, -Cos * CosPitch};
        for (int Index = 0; Index < 9; ++Index)
            Out[Index] = Matrix[Index];
        Out[9] = Position[0] + Matrix[2] * Distance;
        Out[10] = Position[1] + 1.35f + Matrix[5] * Distance;
        Out[11] = Position[2] + Matrix[8] * Distance;
    }
};
