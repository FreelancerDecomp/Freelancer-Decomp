// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include <iterator>

// The std::sort that the retail modules were built with (VC6 <algorithm>: _Sort_0 / _Sort /
// _Median / _Unguarded_partition / _Insertion_sort / _Unguarded_insert, _SORT_MAX 16).
// Instantiations ported with it, e.g.:
//   Content.dll   TU48 FUN_06f55a00 (_Sort_0 with the first _Sort pass inlined) + FUN_06f55cd0
//                 (_Sort) for the empathy rates; TU58 FZonePathLabel FUN_06f7f370 / 06f7f990
//   freelancer.exe GetFilteredServers (inlined at 0x005acb92..0x005acca6): FUN_005afea0 _Sort,
//                 FUN_005affc0 _Insertion_sort, FUN_005b0060 _Median, FUN_005b00d0
//                 _Unguarded_partition; the sound mixer's FUN_0042a810 / 0042a9a0 / 0042a9e0
//   Common.dll    the exclusion-zone sorts FUN_062bcd70 / 062bce70 / 062bcf70 / 062bcfa0
//   Thorn.dll     FUN_06f3fd80 / 06f3ff10 / 06f40120 / 06f401b0 / 06f40230 (master events),
//                 FUN_06f3ff50 / 06f400e0 / 06f40290 (active event types)
//
// It is a port of retail behaviour, not a replacement for std::sort: the algorithm is not
// stable, so the final order of elements that compare equal is decided by these exact
// steps; a different sort gives the same key order but a different order of ties. Every
// comparison goes through Less in retail's order, so a predicate that is not a strict weak
// order (NaN keys, null pointers that compare false both ways, wrapping differences) gives
// retail's result too.
//
// Less is the retail predicate: Less(A, B) is true when A goes first. Elements are copied
// with their copy constructor and assigned with operator=; a retail instantiation whose
// element assignment is not operator= (common_vibe's static store) keeps its own copy.

namespace Vc6Sort
{
    // _Median takes its three elements by value, as VC6's `_Median(_Ty _X, _Ty _Y, _Ty _Z, _Pr _P)`:
    // the caller copy-constructs the arguments and the callee destroys them (retail TU58
    // FZonePathLabel instance: argument copies in _Sort_0 06f7f370 states 1 / 2 and _Sort 06f7f990
    // states 0 / 1, parameter dtors in _Median 06f7fea0 states 0-2). Returning a parameter moves it
    // where retail copy-constructs the return slot; the returned value is the same.
    template <class T, class Pred>
    T Median(T A, T B, T C, Pred Less)
    {
        if (Less(A, B))
        {
            if (Less(B, C))
                return B;
            if (Less(A, C))
                return C;
            return A;
        }
        if (Less(A, C))
            return A;
        if (Less(B, C))
            return C;
        return B;
    }

    // _Unguarded_partition: advance First past elements below Pivot, step Last back past
    // elements above it; stop when they meet (Last <= First), else swap and go on.
    template <class RanIt, class T, class Pred>
    RanIt UnguardedPartition(RanIt First, RanIt Last, T const& Pivot, Pred Less)
    {
        for (;;)
        {
            while (Less(*First, Pivot))
                ++First;
            do
            {
                --Last;
            } while (Less(Pivot, *Last));
            if (Last <= First)
                return First;
            T const Swap = *First;
            *First = *Last;
            *Last = Swap;
            ++First;
        }
    }

    // _Sort: partition around the median of the first, middle ((Last - First) / 2) and last
    // element while more than 16 elements remain; recurse into one side (the right side when
    // it is not larger than the left), loop on the other.
    template <class RanIt, class Pred>
    void SortRange(RanIt First, RanIt Last, Pred Less)
    {
        typedef typename std::iterator_traits<RanIt>::value_type T;
        while (Last - First > 16)
        {
            T const Pivot = Median<T>(*First, First[(Last - First) / 2], *(Last - 1), Less);
            RanIt Cut = UnguardedPartition(First, Last, Pivot, Less);
            if (Last - Cut <= Cut - First)
            {
                SortRange(Cut, Last, Less);
                Last = Cut;
            }
            else
            {
                SortRange(First, Cut, Less);
                First = Cut;
            }
        }
    }

    // _Unguarded_insert: move the element at Position down while it is less than its
    // predecessor (a smaller element is known to sit in front).
    template <class RanIt, class Pred>
    void UnguardedInsert(RanIt Position, Pred Less)
    {
        typedef typename std::iterator_traits<RanIt>::value_type T;
        T const Value = *Position;
        RanIt Previous = Position - 1;
        while (Less(Value, *Previous))
        {
            *Position = *Previous;
            Position = Previous;
            --Previous;
        }
        *Position = Value;
    }

    // _Insertion_sort: an element below the first moves to the front (the range shifted up by
    // one); any other goes through UnguardedInsert.
    template <class RanIt, class Pred>
    void InsertionSort(RanIt First, RanIt Last, Pred Less)
    {
        typedef typename std::iterator_traits<RanIt>::value_type T;
        if (First == Last)
            return;
        for (RanIt It = First + 1; It != Last; ++It)
        {
            T const Value = *It;
            if (Less(Value, *First))
            {
                for (RanIt Move = It; Move != First; --Move)
                    *Move = *(Move - 1);
                *First = Value;
            }
            else
            {
                UnguardedInsert(It, Less);
            }
        }
    }

    // std::sort(First, Last, Less) as VC6 _Sort_0: up to 16 elements -> insertion sort; else
    // _Sort, then an insertion sort of the first 16 and unguarded inserts for the rest.
    template <class RanIt, class Pred>
    void Sort(RanIt First, RanIt Last, Pred Less)
    {
        if (Last - First <= 16)
        {
            InsertionSort(First, Last, Less);
            return;
        }
        SortRange(First, Last, Less);
        InsertionSort(First, First + 16, Less);
        for (RanIt It = First + 16; It != Last; ++It)
            UnguardedInsert(It, Less);
    }
}
