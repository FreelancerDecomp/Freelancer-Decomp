// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// RDLNode and its twelve node classes (NullNode .. RDLRefNode) -- split out of common_text.cpp
// (retail RichDisplayList.cpp) in CMN-SPLIT phase B.

#include "common_text.h"
#include "common_text_internal.h"
#include "dacom.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <map>
#include <string>
#include <deque>
#include <list>
#include <new>
#include <vector>
#include <windows.h>

// ============================================================================
// RDLNode hierarchy — virtual methods mostly stubbed (rendering logic).
// Clone methods allocate new nodes with copied data.
// ============================================================================

RDLNode::RDLNode()
{
    // MSVC sets vtable pointer automatically
}

RDLNode::RDLNode(const RDLNode& Other)
{
    // MSVC sets vtable pointer automatically; no data members to copy
}

RDLNode::~RDLNode()
{
}

// The 24 node-type queries. Every base body is Common.dll.txt 0x06325390 (ICF-folded,
// ordinals 917-940): `XOR EAX,EAX; RET` -- not this class. Each subclass overrides its own
// pair with 0x06261260 `MOV EAX,ECX; RET` (defined next to that class below).
RDLNode::operator TextNode*() { return nullptr; }
RDLNode::operator const TextNode*() const { return nullptr; }
RDLNode::operator TRANode*() { return nullptr; }
RDLNode::operator const TRANode*() const { return nullptr; }
RDLNode::operator PositionNode*() { return nullptr; }
RDLNode::operator const PositionNode*() const { return nullptr; }
RDLNode::operator ParagraphNode*() { return nullptr; }
RDLNode::operator const ParagraphNode*() const { return nullptr; }
RDLNode::operator JustifyNode*() { return nullptr; }
RDLNode::operator const JustifyNode*() const { return nullptr; }
RDLNode::operator ClipNode*() { return nullptr; }
RDLNode::operator const ClipNode*() const { return nullptr; }
RDLNode::operator StyleNode*() { return nullptr; }
RDLNode::operator const StyleNode*() const { return nullptr; }
RDLNode::operator NullNode*() { return nullptr; }
RDLNode::operator const NullNode*() const { return nullptr; }
RDLNode::operator RDLRefNode*() { return nullptr; }
RDLNode::operator const RDLRefNode*() const { return nullptr; }
RDLNode::operator TextPtrNode*() { return nullptr; }
RDLNode::operator const TextPtrNode*() const { return nullptr; }
RDLNode::operator ImageNode*() { return nullptr; }
RDLNode::operator const ImageNode*() const { return nullptr; }
RDLNode::operator AttribStackNode*() { return nullptr; }
RDLNode::operator const AttribStackNode*() const { return nullptr; }

// Subclass overrides: each is 0x06261260 `MOV EAX,ECX; RET` (return this).
NullNode::operator NullNode*() { return this; }
NullNode::operator const NullNode*() const { return this; }
TRANode::operator TRANode*() { return this; }
TRANode::operator const TRANode*() const { return this; }
TextNode::operator TextNode*() { return this; }
TextNode::operator const TextNode*() const { return this; }
TextPtrNode::operator TextPtrNode*() { return this; }
TextPtrNode::operator const TextPtrNode*() const { return this; }
AttribStackNode::operator AttribStackNode*() { return this; }
AttribStackNode::operator const AttribStackNode*() const { return this; }
ImageNode::operator ImageNode*() { return this; }
ImageNode::operator const ImageNode*() const { return this; }
PositionNode::operator PositionNode*() { return this; }
PositionNode::operator const PositionNode*() const { return this; }
ParagraphNode::operator ParagraphNode*() { return this; }
ParagraphNode::operator const ParagraphNode*() const { return this; }
JustifyNode::operator JustifyNode*() { return this; }
JustifyNode::operator const JustifyNode*() const { return this; }
ClipNode::operator ClipNode*() { return this; }
ClipNode::operator const ClipNode*() const { return this; }
StyleNode::operator StyleNode*() { return this; }
StyleNode::operator const StyleNode*() const { return this; }
RDLRefNode::operator RDLRefNode*() { return this; }
RDLRefNode::operator const RDLRefNode*() const { return this; }

// Update(float): pure in RDLNode; every node but RDLRefNode exports the ICF-folded empty
// `RET 4` at 0x062ed730 as ?Update@<Class>@@UAEXM@Z.
void NullNode::Update(float) {}
void TRANode::Update(float) {}
void TextNode::Update(float) {}
void TextPtrNode::Update(float) {}
void AttribStackNode::Update(float) {}
void ImageNode::Update(float) {}
void PositionNode::Update(float) {}
void ParagraphNode::Update(float) {}
void JustifyNode::Update(float) {}
void ClipNode::Update(float) {}
void StyleNode::Update(float) {}

// --- NullNode ---

NullNode::NullNode() : RDLNode() {}
NullNode::NullNode(const NullNode& Other) : RDLNode(Other) {}

// Every Clone() below is a VC6 new-expression: MSVCRT operator new returns null on failure and
// the constructor is skipped (TEST EAX,EAX / JZ -> return 0) -- `new (std::nothrow)`.
RDLNode* NullNode::Clone() const
{
    // .text:06328630: operator new(4), vftable stamped.
    return new (std::nothrow) NullNode(*this);
}

