// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "client_cliobj.h"

#include "common_goods.h"
#include "common_id.h"
#include "common_ini.h"
#include "common_misc.h"
#include "dacom.h"
#include "dacom_iids.h"
#include "DALib.h"
#include "engine/engbase.h"
#include "perf_options.h"      // GetPerfOptionSlot (FUN_005b3510)
#include "readfile.h"          // IFileSystem (the inline "FileSystem" instance in FUN_0041aa50)
#include "render/rendcomp.h"
#include "render/shading.h"
#include "ship_camera_params.h"
#include "vis_effect_arch.h"

#include <cstddef>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <windows.h>

namespace Archetype
{
    struct FCliMaterialEntry
    {
        unsigned long PathCrc;
        int TextureId;
        int MaterialId;
        void* VMeshHandle;
        void* MatAnimHandle;
        int RefCount;
    };

    namespace
    {
        // Load* TypeIds pushed by Common.dll into exe FUN_0040ca50. Gaps 1/6/7/0x10/0x1E
        // are not pushed by LoadEquipment/LoadAsteroids — default 0x54 if hit.
        enum ECliObjFactoryType
        {
            CliObjTypeSimple = 0,
            CliObjTypeSolar = 2,
            CliObjTypeShip = 3,
            CliObjTypeAsteroid = 4,
            CliObjTypeDynAsteroid = 5,
            CliObjTypeLootCrate = 8,
            CliObjTypeCargoPod = 9,
            CliObjTypeCommodity = 0xA,
            CliObjTypePower = 0xB,
            CliObjTypeEngine = 0xC,
            CliObjTypeShield = 0xD,
            CliObjTypeShieldGenerator = 0xE,
            CliObjTypeThruster = 0xF,
            CliObjTypeGun = 0x11,
            CliObjTypeMineDropper = 0x12,
            CliObjTypeCmDropper = 0x13,
            CliObjTypeScanner = 0x14,
            CliObjTypeLight = 0x15,
            CliObjTypeTractor = 0x16,
            CliObjTypeAttachedFx = 0x17,
            CliObjTypeInternalFx = 0x18,
            CliObjTypeRepairDroid = 0x19,
            CliObjTypeRepairKit = 0x1A,
            CliObjTypeShieldBattery = 0x1B,
            CliObjTypeCloakingDevice = 0x1C,
            CliObjTypeTradeLane = 0x1D,
            CliObjTypeMunition = 0x1F,
            CliObjTypeMine = 0x20,
            CliObjTypeCounterMeasure = 0x21,
            CliObjTypeArmor = 0x22,
        };

        // freelancer.exe FUN_004023f0 node: 0x34, next at +0x30, CRC at +0x24.
        struct FLodNode
        {
            float Ranges[8];
            float Terminator;
            unsigned ObjCrc;
            unsigned char Unk28;
            unsigned char Pad29[3];
            int MinSpecLod;
            FLodNode* Next;
        };

        static_assert(sizeof(FLodNode) == 0x34, "LOD node sizeof 0x34");
        static_assert(offsetof(FLodNode, ObjCrc) == 0x24, "LOD CRC +0x24");
        static_assert(offsetof(FLodNode, MinSpecLod) == 0x2C, "MinSpecLOD +0x2C");
        static_assert(offsetof(FLodNode, Next) == 0x30, "LOD next +0x30");

        FLodNode* MakeLodNode()
        {
            FLodNode* Node = new FLodNode();
            memset(Node, 0, sizeof(FLodNode));
            Node->Ranges[0] = -1.0f;
            Node->MinSpecLod = 1;
            return Node;
        }

        void FreeLodList(FLodNode* Head)
        {
            FLodNode* Node = Head;
            while (Node != nullptr)
            {
                FLodNode* Next = Node->Next;
                delete Node;
                Node = Next;
            }
        }

        // FUN_00402310 — LODranges / MinSpecLOD into the current node pointer.
        bool ReadLodValue(INI_Reader* Reader, FLodNode** NodeSlot)
        {
            if (Reader->is_value("LODranges"))
            {
                if (*NodeSlot == nullptr)
                    *NodeSlot = MakeLodNode();

                FLodNode* Node = *NodeSlot;
                unsigned Index = 0;
                while (!Reader->is_value_empty(Index) && Index < 8)
                {
                    Node->Ranges[Index] = Reader->get_value_float(Index);
                    Index += 1;
                }
                float* TermSlot = reinterpret_cast<float*>(
                    reinterpret_cast<char*>(Node) + Index * 4);
                *TermSlot = -1.0f;
                if (Index < 3)
                    Node->MinSpecLod = 0;
                return true;
            }

            if (Reader->is_value("MinSpecLOD"))
            {
                if (*NodeSlot != nullptr)
                    (*NodeSlot)->MinSpecLod = Reader->get_value_int(0);
                return true;
            }

            return false;
        }

        // FUN_004023f0 — [LOD] header into linked list at ICliObj+0x04.
        void ApplyLodSection(FLodNode** Head, INI_Reader* Reader)
        {
            FLodNode* Node = nullptr;
            if (!Reader->read_value())
                return;

            do
            {
                if (Reader->is_value("obj"))
                {
                    Node = MakeLodNode();
                    char const* Name = Reader->get_value_string();
                    Node->ObjCrc = DACOM_CRC::GetCRC32(Name);
                }
                else
                    ReadLodValue(Reader, &Node);
            } while (Reader->read_value());

            if (Node == nullptr)
                return;

            if (Node->Ranges[0] < 0.0f)
            {
                delete Node;
                return;
            }

            Node->Next = *Head;
            *Head = Node;
        }

        // FCliVc6Vector / FCliMaterialPathPair are declared in client_cliobj.h.

        // Distance / FX record at +0x44 (stride 0x24). EffectArch id list + live instances.
        // Layout from FUN_00555810 / 00555860 / 005559c0 — NOT StarSys (tracker mis-tag).
        // +0x04/+0x14 are VC6 vector cookies (FUN_00406dc0 surface_hit_effects pushes into +0x04).
        struct FCliDistanceRecord
        {
            float DistanceThreshold; // +0x00
            unsigned char EffectIdCookie; // +0x04
            unsigned char EffectIdPad[3];
            unsigned* EffectIdBegin; // +0x08  CreateID keys → FUN_0040f080
            unsigned* EffectIdEnd;   // +0x0C
            unsigned* EffectIdCap;   // +0x10
            unsigned char LiveCookie; // +0x14  IDPMsgHandler this for live vector
            unsigned char LivePad[3];
            void** LiveBegin;        // +0x18  EffectArch*
            void** LiveEnd;          // +0x1C
            void** LiveCap;          // +0x20
        };

        static_assert(sizeof(FCliDistanceRecord) == 0x24, "distance record stride 0x24");
        static_assert(offsetof(FCliDistanceRecord, EffectIdBegin) == 0x08, "effect ids +0x08");
        static_assert(offsetof(FCliDistanceRecord, LiveBegin) == 0x18, "live effects +0x18");
        static_assert(offsetof(FCliDistanceRecord, EffectIdCookie) == 0x04, "id vector header +0x04");
        static_assert(offsetof(FCliDistanceRecord, LiveCookie) == 0x14, "live vector header +0x14");

        static_assert(sizeof(FCliMaterialEntry) == 0x18, "MaterialLib cache entry 0x18");

        // StateFlags (+0x50) bits from FUN_00407100 / Slot10 / Slot11 / Slot12 / Load.
        enum ECliObjStateFlags : unsigned char
        {
            CliObjFlagCacheBuilt = 0x01,
            CliObjFlagSlot10Done = 0x02,
            CliObjFlagEngineSide = 0x04,
            CliObjFlagSlot12Done = 0x08,
            CliObjFlagLodPickDone = 0x10,
        };

        // Retail default case FUN_0040ca50: operator_new(0x54) + FUN_004068d0.
        // Layout +0x1C…+0x50 matches the CliRoot table in freelancer_cliobj.md (listing FUN_004068d0).
        class FCliObj : public ICliObj
        {
        public:
            FCliObj();
            ~FCliObj();

            void AddRef() override;
            void Release() override;
            bool ReadUnknown(INI_Reader* Reader) override;
            void Lod(INI_Reader* Reader) override;
            void Redefine(ICliObj* Donor) override;
            void Bind(Root* Record) override;
            void FreeResources() override;
            void Inherit(ICliObj* Donor) override;
            unsigned long Load(int LoadMode) override;
            void* Slot09(unsigned Flags) override;
            void Slot10() override;
            void Slot11() override;
            void Slot12(unsigned Arg) override;
            void Slot13() override;

            // FUN_00407310 — public non-virtual thunk: [ESP+4] = [ESP+4]; JMP FUN_00407100.
            bool PreloadMaterials(bool bLatchSlot10) { return BuildMaterialCache(bLatchSlot10); }

            // FUN_00407450 — not a vtable slot; distance-threshold pick into record payload.
            void* PickDistanceRecord(float Distance);

            // FUN_00406c50 — recompute LodPick from BoundRoot; keeps stale value if !is_loaded.
            void RecomputeLodPick(float const* BlendSource);
            // FUN_00406cd0 — same with caller-supplied engine index / flag (no Root lookup).
            void RecomputeLodPickExplicit(long EngineIndex, float const* BlendSource,
                                          bool bSolarOrShip);

            // Layout public so offsetof can verify retail offsets (like Archetype::Root).
            FLodNode* LodHead;              // +0x04
            float DistanceRender;           // +0x08 default -1
            float LodPick;                  // +0x0C default -1
            CacheString EnvmapMaterial;     // +0x10
            Root* BoundRoot;                // +0x14
            int RefCount;                   // +0x18
            void* PendingLoad;             // +0x1C
            FCliVc6Vector<FCliMaterialPathPair> MaterialPaths;   // +0x20…+0x2C
            FCliVc6Vector<FCliMaterialEntry*> ResolvedEntries;   // +0x30…+0x3C
            FCliVc6Vector<FCliDistanceRecord> DistanceRecords;   // +0x40…+0x4C
            unsigned char StateFlags;       // +0x50
            unsigned char Pad51[3];

        protected:
            // FUN_004086c0 -- CliRoot::operator= (compiler-generated): +0x04..+0x1C copied raw
            // (LodHead shallow), the three vectors assigned, the +0x50 flag bits and then the
            // whole +0x50 dword. Every derived Inherit (slot 7) runs it, inlined or called.
            void AssignRootMembers(FCliObj const* Src);
            bool BuildMaterialCache(bool bLatchSlot10); // FUN_00407100
            void PushResolvedEntry(FCliMaterialEntry* Entry);
            void FreeVectorBuffers();
            // Not an ICliObj slot — appended after Slot13 so retail 0–13 stay aligned.
            virtual void DestroyMostDerived();
        };

        static_assert(sizeof(FCliObj) == 0x54, "ICliObj base sizeof 0x54");
        static_assert(offsetof(FCliObj, PendingLoad) == 0x1C, "ICliObj+0x1C PendingLoad");
        static_assert(offsetof(FCliObj, MaterialPaths) == 0x20, "ICliObj+0x20 MaterialPaths");
        static_assert(offsetof(FCliObj, ResolvedEntries) == 0x30, "ICliObj+0x30 ResolvedEntries");
        static_assert(offsetof(FCliObj, DistanceRecords) == 0x40, "ICliObj+0x40 DistanceRecords");
        static_assert(offsetof(FCliObj, StateFlags) == 0x50, "ICliObj+0x50 StateFlags");

        // Retail CliShip — operator_new(0x88), cockpit CacheString at +0x60.
        class FCliShip : public FCliObj
        {
        public:
            FCliShip();

            bool ReadUnknown(INI_Reader* Reader) override;
            void Redefine(ICliObj* Donor) override;
            void Inherit(ICliObj* Donor) override;

        protected:
            void DestroyMostDerived() override;

        public:
            // Layout public for offsetof static_asserts (same pattern as FCliObj).
            unsigned Pad54;              // +0x54
            ID_String Id58;              // +0x58
            ID_String Id5C;              // +0x5C
            CacheString CockpitIni;      // +0x60 shiparch cockpit = …
            ID_String Id64;              // +0x64
            float CameraOffsetX;         // +0x68
            float CameraOffsetY;         // +0x6C
            float CamAngularAccel;       // +0x70
            float CamAngularSlerp;       // +0x74
            float TurnAngle78;           // +0x78
            float TurnAngle7C;           // +0x7C
            float TurnAngle80;           // +0x80
            float CamTurnLookAhead;      // +0x84
        };

        static_assert(sizeof(FCliShip) == 0x88, "CliShip sizeof 0x88");
        static_assert(offsetof(FCliShip, CockpitIni) == 0x60, "CliShip cockpit +0x60");
        static_assert(offsetof(FCliShip, CamAngularSlerp) == 0x74, "CliShip cam slerp +0x74");

        // Retail case 2: operator_new(0x88), FUN_004068d0, PTR_FUN_005c7908.
        // Two ID_String clears at +0x54/+0x58 (not init_docking_points).
        // Inline texture at +0x5C length / +0x60 32-byte "DEFAULT_TEXTURE".
        class FCliSolar : public FCliObj
        {
        public:
            FCliSolar();

            bool ReadUnknown(INI_Reader* Reader) override;
            void Redefine(ICliObj* Donor) override;   // FUN_0040a0e0
            void Inherit(ICliObj* Donor) override;

            // +0x54 open_sound / +0x58 close_sound, read raw by the solar notify FUN_005478f0.
            ID_String const* DoorSound(bool bOpen) const
            {
                return bOpen ? &Id54 : &Id58;
            }

            // +0x60 shape_name (default "DEFAULT_TEXTURE"), read raw by the nav map.
            char const* ShapeName() const
            {
                return DefaultTexture;
            }

        protected:
            void DestroyMostDerived() override;

        private:
            ID_String Id54;            // +0x54
            ID_String Id58;            // +0x58
            unsigned TextureLen;       // +0x5C
            char DefaultTexture[32];   // +0x60
            unsigned Unk80;            // +0x80
            unsigned char Unk84;       // +0x84
            unsigned char Pad85[3];
        };

        static_assert(sizeof(FCliSolar) == 0x88, "CliSolar sizeof 0x88");

        // Retail Archetype::CliProj (RTTI .?AUCliProj@Archetype@@, vtable 005c79a8), sizeof 0x60.
        // Factory types 0x20 (Mine) / 0x21 (CounterMeasure) at 0040cc44 / 0040cc92, and the
        // base of CliMunition. Slots: ReadUnknown 00407e60, Redefine 00407f00, Inherit 00407fc0.
        class FCliProj : public FCliObj
        {
        public:
            FCliProj();

            bool ReadUnknown(INI_Reader* Reader) override;
            void Redefine(ICliObj* Donor) override;
            void Inherit(ICliObj* Donor) override;

            float ConstEffectDelay;     // +0x54 (ctor stores dword 0)
            ID_String ConstEffect;      // +0x58
            ID_String OneShotSound;     // +0x5C
        };

        static_assert(sizeof(FCliProj) == 0x60, "CliProj sizeof 0x60");
        static_assert(offsetof(FCliProj, ConstEffectDelay) == 0x54, "CliProj +0x54");
        static_assert(offsetof(FCliProj, OneShotSound) == 0x5C, "CliProj +0x5c");

        // Retail Archetype::CliEQAttachedFX (vtable 005c7fe0), sizeof 0x5C, factory type 0x17 at
        // 0040cfda. Slots: ReadUnknown 0040b6d0, Redefine 0040b740, Inherit 0040b7f0.
        class FCliEQAttachedFX : public FCliObj
        {
        public:
            FCliEQAttachedFX();

            bool ReadUnknown(INI_Reader* Reader) override;
            void Redefine(ICliObj* Donor) override;
            void Inherit(ICliObj* Donor) override;

            ID_String Particles;         // +0x54 "particles"
            bool bUseThrottle;           // +0x58 "use_throttle"
            unsigned char Pad59[3];
        };

        static_assert(sizeof(FCliEQAttachedFX) == 0x5C, "CliEQAttachedFX sizeof 0x5c");
        static_assert(offsetof(FCliEQAttachedFX, bUseThrottle) == 0x58, "CliEQAttachedFX +0x58");

        // Retail Archetype::CliAsteroid (vtable 005c7a3c), sizeof 0x54, factory type 4 at 0040ccc2.
        // ReadUnknown 00408ac0 only forwards to FUN_00406dc0 (the base parser, so no override);
        // Inherit 00408ae0 is CliRoot::operator= without the base Inherit's FUN_00402500 step.
        class FCliAsteroid : public FCliObj
        {
        public:
            void Inherit(ICliObj* Donor) override;
        };

        static_assert(sizeof(FCliAsteroid) == 0x54, "CliAsteroid sizeof 0x54");

        // Retail Archetype::CliDynamicAsteroid (vtable 005c7a78), sizeof 0x5C, factory type 5 at
        // 0040ccf8. Slots: ReadUnknown 00408cd0, Inherit 00408d20 (Redefine is the base 00407050).
        class FCliDynamicAsteroid : public FCliObj
        {
        public:
            FCliDynamicAsteroid();

            bool ReadUnknown(INI_Reader* Reader) override;
            void Inherit(ICliObj* Donor) override;

            ID_String ParticleEffect;    // +0x54 "particle_effect"
            bool bHasParticleEffect;     // +0x58 set with it
            unsigned char Pad59[3];
        };

        static_assert(sizeof(FCliDynamicAsteroid) == 0x5C, "CliDynamicAsteroid sizeof 0x5c");
        static_assert(offsetof(FCliDynamicAsteroid, bHasParticleEffect) == 0x58, "+0x58");

        // Retail Archetype::CliEQTractor (vtable 005c7f78), sizeof 0x60, factory type 0x16 at
        // 0040cf7c. Slots: ReadUnknown 0040b320, Redefine 0040b3f0, Inherit 0040b4c0.
        class FCliTractor : public FCliObj
        {
        public:
            FCliTractor();

            bool ReadUnknown(INI_Reader* Reader) override;
            void Redefine(ICliObj* Donor) override;
            void Inherit(ICliObj* Donor) override;

            unsigned char Color[3];      // +0x54 "color" (bytes via ftol), ctor 0xff each
            unsigned char Pad57;         // +0x57 never written by retail
            ID_String OperatingEffect;   // +0x58 "operating_effect"
            ID_String CompleteSound;     // +0x5c "tractor_complete_snd"
        };

        static_assert(sizeof(FCliTractor) == 0x60, "CliEQTractor sizeof 0x60");
        static_assert(offsetof(FCliTractor, OperatingEffect) == 0x58, "CliEQTractor +0x58");

        // Retail Archetype::CliEQThruster (vtable 005c7d98), sizeof 0x5C, factory type 0xF at
        // 0040cf27. Slots: ReadUnknown 0040a060, Redefine 0040a0e0 (shared with CliSolar),
        // Inherit 0040a190.
        class FCliEQThruster : public FCliObj
        {
        public:
            FCliEQThruster();

            bool ReadUnknown(INI_Reader* Reader) override;
            void Redefine(ICliObj* Donor) override;
            void Inherit(ICliObj* Donor) override;

            ID_String Particles;        // +0x54 "particles"
            CacheString HpParticles;    // +0x58 "hp_particles"
        };

        static_assert(sizeof(FCliEQThruster) == 0x5C, "CliEQThruster sizeof 0x5c");
        static_assert(offsetof(FCliEQThruster, HpParticles) == 0x58, "CliEQThruster +0x58");

        // Retail Archetype::CliEQInternalFX (vtable 005c802c), sizeof 0x64, factory type 0x18 at
        // 0040d029. Slots: ReadUnknown 0040b9f0, Redefine 0040bab0, Inherit 0040bb90.
        class FCliInternalFx : public FCliObj
        {
        public:
            FCliInternalFx();

            bool ReadUnknown(INI_Reader* Reader) override;
            void Redefine(ICliObj* Donor) override;
            void Inherit(ICliObj* Donor) override;

            ID_String UseSound;              // +0x54 "use_sound"
            float InsideSoundCone;           // +0x58 "inside_sound_cone", ctor 360
            float OutsideSoundCone;          // +0x5c "outside_sound_cone", ctor 360
            float OutsideConeAttenuation;    // +0x60 "outside_cone_attenuation", ctor 0
        };

        static_assert(sizeof(FCliInternalFx) == 0x64, "CliEQInternalFX sizeof 0x64");
        static_assert(offsetof(FCliInternalFx, OutsideConeAttenuation) == 0x60, "+0x60");

        // Retail Archetype::CliEQLight (vtable 005c7eb8), sizeof 0x98, factory type 0x15 at
        // 0040cef8 (ctor FUN_0040a9e0). Slots: ReadUnknown 0040ab30, Redefine 0040ae70, Inherit
        // 0040b0a0. flash_light.cpp reads the fields by these offsets.
        class FCliEQLight : public FCliObj
        {
        public:
            FCliEQLight();

            bool ReadUnknown(INI_Reader* Reader) override;
            void Redefine(ICliObj* Donor) override;
            void Inherit(ICliObj* Donor) override;

            float Color[3];          // +0x54 "color" / 255
            float GlowColor[3];      // +0x60 "glow_color" / 255 (seeded by color while all zero)
            float MinColor[3];       // +0x6c "min_color" / 255
            float BulbSize;          // +0x78 "bulb_size", ctor 1
            float GlowSize;          // +0x7c "glow_size", ctor 1
            float AvgDelay;          // +0x80 "avg_delay", ctor 0
            float BlinkDuration;     // +0x84 "blink_duration", ctor 0.3
            float Intensity;         // +0x88 "intensity", ctor 0
            float FlareConeCos[2];   // +0x8c cos(value 1), +0x90 cos(value 0); ctor -1, -1
            float LightsourceCone;   // +0x94 "lightsource_cone", ctor 180
        };

        static_assert(sizeof(FCliEQLight) == 0x98, "CliEQLight sizeof 0x98");
        static_assert(offsetof(FCliEQLight, MinColor) == 0x6c, "CliEQLight +0x6c");
        static_assert(offsetof(FCliEQLight, BulbSize) == 0x78, "CliEQLight +0x78");
        static_assert(offsetof(FCliEQLight, FlareConeCos) == 0x8c, "CliEQLight +0x8c");
        static_assert(offsetof(FCliEQLight, LightsourceCone) == 0x94, "CliEQLight +0x94");

        // Retail Archetype::CliMunition : CliProj (vtable 005c8420), sizeof 0x84, factory type
        // 0x1F at 0040cbbd. Slots: ReadUnknown 004083a0, Redefine 004084b0, Inherit 00408520,
        // Load 004081c0, scalar dtor 0040d1c0 (body 0040d1e0), Slot11 004089a0.
        // Hit-effect CreateID list at +0x64; live EffectArch* at +0x74.
        class FCliMunition : public FCliProj
        {
        public:
            FCliMunition();
            ~FCliMunition();

            bool ReadUnknown(INI_Reader* Reader) override;
            void Redefine(ICliObj* Donor) override;
            void Inherit(ICliObj* Donor) override;
            unsigned long Load(int LoadMode) override;
            void Slot11() override;

            // FUN_00408a70 — random pick from LiveEffects.
            FEffectArch* PickLiveEffect();

        protected:
            void DestroyMostDerived() override;

            void ReleaseLiveEffects();
            void RebuildLiveEffects(unsigned Arg);

        public:
            CacheString HpTrailParent;                 // +0x60
            FCliVc6Vector<unsigned> HitEffectIds;      // +0x64…+0x70
            FCliVc6Vector<FEffectArch*> LiveEffects;   // +0x74…+0x80
        };

        static_assert(sizeof(FCliMunition) == 0x84, "CliMunition sizeof 0x84");
        static_assert(offsetof(FCliMunition, HitEffectIds) == 0x64, "hit ids +0x64");
        static_assert(offsetof(FCliMunition, LiveEffects) == 0x74, "live effects +0x74");

        // Retail Archetype::CliEQShield (vtable 005c83e4), sizeof 0x54, factory type 0xD at
        // 0040cd7c (FUN_004068d0, then the vptr). Every slot is CliRoot's except slot 10
        // (005c840c), which is 00401e90 -- a bare RET: a shield companion never realizes its
        // resolved materials nor unloads engine components the way FUN_00407320 does.
        class FCliEQShield : public FCliObj
        {
        public:
            void Slot10() override;
        };

