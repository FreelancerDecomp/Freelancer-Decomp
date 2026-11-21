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
    static constexpr char DirectX8[] = "DirectX8";
    // PORT(rp9): RP9 (Direct3D 9 mirror of RP8; docs/re/rp9.md)
    static constexpr char DirectX9[] = "DirectX9";

    // Shading libraries / services
    static constexpr char TextureLibrary[] = "TextureLibrary";
    static constexpr char MaterialLibrary[] = "MaterialLibrary";
    static constexpr char MaterialBatcher[] = "MaterialBatcher";
    static constexpr char MaterialAnimation[] = "MaterialAnimation";
    static constexpr char BatchedMaterial[] = "BatchedMaterial";

    // Shading IMaterial types
    static constexpr char DcDt[] = "DcDt";
    static constexpr char DcDtEc[] = "DcDtEc";
    static constexpr char DcDtOcOt[] = "DcDtOcOt";
    static constexpr char DcDtEcOcOt[] = "DcDtEcOcOt";
    static constexpr char DcDtTwo[] = "DcDtTwo";
    static constexpr char DcDtEcTwo[] = "DcDtEcTwo";
    static constexpr char DcDtOcOtTwo[] = "DcDtOcOtTwo";
    static constexpr char DcDtEcOcOtTwo[] = "DcDtEcOcOtTwo";
    static constexpr char DcDtEt[] = "DcDtEt";
    static constexpr char EcEt[] = "EcEt";
    static constexpr char EcEtTwo[] = "EcEtTwo";
    static constexpr char AtmosphereMaterial[] = "AtmosphereMaterial";
    static constexpr char Nebula[] = "Nebula";
    static constexpr char NebulaTwo[] = "NebulaTwo";
    static constexpr char NullMaterial[] = "NullMaterial";
    static constexpr char LightMap[] = "LightMap";
    static constexpr char DetailMapMaterial[] = "DetailMapMaterial";
    static constexpr char DetailMap2Dm1Msk2PassMaterial[] = "DetailMap2Dm1Msk2PassMaterial";
    static constexpr char IllumDetailMapMaterial[] = "IllumDetailMapMaterial";
    static constexpr char Masked2DetailMapMaterial[] = "Masked2DetailMapMaterial";
    static constexpr char BtDetailMapMaterial[] = "BtDetailMapMaterial";
    static constexpr char BtDetailMapTwoMaterial[] = "BtDetailMapTwoMaterial";

    // RendComp
    static constexpr char RenderManager[] = "RenderManager";
    static constexpr char TriMesh[] = "TriMesh";
    static constexpr char SphereMesh[] = "SphereMesh";
    static constexpr char VMesh[] = "VMesh";
    static constexpr char VMeshLibrary[] = "VMeshLibrary";
    static constexpr char VMeshWire[] = "VMeshWire";

    // ximage
    static constexpr char BigImage[] = "BigImage";

    // flmaterials
    static constexpr char BackdropMaterial[] = "BackdropMaterial";
    static constexpr char StarsphereMaterial[] = "StarsphereMaterial";
    static constexpr char CloakMaterial[] = "CloakMaterial";
    static constexpr char ColorizeMaterial[] = "ColorizeMaterial";
    static constexpr char EngineGlowMaterial[] = "EngineGlowMaterial";
    static constexpr char EnvMapMaterial[] = "EnvMapMaterial";
    static constexpr char ExclusionZoneMaterial[] = "ExclusionZoneMaterial";
    static constexpr char GlassMaterial[] = "GlassMaterial";
    static constexpr char HighGlassMaterial[] = "HighGlassMaterial";
    static constexpr char GfGlassMaterial[] = "GFGlassMaterial";
    static constexpr char HudIconMaterial[] = "HUDIconMaterial";
    static constexpr char HudAnimMaterial[] = "HUDAnimMaterial";
    static constexpr char NomadMaterial[] = "NomadMaterial";
    static constexpr char NomadMaterialNoBendy[] = "NomadMaterialNoBendy";
    static constexpr char PlanetWaterMaterial[] = "PlanetWaterMaterial";
    static constexpr char ShadowMaterial[] = "ShadowMaterial";
    static constexpr char VisibleCounterMaterial[] = "VisibleCounterMaterial";
    static constexpr char WireMaterial[] = "WireMaterial";
    static constexpr char WireMaterial2[] = "WireMaterial2";
    static constexpr char FlAppearance[] = "FLAppearance";
}
}
