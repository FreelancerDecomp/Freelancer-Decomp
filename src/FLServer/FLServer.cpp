// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "StdAfx.h"

#include "FLServer.h"
#include "FLServerDoc.h"
#include "FLServerView.h"       // ServerLogf
#include "FLSummaryForm.h"
#include "MainFrm.h"
#include "AccountsDlg.h"
#include "ChoosePlayerDlg.h"
#include "SelectStarSystemDlg.h"
#include "UniverseChatDlg.h"
#include "ServerNewsDlg.h"
#include "ServerConfigDlg.h"
#include "FLServerCmdLine.h"    // CFLServerCommandLineInfo (ParseParam .text:00413880)
#include "Motd.h"               // GMotd
#include "ResourceStrings.h"    // AddResourceDll, LoadIdsString
#include "CrashReport.h"        // InstallCrashHandler (.text:004090f0)

#include "dalib_core.h"         // DALib::Startup / OpenData / CloseData / Shutdown, CriticalWarningFn
#include "dalib_net.h"          // CGunWrapper, IGunHost
#include "dacom.h"
#include "remoteclient.h"       // GetRemoteClientPort, SetServerLogFunction
#include "server_iserverimpl.h" // Server
#include "server_playerdb.h"    // Players
#include "common_ini.h"         // INI_Reader, ILoadingScreen, RegisterLoadingScreen
#include "common_misc.h"        // ParseGUID
#include "common_net.h"         // SetIsMPServer
#include "server_host.h"        // the host shared with FLDedicated (src/ServerHost)

#include <afxpriv.h>             // WM_INITIALUPDATE
#include <float.h>
#include <stdio.h>
#include <wchar.h>
#include <mmsystem.h>
#include <vector>

// FLServer.exe application TU (retail .text:0040ab90..0040d7ff, statics included).

CFLServerApp theApp;            // .data:00426c58 (constructed by .text:0040af50)
CFLServerApp* GApp = NULL;      // .data:004278b8

// .data:00427868 — guards GunWrapper and the gun thread's work (static init .text:0040af80).
HANDLE GAppMutex = CreateMutexA(NULL, FALSE, NULL);
// .data:0042788c — the named "FreelancerCoopMutex" (static init .text:0040af10). Nothing reads
// the handle; holding the named mutex for the process lifetime is its only effect.
HANDLE GCoopMutex = CreateMutexA(NULL, FALSE, "FreelancerCoopMutex");
// .data:0042789c — held by the server thread around IServerImpl::Update (static init
// .text:0040cf70); LockServer / UnlockServer below.
HANDLE GServerMutex = CreateMutexA(NULL, FALSE, NULL);

int GServerRunning = 0;         // .data:004278b4 — server and gun thread loop while set
HANDLE GServerThread = NULL;    // .data:004278ac
DWORD GServerThreadId = 0;      // .data:004278b0
HANDLE GGunThread = NULL;       // .data:004278bc

// .data:004278c4 — the CGunWrapper status the gun thread last logged.
static int GLastGunStatus = 0;
// .data:004278c8 — server frames run since startup (incremented by the server thread, no reader).
static unsigned int GServerFrameCount = 0;
// The frame period (.data:00425548) and the loading screen (.data:004278a0) live in
// src/ServerHost/server_host.cpp.

// .text:0040afa0
void LockApp()
{
	WaitForSingleObject(GAppMutex, INFINITE);
}

// .text:0040afb0
void UnlockApp()
{
	ReleaseMutex(GAppMutex);
}

// .text:0040cf90 — takes the server mutex; a thread other than the server thread keeps its
// message queue pumped while it waits.
void LockServer()
{
	if (GetCurrentThreadId() == GServerThreadId)
	{
		WaitForSingleObject(GServerMutex, INFINITE);
		return;
	}
	if (MsgWaitForMultipleObjects(1, &GServerMutex, FALSE, INFINITE, QS_ALLINPUT) == WAIT_OBJECT_0 + 1)
	{
		MSG Msg;
		do
		{
			while (PeekMessageA(&Msg, NULL, 0, 0, PM_NOREMOVE))
			{
				AfxGetApp()->PumpMessage();
			}
		} while (MsgWaitForMultipleObjects(1, &GServerMutex, FALSE, INFINITE, QS_ALLINPUT) == WAIT_OBJECT_0 + 1);
	}
}

// .text:0040d040
void UnlockServer()
{
	ReleaseMutex(GServerMutex);
}

// The wizard's About box (ctor inlined at .text:0040ca60, dtor 0040ca80, vtable
// .rdata:0041d1d0, empty DoDataExchange 0040cab0, empty message map .rdata:0041d018).
class CAboutDlg : public CDialog
{
public:
	enum { IDD = 100 };

