// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail HandleConnectError (.text:005a8760, export ordinal 69): stores the DirectPlay
// connect HRESULT in .data:0067ea50 -- `MOV EAX,[ESP+4]; MOV [0067ea50],EAX; RET`, so
// __cdecl with one 4-byte argument. Its only caller is retail remoteserver.dll, which
// imports it by name (IAT .rdata:06c0b124) from the CDPClient::OnConnect override at
// .text:06bd98e0 whenever the HRESULT is non-zero. Undecorated export, hence extern "C".
extern "C" __declspec(dllexport) void __cdecl HandleConnectError(long Result);

// WaitForServerResponse (.text:005a89b0) is declared in login_mgr.h next to the globals it
// polls and defined in connect_handshake.cpp.
