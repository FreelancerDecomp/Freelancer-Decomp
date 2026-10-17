// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// FUN_0057da40 / FUN_0057db50 -- IDS resource and plain UTF-16 text into a RenderDisplayList.

#include "ids_rdl.h"
#include "ids_strings.h"

#include <stdlib.h>

namespace
{
    // Retail's cell is 0x0C: {next, prev, RDLNode*}, circular through a sentinel whose [0] is
    // the head and [1] the tail -- Common's RenderDisplayList (ctor 0x06325bb0, copy ctor
    // 0x06325c30, execute/update walk [0]) and this module's FUN_0057eb40 (walks [1] from
    // sentinel[1] = tail) agree on it.
    const unsigned kCellBytes = 3 * sizeof(void*);

    // The single space at .rdata 0x005cef90, which retail substitutes for an EMPTY run before a
    // LF or a VT -- but never before a CR.
    const wchar_t kEmptyLineText[] = L" ";
}

void RdlAppendNode(RenderDisplayList& Rdl, RDLNode* Node)
{
    // One clone in, one clone stored. Retail actually clones TWICE at each site -- it clones
    // the stack node, hands that to the list, which clones it again into the cell, and then
    // deletes the intermediate (0x0057e17b .. 0x0057e1a4). That is what push_back of an owning
    // wrapper compiles to; the surviving object is the same either way, so the port does the
    // one clone the caller already made and stores it directly.
    //
    // FUN_0046bb00 / FUN_0046c800 (insert before the sentinel): the cell's next is the sentinel,
    // its prev the old tail (the cell itself when the tail is null, VC6 _Buynode), the sentinel's
    // prev the cell, then prev->next = the cell; the value; ++count. No test of the list or the
    // node: the sentinel always exists (Common's ctor 0x06325bb0 allocates it) and every caller
    // hands in a fresh clone. Cells are malloc'd like every cell Common's RenderDisplayList makes
    // and frees.
    void** const Sentinel = (void**)Rdl.NodeList;
    void** const Tail = static_cast<void**>(Sentinel[1]);
    void** const Cell = static_cast<void**>(malloc(kCellBytes));
    Cell[0] = Sentinel;
    Cell[1] = Tail != nullptr ? Tail : Cell;
    Sentinel[1] = Cell;
    static_cast<void**>(Cell[1])[0] = Cell;
    Cell[2] = Node;
    Rdl.Count += 1;
}

void RdlClear(RenderDisplayList& Rdl)
{
    // FUN_0046bb50 (fastcall on the list): from the head until the sentinel, each cell is
    // unlinked (prev->next = next, next->prev = prev), a non-null node deleted through its slot 0
    // (flag 1) and the slot nulled, the cell freed and the count decremented. The sentinel stays.
    // Retail tests nothing else: the sentinel always exists (RenderDisplayList's ctor allocates
    // it, Common 0x06325bb0).
    void** const Sentinel = (void**)Rdl.NodeList;
    void** Cur = static_cast<void**>(Sentinel[0]);
    while (Cur != Sentinel)
    {
        void** const Cell = Cur;
        Cur = static_cast<void**>(Cur[0]);
        static_cast<void**>(Cell[1])[0] = Cell[0];
        static_cast<void**>(Cell[0])[1] = Cell[1];
        RDLNode* const Node = static_cast<RDLNode*>(Cell[2]);
        if (Node != nullptr)
        {
            delete Node;
            Cell[2] = nullptr;
        }
        free(Cell);
        Rdl.Count -= 1;
    }
}

bool RdlRemoveLastParagraph(RenderDisplayList& Rdl)
{
    // FUN_0057eb40 (freelancer.exe.txt 0x0057eb40..0x0057eba2):
    //   0057eb46  EAX = sentinel (Rdl+4), ESI = sentinel
    //   0057eb50  ESI = ESI[1]  -- prev, so the scan starts at the tail
    //   0057eb53  ESI == sentinel (empty list) -> 0057eb63 -> ESI == sentinel[0] -> return
    //   0057eb57  node = ESI[2]; CALL [vtable+0x38] -- RDLNode::operator ParagraphNode*()
    //   0057eb61  non-null -> unlink and delete
    //   0057eb63  ESI == sentinel[0] (the head, just tested) -> return; else loop
    //   0057eb6d  ESI[1][0] = ESI[0]; ESI[0][1] = ESI[1]
    //   0057eb7c  node non-null -> node->vtable[0](1) (deleting dtor); ESI[2] = 0
    //   0057eb90  operator delete(ESI); Count -= 1
    // Retail returns whatever is left in EAX (Count-1, or the sentinel pointer when nothing
    // matched); every call site ignores it. The port reports found / not found.
    void** const Sentinel = (void**)Rdl.NodeList;
    void** Cur = static_cast<void**>(Sentinel[1]);

    while (Cur != Sentinel)
    {
        RDLNode* const Node = static_cast<RDLNode*>(Cur[2]);
        if (Node->operator ParagraphNode*() != nullptr)
        {
            void** const Next = static_cast<void**>(Cur[0]);
            void** const Prev = static_cast<void**>(Cur[1]);
            Prev[0] = Next;
            Next[1] = Prev;

            delete Node;
            free(Cur);
            Rdl.Count -= 1;
            return true;
        }

        if (Cur == static_cast<void**>(Sentinel[0]))
            break;
        Cur = static_cast<void**>(Cur[1]);
    }

    return false;
}