	CAboutDlg() : CDialog(IDD, NULL) {}

protected:
	virtual void DoDataExchange(CDataExchange* DX) {}

	DECLARE_MESSAGE_MAP()
};

BEGIN_MESSAGE_MAP(CAboutDlg, CDialog)
END_MESSAGE_MAP()

// .rdata:0041cdb8 — ids from ServerResources.dll's menu 128.
BEGIN_MESSAGE_MAP(CFLServerApp, CWinApp)
	ON_COMMAND(ID_APP_ABOUT, &CFLServerApp::OnAppAbout)
	ON_COMMAND(32771, &CFLServerApp::OnDebugDebug)
	ON_COMMAND(32772, &CFLServerApp::OnDebugAiDebugger)
	ON_UPDATE_COMMAND_UI(32771, &CFLServerApp::OnUpdateDisabled)
	ON_UPDATE_COMMAND_UI(32774, &CFLServerApp::OnUpdateNeedsServer)
	ON_COMMAND(32774, &CFLServerApp::OnEditAccounts)
	ON_COMMAND(32777, &CFLServerApp::OnChatPlayer)
	ON_UPDATE_COMMAND_UI(32777, &CFLServerApp::OnUpdateNeedsServer)
	ON_COMMAND(32776, &CFLServerApp::OnChatStarSystem)
	ON_UPDATE_COMMAND_UI(32776, &CFLServerApp::OnUpdateNeedsServer)
	ON_COMMAND(32775, &CFLServerApp::OnChatUniverse)
	ON_UPDATE_COMMAND_UI(32775, &CFLServerApp::OnUpdateNeedsServer)
	ON_COMMAND(32778, &CFLServerApp::OnServerNews)
	ON_UPDATE_COMMAND_UI(32778, &CFLServerApp::OnUpdateNeedsServer)
	ON_COMMAND(32779, &CFLServerApp::OnDebugCrashMe)
	ON_UPDATE_COMMAND_UI(32779, &CFLServerApp::OnUpdateDisabled)
	ON_UPDATE_COMMAND_UI(32781, &CFLServerApp::OnUpdateViewConsole)
	ON_UPDATE_COMMAND_UI(32780, &CFLServerApp::OnUpdateViewStatusSummary)
	ON_COMMAND(32780, &CFLServerApp::OnSwitchView)
	ON_COMMAND(32781, &CFLServerApp::OnSwitchView)
	ON_COMMAND(ID_FILE_NEW, &CWinApp::OnFileNew)
	ON_COMMAND(ID_FILE_OPEN, &CWinApp::OnFileOpen)
	ON_COMMAND(ID_FILE_PRINT_SETUP, &CWinApp::OnFilePrintSetup)
END_MESSAGE_MAP()

// .text:0040b370
CFLServerApp::CFLServerApp()
	: CWinApp(NULL)
{
	GApp = this;
}

// .text:0040b420
CFLServerApp::~CFLServerApp()
{
	if (RichEditModule != NULL)
	{
		FreeLibrary(RichEditModule);
		RichEditModule = NULL;
	}
	GApp = NULL;
	for (int Index = 0; Index < OwnedStringCount; ++Index)
	{
		if (OwnedStrings[Index] != NULL)
		{
			delete OwnedStrings[Index];
		}
	}
}

// .text:0040b740 — installed as DALib's CriticalWarningFn by InitInstance. Shows the warning
// with Yes/No; anything but Yes ends the process.
static bool __cdecl CriticalWarning(unsigned int Code, const char* Text)
{
	wchar_t Title[0x100];
	wchar_t Message[0x1000];
	if (LoadIdsString(NULL, 0xd2, Title, 0x100) == 0)
	{
		wcscpy(Title, L"Freelancer Critical Warning");
	}
	if (LoadIdsString(NULL, Code, Message, 0x1000) == 0)
	{
		wcscpy(Message, L"Freelancer has encountered an unknown issue.\n");
	}
	if (Text != NULL)
	{
		wcscat(Message, L"\n\n");
		int Length = (int)wcslen(Message);
		MultiByteToWideChar(CP_ACP, 0, Text, -1, Message + Length, 0x1000 - Length);
	}
	if (MessageBoxW(NULL, Message, Title, MB_YESNO | MB_ICONQUESTION | MB_SETFOREGROUND) != IDYES)
	{
		exit(1);
	}
	return true;
}

