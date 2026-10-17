// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CFLServerCntrItem — the rich-edit container item of CFLServerDoc (FLServer.exe, retail
// .text:00406610..004066ff, vtable .rdata:0041c620, CRuntimeClass .data:0042543c).
#pragma once

class CFLServerDoc;
class CFLServerView;

class CFLServerCntrItem : public CRichEditCntrItem
{
	DECLARE_SERIAL(CFLServerCntrItem)

public:
	CFLServerCntrItem(REOBJECT* Preo = NULL, CFLServerDoc* Container = NULL);

	CFLServerDoc* GetDocument()
	{
		return (CFLServerDoc*)CRichEditCntrItem::GetDocument();
	}
	CFLServerView* GetActiveView()
	{
		return (CFLServerView*)CRichEditCntrItem::GetActiveView();
	}

	virtual ~CFLServerCntrItem();
};
