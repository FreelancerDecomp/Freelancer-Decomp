// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "readfile.h"

#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <io.h>
#include <list>
#include <map>
#include <new>
#include <vector>

// DacomStricmp.cpp: DACOM.DLL!stricmp, the import the retail UTF walker lookups call.
int CallDacomStricmp(const char* Left, const char* Right);

namespace
{

    const unsigned long kGenericRead = 0x80000000u;
    const unsigned long kGenericWrite = 0x40000000u;
    const unsigned long kOpenExisting = 3;

    class FDosFileSystem;
    FDosFileSystem* G_DosFactory = nullptr; // DAT_06b7a224
    CRITICAL_SECTION* G_DosRootLock = nullptr;
    ICOManager* G_Dacom = nullptr;          // DAT_06b7a220

    int InvokeDosRootLockedCall(unsigned long Arg0, unsigned long Callback, unsigned long Arg1)
    {
        // FUN_06b76410: lock DAT_06b7a224+0x154, stdcall Callback(Arg0, Arg1)
        // unconditionally, unlock, return its result.
        typedef int(__stdcall* FLockedCall)(unsigned long, unsigned long);
        EnterCriticalSection(G_DosRootLock);
        const int Result = ((FLockedCall)(Callback))(Arg0, Arg1);
        LeaveCriticalSection(G_DosRootLock);
        return Result;
    }

    void CopyPath(char* Dest, unsigned long DestChars, const char* Source)
    {
        if (Dest == nullptr || DestChars == 0)
            return;
        Dest[0] = '\0';
        if (Source == nullptr)
            return;
        std::strncpy(Dest, Source, DestChars - 1);
        Dest[DestChars - 1] = '\0';
    }

    int CopyClampedPathSuffix(char* Out, unsigned long MaxChars, const char* Source)
    {
        unsigned long Count = (unsigned long)(std::strlen(Source));
        if (Count <= 2)
            Count = 2;
        if (Count >= MaxChars)
            Count = MaxChars;
        if (Count == 0)
            return 0;
        std::memcpy(Out, Source, Count);
        Out[Count - 1] = '\0';
        return (int)(Count - 1);
    }

    // Retail DOS directory CreateInstance (LAB_06b746bf): cache strlen, drop one
    // trailing slash, write '\' there.
    unsigned long SealDirectoryPathPrefix(char* Path, unsigned long PathChars)
    {
        if (Path == nullptr || PathChars < 2)
            return 0;
        unsigned long Len = (unsigned long)(std::strlen(Path));
        if (Len + 1 >= PathChars)
            Len = PathChars - 2;
        if (Len > 0 && (Path[Len - 1] == '\\' || Path[Len - 1] == '/'))
            Len = Len - 1;
        Path[Len] = '\\';
        Path[Len + 1] = '\0';
        return Len;
    }

    bool IsJoinSeparator(char Value)
    {
        return Value == '\\' || Value == '/';
    }

    int JoinRoot(const char* Root, const char* Rel, char* Out, long OutChars)
    {
        // 06b75427: CMP EBP,2 / JG.
        if (OutChars <= 2)
            return 0;

        // 06b75434..06b75469: absolute (leading separator or drive colon) -> strncpy.
        if (Rel[0] != '\0' && (IsJoinSeparator(Rel[0]) || Rel[1] == ':'))
        {
            std::strncpy(Out, Rel, (size_t)(OutChars));
            Out[OutChars - 1] = '\0';
            return 1;
        }

        // 06b7546b..06b75490: copy Root, failing once OutChars - 1 characters are used.
        long Length = 0;
        char Value = Root[0];
        while (Value != '\0')
        {
            if (Length >= OutChars - 1)
                return 0;
            Out[Length] = Value;
            Value = Root[Length + 1];
            Length = Length + 1;
        }

        // 06b75494..06b754ab: ensure one separator after the root (even for an empty root).
        char* Cursor = Out + Length;
        if (Cursor == Out || !IsJoinSeparator(Cursor[-1]))
        {
            *Cursor = '\\';
            Cursor = Cursor + 1;
        }

        // 06b754ac..06b75516: drop one leading ".\"; every leading ".." pops one directory.
        if (Rel[0] == '.')
        {
            if (IsJoinSeparator(Rel[1]))
                Rel = Rel + 2;
            while (Rel[0] == '.' && Rel[1] == '.')
            {
                if (Cursor - Out <= 2)
                    return 0;
                Cursor = Cursor - 2;
                while (!IsJoinSeparator(*Cursor))
                {
                    if (Cursor == Out)
                        return 0;
                    Cursor = Cursor - 1;
                }
                Rel = Rel + 2 + (IsJoinSeparator(Rel[2]) ? 1 : 0);
            }
        }

        // 06b75518..06b7552c: ensure the separator again (the pop lands ON one).
        if (Cursor == Out || !IsJoinSeparator(Cursor[-1]))
        {
            *Cursor = '\\';
            Cursor = Cursor + 1;
        }

        // 06b7552d..06b7555f: strncpy the tail into what is left, or terminate here.
        const long Remaining = (long)((Out + OutChars) - Cursor);
        if (Remaining > 1)
        {
            std::strncpy(Cursor, Rel, (size_t)(Remaining));
            Out[OutChars - 1] = '\0';
            return 1;
        }
        *Cursor = '\0';
        return 1;
    }

    // Signed-byte folding also reads the 128 bytes preceding the primary table.
    const unsigned char G_UtfFoldBelowTable[128] = {
        0x62, 0x75, 0x69, 0x6C, 0x64, 0x73, 0x5C, 0x64, 0x61, 0x6C, 0x69, 0x62, 0x73, 0x5C, 0x64, 0x61,
        0x6C, 0x69, 0x62, 0x73, 0x2D, 0x62, 0x75, 0x69, 0x6C, 0x64, 0x5C, 0x62, 0x75, 0x69, 0x6C, 0x64,
        0x5C, 0x53, 0x72, 0x63, 0x5C, 0x44, 0x4F, 0x53, 0x46, 0x69, 0x6C, 0x65, 0x5C, 0x52, 0x65, 0x61,
        0x64, 0x46, 0x69, 0x6C, 0x65, 0x5C, 0x42, 0x61, 0x73, 0x65, 0x55, 0x54, 0x46, 0x2E, 0x63, 0x70,
        0x70, 0x00, 0x00, 0x00, 0x61, 0x74, 0x74, 0x65, 0x6D, 0x70, 0x74, 0x69, 0x6E, 0x67, 0x20, 0x74,
        0x6F, 0x20, 0x63, 0x6C, 0x6F, 0x73, 0x65, 0x20, 0x69, 0x6E, 0x76, 0x61, 0x6C, 0x69, 0x64, 0x20,
        0x68, 0x61, 0x6E, 0x64, 0x6C, 0x65, 0x20, 0x28, 0x25, 0x73, 0x29, 0x00, 0x31, 0x2E, 0x31, 0x31,
        0x5F, 0x49, 0x46, 0x69, 0x6C, 0x65, 0x53, 0x79, 0x73, 0x74, 0x65, 0x6D, 0x00, 0x00, 0x00, 0x00,
    };

    char FoldUtfWildChar(char Value)
    {
        const unsigned char Byte = (unsigned char)(Value);
        if (Byte >= 0x80)
            return (char)(G_UtfFoldBelowTable[Byte - 0x80]);
        if (Byte >= 'A' && Byte <= 'Z')
            return (char)(Byte + ('a' - 'A'));
        if (Byte == '\\')
            return '/';
        return (char)(Byte);
    }

    // FUN_06b761a0 (fastcall Name, Pattern) — recursive '*' / '?' matcher (UTF Find, not DOS
    // FindFirst). No argument checks.
    int MatchUtfStarQuestion(const char* Name, const char* Pattern)
    {
        char PatternChar = *Pattern;
        for (;;)
        {
            if (PatternChar == '*')
            {
                int Matched = MatchUtfStarQuestion(Name, Pattern + 1);
                if (Matched == 0 && *Name != '\0')
                {
                    while (true)
                    {
                        Name = Name + 1;
                        Matched = MatchUtfStarQuestion(Name, Pattern + 1);
                        if (Matched != 0)
                            break;
                        if (*Name == '\0')
                            return 0;
                    }
                }
                return Matched;
            }
            if (PatternChar == '\0')
            {
                if (*Name == '\0')
                    return 1;
                if (FoldUtfWildChar(PatternChar) != FoldUtfWildChar(*Name))
                    return 0;
            }
            else if (PatternChar != '?')
            {
                if (FoldUtfWildChar(PatternChar) != FoldUtfWildChar(*Name))
                    return 0;
            }
            else if (*Name == '\0')
                return 0;
            PatternChar = Pattern[1];
            Pattern = Pattern + 1;
            Name = Name + 1;
        }
    }

    int MatchUtfWildcard(const char* Name, const char* Pattern)
    {
        char PreparedPattern[264];
        char PreparedName[264];
        const char* PatternDot = std::strchr(Pattern, '.');
        if (PatternDot == nullptr)
        {
            CopyPath(PreparedPattern, 264, Pattern);
            const unsigned long Len = (unsigned long)(std::strlen(PreparedPattern));
            if (Len + 2 < 264)
            {
                PreparedPattern[Len] = '.';
                PreparedPattern[Len + 1] = '*';
                PreparedPattern[Len + 2] = '\0';
            }
        }
        else if (PatternDot == Pattern)
        {
            PreparedPattern[0] = '*';
            CopyPath(PreparedPattern + 1, 263, Pattern);
        }
        else
            CopyPath(PreparedPattern, 264, Pattern);

        CopyPath(PreparedName, 264, Name);
        if (std::strchr(Name, '.') == nullptr)
        {
            const unsigned long Len = (unsigned long)(std::strlen(PreparedName));
            if (Len + 1 < 264)
            {
                PreparedName[Len] = '.';
                PreparedName[Len + 1] = '\0';
            }
        }
        return MatchUtfStarQuestion(PreparedName, PreparedPattern);
    }

    const unsigned long kUtfMagic = 0x20465455u; // "UTF "
    const unsigned long kUtfVersion = 0x101u;
    const unsigned long kUtfHeaderSize = 0x38;
    const unsigned long kUtfNode12 = 0x0C;
    const unsigned long kUtfNode2C = 0x2C;

    // On-disk UTF header (FUN_06b71190 / FUN_06b714f0 / LAB_06b71f50).
    // Only named fields have Ghidra evidence. Others stay unnamed.
    struct FUtfHeader
    {
        unsigned long Magic;          // +0x00  0x20465455
        unsigned long Version;        // +0x04  0x101
        unsigned long TreeOffset;     // +0x08
        unsigned long Unk0C;
        unsigned long Unk10;
        unsigned long NodeSize;       // +0x14  0x0C or 0x2C
        unsigned long StringOffset;   // +0x18
        unsigned long Unk1C;
        unsigned long Unk20;
        unsigned long DataOffset;     // +0x24  IDataBuffer+4 → view; Read uses view[+0x24]
        unsigned long Unk28;
        unsigned long Unk2C;
        unsigned long Unk30;
        unsigned long Unk34;
    };

    // The UTF archive map: the local at [ESP+0x14..0x24] of LAB_06b72990 and the object's
    // +4..+0x14 (FUN_06b71420 default {-1, 0, 0, 0, 0}).
    struct FUtfArchiveMap
    {
        HANDLE File;           // +0x00  -1 when none
        HANDLE Mapping;        // +0x04
        void* View;            // +0x08
        unsigned long Size;    // +0x0C
        unsigned long Pos;     // +0x10
    };

    // FUN_06b72dd0 (thiscall on &Map->Mapping): with a mapping, unmap the view (if any,
    // view and size -> 0) and close the mapping; without one, only view and size -> 0.
    void ReleaseArchiveMapping(FUtfArchiveMap* Map)
    {
        if (Map->Mapping != nullptr)
        {
            if (Map->View != nullptr)
            {
                UnmapViewOfFile(Map->View);
                Map->View = nullptr;
                Map->Size = 0;
            }
            CloseHandle(Map->Mapping);
            Map->Mapping = nullptr;
            return;
        }
        Map->View = nullptr;
        Map->Size = 0;
    }

    unsigned long ReadUtfDword(const unsigned char* Base, unsigned long Offset)
    {
        unsigned long Value = 0;
        std::memcpy(&Value, Base + Offset, sizeof(Value));
        return Value;
    }

    // Handle to an in-place archive record; a null Node marks a released handle.
    struct FUtfHandle
    {
        bool bNode2C;
        const unsigned char* Node;   // +0x04
        unsigned long Position;      // +0x08

        // Slot 1: 06b73e50 (byte [node+8] >> 4 & 1) / 06b73e90 (node[0] >> 31).
        bool IsDirectory() const
        {
            if (bNode2C)
                return ((Node[8] >> 4) & 1) != 0;
            return (ReadUtfDword(Node, 0) >> 31) != 0;
        }

        // Slot 2: 06b73e30 (node +0x1c) / 06b73e70 (node +8).
        unsigned long Size() const
        {
            return ReadUtfDword(Node, bNode2C ? 0x1Cu : 0x08u);
        }

        // Slot 3: 06b73e40 (node +0x10) / 06b73e80 (node +4).
        unsigned long DataOffset() const
        {
            return ReadUtfDword(Node, bNode2C ? 0x10u : 0x04u);
        }
    };

    // Find cursor, matched record, pattern and remaining record count.
    struct FUtfFindSlot
    {
        const unsigned char* Next;
        const unsigned char* Last;
        char Pattern[0x108];
        unsigned long Remaining;
    };

    // The first block is embedded; the walker owns and zero-initializes appended blocks.
    struct FUtfFindBlock
    {
        FUtfFindBlock* Next;
        FUtfFindSlot Slots[4];
    };

    static_assert(sizeof(FUtfFindSlot) == 0x114, "UTF find-slot extent");
    static_assert(sizeof(FUtfFindBlock) == 0x454, "UTF find-block extent");

    struct FDosOpenFile
    {
        HANDLE File;
        HANDLE Mapping;
        void* View;
        unsigned long Size;
        unsigned long Pos;
    };

    struct FDosMapToken
    {
        FDosOpenFile* Open;
    };

    // A packed UTF time (low word = DOS date, high word = DOS time) through
    // DosDateTimeToFileTime, as LAB_06b71e30 and LAB_06b71c70 do; the BOOL result is ignored.
    void UtfPackedToFileTime(unsigned long Packed, FILETIME* Out)
    {
        DosDateTimeToFileTime((WORD)(Packed), (WORD)(Packed >> 16), Out);
    }

    const char* FindUtfComponentEnd(const char* Cursor)
    {
        while (*Cursor != '\0')
        {
            if (*Cursor == '\\' || *Cursor == '/')
                return Cursor;
            Cursor = Cursor + 1;
        }
        return nullptr;
    }