        static_assert(sizeof(FCliEQShield) == 0x54, "CliEQShield sizeof 0x54");

        // Retail Archetype::CliEQRepairDroid (vtable 005c83a8), sizeof 0x58, factory type 0x19 at
        // 0040d0ab. Slots: ReadUnknown 0040c810, Redefine 00409bd0 (shared with
        // CliEQShieldGenerator), Inherit 0040c860.
        class FCliRepairDroid : public FCliObj
        {
        public:
            FCliRepairDroid();

            bool ReadUnknown(INI_Reader* Reader) override;
            void Redefine(ICliObj* Donor) override;
            void Inherit(ICliObj* Donor) override;

            ID_String ActiveEffect;      // +0x54 "active_effect"
        };

        static_assert(sizeof(FCliRepairDroid) == 0x58, "CliEQRepairDroid sizeof 0x58");

        // Retail Archetype::CliEQShieldGenerator (vtable 005c7cfc), sizeof 0x70, factory type 0xE
        // at 0040cdba. Slots: ReadUnknown 004098b0, Redefine 00409bd0, Inherit 00409c80, scalar
        // dtor 00409780 (body 004097a0), Slot12 00409fb0, Slot13 0040a010. The +0x60 vector holds
        // the same 0x24-byte records as CliRoot's surface_hit_effects list at +0x40.
        class FCliShieldGenerator : public FCliObj
        {
        public:
            FCliShieldGenerator();
            ~FCliShieldGenerator();

            bool ReadUnknown(INI_Reader* Reader) override;
            void Redefine(ICliObj* Donor) override;
            void Inherit(ICliObj* Donor) override;
            void Slot12(unsigned Arg) override;
            void Slot13() override;

            // FUN_00409f10 -- not a vtable slot; the shield-hit twin of PickDistanceRecord over
            // the +0x60 list. Retail callers 0x0052e81a / 0x0052e96a.
            void* PickShieldHitEffect(float Distance);

        protected:
            void DestroyMostDerived() override;

        public:
            ID_String CollapseParticle;                       // +0x54 "shield_collapse_particle"
            ID_String CollapseSound;                           // +0x58 "shield_collapse_sound"
            ID_String RebuiltSound;                            // +0x5c "shield_rebuilt_sound"
            FCliVc6Vector<FCliDistanceRecord> ShieldHitEffects; // +0x60 "shield_hit_effects"
        };

        static_assert(sizeof(FCliShieldGenerator) == 0x70, "CliEQShieldGenerator sizeof 0x70");
        static_assert(offsetof(FCliShieldGenerator, ShieldHitEffects) == 0x60, "+0x60 hit effects");

        // Retail Archetype::CliEQLauncher (vtable 005c7df0), sizeof 0x84, factory types 0x12 / 0x13
        // (MineDropper / CounterMeasureDropper) at 0040ce88. Slots: ReadUnknown 0040a4d0, Redefine
        // 0040a5e0, Inherit 0040a720, Load 0040a3b0.
        class FCliLauncher : public FCliObj
        {
        public:
            FCliLauncher();

            bool ReadUnknown(INI_Reader* Reader) override;
            void Redefine(ICliObj* Donor) override;
            void Inherit(ICliObj* Donor) override;
            unsigned long Load(int LoadMode) override;

            ID_String FlashParticle;     // +0x54 "flash_particle_name"
            float FlashRadius;           // +0x58 "flash_radius", ctor 4.0
            unsigned LightAnimLength;    // +0x5c
            char LightAnim[0x20];        // +0x60 "light_anim", at most 0x1f chars + NUL
            ID_String DryFireSound;      // +0x80 "dry_fire_sound"

        protected:
            // The fixed-string assign retail inlines at 0040a524, 0040a6b7 and 0040a7e7: a null
            // source empties it; otherwise strlen clamped to 0x1f, copied, NUL-terminated, and the
            // length stored at +0x5c.
            void AssignLightAnim(char const* Text);
        };

        static_assert(sizeof(FCliLauncher) == 0x84, "CliEQLauncher sizeof 0x84");
        static_assert(offsetof(FCliLauncher, LightAnim) == 0x60, "CliEQLauncher +0x60");
        static_assert(offsetof(FCliLauncher, DryFireSound) == 0x80, "CliEQLauncher +0x80");

        // Retail Archetype::CliEQGun : CliEQLauncher (vtable 005c7e6c), sizeof 0x88, factory type
        // 0x11 at 0040ce1a. Slots: ReadUnknown 0040a850 (chains to 0040a4d0 first), Redefine
        // 0040a5e0 and Load 0040a3b0 inherited, Inherit 0040a8a0.
        class FCliGun : public FCliLauncher
        {
        public:
            FCliGun();

            bool ReadUnknown(INI_Reader* Reader) override;
            void Inherit(ICliObj* Donor) override;

            ID_String TurretSound;       // +0x84 "turret_sound"
        };

        static_assert(sizeof(FCliGun) == 0x88, "CliEQGun sizeof 0x88");
        static_assert(offsetof(FCliGun, TurretSound) == 0x84, "CliEQGun +0x84");

        // Retail Archetype::CliEQEngine (vtable 005c7ac4), sizeof 0xAC, factory type 0xC at
        // 0040cd4d (ctor FUN_00408f20). Slots: ReadUnknown 00408ff0, Redefine 004093e0, Inherit
        // 004094e0.
        class FCliEngine : public FCliObj
        {
        public:
            FCliEngine();

            bool ReadUnknown(INI_Reader* Reader) override;
            void Redefine(ICliObj* Donor) override;
            void Inherit(ICliObj* Donor) override;

            int InsideSoundCone;              // +0x54 get_value_int, ctor 0
            int OutsideSoundCone;             // +0x58 get_value_int, ctor 0x168
            float OutsideConeAttenuation;     // +0x5c
            ID_String RumbleSound;            // +0x60
            int RumblePitchRange[2];          // +0x64
            int RumbleAttenRange[2];          // +0x6c
            ID_String CharacterStartSound;    // +0x74
            ID_String CharacterLoopSound;     // +0x78
            int CharacterPitchRange[2];       // +0x7c
            ID_String EngineKillSound;        // +0x84
            ID_String CruiseStartSound;       // +0x88
            ID_String CruiseLoopSound;        // +0x8c
            ID_String CruiseStopSound;        // +0x90
            ID_String CruiseDisruptSound;     // +0x94
            ID_String CruiseBackfireSound;    // +0x98
            ID_String FlameEffect;            // +0x9c
            ID_String TrailEffect;            // +0xa0
            ID_String TrailEffectPlayer;      // +0xa4
            ID_String CruiseDisruptEffect;    // +0xa8
        };

        static_assert(sizeof(FCliEngine) == 0xAC, "CliEQEngine sizeof 0xac");
        static_assert(offsetof(FCliEngine, RumbleSound) == 0x60, "CliEQEngine +0x60");
        static_assert(offsetof(FCliEngine, CharacterPitchRange) == 0x7c, "CliEQEngine +0x7c");
        static_assert(offsetof(FCliEngine, EngineKillSound) == 0x84, "CliEQEngine +0x84");
        static_assert(offsetof(FCliEngine, CruiseDisruptEffect) == 0xa8, "CliEQEngine +0xa8");

        // Retail Archetype::CliEQTradeLane (vtable 005c8074), sizeof 0xC8, factory type 0x1D at
        // 0040d083 (ctor FUN_0040bd90). Slots: ReadUnknown 0040be90, Redefine 0040c290, Inherit
        // 0040c4d0. Non-virtual: FUN_0040c690 (effect AddRef, caller 0x00516cb0) and FUN_0040c7d0
        // (effect Release, caller 0x00516be4). tradelane_fx.cpp reads the IDs by offset.
        class FCliTradeLane : public FCliObj
        {
        public:
            FCliTradeLane();

            bool ReadUnknown(INI_Reader* Reader) override;
            void Redefine(ICliObj* Donor) override;
            void Inherit(ICliObj* Donor) override;

            void AddEffectRef();          // FUN_0040c690
            int ReleaseEffectRef();       // FUN_0040c7d0

            ID_String PlayerEnter;        // +0x54 "tl_player_enter"
            ID_String PlayerTravel;       // +0x58 "tl_player_travel"
            ID_String PlayerSplash;       // +0x5c "tl_player_splash"
            ID_String PlayerExit;         // +0x60 "tl_player_exit"
            ID_String PlayerDisrupt;      // +0x64 "tl_player_disrupt"
            ID_String ShipEnter;          // +0x68 "tl_ship_enter"
            ID_String ShipTravel;         // +0x6c "tl_ship_travel"
            ID_String ShipSplash;         // +0x70 "tl_ship_splash"
            ID_String ShipExit;           // +0x74 "tl_ship_exit"
            ID_String ShipDisrupt;        // +0x78 "tl_ship_disrupt"
            ID_String RingActive;         // +0x7c "tl_ring_active"
            ID_String RingShield;         // +0x80 "tl_ring_shield"
            ID_String RingTube;           // +0x84 "tl_ring_tube"
            ID_String RingActiveStart;    // +0x88 "tl_ring_active_start"
            ID_String RingActiveLoop;     // +0x8c "tl_ring_active_loop"
            ID_String RingActiveEnd;      // +0x90 "tl_ring_active_end"
            float SecsBeforeEnter;        // +0x94 ctor 1
            float SecsBeforeExit;         // +0x98 ctor 1
            float SecsBeforeSplash;       // +0x9c ctor 1
            float ActivationStart;        // +0xa0 ctor 600
            float ActivationEnd;          // +0xa4 ctor 300
            float SpinAccel;              // +0xa8 ctor 25
            float SpinMax;                // +0xac ctor 50
            int EffectRefCount;           // +0xb0 ctor 0
            FEffectArch* Effects[5];      // +0xb4 player effect, else the ship one (0040c690)
        };

        static_assert(sizeof(FCliTradeLane) == 0xC8, "CliEQTradeLane sizeof 0xc8");
        static_assert(offsetof(FCliTradeLane, PlayerEnter) == 0x54, "CliEQTradeLane +0x54");
        static_assert(offsetof(FCliTradeLane, RingActiveEnd) == 0x90, "CliEQTradeLane +0x90");
        static_assert(offsetof(FCliTradeLane, SpinMax) == 0xac, "CliEQTradeLane +0xac");
        static_assert(offsetof(FCliTradeLane, Effects) == 0xb4, "CliEQTradeLane +0xb4");

        FCliShip* AsCliShip(ICliObj* Companion)
        {
            return reinterpret_cast<FCliShip*>(Companion);
        }

        FCliSolar* AsCliSolar(ICliObj* Companion)
        {
            return reinterpret_cast<FCliSolar*>(Companion);
        }

        // --- MaterialLib path cache (DAT_006166d8 / DAT_006166ec) ---

        // DAT_006166ec. LAB_0041a860 (static initializer) runs InitializeCriticalSection at load
        // and registers LAB_0041a880 (DeleteCriticalSection) through atexit (FUN_005b7e6c); a
        // namespace-scope object with that ctor/dtor pair is the same lifetime. There is no
        // lazy "initialised yet?" latch in retail -- every user just enters the lock.
        struct FMaterialCacheLock
        {
            CRITICAL_SECTION Cs;

            FMaterialCacheLock() { InitializeCriticalSection(&Cs); }
            ~FMaterialCacheLock() { DeleteCriticalSection(&Cs); }

            FMaterialCacheLock(FMaterialCacheLock const&) = delete;
            FMaterialCacheLock& operator=(FMaterialCacheLock const&) = delete;
        };
        FMaterialCacheLock GMaterialCacheLock;

        // DAT_006166d8: VC6 std::map<CRC, entry> -- the ENTRY lives in the tree node (value at
        // node + 0xc, the pointer every caller holds), not in a separate allocation. LAB_0041a890
        // builds it empty (FUN_0041b310) and registers LAB_0041a8c0, whose FUN_0041ba80 walk only
        // frees nodes -- nothing is released through the libraries at exit. std::map with the
        // entry by value has exactly those properties (node addresses are stable, so
        // &It->second is the retail node + 0xc pointer).
        std::map<unsigned long, FCliMaterialEntry> GMaterialCacheByCrc;

        // FUN_0041acf0 — init probe/entry fields from path CRC.
        void InitMaterialEntry(FCliMaterialEntry* Entry, char const* Path)
        {
            Entry->PathCrc = DACOM_CRC::GetCRC32(Path);
            Entry->TextureId = -1;
            Entry->MaterialId = -1;
            Entry->VMeshHandle = nullptr;
            Entry->MatAnimHandle = nullptr;
            Entry->RefCount = 1;
        }

        // FUN_0041a9a0 (cdecl Path, FCliMaterialEntry** Out) -> bool inserted. Its one caller is
        // FUN_0041ade0 (0x0041adf6). 0x0041a9b8: enter the lock; the candidate value is built on
        // the stack exactly as FUN_0041acf0 does (CRC, -1, -1, 0, 0, RefCount 1); FUN_0041b380 is
        // the map's unique insert (pair<iterator, bool>); leave the lock; *Out = node + 0xc; and
        // only when the key was ALREADY there does the existing entry gain a reference
        // (0x0041aa34 INC [ECX+0x14]) -- after the lock has been released, as retail does it.
        bool FindOrInsertMaterialEntry(char const* Path, FCliMaterialEntry** Out)
        {
            EnterCriticalSection(&GMaterialCacheLock.Cs);
            FCliMaterialEntry Candidate;
            InitMaterialEntry(&Candidate, Path);
            std::pair<std::map<unsigned long, FCliMaterialEntry>::iterator, bool> const Result =
                GMaterialCacheByCrc.insert(std::pair<unsigned long const, FCliMaterialEntry>(
                    Candidate.PathCrc, Candidate));
            LeaveCriticalSection(&GMaterialCacheLock.Cs);

            FCliMaterialEntry* const Entry = &Result.first->second;
            *Out = Entry;
            if (!Result.second)
                Entry->RefCount += 1;
            return Result.second;
        }

        // FUN_0041ad30 (thiscall on the entry, RET 8: file system, group arg) -- TextureLib load
        // into Entry->TextureId (+0x04), with a texture-group override while the perf-option
        // float slot 1 (FUN_005b3510 = GetPerfOptionSlot, DAT_0067ed14) is below 1.0.
        // Retail tests none of TextureLib, Filesys or Entry.
        bool LoadMaterialTextures(IFileSystem* Filesys, FCliMaterialEntry* Entry, unsigned Arg)
        {
            // 0x0041ad38: the saved group starts at 0.
            unsigned long SavedGroup = 0;
            bool bRestoreGroup = false;
            int GroupOverride = static_cast<int>(Arg);

            // 0x0041ad40..0x0041ad53: FCOMP 1.0f (DAT_005c75dc) / TEST AH,0x05 / JP -- the
            // override path is taken only when the slot is strictly LESS than 1.0 (ordered);
            // >= 1.0 or unordered goes straight to the load with BL = 0.
            if (GetPerfOptionSlot(1) < 1.0f)
            {
                // 0x0041ad59..0x0041ad65: a zero group arg refuses the load outright.
                if (GroupOverride == 0)
                    return false;

                bRestoreGroup = true;
                // 0x0041ad6c: JGE -- a negative arg (the -1 every caller passes) becomes 0x80.
                if (GroupOverride < 0)
                    GroupOverride = 0x80;

                // 0x0041ad84 slot 4 (+0x10) GetGenericSlot(4, &saved); 0x0041ad94 slot 3 (+0x0c)
                // SetGenericSlot(4, group).
                DALib::TextureLib->GetGenericSlot(4, &SavedGroup);
                DALib::TextureLib->SetGenericSlot(4, static_cast<unsigned long>(GroupOverride));
            }

            // 0x0041adab: slot 5 (+0x14) with the entry's +0x04.
            GENRESULT const Result = DALib::TextureLib->LoadTextureGroup(Filesys, &Entry->TextureId);

            // 0x0041adae..0x0041adc5: restore the group only if it was overridden.
            if (bRestoreGroup)
                DALib::TextureLib->SetGenericSlot(4, SavedGroup);

            // 0x0041adcb: SETGE on the load's result -- the texture id is not looked at.
            return Result >= 0;
        }

        void DestroyMaterialEntryResources(FCliMaterialEntry* Entry)
        {
            if (Entry->TextureId != -1)
            {
                DALib::TextureLib->GarbageCollect(Entry->TextureId);
                Entry->TextureId = -1;
            }

            if (Entry->MaterialId != -1)
            {
                DALib::MaterialLib->DeleteGroup(Entry->MaterialId);
                Entry->MaterialId = -1;
            }

            if (Entry->MatAnimHandle != nullptr)
            {
                DALib::MatAnimLibrary->UnloadAnimationGroup(Entry->MatAnimHandle);
                Entry->MatAnimHandle = nullptr;
            }

            if (Entry->VMeshHandle != nullptr)
            {
                DALib::VMeshLibrary->Unload(Entry->VMeshHandle);
                Entry->VMeshHandle = nullptr;
            }
        }

    } // namespace (reopened below): FUN_0041aa50 has callers outside this file

    // FUN_0041aa50(path, bForceApply, arg) — cdecl, returns cache entry*.
    // Callers: this file's companion material walk, and the mesh holder load FUN_00420dd0
    // (client_mesh_load.cpp).
        FCliMaterialEntry* ResolveMaterialEntry(char const* Path, bool bForceApply, unsigned Arg)
        {
            // 0x0041aa75 / 0x0041aa7e: a null or empty path -> null.
            if (Path == nullptr || Path[0] == '\0')
                return nullptr;

            // 0x0041aa88 .. 0x0041aae7: the same find-or-insert FUN_0041a9a0 performs, inlined
            // here (FUN_0041acf0 value, FUN_0041b380 insert, the increment of an existing entry
            // after the unlock).
            FCliMaterialEntry* Entry = nullptr;
            bool const bInserted = FindOrInsertMaterialEntry(Path, &Entry);

            if (bInserted)
            {
                // 0x0041aaed .. 0x0041ab5d: a "FileSystem" instance for Path, built inline through
                // DALib::Data slot 3 (+0x0c) with the DALib::OpenData-shaped descriptor.
                FFileSystemAggDesc Desc;
                memset(&Desc, 0, sizeof(Desc));
                Desc.Size = 0x34;
                Desc.ComponentName = "FileSystem";
                Desc.DataPath = Path;
                Desc.Field10 = 0x80000000u;
                Desc.Field14 = 1;
                Desc.Field1C = 3;
                Desc.Field20 = 0x8000080u;
                Desc.Field30 = 0xffffffffu;

                void* FsOut = nullptr;
                GENRESULT const OpenResult = static_cast<IFileSystem*>(DALib::Data)->CreateInstance(
                    reinterpret_cast<AGGDESC*>(&Desc), &FsOut);
                if (OpenResult < 0)
                {
                    // 0x0041ab64 .. 0x0041ab84: the error, and NOTHING else -- the empty entry
                    // stays in the cache and is still returned below.
                    FDUMP(0x100001,
                          "%s(%d) : *** ERROR: DATA: MaterialLib::Load(%s) couldn't load file.",
                          "E:\\FL\\Scratch\\Source\\Client\\cmn\\MaterialLib.cpp", 0x10a, Path);
                }
                else
                {
                    IFileSystem* const Filesys = static_cast<IFileSystem*>(FsOut);

                    // 0x0041ab9b: FUN_0041ad30 (textures).
                    bool const bTexOk = LoadMaterialTextures(Filesys, Entry, Arg);

                    // 0x0041abaa .. 0x0041abbc: MaterialLib +0x20 into +0x08 -- its RESULT is
                    // what the failure test reads.
                    GENRESULT MaterialResult = -1;
                    if (DALib::MaterialLib != nullptr)
                        MaterialResult =
                            DALib::MaterialLib->LoadFromFileSystem(Filesys, &Entry->MaterialId);

                    // 0x0041abbe .. 0x0041abe5: MatAnim +0x0c into +0x10, VMesh +0x0c(fs, -1)
                    // into +0x0c; these library null tests are retail's.
                    if (DALib::MatAnimLibrary != nullptr)
                        Entry->MatAnimHandle = DALib::MatAnimLibrary->LoadAnimationGroup(Filesys);
                    if (DALib::VMeshLibrary != nullptr)
                        Entry->VMeshHandle = DALib::VMeshLibrary->Load(Filesys, -1);

                    // 0x0041abe8 .. 0x0041ac5a: nothing loaded at all -> the entry leaves the
                    // cache (equal_range FUN_0041ba10 + erase FUN_0041b590 under the lock) and
                    // the result becomes null.
                    if (!bTexOk && MaterialResult < 0 && Entry->VMeshHandle == nullptr &&
                        Entry->MatAnimHandle == nullptr)
                    {
                        unsigned long const Key = Entry->PathCrc;   // the node dies with the erase
                        EnterCriticalSection(&GMaterialCacheLock.Cs);
                        GMaterialCacheByCrc.erase(Key);
                        LeaveCriticalSection(&GMaterialCacheLock.Cs);
                        Entry = nullptr;
                    }
                }

                // 0x0041ac60 .. 0x0041ac7c: the file system is released whichever way it went.
                if (FsOut != nullptr)
                    static_cast<IFileSystem*>(FsOut)->Release();
            }

            // 0x0041ac86: null -> null.
            if (Entry == nullptr)
                return nullptr;

            // 0x0041ac8a .. 0x0041acc1: on request, MaterialLib +0x2c (id, 0, 1.0f) and +0x30
            // (id, 0) -- for a cache hit and a fresh load alike, with no library null test.
            if (bForceApply && Entry->MaterialId != -1)
            {
                DALib::MaterialLib->ApplyVerifyGroup(
                    Entry->MaterialId, nullptr, reinterpret_cast<void*>(0x3f800000u));
                DALib::MaterialLib->ResolveTexturesGroup(Entry->MaterialId, 0.0f);
            }

            return Entry;
        }

    FCliMaterialEntry* ResolveMaterialEntryFromFs(IFileSystem* Filesys, char const* Path,
                                                  bool bForceApply, unsigned Arg)
    {
        // FUN_0041ade0. 0x0041ade8..0x0041ae04: FUN_0041a9a0 -- find-or-insert Path's CRC under
        // the cache lock; an existing entry gains a reference (0x0041aa34), a new one starts
        // at 1. Only a new entry is loaded.
        FCliMaterialEntry* Entry = nullptr;
        bool const bInserted = FindOrInsertMaterialEntry(Path, &Entry);

        if (bInserted)
        {
            // 0x0041ae0a..0x0041ae17: FUN_0041ad30 (textures) from the caller's file system.
            bool bTexOk = LoadMaterialTextures(Filesys, Entry, Arg);

            // 0x0041ae1e..0x0041ae37: MaterialLib +0x20 into the entry's +0x08; its result is
            // what the failure test below reads.
            GENRESULT MaterialResult = -1;
            if (DALib::MaterialLib != nullptr)
                MaterialResult = DALib::MaterialLib->LoadFromFileSystem(Filesys, &Entry->MaterialId);

            // 0x0041ae39..0x0041ae60: MatAnim +0x0c into +0x10, VMesh +0x0c(fs, -1) into +0x0c.
            if (DALib::MatAnimLibrary != nullptr)
                Entry->MatAnimHandle = DALib::MatAnimLibrary->LoadAnimationGroup(Filesys);
            if (DALib::VMeshLibrary != nullptr)
                Entry->VMeshHandle = DALib::VMeshLibrary->Load(Filesys, -1);

            // 0x0041ae63..0x0041aeed: nothing loaded at all -- take the entry out of the cache
            // (equal_range on the entry's own key, FUN_0041bd40 / FUN_0041bd10, then erase
            // FUN_0041b590; erasing the node frees the entry) and return null.
            if (!bTexOk && MaterialResult < 0 && Entry->VMeshHandle == nullptr &&
                Entry->MatAnimHandle == nullptr)
            {
                unsigned long const Key = Entry->PathCrc;       // the node dies with the erase
                EnterCriticalSection(&GMaterialCacheLock.Cs);
                GMaterialCacheByCrc.erase(Key);
                LeaveCriticalSection(&GMaterialCacheLock.Cs);
                return nullptr;
            }
        }

        // 0x0041aeee..0x0041af28: MaterialLib +0x2c / +0x30 on request.
        if (Entry != nullptr && bForceApply && Entry->MaterialId != -1)
        {
            DALib::MaterialLib->ApplyVerifyGroup(
                Entry->MaterialId, nullptr, reinterpret_cast<void*>(0x3f800000u));
            DALib::MaterialLib->ResolveTexturesGroup(Entry->MaterialId, 0.0f);
        }
        return Entry;
    }

