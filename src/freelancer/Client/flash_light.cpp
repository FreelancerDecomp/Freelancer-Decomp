// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "flash_light.h"

#include "engine_light.h"   // ScaleVec3 (FUN_00411d60)
#include "game_options.h"
#include "misc_util.h"      // MiscUtilLoadTextureLibrary (FUN_0041c7e0)
#include "win3d_immediate.h"
#include "win3d_render.h"   // Win3DApplyCamera (FUN_00422950)

#include "common_goods.h"
#include "common_misc.h"    // ICamera
#include "DALib.h"
#include "dacom.h"          // DACOM_CRC::GetCRC32
#include "render/shading.h" // ITextureLibrary2

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

// DAT_006799c0 / DAT_006799c4 -- the "bulb" and "shine" textures of fx\efx.txm, the
// ITextureLibrary2 records slot 12 hands out. Written by FUN_00557550 / FUN_005575e0, read by
// the flare passes of FUN_00558000 (0x005580f3 / 0x005580a1).
void* G_FlashlightBulbTexture = nullptr;
void* G_FlashlightShineTexture = nullptr;

namespace
{
    // The {name, CRC32} query TextureLib slot +0x30 reads (Query+4 = CRC).
    struct FFlareTextureQuery
    {
        char const* Name;  // +0
        unsigned long Crc; // +4
    };

    // DAT_006799c8 -- set by FUN_00558580 when a set holds more than 0x20 lights.
    bool G_FlashlightBatchOverflow = false;

    // DAT_005caeec (1 / 32768), DAT_005da4c0 (0.2), DAT_005da4b8 (0.9).
    float const kRandScale = 3.0517578125e-05f;
    double const kJitterSpan = 0.20000004768371582;
    double const kJitterBase = 0.8999999761581421;

    float CompanionFloat(Archetype::ICliObj* Companion, unsigned Offset)
    {
        return *reinterpret_cast<float const*>(reinterpret_cast<unsigned char const*>(Companion) +
                                               Offset);
    }

    Vector CompanionVector(Archetype::ICliObj* Companion, unsigned Offset)
    {
        return *reinterpret_cast<Vector const*>(reinterpret_cast<unsigned char const*>(Companion) +
                                                Offset);
    }

    char const* const kLightsCameraCpp = "E:\\FL\\Scratch\\Source\\Client\\supt\\LightsCamera.cpp";

    // FUN_00557d00 (thiscall on the vector, no arguments) -- normalise in place with the
    // 0x5f3759df inverse square root and one Newton step: LenSq = (x*x + y*y) + z*z stored as
    // float, Y0 = 0x5f3759df - (bits(LenSq) SAR 1), R = (DAT_005ca220 (1.5f) - Y0 * Y0 * LenSq *
    // DAT_005c75e0 (0.5f)) * Y0 kept on the FPU, each component multiplied by it.
    void FlashLightNormalizeFast(float* V)
    {
        float const LenSq = static_cast<float>(
            (static_cast<double>(V[0]) * V[0] + static_cast<double>(V[1]) * V[1]) +
            static_cast<double>(V[2]) * V[2]);
        int Bits = 0;
        memcpy(&Bits, &LenSq, sizeof(Bits));
        int const GuessBits = 0x5f3759df - (Bits >> 1);
        float Guess = 0.0f;
        memcpy(&Guess, &GuessBits, sizeof(Guess));
        double const Scale =
            (1.5 - static_cast<double>(Guess) * Guess * LenSq * 0.5) * static_cast<double>(Guess);
        V[0] = static_cast<float>(Scale * V[0]);
        V[1] = static_cast<float>(Scale * V[1]);
        V[2] = static_cast<float>(Scale * V[2]);
    }

