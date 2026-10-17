// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// server_savefile.cpp -- the ini-writer object and the "FLS1" container.
//
// ADDED 2026-09-17 (wave 18). See src/include/server_savefile.h for the vtable
// and layout evidence; this file carries the evidence for the behaviour.

#include "../include/server_savefile.h"

#include <windows.h>
#include <cstring>

// ---------------------------------------------------------------------------
// .rdata:DAT_06d6b30c, bytes `46 4c 53 31` read out of EXE\Server.dll. The
// listing prints it as the dword `31534C46h`, which is the same four bytes and
// is NOT how retail uses it: .text:06d4df85 PUSHes the ADDRESS and
// .text:06d4df8f calls fprintf with only the stream and that pointer, so it goes
// out as a bare format string -- four literal characters, no conversion, no
// trailing newline.
// ---------------------------------------------------------------------------
char const Server_FLS1Magic[5] = "FLS1";

// ---------------------------------------------------------------------------
// The cipher key.
//
// .rdata:06d6b318 is the start of the string
//   "Genesis - In the beginning Yahweh created the heavens and the ea..."
// and the encoder indexes it with `i & 3`, so only its first four bytes are ever
// reachable. They are `47 65 6e 65` = "Gene". The whole literal is kept here so
// the source shows the real datum rather than a four-byte transcription of it.
// ---------------------------------------------------------------------------
static char const* const FLS1KeyString =
    "Genesis - In the beginning Yahweh created the heavens and the ea";

void Server_FLS1Transform(char* Data, int Length) {
    // .text:06d48be9 `CMP EBP,EBX` / `JLE` and .text:06d48c09 `CMP EAX,EBP` /
    // `JL` are both SIGNED, so retail does nothing at all for a negative or zero
    // length. Kept as `int` for that reason rather than widened to size_t.
    if (Length <= 0)
        return;

    for (int I = 0; I < Length; ++I) {
        unsigned char const KeyByte =
            static_cast<unsigned char>(FLS1KeyString[I & 3]);
        // ADD DL,AL is an 8-bit add and wraps; OR DL,0x80 forces the top bit, so
        // the mask is never 0 and a run of identical plaintext never survives.
        unsigned char const Mask = static_cast<unsigned char>(
            static_cast<unsigned char>(KeyByte + static_cast<unsigned char>(I)) | 0x80u);
        Data[I] = static_cast<char>(static_cast<unsigned char>(Data[I]) ^ Mask);
    }
}

// ---------------------------------------------------------------------------
// SaveGame -- the { FILE*, std::string } pair (.?AUSaveGame@@).
// ---------------------------------------------------------------------------

SaveGame::SaveGame()
    : File(nullptr), Contents() {
    // FUN_06d4eac0: allocator byte (uninitialised-byte idiom), _Ptr/_Len/_Res = 0,
    // FILE* = 0. The port's std::string default constructor is the same empty
    // string.
}

void SaveGame::CloseFile() {
    // Retail FUN_06d4eae0 (.text:06d4eae0 .. .text:06d4eafa).
    if (File != nullptr) {                                  // .text:06d4eae5 TEST / JZ
        fclose(File);                                       // .text:06d4eaea
        File = nullptr;                                     // .text:06d4eaf3
    }
}

void SaveGame::ReadTail(long Offset) {
    // Retail FUN_06d48960 (.text:06d48960 .. .text:06d48b50, RET 4).
    fseek(File, 0, SEEK_END);                               // .text:06d48978
    long const Size = ftell(File) - Offset;                 // .text:06d4897d, 06d4898f SUB EDI,EAX
    fseek(File, Offset, SEEK_SET);                          // .text:06d48991
    // .text:06d4899c .. .text:06d48a40: MSVCP60's resize(Size) inlined -- erase
    // at Size when the length is not below it (CMP / JC), else an append of
    // (Size - length) zero bytes.
    Contents.resize(static_cast<size_t>(Size));
    // .text:06d48a01 .. .text:06d48a47: the writable pointer (the empty-string
    // static when _Ptr is null), then fread(p, 1, Size, File) at .text:06d48a4e.
    size_t const Read = fread(&Contents[0], 1, static_cast<size_t>(Size), File);
    // .text:06d48a56 .. .text:06d48b47: resize(Read) inlined the same way.
    Contents.resize(Read);
}

void SaveGame::Encode() {
    // Retail FUN_06d48b60 (.text:06d48b60 .. .text:06d48c10). The length is read
    // before the buffer is unshared (.text:06d48b65 `MOV EBP,[ECX + 0xc]`); the
    // unshare / freeze (.text:06d48b72 .. 06d48bdf) is MSVCP60 COW bookkeeping
    // that a non-COW std::string does not need; &Contents[0] is the same writable
    // pointer (the terminator when empty, where the signed length test stops the
    // loop before any access, as retail's empty-string static does).
    int const Length = static_cast<int>(Contents.size());
    Server_FLS1Transform(&Contents[0], Length);             // .text:06d48be7 .. 06d48c0b
}

// ---------------------------------------------------------------------------
// SaveGame_Writer
// ---------------------------------------------------------------------------

