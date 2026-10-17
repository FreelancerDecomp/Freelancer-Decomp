// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"

// Common.dll Universe::Startup / Shutdown — Systems.cpp.
// Linked into freelancer.exe. Maps are ours (std::map), not VC6.
// See docs/re/common.md (Universe).

#include "common_id.h"
#include "common_ini.h"
#include "common_csys.h"          // Matrix / Transform (IZone orientation)
#include "common_weighted_vector.h" // IZone::Population
#include "common_tstring.h"         // TString<N> (ISystem::GetMsgIdPrefix, FSystemObject)

#include <list>

namespace Geometry { struct Sphere; struct Frustum; } // common_misc.h
namespace CmnAsteroid { struct LootableZoneEntry; }   // common_casteroid.h
#include "common_spatial.h"                           // ISpatialPartition, NewSpatial / FreeSpatial

// ?DaysPerSecond@@3NA — not nested in Universe. Default 1/60 before [Time].
COMMON_API extern double DaysPerSecond;

namespace Archetype
{
    struct Asteroid;
    struct Equipment;
    struct Explosion;
    struct MotorData;
    struct Root;
    struct Ship;
    struct Solar;

    // listing 0x91250. All six share one body: return Name.Id (ID_String is a uint).
    // ?GetAsteroidID@Archetype@@YAIABUID_String@@@Z
    COMMON_API unsigned __cdecl GetAsteroidID(ID_String const& Name);
    COMMON_API unsigned __cdecl GetDynamicAsteroidID(ID_String const& Name);
    COMMON_API unsigned __cdecl GetEquipmentID(ID_String const& Name);
    COMMON_API unsigned __cdecl GetShipID(ID_String const& Name);
    COMMON_API unsigned __cdecl GetSimpleID(ID_String const& Name);
    COMMON_API unsigned __cdecl GetSolarID(ID_String const& Name);

    // listing 0x911e0 / 0x91260. ArchDB GSimpleDb.Get — FDUMP Archetype.cpp 0x3c / 0x49.
    COMMON_API Root* __cdecl GetSimple(unsigned NicknameId);
    COMMON_API Root* __cdecl GetSimpleByName(ID_String const& Name);

    // listing 0x996c0 / 0x997a0. ArchDB GAsteroidDb.Get.
    COMMON_API Asteroid* __cdecl GetAsteroid(unsigned NicknameId);
    COMMON_API Asteroid* __cdecl GetAsteroidByName(ID_String const& Name);

    // listing 0x93cd0 / 0x93d40. ArchDB GSolarDb.Get.
    // ?GetSolar@Archetype@@YAPAUSolar@1@I@Z
    // ?GetSolarByName@Archetype@@YAPAUSolar@1@ABUID_String@@@Z
    COMMON_API Solar* __cdecl GetSolar(unsigned NicknameId);
    COMMON_API Solar* __cdecl GetSolarByName(ID_String const& Name);

    // listing 0x93220 / 0x93290. ArchDB GShipDb.Get.
    // ?GetShip@Archetype@@YAPAUShip@1@I@Z
    // ?GetShipByName@Archetype@@YAPAUShip@1@ABUID_String@@@Z
    COMMON_API Ship* __cdecl GetShip(unsigned NicknameId);
    COMMON_API Ship* __cdecl GetShipByName(ID_String const& Name);

    // listing 0x993f0 / 0x99460. ArchDB GEquipmentDb.Get.
    // ?GetEquipment@Archetype@@YAPAUEquipment@1@I@Z
    // ?GetEquipmentByName@Archetype@@YAPAUEquipment@1@ABUID_String@@@Z
    COMMON_API Equipment* __cdecl GetEquipment(unsigned NicknameId);
    COMMON_API Equipment* __cdecl GetEquipmentByName(ID_String const& Name);

    // listing 0xabd10. GExplosionDb map find — silent miss, no ArchDB::Get FDUMP.
    // ?GetExplosion@Archetype@@YAPAUExplosion@1@ABUID_String@@@Z
    COMMON_API Explosion* __cdecl GetExplosion(ID_String const& Name);
}

namespace Universe
{
    struct IZone;

