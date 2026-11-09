// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "SoundManagerInternal.h"
#include "msvcrt_compat.h" // MsvcrtAtoi: retail MSVCRT.DLL wraps mod 2^32, the UCRT saturates
#include "readfile.h"

#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <math.h>
#include <mmreg.h>
#include <msacm.h>
#include <new>
#include <stdint.h>
#include <stdlib.h>
#include <cmath>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "dsound.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "msacm32.lib")

namespace
{
    // DAT_06eeaf0c: initialised by every FSoundManager ctor (06ee1c79), deleted by every dtor
    // (06ee1e09).
    CRITICAL_SECTION G_InstanceLock;
    // DAT_06eeaf00 global instance list (head DAT_06eeaf04, count DAT_06eeaf08). Retail builds the
    // head node in the static initializer 06ee1740 -> 06ee1750 (alloc, self-link, count = 0) at DLL
    // load and registers 06ee17f0 through _onexit (06ee17e0); FInstanceListInit below does both.
    FSoundInstanceNode G_InstanceSentinel;
    unsigned long G_InstanceCount = 0;
    struct FInstanceListInit
    {
        FInstanceListInit()
        {
            G_InstanceSentinel.Next = &G_InstanceSentinel;
            G_InstanceSentinel.Prev = &G_InstanceSentinel;
            G_InstanceCount = 0;
        }
        ~FInstanceListInit(); // 06ee17f0, defined after ReleaseInstancePayload
    };
    FInstanceListInit G_InstanceListInit;
    HANDLE G_MixerEvent = nullptr;   // DAT_06eeaf70: CreateEventA in ctor 06ee1dc2, CloseHandle in dtor 06ee1e15
    HANDLE G_NotifyThread = nullptr; // DAT_06eeaf6c, FUN_06ee1a40
    unsigned long G_MixerStopFlag = 0; // DAT_06eeaf74, set by FUN_06ee4900, read by FUN_06ee18e0

    // DAT_06eeaf5c / DAT_06eeaf60 / DAT_06eea028 / DAT_06eeaf64 / DAT_06eeaf68 / DAT_06eea020
    unsigned long G_AllowHwToSw = 0;
    unsigned long G_AllowSwToHw = 0;
    unsigned long G_ForceFreqToSw = 1;
    unsigned long G_NeverSetStartPos = 0;
    unsigned long G_DisplayExtraDebug = 0;
    unsigned long G_Ds3dCreateFlags = 0x00040090;

    const char kDefaultDeviceId[] = "{00000000-0000-0000-0000-000000000000}";
    // s_0123456789truefalsynox.. at 06eea428 (36 chars + NUL, copied with 9 MOVSD + 1 MOVSB at
    // 06ee3788/395e/4228 and in FUN_06ee72f0). The port had dropped the trailing "OX+-", which cut
    // "0X1F" to "0" instead of reaching the hex branch.
    const char kIntCharset[] = "0123456789truefalsynoxTRUEFALSYNOX+-";
    const char kFloatCharset[] = "0123456789eE.+-";

    // Retail DAT_06ee915c / 914c / 916c — last dword CA8AC24F, not SDK 4FACC94F.
    const GUID kDs3dAlgHrtfFull = {
        0xC2413340, 0x1C1B, 0x11D2, {0x94, 0xF5, 0x00, 0xC0, 0x4F, 0xC2, 0x8A, 0xCA}};
    const GUID kDs3dAlgHrtfLight = {
        0xC2413342, 0x1C1B, 0x11D2, {0x94, 0xF5, 0x00, 0xC0, 0x4F, 0xC2, 0x8A, 0xCA}};
    const GUID kDs3dAlgNoVirtual = {
        0xC241333F, 0x1C1B, 0x11D2, {0x94, 0xF5, 0x00, 0xC0, 0x4F, 0xC2, 0x8A, 0xCA}};
    const GUID kDs3dAlgGuidNull = {0, 0, 0, {0, 0, 0, 0, 0, 0, 0, 0}};

    const unsigned long kSpeakerTable[] = {
        DSSPEAKER_MONO,                             // 0 unused by slot 7
        DSSPEAKER_HEADPHONE,                        // 1
        DSSPEAKER_STEREO,                           // 2
        DSSPEAKER_COMBINED(DSSPEAKER_STEREO, 5),    // 3
        DSSPEAKER_COMBINED(DSSPEAKER_STEREO, 10),   // 4
        DSSPEAKER_COMBINED(DSSPEAKER_STEREO, 20),   // 5
        DSSPEAKER_COMBINED(DSSPEAKER_STEREO, 180),  // 6
        DSSPEAKER_QUAD,                             // 7
        DSSPEAKER_SURROUND,                         // 8
        DSSPEAKER_5POINT1,                          // 9
        7,                                          // 10 DSSPEAKER_7POINT1
    };
    const unsigned long kSpeakerCount = sizeof(kSpeakerTable) / sizeof(kSpeakerTable[0]);

    // FUN_06ee71c0 cdecl (buffer, fmt, ...): _vsnprintf(buffer, 0x2000, fmt, args), returns
    // the buffer. No terminator is forced. Every retail message is formatted through it
    // into a 0x2000-byte stack buffer.
    char* FormatMessage2000(char* Buffer, const char* Fmt, ...)
    {
        va_list Args;
        va_start(Args, Fmt);
        _vsnprintf(Buffer, 0x2000, Fmt, Args);
        va_end(Args);
        return Buffer;
    }

    // Retail calls through the DACOM FDUMP import unconditionally (MOV EAX,[->FDUMP];
    // CALL [EAX]); dacom initialises it statically to DefaultFdump.
    void Fdump(unsigned int Flags, int Line, const char* Message)
    {
        const char* Fmt = kFdumpWarningFmt;
        if (Flags == kFdumpError)
            Fmt = kFdumpErrorFmt;
        else if (Flags == kFdumpNotice)
            Fmt = kFdumpNoticeFmt;
        FDUMP(static_cast<int>(Flags), Fmt, kRetailSource, Line, Message);
    }

    // FDUMP sites inside the statically linked WavLib report wavlib.cpp (06eeadd4).
    void FdumpWavLib(unsigned int Flags, int Line, const char* Message)
    {
        const char* Fmt = kFdumpWarningFmt;
        if (Flags == kFdumpError)
            Fmt = kFdumpErrorFmt;
        FDUMP(static_cast<int>(Flags), Fmt, kWavLibSource, Line, Message);
    }

    void FdumpCouldntCreateArchetype(int Line, int ArchId)
    {
        char Buf[0x2000];
        Fdump(kFdumpWarning, Line,
              FormatMessage2000(Buf, "SoundMgr:Couldn't create archetype %d.\n", ArchId)); // 06eea1ec
    }

    // Retail QIs compare inline (strcmp loop / REPE CMPSB) with no null checks.
    bool IidEqual(const char* Left, const char* Right)
    {
        return strcmp(Left, Right) == 0;
    }

    // 06ee7067..7085 (and the two copies after it): signed-char range tests 0-9, a-f, A-F.
    bool IsGuidHexDigit(char C)
    {
        return (C >= '0' && C <= '9') || (C >= 'a' && C <= 'f') || (C >= 'A' && C <= 'F');
    }

    // 06ee705f..7094: a field starts at the cursor, runs while hex digits last, and the cursor
    // then steps one past the stop character (a NUL included) before strtoul(field, 0, 16).
    unsigned long ParseGuidField(const char*& Cursor)
    {
        const char* Field = Cursor;
        while (*Cursor != '\0' && IsGuidHexDigit(*Cursor))
            ++Cursor;
        ++Cursor;
        return strtoul(Field, nullptr, 16);
    }

    // FUN_06ee6fd0 cdecl (text, guid; sole caller StartupDevice at 06ee3ec4 with +0x0D/+0x38).
    // The default id DAT_06eea024 -> 06eea02c "{00000000-0000-0000-0000-000000000000}" zeroes
    // the GUID. Otherwise: skip to '{' and one past it (past the NUL when there is none);
    // strcpy the rest into a 0xC8-byte stack buffer that is never read again; three
    // hex-run fields (Data1 dword, Data2/Data3 word); then Data4 as eight two-character
    // strtoul pairs, the cursor skipping one extra character after the second pair. No
    // validation and no guards, as retail.
    void ParseGuidString(const char* Text, GUID* Out)
    {
        if (strcmp(Text, kDefaultDeviceId) == 0)
        {
            memset(Out, 0, sizeof(GUID));
            return;
        }
        const char* Cursor = Text;
        while (*Cursor != '\0' && *Cursor != '{')
            ++Cursor;
        ++Cursor;
        char DeadCopy[0xC8]; // 06ee704d local_c8
        strcpy(DeadCopy, Cursor);
        const unsigned long Data1 = ParseGuidField(Cursor);
        const unsigned long Data2 = ParseGuidField(Cursor);
        const unsigned long Data3 = ParseGuidField(Cursor);
        unsigned char Data4[8];
        char Pair[3];
        Pair[0] = Cursor[0];
        Pair[1] = Cursor[1];
        Pair[2] = '\0';
        Cursor += 2;
        Data4[0] = static_cast<unsigned char>(strtoul(Pair, nullptr, 16));
        Pair[0] = Cursor[0];
        Pair[1] = Cursor[1];
        Pair[2] = '\0';
        Cursor += 3;
        Data4[1] = static_cast<unsigned char>(strtoul(Pair, nullptr, 16));
        for (int Index = 2; Index < 8; ++Index)
        {
            Pair[0] = Cursor[0];
            Pair[1] = Cursor[1];
            Pair[2] = '\0';
            Cursor += 2;
            Data4[Index] = static_cast<unsigned char>(strtoul(Pair, nullptr, 16));
        }
        Out->Data1 = Data1;
        Out->Data2 = static_cast<unsigned short>(Data2);
        Out->Data3 = static_cast<unsigned short>(Data3);
        memcpy(Out->Data4, Data4, sizeof(Data4));
    }

    // Retail StartupDevice / FUN_06ee72f0 read every INI value through IProfileParser vtbl+0x20
    // (section, key, buffer, 0x400) into one of four global scratch buffers, never cleared first:
    // DAT_06eeafa0 (integers, also FUN_06ee72f0), DAT_06eeb3c0 (reverb floats), DAT_06eeb7e0
    // (deviceID, 3D_SW_Algorithm) and DAT_06eebc00 (speakerConfiguration presence probe).
    char G_IniIntBuffer[0x420];
    char G_IniFloatBuffer[0x420];
    char G_IniStringBuffer[0x420];
    char G_IniSpeakerBuffer[0x404];

    // Inlined after every integer GetString (StartupDevice 06ee3783.., 06ee3959.., 06ee4223.. and
    // FUN_06ee72f0 06ee7384..): strspn against the 36-char set and cut there only when the span is
    // non-zero; then "true"/"yes" -> 1 and "false"/"no" -> 0 (stricmp, one call each), an 'x' or 'X'
    // anywhere -> strtoul(.., 0, 16), else atoi. The strspn(buf, "0123456789+-") retail runs just
    // before atoi has its result discarded, so nothing is cut there.
    unsigned long ParseIniUnsigned(char* Text)
    {
        const size_t Span = strspn(Text, kIntCharset);
        if (Span != 0)
            Text[Span] = '\0';
        if (_stricmp(Text, "true") == 0)
            return 1;
        if (_stricmp(Text, "yes") == 0)
            return 1;
        if (_stricmp(Text, "false") == 0)
            return 0;
        if (_stricmp(Text, "no") == 0)
            return 0;
        if (strchr(Text, 'x') != nullptr || strchr(Text, 'X') != nullptr)
            return strtoul(Text, nullptr, 16);
        return static_cast<unsigned long>(MsvcrtAtoi(Text));
    }

    // Inlined after every float GetString (06ee3b0d.., 3bee.., 3ccb.., 3da8..): strspn against
    // "0123456789eE.+-" (06eea3c4), cut when non-zero, atof, FSTP float.
    float ParseIniFloat(char* Text)
    {
        const size_t Span = strspn(Text, kFloatCharset);
        if (Span != 0)
            Text[Span] = '\0';
        return static_cast<float>(atof(Text));
    }

    // FUN_06ee74d0 thiscall (Dest, Src) RET 4: replace *Dest with Src (Release old, AddRef new)
    // unless they are equal; returns the stored pointer.
    IDAComponent* AssignComPtr(IDAComponent** Dest, IDAComponent* Src)
    {
        if (*Dest != Src)
        {
            if (*Dest != nullptr)
            {
                (*Dest)->Release();
                *Dest = nullptr;
            }
            *Dest = Src;
            if (Src != nullptr)
                Src->AddRef();
        }
        return *Dest;
    }

    // FUN_06ee7600 thiscall: Release and clear a held COM pointer.
    void ReleaseComPtr(IDAComponent** Ptr)
    {
        if (*Ptr != nullptr)
        {
            (*Ptr)->Release();
            *Ptr = nullptr;
        }
    }

    // StartupDevice's per-key prologue (inlined ten times): Local = 0; FUN_06ee74d0(&Local,
    // cached parser [+0x134]) and, when that leaves nothing, Dacom QueryInterface for
    // "1.11_IProfileParser" into Local. True when a parser is held.
    bool AcquireParserLocal(ICOManager* Dacom, IProfileParser* Cached, IDAComponent** Local)
    {
        *Local = nullptr;
        if (AssignComPtr(Local, Cached) != nullptr)
            return true;
        return Dacom->QueryInterface(Iids::ProfileParser, reinterpret_cast<void**>(Local)) == GR_OK;
    }

    // FUN_06ee72f0 cdecl (dacom, cached, section, key, default, out). Out may be null (then the
    // value lives in the 'cached' argument slot). Parser acquisition as above; when no parser is
    // obtained *Out is NOT written and its previous content is returned - StartupDevice passes one
    // stack variable to every call and reads it back, so a failed acquisition keeps the previous
    // key's value. Otherwise *Out = default, OpenSection(section, 3) (0 -> skip), GetString(key,
    // DAT_06eeafa0, 0x400) (0 -> keep default), parse, CloseSection; Release the parser; return *Out.
    unsigned long ReadProfileInt(ICOManager* Dacom, IProfileParser* Cached, const char* Section,
                                 const char* Key, unsigned long DefaultValue, unsigned long* Out)
    {
        unsigned long Fallback = 0;
        if (Out == nullptr)
            Out = &Fallback;
        IDAComponent* Local = nullptr;
        if (Cached != nullptr)
        {
            Local = Cached;
            Cached->AddRef();
        }
        else if (Dacom->QueryInterface(Iids::ProfileParser, reinterpret_cast<void**>(&Local)) != GR_OK)
            return *Out;
        IProfileParser* Parser = static_cast<IProfileParser*>(Local);
        *Out = DefaultValue;
        void* Handle = Parser->OpenSection(Section, 3);
        if (Handle != nullptr)
        {
            if (Parser->GetString(Handle, Key, G_IniIntBuffer, 0x400) != 0)
                *Out = ParseIniUnsigned(G_IniIntBuffer);
            Parser->CloseSection(Handle);
        }
        const unsigned long Value = *Out;
        Local->Release();
        return Value;
    }

    void CopyAlgGuid(unsigned long* Dest, const GUID* Src)
    {
        memcpy(Dest, Src, sizeof(GUID));
    }

    // FUN_06ee54e0 thiscall RET 0xC: write 3 dwords into Dest.
    void StoreVec3(FVec3* Dest, float X, float Y, float Z)
    {
        Dest->X = X;
        Dest->Y = Y;
        Dest->Z = Z;
    }

    // FUN_06ee5500 cdecl: Out = A - B. Each component is x87 FLD float / FSUB float / FSTP float
    // (Z and Y computed first, stored X, Y, Z) - emulated in double, then rounded to float.
    void SubVec3(FVec3* Out, const FVec3* A, const FVec3* B)
    {
        const float Z = static_cast<float>(static_cast<double>(A->Z) - static_cast<double>(B->Z));
        const float Y = static_cast<float>(static_cast<double>(A->Y) - static_cast<double>(B->Y));
        const float X = static_cast<float>(static_cast<double>(A->X) - static_cast<double>(B->X));
        Out->X = X;
        Out->Y = Y;
        Out->Z = Z;
    }

    void NoticeArch(int Line, const char* Fmt, ISoundSource* PlayKey)
    {
        if (G_DisplayExtraDebug == 0)
            return;
        // MixerTick sites 06ee5032/50d8/5128/5175/533c/53f1: GetArchId on node+8 unguarded.
        char Text[0x2000];
        Fdump(kFdumpNotice, Line, FormatMessage2000(Text, Fmt, PlayKey->GetArchId()));
    }

    // FUN_06ee5f00 thiscall RET 8 (payload at node+8, hw-used count). Returns AL.
    unsigned char MaybeMigrateHwSw(FSoundManager* Manager, FSoundInstanceNode* Node,
                                   unsigned long HwUsed)
    {
        unsigned long Flags = Node->StatusFlags;
        if ((Flags & 0x24) != 0)
            return 0;
        if (Node->NotifyEvent != nullptr)
            return 0;
        if ((Flags & 0x40) != 0)
        {
            if (G_AllowHwToSw == 0 && (Flags & 1) != 0)
                return 0;
            if (G_AllowSwToHw == 0 && (Flags & 1) == 0)
                return 0;
        }
        Flags &= ~8u;
        Node->StatusFlags = Flags;
        const unsigned char bIs3d = Node->PlayKey->Is3d();
        if (bIs3d != 0)
        {
            if ((Manager->CapsFlags & 2) != 0 && Node->DistSq < Node->MaxDistSq &&
                HwUsed < Manager->Field50)
                Node->StatusFlags |= 8;
        }
        else if ((Manager->CapsFlags & 0x20) == 0 && (Manager->CapsFlags & 1) != 0 &&
                 HwUsed < Manager->Field50)
        {
            Node->StatusFlags |= 8;
        }
        Flags = Node->StatusFlags;
        const unsigned char bNeed = static_cast<unsigned char>(((Flags >> 3) ^ Flags) & 1);
        if (bNeed == 0)
            return 0;
        Flags &= ~1u;
        Node->StatusFlags = Flags;
        if (G_DisplayExtraDebug == 0)
            return bNeed;
        char Line[0x2000];
        if (HwUsed > Manager->Field50)
        {
            Fdump(kFdumpNotice, 0xC2B,
                  FormatMessage2000(
                      Line, "Had to remove HW instance of arch %d with priority %d greater than %d.\n",
                      Node->PlayKey->GetArchId(), static_cast<int>(HwUsed),
                      static_cast<int>(Manager->Field50)));
            return bNeed;
        }
        // 06ee6014 TEST AL,1 tests EAX after the AND ~1 at 06ee5fb0, so this notice never
        // fires in retail either; kept as the listing has it.
        if ((Flags & 1) != 0)
        {
            Fdump(kFdumpNotice, 0xC30,
                  FormatMessage2000(Line, "Had to stop HW instance of arch %d to move to SW.\n",
                                    Node->PlayKey->GetArchId()));
        }
        return bNeed;
    }

    // FUN_06ee56e0 stdcall RET 4: factory Initialize hook, always 0.
    unsigned long SoundManagerFactoryInitialize(void* /*Unused*/)
    {
        return 0;
    }

    FAttachedNode* MakeAttachedSentinel()
    {
        // FUN_06ee7d80 head node (0xC); retail stores the self-links into the result unchecked.
        FAttachedNode* Node = static_cast<FAttachedNode*>(calloc(1, sizeof(FAttachedNode)));
        Node->Next = Node;
        Node->Prev = Node;
        return Node;
    }

    // ---- WavLib (statically linked into SoundManager.dll, 06ee8660..06ee8d4d) ----

    HACMDRIVER G_WavLibDriver = nullptr; // DAT_06eeaf7c

    // Instance block handed to acmDriverEnum by FUN_06ee8d00: {name to match, found id}.
    struct FAcmDriverSearch
    {
        const char* LongName;  // +0
        HACMDRIVERID DriverId; // +4
    };

    // LAB_06ee8c60 ACMDRIVERENUMCB (RET 0xC): 1 = keep enumerating, 0 = stop (match stored).
    BOOL CALLBACK WavLibAcmEnum(HACMDRIVERID DriverId, DWORD_PTR Instance, DWORD /*Support*/)
    {
        FAcmDriverSearch* Search = reinterpret_cast<FAcmDriverSearch*>(Instance);
        if (Search == nullptr || Search->LongName == nullptr) // 06ee8c6f / 06ee8c73
            return TRUE;
        ACMDRIVERDETAILSA Details;
        Details.cbStruct = 0x398;
        if (acmDriverDetailsA(DriverId, &Details, 0) != 0)
            return TRUE;
        // SOUND-ACM-DECODE-ONLY: owner-requested compatibility; remove only on owner instruction.
        if (strcmp(Search->LongName, Details.szLongName) != 0 &&
            strcmp(Details.szLongName, "Fraunhofer IIS MPEG Layer-3 Codec (decode only)") != 0)
            return TRUE;
        Search->DriverId = DriverId;
        return FALSE;
    }

    // FUN_06ee8d00 cdecl: enumerate for LongName, then open whatever id the search left
    // (still 0 when nothing matched - acmDriverOpen then fails).
    HACMDRIVER WavLibOpenAcmDriver(const char* LongName)
    {
        FAcmDriverSearch Search;
        Search.LongName = LongName;
        Search.DriverId = nullptr;
        if (acmDriverEnum(WavLibAcmEnum, reinterpret_cast<DWORD_PTR>(&Search), 0) != 0)
            return nullptr;
        HACMDRIVER Driver = nullptr;
        if (acmDriverOpen(&Driver, Search.DriverId, 0) != 0)
            return nullptr;
        return Driver;
    }

    // FUN_06ee8660: unconditional (the caller FUN_06ee86c0 tests DAT_06eeaf7c first).
    // The name is .data:06eeae38 - the "(advanced)" codec, same as SoundStreamer's.
    void WavLibInitAcmDriver()
    {
        G_WavLibDriver = WavLibOpenAcmDriver("Fraunhofer IIS MPEG Layer-3 Codec (advanced)");
        if (G_WavLibDriver == nullptr)
            FdumpWavLib(kFdumpError, 0x20, "StreamingSound: missing MP3 codec?");
    }

