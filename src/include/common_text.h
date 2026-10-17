// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"

// Common.dll TEXT / RDL / geometry cluster.
// Reverse-engineered from EXE/Disasm/Common.dll.c + EXE/Common.map.
// Second pass: all implementable functions complete; remaining stubs marked FULLPORT-DEFERRED.

#include <windows.h>
#include <cstddef>
#include <list>
#include <map>
#include <string>

// Forward declarations
class TextRenderContext;
class DeviceTRC;

// Retail's global enum TextJustify (?set_justify@JustifyNode@@QAEXW4TextJustify@@@Z). Enumerators
// leave no symbols in a binary, so these NAMES are the port's.
// VALUES (2026-09-09, item 2416 / FUN_0632b020, RDLX "JUST" tag handler, Common.dll.txt around
// 0x0632baf4-0x0632bb6a): the "loc" attribute's switch sets the mode byte to 1 for 'C'/'c'
// (center) and 2 for 'R'/'r' (right), 0 otherwise (left/default) -- two independent, directly
// literal samples. This SUPERSEDES the previous Right=1/Center=2 guess, which was based on a
// single ambiguous observation (Freelancer.exe 0x0045ef96 builds a JustifyNode with value 1 --
// consistent with either mapping, since it was never confirmed which alignment that call site
// actually wanted). If a future pass finds evidence 0x0045ef96 specifically needs right-alignment,
// that would directly contradict this finding and both sites should be re-examined together.
enum TextJustify
{
    TextJustifyLeft = 0,
    TextJustifyCenter = 1,
    TextJustifyRight = 2,
    TextJustifyPath = 3   // unconfirmed guess, unchanged
};

// ??0ClipNode@@QAE@ABUtagRECT@@@Z takes a Win32 RECT. Declared rather than included so the
// header stays free of windows.h.
struct tagRECT;
class RenderDisplayList;

// ============================================================================
// Rect — plain struct, 4 ints: left, top, width, height. Size 0x10.
// ============================================================================
struct COMMON_API Rect
{
    // User-declared so that no implicit move members are generated and exported:
    // VC6 had none, and retail exports exactly the copy assignment.
    Rect& operator=(Rect const&) = default;

    int Left;       // +0x00
    int Top;        // +0x04
    int Width;      // +0x08
    int Height;     // +0x0C

    Rect();
    Rect(int InLeft, int InTop, int InWidth, int InHeight);

    void clear();
    void init(int InLeft, int InTop, int InWidth, int InHeight);
    void init_from_ltrb(int InLeft, int InTop, int InRight, int InBottom);

    int rightmost() const;
    int bottommost() const;
    int area() const;
    bool null() const;
    bool nonnull() const;

    int leftmost() const;
    int topmost() const;
    int width() const;
    int height() const;

    void get_center(int* OutX, int* OutY) const;
    bool point_is_inside(int X, int Y) const;
};
static_assert(sizeof(Rect) == 0x10, "Rect must be 0x10");

// RVA 0xBE320 (??8@YA_NABURect@@0@Z). Free-function equality: fields all equal, OR
// both rects are "null" (Width==0 or Height==0) -- two empty rects with different
// leftover Left/Top/dimension bits still compare equal, matching retail exactly.
COMMON_API bool operator==(Rect const& A, Rect const& B);

// RVA 0xBE390 (??9@YA_NABURect@@0@Z). NOT a plain !(A==B): retail's inlined
// "at least one non-empty" check here tests Width>0 && Height>0 (signed JLE/JG),
// while the sibling operator== just above tests Width!=0 && Height!=0 (JZ/JNZ,
// i.e. Rect::null()). Kept as its own faithful copy rather than collapsed to
// !(A==B) -- see common_rect.cpp for the asymmetry this preserves.
COMMON_API bool operator!=(Rect const& A, Rect const& B);

// ============================================================================
// ViewRect -- the same rectangle as edges instead of extents. 4 ints, size 0x10.
// Retail manglings say struct for both (?Rect_to_ViewRect@@YA?AUViewRect@@ABURect@@@Z).
// ============================================================================
struct ViewRect
{
    int Left;       // +0x00
    int Top;        // +0x04
    int Right;      // +0x08  inclusive: Left + Width - 1
    int Bottom;     // +0x0C  inclusive: Top + Height - 1
};
static_assert(sizeof(ViewRect) == 0x10, "ViewRect must be 0x10");

// Common.dll.txt:0631e5f0 / 0631e620. Extents <-> inclusive edges; both are pure arithmetic.
COMMON_API ViewRect __cdecl Rect_to_ViewRect(Rect const& In);
COMMON_API Rect __cdecl ViewRect_to_Rect(ViewRect const& In);
COMMON_API tagRECT __cdecl Rect_to_RECT(Rect const& In);
COMMON_API Rect __cdecl RECT_to_Rect(tagRECT const& In);
COMMON_API Rect __cdecl rect_intersect(Rect const& A, Rect const& B);
COMMON_API Rect __cdecl rect_union(Rect const& A, Rect const& B);

struct FmtStr;
COMMON_API void __cdecl AppendMissionLogData(FmtStr const* Fmt, unsigned char*& Buffer, int& Offset, int MaxSize);
COMMON_API void __cdecl ExtractMissionLogData(FmtStr* Fmt, unsigned char*& Buffer, int& Remaining);
COMMON_API int __cdecl PackMissionLogData(int Type, FmtStr const* Fmt, unsigned char* Buffer, int MaxSize);

class TextRenderContext;
class RenderDisplayList;
// 0x06326600, 828 insns, tracker item 72966 -- the RDLX LAYOUT/line-breaking pass (consumes the tree
// ParseRdlxTag/ParseRdlxEndElement/ParseRdlxCharacters in common_rdlio.cpp build, produces line breaks/positions/
// wrapping/truncation into OutRdl). Ported 2026-09-10 (layoutrichtext-pass11): walks Rdl's node
// list (a documented flat-walk simplification of retail's single-use recursion deque), measuring/
// splitting/wrapping/justifying/positioning each line into OutRdl, with a single-line "truncate
// with an ellipsis" mode gated by Param7. One low-confidence sub-piece (an edge-case list trim when
// the ellipsis itself doesn't fit) and the exact meaning of the boolean return value are flagged as
// documented judgment calls rather than confirmed facts -- see the full trail on the definition in
// common_renderdisplaylist.cpp (dated "layoutrichtext-pass10"/"layoutrichtext-pass11" comment sections).
COMMON_API bool __cdecl LayoutRichText(TextRenderContext& Ctx, RenderDisplayList const& Rdl, int Param3, RenderDisplayList& OutRdl, int Param5, int Param6, bool Param7, int* Param8, bool Param9);
COMMON_API int __cdecl lex_compare(RenderDisplayList const& A, RenderDisplayList const& B, bool Param3);

// ============================================================================
// FmtStr — formatted string with embedded values (linked-list of Val nodes).
// Each node is 0xC: {next*, data_or_subtree, type_tag(ushort)+flags(uchar)}.
//
// Size 0x18, not 0x04. Retail's ??0FmtStr@@QAE@IPBUVal@0@@Z (0630e2d0) zeroes the head, the
// mad-lib id at +0x04 and fourteen bytes at +0x08..+0x15, and ??4FmtStr@@QAEABU0@ABU0@@Z
// (0630dda0) copies exactly those fields one at a time. The fourteen bytes are one counter per
// value kind: every append_* post-increments its own counter and stamps the OLD value into the
// new node's Flags, which is how the formatter later binds each value to its placeholder.
//
// Exported member by member, not with a class-level COMMON_API. Retail exports exactly the
// members marked below and NO default constructor: there is neither ??0FmtStr@@QAE@XZ nor a
// `default constructor closure' ??_FFmtStr@@QAEXXZ in its export table (compare
// ??_FJustifyNode@@QAEXXZ, which a class-level export with a defaulted-argument ctor produces).
// Yet every owner of a FmtStr default-constructs it inline -- MissionVendorProperties'
// ctor (.text:0631a78c-0631a7f5) zeroes Head, MadLibId and the fourteen counters of each
// member in place without calling anything. A class-level export would export an inline
// default ctor too, so the default ctor below is inline and deliberately left unexported.
// ============================================================================
struct FmtStr
{
    struct NavMarker;

    // Node in the FmtStr linked list. Size 0xC. Retail exports no default constructor for it
    // (only the (tag, data, flags), (void const*), (void const*, int&) and copy constructors).
    struct COMMON_API Val
    {
        Val* Next;          // +0x00  next node (or 0)
        // +0x04  the payload; which member is live depends on TypeTag.
        union
        {
            FmtStr* Subtree;    // 0x21: owned nested FmtStr (new / delete)
            NavMarker* Marker;  // 0x4E: owned 0x14-byte block (operator new / operator delete)
            unsigned int Value; // every other tag: the id or number itself; also the raw slot
                                // dword the tag-blind copies move (formerly the Data alias)
        };
        unsigned short TypeTag; // +0x08  0x21=nested FmtStr, 0x4e=data block, etc.
        unsigned char Flags;    // +0x0A

