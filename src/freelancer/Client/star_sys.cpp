// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Retail E:\FL\Scratch\Source\Client\space\StarSys.cpp (+ the StarSphere.h background object),
// freelancer.exe .text 0x00552b60..0x00555760. See star_sys.h for the globals map.

#include "star_sys.h"

#include "cli_asteroid.h"
#include "cli_nebula.h"
#include "client_cliobj.h"
#include "client_gameobj.h"
#include "client_mesh_load.h"
#include "client_objdb.h"
#include "engine_light.h"
#include "game_options.h"
#include "globals.h"
#include "menu_sound_arch.h"
#ifdef FREELANCER_RP9
#include "nebula_volume.h" // NebulaVolume::RegisterNebula (port-only)
#endif
#include "player_behavior.h"
#include "player_spawn.h"
#include "space_dust.h"
#include "space_scene_obj_draw.h"
#include "space_solar_world.h"
#include "star_sphere.h" // StarSphereDestroyAll (FUN_0050c6e0)
#include "texture_panels.h"
#include "texture_shapes.h"
#include "tradelane_fx.h"
#include "voice_profiles.h"

#include "DALib.h"
#include "common_casteroid.h"
#include "common_cequip.h"
#include "common_cobject.h"
#include "common_id.h"
#include "common_iface.h"
#include "common_ini.h"
#include "common_misc.h"
#include "common_physys.h"
#include "common_ivp_real_object.h" // IVP_Real_Object::client_data (+0xa4), the collision filter
#include "common_universe.h"
#include "dacom.h"
#include "dacom_iids.h"
#include "engine/engbase.h"
#include "render/IFLAppearance.h"
#include "soundmanager.h"

#include <cstring>
#include <list>

namespace
{
    char const* const kStarSysFile = "E:\\FL\\Scratch\\Source\\Client\\space\\StarSys.cpp";
    char const* const kStarSphereFile = "E:\\FL\\Scratch\\Source\\Client\\space\\StarSphere.h";

    // {voice, msg} pair of the [Archetype] voice = list (node payload 8 bytes, FUN_00553590).
    struct FStarSysVoiceLine
    {
        unsigned VoiceId;
        unsigned MsgId;
    };

    // DAT_00679870 -- roots still loaded when the previous system was left (FUN_00554e20).
    std::list<Archetype::Root*> G_KeepAliveRoots;
    // DAT_0067987c -- [Archetype] voice = pairs.
    std::list<FStarSysVoiceLine> G_PreloadVoices;
    // DAT_00679888 -- [Archetype] snd = ids.
    std::list<unsigned> G_PreloadSounds;
    // DAT_00679894 -- [Archetype] archetype preloads.
    std::list<Archetype::Root*> G_PreloadArchetypes;
    // DAT_006798a0 -- [Object] archetype= preloads (FUN_005547c0), released once the INI loaded.
    std::list<Archetype::Root*> G_ObjectArchetypes;
    // DAT_006798ac -- the [TexturePanels] shape DB (static ctor LAB_00552e40, dtor LAB_00552e60).
    FShapeDB G_StarSysShapeDB;
    // DAT_006798c4 -- the nebulae of the current system.
    std::list<FNebulaEffect*> G_Nebulae;

    // DAT_006798d0..e4 -- the scripted music override (FUN_00553340 / FUN_00553380).
    // Retail .bss (no static initializer writes d0/d4/6c); FUN_005545a0 stores 1 / -1.0f /
    // 1000.0f at 00554643 / 0055464a / 00554676 on every system enter.
    unsigned char G_MusicOverrideNoFade = 0; // DAT_006798d0
    float G_MusicOverrideFade = 0.0f;        // DAT_006798d4
    unsigned G_MusicOverrideTrack = 0;       // DAT_006798d8
    unsigned G_MusicOverrideBattle = 0;      // DAT_006798dc
    unsigned G_MusicOverrideDanger = 0;      // DAT_006798e0
    unsigned G_MusicOverrideSpace = 0;       // DAT_006798e4
    // DAT_006798e8..f4 -- the system's own tracks ([Music] battle/danger/space; e8 is only
    // ever zeroed).
    unsigned G_MusicSystemOverride = 0;      // DAT_006798e8
    unsigned G_MusicSystemBattle = 0;        // DAT_006798ec
    unsigned G_MusicSystemDanger = 0;        // DAT_006798f0
    unsigned G_MusicSystemSpace = 0;         // DAT_006798f4

    // DAT_00679954 -- the current system.
    Universe::ISystem const* G_System = nullptr;

    // DAT_00679958..70 -- music state machine.
    int G_MusicState = 0;                    // DAT_00679958
    unsigned G_MusicTrack = 0;               // DAT_0067995c
    unsigned char G_bMusicCrossFading = 0;   // DAT_00679960
    float G_MusicCrossFadeTime = 0.0f;       // DAT_00679964
    unsigned char G_bMusicBattleHit = 0;     // DAT_00679968
    float G_MusicDangerScanTime = 0.0f;      // DAT_0067996c
    unsigned char G_bMusicDanger = 0;        // DAT_00679970

    // DAT_00679978 -- player collisions are not reported until this client time.
    double G_PlayerCollisionsSuppressedUntil = 0.0;

    // DAT_006140b4..b6 -- [System] space_color (.data initial {0, 0, 0x40}).
    unsigned char G_SpaceColor[3] = {0x00, 0x00, 0x40};

    // FUN_0054baf0 hands out the player's IObjRW; the CObject behind it is IObjInspectImpl's
    // CBase::Object (retail `ADD EAX,0xc` / `MOV ECX,[EAX+4]`).
    IObjRW* PlayerObjRW()
    {
        return static_cast<IObjRW*>(GetPlayerObjRW());
    }

    CObject* PlayerCObject(IObjRW* Player)
    {
        return static_cast<IObjInspectImpl*>(Player)->Object;
    }

    // FUN_005416c0(id, 0) -- the client ObjDB lookup (ClientObjDb_FindObject).
    IObjRW* FindClientObject(unsigned Id)
    {
        return ClientObjDb_FindObject(Id, 0);
    }

