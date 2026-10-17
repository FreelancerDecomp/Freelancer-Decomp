// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// FLServer.exe common header. Every FLServer translation unit includes this file FIRST:
// MFC must see its own windows.h configuration before any project header pulls windows.h in.
#pragma once

#ifndef VC_EXTRALEAN
#define VC_EXTRALEAN
#endif

#include <sdkddkver.h>

#include <afxwin.h>     // MFC core and standard components
#include <afxext.h>     // MFC extensions (CStatusBar, CToolBar)
#include <afxole.h>     // MFC OLE (COleDocument / COleClientItem used by the rich-edit doc/view)
#include <afxrich.h>    // CRichEditDoc / CRichEditView / CRichEditCntrItem
#include <afxcmn.h>     // common controls (list controls in the dialogs)
#include <afxdlgs.h>
