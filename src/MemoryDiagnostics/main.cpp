// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "diagnostics.h"
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <psapi.h>
#include <algorithm>
#include <memory>
#include <process.h>
#ifdef MD_UI_TEST
#include <cstdio>
#endif

using namespace MemoryDiagnostics;

namespace
{
	enum EControl
	{
		GameEdit = 100, GameBrowse, ArgumentsEdit, LogsEdit, LogsBrowse, ToolsEdit, ToolsBrowse,
		SymbolsEdit, SymbolsBrowse, WindowsSymbols, EnableUst, DisableUst, Start, Attach, PidEdit, Capture,
		Compare, OpenFile, OpenReport, OpenFolder, SnapshotList, Cancel, Help, StateText, ProcessText, ActivityText
	};
	enum class EAction { Enable, Disable, Snapshot, Comparison };
	constexpr UINT GFinishedMessage = WM_APP + 1;

	struct FJob
	{
		HWND Owner = nullptr;
		EAction Action = EAction::Snapshot;
		FSettings Settings;
		std::wstring Destination;
		std::wstring First;
		std::wstring Second;
		DWORD ProcessId = 0;
		FHandle Process;
		FHandle CancelEvent;
		FToolResult Result;
	};

	struct FApplication
	{
		HWND Window = nullptr;
		HFONT Font = nullptr;
		FHandle Process;
		DWORD ProcessId = 0;
		std::wstring Session;
		std::wstring LastReport;
		std::vector<std::wstring> Snapshots;
		bool bBusy = false;
		bool bCloseAfterCancel = false;
		bool bUstKnownAtStart = false;
		bool bUstAtStart = false;
		FJob* Job = nullptr;
	};
	FApplication GApplication;

	std::wstring Text(int Id)
	{
		HWND Control = GetDlgItem(GApplication.Window, Id);
		const int Length = GetWindowTextLengthW(Control);
		std::vector<wchar_t> Buffer(static_cast<size_t>(Length) + 1);
		GetWindowTextW(Control, Buffer.data(), Length + 1);
		return Buffer.data();
	}

	void SetText(int Id, const std::wstring& Value)
	{
		SetDlgItemTextW(GApplication.Window, Id, Value.c_str());
	}

	FSettings Settings()
	{
		FSettings Value;
		Value.GamePath = FullPath(Text(GameEdit));
		Value.GameArguments = Text(ArgumentsEdit);
		Value.LogsPath = Text(LogsEdit).empty() ? L"" : FullPath(Text(LogsEdit));
		Value.ToolsPath = Text(ToolsEdit).empty() ? L"" : FullPath(Text(ToolsEdit));
		Value.SymbolsPath = Text(SymbolsEdit);
		Value.bWindowsSymbols = SendDlgItemMessageW(GApplication.Window, WindowsSymbols, BM_GETCHECK, 0, 0) == BST_CHECKED;
		return Value;
	}

	void ShowError(const std::wstring& Value)
	{
		SetText(ActivityText, Value);
		MessageBoxW(GApplication.Window, Value.c_str(), L"Диагностика памяти", MB_OK | MB_ICONERROR);
	}

	bool IsRunning()
	{
		return GApplication.Process.Get() && WaitForSingleObject(GApplication.Process.Get(), 0) == WAIT_TIMEOUT;
	}

