// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// TU41 system connections: the shortest-path tables (load, build FUN_06f2a400, write) and
// their readers. See SystemConnections.h for the table layout. Lane CNT-POP (docs/re/content.md, TU map).

#include "SystemConnections.h"

#include "ContentInternal.h"    // OpenContentDataIni (FUN_06f24280)
#include "ContentPlayerState.h" // IsContentGateLocked (TU53 FUN_06f6ce90)
#include "ContentStaticIds.h"   // TU41's 16 static-init thunks 06f28f60-06f29140
#include "SystemInfo.h"         // GSystemInfos, FindSystemInfo
#include "common_server.h"  // pub::System::EnumerateConnections, ConnectionEnumerator
#include "common_tstring.h"     // TString
#include "readfile.h"           // IFileSystem (pub::GetDataPath)

#include <cstdio>
#include <stdlib.h>
#include <vector>

// The three path tables. Retail: static map objects (their init/teardown thunks are the
// TU41 static-initializer entries); filled by FUN_06f29750 (load) or FUN_06f2a400 (build).
FSystemPathTable GSystemPathsAll;   // DAT_06fd07c4
FSystemPathTable GSystemPathsGate;  // DAT_06fd07ac
FSystemPathTable GSystemPathsHole;  // DAT_06fd0794