    // One jump connection of a system: the 0x20-byte record the universe loader pushes onto
    // SYSTEM+0x54's vector after every [Object] carrying a `goto` key (FUN_06337250
    // .text:06338155-063381cb, push_back FUN_0633e770). ISystem slots 13/14 hand them out;
    // Server.dll pub::System::EnumerateConnections is the only reader.
    struct FSystemConnection
    {
        unsigned GotoSystemId;  // +0x00 CreateID(goto value 0)
        ID_String Object;       // +0x04 the [Object] nickname id (ID_String::clear, then set)
        unsigned ObjectId;      // +0x08 the same nickname id
        Vector Pos;             // +0x0c the [Object] pos (default -0.1f each)
        unsigned char Visit;    // +0x18 the [Object] visit byte
        unsigned GateType;      // +0x1c ESolarGateType of the goto object's solar archetype
    };
    static_assert(sizeof(FSystemConnection) == 0x20, "FSystemConnection size is 0x20");

    // One [Object] record of a system file as FUN_06336380 reads it (memset 0x118, then the
    // keys below). ISystem slots 15/16 hand them out from the shared object pool; Server.dll
    // pub::System::EnumerateObjects and pub::GF::GetBasePosition are the readers.
    struct FSystemObject
    {
        TString<64> Nickname;   // +0x00 "nickname"
        Vector Pos;             // +0x44 "pos"
        unsigned ArchetypeId;   // +0x50 "archetype" (CreateID -> Archetype::GetAsteroidID)
        int IdsName;            // +0x54 "ids_name"
        int IdsInfo;            // +0x58 "ids_info" / "ids_info_card" / "infocard_ids"
        TString<32> Reputation; // +0x5c "reputation"
        unsigned DockWith;      // +0x80 "dock_with"
        unsigned Goto;          // +0x84 "goto"
        unsigned BaseId;        // +0x88 "base" (0 when get_base fails)
        TString<64> NextRing;   // +0x8c "next_ring"
        TString<64> PrevRing;   // +0xd0 "prev_ring"
        unsigned char Visit;    // +0x114 "visit"; forced to 1 by a "star" key
    };
    static_assert(sizeof(FSystemObject) == 0x118, "FSystemObject size is 0x118");

    // One entry of SYSTEM+0x80's zone list: the INI_Reader state captured at the zone's
    // [Zone] header (.text:063375c8) and the zone as it sits in the zone map (list node
    // +0x08 / +0xc10). FUN_0633e920 appends it only when the zone map insert succeeded.
    struct FSystemZone
    {
        INI_Reader::State State;
        IZone const* Zone;
    };

