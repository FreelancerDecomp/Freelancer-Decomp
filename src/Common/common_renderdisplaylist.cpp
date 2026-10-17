// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// RenderDisplayList, StyleCollection, LayoutRichText and lex_compare -- split out of
// common_text.cpp (retail RichDisplayList.cpp) in CMN-SPLIT phase B.

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
#include <vector>
#include <windows.h>

// LayoutRichText -- .text:06326600-063272b2 (Common.dll.txt 353622-354528), re-ported 2026-10-08
// (CMN-TEXT) from a full trace of the listing. Lays Rdl out into OutRdl as lines no wider than
// Param3: every committed node is cloned into OutRdl behind a PositionNode that places it.
// Param5/Param6 are the H/V origin, Param7 is single-line mode (truncate with a "." marker),
// Param8 receives the laid-out height, Param9 allows word wrap.
//
// Frame (S = ESP after the prologue): [S+0x12] result byte, [S+0x13] "line broken" byte,
// [S+0x14] justify mode, [S+0x18] line-entry list (0x20-byte nodes), [S+0x34] split-fragment
// list (0xc-byte nodes), [S+0x40] VisualSize scratch, [S+0x50] V cursor (seeded with Param6),
// [S+0x54] line ascent, [S+0x58] line height, [S+0x64] the caller's Param3, [S+0x78] a deque of
// {cursor, end} frames, [S+0xac] the "." TextNode, [S+0xc4] its VisualSize, [S+0xe4] the
// marker's TRANode, EBX the width left on the line, ESI the pending node.
//
//  1. get_state + push_front onto Ctx's state stack (restored and popped at the end).
//  2. Marker: a TextNode "." and a TRANode {Attrs = 0x10, mask 0xff, default mask 0}; the TRANode
//     merge is applied to Ctx (inlined TRANode::Execute: ((Attrs & ~Def) | (Default & Def)) & Mask
//     | (Current & ~Mask)) between a second get_state/push_front and set_state(front)/pop_front,
//     and the "." is measured in that font. In single-line mode a dot narrower than Param3
//     reserves its width (Param3 -= width).
//  3. Main loop; exit when no frames, no pending node and no line entries. The pending node comes
//     from the top frame (an exhausted frame is popped). RDLRefNode (operator RDLRefNode*,
//     vtbl+0x60) and StyleNode (vtbl+0x50, StyleCollection::get_style inlined) push a frame over
//     their list and are not laid out themselves. A ParagraphNode (vtbl+0x38) is executed,
//     measured, committed with tag 1 and ends the line; a JustifyNode (vtbl+0x40) sets the mode and
//     is executed. Anything else is measured: if it fits it is executed and committed; otherwise a
//     word split (flag 0, only with Param9 and not Param7), a deferral to the next line (Param9 and
//     the line not empty), or a hard split (flag 1); a node that cannot be split is committed whole
//     (dropped in single-line mode). Split fragments go to the fragment list and are deleted at the
//     end; the right half becomes the pending node.
//  4. Line flush: in single-line mode after a break the marker is committed (the whole line is
//     dropped first when even the dot does not fit Param3); ascent / descent / width over the
//     entries; justify 0 left, 1 centre ((width - line) / 2), 2 right, otherwise left, all from
//     Param5; each tag-0 entry with a width gets a PositionNode {H, V cursor + line ascent - its
//     ascent}, and each tag-0 entry's content is cloned into OutRdl; V cursor += line height.
//     Single-line mode stops after the first flush.
//  5. Cleanup: delete the fragments, restore Ctx from the first saved state (set_pos +
//     set_clip_rect) and pop it, *Param8 = V cursor - Param6, return the result byte (set on every
//     split, deferral or drop).
// The OutRdl inserts (FUN_0632c490) copy-construct by Clone(); retail clones each node into a
// temporary first and deletes the temporary after the insert, which the port keeps.
namespace
{
    // One frame of retail's deque (8 bytes): the next list node and the list's sentinel.
    struct FLayoutFrame
    {
        FRdlListNode* Cur;
        FRdlListNode* End;
    };

    // One line entry (retail payload 0x18 of the 0x20-byte node, copied by FUN_0632d5a0):
    // VisualSize +0x00, content +0x10, tag byte +0x14 (1 = paragraph end, not emitted).
    struct FLayoutLineEntry
    {
        VisualSize Size;
        RDLNode* Content;
        unsigned char Tag;
    };
}

namespace CmnText
{
    // FUN_0632c490: RenderDisplayList insert before Where. A 0xc node is linked in front of
    // Where (a null prev link is replaced by the new node itself), its payload copy-constructed
    // by FUN_0632d550 (= Clone() of *Value), and the count bumped. Every caller passes the
    // list's sentinel, i.e. an append.
    void InsertRdlCloneBefore(RenderDisplayList& Rdl, FRdlListNode* Where, RDLNode* const& Value)
    {
        FRdlListNode* const NewEntry = (FRdlListNode*)malloc(sizeof(FRdlListNode));
        FRdlListNode* Prev = Where->Prev;
        NewEntry->Next = Where;
        if (Prev == nullptr)
        {
            Prev = NewEntry;
        }
        NewEntry->Prev = Prev;
        Where->Prev = NewEntry;
        NewEntry->Prev->Next = NewEntry;
        NewEntry->Node = Value->Clone();
        ++Rdl.Count;
    }
}

