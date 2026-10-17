// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// The client's alchemy mesh factory -- retail freelancer.exe 0x004f67e0..0x004f6dc0, RTTI
// `FLFxMeshFactory : IFxMeshFactory` and `FLFxMesh : IFxMesh, ObjectHandle` (both wrapped in
// the DAComponent<> template, which supplies the reference count and the table-driven
// QueryInterface). The effect-shape warm-up FUN_004f70d0 creates one factory into DAT_00674cdc
// and hands it to IFxRuntime::BindMeshFactory; alchemy.dll then asks it (slot 3) for one
// FLFxMesh per mesh particle. The effect shutdown FUN_004f7140 releases it.
//
// ObjectHandle is retail's name for the 0x10-byte mesh holder FUN_00420d40 builds
// (FMeshHolder, client_mesh_load.h): FLFxMesh carries one at +0x04 and its reference count
// at +0x14, 0x18 bytes in all.
//
// The interfaces themselves are alchemy.dll's and live in src/include/alchemy.h.

struct IFxMeshFactory;

// DAT_00674cdc -- the factory FUN_004f70d0 creates and FUN_004f7140 releases.
extern IFxMeshFactory* G_FLFxMeshFactory;

// FUN_004f67e0 (cdecl Out): operator new(8), the FLFxMeshFactory vtable (005d98bc) and a
// reference count of 1 into *Out -- or null when the allocation failed. Returns true either
// way.
bool FLFxMeshFactoryCreate(IFxMeshFactory** Out);
