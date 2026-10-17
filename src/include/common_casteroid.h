// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"
#include "common_cobject.h"
#include "common_net.h"

#include <list>
#include <map>
#include <vector>

// CmnAsteroid: the asteroid FIELD simulation (Common.dll.c ~0x43bb0..0x462a0, retail
// addresses 0x62a2ff0-0x62a6190 per decomp-tracker). Distinct from the CAsteroid /
// CDynamicAsteroid *object* classes in common_cobject.h -- this namespace owns the grid of
// cubes that spawn and cull them.
//
// Scope note: several retail internals here are hand-rolled VC6 std::list / std::map
// (red-black tree) node layouts. Following this codebase's established convention for
// such cases (see CArchGrpTraverser in common_cobject.h), the port uses real std::map /
// std::list containers with equivalent observable behavior instead of replicating the
// raw node byte layout -- so most classes below carry no static_assert on their layout.
class INI_Reader;
struct IObjDB;
struct IObjRW;

namespace CmnAsteroid
{
    // Common.dll.c 0x452a0 (CAsteroidField::update) stores one of these per cube.
    // CubeStateActive=1 resolved 2026-09-10 (claude-casteroid-spawn-pipeline lane, item
    // 70309's own trace): CAsteroidField::populate_asteroids_internal (Common.dll.txt
    // 131989-131991) stamps a freshly map-inserted cube's State to 1 immediately, BEFORE
    // CAsteroidCube::init() populates it with asteroids; the same function's existing-cube
    // refresh path (131875-131878) treats State==1 exactly like State==2 (Near) -- skip,
    // don't re-touch -- confirming 1 means "freshly created and actively populated",
    // distinct from CubeStateNear's "still in range, timer reset" meaning. update()'s own
    // handling of State==1 was not re-traced this pass (out of scope -- update() remains
    // its own `simplified` item); this port's update() currently only transitions cubes
    // out of CubeStateUnknown, so newly-active cubes are left alone until a future lane
    // re-verifies update()'s full State machine.
    enum CubeState
    {
        CubeStateUnknown = 0,
        CubeStateActive = 1,
        CubeStateNear = 2,
        CubeStateFar = 3,
    };

    // CmnAsteroid::compute_cube_orientation (Common.dll.c 0x44010) indexes each axis with
    // a 0..3 random value into a 4-entry preset list; CAsteroidField's ctor (0x44660)
    // initializes the default list for all 3 axes to exactly {0, PI/2, PI, 3*PI/2}
    // (0x50-0x5c / 0x60-0x6c / 0x70-0x7c), i.e. axis-aligned "snap" rotations.
    struct AxisRotations
    {
        float Angles[3][4];
    };

    static_assert(sizeof(AxisRotations) == 0x30, "CmnAsteroid::AxisRotations sizeof 0x30");

    // CmnAsteroid::closest_cube_coord (Common.dll.c 0x42ff0). Snaps Coord to the nearest
    // multiple of CubeSize plus/minus HalfSize depending on sign.
    COMMON_API int __cdecl closest_cube_coord(int Coord, int CubeSize, int HalfSize);

    // CmnAsteroid::compute_cube_id (Common.dll.c 0x46010). Packs a position's 128-unit
    // grid coordinate into a single id: bit 0x15+ = X (11 bits + sign), bits 10-0x14 = Z
    // (10 bits + sign folded into bit 10), bits 0-9 = Y (9 bits + sign folded into bit
    // 0x200). Returns 0 if any axis falls outside the packable range.
    COMMON_API unsigned long __cdecl compute_cube_id(Vector const& Pos);