bool __cdecl LayoutRichText(TextRenderContext& Ctx, RenderDisplayList const& Rdl, int Param3, RenderDisplayList& OutRdl, int Param5, int Param6, bool Param7, int* Param8, bool Param9)
{
    bool bResult = false;                 // [S+0x12]
    std::list<RDLNode*> Fragments;        // [S+0x34], FUN_062b9490 head / FUN_0627f280 append

    TextRenderContext::State SavedState;  // [S+0x110]
    Ctx.get_state(SavedState);
    Ctx.StateStack.push_front(SavedState);
    const int FullWidth = Param3;         // [S+0x64]

    // The "." node and its attribute node, built on the stack (vftables stamped, no ctor call
    // for the TRANode: set_attributes writes Attrs and the mask, +0x10 is zeroed by hand).
    TextNode Ellipsis;
    Ellipsis.set_text(L".", -1);
    TextRenderAttributes EllipsisAttrs;
    EllipsisAttrs.Packed = 0x10;
    TRANode EllipsisAttrNode;
    EllipsisAttrNode.set_attributes(EllipsisAttrs, 0xff);
    EllipsisAttrNode.set_default_mask(0);

    Ctx.get_state(SavedState);
    Ctx.StateStack.push_front(SavedState);
    {
        const unsigned long Current = Ctx.CurrentAttrs.Packed;
        const unsigned long Default = Ctx.get_default_attributes().Packed;
        const unsigned long DefMask = EllipsisAttrNode.get_default_mask();
        const unsigned long Mask = EllipsisAttrNode.get_mask();
        const unsigned long Attrs = EllipsisAttrNode.get_attributes().Packed;
        Ctx.CurrentAttrs.Packed = ((((~DefMask) & Attrs) | (DefMask & Default)) & Mask) | ((~Mask) & Current);
    }
    VisualSize EllipsisSize;              // [S+0xc4]
    {
        const std::wstring& Dot = Ellipsis.get_text();
        Ctx.get_text_visual_size(Dot.c_str(), EllipsisSize, (int)Dot.size());
    }
    Ctx.set_state(Ctx.StateStack.front());
    Ctx.StateStack.pop_front();

    if (Param7 && EllipsisSize.Width < Param3)
    {
        Param3 -= EllipsisSize.Width;
    }

    std::list<FLayoutLineEntry> Line;     // [S+0x18], FUN_062b9840 head / FUN_0632c550 append
    int JustifyMode = 0;                  // [S+0x14]
    int VCursor = Param6;                 // [S+0x50]
    int Remaining = Param3;               // EBX
    std::deque<FLayoutFrame> Frames;      // [S+0x78], FUN_0632c5b0 / FUN_0632c630 / FUN_0632c8f0
    {
        FRdlListNode* const Sentinel = Rdl.NodeList;
        FLayoutFrame Root = { Sentinel->Next, Sentinel };
        Frames.push_back(Root);
    }
    RDLNode* Node = nullptr;              // ESI

    for (;;) // LAB_06326854
    {
        if (Frames.size() == 0 && Node == nullptr && Line.size() == 0)
        {
            break;
        }
        bool bBroken = false;             // [S+0x13]
        bool bFlush = false;

        if (Node == nullptr)
        {
            if (Frames.size() == 0)
            {
                bFlush = true;            // 0632687e
            }
            else
            {
                FLayoutFrame& Top = Frames.back();
                if (Top.Cur == Top.End)
                {
                    Frames.pop_back();
                    continue;
                }
                Node = Top.Cur->Node;
                Top.Cur = Top.Cur->Next;

                RDLRefNode* const Ref = Node->operator RDLRefNode*();
                if (Ref != nullptr)
                {
                    FRdlListNode* const Sentinel = Ref->get_list().NodeList;
                    FLayoutFrame Frame = { Sentinel->Next, Sentinel };
                    Frames.push_back(Frame);
                    Node = nullptr;
                    continue;
                }
                StyleNode* const Style = Node->operator StyleNode*();
                if (Style != nullptr)
                {
                    RenderDisplayList& StyleList = StyleCollection::get_style(Style->get_style_id());
                    FRdlListNode* const Sentinel = StyleList.NodeList;
                    FLayoutFrame Frame = { Sentinel->Next, Sentinel };
                    Frames.push_back(Frame);
                    Node = nullptr;
                    continue;
                }
            }
        }

        if (!bFlush) // LAB_06326a34
        {
            ParagraphNode* const Paragraph = Node->operator ParagraphNode*();
            JustifyNode* const Justify = Node->operator JustifyNode*();
            if (Paragraph != nullptr)
            {
                Node->Execute(Ctx, false);
                FLayoutLineEntry Entry;
                Node->GetVisualSize(Ctx, Entry.Size);
                Entry.Content = Node;
                Entry.Tag = 1;
                Line.push_back(Entry);
                Node = nullptr;
            }
            else if (Justify != nullptr) // LAB_06326c4a
            {
                JustifyMode = (int)Justify->get_justify();
                Node->Execute(Ctx, false);
                Node = nullptr;
                continue;
            }
            else // LAB_06326c66
            {
                VisualSize Size;          // [S+0x40]
                Node->GetVisualSize(Ctx, Size);
                if (Size.Width <= Remaining) // LAB_06326ecc
                {
                    Node->Execute(Ctx, false);
                    FLayoutLineEntry Entry = { Size, Node, 0 };
                    Line.push_back(Entry);
                    Remaining -= Size.Width;
                    Node = nullptr;
                    continue;
                }

                RDLNode* Left = nullptr;  // [S+0x24]
                RDLNode* Right = nullptr; // [S+0x30]
                bool bDone = false;
                if (!Param7)
                {
                    if (Param9 && Node->SplitAtSize(Ctx, Remaining, 0, Left, Right))
                    {
                        Fragments.push_back(Left);
                        Fragments.push_back(Right);
                        Left->GetVisualSize(Ctx, Size);
                        FLayoutLineEntry Entry = { Size, Left, 0 };
                        Line.push_back(Entry);
                        Node = Right;
                        bBroken = true;
                        bResult = true;
                        bDone = true;
                    }
                    else if (Remaining != Param3 && Param9) // LAB_06326d6d -> LAB_06326ebd
                    {
                        bBroken = true;   // the node waits for the next line
                        bResult = true;
                        bDone = true;
                    }
                }
                if (!bDone) // LAB_06326d85
                {
                    if (Node->SplitAtSize(Ctx, Remaining, 1, Left, Right))
                    {
                        Fragments.push_back(Left);
                        Fragments.push_back(Right);
                        Left->GetVisualSize(Ctx, Size);
                        FLayoutLineEntry Entry = { Size, Left, 0 };
                        Line.push_back(Entry);
                        Node = Right;
                    }
                    else if (Param7) // LAB_06326ebb: dropped
                    {
                        Node = nullptr;
                    }
                    else // LAB_06326e62: committed whole, overflowing
                    {
                        FLayoutLineEntry Entry = { Size, Node, 0 };
                        Line.push_back(Entry);
                        Node = nullptr;
                    }
                    bBroken = true;
                    bResult = true;
                }
            }
        }

        // LAB_06326acd -- line flush.
        if (Param7 && bBroken)
        {
            if (EllipsisSize.Width >= FullWidth)
            {
                Line.clear();
            }
            FLayoutLineEntry Spacer = { { 0, 0, 0, 0 }, &EllipsisAttrNode, 0 };
            Line.push_back(Spacer);
            FLayoutLineEntry Dot = { EllipsisSize, &Ellipsis, 0 };
            Line.push_back(Dot);
        }

        int LineWidth = 0;                // [S+0x28]
        int MaxBelow = 0;                 // EDI
        int MaxAscent = 0;                // [S+0x54]
        for (std::list<FLayoutLineEntry>::const_iterator It = Line.begin(); It != Line.end(); ++It)
        {
            const int Below = It->Size.Height - It->Size.Ascent;
            if (Below > MaxBelow)
            {
                MaxBelow = Below;
            }
            if (It->Size.Ascent > MaxAscent)
            {
                MaxAscent = It->Size.Ascent;
            }
            LineWidth += It->Size.Width;
        }
        const int LineHeight = MaxBelow + MaxAscent; // [S+0x58]

        int HCursor = Param5;             // [S+0x2c] / EBX
        if (JustifyMode == 1)
        {
            HCursor = (FullWidth - LineWidth) / 2 + Param5;
        }
        else if (JustifyMode == 2)
        {
            HCursor = FullWidth - LineWidth + Param5;
        }

        FRdlListNode* const OutSentinel = OutRdl.NodeList;
        for (std::list<FLayoutLineEntry>::const_iterator It = Line.begin(); It != Line.end(); ++It)
        {
            if (It->Tag == 0)
            {
                if (It->Size.Width > 0)
                {
                    PositionNode Pos;
                    Pos.set_pos((float)HCursor,
                                (float)(MaxAscent - It->Size.Ascent + VCursor));
                    RDLNode* const PosClone = Pos.Clone();
                    InsertRdlCloneBefore(OutRdl, OutSentinel, PosClone);
                    delete PosClone;
                }
                RDLNode* const ContentClone = It->Content->Clone();
                InsertRdlCloneBefore(OutRdl, OutSentinel, ContentClone);
                delete ContentClone;
            }
            HCursor += It->Size.Width;
        }

        VCursor += LineHeight;
        Line.clear();
        Remaining = Param3;
        if (Param7)
        {
            break;
        }
    }

    // LAB_063270d6 -- cleanup.
    for (std::list<RDLNode*>::iterator It = Fragments.begin(); It != Fragments.end(); ++It)
    {
        if (*It != nullptr)
        {
            delete *It;
        }
    }
    Fragments.clear();

    {
        const TextRenderContext::State& First = Ctx.StateStack.front();
        Ctx.CurrentAttrs.Packed = First.Attributes;
        Ctx.set_pos(First.PosH, First.PosV);
        Ctx.set_clip_rect(First.ClipRect);
        Ctx.StateStack.pop_front();
    }

    if (Param8 != nullptr)
    {
        *Param8 = VCursor - Param6;
    }
    return bResult;
}