    // ------------------------------------------------------------ FUN_00553100
    // Pin one [Object] archetype of the system file: resolve it by kind (0 simple, 2 solar,
    // 4 asteroid), build the companion's material cache, AddRef, remember it.
    void PreloadObjectArchetype(char const* Name, int Kind)
    {
        // A miss on a solar/asteroid lookup, and an unknown Kind, leave the NAME pointer in the
        // register that is then used as the archetype (00553163 / 005531b4 reload ESI from the
        // argument). Retail dereferences it; the port reproduces that exactly.
        Archetype::Root* Arch =
            reinterpret_cast<Archetype::Root*>(const_cast<char*>(Name));
        if (Kind == 0)
        {
            ID_String Nick;
            Nick.Id = CreateID(Name);
            Arch = Archetype::GetSimple(Archetype::GetSimpleID(Nick));
        }
        else if (Kind == 2)
        {
            ID_String Nick;
            Nick.Id = CreateID(Name);
            unsigned const Id = Archetype::GetSolarID(Nick);
            if ((Id & 0x3fffffffu) != 0)
                Arch = Archetype::GetSolar(Id);
            else
                FDUMP(0x100001, "%s(%d) : *** ERROR: unknown Solar archetype (%s)\n",
                      kStarSysFile, 0x8a, Name);
        }
        else if (Kind == 4)
        {
            ID_String Nick;
            Nick.Id = CreateID(Name);
            unsigned const Id = Archetype::GetAsteroidID(Nick);
            if ((Id & 0x3fffffffu) != 0)
                Arch = Archetype::GetAsteroid(Id);
            else
                FDUMP(0x100001, "%s(%d) : *** ERROR: unknown Asteroid archetype (%s)\n",
                      kStarSysFile, 0x9b, Name);
        }

        Archetype::CliObjPreloadMaterials(Arch->Companion, false); // FUN_00407310(0)
        Arch->AddRef();
        G_ObjectArchetypes.push_back(Arch);
    }