    // CmnAsteroid::compute_cube_orientation (Common.dll.txt 0x44010 -- raw disasm only,
    // the Ghidra .c decompiler dropped the ftol() call arguments here so this was
    // reconstructed by hand from the FPU/LCG instruction trace). Reseeds a CRT-rand-style
    // LCG directly from Seed (bypassing the process-wide rand() stream), burns 0-3 extra
    // steps, then (unless Freq < 0) rolls one probability draw against Freq and bails
    // returning false on failure. On success draws one axis-preset index per axis from
    // Rot and composes three row-mixing rotations (rows 1&2, then 0&2, then 0&1) directly
    // into *InOutOrient -- so the caller must pre-initialize *InOutOrient (retail always
    // passes an identity matrix here; composing 90-degree-multiple presets onto identity
    // is what gives CAsteroidField its axis-aligned cube "snap" rotations).
    COMMON_API bool __cdecl compute_cube_orientation(unsigned long Seed, Matrix* InOutOrient, AxisRotations const& Rot, float Freq);

    // CmnAsteroid::CAsteroidField::load's "[LootableZone]" header record (Common.dll.c
    // 72686-72701, Common.dll.txt 130673-130696 -- retail heap-allocates one of these,
    // operator_new(0x28), per [LootableZone] block: 4x ID_String::clear() at offsets
    // 0x00/0x04/0x08/0x0c, then 6 defaulted dwords: 0x10=1/0x14=1/0x18=1/0x1c=1/0x20=100/
    // 0x24=1). Two ID_String slots (+0x04/+0x0c) and three of the six trailing dwords
    // (+0x18/+0x1c/+0x24) are written for real by the "dynamic_loot_container"/"dynamic_
    // loot_commodity"/"dynamic_loot_count"/"dynamic_loot_difficulty" keys (see load()).
    // The other two ID_String slots (+0x00/+0x08) and the other three dwords (+0x10/+0x14/
    // +0x20) are recognized-but-inert in this exact retail build: the "asteroid_loot_
    // container"/"asteroid_loot_commodity"/"asteroid_loot_count"/"asteroid_loot_difficulty"
    // keys are matched by is_value() purely so the parser doesn't desync (Common.dll.txt
    // 130799-130817 is four bare is_value() calls back-to-back with no get_value_*/store
    // between any of them, cross-checked against the raw disasm, not just the Ghidra .c),
    // but nothing ever writes to these four slots -- they stay at their ctor default
    // forever in every traced path. Modeled here as named fields carrying their confirmed
    // retail default value (this project's placeholder-field policy) rather than omitted,
    // since they are real bytes retail still passes on to the [LootableZone]-target zone's
    // IZone::vtbl[0x8c] call (see load()'s own note -- that call itself is unported).
    struct LootableZoneEntry
    {
        ID_String AsteroidLootContainerId;  // +0x00, default-cleared; "asteroid_loot_container" recognized-but-dead (see above)
        ID_String DynamicLootContainerId;   // +0x04, "dynamic_loot_container"
        ID_String AsteroidLootCommodityId;  // +0x08, default-cleared; "asteroid_loot_commodity" recognized-but-dead (see above)
        ID_String DynamicLootCommodityId;   // +0x0c, "dynamic_loot_commodity"
        int AsteroidLootCountMin;           // +0x10, default 1; "asteroid_loot_count" recognized-but-dead (see above)
        int AsteroidLootCountMax;           // +0x14, default 1; ditto
        int DynamicLootCountMin;            // +0x18, "dynamic_loot_count" value 0, clamped >=1 then min(Min,Max)
        int DynamicLootCountMax;            // +0x1c, "dynamic_loot_count" value 1, clamped >=1
        int AsteroidLootDifficulty;         // +0x20, default 100; "asteroid_loot_difficulty" recognized-but-dead (see above)
        int DynamicLootDifficulty;          // +0x24, "dynamic_loot_difficulty", clamped [1,100]
    };

    static_assert(sizeof(LootableZoneEntry) == 0x28, "CmnAsteroid::LootableZoneEntry sizeof 0x28");

    class CAsteroidField;

