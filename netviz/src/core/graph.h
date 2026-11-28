// graph.h - the network: a dense node array + the adjacency hash table (id -> Node*).
#pragma once
#include <string>
#include "dyn_array.h"
#include "hash_table.h"
#include "node.h"

struct Graph {
    HashTable       table;              // id -> Node*  (O(1) average lookup)
    DynArray<Node*> nodes;              // dense array, node->index is the position here
    long long       arcCount = 0;       // directed arcs; an undirected link counts as 2
    Vec3            center;             // raw -> scene transform: scene = (raw - center) * scale
    float           scale    = 1.0f;
    bool            geo      = false;   // coordinates came from lat/lon (renderer draws a globe)
    std::string     name;
};

enum EdgeStatus {
    EDGE_ADDED,
    EDGE_MERGED,            // arc already existed: kept the smaller latency
    EDGE_SELF_LOOP,
    EDGE_MISSING_SOURCE,
    EDGE_MISSING_TARGET,
    EDGE_BAD_WEIGHT         // negative, NaN or infinite
};

void  GraphInit(Graph& g, HashFn fn = HashFnv1a);
void  GraphDestroy(Graph& g);                           // frees all nodes, the table, the arrays
void  GraphClear(Graph& g);                             // destroy + init again (same hash fn)

Node* GraphAddNode(Graph& g, const std::string& id, Vec3 pos);   // nullptr if id already exists
Node* GraphFindNode(const Graph& g, const std::string& id);

EdgeStatus GraphAddEdgeNodes(Graph& g, Node* a, Node* b, double weight, bool undirected);
EdgeStatus GraphAddEdge(Graph& g, const std::string& from, const std::string& to,
                        double weight, bool undirected);

const Edge* NodeFindArc(const Node* from, const Node* to);       // nullptr if no arc from->to
bool GraphRemoveEdge(Graph& g, const std::string& from, const std::string& to);
bool GraphRemoveNode(Graph& g, const std::string& id);           // also removes every arc touching it

// Computes center/scale from the bounding box so the data fits a sphere of radius 100, then fills
// node->scene for every node. Call once after loading.
void GraphFitScene(Graph& g);
// Geographic datasets: globe of radius 100 centred on the origin (scale 1).
void GraphUseGeoScene(Graph& g);
