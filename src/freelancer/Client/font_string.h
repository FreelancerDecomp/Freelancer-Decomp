// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// FFontString — the 0x34 text holder (Font.cpp) embedded at WinControl+0x7c (ToolTip factory
// LEA ESI+0x7c → FUN_004144b0). It has no vtable and so no RTTI; the port has long called it
// "Font::String", but retail's `.?AUString@Font@@` is the abstract element its children derive
// from (FontStringElement below).
//
// NOT LayoutRichText / RenderDisplayList / TextNode / DeviceTRC (RichTextPane RDL path).
// Children: `.?AUFontString@@` (0x2c) / `.?AUFontNumberString@@` (0x34).
// Measure provider: IFont* at +0x1c (FontImp). See epic:font-string.

#include "font.h"
#include "font_atlas.h"

#include <stddef.h>

// The two-word heap wide string the caption builders fill: text and capacity in wide chars
// (terminator included), the head retail's Font::String and the HUD_Status caption (+0x5d0)
// share, and the low-machine warning text of InitializeEngine (FUN_005b0220). The owner frees
// Text; the builders only grow it (FUN_004142a0 or the same code inline: realloc when there is
// a buffer, else malloc with a zero first char, then Capacity = need).
struct FHudWideText
{
    wchar_t* Text;          // +0x00
    unsigned Capacity;      // +0x04
};

// FUN_004142a0 (thiscall, RET 4) -- grow to Need wide chars when Capacity is smaller: realloc
// an existing buffer (Capacity = Need), else malloc Need * 2 bytes, Text[0] = 0, Capacity = Need.
void WideTextReserve(FHudWideText* Self, unsigned Need);
// FUN_00414280 (thiscall) -- a non-null, non-empty Text gets Text[0] = 0.
void WideTextClear(FHudWideText* Self);
// FUN_00444e20 (thiscall) -- a non-null Text is freed and both words zeroed.
void WideTextFree(FHudWideText* Self);
// FUN_00413df0 (thiscall, RET 8) -- a null Source clears a non-empty Text; otherwise Len
// (0xffffffff: wcslen) characters copied after growing the buffer to Len + 1, and [Len] = 0.
void WideTextAssign(FHudWideText* Self, wchar_t const* Source, unsigned Len);

// Retail `.?AUString@Font@@` -- the abstract element both concrete strings derive from (RTTI:
// FontString's hierarchy DAT_005e7e94 = {FontString, Font::String}; FontNumberString's
// DAT_005e7ef8 = {FontNumberString, Font::NumberString, Font::String}). It has no data and no
// destructor slot: the last Release deletes the object non-virtually (FUN_00414870 ->
// FUN_00414c50, FUN_00414f80 -> FUN_004154f0). Every retail reader of a child slot calls these
// ten slots virtually; the slot's kind byte is read only by FUN_00414640's reuse test.
class FontStringElement
{
public:
    // +0x00 AddRef (LAB_00414860 / LAB_00414f70)
    virtual int AddRef() = 0;
    // +0x04 Release (FUN_00414870 / FUN_00414f80)
    virtual int Release() = 0;
    // +0x08 the font (LAB_004148b0 / FUN_00414fb0)
    virtual void BindFont(IFont* Font) = 0;
    // +0x0c (LAB_004148a0 / LAB_00415070)
    virtual IFont* GetFont() const = 0;
    // +0x10 (LAB_004313f0 / LAB_00415080)
    virtual wchar_t const* GetText() const = 0;
    // +0x14 (LAB_00414920 / LAB_0053e4a0)
    virtual void SetText(wchar_t const* Text) = 0;
    // +0x18 RET 4, returns OutWH (LAB_00414a40 / LAB_00415090)
    virtual int* GetSize(int* OutWH) = 0;
    // +0x1c the shadow colour, then slot 8 with 1 (LAB_00414830 / LAB_004154c0)
    virtual void SetColor(unsigned const* Argb) = 0;
    // +0x20 the shadow flag (FUN_00414850 / FUN_004154e0)
    virtual void SetShadow(unsigned char Value) = 0;
    // +0x24 RET 0xc (LAB_00412e50 / LAB_004137a0)
    virtual void Paint(void* Origin, void* DrawCtx, void* DrawArg) = 0;

protected:
    FontStringElement() = default;
    ~FontStringElement() = default;
};

struct FFontStringChildSlot
{
    FontStringElement* Child; // +0x00
    // +0x04 0 = FontString (0x2c), nonzero = NumberString (0x34). Written only when a slot is
    // appended (FUN_00414640 0x00414729..0x00414745, the copy FUN_00418f10) and read only by
    // FUN_00414640's reuse test; an in-range replacement leaves it as it was (0x0041471b).
    unsigned char Kind;
    unsigned char Pad[3];
};