    // CmnAsteroid::CAsteroidField's per-field "AsteroidCubeArch" spawn list (retail
    // CAsteroidField+4 = Head / +8 = Count of a VC6 intrusive circular list). Confirmed by
    // cross-referencing two independent sites: (1) CAsteroidField's ctor (Common.dll.c
    // 72180-72183) allocates a 0x40-byte self-referential sentinel node via operator_new(0x40)
    // and stores it at this+4 (Head), zeroing this+8 (Count) -- the same VC6 circular
    // intrusive-list idiom used elsewhere in this codebase (e.g. Archetype::EqObj's
    // CollisionGroups/ShieldLinks); (2) CAsteroidField::load's "asteroid" INI key
    // (Common.dll.c 72423-72588) builds exactly one 0x40-byte node -- Archetype::Asteroid* at
    // node+8, 3 extent floats at node+0xc/0x10/0x14, a 3x3 orientation Matrix at
    // node+0x18..+0x3c, bMine/bInstantKill bytes at node+0x3c/0x3d -- and appends it via
    // FUN_062a6930(&tmp, headNode, &out). CAsteroidCube::init (item 70318, Common.dll.txt
    // 132662-132894) walks this SAME list to spawn one CAsteroid per node: its own
    // "AsteroidCubeArch*" parameter is literally the owning CAsteroidField pointer
    // reinterpreted (param+4/param+8 land exactly on this Head/Count pair -- confirmed by the
    // empty-list check `*(int**)(param+4) == param+4`, the classic VC6 "Next of the sentinel
    // points at itself" idiom). Ported as a std::list<AsteroidCubeArchEntry> (a list as in
    // retail: entries never move, which matters because each one releases its archetype).
    //
    // Lifetime (retail): load() builds a temporary entry {Arch 0, Extent 0, Orient identity
    // (FUN_062748a0), flags 0}, appends a plain copy (FUN_062a6930, no AddRef), destroys the
    // temporary (its Arch is 0, so nothing happens -- .text:062a4b05..062a4b18), then stores
    // the archetype into the new node with a raw MOV. Destroying a node releases its archetype
    // (FUN_062a69f0 .text:062a6a15..062a6a1c: Arch != 0 -> Archetype::Root::Release), which
    // balances the Root::load() that load() performed. Copies (the field copy ctor,
    // FUN_062a6980) are plain as well, so a copied list releases the same archetypes again,
    // as in retail.
    struct AsteroidCubeArchEntry
    {
        // .text:062a4ab0..062a4add: the temporary's initial state.
        AsteroidCubeArchEntry()
            : Arch(nullptr)
            , Extent{0.0f, 0.0f, 0.0f}
            , bMine(false)
            , bInstantKill(false)
        {
            for (int Row = 0; Row < 3; ++Row)
                for (int Col = 0; Col < 3; ++Col)
                    Orient.E[Row][Col] = (Row == Col) ? 1.0f : 0.0f;
        }

        // FUN_062a69f0's per-node step: release the archetype when one is set.
        ~AsteroidCubeArchEntry()
        {
            if (Arch != nullptr)
                Arch->Release();
        }

        AsteroidCubeArchEntry(AsteroidCubeArchEntry const&) = default;
        AsteroidCubeArchEntry& operator=(AsteroidCubeArchEntry const&) = default;

        Archetype::Asteroid* Arch;   // node+0x08
        // Per-axis placement offset of this rock within the cube: INI values 1/2/3, clamped
        // to [-1,1], then pre-scaled by half the field's cube size at load() time (see the
        // "asteroid" key's handling in CAsteroidField::load, common_casteroidfield.cpp). Combined
        // with the cube's own Orient via CAsteroidCube::init's TransformMatrix-equivalent call
        // to get the rock's final world-space offset from the cube center.
        Vector Extent;                // node+0x0c..0x14
        // Per-rock orientation offset, composed from INI values 4/5/6 (Euler angles in
        // degrees) via the exact same row-mixing math as compute_cube_orientation (see
        // CAsteroidField::load). Combined with the cube's own Orient via CAsteroidCube::init's
        // MulMatrix-equivalent call to get the rock's final world orientation.
        Matrix Orient;                 // node+0x18..0x3c
        bool bMine;                    // node+0x3c
        bool bInstantKill;             // node+0x3d
    };

