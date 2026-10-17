// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "dacom.h"

#include <windows.h>
#include <cstddef>

// Tiled image interface and creation descriptor.

struct IRenderPipeline;

// Modern contract for the three operations consumed by BigImage.
// Retail provider identity and its remaining methods are unrecovered.
struct IBigImageSource : IDAComponent
{
    virtual GENRESULT DACOM_API CopyPixels(unsigned long Format, void* Buffer,
                                           unsigned long Pitch, int Arg4) = 0; // +0x0c; TODO: Arg4 meaning
    virtual GENRESULT DACOM_API GetSize(unsigned long* Width, unsigned long* Height) = 0; // +0x10
    virtual GENRESULT DACOM_API GetFormat(unsigned long* Format) = 0; // +0x14
};

// DestXy has four floats, UvQuad has eight; TextureHandle is an opaque RP8 handle.
typedef void(__cdecl* FBigImageDrawTile)(void* TextureHandle, unsigned long PackedColor,
                                         float* DestXy, float* UvQuad);

// AGGDESC size 0x2C — not the generic 0x14 DACOM descriptor.
struct FBigImageDesc
{
    unsigned long Size;           // +0x00 must be 0x2C
    const char* Name;             // +0x04 "BigImage"
    IRenderPipeline* Pipeline;  // +0x08
    IBigImageSource* ImageSource; // +0x0C optional external image provider
    unsigned long* SrcFormat;     // +0x10 requested texture format
    unsigned long* DstFormat;     // +0x14 input pixel format
    const void* Pixels;           // +0x18
    unsigned long Width;          // +0x1C
    unsigned long Height;         // +0x20
    unsigned long Pitch;          // +0x24
    FBigImageDrawTile DrawTile;    // +0x28
};

static_assert(offsetof(FBigImageDesc, Pipeline) == 0x08, "BigImage pipeline field");
static_assert(offsetof(FBigImageDesc, DrawTile) == 0x28, "BigImage callback field");
static_assert(sizeof(FBigImageDesc) == 0x2C, "BigImage descriptor extent");

struct IBigImage2 : IDAComponent
{
    // formerly Slot3; TODO: original purpose and return contract.
    virtual GENRESULT DACOM_API NoOp(void* A, void* B) = 0;

    virtual GENRESULT DACOM_API Draw(const RECT* Src, const RECT* Dest) = 0; // +0x10
    virtual GENRESULT DACOM_API GetSize(unsigned long* Width, unsigned long* Height) = 0; // +0x14
    virtual GENRESULT DACOM_API SetAlpha(float Alpha) = 0;                   // +0x18
    virtual GENRESULT DACOM_API SetColor(const float* Rgb) = 0;              // +0x1C three floats
    virtual unsigned long DACOM_API GetTileCount() = 0;                      // +0x20
    virtual GENRESULT DACOM_API GetTile(int Index, void** OutHandle, RECT* OutRect) = 0; // +0x24
    virtual GENRESULT DACOM_API SetFromBuffer(const void* Pixels, unsigned long* Format,
                                              unsigned long Width, unsigned long Height,
                                              unsigned long Pitch) = 0; // +0x28 RET 0x18; fail parms −2
    // formerly Slot11; TODO: original operation purpose.
    virtual GENRESULT DACOM_API ReturnMinusSeven(void* Unused) = 0;
};

typedef IBigImage2 IBigImage;
