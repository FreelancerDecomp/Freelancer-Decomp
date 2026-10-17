// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include <functional>
#include <utility>
#include <vector>

// Retail Common.dll `struct CheapSet<T, Pred>` -- a "set" kept as an unsorted vector. It appears
// in two exports only:
//   ?get_intruder_set_r@CObject@@QAEXAAU?$CheapSet@PAUCObject@@U?$less@PAUCObject@@@std@@@@@Z
//   ?get_intruding_cobjs@PhyCollisionStateManager@PhySys@@QAEH_NAAU?$CheapSet@PAUCObject@@U?$less@PAUCObject@@@std@@@@@Z
// Retail layout is a VC6 vector (allocator byte +0x00, begin +0x04, end +0x08, capacity +0x0c);
// every member is inlined at the call sites:
//   - find (FUN_0627f260, thiscall RET 4): linear walk from begin to end comparing with
//     operator== (not Pred); returns end when absent;
//   - insert: find; absent -> vector insert at end (the IObjInspect* vector insert ICF fold),
//     then find again; the result is built by FUN_062bed70 (pair<iterator, bool> ctor) with
//     true for a new element and false for one already present.
// Pred is never used by those bodies; it only takes part in the mangled name.
template <class T, class Pred = std::less<T>>
struct CheapSet
{
    typedef typename std::vector<T>::iterator Iterator;
    typedef typename std::vector<T>::const_iterator ConstIterator;

    // FUN_0627f260.
    Iterator Find(T const& Value)
    {
        Iterator It = Items.begin();
        for (; It != Items.end(); ++It)
        {
            if (*It == Value)
                return It;
        }
        return Items.end();
    }

    ConstIterator Find(T const& Value) const
    {
        ConstIterator It = Items.begin();
        for (; It != Items.end(); ++It)
        {
            if (*It == Value)
                return It;
        }
        return Items.end();
    }

    // find; absent -> insert at end and find again (retail re-runs FUN_0627f260); FUN_062bed70.
    std::pair<Iterator, bool> Insert(T const& Value)
    {
        Iterator It = Find(Value);
        if (It != Items.end())
            return std::pair<Iterator, bool>(It, false);
        Items.insert(Items.end(), Value);
        return std::pair<Iterator, bool>(Find(Value), true);
    }

    Iterator Begin() { return Items.begin(); }
    Iterator End() { return Items.end(); }
    ConstIterator Begin() const { return Items.begin(); }
    ConstIterator End() const { return Items.end(); }

    // Retail computes (end - begin) >> 2, or 0 while begin is null.
    int Num() const { return static_cast<int>(Items.size()); }

    std::vector<T> Items;
};
