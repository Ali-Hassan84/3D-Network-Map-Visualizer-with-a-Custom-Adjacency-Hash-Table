// tests.cpp - self-contained test runner (no framework). Build + run:  make test
// Covers: hash table, graph edits, BFS / Dijkstra / components against hand-computed answers,
// dataset parser edge cases, file round-trip, picking maths, generator determinism and a
// 100,000-operation randomised comparison against std::unordered_map.
// (std::unordered_map / std::string are used here ONLY as a reference to validate against.)
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include "../core/algorithms.h"
#include "../core/dataset_loader.h"
#include "../core/generator.h"
#include "../core/graph.h"
#include "../core/hash_table.h"
#include "../core/picking.h"
#include "../core/rng.h"

static int g_checks = 0, g_failed = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failed; \
    std::printf("  FAIL line %d: %s\n", __LINE__, #cond); } } while (0)
#define SECTION(name) std::printf("[test] %s\n", name)

static bool Near(double a, double b, double eps = 1e-6) { return std::fabs(a - b) < eps; }

// ------------------------------------------------------------------ hash table
static void TestHashBasics() {
    SECTION("hash table: FNV-1a vectors, insert/find/remove, duplicates");
    CHECK(HashFnv1a("") == 0xcbf29ce484222325ULL);
    CHECK(HashFnv1a("a") == 0xaf63dc4c8601ec8cULL);
    CHECK(HashFnv1a("foobar") == 0x85944171f73967e8ULL);

    HashTable t; HashTableInit(t, 8);
    Node* a = NodeCreate("Server_London", V3(0, 0, 0));
    Node* b = NodeCreate("Server_Sydney", V3(1, 1, 1));
    Node* dup = NodeCreate("Server_London", V3(9, 9, 9));
    CHECK(HashTableInsert(t, a));
    CHECK(HashTableInsert(t, b));
    CHECK(!HashTableInsert(t, dup));                         // duplicate key rejected
    CHECK(t.count == 2);
    CHECK(HashTableFind(t, "Server_London") == a);
    CHECK(HashTableFind(t, "Server_Sydney") == b);
    CHECK(HashTableFind(t, "Server_Paris") == nullptr);      // missing id
    CHECK(HashTableFind(t, "") == nullptr);
    CHECK(HashTableRemove(t, "Server_London"));
    CHECK(!HashTableRemove(t, "Server_London"));             // already gone
    CHECK(HashTableFind(t, "Server_London") == nullptr);
    CHECK(HashTableFind(t, "Server_Sydney") == b);
    CHECK(t.count == 1);
    HashTableDestroy(t);
    NodeDestroy(a); NodeDestroy(b); NodeDestroy(dup);
}

static void TestHashResize() {
    SECTION("hash table: doubles + rehashes when load factor > 0.75");
    HashTable t; HashTableInit(t, 8);
    DynArray<Node*> nodes;
    size_t lastCap = t.capacity;
    int doublings = 0;
    for (int i = 0; i < 1000; ++i) {
        Node* n = NodeCreate("key_" + std::to_string(i), V3(0, 0, 0));
        DynArrayPush(nodes, n);
        HashTableInsert(t, n);
        CHECK(HashTableLoadFactor(t) <= 0.75 + 1e-12);        // invariant after EVERY insert
        if (t.capacity != lastCap) { CHECK(t.capacity == lastCap * 2); ++doublings; lastCap = t.capacity; }
    }
    CHECK(doublings >= 7 && t.resizeCount == static_cast<size_t>(doublings));
    bool all = true;
    for (int i = 0; i < 1000; ++i) all = all && HashTableFind(t, "key_" + std::to_string(i)) == nodes.data[i];
    CHECK(all);                                               // nothing lost during rehash
    HashTableStats s = HashTableComputeStats(t);
    CHECK(s.count == 1000 && s.usedBuckets + s.emptyBuckets == s.capacity);
    CHECK(s.maxChain >= 1 && s.avgProbesHit >= 1.0);
    HashTableDestroy(t);
    for (int i = 0; i < nodes.size; ++i) NodeDestroy(nodes.data[i]);
    DynArrayFree(nodes);
}

static void TestHashWeak() {
    SECTION("hash table: deliberately weak hashes stay correct");
    HashFn fns[3] = { HashWeakConstant, HashWeakFirstChar, HashWeakLength };
    for (int f = 0; f < 3; ++f) {
        HashTable t; HashTableInit(t, 8, fns[f]);
        DynArray<Node*> nodes;
        for (int i = 0; i < 400; ++i) {
            Node* n = NodeCreate("srv" + std::to_string(i), V3(0, 0, 0));
            DynArrayPush(nodes, n);
            CHECK(HashTableInsert(t, n));
        }
        bool all = true;
        for (int i = 0; i < 400; ++i) all = all && HashTableFind(t, "srv" + std::to_string(i)) == nodes.data[i];
        CHECK(all);
        HashTableStats s = HashTableComputeStats(t);
        if (f == 0) CHECK(s.maxChain == 400);                 // constant hash: ONE chain holds everything
        CHECK(s.resizeCount > 0);                             // load-factor trigger still fires
        for (int i = 0; i < 400; i += 2) CHECK(HashTableRemove(t, "srv" + std::to_string(i)));
        CHECK(t.count == 200 && HashTableFind(t, "srv1") == nodes.data[1] && !HashTableFind(t, "srv0"));
        HashTableDestroy(t);
        for (int i = 0; i < nodes.size; ++i) NodeDestroy(nodes.data[i]);
        DynArrayFree(nodes);
    }
}

static void TestHashDifferential() {
    SECTION("hash table: 100,000 random insert/find/delete ops vs std::unordered_map");
    HashTable t; HashTableInit(t, 8);
    std::unordered_map<std::string, Node*> ref;
    Rng rng; RngSeed(rng, 2024);
    bool agree = true;
    for (int op = 0; op < 100000; ++op) {
        std::string key = "k" + std::to_string(RngInt(rng, 5000));   // small key space => many repeats
        uint32_t kind = RngInt(rng, 3);
        if (kind == 0) {
            auto it = ref.find(key);
            Node* n = NodeCreate(key, V3(0, 0, 0));
            bool ins = HashTableInsert(t, n);
            if (it == ref.end()) { agree = agree && ins; ref[key] = n; }
            else { agree = agree && !ins; NodeDestroy(n); }
        } else if (kind == 1) {
            Node* got = HashTableFind(t, key);
            auto it = ref.find(key);
            agree = agree && (got == (it == ref.end() ? nullptr : it->second));
        } else {
            auto it = ref.find(key);
            Node* victim = it == ref.end() ? nullptr : it->second;
            bool removed = HashTableRemove(t, key);
            agree = agree && (removed == (victim != nullptr));
            if (victim) { ref.erase(it); NodeDestroy(victim); }
        }
        if (t.count != ref.size()) { agree = false; }
        if (!agree) { std::printf("  diverged at op %d\n", op); break; }
    }
    CHECK(agree);
    for (auto& kv : ref) { CHECK(HashTableFind(t, kv.first) == kv.second); NodeDestroy(kv.second); }
    HashTableDestroy(t);
}

// ------------------------------------------------------------------ graph + algorithms
// Hand-made ground truth graph (undirected unless noted):
//   A-B 1   A-C 4   B-C 2   B-D 5   C-D 1   D-E 3     F isolated     G-H 7     X->Y 2 (directed)
static void BuildGroundTruth(Graph& g) {
    const char* ids[] = { "A", "B", "C", "D", "E", "F", "G", "H", "X", "Y" };
    for (int i = 0; i < 10; ++i) GraphAddNode(g, ids[i], V3(static_cast<float>(i), 0, 0));
    GraphAddEdge(g, "A", "B", 1, true);  GraphAddEdge(g, "A", "C", 4, true);
    GraphAddEdge(g, "B", "C", 2, true);  GraphAddEdge(g, "B", "D", 5, true);
    GraphAddEdge(g, "C", "D", 1, true);  GraphAddEdge(g, "D", "E", 3, true);
    GraphAddEdge(g, "G", "H", 7, true);  GraphAddEdge(g, "X", "Y", 2, false);
}

static std::string PathString(const Graph& g, const PathResult& p) {
    std::string s;
    for (int i = 0; i < p.length; ++i) s += g.nodes.data[p.nodes[i]]->id;
    return s;
}

static void TestGraphEdits() {
    SECTION("graph: edge rules, arc accounting, node removal");
    Graph g; GraphInit(g);
    CHECK(GraphAddNode(g, "A", V3(0, 0, 0)) != nullptr);
    CHECK(GraphAddNode(g, "A", V3(1, 1, 1)) == nullptr);              // duplicate node
    GraphAddNode(g, "B", V3(1, 0, 0)); GraphAddNode(g, "C", V3(2, 0, 0));
    CHECK(GraphAddEdge(g, "A", "A", 1, true) == EDGE_SELF_LOOP);
    CHECK(GraphAddEdge(g, "A", "Z", 1, true) == EDGE_MISSING_TARGET);
    CHECK(GraphAddEdge(g, "Z", "A", 1, true) == EDGE_MISSING_SOURCE);
    CHECK(GraphAddEdge(g, "A", "B", -1, true) == EDGE_BAD_WEIGHT);
    CHECK(GraphAddEdge(g, "A", "B", NAN, true) == EDGE_BAD_WEIGHT);
    CHECK(g.arcCount == 0);
    CHECK(GraphAddEdge(g, "A", "B", 5, true) == EDGE_ADDED);
    CHECK(g.arcCount == 2);                                           // undirected = 2 arcs
    CHECK(GraphAddEdge(g, "A", "B", 3, true) == EDGE_MERGED);         // redundant edge
    CHECK(Near(NodeFindArc(GraphFindNode(g, "A"), GraphFindNode(g, "B"))->weight, 3));   // smaller kept
    CHECK(Near(NodeFindArc(GraphFindNode(g, "B"), GraphFindNode(g, "A"))->weight, 3));
    CHECK(GraphAddEdge(g, "B", "C", 2, false) == EDGE_ADDED);         // directed
    CHECK(GraphFindNode(g, "C")->out.size == 0 && GraphFindNode(g, "C")->in.size == 1);
    CHECK(g.arcCount == 3);
    CHECK(GraphRemoveNode(g, "B"));
    CHECK(g.arcCount == 0 && g.nodes.size == 2 && GraphFindNode(g, "B") == nullptr);
    CHECK(GraphFindNode(g, "A")->out.size == 0 && GraphFindNode(g, "A")->in.size == 0);
    CHECK(GraphFindNode(g, "C")->in.size == 0);
    CHECK(GraphFindNode(g, "A")->index >= 0 && GraphFindNode(g, "C")->index >= 0);
    CHECK(g.nodes.data[GraphFindNode(g, "C")->index] == GraphFindNode(g, "C"));   // dense index ok
    CHECK(!GraphRemoveNode(g, "B"));
    GraphAddEdge(g, "A", "C", 1, true);
    CHECK(GraphRemoveEdge(g, "A", "C") && g.arcCount == 0);           // removes both arcs
    GraphDestroy(g);
}

static void TestAlgorithms() {
    SECTION("BFS / Dijkstra / components vs hand-computed answers");
    Graph g; GraphInit(g);
    BuildGroundTruth(g);
    Node* A = GraphFindNode(g, "A");

    KHopResult k1 = BfsKHop(g, A, 1, DIR_BOTH);
    CHECK(k1.count == 3 && k1.maxHop == 1);                           // A, B, C
    KHopResult k2 = BfsKHop(g, A, 2, DIR_BOTH);
    CHECK(k2.count == 4);                                             // + D
    KHopResult k3 = BfsKHop(g, A, 3, DIR_BOTH);
    CHECK(k3.count == 5 && k3.maxHop == 3);                           // + E
    KHopResult kAll = BfsKHop(g, A, -1, DIR_BOTH);
    CHECK(kAll.count == 5);                                           // F, G, H, X, Y unreachable
    CHECK(k3.nodes[0] == A->index && k3.hops[0] == 0);
    for (int i = 0; i < k3.count; ++i) {
        const std::string& id = g.nodes.data[k3.nodes[i]]->id;
        int want = id == "A" ? 0 : (id == "B" || id == "C") ? 1 : id == "D" ? 2 : 3;
        CHECK(k3.hops[i] == want);
    }
    KHopFree(k1); KHopFree(k2); KHopFree(k3); KHopFree(kAll);

    Node* Y = GraphFindNode(g, "Y"); Node* X = GraphFindNode(g, "X");
    KHopResult o = BfsKHop(g, Y, 3, DIR_OUT);  CHECK(o.count == 1);   KHopFree(o);
    KHopResult in = BfsKHop(g, Y, 3, DIR_IN);  CHECK(in.count == 2);  KHopFree(in);
    KHopResult both = BfsKHop(g, X, 1, DIR_BOTH); CHECK(both.count == 2); KHopFree(both);

    Node* E = GraphFindNode(g, "E"); Node* F = GraphFindNode(g, "F");
    PathResult p = DijkstraShortestPath(g, A, E);
    CHECK(p.found && Near(p.totalWeight, 7) && PathString(g, p) == "ABCDE");   // 1+2+1+3, not A-C-D-E=8
    CHECK(p.length == 5);
    PathFree(p);
    p = DijkstraShortestPath(g, E, A);
    CHECK(p.found && Near(p.totalWeight, 7) && PathString(g, p) == "EDCBA");   // symmetric
    PathFree(p);
    p = DijkstraShortestPath(g, A, F);  CHECK(!p.found && p.nodes == nullptr); PathFree(p);   // isolated
    p = DijkstraShortestPath(g, A, GraphFindNode(g, "G")); CHECK(!p.found); PathFree(p);     // other component
    p = DijkstraShortestPath(g, A, A);  CHECK(p.found && p.length == 1 && Near(p.totalWeight, 0)); PathFree(p);
    p = DijkstraShortestPath(g, X, Y);  CHECK(p.found && Near(p.totalWeight, 2)); PathFree(p);
    p = DijkstraShortestPath(g, Y, X);  CHECK(!p.found); PathFree(p);                        // direction respected

    ComponentInfo c = LabelComponents(g);
    CHECK(c.count == 4 && c.largest == 5);                            // {A..E} {F} {G,H} {X,Y}
    CHECK(GraphFindNode(g, "A")->component == GraphFindNode(g, "E")->component);
    CHECK(GraphFindNode(g, "A")->component != GraphFindNode(g, "F")->component);
    CHECK(GraphFindNode(g, "X")->component == GraphFindNode(g, "Y")->component);   // weak connectivity
    ComponentInfoFree(c);

    Graph empty; GraphInit(empty);
    ComponentInfo ce = LabelComponents(empty); CHECK(ce.count == 0); ComponentInfoFree(ce);
    GraphDestroy(empty);
    GraphDestroy(g);
}

static void TestDijkstraRandomAgainstBellmanFord() {
    SECTION("Dijkstra vs brute-force Bellman-Ford on random graphs");
    Rng rng; RngSeed(rng, 7);
    bool ok = true;
    for (int round = 0; round < 25 && ok; ++round) {
        Graph g; GraphInit(g);
        int n = 30;
        for (int i = 0; i < n; ++i) GraphAddNode(g, "n" + std::to_string(i), V3(0, 0, 0));
        for (int i = 0; i < 70; ++i)
            GraphAddEdgeNodes(g, g.nodes.data[RngInt(rng, n)], g.nodes.data[RngInt(rng, n)],
                              1 + RngInt(rng, 20), RngInt(rng, 2) == 0);
        double dist[30];
        for (int i = 0; i < n; ++i) dist[i] = 1e18;
        dist[0] = 0;
        for (int it = 0; it < n; ++it)
            for (int u = 0; u < n; ++u)
                for (int e = 0; e < g.nodes.data[u]->out.size; ++e) {
                    const Edge& ed = g.nodes.data[u]->out.data[e];
                    if (dist[u] + ed.weight < dist[ed.to->index]) dist[ed.to->index] = dist[u] + ed.weight;
                }
        for (int v = 0; v < n; ++v) {
            PathResult p = DijkstraShortestPath(g, g.nodes.data[0], g.nodes.data[v]);
            if (dist[v] > 1e17) ok = ok && !p.found;
            else ok = ok && p.found && Near(p.totalWeight, dist[v]);
            PathFree(p);
        }
        GraphDestroy(g);
    }
    CHECK(ok);
}

// ------------------------------------------------------------------ parser
static bool Parse(const char* text, Graph& g, LoadReport& r) { return ParseDatasetText(text, std::strlen(text), g, r); }

static void TestParserHappyPath() {
    SECTION("parser: valid file, comments, CRLF, BOM, lowercase, order independence");
    const char* text =
        "\xEF\xBB\xBF# comment line\r\n"
        "NAME  My Test Net\r\n"
        "edge A B 5            # edge BEFORE its nodes are declared\r\n"
        "node A 0 0 0\r\n"
        "NODE B 10 0 0\t\r\n"
        "\r\n"
        "NODE C 0 10 0\n"
        "EDGE B C 2.5 DIRECTED\n"
        "DEFAULT_LINK DIRECTED\n"
        "EDGE C A 1\n";
    Graph g; GraphInit(g); LoadReport r;
    CHECK(Parse(text, g, r));
    CHECK(r.ok && r.nodesLoaded == 3 && r.edgesLoaded == 3 && LoadReportWarningCount(r) == 0);
    CHECK(g.name == "My Test Net");
    CHECK(g.arcCount == 3);                                           // DEFAULT_LINK is file-global: A->B, B->C, C->A
    const Edge* bc = NodeFindArc(GraphFindNode(g, "B"), GraphFindNode(g, "C"));
    CHECK(bc && Near(bc->weight, 2.5) && !bc->undirectedLink);
    CHECK(NodeFindArc(GraphFindNode(g, "C"), GraphFindNode(g, "B")) == nullptr);
    CHECK(NodeFindArc(GraphFindNode(g, "C"), GraphFindNode(g, "A")) != nullptr);   // directed by default-link
    CHECK(NodeFindArc(GraphFindNode(g, "A"), GraphFindNode(g, "B")) != nullptr);   // edge listed before its nodes
    CHECK(NodeFindArc(GraphFindNode(g, "B"), GraphFindNode(g, "A")) == nullptr);   // directive below still applies
    CHECK(Near(GraphFindNode(g, "B")->pos.x, 10));
    LoadReportFree(r); GraphDestroy(g);
}

static void TestParserEdgeCases() {
    SECTION("parser: duplicates, missing ids, self-loops, bad numbers, malformed lines");
    const char* text =
        "NODE A 0 0 0\n"
        "NODE B 1 0 0\n"
        "NODE C 2 0 0\n"
        "NODE A 9 9 9\n"              // duplicate node
        "NODE D 1 2\n"                // wrong field count
        "NODE E x y z\n"              // not numbers
        "NODE F 1 2 nan\n"            // not finite
        "NODE G\n"                    // auto placed
        "NODE H\n"                    // auto placed
        "EDGE A B 5\n"
        "EDGE A B 3\n"                // redundant, smaller kept
        "EDGE A Z 1\n"                // missing target
        "EDGE Q B 1\n"                // missing source
        "EDGE B B 1\n"                // self loop
        "EDGE A C -4\n"               // negative
        "EDGE A C abc\n"              // not a number
        "EDGE A C\n"                  // too few fields
        "EDGE A C 1 SIDEWAYS\n"       // bad direction word
        "FROBNICATE 1 2 3\n"          // unknown keyword
        "EDGE B C 2 DIRECTED\n";
    Graph g; GraphInit(g); LoadReport r;
    CHECK(Parse(text, g, r));
    CHECK(r.nodesLoaded == 5);                                        // A B C G H
    CHECK(r.duplicateNodes == 1);
    CHECK(r.malformedLines == 3 + 1 + 1 + 1 + 1);                     // D,E,F + 2 bad EDGE + direction + keyword... see below
    CHECK(r.missingEndpoints == 2 && r.selfLoops == 1 && r.badWeights == 1 && r.redundantEdges == 1);
    CHECK(r.edgesLoaded == 2);                                        // A-B and B->C
    CHECK(r.autoPlaced == 2);
    CHECK(Near(NodeFindArc(GraphFindNode(g, "A"), GraphFindNode(g, "B"))->weight, 3));
    CHECK(Near(GraphFindNode(g, "A")->pos.x, 0));                     // duplicate did not overwrite A
    Node* G = GraphFindNode(g, "G"); Node* H = GraphFindNode(g, "H");
    CHECK(V3Length(G->pos) > 1.0f && V3Dist(G->pos, H->pos) > 1e-3f); // got distinct coordinates
    CHECK(r.messages.size > 0 && r.messages.size <= kMaxStoredMessages);
    LoadReportFree(r); GraphDestroy(g);
}

static void TestParserDegenerate() {
    SECTION("parser: empty / garbage / isolated / disconnected / GEO");
    Graph g; GraphInit(g); LoadReport r;
    CHECK(!Parse("", g, r) && !r.ok);
    CHECK(!Parse("# only comments\n\n   \n", g, r));
    CHECK(!Parse("hello world\nthis is not a dataset\n", g, r));
    CHECK(g.nodes.size == 0);
    GraphDestroy(g);

    GraphInit(g);                                                     // isolated + disconnected nodes
    CHECK(Parse("NODE A 0 0 0\nNODE B 1 1 1\nNODE C 2 2 2\n", g, r) && g.arcCount == 0);
    ComponentInfo c = LabelComponents(g);
    CHECK(c.count == 3 && c.largest == 1);
    ComponentInfoFree(c); LoadReportFree(r); GraphDestroy(g);

    GraphInit(g);                                                     // GEO mode
    CHECK(Parse("COORDS GEO\nNODE Origin 0 0\nNODE North 90 0\nNODE East 0 90\nNODE Bad 95 0\nNODE Bad2 0 200\n", g, r));
    CHECK(g.geo && g.nodes.size == 3 && r.malformedLines == 2);
    Node* o = GraphFindNode(g, "Origin"); Node* nth = GraphFindNode(g, "North"); Node* e = GraphFindNode(g, "East");
    CHECK(Near(o->pos.z, 100, 1e-3) && Near(o->pos.x, 0, 1e-3) && Near(o->pos.y, 0, 1e-3));
    CHECK(Near(nth->pos.y, 100, 1e-3));
    CHECK(Near(e->pos.x, 100, 1e-3));                                 // east is +X
    LoadReportFree(r); GraphDestroy(g);

    GraphInit(g);
    CHECK(!LoadDatasetFile("this/file/does/not/exist.txt", g, r) && !r.error.empty());
    LoadReportFree(r); GraphDestroy(g);
}

static void TestRoundTrip() {
    SECTION("generator determinism + write/load round trip");
    Graph a; GraphInit(a); GenerateSyntheticGraph(a, 500, 42, 6);
    Graph b; GraphInit(b); GenerateSyntheticGraph(b, 500, 42, 6);
    Graph c; GraphInit(c); GenerateSyntheticGraph(c, 500, 43, 6);
    CHECK(a.nodes.size == 500 && a.arcCount == b.arcCount && a.arcCount > 1000);
    bool same = true;
    for (int i = 0; i < 500; ++i) same = same && a.nodes.data[i]->pos.x == b.nodes.data[i]->pos.x;
    CHECK(same);
    CHECK(a.arcCount != c.arcCount || a.nodes.data[3]->pos.x != c.nodes.data[3]->pos.x);   // different seed differs

    ComponentInfo comps = LabelComponents(a);
    CHECK(comps.count == 1);                                          // backbone keeps it connected
    ComponentInfoFree(comps);

    const char* tmp = "netviz_roundtrip_test.txt";
    CHECK(WriteDatasetFile(a, tmp));
    Graph d; GraphInit(d); LoadReport r;
    CHECK(LoadDatasetFile(tmp, d, r));
    CHECK(d.nodes.size == a.nodes.size && d.arcCount == a.arcCount && LoadReportWarningCount(r) == 0);
    bool weights = true;
    for (int i = 0; i < a.nodes.size; ++i) {
        const Node* na = a.nodes.data[i];
        const Node* nd = GraphFindNode(d, na->id);
        weights = weights && nd && nd->out.size == na->out.size;
        for (int j = 0; weights && j < na->out.size; ++j) {
            const Edge* e = NodeFindArc(nd, GraphFindNode(d, na->out.data[j].to->id));
            weights = weights && e && Near(e->weight, na->out.data[j].weight, 1e-4);
        }
    }
    CHECK(weights);
    std::remove(tmp);
    LoadReportFree(r); GraphDestroy(a); GraphDestroy(b); GraphDestroy(c); GraphDestroy(d);
}

// ------------------------------------------------------------------ picking maths
static void TestPicking() {
    SECTION("raycasting: unprojection + sphere picking");
    OrbitCamera cam;
    cam.target = V3(0, 0, 0); cam.yaw = 0; cam.pitch = 0; cam.distance = 100; cam.fovyDeg = 90;
    Vec3 pos = OrbitPosition(cam);
    CHECK(Near(pos.z, 100) && Near(pos.x, 0) && Near(pos.y, 0));
    PickRay centre = ScreenPointToRay(cam, 400, 300, 800, 600);
    CHECK(Near(centre.dir.z, -1, 1e-5) && Near(centre.dir.x, 0, 1e-5));
    PickRay right = ScreenPointToRay(cam, 800, 300, 800, 600);        // right edge: tan(45)*aspect = 1.333
    CHECK(Near(right.dir.x / -right.dir.z, 4.0 / 3.0, 1e-4));
    PickRay top = ScreenPointToRay(cam, 400, 0, 800, 600);            // top edge: tan(45) = 1
    CHECK(Near(top.dir.y / -top.dir.z, 1.0, 1e-4));

    Graph g; GraphInit(g);
    Node* n1 = GraphAddNode(g, "near", V3(0, 0, 10));  n1->scene = V3(0, 0, 10);
    Node* n2 = GraphAddNode(g, "far", V3(0, 0, -20));  n2->scene = V3(0, 0, -20);
    Node* n3 = GraphAddNode(g, "side", V3(30, 0, 0));  n3->scene = V3(30, 0, 0);
    CHECK(PickNode(g, centre, 3.0f, 0.0f, cam.fovyDeg, 600) == n1->index);   // nearest of two on the ray
    float t;
    CHECK(RaySphereIntersect(centre, V3(0, 0, 10), 3.0f, &t) && Near(t, 87, 1e-3));
    CHECK(!RaySphereIntersect(centre, V3(0, 20, 10), 3.0f, &t));
    CHECK(!RaySphereIntersect(centre, V3(0, 0, 300), 3.0f, &t));      // behind the camera
    PickRay offset = ScreenPointToRay(cam, 400 + 40, 300, 800, 600);  // ~ 9 px-ish off n1 at 90 deg fov
    CHECK(PickNode(g, offset, 0.1f, 0.0f, cam.fovyDeg, 600) == -1);   // tiny sphere: miss
    CHECK(PickNode(g, offset, 0.1f, 60.0f, cam.fovyDeg, 600) >= 0);   // min pixel radius makes it clickable
    (void)n3;
    GraphDestroy(g);

    OrbitCamera c2;                                                   // orbit/zoom/pan sanity
    float d0 = c2.distance;
    OrbitZoom(c2, 3.0f); CHECK(c2.distance < d0);
    OrbitRotate(c2, 0, 100000); CHECK(c2.pitch <= 1.5f + 1e-6f);      // pitch clamped
    Vec3 before = c2.target; OrbitPan(c2, 50, 20, 720);
    CHECK(V3Dist(before, c2.target) > 0.1f);
}

static void TestScale() {
    SECTION("scale: 100,000 synthetic nodes build, stay bounded, route correctly");
    Graph g; GraphInit(g);
    GenerateSyntheticGraph(g, 100000, 42, 6);
    CHECK(g.nodes.size == 100000 && g.table.count == 100000);
    CHECK(HashTableLoadFactor(g.table) <= 0.75);
    HashTableStats s = HashTableComputeStats(g.table);
    CHECK(s.maxChain < 16);                                           // bounded chains with FNV-1a
    std::printf("       load factor %.3f  avg chain %.2f  peak chain %zu  avg probes/hit %.2f\n",
                s.loadFactor, s.avgChainUsed, s.maxChain, s.avgProbesHit);
    PathResult p = DijkstraShortestPath(g, GraphFindNode(g, "Node_1"), GraphFindNode(g, "Node_100000"));
    CHECK(p.found && p.length >= 2 && p.totalWeight > 0);
    PathFree(p);
    KHopResult k = BfsKHop(g, GraphFindNode(g, "Node_500"), 2, DIR_BOTH);
    CHECK(k.count > 5 && k.count < 200);
    KHopFree(k);
    GraphDestroy(g);
}

int main() {
    TestHashBasics();
    TestHashResize();
    TestHashWeak();
    TestHashDifferential();
    TestGraphEdits();
    TestAlgorithms();
    TestDijkstraRandomAgainstBellmanFord();
    TestParserHappyPath();
    TestParserEdgeCases();
    TestParserDegenerate();
    TestRoundTrip();
    TestPicking();
    TestScale();
    std::printf("\n%d checks, %d failed\n", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
