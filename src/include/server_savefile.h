// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#ifndef SERVER_SAVEFILE_H
#define SERVER_SAVEFILE_H

// server_savefile.h -- the ini-writer object retail's character serializer
// builds on its own stack, and the "FLS1" container every saved character file
// is wrapped in.
//
// ADDED 2026-09-17 (wave 18). Everything here is read off EXE\Disasm\Server.dll.txt
// and, where the listing truncates an operand, off the bytes of EXE\Server.dll
// itself. Nothing here comes from the .c decompilation.
//
// ---------------------------------------------------------------------------
// THE TWO VTABLES
//
// .rdata:PTR_LAB_06d6b5c0 is seven dwords, every one of them .text:LAB_06d5fff0,
// and .text:06d5fff0 is `ff258c4cd606  JMP dword ptr [->MSVCRT.DLL::purecall]`.
// Seven purecall slots means an abstract base with seven pure virtuals and no
// virtual destructor -- there is no eighth slot and no deleting-destructor slot.
// That base is ISave below.
//
// .rdata:PTR_GetID_06d6b5a0 is the concrete override table:
//   +0x00  .text:06ce10f0   `8b4104 MOV EAX,dword ptr [ECX + 0x4]` / `c3 RET`
//   +0x04  .text:06d4c260   fprintf(file, "\n[%s]\n", a)            RET 0x4
//   +0x08  .text:06d4c2e0   fprintf(file, "%s = %g\n", a, (double)b) RET 0x8
//   +0x0c  .text:06d4c2c0   fprintf(file, "%s = %u\n", a, b)        RET 0x8
//   +0x10  .text:06d4c2a0   fprintf(file, "%s = %d\n", a, b)        RET 0x8
//   +0x14  .text:06d4c280   fprintf(file, "%s = %s\n", a, b)        RET 0x8
//   +0x18  .text:06d4c230   if (a) fprintf(file, a); fprintf(file, "\n")  RET 0x4
// The `RET n` of each one is what fixes its arity; Ghidra's own signatures were
// not used. Every one of them reads the FILE* from `[ECX + 0x4]`
// (e.g. .text:06d4c289 `8b4104 MOV EAX,dword ptr [ECX + 0x4]`), which is what
// puts the FILE* at +0x04 and the vptr at +0x00.
//
// SLOT 0 IS NOT CPlayerGroup::GetID. Ghidra prints .text:06ce10f0 as
// `CPlayerGroup::GetID` because `MOV EAX,[ECX+4] / RET` is three bytes that the
// linker folded across every class that has such an accessor. An ICF-folded name
// does not identify its owner. Here the dword at +0x04 is the FILE* -- proved by
// the six sibling slots that fprintf to exactly that dword -- so slot 0 is this
// class's FILE* accessor, whatever the fold named it.
//
// ---------------------------------------------------------------------------
// THE CLASS HIERARCHY (RTTI, bytes read out of EXE\Server.dll)
//
// The COL in front of the concrete table, .rdata:06d6b59c, is 06d70a5c:
//   COL  06d70a5c  -> TypeDescriptor ".?AUSaveGame_Writer@@", CHD 06d70a4c
//   CHD  06d70a4c  attributes 1 (multiple inheritance), 3 bases, BCA 06d70a3c
//   BCA  06d70a3c  = [ BCD 06d70a24 SaveGame_Writer  contained 2, PMD(0,-1,0)
//                      BCD 06d70a0c SaveGame         contained 0, PMD(4,-1,0)
//                      BCD 06d709f4 ISave            contained 0, PMD(0,-1,0) ]
// and the COL in front of the purecall table, .rdata:06d6b5bc, is 06d709e0
// (".?AUISave@@", CHD 06d709d0, one base). So the concrete writer is
// `struct SaveGame_Writer : SaveGame, ISave` -- the BCA lists bases in
// declaration order, SaveGame first -- and the compiler moves the polymorphic
// ISave to +0 and the non-polymorphic SaveGame to +4, exactly as the PMDs say.
// The constructor order agrees: every inlined writer constructor builds the
// SaveGame part FIRST (.text:06d4cf25 CALL FUN_06d4eac0 on writer+4, and
// .text:06d5371e in PlayerDB::create_restart_file), then stamps ISave's table
// (.text:06d4cf41 / .text:06d53738), then the path member, then the concrete
// table (.text:06d4cf4f / .text:06d5374a). All four vptr operands are truncated
// in the listing; the real bytes are `c7 44 24 28 c0 b5 d6 06`,
// `c7 44 24 18 c0 b5 d6 06`, `c7 44 24 24 a0 b5 d6 06` and
// `c7 44 24 14 a0 b5 d6 06`.
//
// THE OBJECT LAYOUT (retail, MSVCP60 std::string = 0x10 bytes)
//   +0x00  vptr (ISave)  06d6b5c0 during construction, then 06d6b5a0
//   +0x04  SaveGame      FILE* at +0x04 (.text:06d4cf8b stores fopen's result),
//                        its std::string buffer at +0x08 (_Ptr +0x0c, _Len +0x10:
//                        .text:06d4df91 / .text:06d4dfa5 are the fwrite data and
//                        count)
//   +0x18  std::string   the path (_Ptr +0x1c, .text:06d4df6c)
// The port keeps the hierarchy and the member order; the byte offsets differ
// only because std::string is not MSVCP60's here. Nothing outside Server.dll
// sees these classes (Common's Reputation::Save takes the ISave* only), so the
// layout is not ABI.
// ---------------------------------------------------------------------------