    int ResolveMaterialEntryList(FCliVc6Vector<FCliMaterialPathPair> const* Paths,
                                 FCliMaterialEntry** Out, int Max)
    {
        // FUN_0041af40.
        int Count = 0;
        if (Paths == nullptr)
            return Count;
        // 0x0041af4e..0x0041af86: more pairs than Max is an error, reported once.
        if (Paths->Begin != nullptr && static_cast<unsigned>(Paths->End - Paths->Begin) >
                                           static_cast<unsigned>(Max))
        {
            FDUMP(0x100001, "%s(%d) : *** ERROR: %s\n",
                  "E:\\FL\\Scratch\\Source\\Client\\cmn\\MaterialLib.cpp", 0x14a,
                  "too many material files passed to the material loader");
        }
        // 0x0041af89..0x0041afb9: FUN_0041aa50(Path, 1, Arg) for each pair, at most Max.
        FCliMaterialPathPair const* Pair = Paths->Begin;
        for (; Pair != Paths->End; ++Pair)
        {
            if (Count >= Max)
                break;
            Out[Count] = ResolveMaterialEntry(Pair->Path, true, Pair->Arg);
            ++Count;
        }
        return Count;
    }

    namespace
    {
        // FUN_0041afd0 — force-realize one cache entry (Slot10 walk). Not a release.
        // 0x0041afd0..0x0041b027: null -> -1; else under the lock, MaterialId != -1 ->
        // MaterialLib +0x2c (id, 0, 1.0f) and +0x30 (id, 0) with no library null test; 0.
        int ForceRealizeMaterialEntry(FCliMaterialEntry* Entry)
        {
            if (Entry == nullptr)
                return -1;

            EnterCriticalSection(&GMaterialCacheLock.Cs);
            if (Entry->MaterialId != -1)
            {
                DALib::MaterialLib->ApplyVerifyGroup(
                    Entry->MaterialId, nullptr, reinterpret_cast<void*>(0x3f800000u));
                DALib::MaterialLib->ResolveTexturesGroup(Entry->MaterialId, 0.0f);
            }
            LeaveCriticalSection(&GMaterialCacheLock.Cs);
            return 0;
        }

    }

    int ReleaseMaterialEntrySlot(FCliMaterialEntry** Slot)
    {
        int Remaining = -1;
        if (*Slot != nullptr)
        {
            EnterCriticalSection(&GMaterialCacheLock.Cs);
            FCliMaterialEntry* Entry = *Slot;
            if (Entry != nullptr)
            {
                --Entry->RefCount;
                if (Entry->RefCount <= 0)
                    DestroyMaterialEntryResources(Entry);
                Remaining = Entry->RefCount;
                // Erasing the node frees the entry with it (retail FUN_0041b590 on the
                // equal_range); Entry is not touched afterwards.
                if (Remaining <= 0)
                {
                    unsigned long const Key = Entry->PathCrc;   // the node dies with the erase
                    GMaterialCacheByCrc.erase(Key);
                }
            }
            LeaveCriticalSection(&GMaterialCacheLock.Cs);
        }
        *Slot = nullptr;
        return Remaining;
    }

    int FlushMaterialCache()
    {
        // FUN_0041b150 (cdecl, no arguments), called once from the client shutdown at 0x005b22f6.
        // 0x0041b156: under the cache lock, and with no library null tests anywhere.
        EnterCriticalSection(&GMaterialCacheLock.Cs);

        // 0x0041b178 .. 0x0041b20c: an in-order walk over every node; each one is counted and has
        // its four handles given back and reset in this order -- TextureLib +0x1c on +0x04 (-1),
        // MaterialLib +0x28 on +0x08 (-1), MatAnimLibrary +0x10 on +0x10 (0), VMeshLibrary +0x10
        // on +0x0c (0) -- each only when set. That is DestroyMaterialEntryResources, the same
        // body ReleaseMaterialEntrySlot (FUN_0041b030) inlines. Reference counts are ignored.
        int Count = 0;
        std::map<unsigned long, FCliMaterialEntry>::iterator It = GMaterialCacheByCrc.begin();
        for (; It != GMaterialCacheByCrc.end(); ++It)
        {
            ++Count;
            DestroyMaterialEntryResources(&It->second);
        }

        // 0x0041b213 .. 0x0041b2c5: erase(begin, end) -- the whole-tree fast path (FUN_0041ba80
        // on the root, head reset) or node-by-node FUN_0041b640; either way the map is empty and
        // every entry is freed with its node.
        GMaterialCacheByCrc.clear();

        LeaveCriticalSection(&GMaterialCacheLock.Cs);

        // EBP: the number of entries flushed.
        return Count;
    }

    namespace
    {
        // The per-mesh body FUN_00425330 (0x00425363..0x004253e4) and its child callback
        // LAB_004253f0 (0x004253f0..0x00425479) share: Renderer slot 11 (+0x2c) fills the level
        // count (local 0, result not read); for I in [0, count) (signed JLE / JL, the count
        // re-read every pass): Renderer slot 13 (+0x34)(mesh, count - I - 1), then Engine slot
        // 18 (+0x48) QueryEngineComponent(mesh, "1.11_IMatHookUp", &hook) with hook = 0 first;
        // a non-negative answer -> hook slot 3 (+0xc), then Release (+8), no null test.
        void UnloadMatHookUpsSimple(void* Handle)
        {
            int LodCount = 0;
            DALib::Renderer->GetArchetypeLodCount(Handle, &LodCount);
            for (int Index = 0; Index < LodCount; ++Index)
            {
                DALib::Renderer->SetArchetypeLod(Handle, LodCount - Index - 1);

                void* MatHook = nullptr;
                GENRESULT const Qi = DALib::Engine->QueryEngineComponent(
                    static_cast<int>(reinterpret_cast<ptrdiff_t>(Handle)),
                    const_cast<char*>(Iids::MatHookUp), &MatHook);
                if (Qi >= 0)
                {
                    IMatHookUp* const Hook = static_cast<IMatHookUp*>(MatHook);
                    Hook->HookUpMaterials();
                    Hook->Release();
                }
            }
        }

        // FUN_00425330 (cdecl, one argument), listing-traced 0x00425330..0x004253e7: a compound
        // archetype (Engine slot 19, +0x4c) walks its children (slot 21, +0x54) with
        // LAB_004253f0 and a null context (the callback answers AL = 1); otherwise the body on
        // the argument itself. No test of the argument or of the DALib globals.
        void UnloadEngineComponents(long EngineIndex)
        {
            void* const Handle = reinterpret_cast<void*>(EngineIndex);
            if (DALib::Engine->IsCompoundArchetype(Handle) != 0)
            {
                DALib::Engine->EnumerateArchetypeChildren(
                    Handle,
                    [](void* Parent, void* ChildHandle, int Zero, void* Ctx) -> int {
                        (void)Parent;
                        (void)Zero;
                        (void)Ctx;
                        UnloadMatHookUpsSimple(ChildHandle);   // LAB_004253f0
                        return 1;
                    },
                    nullptr);
                return;
            }
            UnloadMatHookUpsSimple(Handle);
        }

        // ArchLod apply context (stack block built by FUN_004021b0 before FUN_00401ed0).
        struct FArchLodContext
        {
            FLodNode* Head;       // +0x00
            float MaxDistance;    // +0x04  written by apply; returned as LodPick
            int Unused08;         // +0x08  setnz(bSolarOrShip) — unread by 00401ed0 body
            int MinSpecCap;       // +0x0C  MinSpecLOD when global mute set
            float DistanceScale;  // +0x10  DAT_006101d4
        };

        // DAT_00616470 / DAT_006101d4 — ArchLod global mute + distance scale.
        unsigned char GArchLodMute = 0;
        float GArchLodScale = 1.0f;
        unsigned char GArchLodWarned = 0;

        // FUN_004022b0
        void SetArchLodGlobals(unsigned char bMute, float Scale)
        {
            GArchLodMute = bMute;
            GArchLodScale = Scale;
        }

        // FUN_004022d0
        unsigned char GetArchLodGlobals(float* OutScale)
        {
            if (OutScale != nullptr)
                *OutScale = GArchLodScale;
            return GArchLodMute;
        }

        // .rdata DAT_005c7478 (float 15000.0) and DAT_005c7410 (double 0.700208).
        static float const kArchLodFarCutoff = 15000.0f;
        static double const kArchLodRangeScale = 0.700208;

        // FUN_00401ed0 (cdecl, returns AL = 1 on every path), listing-traced
        // 0x00401ed0..0x00402174 -- the LOD ranges of one mesh onto the renderer. The callback
        // IEngine slot 21 (EnumerateArchetypeChildren) calls with (parent, child, 0, context),
        // and FUN_004021b0 calls directly with (-1, mesh, 0, context). Nothing is null-tested.
        // The node's floats are read raw from the node base (Ranges, then whatever follows at
        // +0x20.. when the renderer reports more than eight levels), as retail indexes them.
        int __cdecl ApplyArchLodRanges(void* ParentOrNeg1, void* MeshHandle, int /*Zero*/,
                                       void* ContextRaw)
        {
            FArchLodContext* const Ctx = static_cast<FArchLodContext*>(ContextRaw);

            // 0x00401eda..0x00401f05: Renderer slot 11 (+0x2c) fills the level count (the local
            // starts at 0; the call's result is not read); 0 levels -> done.
            int LodCount = 0;
            DALib::Renderer->GetArchetypeLodCount(MeshHandle, &LodCount);
            if (LodCount == 0)
                return 1;

            // 0x00401f0b..0x00401f2f: the part name is Engine2 slot 56 (+0xe0)(parent, child) --
            // the child record, whose name is its first member; none -> "Root" (DAT_005c74f8).
            IEngine* const Engine2 = reinterpret_cast<IEngine*>(DALib::Engine2);
            char const* PartName =
                static_cast<char const*>(Engine2->FindArchetypeChild(ParentOrNeg1, MeshHandle));
            if (PartName == nullptr)
                PartName = "Root";

            // 0x00401f2f..0x00401f7d: more than eight levels (signed): the message is formatted
            // (FUN_00402180, 0x2000 bytes) and reported once (DAT_00616471 latch).
            if (LodCount > 8)
            {
                char Message[0x2000];
                _snprintf(Message, sizeof(Message),
                          "number of lods(%d) in mesh exceeded max lods(%d) for %s", LodCount, 8,
                          PartName);
                if (GArchLodWarned == 0)
                {
                    GArchLodWarned = 1;
                    FDUMP(0x100001, "%s(%d) : *** ERROR: %s",
                          "E:\\FL\\Scratch\\Source\\Client\\cmn\\ArchLod.cpp", 0x6a, Message);
                }
            }

            // 0x00401f80..0x00401fbd: the first node whose part CRC (+0x24) is 0 or matches; no
            // list, or no match -> done.
            unsigned long const PartCrc = DACOM_CRC::GetCRC32(PartName);
            FLodNode* const Head = Ctx->Head;
            FLodNode* Match = Head;
            if (Match == nullptr)
                return 1;
            while (Match->ObjCrc != 0 && Match->ObjCrc != PartCrc)
            {
                Match = Match->Next;
                if (Match == nullptr)
                    return 1;
            }
            float const* const Raw = Match->Ranges;

            // 0x00401fbe..0x00401fe1: both +0x28 bytes set; the first "previous" range is
            // range[0] * context scale * 0.700208, stored as a float; the top level is the
            // context's min-spec cap clamped (signed) to the level count.
            Head->Unk28 = 1;
            Match->Unk28 = 1;
            float PriorScaled = static_cast<float>(static_cast<double>(Raw[0]) *
                                                   static_cast<double>(Ctx->DistanceScale) *
                                                   kArchLodRangeScale);
            int Cap = Ctx->MinSpecCap;
            if (Cap > LodCount)
                Cap = LodCount;

            // 0x00401fe3..0x00402013: the far clamp is on when range[count] is below 15000
            // (TEST AH,5 / JP skips greater, equal and unordered) and DAT_006101d4 * that range
            // is above it (TEST AH,0x41 / JNZ skips not-greater and unordered).
            bool bFarClamp = false;
            if (static_cast<double>(Raw[LodCount]) < static_cast<double>(kArchLodFarCutoff) &&
                static_cast<double>(GArchLodScale) * static_cast<double>(Raw[LodCount]) >
                    static_cast<double>(kArchLodFarCutoff))
                bFarClamp = true;

            // 0x00402018..0x00402081: the levels below the cap get the ranges (0, 0) while the
            // next range is not negative (TEST AH,5 / JNP: only an ordered "less" warns); a
            // negative one warns and stops there.
            int Applied = 0;
            if (Cap > 0)
            {
                int Index = 0;
                for (; Index < Cap; ++Index)
                {
                    if (static_cast<double>(Raw[Index + 1]) < 0.0)
                    {
                        FDUMP(0x100002, "%s(%d) : WARNING:General:%s",
                              "E:\\FL\\Scratch\\Source\\Client\\cmn\\ArchLod.cpp", 0xa0,
                              "top lod specified more than the number of lods in the file???");
                        break;
                    }
                    DALib::Renderer->SetArchetypeLodRange(MeshHandle, Index, 0.0f, 0.0f);   // +0x3c
                }
                Applied = Index;
            }

            // 0x00402096..0x00402099: nothing left above the applied levels -> done.
            if (Applied >= LodCount)
                return 1;

            // 0x0040209f..0x004020af: range[applied] above the context's maximum (TEST AH,0x41 /
            // JNZ skips not-greater and unordered) becomes the maximum, copied as a dword.
            if (static_cast<double>(Raw[Applied]) > static_cast<double>(Ctx->MaxDistance))
                Ctx->MaxDistance = Raw[Applied];

            // 0x004020b2..0x00402163: every remaining level I takes the NEXT range, R =
            // range[I + 1] (EDI starts at &range[applied + 1]); a negative or unordered one (TEST
            // AH,1 / JNZ) ends the walk. With the far clamp and R * context scale above 15000,
            // R becomes I + 15000 (FILD + FADD, kept on the FPU). Current = R * 0.700208 as a
            // float; R above the maximum becomes the maximum (as a float); then renderer slot 15
            // (+0x3c)(mesh, I, previous, current) and slot 19 (+0x4c)(mesh, -1, context +8), and
            // the current range is the next "previous".
            int Index = Applied;
            do
            {
                double Range = static_cast<double>(Raw[Index + 1]);
                if (!(Range >= 0.0))
                    return 1;
                if (bFarClamp &&
                    Range * static_cast<double>(Ctx->DistanceScale) >
                        static_cast<double>(kArchLodFarCutoff))
                    Range = static_cast<double>(Index) + static_cast<double>(kArchLodFarCutoff);
                float const CurrentScaled = static_cast<float>(kArchLodRangeScale * Range);
                if (Range > static_cast<double>(Ctx->MaxDistance))
                    Ctx->MaxDistance = static_cast<float>(Range);
                DALib::Renderer->SetArchetypeLodRange(MeshHandle, Index, PriorScaled, CurrentScaled);
                DALib::Renderer->SetArchetypeRootLodSelection(MeshHandle, reinterpret_cast<void*>(-1),
                                        static_cast<unsigned long>(Ctx->Unused08));
                PriorScaled = CurrentScaled;
                ++Index;
            } while (Index < LodCount);

            return 1;
        }

        // FUN_004021b0 — thiscall on LodHead; blends optional source, then ApplyArchLodRanges.
        float ComputeLodPickThis(FLodNode* LodHead, long EngineIndex, float const* BlendSource,
                                 bool bSolarOrShip)
        {
            // FUN_004021b0 (thiscall on the LOD list head, RET 0xc), listing-traced
            // 0x004021b0..0x004022a4. The answer starts as -1.0f (DAT_005c7500).
            float const Result = -1.0f;

            // 0x004021bb..0x00402203: a blend source that is neither null nor the list itself,
            // with a list: every node takes the source's ranges up to the first negative or
            // unordered one (TEST AH,1 / JNZ), at most eight, copied as dwords, then -1.0f in
            // the slot after the last copied one (the ninth slot when all eight were copied).
            if (BlendSource != nullptr && BlendSource != reinterpret_cast<float const*>(LodHead) &&
                LodHead != nullptr)
            {
                FLodNode* Node = LodHead;
                while (Node != nullptr)
                {
                    int I = 0;
                    while (I < 8)
                    {
                        if (!(static_cast<double>(BlendSource[I]) >= 0.0))
                            break;
                        memcpy(&Node->Ranges[I], &BlendSource[I], sizeof(float));
                        I += 1;
                    }
                    float* const Slot =
                        reinterpret_cast<float*>(reinterpret_cast<char*>(Node) + I * 4);
                    *Slot = -1.0f;
                    Node = Node->Next;
                }
            }

            // 0x00402204..0x0040220b: no engine object -> -1.
            if (EngineIndex == -1)
                return Result;

            // 0x00402211..0x0040224b: the context {list, -1.0f, bSolarOrShip != 0, 0,
            // DAT_006101d4}; with DAT_00616470 set the cap is the head's +0x2c (read without a
            // null test).
            FArchLodContext Ctx;
            Ctx.Head = LodHead;
            Ctx.MaxDistance = -1.0f;
            Ctx.Unused08 = bSolarOrShip ? 1 : 0;
            Ctx.MinSpecCap = 0;
            Ctx.DistanceScale = GArchLodScale;
            if (GArchLodMute != 0)
                Ctx.MinSpecCap = LodHead->MinSpecLod;

            // 0x0040224f..0x00402297: a compound archetype (Engine slot 19, +0x4c) runs
            // FUN_00401ed0 over its children (slot 21, +0x54); otherwise FUN_00401ed0(-1, it, 0,
            // &context) directly. The answer is the context's maximum.
            void* const Handle = reinterpret_cast<void*>(EngineIndex);
            if (DALib::Engine->IsCompoundArchetype(Handle) != 0)
            {
                DALib::Engine->EnumerateArchetypeChildren(Handle, ApplyArchLodRanges, &Ctx);
                return Ctx.MaxDistance;
            }
            ApplyArchLodRanges(reinterpret_cast<void*>(-1), Handle, 0, &Ctx);
            return Ctx.MaxDistance;
        }

        // FUN_004022f0 — cdecl thunk: (EngineIndex, LodHead, BlendSource, bSolarOrShip).
        float ComputeLodPick(long EngineIndex, FLodNode* LodHead, int BlendSource,
                             bool bSolarOrShip)
        {
            float const* Blend = reinterpret_cast<float const*>(
                static_cast<ptrdiff_t>(BlendSource));
            return ComputeLodPickThis(LodHead, EngineIndex, Blend, bSolarOrShip);
        }

        // FUN_00402580 is BindRendererLods (part_cache.h); this file has no caller of it.

        // FUN_004024d0 — free chain through head slot.
        void FreeLodChain(FLodNode** HeadSlot)
        {
            if (HeadSlot == nullptr)
                return;
            FreeLodList(*HeadSlot);
            *HeadSlot = nullptr;
        }

        // FUN_00402500 (cdecl; every Redefine and the base Inherit 00406970). Frees the chain
        // in *DestSlot, then copies Source node by node -- but retail uses *DestSlot itself as
        // the walking cursor: each pass stores the new node there, links it after the previous
        // one, then overwrites *DestSlot with the new node's +0x30 (copied from the source
        // node, i.e. the source's next). On return *DestSlot therefore holds the source tail's
        // Next -- null -- and the copied nodes are unreachable (leaked). Every caller then sees
        // an empty LOD list (LodPick -1).
        //
        // Retail UB (deviations.md, "FUN_00402500 aliasing", closed under the owner's 2026-10-08
        // ruling): the base Inherit 00406970 calls this after its memberwise copy, so
        // *DestSlot == Source there and retail frees the donor's chain and then reads the freed
        // nodes (00402510..20, then 00402550 / 00402565). The port does not free a chain that is
        // also the source; the recipient still ends with an empty slot, as in retail. Shipped
        // data never takes that path (only [Light] archetypes use inherit=, and CliEQLight's
        // Inherit 0040b0a0 does not call this).
        void CopyLodChain(FLodNode** DestSlot, FLodNode* Source)
        {
            // 00402503..0040252a.
            if (*DestSlot != Source)
                FreeLodList(*DestSlot);
            *DestSlot = nullptr;

            // 00402532..0040256f.
            FLodNode* Prev = nullptr;
            FLodNode* Src = Source;
            while (Src != nullptr)
            {
                FLodNode* Copy = new FLodNode();
                memcpy(Copy, Src, sizeof(FLodNode));
                *DestSlot = Copy;
                if (Prev != nullptr)
                    Prev->Next = Copy;
                Prev = *DestSlot;
                Src = Src->Next;
                *DestSlot = Prev->Next;
            }
        }

        // --- EffectArch registry (DAT_006164b0) — shared by distance-record + CliMunition ---
        // The lookup FUN_0040f080 (::FindEffectArch) and EffectArch AddRef/Release
        // FUN_0040e960 / FUN_0040e990 (::EffectArchAddRef / ::EffectArchRelease) live with the
        // registry in vis_effect_arch.cpp. This file used to carry its own std::map that
        // nothing ever filled, so every distance-record and munition lookup here missed.

        // FUN_00555810 — random pick from live EffectArch* vector.
        void* ActivateDistanceRecord(FCliDistanceRecord* Record)
        {
            // 00555813..00555824: null live begin or an empty live list -> 0.
            if (Record->LiveBegin == nullptr)
                return nullptr;

            int Count = static_cast<int>(Record->LiveEnd - Record->LiveBegin);
            if (Count == 0)
                return nullptr;

            // 00555839..00555851: rand()*count, CDQ, the sign mask AND 0x7fff added, SAR 15.
            // rand() is at most 0x7fff, so the index is always below count; retail has no clamp.
            int Raw = rand() * Count;
            int Pick = (Raw + ((Raw >> 31) & 0x7fff)) >> 15;
            return Record->LiveBegin[Pick];
        }

        void ClearLiveEffects(FCliDistanceRecord* Record)
        {
            // A null live begin is an empty range (retail FUN_005559c0 just finds begin == end).
            if (Record->LiveBegin == nullptr)
                return;
            void** It = Record->LiveBegin;
            void** End = Record->LiveEnd;
            while (It != End)
            {
                EffectArchRelease(static_cast<FEffectArch*>(*It));
                It += 1;
            }
            Record->LiveEnd = Record->LiveBegin;
        }

        // FUN_00555860 — rebuild live list from EffectId keys (Slot12 Arg unused by AddRef body).
        void ApplyDistanceRecord(FCliDistanceRecord* Record, unsigned Arg)
        {
            // 00555867..005558c0: release every live EffectArch and erase the range.
            ClearLiveEffects(Record);

            int IdCount = 0;
            if (Record->EffectIdBegin != nullptr && Record->EffectIdEnd != nullptr)
                IdCount = static_cast<int>(Record->EffectIdEnd - Record->EffectIdBegin);

            int LiveCap = 0;
            if (Record->LiveBegin != nullptr && Record->LiveCap != nullptr)
                LiveCap = static_cast<int>(Record->LiveCap - Record->LiveBegin);

            if (LiveCap < IdCount)
            {
                int NewCap = IdCount;
                if (NewCap < 0)
                    NewCap = 0;
                void** NewBuf = nullptr;
                if (NewCap > 0)
                    NewBuf = new void*[static_cast<unsigned>(NewCap)];

                // Retail copies old live entries then OnReceive-destroys old buffer — after
                // ClearLiveEffects the live range is empty, so just swap buffers.
                delete[] Record->LiveBegin;
                Record->LiveBegin = NewBuf;
                Record->LiveEnd = NewBuf;
                Record->LiveCap = NewBuf != nullptr ? NewBuf + NewCap : nullptr;
            }

            unsigned* IdIt = Record->EffectIdBegin;
            unsigned* IdEnd = Record->EffectIdEnd;
            while (IdIt != IdEnd)
            {
                // 00555980..005559a6: FUN_0040f080, and for a hit FUN_0040e960(Arg) then
                // push_back (FUN_00537260). The reserve above guarantees room for every id, so
                // the port's plain store never outgrows the buffer.
                FEffectArch* Arch = FindEffectArch(IdIt);
                if (Arch != nullptr)
                {
                    EffectArchAddRef(Arch, Arg);
                    *Record->LiveEnd = Arch;
                    Record->LiveEnd += 1;
                }
                IdIt += 1;
            }
        }

