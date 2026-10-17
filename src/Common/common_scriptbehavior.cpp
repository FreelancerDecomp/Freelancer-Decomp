// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll -- ScriptBehavior (the marshalled script-behavior description).
// CMN-SPLIT stage 1 (2026-10-14): split out of common_iface.cpp unchanged.

#include "common_iface.h"
#include "common_misc.h" // malloc_strcpy / malloc_strfree -- ScriptBehavior string slots

#include <cstring>

// ============================================================================
// ScriptBehavior
// ============================================================================

// The string slots (StringArray[i] / StringArray2[i]) own _strdup / MarshalBuffer::get_string
// heap copies that malloc_strfree releases. Only the first IdArrayCount entries of StringArray
// are ever freed (destroy, dtor, operator=, init all loop `i < [this+0x118]`), while all 32
// StringArray2 entries are freed unconditionally. Neither count is clamped to 32 anywhere in
// retail: a count above 32 indexes past the arrays exactly as retail does (cheap literal UB,
// owner ruling 2026-10-08); every producer (init from a description, unmarshal of a
// server-built buffer) keeps them <= 32.

// .text:0632dcb0. The two FmtStr members are zero-constructed inline (.text:0632dccf-0632dd8d,
// the port's inline FmtStr() zeroes the same 0x16 bytes each), then clear() -- which only
// zeroes, never frees, so the uninitialised string slots are never touched.
ScriptBehavior::ScriptBehavior()
{
    clear();                                                            // .text:0632dd98
}

// .text:0632d760. A plain memberwise copy: both pointer arrays are block-copied with REP MOVSD
// (.text:0632d7ba / 0632d7cd / 0632d7ec / 0632d7ff), so the copy SHARES the source's strings;
// only the two FmtStr members are deep-copied through FmtStr's copy ctor (.text:0632d842 /
// 0632d85c). Destroying both the copy and the source would free the shared strings twice --
// retail's own hazard; no retail consumer calls this overload (Server.dll and freelancer.exe
// import only the default ctor, ordinal 417, at 06d12424 / 06d18020 / 00437763 / 0043c8f8).
ScriptBehavior::ScriptBehavior(ScriptBehavior const& Other)
    : NameFmtStr(Other.NameFmtStr)                                      // .text:0632d842
    , DescFmtStr(Other.DescFmtStr)                                      // .text:0632d85c
{
    BehaviorId = Other.BehaviorId;                                      // .text:0632d781
    Field04 = Other.Field04;                                            // .text:0632d786
    Flag08 = Other.Flag08;                                              // .text:0632d78c
    Flag09 = Other.Flag09;                                              // .text:0632d792
    Flag0a = Other.Flag0a;                                              // .text:0632d798
    Field0c = Other.Field0c;                                            // .text:0632d79e
    Field10 = Other.Field10;                                            // .text:0632d7a4
    Flag14 = Other.Flag14;                                              // .text:0632d7aa
    for (int I = 0; I < 32; ++I)                                        // .text:0632d7ba
    {
        IdArray[I] = Other.IdArray[I];
    }
    for (int I = 0; I < 32; ++I)                                        // .text:0632d7cd
    {
        StringArray[I] = Other.StringArray[I];
    }
    IdArrayCount = Other.IdArrayCount;                                  // .text:0632d7d5
    for (int I = 0; I < 32; ++I)                                        // .text:0632d7ec
    {
        StringArray2[I] = Other.StringArray2[I];
    }
    for (int I = 0; I < 32; ++I)                                        // .text:0632d7ff
    {
        StringArray2Dword[I] = Other.StringArray2Dword[I];
    }
    StringArray2Count = Other.StringArray2Count;                        // .text:0632d807
    Field220 = Other.Field220;                                          // .text:0632d813
    Field224 = Other.Field224;                                          // .text:0632d825
    Field228 = Other.Field228;                                          // .text:0632d83c
    Field25c = Other.Field25c;                                          // .text:0632d86d
}

// .text:0632ddb0. The body is destroy() inlined (free StringArray[i] for i < IdArrayCount,
// free all 32 StringArray2[i], FmtStr::destroy on +0x22c then +0x244); the member
// ~FmtStr calls that follow (+0x244 at .text:0632de42, then +0x22c at .text:0632de51) are the
// compiler-generated member destructors here as well.
ScriptBehavior::~ScriptBehavior()
{
    destroy();
}