void NullNode::Execute(TextRenderContext&, bool) {}
void NullNode::GetVisualSize(const TextRenderContext&, VisualSize& Size) const
{
    Size.Width = 0; Size.Height = 0; Size.Ascent = 0; Size.Descent = 0;
}
void NullNode::GetElementSize(int& OutSize) const { OutSize = 0; }
void NullNode::GetElementPos(int, TextRenderContext&, int& OutPos) const { OutPos = 0; }
bool NullNode::SplitAtSize(const TextRenderContext&, int, unsigned int, RDLNode*& OutLeft, RDLNode*& OutRight) const
{
    OutLeft = nullptr; OutRight = nullptr; return false;
}

// --- TRANode ---

// DAT_0640957c — retail's default TRANode attribute mask, same shared global consolidated as
// GDefaultTextAttribsPacked (common_text_internal.h; TextRenderContext ctor). A default-constructed
// TRANode starts with this in Attrs.Packed rather than 0.

TRANode::TRANode() : RDLNode()
{
    // RVA 0xC8650. Unused08 is left uninitialized by retail (never read anywhere); the port
    // zeroes it.
    Attrs.Packed = GDefaultTextAttribsPacked;
    Unused08 = 0;
    Mask = 0;
    DefaultMask = 0;
}

TRANode::TRANode(const TextRenderAttributes& InAttrs, unsigned int InMask, unsigned int InDefaultMask)
    : RDLNode()
{
    // RVA 0xC8670. Mask takes the second argument, Attrs.Packed is InAttrs masked by it (NOT a
    // straight copy), DefaultMask the third; +0x08 is not written (the port zeroes it).
    Mask = InMask;
    Attrs.Packed = InAttrs.Packed & InMask;
    DefaultMask = InDefaultMask;
    Unused08 = 0;
}

TRANode::TRANode(const TRANode& Other) : RDLNode(Other)
{
    // .text:06325490: +0x04, +0x0C and +0x10 copied; +0x08 is not (the port zeroes it).
    Attrs = Other.Attrs;
    Unused08 = 0;
    Mask = Other.Mask;
    DefaultMask = Other.DefaultMask;
}

TRANode& TRANode::operator=(const TRANode& Other)
{
    // .text:063254c0: +0x04, +0x0C, +0x10 copied, no self test.
    Attrs = Other.Attrs;
    Mask = Other.Mask;
    DefaultMask = Other.DefaultMask;
    return *this;
}

void TRANode::set_attributes(const TextRenderAttributes& InAttrs, unsigned int InMask)
{
    // RVA 0xC86A0 — Mask takes the raw mask; Attrs.Packed is InAttrs masked by it
    // (matches the 3-arg constructor's semantics, not a plain field copy).
    Mask = InMask;
    Attrs.Packed = InAttrs.Packed & InMask;
}

// Retail accessors (ICF-folded one-liners): +0x04 attributes, +0x0C mask, +0x10 default mask.
const TextRenderAttributes& TRANode::get_attributes() { return Attrs; }        // 0x0632e7a0
unsigned int TRANode::get_mask() { return Mask; }                              // 0x0629fb90
void TRANode::set_mask(unsigned int InMask) { Mask = InMask; }                 // 0x06332f40
unsigned int TRANode::get_default_mask() { return DefaultMask; }               // 0x06263a70
void TRANode::set_default_mask(unsigned int InMask) { DefaultMask = InMask; }  // 0x06332f50

RDLNode* TRANode::Clone() const
{
    // .text:06328710: the 3-argument constructor inlined, not a field copy -- the clone's
    // attributes are re-masked by the mask (+0x4 = src+0x4 & src+0xc), +0xc / +0x10 copied.
    return new (std::nothrow) TRANode(Attrs, Mask, DefaultMask);
}

void TRANode::Execute(TextRenderContext& Ctx, bool)
{
    // RVA 0xC86C0 — merges this node's masked attributes into the context's current
    // attributes rather than overwriting them outright:
    //   current_src = ctx.get_default_attributes()   (already resolves attr-stack-top vs
    //                                                  DefaultAttrs depending on stack depth)
    //   merged = (Attrs.Packed & ~DefaultMask) | (current_src.Packed & DefaultMask)   --
    //            DefaultMask selects which bits are inherited from the context instead of
    //            this node's own value.
    //   merged &= Mask   -- Mask selects which bits this node actually applies at all.
    //   ctx.CurrentAttrs.Packed = merged | (ctx.CurrentAttrs.Packed & ~Mask)   -- bits
    //            outside Mask are left untouched in the context.
    unsigned long CurrentSrc = Ctx.get_default_attributes().Packed;
    unsigned long Merged = (Attrs.Packed & ~DefaultMask) | (CurrentSrc & DefaultMask);
    Merged &= Mask;
    unsigned long PrevCurrent = Ctx.get_current_attributes().Packed;
    TextRenderAttributes NewAttrs;
    NewAttrs.Packed = Merged | (PrevCurrent & ~Mask);
    Ctx.set_current_attributes(NewAttrs);
}

void TRANode::GetVisualSize(const TextRenderContext&, VisualSize& Size) const
{
    // RVA 0xC9AF0
    Size.Width = 0; Size.Height = 0; Size.Ascent = 0; Size.Descent = 0;
}

void TRANode::GetElementSize(int& OutSize) const
{
    // RVA 0xC8620
    OutSize = 0;
}

void TRANode::GetElementPos(int, TextRenderContext&, int& OutPos) const
{
    // RVA 0xC99B0
    OutPos = 0;
}

// RVA 0xC9810 — ICF-folded with all non-text node SplitAtSize implementations.
// Retail body: *OutLeft = nullptr; *OutRight = nullptr; return false;
bool TRANode::SplitAtSize(const TextRenderContext&, int, unsigned int, RDLNode*& OutLeft, RDLNode*& OutRight) const
{
    OutLeft = nullptr;
    OutRight = nullptr;
    return false;
}