        // FUN_005559c0 — Release live EffectArch* and reset end=begin.
        void TeardownDistanceRecord(FCliDistanceRecord* Record)
        {
            ClearLiveEffects(Record);
        }

        FCliObj::FCliObj()
            : LodHead(nullptr)
            , DistanceRender(-1.0f)
            , LodPick(-1.0f)
            , BoundRoot(nullptr)
            , RefCount(0)
            , PendingLoad(nullptr)
            , StateFlags(0)
        {
            memset(&MaterialPaths, 0, sizeof(MaterialPaths));
            memset(&ResolvedEntries, 0, sizeof(ResolvedEntries));
            memset(&DistanceRecords, 0, sizeof(DistanceRecords));
            memset(Pad51, 0, sizeof(Pad51));
            EnvmapMaterial.clear();
        }

        // FUN_004de730 — free VC6 vector buffer at header (cookie + begin/end/cap): delete begin, zero ptrs.
        template <typename T>
        void DestroyVc6Buffer(FCliVc6Vector<T>* Vec)
        {
            if (Vec == nullptr)
                return;
            delete[] Vec->Begin;
            Vec->Begin = nullptr;
            Vec->End = nullptr;
            Vec->Capacity = nullptr;
        }

        void FCliObj::FreeVectorBuffers()
        {
            if (DistanceRecords.Begin != nullptr)
            {
                FCliDistanceRecord* It = DistanceRecords.Begin;
                FCliDistanceRecord* End = DistanceRecords.End;
                while (It != End)
                {
                    // FUN_00406b80 (00406bd0..00406be5): FUN_004de730 on the Live vector (+0x14)
                    // then the EffectId vector (+0x04) -- buffers freed only; the live EffectArch
                    // references are NOT released here (that is Slot13's job, FUN_00407530).
                    delete[] It->EffectIdBegin;
                    It->EffectIdBegin = nullptr;
                    It->EffectIdEnd = nullptr;
                    It->EffectIdCap = nullptr;
                    delete[] It->LiveBegin;
                    It->LiveBegin = nullptr;
                    It->LiveEnd = nullptr;
                    It->LiveCap = nullptr;
                    It += 1;
                }
                delete[] DistanceRecords.Begin;
                DistanceRecords.Begin = nullptr;
                DistanceRecords.End = nullptr;
                DistanceRecords.Capacity = nullptr;
            }

            DestroyVc6Buffer(&ResolvedEntries);
            DestroyVc6Buffer(&MaterialPaths);
        }

        // FUN_00407000 — free Live (+0x18) then EffectId (+0x08) buffers of one distance record
        // (no null test on the record in retail).
        void DestroyDistanceRecordBuffers(FCliDistanceRecord* Record)
        {
            delete[] Record->LiveBegin;
            Record->LiveBegin = nullptr;
            Record->LiveEnd = nullptr;
            Record->LiveCap = nullptr;
            delete[] Record->EffectIdBegin;
            Record->EffectIdBegin = nullptr;
            Record->EffectIdEnd = nullptr;
            Record->EffectIdCap = nullptr;
        }

        // VC6 vector<POD>::operator= (FUN_005595c0 for pointers, the +0x04 block of FUN_0040e0a0
        // for unsigned, the +0x20 block of FUN_004086c0 for 8-byte pairs): only a self test; a
        // count within size or capacity reuses the buffer, otherwise it is freed and reallocated
        // to exactly the count. End state matches every branch.
        template <typename T>
        void AssignVc6Vector(FCliVc6Vector<T>* Dst, FCliVc6Vector<T> const* Src)
        {
            if (Dst == Src)
                return;

            unsigned SrcCount = 0;
            if (Src->Begin != nullptr && Src->End != nullptr)
                SrcCount = static_cast<unsigned>(Src->End - Src->Begin);

            unsigned DstCap = 0;
            if (Dst->Begin != nullptr && Dst->Capacity != nullptr)
                DstCap = static_cast<unsigned>(Dst->Capacity - Dst->Begin);

            if (DstCap < SrcCount)
            {
                delete[] Dst->Begin;
                T* NewBuf = nullptr;
                if (SrcCount > 0)
                    NewBuf = new T[SrcCount];
                Dst->Begin = NewBuf;
                Dst->End = NewBuf;
                Dst->Capacity = NewBuf != nullptr ? NewBuf + SrcCount : nullptr;
            }

            unsigned Index = 0;
            while (Index < SrcCount)
            {
                Dst->Begin[Index] = Src->Begin[Index];
                Index += 1;
            }
            if (Dst->Begin != nullptr)
                Dst->End = Dst->Begin + SrcCount;
            else
                Dst->End = nullptr;
        }

        // A record's +0x04 and +0x14 blocks are VC6 vector headers (cookie, begin, end, cap).
        FCliVc6Vector<unsigned>* RecordEffectIds(FCliDistanceRecord* Record)
        {
            return reinterpret_cast<FCliVc6Vector<unsigned>*>(&Record->EffectIdCookie);
        }

        FCliVc6Vector<void*>* RecordLive(FCliDistanceRecord* Record)
        {
            return reinterpret_cast<FCliVc6Vector<void*>*>(&Record->LiveCookie);
        }

        // FUN_0040dfa0 -- copy-construct a distance record in raw storage: threshold, both
        // cookies, and each vector with a buffer of exactly its size (operator_new(n * 4) even
        // for n == 0), end = cap = begin + n.
        void CopyConstructDistanceRecord(FCliDistanceRecord* Dst, FCliDistanceRecord const* Src)
        {
            Dst->DistanceThreshold = Src->DistanceThreshold;
            Dst->EffectIdCookie = Src->EffectIdCookie;
            unsigned IdCount = 0;
            if (Src->EffectIdBegin != nullptr)
                IdCount = static_cast<unsigned>(Src->EffectIdEnd - Src->EffectIdBegin);
            unsigned* Ids = new unsigned[IdCount];
            unsigned J = 0;
            while (J < IdCount)
            {
                Ids[J] = Src->EffectIdBegin[J];
                J += 1;
            }
            Dst->EffectIdBegin = Ids;
            Dst->EffectIdEnd = Ids + IdCount;
            Dst->EffectIdCap = Ids + IdCount;

            Dst->LiveCookie = Src->LiveCookie;
            unsigned LiveCount = 0;
            if (Src->LiveBegin != nullptr)
                LiveCount = static_cast<unsigned>(Src->LiveEnd - Src->LiveBegin);
            void** Live = new void*[LiveCount];
            J = 0;
            while (J < LiveCount)
            {
                Live[J] = Src->LiveBegin[J];
                J += 1;
            }
            Dst->LiveBegin = Live;
            Dst->LiveEnd = Live + LiveCount;
            Dst->LiveCap = Live + LiveCount;
        }

        // FUN_0040e0a0 -- distance record operator=: threshold; the id vector (skipped only on
        // self-assignment, 0040e0bd); then the live vector through FUN_005595c0. Cookies are not
        // copied.
        void AssignDistanceRecord(FCliDistanceRecord* Dst, FCliDistanceRecord const* Src)
        {
            FCliDistanceRecord* MutableSrc = const_cast<FCliDistanceRecord*>(Src);
            Dst->DistanceThreshold = Src->DistanceThreshold;
            if (Dst != Src)
                AssignVc6Vector(RecordEffectIds(Dst), RecordEffectIds(MutableSrc));
            AssignVc6Vector(RecordLive(Dst), RecordLive(MutableSrc));
        }

        // FUN_0040d4b0 -- vector<distance record>::operator=. Self test only. Source count
        //  <= size:     assign over the first n (FUN_0040e0a0), destroy the tail (FUN_0040e200(0)
        //               = FUN_00407000), end = begin + n;
        //  <= capacity: assign over the existing size, copy-construct the rest (FUN_0040dfa0),
        //               end = begin + n;
        //  else:        destroy all (FUN_00407000), free, allocate exactly n, copy-construct each,
        //               end = cap = begin + n.
        void AssignDistanceRecords(FCliVc6Vector<FCliDistanceRecord>* Dst,
                                   FCliVc6Vector<FCliDistanceRecord> const* Src)
        {
            if (Dst == Src)
                return;

            unsigned SrcCount = 0;
            if (Src->Begin != nullptr)
                SrcCount = static_cast<unsigned>(Src->End - Src->Begin);
            unsigned DstSize = 0;
            if (Dst->Begin != nullptr)
                DstSize = static_cast<unsigned>(Dst->End - Dst->Begin);

            if (SrcCount <= DstSize)
            {
                unsigned Index = 0;
                while (Index < SrcCount)
                {
                    AssignDistanceRecord(&Dst->Begin[Index], &Src->Begin[Index]);
                    Index += 1;
                }
                FCliDistanceRecord* It = Dst->Begin + SrcCount;
                while (It != Dst->End)
                {
                    DestroyDistanceRecordBuffers(It);
                    It += 1;
                }
                Dst->End = Dst->Begin + SrcCount;
                return;
            }

            unsigned DstCap = 0;
            if (Dst->Begin != nullptr)
                DstCap = static_cast<unsigned>(Dst->Capacity - Dst->Begin);

            if (SrcCount <= DstCap)
            {
                unsigned Index = 0;
                while (Index < DstSize)
                {
                    AssignDistanceRecord(&Dst->Begin[Index], &Src->Begin[Index]);
                    Index += 1;
                }
                while (Index < SrcCount)
                {
                    CopyConstructDistanceRecord(&Dst->Begin[Index], &Src->Begin[Index]);
                    Index += 1;
                }
                Dst->End = Dst->Begin + SrcCount;
                return;
            }

            FCliDistanceRecord* It = Dst->Begin;
            while (It != Dst->End)
            {
                DestroyDistanceRecordBuffers(It);
                It += 1;
            }
            delete[] Dst->Begin;
            FCliDistanceRecord* Buf = new FCliDistanceRecord[SrcCount];
            unsigned Index = 0;
            while (Index < SrcCount)
            {
                CopyConstructDistanceRecord(&Buf[Index], &Src->Begin[Index]);
                Index += 1;
            }
            Dst->Begin = Buf;
            Dst->End = Buf + SrcCount;
            Dst->Capacity = Buf + SrcCount;
        }

    } // namespace (reopened below): FUN_0040d830 has a caller outside this file (FUN_00446160)

        // FUN_0040d830 insert-at-end for MaterialPaths (stride 8). The buffer comes from
        // new[]; a caller that owns the list frees Begin with delete[].
        void PushMaterialPathPair(FCliVc6Vector<FCliMaterialPathPair>* Vec, char const* Path,
                                  unsigned Arg)
        {
            if (Vec == nullptr)
                return;

            unsigned Count = 0;
            if (Vec->Begin != nullptr && Vec->End != nullptr)
                Count = static_cast<unsigned>(Vec->End - Vec->Begin);
            unsigned Cap = 0;
            if (Vec->Begin != nullptr && Vec->Capacity != nullptr)
                Cap = static_cast<unsigned>(Vec->Capacity - Vec->Begin);

            if (Count + 1 > Cap)
            {
                unsigned Grow = 1;
                if (Vec->Begin != nullptr && Count >= 1)
                    Grow = Count;
                unsigned NewCap = Count + Grow;
                if (NewCap < Count + 1)
                    NewCap = Count + 1;
                FCliMaterialPathPair* NewBuf = new FCliMaterialPathPair[NewCap];
                unsigned Index = 0;
                while (Index < Count)
                {
                    NewBuf[Index] = Vec->Begin[Index];
                    Index += 1;
                }
                delete[] Vec->Begin;
                Vec->Begin = NewBuf;
                Vec->End = NewBuf + Count;
                Vec->Capacity = NewBuf + NewCap;
            }

            Vec->End->Path = Path;
            Vec->End->Arg = Arg;
            Vec->End += 1;
        }

    namespace
    {
        // FUN_0040daa0 insert-at-end for one empty DistanceRecord, returns pointer to it.
        FCliDistanceRecord* PushEmptyDistanceRecord(FCliVc6Vector<FCliDistanceRecord>* Vec)
        {
            if (Vec == nullptr)
                return nullptr;

            unsigned Count = 0;
            if (Vec->Begin != nullptr && Vec->End != nullptr)
                Count = static_cast<unsigned>(Vec->End - Vec->Begin);
            unsigned Cap = 0;
            if (Vec->Begin != nullptr && Vec->Capacity != nullptr)
                Cap = static_cast<unsigned>(Vec->Capacity - Vec->Begin);

            if (Count + 1 > Cap)
            {
                unsigned Grow = 1;
                if (Vec->Begin != nullptr && Count >= 1)
                    Grow = Count;
                unsigned NewCap = Count + Grow;
                if (NewCap < Count + 1)
                    NewCap = Count + 1;
                FCliDistanceRecord* NewBuf = new FCliDistanceRecord[NewCap];
                memset(NewBuf, 0, sizeof(FCliDistanceRecord) * NewCap);
                unsigned Index = 0;
                while (Index < Count)
                {
                    NewBuf[Index] = Vec->Begin[Index];
                    Index += 1;
                }
                delete[] Vec->Begin;
                Vec->Begin = NewBuf;
                Vec->End = NewBuf + Count;
                Vec->Capacity = NewBuf + NewCap;
            }

            memset(Vec->End, 0, sizeof(FCliDistanceRecord));
            FCliDistanceRecord* Added = Vec->End;
            Vec->End += 1;
            return Added;
        }

        void PushEffectIdOnRecord(FCliDistanceRecord* Record, unsigned EffectId)
        {
            if (Record == nullptr)
                return;

            unsigned Count = 0;
            if (Record->EffectIdBegin != nullptr && Record->EffectIdEnd != nullptr)
                Count = static_cast<unsigned>(Record->EffectIdEnd - Record->EffectIdBegin);
            unsigned Cap = 0;
            if (Record->EffectIdBegin != nullptr && Record->EffectIdCap != nullptr)
                Cap = static_cast<unsigned>(Record->EffectIdCap - Record->EffectIdBegin);

            if (Count + 1 > Cap)
            {
                unsigned NewCap = (Count == 0) ? 1u : (Count * 2u);
                unsigned* NewBuf = new unsigned[NewCap];
                unsigned Index = 0;
                while (Index < Count)
                {
                    NewBuf[Index] = Record->EffectIdBegin[Index];
                    Index += 1;
                }
                delete[] Record->EffectIdBegin;
                Record->EffectIdBegin = NewBuf;
                Record->EffectIdEnd = NewBuf + Count;
                Record->EffectIdCap = NewBuf + NewCap;
            }

            *Record->EffectIdEnd = EffectId;
            Record->EffectIdEnd += 1;
        }

        FCliObj::~FCliObj()
        {
            FreeLodList(LodHead);
            LodHead = nullptr;
            FreeVectorBuffers();
        }

        void FCliObj::AddRef()
        {
            RefCount += 1;
        }

        void FCliObj::Release()
        {
            // FUN_00406b60: --ref; at 0 → Slot11 then Slot09(1).
            RefCount -= 1;
            if (RefCount == 0)
            {
                Slot11();
                Slot09(1);
            }
        }

        // FUN_00406dc0 — vtable slot 2 ReadUnknown.
        bool FCliObj::ReadUnknown(INI_Reader* Reader)
        {
            // 00406de2: is_value on the reader with no null test.
            if (Reader->is_value("material_library"))
            {
                int LibId = -1;
                if (!Reader->is_value_empty(2))
                    LibId = Reader->get_value_int(2);
                char const* Path = StringAlloc(Reader->get_value_string(), true);
                PushMaterialPathPair(&MaterialPaths, Path, static_cast<unsigned>(LibId));
                return true;
            }

            if (ReadLodValue(Reader, &LodHead))
                return true;

            if (Reader->is_value("distance_render"))
            {
                DistanceRender = Reader->get_value_float(0);
                return true;
            }

            if (Reader->is_value("envmap_material"))
            {
                EnvmapMaterial.Value = StringAlloc(Reader->get_value_string(), true);
                return true;
            }

            if (Reader->is_value("surface_hit_effects"))
            {
                float Threshold = Reader->get_value_float(0);
                FCliDistanceRecord* Added = PushEmptyDistanceRecord(&DistanceRecords);
                if (Added != nullptr)
                {
                    Added->DistanceThreshold = Threshold;
                    unsigned ValueIndex = 1;
                    while (!Reader->is_value_empty(ValueIndex))
                    {
                        unsigned EffectId = CreateID(Reader->get_value_string(ValueIndex));
                        PushEffectIdOnRecord(Added, EffectId);
                        ValueIndex += 1;
                    }
                }
                return true;
            }

            return false;
        }

        void FCliObj::Lod(INI_Reader* Reader)
        {
            if (Reader == nullptr)
                return;
            ApplyLodSection(&LodHead, Reader);
        }

        // FUN_00407050 — base redefine: deep-copy LOD + DistanceRender, then LodPick.
        // Retail quirk: if BoundRoot is not loaded, LodPick is left untouched.
        void FCliObj::Redefine(ICliObj* Donor)
        {
            // 00407051: the donor's +4 is read with no null test.
            FCliObj* Src = reinterpret_cast<FCliObj*>(Donor);

            CopyLodChain(&LodHead, Src->LodHead);
            if (Src->DistanceRender != DistanceRender)
                DistanceRender = Src->DistanceRender;

            if (LodHead == nullptr)
            {
                LodPick = -1.0f;
                return;
            }

            // FUN_00406c50 / FUN_00407050: early-out keeps stale LodPick when !is_loaded
            // (0040708e: Root::is_loaded on +0x14, no null test).
            if (!BoundRoot->is_loaded())
                return;

            AClassType ClassType = BoundRoot->get_class_type();
            bool bSolarOrShip = (ClassType == AClassShip || ClassType == AClassSolar);
            LodPick = ComputeLodPick(BoundRoot->EngineIndex, LodHead, 0, bSolarOrShip);
        }

        void FCliObj::Bind(Root* Record)
        {
            BoundRoot = Record;
        }

        void FCliObj::FreeResources()
        {
            // LAB_00406d10: JMP Slot11.
            Slot11();
        }

        void FCliObj::AssignRootMembers(FCliObj const* Src)
        {
            // FUN_004086c0 (thiscall, RET 4, returns this).
            LodHead = Src->LodHead;
            DistanceRender = Src->DistanceRender;
            LodPick = Src->LodPick;
            EnvmapMaterial.Value = Src->EnvmapMaterial.Value;
            BoundRoot = Src->BoundRoot;
            RefCount = Src->RefCount;
            PendingLoad = Src->PendingLoad;

            AssignVc6Vector(&MaterialPaths, &Src->MaterialPaths);
            AssignVc6Vector(&ResolvedEntries, &Src->ResolvedEntries);   // FUN_005595c0
            AssignDistanceRecords(&DistanceRecords, &Src->DistanceRecords); // FUN_0040d4b0

            // Bit-merge of flags 1/2/4/8/0x10 is overwritten by a full dword store (retail).
            unsigned DonorFlagsDword = 0;
            memcpy(&DonorFlagsDword, &Src->StateFlags, sizeof(DonorFlagsDword));
            memcpy(&StateFlags, &DonorFlagsDword, sizeof(DonorFlagsDword));
        }

        // LAB_00406970 -- Inherit (slot 7) for CliRoot / CliEQShield.
        void FCliObj::Inherit(ICliObj* Donor)
        {
            // 0040697a: the donor is used with no null test.
            FCliObj* Src = reinterpret_cast<FCliObj*>(Donor);
            int SavedRef = RefCount;

            // Retail first writes LodHead shallow, then FUN_00402500 deep-copies at the end.
            AssignRootMembers(Src);

            RefCount = SavedRef;

            CopyLodChain(&LodHead, Src->LodHead);
            DistanceRender = Src->DistanceRender;
            LodPick = Src->LodPick;
        }

        // FUN_00406c50 — recompute cached LodPick; stale value kept when archetype not loaded.
        void FCliObj::RecomputeLodPick(float const* BlendSource)
        {
            if (LodHead == nullptr)
            {
                LodPick = -1.0f;
                return;
            }

            // 00406c67: Root::is_loaded on +0x14 with no null test (retail has none).
            if (!BoundRoot->is_loaded())
                return;

            AClassType ClassType = BoundRoot->get_class_type();
            bool bSolarOrShip = (ClassType == AClassShip || ClassType == AClassSolar);

            if (LodHead == nullptr)
            {
                LodPick = -1.0f;
                return;
            }

            int BlendArg = 0;
            if (BlendSource != nullptr)
                BlendArg = static_cast<int>(reinterpret_cast<ptrdiff_t>(BlendSource));
            LodPick = ComputeLodPick(BoundRoot->EngineIndex, LodHead, BlendArg, bSolarOrShip);
        }

        // FUN_00406cd0 — explicit-parameter overload (no Root::is_loaded gate).
        void FCliObj::RecomputeLodPickExplicit(long EngineIndex, float const* BlendSource,
                                               bool bSolarOrShip)
        {
            if (LodHead == nullptr)
            {
                LodPick = -1.0f;
                return;
            }

            int BlendArg = 0;
            if (BlendSource != nullptr)
                BlendArg = static_cast<int>(reinterpret_cast<ptrdiff_t>(BlendSource));
            LodPick = ComputeLodPick(EngineIndex, LodHead, BlendArg, bSolarOrShip);
        }

        void FCliObj::PushResolvedEntry(FCliMaterialEntry* Entry)
        {
            // FUN_00407100 append into vector B (+0x30): VC6 growth when free slots < 1.
            // grow = 1 if empty or size < 2, else grow = size; new_cap = size + grow.
            unsigned Count = 0;
            if (ResolvedEntries.Begin != nullptr && ResolvedEntries.End != nullptr)
                Count = static_cast<unsigned>(ResolvedEntries.End - ResolvedEntries.Begin);

            unsigned Cap = 0;
            if (ResolvedEntries.Begin != nullptr && ResolvedEntries.Capacity != nullptr)
                Cap = static_cast<unsigned>(ResolvedEntries.Capacity - ResolvedEntries.Begin);

            if (Count + 1 > Cap)
            {
                unsigned Grow = 1;
                if (ResolvedEntries.Begin != nullptr && Count >= 2)
                    Grow = Count;
                unsigned NewCap = Count + Grow;
                if (NewCap < Count + 1)
                    NewCap = Count + 1;

                FCliMaterialEntry** NewBuf = new FCliMaterialEntry*[NewCap];
                unsigned Index = 0;
                while (Index < Count)
                {
                    NewBuf[Index] = ResolvedEntries.Begin[Index];
                    Index += 1;
                }
                // Retail OnReceive(ICF) on old [begin,end) is a no-op for POD pointers.
                delete[] ResolvedEntries.Begin;
                ResolvedEntries.Begin = NewBuf;
                ResolvedEntries.End = NewBuf + Count;
                ResolvedEntries.Capacity = NewBuf + NewCap;
            }

            *ResolvedEntries.End = Entry;
            ResolvedEntries.End += 1;
        }