    // Universe::ISystem -- the interface of Common.dll's SYSTEM (RTTI .?AUSYSTEM@@, primary
    // vftable .rdata:063a5144, 22 slots, no destructor slot). Retail ISystem carries no data;
    // SYSTEM (src/Common/common_universe_internal.h, common_universe_system.cpp) is ISystem + CommReferrable + the tables the
    // slots hand out. The data members below are the port's storage for the scalar slots
    // (0, 2, 3, 4, 5, 17, 18/19, 20): they stay on the interface because about seventy
    // readers across Common, Server and freelancer read them directly. SYSTEM's slots return
    // exactly these fields, so both paths agree. Slots are named after the field or behaviour
    // their retail body shows.
    struct ISystem
    {
        virtual unsigned GetId() const = 0;                  // +0x00 062969c0  [+0x4c] id (formerly Slot00)
        // +0x04 0633afa0: ADD ECX,4; JMP CommReferrable::GetMsgIdPrefix. The same name as
        // CommReferrable's slot 1, so SYSTEM's one override fills both tables, as in retail
        // (the CommReferrable-side thunk LAB_063415d0 subtracts 4 and lands here).
        virtual TString<64> const& GetMsgIdPrefix() const = 0;
        virtual char const* GetNickname() const = 0;         // +0x08 063390f0  [+0x50] nickname (formerly Slot02)
        virtual int GetStridName() const = 0;                // +0x0c 0633aff0  [+0x68] strid_name (formerly Slot03)
        virtual int GetIdsInfo() const = 0;                  // +0x10 0633b000  [+0x6c] ids_info (formerly Slot04)
        virtual char const* GetFile() const = 0;             // +0x14 0629ba60  [+0x70] file (formerly Slot05)
        // +0x18 0633b020: LEA +0x80, the zone list the loader filled (formerly Slot06).
        virtual std::list<FSystemZone> const* GetZones() const = 0;
        // +0x1c 0633b030: the zone BSP, built from the zone list on first use (formerly Slot07).
        virtual ISpatialPartition* GetZoneTree() const = 0;
        // +0x20 0633b0c0: the property flags at a sphere -- the OR of every touched zone whose
        // flags carry none of 0x7fc0, plus the flags of the smallest-radius touched zone that
        // carries some (formerly Slot08).
        virtual unsigned GetSphereProperties(Geometry::Sphere const* Query) const = 0;
        // +0x24 0633b230: the OR of the property flags of every zone the sphere touches;
        // Server's pub::System::GetNestedProperties forwards to it (formerly Slot09).
        virtual unsigned GetNestedProperties(Geometry::Sphere const* Query) const = 0;
        // +0x28 0633b350: the smallest-radius zone containing Point (formerly Slot10).
        virtual IZone const* GetSmallestZoneAt(Vector const* Point) const = 0;
        // +0x2c 0633b4b0: as +0x28, among zones with a physical effect (formerly Slot11).
        virtual IZone const* GetSmallestPhysicalZoneAt(Vector const* Point) const = 0;
        // +0x30 0633b620: as +0x28, among zones whose property flags share a bit with Mask
        // (formerly Slot12).
        virtual IZone const* GetSmallestZoneWithPropertiesAt(Vector const* Point, unsigned Mask) const = 0;
        // +0x34 0633b790: the number of FSystemConnection records (formerly Slot13).
        virtual int GetConnectionCount() const = 0;
        // +0x38 0633b7b0: copies connection [Index] when it exists; retail returns false on
        // every path (formerly Slot14).
        virtual bool GetConnection(unsigned Index, FSystemConnection* Out) const = 0;
        // +0x3c 0633b7f0: loads the shared object pool from this system's file (FUN_06336750)
        // unless it already holds it, then the pool's 0x118-byte record count (formerly Slot15).
        virtual int GetObjectCount() const = 0;
        // +0x40 0633b840 (RET 8): copies object [Index] of the pool; false past either count
        // (formerly Slot16). Not "GetObject": <wingdi.h> defines that name as a macro.
        virtual bool GetSystemObject(unsigned Index, FSystemObject* Out) const = 0;
        virtual Vector const* GetPos() const = 0;            // +0x44 0633b010  LEA +0x74 pos (formerly Slot17)
        virtual unsigned char GetVisit() const = 0;          // +0x48 0633afb0  byte [+0x64] visit (formerly Slot18)
        virtual void SetVisit(unsigned char NewVisit) = 0;   // +0x4c 0633afe0  set [+0x64] (formerly Slot19)
        virtual float GetNavMapScale() const = 0;            // +0x50 0633afc0  [+0x90] NavMapScale (formerly Slot20)
        virtual unsigned GetSpaceMusic() const = 0;          // +0x54 0633afd0  [+0x94] [Music] space (formerly Slot21)

        unsigned NicknameId;
        char const* Nickname;
        char const* File;
        int StridName;
        int IdsInfo;
        unsigned char Visit;
        float NavMapScale;
        Vector Pos;
    };