// .text:0632a020-0632a1cf. Compares the text of two display lists run by run: from each list it
// takes the next node that answers operator TextNode* (vtbl+0x20; a non-empty string, a null
// buffer standing for MSVCP60's empty string) or operator TextPtrNode* (vtbl+0x68), and compares
// the runs character by character -- raw when Param3 is set, through towlower otherwise -- and
// returns the first difference (unsigned 16-bit code units subtracted).
//   * A's run exhausted while B still has text: -1 (0632a11e).
//   * B's walk reaching the end: (A still has a run) ? 1 : 0 (0632a1c1); a TextPtrNode with a
//     null buffer in B ends the walk the same way (0632a116).
// Two retail quirks reproduced as they are (deterministic):
//   * B's cursor is only advanced past nodes WITHOUT text (0632a0fc); after a run is found it
//     stays on that node, so every refill of B re-reads the same run. Equal names therefore
//     compare as -1, not 0.
//   * B's end test compares B's cursor with A's sentinel (0632a0b6: [ESP+0x20] is param_1).
// RETAIL UB, not reproduced: with distinct lists the B walk never meets A's sentinel, so a B
// with no text left runs onto its own sentinel and dereferences its uninitialised payload.
// The port also stops at B's own sentinel (same result as reaching the end).
int __cdecl lex_compare(RenderDisplayList const& A, RenderDisplayList const& B, bool Param3)
{
    FRdlListNode* const ASentinel = A.NodeList;
    FRdlListNode* const BSentinel = B.NodeList;
    FRdlListNode* ACur = ASentinel->Next;
    FRdlListNode* BCur = BSentinel->Next;

    const wchar_t* AText = nullptr; // EBP
    int ALen = 0;                   // [ESP+0x10]
    const wchar_t* BText = nullptr; // EBX
    int BLen = 0;                   // [ESP+0x24]

    for (;;) // LAB_0632a047
    {
        BText = nullptr;
        BLen = 0;
        for (;;) // LAB_0632a050
        {
            // A fetch (0632a050-0632a0b0): until a run is found or the list ends.
            while (AText == nullptr && ACur != ASentinel)
            {
                RDLNode* const Node = ACur->Node;
                TextNode* const AsText = Node->operator TextNode*();
                TextPtrNode* const AsPtr = Node->operator TextPtrNode*();
                if (AsText != nullptr)
                {
                    ALen = (int)AsText->get_text().size();
                    if (ALen != 0)
                    {
                        AText = AsText->get_text().c_str();
                    }
                }
                else if (AsPtr != nullptr)
                {
                    ALen = AsPtr->get_text_length();
                    AText = AsPtr->get_text();
                }
                ACur = ACur->Next;
            }

            // B fetch (0632a0b2-0632a118), only when B has no run in hand.
            if (BText == nullptr)
            {
                for (;;)
                {
                    if (BCur == ASentinel || BCur == BSentinel)
                    {
                        return AText != nullptr ? 1 : 0;
                    }
                    RDLNode* const Node = BCur->Node;
                    TextNode* const AsText = Node->operator TextNode*();
                    TextPtrNode* const AsPtr = Node->operator TextPtrNode*();
                    if (AsText != nullptr)
                    {
                        BLen = (int)AsText->get_text().size();
                        if (BLen != 0)
                        {
                            BText = AsText->get_text().c_str();
                            break; // cursor stays on this node
                        }
                    }
                    else if (AsPtr != nullptr)
                    {
                        BText = AsPtr->get_text();
                        BLen = AsPtr->get_text_length();
                        if (BText == nullptr)
                        {
                            return AText != nullptr ? 1 : 0;
                        }
                        break; // cursor stays on this node
                    }
                    BCur = BCur->Next;
                }
            }

            // 0632a11e
            if (AText == nullptr)
            {
                return -1;
            }

            bool bAExhausted = true;
            while (ALen > 0)
            {
                if (BLen <= 0)
                {
                    bAExhausted = false; // 0632a191: A still has characters
                    break;
                }
                unsigned int CharA = 0;
                unsigned int CharB = 0;
                if (Param3)
                {
                    CharA = (unsigned short)*AText;
                    CharB = (unsigned short)*BText;
                }
                else
                {
                    CharA = (unsigned short)towlower(*AText);
                    CharB = (unsigned short)towlower(*BText);
                }
                const int Diff = (int)CharA - (int)CharB;
                if (Diff != 0)
                {
                    return Diff;
                }
                ++AText;
                ++BText;
                --ALen;
                --BLen;
            }
            if (bAExhausted)
            {
                // 0632a195
                AText = nullptr;
                ALen = 0;
            }

            // 0632a19b
            if (BLen > 0)
            {
                continue; // LAB_0632a050, B keeps its run
            }
            break; // LAB_0632a047, B's run is dropped
        }
    }
}