SaveGame_Writer::SaveGame_Writer()
    : SaveGame(), ISave(), Path() {
    // Retail order (.text:06d4cf1d .. .text:06d4cf4f): SaveGame's constructor
    // FUN_06d4eac0 on this+4, ISave's vptr 06d6b5c0, the path string's
    // allocator constructor, the concrete vptr 06d6b5a0. The path gets its value
    // in Open().
}

SaveGame_Writer::~SaveGame_Writer() {
    // Retail FUN_06d4c310 (vptr = .rdata:06d6b5a0, then the body): when the
    // FILE* is still set, FUN_06d48960(0), FUN_06d48b60, fclose, fopen(path,
    // "wb"), fprintf "FLS1", fwrite, fclose, FILE* = 0 -- the same effect as
    // FUN_06d4eb00, which is exactly what the inline copy of this destructor on
    // PlayerData_SerializeToFile's open-failure exit calls (.text:06d4cfcd).
    // Then the path string and the SaveGame part (string only) are torn down.
    Close();
}

bool SaveGame_Writer::Open(char const* InPath) {
    // (w4-server-guards) no null test on InPath: both inlined opens strlen the
    // path straight away (.text:06d4cf5e / .text:06d4b989 `SCASB.REPNE`) and all
    // callers pass a local char buffer.
    Path = InPath;                                           // .text:06d4cf74 assign
    // "w+t", not "w". The mode string is .rdata:DAT_06d6b598 and its real bytes
    // are `77 2b 74 00`; the listing shows only `;= 77h  w` because it truncates
    // after the first byte. The "+" is load-bearing -- Close() reads the file
    // back through this same handle -- and so is the "t": the text-mode write
    // expands "\n" to CRLF and the text-mode read-back collapses it again, so
    // the bytes that finally get encoded are LF-only. That is exactly what real
    // saves show: every character file this machine's retail client wrote is
    // LF-only, while EXE\newplayer.fl, which was encoded offline by a tool, has
    // CRLF throughout.
    File = fopen(InPath, "w+t");                             // .text:06d4cf80, stored at .text:06d4cf8b
    return File != nullptr;                                 // .text:06d4cf89 TEST / .text:06d4cf90 JNZ
}

bool SaveGame_Writer::HadWriteError() const {
    // .text:06d4df00 `MOV EAX,[ESP + 0x28]` (the FILE*) then .text:06d4df04
    // `f6400c20 TEST byte ptr [EAX + 0xc],0x20`. MSVCRT's FILE is
    // { char* _ptr; int _cnt; char* _base; int _flag; ... } so +0x0c is _flag,
    // and 0x20 is _IOERR. That is ferror(), inlined.
    // (w4-server-guards) no null test: .text:06d4df04 dereferences the FILE*
    // directly, and the only caller reaches it after a successful open
    // (.text:06d4cf90 `JNZ LAB_06d4d016`).
    return ferror(File) != 0;
}

void SaveGame_Writer::Close() {
    // Retail FUN_06d4eb00 (.text:06d4eb00 .. .text:06d4ebdd), thiscall on the
    // writer; ESI = this+4 is the SaveGame part (.text:06d4eb09 `LEA ESI,[EBP+4]`).
    if (File == nullptr)
        return;                                             // .text:06d4eb0c `JZ LAB_06d4ebdb`

    // .text:06d4eb1a .. .text:06d4eb61 -- fseek(f,0,SEEK_END), ftell,
    // fseek(f,0,SEEK_SET), resize(size) (.text:06d4eb3e), the writable pointer
    // (.text:06d4eb48), fread(p,1,size,f) (.text:06d4eb55), resize(read)
    // (.text:06d4eb61). That is FUN_06d48960 with Offset 0, inlined here (the
    // destructor's copy FUN_06d4c310 CALLs it instead, .text:06d4c34d).
    // Unconditional, as in retail: no test of ftell's result. The handle is
    // TEXT mode, so fread returns fewer bytes than ftell reported by exactly the
    // number of CRs it swallowed, and the second resize trims to that.
    ReadTail(0);

    Encode();                                               // .text:06d4eb69 CALL FUN_06d48b60 on this+4

    CloseFile();                                            // .text:06d4eb6e .. 06d4eb80, FUN_06d4eae0 inlined

    // .text:06d4eb86 .. .text:06d4ebd4 -- reopen the SAME path "wb"
    // (.rdata:DAT_06d6b4e4, real bytes `77 62 00` = "wb"), the four magic bytes
    // through fprintf as the bare format, fwrite the ciphertext, fclose,
    // FILE* = 0. Retail does not check this fopen (.text:06d4eba4 stores EAX,
    // .text:06d4eba6 hands it straight to fprintf) and the fwrite at
    // .text:06d4ebc5 is unconditional.
    RewriteEncoded();
}

