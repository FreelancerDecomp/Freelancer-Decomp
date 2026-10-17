// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// FServerControl -- the 0x9b0-byte server-host settings object FLServer keeps by value inside
// the app at CFLServerApp+0xcc (retail ctor .text:00401440, called from the app ctor at
// .text:0040b3a1; dtor .text:00401730, called from the app dtor at .text:0040b4ea).
// It owns the server log file, the freelancer.ini / SrvLocal ini paths, the list-server
// host/port, the AppGUID and the SStartupInfo the app hands to IServer::Startup.
// Retail has no RTTI or string naming the class; the name follows the app member
// (CFLServerApp::ServerControl). Field offsets are retail's; the std members are modern
// std types (no retail ABI), so offsets past +0x618 hold only in retail.
#pragma once

#include <string>
#include <vector>
#include <fstream>

#include "common_server_types.h"   // SStartupInfo

// .data:004278c0 -- the exe's packed version (written by CFLServerApp::InitInstance at
// .text:0040b99d: (major&0xff | minor<<8) << 16 | build&0xffff), read by SetTitle.
extern unsigned int GServerVersion;

// .text:0040aee0 -- CString::LoadString(Id); on failure the string becomes
// "<Invalid resource id>". Returns whether the load succeeded.
bool LoadStringChecked(CString& Out, UINT Id);

class FServerControl
{
public:
	FServerControl();   // .text:00401440
	~FServerControl();  // .text:00401730

	bool SetAppGuid(const GUID* Guid);                  // .text:004018b0
	bool SetIniPath(const char* Path);                  // .text:004018e0
	bool SetClientModulePath(const char* ModulePath);   // .text:00401a20
	bool SetMaxPlayers(unsigned int MaxPlayers);        // .text:00401a70
	bool OpenLog(const char* FileName);                 // .text:00401a90
	bool CloseLog();                                    // .text:00401c70
	bool WriteLog(const char* Data, int Count);         // .text:00401cb0
	bool AddLogCategory(int Category);                  // .text:00401d40
	bool SetLogLevel(int Level);                        // .text:00401f00
	bool SetMaxConsoleLines(unsigned int Value);        // .text:00401f20
	bool SetConsoleTrimLines(unsigned int Value);       // .text:00401f40
	bool SetUpdateRate(float Rate);                     // .text:00401f70
	bool SetTitle(const char* Prefix);                  // .text:00401fc0
	bool SetListServerHost(const char* HostName);       // .text:004020e0
	bool SetListServerPort(int Port);                   // .text:004021a0
	bool SetServerName(const wchar_t* Name);            // .text:004021b0
	bool SetServerDescription(const wchar_t* Text);     // .text:00402270
	bool SetServerPort(unsigned int Port);              // .text:00402330 (StartupInfo +0x20c)
	void SetBandwidthLogPath(const char* Path);         // .text:00402340
	void SetAllowNewPlayers(unsigned char Value);       // .text:00402370 (StartupInfo +0x211)
	unsigned char GetAllowNewPlayers() const;           // .text:00402380
	void SetAllowPvp(unsigned char Value);              // .text:00402390 (StartupInfo +0x210)
	unsigned char GetAllowPvp() const;                  // .text:004023a0
	void SetAutoAcceptConfig(bool bValue);              // .text:004023b0

public:
	char IniFileName[0x104];           // +0x000 "freelancer.ini"
	char Field_0x104[0x104];           // +0x104 zeroed by the ctor. TODO: meaning (no writer in this TU)
	char BandwidthLogPath[0x104];      // +0x208 "FLServerBWLog-%u.txt" (cmd line .text:00413907); passed to CMainFrame
	char IniDir[0x100];                // +0x30c _splitpath dir of SetIniPath's argument
	char IniDrive[3];                  // +0x40c _splitpath drive
	char IniPath[0x104];               // +0x40f full freelancer.ini path
	char LocalServerIniPath[0x104];    // +0x513 same directory, "SrvLocal-" + IniFileName
	std::wstring ServerPassword;       // +0x618 empty after the ctor; Server Configuration dialog (.text:00406d01),
	                                   //        SetRemoteClientPassword (.text:0040c080), gun report (.text:0040c851)
	bool bListServerReporting;         // +0x628 NOT touched by the ctor (static object: zero). Written by the
	                                   //        Server Configuration dialog from a checkbox (.text:00406cf0); gates
	                                   //        the gun thread (.text:0040c033) and UpdateGunReport (.text:0040c70e)
	unsigned int MaxConsoleLines;      // +0x62c 100000; SetMaxConsoleLines accepts 1..100000; ServerLogf trims
	                                   //        the console once its line count exceeds it (.text:0040e594)
	unsigned int ConsoleTrimLines;     // +0x630 200; SetConsoleTrimLines accepts 1..MaxConsoleLines-1; lines
	                                   //        TrimLines deletes (.text:0040e290, .text:0040e5b0)
	float UpdatePeriod;                // +0x634 0.05f; SetUpdateRate stores 1/Rate (Rate in 1..60)
	char Title[0x100];                 // +0x638 "<prefix> <version>" formatted by SetTitle
	std::string ListServerHost;        // +0x738 "localhost"; [ListServer] hostname (.text:0040c2d3)
	int ListServerPort;                // +0x748 0x9988; [ListServer] p (.text:0040c301)
	std::wstring ServerName;           // +0x74c Server Configuration dialog, 0x42-byte buffer
	std::wstring ServerDescription;    // +0x75c Server Configuration dialog, 0x102-byte buffer
	std::ofstream* LogFile;            // +0x76c
	std::vector<int> LogCategories;    // +0x770 starts as { 0x10000 }; command-line pushes; ServerLogf
	                                   //        accepts a non-zero-level line only if Code>>4 is listed (.text:0040e37c)
	int LogLevel;                      // +0x780 4; SetLogLevel accepts 1..7
	GUID AppGuid;                      // +0x784 [Freelancer] AppGUID (.text:0040c282)
	bool Field_0x794;                  // +0x794 true. TODO: meaning
	SStartupInfo StartupInfo;          // +0x798 handed to IServer::Startup
	bool bLogOpen;                     // +0x9ac
	bool bUnmaskFpuExceptions;         // +0x9ad false; command line (.text:00413b54/00413b60); InitInstance
	                                   //        (.text:0040bae3) and the server thread (.text:0040cc6c) unmask
	                                   //        zero-divide/overflow/invalid when set
	bool bAutoAcceptConfig;            // +0x9ae false; set by the /C command-line switch (.text:00413b71);
	                                   //        its only reader, CServerConfigDlg::OnInitDialog
	                                   //        (.text:00406b3b), then accepts a loaded FLServer.cfg
	                                   //        through OnOK without waiting for the user
};