// ============================================================================
// StyleCollection — static style registry.
// ============================================================================

// Retail .data:06401950, constructed by the static initializer at 0x0632a1d0 (map ctor, then
// atexit(0x0632a200) which tears the tree down at unload).
std::map<unsigned short, StyleCollection::Style> StyleCollection::mStyles;

namespace
{
    // The erase-everything loop retail inlines into both writers (set_style(Rdl) at
    // .text:0632a421-0632a450 and FUN_0632a2f0 at .text:0632a320-0632a343): for every entry,
    // unlink it, destroy its node holder (which deletes the RDLNode through its scalar deleting
    // destructor, FUN_0632d6e0), free the list node and decrement the count. The sentinel stays.
    void EraseAllStyleNodes(RenderDisplayList& Rdl)
    {
        FRdlListNode* const Sentinel = Rdl.NodeList;
        FRdlListNode* Cur = Sentinel->Next;
        while (Cur != Sentinel)
        {
            FRdlListNode* const Next = Cur->Next;
            FRdlListNode* const Prev = Cur->Prev;
            Prev->Next = Next;
            Next->Prev = Prev;
            RDLNode* Node = Cur->Node;
            if (Node != nullptr)
            {
                delete Node;
            }
            free(Cur);
            Rdl.Count--;
            Cur = Next;
        }
    }

