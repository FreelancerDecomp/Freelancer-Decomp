// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// The frame walk and the per-object submit (FUN_00539980, IRenderer2::Slot7) live in
// space_scene_obj_draw.h.

// Legacy foreign space_scene_load SpaceSceneDraw hook — no-op (proxies abolished).
void OnSpaceSceneObjectDraw(void* Renderable, unsigned char SceneKind, unsigned Layer, int SubLayer);

bool ShouldDrawSpaceSceneObject(void* Renderable, unsigned char SceneKind);

unsigned GetSceneDrawCullCount();
void ResetSceneDrawCullCountForFrame();
