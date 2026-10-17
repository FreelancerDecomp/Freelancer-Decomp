// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_newchar.h"

#include "common_misc.h"

#include <cstdio>
#include <cstring>
#include <new>

extern "C"
{
    __declspec(dllimport) extern void(__cdecl* FDUMP)(int flags, const char* fmt, ...);
}

namespace
{
    const char kStateGraphSource[] = "E:\\FL\\Scratch\\Source\\Common\\Ai\\pubStateGraph.cpp";
    const unsigned kStateGraphStride = 0x72c;

    int GStateGraphCount = 0;
    IStateGraph* GStateGraphs = nullptr;

    // LAB_062df410 (static-initializer table, .data:063ea3b8) zeroes the count and the array
    // pointer and registers LAB_062df430 through atexit (FUN_06391f9e): at module teardown the
    // array is released (operator delete, FUN_06391d5a) and the pointer cleared; the count is
    // left. A file-static object with that destructor is the same registration (CMN-S2 AIS).
    struct FStateGraphStorageTeardown
    {
        ~FStateGraphStorageTeardown()
        {
            // 062df430-062df44c
            if (GStateGraphs != nullptr)
            {
                ::operator delete(GStateGraphs);
                GStateGraphs = nullptr;
            }
        }
    };
    FStateGraphStorageTeardown GStateGraphStorageTeardown;

    static_assert(sizeof(IStateGraph) == kStateGraphStride, "IStateGraph stride 0x72c");
}

int CallDacomStricmp(const char* Left, const char* Right);

void __cdecl pub::StateGraph::Free()
{
    // Retail FUN_062df7a0 (and the inline copies at 0x062df430 / 0x062df580): operator delete
    // (FUN_06391d5a) on the array, the pointer cleared; the count is left as it was.
    if (GStateGraphs != nullptr)
    {
        ::operator delete(GStateGraphs);
        GStateGraphs = nullptr;
    }
}

void __cdecl pub::StateGraph::LoadFromFile(char const* Path)
{
    // Retail FUN_062df5d0.
    FILE* File = fopen(Path, "r");
    if (File == nullptr)
    {
        // .text:062df5f4..062df653.
        FDUMP(0x100001, "%s(%d) : *** ERROR: %s", kStateGraphSource, 0x7d,
              "pub::StateGraph: unable to load state graph definitions");
        return;
    }

    int GraphCount = 0;
    int BehaviorCount = 0;
    fscanf(File, "%*s%d", &GraphCount);
    fscanf(File, "%*s%d", &BehaviorCount);
    if (BehaviorCount != kStateGraphBehaviors)
    {
        // .text:062df631..062df65f: reported and left without fclose -- retail leaks the FILE
        // here, and so does the port.
        FDUMP(0x100001, "%s(%d) : *** ERROR: %s", kStateGraphSource, 0x8d,
              "pub::StateGraph: state graph definitions don't sync with current behavior list, need to re-save state_graph.ini");
        return;
    }

    // FUN_062df740 (.text:062df66a): the count stored, then operator new(count * 0x72c) with the
    // 32-bit product (IMUL) -- the previous array is not freed here (refresh_state_graph frees
    // it first; DataConfig::init at .text:06309470 does not). VC6 operator new answers null on
    // failure; every record gets its weights zeroed and its id -1, the id/type/name dwords at
    // +0x04..+0x47 are left as allocated.
    GStateGraphCount = GraphCount;
    unsigned const Bytes = (unsigned)GraphCount * kStateGraphStride;
    GStateGraphs = static_cast<IStateGraph*>(::operator new(Bytes, std::nothrow));
    if (GStateGraphs != nullptr)
    {
        int InitIndex = 0;
        for (; InitIndex < GraphCount; ++InitIndex)
        {
            memset(GStateGraphs[InitIndex].Weights, 0, sizeof(GStateGraphs[InitIndex].Weights));
            GStateGraphs[InitIndex].Id = -1;
        }
    }
    else if (GraphCount > 0)
    {
        // Retail UB: with the allocation failed and a positive count retail goes on and writes
        // through the null array (.text:062df688); the port stops reading instead and drops the
        // count, so the lookups below (which, like retail, do not test the pointer) find nothing.
        GStateGraphCount = 0;
        fclose(File);
        return;
    }

    int GraphIndex = 0;
    for (; GraphIndex < GraphCount; ++GraphIndex)
    {
        IStateGraph& Graph = GStateGraphs[GraphIndex];

        // .text:062df688..062df6a5: the name is scanned straight into +0x08 and the type into a
        // 0x100-byte stack buffer, both with an unbounded "%s". Retail UB: a longer token
        // overruns the field (into the weights, later re-read) or the stack buffer. The port
        // scans both into stack buffers and keeps the name to the field: deterministic for a
        // name longer than 0x3f characters, identical otherwise.
        char NameBuf[0x100];
        char TypeBuf[0x100];
        NameBuf[0] = '\0';
        TypeBuf[0] = '\0';
        fscanf(File, "%255s", NameBuf);
        fscanf(File, "%255s", TypeBuf);
        strncpy(Graph.Name, NameBuf, kStateGraphNameChars - 1);
        Graph.Name[kStateGraphNameChars - 1] = '\0';

        // .text:062df6ab..062df6c6: NEG/SBB/NEG -- 1 (ESCORT) when "ESCORT" occurs, else 0.
        Graph.Type = (strstr(TypeBuf, "ESCORT") != nullptr) ? ESCORT : LEADER;

        // .text:062df6cd..062df6ff: 0x15 x 0x15 "%f" reads into +0x48, row by row.
        int Row = 0;
        for (; Row < kStateGraphBehaviors; ++Row)
        {
            int Col = 0;
            for (; Col < kStateGraphBehaviors; ++Col)
                fscanf(File, "%f", &Graph.Weights[Row][Col]);
        }

        // .text:062df701..062df70f.
        Graph.Id = GraphIndex;
    }

    fclose(File);
}

