// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "lights_camera.h"

#include "DALib.h"
#include "alchemy.h"
#include "dacom.h"
#include "game_options.h"
#include "misc_util.h"
#include "vis_effect_arch.h"

namespace
{
    char const* const GLightsCameraFile = "E:\\FL\\Scratch\\Source\\Client\\supt\\LightsCamera.cpp";
}

// FUN_00557360.
//   0x0055736e..0x00557396  DALib::Alchemy null -> return 0 (no field written).
//   0x00557399..0x005573a3  +0x4 = Vis; FUN_0042e080 on it (its answer is not read).
//   0x005573a8..0x005573dd  node = 0; Dacom slot 3 CreateInstance({8, "FxNode"}, &node).
//   0x005573e4..0x005573fc  FxEffectLibrary slot 6 (node, Vis+0x4 effect_crc, this).
//   0x00557403..0x00557436  Alchemy slot 25 GetRootNode(&root), slot 20 AttachNode(root, node),
//                           root slot 2 Release; result 1.
//   0x0055743a..0x00557468  either failure: FDUMP(0x100001, ..., 0x215, *Vis (its nickname)),
//                           FUN_0042e160(Vis), +0x4 = 0.
//   0x0055746f..0x00557482  node non-null -> node slot 2 Release.
bool FLightsCameraParticles::Create(FVisEffectArch* InVis)
{
    if (DALib::Alchemy == nullptr)
        return false;

    bool bCreated = false;
    Vis = InVis;
    VisEffectArchAddRef(Vis);

    void* Node = nullptr;
    AGGDESC Desc = {8, "FxNode"};
    bool bFailed = true;
    if (DALib::Dacom->CreateInstance(&Desc, &Node) == GR_OK)
    {
        if (DALib::FxEffectLibrary->CreateEffect(Node, reinterpret_cast<void*>(Vis->EffectCrc),
                                                 &Effect) == GR_OK)
        {
            void* Root = nullptr;
            DALib::Alchemy->GetRootNode(&Root);
            DALib::Alchemy->AttachNode(Root, Node);
            static_cast<IDAComponent*>(Root)->Release();
            bCreated = true;
            bFailed = false;
        }
    }
    if (bFailed)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: ParticleEffect could not create Alchemy effect (%s)\n",
              GLightsCameraFile, 0x215, Vis->Nickname);
        VisEffectArchRelease(Vis);
        Vis = nullptr;
    }
    if (Node != nullptr)
        static_cast<IDAComponent*>(Node)->Release();
    return bCreated;
}

// FUN_005574a0. 0x005574a9..0x005574c7: slot 13 (+0x34) with 7, slot 2 Release, +0 = 0,
// FUN_0042e160 on +0x4 (no null test), +0x4 = 0.
void FLightsCameraParticles::Destroy()
{
    if (Effect == nullptr)
        return;
    Effect->Slot13(reinterpret_cast<void*>(7));
    static_cast<IDAComponent*>(Effect)->Release();
    Effect = nullptr;
    VisEffectArchRelease(Vis);
    Vis = nullptr;
}

// FUN_005574d0. 0x005574d6 +0 null -> out; FUN_005792b0(0x25) / (0x26) zero -> out; slot 4
// (+0x10) (0, &Scratch[0]); slot 15 (+0x3c) (&Scratch[1]) -- the center at Scratch[1..3] and
// the radius at Scratch[4] -- negative -> out; FUN_0041c8d0(Camera, &center, radius) false ->
// out; slot 9 (+0x24) (Camera). The 0x14-byte stack block is shared by both slots, as retail's
// local_14 is.
void FLightsCameraParticles::Draw(ICamera* Camera)
{
    if (Effect == nullptr)
        return;
    if (GetGameOption(kGameOptEffectsGroup) == 0)
        return;
    if (GetGameOption(kGameOptParticles) == 0)
        return;

    float Scratch[5];
    Effect->Slot4(nullptr, &Scratch[0]);
    if (Effect->Slot15(&Scratch[1]) < 0)
        return;
    if (!MiscUtilSphereInView(Camera, &Scratch[1], Scratch[4]))
        return;
    Effect->Slot9(Camera);
}
