// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "StdAfx.h"

#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "server_playerdb.h"   // PlayerDB, Players
#include "Motd.h"

std::wstring GMotd;

void SaveMotd()
{
	char Path[0x400] = "";
	Players.MakeLocalUserPath(Path, "motd.dat");
	FILE* File = fopen(Path, "w");
	if (File == NULL)
	{
		return;
	}
	// Retail copies the text into a zeroed 0x1400-wchar stack buffer, then writes wcslen(copy)
	// characters starting at the string's own buffer.
	wchar_t Copy[0x1400] = L"";
	wcscpy(Copy, GMotd.c_str());
	fwrite(GMotd.c_str(), 2, wcslen(Copy), File);
	fclose(File);
}
