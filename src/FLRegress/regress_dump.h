// Freelancer Decomp: FLRegress, the headless regression harness (port-only, not part of retail).
//
// Text dumps of game state, one record per line. Floats are written as their exact bit pattern
// (followed by a readable value), so any change in a result shows up as a diff.

#pragma once

#include <cstdio>

class Vector;

class FRegressWriter
{
public:
    explicit FRegressWriter(FILE* InFile) : File(InFile) {}

    void Line(const char* Format, ...);
    // "<hex bits>(<value>)"; a vector is three of them separated by commas.
    static void FormatFloat(char* Out, size_t Size, float Value);
    static void FormatVector(char* Out, size_t Size, const Vector& Value);

private:
    FILE* File;
};

// The universe as Common.dll loaded it: systems with their connections, objects and zones,
// then bases. Iteration follows the Universe lists, which are ordered maps.
void DumpUniverse(FRegressWriter& Out);

// Every live game object, per CObject class, in creation order (Common's per-class lists).
// Ships, solars, loot and projectiles get one line each with id, archetype, owner, hit points,
// position, orientation and velocity; asteroids and the id-less classes only a count.
void DumpObjects(FRegressWriter& Out);

// One line with the object count of each class, for the periodic progress records.
void DumpObjectCounts(FRegressWriter& Out, unsigned int Frame);
