// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Private RP9 hardware-quirk database for flconfigdatabase.txt: a copy of src/RP8/Mgsdb.h
// (mirror RP8 fixes here). The adapter identifier and caps keep their D3D8 shape
// (Rp9D3d8Types.h), so the database sees exactly the fields RP8 gives it.

#include "Rp9D3d8Types.h"

#include <map>
#include <string>
#include <vector>

class FMgsdbDatabase;

// One 0x20-byte record of a record set: name string +0x00, value string +0x10.
struct FMgsdbRecord
{
    std::string Name;
    std::string Value;
};

// FUN_06d22810 (operator new(0x14)): a vector of records (+0x4..+0xC) and its owner (+0x10).
// The database's live set sits at object+0x18; every "propertyset" definition gets its own.
struct FMgsdbRecordSet
{
    explicit FMgsdbRecordSet(FMgsdbDatabase* InOwner);

    // FUN_06d22851: overwrite the value of the record whose name _stricmp-matches, else append.
    void Insert(const char* Name, const char* Value);
    // FUN_06d22bdf: strncpy the value of the record whose name _stricmp-matches into Out.
    bool Find(const char* Name, char* Out, size_t OutSize) const;

    std::vector<FMgsdbRecord> Records;
    FMgsdbDatabase* Owner;
};

// The 0x584-byte match object (vtable 06d5eaf4). Field offsets are the retail ones.
class FMgsdbDatabase
{
public:
    // FUN_06d27d63: operator new(0x584) + FUN_06d25e9a.
    static FMgsdbDatabase* Create();

    FMgsdbDatabase();  // FUN_06d25e9a
    ~FMgsdbDatabase(); // FUN_06d26172

    // Slot 0, FUN_06d25808 (thiscall, RET 0x18).
    bool Load(const char* Path, const FRp9Identifier8* Ident, const FRp9Caps8* Caps,
              unsigned long Ram, unsigned long VideoRam, unsigned long CpuSpeed);
    // Slot 1, FUN_06d27da0: destroy + operator delete.
    void Destroy();
    // Slot 2, FUN_06d27ff0.
    FMgsdbRecordSet* GetRecords();
    // Slot 3, FUN_06d22c93: the record set of a propertyset definition, or null.
    FMgsdbRecordSet* FindPropertySet(const char* Name);
    // Slot 7, FUN_06d28010: +0x30 (written by the device match of FUN_06d26b14).
    const char* GetDeviceName() const;
    // Slot 8, FUN_06d28050: +0x40 (written by the vendor match of FUN_06d26b14).
    const char* GetVendorName() const;
    // Slot 9, FUN_06d28090: +0x1C.
    bool HasError() const;
    // Slot 10, FUN_06d280b0: +0x20.
    const char* GetError() const;

private:
    // Scanner primitives.
    void SkipLine();                          // FUN_06d22ed0
    void SkipWhite();                         // FUN_06d22f4d
    void SetError(const char* Message);       // FUN_06d22f85
    int ReadHexDigit();                       // FUN_06d233dd
    unsigned long ReadNumber();               // FUN_06d234a6
    const char* ReadQuoted();                 // FUN_06d23639
    const char* ReadFlag(FMgsdbRecordSet* Target); // FUN_06d236f4
    unsigned long ReadGuidWord();             // FUN_06d23cc9
    unsigned long ReadGuidWordSwapped();      // FUN_06d23d5f
    bool Match(const char* Keyword) const;    // FUN_06d23d9b
    const char* EvaluateIf();                 // FUN_06d23e0a

    // Passes of Load.
    bool ReadPropertySets();                  // FUN_06d266f2
    bool ReadLeadingApplyToAll();             // FUN_06d26627
    bool ReadVendorDevice();                  // FUN_06d26b14
    bool ReadTrailingApplyToAll();            // FUN_06d26699
    bool ReadBlock(FMgsdbRecordSet* Target);  // FUN_06d27198

    char* Buffer_;                  // +0x04
    const char* Cursor_;            // +0x08
    const char* End_;               // +0x0C
    const char* LineStart_;         // +0x10
    unsigned long Line_;            // +0x14
    FMgsdbRecordSet* Records_;      // +0x18
    bool bError_;                   // +0x1C
    std::string Error_;             // +0x20
    std::string DeviceName_;        // +0x30
    std::string VendorName_;        // +0x40
    unsigned long CpuSpeed_;        // +0x50
    unsigned long Ram_;             // +0x54
    unsigned long VideoRam_;        // +0x58
    FRp9Identifier8 Ident_;         // +0x5C (D3DADAPTER_IDENTIFIER8 shape)
    FRp9Caps8 Caps_;                // +0x488 (D3DCAPS8 shape)
    // +0x55C: propertyset name -> its record set (owned; deleted by the destructor).
    std::map<std::string, FMgsdbRecordSet*> PropertySets_;
    // +0x570: OverallGraphicDetail value -> the record set that set it (not owned).
    std::map<std::string, FMgsdbRecordSet*> DetailSets_;
};

// FUN_06d27cd7 (cdecl): every record of Source is inserted into Target (FUN_06d22851).
void MgsdbReplay(FMgsdbRecordSet* Target, const FMgsdbRecordSet* Source);
