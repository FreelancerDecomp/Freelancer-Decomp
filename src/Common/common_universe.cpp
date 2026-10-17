// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_universe.h"
#include "common_universe_internal.h"

#include "common_archetype.h"
#include "common_cobjdb.h"
#include "common_csys.h"
#include "common_id.h"
#include "common_ini.h"
#include "common_misc.h" // Geometry::Sphere / Frustum, SphereCull
#include "common_net.h"  // CommReferrable (SYSTEM's second base)
#include "common_reputation.h" // Reputation::Vibe static store (FreeStatic / Begin / EndStaticLoad)
#include "common_zone.h"   // ZONE, the concrete IZone

#include "DALib.h"
#include "dacom.h"
#include "readfile.h"

#include <list>
#include <map>
#include <math.h>
#include <new>
#include <stdlib.h>
#include <string.h>
#include <vector>

extern double DaysPerSecond;

using CmnUniverse::BASE;
using CmnUniverse::GSystemObjectPool;
using CmnUniverse::GSystemObjectPoolOwner;
using CmnUniverse::LoadSystemObjectPool;
using CmnUniverse::SYSTEM;

namespace
{
    const char kSystemsSource[] = "E:\\FL\\Scratch\\Source\\Common\\Systems.cpp";
    const unsigned long kPathChars = 0x104;
    const unsigned kValidIdMask = 0x3fffffff;

    int GUniverseRefCount = 0;
    IFileSystem* GUniverseDirFs = nullptr;


    // The universe object DAT_0658a828: the system map at +0x00, the base map at +0x14, the
    // zone map at +0x28 and the gate map at +0x3c. Retail keeps the records by value in the
    // map nodes: the loader builds each record on its stack, copies it into a {id, record}
    // pair, copies the pair into the node, and destroys the pair and the stack record
    // (FUN_06337250). The maps below do the same.
    std::map<unsigned, SYSTEM> GSystems;
    std::map<unsigned, BASE> GBases;
    std::map<unsigned, ZONE> GZones;
    std::map<unsigned, unsigned> GGateSystems;

    // DAT_0658a824 / DAT_0658a820: the GetFirst/GetNext walks. Retail keeps a bare node
    // pointer that starts as 0 and is written only by those four functions, so a GetNext
    // before any GetFirst dereferences it; a value-initialised iterator is the same null node.
    std::map<unsigned, SYSTEM>::iterator GSystemWalk;
    std::map<unsigned, BASE>::iterator GBaseWalk;

}

namespace CmnUniverse
{
    // The shared [Object] record pool of ISystem slots 15/16: DAT_0658a994..DAT_0658a9a0, a
    // VC6 vector of 0x118-byte records. Its static constructor LAB_06336330 and atexit
    // destructor LAB_06336360 are this std::vector's own construction and destruction.
    std::vector<Universe::FSystemObject> GSystemObjectPool;
    // DAT_0658a9b8: the SYSTEM whose file the pool currently holds. Retail never resets it.
    SYSTEM const* GSystemObjectPoolOwner = nullptr;
}

namespace
{


    // Desc layout is asserted once, in readfile.h (DOS/UTF prefix 0x34, MEM tail to 0x40).

    // Stack slots of FUN_06337250 that outlive one INI section, because retail never
    // re-initialises them per section: the stack SYSTEM's position (+0x74, ESP+0x1f4..0x1fc --
    // the constructor at 06337376-06337441 does not write it, so a [System] without pos /
    // real_pos keeps the previous block's) and the [Object] nickname kept in [ESP+0x20] / ESI
    // (06337f5b loads it at the start of every [Object], 06338114 stores it back, so an object
    // without a nickname key uses the previous object's). Their first values are whatever the
    // stack held (undefined behaviour in retail); the port starts both at zero.
    struct FUniverseLoadCarry
    {
        Vector SystemPos;
        unsigned ObjectNickname;
    };

    void LoadSystemFile(SYSTEM& System, char const* RelFile, FUniverseLoadCarry& Carry);

    void ReleaseUniverseDirFs()
    {
        if (GUniverseDirFs == nullptr)
            return;

        GUniverseDirFs->Release(); // +0x08
        GUniverseDirFs = nullptr;
    }

    // The four erase(begin, end) calls Startup (06337078-063370ee), FUN_06337250
    // (06337279-0633729f) and Shutdown (0633bd94-0633bee2) make, in that order: systems
    // (record destructors FreeSpatial the zone tree), bases, zones (~ZONE releases the mesh
    // handle), gates. Strings are StringCache pointers and are never freed.
    // Port: the walk iterators are reset too; retail leaves GetNextSystem / GetNextBase holding
    // an iterator into the erased nodes (undefined), the port's is end() instead.
    void ClearUniverseTables()
    {
        GSystemWalk = GSystems.end();
        GBaseWalk = GBases.end();
        GSystems.clear();
        GBases.clear();
        GZones.clear();
        GGateSystems.clear();
    }

    // Startup .text:06337169-063371f6: a DOS file system rooted at the universe directory.
    // DALib::Dacom is not null-checked (06337189 `TEST EDX,EDX; JNZ` falls into the call
    // either way); the result is the object on success, else 0 (NEG/SBB/NOT/AND).
    void CreateUniverseDirFs(char const* Directory)
    {
        FFileSystemDesc Desc;
        memset(&Desc, 0, sizeof(Desc));
        Desc.Size = 0x34;
        Desc.ComponentName = "FileSystem";
        Desc.Implementation = "DOS";
        Desc.Path = Directory;
        Desc.Access = 0x80000000;
        Desc.ShareMode = 1;
        Desc.Creation = 3;
        Desc.Flags = 0x8000080;
        Desc.Field30 = 0xFFFFFFFF;

        void* Object = nullptr;
        GENRESULT Result = DALib::Dacom->CreateInstance((AGGDESC*)&Desc, &Object);
        GUniverseDirFs = (Result == GR_OK) ? (IFileSystem*)Object : nullptr;
    }

    bool IdIsValid(unsigned NicknameId)
    {
        return (NicknameId & kValidIdMask) != 0;
    }

    void ReadTimeBlock(INI_Reader& Reader)
    {
        if (!Reader.read_value())
            return;

        do
        {
            if (Reader.is_value("seconds_per_day"))
            {
                // 06337347-06337355: FLD 1.0 / FDIV by the unrounded value, FSTP double.
                DaysPerSecond = 1.0 / Reader.get_value_float(0);
                break;
            }
        } while (Reader.read_value());
    }

