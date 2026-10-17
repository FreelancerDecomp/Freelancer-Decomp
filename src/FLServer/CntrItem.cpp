// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "StdAfx.h"

#include "CntrItem.h"
#include "FLServerDoc.h"

// IMPLEMENT_SERIAL(CFLServerCntrItem, CRichEditCntrItem, 0): retail CRuntimeClass at
// .data:0042543c {"CFLServerCntrItem", 0x84, schema 0, CreateObject 00406610,
// _GetBaseClass 00406670}; GetRuntimeClass 00406680, AfxClassInit initializer 00406690,
// operator>> 004066a0.
IMPLEMENT_SERIAL(CFLServerCntrItem, CRichEditCntrItem, 0)

// .text:004066c0 — CRichEditCntrItem(preo, pContainer) then the derived vtable.
CFLServerCntrItem::CFLServerCntrItem(REOBJECT* Preo, CFLServerDoc* Container)
	: CRichEditCntrItem(Preo, Container)
{
}

// .text:004066e0 — scalar deleting destructor; the body is CRichEditCntrItem's.
CFLServerCntrItem::~CFLServerCntrItem()
{
}