    // Universe::IBase -- the interface of Common.dll's BASE (vftable .rdata:063a4d5c, 27 slots,
    // no destructor slot; the universe loader builds one per [Base] block of universe.ini,
    // FUN_06337250 .text:063386d0-06338cc9). Every slot is a one-instruction getter/setter of
    // a BASE field and is named after that field's universe.ini key.
    // As with ISystem and IZone, the data members stay on the interface in retail BASE's offset
    // order because readers across Common, Server and freelancer read them directly; the slots
    // return exactly these fields.
    struct IBase
    {
        // Slots +0x00..+0x54 are the getters of the [Base] keys (formerly Slot00..Slot21).
        virtual unsigned GetId() const = 0;                 // +0x00 062d1ad0  [+0x04] nickname id
        virtual char const* GetNickname() const = 0;        // +0x04 062a66a0  [+0x08] nickname
        virtual int GetStridName() const = 0;               // +0x08 0629fb90  [+0x0c] strid_name
        virtual char const* GetFile() const = 0;            // +0x0c 06263a70  [+0x10] file
        virtual unsigned GetSystem() const = 0;             // +0x10 06263bb0  [+0x14] system
        // +0x14 0633ba50: c_str of the BGCS_base_run_by string (its buffer, or the library's
        // shared empty string when it never got one).
        virtual char const* GetBgcsBaseRunBy() const = 0;
        virtual unsigned GetShipSml01() const = 0;          // +0x18 063275a0  [+0x30] ship_sml_01
        virtual unsigned GetShipSml02() const = 0;          // +0x1c 063275c0  [+0x34] ship_sml_02
        virtual unsigned GetShipSml03() const = 0;          // +0x20 063275e0  [+0x38] ship_sml_03
        virtual unsigned GetShipMdm01() const = 0;          // +0x24 0626c6b0  [+0x3c] ship_mdm_01
        virtual unsigned GetShipMdm02() const = 0;          // +0x28 062a6690  [+0x40] ship_mdm_02
        virtual unsigned GetShipMdm03() const = 0;          // +0x2c 062a33b0  [+0x44] ship_mdm_03
        virtual unsigned GetShipLrg01() const = 0;          // +0x30 0633ba60  [+0x48] ship_lrg_01
        virtual unsigned GetShipLrg02() const = 0;          // +0x34 062969c0  [+0x4c] ship_lrg_02
        virtual unsigned GetShipLrg03() const = 0;          // +0x38 063390f0  [+0x50] ship_lrg_03
        virtual unsigned GetTerrainTiny() const = 0;        // +0x3c 0633ba70  [+0x54] terrain_tiny
        virtual unsigned GetTerrainSml() const = 0;         // +0x40 0633ba80  [+0x58] terrain_sml
        virtual unsigned GetTerrainMdm() const = 0;         // +0x44 0633ba90  [+0x5c] terrain_mdm
        virtual unsigned GetTerrainLrg() const = 0;         // +0x48 0633baa0  [+0x60] terrain_lrg
        virtual unsigned GetTerrainDyna01() const = 0;      // +0x4c 0633bab0  [+0x64] terrain_dyna_01
        virtual unsigned GetTerrainDyna02() const = 0;      // +0x50 0633aff0  [+0x68] terrain_dyna_02
        virtual bool IsAutosaveForbidden() const = 0;       // +0x54 0633bac0  byte [+0x6c] autosave_forbidden
        virtual void SetSystem(unsigned NewSystemId) = 0;   // +0x58 0633bad0  [+0x14] = arg (formerly Slot22)
        virtual unsigned GetSpaceObjectId() const = 0;      // +0x5c 0633baf0  [+0x28] (formerly Slot23)
        virtual unsigned char GetSpaceObjectVisit() const = 0; // +0x60 0633bb10  byte [+0x2c] (formerly Slot24)
        virtual void SetSpaceObjectId(unsigned NewSpaceObjectId) = 0; // +0x64 0633bae0  [+0x28] = arg (formerly Slot25)
        virtual void SetSpaceObjectVisit(unsigned char NewVisit) = 0; // +0x68 0633bb00  byte [+0x2c] = arg (formerly Slot26)

        unsigned NicknameId;           // +0x04 ID_String "nickname"
        char const* Nickname;          // +0x08 CacheString (StringAlloc'd "nickname")
        int StridName;                 // +0x0c "strid_name"
        char const* File;              // +0x10 CacheString (StringAlloc'd "file")
        unsigned SystemId;             // +0x14 ID_String "system"
        std::string BgcsBaseRunBy;     // +0x18 "BGCS_base_run_by" (a std::string in retail too)
        // +0x28 / +0x2c: the nickname and the visit byte of the system [Object] that carries
        // this base (base = this base, ids_name == strid_name), stored through slots 25/26 by
        // FUN_06337250 .text:06338225 / .text:06338231. The [Base] reader zeroes both
        // (.text:0633877c / .text:06338780).
        unsigned SpaceObjectId;        // +0x28
        unsigned char SpaceObjectVisit;// +0x2c
        unsigned ShipSml[3];           // +0x30 ship_sml_01..03
        unsigned ShipMdm[3];           // +0x3c ship_mdm_01..03
        unsigned ShipLrg[3];           // +0x48 ship_lrg_01..03
        unsigned TerrainTiny;          // +0x54
        unsigned TerrainSml;           // +0x58
        unsigned TerrainMdm;           // +0x5c
        unsigned TerrainLrg;           // +0x60
        unsigned TerrainDyna01;        // +0x64
        unsigned TerrainDyna02;        // +0x68
        bool bAutosaveForbidden;       // +0x6c
    };

