// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "StdAfx.h"
#include "ChatRdl.h"

// .text:004149a0 (list insert before the sentinel, RET 0xc): new cell; cell->next = sentinel;
// cell->prev = old tail (the cell itself when the tail is null, VC6 _Buynode); sentinel->prev =
// cell; cell->prev->next = cell; the value (004155e0: placement-new null test of the cell's
// value slot, then (*Value)->Clone() through vtable +0x1c, no null test of Value's node);
// ++count. Cells are malloc'd because Common's ~RenderDisplayList frees them with free().
void AppendChatRdlNode(RenderDisplayList& Rdl, const FChatRdlNodeHolder& Value)
{
	FRdlListNode* const Sentinel = Rdl.NodeList;
	FRdlListNode* const Tail = Sentinel->Prev;
	FRdlListNode* const Cell = (FRdlListNode*)malloc(sizeof(FRdlListNode));
	Cell->Next = Sentinel;
	Cell->Prev = Tail != nullptr ? Tail : Cell;
	Sentinel->Prev = Cell;
	Cell->Prev->Next = Cell;
	Cell->Node = Value.Node->Clone();
	++Rdl.Count;
}