    // One-slot sink WavLib hands the decoded sound to (slot 0, thiscall, RET 0xC). The only
    // implementation is FUN_06ee1240's stack object (vtable PTR_LAB_06ee9210, FArchetypeWavSink).
    // It is consumed while the file mapping (PCM) or the ACM output buffer is still live.
    class FWavSink
    {
    public:
        virtual unsigned char Consume(const unsigned short* Fmt8, const void* Samples,
                                      unsigned long ByteCount) = 0;
    };

    // Stack object of FUN_06ee8960: {file system, map handle, view, mapped size, read cursor}.
    // Its destructor FUN_06ee8c00 is the EH unwind action (LAB_06ee8f88) and is inlined on every
    // normal exit as well.
    struct FWavLibMap
    {
        IFileSystem* FileSystem;   // +0x00
        int Handle;                // +0x04
        const unsigned char* View; // +0x08
        unsigned long Size;        // +0x0C
        unsigned long Cursor;      // +0x10

        // FUN_06ee8c00
        ~FWavLibMap()
        {
            if (Handle != -1 && FileSystem != nullptr)
            {
                FileSystem->CloseFile(static_cast<unsigned long>(Handle)); // +0x10
                Handle = -1;
            }
        }

        // FUN_06ee8c30 thiscall RET 8: pointer into the view, or 0 when unmapped / out of range.
        const unsigned char* Range(unsigned long Offset, unsigned long Length) const
        {
            if (Handle == -1)
                return nullptr;
            if (Offset + Length > Size)
                return nullptr;
            return View + Offset;
        }
    };

    // FUN_06ee86c0 cdecl: whole-file ACM decode of a compressed WAV; the PCM goes to the sink
    // and is freed afterwards. Trim (encoder delay) and fact (sample count) are honoured only
    // when positive.
    unsigned char WavLibAcmDecode(const WAVEFORMATEX* Src, const unsigned char* Data, unsigned long DataSize,
                                  unsigned long TrimSamples, unsigned long FactSamples, FWavSink* Sink)
    {
        if (G_WavLibDriver == nullptr) // 06ee86cc
            WavLibInitAcmDriver();
        ACMSTREAMHEADER Header;
        memset(&Header, 0, sizeof(Header)); // REP STOSD 0x15
        WAVEFORMATEX Dst;                    // retail sets only the tag before the suggest
        Dst.wFormatTag = WAVE_FORMAT_PCM;
        unsigned char bOk = 0;
        HACMSTREAM Acm = nullptr;
        if (acmFormatSuggest(G_WavLibDriver, const_cast<WAVEFORMATEX*>(Src), &Dst, 0x12,
                             ACM_FORMATSUGGESTF_WFORMATTAG) != 0)
            return 0;
        if (acmStreamOpen(&Acm, G_WavLibDriver, const_cast<WAVEFORMATEX*>(Src), &Dst, nullptr, 0, 0,
                          ACM_STREAMOPENF_NONREALTIME) != 0)
            return 0;
        Header.cbStruct = 0x54;
        Header.cbSrcLength = DataSize;
        if (DataSize < Src->nBlockAlign) // 06ee8753 JNC
            Header.cbSrcLength = Src->nBlockAlign;
        Header.cbSrcLengthUsed = 0;
        Header.cbDstLength = 0;
        if (acmStreamSize(Acm, Header.cbSrcLength, &Header.cbDstLength, ACM_STREAMSIZEF_SOURCE) == 0)
        {
            Header.cbDstLength += 0x2000;
            Header.cbDstLengthUsed = 0;
            Header.pbSrc = const_cast<unsigned char*>(Data);
            Header.pbDst = static_cast<unsigned char*>(malloc(Header.cbDstLength));
            if (Header.pbDst != nullptr && acmStreamPrepareHeader(Acm, &Header, 0) == 0)
            {
                if (acmStreamConvert(Acm, &Header, ACM_STREAMCONVERTF_END) != 0 ||
                    Header.cbDstLengthUsed == 0)
                    FdumpWavLib(kFdumpError, 0x119, "WavLib: failed to decompress sound.\n");
                else if (Header.cbSrcLength != Header.cbSrcLengthUsed)
                    FdumpWavLib(kFdumpError, 0x120, "WavLib: did not decode entire sound.\n");
                else
                {
                    unsigned long Used = Header.cbDstLengthUsed;
                    if (Used == Header.cbDstLength)
                        FdumpWavLib(kFdumpWarning, 0x129, "WavLib: possible incomplete ACM decode.\n");
                    // IMUL bits*channels; CDQ/AND 7/ADD/SAR 3 = signed division by 8.
                    const int FrameBytes =
                        (static_cast<int>(Dst.wBitsPerSample) * static_cast<int>(Dst.nChannels)) / 8;
                    const unsigned char* Pcm = Header.pbDst;
                    const int Trim = static_cast<int>(TrimSamples);
                    if (Trim > 0)
                    {
                        const unsigned long Skip = static_cast<unsigned long>(FrameBytes * Trim);
                        if (Skip < Used)
                        {
                            Pcm += Skip; // the pointer advances; nothing is moved
                            Used -= Skip;
                        }
                    }
                    const int Fact = static_cast<int>(FactSamples);
                    if (Fact > 0)
                    {
                        const unsigned long Cap = static_cast<unsigned long>(FrameBytes * Fact);
                        if (Used > Cap)
                            Used = Cap;
                    }
                    unsigned short Fmt8[4];
                    Fmt8[0] = Dst.nChannels;
                    Fmt8[1] = static_cast<unsigned short>(Dst.wBitsPerSample >> 3);
                    Fmt8[2] = static_cast<unsigned short>(Dst.nSamplesPerSec);
                    Fmt8[3] = static_cast<unsigned short>(Fmt8[1] * Fmt8[0]);
                    bOk = Sink->Consume(Fmt8, Pcm, Used);
                }
            }
        }
        // 06ee890d common exit.
        if (Acm != nullptr)
        {
            if (Header.cbStruct != 0)
                acmStreamUnprepareHeader(Acm, &Header, 0);
            if (Header.pbDst != nullptr)
            {
                free(Header.pbDst);
                Header.pbDst = nullptr;
            }
            acmStreamClose(Acm, 0);
        }
        return bOk;
    }

    // FUN_06ee8960 cdecl: map the file through the caller's IFileSystem (+0x3C/+0x40/+0x28),
    // walk RIFF/WAVE, and hand fmt/data to the sink while the mapping is open. PCM goes to the
    // sink as is; anything else through FUN_06ee86c0. Returns the sink's result, 0 on any
    // failure. No path and no other file access.
    unsigned char WavLibLoad(IFileSystem* FileSystem, FWavSink* Sink)
    {
        FWavLibMap Map;
        Map.FileSystem = FileSystem;
        Map.Handle = -1;
        Map.View = nullptr;
        Map.Size = 0;
        Map.Cursor = 0;
        if (FileSystem == nullptr)
            return 0;
        Map.Handle = FileSystem->CreateMapping(0, 0, 2, 0, 0, 0);
        if (Map.Handle == -1)
            return 0;
        Map.View = reinterpret_cast<const unsigned char*>(static_cast<uintptr_t>(static_cast<unsigned int>(
            FileSystem->MapFileView(static_cast<unsigned long>(Map.Handle), 4, 0, 0, 0))));
        Map.Size = FileSystem->GetSize(0, 0);
        Map.Cursor = 0;
        if (Map.View == nullptr)
            return 0;
        const unsigned long* Riff = reinterpret_cast<const unsigned long*>(Map.Range(0, 0xC));
        if (Riff == nullptr)
            return 0;
        Map.Cursor += 0xC;
        if (Riff[0] != 0x46464952 || Riff[2] != 0x45564157) // "RIFF" / "WAVE"
            return 0;
        // A chunk pointer addresses its 8-byte header; the last chunk of each kind wins.
        const unsigned long* FmtChunk = nullptr;  // ESI
        const unsigned long* FactChunk = nullptr; // EBP
        const unsigned long* TrimChunk = nullptr; // [ESP+0x3C]
        const unsigned long* DataChunk = nullptr; // EBX
        for (;;)
        {
            const unsigned long* Chunk = reinterpret_cast<const unsigned long*>(Map.Range(Map.Cursor, 8));
            if (Chunk == nullptr)
                break;
            Map.Cursor += 8;
            const unsigned long Id = Chunk[0];
            if (Id == 0x74636166) // "fact"
                FactChunk = Chunk;
            else if (Id == 0x6D697274) // "trim"
                TrimChunk = Chunk;
            else if (Id == 0x20746D66) // "fmt "
                FmtChunk = Chunk;
            else if (Id == 0x61746164) // "data"
                DataChunk = Chunk;
            // No even-size padding; JNS clamps a negative sum to 0, JLE clamps to the mapped size.
            long Next = static_cast<long>(Chunk[1] + Map.Cursor);
            if (Next < 0)
                Next = 0;
            if (Next > static_cast<long>(Map.Size))
                Next = static_cast<long>(Map.Size);
            Map.Cursor = static_cast<unsigned long>(Next);
        }
        if (FmtChunk == nullptr || DataChunk == nullptr)
            return 0;
        const WAVEFORMATEX* Format = reinterpret_cast<const WAVEFORMATEX*>(FmtChunk + 2);
        const unsigned short Tag = Format->wFormatTag;
        if (FmtChunk[1] < (Tag == WAVE_FORMAT_PCM ? 0xEu : 0x12u))
            return 0;
        unsigned long FactSamples = 0xFFFFFFFF;
        if (FactChunk != nullptr && FactChunk[1] >= 4)
            FactSamples = FactChunk[2];
        unsigned long TrimSamples = 0xFFFFFFFF;
        if (TrimChunk != nullptr && TrimChunk[1] == 4)
            TrimSamples = TrimChunk[2];
        const unsigned char* Samples = reinterpret_cast<const unsigned char*>(DataChunk + 2);
        if (Tag != WAVE_FORMAT_PCM)
            return WavLibAcmDecode(Format, Samples, DataChunk[1], TrimSamples, FactSamples, Sink);
        unsigned short Fmt8[4];
        Fmt8[0] = Format->nChannels;
        Fmt8[1] = static_cast<unsigned short>(Format->wBitsPerSample >> 3);
        Fmt8[2] = static_cast<unsigned short>(Format->nSamplesPerSec);
        Fmt8[3] = static_cast<unsigned short>(Fmt8[1] * Fmt8[0]);
        return Sink->Consume(Fmt8, Samples, DataChunk[1]);
    }

    // Stack object of FUN_06ee1240 (06ee126a): {vtable PTR_LAB_06ee9210, archetype, DirectSound,
    // flags}.
    class FArchetypeWavSink final : public FWavSink
    {
    public:
        FSoundArchetype* Archetype; // +0x04
        IDirectSound* DirectSound;  // +0x08
        unsigned long Flags;        // +0x0C

        // LAB_06ee12f0 thiscall RET 0xC: frames = bytes / fmt[3] (DIV, no zero test);
        // FUN_06ee1330(fmt, bytes, 0, frames, samples, DS, flags); NEG/SBB/INC -> (hr == 0).
        unsigned char Consume(const unsigned short* Fmt8, const void* Samples,
                              unsigned long ByteCount) override
        {
            const unsigned long Frames = ByteCount / Fmt8[3];
            const GENRESULT Hr =
                Archetype->FillFromPcm(Fmt8, ByteCount, 0, Frames, Samples, DirectSound, Flags);
            return Hr == GR_OK ? 1 : 0;
        }
    };

    // 06ee67d9..680d: y = 0x5F3759DF - (bits(x) SAR 1); on the FPU stack
    // Dist = (((1.5f - ((y*y)*x)*0.5f) * y) * x), stored as float. The products stay on the
    // x87 stack until the FSTP; the port is x87 too (/arch:IA32), and its double locals are
    // exact spills of those register values, so the single float rounding point is the same.
    float QuakeDistance(float DistSq)
    {
        long Bits = *reinterpret_cast<long*>(&DistSq);
        Bits = 0x5F3759DF - (Bits >> 1);
        const float Y = *reinterpret_cast<float*>(&Bits);
        const double YD = Y;
        const double XD = DistSq;
        const double Half = ((YD * YD) * XD) * static_cast<double>(0.5f);
        return static_cast<float>(((static_cast<double>(1.5f) - Half) * YD) * XD);
    }

    // 06ee6826..6858, only when DistSq > MinDistSq: on the x87 stack
    // Min / (Min + Rolloff * (Dist * DistanceFactor - Min)) -> FLDLG2 + FYL2X (log10) ->
    // FMUL double 10.0 -> FADD the client attenuation -> FSTP float. Kept as x87 code because
    // FYL2X is not bit-identical to the CRT log10. No guard on the log argument.

    float SoftwareAttenuation(
        float MinDistance,
        float Dist,
        float DistanceFactor,
        float RolloffFactor,
        float ClientAtten)
    {
        const double minDistance = MinDistance;

        const double scaledDistance =
            static_cast<double>(Dist) * DistanceFactor;

        const double denominator =
            minDistance +
            (scaledDistance - minDistance) * RolloffFactor;

        const double ratio = minDistance / denominator;

        return static_cast<float>(
            10.0 * std::log10(ratio) + ClientAtten
        );
    }


    // FUN_06ee1670 cdecl: clamp vs DAT_06ee9220=-100 / DAT_06ee9250=100,
    // return pow(DAT_06ee9248=2.0, pitch * DAT_06ee9240=0.01f). The clamp is
    // TEST AH,0x05 + JP (fall-through = less -> -100, then skip the second compare) and
    // TEST AH,0x41 + JNZ (fall-through = greater -> 100); NaN is clamped by neither. The
    // product is FMUL by the float constant without a float store, and _CIpow leaves the
    // result on the FPU stack, so the multiplier is a double.
    double PitchToMultiplier(float Pitch)
    {
        if (Pitch < -100.0f)
            Pitch = -100.0f;
        else if (Pitch > 100.0f)
            Pitch = 100.0f;
        return pow(2.0, static_cast<double>(Pitch) * static_cast<double>(0.01f));
    }

    // MSVCRT ftol: truncate toward zero into a 64-bit integer; the callers keep EAX.
    unsigned long Ftol32(double Value)
    {
        return static_cast<unsigned long>(static_cast<long long>(Value));
    }

    // FUN_06ee16c0 cdecl (archetype, play-key): archetype null -> 0; DurationMs (+0x1C) if
    // flags (+0x20) bit1 clear, GetPitch (+0x28, stdcall) nonzero, or pitch == 0.0f
    // (TEST AH,0x44 + JNP: only C3 alone skips, NaN goes on); else
    // ftol(float(DurationMs) / PitchToMultiplier) with the duration FILD qword -> FSTP float
    // and the FDIVR on the FPU stack. No play-key null check.
    unsigned long ScaledDurationMs(FSoundArchetype* Archetype, ISoundSource* PlayKey)
    {
        if (Archetype == nullptr)
            return 0;
        unsigned long Duration = Archetype->DurationMs;
        if ((Archetype->Flags & 2) == 0)
            return Duration;
        float Pitch = 0.0f;
        if (PlayKey->GetPitch(&Pitch) != 0)
            return Duration;
        if (Pitch == 0.0f)
            return Duration;
        const float DurationF = static_cast<float>(static_cast<unsigned long long>(Duration));
        return Ftol32(static_cast<double>(DurationF) / PitchToMultiplier(Pitch));
    }

    // FUN_06ee1040 thiscall: zero payload fields (not +0x18 / +0x30 / +0x34).
    void InitInstancePayload(FSoundInstanceNode* Node)
    {
        Node->PlayKey = nullptr;
        Node->Buffer = nullptr;
        Node->Buffer3d = nullptr;
        Node->NotifyEvent = nullptr;
        Node->Pad18 = 0;
        Node->FeatureFlags = 0;
        Node->StatusFlags = 0;
        Node->DsCapsFlags = 0;
        Node->DistSq = 0.0f;
        Node->MaxDistance = 0.0f;
        Node->MaxDistSq = 0.0f;
        Node->Position.X = 0.0f;
        Node->Position.Y = 0.0f;
        Node->Position.Z = 0.0f;
        Node->Direction.X = 0.0f;
        Node->Direction.Y = 0.0f;
        Node->Direction.Z = 0.0f;
        Node->bIs3dCache = 0;
        Node->LastStartTick = 0;
    }

    // FUN_06ee11a0 thiscall: Release Buffer3d, Stop+Release Buffer, Release PlayKey.
    void ReleaseInstancePayload(FSoundInstanceNode* Node)
    {
        if (Node->Buffer3d != nullptr)
        {
            Node->Buffer3d->Release();
            Node->Buffer3d = nullptr;
        }
        if (Node->Buffer != nullptr)
        {
            Node->Buffer->Stop();
            Node->Buffer->Release();
            Node->Buffer = nullptr;
        }
        if (Node->PlayKey != nullptr)
        {
            Node->PlayKey->Release();
            Node->PlayKey = nullptr;
        }
    }

    // 06ee17f0 (_onexit, once-guard byte DAT_06eeaf24): erase every node still on the DAT_06eeaf00
    // list - unlink, payload dtor FUN_06ee85e0 -> FUN_06ee11a0, node free, DAT_06eeaf08-- - then
    // release the head and zero head/count. The port's head is the embedded sentinel.
    FInstanceListInit::~FInstanceListInit()
    {
        FSoundInstanceNode* Node = G_InstanceSentinel.Next;
        while (Node != &G_InstanceSentinel)
        {
            FSoundInstanceNode* Next = Node->Next;
            Node->Prev->Next = Node->Next;
            Node->Next->Prev = Node->Prev;
            ReleaseInstancePayload(Node);
            free(Node);
            --G_InstanceCount;
            Node = Next;
        }
        G_InstanceCount = 0;
    }

    // FUN_06ee10a0 thiscall RET 4: assign payload with COM Release/AddRef.
    void AssignInstancePayload(FSoundInstanceNode* Dest, const FSoundInstanceNode* Src)
    {
        ReleaseInstancePayload(Dest);
        Dest->PlayKey = Src->PlayKey;
        if (Dest->PlayKey != nullptr)
            Dest->PlayKey->AddRef();
        Dest->Buffer = Src->Buffer;
        if (Dest->Buffer != nullptr)
            Dest->Buffer->AddRef();
        Dest->Buffer3d = Src->Buffer3d;
        if (Dest->Buffer3d != nullptr)
            Dest->Buffer3d->AddRef();
        Dest->NotifyEvent = Src->NotifyEvent;
        Dest->Pad18 = Src->Pad18;
        Dest->FeatureFlags = Src->FeatureFlags;
        Dest->CapsFlags = Src->CapsFlags;
        Dest->StatusFlags = Src->StatusFlags;
        Dest->DsCapsFlags = Src->DsCapsFlags;
        Dest->DistSq = Src->DistSq;
        Dest->MaxDistance = Src->MaxDistance;
        Dest->MaxDistSq = Src->MaxDistSq;
        Dest->MinDistance = Src->MinDistance;
        Dest->MinDistSq = Src->MinDistSq;
        Dest->Position = Src->Position;
        Dest->Direction = Src->Direction;
        Dest->bIs3dCache = Src->bIs3dCache;
        Dest->LastStartTick = Src->LastStartTick;
    }

    // FUN_06ee1080 thiscall RET 4: zero COM ptrs then FUN_06ee10a0.
    void CopyConstructInstancePayload(FSoundInstanceNode* Dest, const FSoundInstanceNode* Src)
    {
        Dest->PlayKey = nullptr;
        Dest->Buffer = nullptr;
        Dest->Buffer3d = nullptr;
        AssignInstancePayload(Dest, Src);
    }

    // FUN_06ee6410 cdecl (samples, byteCount, buffer): Lock(0, 0, &p1, &b1, 0, 0, ENTIREBUFFER);
    // failure (JL) -> FDUMP ERROR 0xD03 "Couldn't lock sound buffer.\n" (06eeac28), -1. Else
    // copy min(byteCount, b1) (REP MOVSD/MOVSB, no null test), Unlock(p1, b1, 0, 0) with its
    // result ignored, return 0.
    int UploadSoundBuffer(IDirectSoundBuffer* Buffer, const void* Samples, unsigned long ByteCount)
    {
        void* Ptr1 = nullptr;
        DWORD Bytes1 = 0;
        if (Buffer->Lock(0, 0, &Ptr1, &Bytes1, nullptr, nullptr, DSBLOCK_ENTIREBUFFER) < 0)
        {
            Fdump(kFdumpError, 0xD03, "Couldn't lock sound buffer.\n");
            return -1;
        }
        unsigned long CopyBytes = ByteCount;
        if (ByteCount > Bytes1)
            CopyBytes = Bytes1;
        memcpy(Ptr1, Samples, CopyBytes);
        Buffer->Unlock(Ptr1, Bytes1, nullptr, 0);
        return 0;
    }

    // FUN_06ee18e0 stdcall RET 4: DS-notify thread. Collect node+0x14 events +
    // DAT_06eeaf70, WFMO; looping wrap SetCurrentPosition(Pad18); else CloseHandle.
    DWORD WINAPI NotifyThreadProc(void* /*Unused*/)
    {
        HANDLE* Handles = nullptr;
        // The play-cursor out slot is the entry PUSH ECX stack slot ([ESP+0x10]): never zeroed,
        // and it keeps its value across iterations when GetCurrentPosition fails.
        DWORD PlayPos = 0;
        for (;;)
        {
            // 06ee18f0..1945: operator new((count + 1) * 4), zero it, collect node+0x14 events.
            EnterCriticalSection(&G_InstanceLock);
            const unsigned long AllocBytes = G_InstanceCount * 4u + 4u;
            Handles = static_cast<HANDLE*>(operator new(AllocBytes));
            memset(Handles, 0, AllocBytes);
            unsigned long Count = 0;
            FSoundInstanceNode* Node = G_InstanceSentinel.Next;
            while (Node != &G_InstanceSentinel)
            {
                if (Node->NotifyEvent != nullptr)
                {
                    Handles[Count] = Node->NotifyEvent;
                    ++Count;
                }
                Node = Node->Next;
            }
            LeaveCriticalSection(&G_InstanceLock);
            Handles[Count] = G_MixerEvent;
            ++Count;
            const DWORD Wait = WaitForMultipleObjects(Count, Handles, FALSE, INFINITE);
            if (Wait == WAIT_FAILED)
                break;
            HANDLE Signaled = Handles[Wait];
            if (Signaled == G_MixerEvent)
            {
                ResetEvent(G_MixerEvent); // 06ee197d
            }
            else
            {
                EnterCriticalSection(&G_InstanceLock);
                Node = G_InstanceSentinel.Next;
                while (Node != &G_InstanceSentinel)
                {
                    if (Node->NotifyEvent == Signaled)
                    {
                        if (Node->PlayKey->IsLooping() != 0)
                        {
                            // 06ee19ae: GetCurrentPosition(&play, NULL) - no write cursor;
                            // the result is not tested.
                            Node->Buffer->GetCurrentPosition(&PlayPos, nullptr);
                            if (PlayPos > Node->FeatureFlags)
                                Node->Buffer->SetCurrentPosition(Node->Pad18);
                            ResetEvent(Signaled);
                        }
                        else
                        {
                            CloseHandle(Node->NotifyEvent);
                            Node->NotifyEvent = nullptr;
                        }
                    }
                    Node = Node->Next;
                }
                LeaveCriticalSection(&G_InstanceLock);
            }
            operator delete(Handles);
            Handles = nullptr;
            if (G_MixerStopFlag != 0)
                break;
        }
        if (Handles != nullptr)
            operator delete(Handles);
        return 0;
    }