        Val(unsigned short InType, void* InData, unsigned char InFlags);
        // Common.dll.c 150131-150156 (`??0Val@FmtStr@@QAE@PBXAAH@Z` / `...QAE@PBX@Z`) --
        // build directly from a serialized Val (InitFromVoid), optionally reporting the number
        // of bytes consumed. Not currently called anywhere else in the port, but real exported
        // retail overloads, kept for API completeness/future callers.
        Val(void const* Src, int& OutConsumed);
        explicit Val(void const* Src);
        // Common.dll.c 150160-150167 (`??0Val@FmtStr@@QAE@ABU01@@Z`) -- retail defines this as
        // a real (non-implicit) copy ctor that forwards to operator=, which is NOT a memberwise
        // copy for TypeTag 0x21/0x4E (see operator= below). Declaring it explicitly here is
        // required: without it, C++ would silently synthesize a shallow memberwise copy ctor
        // and a Val copy would double-own (and double-free) a nested subtree or data block.
        Val(const Val& Other);
        ~Val();

        // Retail returns a const reference: ??4Val@FmtStr@@QAEABU01@ABU01@@Z.
        Val const& operator=(const Val& Other);
        bool operator==(const Val& Other) const;

        static void* operator new(size_t Size);
        static void operator delete(void* Ptr);

        unsigned int flatten(void* Dest, unsigned int MaxSize) const;
        unsigned int get_flattened_size() const;

    private:
        // Retail: ?InitFromVoid@Val@FmtStr@@AAEHPBX@Z (A = private). Besides the two Val(void
        // const*...) ctors it is called by FmtStr::append_void (.text:0630d8b4) and
        // FmtStr::unflatten (.text:0630e0cd), so FmtStr is a friend.
        friend struct FmtStr;
        int InitFromVoid(const void* Src);
    };

    // The 0x14-byte payload append_nav_marker copies into a node (tag 0x4E). Retail names it
    // FmtStr::NavMarker (?append_nav_marker@FmtStr@@QAEXABUNavMarker@1@@Z) and moves it with a
    // 5-dword REP MOVSD; the individual fields are not needed by anything the port ports.
    struct NavMarker
    {
        unsigned int Data[5];
    };

    // Which counter each append_* uses. The values are the byte offsets minus 0x08, so the
    // array index doubles as retail's field offset. Only KindUnused0D (+0x0D) is genuinely never
    // stamped by any exported append_*; KindUnused13 (+0x13) IS used -- by append_fmt_str, which
    // was previously (wrongly) thought to skip its own counter (Common.dll.c:150717-150718).
    enum ValueKind
    {
        KindNavMarker = 0,      // +0x08
        KindSystem = 1,         // +0x09
        KindBase = 2,           // +0x0A
        KindString = 3,         // +0x0B
        KindGood = 4,           // +0x0C
        KindUnused0D = 5,       // +0x0D
        KindInt = 6,            // +0x0E
        KindRepInstance = 7,    // +0x0F
        KindRepGroup = 8,       // +0x10
        KindZoneId = 9,         // +0x11
        KindSpaceObjId = 10,    // +0x12
        KindUnused13 = 11,      // +0x13
        KindInstallation = 12,  // +0x14
        KindLoot = 13,          // +0x15
        KindCount = 14
    };

    Val* Head;                          // +0x00  head of linked list (or nullptr)
    unsigned int MadLibId;              // +0x04  set by begin_mad_lib / the (id, list) ctor
    unsigned char ValueIndex[KindCount];// +0x08  per-kind running index, stamped into Val::Flags

    // Inline and unexported -- see the note above the struct. Zeroes exactly what retail's
    // inlined default construction zeroes (.text:0631a793-0631a7bf).
    FmtStr()
        : Head(nullptr), MadLibId(0)
    {
        for (int Index = 0; Index < KindCount; ++Index)
        {
            ValueIndex[Index] = 0;
        }
    }
    COMMON_API FmtStr(const FmtStr& Other);
    COMMON_API FmtStr(unsigned int InMadLibId, const Val* ValueList);
    COMMON_API ~FmtStr();

    COMMON_API const FmtStr& operator=(const FmtStr& Other);
    COMMON_API bool operator==(const FmtStr& Other) const;

    COMMON_API void destroy();

    // 06309b30 (folded with a pile of other one-line setters) / 062a8920 (an empty function).
    COMMON_API void begin_mad_lib(unsigned int Id);
    COMMON_API void end_mad_lib();

    // Takes ownership of an already-built node.
    COMMON_API void append(Val* Node);
    COMMON_API void append_fmt_str(const FmtStr& Other);
    COMMON_API void append_nav_marker(const NavMarker& Marker);
    COMMON_API int append_void(void* Data);

    COMMON_API void append_base(const unsigned int& Id);
    COMMON_API void append_good(const unsigned int& Id);
    COMMON_API void append_installation(int Id);
    COMMON_API void append_int(unsigned int Value);
    COMMON_API void append_loot(int Id);
    COMMON_API void append_rep_group(const unsigned int& Id);
    COMMON_API void append_rep_instance(const int& Id);
    COMMON_API void append_spaceobj_id(const unsigned int& Id);
    COMMON_API void append_string(unsigned int Id);
    COMMON_API void append_system(const unsigned int& Id);
    COMMON_API void append_zone_id(const unsigned int& Id);

    COMMON_API int flatten(void* Dest, unsigned int MaxSize) const;
    COMMON_API int unflatten(void* Src, unsigned int Size);
};
static_assert(sizeof(FmtStr) == 0x18, "FmtStr must be 0x18");
static_assert(sizeof(FmtStr::Val) == 0x0C, "FmtStr::Val must be 0x0C");
static_assert(sizeof(FmtStr::NavMarker) == 0x14, "FmtStr::NavMarker must be 0x14");
static_assert(offsetof(FmtStr, MadLibId) == 0x04, "FmtStr mad-lib id at +0x04");
static_assert(offsetof(FmtStr, ValueIndex) == 0x08, "FmtStr counters at +0x08");

// ============================================================================
// VisualSize — output struct for text/image measurement. Size 0x10.
// ============================================================================
struct VisualSize
{
    int Width;      // +0x00
    int Height;     // +0x04
    int Ascent;     // +0x08
    int Descent;    // +0x0C
};
static_assert(sizeof(VisualSize) == 0x10, "VisualSize must be 0x10");

// ============================================================================
// TextRenderAttributes — opaque attribute bundle. Retail copies only first dword.
// ============================================================================
struct TextRenderAttributes
{
    unsigned long Packed;   // +0x00  packed attributes (font/style/color)
};

// LineParser32 and TextFile32 (INI infrastructure) are declared in common_ini.h and
// ported in common_textfile.cpp.

// ============================================================================
// TextRenderContext — base class for text rendering. Has vtable. Size 0x3C.
// Layout from ctor @ RVA 0xc7380.
// ============================================================================
class COMMON_API TextRenderContext
{
public:
    // ImageNode holds a pointer to one of these (PBVImage@TextRenderContext@@). Nothing about
    // the class itself is exported, so the port declares only the name. Retail mangles it
    // as a class (VImage@TextRenderContext@@ in ?destroy_image@DeviceTRC@@UAEXPAVImage@...).
    class Image;

    // Nested types. Retail's State is 0x20 bytes: get_state (.text:06327410) writes +0x00/+0x08/
    // +0x0C and tail-calls get_clip_rect(&+0x10); set_state (.text:06327440) reads the same and
    // tail-calls set_clip_rect(+0x10). push_state builds the same record on its stack. The copy
    // helper FUN_0632d510 copies every dword except +0x04, which nothing ever writes or reads.
    struct State
    {
        unsigned long Attributes;   // +0x00  CurrentAttrs (TRC+0x1C)
        unsigned long Unused04;     // +0x04  formerly Field04: never written or read by retail
        int PosV;                   // +0x08  TRC+0x2C
        int PosH;                   // +0x0C  TRC+0x30
        RECT ClipRect;              // +0x10  get_clip_rect / set_clip_rect
    };

    // Layout recovered from SetStandardFont (Common.dll 0xc6570): +0x00 is a char*, +0x04 a
    // dword, and +0x08..+0x0A three flag bytes. FUN_063277f0, which turns a style into a
    // LOGFONTA, reads exactly these.
    struct FontDesc
    {
        char const* Name;       // +0x00
        unsigned long Size;     // +0x04 — scaled by the RichText screen factor before use
        unsigned char bBold;    // +0x08
        unsigned char bItalic;  // +0x09
        unsigned char bUnderline; // +0x0A
        unsigned char Unused0B; // formerly Pad0B: alignment byte, never read
        // 0x0C bytes: DeviceTRC::static_set_font (.text:06327d50) builds one on a 0x0C-byte
        // stack frame (local_c..local_4) and hands it to the LOGFONT builder FUN_063277f0.
    };