namespace
{
    void EmitRun(RenderDisplayList& Rdl, const wchar_t* Start, const wchar_t* End,
                 bool bUseTextPtr)
    {
        const int Len = static_cast<int>(End - Start);
        if (Len <= 0)
            return;

        // Retail builds the node on the stack and appends a CLONE of it, then destroys the
        // original -- which is why TextPtrNode is safe here even though it keeps a pointer:
        // the clone owns whatever the clone decided to own.
        if (bUseTextPtr)
        {
            TextPtrNode Node(Start, Len);
            RdlAppendNode(Rdl, Node.Clone());
        }
        else
        {
            TextNode Node(Start, Len);
            RdlAppendNode(Rdl, Node.Clone());
        }
    }

    void EmitEmptyLine(RenderDisplayList& Rdl)
    {
        // Always a TextNode, never a TextPtrNode -- retail uses the .rdata literal for both
        // values of the flag (0x0057de0a and 0x0057dfd1).
        TextNode Node(kEmptyLineText, -1);
        RdlAppendNode(Rdl, Node.Clone());
    }

    void EmitParagraph(RenderDisplayList& Rdl)
    {
        ParagraphNode Node;
        RdlAppendNode(Rdl, Node.Clone());
    }
}

void AppendTextMaybeXmlRdl(const wchar_t* Text, unsigned int Count, RenderDisplayList& Rdl,
                           bool bUseTextPtr)
{
    // FUN_0057e2c0: XMLReader::read_buffer(Rdl, Text, Count*2) with no test of either argument
    // (0057e2d5..0057e2e3); AL == 0 falls into FUN_0057db50 with the same arguments (0057e2f5).
    XMLReader Reader;
    if (Reader.read_buffer(Rdl, reinterpret_cast<const char*>(Text), Count * 2u))
        return;
    AppendTextAsRdl(Text, Count, Rdl, bUseTextPtr);
}

void AppendTextAsRdl(const wchar_t* Text, unsigned int Count, RenderDisplayList& Rdl,
                     bool bUseTextPtr)
{
    // 0x0057db73: TEST Count / JBE -- a zero length does nothing at all, not even an empty
    // paragraph. Text itself is not tested.
    if (Count == 0)
        return;

    const wchar_t* RunStart = Text;
    const wchar_t* Cursor = Text;
    unsigned int Index = 0;

    while (Index < Count)
    {
        const wchar_t Ch = *Cursor;

        if (Ch == L'\r')
        {
            // 0x0057dbaa. A CR with nothing pending emits NO filler -- unlike LF and VT. That
            // asymmetry is what keeps a CRLF pair from producing two breaks.
            EmitRun(Rdl, RunStart, Cursor, bUseTextPtr);
            EmitParagraph(Rdl);

            ++Cursor;
            ++Index;
            if (Index < Count && *Cursor == L'\n')
            {
                ++Cursor;
                ++Index;
            }
            RunStart = Cursor;
            continue;
        }

        if (Ch == L'\n' || Ch == L'\v')
        {
            // 0x0057de0a (LF) and 0x0057dfd1 (VT): an empty run becomes a single space, so a
            // blank line still occupies a line rather than collapsing away.
            if (RunStart == Cursor)
                EmitEmptyLine(Rdl);
            else
                EmitRun(Rdl, RunStart, Cursor, bUseTextPtr);

            EmitParagraph(Rdl);

            ++Cursor;
            ++Index;

            // 0x0057dfc0: after a LF retail also swallows a following CR, the mirror of the
            // CRLF case above. A VT does not.
            if (Ch == L'\n' && Index < Count && *Cursor == L'\r')
            {
                ++Cursor;
                ++Index;
            }
            RunStart = Cursor;
            continue;
        }

        ++Cursor;
        ++Index;
    }

    // 0x0057e15a: whatever is left over after the last break.
    EmitRun(Rdl, RunStart, Cursor, bUseTextPtr);
}