    float ClampDsDistance(float Value)
    {
        if (Value < 1.0f)
            return 1.0f;
        if (Value > 1.0e9f)
            return 1.0e9f;
        return Value;
    }

    // FUN_06ee1000 thiscall: { IDirectSoundBuffer* +0x00, DSBUFFERDESC +0x04 }.
    // Zeros 0x28 bytes, dwSize=0x24, dwFlags=CTRL3D (0x10), dwBufferBytes=4, lpwfxFormat=arg.
    struct FDsBufferDescWrapper
    {
        IDirectSoundBuffer* Buffer;
        DSBUFFERDESC Desc;
    };

    FDsBufferDescWrapper* InitDsBufferDescWrapper(FDsBufferDescWrapper* This, WAVEFORMATEX* Format)
    {
        This->Buffer = nullptr;
        memset(&This->Desc, 0, sizeof(This->Desc));
        This->Desc.dwSize = 0x24;
        This->Desc.lpwfxFormat = Format;
        This->Desc.dwFlags = 0x10;
        This->Desc.dwBufferBytes = 4;
        return This;
    }

    // FUN_06ee7600 thiscall (VA outside this lane): Release wrapper.Buffer if non-null.
    void ReleaseDsBufferWrapper(FDsBufferDescWrapper* This)
    {
        if (This->Buffer != nullptr)
        {
            This->Buffer->Release();
            This->Buffer = nullptr;
        }
    }
}

FSoundArchetype::FSoundArchetype()
    : nChannels(0), Field06(0), nSamplesPerSec(0), nBlockAlign(0), LoopStart(0), LoopEnd(0),
      NumFrames(0), Gain(0.0f), DurationMs(0), Flags(0), Buffer(nullptr), bSamplesReady(0),
      RefCount(1) // caller after FUN_06ee11f0 stores 1 at +0x2C; ctor itself does not
{
    Pad29[0] = 0;
    Pad29[1] = 0;
    Pad29[2] = 0;
}

FSoundArchetype::~FSoundArchetype()
{
    // FUN_06ee1220 thiscall: vtbl restore then Release [this+0x24].
    if (Buffer != nullptr)
    {
        Buffer->Release();
        Buffer = nullptr;
    }
}

GENRESULT DACOM_API FSoundArchetype::QueryInterface(const char* Iid, void** Out)
{
    // LAB_06ee8190 RET 0xC: walks the one-entry table DAT_06ee94b0 {"1.11_ISoundArchetype", +0}
    // (terminator 06ee94b8) with an inline strcmp; no null checks on Iid or Out. Miss -> *Out = 0,
    // return 0xFFFFFFFD; hit -> AddRef through the found interface, *Out = it, return 0.
    if (!IidEqual(Iid, Iids::SoundArchetype))
    {
        *Out = nullptr; // 06ee81e9, only after the table walk
        return GR_INTERFACE_UNSUPPORTED;
    }
    AddRef();                                 // 06ee822e, before the store
    *Out = static_cast<ISoundArchetype*>(this); // 06ee8236
    return GR_OK;
}

unsigned long DACOM_API FSoundArchetype::AddRef()
{
    ++RefCount;
    return RefCount;
}

unsigned long DACOM_API FSoundArchetype::Release()
{
    if (RefCount > 0)
        --RefCount;
    const unsigned long Remaining = RefCount;
    if (Remaining == 0)
    {
        RefCount = 1;
        delete this;
        return 0;
    }
    return Remaining;
}

GENRESULT DACOM_API FSoundArchetype::GetFormat(void* Out)
{
    // LAB_06ee1530 RET 8: copy [this+4] and [this+8] as two dwords. No null check.
    // EAX leftover is [this+8]; callers ignore HRESULT.
    memcpy(Out, &nChannels, 8);
    unsigned long Packed = 0;
    memcpy(&Packed, &nSamplesPerSec, 4);
    return static_cast<GENRESULT>(Packed);
}

GENRESULT DACOM_API FSoundArchetype::GetSamples(void* /*Out*/)
{
    // LAB_06ee1550 RET 8: FDUMP ERROR 0x204, return 0.
    Fdump(kFdumpError, 0x204, "SoundManager: get_samples() function is not supported."); // 06eea140
    return GR_OK;
}

unsigned long DACOM_API FSoundArchetype::GetNumFrames()
{
    return NumFrames;
}

float DACOM_API FSoundArchetype::GetGain()
{
    // LAB_06ee1590 RET 4: FLD [this+0x18].
    return Gain;
}

unsigned long DACOM_API FSoundArchetype::GetChannelCount()
{
    // LAB_06ee15a0 RET 4: MOVZX EAX, word [this+4].
    return nChannels;
}

unsigned long DACOM_API FSoundArchetype::GetDurationMs()
{
    // LAB_06ee15b0 RET 4: EAX = [this+0x1C].
    return DurationMs;
}

GENRESULT DACOM_API FSoundArchetype::SetSamples(void* /*A*/, void* /*B*/)
{
    // LAB_06ee15f0 RET 0xC: FDUMP ERROR 0x23A, EAX leftover.
    Fdump(kFdumpError, 0x23A, "SoundManager: function set_samples() not supported.\n"); // 06eea178
    return GR_OK;
}

GENRESULT DACOM_API FSoundArchetype::SetGain(float InGain)
{
    // LAB_06ee1620 RET 8: FCOM DAT_06ee9238 (0.0) → store 0 if >; else FCOM
    // DAT_06ee9228 (-100.0) → store DAT_06ee9220 if <; [this+0x28]=1; return 0.
    Gain = InGain;
    if (Gain > 0.0f)
        Gain = 0.0f;
    else if (Gain < -100.0f)
        Gain = -100.0f;
    bSamplesReady = 1;
    return GR_OK;
}

unsigned long DACOM_API FSoundArchetype::IsReady()
{
    // LAB_06ee15c0 RET 4: AL=1.
    return 1;
}

GENRESULT DACOM_API FSoundArchetype::GetLoopRange(unsigned long* Start, unsigned long* End)
{
    // LAB_06ee15d0 RET 0xC: no null check; EAX leftover = LoopEnd.
    *Start = LoopStart;
    *End = LoopEnd;
    return static_cast<GENRESULT>(LoopEnd);
}

GENRESULT FSoundArchetype::FillFromPcm(const unsigned short* Fmt8, unsigned long ByteCount,
                                       unsigned long InLoopStart, unsigned long InLoopEnd,
                                       const void* Samples, IDirectSound* DirectSound,
                                       unsigned long InFlags)
{
    // FUN_06ee1330 thiscall RET 0x1C.
    if (Buffer != nullptr)
    {
        Buffer->Release();
        Buffer = nullptr;
    }
    Flags = InFlags;
    memcpy(&nChannels, Fmt8, 8);
    LoopStart = InLoopStart;
    LoopEnd = InLoopEnd;
    // 06ee1374..139b: unsigned DIVs with no zero guards, as retail.
    NumFrames = ByteCount / nBlockAlign;
    DurationMs = (NumFrames * 1000) / nSamplesPerSec;

    // 06ee1385..13ed: WAVEFORMATEX {PCM, channels, rate, avg, align, bits, 0} where
    // bits = (align*8) IDIV channels, and the format's block align is NOT the input align but
    // LOWORD(ftol(FILD(bits*channels) * 0.125 (DAT_06ee9218))), avg = that word * rate.
    WAVEFORMATEX Wave;
    Wave.wFormatTag = WAVE_FORMAT_PCM;
    Wave.nChannels = nChannels;
    Wave.nSamplesPerSec = nSamplesPerSec;
    const unsigned short Bits =
        static_cast<unsigned short>(static_cast<int>(nBlockAlign) * 8 / static_cast<int>(nChannels));
    Wave.wBitsPerSample = Bits;
    const unsigned short AvgAlign = static_cast<unsigned short>(
        Ftol32(static_cast<double>(static_cast<int>(Bits) * static_cast<int>(nChannels)) * 0.125));
    Wave.nBlockAlign = AvgAlign;
    Wave.nAvgBytesPerSec = static_cast<unsigned long>(AvgAlign) * nSamplesPerSec;
    Wave.cbSize = 0;

    DSBUFFERDESC Desc = {};
    Desc.dwSize = 0x24;
    Desc.dwBufferBytes = ByteCount;
    Desc.dwFlags = G_Ds3dCreateFlags;
    Desc.lpwfxFormat = &Wave;

    if ((InFlags & 8) != 0 && (InFlags & 0x10) != 0)
    {
        Fdump(kFdumpWarning, 0x1AC, "SoundMgr:Archetype requested 3D and Pan controls, disabling pan.\n"); // 06eea0e0
        Flags &= ~0x10u;
    }
    if ((Flags & 8) != 0)
        Desc.dwFlags = (Desc.dwFlags & ~0x40u) | 0x10u;
    else if ((Flags & 0x10) != 0)
        Desc.dwFlags = (Desc.dwFlags & ~0x10u) | 0x40u;
    if ((Flags & 2) != 0)
    {
        Desc.dwFlags |= 0x20u;
        if (G_ForceFreqToSw != 0)
            Flags |= 0x20u;
    }
    if ((Flags & 1) != 0 && (Desc.dwFlags & 0x10) != 0)
        Desc.dwFlags |= 0x20000u;
    if ((Flags & 0x40) != 0 && (LoopStart != 0 || LoopEnd < NumFrames))
        Desc.dwFlags |= 0x10100u;
    if ((Flags & 0x20) != 0)
        Desc.dwFlags = (Desc.dwFlags & ~0x40000u) | 0x8u;

    if (DirectSound->CreateSoundBuffer(&Desc, &Buffer, nullptr) < 0)
    {
        Buffer = nullptr;
        return GR_GENERIC;
    }
    // FUN_06ee6410 (outside range): fail leaves Buffer, returns -1.
    if (UploadSoundBuffer(Buffer, Samples, ByteCount) < 0)
        return GR_GENERIC;
    bSamplesReady = 1;
    return GR_OK;
}

GENRESULT FSoundArchetype::FillFromWav(IDAComponent* FileSystem, IDirectSound* DirectSound,
                                       unsigned long InFlags)
{
    // FUN_06ee1240 thiscall RET 0xC: build the sink on the stack, FUN_06ee8960(FS, &sink).
    // Sink result nonzero -> 0. Otherwise FS+0x84 (name, 0x80), FUN_06ee71c0 = _vsnprintf 0x2000,
    // FDUMP WARNING 0x17D, return -1. Nothing else reads the file.
    FArchetypeWavSink Sink;
    Sink.Archetype = this;
    Sink.DirectSound = DirectSound;
    Sink.Flags = InFlags;
    IFileSystem* Fs = static_cast<IFileSystem*>(FileSystem);
    if (WavLibLoad(Fs, &Sink) != 0)
        return GR_OK;
    char Named[0x80];
    Fs->GetFileSysPath(Named, 0x80);
    char Message[0x2000];
    Fdump(kFdumpWarning, 0x17D,
          FormatMessage2000(Message, "SoundMgr:Couldn't load wav file <%s>.\n", Named));
    return GR_GENERIC;
}

GENRESULT FSoundArchetype::DuplicateBuffer(IDirectSound* DirectSound, IDirectSoundBuffer** Out) const
{
    if (Out == nullptr || DirectSound == nullptr || Buffer == nullptr)
        return GR_GENERIC;
    *Out = nullptr;
    if (FAILED(DirectSound->DuplicateSoundBuffer(Buffer, Out)) || *Out == nullptr)
        return GR_GENERIC;
    return GR_OK;
}

FSoundInner::FSoundInner() : RefCount(1), Self(nullptr) {}

GENRESULT DACOM_API FSoundInner::QueryInterface(const char* Iid, void** Out)
{
    // LAB_06ee8460 RET 0xC: walks DAT_06ee95cc {"1.11_ISoundManager", +0}, DAT_06ee95d4
    // {"1.11_ISoundManagerStatus", +4}, terminator 06ee95dc, with an inline strcmp; no null checks.
    // Hit: the interface at Self(+8)+offset is AddRef'd through its own vtable (+4) - which goes to
    // the controlling unknown - then stored, return 0. Miss: *Out = 0, return 0xFFFFFFFD.
    if (IidEqual(Iid, Iids::SoundManager))
    {
        ISoundManager* Found = static_cast<ISoundManager*>(Self);
        Found->AddRef();
        *Out = Found;
        return GR_OK;
    }
    if (IidEqual(Iid, Iids::SoundManagerStatus))
    {
        ISoundManagerStatus* Found = static_cast<ISoundManagerStatus*>(Self);
        Found->AddRef();
        *Out = Found;
        return GR_OK;
    }
    *Out = nullptr;
    return GR_INTERFACE_UNSUPPORTED;
}

unsigned long DACOM_API FSoundInner::AddRef()
{
    ++RefCount;
    return RefCount;
}

unsigned long DACOM_API FSoundInner::Release()
{
    if (RefCount > 0)
        --RefCount;
    const unsigned long Remaining = RefCount;
    if (Remaining == 0)
    {
        // LAB_06ee8520: count = 1, then if Self(+8) run the dtor FUN_06ee1de0 (which itself calls
        // ShutdownDevice) and free the object; return 0.
        RefCount = 1;
        FSoundManager* Owner = Self;
        if (Owner != nullptr)
        {
            // 06ee8547..854d: FUN_06ee1de0 then free - the object came from calloc (06ee836d).
            Owner->~FSoundManager();
            free(Owner);
        }
        return 0;
    }
    return Remaining;
}

FSoundManagerFactory::FSoundManagerFactory() : RefCount(1), Name(kComponentName) {}

GENRESULT DACOM_API FSoundManagerFactory::QueryInterface(const char* Iid, void** Out)
{
    // LAB_06ee8290 RET 0xC: *Out = 0 first, REPE CMPSB against "1.11_IComponentFactory" (0x17
    // bytes), miss -> 0xFFFFFFFD; hit -> *Out = this, AddRef through vtbl+4, return 0. No null checks.
    *Out = nullptr;
    if (!IidEqual(Iid, Iids::ComponentFactory))
        return GR_INTERFACE_UNSUPPORTED;
    *Out = static_cast<IComponentFactory*>(this);
    AddRef();
    return GR_OK;
}

unsigned long DACOM_API FSoundManagerFactory::AddRef()
{
    ++RefCount;
    return RefCount;
}

unsigned long DACOM_API FSoundManagerFactory::Release()
{
    if (RefCount > 0)
        --RefCount;
    const unsigned long Remaining = RefCount;
    if (Remaining == 0)
    {
        RefCount = 1;
        delete this;
        return 0;
    }
    return Remaining;
}

GENRESULT DACOM_API FSoundManagerFactory::CreateInstance(AGGDESC* Desc, void** Instance)
{
    // FUN_06ee8310 RET 0xC. No null checks on Desc, its name or Instance.
    FSoundAggDesc* SoundDesc = reinterpret_cast<FSoundAggDesc*>(Desc);
    if (SoundDesc->Size != kAggDescSize || strcmp(SoundDesc->Name, Name) != 0)
    {
        *Instance = nullptr; // 06ee841a
        return GR_INTERFACE_UNSUPPORTED;
    }

    // 06ee8366..8382: calloc(1, 0x14C) - every field FUN_06ee1b40 does not store (DeviceGuid
    // +0x38, Window +0xF4, ...) starts zeroed - then the ctor in place.
    void* Memory = calloc(1, sizeof(FSoundManager));
    if (Memory == nullptr)
    {
        *Instance = nullptr; // 06ee8407
        return GR_OUT_OF_MEMORY;
    }
    FSoundManager* Object = new (Memory) FSoundManager();

    Object->BindOuter(SoundDesc);
    // 06ee83c4: FUN_06ee56e0 always returns 0, so the 06ee83d2..83fd failure tail (null the
    // desc inner-out, dtor, free, *Instance = 0) cannot run.
    SoundManagerFactoryInitialize(Object);
    *Instance = static_cast<ISoundManager*>(Object);
    return GR_OK;
}

FSoundManager::FSoundManager()
    : bParserBound(0), MasterGain(0.0f), Field4C(0), Field50(0), MaxChannels(0), EaxEnv(0),
      EaxVol(0.0f), EaxDecay(0.0f), EaxDamp(0.0f), Field68(0), Field6C(0), Field70(0), Field74(0),
      ReverbMix(0.0f), Field7C(0), Field80(0), Field84(0), Field88(0), Flags(0x113), CapsFlags(0),
      Field94(0), SampleRate(0x5622), Channels(2), BitsPerSample(0x10), CoordScale(1),
      DistanceFactor(1.0f), DopplerFactor(1.0f), RolloffFactor(1.0f), Max3dPan(10000.0f),
      Window(nullptr), ArchEntries(nullptr), ArchCount(0), ArchCap(0), bArchEmpty(1), Pad108(0),
      AttachedUnused(nullptr), AttachedSentinel(nullptr), AttachedCount(0),
      InstanceUnused(nullptr), InstanceSentinel(nullptr), InstanceEmbeddedCount(0), NextArchId(1),
      Listener(nullptr), DirectSound(nullptr), Primary(nullptr), Parser(nullptr),
      bCoInitialized(0), Controlling(nullptr)
{
    // FUN_06ee1b40: FUN_06ee7620(+0xF8 map), FUN_06ee7d80(+0x10C list head), FUN_06ee7d50(+0x118
    // list head), then InitializeCriticalSection(DAT_06eeaf0c) (06ee1c79), field stores, and
    // CreateEventA -> DAT_06eeaf70 last (06ee1dc2). Both run on every construction.
    AttachedSentinel = MakeAttachedSentinel();
    // FUN_06ee7d50 head node (0x60), self-linked unchecked (06ee1c1b..1c23).
    InstanceSentinel = static_cast<FSoundInstanceNode*>(calloc(1, sizeof(FSoundInstanceNode)));
    InstanceSentinel->Next = InstanceSentinel;
    InstanceSentinel->Prev = InstanceSentinel;
    InstanceEmbeddedCount = 0;
    InitializeCriticalSection(&G_InstanceLock);
    memset(DeviceIdString, 0, sizeof(DeviceIdString));
    Front.X = 0.0f;
    Front.Y = 0.0f;
    Front.Z = 1.0f;
    Up.X = 0.0f;
    Up.Y = 1.0f;
    Up.Z = 0.0f;
    PadC0[0] = 0;
    PadC0[1] = 0;
    PadC0[2] = 0;
    Position.X = 0.0f;
    Position.Y = 0.0f;
    Position.Z = 0.0f;
    Velocity.X = 0.0f;
    Velocity.Y = 0.0f;
    Velocity.Z = 0.0f;
    Pad105[0] = 0;
    Pad105[1] = 0;
    Pad105[2] = 0;
    Pad139[0] = 0;
    Pad139[1] = 0;
    Pad139[2] = 0;
    Inner.Self = this;
    Controlling = &Inner;
    G_MixerEvent = CreateEventA(nullptr, FALSE, FALSE, nullptr); // 06ee1dc2 -> DAT_06eeaf70
}

FSoundManager::~FSoundManager()
{
    // FUN_06ee1de0: vtbl restore, FUN_06ee4900 (06ee1dff), DeleteCriticalSection(DAT_06eeaf0c)
    // (06ee1e09), CloseHandle(DAT_06eeaf70) (06ee1e15), Release +0x134 and null (06ee1e1b), then the
    // member containers: +0x118 list (FUN_06ee7590/7d10), +0x10C list (FUN_06ee7580/7510), +0xF8 map
    // (FUN_06ee7ad0/7cd0) - ReleaseCom.
    ShutdownDevice();
    DeleteCriticalSection(&G_InstanceLock);
    CloseHandle(G_MixerEvent);
    if (Parser != nullptr)
    {
        Parser->Release();
        Parser = nullptr;
    }
    ReleaseCom();
}

void FSoundManager::BindOuter(FSoundAggDesc* Desc)
{
    // 06ee8387..83be: inner unknown at +0x140 = {vtbl 95c0, count 1, Self}; +0x13C = desc outer
    // if set, and then *desc+0xC = inner unconditionally (06ee8400); else +0x13C = inner.
    Inner.RefCount = 1;
    Inner.Self = this;
    if (Desc->Outer != nullptr)
    {
        Controlling = Desc->Outer;
        *Desc->InnerOut = &Inner;
    }
    else
    {
        Controlling = &Inner;
    }
}

void FSoundManager::ReleaseCom()
{
    // Member-container teardown of FUN_06ee1de0 (06ee1e35..), after ShutdownDevice already emptied
    // the global list, the attached list and the archetype map and released the DirectSound objects.
    // +0x118 pending list: FUN_06ee7590 per node (unlink, payload dtor FUN_06ee11a0, free, count--),
    // then FUN_06ee7d10 frees the head.
    FSoundInstanceNode* Pending = InstanceSentinel->Next;
    while (Pending != InstanceSentinel)
    {
        FSoundInstanceNode* Next = Pending->Next;
        Pending->Prev->Next = Pending->Next;
        Pending->Next->Prev = Pending->Prev;
        ReleaseInstancePayload(Pending);
        free(Pending);
        --InstanceEmbeddedCount;
        Pending = Next;
    }
    free(InstanceSentinel);
    InstanceSentinel = nullptr;
    InstanceEmbeddedCount = 0;
    // +0x10C attached list: FUN_06ee7510 per node (unlink, free; the play key is not released),
    // then the head.
    FAttachedNode* Attached = AttachedSentinel->Next;
    while (Attached != AttachedSentinel)
    {
        FAttachedNode* Next = Attached->Next;
        free(Attached);
        Attached = Next;
    }
    free(AttachedSentinel);
    AttachedSentinel = nullptr;
    AttachedCount = 0;
    // +0xF8 map: FUN_06ee7ad0 erase-all + FUN_06ee7cd0 node frees. Already emptied by
    // FUN_06ee4800 inside ShutdownDevice; only the storage is left.
    free(ArchEntries);
    ArchEntries = nullptr;
    ArchCount = 0;
    ArchCap = 0;
    bArchEmpty = 1;
}

