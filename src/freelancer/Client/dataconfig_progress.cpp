// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "dataconfig_progress.h"

#include "client_debris.h" // LoadDebrisArchFile (FUN_00530d20)
#include "common_ini.h"
#include "common_misc.h" // GetFileSysPath
#include "dacom.h"
#include "DALib.h"
#include "globals.h"
#include "font_imp.h"
#include "graph_library.h"
#include "hud_element_list.h"
#include "ids_strings.h"   // RegisterIdsResourceModule (FUN_0057d800)
#include "menu_fonts_load.h"
#include "menu_sound_arch.h"
#include "sound_config.h"  // LoadClientSoundCfgIni (FUN_00427a70)
#include "texture_shapes.h"
#include "vis_effect_arch.h"
#include "voice_profiles.h"

#include <stdlib.h>
#include <string.h>

namespace
{
    // .rdata 0x005e6a08, the source path FUN_005b0c30's FDUMP names.
    const char kMainCppPath[] = "E:\\FL\\Scratch\\Source\\Client\\main.cpp";

    // Every string field starts {0, ""} (the LAB_00564a60..LAB_00564ae0 static initializers);
    // bDrawScripts is DAT_00614a9c, 01 in the PE image.
    FIntroConfigState GIntroConfigState = {
        {0u, {0}}, {0u, {0}}, {0u, {0}}, {0u, {0}},
        {{0u, {0}}, {0u, {0}}, {0u, {0}}, {0u, {0}}},
        {0u, {0}}, 0, true};

    // FUN_00564b30 per-key store: a null value clears the field (00564c10: length 0, first byte
    // 0), otherwise FUN_00417c50 copies at most 0x3f chars and the length is stored in front.
    void ReadIntroPathValue(INI_Reader& Reader, FIntroPathString& Field)
    {
        char const* const Value = Reader.get_value_string();
        if (Value == nullptr)
        {
            Field.Length = 0;
            Field.Text[0] = '\0';
            return;
        }
        Field.Length = CopyBoundedString(Field.Text, 0x40, Value);
    }

    // FUN_00564b30 (reached through thunk_FUN_00564b30 from FUN_005b0c30 0x005b0e7e). No reset
    // on entry: a key the file does not carry keeps its previous value (the static
    // initializers zero every field before the first load). An open failure only runs the
    // reader's cleanup (00564b99 -> 00564eb8): no logo, no pick.
    void LoadIntroConfigFromIni(char const* IntroIniPath)
    {
        INI_Reader Reader;
        // 0x00564b5d..0x00564b7b: the argument -- already resolved by the caller -- goes
        // through GetFileSysPath against DALib::Data a second time; the result is opened.
        char ResolvedPath[MAX_PATH];
        GetFileSysPath(ResolvedPath, IntroIniPath, DALib::Data);
        if (!Reader.open(ResolvedPath, false))
            return;

        while (Reader.read_header())
        {
            if (Reader.is_header("Images") && Reader.read_value())
            {
                do
                {
                    if (Reader.is_value("TgaPath"))
                        ReadIntroPathValue(Reader, GIntroConfigState.TgaPath);
                    else if (Reader.is_value("ScreenType"))
                        ReadIntroPathValue(Reader, GIntroConfigState.ScreenType);
                    else if (Reader.is_value("ScreenExit"))
                        ReadIntroPathValue(Reader, GIntroConfigState.ScreenExit);
                    else if (Reader.is_value("ScreenReset"))
                        ReadIntroPathValue(Reader, GIntroConfigState.ScreenReset);
                    else if (Reader.is_value("Screen1"))
                        ReadIntroPathValue(Reader, GIntroConfigState.Screen[0]);
                    else if (Reader.is_value("Screen2"))
                        ReadIntroPathValue(Reader, GIntroConfigState.Screen[1]);
                    else if (Reader.is_value("Screen3"))
                        ReadIntroPathValue(Reader, GIntroConfigState.Screen[2]);
                    else if (Reader.is_value("Screen4"))
                        ReadIntroPathValue(Reader, GIntroConfigState.Screen[3]);
                    else if (Reader.is_value("bDrawScripts"))
                        GIntroConfigState.bDrawScripts = Reader.get_value_bool(0);
                    else
                        Reader.log_link("unknown ini parameter"); // 00564e27
                } while (Reader.read_value());
            }
        }

        // 00564e5c..00564e97: the logo name, bounded at 0x3f chars, and its length.
        GIntroConfigState.Logo.Length =
            CopyBoundedString(GIntroConfigState.Logo.Text, 0x40, "FRONT_freelancerlogo.tga");
        // 00564e9c..00564eb2: (rand() * 3) >> 15, i.e. 0, 1 or 2.
        GIntroConfigState.IntroBasePick = (rand() * 3) / 0x8000;
    }

