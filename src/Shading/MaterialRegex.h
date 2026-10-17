// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include <memory>
#include <vector>

// Private Shading regexp program; no retail CRT or container layout crosses a DLL.
class FMaterialRegex
{
public:
    static std::unique_ptr<FMaterialRegex> Compile(const char* Pattern);
    static bool Search(const FMaterialRegex* Expression, const char* Text);

private:
    std::vector<unsigned char> Program;
    unsigned char StartCharacter = 0;
    bool bAnchored = false;
    int MustOffset = -1;
    int MustLength = 0;
};