    // ------------------------------------------------------------ FUN_00553590
    void PreloadVoiceLine(unsigned VoiceId, unsigned MsgId)
    {
        FVoiceProfile* const Voice = LookupClientVoice(VoiceId);
        if (Voice == nullptr)
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: Voice %u is invalid\n", kStarSysFile, 0x2d1,
                  VoiceId);
            return;
        }
        FVoiceMsg* const Msg = Voice->FindMessage(MsgId); // slot 5 (+0x14)
        if (Msg == nullptr)
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: Voice message %u is invalid for voice %u\n",
                  kStarSysFile, 0x2cc, MsgId, VoiceId);
            return;
        }
        Msg->Acquire(); // slot 3 (+0x0c)
        FStarSysVoiceLine Line;
        Line.VoiceId = VoiceId;
        Line.MsgId = MsgId;
        G_PreloadVoices.push_back(Line);
    }

    // ------------------------------------------------------------ FUN_005547c0
    // Walk the system file's [Object] blocks and pin every archetype= as a solar.
    void PreloadSystemObjectArchetypes(unsigned const* SystemId)
    {
        char Path[0x104];
        Universe::get_filename(Path, Universe::get_system(*SystemId)->GetFile());

        INI_Reader Reader;
        if (Reader.open(Path, false))
        {
            while (Reader.read_header())
            {
                if (!Reader.is_header("Object"))
                    continue;
                while (Reader.read_value())
                {
                    if (Reader.is_value("archetype"))
                        PreloadObjectArchetype(Reader.get_value_string(), 2);
                }
            }
        }
    }

    // Copy Src into a TString<N>-shaped buffer: at most N-1 characters, always terminated;
    // a null source gives the empty string (00553f0a..00553f43 / 00553f71..00553fa2).
    void CopyTruncated(char* Dest, unsigned Capacity, char const* Src)
    {
        if (Src == nullptr)
        {
            Dest[0] = '\0';
            return;
        }
        unsigned Len = static_cast<unsigned>(strlen(Src));
        if (Len >= Capacity)
            Len = Capacity - 1;
        memcpy(Dest, Src, Len);
        Dest[Len] = '\0';
    }

    unsigned char FloatToByte(float Value)
    {
        // MSVCRT ftol truncates; the low byte is stored.
        return static_cast<unsigned char>(static_cast<int>(Value));
    }

    // ------------------------------------------------------------ FUN_00553740
    // Load the system INI. Retail parses the file in one pass, section by section.
    void StarSysLoadIni(char const* Path)
    {
        ClientObjDb_FreeSpatial(); // 00553763: FUN_00540e90
        SpaceResetFarClip();       // FUN_00410130

        bool bAsteroidsLoaded = false; // [ESP+0x13], once per call

        INI_Reader Reader;
        if (!Reader.open(Path, false))
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: unknown system (%s)\n", kStarSysFile, 0x446,
                  Path);
            ClientObjDb_RebuildSpatial(); // LAB_00554260 falls into 00554280 (no close)
            return;
        }

        while (Reader.read_header())
        {
            if (Reader.is_header("System") || Reader.is_header("SystemInfo"))
            {
                while (Reader.read_value())
                {
                    if (Reader.is_value("space_color"))
                    {
                        G_SpaceColor[0] = FloatToByte(Reader.get_value_float(0));
                        G_SpaceColor[1] = FloatToByte(Reader.get_value_float(1));
                        G_SpaceColor[2] = FloatToByte(Reader.get_value_float(2));
                    }
                    else if (Reader.is_value("space_farclip"))
                    {
                        SpaceSetFarClip(Reader.get_value_float(0)); // FUN_00410150
                    }
                }
            }
            else if (Reader.is_header("Music"))
            {
                while (Reader.read_value())
                {
                    if (Reader.is_value("space"))
                        G_MusicSystemSpace = CreateID(Reader.get_value_string());
                    else if (Reader.is_value("danger"))
                        G_MusicSystemDanger = CreateID(Reader.get_value_string());
                    else if (Reader.is_value("battle"))
                        G_MusicSystemBattle = CreateID(Reader.get_value_string());
                    else if (Reader.is_value("explore"))
                        FDUMP(0x100002,
                              "%s(%d) : *** WARNING: explore music is obsolete in system file "
                              "%s, valid types are space, danger and battle",
                              kStarSysFile, 0x3c1, Path);
                }
            }
            else if (Reader.is_header("Archetype"))
            {
                while (Reader.read_value())
                {
                    char const* const Value = Reader.get_value_string();
                    if (Reader.is_value("snd"))
                    {
                        ID_String Snd;
                        Snd.Id = CreateID(Value);
                        PreloadClientSoundArch(&Snd); // FUN_00427a10(&id, 1)
                        G_PreloadSounds.push_back(CreateID(Value));
                        continue;
                    }
                    if (Reader.is_value("voice"))
                    {
                        unsigned const VoiceId = CreateID(Reader.get_value_string(0));
                        PreloadVoiceLine(VoiceId, CreateID(Reader.get_value_string(1)));
                        continue;
                    }

                    ID_String Nick;
                    Nick.Id = CreateID(Value);
                    Archetype::Root* Arch = nullptr;
                    if (Reader.is_value("ship"))
                        Arch = Archetype::GetShipByName(Nick);
                    else if (Reader.is_value("equipment"))
                        Arch = Archetype::GetEquipmentByName(Nick);
                    else if (Reader.is_value("simple"))
                        Arch = Archetype::GetSimpleByName(Nick);
                    else if (Reader.is_value("solar"))
                        Arch = Archetype::GetSolarByName(Nick);
                    else if (Reader.is_value("asteroid"))
                        Arch = Archetype::GetAsteroidByName(Nick);
                    else
                        FDUMP(0x100002,
                              "%s(%d) : *** WARNING: unknown (or old-style) preload archetype\n"
                              "  (%s)",
                              kStarSysFile, 0x315, Reader.get_line_ptr());

                    if (Arch != nullptr)
                    {
                        Arch->load(); // Root slot 3
                        if (Arch->Companion != nullptr)
                            Arch->Companion->Load(1); // ICliObj slot 8
                        G_PreloadArchetypes.push_back(Arch); // FUN_005556b0
                    }
                    else
                    {
                        // 00553b07: flags from FUN_00411c50(0x10000, 2) = 0x100002.
                        FDUMP(0x100002, "%s(%d) : *** WARNING: Failed to preload archetype\n  %s",
                              kStarSysFile, 0x321, Reader.get_line_ptr());
                    }
                }
            }
            else if (Reader.is_header("Ambient"))
            {
                while (Reader.read_value())
                {
                    if (Reader.is_value("color"))
                    {
                        G_StarSysAmbientRgb[0] = FloatToByte(Reader.get_value_float(0));
                        G_StarSysAmbientRgb[1] = FloatToByte(Reader.get_value_float(1));
                        G_StarSysAmbientRgb[2] = FloatToByte(Reader.get_value_float(2));
                    }
                }
            }
            else if (Reader.is_header("Background"))
            {
                FStarSphereBackground& Bg = G_StarSphereBackground;
                Bg.Layers[1].Name.Value = StringAlloc("", false); // DAT_00679924
                Bg.Layers[2].Name.Value = StringAlloc("", false); // DAT_00679938
                Bg.Layers[0].Name.Value = StringAlloc("", false); // DAT_00679910
                while (Reader.read_value())
                {
                    if (Reader.is_value("basic_stars"))
                        Bg.Layers[1].Name.Value = StringAlloc(Reader.get_value_string(), false);
                    else if (Reader.is_value("complex_stars"))
                        Bg.Layers[2].Name.Value = StringAlloc(Reader.get_value_string(), false);
                    else if (Reader.is_value("nebulae"))
                        Bg.Layers[0].Name.Value = StringAlloc(Reader.get_value_string(), false);
                    else if (Reader.is_value("file"))
                        Bg.Layers[2].Name.Value = StringAlloc(Reader.get_value_string(), false);
                }
            }
            else if (Reader.is_header("Dust"))
            {
                CacheString Dust;
                Dust.clear();
                while (Reader.read_value())
                {
                    if (Reader.is_value("spacedust"))
                        Dust.Value = StringAlloc(Reader.get_value_string(), false);
                }
                // 00553d6d: FUN_0054bde0(&Dust).
                SpaceDustSetSystemDust(&Dust);
            }
            else if (Reader.is_header("LightSource"))
            {
                void* const Mem = GameLightHeapAlloc(0x12c); // FUN_00537a90
                FGameLight* Light = nullptr;
                if (Mem != nullptr)
                    Light = GameLightConstruct(static_cast<FGameLight*>(Mem)); // FUN_005374e0
                GameLightLoad(Light, &Reader);                               // FUN_00537a40
                GameLightRelease(Light);                                     // FUN_00537ad0
            }
            else if (Reader.is_header("Object"))
            {
                SpawnWorldSolarFromObjectSection(Reader); // FUN_00548a20
            }
            else if (Reader.is_header("TexturePanels"))
            {
                while (Reader.read_value())
                {
                    if (Reader.is_value("file"))
                        G_StarSysShapeDB.Load(GetFullFilename(Reader.get_value_string()),
                                              nullptr); // FUN_00425b30
                }
            }
            else if (Reader.is_header("Asteroids"))
            {
                if (!bAsteroidsLoaded)
                {
                    CmnAsteroid::Load(G_System != nullptr ? G_System->GetId() : 0);
                    bAsteroidsLoaded = true;
                }
                char File[0x100];  // TString<256>
                char Zone[0x40];   // TString<64>
                File[0] = '\0';
                Zone[0] = '\0';
                while (Reader.read_value())
                {
                    if (Reader.is_value("file"))
                        CopyTruncated(File, sizeof(File), Reader.get_value_string());
                    else if (Reader.is_value("zone"))
                        CopyTruncated(Zone, sizeof(Zone), Reader.get_value_string());
                }
                CliAsteroidCreateAndLoad(File, Zone); // FUN_0051ff50
            }
            else if (Reader.is_header("Nebula"))
            {
                char File[0x100];  // TString<256>
                char Zone[0x40];   // TString<64>
                File[0] = '\0';
                Zone[0] = '\0';
                FNebulaEffect* Nebula = nullptr;
                while (Reader.read_value())
                {
                    if (Reader.is_value("file"))
                    {
                        // Every file= creates a new nebula (FUN_00509b00); an earlier one of the
                        // same block is dropped without being freed, as in retail.
                        CopyTruncated(File, sizeof(File), Reader.get_value_string());
                        Nebula = NebulaCreate();
                    }
                    else if (Reader.is_value("zone"))
                    {
                        CopyTruncated(Zone, sizeof(Zone), Reader.get_value_string());
                    }
                }
                if (Nebula != nullptr)
                {
                    using FLoad = void(__thiscall*)(FNebulaEffect*, char const*, char const*);
                    Nebula->Load(File, Zone); // slot 0
#ifdef FREELANCER_RP9
                    // Port-only: the file name for volumetric.ini [NebulaOverride] file=.
                    NebulaVolume::RegisterNebula(Nebula, File);
#endif
                    G_Nebulae.push_back(Nebula);
                }
            }
        }
        Reader.close();
        ClientObjDb_RebuildSpatial(); // 00554280: FUN_00540eb0
    }

    // ------------------------------------------------------------ FUN_00554ce0
    // Drop the [Archetype] preloads of the system being left.
    void ReleaseSystemPreloads()
    {
        for (std::list<Archetype::Root*>::iterator It = G_PreloadArchetypes.begin();
             It != G_PreloadArchetypes.end(); ++It)
            (*It)->Release();
        G_PreloadArchetypes.clear();

        for (std::list<unsigned>::iterator It = G_PreloadSounds.begin();
             It != G_PreloadSounds.end(); ++It)
        {
            ID_String Snd;
            Snd.Id = *It;
            UnloadClientSoundArch(&Snd); // FUN_00427a40
        }
        G_PreloadSounds.clear();

        for (std::list<FStarSysVoiceLine>::iterator It = G_PreloadVoices.begin();
             It != G_PreloadVoices.end(); ++It)
        {
            FVoiceProfile* const Voice = LookupClientVoice(It->VoiceId);
            if (Voice == nullptr)
            {
                FDUMP(0x100002, "%s(%d) : *** WARNING: Voice %u is invalid\n", kStarSysFile,
                      0x354, It->VoiceId);
                continue;
            }
            FVoiceMsg* const Msg = Voice->FindMessage(It->MsgId);
            if (Msg != nullptr)
                Msg->Release(); // slot 4 (+0x10)
            else
                FDUMP(0x100002,
                      "%s(%d) : *** WARNING: Voice message %u is invalid for voice %u\n",
                      kStarSysFile, 0x34f, It->MsgId, It->VoiceId);
        }
        G_PreloadVoices.clear();
    }

    // ------------------------------------------------------------ FUN_00554e20
    // The Archetype::Enum* callback of the leave path: keep every loaded root alive across the
    // system switch (released again by the next FUN_005545a0).
    struct FKeepLoadedRoots : Archetype::ArchEnumerator
    {
        bool Enumerate(Archetype::Root* Record) override
        {
            if (Record->LoadCount > 0)
            {
                Record->AddRef();
                G_KeepAliveRoots.push_back(Record);
            }
            return true;
        }
    };

    // ------------------------------------------------------------ LAB_005544e0
    // PhySys::m_pCollisionFilter while in a system: in multiplayer, collisions that involve the
    // local player's own objects are ignored until DAT_00679978.
    IVP_BOOL __cdecl StarSysCollisionFilter(IVP_Real_Object* A, IVP_Real_Object* B)
    {
        if (SinglePlayer())
            return IVP_TRUE;
        if (A == nullptr || B == nullptr)
            return IVP_TRUE;

        // 00554506 / 0055450e: IVP_Real_Object::client_data (+0xa4), the CObject back-pointer.
        CObject* ObjA = (CObject*)A->client_data;
        CObject* ObjB = (CObject*)B->client_data;

        if (ObjA != nullptr && (static_cast<unsigned>(ObjA->ObjectClass) & 0x67u) == 0x67u)
            return IVP_TRUE;
        if (ObjB != nullptr && (static_cast<unsigned>(ObjB->ObjectClass) & 0x67u) == 0x67u)
            return IVP_TRUE;

        if (ObjA != nullptr && (static_cast<unsigned>(ObjA->ObjectClass) & 3u) != 3u)
            ObjA = nullptr;
        if (ObjB != nullptr && (static_cast<unsigned>(ObjB->ObjectClass) & 3u) != 3u)
            ObjB = nullptr;

        unsigned const OwnerA = ObjA != nullptr ? static_cast<CSimple*>(ObjA)->OwnerPlayer : 0;
        unsigned const OwnerB = ObjB != nullptr ? static_cast<CSimple*>(ObjB)->OwnerPlayer : 0;
        unsigned const Self = GetPlayerConnectionId(); // DAT_00673344
        if (OwnerA != Self && OwnerB != Self)
            return IVP_FALSE;

        // 0055457f: FCOMP / TEST AH,0x41 / JP -- TRUE once now > DAT_00679978 (or unordered).
        if (GetClientElapsedTime() <= G_PlayerCollisionsSuppressedUntil)
            return IVP_FALSE;
        return IVP_TRUE;
    }

    // ------------------------------------------------------------ FUN_00555240
    // Is anything on the player's scanner hostile to the player?
    bool ScannerSeesHostile()
    {
        IObjRW* const Player = PlayerObjRW();
        if (Player == nullptr)
            return false;
        CObject* const Obj = PlayerCObject(Player);
        if (Obj == nullptr)
            return false;
        if ((static_cast<unsigned>(Obj->ObjectClass) & 0x503u) != 0x503u)
            return false;

        CEScanner* const Scanner =
            static_cast<CEScanner*>(static_cast<CEqObj*>(Obj)->EquipManager.FindFirst(0x80000));
        if (Scanner == nullptr)
            return false;

        for (int I = 0; I < Scanner->Scans.Count; ++I)
        {
            Watchable const* const Watched = Scanner->Scans.Entries[I].target();
            if (Watched == nullptr)
                continue;
            // The watcher points at the Watchable sub-object at IObjRW+8.
            IObjRW const* const Target = reinterpret_cast<IObjRW const*>(
                reinterpret_cast<unsigned char const*>(Watched) - 8);
            if (Target == nullptr)
                continue;
            // 005552a1..005552a8: PUSH target, PUSH player -> FUN_0045a490(player, target):
            // the target's attitude towards the player.
            if (GetAttitudeClass(PlayerObjRW(), Target) == -1)
                return true;
        }
        return false;
    }

    // ------------------------------------------------------------ FUN_00552fd0
    // Load one background layer's mesh when it is not bound yet.
    void StarSphereLoadLayer(FStarSphereLayer* Layer)
    {
        if (!ClientMeshHolderHasInstance(&Layer->Holder))
        {
            if (ClientMeshHolderLoad(&Layer->Holder, Layer->Name.Value, nullptr)) // 00420dd0
            {
                float Extent = 0.0f;
                float Center[3] = {0.0f, 0.0f, 0.0f};
                DALib::Engine->GetInstanceExtent(Layer->Holder.EngineInstance, 0, &Extent,
                                                 Center);
                ClientMeshHolderSetPosition(&Layer->Holder, Center); // 00420fd0
                ClientMeshHolderUpdate(&Layer->Holder);              // 00421070
            }
            if (!ClientMeshHolderHasInstance(&Layer->Holder))
                FDUMP(0x100001, "%s(%d) : *** ERROR: couldnt create starsphere %s",
                      kStarSphereFile, 0x35, Layer->Name.Value);
        }
        (void)ClientMeshHolderHasInstance(&Layer->Holder); // the return value is unused
    }

    // ------------------------------------------------------------ FUN_00552ea0
    void StarSphereRender(FStarSphereBackground* This, void* Camera)
    {
        if (GetGameOption(0x19) == 0)
            return;

        if (This->DesiredQuality != This->CurrentQuality)
        {
            switch (This->DesiredQuality)
            {
            case 1:
                StarSphereLoadLayer(&This->Layers[0]);
                ClientMeshHolderRelease(&This->Layers[1].Holder);
                ClientMeshHolderRelease(&This->Layers[2].Holder);
                break;
            case 2:
                StarSphereLoadLayer(&This->Layers[0]);
                StarSphereLoadLayer(&This->Layers[1]);
                ClientMeshHolderRelease(&This->Layers[2].Holder);
                break;
            case 3:
                StarSphereLoadLayer(&This->Layers[0]);
                ClientMeshHolderRelease(&This->Layers[1].Holder);
                StarSphereLoadLayer(&This->Layers[2]);
                break;
            default:
                ClientMeshHolderRelease(&This->Layers[0].Holder);
                ClientMeshHolderRelease(&This->Layers[1].Holder);
                ClientMeshHolderRelease(&This->Layers[2].Holder);
                break;
            }
            This->CurrentQuality = This->DesiredQuality;
        }

        IFLAppearance* Appearance = nullptr;
        if (DALib::Engine->QueryInterface(Iids::FLAppearance,
                                          reinterpret_cast<void**>(&Appearance)) == 0)
        {
            // Draw order: basic stars, complex stars, nebulae.
            if (ClientMeshHolderHasInstance(&This->Layers[1].Holder))
                Appearance->DrawStarsphere(Camera, This->Layers[1].Holder.EngineInstance);
            if (ClientMeshHolderHasInstance(&This->Layers[2].Holder))
                Appearance->DrawStarsphere(Camera, This->Layers[2].Holder.EngineInstance);
            if (ClientMeshHolderHasInstance(&This->Layers[0].Holder))
                Appearance->DrawStarsphere(Camera, This->Layers[0].Holder.EngineInstance);
        }
        if (Appearance != nullptr)
            Appearance->Release();
    }
}