    // FUN_06337250 .text:06337441-06338447: the value loop of one [System] block. The "file"
    // key parses that system's own ini on the spot (.text:063374b2-06338289), so the zones and
    // connections of a system are loaded in the middle of its [System] block, with whatever
    // nickname the block has set by then as their system id.
    void ReadSystemValues(INI_Reader& Reader, SYSTEM& Record, FUniverseLoadCarry& Carry)
    {
        if (!Reader.read_value())
            return;

        do
        {
            if (Reader.is_value("nickname"))
            {
                // 06337465-0633748a: CreateID, then StringAlloc(value, false).
                char const* Text = Reader.get_value_string();
                Record.NicknameId = CreateID(Text);
                Record.Nickname = StringAlloc(Text, false);
            }
            else if (Record.CommReferrable::read_comm_ini_data(Reader))
            {
                // .text:063374a5: msg_id_prefix -> CommReferrable::MsgIdPrefix.
            }
            else if (Reader.is_value("file"))
            {
                // 063374d2-063374ea: StringAlloc(value, false) into +0x70, then the system file
                // is read right here (063374e0-06338289).
                Record.File = StringAlloc(Reader.get_value_string(), false);
                LoadSystemFile(Record, Record.File, Carry);
            }
            else if (Reader.is_value("strid_name"))
            {
                Record.StridName = Reader.get_value_int(0);
            }
            else if (Reader.is_value("ids_info"))
            {
                Record.IdsInfo = Reader.get_value_int(0);
            }
            else if (Reader.is_value("visit"))
            {
                Record.Visit = (unsigned char)Reader.get_value_int(0);
            }
            else if (Reader.is_value("NavMapScale"))
            {
                Record.NavMapScale = Reader.get_value_float(0);
            }
            else if (Reader.is_value("links"))
            {
                // 06338371: the key is recognised and its value is not read.
            }
            else if (Reader.is_value("pos"))
            {
                // 06338393-063383eb: z is 0.0 (DAT_0639c430) when value 2 is empty.
                Record.Pos.x = Reader.get_value_float(0);
                Record.Pos.y = Reader.get_value_float(1);
                if (Reader.is_value_empty(2))
                    Record.Pos.z = 0.0f;
                else
                    Record.Pos.z = Reader.get_value_float(2);
            }
            else if (Reader.is_value("real_pos"))
            {
                Record.Pos.x = Reader.get_value_float(0);
                Record.Pos.y = Reader.get_value_float(1);
                Record.Pos.z = Reader.get_value_float(2);
            }
        } while (Reader.read_value());
    }

    // FUN_06337250 .text:06338451-0633861d, after the value loop. A valid id: the key comes
    // from slot 0 (06338470), the stack record is copied into a {key, SYSTEM} pair
    // (FUN_0633e020), the pair into the map (FUN_0633dea0, no overwrite), and the pair is
    // destroyed (FUN_0633aee0) before the collision error. An invalid id (mask 0x3fffffff)
    // only reports. The msg_id_prefix check runs either way. The caller destroys the stack
    // record afterwards (06338620-063386ad).
    void InsertSystem(SYSTEM& Record)
    {
        if (!IdIsValid(Record.NicknameId))
        {
            FDUMP(0x100001,
                  "%s(%d) : *** ERROR: Failed to insert system \"%s\" into system DB due to invalid ID.\n",
                  kSystemsSource, 0xb39, Record.Nickname);
        }
        else
        {
            bool bInserted = false;
            {
                std::pair<unsigned, SYSTEM> const Pair(Record.GetId(), Record);
                bInserted = GSystems.insert(Pair).second;
            }
            if (!bInserted)
            {
                FDUMP(0x100001, "%s(%d) : *** ERROR: %s", kSystemsSource, 0xb34,
                      "Failed to insert new system into system DB due to ID collision.\n");
            }
        }

        // .text:0633850b-0633861d: a system without msg_id_prefix is an error unless it is one
        // of the listed story/tutorial systems. Each REPE CMPSB compares strlen+1 bytes, i.e.
        // an exact match. "St03" is tested twice in retail's list.
        if (Record.CommReferrable::GetMsgIdPrefix().Length == 0)
        {
            static char const* const kSystemsWithoutPrefix[] = {
                "St01", "St02", "St03", "St02b", "St02c", "St03", "St03b", "Tut", "Ew05",
                "FP7_system", "Ew06",
            };
            char const* Name = Record.Nickname;
            bool bListed = false;
            unsigned Index = 0;
            for (; Index < sizeof(kSystemsWithoutPrefix) / sizeof(kSystemsWithoutPrefix[0]); ++Index)
            {
                if (strcmp(Name, kSystemsWithoutPrefix[Index]) == 0)
                {
                    bListed = true;
                    break;
                }
            }
            if (!bListed)
            {
                FDUMP(0x100001,
                      "%s(%d) : *** ERROR: System \"%s\" has no msg_id_prefix information, which it "
                      "will need to be referred to by space GCS - See TAYBRO",
                      kSystemsSource, 0xb49, Name);
            }
        }
    }

    struct FZoneShapeName
    {
        char const* Name;
        unsigned Value;
    };

