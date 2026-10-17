// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "ThornControlCalls.h"

void ThornSetReverbProperties(IReverbControl* Control,
                              const FThornReverb4* Properties)
{
    Control->SetReverb(Properties->Environment, Properties->Volume,
                       Properties->Decay, Properties->Damping);
}

void ThornSetFloorHeight(ICompoundControl* Control, const float* Height)
{
    Control->SetFloorHeight(*Height);
}
