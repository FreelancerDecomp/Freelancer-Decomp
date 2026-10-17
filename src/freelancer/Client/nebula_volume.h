// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Port-only (no retail counterpart): the ray-marched volumetric nebula, on the Direct3D 9 render
// extensions (render/IRenderPipelineEx.h, RP9.dll). Retail draws the inside of a nebula as a
// fullscreen fog quad, per-object linear D3D fog and interior puff billboards, and its outside
// as axis-locked billboards ("bits"), two crossing fill sheets and the per-nebula far-bucket
// copies of both; while this module draws a nebula it draws one lit, drifting volume (thin haze
// plus dense cloud masses) instead, derived from the nebula's own INI values, from inside and
// from outside (up to [Nebula] max_nebulae nebulae within exterior_max_distance), and lets the
// star sphere / space colour show behind it. Lightning strikes inside its clouds light them from
// within and light nearby ships (driving the nebula's [DynamicLightning] light, or one of the
// volume's own). With the DirectX8 pipeline, without an INTZ scene depth or ps_3_0, with the
// setting off, or with no nebula in range, IsActive() is false and every retail path runs
// unchanged.
// Settings: DATA_EXTRA/VOLUMETRIC/volumetric.ini ([Nebula], [NebulaOverride]); shaders:
// DATA_EXTRA/SHADERS/nebula_volume.hlsl + nebula_volume.hlsli, nebula_composite.hlsl. Both hot-reload
// (render_ex.cpp). See docs/re/rp9.md, "Volumetric nebula".

struct FNebulaEffect;
struct ICamera;

namespace Universe
{
    struct IZone;
}

namespace NebulaVolume
{
    // StarSys [Nebula] loader (star_sys.cpp), after FNebulaEffect::Load: remembers the nebula
    // (the set the volume picks from) and its file name for the [NebulaOverride] file= match.
    void RegisterNebula(FNebulaEffect* Nebula, char const* File);
    // NebulaDestroy (cli_nebula.cpp): forgets the nebula.
    void UnregisterNebula(FNebulaEffect* Nebula);

    // RenderEx (Startup and hot reload): (re)reads [Nebula] / [NebulaOverride] from the resolved
    // volumetric.ini path; nullptr (file missing) = built-in defaults. Applies from the next
    // BeginFrame.
    void LoadSettings(char const* IniPath);
    // RenderEx hot reload, after IRenderPipelineEx::ReloadExPrograms: a volume switched off by a
    // shader that did not compile is tried again.
    void OnProgramsReloaded();
    // RenderEx::Shutdown: destroys the programs, the render target and the noise volume.
    void Shutdown();

