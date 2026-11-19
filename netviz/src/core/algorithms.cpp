#include "algorithms.h"
#include <cfloat>
#include "int_queue.h"
#include "min_heap.h"

// ---------------------------------------------------------------- BFS
KHopResult BfsKHop(const Graph& g, const Node* start, int k, Direction dir) {
    KHopResult r;
    int n = g.nodes.size;
    if (!start || n == 0) return r;

    int* hop = static_cast<int*>(std::malloc(sizeof(int) * n));
    for (int i = 0; i < n; ++i) hop[i] = -1;

    IntQueue q;
    IntQueueInit(q, n);                    // the queue buffer doubles as the BFS visit order
    hop[start->index] = 0;
    IntQueuePush(q, start->index);

    while (!IntQueueEmpty(q)) {
        int ui = IntQueuePop(q);
        const Node* u = g.nodes.data[ui];
        int h = hop[ui];
        if (k >= 0 && h >= k) continue;    // reached the hop limit: do not expand further
        if (dir != DIR_IN) {
            for (int i = 0; i < u->out.size; ++i) {
                int vi = u->out.data[i].to->index;
                if (hop[vi] < 0) { hop[vi] = h + 1; IntQueuePush(q, vi); }
            }
        }
        if (dir != DIR_OUT) {
            for (int i = 0; i < u->in.size; ++i) {
                int vi = u->in.data[i]->index;
                if (hop[vi] < 0) { hop[vi] = h + 1; IntQueuePush(q, vi); }
            }
        }
    }

    r.count = q.tail;
    r.nodes = static_cast<int*>(std::malloc(sizeof(int) * r.count));
    r.hops  = static_cast<int*>(std::malloc(sizeof(int) * r.count));
    for (int i = 0; i < r.count; ++i) {
        r.nodes[i] = q.data[i];
        r.hops[i]  = hop[q.data[i]];
        if (r.hops[i] > r.maxHop) r.maxHop = r.hops[i];
    }
    IntQueueFree(q);
    std::free(hop);
    return r;
}

void KHopFree(KHopResult& r) {
    std::free(r.nodes);
    std::free(r.hops);
    r = KHopResult();
}

// ---------------------------------------------------------------- Dijkstra
PathResult DijkstraShortestPath(const Graph& g, const Node* src, const Node* dst) {
    PathResult res;
    int n = g.nodes.size;
    if (!src || !dst || n == 0) return res;

    double* dist = static_cast<double*>(std::malloc(sizeof(double) * n));
    int*    prev = static_cast<int*>(std::malloc(sizeof(int) * n));
    char*   done = static_cast<char*>(std::calloc(n, 1));
    for (int i = 0; i < n; ++i) { dist[i] = DBL_MAX; prev[i] = -1; }

    MinHeap heap;
    dist[src->index] = 0.0;
    MinHeapPush(heap, 0.0, src->index);

    while (!MinHeapEmpty(heap)) {
        HeapItem top = MinHeapPop(heap);
        int ui = top.value;
        if (done[ui]) continue;            // stale heap entry (a shorter one was handled already)
        done[ui] = 1;
        res.settled++;
        if (ui == dst->index) break;       // target finalised: its distance can no longer improve

        const Node* u = g.nodes.data[ui];
        for (int i = 0; i < u->out.size; ++i) {
            const Edge& e = u->out.data[i];
            int vi = e.to->index;
            if (done[vi]) continue;
            double nd = dist[ui] + e.weight;
            if (nd < dist[vi]) {
                dist[vi] = nd;
                prev[vi] = ui;
                MinHeapPush(heap, nd, vi);
            }
        }
    }

    if (done[dst->index]) {
        res.found = true;
        res.totalWeight = dist[dst->index];
        int len = 0;
        for (int v = dst->index; v != -1; v = prev[v]) ++len;
        res.length = len;
        res.nodes = static_cast<int*>(std::malloc(sizeof(int) * len));
        int pos = len - 1;
        for (int v = dst->index; v != -1; v = prev[v]) res.nodes[pos--] = v;   // fill backwards
    }
    MinHeapFree(heap);
    std::free(dist);
    std::free(prev);
    std::free(done);
    return res;
}

void PathFree(PathResult& p) {
    std::free(p.nodes);
    p = PathResult();
}

// ---------------------------------------------------------------- components
ComponentInfo LabelComponents(Graph& g) {
    ComponentInfo info;
    int n = g.nodes.size;
    for (int i = 0; i < n; ++i) g.nodes.data[i]->component = -1;
    if (n == 0) return info;

    IntQueue q;
    IntQueueInit(q, n);
    for (int s = 0; s < n; ++s) {
        if (g.nodes.data[s]->component >= 0) continue;
        int id = info.count++;
        int size = 0;
        q.head = q.tail = 0;               // reuse the buffer for each component
        g.nodes.data[s]->component = id;
        IntQueuePush(q, s);
        while (!IntQueueEmpty(q)) {
            const Node* u = g.nodes.data[IntQueuePop(q)];
            ++size;
            for (int i = 0; i < u->out.size; ++i) {
                Node* v = u->out.data[i].to;
                if (v->component < 0) { v->component = id; IntQueuePush(q, v->index); }
            }
            for (int i = 0; i < u->in.size; ++i) {
                Node* v = u->in.data[i];
                if (v->component < 0) { v->component = id; IntQueuePush(q, v->index); }
            }
        }
        DynArrayPush(info.sizes, size);
        if (size > info.largest) info.largest = size;
    }
    IntQueueFree(q);
    return info;
}

void ComponentInfoFree(ComponentInfo& c) {
    DynArrayFree(c.sizes);
    c.count = c.largest = 0;
}
