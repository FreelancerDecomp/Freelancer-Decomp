// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "dacom.h"

// Aggregate that loads and initializes the components in the System profile section.
struct ISystemContainer : IAggregateComponent
{
    // +0x10 is also ISystemComponent::Shutdown on the shared vtable (src/system/system.cpp).
    virtual GENRESULT DACOM_API Shutdown() = 0;                  // +0x10
    virtual GENRESULT DACOM_API LoadSystemComponents() = 0;      // +0x14
    virtual GENRESULT DACOM_API UnloadSystemComponents() = 0;    // +0x18
    virtual GENRESULT DACOM_API AddComponent(AGGDESC* Desc) = 0; // +0x1C
};
