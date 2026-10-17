// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CFLServerCommandLineInfo — the command-line parser InitInstance hands to ParseCommandLine.
// Retail: vtable .rdata:0041d1b8, ParseParam .text:00413880, dtor 0040bed0 / scalar 0040bee0
// (both compiler-generated). Switches (first character, either case) fill
// GApp->ServerControl; anything else is logged as unknown and passed to MFC.
#pragma once

class CFLServerCommandLineInfo : public CCommandLineInfo
{
public:
	virtual void ParseParam(const TCHAR* Param, BOOL bFlag, BOOL bLast);
};