    // FUN_063362d0 table PTR_s_sphere_063edb80: name -> int (stricmp).
    // 0 = invalid / miss. CONFIRMED (2026-09-10, d216:izone-shape-investigate lane,
    // decomp-tracker items 2574/11429): the retail IZone-shape-base class's vtable slot
    // 0x18 (PTR_LAB_063a5094+0x18, Common.dll.txt:568507) is ICF-folded byte-identical
    // with Archetype::Root::GetRef, `MOV EAX,[this+0x3c]; RET` -- i.e. it just returns
    // this enum's value directly, matching the field this port names `Shape` below
    // exactly (populated by ParseZoneShape, common_universe.cpp). See
    // CmnAsteroid::CAsteroidField::load's "exclude_dynamic_asteroids" key
    // (src/Common/common_casteroidfield.cpp) for the one confirmed live consumer.
    enum EZoneShape
    {
        ZoneShapeInvalid = 0,
        ZoneShapeSphere = 1,
        ZoneShapeEllipsoid = 2,
        ZoneShapeBox = 3,
        ZoneShapeCylinder = 4,
        ZoneShapeRing = 5,
        ZoneShapeMesh = 6,
    };

    // Universe::IZoneImmersion / Universe::IZone -- the interface of Common.dll's ZONE (RTTI
    // .?AUZONE@@ : .?AUIZone@Universe@@ : .?AUIZoneImmersion@Universe@@, single inheritance,
    // one vftable .rdata:063a5094 of 37 slots +0x00..+0x90). There is NO destructor slot:
    // ZONE's destructor FUN_06339010 is non-virtual and is called directly (06337dee,
    // 0639774d). The concrete class is ZONE (src/include/common_zone.h,
    // src/Common/common_zone.cpp); nothing outside Common.dll constructs one.
    //
    // Retail interfaces carry no data. The data members below are the port's storage for the
    // getter slots: they stay on the interface, in retail ZONE's offset order, because about
    // a hundred and fifty readers across Common, Server and freelancer read them directly
    // (the same arrangement as ISystem above). Every slot that returns one of them returns
    // exactly that field, so both paths agree. The offsets in the comments are retail ZONE's.
    //
    // Slot names come from the Server.dll pub::Zone export that forwards to the slot (for
    // example ?InZone@Zone@pub@@ calls slot +0x34 and nothing else) or, where no export
    // forwards, from the field or behaviour the retail body shows. +0x20 keeps the placeholder
    // (the meaning of its flag bits is unproven).
    struct IZoneImmersion
    {
        // +0x00 LAB_0633ac30. 0.0f when Point is outside the bounding sphere (slot +0x30) or
        // when ClassifyPoint (slot +0x48) is >= 1; otherwise 1.0f, or the edge ramp
        // (1 - ClassifyPoint) / EdgeFraction inside the outer EdgeFraction band.
        //
        // Slots +0x00, +0x3c, +0x40 and +0x48 return double: retail leaves the result on the
        // x87 stack unrounded and its callers consume it there (for example the nebula
        // exclusion check FCOMPs slot +0x00 against 1.0 at .text:062bcce6).
        virtual double GetImmersion(Vector const& Point) const = 0; // (formerly Slot00)
    };

    struct IZone : IZoneImmersion
    {
        virtual unsigned GetId() const = 0;                     // +0x04 062d1ad0  [+0x04]
        virtual unsigned GetSystem() const = 0;                 // +0x08 062a66a0  [+0x08]
        // +0x0c 062d1ae0: LEA +0x0c, the orientation and position as one transform (formerly Slot0C).
        virtual Transform const& GetTransform() const = 0;
        virtual Vector const& GetPos() const = 0;               // +0x10 063390a0  LEA +0x30
        virtual Matrix const& GetOrientation() const = 0;       // +0x14 062d1ae0  LEA +0x0c
        virtual unsigned GetShape() const = 0;                  // +0x18 0626c6b0  [+0x3c]
        virtual unsigned long GetProperties() const = 0;        // +0x1c 062a6690  [+0x40]
        virtual bool Slot20() const = 0;                        // +0x20 063390b0  flags & 0x18000
        virtual Vector const& GetFogColor() const = 0;          // +0x24 063390c0  LEA +0x78 property_fog_color (formerly Slot24)
        virtual Vector GetSize() const = 0;                     // +0x28 063390d0  copy of +0x44
        virtual long GetMeshHandle() const = 0;                 // +0x2c 063390f0  [+0x50] (formerly Slot2C)
        virtual float GetRadius() const = 0;                    // +0x30 06339100  cached +0x68

