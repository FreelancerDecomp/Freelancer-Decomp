// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "dacom.h"
#include "engine/engbase.h"

// Instance values, lighting, override materials and appearance drawing.
struct IFLAppearance : IDAComponent
{
    virtual GENRESULT DACOM_API SetInstanceValue(int Id, void* Value) = 0;
    virtual GENRESULT DACOM_API GetInstanceValue(int Id, void** Out) = 0;
    virtual GENRESULT DACOM_API RemoveInstanceValue(int Key) = 0;
    virtual GENRESULT DACOM_API SetDirectionalLight(const float* First, const float* Second) = 0;
    virtual GENRESULT DACOM_API SetDirectionalLightEnabled(unsigned char Flag) = 0;
    virtual GENRESULT DACOM_API GetOverrideMaterial(unsigned long Index, void** Out) = 0;
    // +0x24/+0x28: Camera and engine instance passed to Renderer2 with the
    // Backdrop (066fe270) / Starsphere (066fe570) override material.
    virtual GENRESULT DACOM_API DrawBackdrop(void* A, void* Extra) = 0;
    virtual GENRESULT DACOM_API DrawStarsphere(void* A, void* Extra) = 0;
    virtual GENRESULT DACOM_API DrawFadingShadows(void* A, void* B, void* C, void* D) = 0;
    virtual GENRESULT DACOM_API DrawFixedShadows(void* A, void* B, void* C, void* D) = 0;
    virtual GENRESULT DACOM_API CountVisibleGeometry(void* A, void* B, void* C) = 0;
};

struct FAggDesc18
{
    unsigned long Size;
    const char* NameAt4;
    void* OuterAt8;
    void** InnerOutAtC;
    const char* ValueAt10;
    IDAComponent* SystemAt14;
};
