// min_heap.h - binary min-heap (priority queue) used by Dijkstra. Lazy deletion: when a shorter
// distance is found we push a new item; stale items are skipped when popped.
#pragma once
#include "dyn_array.h"

struct HeapItem { double key; int value; };
struct MinHeap  { DynArray<HeapItem> items; };

inline bool MinHeapEmpty(const MinHeap& h) { return h.items.size == 0; }
inline void MinHeapFree(MinHeap& h)        { DynArrayFree(h.items); }

inline void MinHeapPush(MinHeap& h, double key, int value) {
    HeapItem it; it.key = key; it.value = value;
    DynArrayPush(h.items, it);
    int i = h.items.size - 1;
    HeapItem* a = h.items.data;
    while (i > 0) {                                  // sift up
        int parent = (i - 1) / 2;
        if (a[parent].key <= a[i].key) break;
        HeapItem t = a[parent]; a[parent] = a[i]; a[i] = t;
        i = parent;
    }
}

inline HeapItem MinHeapPop(MinHeap& h) {             // precondition: not empty
    HeapItem* a = h.items.data;
    HeapItem top = a[0];
    a[0] = a[--h.items.size];
    int n = h.items.size, i = 0;
    while (true) {                                   // sift down
        int l = 2 * i + 1, r = l + 1, m = i;
        if (l < n && a[l].key < a[m].key) m = l;
        if (r < n && a[r].key < a[m].key) m = r;
        if (m == i) break;
        HeapItem t = a[m]; a[m] = a[i]; a[i] = t;
        i = m;
    }
    return top;
}