// .text:0040be90 — runs IServerImpl::Startup; exit code 1 when it succeeded.
static UINT __cdecl StartupThreadProc(LPVOID)
{
	return RunServerStartup(theApp.ServerControl.StartupInfo);
}

// .text:0040b710 — runs IServerImpl::Shutdown.
static UINT __cdecl ShutdownThreadProc(LPVOID)
{
	return RunServerShutdown();
}

// .text:0040afc0 — the list-server (gun) thread: once a second, under GAppMutex, creates the
// CGunWrapper when there is none, pumps it and logs its status changes. Retail returns with a
// plain RET (cdecl) although CreateThread calls it stdcall; the port declares it WINAPI.
static DWORD WINAPI GunThreadProc(LPVOID)
{
	HRESULT Result = CoInitialize(NULL);
	unsigned int ConnectTime = 0;
	wchar_t VersionText[0x400];
	while (GServerRunning != 0)
	{
		WaitForSingleObject(GAppMutex, INFINITE);
		CGunWrapper* Wrapper = GApp->GunWrapper;
		if (Wrapper == NULL)
		{
			_swprintf(VersionText, L"%d", GServerVersion);
			FServerControl& Control = GApp->ServerControl;
			Wrapper = new CGunWrapper("..\\exe\\gundll.dll", Control.AppGuid,
				Control.ListServerHost.c_str(), Control.ListServerPort, VersionText);
			GApp->GunWrapper = Wrapper;
		}
		if (GApp->GunWrapper != NULL)
		{
			GApp->GunWrapper->Update();
			GApp->GunWrapper->DispatchQueue();
			int Status = (int)GApp->GunWrapper->GetStatus();
			if (Status != GLastGunStatus)
			{
				GLastGunStatus = Status;
				CString Text;
				switch (Status)
				{
				case 1:
					LoadStringChecked(Text, 0xf008);
					ServerLogf(ErrorCode{ 0x100004 }, (LPCTSTR)Text);
					break;
				case 4:
					LoadStringChecked(Text, 0xf009);
					ServerLogf(ErrorCode{ 0x100004 }, (LPCTSTR)Text);
					break;
				case 3:
					LoadStringChecked(Text, 0xf00a);
					ServerLogf(ErrorCode{ 0x100004 }, (LPCTSTR)Text);
					if (Wrapper != NULL)
					{
						delete Wrapper;
					}
					GApp->GunWrapper = NULL;
					break;
				case 2:
					ConnectTime = timeGetTime() / 1000;
					LoadStringChecked(Text, 0xf00b);
					ServerLogf(ErrorCode{ 0x100004 }, (LPCTSTR)Text);
					LoadStringChecked(Text, 0xf00c);
					ServerLogf(ErrorCode{ 0x100004 }, (LPCTSTR)Text);
					if (GApp != NULL)
					{
						GApp->UpdateGunReport();
					}
					break;
				}
			}
			// A connection is dropped and re-made after 13 hours (46800 s).
			if (Status == 2 && timeGetTime() / 1000 - ConnectTime > 46800)
			{
				if (Wrapper != NULL)
				{
					delete Wrapper;
				}
				GApp->GunWrapper = NULL;
			}
		}
		ReleaseMutex(GAppMutex);
		Sleep(1000);
	}
	if (Result == S_OK || Result == S_FALSE)
	{
		CoUninitialize();
	}
	return 0;
}

// SetupServerFpu (the FPU setup of InitInstance .text:0040bae3 and the server thread
// .text:0040cc6c) is in src/ServerHost/server_host.cpp.

// The server thread's FLServer parts; the loop itself is RunServerThread
// (src/ServerHost/server_host.cpp).
static bool IsServerRunning()
{
	return GServerRunning != 0;
}

// The part of .text:0040cb40 after motd.dat (from the FPU setup at .text:0040cc6c): the "running"
// line, logged at level 4, and the document the frames report to.
static void* OnServerLoopStart()
{
	CFLServerDoc* Document = NULL;
	if (GApp != NULL)
	{
		SetupServerFpu(GApp->ServerControl.bUnmaskFpuExceptions);
		int SavedLevel = GApp->ServerControl.LogLevel;
		GApp->ServerControl.SetLogLevel(4);
		CString Text;
		LoadStringChecked(Text, 0xf011);
		ServerLogf(ErrorCode{ 0x100004 }, (LPCTSTR)Text);
		GApp->ServerControl.SetLogLevel(SavedLevel);
		Document = (CFLServerDoc*)GApp->MainDocument;
	}
	return Document;
}

static void OnServerFrameRun()
{
	++GServerFrameCount;
}

