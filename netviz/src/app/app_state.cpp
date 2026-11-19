#include "app_state.h"
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../core/timer.h"

// ---------------------------------------------------------------- status line
void AppSetStatus(AppState& s, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(s.status, sizeof(s.status), fmt, ap);
    va_end(ap);
    s.statusUntil = s.now + 6.0;
}

// ---------------------------------------------------------------- lifecycle
void AppInit(AppState& s) {
    GraphInit(s.graph);
    s.comps = ComponentInfo();
    s.tableStats = HashTableComputeStats(s.graph.table);
}

static void FreeSelectionBuffers(AppState& s) {
    KHopFree(s.khop);
    PathFree(s.path);
    std::free(s.hopOf);  s.hopOf = nullptr;
    std::free(s.onPath); s.onPath = nullptr;
}

void AppShutdown(AppState& s) {
    FreeSelectionBuffers(s);
    ComponentInfoFree(s.comps);
    LoadReportFree(s.report);
    GraphDestroy(s.graph);
}

// ---------------------------------------------------------------- dataset (re)loading
void AppResetView(AppState& s) {
    OrbitCamera c;
    c.target = V3(0, 0, 0);
    c.distance = 330.0f;
    if (s.graph.geo && s.graph.nodes.size > 0) {          // face the side of the globe with the data
        Vec3 sum = V3(0, 0, 0);
        for (int i = 0; i < s.graph.nodes.size; ++i) sum = V3Add(sum, s.graph.nodes.data[i]->scene);
        float len = V3Length(sum);
        if (len > 1e-3f * s.graph.nodes.size) {
            c.yaw = std::atan2(sum.x, sum.z);
            float p = std::asin(sum.y / len);
            c.pitch = p < -0.9f ? -0.9f : (p > 0.9f ? 0.9f : p);
        }
    }
    s.cam = c;
}

// Takes ownership of `fresh` and `rep` (shallow copies are intentional: the old ones are not freed).
static void InstallDataset(AppState& s, Graph& fresh, LoadReport& rep, const std::string& label) {
    AppClearSelection(s);
    GraphDestroy(s.graph);
    LoadReportFree(s.report);
    ComponentInfoFree(s.comps);
    s.graph = fresh;
    s.report = rep;
    s.sourceLabel = label;
    s.comps = LabelComponents(s.graph);
    s.tableStats = HashTableComputeStats(s.graph.table);

    int n = s.graph.nodes.size;
    s.nodeRadius = 3.0f * std::cbrt(100.0f / static_cast<float>(n > 100 ? n : 100));
    s.largeMode = n > kFullRenderMaxNodes || s.graph.arcCount > kFullRenderMaxArcs;
    s.labelsAll = n <= 60;
    s.m = Metrics();
    AppResetView(s);
}

bool AppLoadFile(AppState& s, const char* path) {
    Graph fresh; GraphInit(fresh);
    LoadReport rep;
    if (!LoadDatasetFile(path, fresh, rep)) {
        std::fprintf(stderr, "Load failed: %s\n", rep.error.c_str());
        for (int i = 0; i < rep.messages.size && i < 10; ++i)
            std::fprintf(stderr, "  line %d: %s\n", rep.messages.data[i].line, rep.messages.data[i].text);
        AppSetStatus(s, "Load failed: %s", rep.error.c_str());
        s.statusUntil = s.now + 8.0;
        LoadReportFree(rep);
        GraphDestroy(fresh);
        return false;
    }
    const char* base = path;                         // show only the file name
    for (const char* p = path; *p; ++p) if (*p == '/' || *p == '\\') base = p + 1;
    s.synthNodes = 0;
    std::string label = fresh.name.empty() ? std::string(base) : fresh.name + "  (" + base + ")";
    InstallDataset(s, fresh, rep, label);
    AppSetStatus(s, "Loaded %d nodes, %lld arcs from %s", s.graph.nodes.size, s.graph.arcCount, base);
    return true;
}

void AppLoadSynthetic(AppState& s, int nodes, unsigned long long seed) {
    Graph fresh; GraphInit(fresh);
    double t0 = NowNs();
    GenerateSyntheticGraph(fresh, nodes, seed, 6);
    LoadReport rep;
    rep.ok = true;
    rep.nodesLoaded = fresh.nodes.size;
    rep.edgesLoaded = static_cast<int>(fresh.arcCount / 2);
    rep.loadMs = (NowNs() - t0) / 1e6;
    s.synthNodes = nodes;
    s.synthSeed = seed;
    InstallDataset(s, fresh, rep, fresh.name);
    AppSetStatus(s, "Generated %d nodes (seed %llu) in %.0f ms", nodes, seed, rep.loadMs);
}