namespace
{
    // FUN_0057da40's two resource lookups. Retail's custom RDL type is 23.
    const int kRdlResourceType = 23;
    const int kStringTableResourceType = 6;

    const wchar_t* LockRdlResource(HMODULE Module, unsigned int LowId, unsigned int& OutCount)
    {
        HRSRC const Info = FindResourceExA(Module, MAKEINTRESOURCEA(kRdlResourceType),
                                           MAKEINTRESOURCEA(LowId), 0);
        if (Info == nullptr)
            return nullptr;

        const DWORD Bytes = SizeofResource(Module, Info);
        HGLOBAL const Data = LoadResource(Module, Info);
        if (Data == nullptr)
            return nullptr;
        const wchar_t* Cursor = static_cast<const wchar_t*>(LockResource(Data));
        if (Cursor == nullptr)
            return nullptr;

        // 0x0057daf5: the count is in characters, and a leading BOM is not part of the text.
        OutCount = Bytes / 2;
        if (OutCount != 0 && *reinterpret_cast<const unsigned short*>(Cursor) == 0xfeff)
        {
            OutCount -= 1;
            Cursor += 1;
        }
        return Cursor;
    }

    const wchar_t* LockStringTableEntry(HMODULE Module, unsigned int LowId,
                                        unsigned int& OutCount)
    {
        // 0x0057dac9: the same block/entry arithmetic GetIdsStringW uses.
        const unsigned int BlockIndex = (LowId >> 4) + 1;
        const unsigned int EntryIndex = LowId & 0xfu;

        HRSRC const Info = FindResourceExA(Module, MAKEINTRESOURCEA(kStringTableResourceType),
                                           MAKEINTRESOURCEA(BlockIndex), 0);
        if (Info == nullptr)
            return nullptr;
        HGLOBAL const Data = LoadResource(Module, Info);
        if (Data == nullptr)
            return nullptr;
        const unsigned short* Cursor = static_cast<const unsigned short*>(LockResource(Data));
        if (Cursor == nullptr)
            return nullptr;

        for (unsigned int Skip = EntryIndex; Skip != 0; --Skip)
            Cursor = Cursor + Cursor[0] + 1;

        OutCount = Cursor[0];
        return reinterpret_cast<const wchar_t*>(Cursor + 1);
    }
}

bool LoadIdsIntoRdl(unsigned int Ids, RenderDisplayList& Rdl)
{
    HMODULE const Module = GetIdsResourceModule(Ids);
    if (Module == nullptr)
        return false;

    const unsigned int LowId = Ids & 0xffffu;

    unsigned int Count = 0;
    const wchar_t* Text = LockRdlResource(Module, LowId, Count);
    if (Text == nullptr)
        Text = LockStringTableEntry(Module, LowId, Count);
    if (Text == nullptr)
        return false;

    // FUN_0057da40 @ LAB_0057db16: XMLReader::read_buffer(Rdl, text, count*2); on failure
    // FUN_0057db50 (AppendTextAsRdl). Ordinary IDS strings are not XML and take the split
    // path in retail too; RDLX / HTML infocard resources succeed on the XML branch.
    XMLReader Reader;
    if (!Reader.read_buffer(Rdl, reinterpret_cast<const char*>(Text), Count * 2u))
        AppendTextAsRdl(Text, Count, Rdl, true);
    return true;
}

bool LoadIdsTextIntoRdl(unsigned int Ids, RenderDisplayList& Rdl, const wchar_t* DebugText)
{
    // 0x0045ef71, 0x004605da, 0x00469267 and 140 more sites, all the same five steps.
    if (LoadIdsIntoRdl(Ids, Rdl))
    {
        // Strip the resource's trailing break so the text does not push what follows it onto
        // the next line.
        RdlRemoveLastParagraph(Rdl);
        return true;
    }

    // No resource: a left-justified English stub instead.
    JustifyNode Justify(static_cast<TextJustify>(1));
    RdlAppendNode(Rdl, Justify.Clone());

    if (DebugText != nullptr)
    {
        TextNode Text(DebugText, -1);
        RdlAppendNode(Rdl, Text.Clone());
    }
    return false;
}