    // Retail's CAsteroidCube::init takes `struct CmnAsteroid::AsteroidCubeArch *`
    // (?init@CAsteroidCube@CmnAsteroid@@QAEXABVVector@@KPAUAsteroidCubeArch@2@ABUAxisRotations@2@M@Z)
    // and its only caller passes the CAsteroidField pointer itself (.text:062a5d44 PUSH ESI,
    // ESI = the field), while init reads the entry list's head at param+4 (.text:062a62eb).
    // CAsteroidField has no vftable, so the AsteroidCubeArch object sits at the field's
    // offset 0 and holds the entry list. A base class and a first member are
    // indistinguishable in the listing; the port uses a base so the field converts to it
    // implicitly, as the retail call does.
    struct AsteroidCubeArch
    {
        std::list<AsteroidCubeArchEntry> ArchList;
        // Retail +0x0c: the cube size the entries' extents are scaled by. CAsteroidField::load
        // and set_cube_size store the field's cube size here as well as in CubeSize (MOV
        // [ESI+0x44],EAX / MOV [ESI+0xc],EAX), load's "asteroid" key reads it back (FILD
        // [this+0x0c] * 0.5) and the copy ctor copies it (.text:062a3443); the default ctor
        // leaves it unwritten. Formerly Field_0x0C.
        int ArchCubeSize;
    };

    // CmnAsteroid::asteroid_marker (struct, mangled Uasteroid_marker@CmnAsteroid@@): one
    // node of the singly linked "changes" list CAsteroidCube::get_changes builds per live
    // asteroid and CAsteroidField::get_changes concatenates over every cube. 0x14 bytes
    // (operator_new(0x14), .text:062a6601). Server.dll (.text:06d044a3) and freelancer.exe
    // (.text:0052215d) read the list; Common frees it in CAsteroidField::flush_changes.
    // Field names are this port's; the offsets and sources are the stores at
    // .text:062a6627-062a6665.
    struct asteroid_marker
    {
        // Inlined at .text:062a660f-062a6620: every field zeroed.
        asteroid_marker()
            : CubeId(0)
            , AsteroidId(0)
            , ObjId(0)
            , bCubeActive(false)
            , bMine(false)
            , bInstantKill(false)
            , Next(nullptr)
        {
        }
        // FUN_062a56e0 (scalar deleting dtor, recursive): deletes the rest of the chain.
        ~asteroid_marker()
        {
            if (Next != nullptr)
                delete Next;
        }

        unsigned long CubeId;       // +0x00  owning cube's id (cube+0x08)
        unsigned long AsteroidId;   // +0x04  CAsteroid::AsteroidId (+0xe8)
        unsigned ObjId;             // +0x08  CSimple::Id (+0xb0)
        bool bCubeActive;           // +0x0c  cube State == CubeStateActive
        bool bMine;                 // +0x0d  CAsteroid::bMine (+0xf0)
        bool bInstantKill;          // +0x0e  CAsteroid::bInstantKill (+0xf1)
        asteroid_marker* Next;      // +0x10
    };

    // CmnAsteroid::CAsteroidCube: one grid cell of a CAsteroidField. Retail fields
    // confirmed by direct accessors and by cross-checking CAsteroidField::update's raw
    // tree-node indexing (Common.dll.c 0x452a0, which reaches the embedded cube via
    // node+0x10 -- the RB-tree's own 4-pointer node header -- exactly like
    // CmnAsteroid::Find's node+0x10 for CAsteroidSystem): StateTimer +0x00 (zeroed and
    // incremented by update(), NOT unresolved as previously assumed -- see update()'s
    // "node+0x10 => piVar2[4]" indexing), Owner (owning CAsteroidField*) +0x04 (init/the
    // CAsteroidField*-taking ctor), CubeId +0x08 (init), Orient +0x0c (9 floats,
    // get_orient), Pos +0x30 (get_pos), State +0x40 (set_state -- a full dword, not
    // folded into StateTimer), Asteroids list head +0x48 / count +0x4c, SystemId +0x50
    // (set_system, also used as the CollisionGroupId handed to every CAsteroid spawned
    // into this cube). A lone byte at +0x44 is written with indeterminate/uninitialized
    // data by the CAsteroidField*-taking ctor (Common.dll.txt 0x46150: "this[0x44] =
    // param_1._0_1_", the classic Ghidra rendering of an uninitialized member landing on
    // whatever value happened to be in a reused register) and is never read anywhere in
    // the traced call graph -- not modeled here (inert). A second byte at +0x54 (copied
    // member-wise by the copy ctor/operator=) is init()'s "empty cube" flag (bEmpty below),
    // read back by get_changes. The live
    // asteroid population is an intrusive list of CObject* at retail +0x48
    // (~CAsteroidCube releases each one then clears the list, Common.dll.c 0x46190) --
    // ported here as a real std::list<CObject*>.
    class COMMON_API CAsteroidCube
    {
    public:
        // Common.dll.txt 0x46150 (??0CAsteroidCube@CmnAsteroid@@QAE@PAVCAsteroidField@1@@Z).
        // The "shell" ctor populate_asteroids_internal uses when it inserts a brand new
        // cube into the field's map: stamps the owning field, zeroes State/count, and
        // leaves CubeId/Orient/Pos/SystemId to be filled in by the following init() call.
        explicit CAsteroidCube(CAsteroidField* OwnerField);
        ~CAsteroidCube();