        bool FCliObj::BuildMaterialCache(bool bLatchSlot10)
        {
            // FUN_00407100(this, param_1). param_1==0 → latch bit1 + force-apply on resolve.
            if ((StateFlags & CliObjFlagCacheBuilt) != 0)
                return true;

            StateFlags = static_cast<unsigned char>(StateFlags | CliObjFlagCacheBuilt);

            bool bForceApply = bLatchSlot10;

            FCliMaterialPathPair* Pair = MaterialPaths.Begin;
            FCliMaterialPathPair* PairEnd = MaterialPaths.End;
            if (Pair != PairEnd)
            {
                while (Pair != PairEnd)
                {
                    FCliMaterialEntry* Entry =
                        ResolveMaterialEntry(Pair->Path, bForceApply, Pair->Arg);
                    if (Entry != nullptr)
                        PushResolvedEntry(Entry);
                    Pair += 1;
                }
            }

            // 00407169: +0x14 read with no null test.
            char const* ArchPath = BoundRoot->DaArchetype.Value;
            FCliMaterialEntry* RootEntry =
                ResolveMaterialEntry(ArchPath, bForceApply, 0xFFFFFFFFu);
            if (RootEntry != nullptr)
                PushResolvedEntry(RootEntry);

            if (bLatchSlot10)
                StateFlags = static_cast<unsigned char>(StateFlags | CliObjFlagSlot10Done);

            return true;
        }

        void FCliObj::Slot10()
        {
            // FUN_00407320
            if ((StateFlags & CliObjFlagSlot10Done) == 0)
            {
                StateFlags = static_cast<unsigned char>(StateFlags | CliObjFlagSlot10Done);
                FCliMaterialEntry** It = ResolvedEntries.Begin;
                FCliMaterialEntry** End = ResolvedEntries.End;
                while (It != End)
                {
                    // FUN_0041afd0 — force-realize, not release.
                    ForceRealizeMaterialEntry(*It);
                    It += 1;
                }
            }

            if ((StateFlags & CliObjFlagEngineSide) == 0)
            {
                StateFlags = static_cast<unsigned char>(StateFlags | CliObjFlagEngineSide);
                // 0040735c: Root::is_loaded on +0x14, no null test.
                if (BoundRoot->is_loaded())
                    UnloadEngineComponents(BoundRoot->EngineIndex);
            }
        }

        void FCliObj::Slot11()
        {
            // FUN_00406d20
            if ((StateFlags & CliObjFlagCacheBuilt) != 0)
            {
                // Retail PendingLoad branch is unreachable: all producers preserve null
                // (constructor/copy/consumer proof in tracker FUN_00406d20).
                FCliMaterialEntry** It = ResolvedEntries.Begin;
                while (It != ResolvedEntries.End)
                {
                    ReleaseMaterialEntrySlot(It);
                    It += 1;
                }
                ResolvedEntries.End = ResolvedEntries.Begin;
                StateFlags = static_cast<unsigned char>(StateFlags & 0xFCu);
            }

            Slot13();
            StateFlags = static_cast<unsigned char>(StateFlags & 0xEBu);
        }

        void FCliObj::Slot12(unsigned Arg)
        {
            // FUN_004074f0
            StateFlags = static_cast<unsigned char>(StateFlags | CliObjFlagSlot12Done);
            FCliDistanceRecord* It = DistanceRecords.Begin;
            FCliDistanceRecord* End = DistanceRecords.End;
            while (It != End)
            {
                ApplyDistanceRecord(It, Arg);
                It += 1;
            }
        }

        void FCliObj::Slot13()
        {
            // LAB_00407530
            if ((StateFlags & CliObjFlagSlot12Done) == 0)
                return;

            FCliDistanceRecord* It = DistanceRecords.Begin;
            FCliDistanceRecord* End = DistanceRecords.End;
            while (It != End)
            {
                TeardownDistanceRecord(It);
                It += 1;
            }
            StateFlags = static_cast<unsigned char>(StateFlags & 0xF7u);
        }

        void FCliObj::DestroyMostDerived()
        {
            this->~FCliObj();
        }

        void* FCliObj::Slot09(unsigned Flags)
        {
            // FUN_00406940: dtor body then optional operator delete.
            DestroyMostDerived();
            if ((Flags & 1u) != 0)
                ::operator delete(this);
            return this;
        }

        // The body shared by FUN_00407450 (CliRoot, the +0x40 list) and FUN_00409f10
        // (CliEQShieldGenerator, the +0x60 list): null begin or empty list -> 0; the first record
        // whose threshold is above Distance (FCOMP / TEST AH,5 / JNP: strictly less, ordered)
        // selects the record before it, or 0 when it is the first; no such record -> the last.
        void* PickRecordByDistance(FCliVc6Vector<FCliDistanceRecord> const* Records, float Distance)
        {
            if (Records->Begin == nullptr)
                return nullptr;

            int Count = static_cast<int>(Records->End - Records->Begin);
            if (Count == 0)
                return nullptr;

            int Index = 0;
            FCliDistanceRecord* It = Records->Begin;
            while (Index < Count)
            {
                if (Distance < It->DistanceThreshold)
                {
                    if (Index == 0)
                        return nullptr;
                    return ActivateDistanceRecord(It - 1);
                }
                Index += 1;
                It += 1;
            }

            return ActivateDistanceRecord(Records->End - 1);
        }

        void* FCliObj::PickDistanceRecord(float Distance)
        {
            // FUN_00407450
            return PickRecordByDistance(&DistanceRecords, Distance);
        }

        void* FCliShieldGenerator::PickShieldHitEffect(float Distance)
        {
            // FUN_00409f10
            return PickRecordByDistance(&ShieldHitEffects, Distance);
        }

        unsigned long FCliObj::Load(int LoadMode)
        {
            // LAB_00407380 — LoadMode unread in base (Munition override uses it).
            (void)LoadMode;

            if ((StateFlags & CliObjFlagCacheBuilt) == 0)
            {
                if (BuildMaterialCache(true))
                    Slot10();
            }

            if ((StateFlags & CliObjFlagSlot12Done) == 0)
                Slot12(0);

            if ((StateFlags & CliObjFlagLodPickDone) != 0)
                return 1;

            // 004073b5: Root::is_loaded on +0x14, no null test.
            if (!BoundRoot->is_loaded())
            {
                StateFlags = static_cast<unsigned char>(StateFlags | CliObjFlagLodPickDone);
                return 1;
            }

            if (LodHead == nullptr)
            {
                LodPick = -1.0f;
                StateFlags = static_cast<unsigned char>(StateFlags | CliObjFlagLodPickDone);
                return 1;
            }

            if (!BoundRoot->is_loaded())
            {
                StateFlags = static_cast<unsigned char>(StateFlags | CliObjFlagLodPickDone);
                return 1;
            }

            AClassType ClassType = BoundRoot->get_class_type();
            bool bSolarOrShip = false;
            if (ClassType == AClassShip || ClassType == AClassSolar)
                bSolarOrShip = true;

            if (LodHead == nullptr)
            {
                LodPick = -1.0f;
                StateFlags = static_cast<unsigned char>(StateFlags | CliObjFlagLodPickDone);
                return 1;
            }

            LodPick = ComputeLodPick(BoundRoot->EngineIndex, LodHead, 0, bSolarOrShip);
            StateFlags = static_cast<unsigned char>(StateFlags | CliObjFlagLodPickDone);
            return 1;
        }

        // caseD_3 0040ca87..0040cb01: FUN_004068d0, ID_String::clear on +0x58 / +0x5c,
        // CacheString::clear on +0x60, ID_String::clear on +0x64, +0x68 = +0x6c = 0,
        // +0x70 = 0x3d8f5c29 (0.07), +0x74 = 0x3d23d70a (0.04), +0x78 = +0x7c = +0x80 =
        // 0x3e4ccccd (0.2), +0x84 = 0x3f000000 (0.5), vptr 005c77b8. +0x54 is not written.
        FCliShip::FCliShip()
            : Pad54(0)
            , CameraOffsetX(0.0f)
            , CameraOffsetY(0.0f)
        {
            Id58.clear();
            Id5C.clear();
            CockpitIni.clear();
            Id64.clear();
            unsigned const AccelBits = 0x3d8f5c29u;
            unsigned const SlerpBits = 0x3d23d70au;
            unsigned const TurnBits = 0x3e4ccccdu;
            unsigned const LookBits = 0x3f000000u;
            memcpy(&CamAngularAccel, &AccelBits, sizeof(float));
            memcpy(&CamAngularSlerp, &SlerpBits, sizeof(float));
            memcpy(&TurnAngle78, &TurnBits, sizeof(float));
            memcpy(&TurnAngle7C, &TurnBits, sizeof(float));
            memcpy(&TurnAngle80, &TurnBits, sizeof(float));
            memcpy(&CamTurnLookAhead, &LookBits, sizeof(float));
        }

        // LAB_00407560. The three turn angles (004076c2..004077e3) go through
        // ShipCameraClampTurnAngle (ship_camera_params.cpp): below 0.0 -> 0.0, above 90.0 ->
        // 90.0, NaN kept, then times DAT_005c77f4.
        bool FCliShip::ReadUnknown(INI_Reader* Reader)
        {
            // 00407570: FUN_00406dc0 first.
            if (FCliObj::ReadUnknown(Reader))
                return true;

            if (Reader->is_value("bay_doors_open_snd"))
            {
                Id58.Id = CreateID(Reader->get_value_string());
                return true;
            }

            if (Reader->is_value("bay_doors_close_snd"))
            {
                Id5C.Id = CreateID(Reader->get_value_string());
                return true;
            }

            // 004075e8..00407606: StringAlloc(value, 1) -> +0x60.
            if (Reader->is_value("cockpit"))
            {
                CockpitIni.Value = StringAlloc(Reader->get_value_string(), true);
                return true;
            }

            if (Reader->is_value("pilot_mesh"))
            {
                Id64.Id = CreateID(Reader->get_value_string());
                return true;
            }

            if (Reader->is_value("camera_offset"))
            {
                CameraOffsetX = Reader->get_value_float(0);
                CameraOffsetY = Reader->get_value_float(1);
                return true;
            }

            if (Reader->is_value("camera_angular_slerp_multiplier"))
            {
                CamAngularSlerp = Reader->get_value_float(0);
                return true;
            }

            if (Reader->is_value("camera_angular_acceleration"))
            {
                CamAngularAccel = Reader->get_value_float(0);
                return true;
            }

            if (Reader->is_value("camera_horizontal_turn_angle"))
            {
                TurnAngle78 = ShipCameraClampTurnAngle(Reader->get_value_float(0));
                return true;
            }

            if (Reader->is_value("camera_vertical_turn_up_angle"))
            {
                TurnAngle7C = ShipCameraClampTurnAngle(Reader->get_value_float(0));
                return true;
            }

            if (Reader->is_value("camera_vertical_turn_down_angle"))
            {
                TurnAngle80 = ShipCameraClampTurnAngle(Reader->get_value_float(0));
                return true;
            }

            if (Reader->is_value("camera_turn_look_ahead_slerp_amount"))
            {
                CamTurnLookAhead = Reader->get_value_float(0);
                return true;
            }

            return false;
        }

        void FCliShip::DestroyMostDerived()
        {
            this->~FCliShip();
        }

        void FCliShip::Redefine(ICliObj* Donor)
        {
            // FUN_00407830 — base LOD redefine then ship cam/id fields.
            FCliObj::Redefine(Donor);

            FCliShip* Src = AsCliShip(Donor);
            if (Src->Id58.Id != Id58.Id)
                Id58.Id = Src->Id58.Id;
            if (Src->Id5C.Id != Id5C.Id)
                Id5C.Id = Src->Id5C.Id;
            if (Src->CamAngularSlerp != CamAngularSlerp)
                CamAngularSlerp = Src->CamAngularSlerp;
            if (Src->CamAngularAccel != CamAngularAccel)
                CamAngularAccel = Src->CamAngularAccel;
            if (Src->TurnAngle78 != TurnAngle78)
                TurnAngle78 = Src->TurnAngle78;
            if (Src->TurnAngle7C != TurnAngle7C)
                TurnAngle7C = Src->TurnAngle7C;
            if (Src->TurnAngle80 != TurnAngle80)
                TurnAngle80 = Src->TurnAngle80;
            if (Src->CamTurnLookAhead != CamTurnLookAhead)
                CamTurnLookAhead = Src->CamTurnLookAhead;
            if (Src->CameraOffsetX != CameraOffsetX || Src->CameraOffsetY != CameraOffsetY)
            {
                CameraOffsetX = Src->CameraOffsetX;
                CameraOffsetY = Src->CameraOffsetY;
            }
        }

        // FUN_00407990: __RTDynamicCast to CliShip (no null test), RefCount saved, CliRoot's
        // operator= inlined (no FUN_00402500 -- the LOD list stays shared with the donor), the
        // dwords +0x54..+0x84 copied (00407b67..00407bc0), RefCount restored.
        void FCliShip::Inherit(ICliObj* Donor)
        {
            FCliShip* DonorShip = AsCliShip(Donor);
            int SavedRef = RefCount;

            AssignRootMembers(DonorShip);

            Pad54 = DonorShip->Pad54;
            Id58.Id = DonorShip->Id58.Id;
            Id5C.Id = DonorShip->Id5C.Id;
            CockpitIni.Value = DonorShip->CockpitIni.Value;
            Id64.Id = DonorShip->Id64.Id;
            CameraOffsetX = DonorShip->CameraOffsetX;
            CameraOffsetY = DonorShip->CameraOffsetY;
            CamAngularAccel = DonorShip->CamAngularAccel;
            CamAngularSlerp = DonorShip->CamAngularSlerp;
            TurnAngle78 = DonorShip->TurnAngle78;
            TurnAngle7C = DonorShip->TurnAngle7C;
            TurnAngle80 = DonorShip->TurnAngle80;
            CamTurnLookAhead = DonorShip->CamTurnLookAhead;

            RefCount = SavedRef;
        }

        FCliSolar::FCliSolar()
            : TextureLen(0)
            , Unk80(0)
            , Unk84(0)
        {
            Id54.clear();
            Id58.clear();
            memset(DefaultTexture, 0, sizeof(DefaultTexture));
            memset(Pad85, 0, sizeof(Pad85));

            char const* Src = "DEFAULT_TEXTURE";
            unsigned Len = 0;
            while (Src[Len] != '\0')
                Len += 1;
            if (Len > 0x1F)
                Len = 0x1F;
            memcpy(DefaultTexture, Src, Len);
            DefaultTexture[Len] = '\0';
            TextureLen = Len;
        }

        void FCliSolar::DestroyMostDerived()
        {
            this->~FCliSolar();
        }

        // retail FUN_00407bd0.
        bool FCliSolar::ReadUnknown(INI_Reader* Reader)
        {
            if (FCliObj::ReadUnknown(Reader))
                return true;

            if (Reader->is_value("shape_name"))
            {
                char const* Text = Reader->get_value_string();
                if (Text == nullptr)
                {
                    TextureLen = 0;
                    DefaultTexture[0] = '\0';
                    return true;
                }

                unsigned Len = 0;
                while (Text[Len] != '\0' && Len < 0x1F)
                    Len += 1;
                memcpy(DefaultTexture, Text, Len);
                DefaultTexture[Len] = '\0';
                TextureLen = Len;
                return true;
            }

            if (Reader->is_value("infocard_ids") || Reader->is_value("ids_info"))
            {
                Unk80 = static_cast<unsigned>(Reader->get_value_int(0));
                return true;
            }

            if (Reader->is_value("open_sound"))
            {
                Id54.Id = CreateID(Reader->get_value_string());
                return true;
            }

            if (Reader->is_value("close_sound"))
            {
                Id58.Id = CreateID(Reader->get_value_string());
                return true;
            }

            if (Reader->is_value("animated_textures"))
            {
                Unk84 = Reader->get_value_bool(0);
                return true;
            }

            return false;
        }

        // LAB_00407d20: __RTDynamicCast to CliSolar (no null test), RefCount saved, CliRoot's
        // operator= inlined (00407d42..00407dd8; the +0x20 vector through FUN_0040d2c0, the
        // out-of-line copy of the same assign) -- no FUN_00402500, so the LOD list stays shared
        // with the donor -- then the solar fields, RefCount restored at 00407e4e.
        void FCliSolar::Inherit(ICliObj* Donor)
        {
            FCliSolar* DonorSolar = AsCliSolar(Donor);
            int SavedRef = RefCount;

            AssignRootMembers(DonorSolar);

            Id54 = DonorSolar->Id54;
            Id58 = DonorSolar->Id58;

            unsigned Len = 0;
            while (Len < 0x1F && DonorSolar->DefaultTexture[Len] != '\0')
                Len += 1;
            memcpy(DefaultTexture, DonorSolar->DefaultTexture, Len);
            DefaultTexture[Len] = '\0';
            TextureLen = Len;

            Unk80 = DonorSolar->Unk80;
            Unk84 = DonorSolar->Unk84;

            RefCount = SavedRef;
        }

        void AdoptCompanion(ICliObj** Out, FCliObj* Obj)
        {
            *Out = reinterpret_cast<ICliObj*>(Obj);
            Obj->AddRef();
        }

        // exe FUN_00537260 (vector<unsigned>::insert(end(), 1, value), i.e. push_back).
        void PushU32ToVc6Vector(FCliVc6Vector<unsigned>* Vec, unsigned Value)
        {
            if (Vec->End == Vec->Capacity)
            {
                unsigned OldCount = 0;
                if (Vec->Begin != nullptr && Vec->End != nullptr)
                    OldCount = static_cast<unsigned>(Vec->End - Vec->Begin);
                unsigned NewCap = (OldCount == 0) ? 1u : (OldCount * 2u);
                unsigned* NewBuf = new unsigned[NewCap];
                unsigned Index = 0;
                while (Index < OldCount)
                {
                    NewBuf[Index] = Vec->Begin[Index];
                    Index += 1;
                }
                delete[] Vec->Begin;
                Vec->Begin = NewBuf;
                Vec->End = NewBuf + OldCount;
                Vec->Capacity = NewBuf + NewCap;
            }

            *Vec->End = Value;
            Vec->End += 1;
        }

        template <typename T>
        T* NewCompanion(ICliObj** Out)
        {
            T* Obj = new T();
            AdoptCompanion(Out, Obj);
            return Obj;
        }

        // 0040cc4b..0040cc85 (types 0x20 / 0x21): FUN_004068d0, +0x54 = dword 0, ID_String::clear
        // on +0x58 and +0x5c, vptr 005c79a8.
        FCliProj::FCliProj()
        {
            ConstEffectDelay = 0.0f;
            ConstEffect.clear();
            OneShotSound.clear();
        }

        // LAB_00407e60.
        bool FCliProj::ReadUnknown(INI_Reader* Reader)
        {
            // 00407e6c: FUN_00406dc0 first.
            if (FCliObj::ReadUnknown(Reader))
                return true;

            if (Reader->is_value("const_effect_delay"))
            {
                ConstEffectDelay = Reader->get_value_float(0);
                return true;
            }

            if (Reader->is_value("const_effect"))
            {
                ConstEffect.Id = CreateID(Reader->get_value_string());
                return true;
            }

            if (Reader->is_value("one_shot_sound"))
            {
                OneShotSound.Id = CreateID(Reader->get_value_string());
                return true;
            }

            return false;
        }

        // FUN_00407f00. 00407f00..00407f91 is FUN_00407050's body inlined (LOD copy, distance_render
        // copied when not equal, LodPick recomputed), then +0x54 and +0x58 are copied when they
        // differ (00407f94..00407fb2; TEST AH,0x44 / JNP skips only on equal, so NaN copies).
        // +0x5c is not touched.
        void FCliProj::Redefine(ICliObj* Donor)
        {
            FCliObj::Redefine(Donor);

            FCliProj* Src = reinterpret_cast<FCliProj*>(Donor);
            if (Src->ConstEffectDelay != ConstEffectDelay)
                ConstEffectDelay = Src->ConstEffectDelay;
            if (Src->ConstEffect.Id != ConstEffect.Id)
                ConstEffect.Id = Src->ConstEffect.Id;
        }

        // FUN_00407fc0. __RTDynamicCast(Donor, ICliObj -> CliProj) with no null test, RefCount
        // saved, CliRoot::operator= inlined (00407fe2..00408195), +0x54 / +0x58 / +0x5c copied,
        // RefCount restored. No FUN_00402500: the LOD list stays shared with the donor.
        void FCliProj::Inherit(ICliObj* Donor)
        {
            FCliProj* Src = static_cast<FCliProj*>(static_cast<FCliObj*>(Donor));
            int SavedRef = RefCount;

            AssignRootMembers(Src);
            ConstEffectDelay = Src->ConstEffectDelay;
            ConstEffect.Id = Src->ConstEffect.Id;
            OneShotSound.Id = Src->OneShotSound.Id;

            RefCount = SavedRef;
        }

        // 0040cfe1..0040d01a: FUN_004068d0, ID_String::clear on +0x54, +0x58 = 0, vptr 005c7fe0.
        FCliEQAttachedFX::FCliEQAttachedFX()
        {
            Particles.clear();
            bUseThrottle = false;
            memset(Pad59, 0, sizeof(Pad59));
        }

        // LAB_0040b6d0.
        bool FCliEQAttachedFX::ReadUnknown(INI_Reader* Reader)
        {
            if (FCliObj::ReadUnknown(Reader))
                return true;

            if (Reader->is_value("particles"))
            {
                Particles.Id = CreateID(Reader->get_value_string());
                return true;
            }

            if (Reader->is_value("use_throttle"))
            {
                bUseThrottle = Reader->get_value_bool(0);
                return true;
            }

            return false;
        }

        // FUN_0040b740: FUN_00407050's body inlined (0040b740..0040b7d1), then +0x54 (dword) and
        // +0x58 (byte) copied when they differ.
        void FCliEQAttachedFX::Redefine(ICliObj* Donor)
        {
            FCliObj::Redefine(Donor);

            FCliEQAttachedFX* Src = reinterpret_cast<FCliEQAttachedFX*>(Donor);
            if (Src->Particles.Id != Particles.Id)
                Particles.Id = Src->Particles.Id;
            if (Src->bUseThrottle != bUseThrottle)
                bUseThrottle = Src->bUseThrottle;
        }

        // FUN_0040b7f0: __RTDynamicCast to CliEQAttachedFX (no null test), RefCount saved,
        // CliRoot::operator= inlined, +0x54 dword and +0x58 byte copied, RefCount restored.
        void FCliEQAttachedFX::Inherit(ICliObj* Donor)
        {
            FCliEQAttachedFX* Src = reinterpret_cast<FCliEQAttachedFX*>(Donor);
            int SavedRef = RefCount;

            AssignRootMembers(Src);
            Particles.Id = Src->Particles.Id;
            bUseThrottle = Src->bUseThrottle;

            RefCount = SavedRef;
        }

        // FUN_00408ae0: __RTDynamicCast to CliAsteroid (no null test), RefCount saved,
        // CliRoot::operator= inlined (00408afa..00408cba), RefCount restored. Nothing else.
        void FCliAsteroid::Inherit(ICliObj* Donor)
        {
            FCliAsteroid* Src = reinterpret_cast<FCliAsteroid*>(Donor);
            int SavedRef = RefCount;
            AssignRootMembers(Src);
            RefCount = SavedRef;
        }

        // 0040cd1a..0040cd43: FUN_004068d0, ID_String::clear on +0x54, then +0x54 = CreateID(0)
        // (0040cd31..0040cd3e), +0x58 = 0, vptr 005c7a78.
        FCliDynamicAsteroid::FCliDynamicAsteroid()
        {
            ParticleEffect.clear();
            ParticleEffect.Id = CreateID(nullptr);
            bHasParticleEffect = false;
            memset(Pad59, 0, sizeof(Pad59));
        }

        // FUN_00408cd0.
        bool FCliDynamicAsteroid::ReadUnknown(INI_Reader* Reader)
        {
            if (FCliObj::ReadUnknown(Reader))
                return true;

            // 00408ce5..00408d0b: CreateID -> +0x54, BL (1) -> +0x58.
            if (Reader->is_value("particle_effect"))
            {
                ParticleEffect.Id = CreateID(Reader->get_value_string());
                bHasParticleEffect = true;
                return true;
            }

            return false;
        }

