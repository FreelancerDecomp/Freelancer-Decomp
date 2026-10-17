// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Non-MFC TU on purpose: no StdAfx.h, so dalib_net.h and common_errorhandler.h can be included
// without the MFC CObject clash.
#include "CrashReport.h"

#include "dalib_net.h"           // CDPServer::CrashCleanup
#include "common_errorhandler.h" // ErrorHandler::deactivate

#include <windows.h>
#include <string.h>
#include <wchar.h>

namespace
{
	// .data:00426300 — the '|'-separated list of files Dr. Watson attaches, 0x410 wchar ending
	// where GCrashAppName begins. In FLServer.exe only AppendDxDiagReport writes it (all six
	// references are inside 00408b70..00408f5e), so it holds at most the dxdiag report.
	wchar_t GCrashReportFiles[0x410];

	// .data:00426b20 — the application name handed to Dr. Watson (0x80 wchar, the last one
	// forced to zero at .data:00426c1e).
	wchar_t GCrashAppName[0x80];

	// .text:00408b70..00408cbd: run "dxdiag.exe <temp>\dxdiag.txt", wait up to 60 s and, unless
	// it timed out, append the report path to GCrashReportFiles.
	void AppendDxDiagReport()
	{
		STARTUPINFOA StartupInfo;
		memset(&StartupInfo, 0, sizeof(StartupInfo));
		PROCESS_INFORMATION ProcessInfo;
		ProcessInfo.hProcess = NULL;
		ProcessInfo.hThread = NULL;
		ProcessInfo.dwProcessId = 0;
		StartupInfo.cb = 0x44;
		ProcessInfo.dwThreadId = 0;

		char ReportPath[0x104];
		GetTempPathA(0x104, ReportPath);
		strcat(ReportPath, "dxdiag.txt");

		char CommandLine[0x104];
		wsprintfA(CommandLine, "dxdiag.exe %s", ReportPath);
		if (CreateProcessA(NULL, CommandLine, NULL, NULL, FALSE, 0x4000020, NULL, NULL,
			&StartupInfo, &ProcessInfo) == 0)
		{
			return;
		}

		if (WaitForSingleObject(ProcessInfo.hProcess, 60000) != WAIT_TIMEOUT)
		{
			if (wcslen(GCrashReportFiles) != 0)
			{
				wcscat(GCrashReportFiles, L"|");
			}
			size_t Length = wcslen(GCrashReportFiles);
			MultiByteToWideChar(CP_UTF8, 0, ReportPath, -1, GCrashReportFiles + Length,
				(int)(0x410 - Length));
			// .text:00408c98: the terminator lands 0x104 wchar past the old end, not after the
			// appended text.
			GCrashReportFiles[Length + 0x104] = L'\0';
		}
		CloseHandle(ProcessInfo.hProcess);
		CloseHandle(ProcessInfo.hThread);
	}

	// .text:00408cc0..004090e9, the unhandled-exception filter. Breakpoint (0x80000003) and
	// single-step (0x80000004) go to the next handler. Everything else: close a hosted
	// DirectPlay session, then hand the crash to Dr. Watson (dw15.exe) through a 0x1c50-byte
	// shared block and wait for it. Returns EXCEPTION_EXECUTE_HANDLER unless dw15 says the
	// process may continue.
	LONG WINAPI CrashFilter(EXCEPTION_POINTERS* Info)
	{
		LONG Result = 1; // [ebp-0x1c]
		DWORD Code = Info->ExceptionRecord->ExceptionCode;
		if (Code == 0x80000003 || Code == 0x80000004)
		{
			return 0;
		}

		// .text:00408d0d..00408d32: its own __try whose filter (00408d22) returns 1.
		__try
		{
			CDPServer::CrashCleanup();
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
		}

		SECURITY_ATTRIBUTES Security;
		Security.lpSecurityDescriptor = NULL;
		Security.nLength = 0xc;
		Security.bInheritHandle = TRUE;
		HANDLE Mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, &Security, PAGE_READWRITE, 0,
			0x1c50, NULL);
		if (Mapping == NULL)
		{
			return Result;
		}