// --- TextNode ---
//
// Retail keeps one VC6 basic_string<unsigned short> at +0x04 (ctor 0x06328750 zeroes the
// allocator byte and the pointer/length/reserve dwords); the port holds a std::wstring. The
// MSVCP60 reference-count, _Tidy/_Grow/_Xran/_Xlen calls in the listings are that string's
// own inlined members; each function below names the std::wstring operation it performs.

TextNode::TextNode() : RDLNode()
{
    // .text:06328750: an empty string.
}

TextNode::TextNode(const TextNode& Other) : RDLNode(Other), Text(Other.Text)
{
    // .text:06325540: string copy-construct.
}

TextNode::TextNode(const std::wstring& InText, int Len) : RDLNode()
{
    // .text:06328770-063288d4: `if (Len < 0) Len = InText.size();` then
    // Text.assign(InText, 0, Len) -- the count is clamped to InText.size() (063287c3..063287cc).
    if (Len < 0)
    {
        Len = (int)InText.size();
    }
    Text.assign(InText, 0, (std::wstring::size_type)Len);
}

TextNode::TextNode(const wchar_t* InText, int Len) : RDLNode()
{
    // .text:063288e0-063289df: `if (Len < 0) Len = wcslen(InText);` then Text.assign(InText, Len).
    if (Len < 0)
    {
        Len = (int)wcslen(InText);
    }
    Text.assign(InText, (std::wstring::size_type)Len);
}

TextNode::~TextNode()
{
    // .text:063289f0: releases the string.
}

TextNode& TextNode::operator=(const TextNode& Other)
{
    // .text:063256c0: string assign.
    Text = Other.Text;
    return *this;
}

void TextNode::set_text(const std::wstring& InText, int Len)
{
    // .text:06328a40-06328b8f: `if (Len < 0) Len = InText.size();` then
    // Text.assign(InText, 0, Len), the count clamped to InText.size().
    if (Len < 0)
    {
        Len = (int)InText.size();
    }
    Text.assign(InText, 0, (std::wstring::size_type)Len);
}

void TextNode::set_text(const wchar_t* InText, int Len)
{
    // .text:06328ba0-06328c55: `if (Len < 0) Len = wcslen(InText);` then Text.assign(InText, Len).
    if (Len < 0)
    {
        Len = (int)wcslen(InText);
    }
    Text.assign(InText, (std::wstring::size_type)Len);
}

void TextNode::append_text(const wchar_t* InText, int Len)
{
    // .text:06328c60-06328d1f: Text.append(InText, Len) with Len taken as unsigned -- 0 is a
    // no-op (JBE at 06328c87), a negative Len fails the length check (_Xlen at 06328c81) the
    // same way std::wstring::append throws length_error.
    Text.append(InText, (std::wstring::size_type)Len);
}

void TextNode::append_text(const std::wstring& InText, int Len)
{
    // .text:06328d30-06328e04: count = min(InText.size(), unsigned Len) (06328d38..06328d48),
    // then append that many characters from the start of InText.
    Text.append(InText, 0, (std::wstring::size_type)Len);
}

const std::wstring& TextNode::get_text() const
{
    // .text:0632e7a0: LEA EAX,[ECX+0x4].
    return Text;
}

RDLNode* TextNode::Clone() const
{
    // .text:06329130: operator new(0x14), then the string copy-construct inlined.
    return new (std::nothrow) TextNode(*this);
}

void TextNode::Execute(TextRenderContext& Ctx, bool bHighlight)
{
    // .text:06328e10: Ctx.vtbl[+0x18] = execute_text(Text.c_str(), bHighlight, Text.size()).
    Ctx.execute_text(Text.c_str(), bHighlight, (int)Text.size());
}

void TextNode::GetVisualSize(const TextRenderContext& Ctx, VisualSize& Size) const
{
    // .text:06328e40: Ctx.vtbl[+0x0C] = get_text_visual_size(Text.c_str(), Size, Text.size()).
    Ctx.get_text_visual_size(Text.c_str(), Size, (int)Text.size());
}

void TextNode::GetElementSize(int& OutSize) const
{
    // .text:06328e70: MOV EAX,[this+0xC] -- the string length.
    OutSize = (int)Text.size();
}

void TextNode::GetElementPos(int Index, TextRenderContext& Ctx, int& OutPos) const
{
    // .text:06328e80-06328edc: the bound comes from this node's own virtual GetElementSize
    // (vtbl+0x10); for 0 <= Index < count, measure the first Index characters and return the
    // width (the first dword of the VisualSize); otherwise 0.
    int Count = 0;
    GetElementSize(Count);
    if (Index >= 0 && Index < Count)
    {
        VisualSize Size;
        Ctx.get_text_visual_size(Text.c_str(), Size, Index);
        OutPos = Size.Width;
    }
    else
    {
        OutPos = 0;
    }
}