bool AppReload(AppState& s) {
    if (s.synthNodes > 0) { AppLoadSynthetic(s, s.synthNodes, s.synthSeed); return true; }
    if (s.report.path.empty()) return false;
    std::string p = s.report.path;                   // copy: AppLoadFile replaces s.report
    return AppLoadFile(s, p.c_str());
}

// ---------------------------------------------------------------- selection
static void MeasureLookup(AppState& s, const Node* n) {
    // One hash find takes ~tens of nanoseconds, below clock resolution, so time many repetitions.
    const std::string key = n->id;
    int probes = 0;
    const int reps = 2000;
    double t0 = NowNs();
    volatile size_t sink = 0;                        // keeps the compiler from dropping the loops
    for (int i = 0; i < reps; ++i) sink = sink + (HashTableFind(s.graph.table, key, &probes) != nullptr);
    double hashNs = (NowNs() - t0) / reps;

    int count = s.graph.nodes.size;
    int scanReps = 200000 / (count > 0 ? count : 1);
    if (scanReps < 2) scanReps = 2;
    if (scanReps > 2000) scanReps = 2000;
    t0 = NowNs();
    for (int r = 0; r < scanReps; ++r)
        for (int i = 0; i < count; ++i)
            if (s.graph.nodes.data[i]->id == key) { sink = sink + i; break; }
    double scanNs = (NowNs() - t0) / scanReps;

    s.m.hasLookup = true;
    s.m.lookupId = key;
    s.m.lookupNs = hashNs;
    s.m.lookupProbes = probes;
    s.m.scanNs = scanNs;
}

void AppRecompute(AppState& s) {
    FreeSelectionBuffers(s);
    s.m.bfsUs = s.m.dijkstraUs = 0.0;
    if (!s.a) return;
    int n = s.graph.nodes.size;

    double t0 = NowNs();
    s.khop = BfsKHop(s.graph, s.a, s.k, s.dir);
    s.m.bfsUs = (NowNs() - t0) / 1e3;
    s.hopOf = static_cast<int*>(std::malloc(sizeof(int) * n));
    for (int i = 0; i < n; ++i) s.hopOf[i] = -1;
    for (int i = 0; i < s.khop.count; ++i) s.hopOf[s.khop.nodes[i]] = s.khop.hops[i];

    if (s.b) {
        t0 = NowNs();
        s.path = DijkstraShortestPath(s.graph, s.a, s.b);
        s.m.dijkstraUs = (NowNs() - t0) / 1e3;
        s.onPath = static_cast<unsigned char*>(std::calloc(n, 1));
        for (int i = 0; i < s.path.length; ++i) s.onPath[s.path.nodes[i]] = 1;
    }
}

void AppSelectSource(AppState& s, Node* n) {
    s.a = n;
    s.b = nullptr;
    if (n) MeasureLookup(s, n);
    AppRecompute(s);
}

void AppSelectTarget(AppState& s, Node* n) {
    if (!s.a) { AppSelectSource(s, n); return; }
    if (!n || n == s.a) return;
    s.b = n;
    MeasureLookup(s, n);
    AppRecompute(s);
}

void AppClearSelection(AppState& s) {
    s.a = s.b = nullptr;
    FreeSelectionBuffers(s);
    s.m.bfsUs = s.m.dijkstraUs = 0.0;
}

void AppSetHops(AppState& s, int k) {
    if (k < 1) k = 1;
    if (k > kMaxHops) k = kMaxHops;
    if (k == s.k) return;
    s.k = k;
    AppRecompute(s);
}

void AppCycleDirection(AppState& s) {
    s.dir = s.dir == DIR_BOTH ? DIR_OUT : (s.dir == DIR_OUT ? DIR_IN : DIR_BOTH);
    AppRecompute(s);
}

void AppDeleteSelected(AppState& s) {
    if (!s.a) return;
    std::string id = s.a->id;                        // copy: the node is about to be destroyed
    AppClearSelection(s);
    double t0 = NowNs();
    GraphRemoveNode(s.graph, id);
    double us = (NowNs() - t0) / 1e3;
    ComponentInfoFree(s.comps);
    s.comps = LabelComponents(s.graph);
    s.tableStats = HashTableComputeStats(s.graph.table);
    AppSetStatus(s, "Deleted '%s' and its links in %.1f us (F5 reloads the dataset)", id.c_str(), us);
}

int AppDistinctNeighbors(const Node* n) {
    int distinct = n->out.size;
    for (int i = 0; i < n->in.size; ++i) {
        const Node* x = n->in.data[i];
        const Edge* back = NodeFindArc(x, n);        // arc x -> n
        if (back && back->undirectedLink) continue;  // its twin n -> x is already counted
        if (!NodeFindArc(n, x)) ++distinct;
    }
    return distinct;
}