    // Retail exports nothing of Image itself (no ctor, dtor or vftable), so it is not
    // COMMON_API: the only concrete image is DeviceTRC's, created and destroyed inside
    // Common.dll through create_image / destroy_image.
    class Image
    {
    public:
        virtual ~Image() {}

        // ImageNode::GetVisualSize (.text:06329640) reads both straight through the base
        // pointer (FMUL [img+4] / [img+8]); DeviceTRC::BMPImage's ctor (FUN_06327880) fills them.
        float Width;    // +0x04
        float Height;   // +0x08
    };

    virtual ~TextRenderContext();

    TextRenderContext();
    TextRenderContext(const TextRenderContext& Other);
    TextRenderContext& operator=(const TextRenderContext& Other);

    // Attribute management
    const TextRenderAttributes& get_current_attributes();
    const TextRenderAttributes& get_default_attributes();
    void set_current_attributes(const TextRenderAttributes& Attrs);
    void set_default_attributes(const TextRenderAttributes& Attrs);
    void push_default_attributes();
    void pop_default_attributes();
    void clear_default_attributes();

    // Position/origin
    void set_pos(int H, int V);
    void set_origin(int H, int V);
    // Retail: ?get_pos_h@TextRenderContext@@QAEHXZ etc. -- non-const (QAE).
    int get_pos_h();
    int get_pos_v();
    int get_origin_h();
    int get_origin_v();
    void set_pos_h(int H);
    void set_pos_v(int V);
    void set_origin_h(int H);
    void set_origin_v(int V);

    // State stack
    void push_state();
    void pop_state();
    void reset_to_default();

    // Virtual device interface. TextRenderContext's vftable (.rdata:063a3de8) holds the dtor
    // and then eleven _purecall slots (0x06391fb0), and retail exports none of these for the
    // base class, so they are pure here. DeviceTRC (.rdata:063a3e1c) fills them in this order.
    virtual int get_cell_width() const = 0;                                          // +0x04
    virtual int get_cell_height() const = 0;                                         // +0x08
    virtual void get_text_visual_size(const wchar_t* Text, VisualSize& Size, int Len) const = 0; // +0x0C
    virtual void get_font_metrics(int& Ascent, int& Descent) const = 0;              // +0x10
    virtual void get_text_fit(const wchar_t* Text, int MaxWidth, int& FitCount,
                              int* Widths, VisualSize& Size, int Len) const = 0;     // +0x14
    virtual void execute_text(const wchar_t* Text, bool bHighlight, int Len) = 0;    // +0x18
    // Retail ?execute_image@DeviceTRC@@UAEXPBVImage@TextRenderContext@@_N@Z: a pointer and a
    // draw flag. ImageNode::Execute (0x06329620) passes its image and its own bool through.
    virtual void execute_image(const Image* Img, bool bDraw) = 0;                    // +0x1C
    virtual void set_clip_rect(const RECT& ClipRect) = 0;                            // +0x20
    virtual void get_clip_rect(RECT& OutRect) = 0;                                   // +0x24
    virtual Image* create_image(const char* const Filename) = 0;                     // +0x28
    virtual void destroy_image(Image* Img) = 0;                                      // +0x2C

private:
    // State save/restore (used by get_dimensions and layout measurement). Private in retail
    // (?get_state@TextRenderContext@@AAEXAAUState@1@@Z / ?set_state@...@AAEXABUState@1@@Z);
    // besides the class itself only LayoutRichText calls them (.text:06326600 prologue and
    // its two restore paths), so it is a friend.
    friend COMMON_API bool __cdecl LayoutRichText(TextRenderContext& Ctx, RenderDisplayList const& Rdl,
                                                  int Param3, RenderDisplayList& OutRdl, int Param5,
                                                  int Param6, bool Param7, int* Param8, bool Param9);
    void get_state(State& OutState);
    void set_state(const State& InState);

public:
    // Retail holds two VC6 std::lists (allocator byte, head, size): the STATE stack at +0x04
    // (head +0x08 with 0x28-byte nodes = links + a 0x20-byte State, size +0x0C) and the
    // ATTRIBUTE stack at +0x10 (head +0x14 with 0x10-byte nodes, size +0x18). Both are stacks
    // at the FRONT: push_state / push_default_attributes insert before head->_Next and
    // pop_state / pop_default_attributes / get_default_attributes use the front node.
    // LayoutRichText and RenderDisplayList::get_dimensions push onto StateStack directly
    // (FUN_0632c0d0 = push_front). The port holds them as std::lists (No retail ABI); in the
    // Debug CRT a std::list is 12 bytes, which keeps every later field at its retail offset.
    std::list<State> StateStack;                // +0x04
    std::list<TextRenderAttributes> AttrStack;  // +0x10
    TextRenderAttributes CurrentAttrs; // +0x1C  current attributes
    unsigned long Unused20;     // +0x20  formerly Field20: never written or read by any TRC
                                //        function in retail (not by the ctors, copies or dtor)
    TextRenderAttributes DefaultAttrs; // +0x24  default attributes
    unsigned long Unused28;     // +0x28  formerly Field28: never written or read in retail
    int PosV;                   // +0x2C  get_pos_v (0xc7580) / set_pos_v (0xc7590)
    int PosH;                   // +0x30  get_pos_h (0xc75a0) / set_pos_h (0xc75b0)
    int OriginV;                 // +0x34  get_origin_v (0xc75c0) / set_origin_v (0xc75d0)
    int OriginH;                 // +0x38  get_origin_h (0xc75e0) / set_origin_h (0xc75f0)
};

// ============================================================================
// RichText global entry points — Common.dll 0xc6340 / 0xc6410 / 0xc6570 / 0xc65c0.
// E:\FL\Scratch\Source\Common\RichDisplayList.cpp.
// ============================================================================

// 0xc6340. Frees every cached font, sets both screen scale factors from the screen HEIGHT
// (ScaleX = H/768, ScaleY = H/600 — the two reference heights, 1024x768 and 800x600), derives
// the drop-shadow offset as ftol(2 * ScaleX), and records whether the OS major version is
// above 4. Always returns true. The exe passes DAT_00610854, its surface height (005b1f64).
COMMON_API bool __cdecl InitializeRichText(int ScreenHeight);

// 0xc6410. Rebuilds all 32 styles x 8 bold/italic/underline combinations of cached GDI fonts.
COMMON_API void __cdecl ReinitializeRichTextFonts();

// 0xc6570. Registers one of the 32 standard styles. The face name is copied into the
// registry's own 0x80-byte buffer.
COMMON_API void __cdecl SetStandardFont(int StyleIndex, TextRenderContext::FontDesc const& Desc);

// 0xc65c0. Frees every cached font and clears the initialised flag.
COMMON_API void __cdecl ShutdownRichText();

static_assert(sizeof(TextRenderContext) == 0x3C, "TextRenderContext must be 0x3C");

// ============================================================================
// DeviceTRC — concrete TextRenderContext using Win32 GDI. Size 0x50.
// Inherits TextRenderContext, adds HDC and font state at +0x3C..+0x4C.
// ============================================================================
class COMMON_API DeviceTRC : public TextRenderContext
{
public:
    DeviceTRC();
    DeviceTRC(HDC InDC);
    DeviceTRC(const DeviceTRC& Other);
    virtual ~DeviceTRC();

    DeviceTRC& operator=(const DeviceTRC& Other);

    // One cached GDI font: an entry of mFont. 0x7C bytes; mFont is 32 styles x 8
    // bold/italic/underline combinations, row-major, so a font index is (Style << 3) | Combo.
    // The HFONT sits at +0x40 (DAT_064019a8 = mFont + 0x40), the LOGFONTA it was created from
    // at +0x00 and its TEXTMETRICA at +0x44. Nothing of WinFont is exported.
    class WinFont
    {
    public:
        // LAB_063277b0: zeroes LogFont (0x3C bytes) and FontHandle; UnscaledSize and Metrics
        // are left as they are. It and the destructor FUN_063277d0 (DeleteObject(FontHandle)
        // if set, then null it) are the element ctor/dtor of mFont's static array
        // initialisation (LAB_06327750 / LAB_06327780).
        WinFont();
        ~WinFont();

        LOGFONTA LogFont;           // +0x00
        int UnscaledSize;           // +0x3C  FUN_063277f0 stashes the unscaled size here; FILD (signed)
        HFONT FontHandle;           // +0x40
        TEXTMETRICA Metrics;        // +0x44
    };

    // Static methods
    // Retail ?static_set_font@DeviceTRC@@SAXHABUFontDesc@TextRenderContext@@M@Z (.text:06327d50):
    // rebuilds the eight fonts of one style from Desc, scaled by Scale.
    static void __cdecl static_set_font(int Index, const FontDesc& Desc, float Scale);
    static void __cdecl set_image_root(const char* Path);
    static void __cdecl set_drop_shadow_offset(int X, int Y);
    static void __cdecl release_font(int Index);