// ------------------------------------------------------------------------ globals

unsigned char G_StarSysAmbientRgb[3] = {0, 0, 0};     // DAT_006798f8
unsigned char G_StarSysAmbientWorkRgb[3] = {0, 0, 0}; // DAT_0067986c

FStarSphereBackground G_StarSphereBackground; // DAT_00679900, static ctor LAB_00552b60

// ------------------------------------------------------------------------ StarSphere

FStarSphereBackground::FStarSphereBackground()
{
    // FUN_00552b80: each layer = FUN_00420d40 + CacheString::clear.
    for (int I = 0; I < 3; ++I)
    {
        MeshHolderConstruct(&Layers[I].Holder);
        Layers[I].Name.clear();
    }
    CurrentQuality = 0;
    DesiredQuality = 3;
}

FStarSphereBackground::~FStarSphereBackground()
{
    // FUN_00552c70: FUN_00420d10 on the layers, last first. That base destructor only resets
    // the helper's vptr; the instance is not destroyed here.
    // Real member destructors run in reverse order; ObjectHandle releases no instance.
}

void StarSphereDraw(void* Camera)
{
    // FUN_00552e70.
    if (GetGameOption(0x19) != 0 && GetGameOption(0x1a) != 0)
        StarSphereRender(&G_StarSphereBackground, Camera);
}

