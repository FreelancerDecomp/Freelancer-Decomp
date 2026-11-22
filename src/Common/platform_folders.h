// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

#pragma once

// Per-user folders, kept apart from platform.h because the Win32 backend needs the shell
// headers (shlobj.h brings in COM, including its `interface` macro).

#include "platform.h"

#include <shlobj.h> // SHGetFolderPathA

namespace Platform
{
    // Per-user folders (CSIDL_*). kFolderCreate is a flag: or it in to create the folder.
    constexpr int kFolderDocuments = CSIDL_PERSONAL;
    constexpr int kFolderLocalAppData = CSIDL_LOCAL_APPDATA;
    constexpr int kFolderPictures = CSIDL_MYPICTURES;
    constexpr int kFolderCreate = CSIDL_FLAG_CREATE;

    // SHGetFolderPathA(0, Folder, 0, SHGFP_TYPE_CURRENT, Path). Path holds MAX_PATH chars.
    // Returns the HRESULT: 0 (S_OK) on success.
    inline FlLong UserFolder(int Folder, char* Path)
    {
        return ::SHGetFolderPathA(nullptr, Folder, nullptr, SHGFP_TYPE_CURRENT, Path);
    }
} // namespace Platform