        // W6-ZONE: ready for pub::System::InZones
        // +0x34 06339220. true when Point is inside the zone grown by Margin.
        virtual bool InZone(Vector const& Point, float Margin) const = 0;
        // +0x38 06339670. InZone's twin in the zone's XZ plane: the height is ignored
        // (formerly Slot38).
        virtual bool InZoneXZ(Vector const& Point, float Margin) const = 0;
        // +0x3c 06339b00. Signed distance to the surface (negative inside).
        virtual double GetDistance(Vector const& Point) const = 0;
        // +0x40 06339ff0. Distance from the centre to the surface along a world direction
        // (formerly Slot40).
        virtual double GetRadiusAlong(Vector const& Direction) const = 0;
        // +0x44 0633a290. The surface point that belongs to Point (formerly Slot44).
        virtual Vector GetSurfacePoint(Vector const& Point) const = 0;
        // W6-ZONE: ready for pub::System::InZones
        // +0x48 0633a6e0. 1.0 on the surface, below 1 inside, above 1 outside.
        virtual double ClassifyPoint(Vector const& Point) const = 0;
        // +0x4c 0633aa40. First point where Start->End enters the zone.
        virtual bool Intersect(Vector const& Start, Vector const& End, Vector& OutHit) const = 0;

        virtual float GetEdgeFraction() const = 0;              // +0x50 0633ac20  [+0x54] (formerly Slot50)
        virtual bool HasPhysicalEffect() const = 0;             // +0x54 0633ad10  lazy [+0x58] (formerly Slot54)
        virtual float GetDragModifier() const = 0;              // +0x58 0633ad70  [+0x5c] (formerly Slot58)
        virtual float GetDamage() const = 0;                    // +0x5c 0633ad80  [+0x60] (formerly Slot5C)
        virtual float GetInterference() const = 0;             // +0x60 0633ad90  [+0x64] (formerly Slot60)
        // +0x64 0633add0: the number of `faction` population entries (formerly Slot64).
        virtual int GetPopulationCount() const = 0;
        // +0x68 0633adf0: population entry [Index] -> {faction id, weight}; false when out of
        // range (formerly Slot68).
        virtual bool GetPopulationEntry(int Index, unsigned& OutId, float& OutWeight) const = 0;
        virtual weighted_vector<unsigned int> const& GetPopulation() const = 0; // +0x6c 0633ae40
        virtual bool HasPathLabel() const = 0;                  // +0x70 0633ae50  byte [+0x88] path_label (formerly Slot70)
        // +0x74 0633ada0: *[ESP+4] = [+0x6c], EAX = that pointer, RET 4 -- the `music` id as an
        // ID_String returned through the hidden result pointer, as slot +0x28 returns its
        // Vector (formerly Slot74).
        virtual ID_String GetMusic() const = 0;
        // +0x78 0633adb0: the spacedust effect name and spacedust_maxparticles; returns whether
        // the zone has spacedust (flag 0x200000) (formerly Slot78).
        virtual bool GetSpacedust(char const*& OutSpacedust, int& OutMaxParticles) const = 0;
        virtual CmnAsteroid::LootableZoneEntry* GetLootableInfo() const = 0; // +0x7c 06262dd0 [+0x84]
        virtual unsigned char GetVisit() const = 0;             // +0x80 0633ae60  byte [+0x9c] visit (formerly Slot80)
        virtual int GetName() const = 0;                        // +0x84 0633ae80  [+0xa0]
        virtual int GetIdsInfo() const = 0;                     // +0x88 0633ae90  [+0xa4] ids_info (formerly Slot88)
        // +0x8c 0633aea0: free the owned record, set +0x84 (formerly Slot8C).
        virtual void SetLootableInfo(CmnAsteroid::LootableZoneEntry* LootEntry) = 0;
        virtual void SetVisit(unsigned char NewVisit) = 0;      // +0x90 0633ae70  +0x9c = v | 0x20 (formerly Slot90)