void StarSphereApplyDetail(float Value)
{
    // LAB_00552cd0: the thresholds are doubles (DAT_005debf8 0.75, DAT_005c75e8 0.5,
    // DAT_005c75f8 0.001); every step is FCOMP / TEST AH,0x41 / JNZ, i.e. strictly greater.
    double const V = static_cast<double>(Value);
    if (V > 0.75)
    {
        G_StarSphereBackground.DesiredQuality = 3;
        return;
    }
    if (V > 0.5)
    {
        G_StarSphereBackground.DesiredQuality = 2;
        return;
    }
    G_StarSphereBackground.DesiredQuality = 1;
    if (!(V > 0.001))
        G_StarSphereBackground.DesiredQuality = 0;
}

// ------------------------------------------------------------------------ StarSys

Universe::ISystem const* StarSysGetSystem()
{
    return G_System; // FUN_005556a0
}

unsigned StarSysGetActiveSystemId()
{
    // FUN_00554e90: tail-jump into ISystem slot 0.
    if (G_System == nullptr)
        return 0;
    return G_System->GetId();
}

std::list<FNebulaEffect*>& StarSysNebulaList()
{
    return G_Nebulae; // FUN_005553a0
}

FNebulaEffect* StarSysFindNebula(Vector const* Pos, float* OutFactor, FNebulaEffect* Hint)
{
    // FUN_005553b0.
    using FFactor = float(__thiscall*)(FNebulaEffect*, float*);
    float* const PosFloats = const_cast<float*>(reinterpret_cast<float const*>(Pos));
    if (GetGameOption(0x25) != 0 && GetGameOption(0x27) != 0)
    {
        if (Hint != nullptr)
        {
            float const Factor = Hint->ZoneExteriorFactor(PosFloats);
            if (Factor > 0.0f)
            {
                if (OutFactor != nullptr)
                    *OutFactor = Factor;
                return Hint;
            }
        }
        for (std::list<FNebulaEffect*>::iterator It = G_Nebulae.begin(); It != G_Nebulae.end();
             ++It)
        {
            FNebulaEffect* const Nebula = *It;
            float const Factor = Nebula->ZoneExteriorFactor(PosFloats);
            if (Factor > 0.0f)
            {
                if (OutFactor != nullptr)
                    *OutFactor = Factor;
                return Nebula;
            }
        }
    }
    if (OutFactor != nullptr)
        *OutFactor = 0.0f;
    return nullptr;
}