    // Reads either archive record layout in place. Clones borrow their owner's walker.
    class FUtfWalker
    {
    public:
        explicit FUtfWalker(bool bNode2CIn)
            : bNode2C(bNode2CIn), View(nullptr), RootHandle(nullptr), Strings(nullptr),
              FreeCount(0), LastError(0)
        {
            std::memset(&FirstBlock, 0, sizeof(FirstBlock));
            std::memset(ArchiveTimes, 0, sizeof(ArchiveTimes));
        }

        // Destroy owned find blocks; the handle list releases its own nodes.
        ~FUtfWalker()
        {
            while (FirstBlock.Next != nullptr)
            {
                FUtfFindBlock* Block = FirstBlock.Next;
                FirstBlock.Next = Block->Next;
                delete Block;
            }
        }

        // Slot 0: LAB_06b714c0 ([+8]) / LAB_06b71f40 ([+0xc]).
        FUtfHandle* GetRootHandle() const
        {
            return RootHandle;
        }

        // Slot 1: LAB_06b714d0: the view (+4), i.e. the archive header.
        const unsigned char* GetView() const
        {
            return View;
        }

        // Slot 2: LAB_06b714e0: +0x474.
        unsigned long GetLastError() const
        {
            return LastError;
        }

        // formerly Init
        bool InitializeArchiveView(HANDLE File, const void* MapView, unsigned long MapSize)
        {
            if (!bNode2C &&
                ::GetFileTime(File, &ArchiveTimes[0], &ArchiveTimes[1], &ArchiveTimes[2]) == 0)
                return false;
            const unsigned char* Header = (const unsigned char*)(MapView);
            if (Header == nullptr || MapSize <= 0x38)
                return false;
            View = Header;
            if (ReadUtfDword(Header, 0x14) != (bNode2C ? 0x2Cu : 0x0Cu))
                return false;
            const unsigned long TreeOffset = ReadUtfDword(Header, 0x08);
            const unsigned long StringOffset = ReadUtfDword(Header, 0x18);
            if (TreeOffset >= MapSize || StringOffset >= MapSize)
                return false;
            const unsigned char* Tree = Header + TreeOffset;
            RootHandle = AllocHandle(Tree);
            Strings = (const char*)(Header + StringOffset);
            return true;
        }

        // Slot 6: LAB_06b71640 / LAB_06b720f0 (Path, Handle). Directory bit of the record the
        // path names (0x2C: byte [rec+8] & 0x10; 0x0C: bit 31 << 4), -1 when it is missing.
        int GetAttributes(const char* Path, unsigned long BaseValue) const
        {
            const unsigned char* Node = ResolveBase(BaseValue);
            if (Node == nullptr)
                return -1;
            Node = WalkPath(Node, Path);
            if (Node == nullptr)
                return -1;
            return IsDirectoryRecord(Node) ? 0x10 : 0;
        }

        // Slot 7: LAB_06b717a0: -1 -> false; true when Value is the address of a handle in the
        // list (released handles included).
        bool IsValidHandle(unsigned long Value) const
        {
            if (Value == 0xFFFFFFFFu)
                return false;
            std::list<FUtfHandle>::const_iterator It = Handles.begin();
            while (It != Handles.end())
            {
                if ((unsigned long)(&*It) == Value)
                    return true;
                ++It;
            }
            return false;
        }

        // Slot 8: LAB_06b71790: -1 -> 0, anything else is taken as a handle address as is.
        static FUtfHandle* HandleFromValue(unsigned long Value)
        {
            if (Value == 0xFFFFFFFFu)
                return nullptr;
            return (FUtfHandle*)(Value);
        }

        FUtfHandle* Open(const char* Path, unsigned long BaseValue, unsigned long Extra)
        {
            const unsigned char* Node = nullptr;
            if (Extra != 0xFFFFFFFFu)
            {
                const FUtfFindSlot* Slot = SlotFromId(Extra);
                if (Slot == nullptr || Slot->Next == nullptr)
                    return nullptr;
                Node = Slot->Last;
            }
            else
            {
                Node = ResolveBase(BaseValue);
                if (Node != nullptr)
                    Node = WalkPath(Node, Path);
            }
            if (Node == nullptr)
                return nullptr;
            return AllocHandle(Node);
        }

        // Slot 10: LAB_06b719b0: a handle's record = 0 and the free count + 1 (no list or
        // double-release test); always true.
        bool CloseHandle(FUtfHandle* Handle)
        {
            if (Handle != nullptr)
            {
                Handle->Node = nullptr;
                FreeCount = FreeCount + 1;
            }
            return true;
        }

        int FindFirst(const char* Pattern, WIN32_FIND_DATAA* Out, unsigned long BaseValue)
        {
            const unsigned char* Node = nullptr;
            if (BaseValue == 0)
                Node = RootHandle->Node;
            else if (BaseValue != 0xFFFFFFFFu)
                Node = ((const FUtfHandle*)(BaseValue))->Node;

            const char* LastSeparator = nullptr;
            const char* Scan = Pattern;
            while (*Scan != '\0')
            {
                if (IsJoinSeparator(*Scan))
                    LastSeparator = Scan;
                Scan = Scan + 1;
            }
            const char* Mask = Pattern;
            if (LastSeparator != nullptr)
            {
                Mask = LastSeparator + 1;
                if (LastSeparator != Pattern)
                {
                    // Named UB: retail copies the directory part without a bound into a
                    // 0x104-byte stack buffer; the port cuts it there.
                    char Directory[0x104];
                    size_t Length = (size_t)(LastSeparator - Pattern);
                    if (Length > sizeof(Directory) - 1)
                        Length = sizeof(Directory) - 1;
                    std::memcpy(Directory, Pattern, Length);
                    Directory[Length] = '\0';
                    if (Node == nullptr)
                        Node = RootHandle->Node;
                    if (Node != nullptr)
                        Node = WalkPath(Node, Directory);
                }
            }
            if (Node == nullptr || !IsDirectoryRecord(Node))
            {
                LastError = 0xA1;
                return -1;
            }

            const unsigned char* Root = RootHandle->Node;
            const unsigned char* First = nullptr;
            unsigned long Remaining = 0;
            if (bNode2C)
                First = Root + ReadUtfDword(Node, 0x10);
            else
            {
                // 06b725d4..06b725e3: ADD sets ZF on a zero sum -> -1, no LastError.
                if ((unsigned long)(Root) + ReadUtfDword(Node, 0x04) == 0)
                    return -1;
                First = Root + ReadUtfDword(Node, 0x04);
                Remaining = ReadUtfDword(Node, 0x08);
            }

            FUtfFindBlock* Block = &FirstBlock;
            unsigned long Index = 0;
            unsigned long SlotIndex = 0;
            for (;;)
            {
                SlotIndex = 0;
                while (SlotIndex < 4 && Block->Slots[SlotIndex].Next != nullptr)
                {
                    SlotIndex = SlotIndex + 1;
                    Index = Index + 1;
                }
                if (SlotIndex < 4)
                    break;
                if (Block->Next == nullptr)
                {
                    FUtfFindBlock* Added =
                        new (std::nothrow) FUtfFindBlock{};
                    Block->Next = Added;
                    if (Added == nullptr)
                    {
                        LastError = 0x54;
                        return -1;
                    }
                }
                Block = Block->Next;
            }

            FUtfFindSlot* Slot = &Block->Slots[SlotIndex];
            Slot->Next = First;
            Slot->Last = nullptr;
            std::strncpy(Slot->Pattern, Mask, 0x104);
            Slot->Remaining = Remaining;
            const unsigned long Id = Index + 1;
            if (FindNext(Id, Out) == 0)
            {
                FindClose(Id);
                return -1;
            }
            return (int)(Id);
        }

        int FindNext(unsigned long Id, WIN32_FIND_DATAA* Out)
        {
            FUtfFindSlot* Slot = SlotFromId(Id);
            if (Slot == nullptr || Slot->Next == nullptr)
            {
                LastError = 6;
                return 0;
            }
            if (Slot->Next == RootHandle->Node)
            {
                LastError = 0x12;
                return 0;
            }
            const char* Mask = Slot->Pattern;
            const bool bAll = Mask[0] == '*' &&
                              (Mask[1] == '\0' ||
                               ReadUtfDword((const unsigned char*)(Mask), 0) ==
                                   0x002A2E2Au);
            if (!bAll && MatchUtfWildcard(RecordName(Slot->Next), Mask) == 0)
            {
                for (;;)
                {
                    AdvanceSlot(Slot);
                    if (Slot->Next == RootHandle->Node)
                    {
                        LastError = 0x12;
                        return 0;
                    }
                    if (MatchUtfWildcard(RecordName(Slot->Next), Mask) != 0)
                        break;
                }
            }

            const unsigned char* Record = Slot->Next;
            if (bNode2C)
            {
                // 06b71d25..06b71d98: raw flags, the packed times, size +0x1c.
                Out->dwFileAttributes = ReadUtfDword(Record, 0x08);
                UtfPackedToFileTime(ReadUtfDword(Record, 0x20), &Out->ftCreationTime);
                UtfPackedToFileTime(ReadUtfDword(Record, 0x24), &Out->ftLastAccessTime);
                UtfPackedToFileTime(ReadUtfDword(Record, 0x28), &Out->ftLastWriteTime);
                Out->nFileSizeHigh = 0;
                Out->dwReserved0 = 0;
                Out->nFileSizeLow = ReadUtfDword(Record, 0x1C);
                Out->dwReserved1 = 0;
            }
            else
            {
                // 06b727a9..06b7280d: directory bit << 4, the archive's times, size +8 for
                // files and 0 for directories.
                const bool bDirectory = (ReadUtfDword(Record, 0) >> 31) != 0;
                Out->dwFileAttributes = bDirectory ? 0x10u : 0u;
                Out->ftCreationTime = ArchiveTimes[0];
                Out->ftLastAccessTime = ArchiveTimes[1];
                Out->ftLastWriteTime = ArchiveTimes[2];
                Out->nFileSizeHigh = 0;
                Out->nFileSizeLow = bDirectory ? 0u : ReadUtfDword(Record, 0x08);
                Out->dwReserved0 = 0;
                Out->dwReserved1 = 0;
            }
            // Named UB: retail strcpy's the name without a bound; the port stops at MAX_PATH.
            const char* Name = RecordName(Record);
            unsigned long Length = 0;
            while (Name[Length] != '\0' && Length < MAX_PATH - 1)
            {
                Out->cFileName[Length] = Name[Length];
                Length = Length + 1;
            }
            Out->cFileName[Length] = '\0';
            Out->cAlternateFileName[0] = '\0';
            Slot->Last = Record;
            AdvanceSlot(Slot);
            return 1;
        }

        // Slot 13: LAB_06b71dd0 (both walkers): no slot or a free one -> 6, 0; else free it, 1.
        int FindClose(unsigned long Id)
        {
            FUtfFindSlot* Slot = SlotFromId(Id);
            if (Slot == nullptr || Slot->Next == nullptr)
            {
                LastError = 6;
                return 0;
            }
            Slot->Next = nullptr;
            return 1;
        }

        bool GetFileTimes(unsigned long HandleValue, FILETIME* Creation, FILETIME* Access,
                          FILETIME* Write) const
        {
            if (HandleValue == 0)
                return false;
            if (!bNode2C)
            {
                if (Creation != nullptr)
                    *Creation = ArchiveTimes[0];
                if (Access != nullptr)
                    *Access = ArchiveTimes[1];
                if (Write != nullptr)
                    *Write = ArchiveTimes[2];
                return true;
            }
            if (HandleValue == 0xFFFFFFFFu)
                return false;
            const unsigned char* Node = ((const FUtfHandle*)(HandleValue))->Node;
            if (Node == nullptr)
                return false;
            if (Creation != nullptr)
                UtfPackedToFileTime(ReadUtfDword(Node, 0x20), Creation);
            if (Access != nullptr)
                UtfPackedToFileTime(ReadUtfDword(Node, 0x24), Access);
            if (Write != nullptr)
                UtfPackedToFileTime(ReadUtfDword(Node, 0x28), Write);
            return true;
        }

    private:
        bool IsDirectoryRecord(const unsigned char* Record) const
        {
            if (bNode2C)
                return (Record[8] & 0x10) != 0;
            return (ReadUtfDword(Record, 0) >> 31) != 0;
        }

        // 0x2C: string block + rec[+4]; 0x0C: string block + (rec[0] & 0x7fffffff) + 4.
        const char* RecordName(const unsigned char* Record) const
        {
            if (bNode2C)
                return Strings + ReadUtfDword(Record, 0x04);
            return Strings + (ReadUtfDword(Record, 0) & 0x7FFFFFFFu) + 4;
        }

        // The start record of slots 6 and 9: handle 0 and -1 mean the root record, a handle
        // whose record is null (released) falls back to the root record too.
        const unsigned char* ResolveBase(unsigned long BaseValue) const
        {
            const unsigned char* Node = nullptr;
            if (BaseValue != 0 && BaseValue != 0xFFFFFFFFu)
                Node = ((const FUtfHandle*)(BaseValue))->Node;
            if (Node == nullptr)
                Node = RootHandle->Node;
            return Node;
        }

        const unsigned char* FindChild(const char* Name, const unsigned char* Directory) const
        {
            const unsigned char* Root = RootHandle->Node;
            if (bNode2C)
            {
                const unsigned char* Record = Root + ReadUtfDword(Directory, 0x10);
                while (Record != Root)
                {
                    if (CallDacomStricmp(Name, RecordName(Record)) == 0)
                        return Record;
                    Record = RootHandle->Node + ReadUtfDword(Record, 0);
                }
                return nullptr;
            }
            unsigned long Offset = ReadUtfDword(Directory, 0x04);
            const unsigned long End = Offset + ReadUtfDword(Directory, 0x08) * 0x0C;
            while (Offset < End)
            {
                const unsigned char* Record = RootHandle->Node + Offset;
                if (CallDacomStricmp(Name, RecordName(Record)) == 0)
                    return Record;
                Offset = Offset + 0x0C;
            }
            return nullptr;
        }

        const unsigned char* WalkPath(const unsigned char* Node, const char* Path) const
        {
            const char* Cursor = Path;
            if (*Cursor == '\\' || *Cursor == '/')
                Cursor = Cursor + 1;
            if (*Cursor == '\0')
                return Node;
            char Component[0x104];
            for (;;)
            {
                if (!IsDirectoryRecord(Node))
                    return nullptr;
                const char* End = FindUtfComponentEnd(Cursor);
                if (End == nullptr)
                    return FindChild(Cursor, Node);
                size_t Length = (size_t)(End - Cursor);
                if (Length > sizeof(Component) - 1)
                    Length = sizeof(Component) - 1;
                std::memcpy(Component, Cursor, Length);
                Component[Length] = '\0';
                Node = FindChild(Component, Node);
                if (Node == nullptr)
                    return nullptr;
                Cursor = End + 1;
                if (*Cursor == '\0')
                    return Node;
            }
        }