        // Retail accessors are all non-const (QAE): get_pos .text:062a3020, get_orient
        // .text:062a3040, get_id .text:062a66a0 (MOV EAX,[this+8]), get_state .text:062a6690
        // (MOV EAX,[this+0x40]), set_size .text:062a66b0 (MOV [this+0x3c],arg).
        Vector get_pos();
        Matrix get_orient();
        unsigned long get_id();
        CubeState get_state();
        void set_size(int SizeValue);
        void set_system(unsigned SystemId);
        void set_state(CubeState State);

        // .text:062a6260. Stamps CubeId/Pos/StateTimer and rolls this cube's snap-rotation
        // via compute_cube_orientation; on success, walks ArchList and spawns one CAsteroid
        // per entry. The rock's offset is I3DMathEngine +0x2c TransformMatrix (Orient times
        // the entry's Extent), fed to compute_asteroid_id and then added to Pos; its
        // orientation is +0x20 MulMatrix (Orient times the entry's Orient). Both go through
        // Acquire3DMathEngine() (retail DAT_063fc2b4 / FUN_06391cf0).
        void init(Vector const& Pos, unsigned long CubeIdValue, AsteroidCubeArch* CubeArch, AxisRotations const& Rot, float Freq);

        // .text:062a65c0 (ordinal 2665). Null for an empty cube (bEmpty, +0x54) and for any
        // State other than CubeStateActive/CubeStateFar; otherwise one freshly allocated
        // asteroid_marker per live asteroid, linked in list order, head returned.
        asteroid_marker* get_changes();

        unsigned long CubeId;
        // Owning field's back-pointer (retail +0x04); stamped by both the
        // CAsteroidField*-taking ctor and init(), read back by init()'s spawn loop when
        // building each new asteroid's CreateParms::OwnerField.
        CAsteroidField* Owner;
        Matrix Orient;
        Vector Pos;
        // Retail cube+0x3c: the cube's edge size. Named by its exported setter set_size
        // (.text:062a66b0: MOV [this+0x3c],arg). CAsteroidField::populate_asteroids_internal
        // stores the field's CubeSize (field+0x44) here right after the map insert
        // (.text:062a5d2b-062a5d2e, set_size inlined). No reader found in Common.dll.
        int Size;
        CubeState State;
        // Elapsed-time accumulator backing CAsteroidField::update's CubeStateUnknown ->
        // CubeStateNear/Far transition. Retail offset +0x00 (see class-level note above).
        float StateTimer;
        unsigned SystemId;
        std::list<CObject*> Asteroids;
        // Retail cube+0x54: init() stores !compute_cube_orientation(...) here
        // (.text:062a62cc-062a62d2, NEG/SBB/INC) and skips the spawn loop when it is set;
        // get_changes returns null for such a cube (.text:062a65c8).
        bool bEmpty;