    // The quad both flare passes emit -- FUN_005579b0 0x00557b47..0x00557cee and FUN_00557d70
    // 0x00557e4c..0x00557ff0 are the same instruction sequence on their own stack slots.
    // Corners Center - R + U, Center + R + U, Center + R - U, Center - R - U at texture (0,0),
    // (1,0), (1,1), (0,1), rounded as retail rounds them.
    void EmitFlareQuad(float const* Center, float const* R, float const* U)
    {
        // FUN_00421ca0(0, 0); FUN_00423b60 then FUN_00423b30, each FSTP float; FUN_00421cc0.
        Win3DTexCoord2f(0.0f, 0.0f);
        float Left[3];
        Win3DVec3Sub(Left, Center, R);
        float Corner[3];
        Win3DVec3Add(Corner, Left, U);
        Win3DVertex3f(Corner[0], Corner[1], Corner[2]);

        // FUN_00421ca0(1, 0); FUN_00422b80 on Center + R, then on that + U.
        Win3DTexCoord2f(1.0f, 0.0f);
        float RightEdge[3] = {Center[0] + R[0], Center[1] + R[1], Center[2] + R[2]};
        Corner[0] = U[0] + RightEdge[0];
        Corner[1] = U[1] + RightEdge[1];
        Corner[2] = U[2] + RightEdge[2];
        Win3DVertex3f(Corner[0], Corner[1], Corner[2]);

        // FUN_00421ca0(1, 1); Center + R again, then that - U.
        Win3DTexCoord2f(1.0f, 1.0f);
        RightEdge[0] = Center[0] + R[0];
        RightEdge[1] = Center[1] + R[1];
        RightEdge[2] = Center[2] + R[2];
        Corner[0] = RightEdge[0] - U[0];
        Corner[1] = RightEdge[1] - U[1];
        Corner[2] = RightEdge[2] - U[2];
        Win3DVertex3f(Corner[0], Corner[1], Corner[2]);

        // FUN_00421ca0(0, 1); Center - R - U inline: x and y keep Center - R on the FPU, z stores
        // it as a float first (0x00557cb8 / 0x00557fba).
        Win3DTexCoord2f(0.0f, 1.0f);
        float const LeftZ = Center[2] - R[2];
        Corner[0] = static_cast<float>(
            (static_cast<double>(Center[0]) - static_cast<double>(R[0])) - static_cast<double>(U[0]));
        Corner[1] = static_cast<float>(
            (static_cast<double>(Center[1]) - static_cast<double>(R[1])) - static_cast<double>(U[1]));
        Corner[2] = LeftZ - U[2];
        Win3DVertex3f(Corner[0], Corner[1], Corner[2]);
    }
}

FFlashLight::FFlashLight(Archetype::ICliObj* Companion)
{
    // FUN_00557700. 0x00557707..0x0055772d: the identity at +0x4c, position 0.
    MatrixSetIdentity(Orient);
    Pos.x = 0.0f;
    Pos.y = 0.0f;
    Pos.z = 0.0f;

    // 0x00557735..0x00557781: the companion's colours, sizes and cone.
    Color = CompanionVector(Companion, 0x54);
    MinColor = CompanionVector(Companion, 0x6c);
    BulbSize = CompanionFloat(Companion, 0x78);
    GlowSize = CompanionFloat(Companion, 0x7c);
    FlareConeOuterCos = CompanionFloat(Companion, 0x8c);
    FlareConeInnerCos = CompanionFloat(Companion, 0x90);
    Rate = 0.0f;
    BlinkFraction = 1.0f;

    // 0x0055778a..0x005577ac: the period, avg_delay (+0x80) jittered to 0.9 .. 1.1 of itself.
    double const Period =
        (static_cast<double>(static_cast<float>(rand()) * kRandScale) * kJitterSpan + kJitterBase) *
        CompanionFloat(Companion, 0x80);
    float const PeriodF = static_cast<float>(Period);
    // 0x005577b0..0x005577ee: only a positive period sets the rate and the lit fraction:
    // Rate = 1 / period, BlinkFraction = min(blink_duration (+0x84), period) / period.
    if (Period > 0.0)
    {
        double const Inverse = 1.0 / static_cast<double>(PeriodF);
        Rate = static_cast<float>(Inverse);
        float const BlinkDuration = CompanionFloat(Companion, 0x84);
        float const Lit = BlinkDuration <= PeriodF ? BlinkDuration : PeriodF;
        BlinkFraction = static_cast<float>(Inverse * Lit);
    }

    // 0x005577f0..0x00557803: a random starting phase.
    Phase = static_cast<float>(rand()) * kRandScale;

    // +0x18 / +0x24 (BlendColor / GlowColor) are not written by the constructor; FUN_00557860
    // sets both before anything reads them.
}