		unsigned char* Block = (unsigned char*)MapViewOfFile(Mapping,
			FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, 0);
		if (Block != NULL)
		{
			memset(Block, 0, 0x714 * 4);
			HANDLE EventDone = CreateEventA(&Security, FALSE, FALSE, NULL); // [ebp-0x30]
			if (EventDone != NULL)
			{
				HANDLE EventAlive = CreateEventA(&Security, FALSE, FALSE, NULL); // [ebp-0x34]
				if (EventAlive != NULL)
				{
					HANDLE Mutex = CreateMutexA(&Security, FALSE, NULL); // [ebp-0x38]
					if (Mutex != NULL)
					{
						if (DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(),
							GetCurrentProcess(), (HANDLE*)(Block + 0x24), 0x1f0fff, TRUE, 0) != 0)
						{
							DWORD* Words = (DWORD*)Block;
							Words[1] = GetCurrentProcessId();
							Words[2] = GetCurrentThreadId();
							*(HANDLE*)(Block + 0x1c) = EventDone;
							*(HANDLE*)(Block + 0x14) = EventAlive;
							*(HANDLE*)(Block + 0x20) = Mutex;
							Words[0] = 0x1c50;
							*(EXCEPTION_POINTERS**)(Block + 0x10) = Info;
							*(void**)(Block + 0xc) = Info->ExceptionRecord->ExceptionAddress;
							Words[0x34 / 4] = 1;
							Words[0x3c / 4] = 0x10;
							wcscpy((wchar_t*)(Block + 0x48), GCrashAppName);
							strcpy((char*)(Block + 0x1148), "Microsoft\\PCHealth\\ErrorReporting\\DW");
							strcpy((char*)(Block + 0x1040),
								"HKLM\\Software\\Microsoft\\Microsoft Games\\Freelancer\\1.0\\"
								"DigitalProductID");

							HKEY Key;
							if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
								"Software\\Microsoft\\Microsoft Games\\Freelancer\\1.0", 0,
								KEY_QUERY_VALUE, &Key) == ERROR_SUCCESS)
							{
								DWORD LangId;
								DWORD Size = 4;
								if (RegQueryValueExA(Key, "LangID", NULL, NULL, (BYTE*)&LangId,
									&Size) == ERROR_SUCCESS)
								{
									Words[0x1144 / 4] = LangId + 0x400;
								}
								RegCloseKey(Key);
							}

							Words[0x28 / 4] = 0x100;
							memcpy(Block + 0x1210, L"common.dll", 22);

							char ModulePath[0x104];
							GetModuleFileNameA(NULL, ModulePath, 0x104);
							MultiByteToWideChar(CP_UTF8, 0, ModulePath, -1,
								(wchar_t*)(Block + 0xb8), 0x104);

							AppendDxDiagReport();
							memcpy(Block + 0x1418, GCrashReportFiles,
								wcslen(GCrashReportFiles) * 2 + 2);

							ErrorHandler::deactivate();

							STARTUPINFOA StartupInfo;
							memset(&StartupInfo, 0, sizeof(StartupInfo));
							StartupInfo.cb = 0x44;
							PROCESS_INFORMATION ProcessInfo;
							ProcessInfo.hProcess = NULL;
							ProcessInfo.hThread = NULL;
							ProcessInfo.dwProcessId = 0;
							ProcessInfo.dwThreadId = 0;

							char Arguments[0x104];
							wsprintfA(Arguments, " -x -s %u", (UINT_PTR)Mapping);
							if (CreateProcessA("dw15.exe", Arguments, NULL, NULL, TRUE, 0x4000020,
								NULL, NULL, &StartupInfo, &ProcessInfo) == 0)
							{
								Result = 0;
							}
							else
							{
								// .text:00408ff9..00409083.
								bool bWaiting = true;
								do
								{
									if (WaitForSingleObject(EventDone, 20000) == WAIT_OBJECT_0)
									{
										if (WaitForSingleObject(EventAlive, 1) == WAIT_OBJECT_0)
										{
											bWaiting = false;
											if (Words[0x2c / 4] == 0x10)
											{
												Result = 0;
											}
										}
									}
									else
									{
										DWORD MutexWait = WaitForSingleObject(Mutex, 20000);
										if (MutexWait == WAIT_TIMEOUT)
										{
											bWaiting = false;
										}
										else if (MutexWait == WAIT_ABANDONED)
										{
											bWaiting = false;
											ReleaseMutex(Mutex);
										}
										else
										{
											if (WaitForSingleObject(EventDone, 1) != WAIT_OBJECT_0)
											{
												SetEvent(EventAlive);
												bWaiting = false;
											}
											else if (WaitForSingleObject(EventAlive, 1) == WAIT_OBJECT_0)
											{
												bWaiting = false;
											}
											ReleaseMutex(Mutex);
										}
									}
								} while (bWaiting);
							}
						}
						CloseHandle(Mutex);
					}
					CloseHandle(EventAlive);
				}
				CloseHandle(EventDone);
			}
			UnmapViewOfFile(Block);
		}
		CloseHandle(Mapping);
		return Result;
	}
}

// .text:004090f0..0040911c
void InstallCrashHandler(const wchar_t* AppName)
{
	wcsncpy(GCrashAppName, AppName, 0x80);
	GCrashAppName[0x7f] = L'\0';
	SetUnhandledExceptionFilter(CrashFilter);
}