// The frame time and ServerStats go to the document.
static void OnServerFrameDone(void* Context, DWORD FrameStart, DWORD FrameEnd)
{
	CFLServerDoc* Document = (CFLServerDoc*)Context;
	if (Document != NULL)
	{
		Document->SetLoad((int)(FrameEnd - FrameStart));
		ServerStats Stats;
		Server.GetServerStats(Stats);
		Document->SetStats(&Stats);
	}
}

// .text:0040cb40 — the server thread: loads motd.dat, then runs IServerImpl::Update /
// ElapseTime at 30 Hz under GServerMutex (LockServer, then ReleaseMutex), handing the run
// permission back and forth with the local client, and records the frame time and
// ServerStats in the document.
static DWORD WINAPI ServerThreadProc(LPVOID)
{
	FServerThreadHooks Hooks;
	Hooks.IsRunning = IsServerRunning;
	Hooks.Motd = &GMotd;
	Hooks.ApplyMotd = ApplyServerNews;
	Hooks.OnLoopStart = OnServerLoopStart;
	Hooks.LockServer = LockServer;
	Hooks.UnlockServer = UnlockServer;
	Hooks.OnFrameRun = OnServerFrameRun;
	Hooks.OnFrameDone = OnServerFrameDone;
	return RunServerThread(Hooks);
}


// .text:0040b840
BOOL CFLServerApp::InitInstance()
{
	HMODULE EulaModule = LoadLibraryA("..\\EBUEula.dll");
	if (EulaModule == NULL)
	{
		EulaModule = LoadLibraryA("EBUEula.dll");
		if (EulaModule == NULL)
		{
			MessageBoxA(NULL, "FreeLancer failed to attach to EBUEula.dll.", "Notice", MB_ICONHAND);
			return FALSE;
		}
	}
	typedef int (__cdecl* FEbuEula)(const char* RegistryKey, const char* EulaPath,
		const char* WarrantyPath, int Flag);
	FEbuEula EbuEula = (FEbuEula)GetProcAddress(EulaModule, "EBUEula");
	if (EbuEula == NULL)
	{
		FreeLibrary(EulaModule);
		MessageBoxA(NULL, "FreeLancer failed to find EBUEula entry point.", "Notice", MB_ICONHAND);
		return FALSE;
	}
	const char* WarrantyPath = GetFileAttributesA("..\\Warranty.rtf") != INVALID_FILE_ATTRIBUTES
		? "..\\Warranty.rtf" : NULL;
	int bAccepted = EbuEula("Software\\Microsoft\\Microsoft Games\\Freelancer\\1.0",
		"..\\EULA.rtf", WarrantyPath, 1);
	FreeLibrary(EulaModule);
	if (bAccepted == 0)
	{
		return FALSE;
	}

	CriticalWarningFn = CriticalWarning;

	char ModulePath[0x104] = {};
	GetModuleFileNameA(GetModuleHandleA(NULL), ModulePath, 0x104);
	GServerVersion = ReadServerVersion(ModulePath);

	HMODULE ResourceModule = LoadLibraryA("ServerResources.dll");
	if (ResourceModule != NULL)
	{
		AfxSetResourceHandle(ResourceModule);
	}

	SetIsMPServer(true);
	bStartupPending = true;
	bShuttingDown = false;

	if (!AfxOleInit())
	{
		AfxMessageBox(100, 0, (UINT)-1);
		return FALSE;
	}
	// .text:0040b9e4 CWinApp::Enable3dControls: obsolete in MFC 14 (3-D controls are always
	// on), so there is nothing to call.

	if (RichEditModule == NULL)
	{
		RichEditModule = LoadLibraryA("RICHED20.DLL");
		if (RichEditModule == NULL)
		{
			AfxMessageBox("Failed to load RichText DLL.", 0, 0);
			return FALSE;
		}
	}

	AddResourceDll(LoadLibraryA("resources.dll"));
	InstallCrashHandler(L"Freelancer-Server");   // .text:0040ba20 (u"" .data:004256a0)

	ServerControl.SetTitle("FLServer -");
	SetRegistryKey("Microsoft");
	LoadStdProfileSettings(4);
	ServerControl.SetServerPort(0);
	ServerControl.SetMaxPlayers(0x10);

	CSingleDocTemplate* DocTemplate = new CSingleDocTemplate(0x80,
		RUNTIME_CLASS(CFLServerDoc), RUNTIME_CLASS(CMainFrame), RUNTIME_CLASS(CFLServerView));
	DocTemplate->SetContainerInfo(6);
	AddDocTemplate(DocTemplate);

	CFLServerCommandLineInfo CommandLine;
	ParseCommandLine(CommandLine);

	SetupServerFpu(ServerControl.bUnmaskFpuExceptions);

	if (!ProcessShellCommand(CommandLine))
	{
		return FALSE;
	}

	CFrameWnd* Frame = (CFrameWnd*)m_pMainWnd;
	MainView = Frame->GetActiveView();
	SummaryForm = new CFLSummaryForm;
	MainDocument = Frame->GetActiveDocument();

	CCreateContext Context;
	Context.m_pCurrentDoc = MainDocument;
	CRect Rect(0, 0, 0, 0);
	{
		CString Name;
		LoadStringChecked(Name, 0xf00d);
		// .text:0040bc1f calls the virtual CWnd::Create (vtbl+0x5c); CFormView::Create is
		// protected in MFC 14, so dispatch through the public CWnd declaration.
		((CWnd*)SummaryForm)->Create(NULL, Name, WS_CHILD, Rect, m_pMainWnd, AFX_IDW_PANE_FIRST + 1, &Context);
		::SendMessageA(SummaryForm->m_hWnd, WM_INITIALUPDATE, 0, 0);
	}
	SwitchView();
	if (SummaryForm != NULL)
	{
		SummaryForm->ResizeParentToFit(FALSE);
	}
	m_pMainWnd->ShowWindow(SW_SHOW);

	RegisterLoadingScreen(GetServerLoadingScreen());

	ServerControl.SetIniPath(ModulePath);
	ServerControl.SetClientModulePath("RemoteClient.dll");
	{
		int SavedLevel = ServerControl.LogLevel;
		ServerControl.SetLogLevel(4);
		CString Text;
		LoadStringChecked(Text, 0xf00e);
		ServerLogf(ErrorCode{ 0x100004 }, (LPCTSTR)Text);
		ServerControl.SetLogLevel(SavedLevel);
	}

	::UpdateWindow(m_pMainWnd->m_hWnd);
	::RedrawWindow(m_pMainWnd->m_hWnd, NULL, NULL, RDW_UPDATENOW | RDW_ALLCHILDREN);
	::RedrawWindow(SummaryForm->m_hWnd, NULL, NULL, RDW_UPDATENOW | RDW_ALLCHILDREN);

	if (!DALib::Startup(NULL, "DACOMsrv.ini"))
	{
		return FALSE;
	}
	// Retail passes theApp's own buffers (.data:00427133 / 00427237).
	ReadFreelancerIni(ServerControl.IniPath);
	ReadFreelancerIni(ServerControl.LocalServerIniPath);

	CServerConfigDlg ConfigDlg(NULL);
	if (ConfigDlg.DoModal() != IDOK)
	{
		return FALSE;
	}
	::UpdateWindow(m_pMainWnd->m_hWnd);
	SetServerLogFunction(ServerLogf);
	DoWaitCursor(1);
	StartupThread = AfxBeginThread(StartupThreadProc, NULL, THREAD_PRIORITY_NORMAL, 0, CREATE_SUSPENDED, NULL);
	if (StartupThread == NULL)
	{
		return FALSE;
	}
	StartupThread->m_bAutoDelete = FALSE;
	::ResumeThread(StartupThread->m_hThread);
	return TRUE;
}