FFlashLight::FFlashLight(Vector const* InColor, Vector const* InMinColor, float Period,
                         float InPhase, float BlinkDuration, float InBulbSize, float InGlowSize)
{
    // FUN_00557630. 0x00557630..0x00557669: the identity at +0x4c, position 0.
    MatrixSetIdentity(Orient);
    Pos.x = 0.0f;
    Pos.y = 0.0f;
    Pos.z = 0.0f;

    // 0x0055766c..0x005576bd: the two colours copied, Phase / BulbSize / GlowSize from the
    // arguments, the cone 0, Rate 0 and BlinkFraction 1.
    Color = *InColor;
    MinColor = *InMinColor;
    Phase = InPhase;
    BulbSize = InBulbSize;
    GlowSize = InGlowSize;
    FlareConeInnerCos = 0.0f;
    FlareConeOuterCos = 0.0f;
    Rate = 0.0f;
    BlinkFraction = 1.0f;

    // 0x00557632..0x00557639 / 0x005576b0..0x005576be: FCOMP against DAT_005c7474 (0.0f),
    // TEST AH,0x41 + JNZ skips when not greater -- only a positive period goes on.
    if (Period > 0.0f)
    {
        // 0x005576c0..0x005576ca: Rate = DAT_005c75dc (1.0f) / Period, FST float, the quotient
        // kept on the FPU stack.
        double const Inverse = 1.0 / static_cast<double>(Period);
        Rate = static_cast<float>(Inverse);
        // 0x005576cd..0x005576da: FCOMP Period against BlinkDuration, TEST AH,0x05 + JP jumps
        // when not less (and when unordered): Period < BlinkDuration takes Period
        // (0x005576dc..0x005576e6), otherwise BlinkDuration (0x005576ee..0x005576f6).
        float const Lit = Period < BlinkDuration ? Period : BlinkDuration;
        BlinkFraction = static_cast<float>(Inverse * static_cast<double>(Lit));
    }

    // +0x18 / +0x24 (BlendColor / GlowColor) are not written, as in FUN_00557700.
}

void FFlashLight::SetFrame(Csys const& Frame)
{
    // FUN_00557810.
    Orient = Frame.Orient;
    Pos = Frame.Pos;
}

void FFlashLight::Update(float Delta)
{
    // FUN_00557860. 0x00557871..0x0055789e: both colours start as Color.
    BlendColor = Color;
    GlowColor = Color;
    // 0x00557867 / 0x005578a1: nothing more when the rate is 0.
    if (Rate == 0.0f)
        return;

    // 0x005578aa..0x005578e4: Phase += Delta * Rate, wrapped by its floor once it reaches 1.
    double const Advanced = static_cast<double>(Delta) * Rate + Phase;
    if (Advanced < 1.0)
        Phase = static_cast<float>(Advanced);
    else
        Phase = static_cast<float>(Advanced - floor(Advanced));

    // 0x005578e6..0x0055791b: a triangle over the lit fraction of the period, 0 outside it.
    float Level = 0.0f;
    if (Phase <= BlinkFraction)
    {
        Level = Phase * (2.0f / BlinkFraction);
        if (Level > 1.0f)
            Level = 2.0f - Level;
    }

    // 0x0055791b..0x00557948: GlowColor = Level * Color.
    GlowColor.x = Level * Color.x;
    GlowColor.y = Level * Color.y;
    GlowColor.z = Level * Color.z;
    // 0x00557946..0x005579a2: BlendColor = MinColor + Level * (Color - MinColor).
    BlendColor.x = (Color.x - MinColor.x) * Level + MinColor.x;
    BlendColor.y = (Color.y - MinColor.y) * Level + MinColor.y;
    BlendColor.z = (Color.z - MinColor.z) * Level + MinColor.z;
}

