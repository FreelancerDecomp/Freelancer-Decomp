// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail ServerFilter (RTTI .?AVServerFilter@@, vftable 0x005e20a8, one slot: Match
// FUN_00562990). The multiplayer server browser's filter: PageServerSelect keeps one at +0x2a4
// (constructed by FUN_005628e0 at 0x0056d843, the version range reset by Create at
// 0x0056dc35..0x0056dc4f), hands it to CLoginMgr::GetFilteredServers (FUN_005aca40, which calls
// slot 0 on every server) and to the ServerFilterDialog (FUN_005637b0), which edits a copy and
// assigns it back (FUN_00562930).
//
// Every criterion has an enable byte (+0x04..+0x09) and a value; a server passes when every
// enabled criterion matches. Retail has no destructor for it: PageServerSelect's dtor
// (FUN_005723b0) skips +0x2a4.

struct FServerInfo;

class ServerFilter
{
public:
    // FUN_005628e0 -- all criteria off, ping 0..1000, known login 1, the version range the
    // client's own version (DAT_0067ecb8), +0x20 1, password 0, +0x22 1.
    ServerFilter();

    // FUN_00562930 (RET 4) -- every field from +0x04 on, member by member; the vftable stays.
    ServerFilter& operator=(ServerFilter const& Other);

    // slot 0 FUN_00562990 (RET 4) -- false as soon as one enabled criterion fails:
    //   +0x04: Server +0x2c (ping) outside [PingMin, PingMax] (unsigned compares)
    //   +0x05: CLoginMgr::IsKnownLogin(Server) (FUN_005a9950) != +0x14
    //   +0x06: Server +0x8c (version) outside [VersionMin, VersionMax] (unsigned)
    //   +0x07: Server +0x95 != +0x20
    //   +0x08: Server +0x94 (password) != +0x21
    //   +0x09: Server +0x3e != +0x22
    virtual bool Match(FServerInfo const& Server) const;

    unsigned char bFilterPing;      // +0x04
    unsigned char bFilterKnown;     // +0x05
    unsigned char bFilterVersion;   // +0x06
    unsigned char bFilterField95;   // +0x07
    unsigned char bFilterPassword;  // +0x08
    unsigned char bFilterField3e;   // +0x09
    unsigned PingMin;               // +0x0c -- 0
    unsigned PingMax;               // +0x10 -- 1000
    unsigned char KnownLogin;       // +0x14 -- 1
    unsigned VersionMin;            // +0x18 -- DAT_0067ecb8
    unsigned VersionMax;            // +0x1c -- DAT_0067ecb8
    unsigned char Field95;          // +0x20 -- 1. TODO: meaning of FServerInfo +0x95
    unsigned char Password;         // +0x21 -- 0
    unsigned char Field3e;          // +0x22 -- 1. TODO: meaning of FServerInfo +0x3e
};
