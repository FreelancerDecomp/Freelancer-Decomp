// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// rpc_packet_dump.cpp -- packet vtable slot +0x0c, the per-type ostream dump.
//
// WHAT RETAIL HAS. Every one of the 162 packet vtables is four slots wide
// { scalar dtor, encode, decode, dump }. Reading slot 3 out of .rdata for all 162
// constructors at once gives exactly twelve types with a body of their own and 150
// sharing one address:
//
//   .rdata:06db4434  addr .text:IServerImpl::GetServerStats   <- 150 of them
//
// That name is an ICF fold and says nothing: IServerImpl::GetServerStats is itself a
// `RET 0x4` body (06d8eae0), and so is the dump no-op, so the linker gave them one
// address. Treating the name as evidence that the dump slot "is" GetServerStats is the
// mangled-name-read-for-more-than-it-says mistake; what the bytes say is only that
// those 150 types dump nothing.
//
// The twelve that do, with the vtable address the constructor publishes:
//
//   ch2 0x01 CONNECTRESPONSE      LAB_06da8240   vtable 06db4fec, slot .rdata:06db4ff8
//   ch2 0x02 LOGINRESPONSE        LAB_06da84a0   vtable 06db4ffc, slot .rdata:06db5008
//   ch2 0x03 CHARACTERINFO        LAB_06d93310   vtable 06db3dc4, slot .rdata:06db3dd0
//   ch2 0x07 LAUNCH               FUN_06daaee0   vtable 06db50ec, slot .rdata:06db50f8
//   ch2 0x08 CHARSELECTVERIFIED   LAB_06dac020   vtable 06db517c, slot .rdata:06db5188
//   ch2 0x29 SETREPUTATION        FUN_06d92860   vtable 06db3d50, slot .rdata:06db3d5c
//   ch2 0x3c REPAIROBJECT         FUN_06dac2b0   vtable 06db524c, slot .rdata:06db5258
//   ch3 0x01 LOGIN                LAB_06daff50   vtable 06db5ca4, slot .rdata:06db5cb0
//   ch3 0x04 REQUESTLAUNCH        LAB_06daf370   vtable 06db5c14, slot .rdata:06db5c20
//   ch3 0x06 SELECTCHARACTER      LAB_06dafab0   vtable 06db5c94, slot .rdata:06db5ca0
//   ch3 0x3a DESTROYCHAR          LAB_06dafab0   vtable 06db5c84, slot .rdata:06db5c90
//   ch3 0x39 CREATENEWCHAR        LAB_06daf760   vtable 06db5c74, slot .rdata:06db5c80
//
// Eleven bodies for twelve types: LAB_06dafab0 really is one address in two vtables,
// the same way LAB_06daf830 / LAB_06dacca0 are for the codec slots. That was read out
// of .rdata, not inferred from the bodies looking alike.
//
// The vtable column is the address the constructor stores into packet+0x00 -- the LAST
// such store, not the first. THIRTEEN of the 162 constructors store two, an intermediate
// base's table and then the real one, and taking the first picks the wrong table for
// every one of them. Counted over all 162 ctors, not sampled; the list is CHARACTERINFO
// 06d9d4f0, CREATESHIP 06d9cc60, DAMAGEOBJECT 06d9c8d0, CREATELOOT 06d9d140,
// SETREPUTATION 06d9d290, SETGROUPFEELINGS 06d9c540, CREATECOUNTER 06d9cea0,
// SETMISSIONOBJECTIVES 06d9da40, BURNFUSE 06d9e5a0, CREATESOLAR 06d9e630,
// SETCOLLISIONGROUPS 06d9e8b0, MUNCOLLISION 06da0790 and REQCOLLISIONGROUPS 06da10b0.
// ALL THIRTEEN name the same intermediate table, .rdata:PTR_FUN_06db4fbc -- checked on
// each of the thirteen, not sampled; e.g. 06d9c575 then 06d9c5a5 (SETGROUPFEELINGS) and
// 06da07a7 then 06da07cc (MUNCOLLISION).
// Three of them are in this file's twelve, so this is not a footnote.
// 06db4fbc carries a FOURTEENTH reference that is NOT a constructor: 06da1370, the
// base destructor, `MOV dword ptr [param_1],.rdata:PTR_FUN_06db4fbc` then
// `JMP FUN_06d8f4c0`. A plain grep of the listing misses it -- that row prints the
// operand truncated as ".rdata:PTR_FUN_06..." -- and Ghidra's XREF[14] header is what
// catches it. See the long note in rpc_wire_generic.cpp.
//
// WHICH OSTREAM OVERLOAD EACH FIELD USES. Retail imports six of them from MSVCP60 and
// picks a different one per field; the IAT slot and the ordinal comment next to it in
// the listing name each one exactly:
//
//   .rdata:06db317c  ordinal 462  ??6std@@YAAAV?$basic_ostream@DU?$char_traits@D@std@@@0@AAV10@E@Z
//                    std::operator<<(ostream&, unsigned char)
//   .rdata:06db3180  ordinal 432  ??6?$basic_ostream@DU?$char_traits@D@std@@@std@@QAEAAV01@M@Z
//                    basic_ostream<char>::operator<<(float)
//   .rdata:06db3184  ordinal 433  ...@N@Z    operator<<(double)
//   .rdata:06db3188  ordinal 428  ...@H@Z    operator<<(int)
//   .rdata:06db318c  ordinal 439  ...@PBX@Z  operator<<(void const*)
//   .rdata:06db3194  ordinal 464  ??6std@@YAAAV?$basic_ostream@DU?$char_traits@D@std@@@0@AAV10@PBD@Z
//                    std::operator<<(ostream&, char const*)
//   .rdata:06db319c  ordinal 429  ...@I@Z    operator<<(unsigned int)
//
// The label a comment describes is the entry DIRECTLY BELOW it -- checked on the two
// unambiguous rows in the same block, `;1056 ?assign@...` above PTR_assign_06db3178 and
// `;564 ??Mstd@@...` above PTR_operator<_06db3174.
//
// THE ONE THAT MATTERS: the three wide strings are streamed through ordinal 439,
// operator<<(void const*), not through any text overload. 06daf7b1 and 06daff71 are the
// only two call sites of .rdata:06db318c in the module. VC6's ostream has no
// wchar_t const* overload, so `out << (wchar_t*)p` decays to the void const* one and
// retail's debug dump prints the ADDRESS of the account / character / pilot name, never
// its text. Reproduced, because printing the text would be a behaviour retail has not
// got.
//
// The trailing newline is .rdata:DAT_06db3d88, the two bytes `0a 00` -- a one-character
// C string, pushed to the char const* overload like any other literal, not a std::endl.
//
// NO CALLER. Nothing in rpclocal.dll calls slot +0x0c; retail's callers live in the
// network transport. The slot exists here for the same reason it exists there.

