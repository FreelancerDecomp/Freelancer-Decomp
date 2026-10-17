// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "StdAfx.h"
#include "ChatAccountDlg.h"
#include "ChatRdl.h"
#include "RichEditText.h"
#include "FLServer.h"
#include "server_playerdb.h"
#include "server_iserverimpl.h" // Server, g_pPrivateChatHook
#include "common_text.h"        // StyleNode, TextNode, BinaryRDLWriter

#include <map>

namespace
{
	// .data:0042957c — the open account chat windows by account name. Built by the CRT
	// initializer .text:00413eb0, destroyed by its atexit thunk 00413ee0.
	std::map<CiWString, CChatAccountDlg*> GChatAccountDialogs;

	// .data:00428d70 — the encoded outgoing message (0x800 bytes).
	char GChatAccountBuffer[0x800];

	// .data:00428d68 — the shared chat-id header's "console" id: word 0 and byte 2 set to 0 by
	// the CRT initializer .text:00413e00, byte 3 left at its .data zero. The sender of every
	// message this dialog submits.
	const CHAT_ID GChatIdConsole(0x00000000u);

	// Style id of the outgoing message's StyleNode (.text:004142e1).
	const unsigned short ChatAccountStyleId = 0x8283;
	// EM_EXLIMITTEXT limit of the input box and EM_GETTEXTEX byte count of the send path.
	const int InputLimitChars = 0x200;
	const int InputTextBytes = 0x800;
}

// .text:00413fd0 — AccountName empty; three CRichEditCtrl members.
CChatAccountDlg::CChatAccountDlg(CWnd* Parent)
	: CDialog(IDD, Parent)
{
}

// .text:004140a0 — erases this window's entry from the map (equal_range(AccountName) and
// erase of that range, 004154b0 / 00415360 / 00414830); members are then destroyed.
CChatAccountDlg::~CChatAccountDlg()
{
	GChatAccountDialogs.erase(AccountName);
}

// .text:004141a0
void CChatAccountDlg::DoDataExchange(CDataExchange* DX)
{
	DDX_Control(DX, 1047, InputEdit);
	DDX_Control(DX, 1048, LogEdit);
	DDX_Control(DX, 1049, TitleEdit);
}

// Message map .rdata:0041e290.
BEGIN_MESSAGE_MAP(CChatAccountDlg, CDialog)
	ON_NOTIFY(EN_MSGFILTER, 1047, &CChatAccountDlg::OnMsgFilterInput) // 00414260
	ON_BN_CLICKED(1053, &CChatAccountDlg::OnCloseButton)             // 00414690
END_MESSAGE_MAP()

// .text:004141e0 — map find (lower_bound plus the reverse compare 004157a0).
CChatAccountDlg* CChatAccountDlg::FindByName(CiWString& Name)
{
	std::map<CiWString, CChatAccountDlg*>::iterator It = GChatAccountDialogs.find(Name);
	if (It == GChatAccountDialogs.end())
	{
		return nullptr;
	}
	return It->second;
}

// .text:00414510
BOOL CChatAccountDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	::SendMessageA(InputEdit.m_hWnd, EM_SETEVENTMASK, 0, ENM_KEYEVENTS);
	::SendMessageA(InputEdit.m_hWnd, EM_EXLIMITTEXT, 0, InputLimitChars);
	GChatAccountDialogs[AccountName] = this;
	SetRichEditText(TitleEdit.m_hWnd, AccountName.c_str());
	LockServer();
	g_pPrivateChatHook = &CChatAccountDlg::OnPrivateChat;
	UnlockServer();
	return TRUE;
}

// .text:00414680 — empty: Enter in the input box must not close the window.
void CChatAccountDlg::OnOK()
{
}

// .text:00414660 — modeless and self-owned: DestroyWindow (vtable +0x60), then the scalar
// deleting destructor (vtable +0x04, flag 1).
void CChatAccountDlg::OnCancel()
{
	DestroyWindow();
	delete this;
}

// .text:00414690 — JMP [vtable +0xd0], the OnCancel slot.
void CChatAccountDlg::OnCloseButton()
{
	OnCancel();
}

// .text:00414260 — EN_MSGFILTER of the input box. On VK_RETURN (MSGFILTER::wParam; the message
// itself is not tested) with a non-empty input, the text goes out as StyleNode(0x8283) +
// TextNode in the binary rich-text format, from the console id to the account's client id
// (0 when the account is not found), system -1. Either way the input is cleared and the
// key is eaten (*Result = 1); any other key passes (*Result = 0).
void CChatAccountDlg::OnMsgFilterInput(NMHDR* Header, LRESULT* Result)
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
			// Each node is cloned twice, as retail does: the handler clones its stack node into
			// a holder (004142f7..00414306, EH state 2), the list insert 004149a0 clones that
			// clone into the new cell, then the holder deletes the first clone (0041432a..
			// 00414340) before the stack node's destructor runs (00414344..0041434f).
			{
				StyleNode Style(ChatAccountStyleId);
				{
					const FChatRdlNodeHolder FirstClone(Style.Clone());
					AppendChatRdlNode(Rdl, FirstClone);
				}
			}
			// 00414355..004143be, the same sequence for the text (EH states 3/4).
			{
				TextNode Line(Text, -1);
				{
					const FChatRdlNodeHolder FirstClone(Line.Clone());
					AppendChatRdlNode(Rdl, FirstClone);
				}
			}

			BinaryRDLWriter Writer;
			unsigned int Size;
			if (Writer.write_buffer(Rdl, GChatAccountBuffer, sizeof(GChatAccountBuffer), Size))
			{
				CAccount* Account = Players.FindAccountFromName(AccountName);
				// Retail builds the target id from the low word of m_ClientID (+0x34) and
				// zeroes bytes 2 and 3 (0041440e..00414427).
				const CHAT_ID Target(Account != nullptr ? (unsigned short)Account->m_ClientID : 0u);
				Server.SubmitChat(GChatIdConsole, Size, GChatAccountBuffer, Target, -1);
			}
			// Retail releases the server lock before the list is destroyed (00414442).
			UnlockServer();
		}
	}

	InputEdit.SetSel(0, -1);
	::SendMessageA(InputEdit.m_hWnd, WM_CLEAR, 0, 0);
	*Result = 1;
}

// .text:004145a0 — runs on the server thread inside IServerImpl::SubmitChat. Scrolls the log
// to the new lines only when the log does not have the focus.
void __cdecl CChatAccountDlg::OnPrivateChat(wchar_t* Text, int ClientId)
{
	LockServer();
	CAccount* Account = Players.FindAccountFromClientID((unsigned int)ClientId);
	if (Account != nullptr)
	{
		std::map<CiWString, CChatAccountDlg*>::iterator It = GChatAccountDialogs.find(Account->m_Name);
		if (It != GChatAccountDialogs.end() && It->second != nullptr)
		{
			CChatAccountDlg* Dlg = It->second;
			const int LinesBefore = (int)::SendMessageA(Dlg->LogEdit.m_hWnd, EM_GETLINECOUNT, 0, 0);
			AppendRichEditText(Dlg->LogEdit.m_hWnd, Text);
			AppendRichEditText(Dlg->LogEdit.m_hWnd, L"\n");
			const int LinesAfter = (int)::SendMessageA(Dlg->LogEdit.m_hWnd, EM_GETLINECOUNT, 0, 0);
			if (::GetFocus() != Dlg->LogEdit.m_hWnd)
			{
				Dlg->LogEdit.LineScroll(LinesAfter - LinesBefore, 0);
			}
		}
	}
	UnlockServer();
}