namespace
{
// The type switch every TU41 reader opens with: 0 -> DAT_06fd07c4, 1 -> DAT_06fd07ac,
// 2 -> DAT_06fd0794, anything else -> no table.
FSystemPathTable* GetSystemPathTable(int Type)
{
    switch (Type)
    {
    case 0: return &GSystemPathsAll;
    case 1: return &GSystemPathsGate;
    case 2: return &GSystemPathsHole;
    default: return nullptr;
    }
}

// The enumerator prologue of 06f2a190 / 06f2a240 / 06f29c10: ID_String::clear on the ten
// Object slots (the struct has no constructor) and Count = 0.
void ResetConnectionEnumerator(pub::System::ConnectionEnumerator& Enumerator)
{
    for (int Index = 0; Index < 10; ++Index)
    {
        Enumerator.Object[Index].clear();
    }
    Enumerator.Count = 0;
}

// DAT_06fd0818: set by the first FUN_06f2a400 (06f2a44c) and never cleared.
bool GSystemPathTablesBuilt = false;

// DAT_06fb925c (.rdata 0): the player FUN_06f2a400 / FUN_06f2a0b0 ask FUN_06f6ce90 about.
unsigned int const GAnyPlayerId = 0;

// One search of FUN_06f2a400 for one source system and connection type. Retail inlines it
// three times: type 0 into DAT_06fd07c4 (06f2a494-06f2aec8), type 1 into DAT_06fd07ac
// (06f2aef6-06f2b8ca), type 2 into DAT_06fd0794 (06f2b900-06f2be15, through FUN_06f2a0b0 /
// FUN_06f2a2e0). The only difference between the copies: the type-0 pass tests that the
// SOURCE system has a SystemInfo (06f2a740) and on a miss keeps the path queued (06f2a74a ->
// 06f2adce frees the local only; unreachable, the sources come from the SystemInfo table),
// the other two test the path's LAST system (06f2b1aa / 06f2ba4d) and drop the path on a
// miss (FUN_06f2cc20 06f2b837 / 06f2bdb1).
void BuildSystemPathsFrom(unsigned int SourceSystemId, int Type, FSystemPathTable* Table)
{
    std::list<std::list<unsigned int>> Queue;       // FUN_06eaed40 / FUN_06f2cb90
    std::list<unsigned int> Neighbours;
    CollectOpenConnectionSystems(SourceSystemId, &Neighbours, static_cast<ConnectionType>(Type));
    for (std::list<unsigned int>::iterator It = Neighbours.begin(); It != Neighbours.end(); ++It)
    {
        // 06f2a587-06f2a695: {source, neighbour} at the end of the queue.
        std::list<unsigned int> Path;
        Path.push_back(SourceSystemId);
        Path.push_back(*It);
        Queue.push_back(Path);
    }

    while (!Queue.empty())
    {
        // 06f2a6a5-06f2a72b: the first of the shortest queued paths (FUN_06f2a2e0 inlined).
        std::list<unsigned int> Path;
        CopyShortestPath(&Queue, &Path);
        unsigned int const LastSystemId = Path.back();

        unsigned int const CheckedSystemId = (Type == 0) ? SourceSystemId : LastSystemId;
        if (FindSystemInfo(&CheckedSystemId) == nullptr)
        {
            if (Type != 0)
                Queue.remove(Path);
            continue;
        }

        // 06f2a750-06f2a8f9: Table[source] exists, and holds source -> {source}; both are
        // unique inserts (an existing entry is kept). EH map 06fc3610: the empty map is a
        // local (state 5, 06f2a762) that lives to the end of the iteration (06f2adc9).
        FSystemPathsFrom EmptyPaths;
        Table->insert(FSystemPathTable::value_type(SourceSystemId, EmptyPaths));
        FSystemPathTable::iterator From = Table->find(SourceSystemId);
        if (From != Table->end())
        {
            std::list<unsigned int> Self;
            Self.push_back(SourceSystemId);
            From->second.insert(FSystemPathsFrom::value_type(SourceSystemId, Self));
        }

        // 06f2a8fd-06f2a964: every queued copy of this path is dropped (FUN_06f2efe0 compare,
        // FUN_06f2d780 erase).
        Queue.remove(Path);

        // 06f2a966-06f2a981: JL / JLE -- a known path of the same length or shorter wins.
        int const KnownLength = GetSystemPathLength(SourceSystemId, LastSystemId, Type);
        if (KnownLength >= 0 && KnownLength <= static_cast<int>(Path.size()))
            continue;

        // 06f2a99b: the extension list is built before the record step (EH state 9).
        std::list<unsigned int> Next;

        // 06f2a9ab-06f2ab9f: record the path (insert, or assign over the known one).
        From = Table->find(SourceSystemId);
        if (From != Table->end())
        {
            FSystemPathsFrom::iterator To = From->second.find(LastSystemId);
            if (To == From->second.end())
            {
                // 06f2aa91-06f2ab9f: retail first inserts {Last, P} into a throw-away map
                // (EH states 10/11, 06f2aaa3 / 06f2aaeb), then into the inner map (state 12,
                // 06f2ab5b); the throw-away map dies at 06f2ab9f.
                FSystemPathsFrom Scratch;
                Scratch.insert(FSystemPathsFrom::value_type(LastSystemId, Path));
                From->second.insert(FSystemPathsFrom::value_type(LastSystemId, Path));
            }
            else
            {
                To->second = Path;
            }
        }

        // 06f2aba4-06f2ad67: every open connection of the last system extends the path.
        CollectOpenConnectionSystems(LastSystemId, &Next, static_cast<ConnectionType>(Type));
        for (std::list<unsigned int>::iterator It = Next.begin(); It != Next.end(); ++It)
        {
            std::list<unsigned int> Longer(Path);
            Longer.push_back(*It);
            Queue.push_back(Longer);
        }
    }
}

// The body of one file of FUN_06f2bf30 (06f2c0b4-06f2c1c6, 06f2c1f3-06f2c2f6,
// 06f2c323-06f2c440). Retail prints the head node's value for a system without a
// universe.ini nickname; the port's find() == end() dereference is equally undefined (every
// path system comes from universe.ini). bWritePathSystems false drops the ", %s" path
// systems: the second file's stream mix-up (see WriteSystemPathTables).
void WriteSystemPathFile(FILE* File, bool bWritePathSystems, FSystemPathTable const& Table,
                         std::map<unsigned int, TString<0x10>> const& Names)
{
    for (FSystemPathTable::const_iterator From = Table.begin(); From != Table.end(); ++From)
    {
        fprintf(File, "[SystemConnections]\n\n");
        for (FSystemPathsFrom::const_iterator To = From->second.begin(); To != From->second.end(); ++To)
        {
            fprintf(File, "Path = %s, %s", Names.find(From->first)->second.Data,
                    Names.find(To->first)->second.Data);
            if (bWritePathSystems)
            {
                for (std::list<unsigned int>::const_iterator It = To->second.begin();
                     It != To->second.end(); ++It)
                    fprintf(File, ", %s", Names.find(*It)->second.Data);
            }
            fprintf(File, "\n");
        }
        fprintf(File, "\n");
    }
}
} // namespace

