// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Live per-type effect instance lists — DAT_00674ce0/ce4/ce8 (Effect.cpp).
// Walked by FUN_004f7810 / SpaceEffectsDrawFrame. Filled by FUN_004f7af0 / FUN_004fc920
// when an effect instance activates (not by [EffectType] INI meta — that is FUN_004fd5a0).
//
// Neighbor contract (vis_effect_arch / Effect spawn):
 //   After constructing a live Effect* with EffectTypeId at +0x5C, call
 //   EffectTypeListLink(Effect) so SpaceEffectsDrawFrame's bucket walk sees it.
 //   Link fields live at Effect+0x60 (prev), +0x64 (next), +0x68 (self-when-linked).
 //   Type id at +0x5C must be in [0, 0x2f).

// 0x2f buckets: the static initializer LAB_004f6f60 zeroes exactly 0x2f {Count, Head, Tail}
// triples from DAT_00674ce0, and the teardown walk FUN_004f7140 stops at 0x00674f18 (47 buckets);
// 0x00674f14 is a different object.
constexpr int kEffectTypeBucketCount = 0x2f;

// One retail bucket: count / head / tail (stride 3 dwords from DAT_00674ce0).
struct FEffectTypeBucket
{
    int Count;   // DAT_00674ce0[i]
    void* Head;  // DAT_00674ce4[i] — points at Effect+0x60 link node
    void* Tail;  // DAT_00674ce8[i]
};

extern FEffectTypeBucket G_EffectTypeBuckets[kEffectTypeBucketCount];

// FUN_004fc920 — prepend Node into Bucket (Node = Effect+0x60).
void EffectTypeBucketPushFront(FEffectTypeBucket* Bucket, void* Node);

// FUN_004f7af0 — link Effect into G_EffectTypeBuckets[Effect+0x5C] if not yet linked
// and flag bit1 at Effect+0x8 clear. Increments Effect+0x58.
void EffectTypeListLink(void* Effect);

// The full retail unlink-and-release (0x004f7910 / 0x004f716a / 0x004f73ea): linked only;
// +0x68 = 0, node out of the bucket, count--, then RefCount-- and, at <= 0, slot +0x20 (1).
void EffectTypeListUnlinkAndRelease(void* Effect);

// FUN_004f73c0 — every live effect in every bucket: slot +4 (kill), then unlink-and-release.
void EffectTypeListKillAll();