// Helper shared by TextNode::SplitAtSize and TextPtrNode::SplitAtSize.
// Replicates FUN_06328f20: calls get_text_fit (vtable+0x14) to measure how many
// characters fit within MaxWidth, then (unless SplitFlags bit 0 is set) backward-scans
// for whitespace to find a word-boundary split point. Creates two new TextNodes
// (left = [0..split], right = [split..end]). Returns true if split succeeded.
//
// SplitFlags -- added 2026-09-10 (item 72966 / LayoutRichText pass) after confirming
// SplitAtSize's real retail signature carries a 5th (uint) argument. Traced directly
// into FUN_06328f20's body: `TEST byte ptr [ESP+0x44],0x1; JNZ LAB_06328fa4` at
// Common.dll.txt 0x06328f71/0x06328f76 skips the whole backward whitespace-scan block
// (0x06328f78-0x06328f9e) when bit 0 of this flag is set, using the raw get_text_fit
// count as the split position unmodified. Bit 0 clear (the only value LayoutRichText's
// one confirmed call site at 0x06326cad ever passes -- a literal 0) runs the scan, i.e.
// this port's pre-existing behavior below is exactly the bit-0-clear path. Other bits
// of SplitFlags are unconfirmed -- not guessed at, not read here.
//
// 2026-10-08 (CMN-TEXT) full re-trace of FUN_06328f20 (.text:06328f20-06329126, cdecl, 7 args
// (Text, Len, Ctx, MaxSize, Flags, OutLeft*, OutRight*)):
//   Fit = 0; Ctx.get_text_fit(Text, MaxSize, Fit, 0, <stack VisualSize>, Len) (vtbl+0x14);
//   Fit >= Len (signed) -> both outputs 0, false;
//   if !(Flags & 1): i = Fit; while i > 0 && !iswspace(Text[i-1]): --i; Fit = i -- so the left
//     part keeps the whitespace, and a run without whitespace leaves Fit == 0;
//   Fit == 0 -> both outputs 0, false;
//   Left = new TextNode(Text, Fit); Right = new TextNode(Text + Fit, Len - Fit) (both inlined:
//     a negative length means wcslen); *OutLeft = Left, *OutRight = Right, true.
// The previous port split at the raw fit count when no whitespace was found (retail fails the
// split instead) and returned early on an empty / null text without calling get_text_fit.
static bool SplitTextAtSize(const wchar_t* Text, int TextLen,
                            const TextRenderContext& Ctx, int MaxWidth,
                            unsigned int SplitFlags,
                            RDLNode*& OutLeft, RDLNode*& OutRight)
{
    int FitCount = 0;
    VisualSize FitSize;
    Ctx.get_text_fit(Text, MaxWidth, FitCount, nullptr, FitSize, TextLen);

    if (FitCount >= TextLen)
    {
        OutRight = nullptr;
        OutLeft = nullptr;
        return false;
    }

    if ((SplitFlags & 1u) == 0)
    {
        int SplitPos = FitCount;
        while (SplitPos > 0)
        {
            if (iswspace((wint_t)Text[SplitPos - 1]) != 0)
            {
                break;
            }
            --SplitPos;
        }
        FitCount = SplitPos;
    }

    if (FitCount == 0)
    {
        OutRight = nullptr;
        OutLeft = nullptr;
        return false;
    }

    // Each allocation may come back null (06328fbc / 06329067 skip the constructor); the split
    // still reports success with whatever pointers it got.
    TextNode* const LeftNode = new (std::nothrow) TextNode(Text, FitCount);
    TextNode* const RightNode = new (std::nothrow) TextNode(Text + FitCount, TextLen - FitCount);
    OutLeft = LeftNode;
    OutRight = RightNode;
    return true;
}

bool TextNode::SplitAtSize(const TextRenderContext& Ctx, int MaxSize, unsigned int SplitFlags,
                            RDLNode*& OutLeft, RDLNode*& OutRight) const
{
    // RVA 0xC8EE0 — a pure forwarding thunk in retail (Common.dll.txt 0x06328ee0-0x06328f13):
    // pushes this + all 5 args unmodified into FUN_06328f20 (SplitTextAtSize below).
    return SplitTextAtSize(Text.c_str(), (int)Text.size(),
                           Ctx, MaxSize, SplitFlags, OutLeft, OutRight);
}

// --- TextPtrNode ---

TextPtrNode::TextPtrNode() : RDLNode()
{
    // .text:06329300: +0x04 / +0x08 = 0.
    TextPtr = nullptr;
    TextLen = 0;
}

TextPtrNode::TextPtrNode(const TextPtrNode& Other) : RDLNode(Other)
{
    // .text:06325890: +0x04 / +0x08 copied.
    TextPtr = Other.TextPtr;
    TextLen = Other.TextLen;
}

TextPtrNode::TextPtrNode(const wchar_t* Text, int Len) : RDLNode()
{
    // RVA 0xC9320 -- BUG FIX: retail clamps a negative Len to wcslen(Text),
    // it does not store it verbatim (same pattern as TextNode::set_text).
    if (Len < 0)
    {
        Len = (int)wcslen(Text);
    }
    TextPtr = Text;
    TextLen = Len;
}

void TextPtrNode::set_text(const wchar_t* Text, int Len)
{
    // RVA 0xC9350 -- BUG FIX: same negative-Len-means-wcslen clamp as retail.
    if (Len < 0)
    {
        Len = (int)wcslen(Text);
    }
    TextPtr = Text;
    TextLen = Len;
}

const wchar_t* TextPtrNode::get_text() const
{
    // 0x062d1ad0 (ICF-folded): MOV EAX,[ECX+0x4]; RET.
    return TextPtr;
}

int TextPtrNode::get_text_length() const
{
    // 0x062a66a0 (ICF-folded): MOV EAX,[ECX+0x8]; RET.
    return TextLen;
}

RDLNode* TextPtrNode::Clone() const
{
    // .text:06329470: the (Text, Len) constructor inlined, not the copy constructor -- a
    // negative length is replaced by wcslen.
    return new (std::nothrow) TextPtrNode(TextPtr, TextLen);
}

void TextPtrNode::Execute(TextRenderContext& Ctx, bool bHighlight)
{
    // RVA 0xC9390
    Ctx.execute_text(TextPtr, bHighlight, TextLen);
}