    private:
        // Common.dll.c 0x466c0. Packs Pos into a 20-bit local asteroid id (10 bits per
        // axis: abs(round(coord)) >> 1, sign folded into bit 9) via the same
        // truncate-with-epsilon convention as CAsteroidField::create_cube_id/
        // closest_cube_pos. Pure function of Pos -- does not read/write any CAsteroidCube
        // state despite being a member. Returns false (leaving OutId untouched) if any
        // axis doesn't fit in 10 bits.
        bool compute_asteroid_id(Vector const& Pos, unsigned long& OutId);
    };

    // CmnAsteroid::CAsteroidField (Common.dll.c 0x44660 ctor .. 0x45e50 Release). Owns a
    // grid of CAsteroidCube (retail: RB-tree keyed by CmnAsteroid::compute_cube_id,
    // ported as std::map), plus field-wide spawn parameters and an optional exclusion
    // zone.
    class COMMON_API CAsteroidField : public AsteroidCubeArch
    {
    public:
        CAsteroidField();
        ~CAsteroidField();

        // .text:062a4940. Stores the cube size (+0x44 and +0x0c), then parses [Cube]
        // (x/y/zaxis_rotation, asteroid -> ArchList), [Exclusion Zones] (ExclusionMgr) and
        // [LootableZone] (a heap LootableZoneEntry handed to the zone's slot +0x8c, the zone
        // appended to LootZones).
        void load(INI_Reader& Reader, int CubeSize);

    private:
        // Private in retail (?update@CAsteroidField@CmnAsteroid@@AAEXM@Z); its one caller is
        // CAsteroidSystem::update (CALL at .text:062a45e4), a friend.
        friend class CAsteroidSystem;
        // .text:062a52a0: every CubeStateUnknown cube accumulates Dt into StateTimer and turns
        // Far above 3.0f, Near otherwise.
        void update(float Dt);

    public:

        // The field's zone (+0x28, set by set_zone from the exe's CliAsteroid load) answers
        // these through Universe::IZone slots +0x00 (immersion), +0x10 (position), +0x30
        // (radius) and +0x40 (extent along a direction). With no zone: 1.0f / true / 1.0f.
        // .text:062a53a0: the zone's immersion at Pos.
        float get_immersion(Vector const& Pos);
        // .text:062a53c0: Pos lies within (CubeSize + FillDist) * 1.74 of the zone surface.
        bool near_field(Vector const& Pos);
        // .text:062a54e0: 1 - (distance outside the surface) / Dist, 0 when farther than Dist;
        // Dist <= 0 defaults to (FillDist + CubeSize) * 1.2.
        float near_field_fraction(Vector const& Pos, float Dist);

        // CmnAsteroid::CAsteroidField::create_cube_id (Common.dll.c 0x43270). Snaps Pos to
        // this field's cube grid (via closest_cube_coord using CubeSize/HalfSize) and
        // returns the packed id for that cube via compute_cube_id.
        unsigned long create_cube_id(Vector const& Pos);

        // CmnAsteroid::CAsteroidField::closest_cube_pos (Common.dll.c 0x45ee0). Same
        // per-axis snap as create_cube_id (closest_cube_coord + the same
        // truncate-with-epsilon convention), but returns the snapped Vector directly
        // instead of hashing it into a cube id.
        Vector closest_cube_pos(Vector const& Pos);

        // CmnAsteroid::CAsteroidField::find_cube (Common.dll.c 0x46100). Retail's hand-
        // rolled RB-tree lookup by CubeId, faithfully reproduced here as a std::map
        // lookup: returns the cube if present, nullptr otherwise.
        CAsteroidCube* find_cube(unsigned long CubeId);

        // CmnAsteroid::CAsteroidField::populate_asteroids (item 70308, Common.dll.txt
        // 131451-131803, no Ghidra .c -- raw disasm only, hand-traced 2026-09-10). Called
        // from CAsteroidSystem::update (Common.dll.txt 129648) with Pos = the tracked
        // observer's (e.g. player's) current position and Dir = its negated forward/look
        // direction. PORTED FOR REAL 2026-09-10 (claude-casteroid-spawn-pipeline lane) --
        // see the .cpp for the full evidence trail: snaps Pos onto this field's cube grid
        // (closest_cube_pos) and, gated by two always-true-by-default bool cvars
        // (DAT_063eb7fc/DAT_063eb7fd, no write site found for either), calls
        // populate_asteroids_internal for up to 8 grid-neighbor candidates around the
        // snapped position (a 2x2x2 offset lattice covering the grid-cell ambiguity at Pos,
        // threshold (CubeSize*0.99)^2) plus, separately, one more candidate computed ahead
        // of Pos along Dir (Pos + CubeSize*1.25*Dir, threshold a fixed 1000000.0f).
        void populate_asteroids(Vector const& Pos, Vector const& Dir);