// FUN_06f29b20 (cdecl). Type switch 06f29b2b-06f29b38 (other type -> -1); outer find
// (FUN_06f7b0f0 / inlined lower_bound for type 2 at 06f29b72-06f29b89), inner find, value
// = [inner node + 0x18] = the path list's size (06f29c06); not found -> -1 (EDI = -1).
int GetSystemPathLength(unsigned int FromSystemId, unsigned int ToSystemId, int Type)
{
    FSystemPathTable* Table = GetSystemPathTable(Type);
    if (Table == nullptr)
    {
        return -1;
    }
    FSystemPathTable::iterator Outer = Table->find(FromSystemId);
    if (Outer == Table->end())
    {
        return -1;
    }
    FSystemPathsFrom::iterator Inner = Outer->second.find(ToSystemId);
    if (Inner == Outer->second.end())
    {
        return -1;
    }
    return static_cast<int>(Inner->second.size());
}

// FUN_06f29c10 (cdecl). 06f29c4b: Path = FUN_06f29d80(From, To, Type); 06f29c53-06f29c74:
// Next = the path's second element, 0 when the path has fewer than two; result = 0.
// 06f29c77/06f29c87: From & 0x3fffffff and Next & 0x3fffffff both nonzero -> enumerate
// From's connections of Type (06f29cc7); success -> first i with GotoSystemId[i] == Next
// gives ObjectId[i] (06f29ce0-06f29cf1). The path list is destroyed on return.
unsigned int GetNextHopConnectionObject(unsigned int FromSystemId, unsigned int ToSystemId, int Type)
{
    std::list<unsigned int> Path = GetSystemPath(FromSystemId, ToSystemId, Type);

    unsigned int NextSystemId = 0;
    if (Path.size() != 0)
    {
        std::list<unsigned int>::iterator Second = Path.begin();
        ++Second;
        if (Second != Path.end())
        {
            NextSystemId = *Second;
        }
    }

    unsigned int Result = 0;
    unsigned int SystemId = FromSystemId;
    if ((SystemId & 0x3fffffff) != 0 && (NextSystemId & 0x3fffffff) != 0)
    {
        pub::System::ConnectionEnumerator Enumerator;
        ResetConnectionEnumerator(Enumerator);
        if (pub::System::EnumerateConnections(SystemId, Enumerator, static_cast<ConnectionType>(Type)) == 0)
        {
            for (int Index = 0; Index < static_cast<int>(Enumerator.Count); ++Index)
            {
                if (Enumerator.GotoSystemId[Index] == NextSystemId)
                {
                    Result = Enumerator.ObjectId[Index];
                    break;
                }
            }
        }
    }
    return Result;
}

// FUN_06f29d80 (cdecl, returns the list through the hidden pointer). A local empty list;
// Type switch 06f29de8-06f29dfc (other type -> empty); outer find 06f29e11/06f29eca/
// 06f29f80, inner find 06f29e35/06f29eee/06f29fa4; found -> local = inner list (element-wise
// assign + tail erase/insert, 06f29e5c-06f2a02d); the local is copied into the result
// (06f2a032-06f2a074) and destroyed (FUN_06f15350).
std::list<unsigned int> GetSystemPath(unsigned int FromSystemId, unsigned int ToSystemId, int Type)
{
    std::list<unsigned int> Path;
    FSystemPathTable* Table = GetSystemPathTable(Type);
    if (Table != nullptr)
    {
        FSystemPathTable::iterator Outer = Table->find(FromSystemId);
        if (Outer != Table->end())
        {
            FSystemPathsFrom::iterator Inner = Outer->second.find(ToSystemId);
            if (Inner != Outer->second.end())
            {
                Path = Inner->second;
            }
        }
    }
    return Path;
}

