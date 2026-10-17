// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail server-module plumbing (source "network\ServerConnect.cpp" per the FDUMP in
// FUN_005b6f40) and the start / stop of the server connection.
//   FUN_005b6f40  LoadServerModule        FUN_005b6fb0  GetLoadedServer
//   FUN_005b6fc0  GetLoadedChatServer     FUN_005b6fd0  FreeServerModule
//   FUN_005a8080  StartServer             FUN_005a8260  ShutdownServerConnection
// The loaded interfaces live in .data:0067ed58 / 0067ed5c; the ones the client talks to are
// G_Server / G_ChatServer (.data:0067ecd0 / 0067ecd4, login_mgr.h).

struct IServer;

IServer* LoadServerModule(char const* DllName);
IServer* GetLoadedServer();
void* GetLoadedChatServer();
void FreeServerModule();

bool StartServer(char const* IniPath);
void ShutdownServerConnection();

// Accessors for the port's callers that still take untyped pointers (G_Server / G_ChatServer).
void* GetLocalServerInterface();
void* GetLocalChatServerInterface();

// The server half of retail FUN_005b2890 (.text:005b293d-005b2970): when G_Server is set and
// FUN_0042d740 does not hold the local server, Update (+0x2c) then ElapseTime (+0x30).
void TickLocalServer(float DeltaSeconds);