        FUtfHandle* AllocHandle(const unsigned char* Node)
        {
            if (FreeCount != 0)
            {
                std::list<FUtfHandle>::iterator It = Handles.begin();
                while (It != Handles.end())
                {
                    if (It->Node == nullptr)
                    {
                        FreeCount = FreeCount - 1;
                        It->Node = Node;
                        It->Position = 0;
                        return &*It;
                    }
                    ++It;
                }
            }
            FUtfHandle Handle;
            Handle.bNode2C = bNode2C;
            Handle.Node = Node;
            Handle.Position = 0;
            Handles.push_back(Handle);
            return &Handles.back();
        }

        // FUN_06b73ea0 and its inline copies: id - 1 (unsigned) walks one block per 4; a chain
        // that ends first gives null (id 0 runs off the chain).
        FUtfFindSlot* SlotFromId(unsigned long Id)
        {
            unsigned long Index = Id - 1;
            FUtfFindBlock* Block = &FirstBlock;
            while (Index >= 4)
            {
                Block = Block->Next;
                if (Block == nullptr)
                    return nullptr;
                Index = Index - 4;
            }
            return &Block->Slots[Index];
        }

        void AdvanceSlot(FUtfFindSlot* Slot) const
        {
            if (bNode2C)
            {
                Slot->Next = RootHandle->Node + ReadUtfDword(Slot->Next, 0);
                return;
            }
            if (Slot->Remaining > 1)
            {
                Slot->Remaining = Slot->Remaining - 1;
                Slot->Next = Slot->Next + 0x0C;
                return;
            }
            Slot->Next = RootHandle->Node;
        }

        bool bNode2C;
        const unsigned char* View;        // +0x04
        FUtfHandle* RootHandle;           // +0x08 (0x2C) / +0x0C (0x0C)
        const char* Strings;              // +0x0C (0x2C) / +0x08 (0x0C)
        std::list<FUtfHandle> Handles;    // +0x10 (list, count +0x18)
        unsigned long FreeCount;          // +0x1C
        FUtfFindBlock FirstBlock;         // +0x20
        unsigned long LastError;          // +0x474
        FILETIME ArchiveTimes[3];         // +0x478 / +0x480 / +0x488 (0x0C walker only)
    };

    class FNamedFactory : public IComponentFactory
    {
    public:
        explicit FNamedFactory(const char* RegisteredName)
            : Name(RegisteredName), RefCount(1)
        {
        }

        GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override
        {
            *Out = nullptr;
            if (std::strcmp(Iids::ComponentFactory, Iid) != 0)
                return GR_INTERFACE_UNSUPPORTED;
            IComponentFactory* Face = (IComponentFactory*)(this);
            *Out = Face;
            Face->AddRef();
            return GR_OK;
        }

        unsigned long DACOM_API AddRef() override
        {
            RefCount = RefCount + 1;
            return RefCount;
        }

        unsigned long DACOM_API Release() override
        {
            if (RefCount != 0)
                RefCount = RefCount - 1;
            const unsigned long Remaining = RefCount;
            if (Remaining == 0)
            {
                RefCount = 1;
                delete this;
                return 0;
            }
            return Remaining;
        }

        virtual ~FNamedFactory() {}

        const char* Name;
        unsigned long RefCount;
    };

    class FFileSystemBase : public IFileSystem
    {
    public:
        FFileSystemBase() : RefCount(1), NestedChild(nullptr), PathPrefix(0)
        {
            std::memset(RootPath, 0, sizeof(RootPath));
            FileHandle = INVALID_HANDLE_VALUE;
            MappingHandle = nullptr;
            MappedView = nullptr;
            MappedSize = 0;
            MappedPos = 0;
            LastError = 0;
            AccessFlags = 0;
            bFileInstance = 0;
            InitializeCriticalSection(&Lock);
        }

        virtual ~FFileSystemBase()
        {
            CloseMappedFile();
            DeleteCriticalSection(&Lock);
        }

        void AttachNestedChild(IFileSystem* Child)
        {
            if (NestedChild == Child)
                return;
            if (NestedChild != nullptr)
            {
                NestedChild->Release();
                NestedChild = nullptr;
            }
            NestedChild = Child;
            if (NestedChild != nullptr)
                NestedChild->AddRef();
        }

        unsigned long NestedHandle(unsigned long Handle) const
        {
            if (Handle != 0)
                return Handle;
            return (unsigned long)(FileHandle);
        }

        void CopyNestedLastError()
        {
            if (NestedChild != nullptr)
                LastError = (unsigned long)(NestedChild->GetFileSystemError());
            else
                LastError = GetLastError();
        }

        // LAB_06b74060 (UTF, table 06b79204) / LAB_06b770a0 (DOS, table 06b792c0): one-entry
        // {"1.11_IFileSystem", 0} table; hit -> AddRef + *Out = this; miss -> *Out = 0, -3.
        GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override
        {
            if (std::strcmp(Iids::FileSystem, Iid) != 0)
            {
                *Out = nullptr;
                return GR_INTERFACE_UNSUPPORTED;
            }
            IFileSystem* Face = (IFileSystem*)(this);
            Face->AddRef();
            *Out = Face;
            return GR_OK;
        }

        unsigned long DACOM_API AddRef() override
        {
            RefCount = RefCount + 1;
            return RefCount;
        }

        unsigned long DACOM_API Release() override
        {
            if (RefCount != 0)
                RefCount = RefCount - 1;
            const unsigned long Remaining = RefCount;
            if (Remaining == 0)
            {
                RefCount = 1;
                delete this;
                return 0;
            }
            return Remaining;
        }

        int DACOM_API CloseFile(unsigned long Handle) override
        {
            const unsigned long Current = (unsigned long)(FileHandle);
            if (Handle == 0 || Handle == Current)
            {
                LastError = 6;
                return 0;
            }
            if (NestedChild != nullptr)
            {
                const int Result = NestedChild->CloseFile(Handle);
                if (Result == 0)
                    LastError = (unsigned long)(NestedChild->GetFileSystemError());
                return Result;
            }
            if (DropMapToken(Handle) != 0)
                return 1;
            FDosOpenFile* Open = FindOpenFile(Handle);
            if (Open == nullptr)
            {
                LastError = 6;
                return 0;
            }
            CloseOneOpenFile(Handle);
            return 1;
        }

        int DACOM_API Read(unsigned long Handle, void* Buffer, unsigned long Size,
                           unsigned long* BytesRead, unsigned long Extra) override
        {
            if (NestedChild != nullptr)
            {
                const int Result =
                    NestedChild->Read(NestedHandle(Handle), Buffer, Size, BytesRead, Extra);
                if (Result == 0)
                    LastError = (unsigned long)(NestedChild->GetFileSystemError());
                return Result;
            }
            FDosOpenFile* Open = FindOpenFile(Handle);
            if (Open == nullptr)
                return (int)(Size);
            unsigned long Got = 0;
            if (Open->View != nullptr)
            {
                if (Open->Pos < Open->Size)
                {
                    Got = Size;
                    if (Got > Open->Size - Open->Pos)
                        Got = Open->Size - Open->Pos;
                    std::memcpy(Buffer, (const char*)(Open->View) + Open->Pos, Got);
                    Open->Pos = Open->Pos + Got;
                }
            }
            else
            {
                DWORD ReadCount = Size;
                if (ReadFile(Open->File, Buffer, Size, &ReadCount, nullptr) != 0)
                    Got = ReadCount;
            }
            *BytesRead = Got;
            if (Got != Size)
            {
                LastError = 0xFFFFFFFF;
                return 0;
            }
            return 1;
        }

        // DOS LAB_06b75e70 / UTF LAB_06b73d50: LastError=5, return 0.
        // formerly Slot18
        int DACOM_API Write(unsigned long, const void*, unsigned long, unsigned long*,
                             const void*) override
        {
            LastError = 5;
            return 0;
        }
        // DOS LAB_06b75e90 / UTF LAB_06b73d40: LastError=5, return 0.
        // formerly Slot1C
        int DACOM_API RejectOperation1C(unsigned long, unsigned long, unsigned long, unsigned long) override
        {
            LastError = 5;
            return 0;
        }
        // formerly Slot20
        int DACOM_API SeekFile(unsigned long Handle, unsigned long OffLow, unsigned long* OffHighOut,
                             unsigned long Origin) override
        {
            if (NestedChild != nullptr)
            {
                const int Result = NestedChild->SeekFile(NestedHandle(Handle), OffLow, OffHighOut, Origin);
                if (Result == -1)
                    LastError = (unsigned long)(NestedChild->GetFileSystemError());
                return Result;
            }
            unsigned long* HighPtr = OffHighOut;
            unsigned long High = 0;
            if (HighPtr != nullptr)
                High = *HighPtr;
            else if ((long)(OffLow) < 0)
                High = 0xFFFFFFFFu;
            unsigned long long Target = ((unsigned long long)(High) << 32) | OffLow;
            if (Origin == FILE_CURRENT)
            {
                unsigned long CurrentHigh = 0;
                const unsigned long CurrentLow = (unsigned long)(
                    GetFilePosition(Handle, &CurrentHigh));
                Target = Target + (((unsigned long long)(CurrentHigh) << 32) | CurrentLow);
            }
            else if (Origin == FILE_END)
            {
                // Retail reads the high dword from an uninitialised stack slot that only a
                // successful GetFileSize writes; the port starts it at 0.
                unsigned long SizeHigh = 0;
                const unsigned long SizeLow =
                    GetSize(Handle, &SizeHigh);
                Target = (((unsigned long long)(SizeHigh) << 32) | SizeLow) - Target;
            }
            const unsigned long NewLow = (unsigned long)(Target);
            const unsigned long NewHigh = (unsigned long)(Target >> 32);

            FDosOpenFile* Open = FindOpenFile(Handle);
            if (Open == nullptr || Open->View == nullptr)
            {
                LastError = 0xFFFFFFFF;
                return -1;
            }
            Open->Pos = NewLow;
            if (NewLow == 0xFFFFFFFFu)
            {
                LastError = 0xFFFFFFFF;
                return -1;
            }
            if (HighPtr != nullptr)
                *HighPtr = NewHigh;
            return (int)(NewLow);
        }
        // DOS LAB_06b75e50: LastError=5, return 0 (UTF and MEM override).
        // formerly Slot24
        int DACOM_API SetEndOfFile(unsigned long) override
        {
            LastError = 5;
            return 0;
        }

        unsigned long DACOM_API GetSize(unsigned long Handle, unsigned long* HighOut) override
        {
            unsigned long Result = 0;
            if (NestedChild != nullptr)
                Result = NestedChild->GetSize(NestedHandle(Handle), HighOut);
            else
                Result = GetFileSize((HANDLE)(NestedHandle(Handle)),
                                     HighOut);
            if (Result == 0xFFFFFFFFu)
                CopyNestedLastError();
            return Result;
        }

        // formerly Slot2C
        int DACOM_API ReturnOne2C(unsigned long, unsigned long, unsigned long, unsigned long,
                             unsigned long) override
        {
            LastError = 5;
            return 1;
        }
        // formerly Slot30
        int DACOM_API ReturnOne30(unsigned long, unsigned long, unsigned long, unsigned long,
                             unsigned long) override
        {
            LastError = 5;
            return 1;
        }
        // formerly Slot34
        int DACOM_API GetFileTimes(unsigned long Handle, FILETIME* CreationOut, FILETIME* AccessOut,
                             FILETIME* WriteOut) override
        {
            int Result = 0;
            if (NestedChild != nullptr)
                Result = NestedChild->GetFileTimes(NestedHandle(Handle), CreationOut, AccessOut, WriteOut);
            else
                Result = GetFileTime((HANDLE)(NestedHandle(Handle)),
                                     CreationOut,
                                     AccessOut,
                                     WriteOut);
            if (Result == 0)
                CopyNestedLastError();
            return Result;
        }
        // DOS LAB_06b75e90 / UTF LAB_06b73d40: LastError=5, return 0.
        // formerly Slot38
        int DACOM_API RejectOperation38(unsigned long, unsigned long, unsigned long, unsigned long) override
        {
            LastError = 5;
            return 0;
        }
        // formerly Slot3C
        int DACOM_API CreateMapping(unsigned long FileKey, unsigned long A2, unsigned long A3, unsigned long A4,
                             unsigned long A5, unsigned long A6) override
        {
            if (NestedChild != nullptr)
            {
                const int Result = NestedChild->CreateMapping(NestedHandle(FileKey), A2, A3, A4, A5, A6);
                if (Result == 0)
                    LastError = (unsigned long)(NestedChild->GetFileSystemError());
                return Result;
            }
            if ((AccessFlags & 0x40000000u) != 0)
            {
                LastError = 5;
                return 0;
            }
            FDosOpenFile* Open = FindOpenFile(FileKey);
            if (Open == nullptr)
            {
                LastError = 6;
                return 0;
            }
            FDosMapToken* Token = new FDosMapToken();
            Token->Open = Open;
            MapTokens.push_back(Token);
            return (int)(Token);
        }

        // formerly Slot40
        int DACOM_API MapFileView(unsigned long Token, unsigned long Access, unsigned long A3,
                             unsigned long OffLow, unsigned long ByteCount) override
        {
            if (NestedChild != nullptr)
            {
                const int Result = NestedChild->MapFileView(Token, Access, A3, OffLow, ByteCount);
                if (Result == 0)
                    CopyNestedLastError();
                return Result;
            }
            if (Access != FILE_MAP_READ)
            {
                LastError = 5;
                return 0;
            }
            FDosMapToken* Found = nullptr;
            const unsigned long Count = (unsigned long)(MapTokens.size());
            for (unsigned long Index = 0; Index < Count; ++Index)
            {
                if ((unsigned long)(MapTokens[Index]) == Token)
                {
                    Found = MapTokens[Index];
                    break;
                }
            }
            if (Found == nullptr)
            {
                LastError = 6;
                return -1;
            }
            FDosOpenFile* Open = Found->Open;
            if (Open == nullptr)
            {
                LastError = 6;
                return 0;
            }
            if (Open->View == nullptr)
            {
                LastError = 5;
                return 0;
            }
            if (ByteCount > Open->Size - OffLow)
            {
                LastError = 5;
                return 0;
            }
            return (int)((char*)(Open->View) + OffLow);
        }

        // DOS LAB_06b75350: always 1. Does not UnmapViewOfFile.
        // formerly Slot44
        int DACOM_API UnmapFileView(unsigned long) override
        {
            return 1;
        }
        // formerly Slot48
        int DACOM_API FindFirst(const char*, WIN32_FIND_DATAA*) override
        {
            return -1;
        }
        // formerly Slot4C
        int DACOM_API FindNext(unsigned long, WIN32_FIND_DATAA*) override
        {
            return 0;
        }
        // formerly Slot50
        int DACOM_API FindClose(unsigned long) override
        {
            return 0;
        }
        // DOS LAB_06b75ef0 / UTF LAB_06b73d20: LastError=5, return 0.
        // formerly Slot54
        int DACOM_API RejectOperation54(const char*, unsigned long) override
        {
            LastError = 5;
            return 0;
        }
        // DOS LAB_06b75e50 / UTF LAB_06b73d10: LastError=5, return 0.
        // formerly Slot58
        int DACOM_API RejectOperation58(unsigned long) override
        {
            LastError = 5;
            return 0;
        }

