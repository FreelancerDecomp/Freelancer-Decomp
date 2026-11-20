// Freelancer Decomp: FLRegress, the headless regression harness (port-only, not part of retail).

#include "regress_dump.h"

#include <cstdarg>
#include <cstring>

#include "common_universe.h"

void FRegressWriter::Line(const char* Format, ...)
{
    va_list Args;
    va_start(Args, Format);
    vfprintf(File, Format, Args);
    va_end(Args);
    fputc('\n', File);
}

void FRegressWriter::FormatFloat(char* Out, size_t Size, float Value)
{
    unsigned int Bits;
    static_assert(sizeof(Bits) == sizeof(Value), "float is 32-bit");
    memcpy(&Bits, &Value, sizeof(Bits));
    snprintf(Out, Size, "%08x(%.9g)", Bits, (double)Value);
}

void FRegressWriter::FormatVector(char* Out, size_t Size, const Vector& Value)
{
    char X[40];
    char Y[40];
    char Z[40];
    FormatFloat(X, sizeof(X), Value.x);
    FormatFloat(Y, sizeof(Y), Value.y);
    FormatFloat(Z, sizeof(Z), Value.z);
    snprintf(Out, Size, "%s,%s,%s", X, Y, Z);
}

namespace
{
    const char* OrEmpty(const char* Text)
    {
        return Text != nullptr ? Text : "";
    }

    void DumpSystem(FRegressWriter& Out, const Universe::ISystem& System)
    {
        char Pos[128];
        FRegressWriter::FormatVector(Pos, sizeof(Pos), *System.GetPos());
        Out.Line("system id=%08x nickname=%s file=%s strid_name=%d ids_info=%d pos=%s",
            System.GetId(), OrEmpty(System.GetNickname()), OrEmpty(System.GetFile()),
            System.GetStridName(), System.GetIdsInfo(), Pos);

        const int ConnectionCount = System.GetConnectionCount();
        for (int i = 0; i < ConnectionCount; ++i)
        {
            Universe::FSystemConnection Connection;
            if (!System.GetConnection((unsigned)i, &Connection))
            {
                Out.Line("  connection %d unavailable", i);
                continue;
            }
            FRegressWriter::FormatVector(Pos, sizeof(Pos), Connection.Pos);
            Out.Line("  connection goto=%08x object=%08x pos=%s visit=%u gate_type=%u",
                Connection.GotoSystemId, Connection.ObjectId, Pos, (unsigned)Connection.Visit,
                Connection.GateType);
        }

        const int ObjectCount = System.GetObjectCount();
        for (int i = 0; i < ObjectCount; ++i)
        {
            Universe::FSystemObject Object;
            if (!System.GetSystemObject((unsigned)i, &Object))
            {
                Out.Line("  object %d unavailable", i);
                continue;
            }
            FRegressWriter::FormatVector(Pos, sizeof(Pos), Object.Pos);
            Out.Line("  object nickname=%.*s pos=%s archetype=%08x ids_name=%d ids_info=%d"
                     " reputation=%.*s dock_with=%08x goto=%08x base=%08x visit=%u",
                (int)Object.Nickname.Length, Object.Nickname.Data, Pos, Object.ArchetypeId,
                Object.IdsName, Object.IdsInfo, (int)Object.Reputation.Length,
                Object.Reputation.Data, Object.DockWith, Object.Goto, Object.BaseId,
                (unsigned)Object.Visit);
        }

        for (const Universe::IZone* Zone = Universe::first_zone(System.GetId()); Zone != nullptr;
             Zone = Universe::next_zone(Zone))
        {
            char Size[128];
            FRegressWriter::FormatVector(Pos, sizeof(Pos), Zone->GetPos());
            FRegressWriter::FormatVector(Size, sizeof(Size), Zone->GetSize());
            Out.Line("  zone id=%08x shape=%u properties=%08lx pos=%s size=%s",
                Zone->GetId(), Zone->GetShape(), Zone->GetProperties(), Pos, Size);
        }
    }
}

void DumpUniverse(FRegressWriter& Out)
{
    Out.Line("universe systems=%u bases=%u", Universe::num_systems(), Universe::num_bases());

    for (const Universe::ISystem* System = Universe::GetFirstSystem(); System != nullptr;
         System = Universe::GetNextSystem())
    {
        DumpSystem(Out, *System);
    }

    for (const Universe::IBase* Base = Universe::GetFirstBase(); Base != nullptr;
         Base = Universe::GetNextBase())
    {
        Out.Line("base id=%08x nickname=%s system=%08x file=%s strid_name=%d autosave_forbidden=%d",
            Base->GetId(), OrEmpty(Base->GetNickname()), Base->GetSystem(), OrEmpty(Base->GetFile()),
            Base->GetStridName(), Base->IsAutosaveForbidden() ? 1 : 0);
    }
}