#include "rpc_containers.h"
#include "rpc_payloads.h"

#include <ostream>
#include <cstring>

// DAT_06db3d88. Spelled as a named constant so the twelve bodies below read the way the
// listing does -- every one of them pushes this same pointer.
static const char* const kRpcDumpEol = "\n";

// ---------------------------------------------------------------------------
// ch2 0x01 FLPACKET_SERVER_CONNECTRESPONSE -- LAB_06da8240.
//   06da8240 MOV EAX,dword ptr [ECX + 0x20]   the only field
//   06da8254 PUSH .rdata:s_Client_ID:_06db6263+1  "   Client ID: "
//   06da8261 CALL operator<<(unsigned int)
// ---------------------------------------------------------------------------
void RpcDumpServerConnectResponse(const FPacketBase& Packet, std::ostream& Out) {
    const unsigned int* Fields = Packet.PayloadDwords();
    Out << "   Client ID: ";                                  // 06da825a
    Out << Fields[0];                                         // 06da8261  packet+0x20
    Out << kRpcDumpEol;                                       // 06da8268
}

// ---------------------------------------------------------------------------
// ch2 0x02 FLPACKET_SERVER_LOGINRESPONSE -- LAB_06da84a0.
//   06da84a0 XOR EAX,EAX ; 06da84a2 MOV AL,byte ptr [ECX + 0x20]
//   06da84c2 CALL .rdata:06db317c, std::operator<<(ostream&, unsigned char)
// The field is read as ONE BYTE and streamed through the unsigned char overload, so it
// prints as a character, not as a number. That is retail's choice, kept.
// ---------------------------------------------------------------------------
void RpcDumpServerLoginResponse(const FPacketBase& Packet, std::ostream& Out) {
    const unsigned char Result = Packet.Payload()[0];         // 06da84a2  packet+0x20
    Out << "   Result   : ";                                  // 06da84bc
    Out << Result;                                            // 06da84c2
    Out << kRpcDumpEol;                                       // 06da84cc
}