    // FUN_0632a2f0 (.text:0632a2f0-0632a3e1), called on the registry entry by
    // set_style(Id, Attrs). Retail's name for it is not recoverable (nothing about Style is
    // exported). It empties the entry, builds a stack TRANode {Attrs = *Attrs, mask (+0x0C) =
    // 0xFFFFFFFF, default mask (+0x10) = 0} (.text:0632a345-0632a36b), Clone()s it (0632a373),
    // links a new cell at the tail whose node is a Clone() of that clone (FUN_0632d550, no null
    // test of the first clone), and deletes the first clone when non-null (0632a3bd-0632a3d1).
    void ResetStyleToAttributes(StyleCollection::Style& Entry, TextRenderAttributes const& Attrs)
    {
        EraseAllStyleNodes(Entry);
        // TRANode(Attrs, Mask, DefaultMask) stores Attrs & Mask; with Mask = 0xFFFFFFFF that is
        // Attrs unchanged, exactly the raw copy retail makes at .text:0632a349/0632a35b.
        TRANode Temp(Attrs, 0xFFFFFFFFu, 0u);
        RDLNode* const FirstClone = Temp.Clone();
        InsertRdlCloneBefore(Entry, Entry.NodeList, FirstClone);
        delete FirstClone;
    }
}

void __cdecl StyleCollection::set_style(unsigned short Id, const RenderDisplayList& Rdl)
{
    // RVA 0xCA3F0. mStyles[Id] (FUN_0632c190, the map's operator[]: inserts an empty entry on
    // a miss), erase every node of that entry (.text:0632a421-0632a450), then append a Clone()
    // of each of Rdl's nodes in order at the entry's tail (.text:0632a463-0632a4a2; the holder
    // copy FUN_0632d550 is `*Dest = (*Src)->Clone()`, vtable+0x1C).
    // Retail reads Rdl's first node before the erase loop (.text:0632a40d-0632a41b); passing
    // the entry itself (set_style(Id, get_style(Id))) therefore walks freed nodes in retail.
    // The port reads it after the erase, so that case leaves the entry empty.
    Style& Entry = mStyles[Id];
    EraseAllStyleNodes(Entry);
    FRdlListNode* const SrcSentinel = Rdl.NodeList;
    for (FRdlListNode* SrcCur = SrcSentinel->Next; SrcCur != SrcSentinel; SrcCur = SrcCur->Next)
    {
        // 0632a463-0632a49b: the cell is linked first, then its node = Clone() of the source.
        InsertRdlCloneBefore(Entry, Entry.NodeList, SrcCur->Node);
    }
}

void __cdecl StyleCollection::set_style(unsigned short Id, const TextRenderAttributes& Attrs)
{
    // RVA 0xCA4B0: FUN_0632a2f0(&mStyles[Id], Attrs).
    ResetStyleToAttributes(mStyles[Id], Attrs);
}

RenderDisplayList& __cdecl StyleCollection::get_style(unsigned short Id)
{
    // RVA 0xCA4D0. A lookup, not operator[]: lower_bound (FUN_062a2250), rejected when it is
    // end() or Id < its key (.text:0632a4e8-0632a4f9). A hit returns the entry (node + 0x10).
    // A miss inserts nothing and returns a function-local static empty RenderDisplayList
    // (.data:06401940, guard bit 0 of DAT_0640194c, destroyed at exit by LAB_0632a550).
    std::map<unsigned short, Style>::iterator It = mStyles.find(Id);
    if (It != mStyles.end())
    {
        return It->second;
    }
    static RenderDisplayList EmptyStyle;
    return EmptyStyle;
}

void __cdecl StyleCollection::remove_style(unsigned short)
{
    // 0x062a8920 (ICF-folded): a bare RET. Retail never removes a style.
}

// ============================================================================
// RenderDisplayList — linked list of RDLNode pointers.
// ============================================================================

RenderDisplayList::RenderDisplayList()
{
    // RVA 0xC5BB0 (??0RenderDisplayList@@QAE@XZ, ordinal 394). CORRECTED
    // 2026-09-17: this used to leave NodeList null, which made every
    // default-constructed RenderDisplayList in the port a permanently EMPTY
    // list -- RdlxAppendNode's "degenerate/uninitialized Rdl" guard fired on
    // every single append and silently dropped the node. Retail allocates the
    // sentinel right here, exactly as the copy constructor below already did:
    //   .text:06325bb0  51          PUSH  ECX              ; 4 bytes of scratch
    //   .text:06325bb1  8a442403    MOV   AL,[ESP + 0x3]   ; the +0x00 byte flag,
    //   .text:06325bba  8806        MOV   byte ptr [ESI],AL ; left uninitialized
    //   .text:06325bb8  6a0c        PUSH  0xc
    //   .text:06325bbc  e8dbc10600  CALL  operator new
    //   .text:06325bc1  8900        MOV   [EAX],EAX        ; sentinel->next = self
    //   .text:06325bc3  894004      MOV   [EAX + 0x4],EAX  ; sentinel->prev = self
    //   .text:06325bc6  894604      MOV   [ESI + 0x4],EAX  ; this->NodeList
    //   .text:06325bcc  c74608...   MOV   [ESI + 0x8],0x0  ; this->Count
    // Note what retail does NOT do: it never gives the byte at +0x00 a value --
    // AL is read back out of the stack slot PUSH ECX just reserved. Server.dll's
    // own copy of this constructor (FUN_06d1ad30) takes that byte by address and
    // its caller FUN_06d4f570 passes an uninitialized local. The port zeroes it,
    // which is strictly better defined and observable nowhere.
    Allocator = 0;
    FRdlListNode* const Sentinel = (FRdlListNode*)malloc(sizeof(FRdlListNode));
    Sentinel->Next = Sentinel;
    Sentinel->Prev = Sentinel;
    NodeList = Sentinel;
    Count = 0;
}

