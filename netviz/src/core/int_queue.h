// int_queue.h - fixed-capacity FIFO queue of ints (array based). BFS enqueues each node at most
// once, so capacity = node count is always enough and no wrap-around is needed.
#pragma once
#include <cstdlib>

struct IntQueue {
    int* data = nullptr;
    int  head = 0;      // next to dequeue
    int  tail = 0;      // next free slot
};

inline void IntQueueInit(IntQueue& q, int capacity) {
    q.data = static_cast<int*>(std::malloc(sizeof(int) * (capacity > 0 ? capacity : 1)));
    q.head = q.tail = 0;
}
inline void IntQueueFree(IntQueue& q)             { std::free(q.data); q.data = nullptr; }
inline bool IntQueueEmpty(const IntQueue& q)      { return q.head == q.tail; }
inline void IntQueuePush(IntQueue& q, int v)      { q.data[q.tail++] = v; }
inline int  IntQueuePop(IntQueue& q)              { return q.data[q.head++]; }