// ---------------------------------------------------------------------------
// ch2 0x03 FLPACKET_SERVER_CHARACTERINFO -- LAB_06d93310. The only guarded dump:
//   06d93310 MOV EAX,dword ptr [ECX + 0x2c]
//   06d93313 TEST EAX,EAX
//   06d93315 JZ LAB_06d93342        <- writes nothing at all when the count is zero
// ---------------------------------------------------------------------------
void RpcDumpServerCharacterInfo(const FPacketBase& Packet, std::ostream& Out) {
    const unsigned int Count = Packet.PayloadDwords()[3];     // 06d93310  packet+0x2c
    if (Count == 0) {
        return;                                               // 06d93315
    }
    Out << "   Num Characters : ";                            // 06d9332e
    Out << Count;                                             // 06d93335
    Out << kRpcDumpEol;                                       // 06d9333c
}

// ---------------------------------------------------------------------------
// ch2 0x07 FLPACKET_SERVER_LAUNCH -- FUN_06daaee0. Three floats, each on its own line:
//   06daaeef [EDI+0x2c]  "   X: "   06daaf05 operator<<(float)
//   06daaf0e [EDI+0x30]  "   Y: "   06daaf27
//   06daaf30 [EDI+0x34]  "   Z: "   06daaf49
// packet+0x2c..+0x37 is FPayloadServerLaunch::Position, the Vector at payload+0x0c.
// ---------------------------------------------------------------------------
void RpcDumpServerLaunch(const FPacketBase& Packet, std::ostream& Out) {
    const FPayloadServerLaunch* Wire =
        reinterpret_cast<const FPayloadServerLaunch*>(Packet.Payload());
    Out << "   X: ";                                          // 06daaefe
    Out << Wire->Position.x;                                  // 06daaf05
    Out << kRpcDumpEol;                                       // 06daaf0c
    Out << "   Y: ";                                          // 06daaf20
    Out << Wire->Position.y;                                  // 06daaf27
    Out << kRpcDumpEol;                                       // 06daaf2e
    Out << "   Z: ";                                          // 06daaf42
    Out << Wire->Position.z;                                  // 06daaf49
    Out << kRpcDumpEol;                                       // 06daaf50
}

// ---------------------------------------------------------------------------
// ch2 0x08 FLPACKET_SERVER_CHARSELECTVERIFIED -- LAB_06dac020.
//
// One line, two values: `"   RepInstance: " << (unsigned)[+0x20] << " time: "
// << (double)[+0x28] << "\n"`.
//
// The second value is a DOUBLE and it is eight bytes wide. 06dac023 pushes [ECX+0x28]
// and 06dac020 pushes [ECX+0x2c]; 06dac05c then calls .rdata:06db3184, ordinal 433
// `...QAEAAV01@N@Z`, whose one stack argument is 8 bytes, so the callee consumes both
// pushes as a single double at packet+0x28..+0x2f.
//
// FPayloadServerCharSelectVerified declares that range as Field_0x08 and Field_0x0c,
// two dwords, because the sender and the receiver both move it as two dwords. The two
// readings are not in conflict -- this is the only place in the module that treats the
// pair as one scalar -- so the struct is left alone and the double is assembled here.
// ---------------------------------------------------------------------------
void RpcDumpServerCharSelectVerified(const FPacketBase& Packet, std::ostream& Out) {
    const FPayloadServerCharSelectVerified* Wire =
        reinterpret_cast<const FPayloadServerCharSelectVerified*>(Packet.Payload());
    double Time = 0.0;
    ::memcpy(&Time, &Wire->Field_0x08, sizeof(Time));         // 06dac020 / 06dac023
    Out << "   RepInstance: ";                                // 06dac047
    Out << Wire->Field_0x00;                                  // 06dac04e  packet+0x20
    Out << " time: ";                                         // 06dac055
    Out << Time;                                              // 06dac05c
    Out << kRpcDumpEol;                                       // 06dac063
}