// FUN_06f2a190 (cdecl). Result = -2; enumerate every connection (type 0, 06f2a1d4);
// success -> first i with ObjectId[i] == ObjectId: *OutPosition = Pos[i], return 0
// (06f2a20b-06f2a22f); otherwise -2.
int GetConnectionPosition(unsigned int SystemId, unsigned int ObjectId, Vector* OutPosition)
{
    pub::System::ConnectionEnumerator Enumerator;
    ResetConnectionEnumerator(Enumerator);
    if (pub::System::EnumerateConnections(SystemId, Enumerator, CONN_TYPE_ALL) == 0)
    {
        for (int Index = 0; Index < static_cast<int>(Enumerator.Count); ++Index)
        {
            if (Enumerator.ObjectId[Index] == ObjectId)
            {
                *OutPosition = Enumerator.Pos[Index];
                return 0;
            }
        }
    }
    return -2;
}

// FUN_06f2a240 (cdecl). As FUN_06f2a190, the out value is GotoSystemId[i] (06f2a2bb-06f2a2cb).
int GetConnectionGotoSystem(unsigned int SystemId, unsigned int ObjectId, unsigned int* OutGotoSystemId)
{
    pub::System::ConnectionEnumerator Enumerator;
    ResetConnectionEnumerator(Enumerator);
    if (pub::System::EnumerateConnections(SystemId, Enumerator, CONN_TYPE_ALL) == 0)
    {
        for (int Index = 0; Index < static_cast<int>(Enumerator.Count); ++Index)
        {
            if (Enumerator.ObjectId[Index] == ObjectId)
            {
                *OutGotoSystemId = Enumerator.GotoSystemId[Index];
                return 0;
            }
        }
    }
    return -2;
}

// FUN_06f29320 (06f29320-06f2955b, cdecl). Every value line -- "Path" or not -- inserts
// {To, the systems read so far} into the section's map (a unique insert) and empties the
// list; a line other than "Path" re-inserts the previous To (no-op) or {0, empty} first.
// The section's map goes into Table under the last From, unless From & 0x3fffffff is 0.
void __cdecl LoadSystemPathSection(INI_Reader* Reader, FSystemPathTable* Table)
{
    unsigned int ToSystemId = 0;        // ESP+0x10 (frame -0x5c), zeroed at 06f2934a
    unsigned int FromSystemId = 0;      // ESP+0x14 (frame -0x58), zeroed at 06f29346
    unsigned int PathSystemId = 0;      // ESP+0x70: never initialised in retail, kept across lines
    std::list<unsigned int> Path;       // 06f29342-06f2935b
    FSystemPathsFrom Paths;             // FUN_06f2e290
    while (Reader->read_value())
    {
        if (Reader->is_value("Path"))
        {
            // 06f293b6-06f2942a: value 0 = from, value 1 = to, values 2.. = the path.
            pub::GetSystemID(FromSystemId, Reader->get_value_string(0));
            pub::GetSystemID(ToSystemId, Reader->get_value_string(1));
            for (unsigned int Index = 2; !Reader->is_value_empty(Index); ++Index)
            {
                pub::GetSystemID(PathSystemId, Reader->get_value_string(Index));
                Path.push_back(PathSystemId);                                // FUN_06f1e390
            }
        }
        // 06f2942e-06f2947b: FUN_06f2d4b0 insert of {To, copy of Path}, then erase(begin, end).
        Paths.insert(FSystemPathsFrom::value_type(ToSystemId, Path));
        Path.clear();
    }
    // 06f29492-06f294d0: FUN_06f2cf50 insert of {From, copy of Paths}.
    if ((FromSystemId & 0x3fffffff) != 0)
        Table->insert(FSystemPathTable::value_type(FromSystemId, Paths));
}