void StarSysGetFogColor(Vector const* Pos, unsigned char* OutRgb)
{
    // FUN_005552e0.
    float Factor = 0.0f;
    FNebulaEffect* const Nebula = StarSysFindNebula(Pos, &Factor, nullptr);
    if (Nebula != nullptr && Factor >= 1.0f)
    {
        float Rgb[3] = {0.0f, 0.0f, 0.0f};
        using FFill = void(__thiscall*)(FNebulaEffect*, float*);
        Nebula->FillFogRgb(Rgb); // slot 13 (+0x34)
        OutRgb[0] = FloatToByte(Rgb[0] * 255.0f);
        OutRgb[1] = FloatToByte(Rgb[1] * 255.0f);
        OutRgb[2] = FloatToByte(Rgb[2] * 255.0f);
        return;
    }
    OutRgb[0] = G_SpaceColor[0];
    OutRgb[1] = G_SpaceColor[1];
    OutRgb[2] = G_SpaceColor[2];
}

unsigned char const* StarSysSpaceColor()
{
    return G_SpaceColor; // FUN_00555460
}

unsigned char const* StarSysAmbientColor()
{
    // FUN_00555470: base + AMBIENT (option 0x3b), clamped at 0xff only (JG).
    int const Ambient = GetGameOption(0x3b);
    for (int I = 0; I < 3; ++I)
    {
        int const Sum = static_cast<int>(G_StarSysAmbientRgb[I]) + Ambient;
        G_StarSysAmbientWorkRgb[I] =
            Sum > 0xff ? static_cast<unsigned char>(0xff) : static_cast<unsigned char>(Sum);
    }
    return G_StarSysAmbientWorkRgb;
}

void StarSysEnter(unsigned SystemId)
{
    // FUN_005545a0.
    RefreshLoadingProgress();
    G_System = Universe::get_system(SystemId);
    if (G_System == nullptr)
        FDUMP(0x100001, "%s(%d) : *** ERROR: %s", kStarSysFile, 0x48b,
              "Player trying to ENTER invalid system");

    // Retail continues without a test: a null system faults on the next line.
    char Path[0x104];
    Universe::get_filename(Path, G_System->GetFile());

    G_MusicOverrideTrack = 0;
    G_MusicSystemOverride = 0;
    G_MusicOverrideBattle = 0;
    G_MusicSystemBattle = 0;
    G_MusicOverrideDanger = 0;
    G_MusicSystemDanger = 0;
    G_MusicOverrideSpace = 0;
    G_MusicSystemSpace = 0;
    G_MusicOverrideNoFade = 1;
    G_MusicOverrideFade = -1.0f;
    G_MusicState = 0;
    G_MusicTrack = 0;
    G_bMusicCrossFading = 0;
    G_MusicCrossFadeTime = 0.0f;
    G_bMusicBattleHit = 0;
    G_MusicDangerScanTime = 1000.0f;
    G_bMusicDanger = 0;

    PreloadSystemObjectArchetypes(&SystemId); // FUN_005547c0

    PhySys::m_pCollisionFilter = &StarSysCollisionFilter; // LAB_005544e0
    PhySys::Startup(SystemId);
    RefreshLoadingProgress();

    StarSysLoadIni(Path); // FUN_00553740
    RefreshLoadingProgress();

    for (std::list<Archetype::Root*>::iterator It = G_ObjectArchetypes.begin();
         It != G_ObjectArchetypes.end(); ++It)
        (*It)->Release();
    G_ObjectArchetypes.clear();
    RefreshLoadingProgress();

    IObjRW* const Player = PlayerObjRW();
    if (Player != nullptr)
    {
        CObject* const Obj = PlayerCObject(Player);
        if (Obj != nullptr && (static_cast<unsigned>(Obj->ObjectClass) & 0x103u) == 0x103u)
            static_cast<CSimple*>(Obj)->connect(GetClientObjDb()); // vt+0x90
    }

    for (std::list<Archetype::Root*>::iterator It = G_KeepAliveRoots.begin();
         It != G_KeepAliveRoots.end(); ++It)
        (*It)->Release();
    G_KeepAliveRoots.clear();
}

void StarSysLeave(IObjRW* Player)
{
    // FUN_00554ab0. Not all of it is portable yet; every missing callee is named in place.
    if (Player != nullptr)
    {
        FKeepLoadedRoots Keep; // vtable PTR_FUN_005e1210 -> FUN_00554e20
        Archetype::EnumSimples(&Keep);
        Archetype::EnumShips(&Keep);
        Archetype::EnumSolars(&Keep);
        Archetype::EnumAsteroids(&Keep);
        Archetype::EnumDynamicAsteroids(&Keep);
        Archetype::EnumEquipment(&Keep);
    }

    // 00554b21..00554b64: a local list (vtable 005e11c0, FUN_005afe60 node alloc) holding Player
    // -- pushed even when Player is null -- handed to FUN_00541210 below. The Gameobj list
    // entries are the IObjRW pointers themselves (IObjRW is the Gameobj's first base).
    std::list<FClientGameobj*> KeepObjects;
    KeepObjects.push_back(static_cast<FClientGameobj*>(Player));
    // 00554b62..00554b6e: a non-null Player gets its vtable slot 100 (+0x190, LAB_00524e60 on
    // the Gameobj base: draw states cleared, +0x34 = 10.0f).
    if (Player != nullptr)
        static_cast<FClientGameobj*>(Player)->Slot190();
    RefreshLoadingProgress();
    // 00554b7e: FUN_005200b0 -- destroy every client asteroid field (FUN_005205d0 per field).
    CliAsteroidDestroyAll();
    if (!SinglePlayer())
        CmnAsteroid::Unload(StarSysGetActiveSystemId());
    RefreshLoadingProgress();
    TradeLaneShutdown(); // 00554bab: FUN_00515500
    // 00554bb0..00554bb5: FUN_00541210(&list) -- the ObjDB cleared except the list.
    ClientObjDb_ClearExcept(&KeepObjects);
    // 00554bbd: FUN_005382f0 (light grid reset); 00554bc2: FUN_005383c0 (every GameLight
    // detached and disabled, the still-referenced ones re-enabled).
    LightGridReset();
    GameLightLeaveSystem();
    PhySys::Shutdown(StarSysGetActiveSystemId());
    RefreshLoadingProgress();

    ClientMeshHolderRelease(&G_StarSphereBackground.Layers[0].Holder);
    ClientMeshHolderRelease(&G_StarSphereBackground.Layers[1].Holder);
    ClientMeshHolderRelease(&G_StarSphereBackground.Layers[2].Holder);
    G_StarSphereBackground.CurrentQuality = 0;
    RefreshLoadingProgress();

    SpaceDustShutdown(); // 00554c0b: FUN_0054be20
    RefreshLoadingProgress();
    StarSphereDestroyAll(); // 00554c12: FUN_0050c6e0

    for (std::list<FNebulaEffect*>::iterator It = G_Nebulae.begin(); It != G_Nebulae.end(); ++It)
        NebulaDestroy(*It); // FUN_00509b70: slot 22 (+0x58) with 1
    G_Nebulae.clear();

    TexturePanelsDestroyAll();  // FUN_0050c040
    G_StarSysShapeDB.Clear();   // FUN_00425a20
    ReleaseSystemPreloads();    // FUN_00554ce0
    RefreshLoadingProgress();
}