// FUN_06ee8560 / 8580 / 85a0: replace `this` with the controlling unknown at +0x13C and tail-jump
// to its slot 0 / 1 / 2. No null check.
GENRESULT DACOM_API FSoundManager::QueryInterface(const char* Iid, void** Out)
{
    return Controlling->QueryInterface(Iid, Out);
}

unsigned long DACOM_API FSoundManager::AddRef()
{
    return Controlling->AddRef();
}

unsigned long DACOM_API FSoundManager::Release()
{
    return Controlling->Release();
}

GENRESULT DACOM_API FSoundManager::SetPcmFormat(unsigned short* Fmt)
{
    SampleRate = Fmt[2];
    Channels = Fmt[0];
    BitsPerSample = static_cast<unsigned long>(Fmt[3]) << 3;
    return GR_OK;
}

GENRESULT DACOM_API FSoundManager::GetPcmFormat(unsigned short* Fmt)
{
    // LAB_06ee1ff0 RET 8. No null check, no zero-channel check: +2 is the 16-bit +6 value
    // (MOVZX AX) divided by the dword +0x9C (06ee201b..2026).
    Fmt[2] = static_cast<unsigned short>(SampleRate);
    Fmt[0] = static_cast<unsigned short>(Channels);
    Fmt[3] = static_cast<unsigned short>(BitsPerSample >> 3);
    Fmt[1] = static_cast<unsigned short>(static_cast<unsigned long>(Fmt[3]) / Channels);
    return GR_OK;
}

GENRESULT DACOM_API FSoundManager::SetMasterGain(float Gain)
{
    MasterGain = Gain;
    return GR_OK;
}

GENRESULT DACOM_API FSoundManager::SetMaxChannels(unsigned long Count)
{
    MaxChannels = Count;
    return GR_OK;
}

GENRESULT DACOM_API FSoundManager::SetSpeakerConfig(unsigned long Index)
{
    if (DirectSound == nullptr || Index < 1 || Index >= 0xB)
        return GR_GENERIC;
    DirectSound->SetSpeakerConfig(kSpeakerTable[Index]);
    return GR_OK;
}

float DACOM_API FSoundManager::GetMasterGain()
{
    return MasterGain;
}

unsigned long DACOM_API FSoundManager::GetMaxChannels()
{
    return MaxChannels;
}

unsigned long DACOM_API FSoundManager::GetSpeakerConfig()
{
    if (DirectSound == nullptr)
        return 0xC;
    DWORD Config = 0;
    if (DirectSound->GetSpeakerConfig(&Config) < 0)
        return 0xC;
    unsigned long Index = 0;
    while (Index < 0xB)
    {
        if (kSpeakerTable[Index] == Config)
            return Index;
        ++Index;
    }
    return 0xC;
}

GENRESULT DACOM_API FSoundManager::SetEaxPreview(int Env, float Vol, float Decay, float Damp)
{
    // LAB_06ee2100 RET 0x14. +0x12C==0 → -1 without storing. Env: SBB/AND (<0x1A ? env : 0),
    // not clamp-to-0x19. Vol 0..1, Decay 0.1..100 (DAT_06ee9430), Damp 0..2. Flags bit2 off → -1
    // after store. Probe via FUN_06ee1000 (dwBufferBytes=4). QI IKsPropertySet on the probe;
    // QuerySupport EAX20_Buffer ulId=0, need bits GET|SET. If CapsFlags bit2 clear: Get 0x10
    // into +0x68..+0x74 and OR +0x90 with 4. Set 0x10-byte blob. Success does not Release the
    // PropertySet (retail). Fail releases it then FUN_06ee7600.
    if (DirectSound == nullptr)
        return GR_GENERIC;

    EaxEnv = (static_cast<unsigned int>(Env) < 0x1Au) ? Env : 0;
    // Each clamp is float vs double constant: FCOMP lo + TEST AH,5 + JP (fall-through = less ->
    // store the double lo), else FCOMP hi + TEST AH,0x41 + JNZ (fall-through = greater -> store
    // hi), else keep the argument. NaN takes both jumps and is stored unchanged.
    // Bounds DAT_06ee9238 0.0 / 9440 1.0, 9438 0.1 / 9430 100.0, 9238 0.0 / 9248 2.0.
    if (static_cast<double>(Vol) < 0.0)
        EaxVol = static_cast<float>(0.0);
    else if (static_cast<double>(Vol) > 1.0)
        EaxVol = static_cast<float>(1.0);
    else
        EaxVol = Vol;
    if (static_cast<double>(Decay) < 0.1)
        EaxDecay = static_cast<float>(0.1);
    else if (static_cast<double>(Decay) > 100.0)
        EaxDecay = static_cast<float>(100.0);
    else
        EaxDecay = Decay;
    if (static_cast<double>(Damp) < 0.0)
        EaxDamp = static_cast<float>(0.0);
    else if (static_cast<double>(Damp) > 2.0)
        EaxDamp = static_cast<float>(2.0);
    else
        EaxDamp = Damp;

    if ((Flags & 4) == 0)
        return GR_GENERIC;

    WAVEFORMATEX Format;
    Format.wFormatTag = 1;
    Format.nChannels = 2;
    Format.nSamplesPerSec = 0x5622;
    Format.nAvgBytesPerSec = 0x15888;
    Format.nBlockAlign = 4;
    Format.wBitsPerSample = 0x10;
    Format.cbSize = 0;

    FDsBufferDescWrapper Wrapper;
    InitDsBufferDescWrapper(&Wrapper, &Format);
    if (DirectSound->CreateSoundBuffer(&Wrapper.Desc, &Wrapper.Buffer, nullptr) != 0)
    {
        ReleaseDsBufferWrapper(&Wrapper);
        return GR_GENERIC;
    }

    IKsPropertySet* PropertySet = nullptr;
    if (Wrapper.Buffer->QueryInterface(kIidKsPropertySet, reinterpret_cast<void**>(&PropertySet)) !=
        0)
    {
        ReleaseDsBufferWrapper(&Wrapper);
        return GR_GENERIC;
    }

    ULONG Support = 0;
    if (PropertySet->QuerySupport(kDsPropSetEax20Buffer, 0, &Support) != 0 || (Support & 3) == 0)
    {
        PropertySet->Release();
        ReleaseDsBufferWrapper(&Wrapper);
        return GR_GENERIC;
    }

    if ((CapsFlags & 4) == 0)
    {
        // 06ee2299..22db: the 0x10-byte stack buffer is not pre-zeroed and the Get result is
        // ignored; whatever is there lands in +0x68..+0x74.
        unsigned long Cached[4];
        PropertySet->Get(kDsPropSetEax20Buffer, 0, nullptr, 0, Cached, 0x10, nullptr);
        Field68 = Cached[0];
        Field6C = Cached[1];
        Field70 = Cached[2];
        Field74 = Cached[3];
        CapsFlags |= 4;
    }

    struct FEaxBlob
    {
        unsigned long Env;
        float Vol;
        float Decay;
        float Damp;
    };
    FEaxBlob Blob;
    Blob.Env = static_cast<unsigned long>(EaxEnv);
    Blob.Vol = EaxVol;
    Blob.Decay = EaxDecay;
    Blob.Damp = EaxDamp;
    PropertySet->Set(kDsPropSetEax20Buffer, 0, nullptr, 0, &Blob, 0x10);
    ReleaseDsBufferWrapper(&Wrapper);
    return GR_OK;
}

GENRESULT DACOM_API FSoundManager::GetEaxPreview(unsigned long* Env, unsigned long* Vol,
                                                unsigned long* Decay, unsigned long* Damp)
{
    // LAB_06ee2350 RET 0x14: unconditional DWORD copies +0x58..+0x64. No null checks. No status:
    // EAX is left holding the +0x64 dword (06ee236f) at return.
    *Env = static_cast<unsigned long>(EaxEnv);
    memcpy(Vol, &EaxVol, sizeof(float));
    memcpy(Decay, &EaxDecay, sizeof(float));
    unsigned long DampBits;
    memcpy(&DampBits, &EaxDamp, sizeof(float));
    *Damp = DampBits;
    return static_cast<GENRESULT>(DampBits);
}

void FSoundManager::ApplyListener()
{
    // FUN_06ee6d50 thiscall (callers 06ee4729, 06ee6f23): with a listener (+0x128), push the
    // stored +0xA8..+0xEC as-is through SetOrientation (+0x34), SetPosition (+0x38),
    // SetVelocity (+0x40), SetDistanceFactor (+0x2C), SetDopplerFactor (+0x30),
    // SetRolloffFactor (+0x3C), each with dwApply 1 = DS3D_DEFERRED (PUSH 0x1); the mixer
    // commits them (06ee524e). Every listener setter below passes the same 1.
    if (Listener == nullptr)
        return;
    Listener->SetOrientation(Front.X, Front.Y, Front.Z, Up.X, Up.Y, Up.Z, DS3D_DEFERRED);
    Listener->SetPosition(Position.X, Position.Y, Position.Z, DS3D_DEFERRED);
    Listener->SetVelocity(Velocity.X, Velocity.Y, Velocity.Z, DS3D_DEFERRED);
    Listener->SetDistanceFactor(DistanceFactor, DS3D_DEFERRED);
    Listener->SetDopplerFactor(DopplerFactor, DS3D_DEFERRED);
    Listener->SetRolloffFactor(RolloffFactor, DS3D_DEFERRED);
}

GENRESULT DACOM_API FSoundManager::SetOrientation(FVec3* InFront, FVec3* InUp)
{
    // LAB_06ee23d0: negate front → +0xA8; copy up → +0xB4; Front.Z/Up.Z *= FILD(+0xA4);
    // IDirectSound3DListener::SetOrientation(+0x34) dwApply=1. No extra negate. No ApplyListener.
    const float Scale = static_cast<float>(CoordScale);
    Front.X = -InFront->X;
    Front.Y = -InFront->Y;
    Front.Z = -InFront->Z * Scale;
    Up.X = InUp->X;
    Up.Y = InUp->Y;
    Up.Z = InUp->Z * Scale;
    if (Listener != nullptr)
    {
        Listener->SetOrientation(Front.X, Front.Y, Front.Z, Up.X, Up.Y, Up.Z, DS3D_DEFERRED);
    }
    return GR_OK;
}

GENRESULT DACOM_API FSoundManager::SetOrientationFromMatrix(float* Matrix)
{
    // LAB_06ee2380 RET 8: A = m[1], m[4], m[7] (+4/+10/+1C) at [ESP], B = m[2], m[5], m[8]
    // (+8/+14/+20) at [ESP+0xC]; PUSH A, PUSH B, PUSH this -> self+0x34 (SetOrientation) with
    // Front = B (column 2) and Up = A (column 1); its result is returned.
    FVec3 InUp;
    FVec3 InFront;
    InUp.X = Matrix[1];
    InUp.Y = Matrix[4];
    InUp.Z = Matrix[7];
    InFront.X = Matrix[2];
    InFront.Y = Matrix[5];
    InFront.Z = Matrix[8];
    return SetOrientation(&InFront, &InUp);
}

GENRESULT DACOM_API FSoundManager::SetListenerPosition(FVec3* Pos)
{
    // LAB_06ee24a0: copy → +0xCC; Z *= FILD(+0xA4) stored back; SetPosition(+0x38) dwApply=1.
    const float Scale = static_cast<float>(CoordScale);
    Position.X = Pos->X;
    Position.Y = Pos->Y;
    Position.Z = Pos->Z * Scale;
    if (Listener != nullptr)
        Listener->SetPosition(Position.X, Position.Y, Position.Z, DS3D_DEFERRED);
    return GR_OK;
}

GENRESULT DACOM_API FSoundManager::SetListenerVelocity(FVec3* Vel)
{
    // LAB_06ee2500: copy → +0xD8; Z *= scale stored; SetVelocity(+0x40) dwApply=1.
    const float Scale = static_cast<float>(CoordScale);
    Velocity.X = Vel->X;
    Velocity.Y = Vel->Y;
    Velocity.Z = Vel->Z * Scale;
    if (Listener != nullptr)
        Listener->SetVelocity(Velocity.X, Velocity.Y, Velocity.Z, DS3D_DEFERRED);
    return GR_OK;
}

GENRESULT DACOM_API FSoundManager::SetDistanceFactor(float Value)
{
    DistanceFactor = Value;
    if (Listener != nullptr)
        Listener->SetDistanceFactor(Value, DS3D_DEFERRED);
    return GR_OK;
}

GENRESULT DACOM_API FSoundManager::SetDopplerFactor(float Value)
{
    DopplerFactor = Value;
    if (Listener != nullptr)
        Listener->SetDopplerFactor(Value, DS3D_DEFERRED);
    return GR_OK;
}

GENRESULT DACOM_API FSoundManager::SetRolloffFactor(float Value)
{
    RolloffFactor = Value;
    if (Listener != nullptr)
        Listener->SetRolloffFactor(Value, DS3D_DEFERRED);
    return GR_OK;
}

GENRESULT DACOM_API FSoundManager::GetOrientationSpace(FVec3* Pos, FVec3* Vel)
{
    // LAB_06ee2730: copy Front → Pos, Pos.Z *= scale, negate all three Pos; copy Up → Vel, Vel.Z *= scale (no negate).
    const float Scale = static_cast<float>(CoordScale);
    Pos->X = Front.X;
    Pos->Y = Front.Y;
    Pos->Z = Front.Z * Scale;
    Pos->X = -Pos->X;
    Pos->Y = -Pos->Y;
    Pos->Z = -Pos->Z;
    Vel->X = Up.X;
    Vel->Y = Up.Y;
    Vel->Z = Up.Z * Scale;
    // 06ee2798: EAX is left holding the second out pointer at RET 0xC.
    return static_cast<GENRESULT>(reinterpret_cast<uintptr_t>(Vel));
}

GENRESULT DACOM_API FSoundManager::GetOrientationMatrix(float* Out9)
{
    // LAB_06ee25f0 RET 8: self+0x50 (GetOrientationSpace) into SpaceFront (F+0x28, arg1) and
    // SpaceUp (F+0x1C, arg2), result ignored. Right from the stored +0xA8 / +0xB4; Flags bit 4
    // (+0x8C) picks the operand order. Each term is an x87 product of two floats (exact) and one
    // FSUBP, stored as float - computed here in double and rounded once.
    // Column-major out: [0/3/6] Right (Z *= FILD(+0xA4)), [1/4/7] SpaceUp, [2/5/8] SpaceFront.
    FVec3 SpaceFront;
    FVec3 SpaceUp;
    GetOrientationSpace(&SpaceFront, &SpaceUp);
    const double Fx = Front.X;
    const double Fy = Front.Y;
    const double Fz = Front.Z;
    const double Ux = Up.X;
    const double Uy = Up.Y;
    const double Uz = Up.Z;
    FVec3 Right;
    if ((Flags & 0x10) != 0)
    {
        Right.X = static_cast<float>(Uz * Fy - Fz * Uy);
        Right.Y = static_cast<float>(Fz * Ux - Uz * Fx);
        Right.Z = static_cast<float>(Fx * Uy - Ux * Fy);
    }
    else
    {
        Right.X = static_cast<float>(Uy * Fz - Fy * Uz);
        Right.Y = static_cast<float>(Fx * Uz - Fz * Ux);
        Right.Z = static_cast<float>(Fy * Ux - Fx * Uy);
    }
    Out9[0] = Right.X;
    Out9[1] = SpaceUp.X;
    Out9[2] = SpaceFront.X;
    Out9[3] = Right.Y;
    Out9[4] = SpaceUp.Y;
    Out9[5] = SpaceFront.Y;
    Out9[6] = static_cast<float>(static_cast<double>(CoordScale) * Right.Z);
    Out9[7] = SpaceUp.Z;
    Out9[8] = SpaceFront.Z;
    // 06ee26c9: EAX = Out9 at RET 8.
    return static_cast<GENRESULT>(reinterpret_cast<uintptr_t>(Out9));
}

GENRESULT DACOM_API FSoundManager::GetListenerPosition(FVec3* Out)
{
    // FUN_06ee27c0: copy +0xCC; out.Z *= FILD(+0xA4). Does not mutate stored position.
    Out->X = Position.X;
    Out->Y = Position.Y;
    Out->Z = Position.Z * static_cast<float>(CoordScale);
    // 06ee27d4: EAX = Out at RET 8.
    return static_cast<GENRESULT>(reinterpret_cast<uintptr_t>(Out));
}

GENRESULT DACOM_API FSoundManager::GetListenerVelocity(FVec3* Out)
{
    // LAB_06ee2810: copy +0xD8; out.Z *= scale.
    Out->X = Velocity.X;
    Out->Y = Velocity.Y;
    Out->Z = Velocity.Z * static_cast<float>(CoordScale);
    // 06ee2824: EAX = Out at RET 8.
    return static_cast<GENRESULT>(reinterpret_cast<uintptr_t>(Out));
}

float DACOM_API FSoundManager::GetDistanceFactor()
{
    return DistanceFactor;
}

float DACOM_API FSoundManager::GetDopplerFactor()
{
    return DopplerFactor;
}

float DACOM_API FSoundManager::GetRolloffFactor()
{
    return RolloffFactor;
}

GENRESULT DACOM_API FSoundManager::EaxGetInstance(void* Id, GUID* Set, unsigned long Prop, void* Buf,
                                                 unsigned long Cb, unsigned long* Pcb)
{
    // LAB_06ee2890 RET 0x1C: walk DAT_06eeaf04 matching node+8; QI IKsPropertySet on node+0xC;
    // QuerySupport bit0 (GET); IKsPropertySet::Get(+0x0C). HRESULT of Get ignored. Fail → -1.
    GENRESULT Result = GR_GENERIC;
    FSoundInstanceNode* Node = G_InstanceSentinel.Next;
    while (Node != &G_InstanceSentinel)
    {
        if (Node->PlayKey == Id)
        {
            IDirectSoundBuffer* Buffer = Node->Buffer;
            IKsPropertySet* PropertySet = nullptr;
            if (Buffer->QueryInterface(kIidKsPropertySet, reinterpret_cast<void**>(&PropertySet)) == 0)
            {
                ULONG Support = 0;
                PropertySet->QuerySupport(*Set, Prop, &Support);
                if ((Support & 1) != 0)
                {
                    PropertySet->Get(*Set, Prop, nullptr, 0, Buf, Cb, Pcb);
                    Result = GR_OK;
                }
                PropertySet->Release();
            }
            return Result;
        }
        Node = Node->Next;
    }
    return Result;
}

GENRESULT DACOM_API FSoundManager::EaxSetInstance(void* Id, GUID* Set, unsigned long Prop, void* Data,
                                                 unsigned long Cb)
{
    // LAB_06ee2a40 RET 0x18: same instance walk; QuerySupport bit1 (SET); IKsPropertySet::Set(+0x10).
    GENRESULT Result = GR_GENERIC;
    FSoundInstanceNode* Node = G_InstanceSentinel.Next;
    while (Node != &G_InstanceSentinel)
    {
        if (Node->PlayKey == Id)
        {
            IDirectSoundBuffer* Buffer = Node->Buffer;
            IKsPropertySet* PropertySet = nullptr;
            if (Buffer->QueryInterface(kIidKsPropertySet, reinterpret_cast<void**>(&PropertySet)) == 0)
            {
                ULONG Support = 0;
                PropertySet->QuerySupport(*Set, Prop, &Support);
                if ((Support & 2) != 0)
                {
                    PropertySet->Set(*Set, Prop, nullptr, 0, Data, Cb);
                    Result = GR_OK;
                }
                PropertySet->Release();
            }
            return Result;
        }
        Node = Node->Next;
    }
    return Result;
}

GENRESULT DACOM_API FSoundManager::EaxGetPrimary(GUID* Set, unsigned long Prop, void* Buf,
                                                unsigned long Cb, unsigned long* Pcb)
{
    // LAB_06ee2940 RET 0x18: if +0x12C==0 → -1. Probe format 22050/16/2, FUN_06ee1000, CreateSoundBuffer
    // on the wrapper. If wrapper.Buffer≠0, QI IKsPropertySet on primary +0x130; QuerySupport bit0; Get.
    // Always FUN_06ee7600 the probe. HRESULT of Get ignored.
    GENRESULT Result = GR_GENERIC;
    if (DirectSound == nullptr)
        return Result;

    WAVEFORMATEX Format;
    Format.wFormatTag = 1;
    Format.nChannels = 2;
    Format.nSamplesPerSec = 0x5622;
    Format.nAvgBytesPerSec = 0x15888;
    Format.nBlockAlign = 4;
    Format.wBitsPerSample = 0x10;
    Format.cbSize = 0;

    FDsBufferDescWrapper Wrapper;
    InitDsBufferDescWrapper(&Wrapper, &Format);
    DirectSound->CreateSoundBuffer(&Wrapper.Desc, &Wrapper.Buffer, nullptr);
    if (Wrapper.Buffer != nullptr)
    {
        IKsPropertySet* PropertySet = nullptr;
        if (Primary->QueryInterface(kIidKsPropertySet, reinterpret_cast<void**>(&PropertySet)) == 0)
        {
            ULONG Support = 0;
            PropertySet->QuerySupport(*Set, Prop, &Support);
            if ((Support & 1) != 0)
            {
                PropertySet->Get(*Set, Prop, nullptr, 0, Buf, Cb, Pcb);
                Result = GR_OK;
            }
            PropertySet->Release();
        }
    }
    ReleaseDsBufferWrapper(&Wrapper);
    return Result;
}