// .text:0632e370. No self-assignment test: retail releases its own strings and FmtStr first
// (.text:0632e376-0632e3d2, destroy() inlined) and then copies from Other.
ScriptBehavior& ScriptBehavior::operator=(ScriptBehavior const& Other)
{
    destroy();
    BehaviorId = Other.BehaviorId;                                      // .text:0632e3dd
    Field04 = Other.Field04;                                            // .text:0632e3e2
    Flag08 = Other.Flag08;                                              // .text:0632e3e8
    Flag09 = Other.Flag09;                                              // .text:0632e3ee
    Flag0a = Other.Flag0a;                                              // .text:0632e3f4
    Field0c = Other.Field0c;                                            // .text:0632e3fa
    Field10 = Other.Field10;                                            // .text:0632e400
    Flag14 = Other.Flag14;                                              // .text:0632e406

    IdArrayCount = Other.IdArrayCount;                                  // .text:0632e411
    for (int I = 0; I < IdArrayCount; ++I)                              // .text:0632e434-0632e467
    {
        IdArray[I] = Other.IdArray[I];                                  // .text:0632e43b
        malloc_strcpy(&StringArray[I], Other.StringArray[I]);           // .text:0632e448
    }

    StringArray2Count = Other.StringArray2Count;                        // .text:0632e471
    for (int I = 0; I < StringArray2Count; ++I)                         // .text:0632e497-0632e4cd
    {
        malloc_strcpy(&StringArray2[I], Other.StringArray2[I]);         // .text:0632e4a5
        StringArray2Dword[I] = Other.StringArray2Dword[I];              // .text:0632e4ad
    }

    Field220 = Other.Field220;                                          // .text:0632e4d5
    Field224 = Other.Field224;                                          // .text:0632e4e7
    Field228 = Other.Field228;                                          // .text:0632e4fa
    NameFmtStr = Other.NameFmtStr;                                      // .text:0632e500
    DescFmtStr = Other.DescFmtStr;                                      // .text:0632e512
    Field25c = Other.Field25c;                                          // .text:0632e51e
    return *this;
}

// .text:0632de70. Resets every field WITHOUT releasing anything: the string slots are only
// overwritten with 0 (.text:0632dea0 / 0632dec0 `MOV [EAX-0x80],EBX; MOV [EAX],EBX`), so a
// behaviour that still owns strings leaks them -- retail's contract, and what makes clear()
// safe to run from the default ctor on uninitialised slots. The FmtStr members are destroyed
// and then their 0x18 bytes zeroed (REP STOSD x6, .text:0632def7 / 0632df0d).
void ScriptBehavior::clear()
{
    Flag08 = 1;                                                         // .text:0632de78
    Flag0a = 1;                                                         // .text:0632de7b
    Flag14 = 1;                                                         // .text:0632de7e
    BehaviorId = -1;                                                    // .text:0632de82
    Field04 = 0;                                                        // .text:0632de88
    Flag09 = 0;                                                         // .text:0632de8b
    Field0c = 0;                                                        // .text:0632de8e
    Field10 = 0;                                                        // .text:0632de91
    for (int I = 0; I < 32; ++I)                                        // .text:0632dea0-0632dea9
    {
        IdArray[I] = 0;
        StringArray[I] = nullptr;
    }
    IdArrayCount = 0;                                                   // .text:0632deab
    for (int I = 0; I < 32; ++I)                                        // .text:0632dec0-0632dec9
    {
        StringArray2[I] = nullptr;
        StringArray2Dword[I] = 0;
    }
    StringArray2Count = 0;                                              // .text:0632ded3
    Field220 = 0;                                                       // .text:0632ded9
    Field224 = 0;                                                       // .text:0632dedf
    Field228 = 0;                                                       // .text:0632dee5
    NameFmtStr.destroy();                                               // .text:0632deeb
    memset(&NameFmtStr, 0, sizeof(NameFmtStr));                         // .text:0632def7
    DescFmtStr.destroy();                                               // .text:0632df01
    memset(&DescFmtStr, 0, sizeof(DescFmtStr));                         // .text:0632df0d
    Field25c = 0;                                                       // .text:0632df10
}