RenderDisplayList::RenderDisplayList(const RenderDisplayList& Other)
{
    // .text:06325be0 — byte +0 copied, a fresh self-linked head, Count 0, then every source
    // node cloned (vtbl+0x1c) onto the tail. No null tests of the head, the source list or
    // the source nodes (06325c5b only tests the placement address &New[2]).
    Allocator = Other.Allocator;
    FRdlListNode* const Sentinel = (FRdlListNode*)malloc(sizeof(FRdlListNode));
    Sentinel->Next = Sentinel;
    Sentinel->Prev = Sentinel;
    NodeList = Sentinel;
    Count = 0;

    // Walk Other's node list and clone each RDLNode.
    //
    // CORRECTED 2026-09-17 (Server.dll lane, wave 8). This loop used to link the
    // new entries as [0]=prev / [1]=next with the sentinel's [0] as the TAIL,
    // while RdlxAppendNode, the destructor, execute(), update(),
    // get_dimensions(), get_element_count() and get_element_pos() all use
    // [0]=next / [1]=prev and traverse through [0]. A copied list therefore came
    // out REVERSED, and operator= had the same inversion.
    //
    // Retail settles it. ??0RenderDisplayList@@QAE@ABV0@@Z, .text:06325c30, with
    // EBX pinned to the new sentinel S for the whole loop (it is never reassigned
    // inside it -- EBX is loaded once at .text:06325c21 as `MOV EBX,[EAX]`, i.e.
    // S[0], which at that point is S itself):
    //   .text:06325c30  8b7304  MOV  ESI,[EBX + 0x4]   ; ESI = S[1]   (old tail)
    //   .text:06325c33  6a0c    PUSH 0xc
    //   .text:06325c35  e8...   CALL operator new      ; EAX = New
    //   .text:06325c3f  8918    MOV  [EAX],EBX         ; New[0] = S
    //   .text:06325c3d  85f6    TEST ESI,ESI
    //   .text:06325c43  8bf0    MOV  ESI,EAX           ; (degenerate: prev==0 -> self)
    //   .text:06325c45  897004  MOV  [EAX + 0x4],ESI   ; New[1] = old tail
    //   .text:06325c48  894304  MOV  [EBX + 0x4],EAX   ; S[1]   = New
    //   .text:06325c4b  8b4804  MOV  ECX,[EAX + 0x4]   ; ECX = old tail
    //   .text:06325c51  8901    MOV  [ECX],EAX         ; oldtail[0] = New
    //   .text:06325c4e  8d7008  LEA  ESI,[EAX + 0x8]   ; &New[2]
    //   .text:06325c6c  ff521c  CALL [EDX + 0x1c]      ; SrcNode->Clone()
    //   .text:06325c6f  8906    MOV  [ESI],EAX         ; New[2] = the clone
    //   .text:06325c7c  8b6d00  MOV  EBP,[EBP]         ; source cursor advances through [0]
    // "insert New between S and S[1], sentinel[1] is the tail" is an append with
    // [0]=next / [1]=prev -- the same shape RdlxAppendNode already had, and the
    // source walk at .text:06325c7c confirms [0] is next on the read side too.
    FRdlListNode* const OtherSentinel = Other.NodeList;
    for (FRdlListNode* OtherCur = OtherSentinel->Next; OtherCur != OtherSentinel; OtherCur = OtherCur->Next)
    {
        RDLNode* SrcNode = OtherCur->Node;

        FRdlListNode* const NewEntry = (FRdlListNode*)malloc(sizeof(FRdlListNode));
        FRdlListNode* Tail = Sentinel->Prev;
        if (Tail == nullptr) Tail = NewEntry;   // retail's .text:06325c41 fixup
        NewEntry->Next = Sentinel;
        NewEntry->Prev = Tail;
        Sentinel->Prev = NewEntry;
        Tail->Next = NewEntry;
        NewEntry->Node = SrcNode->Clone();
        Count++;
    }
}

RenderDisplayList::~RenderDisplayList()
{
    // .text:06325d90: every entry unlinked, its node holder destroyed (FUN_063272c0: delete
    // the node through its scalar deleting dtor when non-null, null the slot), the entry
    // freed and --Count; then the head freed and +0x04 / +0x08 zeroed. +0x00 is not touched.
    FRdlListNode* const Sentinel = NodeList;
    FRdlListNode* Cur = Sentinel->Next;
    while (Cur != Sentinel)
    {
        FRdlListNode* const Next = Cur->Next;
        FRdlListNode* const Prev = Cur->Prev;
        Prev->Next = Next;
        Next->Prev = Prev;
        if (Cur->Node != nullptr)
        {
            delete Cur->Node;
            Cur->Node = nullptr;
        }
        free(Cur);
        --Count;
        Cur = Next;
    }
    free(Sentinel);
    NodeList = nullptr;
    Count = 0;
}

