// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// VC6 <algorithm> std::sort as MSVC 6 instantiated it inside freelancer.exe: median-of-three
// quicksort while a range holds more than 16 elements (_SORT_MAX), then an insertion sort of
// the first 16 and an unguarded insert of every later element. The current CRT's std::sort
// orders equal elements differently, so a port whose retail call sorts with a comparator that
// has ties uses this to keep retail's order.
//
// The freelancer.exe instantiation on FServerInfo* (GetFilteredServers, std::sort inlined at
// 0x005acb92..0x005acca6):
//   FUN_005afea0  _Sort                 (the median and the partition inlined)
//   FUN_005affc0  _Insertion_sort
//   FUN_005b0060  _Median
//   FUN_005b00d0  _Unguarded_partition

namespace Vc6Sort
{
    // FUN_005b0060 (cdecl): the median of three by Less.
    template <class T, class Pred>
    T Median(T A, T B, T C, Pred Less)
    {
        if (Less(A, B))
            return Less(B, C) ? B : (Less(A, C) ? C : A);
        return Less(A, C) ? A : (Less(B, C) ? C : B);
    }

    // FUN_005b00d0 (cdecl): advance First past elements below Pivot, step Last back past
    // elements above it; stop when they meet (Last <= First, unsigned), else swap and go on.
    template <class T, class Pred>
    T* UnguardedPartition(T* First, T* Last, T Pivot, Pred Less)
    {
        for (;; ++First)
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
        }
    }

    // _Unguarded_insert (inlined into FUN_005affc0 and std::sort): shift the elements above
    // Value up by one until one is not above it, then store Value.
    template <class T, class Pred>
    void UnguardedInsert(T* Last, T Value, Pred Less)
    {
        T* Prev = Last - 1;
        while (Less(Value, *Prev))
        {
            *Last = *Prev;
            Last = Prev;
            --Prev;
        }
        *Last = Value;
    }

    // FUN_005affc0 (cdecl): an element below the first moves to the front (the range shifted
    // up by one); any other goes through UnguardedInsert.
    template <class T, class Pred>
    void InsertionSort(T* First, T* Last, Pred Less)
    {
        if (First == Last)
            return;
        for (T* Current = First + 1; Current != Last; ++Current)
        {
            T const Value = *Current;
            if (Less(Value, *First))
            {
                for (T* Move = Current; Move != First; --Move)
                    *Move = *(Move - 1);
                *First = Value;
            }
            else
            {
                UnguardedInsert(Current, Value, Less);
            }
        }
    }

    // FUN_005afea0 (cdecl): while more than 16 elements remain, partition around the median of
    // the first, middle ((Last - First) / 2, signed) and last element; recurse into the shorter
    // side when the right one is not longer (L - M <= M - F), else into the left one.
    template <class T, class Pred>
    void SortRange(T* First, T* Last, Pred Less)
    {
        int Count = static_cast<int>(Last - First);
        while (Count > 16)
        {
            T* const Mid = UnguardedPartition(
                First, Last, Median(*First, First[Count / 2], *(Last - 1), Less), Less);
            if ((Last - Mid) <= (Mid - First))
            {
                SortRange(Mid, Last, Less);
                Last = Mid;
            }
            else
            {
                SortRange(First, Mid, Less);
                First = Mid;
            }
            Count = static_cast<int>(Last - First);
        }
    }

    // std::sort: 16 or fewer -> InsertionSort; otherwise SortRange, InsertionSort of the first
    // 16, and UnguardedInsert of every element after them.
    template <class T, class Pred>
    void Sort(T* First, T* Last, Pred Less)
    {
        if (Last - First <= 16)
        {
            InsertionSort(First, Last, Less);
            return;
        }
        SortRange(First, Last, Less);
        InsertionSort(First, First + 16, Less);
        for (T* Current = First + 16; Current != Last; ++Current)
            UnguardedInsert(Current, *Current, Less);
    }
}
