// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Universe\MissionCreatedSolars.ini (see mission_created_solars.h).

#include "mission_created_solars.h"

#include "common_id.h"    // CreateID
#include "common_ini.h"   // INI_Reader
#include "common_misc.h"  // GetFileSysPath

std::vector<FMissionCreatedSolar> G_MissionCreatedSolars;
unsigned char G_bMissionCreatedSolarsLoaded = 0;

void LoadMissionCreatedSolars()
{
    // FUN_00476c40.
    if (G_bMissionCreatedSolarsLoaded != 0)
        return;

    // 00476c71..00476c86: the latch first, then the path against no file system.
    char Path[0x104];
    G_bMissionCreatedSolarsLoaded = 1;
    GetFileSysPath(Path, "Universe\\MissionCreatedSolars.ini", nullptr);

    INI_Reader Reader;
    if (!Reader.open(Path, false))
        return;

    unsigned BaseId = 0;
    int IdsInfo = 0;
    float Pos[3] = {0.0f, 0.0f, 0.0f};
    while (Reader.read_header())
    {
        if (!Reader.is_header("MissionCreatedSolar"))
            continue;

        // 00476cec..00476cf7: the three "seen" flags cleared per section.
        bool bHasBase = false;
        bool bHasInfo = false;
        bool bHasPos = false;
        while (Reader.read_value())
        {
            if (Reader.is_value("base"))
            {
                BaseId = CreateID(Reader.get_value_string());
                bHasBase = true;
            }
            else if (Reader.is_value("ids_info"))
            {
                IdsInfo = static_cast<int>(Reader.get_value_float(0));
                bHasInfo = true;
            }
            else if (Reader.is_value("pos"))
            {
                Pos[0] = Reader.get_value_float(0);
                Pos[1] = Reader.get_value_float(1);
                Pos[2] = Reader.get_value_float(2);
                bHasPos = true;
            }

            // 00476dc1..00476e13: all three seen -> push the record, start over.
            if (bHasBase && bHasInfo && bHasPos)
            {
                FMissionCreatedSolar Record;
                Record.BaseId = BaseId;
                Record.Pos[0] = Pos[0];
                Record.Pos[1] = Pos[1];
                Record.Pos[2] = Pos[2];
                Record.IdsInfo = IdsInfo;
                G_MissionCreatedSolars.push_back(Record); // FUN_00477510(end, 1, &Record)
                bHasBase = false;
                bHasInfo = false;
                bHasPos = false;
            }
        }
    }
    // 00476e3d..00476fc4: the reader's destructor (inlined in retail).
}

bool FindMissionCreatedSolar(unsigned BaseId, float* OutPos, int* OutIdsInfo)
{
    // FUN_00476fe0.
    LoadMissionCreatedSolars();

    int const Count = static_cast<int>(G_MissionCreatedSolars.size());
    for (int Index = 0; Index < Count; ++Index)
    {
        FMissionCreatedSolar const& Record = G_MissionCreatedSolars[Index];
        if (Record.BaseId != BaseId)
            continue;
        OutPos[0] = Record.Pos[0];
        OutPos[1] = Record.Pos[1];
        OutPos[2] = Record.Pos[2];
        *OutIdsInfo = Record.IdsInfo;
        return true;
    }
    return false;
}