RenderDisplayList& RenderDisplayList::operator=(const RenderDisplayList& Other)
{
    // .text:06325cb0-06325d88: VC6 list::operator= over an owning-pointer element (copy
    // construction = FUN_0632d550, which calls Clone() through vtbl+0x1c; destruction =
    // FUN_063272c0, which deletes the node through its scalar deleting dtor and nulls the slot;
    // element ASSIGNMENT = a plain pointer copy). Skipped entirely when this == &Other.
    //   1. Over the common prefix, each of this list's entries gets Other's pointer
    //      (MOV [EAX+8],[EDI+8] at 06325ce4).
    //   2. If this list is longer, every surplus entry is unlinked, its node deleted
    //      (FUN_063272c0), the entry freed, --Count (06325d00-06325d2e).
    //   3. Otherwise Other's remaining entries are cloned onto this list's tail
    //      (06325d40-06325d7f, FUN_0632d550 = Clone()).
    // +0x00 is not touched (only the copy constructor copies that byte).
    //
    // RETAIL UB, not reproduced: step 1 leaks this list's old nodes and leaves both lists
    // pointing at the same RDLNode objects, so destroying either list later frees nodes the
    // other still uses (a double delete once both are gone). The port keeps the shape but
    // makes step 1 deterministic: the old node is deleted and Other's node is cloned into the
    // slot, so the content after the assignment is what retail's is, without the shared
    // ownership.
    if (this == &Other)
    {
        return *this;
    }

    FRdlListNode* const Sentinel = NodeList;
    FRdlListNode* const OtherSentinel = Other.NodeList;
    FRdlListNode* Cur = Sentinel->Next;
    FRdlListNode* OtherCur = OtherSentinel->Next;

    // 1. Common prefix.
    while (Cur != Sentinel && OtherCur != OtherSentinel)
    {
        RDLNode* const Old = Cur->Node;
        RDLNode* const Src = OtherCur->Node;
        Cur->Node = (Src != nullptr) ? Src->Clone() : nullptr; // retail copies the pointer, null too
        delete Old;
        Cur = Cur->Next;
        OtherCur = OtherCur->Next;
    }

    // 2. Erase this list's surplus.
    while (Cur != Sentinel)
    {
        FRdlListNode* const Next = Cur->Next;
        FRdlListNode* const Prev = Cur->Prev;
        Prev->Next = Next;
        Next->Prev = Prev;
        if (Cur->Node != nullptr)
        {
            delete Cur->Node;
            Cur->Node = nullptr;
        }
        free(Cur);
        --Count;
        Cur = Next;
    }

    // 3. Clone Other's remainder onto the tail (insert before the sentinel).
    while (OtherCur != OtherSentinel)
    {
        FRdlListNode* const NewEntry = (FRdlListNode*)malloc(sizeof(FRdlListNode));
        FRdlListNode* Tail = Sentinel->Prev;
        if (Tail == nullptr)
        {
            Tail = NewEntry; // retail's degenerate-head fixup (06325d51)
        }
        NewEntry->Next = Sentinel;
        NewEntry->Prev = Tail;
        Sentinel->Prev = NewEntry;
        Tail->Next = NewEntry;
        NewEntry->Node = OtherCur->Node->Clone();
        ++Count;
        OtherCur = OtherCur->Next;
    }
    return *this;
}

void RenderDisplayList::execute(TextRenderContext& Ctx, bool bHighlight)
{
    // .text:06329cd0: from head->Next until the head, node vtbl+0x04 (Execute)(Ctx, bHighlight).
    // No null tests in retail.
    FRdlListNode* const Sentinel = NodeList;
    for (FRdlListNode* Cur = Sentinel->Next; Cur != Sentinel; Cur = Cur->Next)
    {
        Cur->Node->Execute(Ctx, bHighlight);
    }
}

void RenderDisplayList::update(float Dt)
{
    // .text:06329d00: from head->Next until the head, node vtbl+0x08 (Update)(Dt). No null
    // tests in retail.
    FRdlListNode* const Sentinel = NodeList;
    for (FRdlListNode* Cur = Sentinel->Next; Cur != Sentinel; Cur = Cur->Next)
    {
        Cur->Node->Update(Dt);
    }
}

