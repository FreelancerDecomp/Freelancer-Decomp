// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "anim_script_set.h"

#include "DALib.h"
#include "common_misc.h"
#include "engine/engbase.h"

#include <new>
#include <string.h>

namespace
{
    // FUN_00555b90 (cdecl) -- the counting callback: ++*Context. The name is not read.
    void __cdecl CountScriptName(char const* /*Name*/, void* Context)
    {
        FAnimScriptSet* Set = static_cast<FAnimScriptSet*>(Context);
        ++Set->Count;
    }

    // FUN_00555ba0 (cdecl) -- the filling callback: copy the name into Names[cursor], store
    // Anim +0x18 (GetNamedDuration) of it on the script being enumerated into
    // Durations[cursor], advance the cursor.
    void __cdecl FillScriptName(char const* Name, void* Context)
    {
        FAnimScriptSet* Set = static_cast<FAnimScriptSet*>(Context);
        malloc_strcpy(&Set->Names[Set->FillCursor], Name);
        Set->Durations[Set->FillCursor] =
            DALib::Anim->GetNamedDuration(reinterpret_cast<void*>(Set->LoadScript), Name);
        ++Set->FillCursor;
    }
}

void FAnimScriptSet::Init()
{
    // FUN_00555a40.
    Count = 0;
    Names = nullptr;
    Durations = nullptr;
    Script = 0xffffffffu;
    LoadScript = 0xffffffffu;
    FillCursor = 0;
}

void FAnimScriptSet::Reset()
{
    // FUN_00555a70.
    Count = 0;
    Names = nullptr;
    Durations = nullptr;
    Script = 0xffffffffu;
    LoadScript = 0xffffffffu;
    FillCursor = 0;
}

void FAnimScriptSet::Destroy()
{
    // FUN_00555a90. 0x00555a9b..0x00555ab7: malloc_strfree every name.
    int Index = 0;
    for (Index = 0; Index < Count; ++Index)
        malloc_strfree(&Names[Index]);
    // 0x00555aba..0x00555add: both arrays.
    if (Names != nullptr)
    {
        operator delete(Names);
        Names = nullptr;
    }
    if (Durations != nullptr)
    {
        operator delete(Durations);
        Durations = nullptr;
    }
    // 0x00555ae0..0x00555afb: Count = 0, then Anim +0x10 (CloseScript) when there is a script.
    Count = 0;
    if (Script != 0xffffffffu)
    {
        DALib::Anim->CloseScript(reinterpret_cast<void*>(Script));
        Script = 0xffffffffu;
    }
    // 0x00555afe..0x00555b0d.
    Script = 0xffffffffu;
    LoadScript = 0xffffffffu;
    Count = 0;
    Names = nullptr;
    Durations = nullptr;
    FillCursor = 0;
}

int FAnimScriptSet::Find(char const* Name) const
{
    // FUN_00555b20: an inlined strcmp against each name in order.
    int Index = 0;
    for (Index = 0; Index < Count; ++Index)
    {
        if (strcmp(Name, Names[Index]) == 0)
            return Index;
    }
    return -1;
}

int FAnimScriptSet::Load(unsigned long NewScript)
{
    // FUN_00555bf0. 0x00555bfa..0x00555bfd: both handles take the argument.
    Script = NewScript;
    LoadScript = NewScript;
    if (NewScript != 0xffffffffu)
    {
        // 0x00555c03..0x00555c1a: count the names.
        Count = 0;
        DALib::Anim->EnumScriptNames(reinterpret_cast<void*>(&CountScriptName),
                                     reinterpret_cast<void*>(NewScript), this);
        // 0x00555c1d..0x00555c39: one pointer and one float per name.
        Names = static_cast<char**>(operator new(static_cast<size_t>(Count) * 4));
        Durations = static_cast<float*>(operator new(static_cast<size_t>(Count) * 4));
        FillCursor = 0;
        // 0x00555c40..0x00555c58: fill them.
        DALib::Anim->EnumScriptNames(reinterpret_cast<void*>(&FillScriptName),
                                     reinterpret_cast<void*>(LoadScript), this);
    }
    // 0x00555c5b..0x00555c64.
    int Result = Count;
    LoadScript = 0xffffffffu;
    FillCursor = 0;
    return Result;
}

unsigned long FAnimScriptSet::Play(void* Root, int Index)
{
    // FUN_00555c70: Anim +0x24 (Script, Root, Names[Index], 0, 0).
    return DALib::Anim->PlayScript(reinterpret_cast<void*>(Script), Root, Names[Index], nullptr,
                                   nullptr);
}

void AnimScriptSetStopPlayback(unsigned long* Playback)
{
    // FUN_00555ca0: Anim +0x38 then Anim +0x28 on the playback, then -1.
    if (*Playback == 0xffffffffu)
        return;
    DALib::Anim->StopPlayback(*Playback);
    DALib::Anim->DestroyScript(reinterpret_cast<void*>(*Playback));
    *Playback = 0xffffffffu;
}