GENRESULT DACOM_API FSoundManager::EaxSetPrimary(GUID* Set, unsigned long Prop, void* Data,
                                                unsigned long Cb)
{
    // LAB_06ee2af0 RET 0x14: same probe + primary QI as Get; QuerySupport bit1; IKsPropertySet::Set.
    GENRESULT Result = GR_GENERIC;
    if (DirectSound == nullptr)
        return Result;

    WAVEFORMATEX Format;
    Format.wFormatTag = 1;
    Format.nChannels = 2;
    Format.nSamplesPerSec = 0x5622;
    Format.nAvgBytesPerSec = 0x15888;
    Format.nBlockAlign = 4;
    Format.wBitsPerSample = 0x10;
    Format.cbSize = 0;

    FDsBufferDescWrapper Wrapper;
    InitDsBufferDescWrapper(&Wrapper, &Format);
    DirectSound->CreateSoundBuffer(&Wrapper.Desc, &Wrapper.Buffer, nullptr);
    if (Wrapper.Buffer != nullptr)
    {
        IKsPropertySet* PropertySet = nullptr;
        if (Primary->QueryInterface(kIidKsPropertySet, reinterpret_cast<void**>(&PropertySet)) == 0)
        {
            ULONG Support = 0;
            PropertySet->QuerySupport(*Set, Prop, &Support);
            if ((Support & 2) != 0)
            {
                PropertySet->Set(*Set, Prop, nullptr, 0, Data, Cb);
                Result = GR_OK;
            }
            PropertySet->Release();
        }
    }
    ReleaseDsBufferWrapper(&Wrapper);
    return Result;
}

FSoundArchetype* FSoundManager::LookupArchetype(int Id)
{
    if (Id == -1)
        return nullptr;
    for (unsigned long Index = 0; Index < ArchCount; ++Index)
    {
        if (ArchEntries[Index].Id == Id)
        {
            ArchEntries[Index].Archetype->AddRef();
            return ArchEntries[Index].Archetype;
        }
    }
    return nullptr;
}

int FSoundManager::InsertArchetype(FSoundArchetype* Archetype)
{
    if (Archetype == nullptr)
        return -1;
    if (ArchCount == ArchCap)
    {
        const unsigned long NewCap = ArchCap == 0 ? 8 : ArchCap * 2;
        FArchMapEntry* Grown =
            static_cast<FArchMapEntry*>(realloc(ArchEntries, NewCap * sizeof(FArchMapEntry)));
        if (Grown == nullptr)
            return -1;
        ArchEntries = Grown;
        ArchCap = NewCap;
    }
    const int Id = static_cast<int>(NextArchId);
    ++NextArchId;
    ArchEntries[ArchCount].Id = Id;
    ArchEntries[ArchCount].Archetype = Archetype;
    ++ArchCount;
    bArchEmpty = 0;
    return Id;
}

void FSoundManager::StopInstancesForArch(int ArchId)
{
    // FUN_06ee4740 thiscall RET 4 (callers 06ee31b1, 06ee481a): walk DAT_06eeaf00; for each node
    // whose key's GetArchId (+0x20, called unconditionally) equals the id: lock, unlink,
    // payload dtor, node free, count--, SetEvent(DAT_06eeaf70), unlock.
    FSoundInstanceNode* Node = G_InstanceSentinel.Next;
    while (Node != &G_InstanceSentinel)
    {
        FSoundInstanceNode* Next = Node->Next;
        if (Node->PlayKey->GetArchId() == ArchId)
        {
            EnterCriticalSection(&G_InstanceLock);
            UnlinkInstance(Node);
            SetEvent(G_MixerEvent);
            LeaveCriticalSection(&G_InstanceLock);
        }
        Node = Next;
    }
}

void FSoundManager::DestroyAllArchetypes()
{
    // FUN_06ee4800: for each archetype in map (ascending id) order, FUN_06ee4740 on its key,
    // then Release the value (06ee481f..4825, no null check); then clear the map. Retail
    // walks an RB-tree at +0xF8; the port keeps the linear array sorted by id (insert appends
    // increasing ids, erase shifts down), so the order matches (deviations.md).
    for (unsigned long Index = 0; Index < ArchCount; ++Index)
    {
        StopInstancesForArch(ArchEntries[Index].Id);
        ArchEntries[Index].Archetype->Release();
    }
    free(ArchEntries);
    ArchEntries = nullptr;
    ArchCount = 0;
    ArchCap = 0;
    bArchEmpty = 1;
}

void FSoundManager::ClearAttached()
{
    // 06ee33b8..3403 (head of FUN_06ee33b0): first pass Releases every attached key (+8, no
    // null check) with the list intact, re-reading the head each step; second pass erases
    // every node (FUN_06ee7510: unlink, free, size--).
    FAttachedNode* Node = AttachedSentinel->Next;
    while (Node != AttachedSentinel)
    {
        Node->PlayKey->Release();
        Node = Node->Next;
    }
    Node = AttachedSentinel->Next;
    while (Node != AttachedSentinel)
    {
        FAttachedNode* Next = Node->Next;
        Node->Prev->Next = Node->Next;
        Node->Next->Prev = Node->Prev;
        free(Node);
        --AttachedCount;
        Node = Next;
    }
}

void FSoundManager::UnlinkInstance(FSoundInstanceNode* Node)
{
    // Inline erase of the DAT_06eeaf00 list as at 06ee3527..355a / 06ee477c..47d1: unlink, payload
    // dtor FUN_06ee11a0 (COM releases only - the node's notify event +0x14 is not closed), node back
    // to the allocator, DAT_06eeaf08-- (no underflow check).
    Node->Prev->Next = Node->Next;
    Node->Next->Prev = Node->Prev;
    ReleaseInstancePayload(Node);
    free(Node);
    --G_InstanceCount;
}

FSoundInstanceNode* FSoundManager::FindInstanceByKey(ISoundSource* PlayKey)
{
    // Inline walks of DAT_06eeaf00 comparing node+8 with the key (06ee4ca0, 06ee4fd1, 06ee34d0,
    // LAB_06ee6f30/6f70); no key test.
    FSoundInstanceNode* Node = G_InstanceSentinel.Next;
    while (Node != &G_InstanceSentinel)
    {
        if (Node->PlayKey == PlayKey)
            return Node;
        Node = Node->Next;
    }
    return nullptr;
}

FAttachedNode* FSoundManager::FindAttached(ISoundSource* PlayKey)
{
    // 06ee4ba0..4bbd: walk the attached list (+0x110 head) comparing node+8 with the key.
    FAttachedNode* Node = AttachedSentinel->Next;
    while (Node != AttachedSentinel)
    {
        if (Node->PlayKey == PlayKey)
            return Node;
        Node = Node->Next;
    }
    return nullptr;
}

void FSoundManager::RefreshInstancePose(FSoundInstanceNode* Node)
{
    // Mixer existing-instance pose @ 06ee4cdc–06ee4db6: listener pos copy, defaults
    // 10/500, client vtbl+0x30/+0x38/+0x34, Z *= +0xA4, SubVec3 (FUN_06ee5500), then
    // DistSq = (x*x + y*y) + z*z on the FPU stack, stored once as float. No null checks.
    if (Node->PlayKey->Is3d() == 0)
        return;
    Node->Position = Position;
    Node->MinDistance = 10.0f;
    Node->MaxDistance = 500.0f;
    Node->PlayKey->GetPosition(&Node->Position);
    Node->PlayKey->GetMaxDistance(&Node->MaxDistance);
    Node->PlayKey->GetMinDistance(&Node->MinDistance);
    Node->Position.Z *= static_cast<float>(CoordScale);
    Node->MaxDistSq = Node->MaxDistance * Node->MaxDistance;
    Node->MinDistSq = Node->MinDistance * Node->MinDistance;
    FVec3 Delta;
    SubVec3(&Delta, &Node->Position, &Position);
    Node->Direction = Delta;
    const double DirX = Node->Direction.X;
    const double DirY = Node->Direction.Y;
    const double DirZ = Node->Direction.Z;
    Node->DistSq = static_cast<float>((DirX * DirX + DirY * DirY) + DirZ * DirZ);
}

unsigned char FSoundManager::MixerAllowCreate(ISoundSource* PlayKey)
{
    // LAB_06ee4e6b–06ee4f51: not 3D -> allow; lookup miss -> deny; arch bit0 clear -> allow;
    // else StoreVec3(src - listener) and allow only when max*max > (z*z + y*y) + x*x
    // (FCOMPP, TEST AH,0x41 + JNZ: fall-through = max^2 greater; equal and NaN deny).
    if (PlayKey->Is3d() == 0)
        return 1;
    const int ArchId = PlayKey->GetArchId();
    FSoundArchetype* Archetype = LookupArchetype(ArchId);
    if (Archetype == nullptr)
        return 0;
    unsigned char bAllow = 1;
    if ((Archetype->Flags & 1) != 0)
    {
        // 06ee4eac: the position local is not pre-initialised; GetPosition (+0x30) fills it.
        FVec3 SrcPos;
        PlayKey->GetPosition(&SrcPos);
        SrcPos.Z *= static_cast<float>(CoordScale);
        float MaxDist = 3.402823466e+38f; // 0x7f7fffff
        PlayKey->GetMaxDistance(&MaxDist);
        // 06ee4ed9..4f06: each difference is x87 FLD float / FSUB float / FSTP float.
        const float Dz = static_cast<float>(static_cast<double>(SrcPos.Z) - Position.Z);
        const float Dy = static_cast<float>(static_cast<double>(SrcPos.Y) - Position.Y);
        const float Dx = static_cast<float>(static_cast<double>(SrcPos.X) - Position.X);
        FVec3 Delta;
        StoreVec3(&Delta, Dx, Dy, Dz);
        const double DistSq = (static_cast<double>(Delta.Z) * Delta.Z +
                               static_cast<double>(Delta.Y) * Delta.Y) +
                              static_cast<double>(Delta.X) * Delta.X;
        const double MaxSq = static_cast<double>(MaxDist) * MaxDist;
        if (!(MaxSq > DistSq))
            bAllow = 0;
    }
    Archetype->Release();
    return bAllow;
}

void FSoundManager::LinkPendingInstance(FSoundInstanceNode* Node)
{
    // FUN_06ee7c00: insert 0x60 node before pending sentinel, copy-construct payload. The port
    // links the already-filled node instead of copying; no checks (the 06ee7c4a test is the
    // placement-new null check on node+8, never zero).
    Node->Next = InstanceSentinel;
    Node->Prev = InstanceSentinel->Prev;
    InstanceSentinel->Prev->Next = Node;
    InstanceSentinel->Prev = Node;
    ++InstanceEmbeddedCount;
}

void FSoundManager::SplicePendingInstances()
{
    // FUN_06ee7c70 (raw _Splice of the whole +0x118 chain before head DAT_06eeaf04) plus the
    // 06ee5204..520d count transfer. The caller (MixerTick 06ee51dc) already checked +0x120 != 0,
    // so the chain is non-empty; 7c70 itself has no checks.
    FSoundInstanceNode* First = InstanceSentinel->Next;
    FSoundInstanceNode* Last = InstanceSentinel->Prev;
    InstanceSentinel->Next = InstanceSentinel;
    InstanceSentinel->Prev = InstanceSentinel;
    First->Prev = G_InstanceSentinel.Prev;
    Last->Next = &G_InstanceSentinel;
    G_InstanceSentinel.Prev->Next = First;
    G_InstanceSentinel.Prev = Last;
    G_InstanceCount += InstanceEmbeddedCount;
    InstanceEmbeddedCount = 0;
}

void FSoundManager::MixerApplyEax(FSoundInstanceNode* Node)
{
    // Tail of FUN_06ee64c0 @ 06ee6ad8..6b52: CapsFlags (+0x90) bit 2; GetEaxEnvironment
    // (+0x4C) into a zeroed dword, nonzero -> skip; buffer QI IKsPropertySet (DAT_06ee917c),
    // nonzero -> skip; QuerySupport (+0x14) into a zeroed dword, result ignored; bit 1 ->
    // Set (+0x10)(DSPROPSETID_EAX20_ListenerProperties, 1, NULL, 0, &probe, 4); Release.
    if ((CapsFlags & 4) == 0)
        return;
    unsigned long Probe = 0;
    const int ProbeHr = Node->PlayKey->GetEaxEnvironment(&Probe);
    if (ProbeHr != 0)
        return;
    IKsPropertySet* PropertySet;
    if (Node->Buffer->QueryInterface(kIidKsPropertySet, reinterpret_cast<void**>(&PropertySet)) != 0)
        return;
    ULONG Support = 0;
    PropertySet->QuerySupport(kDsPropSetEax20Listener, 1, &Support);
    if ((Support & 2) != 0)
        PropertySet->Set(kDsPropSetEax20Listener, 1, nullptr, 0, &Probe, sizeof(Probe));
    PropertySet->Release();
}

unsigned char FSoundManager::MixerApplyInstance(FSoundInstanceNode* Node)
{
    // FUN_06ee64c0 thiscall RET 4. The mixer (06ee5236) pushes node+8 (payload) and ignores
    // AL. C++ Node is the full 0x60 object; payload fields sit at PlayKey onward. Pose is
    // filled by FUN_06ee6e20, not here. No null checks: retail dereferences the play key and
    // the buffer unconditionally.
    ISoundSource* PlayKey = Node->PlayKey;

    // GetApplyMode (+0x54) into a zeroed dword: JL -> keep 1; 0 -> 1, 1 -> 0, else FDUMP
    // WARNING 0xD1A and 1. The value is handed to every DS3D setter as dwApply as-is, so
    // mode 0 means DS3D_DEFERRED (the mixer commits at 06ee524e) and mode 1 DS3D_IMMEDIATE.
    unsigned long Apply = 1;
    unsigned long ApplyMode = 0;
    const int ApplyHr = PlayKey->GetApplyMode(&ApplyMode);
    if (ApplyHr >= 0)
    {
        if (ApplyMode == 0)
            Apply = 1;
        else if (ApplyMode == 1)
            Apply = 0;
        else
        {
            Fdump(kFdumpWarning, 0xD1A, "Invalid apply mode specified");
            Apply = 1;
        }
    }

    float ClientAtten = 0.0f;
    PlayKey->GetAttenuation(&ClientAtten);

    const unsigned char bWas3d = Node->bIs3dCache;
    const unsigned char bIs3d = PlayKey->Is3d();
    Node->bIs3dCache = bIs3d;

    // [ESP+0x13]: the return byte. 3D: SETZ on the old cache; 2D: the old cache itself;
    // set to 1 by any SetPan / SetVolume below.
    unsigned char bChanged;
    IDirectSound3DBuffer* Buffer3d = Node->Buffer3d;
    if (bIs3d != 0)
    {
        bChanged = bWas3d == 0 ? 1 : 0;
        if (Buffer3d != nullptr)
        {
            if (bWas3d == 0)
                Buffer3d->SetMode(DS3DMODE_NORMAL, Apply);
            if ((CapsFlags & 2) != 0)
                Node->StatusFlags |= 8;

            unsigned long SoundMode = 0;
            if (PlayKey->GetSoundMode(&SoundMode) >= 0)
            {
                unsigned long DsMode = DS3DMODE_NORMAL;
                if (SoundMode == 1)
                    DsMode = DS3DMODE_HEADRELATIVE;
                else if (SoundMode != 0)
                {
                    Fdump(kFdumpWarning, 0xD4C, "Invalid sound mode specified.\n");
                    DsMode = DS3DMODE_NORMAL;
                }
                Buffer3d->SetMode(DsMode, Apply);
            }

            unsigned long Inside = 360;
            unsigned long Outside = 360;
            if (PlayKey->GetConeAngles(&Inside, &Outside) >= 0)
            {
                if (Inside > 360)
                    Inside = 360;
                if (Outside > 360)
                    Outside = 360;
                Buffer3d->SetConeAngles(Inside, Outside, Apply);

                // 06ee663e: zeroed vector, result ignored; Z = FILD(CoordScale) * Z, FSTP float.
                FVec3 ConeDir = {};
                PlayKey->GetConeOrientation(&ConeDir);
                ConeDir.Z = static_cast<float>(static_cast<double>(CoordScale) *
                                               static_cast<double>(ConeDir.Z));
                Buffer3d->SetConeOrientation(ConeDir.X, ConeDir.Y, ConeDir.Z, Apply);

                // 06ee668a: zeroed, result ignored; *100.0f FST float; < -10000 (TEST AH,5 +
                // JP, fall-through = less) -> -10000; else > 0 (TEST AH,0x41 + JNZ,
                // fall-through = greater) -> 0; ftol.
                float OutsideVol = 0.0f;
                PlayKey->GetConeOutsideVolume(&OutsideVol);
                float ScaledVol = OutsideVol * 100.0f;
                if (ScaledVol < -10000.0f)
                    ScaledVol = -10000.0f;
                else if (ScaledVol > 0.0f)
                    ScaledVol = 0.0f;
                Buffer3d->SetConeOutsideVolume(static_cast<LONG>(ScaledVol), Apply);
            }

            // 06ee66f4: GetVelocity (+0x48), JL skips; Z = FILD(CoordScale) * Z.
            FVec3 Velocity = {};
            if (PlayKey->GetVelocity(&Velocity) >= 0)
            {
                Velocity.Z = static_cast<float>(static_cast<double>(CoordScale) *
                                                static_cast<double>(Velocity.Z));
                Buffer3d->SetVelocity(Velocity.X, Velocity.Y, Velocity.Z, Apply);
            }

            // 06ee672c: payload+0x30 (MinDistance) clamped [1, 1e9] and written back, then
            // SetMinDistance (+0x44); then payload+0x28 (MaxDistance), SetMaxDistance (+0x40).
            Node->MinDistance = ClampDsDistance(Node->MinDistance);
            Buffer3d->SetMinDistance(Node->MinDistance, Apply);
            Node->MaxDistance = ClampDsDistance(Node->MaxDistance);
            Buffer3d->SetMaxDistance(Node->MaxDistance, Apply);
            Buffer3d->SetPosition(Node->Position.X, Node->Position.Y, Node->Position.Z, Apply);
        }
        else
        {
            // 06ee67d9: software 3D. Dist = DistSq * invsqrt(DistSq) (one Newton step, stored
            // float); attenuation only when DistSq > MinDistSq (TEST AH,0x41 + JNZ,
            // fall-through = greater), with no guard on the log argument.
            const float Dist = QuakeDistance(Node->DistSq);
            if (Node->DistSq > Node->MinDistSq)
                ClientAtten = SoftwareAttenuation(Node->MinDistance, Dist, DistanceFactor,
                                                  RolloffFactor, ClientAtten);

            if ((Node->DsCapsFlags & 0x40) != 0)
            {
                // 06ee6862: listener right = Up x Front on the FPU stack, direction scaled
                // by 1.0f / Dist and stored back as floats, pan = ftol(
                // ((Dz*Rz + Dy*Ry) + Dx*Rx) * Max3dPan). No clamp.
                const double RightX = static_cast<double>(Up.Y) * Front.Z -
                                      static_cast<double>(Up.Z) * Front.Y;
                const double RightY = static_cast<double>(Front.X) * Up.Z -
                                      static_cast<double>(Front.Z) * Up.X;
                const double RightZ = static_cast<double>(Front.Y) * Up.X -
                                      static_cast<double>(Front.X) * Up.Y;
                const double InvDist = 1.0 / static_cast<double>(Dist);
                Node->Direction.X = static_cast<float>(InvDist * Node->Direction.X);
                Node->Direction.Y = static_cast<float>(InvDist * Node->Direction.Y);
                Node->Direction.Z = static_cast<float>(InvDist * Node->Direction.Z);
                const double Dot = (static_cast<double>(Node->Direction.Z) * RightZ +
                                    static_cast<double>(Node->Direction.Y) * RightY) +
                                   static_cast<double>(Node->Direction.X) * RightX;
                const long WantPan = static_cast<long>(Ftol32(Dot * Max3dPan));
                long CurrentPan = 0;
                Node->Buffer->GetPan(&CurrentPan);
                if (WantPan != CurrentPan)
                {
                    Node->Buffer->SetPan(WantPan);
                    bChanged = 1;
                }
            }
        }
    }
    else
    {
        bChanged = bWas3d;
        if (Buffer3d != nullptr && bWas3d != 0)
            Buffer3d->SetMode(DS3DMODE_DISABLE, Apply);
        if ((Node->DsCapsFlags & 0x40) != 0)
        {
            // 06ee6948: client GetPan (+0x2C), JL skips; clamp [-10000, 10000] signed.
            long ClientPan = 0;
            if (PlayKey->GetPan(&ClientPan) >= 0)
            {
                if (ClientPan < -10000)
                    ClientPan = -10000;
                else if (ClientPan > 10000)
                    ClientPan = 10000;
                long CurrentPan = 0;
                Node->Buffer->GetPan(&CurrentPan);
                if (ClientPan != CurrentPan)
                {
                    Node->Buffer->SetPan(ClientPan);
                    bChanged = 1;
                }
            }
        }
    }

    // 06ee69ac: GetArchId (+0x20) -> FUN_06ee31d0. Retail does not test the lookup result
    // (AL is discarded) and goes on with the out-slot; DestroyArchetype (06ee3150) erases
    // every instance node of that archetype from the list (FUN_06ee4740 at 06ee31b1) under
    // the instance lock, so a node the mixer walks always resolves.
    FSoundArchetype* Archetype = LookupArchetype(PlayKey->GetArchId());
    if ((Node->StatusFlags & 0x20) != 0)
    {
        // MOVZX word [arch+8]; GetPitch (+0x28), JL skips; ftol(FUN_06ee1670(pitch) * FIMUL
        // int32 rate); clamp unsigned (JBE 100000 / JNC 100).
        const int Rate = static_cast<int>(Archetype->nSamplesPerSec);
        float Pitch;
        if (PlayKey->GetPitch(&Pitch) >= 0)
        {
            unsigned long Frequency = Ftol32(PitchToMultiplier(Pitch) * static_cast<double>(Rate));
            if (Frequency > 100000)
                Frequency = 100000;
            else if (Frequency < 100)
                Frequency = 100;
            if (Node->Buffer->SetFrequency(Frequency) < 0)
            {
                char Message[0x2000];
                Fdump(kFdumpWarning, 0xDFD,
                      FormatMessage2000(Message, "SoundMgr:Couldn't set freq to %d.\n",
                                        Frequency));
            }
        }
    }
    const float ArchGain = Archetype->Gain;
    Archetype->Release();

    // 06ee6a6b: ((gain + master) + atten) * 100.0f on the FPU stack; > 0 -> 0 (TEST AH,0x41
    // + JNZ, fall-through = greater); < -10000 -> -10000 (TEST AH,5 + JP, fall-through =
    // less); NaN passes both; ftol.
    double Volume = ((static_cast<double>(ArchGain) + MasterGain) + ClientAtten) * 100.0;
    if (Volume > 0.0)
        Volume = 0.0;
    else if (Volume < -10000.0)
        Volume = -10000.0;
    const long WantVolume = static_cast<long>(Ftol32(Volume));
    long CurrentVolume = 0;
    Node->Buffer->GetVolume(&CurrentVolume);
    if (WantVolume != CurrentVolume)
    {
        Node->Buffer->SetVolume(WantVolume);
        bChanged = 1;
    }

    MixerApplyEax(Node);
    return bChanged;
}

