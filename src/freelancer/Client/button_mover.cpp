// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "button_mover.h"

#include "common_csys.h"
#include "globals.h"
#include "wincontrol_base.h"

#include "DALib.h"
#include "engine/engbase.h"
#include "x86math.h"

#include <math.h>

namespace
{
    // DAT_005c8a5c -- pi (0x40490fdb).
    float const kPi = 3.14159274f;

    // DAT_00679b10 -- ButtonScaler::Update's lazily acquired 3DMathEngine (FUN_005b73e0).
    I3DMathEngine* G_ButtonScalerMath = nullptr;
}

// ---------------------------------------------------------------------------------------------
// ButtonMover
// ---------------------------------------------------------------------------------------------

// FUN_0055dec0 -- vftable 0x005e18f8, empty vector, +0x18 = 0. +0x14 / +0x1c stay unwritten.
ButtonMover::ButtonMover()
    : bActive(false)
{
}

// LAB_00401e90 -- RET.
void ButtonMover::OnMoveFinished()
{
}

// LAB_0043d8a0 -- XOR AL,AL / RET 8.
bool ButtonMover::OnMoveStep(WinControl* /*Control*/, float const* /*Position*/)
{
    return false;
}

// FUN_0055dee0 -- the inlined vector insert-at-end of one {Start, End, Control} entry (grow by
// max(size, 1) when full, copy the old range, then the new entry).
void ButtonMover::AddButton(WinControl* Control, float const* Start, float const* End)
{
    FButtonMoveEntry Entry;
    Entry.Start[0] = Start[0];
    Entry.Start[1] = Start[1];
    Entry.Start[2] = Start[2];
    Entry.End[0] = End[0];
    Entry.End[1] = End[1];
    Entry.End[2] = End[2];
    Entry.Control = Control;
    Entries.push_back(Entry);
}

// FUN_0055e120 -- +0x1c = Seconds, +0x18 = 1, +0x14 = 0, then erase(begin, end).
void ButtonMover::Start(float Seconds)
{
    Duration = Seconds;
    bActive = true;
    Elapsed = 0.0f;
    Entries.erase(Entries.begin(), Entries.end());
}

// FUN_0055e170
void ButtonMover::Update()
{
    if (!bActive)
    {
        return;
    }

    // FUN_0042d680 + [+0x14] -> FST [+0x14]; FCOMP [+0x1c] / TEST AH,1 / JNZ: the snap is taken
    // when the (unrounded) sum is not below the duration.
    double const Sum = GetClientFrameTimeCapped() + static_cast<double>(Elapsed);
    Elapsed = static_cast<float>(Sum);
    if (!(Sum < static_cast<double>(Duration)))
    {
        Elapsed = Duration;
        bActive = false;
    }

    float const T = Elapsed / Duration;

    int const Count = static_cast<int>(Entries.size());
    for (int Index = 0; Index < Count; ++Index)
    {
        FButtonMoveEntry const& Entry = Entries[Index];
        float const Dz = Entry.End[2] - Entry.Start[2];
        float const DxT = (Entry.End[0] - Entry.Start[0]) * T;
        float const DyT = (Entry.End[1] - Entry.Start[1]) * T;
        float Position[3];
        Position[0] = DxT + Entry.Start[0];
        Position[1] = DyT + Entry.Start[1];
        Position[2] = Dz * T + Entry.Start[2];
        if (!OnMoveStep(Entry.Control, Position))
        {
            // vt+0xa8 slot 42 -- property 6 sets the control's position.
            Entry.Control->SetProperty(6, reinterpret_cast<uintptr_t>(Position), 0);
        }
    }

    if (!bActive)
    {
        OnMoveFinished();
    }
}

// FUN_0055e290 -- when active: clear the flag, put every control at its End (through
// OnMoveStep / SetProperty(6)), then OnMoveFinished. Inactive: nothing.
void ButtonMover::Finish()
{
    if (!bActive)
    {
        return;
    }
    bActive = false;

    int const Count = static_cast<int>(Entries.size());
    for (int Index = 0; Index < Count; ++Index)
    {
        FButtonMoveEntry const& Entry = Entries[Index];
        float Position[3];
        Position[0] = Entry.End[0];
        Position[1] = Entry.End[1];
        Position[2] = Entry.End[2];
        if (!OnMoveStep(Entry.Control, Position))
        {
            Entry.Control->SetProperty(6, reinterpret_cast<uintptr_t>(Position), 0);
        }
    }

    OnMoveFinished();
}