    unsigned ParseZoneShape(char const* Text)
    {
        // listing 063362d0 / table 063edb80. Six {name,int} pairs, stricmp.
        static FZoneShapeName const kShapes[] = {
            {"sphere", Universe::ZoneShapeSphere},
            {"ellipsoid", Universe::ZoneShapeEllipsoid},
            {"BOX", Universe::ZoneShapeBox},
            {"CYLINDER", Universe::ZoneShapeCylinder},
            {"RING", Universe::ZoneShapeRing},
            {"MESH", Universe::ZoneShapeMesh},
        };

        if (Text == nullptr)
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: invalid ZoneShape \'%s\'\n", kSystemsSource,
                  0x24c, "");
            return Universe::ZoneShapeInvalid;
        }

        unsigned Index = 0;
        for (; Index < 6; ++Index)
        {
            if (_stricmp(Text, kShapes[Index].Name) == 0)
                return kShapes[Index].Value;
        }

        FDUMP(0x100002, "%s(%d) : *** WARNING: invalid ZoneShape \'%s\'\n", kSystemsSource, 0x24c,
              Text);
        return Universe::ZoneShapeInvalid;
    }

    // FUN_06337250 .text:06337527-0633753a (and FUN_06336750 .text:063367d6-063367e8): without
    // the universe directory file system the system file name is copied as it is, with an
    // unbounded byte loop into the 0x108-byte stack buffer. Port: the copy stops at the
    // buffer (a longer name is undefined behaviour in retail); the name is a StringAlloc
    // result, never null.
    void CopyRelPath(char* Dest, char const* RelFile)
    {
        unsigned Index = 0;
        for (;;)
        {
            char Copied = RelFile[Index];
            Dest[Index] = Copied;
            if (Copied == '\0')
                return;
            ++Index;
            if (Index >= kPathChars - 1)
            {
                Dest[Index] = '\0';
                return;
            }
        }
    }

    // FUN_06337250 .text:06337711-06337d77: the value loop of one [Zone] block, into the stack
    // ZONE (constructor .text:063375cd-06337710; the system id at 063376ed is set by the caller).
    void ReadZoneValues(INI_Reader& Reader, ZONE& Zone)
    {
        Universe::IZone* const Record = &Zone;
        if (!Reader.read_value())
            return;

        do
        {
            if (Reader.is_value("nickname") || Reader.is_value("name"))
            {
                // 06337d4d-06337d5f: only the id (ZONE +0x04); the text is not kept.
                Record->NicknameId = CreateID(Reader.get_value_string());
            }
            else if (Reader.is_value("pos"))
            {
                Record->Pos.x = Reader.get_value_float(0);
                Record->Pos.y = Reader.get_value_float(1);
                Record->Pos.z = Reader.get_value_float(2);
            }
            else if (Reader.is_value("rotate"))
            {
                // .text:063377bf-0633781c: the three floats through EulerMatrix, copied over
                // the orientation (REP MOVSD, 9 dwords).
                Vector Angles;
                Angles.x = Reader.get_value_float(0);
                Angles.y = Reader.get_value_float(1);
                Angles.z = Reader.get_value_float(2);
                Record->Orientation = EulerMatrix(Angles);
            }
            else if (Reader.is_value("shape"))
            {
                Record->Shape = ParseZoneShape(Reader.get_value_string(0));
            }
            else if (Reader.is_value("size"))
            {
                Record->Size.x = Reader.get_value_float(0);
                if (Reader.is_value_empty(1))
                    Record->Size.y = 0.0f;
                else
                    Record->Size.y = Reader.get_value_float(1);
                if (Reader.is_value_empty(2))
                    Record->Size.z = 0.0f;
                else
                    Record->Size.z = Reader.get_value_float(2);
            }
            else if (Reader.is_value("mesh"))
            {
                // .text:063378fe-06337912: Archetype::Load(get_value_string(0), &mesh handle,
                // null); the bool result is dropped.
                Archetype::Load(Reader.get_value_string(0), &Record->MeshHandle, nullptr);
            }
            else if (Reader.is_value("edge_fraction"))
            {
                // 06337933-063379b2: the unrounded value (re-read for every test) against
                // 0.0 (TEST 5 / JNP: below) and 1.0 (TEST 5 / JP: at or above, or unordered).
                double const Value = Reader.get_value_float(0);
                if (Value < 0.0)
                    Record->EdgeFraction = 0.0f;
                else if (Value < 1.0)
                    Record->EdgeFraction = (float)Value;
                else
                    Record->EdgeFraction = 1.0f;
            }
            else if (Reader.is_value("property_flags"))
            {
                Record->PropertyFlags |= (unsigned)Reader.get_value_int(0);
            }
            else if (Reader.is_value("property_fog_color"))
            {
                // 063379f3-06337a35: each value FMUL double DAT_0639a1b8 (1/255), FSTP float.
                double const kInv255 = 0.00392156862745098;
                Record->FogColor.x = (float)(Reader.get_value_float(0) * kInv255);
                Record->FogColor.y = (float)(Reader.get_value_float(1) * kInv255);
                Record->FogColor.z = (float)(Reader.get_value_float(2) * kInv255);
            }
            else if (Reader.is_value("drag_modifier"))
            {
                // 06337a5a-06337b01. FST float, then FCOMP float 1.0 (DAT_0639a1d0) on the
                // UNROUNDED value, TEST AH,5 / JP: below 1.0 falls into the error; >= 1.0 or
                // unordered jumps past it. The later tests read the stored float.
                double const Unrounded = Reader.get_value_float(0);
                float const Value = (float)Unrounded;
                if (Unrounded < 1.0)
                {
                    // FUN_062b7750(0x10000, 1) builds the flags 0x100001; get_file_name, then
                    // the zone id through ID_String::get_string ("0x..." text).
                    char const* const FileName = Reader.get_file_name();
                    ID_String ZoneId;
                    ZoneId.Id = Record->NicknameId;
                    FDUMP(0x100001,
                          "%s(%d) : *** ERROR: drag modifier should be >= 1.0  in zone (%s) in file (%s)",
                          kSystemsSource, 0x9cd, ZoneId.get_string(), FileName);
                }
                // 06337abd-06337ada: FCOMP double 1.0, JP (>= 1.0 or unordered) keeps the
                // value, below 1.0 stores 1.0.
                if (Value < 1.0f)
                    Record->DragModifier = 1.0f;
                else
                    Record->DragModifier = Value;
                // 06337ae1-06337af6: TEST AH,0x41 / JNZ skips the flag on below, equal or
                // unordered; only a value above 1.0 sets 0x80000.
                if (Value > 1.0f)
                    Record->PropertyFlags |= 0x80000;
            }
            else if (Reader.is_value("damage"))
            {
                // 06337b1b-06337b39: FST float, FCOMP 0.0f on the unrounded value, TEST AH,0x41
                // / JNZ skips the flag on <= 0 or unordered.
                double const Value = Reader.get_value_float(0);
                Record->Damage = (float)Value;
                if (Value > 0.0)
                    Record->PropertyFlags |= 0x40000;
            }
            else if (Reader.is_value("interference"))
            {
                // 06337b5e-06337b7c: FST float, FCOMP 0.0f on the unrounded value, TEST AH,0x44
                // / JNP skips the flag only when equal (unordered sets it).
                double const Value = Reader.get_value_float(0);
                Record->Interference = (float)Value;
                if (Value != 0.0)
                    Record->PropertyFlags |= 0x100000;
            }
            else if (Reader.is_value("music"))
            {
                Record->MusicId = CreateID(Reader.get_value_string());
                Record->PropertyFlags |= 0x400000;
            }
            else if (Reader.is_value("spacedust"))
            {
                // 06337be3-06337c05: StringAlloc(value, false) into the CacheString at +0x70.
                Record->Spacedust = StringAlloc(Reader.get_value_string(), false);
                Record->PropertyFlags |= 0x200000;
            }
            else if (Reader.is_value("spacedust_maxparticles"))
            {
                Record->SpacedustMaxParticles = Reader.get_value_int(0);
            }
            else if (Reader.is_value("faction"))
            {
                // FUN_06337250 .text:06337c4d-06337cac: MakeId(get_value_string(0)) zero-
                // extended, get_value_float(1), then the pair is inserted at the end of the
                // zone's population (FUN_0633de20 pair ctor, FUN_0633e570 insert(end)).
                unsigned int const FactionId = MakeId(Reader.get_value_string(0));
                float const Weight = Reader.get_value_float(1);
                Record->Population.Items.push_back(std::pair<unsigned int, float>(FactionId, Weight));
            }
            else if (Reader.is_value("path_label"))
            {
                Record->bPathLabel = true;
            }
            else if (Reader.is_value("visit"))
            {
                // .text:06337ce8-06337cfd: through vt+0x90, which ORs in 0x20.
                Record->SetVisit((unsigned char)Reader.get_value_int(0));
            }
            else if (Reader.is_value("ids_name"))
            {
                Record->IdsName = Reader.get_value_int(0);
            }
            else if (Reader.is_value("ids_info"))
            {
                Record->IdsInfo = Reader.get_value_int(0);
            }
        } while (Reader.read_value());
    }

    // FUN_06337250 .text:06337d80-06337e07. Returns the zone as the zone map now holds it, or
    // null when it was not inserted, which decides whether it goes onto its SYSTEM's zone list.
    // An empty id (ID_String::IsEmpty, no mask) is not inserted. Otherwise the stack ZONE is
    // copied into a {id, ZONE} pair (FUN_0633e3f0), the pair into the zone map (FUN_0633df60,
    // no overwrite, no error on a duplicate), and the pair is destroyed (FUN_06339010 at
    // 06337dee) -- its destructor releases the mesh handle the map node still holds, as the
    // stack ZONE's destructor does again afterwards: retail releases a zone mesh twice for the
    // one Archetype::Load, and so does the port.
    Universe::IZone* InsertZone(ZONE const& Record)
    {
        if (Record.NicknameId == 0)
            return nullptr;

        std::pair<std::map<unsigned, ZONE>::iterator, bool> Inserted;
        {
            std::pair<unsigned, ZONE> const Pair(Record.NicknameId, Record);
            Inserted = GZones.insert(Pair);
        }
        if (!Inserted.second)
            return nullptr;
        return &Inserted.first->second;
    }

    unsigned LookupObjectBaseId(char const* Nickname)
    {
        // listing 0633bb20 / 06336666. CreateID + GBases find. Miss → WARNING 0xbff, 0.
        unsigned NicknameId = CreateID(Nickname);
        if (GBases.find(NicknameId) == GBases.end())
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: get_base(%s) failed.\n", kSystemsSource, 0xbff,
                  Nickname);
            return 0;
        }
        return NicknameId;
    }

    unsigned SolarGateTypeFromObjectType(unsigned ObjectType)
    {
        // listing 06338131: TEST AL,0x40 / TEST AH,0x8 / TEST AH,0x4.
        if ((ObjectType & 0x40) != 0)
            return Universe::SolarGateJumpGate;
        if ((ObjectType & 0x800) != 0)
            return Universe::SolarGateJumpHole;
        if ((ObjectType & 0x400) != 0)
            return Universe::SolarGateAirlock;
        return Universe::SolarGateNone;
    }

    // FUN_06336380: read one [Object] section of a system file into a pool record. The key
    // order below is retail's is_value order.
    void ReadSystemObject(Universe::FSystemObject& Record, INI_Reader& Reader)
    {
        memset(&Record, 0, sizeof(Record));        // .text:0633638e-06336397 (0x46 dwords)
        Record.Visit = 0;                          // .text:0633639b
        bool bStar = false;                        // .text:063363a1
        if (!Reader.read_value())
            return;

        do
        {
            if (Reader.is_value("nickname"))
            {
                // FUN_06306020(+4, 0x40, value): TString assignment (null -> empty).
                Record.Nickname = Reader.get_value_string();
            }
            else if (Reader.is_value("next_ring"))
            {
                Record.NextRing = Reader.get_value_string();
            }
            else if (Reader.is_value("prev_ring"))
            {
                Record.PrevRing = Reader.get_value_string();
            }
            else if (Reader.is_value("pos"))
            {
                // .text:063364b4-063364f5: value 1 is read first, then 0, then 2.
                float Y = Reader.get_value_float(1);
                float X = Reader.get_value_float(0);
                float Z = Reader.get_value_float(2);
                Record.Pos.x = X;
                Record.Pos.y = Y;
                Record.Pos.z = Z;
            }
            else if (Reader.is_value("archetype"))
            {
                ID_String Name;
                Name.Id = CreateID(Reader.get_value_string());
                Record.ArchetypeId = Archetype::GetAsteroidID(Name);
            }
            else if (Reader.is_value("ids_name"))
            {
                Record.IdsName = Reader.get_value_int(0);
            }
            else if (Reader.is_value("ids_info") || Reader.is_value("ids_info_card") ||
                     Reader.is_value("infocard_ids"))
            {
                Record.IdsInfo = Reader.get_value_int(0);
            }
            else if (Reader.is_value("goto"))
            {
                Record.Goto = CreateID(Reader.get_value_string(0));
            }
            else if (Reader.is_value("dock_with"))
            {
                Record.DockWith = CreateID(Reader.get_value_string(0));
            }
            else if (Reader.is_value("reputation"))
            {
                Record.Reputation = Reader.get_value_string(0);
            }
            else if (Reader.is_value("visit"))
            {
                Record.Visit = (unsigned char)Reader.get_value_int(0);
            }
            else if (Reader.is_value("base"))
            {
                // .text:0633667a-063366ff: the same lookup and 0xbff warning as FUN_0633bb20.
                Record.BaseId = LookupObjectBaseId(Reader.get_value_string());
            }
            else if (Reader.is_value("star"))
            {
                bStar = true;
            }
        } while (Reader.read_value());

        if (bStar)
            Record.Visit = 1;                      // .text:0633673a
    }

    void BuildSystemFilePath(char* FullPath, char const* RelFile)
    {
        // Retail inlines this in both FUN_06337250 (.text:063374e0-0633753a) and FUN_06336750
        // (.text:06336787-063367e8): with the universe directory file system, its root
        // (vt+0x84 into a 0x104-byte buffer) joined with the name by
        // _makepath(out, NULL, root, name, NULL); without it, a plain copy. No absolute-path
        // test (unlike GetFileSysPath).
        if (GUniverseDirFs != nullptr)
        {
            char Root[kPathChars];
            GUniverseDirFs->GetFileSysPath(Root, kPathChars);
            _makepath(FullPath, nullptr, Root, RelFile, nullptr);
        }
        else
        {
            CopyRelPath(FullPath, RelFile);
        }
    }

}

