// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_newchar.h"

#include "common_ini.h"

#include <map>

extern "C"
{
    __declspec(dllimport) extern void(__cdecl* FDUMP)(int flags, const char* fmt, ...);
}

namespace
{
    const char kRtcSliderSource[] = "E:\\FL\\Scratch\\Source\\Common\\RtcSlider.cpp";
    const float kDefaultFpsHigh = 200.0f;
    const float kFpsCap30 = 30.0f;
    const float kFpsCap20 = 20.0f;

    float GRtcSliderValue = 0.0f;
    std::map<unsigned, FRtcSliderSetting> GRtcSliderSettings;

    // The setting test inlined in every caller (.text:0627a624..0627a65d): mode 0 is enabled
    // when the slider is not below the threshold (FCOMP + TEST AH,0x1 + JZ -> true; NaN false),
    // mode 1 when it is below (TEST AH,0x5 + JP -> false; NaN false), any other mode is false.
    bool SettingIsEnabled(FRtcSliderSetting const& Setting)
    {
        if (Setting.CompareMode == 0)
            return Setting.Threshold <= GRtcSliderValue;
        if (Setting.CompareMode == 1)
            return GRtcSliderValue < Setting.Threshold;
        return false;
    }

    // FUN_06301640 (find) on the map at DAT_063fbbc8: null when the id is absent (the caller's
    // `CMP EAX,[DAT_063fbbcc]` against the head node).
    FRtcSliderSetting const* FindSliderSetting(unsigned NameId)
    {
        std::map<unsigned, FRtcSliderSetting>::const_iterator It = GRtcSliderSettings.find(NameId);
        if (It == GRtcSliderSettings.end())
            return nullptr;
        return &It->second;
    }

    // FUN_0627a8a0: the map insert of {id, {id, threshold, mode}}; an id already present keeps
    // its first record.
    void InsertSliderSetting(unsigned NameId, float Threshold, int CompareMode)
    {
        if (GRtcSliderSettings.find(NameId) != GRtcSliderSettings.end())
            return;

        FRtcSliderSetting Setting;
        Setting.NameId = NameId;
        Setting.Threshold = Threshold;
        Setting.CompareMode = CompareMode;
        GRtcSliderSettings[NameId] = Setting;
    }
}

void __cdecl RtcSlider::load(char const* Path)
{
    // Retail FUN_0627a110 (the RtcSlider object at DAT_063fbbc4: the slider value, then the
    // map); the export 0x0627a5b0 only forwards the path. The reader is closed by its
    // destructor (.text:0627a3c1..).
    INI_Reader Reader;
    if (!Reader.open(Path, false))
    {
        // .text:0627a3a0..0627a3be.
        FDUMP(0x100001, "%s(%d) : *** ERROR: Cannot open rtc performance slider ini file %s\n",
              kRtcSliderSource, 0xba, Path);
        return;
    }

    while (Reader.read_header())
    {
        if (!Reader.is_header("RtcSlider"))
        {
            // .text:0627a360..0627a384.
            FDUMP(0x100002,
                  "%s(%d) : *** WARNING: Invalid header %s in rtc performance slider ini file %s\n",
                  kRtcSliderSource, 0xb4, Reader.get_header_ptr(), Path);
            continue;
        }

        while (Reader.read_value())
        {
            if (Reader.is_value("set"))
            {
                // .text:0627a1ba..0627a1e0: both parameters must be present.
                if (Reader.is_value_empty(0) || Reader.is_value_empty(1))
                {
                    // .text:0627a234..0627a25c.
                    FDUMP(0x100002,
                          "%s(%d) : *** WARNING: Invalid syntax %s in rtc performance slider ini file %s\n",
                          kRtcSliderSource, 0x8d, Reader.get_line_ptr(), Path);
                    continue;
                }
                // .text:0627a1e2..0627a22a: mode 0.
                unsigned NameId = CreateID(Reader.get_value_string(0));
                InsertSliderSetting(NameId, Reader.get_value_float(1), 0);
            }
            else if (Reader.is_value("set_less"))
            {
                if (Reader.is_value_empty(0) || Reader.is_value_empty(1))
                {
                    // .text:0627a2f1..0627a319.
                    FDUMP(0x100002,
                          "%s(%d) : *** WARNING: Invalid syntax %s in rtc performance slider ini file %s\n",
                          kRtcSliderSource, 0xa7, Reader.get_line_ptr(), Path);
                    continue;
                }
                // .text:0627a29f..0627a2ea: mode 1.
                unsigned NameId = CreateID(Reader.get_value_string(0));
                InsertSliderSetting(NameId, Reader.get_value_float(1), 1);
            }
            else
            {
                // .text:0627a320..0627a341: the no-argument get_value_string (0x06310b70).
                FDUMP(0x100002,
                      "%s(%d) : *** WARNING: Invalid value %s in rtc performance slider ini file %s\n",
                      kRtcSliderSource, 0xad, Reader.get_value_string(), Path);
            }
        }
    }
}