        // FUN_00408d20: __RTDynamicCast to CliDynamicAsteroid (no null test), RefCount saved,
        // CliRoot::operator= inlined, +0x54 dword and +0x58 byte copied (00408ef8..00408f06),
        // RefCount restored.
        void FCliDynamicAsteroid::Inherit(ICliObj* Donor)
        {
            FCliDynamicAsteroid* Src = reinterpret_cast<FCliDynamicAsteroid*>(Donor);
            int SavedRef = RefCount;
            AssignRootMembers(Src);
            ParticleEffect.Id = Src->ParticleEffect.Id;
            bHasParticleEffect = Src->bHasParticleEffect;
            RefCount = SavedRef;
        }

        // 0040cf9e..0040cfcb: FUN_004068d0, ID_String::clear on +0x58 and +0x5c, the three color
        // bytes +0x56/+0x55/+0x54 = 0xff, vptr 005c7f78. +0x57 is left as allocated.
        FCliTractor::FCliTractor()
        {
            OperatingEffect.clear();
            CompleteSound.clear();
            Color[2] = 0xff;
            Color[1] = 0xff;
            Color[0] = 0xff;
        }

        // LAB_0040b320 (inside Ghidra's FUN_0040b0a0 span).
        bool FCliTractor::ReadUnknown(INI_Reader* Reader)
        {
            if (FCliObj::ReadUnknown(Reader))
                return true;

            // 0040b348..0040b379: three get_value_float + ftol, low byte stored.
            if (Reader->is_value("color"))
            {
                Color[0] = static_cast<unsigned char>(static_cast<int>(Reader->get_value_float(0)));
                Color[1] = static_cast<unsigned char>(static_cast<int>(Reader->get_value_float(1)));
                Color[2] = static_cast<unsigned char>(static_cast<int>(Reader->get_value_float(2)));
                return true;
            }

            if (Reader->is_value("operating_effect"))
            {
                OperatingEffect.Id = CreateID(Reader->get_value_string());
                return true;
            }

            if (Reader->is_value("tractor_complete_snd"))
            {
                CompleteSound.Id = CreateID(Reader->get_value_string());
                return true;
            }

            return false;
        }

        // FUN_0040b3f0: FUN_00407050's body inlined (0040b3f0..0040b481), then 0040b484..0040b4a8:
        // when any of the three color bytes differs, all three are copied. +0x58/+0x5c untouched.
        void FCliTractor::Redefine(ICliObj* Donor)
        {
            FCliObj::Redefine(Donor);

            FCliTractor* Src = reinterpret_cast<FCliTractor*>(Donor);
            if (Src->Color[0] != Color[0] || Src->Color[1] != Color[1] || Src->Color[2] != Color[2])
            {
                Color[0] = Src->Color[0];
                Color[1] = Src->Color[1];
                Color[2] = Src->Color[2];
            }
        }

        // FUN_0040b4c0: __RTDynamicCast to CliEQTractor (no null test), RefCount saved,
        // CliRoot::operator= inlined, 0040b697..0040b6c1: three color bytes, +0x58, +0x5c,
        // RefCount restored.
        void FCliTractor::Inherit(ICliObj* Donor)
        {
            FCliTractor* Src = reinterpret_cast<FCliTractor*>(Donor);
            int SavedRef = RefCount;
            AssignRootMembers(Src);
            Color[0] = Src->Color[0];
            Color[1] = Src->Color[1];
            Color[2] = Src->Color[2];
            OperatingEffect.Id = Src->OperatingEffect.Id;
            CompleteSound.Id = Src->CompleteSound.Id;
            RefCount = SavedRef;
        }

        // 0040cf49..0040cf6d: FUN_004068d0, ID_String::clear on +0x54, CacheString::clear on
        // +0x58, vptr 005c7d98.
        FCliEQThruster::FCliEQThruster()
        {
            Particles.clear();
            HpParticles.clear();
        }

        // LAB_0040a060.
        bool FCliEQThruster::ReadUnknown(INI_Reader* Reader)
        {
            if (FCliObj::ReadUnknown(Reader))
                return true;

            if (Reader->is_value("particles"))
            {
                Particles.Id = CreateID(Reader->get_value_string());
                return true;
            }

            if (Reader->is_value("hp_particles"))
            {
                HpParticles.Value = StringAlloc(Reader->get_value_string(), false);
                return true;
            }

            return false;
        }

        // FUN_0040a0e0 (also CliSolar's slot 4): FUN_00407050's body inlined, then the dwords at
        // +0x54 and +0x58 copied when they differ (0040a174..0040a187).
        void CliRedefineTwoDwords(FCliObj* This, ICliObj* Donor)
        {
            unsigned* Dst = reinterpret_cast<unsigned*>(reinterpret_cast<char*>(This) + 0x54);
            unsigned const* Src =
                reinterpret_cast<unsigned const*>(reinterpret_cast<char const*>(Donor) + 0x54);
            if (Src[0] != Dst[0])
                Dst[0] = Src[0];
            if (Src[1] != Dst[1])
                Dst[1] = Src[1];
        }

        // FUN_00409bd0 (slot 4 of CliEQShieldGenerator and CliEQRepairDroid): FUN_00407050's body
        // inlined, then 00409c64..00409c6c: the dword at +0x54 copied when it differs.
        void CliRedefineFirstDword(FCliObj* This, ICliObj* Donor)
        {
            unsigned* Dst = reinterpret_cast<unsigned*>(reinterpret_cast<char*>(This) + 0x54);
            unsigned const* Src =
                reinterpret_cast<unsigned const*>(reinterpret_cast<char const*>(Donor) + 0x54);
            if (Src[0] != Dst[0])
                Dst[0] = Src[0];
        }

        void FCliEQThruster::Redefine(ICliObj* Donor)
        {
            FCliObj::Redefine(Donor);
            CliRedefineTwoDwords(this, Donor);
        }

        // CliSolar's vtable 005c7908 slot 4 is the same FUN_0040a0e0: open_sound (+0x54) and
        // close_sound (+0x58) follow the donor; the texture name and infocard do not.
        void FCliSolar::Redefine(ICliObj* Donor)
        {
            FCliObj::Redefine(Donor);
            CliRedefineTwoDwords(this, Donor);
        }

        // FUN_0040a190: __RTDynamicCast to CliEQThruster (no null test), RefCount saved,
        // CliRoot::operator= inlined, +0x54 and +0x58 copied (0040a368..0040a376), RefCount
        // restored.
        void FCliEQThruster::Inherit(ICliObj* Donor)
        {
            FCliEQThruster* Src = reinterpret_cast<FCliEQThruster*>(Donor);
            int SavedRef = RefCount;
            AssignRootMembers(Src);
            Particles.Id = Src->Particles.Id;
            HpParticles.Value = Src->HpParticles.Value;
            RefCount = SavedRef;
        }

        // FUN_0040a9e0: FUN_004068d0, +0x80 = +0x88 = 0, +0x78 = +0x7c = 1.0f, the three color
        // triplets zeroed (+0x6c, +0x60, +0x54 in that order), +0x8c = +0x90 = -1.0f,
        // vptr 005c7eb8, +0x84 = 0.3f (0x3e99999a), +0x94 = 180.0f (0x43340000).
        FCliEQLight::FCliEQLight()
        {
            AvgDelay = 0.0f;
            Intensity = 0.0f;
            BulbSize = 1.0f;
            GlowSize = 1.0f;
            MinColor[0] = 0.0f;
            MinColor[1] = 0.0f;
            MinColor[2] = 0.0f;
            GlowColor[0] = 0.0f;
            GlowColor[1] = 0.0f;
            GlowColor[2] = 0.0f;
            Color[0] = 0.0f;
            Color[1] = 0.0f;
            Color[2] = 0.0f;
            FlareConeCos[0] = -1.0f;
            FlareConeCos[1] = -1.0f;
            BlinkDuration = 0.3f;
            LightsourceCone = 180.0f;
        }

        // LAB_0040ab30 -- CliEQLight companion keys (FlashLight.cpp consumers).
        bool FCliEQLight::ReadUnknown(INI_Reader* Reader)
        {
            if (FCliObj::ReadUnknown(Reader))
                return true;

            static float const kColorScale = 1.0f / 255.0f; // DAT_005c7f68
            static float const kDegToRad = 0.017453292f;    // DAT_005c77f4

            if (Reader->is_value("color"))
            {
                float R = Reader->get_value_float(0) * kColorScale;
                float G = Reader->get_value_float(1) * kColorScale;
                float B = Reader->get_value_float(2) * kColorScale;
                Color[0] = R;
                Color[1] = G;
                Color[2] = B;
                // 0040abc6..0040ac02: each glow component FCOMP 0.0 / TEST AH,0x44 / JP skips --
                // PF is clear only for "equal", so the copy into glow_color (+0x60) happens only
                // while all three glow components are still exactly 0.0 (an unordered compare
                // skips too). A `color=` line therefore seeds an unset glow.
                if (GlowColor[0] == 0.0f && GlowColor[1] == 0.0f && GlowColor[2] == 0.0f)
                {
                    GlowColor[0] = R;
                    GlowColor[1] = G;
                    GlowColor[2] = B;
                }
                return true;
            }

            if (Reader->is_value("glow_color"))
            {
                float R = Reader->get_value_float(0);
                float G = Reader->get_value_float(1);
                float B = Reader->get_value_float(2);
                GlowColor[0] = R * kColorScale;
                GlowColor[1] = G * kColorScale;
                GlowColor[2] = B * kColorScale;
                return true;
            }

            if (Reader->is_value("min_color"))
            {
                float R = Reader->get_value_float(0);
                float G = Reader->get_value_float(1);
                float B = Reader->get_value_float(2);
                MinColor[0] = R * kColorScale;
                MinColor[1] = G * kColorScale;
                MinColor[2] = B * kColorScale;
                return true;
            }

            if (Reader->is_value("avg_delay"))
            {
                AvgDelay = Reader->get_value_float(0);
                return true;
            }

            if (Reader->is_value("intensity"))
            {
                Intensity = Reader->get_value_float(0);
                return true;
            }

            if (Reader->is_value("bulb_size"))
            {
                BulbSize = Reader->get_value_float(0);
                return true;
            }

            if (Reader->is_value("glow_size"))
            {
                GlowSize = Reader->get_value_float(0);
                return true;
            }

            if (Reader->is_value("blink_duration"))
            {
                BlinkDuration = Reader->get_value_float(0);
                return true;
            }

            if (Reader->is_value("flare_cone"))
            {
                // 0040adea..0040ae1e: value 0 read first, then value 1; cos(v1*deg) -> +0x8c,
                // cos(v0*deg) -> +0x90. x87 multiplies and takes FCOS in extended precision
                // before the float store; double is the closest the port gets.
                float V0 = Reader->get_value_float(0);
                float V1 = Reader->get_value_float(1);
                FlareConeCos[0] = static_cast<float>(
                    cos(static_cast<double>(V1) * static_cast<double>(kDegToRad)));
                FlareConeCos[1] = static_cast<float>(
                    cos(static_cast<double>(V0) * static_cast<double>(kDegToRad)));
                return true;
            }

            if (Reader->is_value("lightsource_cone"))
            {
                LightsourceCone = Reader->get_value_float(0);
                return true;
            }

            return false;
        }

        // FUN_0040ae70: FUN_00407050's body inlined, then 0040af04..0040b089. Each color triplet
        // (+0x54, +0x60, +0x6c) is copied whole when any component differs (the first two
        // compares JP straight to the copy, the third JNP skips it -- NaN counts as different);
        // +0x80, +0x88, +0x78, +0x7c, +0x84 each copied unless equal; the flare-cone pair
        // +0x8c/+0x90 copied together when either differs; +0x94 unless equal.
        void FCliEQLight::Redefine(ICliObj* Donor)
        {
            FCliObj::Redefine(Donor);

            FCliEQLight* Src = reinterpret_cast<FCliEQLight*>(Donor);
            if (!(Src->Color[0] == Color[0]) || !(Src->Color[1] == Color[1]) ||
                !(Src->Color[2] == Color[2]))
            {
                Color[0] = Src->Color[0];
                Color[1] = Src->Color[1];
                Color[2] = Src->Color[2];
            }
            if (!(Src->GlowColor[0] == GlowColor[0]) || !(Src->GlowColor[1] == GlowColor[1]) ||
                !(Src->GlowColor[2] == GlowColor[2]))
            {
                GlowColor[0] = Src->GlowColor[0];
                GlowColor[1] = Src->GlowColor[1];
                GlowColor[2] = Src->GlowColor[2];
            }
            if (!(Src->MinColor[0] == MinColor[0]) || !(Src->MinColor[1] == MinColor[1]) ||
                !(Src->MinColor[2] == MinColor[2]))
            {
                MinColor[0] = Src->MinColor[0];
                MinColor[1] = Src->MinColor[1];
                MinColor[2] = Src->MinColor[2];
            }
            if (!(Src->AvgDelay == AvgDelay))
                AvgDelay = Src->AvgDelay;
            if (!(Src->Intensity == Intensity))
                Intensity = Src->Intensity;
            if (!(Src->BulbSize == BulbSize))
                BulbSize = Src->BulbSize;
            if (!(Src->GlowSize == GlowSize))
                GlowSize = Src->GlowSize;
            if (!(Src->BlinkDuration == BlinkDuration))
                BlinkDuration = Src->BlinkDuration;
            if (!(Src->FlareConeCos[0] == FlareConeCos[0]) ||
                !(Src->FlareConeCos[1] == FlareConeCos[1]))
            {
                FlareConeCos[0] = Src->FlareConeCos[0];
                FlareConeCos[1] = Src->FlareConeCos[1];
            }
            if (!(Src->LightsourceCone == LightsourceCone))
                LightsourceCone = Src->LightsourceCone;
        }

        // FUN_0040b0a0: __RTDynamicCast to CliEQLight (no null test), RefCount saved,
        // CliRoot::operator= inlined, 0040b277..0040b30c: +0x54..+0x94 copied, RefCount restored.
        void FCliEQLight::Inherit(ICliObj* Donor)
        {
            FCliEQLight* Src = reinterpret_cast<FCliEQLight*>(Donor);
            int SavedRef = RefCount;
            AssignRootMembers(Src);
            Color[0] = Src->Color[0];
            Color[1] = Src->Color[1];
            Color[2] = Src->Color[2];
            GlowColor[0] = Src->GlowColor[0];
            GlowColor[1] = Src->GlowColor[1];
            GlowColor[2] = Src->GlowColor[2];
            MinColor[0] = Src->MinColor[0];
            MinColor[1] = Src->MinColor[1];
            MinColor[2] = Src->MinColor[2];
            BulbSize = Src->BulbSize;
            GlowSize = Src->GlowSize;
            AvgDelay = Src->AvgDelay;
            BlinkDuration = Src->BlinkDuration;
            Intensity = Src->Intensity;
            FlareConeCos[0] = Src->FlareConeCos[0];
            FlareConeCos[1] = Src->FlareConeCos[1];
            LightsourceCone = Src->LightsourceCone;
            RefCount = SavedRef;
        }

        // 0040d04b..0040d074: FUN_004068d0, ID_String::clear on +0x54, +0x58 = +0x5c = 360.0f
        // (0x43b40000), +0x60 = 0, vptr 005c802c.
        FCliInternalFx::FCliInternalFx()
        {
            UseSound.clear();
            InsideSoundCone = 360.0f;
            OutsideSoundCone = 360.0f;
            OutsideConeAttenuation = 0.0f;
        }

        // LAB_0040b9f0.
        bool FCliInternalFx::ReadUnknown(INI_Reader* Reader)
        {
            if (FCliObj::ReadUnknown(Reader))
                return true;

            if (Reader->is_value("use_sound"))
            {
                UseSound.Id = CreateID(Reader->get_value_string());
                return true;
            }

            if (Reader->is_value("inside_sound_cone"))
            {
                InsideSoundCone = Reader->get_value_float(0);
                return true;
            }

            if (Reader->is_value("outside_sound_cone"))
            {
                OutsideSoundCone = Reader->get_value_float(0);
                return true;
            }

            if (Reader->is_value("outside_cone_attenuation"))
            {
                OutsideConeAttenuation = Reader->get_value_float(0);
                return true;
            }

            return false;
        }

        // FUN_0040bab0: FUN_00407050's body inlined, then 0040bb44..0040bb85: the +0x54 dword
        // copied when it differs; the floats +0x58/+0x5c/+0x60 copied unless equal (TEST AH,0x44
        // / JNP, so NaN copies).
        void FCliInternalFx::Redefine(ICliObj* Donor)
        {
            FCliObj::Redefine(Donor);

            FCliInternalFx* Src = reinterpret_cast<FCliInternalFx*>(Donor);
            if (Src->UseSound.Id != UseSound.Id)
                UseSound.Id = Src->UseSound.Id;
            if (!(Src->InsideSoundCone == InsideSoundCone))
                InsideSoundCone = Src->InsideSoundCone;
            if (!(Src->OutsideSoundCone == OutsideSoundCone))
                OutsideSoundCone = Src->OutsideSoundCone;
            if (!(Src->OutsideConeAttenuation == OutsideConeAttenuation))
                OutsideConeAttenuation = Src->OutsideConeAttenuation;
        }

        // FUN_0040bb90: __RTDynamicCast to CliEQInternalFX (no null test), RefCount saved,
        // CliRoot::operator= inlined, 0040bd68..0040bd82: +0x54..+0x60 copied, RefCount restored.
        void FCliInternalFx::Inherit(ICliObj* Donor)
        {
            FCliInternalFx* Src = reinterpret_cast<FCliInternalFx*>(Donor);
            int SavedRef = RefCount;
            AssignRootMembers(Src);
            UseSound.Id = Src->UseSound.Id;
            InsideSoundCone = Src->InsideSoundCone;
            OutsideSoundCone = Src->OutsideSoundCone;
            OutsideConeAttenuation = Src->OutsideConeAttenuation;
            RefCount = SavedRef;
        }

        // LAB_004083a0. Calls FUN_00406dc0 (CliRoot's ReadUnknown) directly, not CliProj's
        // 00407e60, and repeats the three CliProj keys itself.
        bool FCliMunition::ReadUnknown(INI_Reader* Reader)
        {
            if (FCliObj::ReadUnknown(Reader))
                return true;

            if (Reader->is_value("const_effect_delay"))
            {
                ConstEffectDelay = Reader->get_value_float(0);
                return true;
            }

            if (Reader->is_value("const_effect"))
            {
                ConstEffect.Id = CreateID(Reader->get_value_string());
                return true;
            }

            if (Reader->is_value("one_shot_sound"))
            {
                OneShotSound.Id = CreateID(Reader->get_value_string());
                return true;
            }

            if (Reader->is_value("munition_hit_effect"))
            {
                PushU32ToVc6Vector(&HitEffectIds, CreateID(Reader->get_value_string()));
                return true;
            }

            if (Reader->is_value("HP_trail_parent"))
            {
                HpTrailParent.Value = StringAlloc(Reader->get_value_string(), false);
                return true;
            }

            return false;
        }

        // 0040cbc7..0040cc37: the CliProj part (FUN_004068d0, +0x54 = 0, +0x58/+0x5c cleared),
        // CacheString::clear on +0x60, an empty vector at +0x64 (its +0x68..+0x70 zeroed) and
        // FUN_005a6900 for the one at +0x74, vptr 005c8420.
        FCliMunition::FCliMunition()
        {
            HpTrailParent.clear();
            memset(&HitEffectIds, 0, sizeof(HitEffectIds));
            memset(&LiveEffects, 0, sizeof(LiveEffects));
        }

        // FUN_0040d1e0: free the +0x74 buffer and the +0x64 buffer (no EffectArch release --
        // that happens in Slot11 before Release reaches the destructor), then the CliRoot part.
        FCliMunition::~FCliMunition()
        {
            delete[] LiveEffects.Begin;
            LiveEffects.Begin = nullptr;
            LiveEffects.End = nullptr;
            LiveEffects.Capacity = nullptr;
            delete[] HitEffectIds.Begin;
            HitEffectIds.Begin = nullptr;
            HitEffectIds.End = nullptr;
            HitEffectIds.Capacity = nullptr;
        }

        void FCliMunition::DestroyMostDerived()
        {
            this->~FCliMunition();
        }

        // 004089a3..004089de (and 00408296..004082cf in Load): FUN_0040e990 on every live
        // EffectArch, then erase(begin, end).
        void FCliMunition::ReleaseLiveEffects()
        {
            FEffectArch** It = LiveEffects.Begin;
            while (It != LiveEffects.End)
            {
                EffectArchRelease(*It);
                It += 1;
            }
            LiveEffects.End = LiveEffects.Begin;
        }

        // Tail of LAB_004081c0 (00408296..00408393).
        void FCliMunition::RebuildLiveEffects(unsigned Arg)
        {
            // 00408296..004082cf: release + erase the live list.
            ReleaseLiveEffects();

            // 004082d4..0040834c: reserve(HitEffectIds.size()) -- a new buffer of exactly that
            // many slots when the capacity (FUN_0040d740) is smaller; the (empty) live range
            // is moved over and the old buffer freed.
            unsigned IdCount = 0;
            if (HitEffectIds.Begin != nullptr)
                IdCount = static_cast<unsigned>(HitEffectIds.End - HitEffectIds.Begin);
            unsigned LiveCap = 0;
            if (LiveEffects.Begin != nullptr)
                LiveCap = static_cast<unsigned>(LiveEffects.Capacity - LiveEffects.Begin);
            if (LiveCap < IdCount)
            {
                FEffectArch** NewBuf = new FEffectArch*[IdCount];
                unsigned LiveCount = static_cast<unsigned>(LiveEffects.End - LiveEffects.Begin);
                unsigned Index = 0;
                while (Index < LiveCount)
                {
                    NewBuf[Index] = LiveEffects.Begin[Index];
                    Index += 1;
                }
                delete[] LiveEffects.Begin;
                LiveEffects.Capacity = NewBuf + IdCount;
                LiveEffects.End = NewBuf + LiveCount;
                LiveEffects.Begin = NewBuf;
            }

            // 0040834f..00408393: FUN_0040f080(&id), FUN_0040e960(arch, Arg), push_back
            // (FUN_00537260). The reserve above guarantees room for every push.
            unsigned* IdIt = HitEffectIds.Begin;
            while (IdIt != HitEffectIds.End)
            {
                FEffectArch* Arch = FindEffectArch(IdIt);
                if (Arch != nullptr)
                {
                    EffectArchAddRef(Arch, Arg);
                    *LiveEffects.End = Arch;
                    LiveEffects.End += 1;
                }
                IdIt += 1;
            }
        }

        // FUN_00408a70 (thiscall). Callers 0052ea06 / 00539e2e.
        FEffectArch* FCliMunition::PickLiveEffect()
        {
            // 00408a73..00408a84: null begin or empty -> null.
            if (LiveEffects.Begin == nullptr)
                return nullptr;
            int Count = static_cast<int>(LiveEffects.End - LiveEffects.Begin);
            if (Count == 0)
                return nullptr;

            // 00408a99..00408ab1: (rand() * n + (sign & 0x7fff)) >> 15 (CDQ / AND / ADD / SAR).
            int Raw = rand() * Count;
            int Pick = (Raw + ((Raw >> 31) & 0x7fff)) >> 15;
            return LiveEffects.Begin[Pick];
        }

        // FUN_004089a0: release + erase the live list, then CliRoot's Slot11 inlined
        // (004089e1..00408a69 is 00406d20's body byte for byte).
        void FCliMunition::Slot11()
        {
            ReleaseLiveEffects();
            FCliObj::Slot11();
        }

        // LAB_004081c0. 004081c9..004081e1: "skip" = cache built AND Slot12 done, sampled
        // before anything runs. 004081e1..0040828a is LAB_00407380's body inlined.
        unsigned long FCliMunition::Load(int LoadMode)
        {
            bool bSkipEffectRebuild =
                ((StateFlags & CliObjFlagCacheBuilt) != 0) &&
                ((StateFlags & CliObjFlagSlot12Done) != 0);

            unsigned long Result = FCliObj::Load(LoadMode);

            if (!bSkipEffectRebuild)
                RebuildLiveEffects(static_cast<unsigned>(LoadMode));

            return Result;
        }