namespace CmnUniverse
{
    // FUN_06336750: refill the shared object pool from one system file. Returns false when
    // the file does not open (the pool is left empty).
    bool LoadSystemObjectPool(char const* RelFile)
    {
        GSystemObjectPool.clear();                 // .text:0633677d FUN_06340e70(begin, end)

        char FullPath[kPathChars];
        BuildSystemFilePath(FullPath, RelFile);

        INI_Reader Reader;
        if (!Reader.open(FullPath, false))
            return false;

        // .text:06336817-063368a3: grow to 300 records (0x14820 bytes) when smaller.
        if (GSystemObjectPool.capacity() < 300)
            GSystemObjectPool.reserve(300);

        while (Reader.read_header())
        {
            if (!Reader.is_header("Object"))
                continue;
            // .text:063368d1-0633694e: push_back a zeroed record (the function-static zero
            // record DAT_0658a878), then read into the new back().
            GSystemObjectPool.push_back(Universe::FSystemObject());
            ReadSystemObject(GSystemObjectPool.back(), Reader);
        }
        return true;
    }
}

namespace
{

    // FUN_06337250 .text:06337ef6-06338234: one [Object] section of a system file during the
    // universe load. Only the keys below are read here; the full record is FUN_06336380's job.
    void ReadSystemFileObject(INI_Reader& Reader, SYSTEM& System, FUniverseLoadCarry& Carry)
    {
        unsigned NicknameId = 0;                   // [ESP+0x2c] (ID_String::clear 06337f2b)
        unsigned BaseId = 0;                       // [ESP+0x10]
        int IdsName = 0;                           // [ESP+0x30]
        unsigned ArchetypeId = 0;                  // [ESP+0x24] (ID_String::clear)
        unsigned GotoId = 0;                       // [ESP+0xb4] (written only by goto)
        unsigned char Visit = 0;                   // [ESP+0x18]
        bool bSawGoto = false;                     // BL
        Vector Pos;                                // .text:06337f0c: 0xbdcccccd = -0.1f
        Pos.x = -0.1f;
        Pos.y = -0.1f;
        Pos.z = -0.1f;

        // 06337f4a-06337f51: no value at all -> the section ends here; [ESP+0x20] is not
        // written back (06338114 is past the loop).
        if (!Reader.read_value())
            return;

        // 06337f5b: ESI = [ESP+0x20], the nickname the previous [Object] left behind.
        unsigned CarriedNickname = Carry.ObjectNickname;
        do
        {
            if (Reader.is_value("nickname"))
            {
                NicknameId = CreateID(Reader.get_value_string(0));
                CarriedNickname = NicknameId;      // 06337f90 MOV ESI,EAX
            }
            else if (Reader.is_value("base"))
            {
                BaseId = LookupObjectBaseId(Reader.get_value_string());   // FUN_0633bb20
            }
            else if (Reader.is_value("ids_name"))
            {
                IdsName = Reader.get_value_int(0);
            }
            else if (Reader.is_value("archetype"))
            {
                ArchetypeId = CreateID(Reader.get_value_string(0));
            }
            else if (Reader.is_value("pos"))
            {
                Pos.x = Reader.get_value_float(0);
                Pos.y = Reader.get_value_float(1);
                Pos.z = Reader.get_value_float(2);
            }
            else if (Reader.is_value("visit"))
            {
                Visit = (unsigned char)Reader.get_value_int(0);
            }
            else if (Reader.is_value("goto"))
            {
                // .text:063380b4-063380f9: the gate map entry is {ESI, goto} -- the nickname
                // read so far in this object, else the carried one -- inserted without
                // overwrite (FUN_062fbad0).
                GotoId = CreateID(Reader.get_value_string(0));
                GGateSystems.insert(std::pair<unsigned, unsigned>(CarriedNickname, GotoId));
                bSawGoto = true;
            }
        } while (Reader.read_value());

        Carry.ObjectNickname = CarriedNickname;    // 06338114 MOV [ESP+0x20],ESI

        if (bSawGoto)
        {
            // .text:0633811e-063381cb: gate type from the solar archetype, then push_back of
            // the connection record onto SYSTEM+0x54 (FUN_0633e770): +0x00 goto, +0x04 this
            // object's nickname id ([ESP+0x2c]), +0x08 the carried nickname ([ESP+0x20]),
            // +0x0c pos (x from EBP, y/z from [ESP+0x3c]/[ESP+0x40]), +0x18 visit, +0x1c type.
            ID_String ArchName;
            ArchName.Id = ArchetypeId;
            unsigned GateType = Universe::SolarGateNone;
            Archetype::Solar* SolarArch = Archetype::GetSolarByName(ArchName);
            if (SolarArch != nullptr)
                GateType = SolarGateTypeFromObjectType(SolarArch->ObjectType);

            Universe::FSystemConnection Connection;
            Connection.GotoSystemId = GotoId;
            Connection.Object.Id = NicknameId;
            Connection.ObjectId = CarriedNickname;
            Connection.Pos = Pos;
            Connection.Visit = Visit;
            Connection.GateType = GateType;
            System.Connections.push_back(Connection);
        }

        // .text:063381d0-06338231: an object that names a base updates that base.
        if (BaseId == 0 || !IdIsValid(BaseId))
            return;
        std::map<unsigned, BASE>::iterator Found = GBases.find(BaseId);
        if (Found == GBases.end())
        {
            // 06338290: FUN_062b7750(0x10000, 2) -> flags 0x100002.
            FDUMP(0x100002, "%s(%d) : *** WARNING: get_base(%#X) failed.\n", kSystemsSource, 0xbf3,
                  BaseId);
            return;
        }
        // .text:06338208-06338231: when the base's slot 2 (strid_name) equals this object's
        // ids_name, slot 25 (+0x64) stores the carried nickname ([ESP+0x20]) and slot 26
        // (+0x68) the visit byte ([ESP+0x18]) into the base. (The null test on the node value
        // at 0x0633820b cannot fail: it is node+0x10.)
        Universe::IBase& Base = Found->second;
        if (Base.GetStridName() != IdsName)
            return;
        Base.SetSpaceObjectId(CarriedNickname);
        Base.SetSpaceObjectVisit(Visit);
    }