        int DACOM_API GetCurrentPath(unsigned long MaxChars, char* Out) override
        {
            if (bFileInstance != 0)
            {
                LastError = 5;
                return 0;
            }
            const int Copied = CopyClampedPathSuffix(Out, MaxChars, RootPath + PathPrefix);
            if (Copied != 0)
                return Copied;
            if (NestedChild != nullptr)
            {
                LastError = (unsigned long)(NestedChild->GetFileSystemError());
                return 0;
            }
            LastError = GetLastError();
            return 0;
        }

        int DACOM_API OpenNode(const char* Name) override
        {
            if (bFileInstance != 0)
            {
                LastError = 5;
                return 0;
            }
            char Resolved[0x105];
            if (JoinRoot(RootPath, Name, Resolved, 0x104) == 0)
            {
                LastError = 0xa1;
                return 0;
            }
            const DWORD Attr = GetFileAttributesA(Resolved);
            if (Attr == INVALID_FILE_ATTRIBUTES || (Attr & FILE_ATTRIBUTE_DIRECTORY) == 0)
            {
                LastError = 0xa1;
                return 0;
            }
            // 06b757fd..06b7583d: strlen + REP MOVS of len+1 bytes into +0xd, then the
            // separator check on +0xd[len-1] (len >= 1: a successful join always holds one).
            const size_t Len = std::strlen(Resolved);
            std::memcpy(RootPath, Resolved, Len + 1);
            if (!IsJoinSeparator(RootPath[Len - 1]))
            {
                RootPath[Len] = '\\';
                RootPath[Len + 1] = '\0';
            }
            return 1;
        }

        // DOS LAB_06b75e50 / UTF LAB_06b73d10: LastError=5, return 0.
        // formerly Slot64
        int DACOM_API RejectOperation64(unsigned long) override
        {
            LastError = 5;
            return 0;
        }
        // DOS LAB_06b75ed0 / UTF LAB_06b73d30: LastError=5, return 0.
        // formerly Slot68
        int DACOM_API RejectOperation68(unsigned long, unsigned long, unsigned long) override
        {
            LastError = 5;
            return 0;
        }
        // DOS LAB_06b75ef0 / UTF LAB_06b73d20: LastError=5, return 0.
        // formerly Slot6C
        int DACOM_API RejectOperation6C(unsigned long, unsigned long) override
        {
            LastError = 5;
            return 0;
        }
        // formerly Slot70
        int DACOM_API GetPathAttributes(const char* RelPath) override
        {
            const char* Name = RelPath;
            if (bFileInstance != 0 || Name == nullptr)
            {
                LastError = 5;
                return 0;
            }
            char Resolved[0x105];
            if (JoinRoot(RootPath, Name, Resolved, 0x104) == 0)
            {
                LastError = 0xa1;
                return 0;
            }
            const DWORD Attr = GetFileAttributesA(Resolved);
            if (Attr == INVALID_FILE_ATTRIBUTES)
                CopyNestedLastError();
            return (int)(Attr);
        }
        // DOS LAB_06b75ef0 / UTF LAB_06b73d20: LastError=5, return 0.
        // formerly Slot74
        int DACOM_API RejectOperation74(unsigned long, unsigned long) override
        {
            LastError = 5;
            return 0;
        }
        // formerly Slot78
        int DACOM_API GetFileSystemError() override
        {
            return (int)(LastError);
        }
        int DACOM_API OpenByDesc(FFileSystemDesc* Desc) override
        {
            if (Desc == nullptr)
                return (int)((unsigned long)(FileHandle));
            if (bFileInstance != 0 || Desc->Path == nullptr)
            {
                LastError = 2;
                return -1;
            }

            char Resolved[0x105];
            if (JoinRoot(RootPath, Desc->Path, Resolved, 0x104) == 0)
            {
                LastError = 2;
                return -1;
            }
            const DWORD Attr = GetFileAttributesA(Resolved);
            if (Attr == INVALID_FILE_ATTRIBUTES || (Attr & FILE_ATTRIBUTE_DIRECTORY) != 0)
            {
                LastError = 2;
                return -1;
            }
            const HANDLE Opened = InsertWin32File(Resolved, Desc);
            if (Opened == INVALID_HANDLE_VALUE)
            {
                LastError = 2;
                return -1;
            }
            return (int)((unsigned long)(Opened));
        }
        // formerly Slot80
        int DACOM_API GetFilePosition(unsigned long Handle, unsigned long* HighOut) override
        {
            if (NestedChild != nullptr)
                return NestedChild->GetFilePosition(NestedHandle(Handle), HighOut);
            if (HighOut != 0)
                *HighOut = 0;
            FDosOpenFile* Open = FindOpenFile(Handle);
            if (Open == nullptr || Open->View == nullptr)
            {
                LastError = 0xFFFFFFFF;
                return -1;
            }
            return (int)(Open->Pos);
        }

        void DACOM_API GetFileSysPath(char* Out, unsigned long MaxChars) override
        {
            long Count = (long)(std::strlen(RootPath)) + 1;
            if ((long)(MaxChars) < Count)
                Count = (long)(MaxChars);
            if (Count <= 0 || Out == nullptr)
                return;
            std::memcpy(Out, RootPath, (size_t)(Count));
            if (PathPrefix != 0 && Count > (long)(PathPrefix))
            {
                Out[PathPrefix] = '\0';
                return;
            }
            Out[Count - 1] = '\0';
        }

        // formerly Slot88
        int DACOM_API GetAccessFlags() override
        {
            return (int)(AccessFlags);
        }
        // DOS LAB_06b75e20: *Out = +0x120 (no Out check), AddRef it when set, return 0.
        // formerly Slot8C
        int DACOM_API GetParentFileSystem(IFileSystem** Out) override
        {
            *Out = NestedChild;
            if (NestedChild != nullptr)
                NestedChild->AddRef();
            return 0;
        }
        // formerly Slot90
        int DACOM_API FailOperation90(unsigned long, unsigned long) override
        {
            // Retail LAB_06b776a0: `OR EAX,0xffffffff; RET 0xc` — always fails.
            return (int)(0xFFFFFFFF);
        }
        // formerly Slot94
        int DACOM_API FailOperation94(unsigned long, unsigned long) override
        {
            // Retail LAB_06b776a0 (shared with Slot90): always fails.
            return (int)(0xFFFFFFFF);
        }
        // formerly Slot98
        int DACOM_API FailOperation98(unsigned long, unsigned long, unsigned long, unsigned long,
                             unsigned long) override
        {
            // Retail LAB_06b776b0: `OR EAX,0xffffffff; RET 0x18` — always fails.
            return (int)(0xFFFFFFFF);
        }
        // formerly Slot9C
        int DACOM_API FailOperation9C(unsigned long, unsigned long, unsigned long, unsigned long,
                             unsigned long) override
        {
            // Retail LAB_06b776b0 (shared with Slot98): always fails.
            return (int)(0xFFFFFFFF);
        }
        // formerly SlotA0
        int DACOM_API InvokeLockedCallback(unsigned long Arg0, unsigned long Callback, unsigned long Arg1) override
        {
            if (NestedChild != nullptr)
                return NestedChild->InvokeLockedCallback(Arg0, Callback, Arg1);
            typedef int(__stdcall* FLockedCall)(unsigned long, unsigned long);
            EnterCriticalSection(&Lock);
            const int Result = ((FLockedCall)(Callback))(Arg0, Arg1);
            LeaveCriticalSection(&Lock);
            return Result;
        }
        // formerly SlotA4
        int DACOM_API ResolvePath(char* Dest, const char* Src, unsigned long DestChars) override
        {
            return JoinRoot(RootPath + PathPrefix, Src,
                            Dest, (long)(DestChars));
        }

        void CloseMappedFile()
        {
            if (NestedChild != nullptr)
            {
                NestedChild->CloseFile(NestedHandle(0));
                NestedChild->Release();
                NestedChild->Release();
                NestedChild = nullptr;
            }
            else
            {
                if ((const void*)(this) == (const void*)(G_DosFactory))
                {
                    G_DosRootLock = nullptr;
                    G_DosFactory = nullptr;
                }
                CloseAllOpenFiles();
            }
            FileHandle = INVALID_HANDLE_VALUE;
            MappingHandle = nullptr;
            MappedView = nullptr;
            MappedSize = 0;
            MappedPos = 0;
        }

        HANDLE InsertWin32File(const char* Path, const FFileSystemDesc* Desc)
        {
            const HANDLE File = CreateFileA(Path, Desc->Access, Desc->ShareMode,
                                            (LPSECURITY_ATTRIBUTES)(Desc->Security),
                                            Desc->Creation, Desc->Flags,
                                            (HANDLE)(Desc->TemplateFile));
            if (File == INVALID_HANDLE_VALUE)
                return INVALID_HANDLE_VALUE;

            const unsigned long Key = (unsigned long)(File);
            if (OpenFiles.find(Key) != OpenFiles.end())
                return INVALID_HANDLE_VALUE;
            FDosOpenFile* Open = new FDosOpenFile();
            Open->File = File;
            Open->Mapping = nullptr;
            Open->View = nullptr;
            Open->Size = 0;
            Open->Pos = 0;
            OpenFiles[Key] = Open;
            if (File != nullptr)
            {
                Open->Mapping = CreateFileMappingA(File, nullptr, PAGE_READONLY, 0, 0, nullptr);
                if (Open->Mapping != nullptr)
                {
                    Open->Size = GetFileSize(File, nullptr);
                    Open->View = MapViewOfFile(Open->Mapping, FILE_MAP_READ, 0, 0, Open->Size);
                    Open->Pos = 0;
                }
            }
            return File;
        }

        bool OpenWin32File(const char* Path, const FFileSystemDesc* Desc)
        {
            const HANDLE Opened = InsertWin32File(Path, Desc);
            if (Opened == INVALID_HANDLE_VALUE)
                return false;
            AdoptCurrentFile(Opened);
            return true;
        }

        FDosOpenFile* FindOpenFile(unsigned long Handle)
        {
            unsigned long Key = Handle;
            if (Key == 0)
                Key = (unsigned long)(FileHandle);
            if (Key == 0 || Key == 0xFFFFFFFFu)
                return nullptr;
            std::map<unsigned long, FDosOpenFile*>::iterator It = OpenFiles.find(Key);
            if (It == OpenFiles.end())
                return nullptr;
            return It->second;
        }

        void AdoptCurrentFile(HANDLE Opened)
        {
            FileHandle = Opened;
            FDosOpenFile* Open = FindOpenFile((unsigned long)(Opened));
            if (Open == nullptr)
            {
                MappingHandle = nullptr;
                MappedView = nullptr;
                MappedSize = 0;
                MappedPos = 0;
                return;
            }
            SyncCurrentFromOpen(Open);
        }

        void SyncCurrentFromOpen(const FDosOpenFile* Open)
        {
            if (Open == nullptr || Open->File != FileHandle)
                return;
            MappingHandle = Open->Mapping;
            MappedView = Open->View;
            MappedSize = Open->Size;
            MappedPos = Open->Pos;
        }

        int DropMapToken(unsigned long Token)
        {
            const unsigned long Count = (unsigned long)(MapTokens.size());
            for (unsigned long Index = 0; Index < Count; ++Index)
            {
                if ((unsigned long)(MapTokens[Index]) == Token)
                {
                    delete MapTokens[Index];
                    MapTokens.erase(MapTokens.begin() + (int)(Index));
                    return 1;
                }
            }
            return 0;
        }

        void ReleaseOpenFile(FDosOpenFile* Open)
        {
            if (Open == nullptr)
                return;
            if (Open->View != nullptr)
            {
                UnmapViewOfFile(Open->View);
                Open->View = nullptr;
            }
            Open->Size = 0;
            Open->Pos = 0;
            if (Open->Mapping != nullptr)
            {
                CloseHandle(Open->Mapping);
                Open->Mapping = nullptr;
            }
            if (Open->File != INVALID_HANDLE_VALUE)
            {
                CloseHandle(Open->File);
                Open->File = INVALID_HANDLE_VALUE;
            }
            delete Open;
        }

        void DetachTokensFromOpen(const FDosOpenFile* Open)
        {
            const unsigned long Count = (unsigned long)(MapTokens.size());
            for (unsigned long Index = 0; Index < Count; ++Index)
            {
                if (MapTokens[Index]->Open == Open)
                    MapTokens[Index]->Open = nullptr;
            }
        }

        // The hit path of FUN_06b74bf0 (06b74c87..06b74d36). CloseFile has already rejected
        // the current handle +0x130, so the entry is never the current file.
        void CloseOneOpenFile(unsigned long Handle)
        {
            std::map<unsigned long, FDosOpenFile*>::iterator It = OpenFiles.find(Handle);
            if (It == OpenFiles.end())
                return;
            FDosOpenFile* Open = It->second;
            DetachTokensFromOpen(Open);
            OpenFiles.erase(It);
            ReleaseOpenFile(Open);
        }

        void CloseAllOpenFiles()
        {
            const unsigned long TokenCount = (unsigned long)(MapTokens.size());
            for (unsigned long Index = 0; Index < TokenCount; ++Index)
                delete MapTokens[Index];
            MapTokens.clear();

            std::map<unsigned long, FDosOpenFile*>::iterator It = OpenFiles.begin();
            while (It != OpenFiles.end())
            {
                ReleaseOpenFile(It->second);
                It = OpenFiles.erase(It);
            }
        }

        unsigned long RefCount;
        char RootPath[264];
        IFileSystem* NestedChild;
        unsigned long PathPrefix;
        CRITICAL_SECTION Lock;
        HANDLE FileHandle;
        HANDLE MappingHandle;
        void* MappedView;
        unsigned long MappedSize;
        unsigned long MappedPos;
        unsigned long LastError;
        unsigned long AccessFlags;
        // Retail DOS +0x128: 1 once this instance is bound to one file (or to a nested
        // parent file); FDosFileSystem::CreateInstance refuses to run on such an instance.
        unsigned long bFileInstance;
        std::map<unsigned long, FDosOpenFile*> OpenFiles;
        std::vector<FDosMapToken*> MapTokens;
    };

    class FDosFileSystem final : public FFileSystemBase
    {
    public:
        // FUN_06b74b20 (DOS part): a fresh instance's root is "\".
        FDosFileSystem()
        {
            RootPath[0] = '\\';
            RootPath[1] = '\0';
        }