void StarSysLeaveWithoutPlayer()
{
    // FUN_00554e80: PUSH 0 / CALL FUN_00554ab0.
    StarSysLeave(nullptr);
}

void StarSysUpdate(float Dt)
{
    // FUN_00554eb0.
    void* const Camera = GetClientSoundListenerObject(); // FUN_0040f130
    if (Camera != nullptr)
    {
        SpaceDustUpdate(G_System, Camera); // 00554ecb: FUN_0054beb0(DAT_00679954, cam)
    }
    else
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: %s", kStarSysFile, 0x513,
              "No active camera available upon StarSys update");
    }

    if (GetGameOption(0x25) != 0)
    {
        // 0x00554f1a..0x00554f42: count DAT_006798cc above 0 (JBE), then every nebula of the
        // list DAT_006798c8 through its vtable slot 2 (+0x08) with (camera, dt).
        if (GetGameOption(0x27) != 0 && !G_Nebulae.empty())
        {
            using FNebulaUpdateSlot = void(__thiscall*)(FNebulaEffect*, void*, float);
            for (std::list<FNebulaEffect*>::iterator It = G_Nebulae.begin();
                 It != G_Nebulae.end(); ++It)
                (*It)->Update(Camera, Dt);
        }
        TexturePanelsUpdateAll(Camera, Dt); // FUN_00509f20 walk, slot 3 (+0x0c)
    }

    if (static_cast<int>(CliAsteroidRegistryCount()) > 0) // FUN_005201c0
    {
        if (!SinglePlayer() && GetGameOption(0x21) != 0)
            CmnAsteroid::Update(GetClientObjDb(), Dt);
        CliAsteroidUpdateFields(Camera, Dt); // FUN_00520130
    }

    // ---- music ----
    void* SourceRaw = nullptr;
    if (BindClientBackgroundMusic(&SourceRaw)) // FUN_00428ba0 (AddRef'd)
    {
        ISoundSource* const Source = static_cast<ISoundSource*>(SourceRaw);
        if (Source->IsFinished()) // slot 29 (+0x74)
            StarSysClearMusicOverride();
        Source->Release();
    }

    unsigned SpaceTrack = G_MusicOverrideSpace != 0 ? G_MusicOverrideSpace : G_MusicSystemSpace;
    unsigned const DangerTrack =
        G_MusicOverrideDanger != 0 ? G_MusicOverrideDanger : G_MusicSystemDanger;
    unsigned const BattleTrack =
        G_MusicOverrideBattle != 0 ? G_MusicOverrideBattle : G_MusicSystemBattle;
    unsigned const OverrideTrack =
        G_MusicOverrideTrack != 0 ? G_MusicOverrideTrack : G_MusicSystemOverride;

    if (G_MusicOverrideSpace == 0)
    {
        IObjRW* const Player = PlayerObjRW();
        if (G_System != nullptr && Player != nullptr)
        {
            // ISystem slot 12 (+0x30): the zone at the player's position with mask 0x400000;
            // its slot 29 (+0x74) music replaces the system's space track.
            Universe::IZone const* const Zone =
                G_System->GetSmallestZoneWithPropertiesAt(&Player->get_position(), 0x400000);
            if (Zone != nullptr)
            {
                ID_String const ZoneMusic = Zone->GetMusic();
                if (!ZoneMusic.IsEmpty())
                    SpaceTrack = ZoneMusic.Id;
            }
        }
    }

    bool const bWasBattle = G_MusicState == 2;
    bool const bBattleHit = G_bMusicBattleHit == 1;
    G_bMusicBattleHit = 0;

    bool bPlayerFlag = false;
    {
        void* const Player = GetPlayerObjRW();
        // 005550c4: the client Gameobj's +0x158 word (the jump / dock arm state read by
        // space_scene_obj_draw.cpp GameobjJumpArmState); FClientGameobj keeps it in OpaqueTail.
        if (Player != nullptr)
            bPlayerFlag = *reinterpret_cast<int*>(static_cast<unsigned char*>(Player) + 0x158) != 0;
    }

    // 0x005550d1..0x0055512c: the sum stays on the FPU -- FST stores it to DAT_00679964 as a
    // float, FCOMP compares the unrounded value with MUSIC_CROSS_FADE_DELAY (TEST AH,1 / JNZ:
    // below, or unordered, keeps fading; the reset runs only on ordered >=).
    bool bTimeCrossFade = false;
    double CrossFade = 0.0;
    if (bWasBattle && bBattleHit)
    {
        G_bMusicCrossFading = 1;
        CrossFade = 0.0 + static_cast<double>(Dt);
        bTimeCrossFade = true;
    }
    else if (G_bMusicCrossFading != 0)
    {
        CrossFade = static_cast<double>(G_MusicCrossFadeTime) + static_cast<double>(Dt);
        bTimeCrossFade = true;
    }
    if (bTimeCrossFade)
    {
        G_MusicCrossFadeTime = static_cast<float>(CrossFade);
        if (CrossFade >= static_cast<double>(MUSIC_CROSS_FADE_DELAY))
        {
            G_bMusicCrossFading = 0;
            G_MusicCrossFadeTime = 0.0f;
            G_MusicDangerScanTime = 1000.0f;
        }
    }

    if (OverrideTrack != 0)
        G_bMusicCrossFading = 0;
    else if (G_bMusicCrossFading != 0)
        return;

    // 0x0055514b..0x00555166: the same FST / FCOMP shape against 2.0f (DAT_005d55b4).
    double const ScanTime = static_cast<double>(G_MusicDangerScanTime) + static_cast<double>(Dt);
    G_MusicDangerScanTime = static_cast<float>(ScanTime);
    if (ScanTime >= 2.0)
    {
        G_bMusicDanger = ScannerSeesHostile() ? 1 : 0; // FUN_00555240
        G_MusicDangerScanTime = 0.0f;
    }

    int State = 0;
    if (bBattleHit)
        State = 2;
    else if (G_bMusicDanger != 0)
        State = 1;
    else
        State = bPlayerFlag ? 4 : 0;
    if (OverrideTrack != 0)
        State = 3;

    if (State != G_MusicState)
    {
        G_MusicState = State;
        G_bMusicCrossFading = 1;
        G_MusicCrossFadeTime = 0.0f;
    }

    unsigned Track = 0;
    switch (G_MusicState)
    {
    case 0:
        Track = SpaceTrack;
        break;
    case 1:
        Track = DangerTrack;
        break;
    case 2:
        Track = BattleTrack;
        break;
    case 3:
        Track = OverrideTrack;
        break;
    default:
        Track = 0; // 4 and anything else: silence
        break;
    }

    if (G_MusicTrack != Track)
    {
        SetClientBackgroundMusic(Track, G_MusicOverrideNoFade, G_MusicOverrideFade); // 004288e0
        G_MusicTrack = Track;
    }
}