	void OpenPath(const std::wstring& Path)
	{
		const HINSTANCE Result = ShellExecuteW(GApplication.Window, L"open", Path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
		if (reinterpret_cast<INT_PTR>(Result) <= 32) ShowError(L"Не удалось открыть: " + Path);
	}

	void Browse(int Id, bool bFile)
	{
		if (bFile)
		{
			wchar_t Buffer[32768] = {};
			wcsncpy_s(Buffer, Text(Id).c_str(), _TRUNCATE);
			OPENFILENAMEW Dialog = { sizeof(Dialog) };
			Dialog.hwndOwner = GApplication.Window;
			Dialog.lpstrFile = Buffer;
			Dialog.nMaxFile = 32768;
			Dialog.lpstrFilter = L"Приложения (*.exe)\0*.exe\0Все файлы\0*.*\0";
			Dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
			if (GetOpenFileNameW(&Dialog)) SetText(Id, Buffer);
		}
		else
		{
			BROWSEINFOW Dialog = {};
			Dialog.hwndOwner = GApplication.Window;
			Dialog.lpszTitle = L"Выберите папку";
			Dialog.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
			PIDLIST_ABSOLUTE Item = SHBrowseForFolderW(&Dialog);
			if (Item)
			{
				wchar_t Path[MAX_PATH] = {};
				if (SHGetPathFromIDListW(Item, Path)) SetText(Id, Path);
				CoTaskMemFree(Item);
			}
		}
	}

	std::wstring SessionDirectory()
	{
		return Settings().LogsPath + L"\\" + GApplication.Session;
	}

	void LoadSnapshots()
	{
		GApplication.Snapshots.clear();
		SendDlgItemMessageW(GApplication.Window, SnapshotList, LB_RESETCONTENT, 0, 0);
		if (GApplication.Session.empty()) return;
		const std::wstring Folder = SessionDirectory();
		WIN32_FIND_DATAW Entry = {};
		HANDLE Search = FindFirstFileW((Folder + L"\\snapshot-*.txt").c_str(), &Entry);
		if (Search != INVALID_HANDLE_VALUE)
		{
			do
			{
				if (!(Entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
					GApplication.Snapshots.push_back(Folder + L"\\" + Entry.cFileName);
			} while (FindNextFileW(Search, &Entry));
			FindClose(Search);
		}
		std::sort(GApplication.Snapshots.begin(), GApplication.Snapshots.end());
		for (size_t Index = 0; Index < GApplication.Snapshots.size(); ++Index)
			SendDlgItemMessageW(GApplication.Window, SnapshotList, LB_ADDSTRING, 0,
				reinterpret_cast<LPARAM>(Filename(GApplication.Snapshots[Index]).c_str()));
		// The usual next action is to compare the two most recent snapshots.
		const size_t Count = GApplication.Snapshots.size();
		if (Count > 0) SendDlgItemMessageW(GApplication.Window, SnapshotList, LB_SETSEL, TRUE, Count - 1);
		if (Count > 1) SendDlgItemMessageW(GApplication.Window, SnapshotList, LB_SETSEL, TRUE, Count - 2);
	}

	void Refresh()
	{
		bool bEnabled = false;
		std::wstring Error;
		const bool bRead = ReadUst(Filename(Settings().GamePath), bEnabled, Error);
		SetText(StateText, bRead ? (bEnabled ? L"Сбор стеков: ВКЛЮЧЁН для новых запусков" : L"Сбор стеков: ВЫКЛЮЧЕН для новых запусков") :
			L"Состояние сбора стеков неизвестно: " + Error);
		const bool bRunning = IsRunning();
		std::wstring ProcessStatus = L"Игра не подключена.";
		if (GApplication.Process.Get())
		{
			ProcessStatus = L"PID " + std::to_wstring(GApplication.ProcessId);
			if (bRunning)
			{
				PROCESS_MEMORY_COUNTERS_EX Memory = {};
				Memory.cb = sizeof(Memory);
				if (GetProcessMemoryInfo(GApplication.Process.Get(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&Memory), sizeof(Memory)))
					ProcessStatus += L" | Private Bytes: " + std::to_wstring(Memory.PrivateUsage / (1024 * 1024)) +
					L" МБ | Working Set: " + std::to_wstring(Memory.WorkingSetSize / (1024 * 1024)) + L" МБ";
				ProcessStatus += GApplication.bUstKnownAtStart ?
					(GApplication.bUstAtStart ? L" | запуск со сбором стеков" : L" | запуск БЕЗ сбора стеков") :
					L" | режим при запуске неизвестен";
			}
			else ProcessStatus += L" завершился; снимки сохранены.";
		}
		SetText(ProcessText, ProcessStatus);
		const int ConfigControls[] = { GameEdit, GameBrowse, ArgumentsEdit, LogsEdit, LogsBrowse,
			ToolsEdit, ToolsBrowse, SymbolsEdit, SymbolsBrowse, WindowsSymbols, EnableUst, DisableUst, Attach, PidEdit };
		for (size_t Index = 0; Index < sizeof(ConfigControls) / sizeof(ConfigControls[0]); ++Index)
			EnableWindow(GetDlgItem(GApplication.Window, ConfigControls[Index]), !GApplication.bBusy);
		EnableWindow(GetDlgItem(GApplication.Window, GameEdit), !GApplication.bBusy && !bRunning);
		EnableWindow(GetDlgItem(GApplication.Window, GameBrowse), !GApplication.bBusy && !bRunning);
		EnableWindow(GetDlgItem(GApplication.Window, Start), !GApplication.bBusy && !bRunning);
		EnableWindow(GetDlgItem(GApplication.Window, Capture), !GApplication.bBusy && bRunning);
		const LRESULT Selected = SendDlgItemMessageW(GApplication.Window, SnapshotList, LB_GETSELCOUNT, 0, 0);
		EnableWindow(GetDlgItem(GApplication.Window, Compare), !GApplication.bBusy && Selected == 2);
		EnableWindow(GetDlgItem(GApplication.Window, OpenFile), !GApplication.bBusy && (Selected > 0 || !GApplication.LastReport.empty()));
		EnableWindow(GetDlgItem(GApplication.Window, OpenReport), !GApplication.bBusy && !GApplication.LastReport.empty());
		EnableWindow(GetDlgItem(GApplication.Window, Cancel), GApplication.bBusy && GApplication.Job &&
			(GApplication.Job->Action == EAction::Snapshot || GApplication.Job->Action == EAction::Comparison));
	}

	std::wstring Timestamp()
	{
		SYSTEMTIME Time = {};
		GetLocalTime(&Time);
		wchar_t Buffer[80] = {};
		swprintf_s(Buffer, L"%04u%02u%02u-%02u%02u%02u-%03u", Time.wYear, Time.wMonth, Time.wDay,
			Time.wHour, Time.wMinute, Time.wSecond, Time.wMilliseconds);
		return Buffer;
	}

	unsigned __stdcall Worker(void* Parameter)
	{
		const HRESULT Com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
		FJob* Job = static_cast<FJob*>(Parameter);
		const bool bFlags = Job->Action == EAction::Enable || Job->Action == EAction::Disable;
		std::wstring Arguments;
		if (bFlags)
			Arguments = L"/i " + QuoteArgument(Filename(Job->Settings.GamePath)) +
			(Job->Action == EAction::Enable ? L" +ust" : L" -ust");
		else if (Job->Action == EAction::Snapshot)
			Arguments = L"-p:" + std::to_wstring(Job->ProcessId) + L" " + QuoteArgument(L"-f:" + Job->Destination + L".pending");
		else Arguments = L"-d " + QuoteArgument(Job->First) + L" " + QuoteArgument(Job->Second);
		Job->Result = RunTool(Job->Settings.ToolsPath + (bFlags ? L"\\gflags.exe" : L"\\umdh.exe"),
			Arguments, Job->Settings, bFlags ? L"" :
			(Job->Action == EAction::Snapshot ? Job->Destination + L".tool.txt" : Job->Destination),
			bFlags, Job->Owner, Job->CancelEvent.Get());
		if (Job->Result.bSuccess && bFlags)
		{
			bool bEnabled = false;
			std::wstring Error;
			if (!ReadUst(Filename(Job->Settings.GamePath), bEnabled, Error) || bEnabled != (Job->Action == EAction::Enable))
			{
				Job->Result.bSuccess = false; Job->Result.Error = L"GFlags не изменил ожидаемый флаг. " + Error;
			}
		}
		if (Job->Result.bSuccess && Job->Action == EAction::Snapshot)
		{
			WIN32_FILE_ATTRIBUTE_DATA Info = {};
			if (WaitForSingleObject(Job->Process.Get(), 0) != WAIT_TIMEOUT)
			{
				Job->Result.bSuccess = false; Job->Result.Error = L"Игра завершилась во время снимка; файл не добавлен в список.";
			}
			else if (!GetFileAttributesExW((Job->Destination + L".pending").c_str(), GetFileExInfoStandard, &Info) ||
				(Info.nFileSizeHigh == 0 && Info.nFileSizeLow == 0))
			{
				Job->Result.bSuccess = false; Job->Result.Error = L"UMDH не создал непустой снимок. Откройте файл .tool.txt в папке логов.";
			}
			else if (!MoveFileW((Job->Destination + L".pending").c_str(), Job->Destination.c_str()))
			{
				Job->Result.bSuccess = false; Job->Result.Error = ErrorText(GetLastError());
			}
		}
		PostMessageW(Job->Owner, GFinishedMessage, 0, reinterpret_cast<LPARAM>(Job));
		if (SUCCEEDED(Com)) CoUninitialize();
		return 0;
	}

	void BeginJob(EAction Action)
	{
		if (GApplication.bBusy) return;
		std::unique_ptr<FJob> Job(new FJob);
		Job->Owner = GApplication.Window;
		Job->Action = Action;
		Job->Settings = Settings();
		const bool bFlags = Action == EAction::Enable || Action == EAction::Disable;
		if (Text(GameEdit).empty()) { ShowError(L"Выберите EXE игры."); return; }
		if (bFlags && !IsX86Executable(Job->Settings.GamePath))
		{
			ShowError(L"Для настройки сбора стеков выберите существующий Win32 EXE клиента."); return;
		}
		if (!IsX86Executable(Job->Settings.ToolsPath + (bFlags ? L"\\gflags.exe" : L"\\umdh.exe")))
		{
			ShowError(L"Не найден инструмент x86. Укажите папку Debugging Tools for Windows, содержащую umdh.exe и gflags.exe.");
			return;
		}
		std::wstring Error;
		if (!bFlags)
		{
			if (GApplication.Session.empty() || !EnsureDirectory(SessionDirectory(), Error))
			{
				ShowError(Error.empty() ? L"Сначала запустите или подключите игру." : Error); return;
			}
			const std::wstring Stem = SessionDirectory() + L"\\" + (Action == EAction::Snapshot ? L"snapshot-" : L"compare-") + Timestamp();
			Job->Destination = Stem + L".txt";
			Job->CancelEvent.Reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
			if (!Job->CancelEvent.Get()) { ShowError(ErrorText(GetLastError())); return; }
		}
		if (Action == EAction::Snapshot)
		{
			if (!IsRunning()) { ShowError(L"Игра уже завершилась."); return; }
			if (GApplication.bUstKnownAtStart && !GApplication.bUstAtStart)
			{
				ShowError(L"Игра запущена без сбора стеков. Включите сбор, закройте игру и запустите её снова."); return;
			}
			HANDLE Duplicate = nullptr;
			if (!DuplicateHandle(GetCurrentProcess(), GApplication.Process.Get(), GetCurrentProcess(), &Duplicate, 0, FALSE, DUPLICATE_SAME_ACCESS))
			{
				ShowError(ErrorText(GetLastError())); return;
			}
			Job->Process.Reset(Duplicate);
			Job->ProcessId = GApplication.ProcessId;
		}
		else if (Action == EAction::Comparison)
		{
			int Indices[2] = {};
			if (SendDlgItemMessageW(GApplication.Window, SnapshotList, LB_GETSELCOUNT, 0, 0) != 2 ||
				SendDlgItemMessageW(GApplication.Window, SnapshotList, LB_GETSELITEMS, 2, reinterpret_cast<LPARAM>(Indices)) != 2)
			{
				ShowError(L"Выберите ровно два снимка с Ctrl или Shift."); return;
			}
			Job->First = GApplication.Snapshots[Indices[0]];
			Job->Second = GApplication.Snapshots[Indices[1]];
		}
		SaveSettings(Job->Settings);
		GApplication.bBusy = true;
		GApplication.Job = Job.get();
		const uintptr_t Thread = _beginthreadex(nullptr, 0, Worker, Job.get(), 0, nullptr);
		if (!Thread)
		{
			GApplication.bBusy = false;
			GApplication.Job = nullptr;
			ShowError(L"Не удалось создать рабочий поток.");
			Refresh();
			return;
		}
		CloseHandle(reinterpret_cast<HANDLE>(Thread));
		Job.release();
		SetText(ActivityText, bFlags ? L"Изменение флага через GFlags (Windows может запросить права администратора)..." :
			L"UMDH работает. Первая загрузка символов может занять время; интерфейс доступен, операцию можно отменить.");
		Refresh();
	}

	void SelectProcess(HANDLE Process, DWORD ProcessId, bool bKnownUst, bool bEnabled)
	{
		GApplication.Process.Reset(Process);
		GApplication.ProcessId = ProcessId;
		GApplication.Session = SessionName(Process, ProcessId);
		GApplication.bUstKnownAtStart = bKnownUst;
		GApplication.bUstAtStart = bEnabled;
		GApplication.LastReport.clear();
		SetText(PidEdit, std::to_wstring(ProcessId));
		LoadSnapshots();
		SetText(ActivityText, bKnownUst ? L"Игра запущена. Прогрейте сценарий, затем снимайте память до и после повторений." :
			L"Процесс подключён. Сбор стеков должен быть включён ДО его запуска; OANOCACHE=1 задаётся при запуске из этой утилиты.");
		Refresh();
	}

	void Launch()
	{
		const FSettings Value = Settings();
		if (Text(GameEdit).empty()) { ShowError(L"Выберите EXE игры."); return; }
		if (!_wcsicmp(Filename(Directory(Value.GamePath)).c_str(), L"binasan"))
		{
			ShowError(L"Для поиска утечек выберите обычную Debug-сборку в bin. ASan удерживает память и искажает измерение."); return;
		}
		bool bEnabled = false;
		std::wstring Error;
		if (!ReadUst(Filename(Value.GamePath), bEnabled, Error)) { ShowError(Error); return; }
		if (!bEnabled) { ShowError(L"Сначала нажмите «Включить сбор стеков», затем запускайте игру."); return; }
		HANDLE Process = nullptr;
		DWORD ProcessId = 0;
		if (!StartGame(Value, Process, ProcessId, Error)) { ShowError(Error); return; }
		SaveSettings(Value);
		SelectProcess(Process, ProcessId, true, bEnabled);
	}

	void AttachGame()
	{
		const FSettings Value = Settings();
		const std::wstring Pid = Text(PidEdit);
		wchar_t* End = nullptr;
		const DWORD Requested = Pid.empty() ? 0 : wcstoul(Pid.c_str(), &End, 10);
		if (!Pid.empty() && (!Requested || !End || *End)) { ShowError(L"Укажите числовой PID или очистите поле для автоматического поиска."); return; }
		std::wstring Error;
		if (!EnsureDirectory(Value.LogsPath, Error)) { ShowError(Error); return; }
		HANDLE Process = nullptr;
		DWORD ProcessId = 0;
		if (!FindGame(Value.GamePath, Requested, Process, ProcessId, Error)) { ShowError(Error); return; }
		SaveSettings(Value);
		SelectProcess(Process, ProcessId, false, false);
	}

	HWND Control(const wchar_t* Class, const wchar_t* Caption, DWORD Style, int Id, int X, int Y, int Width, int Height)
	{
		HWND Child = CreateWindowExW((Style & WS_BORDER) ? WS_EX_CLIENTEDGE : 0, Class, Caption,
			WS_CHILD | WS_VISIBLE | Style, X, Y, Width, Height, GApplication.Window,
			reinterpret_cast<HMENU>(static_cast<INT_PTR>(Id)), GetModuleHandleW(nullptr), nullptr);
		SendMessageW(Child, WM_SETFONT, reinterpret_cast<WPARAM>(GApplication.Font), TRUE);
		return Child;
	}

	void CreateControls()
	{
		GApplication.Font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
		const wchar_t* Labels[] = { L"EXE игры", L"Аргументы запуска", L"Папка логов", L"Debugging Tools (x86)", L"Путь символов / PDB" };
		const int Edits[] = { GameEdit, ArgumentsEdit, LogsEdit, ToolsEdit, SymbolsEdit };
		const int Browses[] = { GameBrowse, 0, LogsBrowse, ToolsBrowse, SymbolsBrowse };
		for (int Index = 0; Index < 5; ++Index)
		{
			const int Y = 16 + Index * 36;
			Control(L"STATIC", Labels[Index], 0, 0, 14, Y + 5, 164, 24);
			Control(L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, Edits[Index], 180, Y, 614, 26);
			if (Browses[Index]) Control(L"BUTTON", L"...", WS_TABSTOP, Browses[Index], 804, Y, 40, 26);
		}
		Control(L"BUTTON", L"Символы Windows (скачивать и кэшировать)", WS_TABSTOP | BS_AUTOCHECKBOX, WindowsSymbols, 180, 195, 350, 23);
		Control(L"STATIC", L"PDB из папки EXE — автоматически.", 0, 0, 538, 200, 306, 23);
		Control(L"BUTTON", L"Включить сбор стеков", WS_TABSTOP, EnableUst, 14, 227, 194, 32);
		Control(L"BUTTON", L"Отключить сбор стеков", WS_TABSTOP, DisableUst, 220, 227, 194, 32);
		Control(L"STATIC", L"", 0, StateText, 14, 270, 830, 25);
		Control(L"BUTTON", L"Запустить игру", WS_TABSTOP, Start, 14, 306, 170, 32);
		Control(L"STATIC", L"PID (пусто = поиск)", 0, 0, 206, 314, 140, 24);
		Control(L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL, PidEdit, 350, 308, 100, 28);
		Control(L"BUTTON", L"Подключить игру", WS_TABSTOP, Attach, 464, 306, 180, 32);
		Control(L"BUTTON", L"Как пользоваться", WS_TABSTOP, Help, 660, 306, 184, 32);
		Control(L"STATIC", L"", 0, ProcessText, 14, 350, 830, 32);
		Control(L"STATIC", L"Снимки этого запуска. Выберите пару с Ctrl/Shift; сравнение идёт от раннего к позднему.", 0, 0, 14, 389, 830, 25);
		Control(L"LISTBOX", L"", WS_BORDER | WS_TABSTOP | WS_VSCROLL | LBS_EXTENDEDSEL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
			SnapshotList, 14, 420, 630, 172);
		Control(L"BUTTON", L"Снять память", WS_TABSTOP, Capture, 660, 420, 184, 28);
		Control(L"BUTTON", L"Сравнить пару", WS_TABSTOP, Compare, 660, 452, 184, 28);
		Control(L"BUTTON", L"Открыть снимок", WS_TABSTOP, OpenFile, 660, 484, 184, 28);
		Control(L"BUTTON", L"Открыть отчёт", WS_TABSTOP, OpenReport, 660, 516, 184, 28);
		Control(L"BUTTON", L"Открыть папку логов", WS_TABSTOP, OpenFolder, 660, 548, 184, 28);
		Control(L"BUTTON", L"Отменить операцию", WS_TABSTOP, Cancel, 660, 580, 184, 28);
		Control(L"EDIT", L"", WS_BORDER | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
			ActivityText, 14, 626, 830, 76);
		wchar_t Module[32768] = {};
		GetModuleFileNameW(nullptr, Module, 32768);
		const FSettings Value = LoadSettings(Directory(Module));
		SetText(GameEdit, Value.GamePath);
		SetText(ArgumentsEdit, Value.GameArguments);
		SetText(LogsEdit, Value.LogsPath);
		SetText(ToolsEdit, Value.ToolsPath);
		SetText(SymbolsEdit, Value.SymbolsPath);
		SendDlgItemMessageW(GApplication.Window, WindowsSymbols, BM_SETCHECK, Value.bWindowsSymbols ? BST_CHECKED : BST_UNCHECKED, 0);
		SetText(ActivityText, Value.ToolsPath.empty() ? L"Установите Debugging Tools for Windows из Windows SDK и укажите папку Debuggers\\x86." :
			L"Готово. Включите сбор стеков, запустите игру и снимайте память после прогрева. Флаг сохраняется до явного отключения.");
		Refresh();
		SetTimer(GApplication.Window, 1, 1000, nullptr);
	}

	LRESULT CALLBACK WindowProcedure(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
	{
		switch (Message)
		{
		case WM_CREATE:
			GApplication.Window = Window;
			CreateControls();
			return 0;
		case WM_TIMER:
			Refresh();
			return 0;
		case GFinishedMessage:
		{
			std::unique_ptr<FJob> Job(reinterpret_cast<FJob*>(LParam));
			GApplication.bBusy = false;
			GApplication.Job = nullptr;
			if (!Job->Destination.empty())
			{
				GApplication.LastReport = Job->Action == EAction::Snapshot ? Job->Destination + L".tool.txt" : Job->Destination;
				if (Job->Action == EAction::Snapshot && !Job->Result.bSuccess &&
					GetFileAttributesW((Job->Destination + L".pending").c_str()) != INVALID_FILE_ATTRIBUTES)
					GApplication.LastReport = Job->Destination + L".pending";
			}
			if (Job->Result.bSuccess)
			{
				if (Job->Action == EAction::Snapshot)
				{
					LoadSnapshots(); SetText(ActivityText, L"Снимок сохранён: " + Job->Destination);
				}
				else if (Job->Action == EAction::Comparison)
				{
					SetText(ActivityText, L"Сравнение сохранено: " + Job->Destination); OpenPath(Job->Destination);
				}
				else SetText(ActivityText, L"Флаг изменён. Он действует на новые запуски; уже запущенную игру нужно закрыть и запустить снова.");
			}
			else if (!GApplication.bCloseAfterCancel) ShowError(Job->Result.Error);
			Refresh();
			if (GApplication.bCloseAfterCancel) SendMessageW(Window, WM_CLOSE, 0, 0);
			return 0;
		}
		case WM_COMMAND:
		{
			const int Id = LOWORD(WParam);
			if (Id == LogsEdit && HIWORD(WParam) == EN_KILLFOCUS && !GApplication.bBusy)
			{
				LoadSnapshots(); Refresh(); return 0;
			}
			if (Id == SnapshotList && HIWORD(WParam) == LBN_SELCHANGE) { Refresh(); return 0; }
			if (HIWORD(WParam) != BN_CLICKED) return 0;
			if (GApplication.bBusy && Id != Cancel && Id != Help && Id != OpenFolder) return 0;
			switch (Id)
			{
			case GameBrowse: Browse(GameEdit, true); break;
			case LogsBrowse: Browse(LogsEdit, false); LoadSnapshots(); break;
			case ToolsBrowse: Browse(ToolsEdit, false); break;
			case SymbolsBrowse: Browse(SymbolsEdit, false); break;
			case EnableUst: BeginJob(EAction::Enable); break;
			case DisableUst: BeginJob(EAction::Disable); break;
			case Start: Launch(); break;
			case Attach: AttachGame(); break;
			case Capture: BeginJob(EAction::Snapshot); break;
			case Compare: BeginJob(EAction::Comparison); break;
			case OpenFile:
			{
				int Index = 0;
				if (SendDlgItemMessageW(Window, SnapshotList, LB_GETSELITEMS, 1, reinterpret_cast<LPARAM>(&Index)) == 1)
					OpenPath(GApplication.Snapshots[Index]);
				else if (!GApplication.LastReport.empty()) OpenPath(GApplication.LastReport);
				break;
			}
			case OpenFolder:
			{
				std::wstring Error;
				if (EnsureDirectory(Settings().LogsPath, Error)) OpenPath(Settings().LogsPath);
				else ShowError(Error);
				break;
			}
			case OpenReport:
				if (!GApplication.LastReport.empty()) OpenPath(GApplication.LastReport);
				break;
			case Cancel:
				if (GApplication.Job && GApplication.Job->CancelEvent.Get()) SetEvent(GApplication.Job->CancelEvent.Get());
				break;
			case Help:
				MessageBoxW(Window,
					L"1. Выберите обычный Debug-клиент (bin\\freelancer.exe) и папку логов.\r\n"
					L"2. Включите сбор стеков. UAC запрашивается только для GFlags.\r\n"
					L"3. Запустите игру кнопкой; прогрейте нужный сценарий.\r\n"
					L"4. Снимите память, повторите действия, вернитесь в исходное состояние и снимите снова.\r\n"
					L"5. Повторите цикл ещё раз. Сравните соседние пары: ищите повторяющийся рост одних стеков.\r\n\r\n"
					L"Снимки — живые выделения Windows heap, а не полный дамп и не автоматический приговор об утечке. "
					L"Пулы, VirtualAlloc и видеопамять требуют отдельного анализа.\r\n"
					L"При подключении к уже запущенной игре режим на момент запуска неизвестен. "
					L"Включение флага после запуска не добавляет стеки прошлым выделениям.\r\n"
					L"Снимки каждого процесса лежат в отдельной папке; ошибки UMDH — в .tool.txt.\r\n"
					L"Если скачивание символов Windows затягивается, отмените операцию и снимите соответствующую галочку. Локальные PDB продолжат использоваться.\r\n"
					L"Поле символов принимает путь WinDbg: папки через точку с запятой, cache* и srv*. По умолчанию используется настроенный _NT_SYMBOL_PATH и кэш J:\\pdb_cache.\r\n"
					L"Закрытие утилиты не закрывает игру и не отключает флаг. После работы нажмите «Отключить сбор стеков».\r\n"
					L"https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/using-umdh-to-find-a-user-mode-memory-leak",
					L"Поиск утечки памяти", MB_OK | MB_ICONINFORMATION);
				break;
			}
			Refresh();
			return 0;
		}
		case WM_CLOSE:
			if (GApplication.bBusy)
			{
				if (GApplication.Job && GApplication.Job->CancelEvent.Get())
				{
					GApplication.bCloseAfterCancel = true; SetEvent(GApplication.Job->CancelEvent.Get()); SetText(ActivityText, L"Отмена UMDH перед закрытием...");
				}
				else SetText(ActivityText, L"Дождитесь завершения GFlags, затем закройте окно.");
				return 0;
			}
			SaveSettings(Settings());
			DestroyWindow(Window);
			return 0;
		case WM_DESTROY:
			KillTimer(Window, 1);
			PostQuitMessage(0);
			return 0;
		}
		return DefWindowProcW(Window, Message, WParam, LParam);
	}

	HWND CreateApplicationWindow(HINSTANCE Instance)
	{
		WNDCLASSW Class = {};
		Class.lpfnWndProc = WindowProcedure;
		Class.hInstance = Instance;
		Class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
		Class.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
		Class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
		Class.lpszClassName = L"FreelancerMemoryDiagnostics";
		if (!RegisterClassW(&Class)) return nullptr;
		RECT Bounds = { 0, 0, 858, 718 };
		const DWORD Style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
		AdjustWindowRectEx(&Bounds, Style, FALSE, 0);
		return CreateWindowExW(WS_EX_CONTROLPARENT, Class.lpszClassName, L"Freelancer — диагностика утечек памяти",
			Style, CW_USEDEFAULT, CW_USEDEFAULT, Bounds.right - Bounds.left, Bounds.bottom - Bounds.top,
			nullptr, nullptr, Instance, nullptr);
	}
}

#ifdef MD_UI_TEST
// Construct the real controls without showing a window, launching a game or saving settings.
int VerifyApplicationWindow()
{
	HWND Window = CreateApplicationWindow(GetModuleHandleW(nullptr));
	if (!Window) { printf("FAIL: create application window\n"); return 1; }
	int Failures = 0;
	RECT Client = {};
	GetClientRect(Window, &Client);
	for (int Id = GameEdit; Id <= ActivityText; ++Id)
	{
		HWND Child = GetDlgItem(Window, Id);
		if (!Child) { ++Failures; printf("FAIL: missing control %d\n", Id); continue; }
		RECT Bounds = {};
		GetWindowRect(Child, &Bounds);
		MapWindowPoints(nullptr, Window, reinterpret_cast<POINT*>(&Bounds), 2);
		if (Bounds.left < 0 || Bounds.top < 0 || Bounds.right > Client.right || Bounds.bottom > Client.bottom)
		{
			++Failures; printf("FAIL: control %d outside client area\n", Id);
		}
		if (Id == EnableUst || Id == DisableUst || Id == Help)
		{
			HDC Context = GetDC(Child);
			HGDIOBJ Previous = SelectObject(Context, GApplication.Font);
			SIZE Extent = {};
			const std::wstring Caption = Text(Id);
			GetTextExtentPoint32W(Context, Caption.c_str(), static_cast<int>(Caption.size()), &Extent);
			if (Extent.cx + 16 > Bounds.right - Bounds.left)
			{
				++Failures; printf("FAIL: clipped button caption %d\n", Id);
			}
			SelectObject(Context, Previous);
			ReleaseDC(Child, Context);
		}
	}
	if (Text(ToolsEdit).empty() || Text(GameEdit).empty() || Text(LogsEdit).empty())
	{
		++Failures; printf("FAIL: default paths\n");
	}
	if (IsWindowEnabled(GetDlgItem(Window, Capture)) || IsWindowEnabled(GetDlgItem(Window, Compare)) ||
		IsWindowEnabled(GetDlgItem(Window, Cancel)))
	{
		++Failures; printf("FAIL: idle button state\n");
	}
	DestroyWindow(Window);
	GApplication.Window = nullptr;
	printf("MemoryDiagnostics UI: controls, bounds, captions and idle state: %d failures\n", Failures);
	return Failures;
}
#endif

int WINAPI wWinMain(HINSTANCE Instance, HINSTANCE, wchar_t*, int Show)
{
	const HRESULT Com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	HWND Window = CreateApplicationWindow(Instance);
	if (!Window) { if (SUCCEEDED(Com)) CoUninitialize(); return 1; }
	ShowWindow(Window, Show);
	MSG Message = {};
	while (GetMessageW(&Message, nullptr, 0, 0) > 0)
	{
		if (!IsDialogMessageW(Window, &Message))
		{
			TranslateMessage(&Message); DispatchMessageW(&Message);
		}
	}
	if (SUCCEEDED(Com)) CoUninitialize();
	return static_cast<int>(Message.wParam);
}