// .text:0040b6f0
int CFLServerApp::ExitInstance()
{
	DALib::CloseData();
	DALib::Shutdown(NULL);
	return 0;
}

// .text:0040d670 — PollStartupThread inlined, its result unused.
BOOL CFLServerApp::OnIdle(LONG Count)
{
	PollStartupThread();
	return CWinApp::OnIdle(Count);
}

// .text:0040b510
bool CFLServerApp::BeginShutdown()
{
	if (bShuttingDown)
	{
		return true;
	}
	bShuttingDown = true;
	if (GServerRunning != 0)
	{
		GServerRunning = 0;
		if (GServerThread != NULL)
		{
			// Up to 600 waits of 100 ms, pumping the UI between them; a failed pump or the
			// last wait kills the server thread.
			int Tries = 600;
			if (WaitForSingleObject(GServerThread, 100) == WAIT_TIMEOUT)
			{
				for (;;)
				{
					MSG Msg;
					if (PeekMessageA(&Msg, NULL, 0, 0, PM_NOREMOVE))
					{
						do
						{
							if (!PumpMessage())
							{
								TerminateThread(GServerThread, 0);
								break;
							}
						} while (PeekMessageA(&Msg, NULL, 0, 0, PM_NOREMOVE));
					}
					if (--Tries == 0)
					{
						TerminateThread(GServerThread, 0);
						break;
					}
					if (WaitForSingleObject(GServerThread, 100) != WAIT_TIMEOUT)
					{
						break;
					}
				}
			}
			CloseHandle(GServerThread);
			GServerThread = NULL;
		}
		if (GGunThread != NULL)
		{
			WaitForSingleObject(GGunThread, INFINITE);
			CloseHandle(GGunThread);
			GGunThread = NULL;
		}
	}

	LockApp();
	if (GunWrapper != NULL)
	{
		delete GunWrapper;
		GunWrapper = NULL;
	}
	UnlockApp();

	ShutdownThread = AfxBeginThread(ShutdownThreadProc, NULL, THREAD_PRIORITY_HIGHEST, 0, CREATE_SUSPENDED, NULL);
	if (ShutdownThread != NULL)
	{
		ShutdownThread->m_bAutoDelete = FALSE;
		::ResumeThread(ShutdownThread->m_hThread);
		ShutdownDlg.SetWaitThread(CShutdownDlg::WaitShutdownThread);
		ShutdownDlg.DoModal();
		return true;
	}
	Server.Shutdown();
	return true;
}

