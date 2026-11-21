// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Common.dll ZONE -- the one implementation of Universe::IZone (RTTI .?AUZONE@@ : IZone :
// IZoneImmersion, vftable .rdata:063a5094, 37 slots). Common-internal: nothing outside
// Common.dll constructs, destroys or names a ZONE; other modules see IZone only
// (common_universe.h). The universe loader (common_universe.cpp) builds one on its stack per
// [Zone] block and copies it by value into the zone map, as retail does.
//
// Retail keeps the zone's data on ZONE; the port keeps it on IZone (see the comment on
// IZone), so ZONE adds no members of its own. Bodies: src/Common/common_zone.cpp.

#include "common_universe.h"

struct ZONE : Universe::IZone
{
    ZONE();
    // FUN_0633e3f0: member-wise copy (common_zone.cpp).
    ZONE(ZONE const& Other);
    // FUN_06339010 (.text:06339010-0633909b). Non-virtual, as in retail: the vftable has no
    // destructor slot. Releases the mesh handle through DALib::Engine (vt+0x3c) and frees
    // the owned loot record; the population buffer goes with IZone::Population.
    ~ZONE();

    double GetImmersion(Vector const& Point) const override;
    unsigned GetId() const override;
    unsigned GetSystem() const override;
    Transform const& GetTransform() const override;
    Vector const& GetPos() const override;
    Matrix const& GetOrientation() const override;
    unsigned GetShape() const override;
    unsigned long GetProperties() const override;
    bool Slot20() const override;
    Vector const& GetFogColor() const override;
    Vector GetSize() const override;
    long GetMeshHandle() const override;
    float GetRadius() const override;
    bool InZone(Vector const& Point, float Margin) const override;
    bool InZoneXZ(Vector const& Point, float Margin) const override;
    double GetDistance(Vector const& Point) const override;
    double GetRadiusAlong(Vector const& Direction) const override;
    Vector GetSurfacePoint(Vector const& Point) const override;
    double ClassifyPoint(Vector const& Point) const override;
    bool Intersect(Vector const& Start, Vector const& End, Vector& OutHit) const override;
    float GetEdgeFraction() const override;
    bool HasPhysicalEffect() const override;
    float GetDragModifier() const override;
    float GetDamage() const override;
    float GetInterference() const override;
    int GetPopulationCount() const override;
    bool GetPopulationEntry(int Index, unsigned& OutId, float& OutWeight) const override;
    weighted_vector<unsigned int> const& GetPopulation() const override;
    bool HasPathLabel() const override;
    ID_String GetMusic() const override;
    bool GetSpacedust(char const*& OutSpacedust, int& OutMaxParticles) const override;
    CmnAsteroid::LootableZoneEntry* GetLootableInfo() const override;
    unsigned char GetVisit() const override;
    int GetName() const override;
    int GetIdsInfo() const override;
    void SetLootableInfo(CmnAsteroid::LootableZoneEntry* LootEntry) override;
    void SetVisit(unsigned char NewVisit) override;

private:
    // FUN_0633a917: slot +0x48's box case (a JZ tail), returned unrounded like the slot.
    double ClassifyBox(Vector const& Point) const;
};
