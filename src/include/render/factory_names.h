// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Factory registration names for RENDER DLLs -- the strings RegisterComponent /
// CreateInstance take, not interface ids. The IIDs that used to share this file moved
// to dacom_iids.h on 2026-09-16; these are a different namespace of strings and stay.
// Sources: docs/re/render.md, docs/re/shading.md.
// Do not invent names.

namespace Render
{
namespace Factory
{
    // RP8
    static const char DirectX8[] = "DirectX8";
    // RP9 (port-only Direct3D 9 mirror of RP8; docs/re/rp9.md)
    static const char DirectX9[] = "DirectX9";

    // Shading libraries / services
    static const char TextureLibrary[] = "TextureLibrary";
    static const char MaterialLibrary[] = "MaterialLibrary";
    static const char MaterialBatcher[] = "MaterialBatcher";
    static const char MaterialAnimation[] = "MaterialAnimation";
    static const char BatchedMaterial[] = "BatchedMaterial";

    // Shading IMaterial types
    static const char DcDt[] = "DcDt";
    static const char DcDtEc[] = "DcDtEc";
    static const char DcDtOcOt[] = "DcDtOcOt";
    static const char DcDtEcOcOt[] = "DcDtEcOcOt";
    static const char DcDtTwo[] = "DcDtTwo";
    static const char DcDtEcTwo[] = "DcDtEcTwo";
    static const char DcDtOcOtTwo[] = "DcDtOcOtTwo";
    static const char DcDtEcOcOtTwo[] = "DcDtEcOcOtTwo";
    static const char DcDtEt[] = "DcDtEt";
    static const char EcEt[] = "EcEt";
    static const char EcEtTwo[] = "EcEtTwo";
    static const char AtmosphereMaterial[] = "AtmosphereMaterial";
    static const char Nebula[] = "Nebula";
    static const char NebulaTwo[] = "NebulaTwo";
    static const char NullMaterial[] = "NullMaterial";
    static const char LightMap[] = "LightMap";
    static const char DetailMapMaterial[] = "DetailMapMaterial";
    static const char DetailMap2Dm1Msk2PassMaterial[] = "DetailMap2Dm1Msk2PassMaterial";
    static const char IllumDetailMapMaterial[] = "IllumDetailMapMaterial";
    static const char Masked2DetailMapMaterial[] = "Masked2DetailMapMaterial";
    static const char BtDetailMapMaterial[] = "BtDetailMapMaterial";
    static const char BtDetailMapTwoMaterial[] = "BtDetailMapTwoMaterial";

    // RendComp
    static const char RenderManager[] = "RenderManager";
    static const char TriMesh[] = "TriMesh";
    static const char SphereMesh[] = "SphereMesh";
    static const char VMesh[] = "VMesh";
    static const char VMeshLibrary[] = "VMeshLibrary";
    static const char VMeshWire[] = "VMeshWire";

    // ximage
    static const char BigImage[] = "BigImage";

    // flmaterials
    static const char BackdropMaterial[] = "BackdropMaterial";
    static const char StarsphereMaterial[] = "StarsphereMaterial";
    static const char CloakMaterial[] = "CloakMaterial";
    static const char ColorizeMaterial[] = "ColorizeMaterial";
    static const char EngineGlowMaterial[] = "EngineGlowMaterial";
    static const char EnvMapMaterial[] = "EnvMapMaterial";
    static const char ExclusionZoneMaterial[] = "ExclusionZoneMaterial";
    static const char GlassMaterial[] = "GlassMaterial";
    static const char HighGlassMaterial[] = "HighGlassMaterial";
    static const char GfGlassMaterial[] = "GFGlassMaterial";
    static const char HudIconMaterial[] = "HUDIconMaterial";
    static const char HudAnimMaterial[] = "HUDAnimMaterial";
    static const char NomadMaterial[] = "NomadMaterial";
    static const char NomadMaterialNoBendy[] = "NomadMaterialNoBendy";
    static const char PlanetWaterMaterial[] = "PlanetWaterMaterial";
    static const char ShadowMaterial[] = "ShadowMaterial";
    static const char VisibleCounterMaterial[] = "VisibleCounterMaterial";
    static const char WireMaterial[] = "WireMaterial";
    static const char WireMaterial2[] = "WireMaterial2";
    static const char FlAppearance[] = "FLAppearance";
}
}