    // Overrides
    virtual int get_cell_width() const override;
    virtual int get_cell_height() const override;
    virtual void get_text_visual_size(const wchar_t* Text, VisualSize& Size, int Len) const override;
    virtual void get_font_metrics(int& Ascent, int& Descent) const override;
    virtual void get_text_fit(const wchar_t* Text, int MaxWidth, int& FitCount,
                              int* Widths, VisualSize& Size, int Len) const override;
    virtual void execute_text(const wchar_t* Text, bool bHighlight, int Len) override;
    virtual void execute_image(const Image* Img, bool bDraw) override;
    virtual void set_clip_rect(const RECT& NewClip) override;
    virtual void get_clip_rect(RECT& OutRect) override;
    // Retail: ?create_image@DeviceTRC@@UAEPAVImage@TextRenderContext@@QBD@Z -- the pointer
    // parameter itself is const (QBD).
    virtual Image* create_image(const char* const Filename) override;
    virtual void destroy_image(Image* Img) override;

    void bind_to_dc(HDC InDC);

private:
    // The RichText entry points write the shadow offsets and free / rebuild mFont.
    friend COMMON_API bool __cdecl InitializeRichText(int ScreenHeight);
    friend COMMON_API void __cdecl ReinitializeRichTextFonts();
    friend COMMON_API void __cdecl ShutdownRichText();

    // The concrete image create_image returns (RTTI .?AVBMPImage@DeviceTRC@@, vftable
    // .rdata:063a4608); nothing of it is exported, so it is defined in common_textrender.cpp.
    class BMPImage;

    // Private static data, exported: ?mDropShadowX@DeviceTRC@@0HA (.data:063ed670),
    // ?mDropShadowY@DeviceTRC@@0HA (.data:063ed674), ?mFont@DeviceTRC@@0PAVWinFont@1@A
    // (.data:06401968; a static array member mangles as a pointer).
    static int mDropShadowX;
    static int mDropShadowY;
    static WinFont mFont[256];

    HDC MHdc;                   // +0x3C
    // +0x40..+0x4C, formerly Field40..Field4C: the cached clip rectangle in context
    // coordinates. set_clip_rect (.text:063284a1) stores its argument here, get_clip_rect
    // (.text:063284d0) refreshes it from the DC's clip region (zeroed when the DC has none) and
    // returns it; DeviceTRC() zeroes it, DeviceTRC(HDC) leaves it unwritten.
    RECT ClipRect;
};
static_assert(sizeof(DeviceTRC) == 0x50, "DeviceTRC must be 0x50");

// ============================================================================
// RDLNode hierarchy — Render Display List nodes.
// Base class RDLNode has vtable + minimal data.
// All derived nodes share the same base vtable initially, then overwrite.
// ============================================================================

class NullNode;
class TRANode;
class TextNode;
class TextPtrNode;
class AttribStackNode;
class ImageNode;
class PositionNode;
class ParagraphNode;
class JustifyNode;
class ClipNode;
class StyleNode;
class RDLRefNode;

// Base node. Size 0x04 (vtable only).
class COMMON_API RDLNode
{
public:
    virtual ~RDLNode();

    RDLNode();
    RDLNode(const RDLNode& Other);

    // Virtual interface (slots from vtable analysis)
    virtual RDLNode* Clone() const = 0;
    virtual void Execute(TextRenderContext& Ctx, bool bHighlight) = 0;
    // Per-frame Update(float). Pure in RDLNode (.rdata:063a3e54 +0x08 is _purecall 0x06391fb0;
    // retail exports no ?Update@RDLNode). Every concrete node exports its own override
    // (?Update@<Class>@@UAEXM@Z); all but RDLRefNode's are the ICF-folded `RET 4` at
    // 0x062ed730. RDLRefNode (0x06329b70) forwards to its referenced list.
    virtual void Update(float Dt) = 0;
    virtual void GetVisualSize(const TextRenderContext& Ctx, VisualSize& Size) const = 0;
    virtual void GetElementSize(int& OutSize) const = 0;
    virtual void GetElementPos(int Index, TextRenderContext& Ctx, int& OutPos) const = 0;
    // 2026-09-10 SIGNATURE FIX (item 72966 / LayoutRichText pass): retail's real vtable+0x18
    // slot takes 5 arguments, not 4 -- confirmed two independent ways: (1) LayoutRichText's one
    // confirmed call site (Common.dll.txt 0x06326c9b-0x06326cad) pushes 5 args in order
    // (Ctx, MaxSize, 0, &OutLeft, &OutRight); (2) TextNode::SplitAtSize's own mangled retail
    // symbol (?SplitAtSize@TextNode@@UBE_NABVTextRenderContext@@HIAAPAVRDLNode@@1@Z, Common.dll.txt
    // 358218-358249) decodes to exactly (const TextRenderContext&, int, unsigned int, RDLNode*&,
    // RDLNode*&) -- the "I" between "H" and the two "AAPAVRDLNode@@" pointers is this new
    // SplitFlags param. Traced into the shared split helper (retail FUN_06328f20, Common.dll.txt
    // 0x06328f20 on): `TEST byte ptr [ESP+0x44],0x1; JNZ ...` at 0x06328f71/0x06328f76 gates
    // whether the backward word-boundary whitespace scan runs at all -- bit 0 SET skips the scan
    // and splits exactly at the last character that fits; bit 0 CLEAR (the only value the one
    // known call site ever passes) runs the scan, matching this port's existing
    // SplitTextAtSize behavior exactly. Other bits' meaning is unconfirmed -- not guessed at.
    virtual bool SplitAtSize(const TextRenderContext& Ctx, int MaxSize, unsigned int SplitFlags,
                             RDLNode*& OutLeft, RDLNode*& OutRight) const = 0;

    // Retail's node-type query: 24 virtual conversion operators, one non-const/const pair per
    // concrete node class, exported as ??BRDLNode@@UAEPAV<Class>@@XZ / ??BRDLNode@@UBEPBV<Class>@@XZ
    // (ordinals 917-940). All 24 base bodies are one ICF-folded stub, Common.dll.txt 0x06325390
    // `XOR EAX,EAX; RET` (return null). Every subclass overrides exactly its own pair with the
    // ICF-folded `MOV EAX,ECX; RET` at 0x06261260 (return this; ??B<Class>@@UAEPAV0@XZ, ordinals
    // 903-916 and neighbours). RDLNode's vtable (.rdata:063a3e54) is 32 slots: 8 ordinary
    // virtuals, then these 24 from +0x20 to +0x7C. The pair offsets, read off each subclass's own
    // vtable (the slot pair that holds 0x06261260): TextNode +0x20, TRANode +0x28, PositionNode
    // +0x30, ParagraphNode +0x38, JustifyNode +0x40, ClipNode +0x48, StyleNode +0x50, NullNode
    // +0x58, RDLRefNode +0x60, TextPtrNode +0x68, ImageNode +0x70, AttribStackNode +0x78. The
    // non-const operator is the even slot: ParseRdlxCharacters appends into what +0x20 returns.
    // Declared in that order. Callers: BinaryRDLWriter::write_buffer (0x0632a970: +0x28, +0x30,
    // +0x20, +0x60, +0x38, +0x50, +0x68), ParseRdlxCharacters (+0x20), freelancer.exe
    // FUN_0057eb40 (+0x38), LayoutRichText's main loop (+0x38/+0x40/+0x50/+0x60).
    virtual operator TextNode*();
    virtual operator const TextNode*() const;
    virtual operator TRANode*();
    virtual operator const TRANode*() const;
    virtual operator PositionNode*();
    virtual operator const PositionNode*() const;
    virtual operator ParagraphNode*();
    virtual operator const ParagraphNode*() const;
    virtual operator JustifyNode*();
    virtual operator const JustifyNode*() const;
    virtual operator ClipNode*();
    virtual operator const ClipNode*() const;
    virtual operator StyleNode*();
    virtual operator const StyleNode*() const;
    virtual operator NullNode*();
    virtual operator const NullNode*() const;
    virtual operator RDLRefNode*();
    virtual operator const RDLRefNode*() const;
    virtual operator TextPtrNode*();
    virtual operator const TextPtrNode*() const;
    virtual operator ImageNode*();
    virtual operator const ImageNode*() const;
    virtual operator AttribStackNode*();
    virtual operator const AttribStackNode*() const;

protected:
    // Note: MSVC implicitly adds vtable pointer at +0x00 (4 bytes).
    // RDLNode has no additional data members beyond the vtable.
};
static_assert(sizeof(RDLNode) == 0x04, "RDLNode must be 0x04");

// NullNode — empty/no-op node. Size 0x04.
class COMMON_API NullNode : public RDLNode
{
public:
    NullNode();
    NullNode(const NullNode& Other);