// .text:0632df20. Releases the owned strings and the FmtStr contents; counts and the other
// fields are left as they are.
void ScriptBehavior::destroy()
{
    for (int I = 0; I < IdArrayCount; ++I)                              // .text:0632df37-0632df4c
    {
        malloc_strfree(&StringArray[I]);                                // .text:0632df38
    }
    for (int I = 0; I < 32; ++I)                                        // .text:0632df60-0632df6d
    {
        malloc_strfree(&StringArray2[I]);                               // .text:0632df61
    }
    NameFmtStr.destroy();                                               // .text:0632df75
    DescFmtStr.destroy();                                               // .text:0632df83 (tail JMP)
}

unsigned long ScriptBehavior::get_key() const
{
    // .text:06261d90 (export 2848, ICF-folded with Rect::leftmost, BaseData::get_base_id and
    // a dozen other getters): `MOV EAX,dword ptr [this]; RET` -- the dword at +0x00, which is
    // BehaviorId. The previous body returned Field04 (+0x04).
    return static_cast<unsigned long>(BehaviorId);
}

// .text:0632e530. Starts with destroy() inlined (.text:0632e535-0632e590: free StringArray[i]
// for i < the OLD IdArrayCount, free all 32 StringArray2[i], FmtStr::destroy x2) -- no clear(),
// no zeroing -- then copies the description. Strings go through malloc_strcpy, which stores
// a null slot for a null source.
void ScriptBehavior::init(CharacterBehaviorDescription const& Desc)
{
    destroy();
    BehaviorId = -1;                                                    // .text:0632e599
    Field04 = static_cast<unsigned>(Desc.Field00);                      // .text:0632e5a1
    Flag08 = Desc.Field04;                                              // .text:0632e5a7
    Flag09 = Desc.Field05;                                              // .text:0632e5ad
    Flag0a = Desc.Field06;                                              // .text:0632e5b3
    Field0c = Desc.Field08;                                             // .text:0632e5b9
    Field10 = Desc.Field0c;                                             // .text:0632e5bf
    Flag14 = Desc.Field10;                                              // .text:0632e5c5

    IdArrayCount = Desc.IdArrayCount;                                   // .text:0632e5d0
    for (int I = 0; I < IdArrayCount; ++I)                              // .text:0632e5f0-0632e619
    {
        IdArray[I] = Desc.IdArray[I];                                   // .text:0632e5f3
        malloc_strcpy(&StringArray[I], Desc.StringArray[I]);            // .text:0632e5fa
    }

    StringArray2Count = Desc.StringArray2Count;                         // .text:0632e623
    for (int I = 0; I < StringArray2Count; ++I)                         // .text:0632e640-0632e66c
    {
        malloc_strcpy(&StringArray2[I], Desc.StringArray2[I]);          // .text:0632e648
        StringArray2Dword[I] = Desc.StringArray2Dword[I];               // .text:0632e656
    }

    Field220 = Desc.Field21c;                                           // .text:0632e674
    Field224 = Desc.Field220;                                           // .text:0632e680
    Field228 = Desc.Field224;                                           // .text:0632e699
    NameFmtStr = Desc.NameFmtStr;                                       // .text:0632e69f
    DescFmtStr = Desc.DescFmtStr;                                       // .text:0632e6b1
    Field25c = Desc.Field258;                                           // .text:0632e6bd
}

// .text:0632df90. Field order on the wire as retail; the bools go through put_bool, which
// normalises to 0/1 itself (.text:06312946 SETNZ).
void ScriptBehavior::marshal(MarshalBuffer* Buf) const
{
    Buf->put_void(&BehaviorId, 4);                                      // .text:0632dfaa
    Buf->put_void(&Field04, 4);                                         // .text:0632dfbf
    Buf->put_bool(Flag08 != 0);                                         // .text:0632dfcc
    Buf->put_bool(Flag09 != 0);                                         // .text:0632dfd9
    Buf->put_bool(Flag0a != 0);                                         // .text:0632dfe6
    Buf->put_void(&Field0c, 4);                                         // .text:0632dffb
    Buf->put_void(&Field10, 4);                                         // .text:0632e010
    Buf->put_bool(Flag14 != 0);                                         // .text:0632e01d
    Buf->put_void(&IdArrayCount, 4);                                    // .text:0632e035
    for (int I = 0; I < IdArrayCount; ++I)                              // .text:0632e050-0632e07b
    {
        Buf->put_void(&IdArray[I], 4);                                  // .text:0632e060
        Buf->put_string(StringArray[I]);                                // .text:0632e06a
    }
    Buf->put_void(&StringArray2Count, 4);                               // .text:0632e090
    for (int I = 0; I < StringArray2Count; ++I)                         // .text:0632e0a7-0632e0d2
    {
        Buf->put_string(StringArray2[I]);                               // .text:0632e0ad
        Buf->put_void(&StringArray2Dword[I], 4);                        // .text:0632e0c1
    }
    Buf->put_void(&Field220, 4);                                        // .text:0632e0e7
    Buf->put_void(&Field224, 4);                                        // .text:0632e0ff
    Buf->put_void(&Field228, 4);                                        // .text:0632e117
    Buf->put_FmtStr(NameFmtStr);                                        // .text:0632e125
    Buf->put_FmtStr(DescFmtStr);                                        // .text:0632e133
    Buf->put_void(&Field25c, 4);                                        // .text:0632e14b
}