bool __cdecl RtcSlider::is_enabled(char const* Name)
{
    // Retail 0x0627a5c0: an absent name is reported with the name (line 0xe1) and is enabled.
    FRtcSliderSetting const* Setting = FindSliderSetting(CreateID(Name));
    if (Setting == nullptr)
    {
        FDUMP(0x100002, "%s(%d) : *** WARNING: Cannot find performance slider setting for name %s\n",
              kRtcSliderSource, 0xe1, Name);
        return true;
    }
    return SettingIsEnabled(*Setting);
}

bool __cdecl RtcSlider::is_enabled(unsigned long NameCrc)
{
    // Retail 0x0627a660: crc 0 is enabled without a lookup; an absent crc is reported with the
    // crc (line 0xd4) and is enabled.
    if (NameCrc == 0)
        return true;
    FRtcSliderSetting const* Setting = FindSliderSetting(NameCrc);
    if (Setting == nullptr)
    {
        FDUMP(0x100002, "%s(%d) : *** WARNING: Cannot find performance slider setting for name_crc %u\n",
              kRtcSliderSource, 0xd4, NameCrc);
        return true;
    }
    return SettingIsEnabled(*Setting);
}

void __cdecl RtcSlider::set_slider(float Value)
{
    GRtcSliderValue = Value;
}

float __cdecl RtcSlider::get_slider()
{
    return GRtcSliderValue;
}

float __cdecl RtcSlider::get_fps_cap()
{
    // Retail 0x0627a740: is_enabled("fps_20_cap") then is_enabled("fps_30_cap") inlined; an
    // absent name is enabled (the same warning, line 0xe1), so it returns that cap. 20.0 is
    // DAT_0639b21c (0x41a00000), 30.0 DAT_0639b20c (0x41f00000), 200.0 the 0x43480000 local.
    FRtcSliderSetting const* Fps20 = FindSliderSetting(CreateID("fps_20_cap"));
    if (Fps20 == nullptr)
    {
        FDUMP(0x100002, "%s(%d) : *** WARNING: Cannot find performance slider setting for name %s\n",
              kRtcSliderSource, 0xe1, "fps_20_cap");
        return kFpsCap20;
    }
    if (SettingIsEnabled(*Fps20))
        return kFpsCap20;

    FRtcSliderSetting const* Fps30 = FindSliderSetting(CreateID("fps_30_cap"));
    if (Fps30 == nullptr)
    {
        FDUMP(0x100002, "%s(%d) : *** WARNING: Cannot find performance slider setting for name %s\n",
              kRtcSliderSource, 0xe1, "fps_30_cap");
        return kFpsCap30;
    }
    if (SettingIsEnabled(*Fps30))
        return kFpsCap30;
    return kDefaultFpsHigh;
}

float __cdecl RtcSlider::get_default_fps_cap()
{
    return kDefaultFpsHigh;
}

bool __cdecl RtcSlider::validate_setting(char const* Name, char const* FileName)
{
    // 0x0627a710: MOV AL,1; RET. Retail checks nothing -- the setting name and the file it
    // came from are both ignored.
    (void)Name;
    (void)FileName;
    return true;
}