void RenderDisplayList::get_dimensions(TextRenderContext& Ctx, float& OutWidth,
                                        float& OutHeight, float& OutDepth)
{
    // .text:06329d30-06329f27 (frame: S0 = ESP after the 0x203c _chkstk allocation; the loop
    // and the epilogue run at S0-0x10 / S0-0x14, pinned by the parameter reads
    // [ESP+0x204c] = Ctx and [ESP+0x2054] = &OutWidth).
    //
    // .text:06329d44-06329d9e: push_state inlined. The 0x20-byte record is built on the stack at
    // S0+0x1c {CurrentAttrs, <unwritten>, PosV, PosH, clip RECT via vtbl+0x24} and copied onto
    // the state list (FUN_0632c0d0 = push_front). The same stack bytes are then reused as the
    // VisualSize every GetVisualSize below writes into, so that VisualSize starts out as the
    // record: Ascent (+0x08) = the saved PosV. Only Ascent is ever read from it.
    VisualSize NodeSize; // S0+0x1c
    NodeSize.Width = (int)Ctx.CurrentAttrs.Packed;
    NodeSize.Height = 0; // record +0x04, never written by retail; never read here
    NodeSize.Ascent = Ctx.get_pos_v();
    NodeSize.Descent = Ctx.get_pos_h();
    Ctx.push_state();

    // .text:06329da3-06329df5: reset_to_default inlined (its "size %d\n" leak diagnostic, the
    // attribute-list clear, CurrentAttrs = DefaultAttrs), then PosV = PosH = 0.
    Ctx.reset_to_default();
    Ctx.set_pos(0, 0);

    // .text:06329d65-06329d85 seeds; 0x47c35000 = 100000.0f.
    float MinV = 100000.0f;   // S0+0x04
    float MaxAscent = 0.0f;   // S0+0x08
    float MinH = 100000.0f;   // S0+0x0c
    float MaxH = 0.0f;        // S0+0x10
    float MaxV = 0.0f;        // S0+0x14

    // .text:06329df8-06329e9f: from head->Next until the head (re-read each step), no null tests.
    FRdlListNode* const Sentinel = NodeList;
    for (FRdlListNode* Cur = Sentinel->Next; Cur != Sentinel; Cur = Cur->Next)
    {
        RDLNode* const Node = Cur->Node;

        // vtbl+0x0c (Ctx, &S0+0x1c) then vtbl+0x04 (Ctx, false). A node whose GetVisualSize
        // writes nothing (RDLRefNode) leaves the previous Ascent in place.
        Node->GetVisualSize(Ctx, NodeSize);
        Node->Execute(Ctx, false);

        // .text:06329e1f-06329e25: FILD PosH stays in ST0 (an exact double); FILD PosV is
        // stored to the S0+0x00 float scratch. The raw pen fields, no origin.
        const double CurH = (double)Ctx.get_pos_h();
        const float CurV = (float)Ctx.get_pos_v();

        // 06329e2d FCOMP + TEST AH,0x41 + JZ: skip when MaxH > H, else FST (rounded to float).
        if (!(MaxH > CurH))
        {
            MaxH = (float)CurH;
        }
        // 06329e3e FCOMP + TEST AH,0x05 + JNP: skip when MinH < H, else FSTP.
        if (!(MinH < CurH))
        {
            MinH = (float)CurH;
        }
        // 06329e53 / 06329e6a: the same two tests against the float scratch, MOV copies.
        if (!(MaxV > CurV))
        {
            MaxV = CurV;
        }
        if (!(MinV < CurV))
        {
            MinV = CurV;
        }
        // 06329e7d: FILD [S0+0x24] = NodeSize.Ascent (exact double) against the float maximum.
        const double Ascent = (double)NodeSize.Ascent;
        if (!(MaxAscent > Ascent))
        {
            MaxAscent = (float)Ascent;
        }
    }

    // .text:06329ea5-06329f01: pop_state inlined (CurrentAttrs / PosV / PosH from the front
    // record, set_clip_rect via vtbl+0x20, unlink and free the record).
    Ctx.pop_state();

    // .text:06329ee4-06329f1f (ESP = S0-0x14 for the first FLD: the operator delete argument is
    // still on the stack): float subtractions, each FSTP'd to its output.
    OutWidth = MaxH - MinH;
    OutHeight = MaxV - MinV;
    OutDepth = MaxAscent;
}

void RenderDisplayList::optimize()
{
    // 0x062a8920 (ICF-folded): a bare RET. Retail's optimize does nothing.
}

int RenderDisplayList::get_element_count() const
{
    // .text:06329f30: the sum of every node's vtbl+0x10 (GetElementSize) -- not the node
    // count. No null tests in retail.
    int Total = 0;
    FRdlListNode* const Sentinel = NodeList;
    for (FRdlListNode* Cur = Sentinel->Next; Cur != Sentinel; Cur = Cur->Next)
    {
        int NodeSize = 0;
        Cur->Node->GetElementSize(NodeSize);
        Total += NodeSize;
    }
    return Total;
}

bool RenderDisplayList::get_element_pos(TextRenderContext& Ctx, int Index,
                                         float& OutH, float& OutV, float& OutDepth)
{
    // .text:06329f70-0632a013. LocalPos starts at 0. For each node: Size = vtbl+0x10; if
    // Index < Size (signed JL) the node's vtbl+0x14 GetElementPos(Index, Ctx, &LocalPos) is
    // called and the walk stops; otherwise the node is executed (vtbl+0x04, highlight 0) and
    // Index -= Size. Then, found or not: OutH = float(Ctx.PosH + LocalPos), OutV = float(PosV),
    // OutDepth = float(Ctx vtbl+0x08 get_cell_height()), and the result is always true.
    int LocalPos = 0;
    FRdlListNode* const Sentinel = NodeList;
    for (FRdlListNode* Cur = Sentinel->Next; Cur != Sentinel; Cur = Cur->Next)
    {
        RDLNode* const Node = Cur->Node;
        int Size = 0;
        Node->GetElementSize(Size);
        if (Index < Size)
        {
            Node->GetElementPos(Index, Ctx, LocalPos);
            break;
        }
        Node->Execute(Ctx, false);
        Index -= Size;
    }

    OutH = (float)(Ctx.get_pos_h() + LocalPos);
    OutV = (float)Ctx.get_pos_v();
    OutDepth = (float)Ctx.get_cell_height();
    return true;
}
