// dyn_array.h - minimal growable array (our own replacement for std::vector).
// Only for trivially copyable T (ints, pointers, plain structs): it grows with realloc.
#pragma once
#include <cstdio>
#include <cstdlib>
#include <type_traits>

template <typename T>
struct DynArray {
    T*  data     = nullptr;
    int size     = 0;
    int capacity = 0;
};

template <typename T>
void DynArrayReserve(DynArray<T>& a, int minCapacity) {
    static_assert(std::is_trivially_copyable<T>::value, "DynArray needs trivially copyable T");
    if (minCapacity <= a.capacity) return;
    int newCap = a.capacity > 0 ? a.capacity : 4;
    while (newCap < minCapacity) newCap *= 2;
    T* p = static_cast<T*>(std::realloc(a.data, sizeof(T) * static_cast<size_t>(newCap)));
    if (!p) { std::fprintf(stderr, "fatal: out of memory\n"); std::abort(); }
    a.data = p;
    a.capacity = newCap;
}

template <typename T>
void DynArrayPush(DynArray<T>& a, const T& value) {
    DynArrayReserve(a, a.size + 1);
    a.data[a.size++] = value;
}

// Removes element i in O(1) by moving the last element into its slot (order not preserved).
template <typename T>
void DynArraySwapRemove(DynArray<T>& a, int i) {
    a.data[i] = a.data[a.size - 1];
    a.size--;
}

template <typename T>
void DynArrayClear(DynArray<T>& a) { a.size = 0; }

template <typename T>
void DynArrayFree(DynArray<T>& a) {
    std::free(a.data);
    a.data = nullptr;
    a.size = a.capacity = 0;
}