void TextPtrNode::GetVisualSize(const TextRenderContext& Ctx, VisualSize& Size) const
{
    // RVA 0xC93B0
    Ctx.get_text_visual_size(TextPtr, Size, TextLen);
}

void TextPtrNode::GetElementSize(int& OutSize) const
{
    // Common.dll.c 0xc93d0: *param_1 = *(this+0x8) (TextLen).
    OutSize = TextLen;
}

void TextPtrNode::GetElementPos(int Index, TextRenderContext& Ctx, int& OutPos) const
{
    // .text:063293e0: Count = this->GetElementSize() (vtbl+0x10, TextLen); out of [0, Count)
    // writes 0, otherwise Ctx vtbl+0xc (TextPtr, &Size, Index) and Width. No null test of TextPtr.
    int Count = 0;
    GetElementSize(Count);
    if (Index >= 0 && Index < Count)
    {
        VisualSize Size;
        Ctx.get_text_visual_size(TextPtr, Size, Index);
        OutPos = Size.Width;
    }
    else
    {
        OutPos = 0;
    }
}

bool TextPtrNode::SplitAtSize(const TextRenderContext& Ctx, int MaxSize, unsigned int SplitFlags,
                               RDLNode*& OutLeft, RDLNode*& OutRight) const
{
    // .text:06329440: forwards ([this+4], [this+8], Ctx, MaxSize, Flags, OutLeft, OutRight) to
    // FUN_06328f20 (SplitTextAtSize).
    return SplitTextAtSize(TextPtr, TextLen, Ctx, MaxSize, SplitFlags,
                           OutLeft, OutRight);
}

// --- AttribStackNode ---

// Common.dll.c 0xc94b0: retail default-constructs the operation to 3 (neither push nor pop),
// not 0.
AttribStackNode::AttribStackNode() : RDLNode() { Operation = (Op)3; }

// Common.dll.c 0xc94c0: stores the caller-supplied Op verbatim (no validation/clamping).
AttribStackNode::AttribStackNode(Op op) : RDLNode() { Operation = op; }

AttribStackNode::AttribStackNode(const AttribStackNode& Other) : RDLNode(Other)
{
    Operation = Other.Operation;
}

AttribStackNode& AttribStackNode::operator=(const AttribStackNode& Other)
{
    // .text:06325980: +0x04 copied, no self test.
    Operation = Other.Operation;
    return *this;
}

AttribStackNode::Op AttribStackNode::get_operation()
{
    // 0x062d1ad0 (ICF-folded): MOV EAX,[ECX+0x4]; RET.
    return Operation;
}

void AttribStackNode::set_operation(Op op)
{
    // 0x06309b30 (ICF-folded): MOV EAX,[ESP+4]; MOV [ECX+0x4],EAX; RET 4.
    Operation = op;
}

RDLNode* AttribStackNode::Clone() const { return new (std::nothrow) AttribStackNode(*this); } // .text:063295b0

void AttribStackNode::Execute(TextRenderContext& Ctx, bool)
{
    // .text:063294e0: switch on the operation, each case the inlined body of the matching
    // TextRenderContext attribute-stack call. 0 = push_front(CurrentAttrs) (LAB_06329561),
    // 1 = pop_front (LAB_06329533), 2 = erase every node (06329506 loop); anything else nothing.
    switch ((int)Operation)
    {
    case 0:
        Ctx.push_default_attributes();
        break;
    case 1:
        Ctx.pop_default_attributes();
        break;
    case 2:
        Ctx.clear_default_attributes();
        break;
    default:
        break;
    }
}

void AttribStackNode::GetVisualSize(const TextRenderContext&, VisualSize& Size) const
{
    Size.Width = 0; Size.Height = 0; Size.Ascent = 0; Size.Descent = 0;
}
void AttribStackNode::GetElementSize(int& OutSize) const { OutSize = 0; }
void AttribStackNode::GetElementPos(int, TextRenderContext&, int& OutPos) const { OutPos = 0; }
bool AttribStackNode::SplitAtSize(const TextRenderContext&, int, unsigned int, RDLNode*& L, RDLNode*& R) const
{
    L = nullptr; R = nullptr; return false;
}

// --- ImageNode ---

ImageNode::ImageNode() : RDLNode() { Img = nullptr; }
ImageNode::ImageNode(const ImageNode& Other) : RDLNode(Other) { Img = Other.Img; }

// RVA 0xC95F0 — the pointer goes straight into +0x04, no lookup.
ImageNode::ImageNode(TextRenderContext::Image const* InImg) : RDLNode() { Img = InImg; }

// Common.dll.c 0xc9610: clears Img (non-owning pointer -- not deleted) before chaining
// to ~RDLNode(). Functionally equivalent to the implicit default dtor this class had
// before, since nothing reads Img after this point either way; added explicitly to
// match retail 1:1.
ImageNode::~ImageNode()
{
    Img = nullptr;
}

ImageNode& ImageNode::operator=(const ImageNode& Other)
{
    // .text:06325980 (ICF-shared with AttribStackNode/JustifyNode::operator=): +0x04 copied,
    // no self test.
    Img = Other.Img;
    return *this;
}

RDLNode* ImageNode::Clone() const { return new (std::nothrow) ImageNode(*this); } // .text:063296c0

void ImageNode::Execute(TextRenderContext& Ctx, bool bHighlight)
{
    // .text:06329620: Ctx.vtbl[+0x1C](Img, bHighlight) -- the image pointer and this call's
    // flag go straight through, no null check.
    Ctx.execute_image(Img, bHighlight);
}

TextRenderContext::Image const* ImageNode::get_image() { return Img; }         // 0x062d1ad0
void ImageNode::set_image(TextRenderContext::Image const* InImg) { Img = InImg; } // 0x06309b30

