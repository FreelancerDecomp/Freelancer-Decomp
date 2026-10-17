// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "globals.h"
#include "msvcrt_compat.h" // MsvcrtAtoi: retail MSVCRT.DLL wraps mod 2^32, the UCRT saturates
#include "common_ini.h"
#include "dacom.h"
#include "game_options.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

// DAT_00679bc0. Zero-initialised like retail's .bss copy; FUN_005621a0 fills it.
// (The definition lives in window.cpp next to CreateGameWindow.)

// DAT_0067ecbc.
bool G_bFpExceptionTrap = false;

// FUN_00417c50 (.text:00417c50-00417c91).
//   ESI = Src; EAX = 0; if Src == 0 return 0 (Dest untouched).
//   EAX = strlen(Src) (REPNE SCASB); if EAX >= Capacity: EAX = Capacity - 1 (JC skips when below).
//   copy EAX bytes (MOVSD/MOVSB), Dest[EAX] = 0, return EAX.
unsigned CopyBoundedString(char* Dest, unsigned Capacity, const char* Src)
{
    if (Src == nullptr)
        return 0;

    unsigned Length = static_cast<unsigned>(strlen(Src));
    if (Length >= Capacity)
        Length = Capacity - 1;
    memcpy(Dest, Src, Length);
    Dest[Length] = '\0';
    return Length;
}

// FUN_005621a0 (.text:005621a0-005626aa), __thiscall(this = &DAT_00679bc0, FClientCommandLine*),
// called once from WinMain at 0x005b31dd.
void LoadStartupConfig(DisplaySettings* Options, const FClientCommandLine* CommandLine)
{
    // 0x005621c8..0x005621e0: scatter the 71 descriptors at .data:006143b8 into this+0x28.
    // No bound check: every Index in the constant table is in range.
    for (int Row = 0; Row < GGameOptionDescCount; ++Row)
        Options->options[GGameOptionDescs[Row].Index] = GGameOptionDescs[Row].DefaultValue;

    // 0x005621e2..0x00562215.
    Options->ini_path.Length = CopyBoundedString(Options->ini_path.Text, 0x104, "Freelancer.ini");
    Options->data_path.Length = CopyBoundedString(Options->data_path.Text, 0x104, "..\\Data");

    // 0x00562225..0x00562265, in retail's store order.
    Options->field_878 = 0;
    Options->quick_flag = 0;
    Options->color_bpp = -1;
    Options->depth_bpp = -1;
    Options->stencil_bpp = 0;
    Options->width = 0x400;
    Options->height = 0x300;
    Options->interval = 1;
    Options->buffer_count = 2;
    Options->border = 1;
    Options->refresh_rate = 0x3c;
    Options->mode_field00 = -1;
    Options->fullscreen = 1;

    // [ESP+0x14]: the movie_file counter, zeroed at 0x00562269 and kept across every [Startup]
    // section in the file.
    int MovieCount = 0;

    INI_Reader Reader;
    if (Reader.open(Options->ini_path.Text, false))
    {
        if (Reader.read_header())
        {
            do
            {
                if (Reader.is_header("Freelancer"))
                {
                    if (Reader.read_value())
                    {
                        do
                        {
                            if (Reader.is_value("data path"))
                            {
                                const char* Value = Reader.get_value_string();
                                if (Value == nullptr)
                                {
                                    Options->data_path.Length = 0;
                                    Options->data_path.Text[0] = '\0';
                                }
                                else
                                {
                                    Options->data_path.Length =
                                        CopyBoundedString(Options->data_path.Text, 0x104, Value);
                                }
                            }
                        } while (Reader.read_value());
                    }
                }
                else if (Reader.is_header("Startup"))
                {
                    if (Reader.read_value())
                    {
                        do
                        {
                            // 0x00562378: the count is tested after the name, and bumped before the
                            // null check, so a null value still uses up a slot.
                            if (Reader.is_value("movie_file") && MovieCount < 3)
                            {
                                const char* Value = Reader.get_value_string();
                                FClientPathString& Movie = Options->movie_files[MovieCount];
                                ++MovieCount;
                                if (Value == nullptr)
                                {
                                    Movie.Length = 0;
                                    Movie.Text[0] = '\0';
                                }
                                else
                                {
                                    Movie.Length = CopyBoundedString(Movie.Text, 0x104, Value);
                                }
                            }
                        } while (Reader.read_value());
                    }
                }
                else if (Reader.is_header("Data"))
                {
                    if (Reader.read_value())
                    {
                        do
                        {
                            if (Reader.is_value("gate_tunnels"))
                            {
                                const char* Value = Reader.get_value_string();
                                if (Value == nullptr)
                                {
                                    Options->gate_tunnels.Length = 0;
                                    Options->gate_tunnels.Text[0] = '\0';
                                }
                                else
                                {
                                    Options->gate_tunnels.Length =
                                        CopyBoundedString(Options->gate_tunnels.Text, 0x104, Value);
                                }
                            }
                            else if (Reader.is_value("jump_effect"))
                            {
                                const char* Value = Reader.get_value_string();
                                if (Value == nullptr)
                                {
                                    Options->jump_effect.Length = 0;
                                    Options->jump_effect.Text[0] = '\0';
                                }
                                else
                                {
                                    Options->jump_effect.Length =
                                        CopyBoundedString(Options->jump_effect.Text, 0x104, Value);
                                }
                            }
                        } while (Reader.read_value());
                    }
                }
            } while (Reader.read_header());
        }
    }
    else
    {
        // 0x005624a0.
        FDUMP(0x100001, "%s(%d) : *** ERROR: Unable to read Freelancer config file (%s)",
              "E:\\FL\\Scratch\\Source\\Client\\config.cpp", 0x17f, Options->ini_path.Text);
    }

    // 0x005624c1..0x0056252b: "+Q"/"-Q" and "+X"/"-X". A token that does not start with '+' or
    // '-' clears both the sign and the letter, so it matches nothing. The sign is '+'
    // (anything but '-'); "Q" ignores it.
    for (unsigned Index = 0; Index < CommandLine->Count; ++Index)
    {
        const char* Token = CommandLine->Argv[Index];
        bool bPlus = false;
        char Letter = 0;
        if (Token[0] == '+' || Token[0] == '-')
        {
            bPlus = Token[0] != '-';
            Letter = static_cast<char>(toupper(Token[1]));
        }

        if (Letter == 'Q')
            Options->quick_flag = 1;
        else if (Letter == 'X')
            G_bFpExceptionTrap = bPlus;
    }

    // 0x0056252f..0x005626a3 is the inlined INI_Reader destructor.
}