unsigned char FSoundManager::OneShotAlreadyExpired(ISoundSource* PlayKey)
{
    // FUN_06ee6070 thiscall RET 4 (sole caller 06ee4c60): IsLooping (+0x1C) -> 0 (keep);
    // GetArchId (+0x20) -> FUN_06ee31d0 lookup, miss -> 1; else
    // Field4C > FUN_06ee16c0(arch, key) + GetStartTick (+0x10) (unsigned, SETA), then the
    // archetype is released. No play-key null check.
    if (PlayKey->IsLooping() != 0)
        return 0;
    const int ArchId = PlayKey->GetArchId();
    FSoundArchetype* Archetype = LookupArchetype(ArchId);
    if (Archetype == nullptr)
        return 1;
    const unsigned long DurationMs = ScaledDurationMs(Archetype, PlayKey);
    const unsigned long StartTick = PlayKey->GetStartTick();
    Archetype->Release();
    return Field4C > (DurationMs + StartTick) ? 1 : 0;
}

void FSoundManager::ApplyListenerFromSource(ISoundListenerPose* Pose)
{
    // FUN_06ee6e20 thiscall RET 4 (sole caller 06ee4b81, which skips the call for a null
    // pose). Locals: front (0,0,1), up (0,1,0), one vector (0,0,0) and one float 1.0f. The
    // position call and the velocity call share the vector, and the three factor calls share
    // the float, so a failed getter hands the next setter whatever the previous getter left
    // there. Each pair is skipped on JL; then FUN_06ee6d50.
    FVec3 InFront;
    FVec3 InUp;
    InFront.X = 0.0f;
    InFront.Y = 0.0f;
    InFront.Z = 1.0f;
    InUp.X = 0.0f;
    InUp.Y = 1.0f;
    InUp.Z = 0.0f;
    FVec3 InVec;
    InVec.X = 0.0f;
    InVec.Y = 0.0f;
    InVec.Z = 0.0f;
    float InScalar = 1.0f;
    if (Pose->GetOrientation(&InFront, &InUp) >= 0)
        SetOrientation(&InFront, &InUp);
    if (Pose->GetPosition(&InVec) >= 0)
        SetListenerPosition(&InVec);
    if (Pose->GetVelocity(&InVec) >= 0)
        SetListenerVelocity(&InVec);
    if (Pose->GetDistanceFactor(&InScalar) >= 0)
        SetDistanceFactor(InScalar);
    if (Pose->GetDopplerFactor(&InScalar) >= 0)
        SetDopplerFactor(InScalar);
    if (Pose->GetRolloffFactor(&InScalar) >= 0)
        SetRolloffFactor(InScalar);
    ApplyListener();
}

unsigned char FSoundManager::MixerSetStartOffset(FSoundInstanceNode* Node)
{
    // FUN_06ee6b70 thiscall RET 4 (sole caller 06ee528c). Payload = node+8. AL gates the
    // mixer's Play. No null checks in retail.
    ISoundSource* PlayKey = Node->PlayKey;
    unsigned long Tick;
    if ((Node->StatusFlags & 0x40) == 0)
    {
        PlayKey->Slot3();
        Tick = PlayKey->GetStartTick();
    }
    else
    {
        Tick = PlayKey->GetStartTick();
        if (Node->LastStartTick == Tick)
            return 0;
    }
    Node->LastStartTick = Tick;
    if (G_NeverSetStartPos != 0)
        return 1;
    FSoundArchetype* Archetype = LookupArchetype(PlayKey->GetArchId());
    if (Archetype == nullptr)
        return 0;
    unsigned long Delta = Field4C - Tick;
    if ((Node->StatusFlags & 0x20) != 0)
    {
        // 06ee6bfa: GetPitch (+0x28) into its own slot; 0 -> FUN_06ee1670(pitch), else clear
        // bit 0x20 and use 1.0f; Delta = ftol(FILD qword(u32 Delta) * multiplier).
        float Pitch;
        double Multiplier = 1.0;
        if (PlayKey->GetPitch(&Pitch) == 0)
            Multiplier = PitchToMultiplier(Pitch);
        else
            Node->StatusFlags &= ~0x20u;
        Delta = Ftol32(static_cast<double>(static_cast<unsigned long long>(Delta)) * Multiplier);
    }
    // 06ee6c4a: ftol(FILD qword(u32 Delta) * 0.001f (DAT_06ee949c) * FIMUL int32 word rate).
    unsigned long Samples =
        Ftol32((static_cast<double>(static_cast<unsigned long long>(Delta)) *
                static_cast<double>(0.001f)) *
               static_cast<double>(static_cast<int>(Archetype->nSamplesPerSec)));
    // 06ee6c7c: looping and past LoopEnd (unsigned JBE) -> wrap with an unsigned DIV; no
    // zero-length guard in retail.
    if (PlayKey->IsLooping() != 0 && Samples > Archetype->LoopEnd)
        Samples = ((Samples - Archetype->LoopStart) % (Archetype->LoopEnd - Archetype->LoopStart)) +
                  Archetype->LoopStart;
    const unsigned long ByteOffset = static_cast<unsigned long>(Archetype->nBlockAlign) * Samples;
    const unsigned long MaxBytes =
        Archetype->NumFrames * static_cast<unsigned long>(Archetype->nBlockAlign);
    // 06ee6cac JBE -> 06ee6b9d returns 0 WITHOUT releasing the archetype the lookup AddRef'd:
    // a retail reference leak, reproduced on purpose.
    if (MaxBytes <= ByteOffset)
        return 0;
    if (G_DisplayExtraDebug != 0 && PlayKey->IsLooping() == 0)
    {
        char Line[0x2000];
        Fdump(kFdumpNotice, 0xEAC,
              FormatMessage2000(Line, "Setting start offset %d for non-looping instance of %d.\n", // 06eeacac
                                static_cast<int>(ByteOffset), PlayKey->GetArchId()));
    }
    Node->Buffer->SetCurrentPosition(ByteOffset);
    Archetype->Release();
    if ((Node->StatusFlags & 0x40) == 0)
        return 1;
    DWORD Status = 0;
    Node->Buffer->GetStatus(&Status);
    return static_cast<unsigned char>((~static_cast<unsigned char>(Status)) & 1);
}

FSoundInstanceNode* FSoundManager::CreateInstanceFromKey(ISoundSource* PlayKey)
{
    // FUN_06ee60e0 thiscall RET 8 (playkey, payload; sole caller 06ee4f6b), returns 0 / -1.
    // Retail fills a stack payload that MixerTick freshly zeroed (FUN_06ee1040 at 06ee4f5b),
    // so its opening Buffer3d / Stop+Buffer / PlayKey releases (06ee6124..6164) find nothing;
    // the port fills a freshly zeroed intrusive node instead and returns it (nullptr = -1).
    // No null checks on the key, the device or the archetype buffer, as retail.
    char Message[0x2000];
    const int ArchId = PlayKey->GetArchId();
    FSoundArchetype* Archetype = LookupArchetype(ArchId);
    if (Archetype == nullptr)
    {
        // 06ee63c6: miss -> WARNING 0xCE5 unless the id is -1.
        if (ArchId != -1)
            Fdump(kFdumpWarning, 0xCE5,
                  FormatMessage2000(Message, "SoundMgr:Couldn't create an instance of archetype %d.\n", // 06eeab80
                                    ArchId));
        return nullptr;
    }

    // Port-only allocation (retail's list node comes from FUN_06ee7c00 in MixerTick).
    FSoundInstanceNode* Node = static_cast<FSoundInstanceNode*>(calloc(1, sizeof(FSoundInstanceNode)));
    if (Node == nullptr)
    {
        Archetype->Release();
        return nullptr;
    }
    InitInstancePayload(Node);

    // 06ee616a: DuplicateSoundBuffer (+0x14)(arch +0x24, &payload+4); JL -> WARNING 0xCDD,
    // Release the archetype, then fall into the 06ee63c6 tail, which (the id is not -1 here)
    // also emits WARNING 0xCE5; return -1.
    if (DirectSound->DuplicateSoundBuffer(Archetype->Buffer, &Node->Buffer) < 0)
    {
        Fdump(kFdumpWarning, 0xCDD,
              FormatMessage2000(Message, "SoundMgr:Couldn't duplicate archetype buffer %d.\n", ArchId)); // 06eeabb8
        Archetype->Release();
        Fdump(kFdumpWarning, 0xCE5,
              FormatMessage2000(Message, "SoundMgr:Couldn't create an instance of archetype %d.\n", // 06eeab80
                                ArchId));
        free(Node);
        return nullptr;
    }
    IDirectSoundBuffer* Dup = Node->Buffer;

    Node->PlayKey = PlayKey;
    PlayKey->AddRef();

    DSBCAPS Caps = {};
    Caps.dwSize = 0x14;
    Dup->GetCaps(&Caps);
    Node->DsCapsFlags = Caps.dwFlags;

    if ((Caps.dwFlags & 0x10) != 0)
    {
        // 06ee61ca: QI IDirectSound3DBuffer (DAT_06ee919c) into payload+8, result ignored.
        Dup->QueryInterface(kIidDirectSound3DBufferRetail, reinterpret_cast<void**>(&Node->Buffer3d));
        if (Node->Buffer3d != nullptr)
        {
            Node->StatusFlags |= 0x10;
            Node->bIs3dCache = 1;
        }
        else
            Node->StatusFlags &= ~0x10u;
    }

    if ((Caps.dwFlags & 0x20) != 0)
    {
        // 06ee61fe: GetFrequency (+0x20); >= 0 -> |= 0x20; else, with extra debug, NOTICE
        // 0xCC1 with GetArchId asked again (06ee6223).
        DWORD DummyFreq = 0;
        if (Dup->GetFrequency(&DummyFreq) >= 0)
            Node->StatusFlags |= 0x20;
        else if (G_DisplayExtraDebug != 0)
            Fdump(kFdumpNotice, 0xCC1,
                  FormatMessage2000(Message, "Failed to get requested freq control for instance of %d.\n", // 06eeabec
                                    PlayKey->GetArchId()));
    }

    if ((Caps.dwFlags & 0x8) != 0)
        Node->StatusFlags = (Node->StatusFlags & ~0x8u) | 0x4;

    if ((Caps.dwFlags & 0x100) != 0)
    {
        const unsigned long Align = Archetype->nBlockAlign;
        Node->Pad18 = Archetype->LoopStart * Align;
        Node->FeatureFlags = Archetype->LoopEnd * Align;
    }

    Archetype->Release();

    if (PlayKey->Is3d() != 0)
    {
        Node->Position = Position;
        Node->MinDistance = 10.0f;
        Node->MaxDistance = 500.0f;
        PlayKey->GetPosition(&Node->Position);
        PlayKey->GetMaxDistance(&Node->MaxDistance);
        PlayKey->GetMinDistance(&Node->MinDistance);
        Node->Position.Z = Node->Position.Z * static_cast<float>(CoordScale);
        Node->MaxDistSq = Node->MaxDistance * Node->MaxDistance;
        Node->MinDistSq = Node->MinDistance * Node->MinDistance;
        // 06ee631e: the three differences are FSTP'd as floats (FUN_06ee54e0), then
        // DistSq = (x*x + y*y) + z*z on the FPU stack, stored once as float.
        // Each difference is FLD float / FSUB float / FSTP float (Z, Y, X) - emulated in double.
        const float DiffZ = static_cast<float>(static_cast<double>(Node->Position.Z) - Position.Z);
        const float DiffY = static_cast<float>(static_cast<double>(Node->Position.Y) - Position.Y);
        const float DiffX = static_cast<float>(static_cast<double>(Node->Position.X) - Position.X);
        StoreVec3(&Node->Direction, DiffX, DiffY, DiffZ);
        const double DirX = Node->Direction.X;
        const double DirY = Node->Direction.Y;
        const double DirZ = Node->Direction.Z;
        Node->DistSq = static_cast<float>((DirX * DirX + DirY * DirY) + DirZ * DirZ);
    }

    // FUN_06ee60e0 fills payload only. MixerTick inserts via FUN_06ee7c00 pending list.
    return Node;
}

int DACOM_API FSoundManager::CreateArchetypePcm(void* Fmt, unsigned long ByteCount,
                                                unsigned long LoopStart, unsigned long LoopEnd,
                                                void* Samples, unsigned long ArchFlags)
{
    // FUN_06ee2be0 RET 0x1C: new 0x30 + FUN_06ee11f0, FUN_06ee1330 fill, FUN_06ee7e10 map insert.
    // A null operator new result is carried on (06ee2c12 EDI = 0) and dereferenced unchecked.
    FSoundArchetype* Archetype = new (std::nothrow) FSoundArchetype();
    if (DirectSound == nullptr ||
        Archetype->FillFromPcm(static_cast<unsigned short*>(Fmt), ByteCount, LoopStart, LoopEnd,
                               Samples, DirectSound, ArchFlags) != GR_OK)
    {
        Archetype->Release();
        FdumpCouldntCreateArchetype(0x59D, static_cast<int>(NextArchId));
        return -1;
    }
    return InsertArchetype(Archetype);
}

int DACOM_API FSoundManager::CreateArchetypeFromSource(void* Source, unsigned long ArchFlags)
{
    // LAB_06ee2dc0 RET 0xC: FUN_06ee2be0's sequence with the FUN_06ee1330 arguments read from a
    // PCM descriptor: format = desc+0 (8 bytes), byte count = desc+0x14, loop start = desc+0x8,
    // loop end = desc+0xC, samples = desc+0x18 (read only once the device is known to exist).
    // Device missing or fill < 0 -> Release, WARNING 0x5B3 with the unchanged next id, -1.
    // A null operator new result is carried on (06ee2df2 EDI = 0) and dereferenced unchecked.
    FSoundArchetype* Archetype = new (std::nothrow) FSoundArchetype();
    const unsigned char* Desc = static_cast<const unsigned char*>(Source);
    if (DirectSound == nullptr ||
        Archetype->FillFromPcm(reinterpret_cast<const unsigned short*>(Desc),
                               *reinterpret_cast<const unsigned long*>(Desc + 0x14),
                               *reinterpret_cast<const unsigned long*>(Desc + 0x8),
                               *reinterpret_cast<const unsigned long*>(Desc + 0xC),
                               *reinterpret_cast<void* const*>(Desc + 0x18), DirectSound,
                               ArchFlags) != GR_OK)
    {
        Archetype->Release();
        FdumpCouldntCreateArchetype(0x5B3, static_cast<int>(NextArchId));
        return -1;
    }
    return InsertArchetype(Archetype);
}

int DACOM_API FSoundManager::CreateArchetypeFromWav(IDAComponent* FileSystem,
                                                    unsigned long ArchFlags)
{
    // LAB_06ee2f90 RET 0xC: new 0x30 + FUN_06ee11f0; device missing or FUN_06ee1240 < 0 ->
    // Release, WARNING 0x5C5 with the unchanged next id, -1; else id = next++, map insert.
    // No file-system null test in retail.
    // A null operator new result is carried on (06ee2fc2 EDI = 0) and dereferenced unchecked.
    FSoundArchetype* Archetype = new (std::nothrow) FSoundArchetype();
    if (DirectSound == nullptr || Archetype->FillFromWav(FileSystem, DirectSound, ArchFlags) != GR_OK)
    {
        Archetype->Release();
        FdumpCouldntCreateArchetype(0x5C5, static_cast<int>(NextArchId));
        return -1;
    }
    return InsertArchetype(Archetype);
}

GENRESULT DACOM_API FSoundManager::DestroyArchetype(int Id)
{
    // LAB_06ee3150 RET 8: map find (FUN_06ee80d0 lower_bound + key check); miss -> -1. Take the
    // value, erase the entry (FUN_06ee76d0); a null value -> -1 (after the erase); otherwise
    // FUN_06ee4740(id) removes that archetype's instances, the value is released, 0. No
    // lookup AddRef/Release pair, and the id -1 needs no special case (it is never a key).
    unsigned long Index = 0;
    while (Index < ArchCount && ArchEntries[Index].Id != Id)
        ++Index;
    if (Index == ArchCount)
        return GR_GENERIC;
    FSoundArchetype* Archetype = ArchEntries[Index].Archetype;
    // Erase keeping the ascending-id order the retail map iterates in.
    memmove(&ArchEntries[Index], &ArchEntries[Index + 1],
            (ArchCount - Index - 1) * sizeof(FArchMapEntry));
    --ArchCount;
    if (ArchCount == 0)
        bArchEmpty = 1;
    if (Archetype == nullptr)
        return GR_GENERIC;
    StopInstancesForArch(Id);
    Archetype->Release();
    return GR_OK;
}

GENRESULT DACOM_API FSoundManager::QueryArchetype(int Id, void** Out)
{
    // LAB_06ee3250 RET 0xC: Out==0 → -1; FUN_06ee31d0; fail → *Out=0,-1;
    // QI 1.11_ISoundArchetype; fail FDUMP 0x627 then Release → -1; else Release → 0.
    if (Out == nullptr)
        return GR_GENERIC;
    FSoundArchetype* Archetype = LookupArchetype(Id);
    if (Archetype == nullptr)
    {
        *Out = nullptr;
        return GR_GENERIC;
    }
    const GENRESULT Result = Archetype->QueryInterface(Iids::SoundArchetype, Out);
    if (Result != GR_OK)
    {
        Fdump(kFdumpWarning, 0x627, "Failed to query the IID_ISoundArchetype.\n"); // 06eea214
        Archetype->Release();
        return GR_GENERIC;
    }
    Archetype->Release();
    return GR_OK;
}

GENRESULT DACOM_API FSoundManager::AttachPlayKey(ISoundSource* PlayKey, unsigned long Index)
{
    // FUN_06ee32e0 RET 0xC: null key → -1; AddRef; insert before the Index-th node, or before
    // the head when Index >= count (JNC). FUN_06ee7d80 node. The head (+0x110) always exists
    // (ctor 06ee1bbd), so retail does not test it.
    if (PlayKey == nullptr)
        return GR_GENERIC;
    ISoundSource* Key = PlayKey;
    Key->AddRef();
    FAttachedNode* InsertBefore = AttachedSentinel;
    if (Index < AttachedCount)
    {
        InsertBefore = AttachedSentinel->Next;
        unsigned long Walk = 0;
        while (Walk < Index && InsertBefore != AttachedSentinel)
        {
            InsertBefore = InsertBefore->Next;
            ++Walk;
        }
    }
    FAttachedNode* Node = static_cast<FAttachedNode*>(calloc(1, sizeof(FAttachedNode)));
    if (Node == nullptr)
        return GR_GENERIC;
    Node->PlayKey = Key;
    Node->Next = InsertBefore;
    Node->Prev = InsertBefore->Prev;
    InsertBefore->Prev->Next = Node;
    InsertBefore->Prev = Node;
    ++AttachedCount;
    return GR_OK;
}

GENRESULT DACOM_API FSoundManager::DetachPlayKey(ISoundSource* PlayKey)
{
    // LAB_06ee34d0 RET 8: unlink matching instance (CS+FUN_06ee85e0/7d10+SetEvent), then attached.
    FSoundInstanceNode* Inst = FindInstanceByKey(PlayKey);
    if (Inst != nullptr)
    {
        EnterCriticalSection(&G_InstanceLock);
        UnlinkInstance(Inst);
        SetEvent(G_MixerEvent);
        LeaveCriticalSection(&G_InstanceLock);
    }
    // 06ee3571..35f1: EVERY attached node whose key matches is released (vtbl+8), unlinked, freed,
    // +0x114--; the walk continues past each removal.
    FAttachedNode* Attached = AttachedSentinel->Next;
    while (Attached != AttachedSentinel)
    {
        if (Attached->PlayKey == PlayKey)
        {
            Attached->PlayKey->Release();
            FAttachedNode* Next = Attached->Next;
            Attached->Prev->Next = Attached->Next;
            Attached->Next->Prev = Attached->Prev;
            free(Attached);
            --AttachedCount;
            Attached = Next;
        }
        else
        {
            Attached = Attached->Next;
        }
    }
    return GR_OK;
}

GENRESULT DACOM_API FSoundManager::ReplaceAttachedList(void** Keys, unsigned long Count)
{
    // FUN_06ee33b0 RET 0xC: Release+erase attached; Count==0 → destroy all instances
    // (FUN_06ee7590 each) + SetEvent under the lock, 0. Otherwise append each key at the end
    // (FUN_06ee7bb0 node, size++, then AddRef); a null key returns -1 at once, leaving the
    // keys already appended attached. No test on the key array pointer itself.
    ClearAttached();
    if (Count == 0)
    {
        EnterCriticalSection(&G_InstanceLock);
        FSoundInstanceNode* Node = G_InstanceSentinel.Next;
        while (Node != &G_InstanceSentinel)
        {
            FSoundInstanceNode* Next = Node->Next;
            UnlinkInstance(Node);
            Node = Next;
        }
        SetEvent(G_MixerEvent);
        LeaveCriticalSection(&G_InstanceLock);
        return GR_OK;
    }
    for (unsigned long Index = 0; Index < Count; ++Index)
    {
        if (Keys[Index] == nullptr)
            return GR_GENERIC;
        ISoundSource* Key = static_cast<ISoundSource*>(Keys[Index]);
        FAttachedNode* Node = static_cast<FAttachedNode*>(calloc(1, sizeof(FAttachedNode)));
        Node->PlayKey = Key;
        Node->Next = AttachedSentinel;
        Node->Prev = AttachedSentinel->Prev;
        AttachedSentinel->Prev->Next = Node;
        AttachedSentinel->Prev = Node;
        ++AttachedCount;
        Key->AddRef();
    }
    return GR_OK;
}

unsigned long DACOM_API FSoundManager::GetAttachedCount()
{
    return AttachedCount;
}

unsigned long DACOM_API FSoundManager::GetPlayingCount()
{
    return G_InstanceCount;
}

GENRESULT DACOM_API FSoundManager::BindParser()
{
    // LAB_06ee56f0: nested this is object+8; body uses object+0x134 / +0x0C.
    if (Parser != nullptr)
    {
        Parser->Release();
        Parser = nullptr;
    }
    // 06ee5712..5721: DACOM_Acquire() unchecked, QI("1.11_IProfileParser") straight into &+0x134;
    // its result is ignored.
    ICOManager* Dacom = DACOM_Acquire();
    Dacom->QueryInterface(Iids::ProfileParser, reinterpret_cast<void**>(&Parser));
    bParserBound = 1;
    return GR_OK;
}