    virtual RDLNode* Clone() const override;
    virtual void Execute(TextRenderContext& Ctx, bool bHighlight) override;
    virtual void Update(float Dt) override; // 0x062ed730 (ICF-folded empty `RET 4`)
    virtual void GetVisualSize(const TextRenderContext& Ctx, VisualSize& Size) const override;
    virtual void GetElementSize(int& OutSize) const override;
    virtual void GetElementPos(int Index, TextRenderContext& Ctx, int& OutPos) const override;
    virtual bool SplitAtSize(const TextRenderContext& Ctx, int MaxSize, unsigned int SplitFlags,
                             RDLNode*& OutLeft, RDLNode*& OutRight) const override;
    // Retail ??BNullNode@@UAEPAV0@XZ / ??BNullNode@@UBEPBV0@XZ: both are the ICF-folded
    // `MOV EAX,ECX; RET` at Common.dll.txt 0x06261260 (return this). See RDLNode.
    virtual operator NullNode*() override;
    virtual operator const NullNode*() const override;
};
static_assert(sizeof(NullNode) == 0x04, "NullNode must be 0x04");

// TRANode — text render attributes node. Size 0x14.
class COMMON_API TRANode : public RDLNode
{
public:
    TRANode();
    TRANode(const TextRenderAttributes& InAttrs, unsigned int InMask, unsigned int InDefaultMask);
    TRANode(const TRANode& Other);

    TRANode& operator=(const TRANode& Other);

    void set_attributes(const TextRenderAttributes& InAttrs, unsigned int InMask);

    // Retail accessors, all non-const (QAE), each an ICF-folded one-liner:
    const TextRenderAttributes& get_attributes(); // 0x0632e7a0  LEA EAX,[ECX+0x4]
    unsigned int get_mask();                      // 0x0629fb90  MOV EAX,[ECX+0xC]
    void set_mask(unsigned int InMask);           // 0x06332f40  MOV [ECX+0xC],arg
    unsigned int get_default_mask();              // 0x06263a70  MOV EAX,[ECX+0x10]
    void set_default_mask(unsigned int InMask);   // 0x06332f50  MOV [ECX+0x10],arg

    virtual RDLNode* Clone() const override;
    virtual void Execute(TextRenderContext& Ctx, bool bHighlight) override;
    virtual void Update(float Dt) override; // 0x062ed730 (ICF-folded empty `RET 4`)
    virtual void GetVisualSize(const TextRenderContext& Ctx, VisualSize& Size) const override;
    virtual void GetElementSize(int& OutSize) const override;
    virtual void GetElementPos(int Index, TextRenderContext& Ctx, int& OutPos) const override;
    virtual bool SplitAtSize(const TextRenderContext& Ctx, int MaxSize, unsigned int SplitFlags,
                             RDLNode*& OutLeft, RDLNode*& OutRight) const override;
    // Retail ??BTRANode@@UAEPAV0@XZ / ??BTRANode@@UBEPBV0@XZ: both are the ICF-folded
    // `MOV EAX,ECX; RET` at Common.dll.txt 0x06261260 (return this). See RDLNode.
    virtual operator TRANode*() override;
    virtual operator const TRANode*() const override;
    friend class BinaryRDLWriter; // write_buffer (0x0632a970) reads the fields directly

private:
    TextRenderAttributes Attrs; // +0x04
    unsigned int Unused08;      // +0x08  formerly Field08: no ctor writes it and nothing reads it
                                //        (retail leaves stack garbage; the port zeroes it)
    unsigned int Mask;          // +0x0C  formerly Field0C: get_mask / set_mask (0x0629fb90)
    unsigned int DefaultMask;   // +0x10  formerly Field10: get_default_mask (0x06263a70)
};
static_assert(sizeof(TRANode) == 0x14, "TRANode must be 0x14");

// TextNode — owns a wide string. Retail: vtable + one VC6 basic_string<unsigned short> at +0x04
// (allocator byte, pointer +0x08, length +0x0C, reserve +0x10) = 0x14 bytes, nothing else. The
// port holds the same single member as a std::wstring (modern std type, so the size differs).
class COMMON_API TextNode : public RDLNode
{
public:
    TextNode();
    TextNode(const TextNode& Other);
    // Len < 0 means "the whole string" in both constructors, set_text overloads and the
    // basic_string append_text; the pointer forms measure with wcslen.
    TextNode(const std::wstring& InText, int Len);   // 0x06328770
    TextNode(const wchar_t* InText, int Len);        // 0x063288e0
    ~TextNode();

    TextNode& operator=(const TextNode& Other);

    void set_text(const std::wstring& InText, int Len);    // 0x06328a40
    void set_text(const wchar_t* InText, int Len);         // 0x06328ba0
    void append_text(const wchar_t* InText, int Len);      // 0x06328c60
    void append_text(const std::wstring& InText, int Len); // 0x06328d30
    // Retail ?get_text@TextNode@@QBEABV?$basic_string@G...@std@@XZ (0x0632e7a0, LEA EAX,[ECX+4]).
    const std::wstring& get_text() const;

    virtual RDLNode* Clone() const override;
    virtual void Execute(TextRenderContext& Ctx, bool bHighlight) override;
    virtual void Update(float Dt) override; // 0x062ed730 (ICF-folded empty `RET 4`)
    virtual void GetVisualSize(const TextRenderContext& Ctx, VisualSize& Size) const override;
    virtual void GetElementSize(int& OutSize) const override;
    virtual void GetElementPos(int Index, TextRenderContext& Ctx, int& OutPos) const override;
    virtual bool SplitAtSize(const TextRenderContext& Ctx, int MaxSize, unsigned int SplitFlags,
                             RDLNode*& OutLeft, RDLNode*& OutRight) const override;
    // Retail ??BTextNode@@UAEPAV0@XZ / ??BTextNode@@UBEPBV0@XZ: both are the ICF-folded
    // `MOV EAX,ECX; RET` at Common.dll.txt 0x06261260 (return this). See RDLNode.
    virtual operator TextNode*() override;
    virtual operator const TextNode*() const override;
    friend class BinaryRDLWriter; // write_buffer (0x0632a970) reads the fields directly

private:
    std::wstring Text;          // +0x04
};

// TextPtrNode — references external text (no ownership). Size 0x0C.
class COMMON_API TextPtrNode : public RDLNode
{
public:
    TextPtrNode();
    TextPtrNode(const TextPtrNode& Other);
    TextPtrNode(const wchar_t* Text, int Len);

    void set_text(const wchar_t* Text, int Len);
    // Retail ?get_text@TextPtrNode@@QBEPBGXZ (0x062d1ad0, MOV EAX,[ECX+0x4]) and
    // ?get_text_length@TextPtrNode@@QBEHXZ (0x062a66a0, MOV EAX,[ECX+0x8]); both const.
    const wchar_t* get_text() const;
    int get_text_length() const;

    virtual RDLNode* Clone() const override;
    virtual void Execute(TextRenderContext& Ctx, bool bHighlight) override;
    virtual void Update(float Dt) override; // 0x062ed730 (ICF-folded empty `RET 4`)
    virtual void GetVisualSize(const TextRenderContext& Ctx, VisualSize& Size) const override;
    virtual void GetElementSize(int& OutSize) const override;
    virtual void GetElementPos(int Index, TextRenderContext& Ctx, int& OutPos) const override;
    virtual bool SplitAtSize(const TextRenderContext& Ctx, int MaxSize, unsigned int SplitFlags,
                             RDLNode*& OutLeft, RDLNode*& OutRight) const override;
    // Retail ??BTextPtrNode@@UAEPAV0@XZ / ??BTextPtrNode@@UBEPBV0@XZ: both are the ICF-folded
    // `MOV EAX,ECX; RET` at Common.dll.txt 0x06261260 (return this). See RDLNode.
    virtual operator TextPtrNode*() override;
    virtual operator const TextPtrNode*() const override;
    friend class BinaryRDLWriter; // write_buffer (0x0632a970) reads the fields directly

private:
    // Retail is 0x0C bytes: Clone (.text:06329470) allocates operator new(0xc) and every
    // constructor writes only +0x04 / +0x08.
    const wchar_t* TextPtr;        // +0x04
    int TextLen;                   // +0x08  signed: the ctor tests it with JGE (06329334)
};
static_assert(sizeof(TextPtrNode) == 0x0C, "TextPtrNode must be 0x0C");

// AttribStackNode — pushes/pops attribute state. Size 0x08.
class COMMON_API AttribStackNode : public RDLNode
{
public:
    // Retail Execute() only special-cases 0 (push) and 1 (pop); the default
    // ctor stores the literal 3 (neither push nor pop -- an inert sentinel),
    // so that value is kept nameless here rather than invented.
    // Fixed int underlying type: the field also holds 2 (clear) and 3 (the default ctor's
    // inert value).
    enum Op : int { Op_Push = 0, Op_Pop = 1 };

    AttribStackNode();
    explicit AttribStackNode(Op op);
    AttribStackNode(const AttribStackNode& Other);

    AttribStackNode& operator=(const AttribStackNode& Other);