    // The system-file half of FUN_06337250 (.text:063374e0-06338289): build the path, open
    // the file, allocate every [Object] vibe (FUN_062b8de0), rewind, then read [Zone] /
    // [Music] / [Object]. A file that does not open is skipped silently (06337567).
    void LoadSystemFile(SYSTEM& System, char const* RelFile, FUniverseLoadCarry& Carry)
    {
        char FullPath[kPathChars];
        BuildSystemFilePath(FullPath, RelFile);

        INI_Reader Reader;
        if (!Reader.open(FullPath, false))
            return;

        Reputation::SolarReps::LoadObjectVibesFromSystemIni(Reader);
        Reader.reset();

        while (Reader.read_header())
        {
            if (Reader.is_header("Zone"))
            {
                // .text:063375c0-063375c8: the reader state is taken at the header, before
                // any value of the section is read.
                Universe::FSystemZone Entry;
                Reader.get_state(Entry.State);

                // The stack ZONE (ESP+0xd0, ctor .text:063375cd-06337710); 063376ed: its
                // system id is the SYSTEM's nickname id so far (+0x4c).
                ZONE Stack;
                Stack.SystemId = System.NicknameId;
                ReadZoneValues(Reader, Stack);
                Universe::IZone* const Inserted = InsertZone(Stack);
                if (Inserted != nullptr)
                {
                    Entry.Zone = Inserted;
                    System.Zones.push_back(Entry);      // FUN_0633e920
                }
                // 06337e2d-06337e84: the stack ZONE's destructor (inline ~ZONE).
            }
            else if (Reader.is_header("Music"))
            {
                // .text:06337ea3-06337ef1
                if (!Reader.read_value())
                    continue;
                do
                {
                    if (Reader.is_value("space"))
                        System.SpaceMusicId = CreateID(Reader.get_value_string());
                } while (Reader.read_value());
            }
            else if (Reader.is_header("Object"))
            {
                ReadSystemFileObject(Reader, System, Carry);
            }
        }
    }