bool StarSysHasVoiceMessage(unsigned VoiceId, unsigned MsgId)
{
    // FUN_00553660.
    for (std::list<FStarSysVoiceLine>::iterator It = G_PreloadVoices.begin();
         It != G_PreloadVoices.end(); ++It)
    {
        if (It->MsgId == MsgId && It->VoiceId == VoiceId)
            return true;
    }
    return false;
}

void StarSysPreloadVoiceMessage(unsigned VoiceId, unsigned MsgId)
{
    // FUN_00553590.
    PreloadVoiceLine(VoiceId, MsgId);
}

void StarSysSetMusicOverride(unsigned SpaceMusic, unsigned DangerMusic, unsigned BattleMusic,
                             unsigned OverrideMusic, float Fade, bool bNoFadeFlag)
{
    // FUN_00553340: the byte flag stores SETZ of the sixth argument.
    G_MusicOverrideSpace = SpaceMusic;
    G_MusicOverrideTrack = OverrideMusic;
    G_MusicOverrideDanger = DangerMusic;
    G_MusicOverrideBattle = BattleMusic;
    G_MusicOverrideFade = Fade;
    G_MusicOverrideNoFade = bNoFadeFlag ? 0 : 1;
}

void StarSysClearMusicOverride()
{
    // FUN_00553380 (the same six stores are inlined in FUN_00554eb0 at 00554fc3).
    G_MusicOverrideTrack = 0;
    G_MusicOverrideBattle = 0;
    G_MusicOverrideDanger = 0;
    G_MusicOverrideSpace = 0;
    G_MusicOverrideFade = -1.0f;
    G_MusicOverrideNoFade = 1;
}

void StarSysNoteDamage(IObjRW* Victim, void const* DamageRecord)
{
    // FUN_00553250.
    IObjRW* const Player = PlayerObjRW();
    if (Player == nullptr)
        return;
    unsigned const PlayerId = Player->get_id();
    unsigned const InflictorId =
        *reinterpret_cast<unsigned const*>(static_cast<unsigned char const*>(DamageRecord) + 0x14);

    bool bFlag = false;
    if (Victim->get_id() == PlayerId)
    {
        // 00553289..00553290: PUSH object, PUSH player -> FUN_0045a490(player, object).
        if (GetAttitudeClass(PlayerObjRW(), FindClientObject(InflictorId)) == -1)
            bFlag = true;
    }
    if (InflictorId == PlayerId)
    {
        if (GetAttitudeClass(PlayerObjRW(), Victim) == -1) // 005532a6..005532ad
        {
            G_bMusicBattleHit = 1;
            return;
        }
    }
    if (bFlag)
        G_bMusicBattleHit = 1;
}

void StarSysNoteTargetChange(unsigned ShipId)
{
    // FUN_005532d0.
    IObjRW* const Player = PlayerObjRW();
    if (Player == nullptr)
        return;
    if (ShipId != Player->get_id())
        return;
    IObjRW* const Go = FindClientObject(ShipId);
    if (Go == nullptr)
        return;
    CObject* const Obj = PlayerCObject(Go);
    if (Obj == nullptr)
        return;
    if ((static_cast<unsigned>(Obj->ObjectClass) & 0x503u) != 0x503u)
        return;
    IObjRW* const Target = static_cast<CShip*>(Obj)->get_target();
    if (Target == nullptr)
        return;
    if (GetAttitudeClass(PlayerObjRW(), Target) == -1) // FUN_0045a490(player, target)
        G_bMusicBattleHit = 1;
}

void StarSysSuppressPlayerCollisions(double Seconds)
{
    // FUN_005544d0.
    G_PlayerCollisionsSuppressedUntil = GetClientElapsedTime() + Seconds;
}

int GetAttitudeClass(IObjInspect const* Target, IObjInspect const* Viewer)
{
    // FUN_0045a490: -1 hostile (<= -0.6), 1 friendly (>= 0.6), 0 otherwise / on failure.
    if (Target == nullptr || Viewer == nullptr)
        return 0;
    float Attitude = 0.0f;
    if (Viewer->get_attitude_towards(Attitude, Target) != 0)
        return 0;
    // FCOMP -0.6f / TEST AH,0x41 / JP: -1 unless Attitude > -0.6 (or unordered).
    if (Attitude <= -0.6f)
        return -1;
    // FCOMP 0.6f / TEST AH,0x1 / JNZ: 0 when below (or unordered).
    if (!(Attitude >= 0.6f))
        return 0;
    return 1;
}
