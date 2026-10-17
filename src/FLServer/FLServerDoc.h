// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CFLServerDoc — FLServer.exe's document (retail CreateObject .text:0040d800, ctor 0040d900,
// dtor 0040d990, vtable .rdata:0041d340, CRuntimeClass .rdata:0041d2bc, message map
// .rdata:0041d338). A CRichEditDoc that also carries the server load figures and the last
// ServerStats snapshot; the console view, the summary form and the main frame read them.
#pragma once

#include "common_server_types.h"

class CFLServerDoc : public CRichEditDoc
{
protected:
	CFLServerDoc();
	DECLARE_DYNCREATE(CFLServerDoc)

public:
	enum { LoadHistorySize = 64 };

	// .text:0040daa0 — the last value given to SetLoad.
	int GetLoad();
	// .text:0040dae0 — mean of the LoadHistorySize history slots; recomputed only when a
	// SetLoad happened since the last call.
	int GetAverageLoad();
	// .text:0040db50 — records a load sample; fails (returns false) without blocking when the
	// mutex is held elsewhere.
	bool SetLoad(int Load);
	// .text:0040dbe0 — stores a ServerStats snapshot; fails without blocking like SetLoad.
	bool SetStats(const ServerStats* Stats);
	// .text:0040dc50 — copies the snapshot out (blocks on the mutex).
	void GetStats(ServerStats* Stats);

	virtual CRichEditCntrItem* CreateClientItem(REOBJECT* Preo) const;   // 0040d9d0
	virtual BOOL OnNewDocument();                                       // 0040d9c0
	virtual void Serialize(CArchive& Ar);                                // 0040da40
	virtual void SetTitle(LPCTSTR Title);                                // 0040da80
	virtual BOOL SaveModified();                                         // 0040da70
	virtual COleClientItem* GetPrimarySelectedItem(CView* View);         // 0040dcb0

	virtual ~CFLServerDoc();

protected:
	DECLARE_MESSAGE_MAP()

	HANDLE Mutex;                        // +0xf4
	int Load;                            // +0xf8
	bool bAverageDirty;                  // +0xfc
	int LoadHistory[LoadHistorySize];    // +0x100
	int LoadHistoryIndex;                // +0x200
	int AverageLoad;                     // +0x204
	ServerStats Stats;                   // +0x208
};