    // Retail ?get_operation@AttribStackNode@@QAE?AW4Op@1@XZ (0x062d1ad0, MOV EAX,[ECX+0x4]) and
    // ?set_operation@AttribStackNode@@QAEXW4Op@1@@Z (0x06309b30, MOV [ECX+0x4],arg); non-const.
    Op get_operation();
    void set_operation(Op op);

    virtual RDLNode* Clone() const override;
    virtual void Execute(TextRenderContext& Ctx, bool bHighlight) override;
    virtual void Update(float Dt) override; // 0x062ed730 (ICF-folded empty `RET 4`)
    virtual void GetVisualSize(const TextRenderContext& Ctx, VisualSize& Size) const override;
    virtual void GetElementSize(int& OutSize) const override;
    virtual void GetElementPos(int Index, TextRenderContext& Ctx, int& OutPos) const override;
    virtual bool SplitAtSize(const TextRenderContext& Ctx, int MaxSize, unsigned int SplitFlags,
                             RDLNode*& OutLeft, RDLNode*& OutRight) const override;
    // Retail ??BAttribStackNode@@UAEPAV0@XZ / ??BAttribStackNode@@UBEPBV0@XZ: both are the ICF-folded
    // `MOV EAX,ECX; RET` at Common.dll.txt 0x06261260 (return this). See RDLNode.
    virtual operator AttribStackNode*() override;
    virtual operator const AttribStackNode*() const override;

private:
    Op Operation;               // +0x04  formerly Field04: get_operation / set_operation
};
static_assert(sizeof(AttribStackNode) == 0x08, "AttribStackNode must be 0x08");

// ImageNode — displays an image. Size 0x08.
class COMMON_API ImageNode : public RDLNode
{
public:
    ImageNode();
    ImageNode(const ImageNode& Other);
    // 063295f0 stores the argument straight into +0x04: the node holds a POINTER to the image,
    // not an id. Same size either way, but a different type.
    explicit ImageNode(TextRenderContext::Image const* Img);
    virtual ~ImageNode();

    ImageNode& operator=(const ImageNode& Other);

    TextRenderContext::Image const* get_image();          // 0x062d1ad0  MOV EAX,[ECX+0x4]
    void set_image(TextRenderContext::Image const* InImg); // 0x06309b30  MOV [ECX+0x4],arg

    virtual RDLNode* Clone() const override;
    virtual void Execute(TextRenderContext& Ctx, bool bHighlight) override;
    virtual void Update(float Dt) override; // 0x062ed730 (ICF-folded empty `RET 4`)
    virtual void GetVisualSize(const TextRenderContext& Ctx, VisualSize& Size) const override;
    virtual void GetElementSize(int& OutSize) const override;
    virtual void GetElementPos(int Index, TextRenderContext& Ctx, int& OutPos) const override;
    virtual bool SplitAtSize(const TextRenderContext& Ctx, int MaxSize, unsigned int SplitFlags,
                             RDLNode*& OutLeft, RDLNode*& OutRight) const override;
    // Retail ??BImageNode@@UAEPAV0@XZ / ??BImageNode@@UBEPBV0@XZ: both are the ICF-folded
    // `MOV EAX,ECX; RET` at Common.dll.txt 0x06261260 (return this). See RDLNode.
    virtual operator ImageNode*() override;
    virtual operator const ImageNode*() const override;

private:
    TextRenderContext::Image const* Img;  // +0x04
};
static_assert(sizeof(ImageNode) == 0x08, "ImageNode must be 0x08");

// PositionNode — sets cursor position. Size 0x10.
class COMMON_API PositionNode : public RDLNode
{
public:
    PositionNode();
    PositionNode(const PositionNode& Other);

    PositionNode& operator=(const PositionNode& Other);

    // Retail ?set_pos@PositionNode@@QAEXMM@Z (.text:06329720): two floats, stored raw.
    void set_pos(float H, float V);
    // Retail accessors, all non-const (QAE):
    float get_pos_h();              // 0x06261db0  FLD [ECX+0xC]
    float get_pos_v();              // 0x06261da0  FLD [ECX+0x8]
    void set_pos_h(float H);        // 0x06332f40  MOV [ECX+0xC],arg
    void set_pos_v(float V);        // 0x0631d940  MOV [ECX+0x8],arg
    bool is_rel_h();                // 0x06329740  MOV AL,[ECX+0x4]
    bool is_rel_v();                // 0x06329750  MOV AL,[ECX+0x5]
    void set_rel_h(bool bRel);
    void set_rel_v(bool bRel);

    virtual RDLNode* Clone() const override;
    virtual void Execute(TextRenderContext& Ctx, bool bHighlight) override;
    virtual void Update(float Dt) override; // 0x062ed730 (ICF-folded empty `RET 4`)
    virtual void GetVisualSize(const TextRenderContext& Ctx, VisualSize& Size) const override;
    virtual void GetElementSize(int& OutSize) const override;
    virtual void GetElementPos(int Index, TextRenderContext& Ctx, int& OutPos) const override;
    virtual bool SplitAtSize(const TextRenderContext& Ctx, int MaxSize, unsigned int SplitFlags,
                             RDLNode*& OutLeft, RDLNode*& OutRight) const override;
    // Retail ??BPositionNode@@UAEPAV0@XZ / ??BPositionNode@@UBEPBV0@XZ: both are the ICF-folded
    // `MOV EAX,ECX; RET` at Common.dll.txt 0x06261260 (return this). See RDLNode.
    virtual operator PositionNode*() override;
    virtual operator const PositionNode*() const override;
    friend class BinaryRDLWriter; // write_buffer (0x0632a970) reads the fields directly
    friend class BinaryRDLReader; // read_buffer (0x0632a560) writes the fields directly

private:
    // Retail layout (ctor .text:063296f0, copy ctor .text:063259f0, is_rel_h .text:06329740,
    // set_rel_h .text:06329760, set_pos .text:06329720, Execute .text:06329780): the two
    // relative flags are separate bytes, the position is two floats with V before H.
    bool bRelH;                 // +0x04
    bool bRelV;                 // +0x05
    float PosV;                 // +0x08
    float PosH;                 // +0x0C
};
static_assert(sizeof(PositionNode) == 0x10, "PositionNode must be 0x10");

// ParagraphNode — paragraph break. Size 0x04.
class COMMON_API ParagraphNode : public RDLNode
{
public:
    ParagraphNode();
    ParagraphNode(const ParagraphNode& Other);

    virtual RDLNode* Clone() const override;
    virtual void Execute(TextRenderContext& Ctx, bool bHighlight) override;
    virtual void Update(float Dt) override; // 0x062ed730 (ICF-folded empty `RET 4`)
    virtual void GetVisualSize(const TextRenderContext& Ctx, VisualSize& Size) const override;
    virtual void GetElementSize(int& OutSize) const override;
    virtual void GetElementPos(int Index, TextRenderContext& Ctx, int& OutPos) const override;
    virtual bool SplitAtSize(const TextRenderContext& Ctx, int MaxSize, unsigned int SplitFlags,
                             RDLNode*& OutLeft, RDLNode*& OutRight) const override;
    // Retail ??BParagraphNode@@UAEPAV0@XZ / ??BParagraphNode@@UBEPBV0@XZ: both are the ICF-folded
    // `MOV EAX,ECX; RET` at Common.dll.txt 0x06261260 (return this). See RDLNode.
    virtual operator ParagraphNode*() override;
    virtual operator const ParagraphNode*() const override;
};
static_assert(sizeof(ParagraphNode) == 0x04, "ParagraphNode must be 0x04");

// JustifyNode — text justification. Size 0x08.
class COMMON_API JustifyNode : public RDLNode
{
public:
    JustifyNode(const JustifyNode& Other);
    // 063298d0 -- stores the mode at +0x04. Retail's only other constructor: there is no
    // ??0JustifyNode@@QAE@XZ, and the exported `default constructor closure'
    // ??_FJustifyNode@@QAEXXZ (06325af0: PUSH 0; CALL 063298d0) is what a defaulted argument
    // of 0 on this constructor produces -- so the default lives here, not in a separate ctor.
    explicit JustifyNode(TextJustify Mode = TextJustifyLeft);

    JustifyNode& operator=(const JustifyNode& Other);

    // Retail ?get_justify@JustifyNode@@QAE?AW4TextJustify@@XZ (0x062d1ad0, MOV EAX,[ECX+0x4])
    // and ?set_justify@JustifyNode@@QAEXW4TextJustify@@@Z (0x06309b30, MOV [ECX+0x4],arg).
    TextJustify get_justify();
    void set_justify(TextJustify Mode);