// ---------------------------------------------------------------------------
// ch2 0x29 FLPACKET_SERVER_SETREPUTATION -- FUN_06d92860.
//   06d9286f [EDI+0x58]  "   Target     : "   06d92885 operator<<(unsigned int)
//   06d9288e [EDI+0x5c]  "   Affiliation: "   06d928a7
// packet+0x58 / +0x5c are payload+0x38 / +0x3c of FPayloadServerSetReputation.
// ---------------------------------------------------------------------------
void RpcDumpServerSetReputation(const FPacketBase& Packet, std::ostream& Out) {
    const FPayloadServerSetReputation* Wire =
        reinterpret_cast<const FPayloadServerSetReputation*>(Packet.Payload());
    Out << "   Target     : ";                                // 06d9287e
    Out << Wire->VibeHandle;                                  // 06d92885  packet+0x58
    Out << kRpcDumpEol;                                       // 06d9288c
    Out << "   Affiliation: ";                                // 06d928a0
    Out << Wire->Field_0x3c;                                  // 06d928a7  packet+0x5c
    Out << kRpcDumpEol;                                       // 06d928ae
}

// ---------------------------------------------------------------------------
// ch2 0x3c FLPACKET_SERVER_REPAIROBJECT -- FUN_06dac2b0. Three lines, and the third
// uses a DIFFERENT overload from the first two:
//   06dac2bf [EDI+0x20]  "   Object ID  : "  06dac2d5 operator<<(unsigned int)  .rdata:06db319c
//   06dac2de [EDI+0x24]  "   Equip ID   : "  06dac2f7 operator<<(unsigned int)  .rdata:06db319c
//   06dac300 [EDI+0x28]  "   Points     : "  06dac319 operator<<(int)           .rdata:06db3188
// So the third field is SIGNED where the first two are unsigned. Reproduced.
// ---------------------------------------------------------------------------
void RpcDumpServerRepairObject(const FPacketBase& Packet, std::ostream& Out) {
    const unsigned int* Fields = Packet.PayloadDwords();
    Out << "   Object ID  : ";                                // 06dac2ce
    Out << Fields[0];                                         // 06dac2d5  packet+0x20
    Out << kRpcDumpEol;                                       // 06dac2dc
    Out << "   Equip ID   : ";                                // 06dac2f0
    Out << Fields[1];                                         // 06dac2f7  packet+0x24
    Out << kRpcDumpEol;                                       // 06dac2fe
    Out << "   Points     : ";                                // 06dac312
    Out << (int)Fields[2];                                    // 06dac319  packet+0x28, signed
    Out << kRpcDumpEol;                                       // 06dac320
}

// ---------------------------------------------------------------------------
// ch3 0x01 FLPACKET_CLIENT_LOGIN -- LAB_06daff50.
//   06daff60 ADD ECX,0x20        -> &AccountName, the wide string at payload+0x000
//   06daff71 CALL .rdata:06db318c, operator<<(void const*)
// ---------------------------------------------------------------------------
void RpcDumpClientLogin(const FPacketBase& Packet, std::ostream& Out) {
    const void* Account = static_cast<const void*>(Packet.Payload());   // 06daff60
    Out << "   Account: ";                                    // 06daff6a
    Out << Account;                                           // 06daff71  the ADDRESS
    Out << kRpcDumpEol;                                       // 06daff78
}