void FFlashLight::DrawShine(ICamera* Camera)
{
    // FUN_005579b0. 0x005579c6..0x005579eb: the position copied to the stack (the orientation
    // copy at 0x005579ef is never read).
    float const Center[3] = {Pos.x, Pos.y, Pos.z};

    // 0x005579b9..0x00557a14: FCOMP each GlowColor (+0x24) component against DAT_005c8ba8
    // (0.0), TEST AH,0x44: JP (not equal or unordered) goes on to draw, the third compare's
    // JNP (equal) returns -- nothing for a black GlowColor.
    if (GlowColor.x == 0.0f && GlowColor.y == 0.0f && GlowColor.z == 0.0f)
        return;

    // 0x00557a1a..0x00557a27: the cone term starts at DAT_005c75dc (1.0f).
    double Level = 1.0;
    // 0x00557a2b..0x00557a36: FCOMP +0x44 against +0x48, TEST AH,0x41 + JNZ keeps 1.0 unless
    // +0x44 is greater.
    if (FlareConeOuterCos > FlareConeInnerCos)
    {
        // 0x00557a38..0x00557a4f: Dir = Pos - camera position (slot 3, FUN_00423b60).
        float Dir[3];
        Win3DVec3Sub(Dir, &Pos.x, Camera->GetPosition());
        // 0x00557a52..0x00557a56: FUN_00557d00.
        FlashLightNormalizeFast(Dir);
        // 0x00557a5b..0x00557a71: FUN_004cbfa0 (the light's third axis, +0x4c [0][2] / [1][2]
        // / [2][2]) and FUN_004cbf40 ((z*z' + y*y') + x*x', left on the FPU).
        float const Axis[3] = {Orient.E[0][2], Orient.E[1][2], Orient.E[2][2]};
        double const DotWide =
            (static_cast<double>(Dir[2]) * Axis[2] + static_cast<double>(Dir[1]) * Axis[1]) +
            static_cast<double>(Dir[0]) * Axis[0];
        // 0x00557a76: FST float keeps a rounded copy; the first compare uses the FPU value.
        float const Dot = static_cast<float>(DotWide);
        // 0x00557a7a..0x00557a8d: FCOMP against +0x48, TEST AH,0x05 + JP: below it (less)
        // loads DAT_005c7474 (0.0f).
        if (DotWide < static_cast<double>(FlareConeInnerCos))
        {
            Level = 0.0;
        }
        // 0x00557a8f..0x00557a9b: the stored copy against +0x44; not less keeps 1.0
        // (0x00557aae reloads it).
        else if (Dot < FlareConeOuterCos)
        {
            // 0x00557a9d..0x00557aaa: (Dot - [+0x48]) / ([+0x44] - [+0x48]), FDIVP.
            Level = (static_cast<double>(Dot) - static_cast<double>(FlareConeInnerCos)) /
                    (static_cast<double>(FlareConeOuterCos) -
                     static_cast<double>(FlareConeInnerCos));
        }
    }

    // 0x00557ab2..0x00557ad0: FUN_00411d60(GlowColor, (float)Level), then FUN_00421ad0.
    float Shade[3];
    ScaleVec3(Shade, &GlowColor.x, static_cast<float>(Level));
    Win3DColor3f(Shade[0], Shade[1], Shade[2]);

    // 0x00557ad5..0x00557b0e: the camera's first two columns (slot 4, FUN_00422b80).
    float const* View = Camera->GetOrientation();
    float const Right[3] = {View[0], View[3], View[6]};
    View = Camera->GetOrientation();
    float const Up[3] = {View[1], View[4], View[7]};
    // 0x00557b13..0x00557b42: both scaled by GlowSize (+0x3c), FUN_00411d60.
    float R[3];
    ScaleVec3(R, Right, GlowSize);
    float U[3];
    ScaleVec3(U, Up, GlowSize);

    // 0x00557b47..0x00557cee.
    EmitFlareQuad(Center, R, U);
}

void FFlashLight::DrawBulb(ICamera* Camera)
{
    // FUN_00557d70. 0x00557d80..0x00557da5: the position copied to the stack (the orientation
    // copy at 0x00557da9 is never read).
    float const Center[3] = {Pos.x, Pos.y, Pos.z};

    // 0x00557d76..0x00557dcb: the same three FCOMP / TEST AH,0x44 tests on BlendColor (+0x18)
    // -- nothing for black.
    if (BlendColor.x == 0.0f && BlendColor.y == 0.0f && BlendColor.z == 0.0f)
        return;

    // 0x00557dd1..0x00557ddd: FUN_00421ad0 with BlendColor as it is.
    Win3DColor3f(BlendColor.x, BlendColor.y, BlendColor.z);

    // 0x00557de2..0x00557e1f: the camera's first two columns (slot 4, stored float by float).
    float const* View = Camera->GetOrientation();
    float const Right[3] = {View[0], View[3], View[6]};
    View = Camera->GetOrientation();
    float const Up[3] = {View[1], View[4], View[7]};
    // 0x00557e23..0x00557e47: both scaled by BulbSize (+0x38), FUN_00411d60.
    float R[3];
    ScaleVec3(R, Right, BulbSize);
    float U[3];
    ScaleVec3(U, Up, BulbSize);

    // 0x00557e4c..0x00557ff0.
    EmitFlareQuad(Center, R, U);
}