void FSoundManager::DumpDeviceNotices()
{
    // FUN_06ee5b20 thiscall (Ghidra also split the branch targets LAB_06ee5c0d / LAB_06ee5c50 /
    // LAB_06ee5e8b). Every line is formatted by FUN_06ee71c0 into one 0x2000 stack buffer and
    // sent as NOTICE. "" is DAT_06eeaf78, " NOT" is DAT_06eeaae4.
    char Line[0x2000];
    const char* Enabled = ((CapsFlags & 1) != 0) ? "ENABLED" : "DISABLED";
    const char* Requested = ((Flags & 1) != 0) ? "" : " NOT";
    Fdump(kFdumpNotice, 0xBA0,
          FormatMessage2000(Line, "SoundMgr:2D HW%s requested and %s with %d HW buffers available.\n",
                            Requested, Enabled, Field50));

    Enabled = ((CapsFlags & 2) != 0) ? "ENABLED" : "DISABLED";
    Requested = ((Flags & 2) != 0) ? "" : " NOT";
    Fdump(kFdumpNotice, 0xBA5,
          FormatMessage2000(Line,
                            "SoundMgr:3D HW%s requested and %s with %d 3D HW buffers available.\n",
                            Requested, Enabled, Field94));

    if ((CapsFlags & 3) != 0)
    {
        Requested = (G_AllowHwToSw != 0) ? "" : " NOT";
        Fdump(kFdumpNotice, 0xBAA,
              FormatMessage2000(Line, "SoundMgr:Transfer of HW buffers to SW while playing%s allowed.\n",
                                Requested));
        Requested = (G_AllowSwToHw != 0) ? "" : " NOT";
        Fdump(kFdumpNotice, 0xBAC,
              FormatMessage2000(Line, "SoundMgr:Transfer of SW buffers to HW while playing%s allowed.\n",
                                Requested));
    }

    // 06ee5c81: TEST AH,2 / AH,1 / AL sign (0x80) / AL,0x40 / AH,4 on the +0x90 dword.
    // Literals 06eea9a0 / 964 / 928 / 8f0 / 8c0 / 894 / 86c / 830 / 804 / 7e0 (all end in ".\n").
    if ((CapsFlags & 0x200) != 0)
        Fdump(kFdumpNotice, 0xBB1,
              FormatMessage2000(Line, "SoundMgr:Using Full Algorithm for DSound3D SW buffers.\n"));
    else if ((CapsFlags & 0x100) != 0)
        Fdump(kFdumpNotice, 0xBB5,
              FormatMessage2000(Line, "SoundMgr:Using Light Algorithm for DSound3D SW buffers.\n"));
    else if ((CapsFlags & 0x80) != 0)
        Fdump(kFdumpNotice, 0xBB9,
              FormatMessage2000(Line, "SoundMgr:Using Basic Algorithm for DSound3D SW buffers.\n"));
    else if ((CapsFlags & 0x40) != 0)
        Fdump(kFdumpNotice, 0xBBD,
              FormatMessage2000(Line, "SoundMgr:Using User Default for DSound3D SW buffers.\n"));
    else if ((CapsFlags & 0x400) != 0)
        Fdump(kFdumpNotice, 0xBC1,
              FormatMessage2000(Line, "SoundMgr:Disabling DSound3D for SW buffers.\n"));

    if ((CapsFlags & 0x10) != 0)
        Fdump(kFdumpNotice, 0xBC6, FormatMessage2000(Line, "SoundMgr:Using Right Handed Coordinates.\n"));
    if ((CapsFlags & 0x20) != 0)
        Fdump(kFdumpNotice, 0xBCA, FormatMessage2000(Line, "SoundMgr:Forcing all 2D buffers to SW.\n"));
    if (G_ForceFreqToSw != 0)
        Fdump(kFdumpNotice, 0xBCF,
              FormatMessage2000(Line, "SoundMgr:Forcing all frequency controlled buffers to SW.\n"));
    if (G_NeverSetStartPos != 0)
        Fdump(kFdumpNotice, 0xBD4, FormatMessage2000(Line, "SoundMgr:Offsetting start times disabled.\n"));
    if (G_DisplayExtraDebug != 0)
        Fdump(kFdumpNotice, 0xBD9, FormatMessage2000(Line, "SoundMgr:Extra debug info enabled.\n"));

    Enabled = ((CapsFlags & 4) != 0) ? "ENABLED" : "DISABLED";
    Fdump(kFdumpNotice, 0xBDE, FormatMessage2000(Line, "SoundMgr:EAX %s.\n", Enabled));
    Fdump(kFdumpNotice, 0xBE0, FormatMessage2000(Line, "SoundMgr:Max channels set to %d.\n", MaxChannels));
}

GENRESULT FSoundManager::CreateDirectSound()
{
    // FUN_06ee5730 thiscall. Already-created +0x12C returns -1, not success.
    if (DirectSound != nullptr)
        return GR_GENERIC;

    const HRESULT InitHr = CoInitialize(nullptr);
    if (InitHr < 0)
        return GR_GENERIC;
    bCoInitialized = 1;

    if (DirectSound == nullptr)
    {
        CoCreateInstance(kClsidDirectSound, nullptr, CLSCTX_INPROC_SERVER, kIidDirectSoundRetail,
                         reinterpret_cast<void**>(&DirectSound));
    }
    if (DirectSound == nullptr)
        return GR_GENERIC;

    HRESULT Hr = DirectSound->Initialize(&DeviceGuid);
    if (Hr < 0)
    {
        char Warn[0x2000];
        Fdump(kFdumpWarning, 0xB31,
              FormatMessage2000(Warn, "SoundManager:Couldn't create DSound device %s.\n", // 06eea778
                                DeviceIdString));
        Hr = DirectSound->Initialize(nullptr);
        if (Hr < 0)
        {
            Fdump(kFdumpWarning, 0xB34, "SoundManager:Couldn't initialize default DSound device.\n"); // 06eea73c
            DirectSound->Release();
            DirectSound = nullptr;
            return GR_GENERIC;
        }
    }

    DirectSound->SetCooperativeLevel(Window, DSSCL_PRIORITY);

    DSCAPS Caps = {};
    Caps.dwSize = 0x60;
    DirectSound->GetCaps(&Caps);
    Field94 = Caps.dwFreeHw3DAllBuffers;
    Field50 = Caps.dwFreeHwMixingAllBuffers;
    if (Caps.dwFreeHwMixingAllBuffers + Caps.dwFreeHw3DAllBuffers != 0 && MaxChannels == 0)
    {
        unsigned long Mix = Caps.dwFreeHw3DAllBuffers;
        if (Mix < Caps.dwFreeHwMixingAllBuffers)
            Mix = Caps.dwFreeHwMixingAllBuffers;
        MaxChannels = Mix;
        if (MaxChannels < 8)
            MaxChannels = 8;
    }

    DSBUFFERDESC PrimaryDesc = {};
    PrimaryDesc.dwSize = 0x24;
    PrimaryDesc.dwFlags = DSBCAPS_PRIMARYBUFFER | DSBCAPS_CTRL3D;
    DirectSound->CreateSoundBuffer(&PrimaryDesc, &Primary, nullptr);
    if (Primary == nullptr)
    {
        Fdump(kFdumpNotice, 0xB8E, "SoundManager: Couldn't create primary sound buffer.\n"); // 06eea6cc
        DirectSound->Release();
        DirectSound = nullptr;
        return GR_GENERIC;
    }

    CapsFlags &= ~3u;
    if ((Caps.dwFlags & DSCAPS_EMULDRIVER) != 0 || Caps.dwUnlockTransferRateHwBuffers != 0)
    {
        // 06ee59e3: Flags &= ~3, then NOTICE 0xB5D formatted through FUN_06ee71c0.
        Flags &= ~3u;
        char Isa[0x2000];
        Fdump(kFdumpNotice, 0xB5D,
              FormatMessage2000(Isa, "ISA or slow sound card detected. Disabling HW buffers.\n")); // 06eea704
    }
    else
    {
        WAVEFORMATEX Fmt = {};
        Fmt.wFormatTag = 1;
        Fmt.nChannels = 2;
        Fmt.nSamplesPerSec = 0x5622;
        Fmt.nAvgBytesPerSec = 0x15888;
        Fmt.nBlockAlign = 4;
        Fmt.wBitsPerSample = 0x10;
        if ((Flags & 1) != 0)
        {
            DSBUFFERDESC Probe = {};
            Probe.dwSize = 0x24;
            Probe.dwFlags = DSBCAPS_LOCHARDWARE;
            Probe.dwBufferBytes = 4;
            Probe.lpwfxFormat = &Fmt;
            IDirectSoundBuffer* ProbeBuf = nullptr;
            if (DirectSound->CreateSoundBuffer(&Probe, &ProbeBuf, nullptr) >= 0)
                CapsFlags |= 1;
            if (ProbeBuf != nullptr)
                ProbeBuf->Release();
        }
        if ((Flags & 2) != 0)
        {
            DSBUFFERDESC Probe = {};
            Probe.dwSize = 0x24;
            Probe.dwFlags = DSBCAPS_LOCHARDWARE | DSBCAPS_CTRL3D;
            Probe.dwBufferBytes = 4;
            Probe.lpwfxFormat = &Fmt;
            IDirectSoundBuffer* ProbeBuf = nullptr;
            if (DirectSound->CreateSoundBuffer(&Probe, &ProbeBuf, nullptr) >= 0)
                CapsFlags |= 2;
            if (ProbeBuf != nullptr)
                ProbeBuf->Release();
        }
    }

    if ((PrimaryDesc.dwFlags & DSBCAPS_CTRL3D) != 0)
        Primary->QueryInterface(kIidDirectSound3DListenerRetail, reinterpret_cast<void**>(&Listener));

    unsigned long BlockAlign = (Channels * BitsPerSample) >> 3;
    unsigned long AvgBytes = SampleRate * BlockAlign;
    WAVEFORMATEX PrimaryFmt = {};
    PrimaryFmt.wFormatTag = 1;
    PrimaryFmt.nChannels = static_cast<WORD>(Channels);
    PrimaryFmt.nSamplesPerSec = SampleRate;
    PrimaryFmt.nAvgBytesPerSec = AvgBytes;
    PrimaryFmt.nBlockAlign = static_cast<WORD>(BlockAlign);
    PrimaryFmt.wBitsPerSample = static_cast<WORD>(BitsPerSample);
    Primary->SetFormat(&PrimaryFmt);

    SetEaxPreview(EaxEnv, EaxVol, EaxDecay, EaxDamp);
    Primary->Play(0, 0, DSBPLAY_LOOPING);
    DumpDeviceNotices();
    return GR_OK;
}

GENRESULT DACOM_API FSoundManager::StartupDevice(HWND Hwnd, unsigned long InFlags)
{
    // LAB_06ee3670 RET 0xC. Parser bind runs before the flags==0 reject.
    if (bParserBound == 0)
        BindParser();

    Window = Hwnd;
    if (InFlags == 0)
    {
        Fdump(kFdumpError, 0x6D5, "SoundManager: startup options must be non-zero.\n"); // 06eea47c
        return GR_GENERIC;
    }

    Flags = InFlags;
    if ((InFlags & 0x800) != 0)
        Flags &= ~3u;

    // One integer and one float stack variable ([ESP+0x10] / [ESP+0x1C] in retail) carry every
    // value. Each block resets them to its default only once a parser was obtained, so a block
    // whose acquisition fails inherits the previous block's value, exactly as retail does.
    unsigned long Value = 0;
    float FloatValue = 0.0f;
    ICOManager* Dacom = DACOM_Acquire();
    IDAComponent* Local = nullptr;

    // 06ee36f0..38e3 useEAX (default = current bit 2). The parser is released inline here.
    if (AcquireParserLocal(Dacom, Parser, &Local))
    {
        IProfileParser* Ini = static_cast<IProfileParser*>(Local);
        Value = Flags & 4;
        void* Audio = Ini->OpenSection("AudioProperties", 3);
        if (Audio != nullptr)
        {
            if (Ini->GetString(Audio, "useEAX", G_IniIntBuffer, 0x400) != 0)
                Value = ParseIniUnsigned(G_IniIntBuffer);
            Ini->CloseSection(Audio);
        }
    }
    if (Local != nullptr)
        Local->Release();
    if (Value != 0)
        Flags |= 4;
    else
        Flags &= ~4u;

    // 06ee38e9..3a9a reverbBaseEnvironment (default 0) -> +0x58.
    if (AcquireParserLocal(Dacom, Parser, &Local))
    {
        IProfileParser* Ini = static_cast<IProfileParser*>(Local);
        Value = 0;
        void* Audio = Ini->OpenSection("AudioProperties", 3);
        if (Audio != nullptr)
        {
            if (Ini->GetString(Audio, "reverbBaseEnvironment", G_IniIntBuffer, 0x400) != 0)
                Value = ParseIniUnsigned(G_IniIntBuffer);
            Ini->CloseSection(Audio);
        }
    }
    ReleaseComPtr(&Local);
    EaxEnv = static_cast<int>(Value);

    // 06ee3a9d..3b7b reverbMasterVolume (default 0.0f) -> +0x5C.
    if (AcquireParserLocal(Dacom, Parser, &Local))
    {
        IProfileParser* Ini = static_cast<IProfileParser*>(Local);
        FloatValue = 0.0f;
        void* Audio = Ini->OpenSection("AudioProperties", 3);
        if (Audio != nullptr)
        {
            if (Ini->GetString(Audio, "reverbMasterVolume", G_IniFloatBuffer, 0x400) != 0)
                FloatValue = ParseIniFloat(G_IniFloatBuffer);
            Ini->CloseSection(Audio);
        }
    }
    ReleaseComPtr(&Local);
    EaxVol = FloatValue;

    // 06ee3b7e..3c5c reverbDecayTime (default 0.1f = 0x3DCCCCCD) -> +0x60.
    if (AcquireParserLocal(Dacom, Parser, &Local))
    {
        IProfileParser* Ini = static_cast<IProfileParser*>(Local);
        FloatValue = 0.1f;
        void* Audio = Ini->OpenSection("AudioProperties", 3);
        if (Audio != nullptr)
        {
            if (Ini->GetString(Audio, "reverbDecayTime", G_IniFloatBuffer, 0x400) != 0)
                FloatValue = ParseIniFloat(G_IniFloatBuffer);
            Ini->CloseSection(Audio);
        }
    }
    ReleaseComPtr(&Local);
    EaxDecay = FloatValue;

    // 06ee3c5f..3d39 reverbDamping (default 1.0f) -> +0x64.
    if (AcquireParserLocal(Dacom, Parser, &Local))
    {
        IProfileParser* Ini = static_cast<IProfileParser*>(Local);
        FloatValue = 1.0f;
        void* Audio = Ini->OpenSection("AudioProperties", 3);
        if (Audio != nullptr)
        {
            if (Ini->GetString(Audio, "reverbDamping", G_IniFloatBuffer, 0x400) != 0)
                FloatValue = ParseIniFloat(G_IniFloatBuffer);
            Ini->CloseSection(Audio);
        }
    }
    ReleaseComPtr(&Local);
    EaxDamp = FloatValue;

    // 06ee3d3c..3e1b reverbMix (default 0.0f) -> +0x78 = ftol(value).
    if (AcquireParserLocal(Dacom, Parser, &Local))
    {
        IProfileParser* Ini = static_cast<IProfileParser*>(Local);
        FloatValue = 0.0f;
        void* Audio = Ini->OpenSection("AudioProperties", 3);
        if (Audio != nullptr)
        {
            if (Ini->GetString(Audio, "reverbMix", G_IniFloatBuffer, 0x400) != 0)
                FloatValue = ParseIniFloat(G_IniFloatBuffer);
            Ini->CloseSection(Audio);
        }
    }
    ReleaseComPtr(&Local);
    *reinterpret_cast<int*>(&ReverbMix) = static_cast<int>(FloatValue);

    // 06ee3e1e..3ec4: the default "{00000000-...}" (via PTR DAT_06eea024) is copied into +0x0D only
    // once a parser was obtained (06ee3e60); without one +0x0D keeps the ctor's zeros. Both copies
    // are strncpy(.., 0x28) with no forced terminator; the INI value goes through DAT_06eeb7e0.
    if (AcquireParserLocal(Dacom, Parser, &Local))
    {
        IProfileParser* Ini = static_cast<IProfileParser*>(Local);
        strncpy(DeviceIdString, kDefaultDeviceId, 0x28);
        void* Section = Ini->OpenSection("SoundManager", 3);
        if (Section != nullptr)
        {
            if (Ini->GetString(Section, "deviceID", G_IniStringBuffer, 0x400) != 0)
                strncpy(DeviceIdString, G_IniStringBuffer, 0x28);
            Ini->CloseSection(Section);
        }
    }
    ReleaseComPtr(&Local);
    ParseGuidString(DeviceIdString, &DeviceGuid); // FUN_06ee6fd0(+0x0D, +0x38)

    const unsigned long AlgBits = Flags & 0x3C0;
    const char* AlgName = "USER";
    if (AlgBits == 0x200)
        AlgName = "FULL";
    else if (AlgBits == 0x80)
        AlgName = "BASIC";
    else if (AlgBits == 0x100)
        AlgName = "LIGHT";
    else if (AlgBits == 0x400)
        AlgName = "NONE";
    // 06ee3f2a sprintf's the name into a stack buffer; 06ee3f66 strncpy(AlgBuf, it, 0x20) runs only
    // once a parser was obtained, then the INI value (via DAT_06eeb7e0) overwrites it with another
    // strncpy(.., 0x20). Without a parser retail compares an uninitialised AlgBuf; the port fills it
    // with the default name up front so that case stays defined.
    char AlgBuf[0x20];
    strncpy(AlgBuf, AlgName, 0x20);
    if (AcquireParserLocal(Dacom, Parser, &Local))
    {
        IProfileParser* Ini = static_cast<IProfileParser*>(Local);
        void* Section = Ini->OpenSection("SoundManager", 3);
        if (Section != nullptr)
        {
            if (Ini->GetString(Section, "3D_SW_Algorithm", G_IniStringBuffer, 0x400) != 0)
                strncpy(AlgBuf, G_IniStringBuffer, 0x20);
            Ini->CloseSection(Section);
        }
    }
    ReleaseComPtr(&Local);
    Flags &= ~0x3C0u;
    if (_stricmp(AlgBuf, "FULL") == 0)
    {
        Flags |= 0x200;
        CapsFlags |= 0x200;
        CopyAlgGuid(&Field7C, &kDs3dAlgHrtfFull);
    }
    else if (_stricmp(AlgBuf, "LIGHT") == 0)
    {
        Flags |= 0x100;
        CapsFlags |= 0x100;
        CopyAlgGuid(&Field7C, &kDs3dAlgHrtfLight);
    }
    else if (_stricmp(AlgBuf, "BASIC") == 0)
    {
        Flags |= 0x80;
        CapsFlags |= 0x80;
        CopyAlgGuid(&Field7C, &kDs3dAlgNoVirtual);
    }
    else if (_stricmp(AlgBuf, "USER") == 0)
    {
        Flags |= 0x40;
        CapsFlags |= 0x40;
        CopyAlgGuid(&Field7C, &kDs3dAlgGuidNull);
    }
    else if (_stricmp(AlgBuf, "NONE") == 0)
    {
        G_Ds3dCreateFlags = (G_Ds3dCreateFlags & ~0x10u) | 0x40u;
        CopyAlgGuid(&Field7C, &kDs3dAlgGuidNull);
    }

    // 06ee41a3..4369 useNoHW (default = current bit 0x800) - a non-zero value clears bits 0/1.
    if (AcquireParserLocal(Dacom, Parser, &Local))
    {
        IProfileParser* Ini = static_cast<IProfileParser*>(Local);
        Value = Flags & 0x800;
        void* Section = Ini->OpenSection("SoundManager", 3);
        if (Section != nullptr)
        {
            if (Ini->GetString(Section, "useNoHW", G_IniIntBuffer, 0x400) != 0)
                Value = ParseIniUnsigned(G_IniIntBuffer);
            Ini->CloseSection(Section);
        }
    }
    ReleaseComPtr(&Local);
    if (Value != 0)
        Flags &= ~3u;

    // From here on FUN_06ee72f0 does the reads; every call writes the shared Value and the caller
    // reads Value back (not the return), so a failed parser acquisition keeps the previous value.
    IProfileParser* Cached = Parser;
    ReadProfileInt(Dacom, Cached, "SoundManager", "rightHandedCoordinates", Flags & 0x10, &Value);
    if (Value != 0)
    {
        Flags |= 0x10;
        CapsFlags |= 0x10;
        CoordScale = -1; // +0xA4
    }
    else
        Flags &= ~0x10u;

    ReadProfileInt(Dacom, Cached, "SoundManager", "createAll2DInSoftware", Flags & 0x20, &Value);
    if (Value != 0)
    {
        Flags |= 0x20;
        CapsFlags |= 0x20;
    }
    else
    {
        Flags &= ~0x20u;
        CapsFlags &= ~0x20u;
    }

    // 06ee444a..4506: presence probe of speakerConfiguration into DAT_06eebc00; only when the key
    // exists is it read through FUN_06ee72f0 (default 2) and applied after the device is up.
    unsigned char bSpeakerKey = 0;   // [ESP+0x27]
    unsigned long SpeakerValue = 0;  // [ESP+0x1C]
    int SpeakerFound = -1;           // EDI
    if (AcquireParserLocal(Dacom, Parser, &Local))
    {
        IProfileParser* Ini = static_cast<IProfileParser*>(Local);
        void* Section = Ini->OpenSection("SoundManager", 3);
        if (Section != nullptr)
        {
            if (Ini->GetString(Section, "speakerConfiguration", G_IniSpeakerBuffer, 0x400) != 0)
                SpeakerFound = 0;
            Ini->CloseSection(Section);
        }
    }
    ReleaseComPtr(&Local);
    if (SpeakerFound >= 0)
    {
        ReadProfileInt(Dacom, Cached, "SoundManager", "speakerConfiguration", 2, &Value);
        bSpeakerKey = 1;
        SpeakerValue = Value;
    }

    ReadProfileInt(Dacom, Cached, "SoundManager", "maxSoundChannels", MaxChannels, &Value);
    MaxChannels = Value; // +0x54

    ReadProfileInt(Dacom, Cached, "SoundManager", "use2DHW", Flags & 1, &Value);
    if (Value != 0)
        Flags |= 1;
    else
        Flags &= ~1u;

    ReadProfileInt(Dacom, Cached, "SoundManager", "use3DHW", Flags & 2, &Value);
    if (Value != 0)
        Flags |= 2;
    else
        Flags &= ~2u;

    ReadProfileInt(Dacom, Cached, "SoundManager", "ALLOW_HW_TO_SW", G_AllowHwToSw, &Value);
    if (Value != 0 && (Flags & 3) != 0)
        G_AllowHwToSw = 1;
    else
        G_AllowHwToSw = 0;

    ReadProfileInt(Dacom, Cached, "SoundManager", "ALLOW_SW_TO_HW", G_AllowSwToHw, &Value);
    if (Value != 0 && (Flags & 3) != 0)
        G_AllowSwToHw = 1;
    else
        G_AllowSwToHw = 0;

    ReadProfileInt(Dacom, Cached, "SoundManager", "FORCE_FREQ_CONTROL_TO_SW", G_ForceFreqToSw, &Value);
    G_ForceFreqToSw = Value;
    ReadProfileInt(Dacom, Cached, "SoundManager", "NEVER_SET_START_POS", G_NeverSetStartPos, &Value);
    G_NeverSetStartPos = Value;
    ReadProfileInt(Dacom, Cached, "SoundManager", "DISPLAY_EXTRA_DEBUG_INFO", G_DisplayExtraDebug,
                   &Value);
    G_DisplayExtraDebug = Value;

    // 06ee46c2..46fe: IMUL value,100 then FILD qword {product, 0} (unsigned) -> FSTP float +0xF0.
    ReadProfileInt(Dacom, Cached, "SoundManager", "max3DPan", 0x64, &Value);
    Max3dPan = static_cast<float>(static_cast<unsigned long long>(Value * 100));

    // 06ee4704..470d: FUN_06ee5730 < 0 -> return -1.
    if (CreateDirectSound() < 0)
        return GR_GENERIC;
    if (bSpeakerKey != 0)
        SetSpeakerConfig(SpeakerValue); // this->vtbl+0x1C
    ApplyListener();                    // FUN_06ee6d50
    return GR_OK;
}