void ImageNode::GetVisualSize(const TextRenderContext&, VisualSize& Size) const
{
    // .text:06329640: no image -> all four zero. Otherwise Width = ftol(DAT_063ed66c (ScaleY)
    // * Img->Width), then Height = Ascent = ftol(ScaleY * Img->Height), Descent = 0.
    if (Img == nullptr)
    {
        Size.Height = 0;
        Size.Width = 0;
        Size.Ascent = 0;
        Size.Descent = 0;
        return;
    }
    Size.Width = (int)((double)GRichTextScaleY * (double)Img->Width);
    const int ScaledHeight = (int)((double)GRichTextScaleY * (double)Img->Height);
    Size.Descent = 0;
    Size.Ascent = ScaledHeight;
    Size.Height = ScaledHeight;
}

void ImageNode::GetElementSize(int& OutSize) const
{
    // RVA 0xC96A0: OutSize = (Img != nullptr) ? 1 : 0.
    OutSize = (Img != nullptr) ? 1 : 0;
}

void ImageNode::GetElementPos(int, TextRenderContext&, int& OutPos) const { OutPos = 0; }
bool ImageNode::SplitAtSize(const TextRenderContext&, int, unsigned int, RDLNode*& L, RDLNode*& R) const
{
    L = nullptr; R = nullptr; return false;
}

// --- PositionNode ---

PositionNode::PositionNode() : RDLNode()
{
    // .text:063296f0: both flag bytes and both float dwords zeroed.
    bRelH = false; bRelV = false; PosV = 0.0f; PosH = 0.0f;
}

PositionNode::PositionNode(const PositionNode& Other) : RDLNode(Other)
{
    // RVA 0xC59F0
    bRelH = Other.bRelH;
    bRelV = Other.bRelV;
    PosV = Other.PosV;
    PosH = Other.PosH;
}

PositionNode& PositionNode::operator=(const PositionNode& Other)
{
    // .text:06325a20: bytes +4/+5 then dwords +8/+0xC copied, no self test.
    bRelH = Other.bRelH;
    bRelV = Other.bRelV;
    PosV = Other.PosV;
    PosH = Other.PosH;
    return *this;
}

void PositionNode::set_pos(float H, float V)
{
    // RVA 0xC9720: [this+0xc] = H, [this+0x8] = V (raw dword moves).
    PosH = H;
    PosV = V;
}

bool PositionNode::is_rel_h() { return bRelH; }         // 0x06329740
bool PositionNode::is_rel_v() { return bRelV; }         // 0x06329750
float PositionNode::get_pos_h() { return PosH; }        // 0x06261db0
float PositionNode::get_pos_v() { return PosV; }        // 0x06261da0
void PositionNode::set_pos_h(float H) { PosH = H; }     // 0x06332f40
void PositionNode::set_pos_v(float V) { PosV = V; }     // 0x0631d940
void PositionNode::set_rel_h(bool bRel) { bRelH = bRel; }
void PositionNode::set_rel_v(bool bRel) { bRelV = bRel; }

RDLNode* PositionNode::Clone() const
{
    // .text:06329830: the default constructor (all four fields zeroed) inside the null-checked
    // new-expression, then the four fields copied UNCONDITIONALLY -- a failed allocation writes
    // through null (deterministic fault, reproduced). The floats go through FLD/FSTP: PosV is
    // loaded first, PosH stored first.
    PositionNode* const NewNode = new (std::nothrow) PositionNode();
    NewNode->bRelH = bRelH;
    NewNode->bRelV = bRelV;
    NewNode->PosH = PosH;
    NewNode->PosV = PosV;
    return NewNode;
}

void PositionNode::Execute(TextRenderContext& Ctx, bool)
{
    // RVA 0xC9780: FLD PosH, FIADD Ctx.PosH when bRelH; FLD PosV, FIADD Ctx.PosV when bRelV;
    // then two ftol (truncation) stores, V into Ctx+0x2c first, H into Ctx+0x30. The sums
    // are formed in double so an int32 context position is added without float rounding,
    // as the x87 stack does.
    double H = PosH;
    if (bRelH)
        H += (double)Ctx.get_pos_h();
    double V = PosV;
    if (bRelV)
        V += (double)Ctx.get_pos_v();
    Ctx.set_pos_v((int)V);
    Ctx.set_pos_h((int)H);
}

void PositionNode::GetVisualSize(const TextRenderContext&, VisualSize& Size) const
{
    // .text:063297c0: all four zeroed, then Width = ftol(PosH) when bRelH and
    // Height = ftol(PosV) when bRelV (truncation toward zero).
    Size.Width = 0; Size.Height = 0; Size.Ascent = 0; Size.Descent = 0;
    if (bRelH)
    {
        Size.Width = (int)PosH;
    }
    if (bRelV)
    {
        Size.Height = (int)PosV;
    }
}

void PositionNode::GetElementSize(int& OutSize) const { OutSize = 0; }
void PositionNode::GetElementPos(int, TextRenderContext&, int& OutPos) const { OutPos = 0; }
bool PositionNode::SplitAtSize(const TextRenderContext&, int, unsigned int, RDLNode*& L, RDLNode*& R) const
{
    L = nullptr; R = nullptr; return false;
}

// --- ParagraphNode ---

// Common.dll.c 0xc5ab0
ParagraphNode::ParagraphNode() : RDLNode() {}
ParagraphNode::ParagraphNode(const ParagraphNode& Other) : RDLNode(Other) {}

RDLNode* ParagraphNode::Clone() const { return new (std::nothrow) ParagraphNode(*this); } // .text:063298b0

