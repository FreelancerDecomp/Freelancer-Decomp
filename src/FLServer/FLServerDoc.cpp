// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "StdAfx.h"

#include "FLServerDoc.h"
#include "CntrItem.h"
#include "FLServer.h"

// IMPLEMENT_DYNCREATE(CFLServerDoc, CRichEditDoc): retail CRuntimeClass .rdata:0041d2bc
// {"CFLServerDoc", 0x234, 0xffff, CreateObject 0040d800, _GetBaseClass 0040d8c0};
// GetRuntimeClass 0040d8d0.
IMPLEMENT_DYNCREATE(CFLServerDoc, CRichEditDoc)

// .rdata:0041d338 (GetMessageMap 0040d8f0); the base map is COleServerDoc's because
// CRichEditDoc declares none of its own.
BEGIN_MESSAGE_MAP(CFLServerDoc, CRichEditDoc)
	ON_UPDATE_COMMAND_UI(ID_OLE_EDIT_LINKS, &CRichEditDoc::OnUpdateEditLinksMenu)
	ON_COMMAND(ID_OLE_EDIT_LINKS, &CRichEditDoc::OnEditLinks)
	ON_UPDATE_COMMAND_UI_RANGE(ID_OLE_VERB_FIRST, ID_OLE_VERB_LAST, &CRichEditDoc::OnUpdateObjectVerbMenu)
END_MESSAGE_MAP()

// .text:0040d900 (inlined into CreateObject 0040d800).
CFLServerDoc::CFLServerDoc()
{
	Load = 33;
	Mutex = CreateMutexA(NULL, FALSE, NULL);
	for (int Index = 0; Index < LoadHistorySize; ++Index)
	{
		LoadHistory[Index] = 33;
	}
	AverageLoad = 33;
	LoadHistoryIndex = 0;
	bAverageDirty = true;
	memset(&Stats, 0, sizeof(Stats));
}

// .text:0040d990
CFLServerDoc::~CFLServerDoc()
{
	if (Mutex != NULL)
	{
		CloseHandle(Mutex);
		Mutex = NULL;
	}
}

// .text:0040d9c0
BOOL CFLServerDoc::OnNewDocument()
{
	return CRichEditDoc::OnNewDocument() ? TRUE : FALSE;
}

// .text:0040d9d0
CRichEditCntrItem* CFLServerDoc::CreateClientItem(REOBJECT* Preo) const
{
	return new CFLServerCntrItem(Preo, (CFLServerDoc*)this);
}

// .text:0040da40 — plain text, not RTF.
void CFLServerDoc::Serialize(CArchive& Ar)
{
	m_bRTF = FALSE;
	CRichEditDoc::Serialize(Ar);
}

// .text:0040da70
BOOL CFLServerDoc::SaveModified()
{
	return TRUE;
}

// .text:0040da80 — the document title is always the server's title string.
void CFLServerDoc::SetTitle(LPCTSTR Title)
{
	if (GApp != NULL)
	{
		Title = GApp->GetServerTitle();
	}
	CRichEditDoc::SetTitle(Title);
}

// .text:0040daa0
int CFLServerDoc::GetLoad()
{
	if (Mutex != NULL)
	{
		WaitForSingleObject(Mutex, INFINITE);
		int Result = Load;
		ReleaseMutex(Mutex);
		return Result;
	}
	return Load;
}

// .text:0040dae0
int CFLServerDoc::GetAverageLoad()
{
	if (Mutex != NULL)
	{
		WaitForSingleObject(Mutex, INFINITE);
		if (bAverageDirty)
		{
			int Sum = 0;
			for (int Index = 0; Index < LoadHistorySize; ++Index)
			{
				Sum += LoadHistory[Index];
			}
			AverageLoad = Sum / LoadHistorySize;
			bAverageDirty = false;
		}
		int Result = AverageLoad;
		ReleaseMutex(Mutex);
		return Result;
	}
	return AverageLoad;
}

// .text:0040db50
bool CFLServerDoc::SetLoad(int NewLoad)
{
	if (Mutex != NULL)
	{
		DWORD Wait = WaitForSingleObject(Mutex, 0);
		if (Wait != WAIT_OBJECT_0 && Wait != WAIT_ABANDONED)
		{
			return false;
		}
		Load = NewLoad;
		LoadHistory[LoadHistoryIndex] = NewLoad;
		++LoadHistoryIndex;
		if (LoadHistoryIndex >= LoadHistorySize)
		{
			LoadHistoryIndex = 0;
		}
		bAverageDirty = true;
		ReleaseMutex(Mutex);
		return true;
	}
	Load = NewLoad;
	return true;
}

// .text:0040dbe0
bool CFLServerDoc::SetStats(const ServerStats* NewStats)
{
	if (Mutex != NULL)
	{
		DWORD Wait = WaitForSingleObject(Mutex, 0);
		if (Wait != WAIT_OBJECT_0 && Wait != WAIT_ABANDONED)
		{
			return false;
		}
		Stats = *NewStats;
		ReleaseMutex(Mutex);
		return true;
	}
	Stats = *NewStats;
	return true;
}

// .text:0040dc50
void CFLServerDoc::GetStats(ServerStats* OutStats)
{
	if (Mutex != NULL)
	{
		WaitForSingleObject(Mutex, INFINITE);
		*OutStats = Stats;
		ReleaseMutex(Mutex);
		return;
	}
	*OutStats = Stats;
}

// .text:0040dcb0 — the rich-edit selection always comes from the app's main view.
COleClientItem* CFLServerDoc::GetPrimarySelectedItem(CView* View)
{
	if (View != GApp->GetMainView())
	{
		View = GApp->GetMainView();
	}
	return CRichEditDoc::GetPrimarySelectedItem(View);
}