// FUN_06f29750 (06f29750-06f29a87, cdecl). The three readers are constructed in this order
// and destroyed together at the end (06f2991d-06f29a6a).
void __cdecl LoadSystemPathTables()
{
    // 06f2976f-06f297b5: FUN_06f2d190 erase(begin, end) on each table.
    GSystemPathsAll.clear();
    GSystemPathsGate.clear();
    GSystemPathsHole.clear();

    INI_Reader AllReader;
    OpenContentDataIni("Universe\\systems_shortest_path.ini", &AllReader);   // result dropped
    while (AllReader.read_header())
    {
        if (AllReader.is_header("SystemConnections"))
            LoadSystemPathSection(&AllReader, &GSystemPathsAll);
    }
    AllReader.close();

    INI_Reader GateReader;
    OpenContentDataIni("Universe\\shortest_legal_path.ini", &GateReader);
    while (GateReader.read_header())
    {
        if (GateReader.is_header("SystemConnections"))
            LoadSystemPathSection(&GateReader, &GSystemPathsGate);
    }
    GateReader.close();

    INI_Reader HoleReader;
    OpenContentDataIni("Universe\\shortest_illegal_path.ini", &HoleReader);
    while (HoleReader.read_header())
    {
        if (HoleReader.is_header("SystemConnections"))
            LoadSystemPathSection(&HoleReader, &GSystemPathsHole);
    }
    HoleReader.close();
}

// FUN_06f29a90 (06f29a90-06f29b10, cdecl): the existence test opens the file with fopen "r"
// (path = pub::GetDataPath() dir + name, _makepath) and closes it after the load.
void __cdecl InitSystemPathTables()
{
    char DataDir[0x104];
    char FullPath[0x104];
    IFileSystem* DataFileSystem = pub::GetDataPath();
    DataFileSystem->GetFileSysPath(DataDir, 0x104);                         // vtbl[+0x84]
    _makepath(FullPath, nullptr, DataDir, "Universe\\systems_shortest_path.ini", nullptr);
    FILE* File = fopen(FullPath, "r");
    if (File == nullptr)
    {
        BuildSystemPathTables(true);                                        // 06f29aea
        return;
    }
    LoadSystemPathTables();                                                 // 06f29afa
    fclose(File);
}

// FUN_06f2a0b0 (06f2a0b0-06f2a183, cdecl).
void __cdecl CollectOpenConnectionSystems(unsigned int SystemId, std::list<unsigned int>* Out,
                                          ConnectionType Type)
{
    pub::System::ConnectionEnumerator Enumerator;
    ResetConnectionEnumerator(Enumerator);                                  // 06f2a0bf-06f2a0e9
    if (pub::System::EnumerateConnections(SystemId, Enumerator, Type) != 0)
        return;
    for (int Index = 0; Index < static_cast<int>(Enumerator.Count); ++Index)
    {
        // 06f2a116-06f2a15f: the goto system is appended unless the object is locked.
        unsigned int const GotoSystemId = Enumerator.GotoSystemId[Index];
        if (!IsContentGateLocked(&GAnyPlayerId, &Enumerator.ObjectId[Index]))
            Out->push_back(GotoSystemId);
    }
}

// FUN_06f2a2e0 (06f2a2e0-06f2a3f2, cdecl): CMP / JLE -- a later path of equal length does not
// replace the first.
int __cdecl CopyShortestPath(std::list<std::list<unsigned int>>* Paths, std::list<unsigned int>* Out)
{
    int BestLength = -1;
    for (std::list<std::list<unsigned int>>::iterator It = Paths->begin(); It != Paths->end(); ++It)
    {
        int const Length = static_cast<int>(It->size());
        if (BestLength != -1 && BestLength <= Length)
            continue;
        if (Out != &*It)
            *Out = *It;                                                     // inlined assign
        BestLength = Length;
    }
    return BestLength;
}