    virtual RDLNode* Clone() const override;
    virtual void Execute(TextRenderContext& Ctx, bool bHighlight) override;
    virtual void Update(float Dt) override; // 0x062ed730 (ICF-folded empty `RET 4`)
    virtual void GetVisualSize(const TextRenderContext& Ctx, VisualSize& Size) const override;
    virtual void GetElementSize(int& OutSize) const override;
    virtual void GetElementPos(int Index, TextRenderContext& Ctx, int& OutPos) const override;
    virtual bool SplitAtSize(const TextRenderContext& Ctx, int MaxSize, unsigned int SplitFlags,
                             RDLNode*& OutLeft, RDLNode*& OutRight) const override;
    // Retail ??BJustifyNode@@UAEPAV0@XZ / ??BJustifyNode@@UBEPBV0@XZ: both are the ICF-folded
    // `MOV EAX,ECX; RET` at Common.dll.txt 0x06261260 (return this). See RDLNode.
    virtual operator JustifyNode*() override;
    virtual operator const JustifyNode*() const override;

private:
    TextJustify JustifyMode;    // +0x04
};
static_assert(sizeof(JustifyNode) == 0x08, "JustifyNode must be 0x08");

// ClipNode — clipping rectangle. Size 0x14.
class COMMON_API ClipNode : public RDLNode
{
public:
    ClipNode();
    ClipNode(const ClipNode& Other);
    // 06329940 -- copies the four RECT members into +0x04..+0x10 in order.
    explicit ClipNode(tagRECT const& Rect);

    ClipNode& operator=(const ClipNode& Other);

    // 06329970 -- retail takes a single RECT reference, not four ints.
    void set_rect(tagRECT const& Rect);
    // Retail ?get_rect@ClipNode@@QAEABUtagRECT@@XZ (0x0632e7a0, LEA EAX,[ECX+0x4]): a
    // reference to the RECT stored at +0x04. Non-const.
    const tagRECT& get_rect();

    virtual RDLNode* Clone() const override;
    virtual void Execute(TextRenderContext& Ctx, bool bHighlight) override;
    virtual void Update(float Dt) override; // 0x062ed730 (ICF-folded empty `RET 4`)
    virtual void GetVisualSize(const TextRenderContext& Ctx, VisualSize& Size) const override;
    virtual void GetElementSize(int& OutSize) const override;
    virtual void GetElementPos(int Index, TextRenderContext& Ctx, int& OutPos) const override;
    virtual bool SplitAtSize(const TextRenderContext& Ctx, int MaxSize, unsigned int SplitFlags,
                             RDLNode*& OutLeft, RDLNode*& OutRight) const override;
    // Retail ??BClipNode@@UAEPAV0@XZ / ??BClipNode@@UBEPBV0@XZ: both are the ICF-folded
    // `MOV EAX,ECX; RET` at Common.dll.txt 0x06261260 (return this). See RDLNode.
    virtual operator ClipNode*() override;
    virtual operator const ClipNode*() const override;

private:
    // One RECT (left/top/right/bottom at +0x04..+0x10): get_rect hands out its address.
    tagRECT ClipRect;           // +0x04
};
static_assert(sizeof(ClipNode) == 0x14, "ClipNode must be 0x14");

// StyleNode — references a style by ID. Size 0x08.
class COMMON_API StyleNode : public RDLNode
{
public:
    StyleNode();
    StyleNode(const StyleNode& Other);
    // 06329a10 -- stores the id as a WORD at +0x04.
    explicit StyleNode(unsigned short Id);

    StyleNode& operator=(const StyleNode& Other);

    // Retail ?get_style_id@StyleNode@@QAEGXZ (0x06329a30, MOV AX,[ECX+0x4]) -- non-const.
    unsigned short get_style_id();
    void set_style_id(unsigned short Id);

    virtual RDLNode* Clone() const override;
    virtual void Execute(TextRenderContext& Ctx, bool bHighlight) override;
    virtual void Update(float Dt) override; // 0x062ed730 (ICF-folded empty `RET 4`)
    virtual void GetVisualSize(const TextRenderContext& Ctx, VisualSize& Size) const override;
    virtual void GetElementSize(int& OutSize) const override;
    virtual void GetElementPos(int Index, TextRenderContext& Ctx, int& OutPos) const override;
    virtual bool SplitAtSize(const TextRenderContext& Ctx, int MaxSize, unsigned int SplitFlags,
                             RDLNode*& OutLeft, RDLNode*& OutRight) const override;
    // Retail ??BStyleNode@@UAEPAV0@XZ / ??BStyleNode@@UBEPBV0@XZ: both are the ICF-folded
    // `MOV EAX,ECX; RET` at Common.dll.txt 0x06261260 (return this). See RDLNode.
    virtual operator StyleNode*() override;
    virtual operator const StyleNode*() const override;

private:
    unsigned short StyleId;     // +0x04
    unsigned short Padding;     // +0x06
};
static_assert(sizeof(StyleNode) == 0x08, "StyleNode must be 0x08");

// RDLRefNode — references another RenderDisplayList. Size 0x08.
class COMMON_API RDLRefNode : public RDLNode
{
public:
    RDLRefNode(RenderDisplayList& Ref);
    RDLRefNode(const RDLRefNode& Other);

    // Retail ?get_list@RDLRefNode@@QAEABVRenderDisplayList@@XZ (0x062d1ad0, MOV EAX,[ECX+0x4]):
    // the +0x04 member is a reference, returned as-is. Being a reference, it also makes the
    // implicit copy assignment deleted -- which is why retail exports no ??4RDLRefNode.
    const RenderDisplayList& get_list();

    virtual void Update(float Dt) override;

    virtual RDLNode* Clone() const override;
    virtual void Execute(TextRenderContext& Ctx, bool bHighlight) override;
    virtual void GetVisualSize(const TextRenderContext& Ctx, VisualSize& Size) const override;
    virtual void GetElementSize(int& OutSize) const override;
    virtual void GetElementPos(int Index, TextRenderContext& Ctx, int& OutPos) const override;
    virtual bool SplitAtSize(const TextRenderContext& Ctx, int MaxSize, unsigned int SplitFlags,
                             RDLNode*& OutLeft, RDLNode*& OutRight) const override;
    // Retail ??BRDLRefNode@@UAEPAV0@XZ / ??BRDLRefNode@@UBEPBV0@XZ: both are the ICF-folded
    // `MOV EAX,ECX; RET` at Common.dll.txt 0x06261260 (return this). See RDLNode.
    virtual operator RDLRefNode*() override;
    virtual operator const RDLRefNode*() const override;
    friend class BinaryRDLWriter; // write_buffer (0x0632a970) reads the fields directly

private:
    RenderDisplayList& List;    // +0x04
};
static_assert(sizeof(RDLRefNode) == 0x08, "RDLRefNode must be 0x08");

// ============================================================================
// RenderDisplayList — ordered list of RDLNode pointers. Size 0x0C.
// Linked list at +0x04 (each node 0xC: {prev*, next*, RDLNode*}).
// ============================================================================
// One cell of RenderDisplayList's node list (VC6 std::list<RDLNode*>::_Node, 0x0C bytes). The
// sentinel is a cell too; its Node is never read.
struct FRdlListNode
{
    FRdlListNode* Next;         // +0x00
    FRdlListNode* Prev;         // +0x04
    RDLNode* Node;              // +0x08
};
static_assert(sizeof(FRdlListNode) == 0x0C, "FRdlListNode must be 0x0C");

class COMMON_API RenderDisplayList
{
public:
    RenderDisplayList();
    RenderDisplayList(const RenderDisplayList& Other);
    ~RenderDisplayList();

    RenderDisplayList& operator=(const RenderDisplayList& Other);

    void execute(TextRenderContext& Ctx, bool bHighlight);
    void update(float Dt);
    void get_dimensions(TextRenderContext& Ctx, float& OutWidth, float& OutHeight, float& OutDepth);
    int get_element_count() const;
    bool get_element_pos(TextRenderContext& Ctx, int Index, float& OutH, float& OutV, float& OutDepth);
    // Retail ?optimize@RenderDisplayList@@QAEXXZ is 0x062a8920, a bare RET: an empty body.
    void optimize();

    // Data members accessible for cross-class iteration (RDLRefNode::Update etc.). Retail is a
    // VC6 std::list<RDLNode*>: allocator byte, head sentinel, size. List cells are 0x0C-byte
    // FRdlListNode blocks; every module allocates them with malloc and frees them with free.
    unsigned char Allocator;    // +0x00  formerly `void* Head`: the list's allocator byte, never read
    FRdlListNode* NodeList;     // +0x04  head sentinel (Next = first, Prev = last)
    int Count;                  // +0x08  element count
};
static_assert(sizeof(RenderDisplayList) == 0x0C, "RenderDisplayList must be 0x0C");

// ============================================================================
// StyleCollection -- static style registry. No instance data (all static members).
// Declared after RenderDisplayList because the registry stores the lists by value.
// ============================================================================
class COMMON_API StyleCollection
{
public:
    // User-declared so that no implicit move members are generated and exported:
    // VC6 had none, and retail exports exactly the copy assignment.
    StyleCollection& operator=(StyleCollection const&) = default;