void ParagraphNode::Execute(TextRenderContext&, bool)
{
    // Vtable slot +0x4 of ??_7ParagraphNode@@6B@ (.rdata:063a4298) is the ICF-shared `RET 8`
    // at 0x0628f050.
}

void ParagraphNode::GetVisualSize(const TextRenderContext& Ctx, VisualSize& Size) const
{
    // .text:06329880: a line break measures as an empty run one text line tall -- Width = 0,
    // Height = Ctx vtbl+0x08 (get_cell_height), then Ctx vtbl+0x10 (get_font_metrics) writes
    // Ascent and Descent.
    Size.Width = 0;
    Size.Height = Ctx.get_cell_height();
    Ctx.get_font_metrics(Size.Ascent, Size.Descent);
}

void ParagraphNode::GetElementSize(int& OutSize) const { OutSize = 0; }
void ParagraphNode::GetElementPos(int, TextRenderContext&, int& OutPos) const { OutPos = 0; }
bool ParagraphNode::SplitAtSize(const TextRenderContext&, int, unsigned int, RDLNode*& L, RDLNode*& R) const
{
    L = nullptr; R = nullptr; return false;
}

// --- JustifyNode ---

// RVA 0xC98D0: MOV [EAX+0x4],arg. The default argument (TextJustifyLeft, 0) is retail's own:
// its exported `default constructor closure' 0x06325af0 is `PUSH 0; CALL 063298d0; RET`.
JustifyNode::JustifyNode(TextJustify Mode) : RDLNode()
{
    JustifyMode = Mode;
}
JustifyNode::JustifyNode(const JustifyNode& Other) : RDLNode(Other)
{
    JustifyMode = Other.JustifyMode;
}

JustifyNode& JustifyNode::operator=(const JustifyNode& Other)
{
    // .text:06325980 (ICF-shared with AttribStackNode/ImageNode::operator=): +0x04 copied,
    // no self test.
    JustifyMode = Other.JustifyMode;
    return *this;
}

TextJustify JustifyNode::get_justify()
{
    // 0x062d1ad0 (ICF-folded): MOV EAX,[ECX+0x4]; RET.
    return JustifyMode;
}

void JustifyNode::set_justify(TextJustify Mode)
{
    // 0x06309b30 (ICF-folded): MOV EAX,[ESP+4]; MOV [ECX+0x4],EAX; RET 4.
    JustifyMode = Mode;
}

RDLNode* JustifyNode::Clone() const { return new (std::nothrow) JustifyNode(*this); } // .text:063298f0

void JustifyNode::Execute(TextRenderContext&, bool)
{
    // RVA 0xC98C0 (ICF-folded to 0x2F050) — no-op in retail
}

void JustifyNode::GetVisualSize(const TextRenderContext&, VisualSize& Size) const
{
    Size.Width = 0; Size.Height = 0; Size.Ascent = 0; Size.Descent = 0;
}
void JustifyNode::GetElementSize(int& OutSize) const { OutSize = 0; }
void JustifyNode::GetElementPos(int, TextRenderContext&, int& OutPos) const { OutPos = 0; }
bool JustifyNode::SplitAtSize(const TextRenderContext&, int, unsigned int, RDLNode*& L, RDLNode*& R) const
{
    L = nullptr; R = nullptr; return false;
}

// --- ClipNode ---

ClipNode::ClipNode() : RDLNode()
{
    ClipRect.left = 0; ClipRect.top = 0; ClipRect.right = 0; ClipRect.bottom = 0;
}

// RVA 0xC9940 — the four RECT members go to +0x04..+0x10 in declaration order, so
// left/top/right/bottom map onto the node's own four ints one for one.
ClipNode::ClipNode(tagRECT const& Rect) : RDLNode()
{
    ClipRect.left = Rect.left;
    ClipRect.top = Rect.top;
    ClipRect.right = Rect.right;
    ClipRect.bottom = Rect.bottom;
}

ClipNode::ClipNode(const ClipNode& Other) : RDLNode(Other)
{
    // RVA 0xC5B00
    ClipRect.left = Other.ClipRect.left;
    ClipRect.top = Other.ClipRect.top;
    ClipRect.right = Other.ClipRect.right;
    ClipRect.bottom = Other.ClipRect.bottom;
}

ClipNode& ClipNode::operator=(const ClipNode& Other)
{
    // .text:06325b40: the four RECT dwords at +4 copied in order, no self test.
    ClipRect.left = Other.ClipRect.left;
    ClipRect.top = Other.ClipRect.top;
    ClipRect.right = Other.ClipRect.right;
    ClipRect.bottom = Other.ClipRect.bottom;
    return *this;
}

void ClipNode::set_rect(tagRECT const& Rect)
{
    // RVA 0xC9970 -- retail reads Rect.left/top/right/bottom, not 4 int params.
    ClipRect.left = Rect.left;
    ClipRect.top = Rect.top;
    ClipRect.right = Rect.right;
    ClipRect.bottom = Rect.bottom;
}

const tagRECT& ClipNode::get_rect()
{
    // 0x0632e7a0 (ICF-folded): LEA EAX,[ECX+0x4]; RET.
    return ClipRect;
}

RDLNode* ClipNode::Clone() const { return new (std::nothrow) ClipNode(*this); } // .text:063299c0

void ClipNode::Execute(TextRenderContext& Ctx, bool)
{
    // RVA 0xC9990
    RECT R;
    R.left = ClipRect.left; R.top = ClipRect.top; R.right = ClipRect.right; R.bottom = ClipRect.bottom;
    Ctx.set_clip_rect(R);
}