        // LAB_06b74470 (stdcall RET 0xC). Single exit: *Instance = Created, return Result
        // (06b749e3). Every early failure leaves Created = 0.
        GENRESULT DACOM_API CreateInstance(AGGDESC* Desc, void** Instance) override
        {
            FFileSystemDesc* FsDesc = (FFileSystemDesc*)(Desc);
            GENRESULT Result = GR_OK;
            void* Created = nullptr;

            // 06b74483..06b744ad: size 0x34 / 0x30, CMPSB 0xB bytes vs "FileSystem" (== strcmp).
            if ((FsDesc->Size != 0x34 && FsDesc->Size != 0x30) ||
                std::strcmp(FsDesc->ComponentName, "FileSystem") != 0)
                Result = GR_INTERFACE_UNSUPPORTED;
            // 06b744b3: only a null Path pointer is refused; "" resolves against the root.
            else if (FsDesc->Path == nullptr)
                Result = GR_GENERIC;
            else if (FsDesc->Access != kGenericRead || FsDesc->Creation != kOpenExisting)
                Result = GR_GENERIC;
            // 06b744dd: an instance already bound to a file cannot create.
            else if (bFileInstance != 0)
                Result = GR_GENERIC;
            else if (this == G_DosFactory)
                Result = CreateOnSingleton(FsDesc, &Created);
            else if (FsDesc->Parent != nullptr)
                Result = GR_GENERIC;
            else
                Result = CreateRelative(Desc, &Created);

            *Instance = Created;
            return Result;
        }

    private:
        void AdoptDirectoryRoot(const char* Path)
        {
            CopyPath(RootPath, 264, Path);
            PathPrefix = SealDirectoryPathPrefix(RootPath, 264);
        }