    // Retail's registry value type (V Style@StyleCollection in the mangled mStyles). Nothing of
    // it is exported. It is 0x0C bytes and shaped exactly like a RenderDisplayList: map node
    // payload at +0x10 is what get_style (.text:0632a50f `ADD EAX,0x10`) returns as a
    // RenderDisplayList&, set_style(Rdl) (.text:0632a40a/0632a444) walks its node list at +0x04
    // and count at +0x08, and the map's operator[] (FUN_0632c190) copy-constructs it with the
    // list copy sequence. So the port models it as the list itself.
    class Style : public RenderDisplayList
    {
    };

    static void __cdecl set_style(unsigned short Id, const RenderDisplayList& Rdl);
    static void __cdecl set_style(unsigned short Id, const TextRenderAttributes& Attrs);
    static RenderDisplayList& __cdecl get_style(unsigned short Id);
    // Retail ?remove_style@StyleCollection@@SAXG@Z is 0x062a8920, a bare RET: it never
    // removes anything.
    static void __cdecl remove_style(unsigned short Id);

protected:
    // Retail ?mStyles@StyleCollection@@1V?$map@GVStyle@StyleCollection@@...@std@@A (.data:06401950),
    // a protected static std::map keyed by the 16-bit style id.
    static std::map<unsigned short, Style> mStyles;
};

// ============================================================================
// BinaryRDLReader / BinaryRDLWriter -- binary serialization for RDL (the chat wire format).
// Both are vtable-only (size 0x04), concrete, with no destructor at all: retail exports
// ??0 / ??0(const&) / ??4 for each and nothing else special (Common.dll.txt 0x06325ee0..
// 0x06325f18, 0x0628d500), and neither vtable has a destructor slot.
//
// Stream = back-to-back records {int32 Type; uint32 Length; byte Payload[Length]}:
//   1 TRANode       Length 8: +0 Attrs.Packed, +4 mask (TRANode+0x0C)
//   2 Text          Length 2*n+2: n UTF-16 units and a terminating NUL
//                   (written from TextNode or TextPtrNode, read back as a TextNode)
//   4 PositionNode  Length 9: +0 byte flags (bit0 = +0x04 rel_h, bit1 = +0x05 rel_v),
//                   +1 float = PositionNode+0x08, +5 float = PositionNode+0x0C (unaligned)
//   5 ParagraphNode Length 0
//   6 StyleNode     Length 2: +0 uint16 style id
// An RDLRefNode is written as the records of the list it references, inline.
// ============================================================================
class COMMON_API BinaryRDLReader
{
public:
    BinaryRDLReader();
    BinaryRDLReader(const BinaryRDLReader& Other);

    // ??_7BinaryRDLReader@@6B@ (.rdata:063a4554): [0] read_buffer, [1] read_file,
    // [2] extract_text_from_buffer.
    // 0x0632a560 ?read_buffer@BinaryRDLReader@@UAE_NAAVRenderDisplayList@@PBDI@Z.
    virtual bool read_buffer(RenderDisplayList& Rdl, const char* Buffer, unsigned int Size);
    // 0x0632c0c0 ?read_file@BinaryRDLReader@@UAE_NAAVRenderDisplayList@@PBD@Z -- the shared
    // `XOR AL,AL; RET 8` stub: always false.
    virtual bool read_file(RenderDisplayList& Rdl, const char* Filename);
    // 0x0632a8a0 ?extract_text_from_buffer@BinaryRDLReader@@UAE_NPAGIAAIPBDI@Z: concatenates
    // the text of every Type 2 record. Retail never touches OutSize.
    virtual bool extract_text_from_buffer(wchar_t* Dest, unsigned int MaxSize, unsigned int& OutSize,
                                          const char* Buffer, unsigned int Size);
};
static_assert(sizeof(BinaryRDLReader) == 0x04, "BinaryRDLReader must be 0x04");

class COMMON_API BinaryRDLWriter
{
public:
    BinaryRDLWriter();
    BinaryRDLWriter(const BinaryRDLWriter& Other);

    // ??_7BinaryRDLWriter@@6B@ (.rdata:063a4564, ordinal 974): [0] write_buffer, [1] write_file.
    // 0x0632a970 ?write_buffer@BinaryRDLWriter@@UAE_NABVRenderDisplayList@@PADIAAI@Z. OutSize
    // receives the bytes written -- on failure, the bytes of the records that did fit. Server.dll
    // hands it straight to IServerImpl::SubmitChat (FUN_06d4f570, 0x06d4f8b1 -> 0x06d4f8e6).
    virtual bool write_buffer(const RenderDisplayList& Rdl, char* Dest, unsigned int MaxSize,
                              unsigned int& OutSize);
    // 0x0632c0c0 ?write_file@BinaryRDLWriter@@UAE_NABVRenderDisplayList@@PBD@Z (ordinal 3858) --
    // the shared `XOR AL,AL; RET 8` stub: always false.
    virtual bool write_file(const RenderDisplayList& Rdl, const char* Filename);
};
static_assert(sizeof(BinaryRDLWriter) == 0x04, "BinaryRDLWriter must be 0x04");

// ============================================================================
// XMLReader / XMLWriter — XML serialization for RDL.
// Both are vtable-only (size 0x04).
// ============================================================================
class COMMON_API XMLReader
{
public:
    XMLReader();
    XMLReader(const XMLReader& Other);
    // Retail has no destructor: vftable 063a4538 is {read_buffer, read_file,
    // extract_text_from_buffer} and no ??1XMLReader is exported.

    // Common.dll.c/.txt 0xcac30 (Common.dll.txt:362106-362299). Ported 2026-09-09 (D109, item
    // 73110): same CoCreateInstance/putContentHandler wiring as read_file below, but wraps
    // (Buffer, Size) in a BSTR and calls ISAXXMLReader::parse(VARIANT) instead of parseURL. Full
    // instruction-by-instruction trace and the one deliberately-simplified edge case (retail's
    // OOM path throws a real C++ exception this port doesn't reproduce; treated as an ordinary
    // parse failure instead) are documented on the definition in common_rdlio.cpp.
    virtual bool read_buffer(RenderDisplayList& Rdl, const char* Buffer, unsigned int Size);
    // Common.dll.c/.txt 0xcbf80 (Common.dll.txt:364321-364417). Ported 2026-09-09 (D109, item
    // 13991): CoCreateInstance()s Microsoft's real MSXML2 SAXXMLReader COM object
    // (CLSID_SAXXMLReader), registers a genuine ISAXContentHandler-implementing sink (see
    // RdlxSaxContentHandler in common_rdlio.cpp) via putContentHandler, mbstowcs's Filename, and
    // calls parseURL. NOT a DOM/IPersistStreamInit load as previously assumed -- see the
    // detailed evidence trail on the definition in common_rdlio.cpp.
    virtual bool read_file(RenderDisplayList& Rdl, const char* Filename);
    // Common.dll.c/.txt 0xcc0a0: concrete default — XOR AL,AL; RET (always false). Retail's
    // XMLReader is instantiable; this was wrongly pure in the port and blocked IDS→RDL XML.
    // Retail ?extract_text_from_buffer@XMLReader@@UAE_NPAGIAAIPBDI@Z.
    virtual bool extract_text_from_buffer(wchar_t* Dest, unsigned int MaxSize, unsigned int& OutSize,
                                          const char* Buffer, unsigned int Size);
};
static_assert(sizeof(XMLReader) == 0x04, "XMLReader must be 0x04");

class COMMON_API XMLWriter
{
public:
    XMLWriter();
    XMLWriter(const XMLWriter& Other);
    // Retail has no destructor: vftable 063a4548 is {write_buffer, write_file} and no
    // ??1XMLWriter is exported.

    // Common.dll.c/.txt 0xcc0b0 (?write_buffer@XMLWriter@@UAE_NABVRenderDisplayList@@PADIAAI@Z):
    // retail's mangled signature is (RenderDisplayList const&, char* Dest, unsigned int MaxSize,
    // unsigned int& OutSize) -- an extra by-ref out-size param the old 3-arg declaration was
    // missing -- and the body is a concrete `XOR AL,AL; RET 0x10` (always returns false), i.e.
    // neither slot is pure in retail. Base XMLWriter provides
    // a default "unsupported" implementation; only the mangled interface (not vtable-only size)
    // changes since XMLWriter stays vtable-pointer-only at 0x04 bytes.
    virtual bool write_buffer(const RenderDisplayList& Rdl, char* Dest, unsigned int MaxSize,
                              unsigned int& OutSize);
    // .text:0632c0c0 (XOR AL,AL; RET 8), an ICF stub shared with BinaryRDLWriter::write_file and
    // others: retail XMLWriter::write_file is concrete and always fails.
    virtual bool write_file(const RenderDisplayList& Rdl, const char* Filename);
};
static_assert(sizeof(XMLWriter) == 0x04, "XMLWriter must be 0x04");

// MetaList<GoodDesc> is the real MetaList<T> template (common_metalist.h).
