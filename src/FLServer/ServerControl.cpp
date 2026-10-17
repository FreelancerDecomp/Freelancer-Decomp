// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "StdAfx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mbstring.h>

#include "ServerControl.h"

// .data:004278c0
unsigned int GServerVersion = 0;

// .data:00425420 -> .data:00425424 "SrvLocal-"
static const char* GLocalServerPrefix = "SrvLocal-";

// .data:00425fd8 -- an empty wide string the ctor assigns to ServerName / ServerDescription.
static const wchar_t GEmptyWide[1] = { 0 };

bool LoadStringChecked(CString& Out, UINT Id)
{
	const bool bLoaded = Out.LoadString(Id) != 0;
	if (!bLoaded)
	{
		Out = "<Invalid resource id>";
	}
	return bLoaded;
}

// .text:00401440. The order of the stores follows the listing.
FServerControl::FServerControl()
	: ServerPassword()
	, MaxConsoleLines(100000)
	, ConsoleTrimLines(200)
	, ListServerHost()
	, ServerName()
	, ServerDescription()
	, LogFile(nullptr)
	, LogCategories()
	, LogLevel(4)
	, Field_0x794(true)
	, bLogOpen(false)
	, bUnmaskFpuExceptions(false)
	, bAutoAcceptConfig(false)
{
	memset(IniFileName, 0, sizeof(IniFileName));
	memset(IniPath, 0, sizeof(IniPath));
	memset(Field_0x104, 0, sizeof(Field_0x104));
	memset(&StartupInfo, 0, sizeof(StartupInfo));
	StartupInfo.bAllowPvp = 0;
	StartupInfo.Port = 0x36d9;
	StartupInfo.bAllowNewPlayers = 1;
	memset(IniDir, 0, sizeof(IniDir));
	memset(IniDrive, 0, sizeof(IniDrive));

	strcpy(IniFileName, "freelancer.ini");
	strcpy(IniPath, "freelancer.ini");

	UpdatePeriod = 0.05f;
	memset(Title, 0, sizeof(Title));

	ListServerHost.assign("localhost");
	ListServerPort = 0x9988;
	ServerName.assign(GEmptyWide);
	ServerDescription.assign(GEmptyWide);

	memset(BandwidthLogPath, 0, sizeof(BandwidthLogPath));
	memset(LocalServerIniPath, 0, sizeof(LocalServerIniPath));

	// .text:004016fd: insert(end, 1, 0x10000) on the empty vector.
	LogCategories.insert(LogCategories.end(), 1, 0x10000);

	memset(&AppGuid, 0, sizeof(AppGuid));
}

// .text:00401730: close the log, then the members are destroyed in reverse order
// (vector +0x770, wstrings +0x75c / +0x74c, string +0x738, wstring +0x618).
FServerControl::~FServerControl()
{
	if (bLogOpen)
	{
		if (LogFile != nullptr)
		{
			delete LogFile;
		}
		LogFile = nullptr;
		bLogOpen = false;
	}
}

// .text:004018b0: copies the 16 bytes, always succeeds.
bool FServerControl::SetAppGuid(const GUID* Guid)
{
	AppGuid = *Guid;
	return true;
}

// .text:004018e0
bool FServerControl::SetIniPath(const char* Path)
{
	if (*Path == '\0')
	{
		strcpy(IniPath, IniFileName);
		strcpy(LocalServerIniPath, GLocalServerPrefix);
		strcat(LocalServerIniPath, IniFileName);
	}
	else
	{
		_splitpath(Path, IniDrive, IniDir, nullptr, nullptr);
		_makepath(IniPath, IniDrive, IniDir, IniFileName, nullptr);

		char LocalName[0x104];
		strcpy(LocalName, GLocalServerPrefix);
		strcat(LocalName, IniFileName);
		_makepath(LocalServerIniPath, IniDrive, IniDir, LocalName, nullptr);
	}
	strcpy(StartupInfo.IniPath, IniPath);
	return true;
}

// .text:00401a20. A 0x104-character path passes the length test, so retail's copy writes its
// terminator into StartupInfo.IniPath[0]; the copy goes through the struct base to keep that.
bool FServerControl::SetClientModulePath(const char* ModulePath)
{
	if (*ModulePath == '\0')
	{
		return false;
	}
	const size_t Length = strlen(ModulePath);
	if (Length > 0x104)
	{
		return false;
	}
	memcpy((char*)&StartupInfo, ModulePath, Length + 1);
	return true;
}

// .text:00401a70
bool FServerControl::SetMaxPlayers(unsigned int MaxPlayers)
{
	if (MaxPlayers == 0)
	{
		return false;
	}
	StartupInfo.MaxPlayers = MaxPlayers;
	return true;
}

