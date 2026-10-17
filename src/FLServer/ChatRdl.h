// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Rich-text list helper shared by the three chat dialogs (account, star system, universe).
// The RenderDisplayList header expands one std::list<holder> insert that all three chat send
// handlers (.text:00414260, 00416760, 004182f0) call: .text:004149a0 (cell link) with the value
// construction 004155e0 (clone of the holder's node) and the holder destructor 004144f0.
#pragma once

#include "common_text.h"

// The list's value type: owns one node. The send handlers keep one on the stack ([esp+8] /
// [esp+0xc]) for the first clone of their stack node. Destructor .text:004144f0: when the
// node is non-null, its scalar deleting destructor (vtable +0x00, flag 1), then the pointer
// is nulled. Retail inlines it on the normal path (e.g. 0041432a..00414340) and calls 004144f0
// from the unwind funclets (account 0041ab48 / 0041ab5f, system 0041acc8 / 0041acdf, universe
// 0041ad58 / 0041ad6f), so the port holds the first clone in this RAII object.
struct FChatRdlNodeHolder
{
	explicit FChatRdlNodeHolder(RDLNode* InNode)
		: Node(InNode)
	{
	}

	~FChatRdlNodeHolder()
	{
		if (Node != nullptr)
		{
			delete Node;
			Node = nullptr;
		}
	}

	FChatRdlNodeHolder(const FChatRdlNodeHolder&) = delete;
	FChatRdlNodeHolder& operator=(const FChatRdlNodeHolder&) = delete;

	RDLNode* Node;
};

// Appends a new cell at the tail of Rdl's node list whose node is a Clone() of Value's node
// (the second clone; the caller deletes its first clone when Value goes out of scope).
void AppendChatRdlNode(RenderDisplayList& Rdl, const FChatRdlNodeHolder& Value);
