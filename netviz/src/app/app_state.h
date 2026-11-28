// app_state.h - everything the visualizer remembers between frames, plus the selection logic.
// This header and app_state.cpp do NOT depend on raylib (input.cpp / renderer.cpp / ui.cpp do).
#pragma once
#include <string>
#include "../core/algorithms.h"
#include "../core/dataset_loader.h"
#include "../core/generator.h"
#include "../core/graph.h"
#include "../core/orbit_camera.h"
#include "../core/picking.h"

// Rendering budgets: above these limits the scene switches to "large mode" (point cloud for the
// whole graph + full detail only for the active neighbourhood).
const int kFullRenderMaxNodes = 3000;
const long long kFullRenderMaxArcs = 30000;
const int kMaxHighlightNodes = 3000;     // spheres drawn for the k-hop set
const int kMaxHighlightEdges = 20000;    // line segments drawn for the k-hop set
const int kMaxHops = 30;

struct SearchBox {
    bool  active = false;
    char  text[65] = {0};
    int   len = 0;
    int   slot = 0;                  // 0 = pick as source (A), 1 = pick as route target (B)
    Node* suggestions[6] = {nullptr};
    int   suggestionCount = 0;
    int   highlighted = 0;
};

struct Metrics {
    bool   hasLookup = false;
    std::string lookupId;
    double lookupNs = 0.0;           // hash find, averaged over many repetitions
    int    lookupProbes = 0;         // key comparisons for that find
    double scanNs = 0.0;             // linear array scan for the same key
    double bfsUs = 0.0;
    double dijkstraUs = 0.0;
    double pickUs = 0.0;             // last ray cast over all nodes
    bool   hasPick = false;
};

struct AppState {
    Graph          graph;
    LoadReport     report;
    std::string    sourceLabel;      // file name or "synthetic ..."
    int            synthNodes = 0;   // >0 when the current graph was generated (for F5 reload)
    unsigned long long synthSeed = 0;

    ComponentInfo  comps;
    HashTableStats tableStats;

    OrbitCamera    cam;
    float          nodeRadius = 3.0f;
    bool           largeMode = false;

    Node*          a = nullptr;      // selected node (source)
    Node*          b = nullptr;      // route target
    int            k = 1;            // hop radius
    Direction      dir = DIR_BOTH;
    KHopResult     khop;
    PathResult     path;
    int*           hopOf = nullptr;  // per node index: hops from A (-1 = outside the k-hop set)
    unsigned char* onPath = nullptr; // per node index: 1 if on the shortest path
    Metrics        m;

    bool colorByComponent = false;
    bool labelsAll = true;
    bool showGlobe = true;
    bool showHelp = false;
    bool showReport = false;
    SearchBox search;

    double now = 0.0;                // seconds, refreshed every frame by main.cpp
    char   status[160] = {0};
    double statusUntil = 0.0;        // `now` deadline for the status line

    // mouse gesture tracking (input.cpp)
    bool  leftDown = false, panDown = false;
    float dragDist = 0.0f;
};

void AppInit(AppState& s);
void AppShutdown(AppState& s);

bool AppLoadFile(AppState& s, const char* path);
void AppLoadSynthetic(AppState& s, int nodes, unsigned long long seed);
bool AppReload(AppState& s);
void AppResetView(AppState& s);

void AppSelectSource(AppState& s, Node* n);
void AppSelectTarget(AppState& s, Node* n);
void AppClearSelection(AppState& s);
void AppSetHops(AppState& s, int k);
void AppCycleDirection(AppState& s);
void AppDeleteSelected(AppState& s);
void AppRecompute(AppState& s);                  // re-run BFS (+ Dijkstra if B is set)

int  AppDistinctNeighbors(const Node* n);        // nodes adjacent through an out- or in-arc
void AppSetStatus(AppState& s, const char* fmt, ...);