static_assert(sizeof(FFontStringChildSlot) == 8, "child slot 8 bytes");

// Retail `.?AUFontString@@` — size 0x2c, vtable PTR_LAB_005c8cb8.
class FontStringChild final : public FontStringElement
{
public:
    FontStringChild();
    // FUN_00414c50 -- reached only from Release.
    ~FontStringChild();

    // +0x00 LAB_00414860 AddRef
    int AddRef() override;
    // +0x04 FUN_00414870 Release
    int Release() override;
    // +0x08 LAB_004148b0 — bind IFont* into +0x0c
    void BindFont(IFont* Font) override;
    // +0x0c — return Font_ (misnamed GetConnection in Ghidra)
    IFont* GetFont() const override;
    // +0x10 LAB_004313f0 — return Text_ (+0x04). The NumberString face build reads the glyph
    // string through it (FUN_00414070 0x004141e1, LAB_00415090 0x00415216).
    wchar_t const* GetText() const override;
    // +0x14 LAB_00414920 SetText
    void SetText(wchar_t const* Text) override;
    // +0x18 LAB_00414a40 GetSize → Out[0]=W Out[1]=H (needs IFont::MeasureString); EAX = OutWH.
    int* GetSize(int* OutWH) override;
    // +0x1c LAB_00414830 — Color_ (+0x24) = *Argb, then a tail jump into slot 8 with 1.
    void SetColor(unsigned const* Argb) override;
    // +0x20 FUN_00414850 — bShadow_ (+0x28) = Value.
    void SetShadow(unsigned char Value) override;
    // +0x24 LAB_00412e50 (RET 0xc) — the atlas pages as screen quads. Retail's second and third
    // arguments are floats (left / right trim of the string box); the void* slots carry their bits.
    void Paint(void* Origin, void* DrawCtx, void* DrawArg) override;

    wchar_t* Text_;     // +0x04
    unsigned Capacity_; // +0x08
    IFont* Font_;       // +0x0c
    int RefCount_;      // +0x10
    FFontAtlasPage* GlyphHead_; // +0x14 atlas page grid (FUN_004127d0)
    int CachedW_;       // +0x18
    int CachedH_;       // +0x1c
    int LayoutGen_;     // +0x20  vs G_FontLayoutGeneration
    unsigned Color_;    // +0x24  shadow colour, 0xff080808
    unsigned char bShadow_; // +0x28 draw the shadow quad (Paint 0x00413225)
    unsigned char Pad29_[3];
};

static_assert(sizeof(FontStringChild) == 0x2c, "FontString child 0x2c");

// Retail `.?AUFontNumberString@@` — size 0x34, ctor FUN_00414d10, one vtable of 13 slots at
// PTR_LAB_005c8ce8 (0x005c8d08 is slot 8 of it, not a second vtable). Retail derives it from the
// interface `.?AUNumberString@Font@@` (slots 10..12 below), itself a Font::String; the port folds
// that interface into this class, whose only user is FontStringSetNumberText. The text is a VC6
// vector<char> of glyph indices (0..16 into "-.0123456789MK%/" plus NUL), drawn one quad per
// glyph out of a per-font face atlas (FNumberFace).
class NumberStringChild final : public FontStringElement
{
public:
    NumberStringChild();
    // FUN_004154f0 -- reached only from Release.
    ~NumberStringChild();

    // +0x00 LAB_00414f70 — ++RefCount_.
    int AddRef() override;
    // +0x04 FUN_00414f80 — --RefCount_; at < 1 FUN_004154f0 + operator delete.
    int Release() override;
    // +0x08 FUN_00414fb0 — the face key; a change drops the face.
    void BindFont(IFont* Font) override;
    // +0x0c LAB_00415070 — return Font_ (+0x14).
    IFont* GetFont() const override;
    // +0x10 LAB_00415080 — return DAT_005c8ce0 (the shared empty wide string).
    wchar_t const* GetText() const override;
    // +0x14 LAB_0053e4a0 — RET 4, the text slot does nothing on a NumberString.
    void SetText(wchar_t const* Text) override;
    // +0x18 LAB_00415090 — {Width_ (sum of glyph advances), Height_ (face glyph height)}.
    int* GetSize(int* OutWH) override;
    // +0x1c LAB_004154c0 — Color_ = *Argb, then a tail jump into slot 8 with 1.
    void SetColor(unsigned const* Argb) override;
    // +0x20 FUN_004154e0 — bShadow_ = Value (the ctor calls it with 1).
    void SetShadow(unsigned char Value) override;
    // +0x24 LAB_004137a0 (RET 0xc) — per-glyph quads into the screen-quad batcher.
    void Paint(void* Origin, void* DrawCtx, void* DrawArg) override;
    // +0x28 LAB_00414f00 (RET 0xc) — _snprintf("%%.%df%c") then slot 12 with (double)Value.
    virtual void FormatFloat(float Value, int Decimals, char Suffix);
    // +0x2c LAB_00414f50 (RET 8) — slot 12 with "%d%c" (DAT_005c8d28).
    virtual void FormatInt(int Value, char Suffix);
    // +0x30 LAB_00414db0 (cdecl) — _vsnprintf into 0x20 bytes, then the glyph vector rebuilt
    // through the DAT_00616538 map; LayoutGen_ = 0.
    virtual void Printf(char const* Format, ...);