    void ReadBaseValues(INI_Reader& Reader, Universe::IBase* Record)
    {
        if (!Reader.read_value())
            return;

        do
        {
            if (Reader.is_value("nickname"))
            {
                char const* Text = Reader.get_value_string();
                Record->NicknameId = CreateID(Text);
                Record->Nickname = StringAlloc(Text, false);
            }
            else if (Reader.is_value("strid_name"))
            {
                Record->StridName = Reader.get_value_int(0);
            }
            else if (Reader.is_value("file"))
            {
                Record->File = StringAlloc(Reader.get_value_string(), false);
            }
            else if (Reader.is_value("system"))
            {
                Record->SystemId = CreateID(Reader.get_value_string());
            }
            else if (Reader.is_value("BGCS_base_run_by"))
            {
                // 06338873-063388bd: std::string assignment (_Grow(len, true) + copy; an empty
                // value empties the string).
                Record->BgcsBaseRunBy = Reader.get_value_string();
            }
            else if (Reader.is_value("ship_sml_01"))
            {
                Record->ShipSml[0] = CreateID(Reader.get_value_string());
            }
            else if (Reader.is_value("ship_sml_02"))
            {
                Record->ShipSml[1] = CreateID(Reader.get_value_string());
            }
            else if (Reader.is_value("ship_sml_03"))
            {
                Record->ShipSml[2] = CreateID(Reader.get_value_string());
            }
            else if (Reader.is_value("ship_mdm_01"))
            {
                Record->ShipMdm[0] = CreateID(Reader.get_value_string());
            }
            else if (Reader.is_value("ship_mdm_02"))
            {
                Record->ShipMdm[1] = CreateID(Reader.get_value_string());
            }
            else if (Reader.is_value("ship_mdm_03"))
            {
                Record->ShipMdm[2] = CreateID(Reader.get_value_string());
            }
            else if (Reader.is_value("ship_lrg_01"))
            {
                Record->ShipLrg[0] = CreateID(Reader.get_value_string());
            }
            else if (Reader.is_value("ship_lrg_02"))
            {
                Record->ShipLrg[1] = CreateID(Reader.get_value_string());
            }
            else if (Reader.is_value("ship_lrg_03"))
            {
                Record->ShipLrg[2] = CreateID(Reader.get_value_string());
            }
            else if (Reader.is_value("terrain_tiny"))
            {
                Record->TerrainTiny = CreateID(Reader.get_value_string());
            }
            else if (Reader.is_value("terrain_sml"))
            {
                Record->TerrainSml = CreateID(Reader.get_value_string());
            }
            else if (Reader.is_value("terrain_mdm"))
            {
                Record->TerrainMdm = CreateID(Reader.get_value_string());
            }
            else if (Reader.is_value("terrain_lrg"))
            {
                Record->TerrainLrg = CreateID(Reader.get_value_string());
            }
            else if (Reader.is_value("terrain_dyna_01"))
            {
                Record->TerrainDyna01 = CreateID(Reader.get_value_string());
            }
            else if (Reader.is_value("terrain_dyna_02"))
            {
                Record->TerrainDyna02 = CreateID(Reader.get_value_string());
            }
            else if (Reader.is_value("autosave_forbidden"))
            {
                Record->bAutosaveForbidden = Reader.get_value_bool(0);
            }
        } while (Reader.read_value());
    }

    // FUN_06337250 .text:06338bae-06338c95, after the [Base] value loop. A valid id: the key
    // from slot 0 (06338bc4), the stack BASE copied into a {key, BASE} pair (FUN_0633e210),
    // the pair into the base map (FUN_0633ead0, no overwrite), the pair destroyed (its
    // BGCS string released), then the collision error. An invalid id only reports. The
    // caller destroys the stack BASE afterwards (06338c95-06338cc9).
    void InsertBase(BASE const& Record)
    {
        if (!IdIsValid(Record.NicknameId))
        {
            FDUMP(0x100001,
                  "%s(%d) : *** ERROR: Failed to insert base \"%s\" into system DB due to invalid ID.\n",
                  kSystemsSource, 0xbc0, Record.Nickname);
            return;
        }

        bool bInserted = false;
        {
            std::pair<unsigned, BASE> const Pair(Record.GetId(), Record);
            bInserted = GBases.insert(Pair).second;
        }
        if (!bInserted)
        {
            FDUMP(0x100001, "%s(%d) : *** ERROR: %s", kSystemsSource, 0xbbb,
                  "Failed to insert new base into system DB due to ID collision.\n");
        }
    }

