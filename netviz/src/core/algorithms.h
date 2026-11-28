// algorithms.h - BFS (k-hop), Dijkstra shortest path, connected-component labeling.
// All run on dense node indices (node->index), so no hashing happens inside the loops.
#pragma once
#include "dyn_array.h"
#include "graph.h"

enum Direction { DIR_OUT, DIR_IN, DIR_BOTH };    // which arcs a traversal may follow

// ---- Breadth-first search limited to k hops ---------------------------------------------
struct KHopResult {
    int* nodes   = nullptr;   // node indices in BFS order; nodes[0] is the start node
    int* hops    = nullptr;   // hops[i] = distance (in edges) from the start to nodes[i]
    int  count   = 0;
    int  maxHop  = 0;         // deepest level actually reached
};
// k < 0 means "no limit" (whole reachable region).
KHopResult BfsKHop(const Graph& g, const Node* start, int k, Direction dir);
void       KHopFree(KHopResult& r);

// ---- Dijkstra (minimum total latency, follows arc direction) -----------------------------
struct PathResult {
    bool   found       = false;
    int*   nodes       = nullptr;   // node indices from source to target
    int    length      = 0;         // number of nodes on the path (hops = length - 1)
    double totalWeight = 0.0;
    int    settled     = 0;         // nodes finalised by the search (work done)
};
PathResult DijkstraShortestPath(const Graph& g, const Node* src, const Node* dst);
void       PathFree(PathResult& p);

// ---- Weakly connected components (BFS over out+in arcs) ----------------------------------
struct ComponentInfo {
    int           count   = 0;
    int           largest = 0;     // size of the biggest component
    DynArray<int> sizes;           // sizes.data[componentId]
};
// Sets node->component for every node.
ComponentInfo LabelComponents(Graph& g);
void          ComponentInfoFree(ComponentInfo& c);