        // LAB_004084b0: FUN_00402500 on the LOD list, distance_render copied when unequal,
        // FUN_00406c50(0) -- together FUN_00407050's effect -- then +0x54, +0x58 and the
        // +0x60 string pointer copied when unequal. +0x5c is not touched.
        void FCliMunition::Redefine(ICliObj* Donor)
        {
            FCliMunition* Src = reinterpret_cast<FCliMunition*>(Donor);

            CopyLodChain(&LodHead, Src->LodHead);
            if (Src->DistanceRender != DistanceRender)
                DistanceRender = Src->DistanceRender;
            RecomputeLodPick(nullptr);

            if (Src->ConstEffectDelay != ConstEffectDelay)
                ConstEffectDelay = Src->ConstEffectDelay;
            if (Src->ConstEffect.Id != ConstEffect.Id)
                ConstEffect.Id = Src->ConstEffect.Id;
            if (Src->HpTrailParent.Value != HpTrailParent.Value)
                HpTrailParent.Value = Src->HpTrailParent.Value;
        }

        // FUN_00408520: __RTDynamicCast to CliMunition (no null test), RefCount saved,
        // FUN_004086c0 (CliRoot::operator=), +0x54..+0x60 copied, the +0x64 id vector
        // assigned (inline), the +0x74 EffectArch* vector assigned (FUN_005595c0 -- a plain
        // pointer copy, no AddRef and no release of what was there), RefCount restored.
        void FCliMunition::Inherit(ICliObj* Donor)
        {
            FCliMunition* Src = reinterpret_cast<FCliMunition*>(Donor);
            int SavedRef = RefCount;

            AssignRootMembers(Src);

            ConstEffectDelay = Src->ConstEffectDelay;
            ConstEffect.Id = Src->ConstEffect.Id;
            OneShotSound.Id = Src->OneShotSound.Id;
            HpTrailParent.Value = Src->HpTrailParent.Value;

            AssignVc6Vector(&HitEffectIds, &Src->HitEffectIds);
            AssignVc6Vector(&LiveEffects, &Src->LiveEffects);

            RefCount = SavedRef;
        }

        // 00401e90: RET.
        void FCliEQShield::Slot10()
        {
        }

        // 0040d0c9..0040d0e4: FUN_004068d0, ID_String::clear on +0x54, vptr 005c83a8.
        FCliRepairDroid::FCliRepairDroid()
        {
            ActiveEffect.clear();
        }

        // FUN_0040c810.
        bool FCliRepairDroid::ReadUnknown(INI_Reader* Reader)
        {
            if (FCliObj::ReadUnknown(Reader))
                return true;

            if (Reader->is_value("active_effect"))
            {
                ActiveEffect.Id = CreateID(Reader->get_value_string());
                return true;
            }

            return false;
        }

        // Slot 4 is FUN_00409bd0, shared with CliEQShieldGenerator.
        void FCliRepairDroid::Redefine(ICliObj* Donor)
        {
            FCliObj::Redefine(Donor);
            CliRedefineFirstDword(this, Donor);
        }

        // FUN_0040c860: __RTDynamicCast to CliEQRepairDroid (no null test), RefCount saved,
        // CliRoot::operator= inlined, 0040ca3c: +0x54 copied, RefCount restored.
        void FCliRepairDroid::Inherit(ICliObj* Donor)
        {
            FCliRepairDroid* Src = reinterpret_cast<FCliRepairDroid*>(Donor);
            int SavedRef = RefCount;
            AssignRootMembers(Src);
            ActiveEffect.Id = Src->ActiveEffect.Id;
            RefCount = SavedRef;
        }

        // 0040cddc..0040ce0d: FUN_004068d0, ID_String::clear on +0x54/+0x58/+0x5c, the +0x60
        // vector empty (allocator byte copied, three null pointers), vptr 005c7cfc.
        FCliShieldGenerator::FCliShieldGenerator()
        {
            CollapseParticle.clear();
            CollapseSound.clear();
            RebuiltSound.clear();
            memset(&ShieldHitEffects, 0, sizeof(ShieldHitEffects));
        }

        // FUN_004097a0: for every +0x60 record, the live buffer (+0x18) and the EffectId buffer
        // (+0x04) are freed -- no EffectArch release -- then the +0x64 buffer; then the CliRoot
        // part (vptr 005c7734, FUN_004024d0, the +0x44 records via FUN_00407000, +0x34, +0x24).
        FCliShieldGenerator::~FCliShieldGenerator()
        {
            FCliDistanceRecord* It = ShieldHitEffects.Begin;
            FCliDistanceRecord* End = ShieldHitEffects.End;
            while (It != End)
            {
                DestroyDistanceRecordBuffers(It);
                It += 1;
            }
            delete[] ShieldHitEffects.Begin;
            ShieldHitEffects.Begin = nullptr;
            ShieldHitEffects.End = nullptr;
            ShieldHitEffects.Capacity = nullptr;
        }

        void FCliShieldGenerator::DestroyMostDerived()
        {
            this->~FCliShieldGenerator();
        }

        // LAB_004098b0.
        bool FCliShieldGenerator::ReadUnknown(INI_Reader* Reader)
        {
            if (FCliObj::ReadUnknown(Reader))
                return true;

            if (Reader->is_value("shield_collapse_particle"))
            {
                CollapseParticle.Id = CreateID(Reader->get_value_string());
                return true;
            }

            if (Reader->is_value("shield_collapse_sound"))
            {
                CollapseSound.Id = CreateID(Reader->get_value_string());
                return true;
            }

            if (Reader->is_value("shield_rebuilt_sound"))
            {
                RebuiltSound.Id = CreateID(Reader->get_value_string());
                return true;
            }

            // 00409974..00409bac: value 0 read first, an empty record appended (FUN_0040daa0),
            // the float stored into the new last record, then CreateID of values 1.. pushed
            // into its EffectId list until is_value_empty(i). Returns 1.
            if (Reader->is_value("shield_hit_effects"))
            {
                float Threshold = Reader->get_value_float(0);
                FCliDistanceRecord* Added = PushEmptyDistanceRecord(&ShieldHitEffects);
                Added->DistanceThreshold = Threshold;
                unsigned ValueIndex = 1;
                while (!Reader->is_value_empty(ValueIndex))
                {
                    unsigned EffectId = CreateID(Reader->get_value_string(ValueIndex));
                    PushEffectIdOnRecord(Added, EffectId);
                    ValueIndex += 1;
                }
                return true;
            }

            return false;
        }

        // Slot 4 is FUN_00409bd0: FUN_00407050's body, then +0x54 copied when it differs.
        void FCliShieldGenerator::Redefine(ICliObj* Donor)
        {
            FCliObj::Redefine(Donor);
            CliRedefineFirstDword(this, Donor);
        }

        // LAB_00409c80: __RTDynamicCast to CliEQShieldGenerator (no null test), RefCount saved;
        // CliRoot::operator= written out with its helpers called (00409ca6..00409d40: +4..+0x1c
        // raw, FUN_0040d2c0 / FUN_005595c0 / FUN_0040d4b0 on the three vectors, the +0x50 flag
        // bits then the dword); +0x54/+0x58/+0x5c raw; the +0x60 record vector assigned when the
        // two differ (00409d58); RefCount restored.
        void FCliShieldGenerator::Inherit(ICliObj* Donor)
        {
            FCliShieldGenerator* Src = reinterpret_cast<FCliShieldGenerator*>(Donor);
            int SavedRef = RefCount;
            AssignRootMembers(Src);
            CollapseParticle.Id = Src->CollapseParticle.Id;
            CollapseSound.Id = Src->CollapseSound.Id;
            RebuiltSound.Id = Src->RebuiltSound.Id;
            AssignDistanceRecords(&ShieldHitEffects, &Src->ShieldHitEffects);
            RefCount = SavedRef;
        }

        // FUN_00409fb0: flag 8 raised, FUN_00555860(Arg) on every +0x40 record, then on every
        // +0x60 record.
        void FCliShieldGenerator::Slot12(unsigned Arg)
        {
            FCliObj::Slot12(Arg);
            FCliDistanceRecord* It = ShieldHitEffects.Begin;
            while (It != ShieldHitEffects.End)
            {
                ApplyDistanceRecord(It, Arg);
                It += 1;
            }
        }

        // FUN_0040a010: the +0x40 records released and flag 8 dropped only when flag 8 was set
        // (FUN_00407530's body); the +0x60 records are released (FUN_005559c0) unconditionally.
        void FCliShieldGenerator::Slot13()
        {
            FCliObj::Slot13();
            FCliDistanceRecord* It = ShieldHitEffects.Begin;
            while (It != ShieldHitEffects.End)
            {
                TeardownDistanceRecord(It);
                It += 1;
            }
        }

        // 0040ce41..0040ce69 / 0040cea9..0040ced3: FUN_004068d0, ID_String::clear on +0x54,
        // +0x58 = 4.0f (0x40800000), +0x5c = 0, +0x60 = 0, ID_String::clear on +0x80, vptr
        // 005c7df0.
        FCliLauncher::FCliLauncher()
        {
            FlashParticle.clear();
            FlashRadius = 4.0f;
            LightAnimLength = 0;
            LightAnim[0] = 0;
            DryFireSound.clear();
        }

        void FCliLauncher::AssignLightAnim(char const* Text)
        {
            if (Text == nullptr)
            {
                LightAnimLength = 0;
                LightAnim[0] = 0;
                return;
            }

            // REPNE SCASB strlen; CMP 0x20 / JC keeps it, else 0x1f; REP MOVSD/MOVSB copy.
            unsigned Len = static_cast<unsigned>(strlen(Text));
            if (Len >= 0x20u)
                Len = 0x1fu;
            memcpy(LightAnim, Text, Len);
            LightAnim[Len] = 0;
            LightAnimLength = Len;
        }

        // FUN_0040a4d0.
        bool FCliLauncher::ReadUnknown(INI_Reader* Reader)
        {
            if (FCliObj::ReadUnknown(Reader))
                return true;

            if (Reader->is_value("flash_radius"))
            {
                FlashRadius = Reader->get_value_float(0);
                return true;
            }

            if (Reader->is_value("light_anim"))
            {
                AssignLightAnim(Reader->get_value_string());
                return true;
            }

            if (Reader->is_value("flash_particle_name"))
            {
                FlashParticle.Id = CreateID(Reader->get_value_string());
                return true;
            }

            if (Reader->is_value("dry_fire_sound"))
            {
                DryFireSound.Id = CreateID(Reader->get_value_string());
                return true;
            }

            return false;
        }

        // FUN_0040a5e0 (slot 4 of CliEQLauncher and CliEQGun): FUN_00407050's body (with the
        // LodPick step through FUN_00406cd0, the same computation), then 0040a663: +0x58 copied
        // unless equal; 0040a676: the light_anim string re-assigned when strcmp says it differs;
        // 0040a6fe: +0x54 copied when it differs. +0x80 (and the gun's +0x84) are not touched.
        void FCliLauncher::Redefine(ICliObj* Donor)
        {
            FCliObj::Redefine(Donor);

            FCliLauncher* Src = reinterpret_cast<FCliLauncher*>(Donor);
            if (!(Src->FlashRadius == FlashRadius))
                FlashRadius = Src->FlashRadius;
            if (strcmp(Src->LightAnim, LightAnim) != 0)
                AssignLightAnim(Src->LightAnim);
            if (Src->FlashParticle.Id != FlashParticle.Id)
                FlashParticle.Id = Src->FlashParticle.Id;
        }

        // LAB_0040a720: __RTDynamicCast to CliEQLauncher (no null test), RefCount saved,
        // CliRoot::operator= written out with its helpers called, +0x54 / +0x58, the light_anim
        // string assigned from the donor's (0040a7e7), +0x80, RefCount restored.
        void FCliLauncher::Inherit(ICliObj* Donor)
        {
            FCliLauncher* Src = reinterpret_cast<FCliLauncher*>(Donor);
            int SavedRef = RefCount;
            AssignRootMembers(Src);
            FlashParticle.Id = Src->FlashParticle.Id;
            FlashRadius = Src->FlashRadius;
            AssignLightAnim(Src->LightAnim);
            DryFireSound.Id = Src->DryFireSound.Id;
            RefCount = SavedRef;
        }

        // FUN_0040a3b0. Whether this object was already fully loaded (flags 1 and 8 both set) is
        // taken on entry (0040a3b6..0040a3c9); 0040a3ce..0040a472 is FUN_00407380's body; then,
        // unless it was already loaded, the launcher's projectile_archetype (Root +0x98) is looked
        // up with Archetype::GetEquipment and that equipment's companion (Root +0x04) is loaded
        // with the same argument (0040a4b7 JMP [vt+0x20]).
        unsigned long FCliLauncher::Load(int LoadMode)
        {
            bool const bWasLoaded = (StateFlags & CliObjFlagCacheBuilt) != 0 &&
                                    (StateFlags & CliObjFlagSlot12Done) != 0;

            unsigned long const Result = FCliObj::Load(LoadMode);
            if (bWasLoaded)
                return Result;

            Launcher const* LauncherArch = static_cast<Launcher const*>(BoundRoot);
            if (LauncherArch->ProjectileArchetype.IsEmpty())
                return Result;

            Equipment* Projectile = GetEquipment(LauncherArch->ProjectileArchetype.Id);
            if (Projectile == nullptr)
                return Result;

            return Projectile->Companion->Load(LoadMode);
        }

        // 0040ce71..0040ce7e: the launcher part, then ID_String::clear on +0x84 and vptr 005c7e6c.
        FCliGun::FCliGun()
        {
            TurretSound.clear();
        }

        // FUN_0040a850: FUN_0040a4d0 first, then "turret_sound" -> CreateID -> +0x84.
        bool FCliGun::ReadUnknown(INI_Reader* Reader)
        {
            if (FCliLauncher::ReadUnknown(Reader))
                return true;

            if (Reader->is_value("turret_sound"))
            {
                TurretSound.Id = CreateID(Reader->get_value_string());
                return true;
            }

            return false;
        }

        // LAB_0040a8a0: __RTDynamicCast to CliEQGun (no null test); the same body as 0040a720
        // plus +0x84 (0040a9c1); RefCount restored.
        void FCliGun::Inherit(ICliObj* Donor)
        {
            FCliGun* Src = reinterpret_cast<FCliGun*>(Donor);
            int SavedRef = RefCount;
            AssignRootMembers(Src);
            FlashParticle.Id = Src->FlashParticle.Id;
            FlashRadius = Src->FlashRadius;
            AssignLightAnim(Src->LightAnim);
            DryFireSound.Id = Src->DryFireSound.Id;
            TurretSound.Id = Src->TurretSound.Id;
            RefCount = SavedRef;
        }

        // FUN_00408f20: FUN_004068d0; +0x54 = 0, +0x58 = 0x168, +0x5c = 0; ID_String::clear on
        // +0x60; +0x64..+0x70 = 0; clear on +0x74, +0x78; +0x7c = +0x80 = 0; clear on +0x84 and
        // every ID from +0x88 to +0xa8; vptr 005c7ac4.
        FCliEngine::FCliEngine()
        {
            InsideSoundCone = 0;
            OutsideSoundCone = 0x168;
            OutsideConeAttenuation = 0.0f;
            RumbleSound.clear();
            RumblePitchRange[0] = 0;
            RumblePitchRange[1] = 0;
            RumbleAttenRange[0] = 0;
            RumbleAttenRange[1] = 0;
            CharacterStartSound.clear();
            CharacterLoopSound.clear();
            CharacterPitchRange[0] = 0;
            CharacterPitchRange[1] = 0;
            EngineKillSound.clear();
            CruiseStartSound.clear();
            CruiseLoopSound.clear();
            CruiseStopSound.clear();
            CruiseDisruptSound.clear();
            CruiseBackfireSound.clear();
            FlameEffect.clear();
            TrailEffect.clear();
            TrailEffectPlayer.clear();
            CruiseDisruptEffect.clear();
        }

        // The three "*_range" keys of 00409172 / 004091bd / 00409260: two get_value_int stored,
        // then when the first is greater (CMP / JLE, signed) INI_Reader::log_link with the
        // "... out of order!" text and the second forced down to the first.
        void ReadEngineRange(INI_Reader* Reader, int* Range, char const* OutOfOrder)
        {
            Range[0] = Reader->get_value_int(0);
            Range[1] = Reader->get_value_int(1);
            if (Range[0] > Range[1])
            {
                Reader->log_link(OutOfOrder);
                Range[1] = Range[0];
            }
        }

        // LAB_00408ff0.
        bool FCliEngine::ReadUnknown(INI_Reader* Reader)
        {
            if (FCliObj::ReadUnknown(Reader))
                return true;

            if (Reader->is_value("flame_effect"))
            {
                FlameEffect.Id = CreateID(Reader->get_value_string(0));
                return true;
            }

            if (Reader->is_value("trail_effect"))
            {
                TrailEffect.Id = CreateID(Reader->get_value_string(0));
                return true;
            }

            if (Reader->is_value("trail_effect_player"))
            {
                TrailEffectPlayer.Id = CreateID(Reader->get_value_string(0));
                return true;
            }

            if (Reader->is_value("cruise_disrupt_effect"))
            {
                CruiseDisruptEffect.Id = CreateID(Reader->get_value_string(0));
                return true;
            }

            if (Reader->is_value("inside_sound_cone"))
            {
                InsideSoundCone = Reader->get_value_int(0);
                return true;
            }

            if (Reader->is_value("outside_sound_cone"))
            {
                OutsideSoundCone = Reader->get_value_int(0);
                return true;
            }

            if (Reader->is_value("outside_cone_attenuation"))
            {
                OutsideConeAttenuation = Reader->get_value_float(0);
                return true;
            }

            if (Reader->is_value("rumble_sound"))
            {
                RumbleSound.Id = CreateID(Reader->get_value_string());
                return true;
            }

            if (Reader->is_value("rumble_pitch_range"))
            {
                ReadEngineRange(Reader, RumblePitchRange, "rumble_pitch_range out of order!");
                return true;
            }

            if (Reader->is_value("rumble_atten_range"))
            {
                ReadEngineRange(Reader, RumbleAttenRange, "rumble_atten_range out of order!");
                return true;
            }

            if (Reader->is_value("character_start_sound"))
            {
                CharacterStartSound.Id = CreateID(Reader->get_value_string());
                return true;
            }

            if (Reader->is_value("character_loop_sound"))
            {
                CharacterLoopSound.Id = CreateID(Reader->get_value_string());
                return true;
            }

            if (Reader->is_value("character_pitch_range"))
            {
                ReadEngineRange(Reader, CharacterPitchRange,
                                "character_pitch_range out of order!");
                return true;
            }

            if (Reader->is_value("cruise_start_sound"))
            {
                CruiseStartSound.Id = CreateID(Reader->get_value_string());
                return true;
            }

            if (Reader->is_value("cruise_loop_sound"))
            {
                CruiseLoopSound.Id = CreateID(Reader->get_value_string());
                return true;
            }

            if (Reader->is_value("cruise_stop_sound"))
            {
                CruiseStopSound.Id = CreateID(Reader->get_value_string());
                return true;
            }

            if (Reader->is_value("cruise_disrupt_sound"))
            {
                CruiseDisruptSound.Id = CreateID(Reader->get_value_string());
                return true;
            }

            if (Reader->is_value("cruise_backfire_sound"))
            {
                CruiseBackfireSound.Id = CreateID(Reader->get_value_string());
                return true;
            }

            if (Reader->is_value("engine_kill_sound"))
            {
                EngineKillSound.Id = CreateID(Reader->get_value_string());
                return true;
            }

            return false;
        }

        // FUN_004093e0: FUN_00407050's body inlined, then 00409474..004094c5: each of the three
        // int pairs (+0x64/+0x68, +0x6c/+0x70, +0x7c/+0x80) copied whole when either half
        // differs. Nothing else follows the donor.
        void FCliEngine::Redefine(ICliObj* Donor)
        {
            FCliObj::Redefine(Donor);

            FCliEngine* Src = reinterpret_cast<FCliEngine*>(Donor);
            if (Src->RumblePitchRange[0] != RumblePitchRange[0] ||
                Src->RumblePitchRange[1] != RumblePitchRange[1])
            {
                RumblePitchRange[0] = Src->RumblePitchRange[0];
                RumblePitchRange[1] = Src->RumblePitchRange[1];
            }
            if (Src->RumbleAttenRange[0] != RumbleAttenRange[0] ||
                Src->RumbleAttenRange[1] != RumbleAttenRange[1])
            {
                RumbleAttenRange[0] = Src->RumbleAttenRange[0];
                RumbleAttenRange[1] = Src->RumbleAttenRange[1];
            }
            if (Src->CharacterPitchRange[0] != CharacterPitchRange[0] ||
                Src->CharacterPitchRange[1] != CharacterPitchRange[1])
            {
                CharacterPitchRange[0] = Src->CharacterPitchRange[0];
                CharacterPitchRange[1] = Src->CharacterPitchRange[1];
            }
        }

        // FUN_004094e0: __RTDynamicCast to CliEQEngine (no null test), RefCount saved,
        // CliRoot::operator= inlined, 004096a9..0040976d: every dword +0x54..+0xa8 copied,
        // RefCount restored.
        void FCliEngine::Inherit(ICliObj* Donor)
        {
            FCliEngine* Src = reinterpret_cast<FCliEngine*>(Donor);
            int SavedRef = RefCount;
            AssignRootMembers(Src);
            InsideSoundCone = Src->InsideSoundCone;
            OutsideSoundCone = Src->OutsideSoundCone;
            OutsideConeAttenuation = Src->OutsideConeAttenuation;
            RumbleSound.Id = Src->RumbleSound.Id;
            RumblePitchRange[0] = Src->RumblePitchRange[0];
            RumblePitchRange[1] = Src->RumblePitchRange[1];
            RumbleAttenRange[0] = Src->RumbleAttenRange[0];
            RumbleAttenRange[1] = Src->RumbleAttenRange[1];
            CharacterStartSound.Id = Src->CharacterStartSound.Id;
            CharacterLoopSound.Id = Src->CharacterLoopSound.Id;
            CharacterPitchRange[0] = Src->CharacterPitchRange[0];
            CharacterPitchRange[1] = Src->CharacterPitchRange[1];
            EngineKillSound.Id = Src->EngineKillSound.Id;
            CruiseStartSound.Id = Src->CruiseStartSound.Id;
            CruiseLoopSound.Id = Src->CruiseLoopSound.Id;
            CruiseStopSound.Id = Src->CruiseStopSound.Id;
            CruiseDisruptSound.Id = Src->CruiseDisruptSound.Id;
            CruiseBackfireSound.Id = Src->CruiseBackfireSound.Id;
            FlameEffect.Id = Src->FlameEffect.Id;
            TrailEffect.Id = Src->TrailEffect.Id;
            TrailEffectPlayer.Id = Src->TrailEffectPlayer.Id;
            CruiseDisruptEffect.Id = Src->CruiseDisruptEffect.Id;
            RefCount = SavedRef;
        }

        // FUN_0040bd90: FUN_004068d0; ID_String::clear on +0x54..+0x90; +0x94 = +0x9c = +0x98 =
        // 1.0f; vptr 005c8074; +0xa4 = 300, +0xa0 = 600, +0xa8 = 25, +0xac = 50, +0xb0 = 0.
        // Retail leaves the five effect pointers at +0xb4 as allocated; FUN_0040c690 writes them
        // before anything reads them, so the port starts them null.
        FCliTradeLane::FCliTradeLane()
        {
            PlayerEnter.clear();
            PlayerTravel.clear();
            PlayerSplash.clear();
            PlayerExit.clear();
            PlayerDisrupt.clear();
            ShipEnter.clear();
            ShipTravel.clear();
            ShipSplash.clear();
            ShipExit.clear();
            ShipDisrupt.clear();
            RingActive.clear();
            RingShield.clear();
            RingTube.clear();
            RingActiveStart.clear();
            RingActiveLoop.clear();
            RingActiveEnd.clear();
            SecsBeforeEnter = 1.0f;
            SecsBeforeSplash = 1.0f;
            SecsBeforeExit = 1.0f;
            ActivationEnd = 300.0f;
            ActivationStart = 600.0f;
            SpinAccel = 25.0f;
            SpinMax = 50.0f;
            EffectRefCount = 0;
            unsigned Index = 0;
            while (Index < 5)
            {
                Effects[Index] = nullptr;
                Index += 1;
            }
        }

