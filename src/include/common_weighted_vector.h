// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// weighted_vector<T> moved out of common_goods.h (2026-09-24) so that common_universe.h can
// hold the zone population (Universe::IZone::Population, retail zone +0x8c) without
// including common_goods.h, which includes common_net.h, which includes common_universe.h.
// common_goods.h includes this header, so every existing user still sees it.

#include "common_exports.h"

#include <cstdlib>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// weighted_vector<T> -- retail Common.dll template, instantiated and exported only for
// T = unsigned int (nine exports). Retail layout is a VC6 std::vector<std::pair<T, float>>
// (allocator byte + first/last/end: the copy ctor at 0x06261100 walks 8-byte pairs at
// +4..+8, GetSumOfWeights reads pair+4). The port keeps a modern
// std::vector<std::pair<T, float>> member.
//
// Retail name and class-key: `class weighted_vector` (mangled V?$weighted_vector@I@@,
// e.g. Server's ?GetPopulation@Zone@pub@@YAHIAAPBV?$weighted_vector@I@@@Z). This is
// the ONE declaration; common_server.h only forward-declares it. The explicit
// instantiation below exports exactly retail's members: the three ctors, the dtor, both
// operator= (returning `const&`, as retail's mangling says), GetSumOfWeights and both
// Pick overloads (one ICF body in retail).
template <typename T>
class weighted_vector
{
public:
    // 0x06266310 ??0?$weighted_vector@I@@QAE@XZ -- zero the four dwords (empty vector).
    weighted_vector() {}

    // 0x06261100 ??0?$weighted_vector@I@@QAE@ABV0@@Z -- the vector copy: a buffer of
    // exactly Other's size, every pair copied (weights kept), capacity == size.
    weighted_vector(weighted_vector const& Other) : Items(Other.Items) {}

    // 0x06266330 ??0?$weighted_vector@I@@QAE@ABV?$vector@IV?$allocator@I@std@@@std@@@Z --
    // empty vector, reserve(Ids.size()) (.text:062663b7..06266431), then every id from
    // Ids appended in order with weight 1.0 (.text:06266440..06266468, 0x3f800000).
    explicit weighted_vector(std::vector<T> const& Ids)
    {
        Items.reserve(Ids.size());
        for (unsigned Index = 0; Index < Ids.size(); ++Index)
            Items.push_back(std::pair<T, float>(Ids[Index], 1.0f));
    }

    // 0x06269110 ??1?$weighted_vector@I@@QAE@XZ -- free the buffer (ICF-shared with the
    // other VC6 vector dtors).
    ~weighted_vector() {}

    // 0x062665c0 ??4?$weighted_vector@I@@QAEABV0@ABV0@@Z -- NOT the std::vector operator=:
    // erase everything (.text:062665c7..062665f8), reserve(Other.size())
    // (.text:062665fb..06266697), then append a copy of every pair of Other
    // (.text:0626669b..062666ca). There is no self-assignment test, so `v = v` empties v
    // (Other's size is read after the erase); the port does the same.
    weighted_vector const& operator=(weighted_vector const& Other)
    {
        Items.clear();
        Items.reserve(Other.Items.size());
        for (unsigned Index = 0; Index < Other.Items.size(); ++Index)
            Items.push_back(Other.Items[Index]);
        return *this;
    }

    // 0x06266490 ??4?$weighted_vector@I@@QAEABV0@ABV?$vector@IV?$allocator@I@std@@@std@@@Z
    // -- erase everything (.text:06266497..062664c8), reserve(Ids.size())
    // (.text:062664cb..06266579), append every id with weight 1.0
    // (.text:0626657a..062665aa).
    weighted_vector const& operator=(std::vector<T> const& Ids)
    {
        Items.clear();
        Items.reserve(Ids.size());
        for (unsigned Index = 0; Index < Ids.size(); ++Index)
            Items.push_back(std::pair<T, float>(Ids[Index], 1.0f));
        return *this;
    }

    // 0x062666e0 ?GetSumOfWeights@?$weighted_vector@I@@QBEMXZ -- FLD 0.0, then FADD of
    // every pair's weight (+4) in order; the sum is never stored and is returned unrounded
    // in ST0, hence double (no importer in any module, no caller in Common).
    double GetSumOfWeights() const
    {
        double Sum = 0.0;
        for (unsigned Index = 0; Index < Items.size(); ++Index)
            Sum += Items[Index].second;
        return Sum;
    }

    // 0x06266700 ?Pick@?$weighted_vector@I@@QBEPBU?$pair@IM@std@@XZ (and the non-const
    // overload, same body). Weighted random choice:
    //   - Sum = 0.0 + every weight (.text:06266700..0626671f); empty -> end();
    //   - Sum == 0.0 -> end() (.text:06266725 FCOMP 0.0 / TEST AH,0x44 / JNP: C3 alone);
    //   - Target = rand() * (1/32768) * Sum (.text:06266732..0626674f; the constant at
    //     .rdata:063997d0 is 0x38000000 = 2^-15);
    //   - walk again accumulating weights and return the first pair whose running total
    //     Acc satisfies Target <= Acc (.text:06266763 FLD Target / FCOMP ST1 /
    //     TEST AH,0x41 / JNP: C0 or C3 alone); none -> end().
    // Retail keeps both running sums on the x87 stack (no store between the FADDs) and
    // stores only the total (FST float, 0x06266721) and Target (FSTP float, 0x0626674f), so
    // each FADD is rounded by the thread's x87 precision control. That differs by process:
    // the single-player client runs Server/Content on its main thread (IServer::Update from
    // FUN_0042d740; no thread is created by freelancer.exe, Server, Common, Content or
    // rpclocal), where Direct3D 8 CreateDevice without FPU_PRESERVE (EXE\dacom.ini, rp8
    // .text:06d06bb4) leaves 24-bit precision; FLServer.exe sets 53-bit itself
    // (_control87(0x9001f, 0xfffff) at 0x0040bb17). The port is x87 (/arch:IA32,
    // src/Directory.Build.props), so a double running sum held in an x87 register is rounded
    // by the same control word: 24-bit in the client (identical to the old float version),
    // 53-bit in a server process. A float variable would force 24-bit everywhere. The zero
    // test reads the unrounded sum (FCOMP right after the FST).
    std::pair<T, float> const* Pick() const
    {
        std::pair<T, float> const* End = Items.data() + Items.size();
        if (Items.empty())
            return End;
        double Sum = 0.0;
        for (unsigned Index = 0; Index < Items.size(); ++Index)
            Sum += Items[Index].second;
        float const Total = (float)Sum;
        if (Sum == 0.0)
            return End;
        float const Target = (float)((double)rand() * (1.0f / 32768.0f) * Total);
        double Acc = 0.0;
        for (unsigned Index = 0; Index < Items.size(); ++Index)
        {
            Acc += Items[Index].second;
            if (Target <= Acc)
                return &Items[Index];
        }
        return End;
    }

    std::pair<T, float>* Pick()
    {
        weighted_vector const& ConstThis = *this;
        return const_cast<std::pair<T, float>*>(ConstThis.Pick());
    }

    std::vector<std::pair<T, float>> Items;
};

// Retail Common.dll exports the unsigned int instantiation. The explicit instantiation
// definition is in common_equipdesc.cpp; importing modules take it from Common.dll.
#ifndef COMMON_EXPORTS
extern template class COMMON_API weighted_vector<unsigned int>;
#endif