        // .text:062a5600 (ordinal 2559): every CubeStateFar cube is erased, every other
        // cube is reset to CubeStateUnknown; then the cached asteroid_marker list (+0x2c) is
        // deleted and the cache cleared.
        void flush_changes();
        // .text:062a5300 (ordinal 2666): the cached list (+0x2c) if one exists; otherwise
        // the concatenation of every cube's CAsteroidCube::get_changes in map order, cached.
        asteroid_marker* get_changes();

        int get_cube_size();
        void set_cube_size(int Size);
        // .text:062a33e0 (ICF fold with CAsteroidCube::set_state): MOV [this+0x40],arg.
        void set_system(unsigned SystemIdValue);
        // .text:0629d990: LEA EAX,[this+0x50].
        AxisRotations& get_axis_rotations();
        // .text:063275a0: MOV EAX,[this+0x30].
        Exclusion::AsteroidExclusionZoneMgr* get_exclusion_mgr();
        // .text:062a5e80: the first zone in the [LootableZone] list (+0x38) whose
        // containment (IZone slot 0x00) at Pos is non-zero; otherwise this field's own Zone
        // (+0x28) if it owns a loot entry (slot 0x7c, .text:06262dd0 MOV EAX,[this+0x84]);
        // otherwise null.
        Universe::IZone const* get_lootable_zone(Vector const& Pos) const;
        void set_fill_dist(int Dist);
        void set_empty_freq(float Freq);

        // Common.dll.c 0xdbae0 / ICF with CECargo::SetCount: `*(this+0x28) = param`.
        // Written from freelancer.exe FUN_0051ff50 (CliAsteroid load), not from Common.dll
        // itself -- earlier notes claiming Zone is "never assigned" only scanned Common.
        void set_zone(Universe::IZone const* InZone);

        int AddRef();
        int Release();

        int RefCount;
        std::map<unsigned long, CAsteroidCube> Cubes;
        // Retail field+0x28: IZone pointer. Assigned by set_zone (exe CliAsteroid path);
        // Common.dll load/exclusion only reads it. Stored as Universe::IZone* (port's
        // plain struct); retail's polymorphic shape-base identity is item 2574.
        Universe::IZone* Zone;
        // Retail field+0x2c: the cached asteroid_marker list built by get_changes and freed by
        // flush_changes; zeroed by the ctor (.text:062a475c).
        asteroid_marker* Changes;
        // Retail field+0x30: owned, destroyed by ~CAsteroidField (Common.dll.c 0x447a0).
        Exclusion::AsteroidExclusionZoneMgr* ExclusionMgr;
        // Retail field+0x40 (confirmed 2026-09-10, item 70309's own trace: populate_
        // asteroids_internal's new-cube path, Common.dll.txt 131992/131997, reads this
        // field and stamps it onto the newly-spawned CAsteroidCube::SystemId, which item
        // 70318's init() spawn loop then uses as every new CAsteroid's CollisionGroupId).
        unsigned SystemId;
        // Retail field+0x50 (confirmed 2026-09-10, item 70309's own trace: populate_
        // asteroids_internal's CAsteroidCube::init() call passes &(this+0x50) as the Rot
        // argument, Common.dll.txt 131999-132000) -- sits right after EmptyFreq (+0x4c).
        AxisRotations Rotations;
        // Retail field+0x44 (confirmed byte-identical to set_cube_size's whole body,
        // Common.dll.txt 0x433a0/0x433b0; default 0xfa=250, ctor Common.dll.txt
        // 0x62a4745). Also the value near_field/near_field_fraction sum with FillDist
        // below to compute their near-zone margin -- see get_immersion/near_field note.
        int CubeSize;
        // Retail field+0x48 (confirmed byte-identical to set_fill_dist's whole body,
        // Common.dll.txt 0x433c0; default 0x3e8=1000, ctor Common.dll.txt 0x62a474c).
        int FillDist;
        // Retail field+0x4c (confirmed byte-identical to set_empty_freq's whole body,
        // Common.dll.txt 0x433d0; default 0.0f, ctor Common.dll.txt 0x62a4753).
        float EmptyFreq;
        // Retail field+4 (Head) / +8 (Count), the per-field "AsteroidCubeArch" spawn list, is
        // AsteroidCubeArch::ArchList (the base above), populated by load()'s "asteroid" key and
        // consumed by CAsteroidCube::init's spawn loop.
        // Retail +0x34 (allocator) / +0x38 (head) / +0x3c (count): a VC6 std::list of zone
        // pointers (0xc-byte nodes, the zone at node+8), appended by load()'s [LootableZone]
        // section when a "zone" key resolved a zone (.text:062a5205..062a5235) and walked by
        // get_lootable_zone (.text:062a5e80).
        std::list<Universe::IZone*> LootZones;

