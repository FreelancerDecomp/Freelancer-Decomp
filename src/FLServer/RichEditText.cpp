// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "StdAfx.h"

#include <richedit.h>

#include "RichEditText.h"

int GetRichEditText(HWND Window, wchar_t* Buf, int BufBytes)
{
	GETTEXTEX Request;
	Request.cb = BufBytes;
	Request.flags = GT_DEFAULT;
	Request.codepage = 1200;
	Request.lpDefaultChar = NULL;
	Request.lpUsedDefChar = NULL;
	return (int)SendMessageA(Window, EM_GETTEXTEX, (WPARAM)&Request, (LPARAM)Buf);
}

void SetRichEditText(HWND Window, const wchar_t* Text)
{
	SETTEXTEX Request;
	Request.flags = ST_DEFAULT;
	Request.codepage = 1200;
	SendMessageA(Window, EM_SETTEXTEX, (WPARAM)&Request, (LPARAM)Text);
}

void AppendRichEditText(HWND Window, const wchar_t* Text)
{
	const LRESULT Length = SendMessageA(Window, WM_GETTEXTLENGTH, 0, 0);
	SendMessageA(Window, EM_SETSEL, (WPARAM)Length, (LPARAM)Length);
	SETTEXTEX Request;
	Request.flags = ST_SELECTION;
	Request.codepage = 1200;
	SendMessageA(Window, EM_SETTEXTEX, (WPARAM)&Request, (LPARAM)Text);
}
