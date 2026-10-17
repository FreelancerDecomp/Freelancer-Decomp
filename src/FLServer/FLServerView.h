// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CFLServerView — FLServer.exe's console view, a CRichEditView (retail CreateObject
// .text:0040dde0, ctor 0040ded0, dtor 0040df40, vtable .rdata:0041d550, CRuntimeClass
// .rdata:0041d49c, message map .rdata:0041d548). While it exists it is the target of
// ServerLogf (.text:0040e330), which it installs as DACOM's FDUMP hook.
#pragma once

#include "remoteclient.h"   // ErrorCode

class CFLServerDoc;

class CFLServerView : public CRichEditView
{
protected:
	CFLServerView();
	DECLARE_DYNCREATE(CFLServerView)

public:
	CFLServerDoc* GetDocument() { return (CFLServerDoc*)m_pDocument; }

	virtual BOOL PreCreateWindow(CREATESTRUCT& Cs);     // 0040e040
	virtual void OnInitialUpdate();                     // 0040e050
	virtual BOOL OnPreparePrinting(CPrintInfo* Info);   // 0040e160

	virtual ~CFLServerView();

	// .text:0040e280 — deletes the oldest ServerControl.ConsoleTrimLines lines.
	bool TrimLines();

	unsigned int LineCount;   // +0x184: lines appended by ServerLogf since the last trim

protected:
	afx_msg void OnDestroy();                                       // 0040e170
	afx_msg void OnKeyDown(UINT Char, UINT RepCnt, UINT Flags);     // 0040e680

	DECLARE_MESSAGE_MAP()
};

// .text:0040e330 — exported @57 through FLServer.def; also DACOM's FDUMP while the view lives
// and RemoteClient's log function (SetServerLogFunction). Must stay
// ?ServerLogf@@YAHUErrorCode@@PBDZZ.
int __cdecl ServerLogf(ErrorCode Code, const char* Format, ...);