void ClipNode::GetVisualSize(const TextRenderContext&, VisualSize& Size) const
{
    Size.Width = 0; Size.Height = 0; Size.Ascent = 0; Size.Descent = 0;
}
void ClipNode::GetElementSize(int& OutSize) const { OutSize = 0; }
void ClipNode::GetElementPos(int, TextRenderContext&, int& OutPos) const { OutPos = 0; }
bool ClipNode::SplitAtSize(const TextRenderContext&, int, unsigned int, RDLNode*& L, RDLNode*& R) const
{
    L = nullptr; R = nullptr; return false;
}

// --- StyleNode ---

StyleNode::StyleNode() : RDLNode() { StyleId = 0; Padding = 0; }

// RVA 0xC9A10 — only the WORD at +0x04 is written; the two bytes above it stay as they were.
StyleNode::StyleNode(unsigned short Id) : RDLNode()
{
    StyleId = Id;
    Padding = 0;
}
StyleNode::StyleNode(const StyleNode& Other) : RDLNode(Other)
{
    StyleId = Other.StyleId;
    Padding = 0;
}

StyleNode& StyleNode::operator=(const StyleNode& Other)
{
    // .text:06325b90: word +4 copied, no self test.
    StyleId = Other.StyleId;
    return *this;
}

// 0x06329a30: MOV AX,[ECX+0x4]; RET. Non-const in retail (QAE).
unsigned short StyleNode::get_style_id() { return StyleId; }
void StyleNode::set_style_id(unsigned short Id) { StyleId = Id; }

RDLNode* StyleNode::Clone() const { return new (std::nothrow) StyleNode(*this); } // .text:06329b10

void StyleNode::Execute(TextRenderContext& Ctx, bool bHighlight)
{
    // RVA 0xC9A50
    StyleCollection::get_style(StyleId).execute(Ctx, bHighlight);
}

void StyleNode::GetVisualSize(const TextRenderContext&, VisualSize& Size) const
{
    Size.Width = 0; Size.Height = 0; Size.Ascent = 0; Size.Descent = 0;
}
void StyleNode::GetElementSize(int& OutSize) const { OutSize = 0; }
void StyleNode::GetElementPos(int, TextRenderContext&, int& OutPos) const { OutPos = 0; }
bool StyleNode::SplitAtSize(const TextRenderContext&, int, unsigned int, RDLNode*& L, RDLNode*& R) const
{
    L = nullptr; R = nullptr; return false;
}

// --- RDLRefNode ---

// 0x06325df0: MOV [EAX+0x4],arg -- binds the reference member.
RDLRefNode::RDLRefNode(RenderDisplayList& Ref) : RDLNode(), List(Ref)
{
}

// 0x06325e10: copies the +0x04 reference (the same list, not a copy of it).
RDLRefNode::RDLRefNode(const RDLRefNode& Other) : RDLNode(Other), List(Other.List)
{
}

const RenderDisplayList& RDLRefNode::get_list()
{
    // 0x062d1ad0 (ICF-folded): MOV EAX,[ECX+0x4]; RET.
    return List;
}

void RDLRefNode::Update(float Dt)
{
    // .text:06329b70: from the referenced list's head->Next until the head, call each node's
    // vtbl+8 (Update) with Dt. No null tests: the head always exists and nodes are never null.
    FRdlListNode* const Sentinel = List.NodeList;
    for (FRdlListNode* Cur = Sentinel->Next; Cur != Sentinel; Cur = Cur->Next)
    {
        Cur->Node->Update(Dt);
    }
}

RDLNode* RDLRefNode::Clone() const { return new (std::nothrow) RDLRefNode(*this); } // .text:06329c40

void RDLRefNode::Execute(TextRenderContext& Ctx, bool bHighlight)
{
    // .text:06329b40: the referenced list's execute inlined (every node's vtbl+0x04).
    List.execute(Ctx, bHighlight);
}

void RDLRefNode::GetVisualSize(const TextRenderContext&, VisualSize&) const
{
    // Vtable slot +0xc of ??_7RDLRefNode@@6B@ (.rdata:063a44c0) is the ICF-shared `RET 8` at
    // 0x0628f050: Size is left exactly as the caller had it.
}

void RDLRefNode::GetElementSize(int& OutSize) const
{
    // .text:06329ba0: the referenced list's get_element_count inlined -- the sum of every
    // node's vtbl+0x10, stored once at the end. No null tests in retail.
    FRdlListNode* const Sentinel = List.NodeList;
    int Accum = 0;
    for (FRdlListNode* Cur = Sentinel->Next; Cur != Sentinel; Cur = Cur->Next)
    {
        int NodeSize = 0;
        Cur->Node->GetElementSize(NodeSize);
        Accum += NodeSize;
    }
    OutSize = Accum;
}

void RDLRefNode::GetElementPos(int Index, TextRenderContext& Ctx, int& OutPos) const
{
    // .text:06329bf0: the referenced list's get_element_pos(Ctx, Index, &H, &V, &Depth); true ->
    // OutPos = ftol(H) (truncation), false -> 0. The three floats are stack scratch that
    // get_element_pos always writes; the port zero-initialises them.
    float OutH = 0.0f;
    float OutV = 0.0f;
    float OutDepth = 0.0f;
    if (List.get_element_pos(Ctx, Index, OutH, OutV, OutDepth))
    {
        OutPos = (int)OutH;
    }
    else
    {
        OutPos = 0;
    }
}
bool RDLRefNode::SplitAtSize(const TextRenderContext&, int, unsigned int, RDLNode*& L, RDLNode*& R) const
{
    L = nullptr; R = nullptr; return false;
}