// .text:0040b6a0
bool CFLServerApp::PollShutdownThread()
{
	if (ShutdownThread != NULL)
	{
		if (WaitForSingleObject(ShutdownThread->m_hThread, 0) == WAIT_TIMEOUT)
		{
			return false;
		}
		if (ShutdownThread != NULL)
		{
			delete ShutdownThread;
		}
		ShutdownThread = NULL;
		bShuttingDown = false;
	}
	return true;
}

// .text:0040c100
bool CFLServerApp::PollStartupThread()
{
	if (StartupThread == NULL)
	{
		return true;
	}
	DWORD ExitCode;
	bool bStarted = false;
	if (GetExitCodeThread(StartupThread->m_hThread, &ExitCode))
	{
		if (ExitCode == STILL_ACTIVE)
		{
			return false;
		}
		bStarted = ExitCode == 1;
		if (StartupThread != NULL)
		{
			delete StartupThread;
		}
		StartupThread = NULL;
		DoWaitCursor(-1);
		bStartupPending = false;
	}
	return OnServerStartupFinished(bStarted);
}

// .text:0040bf20
void CFLServerApp::WaitForStartupThread()
{
	if (bStartupPending)
	{
		ShutdownDlg.SetWaitThread(CShutdownDlg::WaitStartupThread);
		ShutdownDlg.DoModal();
	}
}

// .text:0040bf50
bool CFLServerApp::OnServerStartupFinished(bool bStarted)
{
	if (bStarted)
	{
		GServerRunning = 1;
		int SavedLevel = ServerControl.LogLevel;
		ServerControl.SetLogLevel(4);
		{
			CString Text;
			LoadStringChecked(Text, 0xf00f);
			ServerLogf(ErrorCode{ 0x100004 }, (LPCTSTR)Text);
		}
		ServerControl.SetLogLevel(SavedLevel);
		// KERNEL32 CreateThread (.text:0040bff7); the CWinThread member hides the name here.
		GServerThread = ::CreateThread(NULL, 0, ServerThreadProc, NULL, 0, &GServerThreadId);
		if (GServerThread == NULL)
		{
			GServerRunning = 0;
			return false;
		}
		GGunThread = ServerControl.bListServerReporting
			? ::CreateThread(NULL, 0, GunThreadProc, NULL, 0, NULL) : NULL;
	}
	PublishServerSession(ServerControl.ServerName.c_str(), ServerControl.ServerPassword.c_str(),
		ServerControl.GetAllowPvp() != 0, ServerControl.GetAllowNewPlayers() != 0, GServerVersion,
		ServerControl.ServerDescription.c_str());
	((CMainFrame*)m_pMainWnd)->OpenBandwidthLog(ServerControl.BandwidthLogPath);
	return true;
}

