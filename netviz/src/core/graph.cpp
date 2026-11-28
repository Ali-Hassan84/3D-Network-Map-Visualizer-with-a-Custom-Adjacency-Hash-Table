#include "graph.h"
#include <cmath>

void GraphInit(Graph& g, HashFn fn) {
    HashTableInit(g.table, 16, fn);
    g.nodes = DynArray<Node*>();
    g.arcCount = 0;
    g.center = V3(0, 0, 0);
    g.scale = 1.0f;
    g.geo = false;
    g.name.clear();
}

void GraphDestroy(Graph& g) {
    for (int i = 0; i < g.nodes.size; ++i) NodeDestroy(g.nodes.data[i]);
    DynArrayFree(g.nodes);
    HashTableDestroy(g.table);
    g.arcCount = 0;
}

void GraphClear(Graph& g) {
    HashFn fn = g.table.hashFn;
    GraphDestroy(g);
    GraphInit(g, fn);
}

Node* GraphAddNode(Graph& g, const std::string& id, Vec3 pos) {
    Node* n = NodeCreate(id, pos);
    if (!HashTableInsert(g.table, n)) {      // duplicate id
        NodeDestroy(n);
        return nullptr;
    }
    n->index = g.nodes.size;
    DynArrayPush(g.nodes, n);
    return n;
}

Node* GraphFindNode(const Graph& g, const std::string& id) {
    return HashTableFind(g.table, id);
}

const Edge* NodeFindArc(const Node* from, const Node* to) {
    for (int i = 0; i < from->out.size; ++i)
        if (from->out.data[i].to == to) return &from->out.data[i];
    return nullptr;
}

// Adds arc from->to. Returns true if new, false if it already existed (weight lowered if needed).
static bool AddArc(Node* from, Node* to, double weight, bool undirectedLink) {
    for (int i = 0; i < from->out.size; ++i) {
        Edge& e = from->out.data[i];
        if (e.to == to) {
            if (weight < e.weight) e.weight = weight;
            e.undirectedLink = e.undirectedLink || undirectedLink;
            return false;
        }
    }
    Edge e;
    e.to = to;
    e.weight = weight;
    e.undirectedLink = undirectedLink;
    DynArrayPush(from->out, e);
    DynArrayPush(to->in, from);
    return true;
}

EdgeStatus GraphAddEdgeNodes(Graph& g, Node* a, Node* b, double weight, bool undirected) {
    if (!a) return EDGE_MISSING_SOURCE;
    if (!b) return EDGE_MISSING_TARGET;
    if (a == b) return EDGE_SELF_LOOP;
    if (!(weight >= 0.0) || std::isinf(weight)) return EDGE_BAD_WEIGHT;   // also catches NaN

    bool added = AddArc(a, b, weight, undirected);
    if (added) g.arcCount++;
    if (undirected) {
        if (AddArc(b, a, weight, true)) { g.arcCount++; added = true; }
    }
    return added ? EDGE_ADDED : EDGE_MERGED;
}

EdgeStatus GraphAddEdge(Graph& g, const std::string& from, const std::string& to,
                        double weight, bool undirected) {
    return GraphAddEdgeNodes(g, GraphFindNode(g, from), GraphFindNode(g, to), weight, undirected);
}

// Removes arc from->to; returns true if it existed.
static bool RemoveArc(Graph& g, Node* from, Node* to) {
    for (int i = 0; i < from->out.size; ++i) {
        if (from->out.data[i].to == to) {
            DynArraySwapRemove(from->out, i);
            for (int j = 0; j < to->in.size; ++j) {
                if (to->in.data[j] == from) { DynArraySwapRemove(to->in, j); break; }
            }
            g.arcCount--;
            return true;
        }
    }
    return false;
}

bool GraphRemoveEdge(Graph& g, const std::string& from, const std::string& to) {
    Node* a = GraphFindNode(g, from);
    Node* b = GraphFindNode(g, to);
    if (!a || !b) return false;
    const Edge* arc = NodeFindArc(a, b);
    if (!arc) return false;
    bool pair = arc->undirectedLink;
    RemoveArc(g, a, b);
    if (pair) RemoveArc(g, b, a);                          // undirected link = both arcs go
    return true;
}

bool GraphRemoveNode(Graph& g, const std::string& id) {
    Node* n = GraphFindNode(g, id);
    if (!n) return false;

    while (n->out.size > 0) RemoveArc(g, n, n->out.data[n->out.size - 1].to);   // arcs leaving n
    while (n->in.size > 0)  RemoveArc(g, n->in.data[n->in.size - 1], n);        // arcs entering n

    HashTableRemove(g.table, id);
    int i = n->index;                                      // swap-with-last in the dense array
    Node* last = g.nodes.data[g.nodes.size - 1];
    g.nodes.data[i] = last;
    last->index = i;
    g.nodes.size--;
    NodeDestroy(n);
    return true;
}

void GraphFitScene(Graph& g) {
    if (g.nodes.size == 0) { g.center = V3(0, 0, 0); g.scale = 1.0f; return; }
    Vec3 lo = g.nodes.data[0]->pos, hi = lo;
    for (int i = 1; i < g.nodes.size; ++i) {
        Vec3 p = g.nodes.data[i]->pos;
        if (p.x < lo.x) lo.x = p.x;
        if (p.x > hi.x) hi.x = p.x;
        if (p.y < lo.y) lo.y = p.y;
        if (p.y > hi.y) hi.y = p.y;
        if (p.z < lo.z) lo.z = p.z;
        if (p.z > hi.z) hi.z = p.z;
    }
    g.center = V3((lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f);
    float radius = 0.0f;
    for (int i = 0; i < g.nodes.size; ++i) {
        float d = V3Dist(g.nodes.data[i]->pos, g.center);
        if (d > radius) radius = d;
    }
    g.scale = radius > 1e-9f ? 100.0f / radius : 1.0f;
    for (int i = 0; i < g.nodes.size; ++i) {
        Node* n = g.nodes.data[i];
        n->scene = V3Scale(V3Sub(n->pos, g.center), g.scale);
    }
}

void GraphUseGeoScene(Graph& g) {
    g.center = V3(0, 0, 0);
    g.scale = 1.0f;
    g.geo = true;
    for (int i = 0; i < g.nodes.size; ++i) g.nodes.data[i]->scene = g.nodes.data[i]->pos;
}
