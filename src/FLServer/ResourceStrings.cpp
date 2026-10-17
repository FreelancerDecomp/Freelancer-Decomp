// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Plain C++ TU (no MFC): only Win32 resource calls and the std::vector of modules.
#include "ResourceStrings.h"

#include <cwchar>

std::vector<HMODULE> GResourceDlls;

// .text:00417480 — the returned base is computed before the push_back (size 0 when the vector
// has no storage yet). Retail's inlined VC6 insert grows to size + max(size, 1); the port lets
// std::vector choose its capacity, which nothing observes.
unsigned int AddResourceDll(HMODULE Module)
{
	const unsigned int IdsBase = (unsigned int)GResourceDlls.size() << 16;
	GResourceDlls.push_back(Module);
	return IdsBase;
}

// .text:00417600
int LoadResourceString(HMODULE Module, unsigned int Id, wchar_t* Buf, int BufChars)
{
	if (Buf != nullptr && BufChars < 1)
	{
		return 0;
	}

	const WORD Block = (WORD)((Id >> 4) + 1);
	unsigned int Skip = Id & 0xf;
	const wchar_t* Entry = nullptr;
	HRSRC Resource = ::FindResourceExA(Module, MAKEINTRESOURCEA(6), MAKEINTRESOURCEA(Block), 0);
	if (Resource != nullptr)
	{
		HGLOBAL Loaded = ::LoadResource(Module, Resource);
		if (Loaded != nullptr)
		{
			Entry = (const wchar_t*)::LockResource(Loaded);
		}
	}
	if (Entry == nullptr)
	{
		if (Buf != nullptr)
		{
			Buf[0] = 0;
		}
		return 0;
	}

	// Each entry is a length word followed by that many UTF-16 units.
	while (Skip != 0)
	{
		--Skip;
		Entry += *Entry + 1;
	}

	const unsigned int Length = *Entry;
	if (Buf == nullptr)
	{
		return (int)Length;
	}
	unsigned int Count = (unsigned int)(BufChars - 1);
	if (Count >= Length)
	{
		Count = Length;
	}
	wcsncpy(Buf, Entry + 1, Count);
	Buf[Count] = 0;
	return (int)Count;
}

// .text:004176b0
int LoadIdsString(void* Unused, unsigned int Ids, wchar_t* Buf, int BufChars)
{
	(void)Unused;
	const unsigned int DllIndex = Ids >> 16;
	HMODULE Module;
	unsigned int Id;
	if (DllIndex < GResourceDlls.size())
	{
		Module = GResourceDlls[DllIndex];
		Id = Ids & 0xffff;
	}
	else
	{
		Module = nullptr;
		Id = Ids;
	}
	return LoadResourceString(Module, Id, Buf, BufChars);
}