        // LAB_0040be90.
        bool FCliTradeLane::ReadUnknown(INI_Reader* Reader)
        {
            if (FCliObj::ReadUnknown(Reader))
                return true;

            // 0040beb3..0040c16a: sixteen ID keys, CreateID(get_value_string()).
            struct FIdKey
            {
                char const* Key;
                ID_String FCliTradeLane::* Field;
            };
            static FIdKey const IdKeys[16] = {
                { "tl_player_enter", &FCliTradeLane::PlayerEnter },
                { "tl_player_travel", &FCliTradeLane::PlayerTravel },
                { "tl_player_splash", &FCliTradeLane::PlayerSplash },
                { "tl_player_exit", &FCliTradeLane::PlayerExit },
                { "tl_player_disrupt", &FCliTradeLane::PlayerDisrupt },
                { "tl_ship_enter", &FCliTradeLane::ShipEnter },
                { "tl_ship_travel", &FCliTradeLane::ShipTravel },
                { "tl_ship_splash", &FCliTradeLane::ShipSplash },
                { "tl_ship_exit", &FCliTradeLane::ShipExit },
                { "tl_ship_disrupt", &FCliTradeLane::ShipDisrupt },
                { "tl_ring_active", &FCliTradeLane::RingActive },
                { "tl_ring_shield", &FCliTradeLane::RingShield },
                { "tl_ring_tube", &FCliTradeLane::RingTube },
                { "tl_ring_active_start", &FCliTradeLane::RingActiveStart },
                { "tl_ring_active_loop", &FCliTradeLane::RingActiveLoop },
                { "tl_ring_active_end", &FCliTradeLane::RingActiveEnd },
            };
            unsigned Index = 0;
            while (Index < 16)
            {
                if (Reader->is_value(IdKeys[Index].Key))
                {
                    (this->*(IdKeys[Index].Field)).Id = CreateID(Reader->get_value_string());
                    return true;
                }
                Index += 1;
            }

            // 0040c174..0040c26d: seven floats, get_value_float(0).
            struct FFloatKey
            {
                char const* Key;
                float FCliTradeLane::* Field;
            };
            static FFloatKey const FloatKeys[7] = {
                { "secs_before_enter", &FCliTradeLane::SecsBeforeEnter },
                { "secs_before_exit", &FCliTradeLane::SecsBeforeExit },
                { "secs_before_splash", &FCliTradeLane::SecsBeforeSplash },
                { "activation_start", &FCliTradeLane::ActivationStart },
                { "activation_end", &FCliTradeLane::ActivationEnd },
                { "spin_max", &FCliTradeLane::SpinMax },
                { "spin_accel", &FCliTradeLane::SpinAccel },
            };
            Index = 0;
            while (Index < 7)
            {
                if (Reader->is_value(FloatKeys[Index].Key))
                {
                    this->*(FloatKeys[Index].Field) = Reader->get_value_float(0);
                    return true;
                }
                Index += 1;
            }

            return false;
        }

        // FUN_0040c290. Unlike every other Redefine it tests the donor (0040c29d: TEST EDI,EDI /
        // JZ out). Then FUN_00407050's body (LodPick through FUN_00406cd0), then five floats,
        // each -- when it differs from the donor's (TEST AH,0x44 / JNP skips only on equal) --
        // FDUMPed as a warning from CliArch.cpp with (this value, donor value) and then copied.
        void FCliTradeLane::Redefine(ICliObj* Donor)
        {
            if (Donor == nullptr)
                return;

            FCliObj::Redefine(Donor);

            FCliTradeLane* Src = reinterpret_cast<FCliTradeLane*>(Donor);
            char const* const File = "E:\\FL\\Scratch\\Source\\Client\\cmn\\CliArch.cpp";
            if (!(Src->SpinMax == SpinMax))
            {
                FDUMP(0x100002, "%s(%d) : *** WARNING: %f: 'spin_max' changed to %f.", File,
                      0x747, static_cast<double>(SpinMax), static_cast<double>(Src->SpinMax));
                SpinMax = Src->SpinMax;
            }
            if (!(Src->SpinAccel == SpinAccel))
            {
                FDUMP(0x100002, "%s(%d) : *** WARNING: %f: 'spin_accel' changed to %f.", File,
                      0x748, static_cast<double>(SpinAccel), static_cast<double>(Src->SpinAccel));
                SpinAccel = Src->SpinAccel;
            }
            if (!(Src->SecsBeforeEnter == SecsBeforeEnter))
            {
                FDUMP(0x100002, "%s(%d) : *** WARNING: %f: 'secs_before_enter' changed to %f.",
                      File, 0x74a, static_cast<double>(SecsBeforeEnter),
                      static_cast<double>(Src->SecsBeforeEnter));
                SecsBeforeEnter = Src->SecsBeforeEnter;
            }
            if (!(Src->SecsBeforeSplash == SecsBeforeSplash))
            {
                FDUMP(0x100002, "%s(%d) : *** WARNING: %f: 'secs_before_splash' changed to %f.",
                      File, 0x74b, static_cast<double>(SecsBeforeSplash),
                      static_cast<double>(Src->SecsBeforeSplash));
                SecsBeforeSplash = Src->SecsBeforeSplash;
            }
            if (!(Src->SecsBeforeExit == SecsBeforeExit))
            {
                FDUMP(0x100002, "%s(%d) : *** WARNING: %f: 'secs_before_exit' changed to %f.",
                      File, 0x74c, static_cast<double>(SecsBeforeExit),
                      static_cast<double>(Src->SecsBeforeExit));
                SecsBeforeExit = Src->SecsBeforeExit;
            }
        }

        // LAB_0040c4d0: __RTDynamicCast to CliEQTradeLane (no null test), RefCount saved,
        // CliRoot::operator= written out with its helpers called, then every dword +0x54..+0xb0
        // and the five effect pointers +0xb4..+0xc4 copied raw (no AddRef), RefCount restored.
        void FCliTradeLane::Inherit(ICliObj* Donor)
        {
            FCliTradeLane* Src = reinterpret_cast<FCliTradeLane*>(Donor);
            int SavedRef = RefCount;
            AssignRootMembers(Src);
            memcpy(&PlayerEnter, &Src->PlayerEnter, 0xc8 - 0x54);
            RefCount = SavedRef;
        }

        // FUN_0040c690: the first reference resolves the five effects -- the player variant
        // (+0x54..+0x64) through FUN_0040f080, else the ship variant (+0x68..+0x78) -- and
        // takes a FUN_0040e960(0) reference on each one found; every call bumps +0xb0.
        void FCliTradeLane::AddEffectRef()
        {
            if (EffectRefCount == 0)
            {
                static ID_String FCliTradeLane::* const PlayerIds[5] = {
                    &FCliTradeLane::PlayerEnter, &FCliTradeLane::PlayerTravel,
                    &FCliTradeLane::PlayerSplash, &FCliTradeLane::PlayerExit,
                    &FCliTradeLane::PlayerDisrupt,
                };
                static ID_String FCliTradeLane::* const ShipIds[5] = {
                    &FCliTradeLane::ShipEnter, &FCliTradeLane::ShipTravel,
                    &FCliTradeLane::ShipSplash, &FCliTradeLane::ShipExit,
                    &FCliTradeLane::ShipDisrupt,
                };
                unsigned Slot = 0;
                while (Slot < 5)
                {
                    FEffectArch* Fx = FindEffectArch(&(this->*(PlayerIds[Slot])).Id);
                    Effects[Slot] = Fx;
                    if (Fx == nullptr)
                    {
                        Fx = FindEffectArch(&(this->*(ShipIds[Slot])).Id);
                        Effects[Slot] = Fx;
                    }
                    if (Effects[Slot] != nullptr)
                        EffectArchAddRef(Effects[Slot], 0);
                    Slot += 1;
                }
            }
            EffectRefCount += 1;
        }

        // FUN_0040c7d0: --(+0xb0); at zero each non-null effect is released (FUN_0040e990) and
        // its slot cleared. Returns the remaining count.
        int FCliTradeLane::ReleaseEffectRef()
        {
            EffectRefCount -= 1;
            if (EffectRefCount == 0)
            {
                unsigned Slot = 0;
                while (Slot < 5)
                {
                    if (Effects[Slot] != nullptr)
                    {
                        EffectArchRelease(Effects[Slot]);
                        Effects[Slot] = nullptr;
                    }
                    Slot += 1;
                }
            }
            return EffectRefCount;
        }
    }

    // FUN_0040ca50 (DAT_006164a0 vtable slot 0, thiscall, RET 8). Index = TypeId - 2 into the
    // byte table 0040d1a0 (JA past 0x1f, i.e. types 0/1 and >= 0x22, goes to caseD_7). Every
    // case stores the new object through Out with no null test (0040cee5 / 0040d127 /
    // 0040d0ea ...) and calls its slot 0 (AddRef, 00406b50). Retail returns EAX = 0; the port's
    // ICliObjFactory::Create is void and no caller reads it.
    void FCliObjFactory::Create(int TypeId, ICliObj** Out)
    {
        switch (TypeId)
        {
        case CliObjTypeSolar:
        {
            FCliSolar* Solar = new FCliSolar();
            AdoptCompanion(Out, Solar);
            return;
        }

        case CliObjTypeShip:
        {
            FCliShip* Ship = new FCliShip();
            AdoptCompanion(Out, Ship);
            return;
        }

        case CliObjTypeAsteroid:
        {
            // caseD_4 0040ccc2: operator_new(0x54), FUN_004068d0, vptr 005c7a3c (CliAsteroid).
            NewCompanion<FCliAsteroid>(Out);
            return;
        }

        case CliObjTypeDynAsteroid:
        {
            // caseD_5 0040ccf8: operator_new(0x5c), vptr 005c7a78 (CliDynamicAsteroid).
            NewCompanion<FCliDynamicAsteroid>(Out);
            return;
        }

        case CliObjTypeEngine:
        {
            // caseD_c: operator_new(0xac), FUN_00408f20 (CliEQEngine ctor).
            NewCompanion<FCliEngine>(Out);
            return;
        }

        case CliObjTypeShield:
        {
            // caseD_d 0040cd7c: operator_new(0x54), FUN_004068d0, vptr 005c83e4 (CliEQShield).
            NewCompanion<FCliEQShield>(Out);
            return;
        }

        case CliObjTypeShieldGenerator:
        {
            // caseD_e 0040cdba: operator_new(0x70), vptr 005c7cfc (CliEQShieldGenerator).
            NewCompanion<FCliShieldGenerator>(Out);
            return;
        }

        case CliObjTypeThruster:
        {
            // caseD_f 0040cf27: operator_new(0x5c), vptr 005c7d98 (CliEQThruster).
            NewCompanion<FCliEQThruster>(Out);
            return;
        }

        case CliObjTypeGun:
        {
            // caseD_11 0040ce1a: operator_new(0x88), vptr 005c7e6c (CliEQGun : CliEQLauncher).
            NewCompanion<FCliGun>(Out);
            return;
        }

        case CliObjTypeMineDropper:
        case CliObjTypeCmDropper:
        {
            // caseD_12 0040ce88 (types 0x12 and 0x13 share it through the byte table 0040d1a0):
            // operator_new(0x84), vptr 005c7df0 (CliEQLauncher).
            NewCompanion<FCliLauncher>(Out);
            return;
        }

        case CliObjTypeLight:
        {
            // caseD_15 0040cef8: operator_new(0x98), FUN_0040a9e0 (CliEQLight ctor).
            NewCompanion<FCliEQLight>(Out);
            return;
        }

        case CliObjTypeTractor:
        {
            // caseD_16 0040cf7c: operator_new(0x60), vptr 005c7f78 (CliEQTractor).
            NewCompanion<FCliTractor>(Out);
            return;
        }

        case CliObjTypeAttachedFx:
        {
            // caseD_17 0040cfda: operator_new(0x5c), vptr 005c7fe0 (CliEQAttachedFX).
            NewCompanion<FCliEQAttachedFX>(Out);
            return;
        }

        case CliObjTypeInternalFx:
        {
            // caseD_18 0040d029: operator_new(0x64), vptr 005c802c (CliEQInternalFX).
            NewCompanion<FCliInternalFx>(Out);
            return;
        }

        case CliObjTypeRepairDroid:
        {
            // caseD_19 0040d0ab: operator_new(0x58), vptr 005c83a8 (CliEQRepairDroid).
            NewCompanion<FCliRepairDroid>(Out);
            return;
        }

        case CliObjTypeTradeLane:
        {
            // caseD_1d 0040d083: operator_new(0xc8), FUN_0040bd90 (CliEQTradeLane ctor).
            NewCompanion<FCliTradeLane>(Out);
            return;
        }

        case CliObjTypeMunition:
        {
            // caseD_1f 0040cbc7: operator_new(0x84), CliProj part + vptr 005c8420 (CliMunition).
            NewCompanion<FCliMunition>(Out);
            return;
        }

        case CliObjTypeMine:
        case CliObjTypeCounterMeasure:
        {
            // caseD_20 0040cc44 / caseD_21 0040cc92: operator_new(0x60), vptr 005c79a8 (CliProj).
            NewCompanion<FCliProj>(Out);
            return;
        }

        default:
        {
            // caseD_7 0040d0fc: operator_new(0x54) + FUN_004068d0 (the CliRoot vptr it sets).
            // Byte table 0x13 entries: types 6, 7, LootCrate 8, CargoPod 9, Commodity 0xA,
            // Power 0xB, 0x10, Scanner 0x14, RepairKit 0x1A, ShieldBattery 0x1B,
            // CloakingDevice 0x1C, 0x1E; plus the JA range: Simple 0, 1 and Armor 0x22+.
            FCliObj* Simple = new FCliObj();
            AdoptCompanion(Out, Simple);
            return;
        }
        }
    }

    FCliObjFactory GCliObjFactory;

    bool CliObjPreloadMaterials(ICliObj* Companion, bool bDeferApply)
    {
        // FUN_00407310. Retail calls it directly (not through the vtable) on Root+0x04, which
        // FCliObjFactory::Create always fills with an FCliObj or a subclass of it.
        // 0040712b/00407171 SETZ: retail's zero means apply now and latch slot 10.
        return static_cast<FCliObj*>(Companion)->PreloadMaterials(!bDeferApply);
    }

    void CliTradeLaneAddEffectRef(ICliObj* Companion)
    {
        // FUN_0040c690, called at 0x00516cb0 on Equip->arch->Root+0x04 with no null test; the
        // equipment is a TradeLane, whose companion the factory built as CliEQTradeLane.
        static_cast<FCliTradeLane*>(static_cast<FCliObj*>(Companion))->AddEffectRef();
    }

    int CliTradeLaneReleaseEffectRef(ICliObj* Companion)
    {
        // FUN_0040c7d0, called at 0x00516be4 the same way.
        return static_cast<FCliTradeLane*>(static_cast<FCliObj*>(Companion))->ReleaseEffectRef();
    }

    FEffectArch* CliObjPickDistanceRecord(ICliObj* Companion, float Distance)
    {
        // FUN_00407450, thiscall on the archetype's companion; GameObject slot 104 calls it with
        // ECX = the companion or null (0x00539d47) and no test, so a null companion reaches the
        // callee exactly as in retail.
        return static_cast<FEffectArch*>(static_cast<FCliObj*>(Companion)->PickDistanceRecord(Distance));
    }

    FEffectArch* CliShieldGeneratorPickHitEffect(ICliObj* Companion, float Distance)
    {
        // FUN_00409f10 on a shield generator's companion (the factory builds CliEQShieldGenerator
        // for that equipment type).
        return static_cast<FEffectArch*>(
            static_cast<FCliShieldGenerator*>(static_cast<FCliObj*>(Companion))
                ->PickShieldHitEffect(Distance));
    }

    FEffectArch* CliMunitionPickLiveEffect(ICliObj* Companion)
    {
        // FUN_00408a70 on a munition archetype's companion (CliMunition by the factory table).
        return static_cast<FCliMunition*>(static_cast<FCliObj*>(Companion))->PickLiveEffect();
    }

    ID_String const* CliShipBayDoorSound(ICliObj* Companion, bool bOpen)
    {
        // The raw reads FUN_00545670 case 2 makes on a ship archetype's companion (a CliShip by
        // the factory table): +0x58 bay_doors_open_snd (0x00545804), +0x5c bay_doors_close_snd
        // (0x00545837).
        FCliShip* const Ship = static_cast<FCliShip*>(static_cast<FCliObj*>(Companion));
        return bOpen ? &Ship->Id58 : &Ship->Id5C;
    }

    ID_String const* CliSolarDoorSound(ICliObj* Companion, bool bOpen)
    {
        // FUN_005478f0 on a solar archetype's companion (a CliSolar by the factory table): +0x54
        // open_sound (0x00547c37 / 0x00547ec5), +0x58 close_sound (0x00547a43 / 0x00547ef8).
        return static_cast<FCliSolar*>(static_cast<FCliObj*>(Companion))->DoorSound(bOpen);
    }

    ID_String const* CliEngineFlameEffect(ICliObj* Companion)
    {
        // Gf ObjectShip FUN_00446ca0 0x00446cfd..0x00446d00: ADD ESI,0x9c on the companion of an
        // archetype whose class is Engine (0xc).
        return &static_cast<FCliEngine*>(static_cast<FCliObj*>(Companion))->FlameEffect;
    }

    ID_String const* CliEngineRumbleSound(ICliObj* Companion)
    {
        // FUN_00447f90 0x0044804d: MOV EBP,[ESI+0x60].
        return &static_cast<FCliEngine*>(static_cast<FCliObj*>(Companion))->RumbleSound;
    }

    ID_String const* CliEngineCharacterLoopSound(ICliObj* Companion)
    {
        // FUN_00447f90 0x0044804a: MOV ECX,[ESI+0x78].
        return &static_cast<FCliEngine*>(static_cast<FCliObj*>(Companion))->CharacterLoopSound;
    }

    // The raw-offset reads Gf::ObjectProp makes on Root+0x04 (FUN_004464f0 0x00446562..
    // 0x00446588): +0x10 and +0x20. The same FCliObj-or-subclass guarantee as above holds.
    char const* CliObjEnvmapMaterial(ICliObj* Companion)
    {
        return static_cast<FCliObj*>(Companion)->EnvmapMaterial.Value;
    }

    FCliVc6Vector<FCliMaterialPathPair> const* CliObjMaterialPaths(ICliObj* Companion)
    {
        return &static_cast<FCliObj*>(Companion)->MaterialPaths;
    }

    char const* CliSolarShapeName(ICliObj* Companion)
    {
        // Companion +0x60 (e.g. the nav map's FUN_004917c0 at 0x00491aa2..0x00491ab9).
        return static_cast<FCliSolar*>(Companion)->ShapeName();
    }

    void CliObjRecomputeLodPickExplicit(ICliObj* Companion, long EngineIndex,
                                        float const* BlendSource, bool bSolarOrShip)
    {
        // FUN_00406cd0, called directly (thiscall on the companion) by FUN_005595a0 and
        // FUN_004468f0.
        static_cast<FCliObj*>(Companion)->RecomputeLodPickExplicit(EngineIndex, BlendSource,
                                                                   bSolarOrShip);
    }

    void CliObjRecomputeLodPick(ICliObj* Companion, float const* BlendSource)
    {
        // FUN_00406c50, called directly (thiscall on the companion, RET 4) by the
        // level_of_detail enumerator LAB_005b4540 and by FUN_004084b0.
        static_cast<FCliObj*>(Companion)->RecomputeLodPick(BlendSource);
    }

    void CliObjSetArchLodGlobals(unsigned char bMute, float Scale)
    {
        SetArchLodGlobals(bMute, Scale); // FUN_004022b0
    }

    unsigned char CliObjGetArchLodGlobals(float* OutScale)
    {
        return GetArchLodGlobals(OutScale); // FUN_004022d0
    }

    ID_String const* CliProjConstEffect(ICliObj* Companion)
    {
        return &static_cast<FCliProj*>(static_cast<FCliObj*>(Companion))->ConstEffect;
    }

    float CliProjConstEffectDelay(ICliObj* Companion)
    {
        return static_cast<FCliProj*>(static_cast<FCliObj*>(Companion))->ConstEffectDelay;
    }

    char const* CliMunitionHpTrailParent(ICliObj* Companion)
    {
        // The raw +0x60 read Guided slot 112 LAB_0053f030 makes (0x0053f07d / 0x0053f0b1):
        // the CacheString's text pointer, tested by its first byte.
        return static_cast<FCliMunition*>(static_cast<FCliObj*>(Companion))->HpTrailParent.Value;
    }

    bool CliDynamicAsteroidHasParticleEffect(ICliObj* Companion)
    {
        return static_cast<FCliDynamicAsteroid*>(static_cast<FCliObj*>(Companion))
            ->bHasParticleEffect;
    }

    ID_String const* CliDynamicAsteroidParticleEffect(ICliObj* Companion)
    {
        return &static_cast<FCliDynamicAsteroid*>(static_cast<FCliObj*>(Companion))
                    ->ParticleEffect;
    }

    void LoadMountedEquipmentCompanions(EquipDescVector const* Items, int LoadMode)
    {
        // FUN_004252d0 (cdecl). 0x004252d5 / 0x004252d8: Begin (+4) and End (+8), stride 0x20.
        for (EquipDesc const* Desc = Items->Begin; Desc != Items->End; ++Desc)
        {
            // 0x004252f4: Archetype::GetEquipment(Desc +0x04).
            Equipment* const Arch = GetEquipment(Desc->ArchId);
            if (Arch == nullptr)
                continue;
            // 0x00425301: EquipDesc::is_internal; 0x0042530b: the byte at +0x0c.
            if (Desc->is_internal())
                continue;
            if (!Desc->bEquipped)
                continue;
            // 0x00425312..0x0042531c: Root +0x04 companion, vt+0x20 Load(LoadMode).
            Arch->Companion->Load(LoadMode);
        }
    }

    namespace
    {
        // The per-instance body FUN_00425480 runs twice (0x004254a1..0x004254fe for the
        // instance, 0x00425521..0x00425591 for each related instance): renderer slot 8 (+0x20)
        // yields a count, then for every index from the last down renderer slot 10 (+0x28),
        // and the instance's IMatHookUp (IEngine slot 30 +0x78 QueryInstance) gets its slot 3
        // (+0x0c) and is released (+0x08). The out pointer is zeroed before each query
        // (0x004254d2 / 0x00425565); the count is zeroed before slot 8 (0x00425494 / 0x00425527).
        void HookUpInstanceMaterials(void* Instance)
        {
            int Count = 0;
            DALib::Renderer->GetInstanceLodCount(Instance, &Count);
            for (int Index = 0; Index < Count; ++Index)
            {
                DALib::Renderer->SetInstanceLod(Instance, Count - Index - 1);
                void* Hook = nullptr;
                if (DALib::Engine->QueryInstance(Instance, Iids::MatHookUp, &Hook) >= 0)
                {
                    IMatHookUp* const MatHook = static_cast<IMatHookUp*>(Hook);
                    MatHook->HookUpMaterials();
                    MatHook->Release();
                }
            }
        }
    }

    void HookUpInstanceTreeMaterials(long EngineIndex)
    {
        // FUN_00425480 (cdecl).
        void* const Instance = reinterpret_cast<void*>(EngineIndex);
        HookUpInstanceMaterials(Instance);
        // 0x0042550f / 0x004255a1: IEngine slot 54 (+0xd8) GetRelatedInstance(Instance, 1,
        // cursor), starting from -1 and ending at -1.
        void* const kNoInstance = reinterpret_cast<void*>(-1);
        void* Related = DALib::Engine->GetRelatedInstance(Instance, 1, kNoInstance);
        while (Related != kNoInstance)
        {
            HookUpInstanceMaterials(Related);
            Related = DALib::Engine->GetRelatedInstance(Instance, 1, Related);
        }
    }
}