// FUN_004242e0 (.text:004242e0-00424434), __thiscall(this = render mode block,
// FClientCommandLine*), RET 4. The only caller is InitializeEngine (0x005b1799), on its local mode
// block after PerfOptions.ini has filled it.
bool ApplyDisplayCmdlineOverrides(DisplaySettings* Mode, const FClientCommandLine* CommandLine)
{
    bool bFound = false; // [ESP+local_6]

    for (unsigned Index = 0; Index < CommandLine->Count; ++Index)
    {
        const char* Token = CommandLine->Argv[Index];
        bool bPlus = false; // BL: '+' sets it, '-' clears it (SETNZ after CMP AL,'-')
        char Letter = 0;    // [ESP+local_5]; zero for a token without a sign
        const char* Tail = Token;
        if (Token[0] == '+' || Token[0] == '-')
        {
            bPlus = Token[0] != '-';
            Letter = static_cast<char>(toupper(Token[1]));
            Tail = Token + 2;
        }

        // switchD_00424358: Letter - 'B' in [0, 0x15], table at 0x00424438 / 0x0042444c.
        switch (Letter)
        {
        case 'B':
            bFound = true;
            Mode->border = bPlus ? 0 : 1;
            break;
        case 'F':
            bFound = true;
            Mode->fullscreen = bPlus ? 0 : 1;
            break;
        case 'M':
        {
            bFound = true;
            const int Size = MsvcrtAtoi(Tail);
            if (Size > 800)
            {
                if (Size == 0x400)
                {
                    Mode->width = 0x400;
                    Mode->height = 0x300;
                }
                else
                {
                    Mode->width = 0x280;
                    Mode->height = 0x1e0;
                }
            }
            else if (Size == 800)
            {
                Mode->width = 0x320;
                Mode->height = 0x258;
            }
            else if (Size == 0x140)
            {
                Mode->width = 0x140;
                Mode->height = 0xf0;
            }
            else if (Size == 0x200)
            {
                Mode->width = 0x200;
                Mode->height = 0x180;
            }
            else
            {
                Mode->width = 0x280;
                Mode->height = 0x1e0;
            }
            break;
        }
        case 'W':
            bFound = true;
            Mode->fullscreen = 0;
            break;
        default:
            break;
        }
    }

    return bFound;
}