// .text:0040c190 — returns whether a [Freelancer] "Data path" was found (and opened).
bool CFLServerApp::ReadFreelancerIni(const char* Path)
{
	INI_Reader Reader;
	bool bDataPath = false;
	if (Reader.open(Path, false))
	{
		while (Reader.read_header())
		{
			if (Reader.is_header("Freelancer"))
			{
				while (Reader.read_value())
				{
					if (Reader.is_value("Data path"))
					{
						DALib::OpenData(Reader.get_value_string());
						bDataPath = true;
					}
					else if (Reader.is_value("AppGUID"))
					{
						GUID Guid;
						ServerControl.SetAppGuid(ParseGUID(Reader.get_value_string(), &Guid));
					}
				}
			}
			else if (Reader.is_header("ListServer"))
			{
				while (Reader.read_value())
				{
					if (Reader.is_value("hostname"))
					{
						ServerControl.SetListServerHost(Reader.get_value_string());
					}
					else if (Reader.is_value("port"))
					{
						ServerControl.SetListServerPort((int)Reader.get_value_float(0));
					}
				}
			}
			else if (Reader.is_header("Server"))
			{
				// Retail tests "death_penalty" and ignores the answer (.text:0040c330..0040c343).
				while (Reader.read_value())
				{
					Reader.is_value("death_penalty");
				}
			}
			else if (Reader.is_header("Resources"))
			{
				while (Reader.read_value())
				{
					if (Reader.is_value("DLL"))
					{
						AddResourceDll(LoadLibraryA(Reader.get_value_string()));
					}
				}
			}
		}
	}
	return bDataPath;
}

// .text:0040c6f0 — publishes the session description to the list server through the gun
// host: the session name, then "GameOpts" (ports, max players, password flag, PvP, new
// players, signature, server id) and "GameDesc" (the description).
bool CFLServerApp::UpdateGunReport()
{
	if (!ServerControl.bListServerReporting)
	{
		return true;
	}
	bool bResult = true;
	WaitForSingleObject(GAppMutex, INFINITE);
	IGunHost* Host = GunWrapper != NULL ? GunWrapper->GetHost() : NULL;
	if (Host != NULL && Host->Slot03(ServerControl.ServerName.c_str()) == 0)
	{
		wchar_t Text[0x400];
		wchar_t* Cursor = Text;
		std::vector<unsigned long> Ports;
		GetRemoteClientPort(Ports);
		std::vector<unsigned long>::iterator It = Ports.begin();
		while (It != Ports.end())
		{
			Cursor += _swprintf(Cursor, L"%u", *It);
			++It;
			if (It == Ports.end())
			{
				break;
			}
			wcscat(Cursor, L",");
			Cursor += 1;
		}
		const wchar_t* Password = ServerControl.ServerPassword.c_str();
		_swprintf(Cursor, L":%d:%d:%d:%d:%S:%d", ServerControl.StartupInfo.MaxPlayers,
			wcslen(Password) > 0 ? 1 : 0, (int)ServerControl.GetAllowPvp(),
			(int)ServerControl.GetAllowNewPlayers(), Server.GetServerSig(), Server.GetServerID());
		Host->Slot09(0, "GameOpts", Text);
		Host->Slot09(0, "GameDesc", ServerControl.ServerDescription.c_str());
	}
	else
	{
		bResult = false;
	}
	ReleaseMutex(GAppMutex);
	return bResult;
}

LPCTSTR CFLServerApp::GetServerTitle() const
{
	// app +0x704 == ServerControl +0x638.
	return ServerControl.Title;
}

// .text:0040d420
CView* CFLServerApp::SwitchView()
{
	CFrameWnd* Frame = (CFrameWnd*)m_pMainWnd;
	CView* OldView = Frame->GetActiveView();
	CView* NewView = MainView;
	if (OldView == MainView)
	{
		::GetWindowRect(m_pMainWnd->m_hWnd, &ConsoleFrameRect);
		NewView = SummaryForm;
	}

	// The two views swap control ids so the active one is always AFX_IDW_PANE_FIRST.
	LONG OldId = ::GetWindowLongA(OldView->m_hWnd, GWL_ID);
	LONG NewId = ::GetWindowLongA(NewView->m_hWnd, GWL_ID);
	::SetWindowLongA(OldView->m_hWnd, GWL_ID, NewId);
	::SetWindowLongA(NewView->m_hWnd, GWL_ID, OldId);
	OldView->ShowWindow(SW_HIDE);
	NewView->ShowWindow(SW_SHOW);

	HWND FrameWnd = m_pMainWnd->GetSafeHwnd();
	if (NewView == SummaryForm)
	{
		::SetWindowLongA(FrameWnd, GWL_STYLE, ::GetWindowLongA(FrameWnd, GWL_STYLE) & ~WS_THICKFRAME);
	}
	else
	{
		::SetWindowLongA(FrameWnd, GWL_STYLE, ::GetWindowLongA(FrameWnd, GWL_STYLE) | WS_THICKFRAME);
	}

	((CFrameWnd*)m_pMainWnd)->SetActiveView(NewView, TRUE);
	((CFrameWnd*)m_pMainWnd)->RecalcLayout(TRUE);
	::InvalidateRect(NewView->m_hWnd, NULL, TRUE);

	if (NewView == SummaryForm)
	{
		SummaryForm->ResizeParentToFit(FALSE);
		return OldView;
	}
	m_pMainWnd->SetWindowPos(NULL, 0, 0, ConsoleFrameRect.right - ConsoleFrameRect.left,
		ConsoleFrameRect.bottom - ConsoleFrameRect.top, SWP_NOMOVE | SWP_NOZORDER);
	return OldView;
}