void SaveGame_Writer::ReadBackAndEncode() {
    // Retail FUN_06d53b90 (.text:06d53b90 .. .text:06d53d48). Ported 2026-09-24
    // (claude-code:w5-server-06d4).
    if (File == nullptr)
        return;                                             // .text:06d53b9c JZ LAB_06d53d43

    fseek(File, 0, SEEK_END);                             // .text:06d53baf
    long const Size = ftell(File);                        // .text:06d53bb5
    fseek(File, 0, SEEK_SET);                             // .text:06d53bc9

    // .text:06d53bd4 .. .text:06d53c46: MSVCP60's resize(Size) inlined -- an
    // erase at Size when Size <= length (CMP/JA unsigned), otherwise an append of
    // (Size - length) zero bytes behind the _Xlen check.
    Contents.resize(static_cast<size_t>(Size));
    // .text:06d53c4a .. .text:06d53c6b: the writable pointer (the empty-string
    // static when _Ptr is null), then fread(p, 1, Size, File).
    size_t const Got = fread(&Contents[0], 1, static_cast<size_t>(Size), File);
    // .text:06d53c71 .. .text:06d53cdc: resize(Got) inlined the same way. The
    // handle is text mode ("w+t"), so Got is short of Size by the CRs dropped.
    Contents.resize(Got);

    // .text:06d53ce2 .. .text:06d53d41: length re-read, the COW split / freeze of
    // _Ptr, then the cipher loop -- FUN_06d48b60's body inlined (signed JLE /
    // JL on the length, key "Gene"[i & 3] + i, | 0x80, XOR).
    Server_FLS1Transform(&Contents[0], static_cast<int>(Contents.size()));
}

void SaveGame_Writer::RewriteEncoded() {
    // PlayerDB::create_restart_file .text:06d5398f .. .text:06d539e5 (and its
    // copy at .text:06d53a37 .. .text:06d53a8d).
    File = fopen(Path.c_str(), "wb");                   // .text:06d539a2, .rdata:06d6b4e4
    fprintf(File, Server_FLS1Magic);                      // .text:06d539ae, magic as the format
    fwrite(Contents.data(), 1, Contents.size(), File);    // .text:06d539d1
    fclose(File);                                         // .text:06d539dc
    File = nullptr;                                       // .text:06d539e5
}

FILE* SaveGame_Writer::GetFile() const {
    // slot +0x00, .text:06ce10f0 `MOV EAX,[ECX + 0x4]` / `RET`.
    return File;
}

void SaveGame_Writer::WriteSection(char const* Name) {
    // slot +0x04, .text:06d4c260: fprintf(file, "\n[%s]\n", Name). RET 0x4.
    // None of the six writer slots (+0x04 .. +0x18) tests the FILE*: both callers
    // only write after a successful Open (FUN_06d4ccd0 .text:06d4cf96,
    // FUN_06d4b790 .text:06d4b9f7). The port's null guards were removed 2026-09-24.
    fprintf(File, "\n[%s]\n", Name);
}

void SaveGame_Writer::WriteFloat(char const* Key, float Value) {
    // slot +0x08, .text:06d4c2e0. `FLD float ptr [ESP + 0x8]` then
    // `FSTP double ptr [ESP]` -- the argument arrives as a float and is widened
    // to a double for "%s = %g\n" (.rdata:s_%s_=_%g_06d6b5f4). RET 0x8.
    fprintf(File, "%s = %g\n", Key, static_cast<double>(Value));
}

void SaveGame_Writer::WriteUInt(char const* Key, unsigned int Value) {
    // slot +0x0c, .text:06d4c2c0, "%s = %u\n" (.rdata:s_%s_=_%u_06d6b5e8). RET 0x8.
    fprintf(File, "%s = %u\n", Key, Value);
}

void SaveGame_Writer::WriteInt(char const* Key, int Value) {
    // slot +0x10, .text:06d4c2a0, "%s = %d\n" (.rdata:s_%s_=_%d_06d6b5dc). RET 0x8.
    fprintf(File, "%s = %d\n", Key, Value);
}

void SaveGame_Writer::WriteString(char const* Key, char const* Value) {
    // slot +0x14, .text:06d4c280, "%s = %s\n" (.rdata:s_%s_=_%s_06d6b578). RET 0x8.
    // This is the slot Server_WriteCostume dispatches (`CALL dword ptr [EAX + 0x14]`
    // at .text:06d46997 and its three copies).
    fprintf(File, "%s = %s\n", Key, Value);
}

void SaveGame_Writer::WriteLine(char const* Text) {
    // slot +0x18, .text:06d4c230. Two calls, and the first one is skipped when
    // the argument is null (.text:06d4c234 `TEST EAX,EAX` / `JZ LAB_06d4c24c`):
    //   if (Text) fprintf(file, Text);        <- Text is the FORMAT, not an
    //                                            argument to one
    //   fprintf(file, "\n");                  <- .rdata:DAT_06d6af50, one byte 0x0a
    // RET 0x4.
    //
    // Retail really does pass the caller's string as the format string
    // (.text:06d4c242 PUSH EAX / .text:06d4c246 PUSH [ESI+4] / .text:06d4c247
    // CALL fprintf -- two arguments, no "%s"), so the port does too.
    if (Text != nullptr)
        fprintf(File, Text);
    fprintf(File, "\n");
}