        GENRESULT CreateOnSingleton(FFileSystemDesc* FsDesc, void** Created)
        {
            FDosFileSystem* Child = new (std::nothrow) FDosFileSystem();
            *Created = (Child != nullptr) ? (IFileSystem*)(Child) : nullptr;

            if (FsDesc->Parent != nullptr)
            {
                Child->AttachNestedChild((IFileSystem*)(FsDesc->Parent));
                Child->FileHandle = (HANDLE)(FsDesc->Extra);
                Child->bFileInstance = 1;
                Child->AccessFlags = FsDesc->Access;
                std::strncpy(RootPath, FsDesc->Path, 0x108);
                PathPrefix = 0;
                return GR_OK;
            }

            const DWORD Attributes = GetFileAttributesA(FsDesc->Path);
            if (Attributes != INVALID_FILE_ATTRIBUTES)
            {
                if ((Attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
                {
                    Child->AccessFlags = FsDesc->Access;
                    Child->AdoptDirectoryRoot(FsDesc->Path);
                    return GR_OK;
                }
                if (Child->OpenWin32File(FsDesc->Path, FsDesc))
                {
                    std::strncpy(Child->RootPath, FsDesc->Path, 0x108);
                    Child->PathPrefix = 0;
                    Child->AccessFlags = FsDesc->Access;
                    Child->bFileInstance = 1;
                    return GR_OK;
                }
            }

            // 06b749a8: destroy the child, report GetLastError on THIS, -6.
            if (Child != nullptr)
                delete Child;
            *Created = nullptr;
            LastError = GetLastError();
            return (GENRESULT)(0xFFFFFFFA);
        }

        GENRESULT CreateRelative(AGGDESC* Desc, void** Created)
        {
            FFileSystemDesc* FsDesc = (FFileSystemDesc*)(Desc);
            char Resolved[0x105];
            if (JoinRoot(RootPath, FsDesc->Path, Resolved, 0x104) == 0)
                return GR_GENERIC;

            const DWORD Attributes = GetFileAttributesA(Resolved);
            if (Attributes == INVALID_FILE_ATTRIBUTES)
                return GR_GENERIC;

            if ((Attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
            {
                FDosFileSystem* Child = new (std::nothrow) FDosFileSystem();
                if (Child == nullptr)
                    return GR_GENERIC;
                Child->AdoptDirectoryRoot(Resolved);
                *Created = (IFileSystem*)(Child);
                return GR_OK;
            }

            // 06b7470a: CMPSB 4 bytes vs "DOS\0" (DAT_06b7a1d8) == case-sensitive strcmp.
            if (FsDesc->Implementation != nullptr && std::strcmp(FsDesc->Implementation, "DOS") == 0)
            {
                FDosFileSystem* Child = new (std::nothrow) FDosFileSystem();
                if (!Child->OpenWin32File(Resolved, FsDesc))
                {
                    delete Child;
                    LastError = GetLastError();
                    return (GENRESULT)(0xFFFFFFFA);
                }
                // 06b7477a: the root keeps Desc->Path as given, not the resolved path.
                std::strncpy(Child->RootPath, FsDesc->Path, 0x108);
                Child->PathPrefix = 0;
                Child->AccessFlags = FsDesc->Access;
                Child->bFileInstance = 1;
                *Created = (IFileSystem*)(Child);
                return GR_OK;
            }

            // 06b747e8..06b74803: not a "DOS" file — DACOM picks another "FileSystem"
            // factory with desc+0x28 = this; the field is cleared again afterwards.
            FsDesc->Parent = (IFileSystem*)(this);
            const GENRESULT Result = G_Dacom->CreateInstance(Desc, Created);
            FsDesc->Parent = nullptr;
            return Result;
        }

    // formerly Slot48
    public:
        // OpenNode (+0x60) is FFileSystemBase::OpenNode = LAB_06b756b0.

        int DACOM_API FindFirst(const char* Pattern, WIN32_FIND_DATAA* FindData) override
        {
            if (bFileInstance != 0)
            {
                LastError = 2;
                return -1;
            }
            char Resolved[0x105] = {};
            if (JoinRoot(RootPath, Pattern, Resolved, 0x104) != 0)
            {
                LastError = 0xa1;
                return -1;
            }
            const HANDLE Found =
                FindFirstFileA(Resolved, FindData);
            if (Found == nullptr)
            {
                CopyNestedLastError();
                return 0;
            }
            return (int)((unsigned long)(Found));
        }

        // formerly Slot4C
        int DACOM_API FindNext(unsigned long Handle, WIN32_FIND_DATAA* FindData) override
        {
            const BOOL Result = FindNextFileA((HANDLE)(Handle),
                                              FindData);
            if (Result == 0)
                CopyNestedLastError();
            return Result;
        }

        // formerly Slot50
        int DACOM_API FindClose(unsigned long Handle) override
        {
            const BOOL Result = ::FindClose((HANDLE)(Handle));
            if (Result == 0)
                CopyNestedLastError();
            return Result;
        }
    };

    class FUtfFileSystem final : public FFileSystemBase
    {
    public:
        FUtfFileSystem()
            : OwnPathPrefix(0), OpenNodeHandle(nullptr), ArchiveOwner(nullptr),
              ParentFileSystem(nullptr), Walker(nullptr), OwnRootHandle(nullptr)
        {
            std::memset(OwnPath, 0, sizeof(OwnPath));
            std::memset(CurrentPath, 0, sizeof(CurrentPath));
            OwnPath[0] = '\\';
            CurrentPath[0] = '\\';
        }

        ~FUtfFileSystem() override
        {
            if (OpenNodeHandle != nullptr)
            {
                ActiveWalker()->CloseHandle(OpenNodeHandle);
                OpenNodeHandle = nullptr;
            }
            if (ParentFileSystem != nullptr)
            {
                ParentFileSystem->Release();
                ParentFileSystem = nullptr;
            }
            if (ArchiveOwner != nullptr)
            {
                ArchiveOwner->Release();
                ArchiveOwner = nullptr;
            }
            if (Walker != nullptr)
            {
                delete Walker;
                Walker = nullptr;
            }
            if (MappingHandle != nullptr)
            {
                if (MappedView != nullptr)
                {
                    UnmapViewOfFile(MappedView);
                    MappedView = nullptr;
                    MappedSize = 0;
                }
                CloseHandle(MappingHandle);
                MappingHandle = nullptr;
            }
            else
            {
                MappedView = nullptr;
                MappedSize = 0;
            }
            if (FileHandle != INVALID_HANDLE_VALUE)
            {
                CloseHandle(FileHandle);
                FileHandle = INVALID_HANDLE_VALUE;
            }
            MapTokens.clear();
        }

        // The walker every walker-backed slot uses: +0x230's +0x238, else the own +0x238.
        FUtfWalker* ActiveWalker() const
        {
            if (ArchiveOwner != nullptr)
                return ArchiveOwner->Walker;
            return Walker;
        }

        // This object's root handle: the own walker's slot 0, else +0x23c (a clone's node).
        FUtfHandle* RootHandle() const
        {
            if (Walker != nullptr)
                return Walker->GetRootHandle();
            return OwnRootHandle;
        }

        GENRESULT DACOM_API CreateInstance(AGGDESC* Desc, void** Instance) override
        {
            *Instance = nullptr;
            FFileSystemDesc* FsDesc = (FFileSystemDesc*)(Desc);
            if (FsDesc == nullptr || FsDesc->ComponentName == nullptr)
                return GR_GENERIC;
            if (FsDesc->Size != 0x34 && FsDesc->Size != 0x30)
                return GR_INTERFACE_UNSUPPORTED;
            if (std::strcmp(FsDesc->ComponentName, "FileSystem") != 0)
                return GR_INTERFACE_UNSUPPORTED;
            if (FsDesc->Implementation != nullptr && std::strcmp(FsDesc->Implementation, "UTF") != 0)
                return GR_GENERIC;
            if (FsDesc->Access != kGenericRead || FsDesc->Creation != kOpenExisting)
                return GR_GENERIC;

            // 06b72a37..06b72d92: one exit stores *Out = Created and Releases the
            // IDataBuffer the parent may have handed out.
            GENRESULT Result = GR_OK;
            void* Created = nullptr;
            IDataBuffer* DataBuffer = nullptr;
            const char* OpenPath = FsDesc->Path;
            // 06b72a4c / 06b72ad5: the root test is on the own path +0x20 == "\" (the
            // registered factory; an archive or a clone holds the Desc->Path it was made from).
            const bool bAtRoot = OwnPath[0] == '\\' && OwnPath[1] == '\0';
            // Named UB: _fullpath's result is not checked; on failure retail hands
            // uninitialised stack bytes to _access/CreateFileA. Zero-filled here.
            char FullPath[0x104] = {};

            if (FsDesc->Parent != nullptr)
            {
                IFileSystem* ParentFs = (IFileSystem*)(FsDesc->Parent);
                bool bBuildPath = true;
                if (bAtRoot &&
                    ParentFs->QueryInterface(Iids::DataBuffer,
                                             (void**)(&DataBuffer)) == GR_OK)
                    bBuildPath = false;
                if (bBuildPath)
                {
                    char DirPath[0x104] = {};
                    char MadePath[0x104] = {};
                    ParentFs->GetFileSysPath(DirPath, 0x104);
                    _makepath(MadePath, nullptr, DirPath, FsDesc->Path, nullptr);
                    _fullpath(FullPath, MadePath, 0x104);
                    if (_access(FullPath, 0) == -1)
                        Result = GR_GENERIC;
                    else
                        OpenPath = FullPath;
                }
            }

            if (Result == GR_OK)
            {
                if (bAtRoot)
                    Result = OpenArchiveFromDesc(FsDesc, OpenPath, DataBuffer, &Created);
                else
                    Result = CloneAtNode(FsDesc, &Created);
            }

            *Instance = Created;
            if (DataBuffer != nullptr)
                DataBuffer->Release();
            return Result;
        }

    private:
        void SetOwnPath(const char* Path, FUtfHandle* Handle)
        {
            size_t Length = std::strlen(Path);
            if (Length > sizeof(OwnPath) - 2)
                Length = sizeof(OwnPath) - 2;
            OwnPathPrefix = (unsigned long)(Length);
            std::memcpy(OwnPath, Path, Length);
            OwnPath[Length] = '\0';
            if (Handle == nullptr)
                return;
            OwnRootHandle = Handle;
            if (!Handle->IsDirectory())
                return;
            if ((long)(OwnPathPrefix) > 0 &&
                IsJoinSeparator(OwnPath[OwnPathPrefix - 1]))
                OwnPathPrefix = OwnPathPrefix - 1;
            OwnPath[OwnPathPrefix] = '\\';
        }

        static GENRESULT OpenArchiveFromDesc(FFileSystemDesc* Desc, const char* OpenPath,
                                             IDataBuffer* DataBuffer, void** Created)
        {
            GENRESULT Result = GR_OK;
            FUtfArchiveMap Map;
            Map.File = INVALID_HANDLE_VALUE;
            Map.Mapping = nullptr;
            Map.View = nullptr;
            Map.Size = 0;
            Map.Pos = 0;
            if (DataBuffer != nullptr)
            {
                Result = GR_GENERIC;
                void* Buffer = nullptr;
                unsigned long Size = 0;
                if (DataBuffer->GetBuffer(&Buffer, &Size) == GR_OK && Buffer != nullptr &&
                    Size != 0)
                {
                    Map.Size = Size;
                    Map.View = Buffer;
                    Map.Pos = 0;
                    Result = OpenArchive(Desc, &Map, Created);
                }
            }
            else
            {
                Map.File = CreateFileA(OpenPath, Desc->Access, Desc->ShareMode,
                                       (LPSECURITY_ATTRIBUTES)(Desc->Security),
                                       Desc->Creation, Desc->Flags,
                                       (HANDLE)(Desc->TemplateFile));
                if (Map.File == INVALID_HANDLE_VALUE && Map.View == nullptr)
                    Result = GR_GENERIC;
                else
                {
                    if (Map.File != nullptr)
                    {
                        Map.Mapping =
                            CreateFileMappingA(Map.File, nullptr, PAGE_READONLY, 0, 0, nullptr);
                        if (Map.Mapping != nullptr)
                        {
                            Map.Size = GetFileSize(Map.File, nullptr);
                            Map.View = MapViewOfFile(Map.Mapping, FILE_MAP_READ, 0, 0, Map.Size);
                            Map.Pos = 0;
                        }
                    }
                    Result = OpenArchive(Desc, &Map, Created);
                }
            }
            ReleaseArchiveMapping(&Map);
            if (Map.File != INVALID_HANDLE_VALUE)
            {
                CloseHandle(Map.File);
                Map.File = INVALID_HANDLE_VALUE;
            }
            ReleaseArchiveMapping(&Map);
            return Result;
        }

        static GENRESULT OpenArchive(FFileSystemDesc* Desc, FUtfArchiveMap* Map, void** Out)
        {
            FUtfHeader Header;
            unsigned long Got = 0;
            if (Map->View != nullptr)
            {
                if (Map->Pos < Map->Size)
                {
                    Got = Map->Size - Map->Pos;
                    if (Got > kUtfHeaderSize)
                        Got = kUtfHeaderSize;
                    std::memcpy(&Header, (const char*)(Map->View) + Map->Pos, Got);
                    Map->Pos = Map->Pos + Got;
                }
            }
            else
            {
                DWORD Read = 0;
                if (ReadFile(Map->File, &Header, kUtfHeaderSize, &Read, nullptr) == 0)
                    return (GENRESULT)(0xFFFFFFFA);
                Got = Read;
            }
            if (Got != kUtfHeaderSize || Header.Magic != kUtfMagic || Header.Version != kUtfVersion)
                return (GENRESULT)(0xFFFFFFFA);

            FUtfFileSystem* Object = new FUtfFileSystem();
            if (Header.NodeSize != kUtfNode12 && Header.NodeSize != kUtfNode2C)
            {
                Object->Release();
                *Out = nullptr;
                return (GENRESULT)(0xFFFFFFFA);
            }
            Object->Walker = new FUtfWalker(Header.NodeSize == kUtfNode2C);

            Object->FileHandle = Map->File;
            Map->File = INVALID_HANDLE_VALUE;
            Object->MappingHandle = Map->Mapping;
            Object->MappedView = Map->View;
            Object->MappedSize = Map->Size;
            Object->MappedPos = Map->Pos;
            Map->Mapping = nullptr;
            Map->View = nullptr;
            Object->SetOwnPath(Desc->Path, nullptr);
            Object->AccessFlags = Desc->Access;
            if (!Object->Walker->InitializeArchiveView(Object->FileHandle, Object->MappedView, Object->MappedSize))
            {
                Object->Release();
                *Out = nullptr;
                return (GENRESULT)(0xFFFFFFFA);
            }
            *Out = (IFileSystem*)(Object);
            return GR_OK;
        }

        GENRESULT CloneAtNode(FFileSystemDesc* Desc, void** Created)
        {
            FUtfWalker* ActiveW = ActiveWalker();
            if (ActiveW == nullptr)
                return GR_GENERIC;
            char Dest[0x105] = {};
            ResolvePath(Dest, Desc->Path,
                   0x104);
            const unsigned long Extra = Desc->Size == 0x34 ? Desc->Field30 : 0xFFFFFFFFu;
            FUtfHandle* Found =
                ActiveW->Open(Dest, (unsigned long)(RootHandle()), Extra);
            if (Found == nullptr)
                return GR_GENERIC;

            FUtfFileSystem* Child = new FUtfFileSystem();
            *Created = (IFileSystem*)(Child);
            Child->SetOwnPath(Desc->Path, Found);
            if (Child->ParentFileSystem != this)
            {
                Child->ParentFileSystem = this;
                AddRef();
            }
            FUtfFileSystem* Topmost = this;
            while (Topmost->ParentFileSystem != nullptr)
                Topmost = Topmost->ParentFileSystem;
            if (Child->ArchiveOwner != Topmost)
            {
                Child->ArchiveOwner = Topmost;
                Topmost->AddRef();
            }
            Child->AccessFlags = AccessFlags & Desc->Access;
            return GR_OK;
        }

    public:
        int DACOM_API GetCurrentPath(unsigned long MaxChars, char* Out) override
        {
            if (ParentFileSystem == nullptr)
            {
                LastError = 5;
                return 0;
            }
            LastError = 0;
            return CopyClampedPathSuffix(Out, MaxChars, OwnPath + OwnPathPrefix);
        }

        // UTF LAB_06b73c70: n = strlen(+0x20) + 1, clamped to MaxChars (signed); n > 0 and
        // Out -> memcpy n bytes. No terminator fix-up when MaxChars cuts the path.
        void DACOM_API GetFileSysPath(char* Out, unsigned long MaxChars) override
        {
            long Count = (long)(std::strlen(OwnPath)) + 1;
            if ((long)(MaxChars) < Count)
                Count = (long)(MaxChars);
            if (Count > 0 && Out != nullptr)
                std::memcpy(Out, OwnPath, (size_t)(Count));
        }

        // UTF LAB_06b73130: FUN_06b75420's body with the root = the current path +0x128.
        // formerly SlotA4
        int DACOM_API ResolvePath(char* Dest, const char* Src, unsigned long DestChars) override
        {
            return JoinRoot(CurrentPath, Src,
                            Dest, (long)(DestChars));
        }

        int DACOM_API OpenNode(const char* Name) override
        {
            FUtfHandle* Root = RootHandle();
            FUtfWalker* ActiveW = ActiveWalker();
            if (Root == nullptr || !Root->IsDirectory() || ActiveW == nullptr)
            {
                LastError = 5;
                return 0;
            }

            char Dest[0x105];
            if (ResolvePath(Dest, Name,
                       0x104) == 0)
            {
                LastError = 0xA1;
                return 0;
            }

            FUtfHandle* Opened =
                ActiveW->Open(Dest, (unsigned long)(RootHandle()), 0xFFFFFFFFu);
            if (Opened == nullptr)
            {
                LastError = 0xA1;
                return 0;
            }

            if (OpenNodeHandle != nullptr)
                ActiveW->CloseHandle(OpenNodeHandle);
            OpenNodeHandle = Opened;
            // 06b735d4..06b73618: strlen + REP MOVS len+1 into +0x128, then '\' + NUL at
            // [len] when +0x128[len-1] is not a separator (len >= 1 after a join).
            const size_t Len = std::strlen(Dest);
            std::memcpy(CurrentPath, Dest, Len + 1);
            if (!IsJoinSeparator(CurrentPath[Len - 1]))
            {
                CurrentPath[Len] = '\\';
                CurrentPath[Len + 1] = '\0';
            }
            return 1;
        }

        FUtfHandle* ResolveHandle(unsigned long Handle) const
        {
            if (Handle == 0)
                return RootHandle();
            return FUtfWalker::HandleFromValue(Handle);
        }

        // UTF LAB_06b72e90: no handle -> LastError=5, -1; else the handle's size (slot 2).
        unsigned long DACOM_API GetSize(unsigned long Handle, unsigned long* /*HighOut*/) override
        {
            FUtfHandle* UtfHandle = ResolveHandle(Handle);
            if (UtfHandle == nullptr)
            {
                LastError = 5;
                return 0xFFFFFFFFu;
            }
            return UtfHandle->Size();
        }

        // formerly Slot20
        int DACOM_API SeekFile(unsigned long Handle, unsigned long OffLow, unsigned long*,
                             unsigned long Origin) override
        {
            FUtfHandle* UtfHandle = ResolveHandle(Handle);
            if (UtfHandle == nullptr)
            {
                LastError = 5;
                return -1;
            }
            const unsigned long Size = UtfHandle->Size();
            unsigned long NewPos = OffLow;
            if (Origin == FILE_CURRENT)
                NewPos = UtfHandle->Position + OffLow;
            else if (Origin == FILE_END)
                NewPos = Size - OffLow;
            if (NewPos > Size)
                return -1;
            UtfHandle->Position = NewPos;
            return (int)(NewPos);
        }

        // formerly Slot34
        int DACOM_API GetFileTimes(unsigned long Handle, FILETIME* CreationOut, FILETIME* AccessOut,
                             FILETIME* WriteOut) override
        {
            FUtfWalker* ActiveW = ActiveWalker();
            if (ActiveW == nullptr)
            {
                LastError = 6;
                return 0;
            }
            FUtfHandle* UtfHandle = ResolveHandle(Handle);
            if (UtfHandle == nullptr)
            {
                LastError = 6;
                return 0;
            }
            ActiveW->GetFileTimes((unsigned long)(UtfHandle),
                                  CreationOut,
                                  AccessOut,
                                  WriteOut);
            return 1;
        }

        // formerly Slot70
        int DACOM_API GetPathAttributes(const char* Path) override
        {
            FUtfWalker* ActiveW = ActiveWalker();
            if (ActiveW == nullptr)
            {
                LastError = 5;
                return 0;
            }
            char Dest[0x105];
            if (ResolvePath(Dest, Path, 0x104) == 0)
            {
                LastError = 0xA1;
                return 0;
            }
            const int Attributes =
                ActiveW->GetAttributes(Dest, (unsigned long)(RootHandle()));
            if (Attributes == -1)
            {
                LastError = 0xA1;
                return 0;
            }
            return Attributes;
        }

        // formerly Slot80
        int DACOM_API GetFilePosition(unsigned long Handle, unsigned long* HighOut) override
        {
            if (ParentFileSystem == nullptr || Handle == 0)
            {
                LastError = 5;
                return 0;
            }
            const int Result = ParentFileSystem->GetFilePosition(Handle, HighOut);
            if (Result == -1)
                LastError = (unsigned long)(ParentFileSystem->GetFileSystemError());
            return Result;
        }

        int DACOM_API CloseFile(unsigned long Handle) override
        {
            if (Handle == 0xFFFFFFFFu)
            {
                LastError = 5;
                return 0;
            }
            FUtfWalker* ActiveW = ActiveWalker();
            if (ActiveW == nullptr)
            {
                LastError = 5;
                return 0;
            }
            std::list<FUtfHandle*>::iterator Token = FindMapToken(Handle);
            if (Token != MapTokens.end())
            {
                MapTokens.erase(Token);
                return 1;
            }
            FUtfHandle* UtfHandle = nullptr;
            if (ActiveW->IsValidHandle(Handle))
                UtfHandle = FUtfWalker::HandleFromValue(Handle);
            if (UtfHandle == nullptr)
            {
                // 06b7372b..06b73769: FUN_06b737a0 (_vsnprintf into a 0x2000 stack buffer) with
                // the owner's (+0x230) or own +0x20 path, then FDUMP as a WARNING, line 0x29b.
                char Message[0x2000];
                const char* Path = ArchiveOwner != nullptr ? ArchiveOwner->OwnPath : OwnPath;
                _snprintf(Message, sizeof(Message), "attempting to close invalid handle (%s)", Path);
                FDUMP(0x100002, "%s(%d) : WARNING:General:%s",
                      "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\DOSFile\\ReadFile\\BaseUTF.cpp",
                      0x29B, Message);
                LastError = 6;
                return 0;
            }
            return ActiveW->CloseHandle(UtfHandle) ? 1 : 0;
        }

        int DACOM_API Read(unsigned long Handle, void* Buffer, unsigned long Size,
                           unsigned long* BytesRead, unsigned long /*Extra*/) override
        {
            if (Buffer == nullptr || Size == 0)
            {
                LastError = 0x57;
                return 0;
            }
            FUtfHandle* UtfHandle = ResolveHandle(Handle);
            if (UtfHandle == nullptr)
            {
                LastError = 6;
                return 0;
            }
            FUtfFileSystem* Archive = ArchiveOwner;
            if (Archive == nullptr)
            {
                Archive = this;
                AddRef();
                Release();
            }
            if ((Archive->FileHandle == INVALID_HANDLE_VALUE && Archive->MappedView == nullptr) ||
                Archive->Walker == nullptr)
            {
                LastError = 0x1E;
                return 0;
            }
            const unsigned long DataSection = ReadUtfDword(Archive->Walker->GetView(), 0x24);
            const unsigned long Position = UtfHandle->Position;
            const unsigned long FileOff = DataSection + UtfHandle->DataOffset() + Position;
            unsigned long End = Position + Size;
            const unsigned long HandleSize = UtfHandle->Size();
            if (End > HandleSize)
                End = HandleSize;
            UtfHandle->Position = End;
            unsigned long Count = End - Position;
            if (FileOff > Archive->MappedSize)
            {
                LastError = 0x1E;
                return 0;
            }
            if (Count > Archive->MappedSize - FileOff)
                Count = Archive->MappedSize - FileOff;
            std::memcpy(Buffer, (const char*)(Archive->MappedView) + FileOff, Count);
            Archive->MappedPos = FileOff + Count;
            if (Count == 0)
            {
                LastError = 0x1E;
                return 0;
            }
            *BytesRead = Count;
            return 1;
        }

        // formerly Slot3C
        int DACOM_API CreateMapping(unsigned long FileKey, unsigned long, unsigned long Protect,
                             unsigned long, unsigned long, unsigned long) override
        {
            if (Protect != PAGE_READONLY || ArchiveOwner == nullptr || ArchiveOwner->MappedView == nullptr ||
                ArchiveOwner->Walker == nullptr)
            {
                LastError = 5;
                return -1;
            }
            FUtfHandle* UtfHandle = nullptr;
            if (FileKey == 0)
                UtfHandle = OwnRootHandle;
            else
                UtfHandle = FUtfWalker::HandleFromValue(FileKey);
            if (UtfHandle == nullptr)
            {
                LastError = 6;
                return -1;
            }
            MapTokens.push_back(UtfHandle);
            return (int)((unsigned long)(&MapTokens.back()));
        }

        // formerly Slot40
        int DACOM_API MapFileView(unsigned long Token, unsigned long Access, unsigned long,
                             unsigned long OffLow, unsigned long ByteCount) override
        {
            if (Access != FILE_MAP_READ)
            {
                LastError = 5;
                return 0;
            }
            std::list<FUtfHandle*>::iterator Found = FindMapToken(Token);
            if (Found == MapTokens.end())
            {
                LastError = 6;
                return -1;
            }
            FUtfWalker* ActiveW = ActiveWalker();
            if (ActiveW == nullptr)
            {
                LastError = 5;
                return 0;
            }
            FUtfHandle* UtfHandle = *Found;
            if (UtfHandle == nullptr)
            {
                LastError = 6;
                return 0;
            }
            if (ArchiveOwner == nullptr || ArchiveOwner->MappedView == nullptr)
            {
                LastError = 5;
                return 0;
            }
            if (ByteCount > UtfHandle->Size() - OffLow)
            {
                LastError = 5;
                return 0;
            }
            const unsigned long DataSection = ReadUtfDword(ActiveW->GetView(), 0x24);
            const unsigned long FileOff = DataSection + UtfHandle->DataOffset();
            return (int)(
                (unsigned long)((const char*)(ArchiveOwner->MappedView)) +
                FileOff + OffLow);
        }

        // The +0x240 token whose payload address is Value (06b736a7 / 06b738db scans).
        std::list<FUtfHandle*>::iterator FindMapToken(unsigned long Value)
        {
            std::list<FUtfHandle*>::iterator It = MapTokens.begin();
            while (It != MapTokens.end())
            {
                if ((unsigned long)(&*It) == Value)
                    return It;
                ++It;
            }
            return MapTokens.end();
        }

        // UTF LAB_06b739e0: 1 if parent UTF at +0x230, else error 5 / 0.
        // formerly Slot44
        int DACOM_API UnmapFileView(unsigned long) override
        {
            if (ArchiveOwner != nullptr)
                return 1;
            LastError = 5;
            return 0;
        }

        int DACOM_API OpenByDesc(FFileSystemDesc* Desc) override
        {
            FUtfWalker* ActiveW = ActiveWalker();
            if (ActiveW == nullptr)
            {
                LastError = 2;
                return -1;
            }
            FUtfHandle* Opened = nullptr;
            if (OpenNodeHandle != nullptr)
            {
                const unsigned long Extra = Desc->Size == 0x34 ? Desc->Field30 : 0xFFFFFFFFu;
                Opened = ActiveW->Open(Desc->Path, (unsigned long)(OpenNodeHandle),
                                       Extra);
            }
            else
            {
                char Dest[0x105];
                if (ResolvePath(Dest,
                           Desc->Path, 0x104) == 0)
                {
                    LastError = 0xA1;
                    return -1;
                }
                const unsigned long Extra = Desc->Size == 0x34 ? Desc->Field30 : 0xFFFFFFFFu;
                Opened = ActiveW->Open(Dest, (unsigned long)(RootHandle()), Extra);
            }
            if (Opened == nullptr)
            {
                LastError = 2;
                return -1;
            }
            return (int)((unsigned long)(Opened));
        }

        // UTF LAB_06b73cc0: *Out = +0x234 (no Out check), AddRef it when set, return 0.
        // formerly Slot8C
        int DACOM_API GetParentFileSystem(IFileSystem** Out) override
        {
            *Out = ParentFileSystem;
            if (ParentFileSystem != nullptr)
                ParentFileSystem->AddRef();
            return 0;
        }

        // formerly SlotA0
        int DACOM_API InvokeLockedCallback(unsigned long Arg0, unsigned long Callback, unsigned long Arg1) override
        {
            return ParentFileSystem->InvokeLockedCallback(Arg0, Callback, Arg1);
        }

        // formerly Slot24
        int DACOM_API SetEndOfFile(unsigned long) override
        {
            LastError = 5;
            return 0;
        }

        // formerly Slot48
        int DACOM_API FindFirst(const char* Pattern, WIN32_FIND_DATAA* FindData) override
        {
            FUtfWalker* ActiveW = ActiveWalker();
            if (ActiveW == nullptr)
            {
                LastError = 2;
                return -1;
            }
            WIN32_FIND_DATAA* Out = FindData;
            int Result = -1;
            if (OpenNodeHandle != nullptr)
                Result = ActiveW->FindFirst(Pattern, Out,
                                            (unsigned long)(OpenNodeHandle));
            else
            {
                char Dest[0x105];
                if (ResolvePath(Dest, Pattern, 0x104) == 0)
                {
                    LastError = 0xA1;
                    return -1;
                }
                Result = ActiveW->FindFirst(Dest, Out, (unsigned long)(OwnRootHandle));
            }
            if (Result == -1)
                LastError = ActiveW->GetLastError();
            return Result;
        }

        // formerly Slot4C
        int DACOM_API FindNext(unsigned long Handle, WIN32_FIND_DATAA* FindData) override
        {
            FUtfWalker* ActiveW = ActiveWalker();
            if (ActiveW == nullptr)
            {
                LastError = 2;
                return 0;
            }
            const int Result =
                ActiveW->FindNext(Handle, FindData);
            if (Result == 0)
                LastError = ActiveW->GetLastError();
            return Result;
        }

        // formerly Slot50
        int DACOM_API FindClose(unsigned long Handle) override
        {
            FUtfWalker* ActiveW = ActiveWalker();
            if (ActiveW == nullptr)
            {
                LastError = 2;
                return 0;
            }
            const int Result = ActiveW->FindClose(Handle);
            if (Result == 0)
                LastError = ActiveW->GetLastError();
            return Result;
        }

        // +0x20: the path this object was created from (Desc->Path, "\" for the factory),
        // +0x124: its length / the clone's prefix, +0x128: the current path OpenNode keeps.
        char OwnPath[264];
        unsigned long OwnPathPrefix;
        char CurrentPath[264];
        FUtfHandle* OpenNodeHandle;       // +0x22c, the handle OpenNode last opened
        FUtfFileSystem* ArchiveOwner;     // +0x230, AddRef'd
        FUtfFileSystem* ParentFileSystem; // +0x234, AddRef'd
        FUtfWalker* Walker;               // +0x238, archives only
        FUtfHandle* OwnRootHandle;        // +0x23c, a clone's node handle (owner's walker)
        std::list<FUtfHandle*> MapTokens; // +0x240, Slot3C tokens
    };

    class FMemFileSystem final : public FFileSystemBase
    {
    public:
        class FMemDataBuffer final : public IDataBuffer
        {
        public:
            explicit FMemDataBuffer(FMemFileSystem* OwnerIn) : Owner(OwnerIn) {}

            GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override
            {
                return Owner->QueryInterface(Iid, Out);
            }
            unsigned long DACOM_API AddRef() override { return Owner->AddRef(); }
            unsigned long DACOM_API Release() override { return Owner->Release(); }
            // LAB_06b772a0 (slot 3 of the inner vtable 06b792e0, this = MEM+4): *OutPtr = [MEM+8]
            // (buffer), *OutSize = [MEM+0xC] (size), return 0. No argument checks.
            GENRESULT DACOM_API GetBuffer(void** OutPtr, unsigned long* OutSize) override
            {
                *OutPtr = Owner->MappedView;
                *OutSize = Owner->AllocSize;
                return GR_OK;
            }

            FMemFileSystem* Owner;
        };

        FMemFileSystem()
            : FFileSystemBase(), Inner(this), AllocSize(0), MemFlags(0), Access(0), LogicalSize(0)
        {
        }

        ~FMemFileSystem() override
        {
            ReleaseMemBuffer();
            MappedView = nullptr;
            MappingHandle = nullptr;
            FileHandle = INVALID_HANDLE_VALUE;
        }

        // LAB_06b77a50, table 06b79398: {"1.11_IFileSystem", 0}, {"1.11_IDataBuffer", 4}, null.
        // Walked in order; hit -> AddRef on the resolved face, *Out = face, 0; miss -> *Out = 0, -3.
        GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override
        {
            if (std::strcmp(Iids::FileSystem, Iid) == 0)
            {
                IFileSystem* Face = (IFileSystem*)(this);
                Face->AddRef();
                *Out = Face;
                return GR_OK;
            }
            if (std::strcmp(Iids::DataBuffer, Iid) == 0)
            {
                IDataBuffer* Face = (IDataBuffer*)(&Inner);
                Face->AddRef();
                *Out = Face;
                return GR_OK;
            }
            *Out = nullptr;
            return GR_INTERFACE_UNSUPPORTED;
        }

        // LAB_06b77290 (RET 0xc): *Instance = 0 with no null check, return -1.
        GENRESULT DACOM_API CreateInstance(AGGDESC*, void** Instance) override
        {
            *Instance = nullptr;
            return GR_GENERIC;
        }

        // formerly Slot88
        int DACOM_API GetAccessFlags() override
        {
            return (int)(Access);
        }

        // formerly InitFromDesc
        GENRESULT InitializeFromDescriptor(FFileSystemDesc* Desc)
        {
            if (Desc->Parent != nullptr)
                return (GENRESULT)(0xFFFFFFFE);
            // FUN_06b777f0: null Implementation or CMPSB.REPE over "MEM\0" (DAT_06b7a1f0,
            // case-sensitive) mismatch -> -1.
            if (Desc->Implementation == nullptr || std::strcmp(Desc->Implementation, "MEM") != 0)
                return GR_GENERIC;

            Access = Desc->Access;
            void* Source = Desc->MemBuffer;
            if ((Desc->MemFlags & 1) == 0)
            {
                if (Source != nullptr)
                    AllocSize = Desc->MemSize;
                if (AllocSize != 0)
                {
                    MappedView = VirtualAlloc(nullptr, AllocSize, MEM_COMMIT, PAGE_READWRITE);
                    if (MappedView == nullptr)
                        AllocSize = 0;
                }
                MemFlags = MemFlags | 5;
            }
            else
            {
                MappedView = Source;
                if (Source == nullptr)
                    AllocSize = 0;
                else
                    AllocSize = Desc->MemSize;
                if ((Desc->MemFlags & 2) != 0)
                    MemFlags = MemFlags | 1;
                MemFlags = MemFlags | 2;
            }

            MappedSize = AllocSize;
            if (MappedView != nullptr)
            {
                if (Desc->Creation > 2 && Desc->Creation < 5)
                    LogicalSize = AllocSize;
                if (Source != nullptr && Source != MappedView && AllocSize != 0)
                    std::memcpy(MappedView, Source, AllocSize);
            }
            if (Desc->Path != nullptr)
                CopyPath(RootPath, 264, Desc->Path);
            MappedPos = 0;
            return GR_OK;
        }

        // FUN_06b77250: owned (flags & 1) -> VirtualFree(MEM_RELEASE) when flags & 4, else
        // free(); the view (+8) is released without a null test and cleared in every path.
        void ReleaseMemBuffer()
        {
            if ((MemFlags & 1) != 0)
            {
                if ((MemFlags & 4) != 0)
                {
                    VirtualFree(MappedView, 0, MEM_RELEASE);
                    MappedView = nullptr;
                    return;
                }
                free(MappedView);
            }
            MappedView = nullptr;
        }

        // MEM LAB_06b775b0 (`XOR EAX,EAX; RET 8`): +0x10 / +0x44 / +0x50 / +0x58 / +0x60 /
        // +0x64 / +0x70 return 0 and leave LastError alone (+0x50 comes from the base).
        int DACOM_API CloseFile(unsigned long) override { return 0; }

        // formerly Slot44
        int DACOM_API UnmapFileView(unsigned long) override { return 0; }

        // formerly Slot58
        int DACOM_API RejectOperation58(unsigned long) override { return 0; }

        int DACOM_API OpenNode(const char*) override { return 0; }

        // formerly Slot64
        int DACOM_API RejectOperation64(unsigned long) override { return 0; }

        // formerly Slot70
        int DACOM_API GetPathAttributes(const char*) override { return 0; }

        // formerly Slot54
        int DACOM_API RejectOperation54(const char*, unsigned long) override { return 0; }

        int DACOM_API GetCurrentPath(unsigned long, char*) override { return 0; }

        // formerly Slot6C
        int DACOM_API RejectOperation6C(unsigned long, unsigned long) override { return 0; }

        // formerly Slot74
        int DACOM_API RejectOperation74(unsigned long, unsigned long) override { return 0; }

        // formerly Slot78
        int DACOM_API GetFileSystemError() override { return 0; }

        int DACOM_API OpenByDesc(FFileSystemDesc*) override { return -1; }

        // formerly SlotA0
        int DACOM_API InvokeLockedCallback(unsigned long Arg0, unsigned long Callback, unsigned long Arg1) override
        {
            return InvokeDosRootLockedCall(Arg0, Callback, Arg1);
        }

        // formerly SlotA4
        int DACOM_API ResolvePath(char* DestArg, const char* SrcArg,
                             unsigned long DestCharsArg) override
        {
            char* Dest = DestArg;
            const char* Src = SrcArg;
            const long DestChars = (long)(DestCharsArg);
            int Result = 1;
            if (DestChars <= 0)
                return 0;
            if (Src[0] == '\\' || Src[0] == '/')
            {
                std::strncpy(Dest, Src, (size_t)(DestChars));
                Dest[DestChars - 1] = '\0';
                return 1;
            }
            std::strncpy(Dest, RootPath, (size_t)(DestChars));
            if (Src[0] == '.')
            {
                if (IsJoinSeparator(Src[1]))
                    Src = Src + 2;
                while (Src[0] == '.' && Src[1] == '.')
                {
                    const long Length = (long)(std::strlen(Dest));
                    if (Length <= 2)
                        Result = 0;
                    else
                    {
                        Dest[Length - 1] = '\0';
                        char* LastSeparator = nullptr;
                        for (char* Cursor = Dest; *Cursor != '\0'; ++Cursor)
                        {
                            if (IsJoinSeparator(*Cursor))
                                LastSeparator = Cursor;
                        }
                        if (LastSeparator != nullptr)
                            LastSeparator[1] = '\0';
                        else
                            Result = 0;
                    }
                    Src = Src + 2;
                    if (IsJoinSeparator(Src[0]))
                        Src = Src + 1;
                }
            }
            const long Length = (long)(std::strlen(Dest));
            const long Remaining = DestChars - Length;
            if (Remaining > 0)
                std::strncpy(Dest + Length, Src, (size_t)(Remaining));
            return Result;
        }

        // MEM LAB_06b77690: *Out = 0 (no Out check), return -1.
        // formerly Slot8C
        int DACOM_API GetParentFileSystem(IFileSystem** Out) override
        {
            *Out = nullptr;
            return -1;
        }

        // formerly Slot80
        int DACOM_API GetFilePosition(unsigned long Handle, unsigned long* HighOut) override
        {
            if (Handle != 0)
                return -1;
            if (HighOut != 0)
                *HighOut = 0;
            return (int)(MappedPos);
        }

        // MEM vtable06b792f0+0x18 = LAB_06b77330, not the DOS failure stub.
        // formerly Slot18
        int DACOM_API Write(unsigned long Handle, const void* Buffer, unsigned long Size,
                             unsigned long* BytesWritten, const void* PositionOverride) override
        {
            if (Handle != 0 || (Access & kGenericWrite) == 0)
                return 0;
            if (PositionOverride != nullptr)
                std::memcpy(&MappedPos, (const char*)(PositionOverride) + 8,
                            sizeof(MappedPos));
            unsigned long ToWrite = Size;
            const unsigned long End = MappedPos + Size;
            if (End > AllocSize)
            {
                if ((MemFlags & 2) != 0)
                {
                    if (MappedPos >= AllocSize)
                        ToWrite = 0;
                    else
                        ToWrite = AllocSize - MappedPos;
                }
                else
                {
                    // 77392..773f7: unsigned wrapping round-up, no clamp.
                    const unsigned long NewSize = (End + 0xFFFFu) & 0xFFFFF000u;
                    void* NewView = VirtualAlloc(nullptr, NewSize, MEM_COMMIT, PAGE_READWRITE);
                    if (NewView == nullptr)
                        ToWrite = 0;
                    else
                    {
                        if (MappedView != nullptr)
                        {
                            std::memcpy(NewView, MappedView, AllocSize);
                            VirtualFree(MappedView, 0, MEM_RELEASE);
                        }
                        MappedView = NewView;
                        AllocSize = NewSize;
                        MappedSize = NewSize;
                        MemFlags = MemFlags | 4;
                    }
                }
            }
            if (ToWrite != 0)
                std::memcpy((char*)(MappedView) + MappedPos, Buffer, ToWrite);
            if (BytesWritten != nullptr)
                *BytesWritten = ToWrite;
            MappedPos = MappedPos + ToWrite;
            if (ToWrite != 0 && LogicalSize < MappedPos)
                LogicalSize = MappedPos;
            return 1;
        }

        // formerly Slot24
        int DACOM_API SetEndOfFile(unsigned long) override
        {
            if ((Access & kGenericWrite) == 0)
                return 0;
            if (MappedPos > AllocSize)
            {
                if ((MemFlags & 2) != 0)
                    return 0;
                const unsigned long NewSize = (MappedPos + 0xFFFFu) & 0xFFFFF000u;
                void* NewView = VirtualAlloc(nullptr, NewSize, MEM_COMMIT, PAGE_READWRITE);
                if (NewView != nullptr)
                {
                    if (MappedView != nullptr)
                    {
                        std::memcpy(NewView, MappedView, AllocSize);
                        VirtualFree(MappedView, 0, MEM_RELEASE);
                    }
                    MappedView = NewView;
                    MemFlags = MemFlags | 4;
                    AllocSize = NewSize;
                    MappedSize = NewSize;
                }
            }
            LogicalSize = MappedPos;
            return 1;
        }

        // formerly Slot1C
        int DACOM_API RejectOperation1C(unsigned long, unsigned long, unsigned long, unsigned long) override
        {
            return 0;
        }
        // formerly Slot2C
        int DACOM_API ReturnOne2C(unsigned long, unsigned long, unsigned long, unsigned long,
                             unsigned long) override
        {
            return 1;
        }
        // formerly Slot30
        int DACOM_API ReturnOne30(unsigned long, unsigned long, unsigned long, unsigned long,
                             unsigned long) override
        {
            return 1;
        }
        // formerly Slot34
        int DACOM_API GetFileTimes(unsigned long, FILETIME*, FILETIME*, FILETIME*) override
        {
            return 0;
        }
        // formerly Slot38
        int DACOM_API RejectOperation38(unsigned long, unsigned long, unsigned long, unsigned long) override
        {
            return 0;
        }
        // formerly Slot3C
        int DACOM_API CreateMapping(unsigned long, unsigned long, unsigned long, unsigned long,
                             unsigned long, unsigned long) override
        {
            return 0;
        }
        // formerly Slot40
        int DACOM_API MapFileView(unsigned long, unsigned long, unsigned long, unsigned long,
                             unsigned long) override
        {
            return 0;
        }
        // formerly Slot68
        int DACOM_API RejectOperation68(unsigned long, unsigned long, unsigned long) override
        {
            return 0;
        }

        int DACOM_API Read(unsigned long Handle, void* Buffer, unsigned long Size,
                           unsigned long* BytesRead, unsigned long Extra) override
        {
            if (Handle != 0)
                return 0;
            if (Extra != 0)
                MappedPos = *(unsigned long*)(Extra + 8);
            if (MappedPos > LogicalSize)
                MappedPos = LogicalSize;
            unsigned long Remain = 0;
            if (MappedPos < LogicalSize)
                Remain = LogicalSize - MappedPos;
            unsigned long Got = Size;
            if (Got > Remain)
                Got = Remain;
            // 06b772fb..06b7731b: REP MOVS from +8 + position and *BytesRead store, both
            // without checks (a zero count copies nothing).
            std::memcpy(Buffer, (char*)(MappedView) + MappedPos, Got);
            *BytesRead = Got;
            MappedPos = MappedPos + Got;
            return 1;
        }

        unsigned long DACOM_API GetSize(unsigned long Handle, unsigned long* HighOut) override
        {
            if (Handle != 0)
                return 0xFFFFFFFF;
            if (HighOut != 0)
                *HighOut = 0;
            return LogicalSize;
        }

        // formerly Slot20
        int DACOM_API SeekFile(unsigned long Handle, unsigned long OffLow, unsigned long* OffHighOut,
                             unsigned long Origin) override
        {
            // MEM LAB_06b77460: handle != 0 -> -1; origin 1 = pos + off, 2 = alloc (+0xc) - off
            // (unsigned, no clamp), anything else = off; pos = it; high out (if any) = 0.
            if (Handle != 0)
                return -1;
            unsigned long NewPos = OffLow;
            if (Origin == FILE_CURRENT)
                NewPos = MappedPos + OffLow;
            else if (Origin == FILE_END)
                NewPos = AllocSize - OffLow;
            MappedPos = NewPos;
            if (OffHighOut != 0)
                *OffHighOut = 0;
            return (int)(NewPos);
        }

        FMemDataBuffer Inner;
        unsigned long AllocSize;
        unsigned long MemFlags;
        unsigned long Access;
        unsigned long LogicalSize;
    };

    class FMemFactory final : public FNamedFactory
    {
    public:
        FMemFactory() : FNamedFactory("FileSystem") {}

        GENRESULT DACOM_API CreateInstance(AGGDESC* Desc, void** Instance) override
        {
            if (Desc->size != 0x40 || std::strcmp(Desc->interface_name, Name) != 0)
            {
                *Instance = nullptr;
                return GR_INTERFACE_UNSUPPORTED;
            }
            FMemFileSystem* Object = new (std::nothrow) FMemFileSystem();
            if (Object == nullptr)
            {
                *Instance = nullptr;
                return GR_OUT_OF_MEMORY;
            }
            FFileSystemDesc* MemDesc = (FFileSystemDesc*)(Desc);
            const GENRESULT InitResult = Object->InitializeFromDescriptor(MemDesc);
            if (InitResult != GR_OK)
            {
                delete Object;
                *Instance = nullptr;
                return InitResult;
            }
            *Instance = (IFileSystem*)(Object);
            return GR_OK;
        }
    };

    template <typename FFactory>
    bool OpenSearchPathEntry(FFactory* Factory, char* Path, IFileSystem** Out)
    {
        unsigned char Raw[0x34];
        std::memset(Raw, 0, sizeof(Raw));
        FFileSystemDesc* Desc = (FFileSystemDesc*)(Raw);
        Desc->Size = 0x34;
        Desc->ComponentName = "FileSystem";
        Desc->Implementation = "DOS";
        Desc->Path = Path;
        Desc->Access = kGenericRead;
        Desc->ShareMode = 1;
        Desc->Creation = kOpenExisting;
        Desc->Flags = 0x08000080;
        Desc->Field30 = 0xFFFFFFFF;

        bool bResult = false;
        IFileSystem* Created = nullptr;
        *Out = nullptr;
        char* Rest = nullptr;
        const char First = Path[0];
        if ((First == '\\' || First == '/') && (Path[1] == '\\' || Path[1] == '/'))
        {
            if (Path[2] == '\\' || Path[2] == '/')
                return false;
            char* Cursor = Path + 3;
            if (*Cursor == '\0')
                return false;
            while (*Cursor != '\\' && *Cursor != '/')
            {
                Cursor = Cursor + 1;
                if (*Cursor == '\0')
                    return false;
            }
            Cursor = Cursor + 1;
            if (*Cursor == '\\' || *Cursor == '/')
                return false;
            while (*Cursor != '\0' && *Cursor != '\\' && *Cursor != '/')
                Cursor = Cursor + 1;
            if (*Cursor != '\0')
            {
                *Cursor = '\0';
                Rest = Cursor + 1;
            }
        }
        else
        {
            char* Cursor = Path + 1;
            if (std::isalpha((unsigned char)(First)) != 0 && Path[1] == ':')
                Cursor = Path + 3;
            while (*Cursor != '\0' && *Cursor != '\\' && *Cursor != '/')
                Cursor = Cursor + 1;
            if (*Cursor != '\0')
            {
                *Cursor = '\0';
                Rest = Cursor + 1;
            }
            // 06b77f43: retail releases Created here when set; it is always null at this point.
        }

        if (Factory->CreateInstance((AGGDESC*)(Desc), (void**)(&Created)) == GR_OK)
        {
            if (Rest != nullptr && *Rest != '\0')
                bResult = OpenSearchPathEntry(Created, Rest, Out);
            else
            {
                *Out = Created;
                Created->AddRef();
                bResult = true;
            }
        }
        if (Created != nullptr)
            Created->Release();
        return bResult;
    }

    // Ordered child file systems searched for an instance request.
    class FSearchPathInstance final : public ISearchPath
    {
    public:
        FSearchPathInstance() : RefCount(1), ChildCount(0)
        {
            std::memset(PathText, 0, sizeof(PathText));
        }

        // FUN_06b78210 zero path: children released (ReleaseChildren), count = 0, path[0] = 0,
        // free(this). The vector frees itself here.
        ~FSearchPathInstance()
        {
            ReleaseChildren();
            ChildCount = 0;
            PathText[0] = '\0';
        }

        // LAB_06b78140, table 06b793d8: {"1.11_ISearchPath", 0}, {"1.11_IComponentFactory", 0},
        // null. Both entries resolve to this; miss -> *Out = 0, -3.
        GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override
        {
            if (std::strcmp(Iids::SearchPath, Iid) != 0 && std::strcmp(Iids::ComponentFactory, Iid) != 0)
            {
                *Out = nullptr;
                return GR_INTERFACE_UNSUPPORTED;
            }
            ISearchPath* Face = (ISearchPath*)(this);
            Face->AddRef();
            *Out = Face;
            return GR_OK;
        }

        unsigned long DACOM_API AddRef() override
        {
            RefCount = RefCount + 1;
            return RefCount;
        }

        unsigned long DACOM_API Release() override
        {
            if (RefCount != 0)
                RefCount = RefCount - 1;
            const unsigned long Remaining = RefCount;
            if (Remaining == 0)
            {
                RefCount = 1;
                delete this;
                return 0;
            }
            return Remaining;
        }

        GENRESULT DACOM_API CreateInstance(AGGDESC* Desc, void** Instance) override
        {
            *Instance = nullptr;
            GENRESULT Result = (GENRESULT)(0xFFFFFFFA);
            for (int Index = 0; Index < ChildCount; ++Index)
            {
                IFileSystem* Child = Children[(size_t)(Index)];
                if (Child == nullptr)
                    continue;
                Result = Child->CreateInstance(Desc, Instance);
                if (Result == GR_OK)
                    return Result;
            }
            return Result;
        }

        int DACOM_API SetPath(const char* PathList) override
        {
            if (PathList == nullptr)
                return -2;
            ReleaseChildren();
            ChildCount = 0;
            PathText[0] = '\0';
            std::strncpy(PathText, PathList, 0x104);
            char Local[0x108];
            std::memset(Local, 0, sizeof(Local));
            std::memcpy(Local, PathText, 0x104);
            ChildCount = SplitSearchPath(Local, 0);
            return 0;
        }

        int SplitSearchPath(char* Str, int Depth)
        {
            char* Segment = Str;
            char* Semi = std::strchr(Segment, ';');
            while (Semi != nullptr)
            {
                *Semi = '\0';
                if (Semi != Segment)
                {
                    const int Count = SplitSearchPath(Semi + 1, Depth + 1);
                    OpenSearchPathSlot((size_t)(Depth), Segment);
                    return Count;
                }
                Segment = Semi + 1;
                Semi = std::strchr(Segment, ';');
            }
            int Count = Depth;
            if (*Segment != '\0')
                Count = Count + 1;
            if (Count != 0)
                Children.assign((size_t)(Count), nullptr);
            if (*Segment != '\0')
                OpenSearchPathSlot((size_t)(Count - 1), Segment);
            return Count;
        }

        void OpenSearchPathSlot(size_t Slot, char* Segment)
        {
            if (Children[Slot] != nullptr)
            {
                Children[Slot]->Release();
                Children[Slot] = nullptr;
            }
            OpenSearchPathEntry(G_Dacom, Segment, &Children[Slot]);
        }

        // LAB_06b77fc0: copy this+4 path string to dest, max chars. RET 0xC.
        // formerly Slot14
        int DACOM_API GetPath(char* Dest, unsigned long MaxChars) override
        {
            char* Out = Dest;
            unsigned long Len = (unsigned long)(std::strlen(PathText)) + 1;
            unsigned long CopyLen = MaxChars;
            if (MaxChars >= Len)
                CopyLen = Len;
            if (CopyLen == 0 || Out == nullptr)
                return (int)(CopyLen);
            std::memcpy(Out, PathText, CopyLen);
            Out[CopyLen - 1] = '\0';
            return (int)(CopyLen);
        }

        // 06b77c32..06b77c6a / 06b7823a..06b78279: every allocated slot from the last to the
        // first is released and cleared, then the array block is freed.
        void ReleaseChildren()
        {
            for (size_t Index = Children.size(); Index > 0; --Index)
            {
                if (Children[Index - 1] != nullptr)
                {
                    Children[Index - 1]->Release();
                    Children[Index - 1] = nullptr;
                }
            }
            Children.clear();
        }

        unsigned long RefCount;            // +0x114
        char PathText[0x108];              // +0x4
        std::vector<IFileSystem*> Children; // +0x10c block, slot count = size()
        int ChildCount;                    // +0x110
    };

    class FSearchPathFactory final : public FNamedFactory
    {
    public:
        FSearchPathFactory() : FNamedFactory("SearchPath") {}

        // LAB_06b78090: size != 8 or case-sensitive name mismatch -> *Out = 0, -3;
        // calloc(0x118) failure -> *Out = 0, -4; success -> refcount 1, *Out = object, 0.
        GENRESULT DACOM_API CreateInstance(AGGDESC* Desc, void** Instance) override
        {
            if (Desc->size != 0x8 || std::strcmp(Desc->interface_name, Name) != 0)
            {
                *Instance = nullptr;
                return GR_INTERFACE_UNSUPPORTED;
            }
            FSearchPathInstance* Object = new (std::nothrow) FSearchPathInstance();
            if (Object == nullptr)
            {
                *Instance = nullptr;
                return GR_OUT_OF_MEMORY;
            }
            *Instance = (ISearchPath*)(Object);
            return GR_OK;
        }
    };
}

BOOL WINAPI DllMain(HINSTANCE Instance, DWORD Reason, LPVOID /*Reserved*/)
{
    if (Reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(Instance);
        G_DosFactory = new (std::nothrow) FDosFileSystem();
        if (G_DosFactory == nullptr)
            return TRUE;
        G_DosRootLock = &G_DosFactory->Lock;
        G_Dacom = DACOM_Acquire();
        if (G_Dacom == nullptr)
            return TRUE;
        G_Dacom->RegisterComponent(G_DosFactory, "FileSystem", (void*)(0x40000000));
        G_DosFactory->Release();

        // FUN_06b73d60 / FUN_06b77900 / FUN_06b78020 return null on allocation failure; a
        // null UTF or MEM factory is skipped, a null SearchPath factory ends the attach.
        FUtfFileSystem* UtfFactory = new (std::nothrow) FUtfFileSystem();
        if (UtfFactory != nullptr)
        {
            G_Dacom->RegisterComponent(UtfFactory, "FileSystem", (void*)(0x80000000));
            UtfFactory->Release();
        }

        FMemFactory* MemFactory = new (std::nothrow) FMemFactory();
        if (MemFactory != nullptr)
        {
            G_Dacom->RegisterComponent(MemFactory, "FileSystem", (void*)(0x80000000));
            MemFactory->Release();
        }

        FSearchPathFactory* SearchFactory = new (std::nothrow) FSearchPathFactory();
        if (SearchFactory == nullptr)
            return TRUE;
        G_Dacom->RegisterComponent(SearchFactory, "SearchPath", (void*)(0x80000000));
        SearchFactory->Release();
    }
    else if (Reason == DLL_PROCESS_DETACH)
    {
        // FUN_06b76220 param==0: FUN_06b73e00 is empty; Release DAT_06b7a220
        // (ICOManager). No UnregisterComponent; DAT_06b7a220 is not cleared.
        if (G_Dacom != nullptr)
            G_Dacom->Release();
    }
    return TRUE;
}
