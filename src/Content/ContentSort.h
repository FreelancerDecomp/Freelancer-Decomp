// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// The std::sort that retail Content.dll was built with (VC6 <algorithm>: _Sort_0 / _Sort /
// _Median / _Unguarded_partition / _Insertion_sort / _Unguarded_insert, _SORT_MAX 16).
// Every instantiation in the listing has this shape, e.g. TU48 FUN_06f55a00 (_Sort_0 with
// the first _Sort pass inlined) + FUN_06f55cd0 (_Sort) for the empathy rates.
//
// It is a port of retail behaviour, not a replacement for std::sort: the algorithm is not
// stable, so the final order of elements that compare equal is decided by these exact
// steps; a different sort gives the same key order but a different order of ties.
//
// Less is the retail predicate (a strict weak order): Less(A, B) is true when A goes first.

// _Median takes its three elements by value, as VC6's `_Median(_Ty _X, _Ty _Y, _Ty _Z, _Pr _P)`:
// the caller copy-constructs the arguments and the callee destroys them (retail TU58
// FZonePathLabel instance: argument copies in _Sort_0 06f7f370 states 1 / 2 and _Sort 06f7f990
// states 0 / 1, parameter dtors in _Median 06f7fea0 states 0-2). Returning a parameter moves it
// where retail copy-constructs the return slot; the returned value is the same.
template <typename TElement, typename TLess>
TElement ContentSortMedian(TElement A, TElement B, TElement C, TLess Less)
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

template <typename TElement, typename TLess>
TElement* ContentSortPartition(TElement* First, TElement* Last, TElement const& Pivot, TLess Less)
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
        TElement const Swap = *First;
        *First = *Last;
        *Last = Swap;
        ++First;
    }
}

// _Sort: partition while more than 16 elements remain; recurse into one side (the right
// side when it is not larger than the left), loop on the other.
template <typename TElement, typename TLess>
void ContentSortRange(TElement* First, TElement* Last, TLess Less)
{
    while (Last - First > 16)
    {
        TElement const Pivot =
            ContentSortMedian(*First, First[(Last - First) / 2], *(Last - 1), Less);
        TElement* Cut = ContentSortPartition(First, Last, Pivot, Less);
        if (Last - Cut <= Cut - First)
        {
            ContentSortRange(Cut, Last, Less);
            Last = Cut;
        }
        else
        {
            ContentSortRange(First, Cut, Less);
            First = Cut;
        }
    }
}

// _Unguarded_insert: move the element at Position down while it is less than its
// predecessor (a smaller element is known to sit in front).
template <typename TElement, typename TLess>
void ContentSortInsertUnguarded(TElement* Position, TLess Less)
{
    TElement const Value = *Position;
    TElement* Previous = Position - 1;
    while (Less(Value, *Previous))
    {
        *Position = *Previous;
        Position = Previous;
        --Previous;
    }
    *Position = Value;
}

// _Insertion_sort.
template <typename TElement, typename TLess>
void ContentSortInsertion(TElement* First, TElement* Last, TLess Less)
{
    if (First == Last)
        return;
    for (TElement* It = First + 1; It != Last; ++It)
    {
        TElement const Value = *It;
        if (Less(Value, *First))
        {
            for (TElement* Move = It; Move != First; --Move)
                *Move = *(Move - 1);
            *First = Value;
        }
        else
        {
            ContentSortInsertUnguarded(It, Less);
        }
    }
}

// std::sort(First, Last, Less) as VC6 _Sort_0: up to 16 elements -> insertion sort; else
// _Sort, then an insertion sort of the first 16 and unguarded inserts for the rest.
template <typename TElement, typename TLess>
void ContentSort(TElement* First, TElement* Last, TLess Less)
{
    if (Last - First <= 16)
    {
        ContentSortInsertion(First, Last, Less);
        return;
    }
    ContentSortRange(First, Last, Less);
    ContentSortInsertion(First, First + 16, Less);
    for (TElement* It = First + 16; It != Last; ++It)
        ContentSortInsertUnguarded(It, Less);
}