    // SpaceObjDrawPass, right after the field controller is picked: chooses the nebulae the
    // volume draws this frame -- every registered nebula the camera is inside, and with
    // [Nebula] exterior those within exterior_max_distance of their surface, nearest first, at
    // most max_nebulae. Controller / Enclosure are DAT_00675244 / DAT_0067524c; bNebulaOption:
    // the [EFFECTS] and NEBULA game options (off = nothing, as retail draws no nebula then);
    // SceneFar: the far plane the opaque scene is drawn with outside a nebula's core (a nebula
    // whose surface lies beyond it is drawn in the far bucket's painter order instead of over
    // the opaque scene).
    // Then runs the lightning: strikes inside the drawn nebulae's clouds (point lights of the
    // ray march) and the ship light they drive. With nothing drawn every strike goes and a
    // light the strikes drove is left dark for retail.
    void BeginFrame(ICamera* Camera, FNebulaEffect* Controller, int Enclosure, bool bNebulaOption,
                    float SceneFar);
    // NebulaAmbientTick (cli_nebula.cpp, FUN_005097c0): true while the volume's lightning
    // strikes drive this nebula's [DynamicLightning] GameLight (+0x270) -- the camera fully
    // inside it, the volume drawing it with lightning_enabled and lightning_lights_ships; the
    // retail random-direction flash then stays out. False on RP8 and whenever the volume is
    // off.
    bool DrivesNebulaLight(FNebulaEffect const* Nebula);
    // True between BeginFrame and Draw when at least one nebula will be drawn.
    bool IsActive();
    // True between BeginFrame and Draw when the volume draws this nebula.
    bool IsActiveFor(FNebulaEffect const* Nebula);
    // Retail-part switches ([Nebula] replace_*), only meaningful while active. The exterior ones
    // (object fog, bits, fill sheets) answer true only while the volume draws the nebula at full
    // strength (closer than exterior_max_distance - exterior_fade_distance): in the fade band
    // both are drawn.
    // ReplacesObjectFog gates StarSysApplyObjectLighting's fog block (FUN_005554d0), which
    // every fogged object goes through with its containing nebula: ships and solars
    // (Gameobj +0x38) and asteroid fields with contains_fog_zone (CliAsteroidAmbientPass hands
    // it the field controller), so asteroids inside the volume lose their D3D fog too.
    bool ReplacesObjectFog(FNebulaEffect const* Nebula);
    bool ReplacesFogOverlay(FNebulaEffect const* Nebula);
    bool ReplacesClouds(FNebulaEffect const* Nebula);
    // FNebulaEffect::DrawBoltsPass -> NebulaDrawBolts (the exterior billboards, FUN_00507ed0) and
    // FNebulaEffect::DrawFogShell -> NebulaUpdateBoltCamera (the two crossing fill sheets,
    // FUN_005074e0), both for the controller and for the far-bucket ZItemNebula nodes.
    bool ReplacesExteriorBits(FNebulaEffect const* Nebula);
    bool ReplacesFillSheets(FNebulaEffect const* Nebula);
    // [Nebula] real_background: while the volume draws the field controller from inside, also
    // fully inside, SpaceObjDrawPass draws the star sphere and clears to the system's space
    // colour instead of the fog colour -- the volume's haze provides the fog. True from
    // BeginFrame through the end of the frame (the clear colour is set after Draw); false when
    // the controller's draw failed.
    bool ShowsRealBackground();
    // SpaceObjDrawPass's far plane fully inside a field (retail: FogFarClamp, about [Fog] far
    // + 500): with [Nebula] extend_far_plane, pushed out to 1.5 march caps (the haze has hidden
    // everything there; the thinner haze would otherwise show objects popping in at retail's
    // plane) and at least past the extended asteroid spawn distance, at most MaxFar. RetailFar
    // unchanged when inactive.
    float InsideFarPlane(float RetailFar, float MaxFar);
    // CliAsteroidTickOne (cli_asteroid.cpp): the depth (metres along the view axis) out to which
    // the cube asteroids of the field in FieldZone should be drawn so that their spawn boundary
    // lies where the volume's haze leaves [Nebula] asteroid_spawn_transmittance (plus the
    // camera's distance to the nebula when outside it), at most asteroid_max_spawn_distance;
    // OutFadeFraction: asteroid_fade_fraction (the fade band as a fraction of that depth).
    // 0 when no drawn nebula overlaps the field or asteroid_spawn is off: retail's fill_dist.
    float AsteroidSpawnReach(Universe::IZone const* FieldZone, float* OutFadeFraction);

    // What the frame draws that the scene depth does not hold, reported while active (between
    // BeginFrame and Draw; ignored otherwise). Each nebula pass uses what was drawn before it.
    // FStarSphere::DrawBillboard (the star glow / centre sprites, no depth test or write, also
    // the in-nebula burn-through): the star's world position, the sprite's half size (m) and its
    // alpha argument. Where the glow is most of the scene the volume attenuates the pixel by the
    // transmittance up to the star, not by the whole ray's (a star in front of a nebula stays
    // visible; behind it, it is hidden as before).
    void NoteStarGlow(float const* WorldPos, float Radius, float Alpha);
    // FClientSolarGameobj::Slot188 (planets and moons; the far bucket draws them without a depth
    // test): their bounding sphere ends the volume's rays like the scene depth would.
    void NoteOpaqueSphere(float const* WorldCentre, float Radius);
    // CliAsteroidTickOne's extended cube walk (AsteroidSpawnReach): its cubes fade out between
    // FadeEnd - FadeBand and FadeEnd (view depth, m) and still write depth while translucent;
    // the volume adds the medium behind them for the part they let through.
    void NoteAsteroidFade(float FadeEnd, float FadeBand, float CubeSize);

    // The far bucket's ZItemNebula node (FZItemNebula::Draw, space_scene_obj_draw.cpp): a nebula
    // the volume draws whose surface lies beyond the opaque scene's far plane is drawn here, in
    // painter order with the far solars, rings and asteroid bands, without a depth test.
    void DrawBackground(FNebulaEffect* Nebula, ICamera* Camera);
    // SpaceObjDrawPass, after the opaque scene (DrainMeshAndAsteroids) and before the effects:
    // ray-marches every nebula not drawn yet (farthest first) against the scene depth and
    // composites each over the back buffer. Near / Far are the planes the scene was drawn with.
    // Ends the frame (IsActive() false).
    void Draw(ICamera* Camera, float Near, float Far);
}