    // FUN_06337250 (thiscall on the universe object DAT_0658a828, RET 4). Returns the system
    // map's size ([ESI+0x10], 06338cea), which is 0 when the file does not open.
    // It also drives the reputation Vibe static store: emptied before the reader (063372a4),
    // loading mode after a successful open (063372db), sorted after the header loop
    // (06338ce5) -- so every Vibe allocated while the universe loads is a static entry.
    unsigned LoadUniverseIni(char const* FullPath)
    {
        // 06337279-0633729f: erase(begin, end) on the system, base, zone and gate maps.
        ClearUniverseTables();
        // 063372a4: thunk_FUN_06320550.
        Reputation::Vibe::FreeStatic();

        FUniverseLoadCarry Carry;
        Carry.SystemPos.x = 0.0f;
        Carry.SystemPos.y = 0.0f;
        Carry.SystemPos.z = 0.0f;
        Carry.ObjectNickname = 0;

        INI_Reader Reader;
        if (!Reader.open(FullPath, false))
            return (unsigned)GSystems.size();

        // 063372db: FUN_06320430.
        Reputation::Vibe::BeginStaticLoad();

        while (Reader.read_header())
        {
            if (Reader.is_header("Time"))
            {
                ReadTimeBlock(Reader);
            }
            else if (Reader.is_header("System"))
            {
                // 06337376-06337441: the stack SYSTEM; its +0x74 position is not constructed
                // (see FUniverseLoadCarry), so it still holds the previous block's.
                SYSTEM Stack;
                Stack.Pos = Carry.SystemPos;
                ReadSystemValues(Reader, Stack, Carry);
                InsertSystem(Stack);
                Carry.SystemPos = Stack.Pos;
                // 06338620-063386ad: the stack SYSTEM's destructor (FreeSpatial when set, the
                // zone list, the connection vector).
            }
            else if (Reader.is_header("Base"))
            {
                // 063386d0-06338780: the stack BASE (constructor below).
                BASE Stack;
                ReadBaseValues(Reader, &Stack);
                InsertBase(Stack);
                // 06338c95-06338cc9: the stack BASE's destructor (its BGCS string).
            }
        }

        // 06338ce5: FUN_06320530 (only on the opened path; 063372d5 jumps past it).
        Reputation::Vibe::EndStaticLoad();

        return (unsigned)GSystems.size();
    }
}

namespace Archetype
{
    unsigned __cdecl GetAsteroidID(ID_String const& Name)
    {
        return Name.Id;
    }

    unsigned __cdecl GetDynamicAsteroidID(ID_String const& Name)
    {
        return Name.Id;
    }

    unsigned __cdecl GetEquipmentID(ID_String const& Name)
    {
        return Name.Id;
    }

    unsigned __cdecl GetShipID(ID_String const& Name)
    {
        return Name.Id;
    }

    unsigned __cdecl GetSimpleID(ID_String const& Name)
    {
        return Name.Id;
    }

    unsigned __cdecl GetSolarID(ID_String const& Name)
    {
        return Name.Id;
    }

}

namespace Universe
{
    bool __cdecl Startup(char const* RelPath)
    {
        // listing 06337060. Refcount DAT_0658a9bc: skip load if already != 0.
        if (GUniverseRefCount == 0)
        {
            // 06337073-063370ee: the four maps erased; 063370f3 thunk_FUN_06320550 (the
            // reputation Vibe static store emptied); 063370f8-06337107 the old directory file
            // system released.
            ClearUniverseTables();
            Reputation::Vibe::FreeStatic();
            ReleaseUniverseDirFs();

            char FullPath[kPathChars];
            GetFileSysPath(FullPath, RelPath, DALib::Data);

            char Drive[kPathChars];
            char Dir[kPathChars];
            char Directory[kPathChars];
            _splitpath(FullPath, Drive, Dir, nullptr, nullptr);
            _makepath(Directory, Drive, Dir, nullptr, nullptr);

            CreateUniverseDirFs(Directory);

            unsigned SystemCount = LoadUniverseIni(FullPath);
            if (SystemCount == 0)
            {
                FDUMP(0x100001, "%s(%d) : *** ERROR: could not create universe '%s'\n",
                      kSystemsSource, 0xc2d, FullPath);
            }
        }

        GUniverseRefCount += 1;
        return GUniverseDirFs != nullptr;
    }

    void __cdecl Shutdown()
    {
        // listing 0633bd80. DEC then JNZ — teardown only at 0.
        GUniverseRefCount -= 1;
        if (GUniverseRefCount != 0)
            return;

        // 0633bd94-0633bee2: systems, bases, zones, gates erased; 0633bee4 thunk_FUN_06320550
        // (the reputation Vibe static store emptied); then the directory file system released
        // (vt+0x08) and nulled.
        ClearUniverseTables();
        Reputation::Vibe::FreeStatic();
        ReleaseUniverseDirFs();
    }

    unsigned __cdecl num_systems()
    {
        return (unsigned)GSystems.size();
    }