    private:
        // CmnAsteroid::CAsteroidField::populate_asteroids_internal (item 70309, private
        // per retail's own mangled name -- see the .cpp for the full evidence trail).
        // Pos = the fixed reference position populate_asteroids was called with; Candidate
        // = one grid-snapped cube-center position to evaluate; DistSqThreshold = the
        // caller-supplied squared-distance gate (populate_asteroids passes a different
        // fixed value per call site -- see its own comment).
        void populate_asteroids_internal(Vector const& Pos, Vector const& Candidate, float DistSqThreshold);
    };

    // CmnAsteroid::CAsteroidSystem (.text:062a4250 ctor .. 062a4640 FindNext). One per
    // loaded universe system, held by value in the registry map; owns the CAsteroidField
    // instances created for it (one reference each, released by the destructor).
    class COMMON_API CAsteroidSystem
    {
    public:
        CAsteroidSystem();
        ~CAsteroidSystem();

        // .text:062a4410. Creates a field with RefCount 1, links it into Fields,
        // and stamps it with this system's SystemId.
        CAsteroidField* create_field();
        // .text:062a44c0: flush_changes on every field; for every IObjDB candidate (mask
        // 0x7f0000) not on a trade lane, near_field / populate_asteroids(Pos, -forward) on at
        // most two fields; then update(Dt) on every field.
        void update(IObjDB* ObjDb, float Dt);

        int AddRef();
        int Release();

        CAsteroidField* FindFirst();
        CAsteroidField* FindNext();

        // .text:062d1ad0 (ordinal 3021, ICF-folded `MOV EAX,[this+4]; RET`).
        unsigned get_sys_id() { return SystemId; }
        // .text:06309b30 (ordinal 3673, ICF-folded `MOV [this+4],arg; RET 4`).
        void set_sys_id(unsigned SysId) { SystemId = SysId; }

        int RefCount;                                   // +0x00
        unsigned SystemId;                              // +0x04
        std::list<CAsteroidField*> Fields;              // +0x08 (allocator, head, size)
        std::list<CAsteroidField*>::iterator FindIter;  // +0x14 FindFirst / FindNext cursor
    };

    // Global registry (CmnAsteroid::Load/Unload/Find/Update/CreateAsteroidField), keyed
    // by universe SystemId: the file-scope std::map at DAT_063fc294 (common_casteroid.cpp,
    // GAsteroidSystems).
    COMMON_API int __cdecl Load(unsigned SystemId);
    COMMON_API int __cdecl Unload(unsigned SystemId);
    COMMON_API void __cdecl Update(IObjDB* ObjDb, float Dt);
    COMMON_API CAsteroidField* __cdecl CreateAsteroidField(unsigned SystemId);
    COMMON_API CAsteroidSystem* __cdecl Find(unsigned SystemId);
}