// FUN_06f2a400 (06f2a400-06f2be55, cdecl). Retail iterates a full copy of the SystemInfo
// table (FUN_06f75f20 + FUN_06f2ccb0 at 06f2a460, EH state 0; destroyed by FUN_06f2ce20 at
// 06f2be3b) and reads only its keys (node +0xc). The port holds the same copy.
void __cdecl BuildSystemPathTables(bool bWrite)
{
    if (GSystemPathTablesBuilt)
        return;
    // 06f2a42e-06f2a44c: FUN_06f2d250 empties the tables, then the flag is set.
    GSystemPathsHole.clear();
    GSystemPathsGate.clear();
    GSystemPathsAll.clear();
    GSystemPathTablesBuilt = true;

    std::map<unsigned int, FSystemInfo> const Systems(GSystemInfos);       // 06f2a460, state 0

    for (std::map<unsigned int, FSystemInfo>::const_iterator It = Systems.begin(); It != Systems.end(); ++It)
        BuildSystemPathsFrom(It->first, 0, &GSystemPathsAll);               // 06f2a494-06f2aec8
    for (std::map<unsigned int, FSystemInfo>::const_iterator It = Systems.begin(); It != Systems.end(); ++It)
        BuildSystemPathsFrom(It->first, 1, &GSystemPathsGate);              // 06f2aef6-06f2b8ca
    for (std::map<unsigned int, FSystemInfo>::const_iterator It = Systems.begin(); It != Systems.end(); ++It)
        BuildSystemPathsFrom(It->first, 2, &GSystemPathsHole);              // 06f2b900-06f2be15

    if (bWrite)
        WriteSystemPathTables();                                            // 06f2be24
}

// FUN_06f2bf30 (06f2bf30-06f2c4xx, cdecl): nicknames of universe.ini [System] sections
// (TString<0x10> = FUN_06f2db50, at most 15 chars; unique insert per id), then the three
// files with their paths hardcoded relative to the working directory.
void __cdecl WriteSystemPathTables()
{
    std::map<unsigned int, TString<0x10>> Names;                            // FUN_06f2d8d0
    INI_Reader Reader;
    OpenContentDataIni("Universe\\universe.ini", &Reader);
    unsigned int SystemId = 0;          // ESP+0x10: not initialised in retail
    while (Reader.read_header())
    {
        if (!Reader.is_header("System"))
            continue;
        while (Reader.read_value())
        {
            if (!Reader.is_value("nickname"))
                continue;
            TString<0x10> Nickname;
            Nickname = Reader.get_value_string(0);
            pub::GetSystemID(SystemId, Nickname.Data);
            Names.insert(std::map<unsigned int, TString<0x10>>::value_type(SystemId, Nickname));
        }
    }
    Reader.close();

    // 06f2c089-06f2c1d3: EBP = the first file (DAT_06fb93a0 = "wt").
    FILE* AllFile = fopen("..\\data\\Universe\\systems_shortest_path.ini", "wt");
    bool const bAllFileOpened = AllFile != nullptr;
    if (AllFile != nullptr)
    {
        WriteSystemPathFile(AllFile, true, GSystemPathsAll, Names);
        fclose(AllFile);                                                    // 06f2c1cd
    }

    // 06f2c1d6-06f2c303: EBX = the second file. Its ", %s" lines go through EBP (06f2c29f), the
    // FIRST file's FILE*, closed at 06f2c1cd; every other line of this file uses EBX. In VC6
    // msvcrt the FILE objects are slots of the static _iob[] table and fopen takes the first
    // free one (from index 3); nothing opens a stream between the fclose at 06f2c1cd and the
    // fopen at 06f2c1e0 (the universe.ini reader is memory-mapped, not a FILE), so this fopen
    // gets file 1's slot back: EBP == EBX and the path systems land in the second file, where
    // they belong. When the first fopen failed, EBP is null and the lines are not written (the
    // port does not reproduce the null-stream call; it drops them). The port never touches
    // the closed stream.
    FILE* GateFile = fopen("..\\data\\Universe\\shortest_legal_path.ini", "wt");
    if (GateFile != nullptr)
    {
        WriteSystemPathFile(GateFile, bAllFileOpened, GSystemPathsGate, Names);
        fclose(GateFile);                                                   // 06f2c2fd
    }

    // 06f2c306-06f2c44d: EBP = the third file, used for every line.
    FILE* HoleFile = fopen("..\\data\\Universe\\shortest_illegal_path.ini", "wt");
    if (HoleFile != nullptr)
    {
        WriteSystemPathFile(HoleFile, true, GSystemPathsHole, Names);
        fclose(HoleFile);                                                   // 06f2c447
    }
}