    // One [Data] value of FUN_005b0c30 (0x005b0ce3..0x005b0ee9): an is_value chain in listing
    // order. Every key but fonts / rich_fonts / intro resolves the value with
    // GetFileSysPath(path, value, 0) into the 0x104-byte frame buffer first; intro resolves
    // against DALib::Data into a second buffer (ESP+0x1688). Loader results are dropped.
    void LoadClientDataValue(INI_Reader& Reader)
    {
        char Path[MAX_PATH];
        if (Reader.is_value("fonts"))
        {
            // 0x005b0d02..0x005b0d0e: FUN_00417800(value, DALib::Data).
            LoadFonts(Reader.get_value_string(), DALib::Data);
        }
        else if (Reader.is_value("rich_fonts"))
        {
            // 0x005b0d2d..0x005b0d39: FUN_0057e310(value, DALib::Data).
            LoadRichFontsFromIni(Reader.get_value_string(), DALib::Data);
        }
        else if (Reader.is_value("sounds"))
        {
            // 0x005b0d58..0x005b0d6f: FUN_0042b030(path, 0).
            GetFileSysPath(Path, Reader.get_value_string(), nullptr);
            LoadClientSoundArchIni(Path, false);
        }
        else if (Reader.is_value("voices"))
        {
            // 0x005b0d8e..0x005b0da3: FUN_00430b00(path).
            GetFileSysPath(Path, Reader.get_value_string(), nullptr);
            LoadClientVoicesIni(Path);
        }
        else if (Reader.is_value("effects"))
        {
            // 0x005b0dbf..0x005b0dd6: FUN_0040eac0(path, 0).
            GetFileSysPath(Path, Reader.get_value_string(), nullptr);
            EffectArchLoadIni(Path, false);
        }
        else if (Reader.is_value("effect_shapes"))
        {
            // 0x005b0df5..0x005b0e0a: FUN_004f7230(path) -- queued, FUN_004f70d0 drains it at the
            // end of InitializeEngine.
            GetFileSysPath(Path, Reader.get_value_string(), nullptr);
            AddEffectShapesFile(Path);
        }
        else if (Reader.is_value("debris"))
        {
            // 0x005b0e26..0x005b0e3d: FUN_00530d20(path, 0).
            GetFileSysPath(Path, Reader.get_value_string(), nullptr);
            LoadDebrisArchFile(Path, false);
        }
        else if (Reader.is_value("intro"))
        {
            // 0x005b0e55..0x005b0e7e: GetFileSysPath(path, value, DALib::Data), then
            // thunk_FUN_00564b30(path).
            char IntroPath[MAX_PATH];
            GetFileSysPath(IntroPath, Reader.get_value_string(), DALib::Data);
            LoadIntroConfigFromIni(IntroPath);
        }
        else if (Reader.is_value("HUD"))
        {
            // 0x005b0e9e..0x005b0eb3: FUN_004c7370(path) stores the path only; FUN_004c73d0
            // instantiates the elements later.
            GetFileSysPath(Path, Reader.get_value_string(), nullptr);
            SetHudIniPath(Path);
        }
        else if (Reader.is_value("igraph"))
        {
            // 0x005b0ec5..0x005b0ee1: FUN_004197e0(path).
            GetFileSysPath(Path, Reader.get_value_string(), nullptr);
            GraphLibraryLoad(Path);
        }
    }
}

FIntroConfigState const* GetIntroConfigState()
{
    return &GIntroConfigState;
}

bool LoadClientIniData(char const* IniPath)
{
    // FUN_005b0c30 (.text:005b0c30-005b12f8). 0x005b0c6b calls FUN_004b6c50 (MOV AL,1 / RET)
    // and drops the result -- nothing to port.
    INI_Reader Reader;
    if (Reader.open(IniPath, false))
    {
        // 0x005b0c8f / 0x005b0f98: one read_header per section; a false one ends the scan.
        while (Reader.read_header())
        {
            if (Reader.is_header("Data"))
            {
                // 0x005b0cd3..0x005b0efa.
                if (Reader.read_value())
                {
                    do
                    {
                        LoadClientDataValue(Reader);
                    } while (Reader.read_value());
                }
            }
            else if (Reader.is_header("Resources"))
            {
                // 0x005b0eff..0x005b0f96: every DLL key is LoadLibraryA'd; a failure is
                // reported (main.cpp line 0x22c) and the null handle is still pushed onto the
                // ids module list (FUN_0057d800 at 0x005b0f7f runs on both paths), so the
                // ids of the DLLs after it keep their index.
                if (Reader.read_value())
                {
                    do
                    {
                        if (Reader.is_value("DLL"))
                        {
                            HMODULE const Module = LoadLibraryA(Reader.get_value_string());
                            if (Module == nullptr)
                            {
                                char const* const Name = Reader.get_value_string();
                                DWORD const Error = GetLastError();
                                FDUMP(0x100001,
                                      "%s(%d) : *** ERROR: Couldn't load %s! Error code %d\n",
                                      kMainCppPath, 0x22c, Name, Error);
                            }
                            RegisterIdsResourceModule(Module);
                        }
                    } while (Reader.read_value());
                }
            }
        }

        // 0x005b0fad..0x005b0fc9: still inside the open success -- FUN_00427a70 on
        // GetFileSysPath("audio\\SoundCFG.ini", 0).
        char SoundCfgPath[MAX_PATH];
        GetFileSysPath(SoundCfgPath, "audio\\SoundCFG.ini", nullptr);
        LoadClientSoundCfgIni(SoundCfgPath);
    }
    // 0x005b0fcc..0x005b12f8: the reader's destructor, then MOV AL,1 on both exits.
    return true;
}