void __cdecl pub::StateGraph::refresh_state_graph()
{
    char Path[0x104];
    Free();
    GetFileSysPath(Path, "AI\\state_graph.db", nullptr);
    LoadFromFile(Path);
}

int __cdecl pub::StateGraph::get_state_graph(char const* Name, Type GraphType)
{
    // Retail 0x062df450: the records in order while index < count (no pointer test), DACOM
    // stricmp(record name, Name), then the type; the first match answers its id.
    int Index = 0;
    for (; Index < GStateGraphCount; ++Index)
    {
        if (CallDacomStricmp(GStateGraphs[Index].Name, Name) != 0)
            continue;
        if (GStateGraphs[Index].Type != GraphType)
            continue;
        return GStateGraphs[Index].Id;
    }
    return -1;
}

int __cdecl pub::StateGraph::get_state_graph(int GraphId, Type GraphType)
{
    // Retail 0x062df4c0: as above with the name of record GraphId (`IMUL EAX,EAX,0x72c` +
    // 8). Count <= 0 answers -1 before the record is touched, as retail's JLE at 062df4cd.
    // Very strange, but retail: GraphId is not range-checked (062df4cf-062df4df), so an id
    // outside [0, count) reads past the array. The former `-1` range guard was invented
    // (removed CMN-S2 AIS 2026-10-14, owner's ruling on cheap retail UB).
    if (GStateGraphCount <= 0)
        return -1;

    char const* Name = GStateGraphs[GraphId].Name;
    int Index = 0;
    for (; Index < GStateGraphCount; ++Index)
    {
        if (CallDacomStricmp(GStateGraphs[Index].Name, Name) != 0)
            continue;
        if (GStateGraphs[Index].Type != GraphType)
            continue;
        return GStateGraphs[Index].Id;
    }
    return -1;
}

IStateGraph const* __cdecl pub::StateGraph::get_state_graph_internal(int GraphId)
{
    // Retail 0x062df540: the first record whose id (+0) equals GraphId, else null; no pointer
    // test.
    int Index = 0;
    for (; Index < GStateGraphCount; ++Index)
    {
        if (GStateGraphs[Index].Id == GraphId)
            return &GStateGraphs[Index];
    }
    return nullptr;
}