FFlashLightSet::FFlashLightSet()
{
    // FUN_00558180: the allocator byte, then an empty list.
}

FFlashLightSet::~FFlashLightSet()
{
    // FUN_005581b0. 0x005581e2..0x00558206: free each entry's light, then the entry.
    std::list<FFlashLightEntry*>::iterator It = Entries.begin();
    for (; It != Entries.end(); ++It)
    {
        FFlashLightEntry* Entry = *It;
        if (Entry == nullptr)
            continue;
        if (Entry->Light != nullptr)
            delete Entry->Light;
        delete Entry;
    }
    // 0x00558208..0x0055827c: the nodes and the head go with the list.
}

void FFlashLightSet::Clear()
{
    // FUN_005582a0: the same frees, then every node erased.
    std::list<FFlashLightEntry*>::iterator It = Entries.begin();
    for (; It != Entries.end(); ++It)
    {
        FFlashLightEntry* Entry = *It;
        if (Entry == nullptr)
            continue;
        if (Entry->Light != nullptr)
            delete Entry->Light;
        delete Entry;
    }
    Entries.clear();
}

void FFlashLightSet::Add(Archetype::ICliObj* Companion, int HardpointIndex)
{
    // FUN_00558310. 0x0055832c..0x0055836e: the entry, then the light.
    FFlashLightEntry* Entry = new FFlashLightEntry;
    FFlashLight* Light = new FFlashLight(Companion);
    // 0x00558372..0x0055840e: an identity frame copied into +0x4c (the constructor already
    // left the same values there).
    Matrix Identity;
    MatrixSetIdentity(Identity);
    Light->Orient = Identity;
    Light->Pos.x = 0.0f;
    Light->Pos.y = 0.0f;
    Light->Pos.z = 0.0f;
    Entry->Light = Light;
    Entry->HardpointIndex = HardpointIndex;
    // 0x00558417..0x0055845b: appended at the end.
    Entries.push_back(Entry);
}

void FFlashLightSet::Update(HardpointSummary const* Hardpoints, float Delta)
{
    // FUN_00558470.
    std::list<FFlashLightEntry*>::iterator It = Entries.begin();
    for (; It != Entries.end(); ++It)
    {
        FFlashLightEntry* Entry = *It;
        // 0x00558492..0x005584aa: the hardpoint's instance frame.
        Csys const World = DALib_Engine_get_csys(Hardpoints->get_instance(Entry->HardpointIndex));
        // 0x005584b0..0x00558500: the hardpoint's local frame (position, then orientation;
        // retail reads get_info twice).
        Csys Local;
        Local.Pos = Hardpoints->get_info(Entry->HardpointIndex).Position;
        Local.Orient = Hardpoints->get_info(Entry->HardpointIndex).Orient;
        // 0x00558503..0x0055854f: into world space, stored as the light's matrix and position.
        Csys const Placed = World.trans_from_frame(Local);
        Entry->Light->Orient = Placed.Orient;
        Entry->Light->Pos = Placed.Pos;
        // 0x00558551..0x00558557.
        Entry->Light->Update(Delta);
    }
}

void FFlashLightSet::Draw(ICamera* Camera)
{
    // FUN_00558580: at most 0x20 lights on the stack; one more raises DAT_006799c8.
    FFlashLight* Batch[0x20];
    int Count = 0;
    std::list<FFlashLightEntry*>::iterator It = Entries.begin();
    for (; It != Entries.end(); ++It)
    {
        if (Count >= 0x20)
        {
            G_FlashlightBatchOverflow = true;
            break;
        }
        Batch[Count] = (*It)->Light;
        ++Count;
    }
    if (Count > 0)
        DrawFlashlightFlares(Batch, Count, Camera);
}