#include <cstdio>
#include <string>

// ---------------------------------------------------------------------------
// ISave -- retail's abstract writer base (vtable .rdata:06d6b5c0, all purecall).
//
// No virtual destructor: retail's table has exactly seven slots and none of them
// is a destructor. Adding one here would insert a slot retail does not have, so
// the one place that owns a writer owns it by value.
//
// A struct, not a class: Server's RTTI names it .?AUISave@@, and Common.dll's
// export ?Save@Reputation@@YAXPAUISave@@@Z takes `struct ISave *`. This is the
// only declaration; Common.dll's Reputation::Save includes this header.
// ---------------------------------------------------------------------------
struct ISave {
public:
    virtual FILE* GetFile() const = 0;                                  // +0x00
    virtual void WriteSection(char const* Name) = 0;                    // +0x04
    virtual void WriteFloat(char const* Key, float Value) = 0;          // +0x08
    virtual void WriteUInt(char const* Key, unsigned int Value) = 0;    // +0x0c
    virtual void WriteInt(char const* Key, int Value) = 0;              // +0x10
    virtual void WriteString(char const* Key, char const* Value) = 0;   // +0x14
    virtual void WriteLine(char const* Text) = 0;                       // +0x18

protected:
    ~ISave() {}   // non-virtual and protected: retail has no destructor slot.
};

// ---------------------------------------------------------------------------
// SaveGame -- retail .?AUSaveGame@@ (TypeDescriptor .data:06d8d4b0), the
// { FILE*, std::string } pair every character-file reader and writer works on.
// Non-polymorphic: no vptr, it appears in RTTI only as SaveGame_Writer's base.
//
// Its methods, each a thiscall whose ECX is the pair itself -- the writer's
// sites pass writer+4 (.text:06d4c338 / .text:06d4eb09 `LEA ESI,[reg + 0x4]`),
// the bare-pair sites pass the pair (.text:06d53862 .. 06d5386b):
//   FUN_06d4eac0  constructor
//   FUN_06d4eae0  CloseFile
//   FUN_06d48960  ReadTail   (RET 4)
//   FUN_06d48b60  Encode
//
// NO USER DESTRUCTOR. Retail tears the SaveGame part of a writer down with the
// string destructor only: the open-failure exit of PlayerData_SerializeToFile
// runs the writer's destructor inline -- .text:06d4cfb6 concrete vptr,
// .text:06d4cfcd CALL FUN_06d4eb00, .text:06d4cfe0 _Tidy of the path,
// .text:06d4cff7 _Tidy of the buffer -- and there is no FILE* test after the
// opaque call, so SaveGame's own destructor does not fclose. The EH unwind for
// the half-built writer in PlayerDB::create_restart_file agrees: state 0
// (.text:06d5372d) is Unwind@06d63920 -> FUN_06d3cd20 on EBP-0x554 = writer+4,
// and FUN_06d3cd20 is only the _Tidy of the std::string at +4 (ICF-shared by
// every { dword, std::string } type). The closing destructor FUN_06d48c20
// belongs to FSaveFileBuffer below, not to SaveGame.
// ---------------------------------------------------------------------------
struct SaveGame {
    // FUN_06d4eac0 (.text:06d4eac0 .. .text:06d4ead8, plain RET): the string's
    // allocator byte at +4 (the `MOV CL,[ESP+3]` uninitialised-byte idiom),
    // _Ptr/_Len/_Res = 0 at +8/+0xc/+0x10, then FILE* = 0 at +0.
    SaveGame();

    // FUN_06d4eae0 (.text:06d4eae0 .. .text:06d4eafa, plain RET):
    // `if (File) { fclose(File); File = 0; }`. Callers .text:06d4e02c,
    // .text:06d53844 (bare pair), .text:06d5398a / .text:06d53a32 (writer+4).
    void CloseFile();

