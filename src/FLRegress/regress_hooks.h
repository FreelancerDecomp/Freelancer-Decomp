// Freelancer Decomp: FLRegress, the headless regression harness -- PORT(flregress), not part of retail.
//
// Two things a run must not take from the machine it runs on, redirected in this process only by
// rewriting the import tables of the game modules (the DLLs beside the executable). No game
// module changes.
//
// - Time. The game reads the wall clock in places that feed the simulation:
//   IServerImpl::ElapseTime's Timing::Delta (which drives pub::GetTime), the weapons-offline
//   repair timer, srand seeds. Two runs would then differ by how long each frame really took.
//   Once the virtual clock is enabled, QueryPerformanceCounter, timeGetTime and GetTickCount
//   return a clock that starts at a fixed value and moves only by Advance().
// - The user folder. Accounts and single-player saves live under My Documents\My Games\
//   Freelancer; a run would see the previous run's saves (and could touch a player's real
//   ones). SHGetFolderPathA(CSIDL_PERSONAL) answers a sandbox folder instead.

#pragma once

namespace RegressHooks
{
    // The folder SHGetFolderPathA returns for CSIDL_PERSONAL from now on.
    void SetDocumentsFolder(const char* Path);

    // Patches the game modules loaded so far; call again after more DLLs are loaded. Returns
    // the number of import entries patched by this call.
    int Install();

    // From now on the time functions answer the virtual clock (before this they pass through
    // to Windows).
    void EnableVirtualClock();

    // Moves the virtual clock forward by the same whole number of counter ticks for the same
    // Seconds, so the elapsed time the game measures is identical every frame.
    void Advance(double Seconds);
}
