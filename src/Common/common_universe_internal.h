// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Common-internal declarations shared by the three TUs of retail's Systems.cpp cluster:
// common_universe.cpp (loader, Universe:: API, registries), common_universe_system.cpp (SYSTEM)
// and common_universe_base.cpp (BASE). Nothing here is exported.

#include "common_universe.h"
#include "common_misc.h" // Geometry::Sphere
#include "common_net.h"  // CommReferrable (SYSTEM's second base)

#include <list>
#include <vector>

namespace CmnUniverse
{
    // Common.dll SYSTEM (RTTI .?AUSYSTEM@@): Universe::ISystem at +0 (vftable .rdata:063a5144)
    // and CommReferrable at +4 (vftable .rdata:063a5134, msg_id_prefix TString<64> at +8).
    // Retail layout, for reference (the port keeps the scalars on ISystem, see its comment):
    //   +0x4c id  +0x50 nickname  +0x54..+0x60 connection vector  +0x64 visit
    //   +0x68 strid_name  +0x6c ids_info  +0x70 file  +0x74 pos
    //   +0x80 zone list  +0x8c ISpatialPartition* (lazy)  +0x90 NavMapScale  +0x94 [Music] space
    // Retail builds one on the stack per [System] block of universe.ini (FUN_06337250
    // .text:06337376-06337441), copies it into the system map (FUN_0633e020/FUN_0633dea0) and
    // destroys the stack copy (FUN_0633aee0). The port keeps the records by value the same way.
    struct SYSTEM : public Universe::ISystem, public CommReferrable
    {
        std::vector<Universe::FSystemConnection> Connections; // +0x54 (begin +0x58)
        std::list<Universe::FSystemZone> Zones;               // +0x80 (head +0x84, size +0x88)
        mutable ISpatialPartition* Spatial;                   // +0x8c
        unsigned SpaceMusicId;                                // +0x94

        SYSTEM();
        ~SYSTEM();

        unsigned GetId() const override;
        TString<64> const& GetMsgIdPrefix() const override;
        char const* GetNickname() const override;
        int GetStridName() const override;
        int GetIdsInfo() const override;
        char const* GetFile() const override;
        std::list<Universe::FSystemZone> const* GetZones() const override;
        ISpatialPartition* GetZoneTree() const override;
        unsigned GetSphereProperties(Geometry::Sphere const* Query) const override;
        unsigned GetNestedProperties(Geometry::Sphere const* Query) const override;
        Universe::IZone const* GetSmallestZoneAt(Vector const* Point) const override;
        Universe::IZone const* GetSmallestPhysicalZoneAt(Vector const* Point) const override;
        Universe::IZone const* GetSmallestZoneWithPropertiesAt(Vector const* Point, unsigned Mask) const override;
        int GetConnectionCount() const override;
        bool GetConnection(unsigned Index, Universe::FSystemConnection* Out) const override;
        int GetObjectCount() const override;
        bool GetSystemObject(unsigned Index, Universe::FSystemObject* Out) const override;
        Vector const* GetPos() const override;
        unsigned char GetVisit() const override;
        void SetVisit(unsigned char NewVisit) override;
        float GetNavMapScale() const override;
        unsigned GetSpaceMusic() const override;

    private:
        // The lazy zone-tree build retail inlines at the head of slots 7-12
        // (.text:0633b036-0633b0b4 in slot 7).
        ISpatialPartition* BuildZoneTree() const;
    };

    // Common.dll BASE (vftable .rdata:063a4d5c): Universe::IBase's implementation. Every slot
    // is a single load or store of the field named in common_universe.h.
    struct BASE : public Universe::IBase
    {
        BASE();

        unsigned GetId() const override;
        char const* GetNickname() const override;
        int GetStridName() const override;
        char const* GetFile() const override;
        unsigned GetSystem() const override;
        char const* GetBgcsBaseRunBy() const override;
        unsigned GetShipSml01() const override;
        unsigned GetShipSml02() const override;
        unsigned GetShipSml03() const override;
        unsigned GetShipMdm01() const override;
        unsigned GetShipMdm02() const override;
        unsigned GetShipMdm03() const override;
        unsigned GetShipLrg01() const override;
        unsigned GetShipLrg02() const override;
        unsigned GetShipLrg03() const override;
        unsigned GetTerrainTiny() const override;
        unsigned GetTerrainSml() const override;
        unsigned GetTerrainMdm() const override;
        unsigned GetTerrainLrg() const override;
        unsigned GetTerrainDyna01() const override;
        unsigned GetTerrainDyna02() const override;
        bool IsAutosaveForbidden() const override;
        void SetSystem(unsigned NewSystemId) override;
        unsigned GetSpaceObjectId() const override;
        unsigned char GetSpaceObjectVisit() const override;
        void SetSpaceObjectId(unsigned NewSpaceObjectId) override;
        void SetSpaceObjectVisit(unsigned char NewVisit) override;
    };

    // The shared [Object] record pool of ISystem slots 15/16 and the SYSTEM whose file it
    // holds; with their loader, defined in common_universe.cpp.
    extern std::vector<Universe::FSystemObject> GSystemObjectPool;
    extern SYSTEM const* GSystemObjectPoolOwner;
    bool LoadSystemObjectPool(char const* RelFile);
}