        unsigned NicknameId;               // +0x04 ID_String "nickname"
        unsigned SystemId;                 // +0x08
        Matrix Orientation;                // +0x0c EulerMatrix("rotate"); identity by default
        Vector Pos;                        // +0x30 "pos" (with Orientation: a Transform)
        unsigned Shape;                    // +0x3c EZoneShape
        unsigned PropertyFlags;            // +0x40
        Vector Size;                       // +0x44
        long MeshHandle = -1;              // +0x50 Archetype::Load("mesh"); -1 when none
        float EdgeFraction;                // +0x54 clamped to [0, 1]
        mutable int PhysicalEffect = -1;   // +0x58 slot +0x54's cache, -1 until computed
        float DragModifier;                // +0x5c
        float Damage;                      // +0x60
        float Interference;                // +0x64
        mutable float CachedRadius = -1.0f;// +0x68 slot +0x30's cache, -1 until computed
        unsigned MusicId;                  // +0x6c
        char const* Spacedust;             // +0x70
        int SpacedustMaxParticles;         // +0x74
        Vector FogColor;                   // +0x78 property_fog_color / 255
        CmnAsteroid::LootableZoneEntry* OwnedLootEntry = nullptr; // +0x84 slot +0x8c's record (malloc'd)
        bool bPathLabel;                   // +0x88
        // +0x8c the faction population: one {MakeId(faction), weight} pair per `faction =`
        // line (FUN_06337250 .text:06337c38-06337cac). Freed by the destructor (.text:0633906a).
        weighted_vector<unsigned int> Population;
        unsigned char Visit;               // +0x9c
        int IdsName;                       // +0xa0
        int IdsInfo;                       // +0xa4
        // (No nickname text: retail ZONE keeps only the id at +0x04; get_zone(system, name)
        // hashes the name it is given.)
    };

    // listing 06338131: Solar+0x10 ObjectType bits after GetSolarByName on goto
    // (FSystemConnection::GateType).
    enum ESolarGateType
    {
        SolarGateNone = 0,
        SolarGateJumpGate = 1,   // JUMP_GATE 0x40
        SolarGateJumpHole = 2,   // JUMP_HOLE 0x800
        SolarGateAirlock = 3,    // AIRLOCK_GATE 0x400
    };

    // bool __cdecl Universe::Startup(char const *)
    // ?Startup@Universe@@YA_NPBD@Z
    COMMON_API bool __cdecl Startup(char const* RelPath);

    // void __cdecl Universe::Shutdown(void)
    // ?Shutdown@Universe@@YAXXZ
    COMMON_API void __cdecl Shutdown();

    COMMON_API unsigned __cdecl num_systems();
    COMMON_API unsigned __cdecl get_system_id(char const* Nickname);
    COMMON_API ISystem const* __cdecl get_system(unsigned NicknameId);
    // listing 0xdc650 — IFS GUniverseDirFs +0x84 + _makepath; else memcpy RelPath.
    COMMON_API void __cdecl get_filename(char* Dest, char const* RelPath);
    COMMON_API ISystem* __cdecl GetFirstSystem();
    COMMON_API ISystem* __cdecl GetNextSystem();

    COMMON_API unsigned __cdecl num_bases();
    COMMON_API unsigned __cdecl get_base_id(char const* Nickname);
    COMMON_API IBase* __cdecl get_base(unsigned NicknameId);
    COMMON_API IBase* __cdecl GetFirstBase();
    COMMON_API IBase* __cdecl GetNextBase();

    // listing 0633c310 / 0633c380. Global map by zone CreateID (system id unused).
    COMMON_API IZone const* __cdecl get_zone(unsigned NicknameId);
    COMMON_API IZone const* __cdecl get_zone(unsigned SystemId, char const* Nickname);
    COMMON_API IZone* __cdecl get_non_const_zone(unsigned SystemId, char const* Nickname);
    COMMON_API IZone* __cdecl get_non_const_zone(unsigned SystemId, unsigned NicknameId);
    COMMON_API IZone const* __cdecl first_zone(unsigned SystemId);
    COMMON_API IZone const* __cdecl next_zone(IZone const* Current);

    // listing 0633b5e0. Gate map: object CreateID → goto system CreateID.
    COMMON_API unsigned __cdecl get_gate_system(unsigned NicknameId);
}

// ISpatialPartition, FSpatialEntry, FSpatialQueryResult, NewSpatial / FreeSpatial and the
// SpatialFill helpers: common_spatial.h (included at the top).
