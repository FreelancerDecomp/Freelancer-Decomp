// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "diagnostics.h"
#include <shellapi.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <algorithm>
#include <cwchar>
#include <map>

namespace MemoryDiagnostics
{
	std::wstring ErrorText(DWORD Code)
	{
		wchar_t* Buffer = nullptr;
		FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
			FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, Code, 0, reinterpret_cast<wchar_t*>(&Buffer), 0, nullptr);
		std::wstring Text = Buffer ? Buffer : L"Неизвестная ошибка Windows";
		if (Buffer) LocalFree(Buffer);
		return Text + L" (" + std::to_wstring(Code) + L")";
	}

	// Encode one argv entry for the Windows CRT parser, including trailing backslashes.
	std::wstring QuoteArgument(const std::wstring& Value)
	{
		std::wstring Result = L"\"";
		size_t Slashes = 0;
		for (size_t Index = 0; Index < Value.size(); ++Index)
		{
			if (Value[Index] == L'\\') ++Slashes;
			else
			{
				Result.append(Slashes * (Value[Index] == L'\"' ? 2 : 1), L'\\');
				Slashes = 0;
				if (Value[Index] == L'\"') Result += L'\\';
				Result += Value[Index];
			}
		}
		Result.append(Slashes * 2, L'\\');
		return Result + L'\"';
	}

	std::wstring Directory(const std::wstring& Path)
	{
		const size_t Separator = Path.find_last_of(L"\\/");
		if (Separator == std::wstring::npos) return L".";
		if (Separator == 2 && Path[1] == L':') return Path.substr(0, 3);
		return Path.substr(0, Separator);
	}

	std::wstring Filename(const std::wstring& Path)
	{
		const size_t Separator = Path.find_last_of(L"\\/");
		return Separator == std::wstring::npos ? Path : Path.substr(Separator + 1);
	}

	std::wstring FullPath(const std::wstring& Path)
	{
		const DWORD Size = GetFullPathNameW(Path.c_str(), 0, nullptr, nullptr);
		if (!Size) return Path;
		std::vector<wchar_t> Buffer(Size);
		if (!GetFullPathNameW(Path.c_str(), Size, Buffer.data(), nullptr)) return Path;
		return Buffer.data();
	}

	static std::wstring ReadString(HKEY Root, const wchar_t* Key, const wchar_t* Name, REGSAM View = 0)
	{
		HKEY OpenKey = nullptr;
		if (RegOpenKeyExW(Root, Key, 0, KEY_QUERY_VALUE | View, &OpenKey) != ERROR_SUCCESS) return L"";
		DWORD Type = 0;
		DWORD Size = 0;
		std::wstring Result;
		if (RegQueryValueExW(OpenKey, Name, nullptr, &Type, nullptr, &Size) == ERROR_SUCCESS &&
			(Type == REG_SZ || Type == REG_EXPAND_SZ) && Size > 0)
		{
			std::vector<wchar_t> Buffer(Size / sizeof(wchar_t) + 1, 0);
			if (RegQueryValueExW(OpenKey, Name, nullptr, &Type,
				reinterpret_cast<BYTE*>(Buffer.data()), &Size) == ERROR_SUCCESS) Result = Buffer.data();
		}
		RegCloseKey(OpenKey);
		return Result;
	}

	bool IsX86Executable(const std::wstring& Path)
	{
		FHandle File(CreateFileW(Path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
			nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
		if (File.Get() == INVALID_HANDLE_VALUE) return false;
		IMAGE_DOS_HEADER Dos = {};
		DWORD Read = 0;
		if (!ReadFile(File.Get(), &Dos, sizeof(Dos), &Read, nullptr) || Read != sizeof(Dos) ||
			Dos.e_magic != IMAGE_DOS_SIGNATURE || Dos.e_lfanew < 0) return false;
		LARGE_INTEGER Position = {};
		Position.QuadPart = Dos.e_lfanew;
		if (!SetFilePointerEx(File.Get(), Position, nullptr, FILE_BEGIN)) return false;
		DWORD Signature = 0;
		IMAGE_FILE_HEADER Header = {};
		return ReadFile(File.Get(), &Signature, sizeof(Signature), &Read, nullptr) &&
			Read == sizeof(Signature) && Signature == IMAGE_NT_SIGNATURE &&
			ReadFile(File.Get(), &Header, sizeof(Header), &Read, nullptr) &&
			Read == sizeof(Header) && Header.Machine == IMAGE_FILE_MACHINE_I386;
	}

	std::wstring FindTools()
	{
		std::vector<std::wstring> Candidates;
		const std::wstring Root = ReadString(HKEY_LOCAL_MACHINE,
			L"SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots", L"KitsRoot10", KEY_WOW64_32KEY);
		if (!Root.empty()) Candidates.push_back(Root + L"Debuggers\\x86");
		wchar_t ProgramFiles[MAX_PATH] = {};
		if (GetEnvironmentVariableW(L"ProgramFiles(x86)", ProgramFiles, MAX_PATH))
			Candidates.push_back(std::wstring(ProgramFiles) + L"\\Windows Kits\\10\\Debuggers\\x86");
		wchar_t Found[32768] = {};
		if (SearchPathW(nullptr, L"umdh.exe", nullptr, 32768, Found, nullptr)) Candidates.push_back(Directory(Found));
		for (size_t Index = 0; Index < Candidates.size(); ++Index)
		{
			if (IsX86Executable(Candidates[Index] + L"\\umdh.exe") &&
				IsX86Executable(Candidates[Index] + L"\\gflags.exe")) return Candidates[Index];
		}
		return L"";
	}

	bool EnsureDirectory(const std::wstring& Path, std::wstring& Error)
	{
		if (Path.empty()) { Error = L"Укажите папку для логов."; return false; }
		const std::wstring Absolute = FullPath(Path);
		// Project scratch output must stay off the nearly-full C: drive.
		if (Absolute.size() > 1 && (Absolute[0] == L'C' || Absolute[0] == L'c') && Absolute[1] == L':')
		{
			Error = L"Для логов и кэша символов выберите диск, отличный от C:."; return false;
		}
		const int Status = SHCreateDirectoryExW(nullptr, Absolute.c_str(), nullptr);
		if (Status != ERROR_SUCCESS && Status != ERROR_ALREADY_EXISTS && Status != ERROR_FILE_EXISTS)
		{
			Error = L"Не удалось создать папку: " + Absolute + L"\r\n" + ErrorText(Status); return false;
		}
		const DWORD Attributes = GetFileAttributesW(Absolute.c_str());
		if (Attributes == INVALID_FILE_ATTRIBUTES || !(Attributes & FILE_ATTRIBUTE_DIRECTORY))
		{
			Error = L"Путь не является папкой: " + Absolute; return false;
		}
		return true;
	}

	bool ReadUst(const std::wstring& Image, bool& bEnabled, std::wstring& Error)
	{
		bEnabled = false;
		HKEY Key = nullptr;
		const std::wstring Path = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\" + Image;
		LSTATUS Status = RegOpenKeyExW(HKEY_LOCAL_MACHINE, Path.c_str(), 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &Key);
		if (Status == ERROR_FILE_NOT_FOUND) return true;
		if (Status != ERROR_SUCCESS) { Error = ErrorText(Status); return false; }
		wchar_t Buffer[512] = {};
		DWORD Size = sizeof(Buffer) - sizeof(wchar_t);
		DWORD Type = 0;
		Status = RegQueryValueExW(Key, L"GlobalFlag", nullptr, &Type, reinterpret_cast<BYTE*>(Buffer), &Size);
		RegCloseKey(Key);
		if (Status == ERROR_FILE_NOT_FOUND) return true;
		if (Status != ERROR_SUCCESS) { Error = ErrorText(Status); return false; }
		DWORD Flags = 0;
		if (Type == REG_DWORD && Size == sizeof(DWORD)) memcpy(&Flags, Buffer, sizeof(Flags));
		else if (Type == REG_SZ)
		{
			const std::wstring Text(reinterpret_cast<const wchar_t*>(Buffer), Size / sizeof(wchar_t));
			wchar_t* End = nullptr;
			Flags = wcstoul(Text.c_str(), &End, 16);
			if (End == Text.c_str()) { Error = L"Не удалось прочитать GlobalFlag."; return false; }
		}
		else { Error = L"Неожиданный тип GlobalFlag."; return false; }
		bEnabled = (Flags & 0x1000) != 0;
		return true;
	}

	struct FEnvironmentLess
	{
		bool operator()(const std::wstring& Left, const std::wstring& Right) const
		{
			return _wcsicmp(Left.c_str(), Right.c_str()) < 0;
		}
	};

	static std::wstring ConfiguredSymbols()
	{
		wchar_t Buffer[32768] = {};
		if (GetEnvironmentVariableW(L"_NT_SYMBOL_PATH", Buffer, 32768)) return Buffer;
		const std::wstring User = ReadString(HKEY_CURRENT_USER, L"Environment", L"_NT_SYMBOL_PATH");
		if (!User.empty()) return User;
		return L"cache*J:\\pdb_cache;SRV*J:\\pdb_cache*https://msdl.microsoft.com/download/symbols";
	}

	std::wstring BuildSymbolPath(const FSettings& Settings)
	{
		const std::wstring Configured = Settings.SymbolsPath.empty() ? ConfiguredSymbols() : Settings.SymbolsPath;
		std::wstring Result = Directory(Settings.GamePath);
		bool bHasServer = false;
		size_t Start = 0;
		while (Start < Configured.size())
		{
			const size_t End = Configured.find(L';', Start);
			std::wstring Entry = Configured.substr(Start, End == std::wstring::npos ? End : End - Start);
			const bool bServer = !_wcsnicmp(Entry.c_str(), L"srv*", 4);
			const bool bSymSrv = !_wcsnicmp(Entry.c_str(), L"symsrv*", 7);
			bHasServer = bHasServer || bServer || bSymSrv;
			if (!Settings.bWindowsSymbols)
			{
				// Keep the local symbol store while removing upstream download locations.
				if (bServer || bSymSrv)
				{
					const size_t CacheStart = bServer ? 4 : Entry.find(L'*', 7) + 1;
					const size_t CacheEnd = Entry.find(L'*', CacheStart);
					const std::wstring Cache = CacheStart > 0 ? Entry.substr(CacheStart,
						CacheEnd == std::wstring::npos ? CacheEnd : CacheEnd - CacheStart) : L"";
					Entry = Cache.empty() || !_wcsnicmp(Cache.c_str(), L"http", 4) ? L"" : L"srv*" + Cache;
				}
				else if (!_wcsnicmp(Entry.c_str(), L"http://", 7) || !_wcsnicmp(Entry.c_str(), L"https://", 8)) Entry.clear();
			}
			if (!Entry.empty()) Result += L";" + Entry;
			if (End == std::wstring::npos) break;
			Start = End + 1;
		}
		if (Settings.bWindowsSymbols && !bHasServer)
			Result += L";srv*J:\\pdb_cache*https://msdl.microsoft.com/download/symbols";
		return Result;
	}

	std::vector<wchar_t> BuildEnvironment(const FSettings& Settings)
	{
		std::map<std::wstring, std::wstring, FEnvironmentLess> Values;
		wchar_t* Environment = GetEnvironmentStringsW();
		if (Environment)
		{
			for (const wchar_t* Entry = Environment; *Entry; Entry += wcslen(Entry) + 1)
			{
				const std::wstring Text(Entry);
				// Preserve the special =D:=... current-directory entries as well.
				const size_t Equal = Text.find(L'=', Text[0] == L'=' ? 1 : 0);
				if (Equal != std::wstring::npos) Values[Text.substr(0, Equal)] = Text.substr(Equal + 1);
			}
			FreeEnvironmentStringsW(Environment);
		}
		Values[L"_NT_SYMBOL_PATH"] = BuildSymbolPath(Settings);
		Values[L"OANOCACHE"] = L"1";
		Values[L"TEMP"] = FullPath(Settings.LogsPath);
		Values[L"TMP"] = FullPath(Settings.LogsPath);
		std::vector<wchar_t> Block;
		for (std::map<std::wstring, std::wstring, FEnvironmentLess>::const_iterator It = Values.begin(); It != Values.end(); ++It)
		{
			const std::wstring Entry = It->first + L"=" + It->second;
			Block.insert(Block.end(), Entry.begin(), Entry.end());
			Block.push_back(0);
		}
		Block.push_back(0);
		return Block;
	}

	FToolResult RunTool(const std::wstring& Tool, const std::wstring& Arguments,
		const FSettings& Settings, const std::wstring& OutputPath, bool bElevated, HWND Owner, HANDLE CancelEvent)
	{
		FToolResult Result;
		HANDLE Process = nullptr;
		FHandle Output;
		FHandle Input;
		if (bElevated)
		{
			SHELLEXECUTEINFOW Execute = { sizeof(Execute) };
			Execute.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
			Execute.hwnd = Owner;
			Execute.lpVerb = L"runas";
			Execute.lpFile = Tool.c_str();
			Execute.lpParameters = Arguments.c_str();
			Execute.lpDirectory = Settings.ToolsPath.c_str();
			Execute.nShow = SW_HIDE;
			if (!ShellExecuteExW(&Execute)) { Result.Error = ErrorText(GetLastError()); return Result; }
			Process = Execute.hProcess;
		}
		else
		{
			SECURITY_ATTRIBUTES Security = { sizeof(Security), nullptr, TRUE };
			// Console tools have no visible window; collect both stdout and stderr in one report.
			Output.Reset(CreateFileW(OutputPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &Security,
				CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
			if (Output.Get() == INVALID_HANDLE_VALUE) { Result.Error = ErrorText(GetLastError()); return Result; }
			Input.Reset(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
				&Security, OPEN_EXISTING, 0, nullptr));
			if (Input.Get() == INVALID_HANDLE_VALUE) { Result.Error = ErrorText(GetLastError()); return Result; }
			STARTUPINFOW Startup = { sizeof(Startup) };
			Startup.dwFlags = STARTF_USESTDHANDLES;
			Startup.hStdInput = Input.Get();
			Startup.hStdOutput = Output.Get();
			Startup.hStdError = Output.Get();
			PROCESS_INFORMATION Info = {};
			std::wstring Command = QuoteArgument(Tool) + L" " + Arguments;
			std::vector<wchar_t> Environment = BuildEnvironment(Settings);
			if (!CreateProcessW(Tool.c_str(), &Command[0], nullptr, nullptr, TRUE,
				CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, Environment.data(),
				Settings.ToolsPath.c_str(), &Startup, &Info))
			{
				Result.Error = ErrorText(GetLastError()); return Result;
			}
			CloseHandle(Info.hThread);
			Process = Info.hProcess;
		}
		FHandle OwnedProcess(Process);
		if (!Process) { Result.Error = L"Инструмент не вернул дескриптор процесса."; return Result; }
		HANDLE WaitHandles[] = { Process, CancelEvent };
		const DWORD Wait = WaitForMultipleObjects(CancelEvent ? 2 : 1, WaitHandles, FALSE, INFINITE);
		if (Wait == WAIT_OBJECT_0 + 1)
		{
			// This handle always belongs to UMDH, never to the observed game.
			if (!TerminateProcess(Process, ERROR_CANCELLED))
			{
				Result.Error = ErrorText(GetLastError()); return Result;
			}
			WaitForSingleObject(Process, INFINITE);
			Result.Error = L"Операция отменена. Незавершённые файлы сохранены для диагностики.";
			Result.ExitCode = ERROR_CANCELLED;
			return Result;
		}
		if (Wait != WAIT_OBJECT_0 || !GetExitCodeProcess(Process, &Result.ExitCode))
		{
			Result.Error = ErrorText(GetLastError()); return Result;
		}
		Result.bSuccess = Result.ExitCode == 0;
		if (!Result.bSuccess) Result.Error = L"Инструмент завершился с кодом " + std::to_wstring(Result.ExitCode) +
			(OutputPath.empty() ? L"." : L". Подробности: " + OutputPath);
		return Result;
	}

	bool StartGame(const FSettings& Settings, HANDLE& Process, DWORD& ProcessId, std::wstring& Error)
	{
		if (!IsX86Executable(Settings.GamePath))
		{
			Error = L"Выберите существующий Win32 EXE клиента."; return false;
		}
		if (!EnsureDirectory(Settings.LogsPath, Error)) return false;
		std::vector<wchar_t> Environment = BuildEnvironment(Settings);
		std::wstring Command = QuoteArgument(Settings.GamePath) + L" " + Settings.GameArguments;
		STARTUPINFOW Startup = { sizeof(Startup) };
		PROCESS_INFORMATION Info = {};
		const std::wstring WorkingDirectory = Directory(Settings.GamePath);
		if (!CreateProcessW(Settings.GamePath.c_str(), &Command[0], nullptr, nullptr, FALSE,
			CREATE_UNICODE_ENVIRONMENT, Environment.data(), WorkingDirectory.c_str(), &Startup, &Info))
		{
			Error = ErrorText(GetLastError()); return false;
		}
		CloseHandle(Info.hThread);
		Process = Info.hProcess;
		ProcessId = Info.dwProcessId;
		return true;
	}

	bool FindGame(const std::wstring& Path, DWORD RequestedId, HANDLE& Process, DWORD& ProcessId, std::wstring& Error)
	{
		FHandle Snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
		if (Snapshot.Get() == INVALID_HANDLE_VALUE) { Error = ErrorText(GetLastError()); return false; }
		PROCESSENTRY32W Entry = { sizeof(Entry) };
		const std::wstring Image = Filename(Path);
		const std::wstring Absolute = FullPath(Path);
		FHandle Match;
		DWORD MatchedId = 0;
		if (Process32FirstW(Snapshot.Get(), &Entry))
		{
			do
			{
				if ((RequestedId && Entry.th32ProcessID != RequestedId) || _wcsicmp(Entry.szExeFile, Image.c_str())) continue;
				FHandle Candidate(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ | SYNCHRONIZE,
					FALSE, Entry.th32ProcessID));
				if (!Candidate.Get())
				{
					if (RequestedId) { Error = ErrorText(GetLastError()); return false; }
					continue;
				}
				wchar_t Buffer[32768] = {};
				DWORD Size = 32768;
				if (!QueryFullProcessImageNameW(Candidate.Get(), 0, Buffer, &Size) || _wcsicmp(Buffer, Absolute.c_str())) continue;
				if (MatchedId) { Error = L"Запущено несколько экземпляров. Укажите PID нужной игры."; return false; }
				HANDLE Duplicate = nullptr;
				if (!DuplicateHandle(GetCurrentProcess(), Candidate.Get(), GetCurrentProcess(), &Duplicate, 0, FALSE, DUPLICATE_SAME_ACCESS))
				{
					Error = ErrorText(GetLastError()); return false;
				}
				Match.Reset(Duplicate);
				MatchedId = Entry.th32ProcessID;
			} while (Process32NextW(Snapshot.Get(), &Entry));
		}
		if (!MatchedId) { Error = L"Запущенный процесс с выбранным путём не найден."; return false; }
		if (!DuplicateHandle(GetCurrentProcess(), Match.Get(), GetCurrentProcess(), &Process, 0, FALSE, DUPLICATE_SAME_ACCESS))
		{
			Error = ErrorText(GetLastError()); return false;
		}
		ProcessId = MatchedId;
		return true;
	}

	std::wstring SessionName(HANDLE Process, DWORD ProcessId)
	{
		FILETIME Created = {}, Exited = {}, Kernel = {}, User = {};
		if (!GetProcessTimes(Process, &Created, &Exited, &Kernel, &User)) return L"";
		wchar_t Name[96] = {};
		swprintf_s(Name, L"session-%08lX%08lX-pid%lu", Created.dwHighDateTime, Created.dwLowDateTime, ProcessId);
		return Name;
	}

	static const wchar_t* GSettingsKey = L"Software\\FreelancerPort\\MemoryDiagnostics";

	FSettings LoadSettings(const std::wstring& ApplicationDirectory)
	{
		FSettings Settings;
		Settings.GamePath = ReadString(HKEY_CURRENT_USER, GSettingsKey, L"GamePath");
		Settings.GameArguments = ReadString(HKEY_CURRENT_USER, GSettingsKey, L"GameArguments");
		Settings.LogsPath = ReadString(HKEY_CURRENT_USER, GSettingsKey, L"LogsPath");
		Settings.ToolsPath = ReadString(HKEY_CURRENT_USER, GSettingsKey, L"ToolsPath");
		Settings.SymbolsPath = ReadString(HKEY_CURRENT_USER, GSettingsKey, L"SymbolsPath");
		Settings.bWindowsSymbols = ReadString(HKEY_CURRENT_USER, GSettingsKey, L"WindowsSymbols") != L"0";
		if (Settings.GamePath.empty()) Settings.GamePath = FullPath(ApplicationDirectory + L"\\..\\bin\\freelancer.exe");
		if (Settings.LogsPath.empty()) Settings.LogsPath = L"D:\\Temp\\FreelancerLeaks";
		if (Settings.ToolsPath.empty()) Settings.ToolsPath = FindTools();
		if (Settings.SymbolsPath.empty()) Settings.SymbolsPath = ConfiguredSymbols();
		return Settings;
	}

	void SaveSettings(const FSettings& Settings)
	{
		HKEY Key = nullptr;
		if (RegCreateKeyExW(HKEY_CURRENT_USER, GSettingsKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &Key, nullptr) != ERROR_SUCCESS) return;
		const wchar_t* Names[] = { L"GamePath", L"GameArguments", L"LogsPath", L"ToolsPath", L"SymbolsPath" };
		const std::wstring* Values[] = { &Settings.GamePath, &Settings.GameArguments, &Settings.LogsPath, &Settings.ToolsPath, &Settings.SymbolsPath };
		for (size_t Index = 0; Index < 5; ++Index)
			RegSetValueExW(Key, Names[Index], 0, REG_SZ, reinterpret_cast<const BYTE*>(Values[Index]->c_str()),
				static_cast<DWORD>((Values[Index]->size() + 1) * sizeof(wchar_t)));
		const wchar_t* WindowsSymbols = Settings.bWindowsSymbols ? L"1" : L"0";
		RegSetValueExW(Key, L"WindowsSymbols", 0, REG_SZ, reinterpret_cast<const BYTE*>(WindowsSymbols), 2 * sizeof(wchar_t));
		RegCloseKey(Key);
	}
}