    unsigned __cdecl get_system_id(char const* Nickname)
    {
        unsigned NicknameId = CreateID(Nickname);
        if (GSystems.find(NicknameId) == GSystems.end())
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: get_system(%s) failed.\n", kSystemsSource, 0xbe4,
                  Nickname);
            return 0;
        }
        return NicknameId;
    }

    ISystem const* __cdecl get_system(unsigned NicknameId)
    {
        if (!IdIsValid(NicknameId))
            return nullptr;

        std::map<unsigned, SYSTEM>::iterator Found = GSystems.find(NicknameId);
        if (Found == GSystems.end())
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: get_system(%#X) failed.\n", kSystemsSource, 0xbd8,
                  NicknameId);
            return nullptr;
        }
        return &Found->second;
    }

    void __cdecl get_filename(char* Dest, char const* RelPath)
    {
        // Common.dll.txt:0633c650. With the universe directory file system (DAT_0658a9b4):
        // its root (vt+0x84 into a 0x104-byte buffer) joined by
        // _makepath(Dest, NULL, root, RelPath, NULL); without it, a byte copy of RelPath into
        // Dest through its NUL. No argument checks.
        if (GUniverseDirFs != nullptr)
        {
            char Root[kPathChars];
            GUniverseDirFs->GetFileSysPath(Root, kPathChars);
            _makepath(Dest, nullptr, Root, RelPath, nullptr);
            return;
        }
        strcpy(Dest, RelPath);
    }

    // 0633c030 / 0633c0a0: post-increment walks -- the element under the walk is returned
    // and the walk moves to its in-order successor before the return.
    ISystem* __cdecl GetFirstSystem()
    {
        GSystemWalk = GSystems.begin();
        if (GSystemWalk == GSystems.end())
            return nullptr;
        SYSTEM* const Current = &GSystemWalk->second;
        ++GSystemWalk;
        return Current;
    }

    ISystem* __cdecl GetNextSystem()
    {
        if (GSystemWalk == GSystems.end())
            return nullptr;
        SYSTEM* const Current = &GSystemWalk->second;
        ++GSystemWalk;
        return Current;
    }

    unsigned __cdecl num_bases()
    {
        return (unsigned)GBases.size();
    }

    unsigned __cdecl get_base_id(char const* Nickname)
    {
        unsigned NicknameId = CreateID(Nickname);
        if (GBases.find(NicknameId) == GBases.end())
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: get_base(%s) failed.\n", kSystemsSource, 0xbff,
                  Nickname);
            return 0;
        }
        return NicknameId;
    }

    IBase* __cdecl get_base(unsigned NicknameId)
    {
        if (!IdIsValid(NicknameId))
            return nullptr;

        std::map<unsigned, BASE>::iterator Found = GBases.find(NicknameId);
        if (Found == GBases.end())
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: get_base(%#X) failed.\n", kSystemsSource, 0xbf3,
                  NicknameId);
            return nullptr;
        }
        return &Found->second;
    }

    // 0633c230 / 0633c2a0: the same post-increment walks over the base map.
    IBase* __cdecl GetFirstBase()
    {
        GBaseWalk = GBases.begin();
        if (GBaseWalk == GBases.end())
            return nullptr;
        BASE* const Current = &GBaseWalk->second;
        ++GBaseWalk;
        return Current;
    }

    IBase* __cdecl GetNextBase()
    {
        if (GBaseWalk == GBases.end())
            return nullptr;
        BASE* const Current = &GBaseWalk->second;
        ++GBaseWalk;
        return Current;
    }

    IZone const* __cdecl get_zone(unsigned NicknameId)
    {
        // listing 0633c310. No 0x3fffffff mask. Miss → WARNING 0xc8e, nullptr.
        std::map<unsigned, ZONE>::iterator Found = GZones.find(NicknameId);
        if (Found == GZones.end())
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: get_zone(%d) failed", kSystemsSource, 0xc8e,
                  NicknameId);
            return nullptr;
        }
        return &Found->second;
    }

    IZone const* __cdecl get_zone(unsigned SystemId, char const* Nickname)
    {
        // listing 0633c380. SystemId unused. Lookup is CreateID(nickname) on the global map.
        (void)SystemId;
        char const* Name = Nickname;
        unsigned NicknameId = CreateID(Nickname);
        std::map<unsigned, ZONE>::iterator Found = GZones.find(NicknameId);
        if (Found == GZones.end())
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: get_zone(%s) failed", kSystemsSource, 0xc9c,
                  Name);
            return nullptr;
        }
        return &Found->second;
    }

    IZone* __cdecl get_non_const_zone(unsigned SystemId, char const* Nickname)
    {
        (void)SystemId;
        char const* Name = Nickname;
        unsigned NicknameId = CreateID(Nickname);
        std::map<unsigned, ZONE>::iterator Found = GZones.find(NicknameId);
        if (Found == GZones.end())
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: get_non_const_zone(%s) failed", kSystemsSource,
                  0xcaa, Name);
            return nullptr;
        }
        return &Found->second;
    }

    IZone* __cdecl get_non_const_zone(unsigned SystemId, unsigned NicknameId)
    {
        (void)SystemId;
        std::map<unsigned, ZONE>::iterator Found = GZones.find(NicknameId);
        if (Found == GZones.end())
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: get_non_const_zone(%d) failed", kSystemsSource,
                  0xcb7, NicknameId);
            return nullptr;
        }
        return &Found->second;
    }

    IZone const* __cdecl first_zone(unsigned SystemId)
    {
        // listing 0633c4f0: find the system in the system map; if its zone list (SYSTEM+0x80,
        // size at +0x88) is not empty, return the first entry's zone (node +0xc10), else null.
        std::map<unsigned, SYSTEM>::iterator Found = GSystems.find(SystemId);
        if (Found == GSystems.end())
            return nullptr;
        if (Found->second.Zones.size() == 0)
            return nullptr;
        return Found->second.Zones.front().Zone;
    }

    IZone const* __cdecl next_zone(IZone const* Current)
    {
        // listing 0633c550: the system comes from the zone's own slot 2 (system id), then its
        // zone list is walked for the entry whose zone id (slot 1) matches Current's; the
        // entry after it is returned, or null at the end of the list.
        if (Current == nullptr)
            return nullptr;

        std::map<unsigned, SYSTEM>::iterator Found = GSystems.find(Current->GetSystem());
        if (Found == GSystems.end())
            return nullptr;

        std::list<FSystemZone> const& Zones = Found->second.Zones;
        std::list<FSystemZone>::const_iterator It = Zones.begin();
        std::list<FSystemZone>::const_iterator End = Zones.end();
        for (; It != End; ++It)
        {
            if (It->Zone == nullptr)
                continue;
            if (It->Zone->GetId() != Current->GetId())
                continue;
            ++It;
            if (It == End)
                return nullptr;
            return It->Zone;
        }
        return nullptr;
    }

    unsigned __cdecl get_gate_system(unsigned NicknameId)
    {
        // listing 0633c5e0. DAT_0658a868 map: key = object CreateID, value = goto system.
        std::map<unsigned, unsigned>::iterator Found = GGateSystems.find(NicknameId);
        if (Found == GGateSystems.end())
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: get_gate_system(%d) failed", kSystemsSource,
                  0xd15, NicknameId);
            return 0;
        }
        return Found->second;
    }
}


// NewSpatial / FreeSpatial and the spatial partitions live in common_spatial.cpp.