    unsigned char Alloc04_;      // +0x04 the vector's allocator byte (ctor copies a stack byte)
    unsigned char Pad05_[3];
    unsigned char* GlyphFirst_;  // +0x08 vector<char> begin
    unsigned char* GlyphLast_;   // +0x0c end
    unsigned char* GlyphEnd_;    // +0x10 capacity end
    IFont* Font_;                // +0x14 the face key
    int RefCount_;               // +0x18
    unsigned Color_;             // +0x1c shadow colour, ctor 0xff080808
    unsigned char bShadow_;      // +0x20 draw the shadow quad (FUN_004154e0)
    unsigned char Pad21_[3];
    int LayoutGen_;              // +0x24 vs G_FontLayoutGeneration (GetSize cache)
    int Width_;                  // +0x28 sum of advances
    int Height_;                 // +0x2c face glyph height
    void* FaceCache_;            // +0x30 FUN_00413710 face* (FNumberFace*)
};

static_assert(offsetof(NumberStringChild, GlyphFirst_) == 0x08, "NumberString glyphs +0x08");
static_assert(offsetof(NumberStringChild, Font_) == 0x14, "NumberString key +0x14");
static_assert(offsetof(NumberStringChild, RefCount_) == 0x18, "NumberString RefCount +0x18");
static_assert(offsetof(NumberStringChild, bShadow_) == 0x20, "NumberString shadow +0x20");
static_assert(offsetof(NumberStringChild, LayoutGen_) == 0x24, "NumberString gen +0x24");
static_assert(offsetof(NumberStringChild, FaceCache_) == 0x30, "NumberString Face +0x30");
static_assert(sizeof(NumberStringChild) == 0x34, "NumberString 0x34");

// One glyph's texture rect in the face atlas (FUN_00414070 00414228..00414263).
struct FNumberGlyphUv
{
    float U0; // +0x00 previous glyph's U1 (0 for the first)
    float V0; // +0x04 always 0
    float U1; // +0x08 running advance sum / page width
    float V1; // +0x0c page used height / page height
};
static_assert(sizeof(FNumberGlyphUv) == 0x10, "glyph uv 0x10");

// FUN_00413710 — refcounted 0x164 face keyed by the NumberString's font (+0x14).
struct FNumberFace
{
    FontStringChild* Glyph;   // +0x00 FontString holding "-.0123456789MK%/" (FUN_00417620)
    int LayoutGen;            // +0x04 zeroed by FUN_00413710 (new face) and FUN_00415050 (clear)
    int Advance[17];          // +0x08 IFont slot 10 widths, one per glyph index
    FNumberGlyphUv Uv[17];    // +0x4c
    IFont* FontKey;           // +0x15c
    int RefCount;             // +0x160
};
static_assert(offsetof(FNumberFace, Advance) == 0x08, "face advances +0x08");
static_assert(offsetof(FNumberFace, Uv) == 0x4c, "face uv +0x4c");
static_assert(offsetof(FNumberFace, FontKey) == 0x15c, "face FontKey");
static_assert(offsetof(FNumberFace, RefCount) == 0x160, "face RefCount");
static_assert(sizeof(FNumberFace) == 0x164, "face 0x164");

FNumberFace* NumberFaceGetOrCreate(IFont* FontKey);
void NumberFaceRelease(FNumberFace* Face);
// The walk over DAT_00616524 inlined in FUN_004180c0 (004180d3..00418116): for every face,
// FUN_00415050 (release +0, zero +4). Faces and nodes stay.
void NumberFaceResetAll();

struct FFontString;
void FontStringConstruct(FFontString* Self);
void FontStringDestroy(FFontString* Self);