    // FUN_06d48960 (.text:06d48960 .. .text:06d48b50, RET 4): fseek end, size =
    // ftell - Offset, fseek(Offset), resize(size), fread through the writable
    // pointer, resize(read). 17 call sites.
    void ReadTail(long Offset);

    // FUN_06d48b60 (.text:06d48b60 .. .text:06d48c10, plain RET): length read
    // first (.text:06d48b65 `MOV EBP,[ECX + 0xc]`), the buffer unshared, then the
    // FLS1 cipher loop over it (Server_FLS1Transform). 13 call sites.
    void Encode();

    FILE* File;              // +0x00
    std::string Contents;    // +0x04
};

// ---------------------------------------------------------------------------
// SaveGame_Writer -- retail .?AUSaveGame_Writer@@, the concrete writer
// (vtable .rdata:06d6b5a0, COL 06d70a5c). Bases in retail declaration order.
//
// Open() is retail's .text:06d4cf7a fopen; Close() is retail's .text:06d4eb00.
// ---------------------------------------------------------------------------
struct SaveGame_Writer : public SaveGame, public ISave {
public:
    SaveGame_Writer();

    // Retail FUN_06d4c310 (plain RET, own EH frame): concrete vptr, then the
    // FUN_06d4eb00 body (the inline copy at .text:06d4cfb6 .. 06d4cfcd CALLs it),
    // then the path and the SaveGame part.
    ~SaveGame_Writer();

    // .text:06d4cf7a .. .text:06d4cf86 -- fopen(path, "w+t"). The mode operand is
    // .rdata:DAT_06d6b598, which the listing prints as `;= 77h  w` because it
    // shows only the first byte. The real bytes there are `77 2b 74 00`, i.e.
    // "w+t" -- read/write text. It has to be readable: Close() reads the file
    // back through this same handle before encoding it.
    bool Open(char const* InPath);

    // .text:06d4eb00 -- read the whole file back, encode it in place, then
    // rewrite the same path as "FLS1" + ciphertext. Idempotent: a second call
    // with no open file does nothing (.text:06d4eb0c `JZ LAB_06d4ebdb`).
    void Close();

    // Retail FUN_06d53b90 (thiscall on the writer, plain RET; callers
    // PlayerDB::create_restart_file .text:06d53981 and .text:06d53a29). The
    // first half of Close() as a function of its own: with the FILE* set,
    // fseek end / ftell / fseek 0, the buffer resized to that size (inlined as
    // erase / zero-append), fread through the SAME handle, resized to what was
    // read, then the FLS1 cipher over it. It neither closes nor rewrites.
    void ReadBackAndEncode();

    // CloseFile() (FUN_06d4eae0) is SaveGame's: the writer's callers
    // (.text:06d5398a / .text:06d53a32) pass writer+4 in ECX.

    // The closeout create_restart_file inlines twice after ReadBackAndEncode +
    // CloseFile (.text:06d5398f .. 06d539e5 and .text:06d53a37 .. 06d53a8d):
    // fopen(path, "wb" .rdata:06d6b4e4), fprintf(f, "FLS1") with the magic as
    // the whole format, fwrite(buffer, 1, size, f), fclose, FILE* = 0. No test
    // of the fopen result, as in retail. Not a retail function of its own.
    void RewriteEncoded();

    // .text:06d4df04 `f6400c20 TEST byte ptr [EAX + 0xc],0x20`. EAX is the FILE*,
    // +0x0c is MSVCRT's `_flag` and 0x20 is _IOERR -- an inlined ferror(). Retail
    // tests it once, right before the closeout, and FDUMPs
    // "*** WARNING: Error writing ..." from PlayerData.cpp line 0xc6d
    // (.text:06d4df19 pushes 0xc6d) when it is set.
    bool HadWriteError() const;

    FILE* GetFile() const override;                                         // +0x00
    void WriteSection(char const* Name) override;                           // +0x04
    void WriteFloat(char const* Key, float Value) override;                 // +0x08
    void WriteUInt(char const* Key, unsigned int Value) override;           // +0x0c
    void WriteInt(char const* Key, int Value) override;                     // +0x10
    void WriteString(char const* Key, char const* Value) override;          // +0x14
    void WriteLine(char const* Text) override;                              // +0x18

    // +0x04 .. +0x18 is the SaveGame base (File, Contents).
    std::string Path;        // +0x18

private:
    SaveGame_Writer(SaveGame_Writer const&);
    SaveGame_Writer& operator=(SaveGame_Writer const&);
};