// .text:00401a90. Retail stores the new stream even when the open failed (failbit set);
// bLogOpen only tracks that the allocation succeeded.
bool FServerControl::OpenLog(const char* FileName)
{
	if (bLogOpen)
	{
		if (LogFile != nullptr)
		{
			delete LogFile;
		}
		LogFile = nullptr;
		bLogOpen = false;
	}
	bLogOpen = false;
	LogFile = new std::ofstream(FileName, std::ios::out);
	if (LogFile != nullptr)
	{
		bLogOpen = true;
	}
	return bLogOpen;
}

// .text:00401c70
bool FServerControl::CloseLog()
{
	if (!bLogOpen)
	{
		return false;
	}
	if (LogFile != nullptr)
	{
		delete LogFile;
	}
	LogFile = nullptr;
	bLogOpen = false;
	return true;
}

// .text:00401cb0: ostream::write then the inlined ostream::flush.
bool FServerControl::WriteLog(const char* Data, int Count)
{
	if (!bLogOpen || LogFile == nullptr)
	{
		return false;
	}
	LogFile->write(Data, Count);
	LogFile->flush();
	return true;
}

// .text:00401d40: insert(end, 1, Value) (the vector growth is inlined in retail).
bool FServerControl::AddLogCategory(int Category)
{
	LogCategories.insert(LogCategories.end(), 1, Category);
	return true;
}

// .text:00401f00
bool FServerControl::SetLogLevel(int Level)
{
	if (Level > 0 && Level < 8)
	{
		LogLevel = Level;
		return true;
	}
	return false;
}

// .text:00401f20
bool FServerControl::SetMaxConsoleLines(unsigned int Value)
{
	if (Value != 0 && Value <= 100000)
	{
		MaxConsoleLines = Value;
		return true;
	}
	return false;
}

// .text:00401f40
bool FServerControl::SetConsoleTrimLines(unsigned int Value)
{
	if (Value != 0 && Value <= MaxConsoleLines - 1)
	{
		ConsoleTrimLines = Value;
		return true;
	}
	return false;
}

// .text:00401f70. x87: FCOMP 60.0 / TEST AH,0x41 / JNZ continues when Rate <= 60 (or unordered);
// FCOMP 1.0 / TEST AH,0x5 / JP continues when Rate is not below 1.0 (or unordered).
bool FServerControl::SetUpdateRate(float Rate)
{
	if ((double)Rate > 60.0)
	{
		return false;
	}
	if ((double)Rate < 1.0)
	{
		return false;
	}
	UpdatePeriod = (float)(1.0 / (double)Rate);
	return true;
}

// .text:00401fc0: Title = sprintf(string 0xf027, Prefix, major, minor, build), cut to 0xff bytes.
bool FServerControl::SetTitle(const char* Prefix)
{
	if (Prefix == nullptr || *Prefix == '\0')
	{
		return false;
	}

	CString Format;
	LoadStringChecked(Format, 0xf027);

	char Buffer[0x100];
	const unsigned int High = GServerVersion >> 16;
	sprintf(Buffer, (const char*)Format, Prefix, (High >> 8) & 0xff, High & 0xff, GServerVersion & 0xffff);
	if (strlen(Buffer) < 0x100)
	{
		strcpy(Title, Buffer);
	}
	else
	{
		_mbsnbcpy((unsigned char*)Title, (const unsigned char*)Buffer, 0xff);
	}
	return true;
}

// .text:004020e0
bool FServerControl::SetListServerHost(const char* HostName)
{
	ListServerHost.assign(HostName);
	return true;
}

// .text:004021a0
bool FServerControl::SetListServerPort(int Port)
{
	ListServerPort = Port;
	return true;
}

// .text:004021b0
bool FServerControl::SetServerName(const wchar_t* Name)
{
	ServerName.assign(Name);
	return true;
}

// .text:00402270
bool FServerControl::SetServerDescription(const wchar_t* Text)
{
	ServerDescription.assign(Text);
	return true;
}

// .text:00402330
bool FServerControl::SetServerPort(unsigned int Port)
{
	StartupInfo.Port = Port;
	return true;
}

// .text:00402340
void FServerControl::SetBandwidthLogPath(const char* Path)
{
	strncpy(BandwidthLogPath, Path, 0x104);
	BandwidthLogPath[0x103] = '\0';
}

// .text:00402370
void FServerControl::SetAllowNewPlayers(unsigned char Value)
{
	StartupInfo.bAllowNewPlayers = Value;
}

// .text:00402380: returns the raw byte.
unsigned char FServerControl::GetAllowNewPlayers() const
{
	return StartupInfo.bAllowNewPlayers;
}

// .text:00402390
void FServerControl::SetAllowPvp(unsigned char Value)
{
	StartupInfo.bAllowPvp = Value;
}

// .text:004023a0
unsigned char FServerControl::GetAllowPvp() const
{
	return StartupInfo.bAllowPvp;
}

// .text:004023b0
void FServerControl::SetAutoAcceptConfig(bool bValue)
{
	bAutoAcceptConfig = bValue;
}