// .text:0040cae0
void CFLServerApp::OnAppAbout()
{
	CAboutDlg AboutDlg;
	AboutDlg.DoModal();
}

// .text:0040cf50
void CFLServerApp::OnDebugDebug()
{
}

// .text:0040cf40
void CFLServerApp::OnDebugAiDebugger()
{
}

// .text:0040cf60 / 0040d410
void CFLServerApp::OnUpdateDisabled(CCmdUI* CmdUI)
{
	CmdUI->Enable(FALSE);
}

// .text:0040d050 / 0040d230 / 0040d290 / 0040d2f0 / 0040d3e0 — enabled once the startup
// thread has finished.
void CFLServerApp::OnUpdateNeedsServer(CCmdUI* CmdUI)
{
	CmdUI->Enable(StartupThread == NULL);
}

// .text:0040d070 — modal "Manage Accounts & Characters" (the dialog's dtor is inlined at
// 0040d0aa..0040d131; its out-of-line copy is 0040d150).
void CFLServerApp::OnEditAccounts()
{
	CAccountsDlg AccountsDlg(NULL);
	AccountsDlg.DoModal();
}

// .text:0040d1f0 — the modeless "Choose A Player" box: brought to the front when it already
// exists, created otherwise.
void CFLServerApp::OnChatPlayer()
{
	if (GChoosePlayerDlg.m_hWnd != NULL)
	{
		GChoosePlayerDlg.ShowWindow(SW_SHOW);
		::BringWindowToTop(GChoosePlayerDlg.m_hWnd);
		return;
	}
	GChoosePlayerDlg.Create(CChoosePlayerDlg::IDD, NULL);
	GChoosePlayerDlg.ShowWindow(SW_SHOW);
}

// .text:0040d250 — the modeless "Select A Star System" box, as OnChatPlayer.
void CFLServerApp::OnChatStarSystem()
{
	if (GSelectStarSystemDlg.m_hWnd != NULL)
	{
		GSelectStarSystemDlg.ShowWindow(SW_SHOW);
		::BringWindowToTop(GSelectStarSystemDlg.m_hWnd);
		return;
	}
	GSelectStarSystemDlg.Create(CSelectStarSystemDlg::IDD, NULL);
	GSelectStarSystemDlg.ShowWindow(SW_SHOW);
}

// .text:0040d2b0 — the modeless universe chat box (dialog 0x92, .data:0042a6a8), as
// OnChatPlayer.
void CFLServerApp::OnChatUniverse()
{
	if (GUniverseChatDlg.m_hWnd != NULL)
	{
		GUniverseChatDlg.ShowWindow(SW_SHOW);
		::BringWindowToTop(GUniverseChatDlg.m_hWnd);
		return;
	}
	GUniverseChatDlg.Create(CUniverseChatDlg::IDD, NULL);
	GUniverseChatDlg.ShowWindow(SW_SHOW);
}

// .text:0040d310 — modal "Edit Server News" (dtor inlined at 0040d34a..0040d36d, out-of-line
// copy 0040d390).
void CFLServerApp::OnServerNews()
{
	CServerNewsDlg NewsDlg(NULL);
	NewsDlg.DoModal();
}

// .text:0040d400 — "Crash Me": a deliberate write through a null pointer.
void CFLServerApp::OnDebugCrashMe()
{
	*(volatile char*)0 = 0;
}

// .text:0040d580
void CFLServerApp::OnUpdateViewConsole(CCmdUI* CmdUI)
{
	if (((CFrameWnd*)m_pMainWnd)->GetActiveView() == MainView)
	{
		CmdUI->Enable(FALSE);
		CmdUI->SetCheck(1);
		return;
	}
	CmdUI->Enable(TRUE);
	CmdUI->SetCheck(0);
}

// .text:0040d5c0
void CFLServerApp::OnUpdateViewStatusSummary(CCmdUI* CmdUI)
{
	if (((CFrameWnd*)m_pMainWnd)->GetActiveView() == SummaryForm)
	{
		CmdUI->Enable(FALSE);
		CmdUI->SetCheck(1);
		return;
	}
	CmdUI->Enable(TRUE);
	CmdUI->SetCheck(0);
}

// .text:0040d600
void CFLServerApp::OnSwitchView()
{
	SwitchView();
}