// ---------------------------------------------------------------------------
// The "FLS1" container.
//
// EVERY saved character file is wrapped, not just multiplayer ones -- see the
// note on Server_FLS1Transform in server_savefile.cpp for the proof.
//
// .rdata:DAT_06d6b30c is the four bytes `46 4c 53 31` = "FLS1" (read out of
// EXE\Server.dll; the listing prints it as the dword `31534C46h`). Retail passes
// it to fprintf as the WHOLE format string, with no arguments -- .text:06d4df85
// PUSHes it and .text:06d4df8f calls fprintf with a two-argument stack.
// ---------------------------------------------------------------------------
extern char const Server_FLS1Magic[5];   // "FLS1"

// The stand-alone reading pair: FUN_06d47120 builds one at ESP+0x34,
// PlayerDB::ReadCharacterName (.text:06d52fe0) at ESP+0xc, FUN_06d4a5d0 at S+0x4c,
// FUN_06d4b790 (the template) at S+0x54, PlayerDB::create_restart_file (the
// template) at ESP+0x44.
//
// It is NOT a plain SaveGame, because its destructor closes the file and
// SaveGame's does not (see SaveGame above): FUN_06d48c20 is called out of line
// on it by create_restart_file (.text:06d539ed, .text:06d53af0) and by the EH
// unwinds Unwind@06d63080 / 06d63268 / 06d632e2 / 06d638d0 / 06d63936 -- unwind
// funclets only ever call destructors. Its constructor is SaveGame's own
// (.text:06d537f6 CALL FUN_06d4eac0 on the template), and ReadTail / Encode /
// CloseFile are called on it with the same ECX, so it is a SaveGame plus a
// closing destructor. It is non-polymorphic and never a base of a polymorphic
// class, so it leaves no RTTI and its retail name is unknown; FSaveFileBuffer is
// the port's name for it.
struct FSaveFileBuffer : public SaveGame {
    // FUN_06d48c20 (.text:06d48c20 .. .text:06d48c72, plain RET):
    // `if (File) { fclose(File); File = 0; }` (.text:06d48c23 .. .text:06d48c33,
    // CloseFile's body inlined), then the string's _Tidy -- here the base's own
    // member destructor. FUN_06d47120, FUN_06d4a5d0, FUN_06d4b790 and
    // PlayerDB::ReadCharacterName inline the same sequence at their exits.
    ~FSaveFileBuffer() {
        CloseFile();
    }
};

// Retail FUN_06d48b60, .text:06d48bf0 .. .text:06d48c0b:
//
//   MOV  BL,byte ptr [EAX + ESI*0x1]     ; in[i]
//   MOV  ECX,EAX
//   AND  ECX,0x3
//   MOV  DL,byte ptr [ECX + 0x06d6b318]  ; key[i & 3]
//   ADD  DL,AL                           ; + (unsigned char)i, 8-bit wrap
//   OR   DL,0x80
//   XOR  BL,DL
//   MOV  byte ptr [EAX + ESI*0x1],BL
//
// XOR, so the same routine encodes and decodes. The `MOV DL,[ECX + disp32]` at
// .text:06d48bf8 really is a memory load: its bytes are `8a 91 18 b3 d6 06`,
// opcode 0x8a with mod=10/rm=001, i.e. base ECX plus disp32. That check matters,
// because the sibling instruction at .text:06d4ceee looks like the same thing in
// Ghidra's output and is not -- see PlayerData_SerializeToFile.
void Server_FLS1Transform(char* Data, int Length);

// ---------------------------------------------------------------------------
// The account-path helpers and the last-written-path global, shared by the
// serializer (server_pub.cpp) and the save wrappers (server_savegame.cpp).
// ---------------------------------------------------------------------------

// Retail FUN_06d460f0: GetUserDataPath + "Accts" / "Accts\MultiPlayer" into the
// global 0x104 buffer DAT_06d90158, whose address it returns.
char const* Server_GetAccountsRoot();

// Retail FUN_06d46370: the MP account directory name, "%02x-%08x".
void Server_AccountDirName(char* Out, wchar_t const* AccountName);

// Retail CRC engine DAT_06d8fd38 as configured by LAB_06d46250 (width 31, poly
// 0x8005, reflected, XorIn/XorOut 0): reset, FUN_06d46850 over ByteCount bytes,
// FUN_06d467b0. Used for account directory names, character-ID file names and
// the account/password file XOR keys (low 16 bits of the result).
unsigned int Server_AccountCrc(unsigned char const* Bytes, unsigned int ByteCount);

// Retail DAT_06d902f8 (0x104 bytes): the path FUN_06d4ccd0 last opened for
// writing (safe_strcpy at .text:06d4d016); FUN_06d4c600 copies it to
// AutoSave.fl (.text:06d4c7e4).
extern char GServerLastSavePath[0x104];

#endif // SERVER_SAVEFILE_H