// Font::String owns its text buffer and its child slots, so it is a RAII holder (AGENTS.md RE
// rule 13): retail constructs it with FUN_004144b0 and tears it down with FUN_00444e50 on both
// the normal and the unwind path of every frame and object that holds one (e.g. the hover
// records' Unwind@ funclets around FUN_005a0530 / FUN_0055bde0, TextWin's member dtor
// 0x005a1b58..0x005a1bb3). The retail class has no copy: every copy site is a hand-written
// member walk (UIFastTable FUN_00590b40).
struct FFontString
{
    // FUN_004144b0
    FFontString() { FontStringConstruct(this); }
    // FUN_00444e50
    ~FFontString() { FontStringDestroy(this); }
    FFontString(FFontString const&) = delete;
    FFontString& operator=(FFontString const&) = delete;

    wchar_t* Text;              // +0x00
    unsigned Capacity;          // +0x04
    int Field08;                // +0x08  sentinel 0x7fff
    unsigned char Field0C;      // +0x0c
    unsigned char Pad0D[3];
    FFontStringChildSlot* ChildBegin; // +0x10
    FFontStringChildSlot* ChildEnd;   // +0x14
    FFontStringChildSlot* ChildCap;   // +0x18
    IFont* FontIface;           // +0x1c
    int ExtentW;                // +0x20
    int ExtentH;                // +0x24
    int LayoutGen;              // +0x28
    int ScaleKey;               // +0x2c
    unsigned char bMultiLine;   // +0x30
    unsigned char Pad31[3];
};

static_assert(offsetof(FFontString, FontIface) == 0x1c, "Font::String +0x1c");
static_assert(sizeof(FFontString) == 0x34, "Font::String size 0x34");

void FontStringClearContents(FFontString* Self);
// FUN_00445dc0 -- Release on every non-null child in [First, Last); the slots stay as they are.
void FontStringChildSlotsDestroy(FFontStringChildSlot* First, FFontStringChildSlot* Last);
// FUN_00445d70 -- the child vector's destructor: children released, buffer freed, zeroed.
void FontStringChildVectorDestroy(FFontString* Self);
// FUN_00444e50 -- Font::String's destructor (~FFontString): FUN_004144f0, the child vector's
// destructor, the text buffer freed. It leaves the string empty, so running it again is a no-op.
void FontStringSetText(FFontString* Self, wchar_t const* Text);
void FontStringSetScale(FFontString* Self, float Param1, float Param2);
void FontStringSetFont(FFontString* Self, IFont* Font); // FUN_004ea670
// FUN_004159a0 (thiscall, RET 8; the first argument is always 0 and never read) — when the
// wide text is non-empty it is freed and every child released (the FUN_004144f0 body, gated);
// then the NumberString child at 0 (FUN_00414640(0, 1)) gets Format through slot 12 (so it
// is a printf format), and ExtentW / ExtentH are rebuilt as max width / summed height over the
// children's slot 6. Sole caller: TextControl FUN_0058a580.
void FontStringSetNumberText(FFontString* Self, char const* Format);
void FontStringRebuild(FFontString* Self);              // FUN_004155c0
int* FontStringGetExtent(FFontString* Self, int* OutWH); // FUN_00414570
// FUN_004145a0 -- the child (line) count after a stale layout is rebuilt; 0 without children.
// Caller: NN_News RefreshRows (0x004a53be), which moves a one-line headline down.
int FontStringChildCount(FFontString* Self);

// FUN_004143c0 thiscall: ECX = draw origin (not Font::String).
// Paint (00412e50) FLD [Origin] / [Origin+4] — both floats.
struct FFontStringDrawOrigin
{
    float X;     // +0x00
    float Y;     // +0x04
    float Scale; // +0x08 box width: ftol -> ScaleKey (004143e7); FUN_004142f0 aligns X in it
    float Height; // +0x0c box height; FUN_004142f0 aligns Y in it
    unsigned Field10; // +0x10 horizontal mode (FUN_004142f0): 1 centre, 2 right, else left
    unsigned Field14; // +0x14 vertical mode: 1 centre, 2 bottom, else top
};

void FontStringDrawChildren(FFontStringDrawOrigin* Origin, FFontString* Self, void* DrawCtx,
                            void* DrawArg);

// FUN_00414640 — find-or-create child at Index (Kind 0 = FontString).
FontStringElement* FontStringEnsureChild(FFontString* Self, unsigned Index, unsigned char Kind);
NumberStringChild* FontStringEnsureNumberChild(FFontString* Self, unsigned Index);

extern int G_FontLayoutGeneration;
