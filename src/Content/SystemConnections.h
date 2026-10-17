// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// TU41 system connections; CNT-POP extends with FUN_06f2a400.
//
// Content.dll TU41 (06f29160-06f2c3xx): the jump-gate / jump-hole connection helpers over
// pub::System::EnumerateConnections, and the three system-path tables
//   DAT_06fd07c4 -- every connection        (ConnectionType 0, CONN_TYPE_ALL)
//   DAT_06fd07ac -- jump gates only         (ConnectionType 1, CONN_TYPE_JUMP_GATE)
//   DAT_06fd0794 -- jump holes only         (ConnectionType 2, CONN_TYPE_JUMP_HOLE)
// each a map<from system, map<to system, list<system id>>> holding the system path from
// "from" to "to". The tables are filled only by FUN_06f2a400 (CNT-POP, not ported yet), so
// until then every path lookup below finds nothing -- the retail readers are ported 1:1 and
// the filler is the open gap.

#include "common_ini.h"     // Vector, INI_Reader
#include "common_server.h"  // ConnectionType

#include <list>
#include <map>

// map<to system, list<system id>> -- the inner node's value list is the path itself.
typedef std::map<unsigned int, std::list<unsigned int>> FSystemPathsFrom;
// map<from system, FSystemPathsFrom>.
typedef std::map<unsigned int, FSystemPathsFrom> FSystemPathTable;

extern FSystemPathTable GSystemPathsAll;   // DAT_06fd07c4 (type 0)
extern FSystemPathTable GSystemPathsGate;  // DAT_06fd07ac (type 1)
extern FSystemPathTable GSystemPathsHole;  // DAT_06fd0794 (type 2)

// FUN_06f29b20 -- size of the From->To path in the table for Type, -1 if none.
int GetSystemPathLength(unsigned int FromSystemId, unsigned int ToSystemId, int Type);

// FUN_06f29c10 -- the connection object in FromSystemId leading to the second system of the
// From->To path (0 when there is none).
unsigned int GetNextHopConnectionObject(unsigned int FromSystemId, unsigned int ToSystemId, int Type);

// FUN_06f29d80 -- copy of the From->To path in the table for Type (empty if none).
std::list<unsigned int> GetSystemPath(unsigned int FromSystemId, unsigned int ToSystemId, int Type);

// FUN_06f2a190 -- position of connection object ObjectId in SystemId; 0 found, -2 not.
int GetConnectionPosition(unsigned int SystemId, unsigned int ObjectId, Vector* OutPosition);

// FUN_06f2a240 -- destination system of connection object ObjectId in SystemId; 0 found,
// -2 not.
int GetConnectionGotoSystem(unsigned int SystemId, unsigned int ObjectId, unsigned int* OutGotoSystemId);

// ---------------------------------------------------------------------------------------
// Filling the tables (lane CNT-POP).
// ---------------------------------------------------------------------------------------

// FUN_06f29a90 (cdecl): DATA\Universe\systems_shortest_path.ini exists -> LoadSystemPathTables,
// else BuildSystemPathTables(true). Caller GameSupport msg 0x40 (06effe92).
void __cdecl InitSystemPathTables();

// FUN_06f29750 (cdecl): the three tables are emptied and read back from
// systems_shortest_path.ini (every connection), shortest_legal_path.ini (jump gates) and
// shortest_illegal_path.ini (jump holes).
void __cdecl LoadSystemPathTables();

// FUN_06f29320 (cdecl): one [SystemConnections] section into Table.
void __cdecl LoadSystemPathSection(INI_Reader* Reader, FSystemPathTable* Table);

// FUN_06f2a400 (cdecl): the tables computed from the universe (a shortest-path search per
// system and connection type, locked gates left out); runs once per process (DAT_06fd0818).
// bWrite: the three ini files are written afterwards (FUN_06f2bf30).
void __cdecl BuildSystemPathTables(bool bWrite);

// FUN_06f2bf30 (cdecl): the three tables into ..\data\Universe\*.ini, systems by nickname.
void __cdecl WriteSystemPathTables();

// FUN_06f2a0b0 (cdecl): Out += the destination systems of SystemId's connections of Type
// whose object is not locked for everyone (TU53 FUN_06f6ce90 with player 0).
void __cdecl CollectOpenConnectionSystems(unsigned int SystemId, std::list<unsigned int>* Out,
                                          ConnectionType Type);

// FUN_06f2a2e0 (cdecl): Out = the first of the shortest paths of Paths (Out untouched when
// Paths is empty); returns its length, -1 when Paths is empty.
int __cdecl CopyShortestPath(std::list<std::list<unsigned int>>* Paths, std::list<unsigned int>* Out);
