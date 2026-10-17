// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Export when building Server.dll, import when another PE includes these headers.
// Server.vcxproj defines SERVER_EXPORTS. Do not define it in freelancer / other DLLs.

#ifdef SERVER_EXPORTS
#define SERVER_API __declspec(dllexport)
#else
#define SERVER_API __declspec(dllimport)
#endif

// Suppress dll-interface warnings for STL members
#pragma warning(disable : 4251)
#pragma warning(disable : 4275)
