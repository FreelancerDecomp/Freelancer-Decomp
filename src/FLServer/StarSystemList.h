// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Star-system name and list helpers (retail .text:00417700 / 004177d0, the resource-strings TU).
// Implemented in StarSystemList.cpp, a non-MFC translation unit (see the note there).
#pragma once

class IFLListBox;

// .text:00417700 (cdecl) — the system's display name (strid_name through the resource DLLs)
// into Buf, or L"???" when Server.dll's SysDB has no such system. Under the server lock.
void GetSystemName(unsigned int SystemId, wchar_t* Buf, int BufChars);

// .text:004177d0 (cdecl) — walks the Universe systems and adds the name of every system that
// SysDB knows and whose Players.CountPlayersInSystem is >= MinPlayers to List (item data = the
// system id).
void FillStarSystemList(unsigned int MinPlayers, IFLListBox* List);