// ---------------------------------------------------------------------------
// ch3 0x04 FLPACKET_CLIENT_REQUESTLAUNCH -- LAB_06daf370.
// ---------------------------------------------------------------------------
void RpcDumpClientRequestLaunch(const FPacketBase& Packet, std::ostream& Out) {
    const unsigned int* Fields = Packet.PayloadDwords();
    Out << "   Object ID: ";                                  // 06daf38a
    Out << Fields[0];                                         // 06daf391  packet+0x20
    Out << kRpcDumpEol;                                       // 06daf398
}

// ---------------------------------------------------------------------------
// ch3 0x06 SELECTCHARACTER and ch3 0x3a DESTROYCHAR -- LAB_06dafab0, ONE body in two
// vtables (.rdata:06db5c98 and .rdata:06db5c88 hold the same address in slot +0x0c).
// ---------------------------------------------------------------------------
void RpcDumpWireCharacterIdDump(const FPacketBase& Packet, std::ostream& Out) {
    const void* Character = static_cast<const void*>(Packet.Payload());  // 06dafac0
    Out << "   Character: ";                                  // 06dafaca
    Out << Character;                                         // 06dafad0  the ADDRESS
    Out << kRpcDumpEol;                                       // 06dafad6
}

// ---------------------------------------------------------------------------
// ch3 0x39 FLPACKET_CLIENT_CREATENEWCHAR -- LAB_06daf760. Five values on ONE line, and
// only one newline, at the very end. The push order settles the field order:
//
//   06daf760  MOV EAX,dword ptr [ECX + 0xac]
//   06daf766  MOV EDX,dword ptr [ECX + 0xa8]
//   06daf76d  PUSH .rdata:DAT_06db3d88          -- the newline
//   06daf772  PUSH EAX                          -- +0xac
//   06daf773  MOV EAX,dword ptr [ECX + 0xa4]
//   06daf77f  PUSH .rdata:s_Pilot:_06db66d8
//   06daf784  PUSH EDX                          -- +0xa8
//   06daf785  MOV EDX,dword ptr [ECX + 0xa0]
//   06daf78b  PUSH .rdata:s_Pkg:_06db66cc
//   06daf790  PUSH EAX                          -- +0xa4
//   06daf791  MOV EAX,dword ptr [ESP + 0x20]    -- the stream
//   06daf795  PUSH .rdata:s_Base:_06db66c0
//   06daf79a  PUSH EDX                          -- +0xa0
//   06daf79b  PUSH .rdata:s_Fac:_06db66b4
//   06daf7a0  ADD ECX,0x20
//   06daf7a3  PUSH ECX                          -- &Name
//   06daf7a4  PUSH .rdata:s_Name:_06db66a7+1
//   06daf7a9  PUSH EAX                          -- the stream
//
// so the chain unwinds Name, Fac, Base, Pkg, Pilot. The name goes through
// operator<<(void const*) at 06daf7b1; the four dwords through operator<<(unsigned int)
// at 06daf7bf / 06daf7cd / 06daf7db / 06daf7e9.
// ---------------------------------------------------------------------------
void RpcDumpClientCreateNewChar(const FPacketBase& Packet, std::ostream& Out) {
    const FPayloadClientCreateNewChar* Wire =
        reinterpret_cast<const FPayloadClientCreateNewChar*>(Packet.Payload());
    Out << "   Name: ";                                       // 06daf7aa
    Out << static_cast<const void*>(Wire->Name);              // 06daf7b1  the ADDRESS
    Out << "   Fac: ";                                        // 06daf7b8
    Out << Wire->Field_0x80;                                  // 06daf7bf  packet+0xa0
    Out << "   Base: ";                                       // 06daf7c6
    Out << Wire->Field_0x84;                                  // 06daf7cd  packet+0xa4
    Out << "   Pkg: ";                                        // 06daf7d4
    Out << Wire->Field_0x88;                                  // 06daf7db  packet+0xa8
    Out << "   Pilot: ";                                      // 06daf7e2
    Out << Wire->Field_0x8c;                                  // 06daf7e9  packet+0xac
    Out << kRpcDumpEol;                                       // 06daf7f0
}
