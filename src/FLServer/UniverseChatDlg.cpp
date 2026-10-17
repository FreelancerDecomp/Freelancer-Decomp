// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "StdAfx.h"
#include "UniverseChatDlg.h"
#include "ChatRdl.h"
#include "RichEditText.h"
#include "FLServer.h"
#include "server_iserverimpl.h" // Server, g_pUniverseChatHook
#include "common_text.h"        // StyleNode, TextNode, BinaryRDLWriter

namespace
{
	// .data:00429ea8 — the encoded outgoing message (0x800 bytes).
	char GChatUniverseBuffer[0x800];

	// .data:00429ea4 — the shared chat-id header's "universe" id: word 0 and byte 2 = 1 set by
	// the CRT initializer .text:00417ff0, byte 3 left at its .data zero. The target of every
	// message this dialog submits; the sender is a zero id built on the stack (0041849b).
	const CHAT_ID GChatIdUniverse(0x00010000u);

	// Style id of the outgoing message's StyleNode (.text:00418374).
	const unsigned short ChatUniverseStyleId = 0xd681;
	const int InputLimitChars = 0x200;
	const int InputTextBytes = 0x800;
}

// .data:0042a6a8 — constructed by the CRT initializer .text:00418090 (ctor with a null parent),
// destroyed by its atexit thunk 004180b0.
CUniverseChatDlg GUniverseChatDlg;

// .text:00418120
CUniverseChatDlg::CUniverseChatDlg(CWnd* Parent)
	: CDialog(IDD, Parent)
{
}

// .text:004181c0
void CUniverseChatDlg::DoDataExchange(CDataExchange* DX)
{
	DDX_Control(DX, 1047, InputEdit);
	DDX_Control(DX, 1048, LogEdit);
}

// Message map .rdata:0041e858.
BEGIN_MESSAGE_MAP(CUniverseChatDlg, CDialog)
	ON_NOTIFY(EN_MSGFILTER, 1047, &CUniverseChatDlg::OnMsgFilterInput) // 004182f0
	ON_BN_CLICKED(1053, &CUniverseChatDlg::OnCloseButton)             // 00418590
	ON_WM_CLOSE()                                                     // 004185a0
	ON_WM_DESTROY()                                                   // 004185b0
END_MESSAGE_MAP()

// .text:00418210
BOOL CUniverseChatDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	::SendMessageA(InputEdit.m_hWnd, EM_SETEVENTMASK, 0, ENM_KEYEVENTS);
	::SendMessageA(InputEdit.m_hWnd, EM_EXLIMITTEXT, 0, InputLimitChars);
	LockServer();
	g_pUniverseChatHook = &CUniverseChatDlg::OnUniverseChat;
	UnlockServer();
	return TRUE;
}

// .text:00418580 — empty: Enter in the input box must not close the window.
void CUniverseChatDlg::OnOK()
{
}

// .text:00418570 — JMP [vtable +0x60]: DestroyWindow only (the object is a global).
void CUniverseChatDlg::OnCancel()
{
	DestroyWindow();
}

// .text:00418590 — JMP [vtable +0x60], DestroyWindow.
void CUniverseChatDlg::OnCloseButton()
{
	DestroyWindow();
}

// .text:004185a0 — DestroyWindow (vtable +0x60), then JMP CWnd::Default.
void CUniverseChatDlg::OnClose()
{
	DestroyWindow();
	Default();
}

// .text:004185b0
void CUniverseChatDlg::OnDestroy()
{
	CWnd::OnDestroy();
	g_pUniverseChatHook = nullptr;
}

// .text:004182f0 — as CChatAccountDlg::OnMsgFilterInput, with StyleNode(0xd681), sender 0,
// target the "universe" id, system -1.
void CUniverseChatDlg::OnMsgFilterInput(NMHDR* Header, LRESULT* Result)
{
	const MSGFILTER* Filter = (const MSGFILTER*)Header;
	if (Filter->wParam != VK_RETURN)
	{
		*Result = 0;
		return;
	}

	if (::SendMessageA(InputEdit.m_hWnd, WM_GETTEXTLENGTH, 0, 0) != 0)
	{
		wchar_t Text[InputTextBytes / sizeof(wchar_t)];
		GetRichEditText(InputEdit.m_hWnd, Text, InputTextBytes);
		LockServer();
		{
			RenderDisplayList Rdl;
			// Double clone as in CChatAccountDlg::OnMsgFilterInput: holder of the first clone
			// (0041838a..00418396), list insert 004149a0 (second clone), holder delete
			// (004183bd..004183d3), ~StyleNode (004183e2); the text at 004183e8..00418451.
			{
				StyleNode Style(ChatUniverseStyleId);
				{
					const FChatRdlNodeHolder FirstClone(Style.Clone());
					AppendChatRdlNode(Rdl, FirstClone);
				}
			}
			{
				TextNode Line(Text, -1);
				{
					const FChatRdlNodeHolder FirstClone(Line.Clone());
					AppendChatRdlNode(Rdl, FirstClone);
				}
			}

			BinaryRDLWriter Writer;
			unsigned int Size;
			if (Writer.write_buffer(Rdl, GChatUniverseBuffer, sizeof(GChatUniverseBuffer), Size))
			{
				const CHAT_ID Sender(0u);
				Server.SubmitChat(Sender, Size, GChatUniverseBuffer, GChatIdUniverse, -1);
			}
			// Retail releases the server lock before the list is destroyed (004184b6).
			UnlockServer();
		}
	}

	InputEdit.SetSel(0, -1);
	::SendMessageA(InputEdit.m_hWnd, WM_CLEAR, 0, 0);
	*Result = 1;
}

// .text:00418270 — writes to the global window's log with no window test (the hook is only
// installed while the window exists).
void __cdecl CUniverseChatDlg::OnUniverseChat(wchar_t* Text, int PlayerId)
{
	(void)PlayerId;
	LockServer();
	const HWND Log = GUniverseChatDlg.LogEdit.m_hWnd;
	const int LinesBefore = (int)::SendMessageA(Log, EM_GETLINECOUNT, 0, 0);
	AppendRichEditText(Log, Text);
	AppendRichEditText(Log, L"\n");
	const int LinesAfter = (int)::SendMessageA(GUniverseChatDlg.LogEdit.m_hWnd, EM_GETLINECOUNT, 0, 0);
	if (::GetFocus() != GUniverseChatDlg.LogEdit.m_hWnd)
	{
		GUniverseChatDlg.LogEdit.LineScroll(LinesAfter - LinesBefore, 0);
	}
	UnlockServer();
}
