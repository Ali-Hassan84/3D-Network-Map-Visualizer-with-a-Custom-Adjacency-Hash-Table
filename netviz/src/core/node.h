// node.h - the record stored for every network node.
#pragma once
#include <string>
#include "dyn_array.h"
#include "vec3.h"

struct Node;

// One directed arc. An undirected link is stored as TWO arcs (a->b and b->a) that both have
// undirectedLink = true, so renderers can draw it once, without an arrowhead.
struct Edge {
    Node*  to;
    double weight;          // latency (ms), always >= 0
    bool   undirectedLink;
};

struct Node {
    std::string     id;         // unique key, e.g. "Server_London"
    Vec3            pos;        // raw coordinates exactly as given by the dataset
    Vec3            scene;      // normalised coordinates used by renderer + picking (see GraphFitScene)
    int             index;      // position in Graph::nodes (dense, 0..n-1), kept up to date on removal
    int             component;  // weakly-connected component id, -1 until LabelComponents() ran
    DynArray<Edge>  out;        // outgoing arcs (the "adjacency list")
    DynArray<Node*> in;         // reverse adjacency: who points at me (O(degree) removal, in-degree)
};

Node* NodeCreate(const std::string& id, Vec3 pos);
void  NodeDestroy(Node* n);     // frees the node's arrays; does NOT touch other nodes
