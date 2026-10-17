// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "StdAfx.h"
#include "FLServerCmdLine.h"
#include "FLServer.h"        // GApp, FServerControl
#include "FLServerView.h"    // ServerLogf, ErrorCode
#include "msvcrt_compat.h"   // MsvcrtAtoi

#include <cstdio>
#include <ctime>

namespace
{
	// ServerLogf code of every message ParseParam writes (.text:004138aa, MOV EDI,0x100052).
	const ErrorCode CommandLineLogCode = { 0x100052 };

	// Logs string resource Id formatted with one argument (CString destroyed after the log).
	template <typename TArg>
	void LogCommandLineError(UINT Id, TArg Arg)
	{
		CString Format;
		LoadStringChecked(Format, Id);
		ServerLogf(CommandLineLogCode, (LPCTSTR)Format, Arg);
	}
}

// .text:00413880 — only flags are parsed, and only once the app pointer exists; everything
// else goes to CCommandLineInfo::ParseParam. Numeric switches use (MSVCRT) atoi; a rejected
// value is logged with resource strings 0xf01f..0xf024. 'C' falls through into the unknown-
// switch path (retail has no break there): it sets ServerControl's flag, logs 0xf025 and hands
// the parameter to MFC as well.
void CFLServerCommandLineInfo::ParseParam(const TCHAR* Param, BOOL bFlag, BOOL bLast)
{
	if (!bFlag || GApp == NULL)
	{
		CCommandLineInfo::ParseParam(Param, bFlag, bLast);
		return;
	}

	FServerControl& Control = GApp->ServerControl;
	switch (Param[0])
	{
	case 'B':
	case 'b':
		if (Param[1] == 0)
		{
			time_t Now;
			char Path[0x100];
			sprintf(Path, "FLServerBWLog-%u.txt", (unsigned int)time(&Now));
			Control.SetBandwidthLogPath(Path);
		}
		else
		{
			Control.SetBandwidthLogPath(Param + 1);
		}
		return;

	case 'P':
	case 'p':
	{
		const int Value = MsvcrtAtoi(Param + 1);
		if (!Control.SetServerPort((unsigned int)Value))
		{
			LogCommandLineError(0xf01f, Value);
		}
		return;
	}

	case 'L':
	case 'l':
		if (!Control.OpenLog(Param + 1))
		{
			LogCommandLineError(0xf020, Param + 1);
		}
		return;

	case 'S':
	case 's':
	{
		const int Value = MsvcrtAtoi(Param + 1);
		if (!Control.SetLogLevel(Value))
		{
			LogCommandLineError(0xf021, Value);
		}
		return;
	}

	case 'M':
	case 'm':
	{
		const int Value = MsvcrtAtoi(Param + 1);
		if (!Control.SetMaxConsoleLines((unsigned int)Value))
		{
			LogCommandLineError(0xf022, Value);
		}
		return;
	}

	case 'T':
	case 't':
	{
		const int Value = MsvcrtAtoi(Param + 1);
		if (!Control.SetConsoleTrimLines((unsigned int)Value))
		{
			LogCommandLineError(0xf023, Value);
		}
		return;
	}

	case 'H':
	case 'h':
	{
		// Rate is left unset when sscanf matches nothing, as in retail.
		float Rate;
		sscanf(Param + 1, "%f", &Rate);
		if (!Control.SetUpdateRate(Rate))
		{
			LogCommandLineError(0xf024, (double)Rate);
		}
		return;
	}

	case 'E':
	case 'e':
		Control.AddLogCategory((int)(signed char)Param[1]);
		return;

	case 'X':
	case 'x':
		if (Param[1] == '+' || Param[1] == 0)
		{
			Control.bUnmaskFpuExceptions = true;
		}
		else if (Param[1] == '-')
		{
			Control.bUnmaskFpuExceptions = false;
		}
		return;

	case 'C':
	case 'c':
		Control.SetAutoAcceptConfig(true);
		// fall through: retail continues into the unknown-switch path (00413b76).
	default:
	{
		CString Format;
		LoadStringChecked(Format, 0xf025);
		ServerLogf(CommandLineLogCode, (LPCTSTR)Format, Param);
		CCommandLineInfo::ParseParam(Param, bFlag, bLast);
		return;
	}
	}
}