// ---------------------------------------------------------------------------------------------
// ButtonScaler
// ---------------------------------------------------------------------------------------------

// FUN_0055e340 -- vftable 0x005e1904, empty vector, +0x18 = 0.
ButtonScaler::ButtonScaler()
    : bActive(false)
{
}

// LAB_00401e90 -- RET.
void ButtonScaler::OnScaleFinished()
{
}

// LAB_0043d8a0 -- XOR AL,AL / RET 8.
bool ButtonScaler::OnScaleStep(WinControl* /*Control*/, float const* /*Scale*/)
{
    return false;
}

// FUN_0055e360 -- the inlined vector insert-at-end of one {From, To, Control} entry.
void ButtonScaler::AddButton(WinControl* Control, float From, float To)
{
    FButtonScaleEntry Entry;
    Entry.From = From;
    Entry.To = To;
    Entry.Control = Control;
    Entries.push_back(Entry);
}

// FUN_0055e590 -- +0x1c = Seconds, +0x18 = 1, +0x14 = 0, then erase(begin, end).
void ButtonScaler::Start(float Seconds)
{
    Duration = Seconds;
    bActive = true;
    Elapsed = 0.0f;
    Entries.erase(Entries.begin(), Entries.end());
}

// FUN_0055e5e0
void ButtonScaler::Update()
{
    if (!bActive)
    {
        return;
    }

    double const Sum = GetClientFrameTimeCapped() + static_cast<double>(Elapsed);
    Elapsed = static_cast<float>(Sum);
    if (!(Sum < static_cast<double>(Duration)))
    {
        Elapsed = Duration;
        bActive = false;
    }

    // 1 - (cos(t * pi) + 1) * 0.5 (DAT_005c8a5c pi, DAT_005c75dc 1.0, DAT_005c75e0 0.5).
    double const Phase = static_cast<double>(Elapsed / Duration) * kPi;
    float const S = static_cast<float>(1.0 - (cos(Phase) + 1.0) * 0.5);

    int const Count = static_cast<int>(Entries.size());
    for (int Index = 0; Index < Count; ++Index)
    {
        FButtonScaleEntry const& Entry = Entries[Index];
        float Scale = (Entry.To - Entry.From) * S + Entry.From;
        if (Entry.Control == nullptr)
        {
            continue;
        }
        if (OnScaleStep(Entry.Control, &Scale))
        {
            continue;
        }

        // Identity rotation, translation copied from the instance's current transform (+0x24),
        // the rotation scaled by the math engine, then SetInstanceMatrix + UpdateInstance.
        void* const Instance = Entry.Control->MeshHolder.EngineInstance;
        FTransform Transform;
        Transform.Rotation.D[0][0] = 1.0f;
        Transform.Rotation.D[0][1] = 0.0f;
        Transform.Rotation.D[0][2] = 0.0f;
        Transform.Rotation.D[1][0] = 0.0f;
        Transform.Rotation.D[1][1] = 1.0f;
        Transform.Rotation.D[1][2] = 0.0f;
        Transform.Rotation.D[2][0] = 0.0f;
        Transform.Rotation.D[2][1] = 0.0f;
        Transform.Rotation.D[2][2] = 1.0f;
        float const* const Current = DALib::Engine->GetInstanceTransform(Instance);
        Transform.Translation.X = Current[9];
        Transform.Translation.Y = Current[10];
        Transform.Translation.Z = Current[11];

        if (G_ButtonScalerMath == nullptr)
        {
            G_ButtonScalerMath = Acquire3DMathEngine();
        }
        G_ButtonScalerMath->Scale(Transform.Rotation, Transform.Rotation, Scale);

        DALib::Engine->SetInstanceMatrix(Instance, &Transform);
        DALib::Engine->UpdateInstance(Instance, 0, nullptr);
    }

    if (!bActive)
    {
        OnScaleFinished();
    }
}