void DrawFlashlightFlares(FFlashLight* const* Lights, int Count, ICamera* Camera)
{
    // FUN_00558000.
    if (GetGameOption(kGameOptEffectsGroup) == 0)
        return;
    if (GetGameOption(kGameOptFlashlights) == 0)
        return;
    if (Count == 0)
        return;

    // 0x0055803b..0x0055804c: FUN_00422610(Camera, LightsCamera.cpp, 0x339), FUN_00422950.
    Win3DSetViewport(Camera, kLightsCameraCpp, 0x339);
    Win3DApplyCamera(Camera);
    // 0x00558051..0x0055808b: no culling, additive blend (2, 2), no depth write, depth test on
    // with compare 4 (LESSEQUAL), no fog.
    Win3DDisable(kWin3DCullFace);
    Win3DEnable(kWin3DBlend);
    Win3DSetBlendFunc(kWin3DBlendOne, kWin3DBlendOne);
    Win3DSetDepthWrite(0);
    Win3DEnable(kWin3DDepthTest);
    Win3DSetDepthFunc(4);
    Win3DDisable(kWin3DFog);

    // 0x00558090..0x005580ba: TextureLib slot 22 on the "shine" record (DAT_006799c4), frame 0;
    // its out[0] bound (FUN_004220a0) without testing the result, then FUN_00421670(8) (quads).
    // Retail's out buffer is uninitialised stack; the port zeroes it.
    unsigned long ShineFrame[5] = {0, 0, 0, 0, 0};
    DALib::TextureLib->GetTextureFrame(G_FlashlightShineTexture, 0, ShineFrame);
    Win3DBindTexture(reinterpret_cast<void*>(ShineFrame[0]));
    Win3DBegin(kWin3DQuads);
    // 0x005580c6..0x005580dd: FUN_005579b0 on every light.
    int Index = 0;
    for (Index = 0; Index < Count; ++Index)
        Lights[Index]->DrawShine(Camera);
    // 0x005580df: FUN_00421690.
    Win3DEnd();

    // 0x005580e4..0x0055810c: the same with the "bulb" record (DAT_006799c0).
    unsigned long BulbFrame[5] = {0, 0, 0, 0, 0};
    DALib::TextureLib->GetTextureFrame(G_FlashlightBulbTexture, 0, BulbFrame);
    Win3DBindTexture(reinterpret_cast<void*>(BulbFrame[0]));
    Win3DBegin(kWin3DQuads);
    // 0x00558114..0x0055812d: FUN_00557d70 on every light.
    for (Index = 0; Index < Count; ++Index)
        Lights[Index]->DrawBulb(Camera);
    // 0x0055812f.
    Win3DEnd();

    // 0x00558134..0x00558164: depth compare 2, depth test, depth write, culling and blending
    // on, blend (5, 6).
    Win3DSetDepthFunc(2);
    Win3DEnable(kWin3DDepthTest);
    Win3DSetDepthWrite(1);
    Win3DEnable(kWin3DCullFace);
    Win3DEnable(kWin3DBlend);
    Win3DSetBlendFunc(kWin3DBlendSrcAlpha, kWin3DBlendInvSrcAlpha);
}

void LoadFlashlightTextures()
{
    // FUN_00557550. The texture group is loaded, the two records taken by {name, CRC} through
    // TextureLib slot 12 (+0x30), and the group released straight away with slot 7 (+0x1c).
    int const Group = MiscUtilLoadTextureLibrary("fx\\efx.txm");

    FFlareTextureQuery Query;
    // Raw retail bytes at 0x005db6f8 are "bulb\0"; Ghidra's DAT_ label shows only 'b'.
    Query.Name = "bulb";
    Query.Crc = DACOM_CRC::GetCRC32("bulb");
    DALib::TextureLib->FindTextureByCrc(&Query, &G_FlashlightBulbTexture);

    Query.Name = "shine";
    Query.Crc = DACOM_CRC::GetCRC32("shine");
    DALib::TextureLib->FindTextureByCrc(&Query, &G_FlashlightShineTexture);

    DALib::TextureLib->GarbageCollect(Group);
}

void ReleaseFlashlightTextures()
{
    // FUN_005575e0. TextureLib slot 14 (+0x38) on each record that is set, then clear it.
    if (G_FlashlightBulbTexture != nullptr)
    {
        DALib::TextureLib->ReleaseTextureRecord(G_FlashlightBulbTexture);
        G_FlashlightBulbTexture = nullptr;
    }
    if (G_FlashlightShineTexture != nullptr)
    {
        DALib::TextureLib->ReleaseTextureRecord(G_FlashlightShineTexture);
        G_FlashlightShineTexture = nullptr;
    }
}
