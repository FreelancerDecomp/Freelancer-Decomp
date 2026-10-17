// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <string>
#include <vector>

namespace MemoryDiagnostics
{
	struct FSettings
	{
		std::wstring GamePath;
		std::wstring GameArguments;
		std::wstring LogsPath;
		std::wstring ToolsPath;
		std::wstring SymbolsPath;
		bool bWindowsSymbols = true;
	};

	class FHandle
	{
	public:
		explicit FHandle(HANDLE Value = nullptr) : Handle(Value) {}
		~FHandle() { Reset(); }
		FHandle(const FHandle&) = delete;
		FHandle& operator=(const FHandle&) = delete;
		HANDLE Get() const { return Handle; }
		void Reset(HANDLE Value = nullptr)
		{
			if (Handle && Handle != INVALID_HANDLE_VALUE) CloseHandle(Handle);
			Handle = Value;
		}
	private:
		HANDLE Handle;
	};

	struct FToolResult
	{
		bool bSuccess = false;
		DWORD ExitCode = 0;
		std::wstring Error;
	};

	std::wstring ErrorText(DWORD Code);
	std::wstring QuoteArgument(const std::wstring& Value);
	std::wstring Directory(const std::wstring& Path);
	std::wstring Filename(const std::wstring& Path);
	std::wstring FullPath(const std::wstring& Path);
	std::wstring FindTools();
	bool IsX86Executable(const std::wstring& Path);
	bool EnsureDirectory(const std::wstring& Path, std::wstring& Error);
	bool ReadUst(const std::wstring& Image, bool& bEnabled, std::wstring& Error);
	std::wstring BuildSymbolPath(const FSettings& Settings);
	std::vector<wchar_t> BuildEnvironment(const FSettings& Settings);
	FToolResult RunTool(const std::wstring& Tool, const std::wstring& Arguments,
		const FSettings& Settings, const std::wstring& OutputPath, bool bElevated, HWND Owner,
		HANDLE CancelEvent = nullptr);
	bool StartGame(const FSettings& Settings, HANDLE& Process, DWORD& ProcessId, std::wstring& Error);
	bool FindGame(const std::wstring& Path, DWORD RequestedId, HANDLE& Process, DWORD& ProcessId, std::wstring& Error);
	std::wstring SessionName(HANDLE Process, DWORD ProcessId);
	FSettings LoadSettings(const std::wstring& ApplicationDirectory);
	void SaveSettings(const FSettings& Settings);
}