unsigned char FSoundManager::SetupInstanceNotify(FSoundInstanceNode* Node)
{
    // FUN_06ee1a40 stdcall RET 4. Arg = payload (node+8).
    if (G_NotifyThread == nullptr)
    {
        unsigned long Tid = 0;
        G_NotifyThread = CreateThread(nullptr, 0, NotifyThreadProc, nullptr, 0, &Tid);
        if (G_NotifyThread == nullptr)
        {
            Fdump(kFdumpError, 0x347, "SoundManager: couldn't create thread for looping sounds.\n"); // 06eea1b0
            return 0;
        }
    }
    if (Node->NotifyEvent != nullptr)
    {
        CloseHandle(Node->NotifyEvent);
        Node->NotifyEvent = nullptr;
    }
    Node->NotifyEvent = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    if (Node->NotifyEvent == nullptr)
        return 0;
    IDirectSoundNotify* Notify = nullptr;
    if (Node->Buffer->QueryInterface(kIidDirectSoundNotify, reinterpret_cast<void**>(&Notify)) < 0)
    {
        CloseHandle(Node->NotifyEvent);
        Node->NotifyEvent = nullptr;
        return 0;
    }
    DSBPOSITIONNOTIFY Pos;
    Pos.dwOffset = Node->FeatureFlags;
    Pos.hEventNotify = Node->NotifyEvent;
    const HRESULT Hr = Notify->SetNotificationPositions(1, &Pos);
    Notify->Release();
    if (Hr < 0)
        return 0;
    Node->StatusFlags |= 2;
    return 1;
}

GENRESULT DACOM_API FSoundManager::ShutdownDevice()
{
    // FUN_06ee4900 RET 0x4. Does not Release parser cache at +0x134.
    // 06ee4900..4921: only a running notify thread is stopped - flag DAT_06eeaf74 = 1, close the
    // handle DAT_06eeaf6c, null it. No SetEvent, no wait.
    if (G_NotifyThread != nullptr)
    {
        G_MixerStopFlag = 1;
        CloseHandle(G_NotifyThread);
        G_NotifyThread = nullptr;
    }
    // 06ee4927..498d: drain the global list DAT_06eeaf00 without taking DAT_06eeaf0c - unlink,
    // payload dtor FUN_06ee85e0 -> FUN_06ee11a0, node free FUN_06ee7d10, DAT_06eeaf08--.
    if (G_InstanceCount != 0)
    {
        FSoundInstanceNode* Node = G_InstanceSentinel.Next;
        while (Node != &G_InstanceSentinel)
        {
            FSoundInstanceNode* Next = Node->Next;
            Node->Prev->Next = Node->Next;
            Node->Next->Prev = Node->Prev;
            ReleaseInstancePayload(Node);
            free(Node);
            --G_InstanceCount;
            Node = Next;
        }
    }
    // 06ee4993..49f8: drain the +0x110 attached list; nodes go back to the allocator, the play
    // keys are NOT released (AttachPlayKey's AddRef is only undone by DetachPlayKey).
    if (AttachedCount != 0)
    {
        FAttachedNode* Attached = AttachedSentinel->Next;
        while (Attached != AttachedSentinel)
        {
            FAttachedNode* Next = Attached->Next;
            Attached->Prev->Next = Attached->Next;
            Attached->Next->Prev = Attached->Prev;
            free(Attached);
            --AttachedCount;
            Attached = Next;
        }
    }
    DestroyAllArchetypes(); // 06ee49fe FUN_06ee4800
    if ((CapsFlags & 4) != 0)
        SetEaxPreview(static_cast<int>(Field68), *reinterpret_cast<float*>(&Field74),
                      *reinterpret_cast<float*>(&Field70), *reinterpret_cast<float*>(&Field6C));
    if (Primary != nullptr)
    {
        Primary->Release();
        Primary = nullptr;
    }
    if (Listener != nullptr)
    {
        Listener->Release();
        Listener = nullptr;
    }
    if (DirectSound != nullptr)
    {
        DirectSound->Release();
        DirectSound = nullptr;
    }
    if (bCoInitialized != 0)
        CoUninitialize();
    bCoInitialized = 0;
    return GR_OK;
}

GENRESULT DACOM_API FSoundManager::GetDirectSound(IDirectSound** Out)
{
    // LAB_06ee4a90 RET 8. No check on Out: DS null -> *Out = 0, -1; else *Out = DS, AddRef, 0.
    if (DirectSound == nullptr)
    {
        *Out = nullptr;
        return GR_GENERIC;
    }
    *Out = DirectSound;
    DirectSound->AddRef();
    return GR_OK;
}

unsigned long DACOM_API FSoundManager::GetTickCountSlot()
{
    return GetTickCount();
}

GENRESULT FSoundManager::GetDeviceSnapshot(void* Out)
{
    // LAB_06ee4ad0 thiscall RET 0x4. No memset. Caps copies are dwFree* not dwMax*.
    unsigned char* Bytes = static_cast<unsigned char*>(Out);
    memcpy(Bytes + 4, &CapsFlags, 4);
    memcpy(Bytes + 8, &MaxChannels, 4);
    memcpy(Bytes + 12, &Field94, 4);
    if (DirectSound != nullptr)
    {
        // 06ee4afd..4b0d: only dwSize is set before GetCaps (+0x10), whose result is ignored.
        DSCAPS Caps;
        Caps.dwSize = 0x60;
        DirectSound->GetCaps(&Caps);
        memcpy(Bytes + 0x10, &Caps.dwFreeHw3DAllBuffers, 4);
        memcpy(Bytes + 0x14, &Caps.dwFreeHwMixingAllBuffers, 4);
        memcpy(Bytes + 0x18, &DeviceGuid, sizeof(GUID));
    }
    return GR_OK;
}

GENRESULT DACOM_API FSoundManager::MixerTick(ISoundListenerPose* ListenerPose)
{
    // LAB_06ee4b50 stdcall RET 8. Listing-complete: 6e20, prune, is_active/start-tick/
    // 6070, pose+range cull, 1040/60e0/5f00/7c00 pending, 7c70 splice, 64c0, Play+6b70.
    unsigned char bDirty = 0;
    Field4C = GetTickCount();
    if (ListenerPose != nullptr)
        ApplyListenerFromSource(ListenerPose);

    FSoundInstanceNode* Node = G_InstanceSentinel.Next;
    while (Node != &G_InstanceSentinel)
    {
        FSoundInstanceNode* Next = Node->Next;
        if (FindAttached(Node->PlayKey) == nullptr)
        {
            EnterCriticalSection(&G_InstanceLock);
            UnlinkInstance(Node);
            bDirty = 1;
            LeaveCriticalSection(&G_InstanceLock);
        }
        Node = Next;
    }

    unsigned long Created = 0;
    unsigned long AttachedIndex = 0;
    {
        // 06ee4c17: the attached head (+0x110) always exists; retail does not test it.
        FAttachedNode* Attached = AttachedSentinel->Next;
        while (Attached != AttachedSentinel)
        {
            ISoundSource* PlayKey = Attached->PlayKey;
            unsigned char bSkip = 0;
            if (Created >= MaxChannels)
                bSkip = 1;
            else if (PlayKey->IsActive() == 0)
                bSkip = 1;
            else if (OneShotAlreadyExpired(PlayKey) != 0)
                bSkip = 1;
            else if (Field4C < PlayKey->GetStartTick())
                bSkip = 1;

            if (bSkip != 0)
            {
                FSoundInstanceNode* Existing = FindInstanceByKey(PlayKey);
                if (Existing != nullptr)
                {
                    // 06ee505e..5153: the notice is emitted before LeaveCriticalSection.
                    EnterCriticalSection(&G_InstanceLock);
                    UnlinkInstance(Existing);
                    bDirty = 1;
                    if (Created >= MaxChannels)
                        NoticeArch(0x980,
                                   "Had to stop instance of arch %d because max channels was exceeded.\n",
                                   PlayKey);
                    else
                        NoticeArch(0x97C,
                                   "Had to stop instance of arch %d because it is no longer playing.\n",
                                   PlayKey);
                    LeaveCriticalSection(&G_InstanceLock);
                }
                else if (Created >= MaxChannels)
                    NoticeArch(0x98C,
                               "Ignoring instance of arch %d because max channels was exceeded.\n",
                               PlayKey);
                else
                    NoticeArch(0x988,
                               "Ignoring instance of arch %d because it is no longer playing.\n",
                               PlayKey);
            }
            else
            {
                FSoundInstanceNode* Existing = FindInstanceByKey(PlayKey);
                if (Existing != nullptr)
                {
                    // 06ee4dcd: cull unless DistSq < MaxDistSq (TEST AH,5 + JP: equal, greater
                    // and NaN all jump to the cull at 06ee4e12).
                    RefreshInstancePose(Existing);
                    if (PlayKey->Is3d() != 0 && (Existing->DsCapsFlags & 0x20000) != 0 &&
                        !(Existing->DistSq < Existing->MaxDistSq))
                    {
                        EnterCriticalSection(&G_InstanceLock);
                        UnlinkInstance(Existing);
                        bDirty = 1;
                        LeaveCriticalSection(&G_InstanceLock);
                    }
                    else
                    {
                        ++Created;
                        Existing->CapsFlags = AttachedIndex;
                        if (MaybeMigrateHwSw(this, Existing, AttachedIndex) != 0)
                        {
                            Existing->Buffer->Stop();
                            Existing->StatusFlags &= ~0x40u;
                        }
                    }
                }
                else if (MixerAllowCreate(PlayKey) != 0)
                {
                    // 06ee4f57..4fb0: on success Created++, payload+0x18 = attached index,
                    // FUN_06ee5f00 (result unused), then the pending-list insert.
                    FSoundInstanceNode* CreatedNode = CreateInstanceFromKey(PlayKey);
                    if (CreatedNode != nullptr)
                    {
                        ++Created;
                        CreatedNode->CapsFlags = AttachedIndex;
                        MaybeMigrateHwSw(this, CreatedNode, AttachedIndex);
                        LinkPendingInstance(CreatedNode);
                    }
                }
            }
            ++AttachedIndex;
            Attached = Attached->Next;
        }
    }

    // 06ee51ba: pending count -> lock, dirty = 1 (before the re-check), splice if still non-zero.
    if (InstanceEmbeddedCount != 0)
    {
        EnterCriticalSection(&G_InstanceLock);
        bDirty = 1;
        if (InstanceEmbeddedCount != 0)
            SplicePendingInstances();
        LeaveCriticalSection(&G_InstanceLock);
    }

    Node = G_InstanceSentinel.Next;
    while (Node != &G_InstanceSentinel)
    {
        MixerApplyInstance(Node);
        Node = Node->Next;
    }

    if (Listener != nullptr)
        Listener->CommitDeferredSettings();

    // 06ee5251..54b9: play pass. IsLooping (+0x1C) is asked before FUN_06ee6b70; no null
    // checks on the key or the buffer.
    Node = G_InstanceSentinel.Next;
    while (Node != &G_InstanceSentinel)
    {
        FSoundInstanceNode* Next = Node->Next;
        const unsigned char bLooping = Node->PlayKey->IsLooping();
        const DWORD PlayFlags = bLooping != 0 ? DSBPLAY_LOOPING : 0;
        if (MixerSetStartOffset(Node) == 0)
        {
            Node = Next;
            continue;
        }
        if (PlayFlags != 0 && (Node->DsCapsFlags & 0x100) != 0)
            SetupInstanceNotify(Node);
        unsigned long LocFlags = 0;
        if ((Node->DsCapsFlags & 0x40000) != 0)
        {
            if ((Node->StatusFlags & 8) != 0)
                LocFlags = DSBPLAY_LOCHARDWARE;
            else
                LocFlags = DSBPLAY_LOCSOFTWARE;
        }
        HRESULT Hr = Node->Buffer->Play(0, 0, PlayFlags | LocFlags);
        if (Hr < 0)
        {
            char Line[0x2000];
            if (Hr == static_cast<HRESULT>(0x88780096))
                Fdump(kFdumpError, 0xA17, "Sound buffer was lost!");
            if (LocFlags == DSBPLAY_LOCHARDWARE)
            {
                if (G_DisplayExtraDebug != 0)
                    Fdump(kFdumpNotice, 0xA2C,
                          FormatMessage2000(
                              Line, "Trying to restart instance of arch %d in SW because HW not available.\n",
                              Node->PlayKey->GetArchId()));
                // 06ee5367: EBP becomes 4, so a successful retry is not marked HW below.
                LocFlags = DSBPLAY_LOCSOFTWARE;
                Hr = Node->Buffer->Play(0, 0, PlayFlags | LocFlags);
                if (Hr >= 0)
                {
                    Node->StatusFlags = (Node->StatusFlags & ~1u) | LocFlags;
                    if (G_DisplayExtraDebug != 0)
                        Fdump(kFdumpNotice, 0xA3D, "SUCCEEDED!\n");
                }
                else if (G_DisplayExtraDebug != 0)
                    Fdump(kFdumpNotice, 0xA43, "FAILED!\n");
            }
            else
            {
                if (G_DisplayExtraDebug != 0)
                    Fdump(kFdumpNotice, 0xA4D,
                          FormatMessage2000(
                              Line, "Trying to restart instance of arch %d in for some unknown reason.\n",
                              Node->PlayKey->GetArchId()));
                Hr = Node->Buffer->Play(0, 0, PlayFlags);
                if (Hr >= 0)
                {
                    Node->StatusFlags = (Node->StatusFlags & ~1u) | 4u;
                    if (G_DisplayExtraDebug != 0)
                        Fdump(kFdumpNotice, 0xA5D, "SUCCEEDED!\n");
                }
                else if (G_DisplayExtraDebug != 0)
                    Fdump(kFdumpNotice, 0xA62, "FAILED!\n");
            }
        }
        if (Hr >= 0)
        {
            Node->StatusFlags |= 0x40;
            if (LocFlags == DSBPLAY_LOCHARDWARE)
                Node->StatusFlags |= 1;
        }
        Node = Next;
    }

    if (bDirty != 0)
        SetEvent(G_MixerEvent); // 06ee54c9..54d1

    return GR_OK;
}

GENRESULT DACOM_API FSoundManager::StopInstanceBuffer(ISoundSource* PlayKey)
{
    FSoundInstanceNode* Node = FindInstanceByKey(PlayKey);
    if (Node == nullptr || Node->Buffer == nullptr)
        return GR_OK;
    Node->Buffer->Stop();
    return GR_OK;
}

GENRESULT DACOM_API FSoundManager::PlayInstanceBuffer(ISoundSource* PlayKey)
{
    // LAB_06ee6f70: match node+8 == key; buffer (+0xC) null -> 0; IsLooping on the key.
    FSoundInstanceNode* Node = FindInstanceByKey(PlayKey);
    if (Node == nullptr || Node->Buffer == nullptr)
        return GR_OK;
    const unsigned char bLooping = PlayKey->IsLooping();
    const DWORD PlayFlags = bLooping != 0 ? DSBPLAY_LOOPING : 0;
    Node->Buffer->Play(0, 0, PlayFlags);
    return GR_OK;
}

GENRESULT DACOM_API FSoundManager::GetArchetypeFormat(void* Id, void* Out)
{
    // LAB_06ee5530: miss EAX=0; hit GetFormat then return Release's EAX.
    FSoundArchetype* Archetype = LookupArchetype(static_cast<int>(reinterpret_cast<intptr_t>(Id)));
    if (Archetype == nullptr)
        return GR_OK;
    Archetype->GetFormat(Out);
    return static_cast<GENRESULT>(Archetype->Release());
}

GENRESULT DACOM_API FSoundManager::GetArchetypeSamples(void* Id, void* Out)
{
    // LAB_06ee5570: miss returns 0, no FDUMP. Hit returns GetSamples EAX after Release.
    FSoundArchetype* Archetype = LookupArchetype(static_cast<int>(reinterpret_cast<intptr_t>(Id)));
    if (Archetype == nullptr)
        return GR_OK;
    const GENRESULT Result = Archetype->GetSamples(Out);
    Archetype->Release();
    return Result;
}

unsigned long DACOM_API FSoundManager::GetArchetypeFrames(void* Id)
{
    FSoundArchetype* Archetype = LookupArchetype(static_cast<int>(reinterpret_cast<intptr_t>(Id)));
    if (Archetype == nullptr)
        return 0;
    const unsigned long Frames = Archetype->GetNumFrames();
    Archetype->Release();
    return Frames;
}

float DACOM_API FSoundManager::GetArchetypeGain(void* Id)
{
    // LAB_06ee55f0: FLD arch+0x18 after Release; miss FLD 0.0.
    FSoundArchetype* Archetype = LookupArchetype(static_cast<int>(reinterpret_cast<intptr_t>(Id)));
    if (Archetype == nullptr)
        return 0.0f;
    const float Value = Archetype->Gain;
    Archetype->Release();
    return Value;
}

unsigned long DACOM_API FSoundManager::GetArchetypeChannels(void* Id)
{
    // LAB_06ee5630: MOVZX arch+4, then Release.
    FSoundArchetype* Archetype = LookupArchetype(static_cast<int>(reinterpret_cast<intptr_t>(Id)));
    if (Archetype == nullptr)
        return 0;
    const unsigned long Count = Archetype->nChannels;
    Archetype->Release();
    return Count;
}

GENRESULT DACOM_API FSoundManager::SetArchetypeSamples(void* /*A*/, void* /*B*/, void* /*C*/)
{
    // LAB_06ee5670 RET 0x10: FDUMP only, no lookup.
    Fdump(kFdumpError, 0xAF5, "SoundManager: set_samples() not supported.");
    return GR_OK;
}

unsigned long DACOM_API FSoundManager::GetArchetypeReady(void* Id)
{
    // LAB_06ee56a0: arch vtbl+0x2C, miss AL=0.
    FSoundArchetype* Archetype = LookupArchetype(static_cast<int>(reinterpret_cast<intptr_t>(Id)));
    if (Archetype == nullptr)
        return 0;
    const unsigned long Ready = Archetype->IsReady();
    Archetype->Release();
    return Ready;
}

GENRESULT DACOM_API FSoundManager::GetInstanceStatus(int Index, void* Out)
{
    // LAB_06ee3620 RET 0xC: Index in [0, DAT_06eeaf08); walk sentinel→Next Index times;
    // Out+0=node+8, Out+4=node+0x24, Out+8=node+0x28. No Out-null check.
    if (Index < 0 || static_cast<unsigned long>(Index) >= G_InstanceCount)
        return GR_GENERIC;
    FSoundInstanceNode* Node = G_InstanceSentinel.Next;
    int Remain = Index;
    while (Remain != 0)
    {
        --Remain;
        Node = Node->Next;
    }
    unsigned long* Dwords = static_cast<unsigned long*>(Out);
    Dwords[0] = reinterpret_cast<unsigned long>(Node->PlayKey);
    Dwords[1] = Node->StatusFlags;
    Dwords[2] = Node->DsCapsFlags;
    return GR_OK;
}

BOOL WINAPI DllMain(HINSTANCE Instance, DWORD Reason, LPVOID /*Reserved*/)
{
    if (Reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(Instance);
        // FUN_06ee71f0: DACOM_Acquire is called once for the null test and again for the
        // RegisterComponent call (06ee720e / 06ee7267). Literals 06eead5c / 06eeace8 / 06eead24.
        if (DACOM_Acquire() == nullptr)
        {
            Fdump(kFdumpWarning, 0x10B4, "SoundManager: DllMain: unable to get DACOM!\n");
            return TRUE;
        }
        FSoundManagerFactory* Factory = new (std::nothrow) FSoundManagerFactory();
        if (Factory == nullptr)
        {
            Fdump(kFdumpWarning, 0x10C0, "SoundManager: DllMain: unable to create component factory!\n");
            return TRUE;
        }
        const GENRESULT Result =
            DACOM_Acquire()->RegisterComponent(Factory, kComponentName,
                                               reinterpret_cast<void*>(kRegisterPriority));
        if (Result != GR_OK)
            Fdump(kFdumpWarning, 0x10BA, "SoundManager: DllMain: unable to register component!\n");
        Factory->Release();
    }
    return TRUE;
}
