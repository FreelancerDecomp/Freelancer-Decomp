// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail per-frame tick FUN_005b2890 (partial).
void TickClientFrame(double DeltaSeconds); // FUN_005b2890

// The render-frame bracket for the modal loops outside TickClientFrame (the NN dialogs'
// FUN_00473cb0 / FUN_00473e00 / FUN_00473f70 and WaitForServerResponse's multiplayer wait):
// FUN_00424f30 -- BeginScene (with the device-recovery path); true when the frame began.
bool BeginClientRenderFrame();
// FUN_00425080 then FUN_00425140 -- EndScene and present, back to back, as the modal loops and
// the menu frame issue them (only when FUN_00424f30 returned true).
void EndClientRenderFrame();

// The two halves on their own, for the space frame LAB_0054b000, which runs FUN_00541510,
// FUN_00541580, FUN_0054a940 and the camera-mode checks between them (0x0054b159 / 0x0054b21a).
void EndClientRenderScene();      // FUN_00425080
void PresentClientRenderFrame();  // FUN_00425140

// FUN_0041b2e0 (cdecl, dt) -- DALib::TextureLib slot 0x20 (0, dt), then DALib::MatAnimLibrary
// slot 0x18 (-1, dt): the streaming-texture and material-animation tick.
void TickMaterialAnimationPlayback(float DeltaSeconds);