// .text:0632e160. No clear(), no clamp: every field is overwritten in place (string slots
// that still owned a string leak, as in retail). Each dword is read through ONE scratch
// slot -- retail reuses its own `param_1` stack cell (.text:0632e182 ... 0632e2b1) -- and then
// stored, so when MarshalBuffer::get_void has too few bytes left and writes nothing, the field
// receives the scratch slot's previous content (initially the MarshalBuffer pointer itself).
// The trailing +0x25c dword goes through a different, never-initialised stack cell (local_28,
// .text:0632e344); the port starts that one at 0 -- the only point where it differs from
// retail, and only on a truncated buffer.
void ScriptBehavior::unmarshal(MarshalBuffer* Buf)
{
    unsigned int Scratch = static_cast<unsigned int>(reinterpret_cast<uintptr_t>(Buf));

    Buf->get_void(&Scratch, 4);                                         // .text:0632e18b
    BehaviorId = static_cast<int>(Scratch);                             // .text:0632e19a
    Buf->get_void(&Scratch, 4);                                         // .text:0632e19f
    Field04 = Scratch;                                                  // .text:0632e1aa
    Flag08 = Buf->get_bool();                                           // .text:0632e1b4
    Flag09 = Buf->get_bool();                                           // .text:0632e1be
    Flag0a = Buf->get_bool();                                           // .text:0632e1cf
    Buf->get_void(&Scratch, 4);                                         // .text:0632e1d2
    Field0c = Scratch;                                                  // .text:0632e1e4
    Buf->get_void(&Scratch, 4);                                         // .text:0632e1e7
    Field10 = Scratch;                                                  // .text:0632e1f0
    Flag14 = Buf->get_bool();                                           // .text:0632e203
    Buf->get_void(&Scratch, 4);                                         // .text:0632e206
    IdArrayCount = static_cast<int>(Scratch);                           // .text:0632e213
    for (int I = 0; I < IdArrayCount; ++I)                              // .text:0632e221-0632e24b
    {
        Buf->get_void(&Scratch, 4);                                     // .text:0632e22a
        IdArray[I] = Scratch;                                           // .text:0632e233
        StringArray[I] = Buf->get_string();                             // .text:0632e23d
    }
    Buf->get_void(&Scratch, 4);                                         // .text:0632e256
    StringArray2Count = static_cast<int>(Scratch);                      // .text:0632e263
    for (int I = 0; I < StringArray2Count; ++I)                         // .text:0632e271-0632e29b
    {
        StringArray2[I] = Buf->get_string();                            // .text:0632e278
        Buf->get_void(&Scratch, 4);                                     // .text:0632e284
        StringArray2Dword[I] = Scratch;                                 // .text:0632e28d
    }
    Buf->get_void(&Scratch, 4);                                         // .text:0632e2a6
    Field220 = Scratch;                                                 // .text:0632e2b8
    Buf->get_void(&Scratch, 4);                                         // .text:0632e2be
    Field224 = Scratch;                                                 // .text:0632e2d0
    Buf->get_void(&Scratch, 4);                                         // .text:0632e2d6
    Field228 = Scratch;                                                 // .text:0632e2e3
    NameFmtStr = Buf->get_FmtStr();                                     // .text:0632e2ec / 0632e300
    DescFmtStr = Buf->get_FmtStr();                                     // .text:0632e31c / 0632e330
    unsigned int TrailingScratch = 0;
    Buf->get_void(&TrailingScratch, 4);                                 // .text:0632e34b
    Field25c = TrailingScratch;                                         // .text:0632e358
}
