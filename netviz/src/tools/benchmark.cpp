// benchmark.cpp - compares the custom hash table with std::unordered_map and a plain array scan,
// and reports collision statistics (also under deliberately weak hash functions).
// Build + run:  make bench        (std containers are used here ONLY as the comparison baseline)
#include <cstdio>
#include <string>
#include <unordered_map>
#include "../core/hash_table.h"
#include "../core/rng.h"
#include "../core/timer.h"

static volatile size_t g_sink = 0;   // stops the compiler from deleting lookups whose result is unused

static void MakeKeys(DynArray<Node*>& nodes, int n) {
    for (int i = 0; i < n; ++i) DynArrayPush(nodes, NodeCreate("Server_" + std::to_string(i), V3(0, 0, 0)));
}
static void FreeKeys(DynArray<Node*>& nodes) {
    for (int i = 0; i < nodes.size; ++i) NodeDestroy(nodes.data[i]);
    DynArrayFree(nodes);
}
static void Shuffle(DynArray<int>& order, Rng& rng) {
    for (int i = order.size - 1; i > 0; --i) {
        int j = static_cast<int>(RngInt(rng, static_cast<uint32_t>(i + 1)));
        int t = order.data[i]; order.data[i] = order.data[j]; order.data[j] = t;
    }
}

static void CompareContainers(int n) {
    DynArray<Node*> nodes; MakeKeys(nodes, n);
    DynArray<int> order;                                   // random lookup / delete order
    for (int i = 0; i < n; ++i) DynArrayPush(order, i);
    Rng rng; RngSeed(rng, 99); Shuffle(order, rng);

    // ---- custom hash table
    HashTable t; HashTableInit(t, 16);
    double t0 = NowNs();
    for (int i = 0; i < n; ++i) HashTableInsert(t, nodes.data[i]);
    double insNs = (NowNs() - t0) / n;
    t0 = NowNs();
    for (int i = 0; i < n; ++i) g_sink += HashTableFind(t, nodes.data[order.data[i]]->id) != nullptr;
    double findNs = (NowNs() - t0) / n;
    HashTableStats st = HashTableComputeStats(t);
    t0 = NowNs();
    for (int i = 0; i < n; ++i) HashTableRemove(t, nodes.data[order.data[i]]->id);
    double delNs = (NowNs() - t0) / n;
    HashTableDestroy(t);

    // ---- std::unordered_map baseline
    std::unordered_map<std::string, Node*> um;
    t0 = NowNs();
    for (int i = 0; i < n; ++i) um[nodes.data[i]->id] = nodes.data[i];
    double uInsNs = (NowNs() - t0) / n;
    t0 = NowNs();
    for (int i = 0; i < n; ++i) g_sink += um.find(nodes.data[order.data[i]]->id) != um.end();
    double uFindNs = (NowNs() - t0) / n;
    t0 = NowNs();
    for (int i = 0; i < n; ++i) um.erase(nodes.data[order.data[i]]->id);
    double uDelNs = (NowNs() - t0) / n;

    // ---- plain array scan (sampled: a full run would be O(n^2) for the big sizes)
    int samples = n <= 1000 ? n : 1000;
    t0 = NowNs();
    for (int s = 0; s < samples; ++s) {
        const std::string& key = nodes.data[order.data[s]]->id;
        for (int i = 0; i < nodes.size; ++i) if (nodes.data[i]->id == key) { g_sink += i; break; }
    }
    double scanNs = (NowNs() - t0) / samples;

    std::printf("%7d | custom  insert %7.0f  find %7.0f  delete %7.0f ns/op\n", n, insNs, findNs, delNs);
    std::printf("        | std::um insert %7.0f  find %7.0f  delete %7.0f ns/op\n", uInsNs, uFindNs, uDelNs);
    std::printf("        | array scan (find) %9.0f ns/op   -> hash is %.0fx faster\n", scanNs, scanNs / findNs);
    std::printf("        | chains: load %.3f  avg(used) %.2f  peak %zu  probes/hit %.2f  resizes %zu  buckets %zu\n\n",
                st.loadFactor, st.avgChainUsed, st.maxChain, st.avgProbesHit, st.resizeCount, st.capacity);
    DynArrayFree(order);
    FreeKeys(nodes);
}

static void WeakHashStress(const char* name, HashFn fn, int n) {
    DynArray<Node*> nodes; MakeKeys(nodes, n);
    HashTable t; HashTableInit(t, 16, fn);
    double t0 = NowNs();
    for (int i = 0; i < n; ++i) HashTableInsert(t, nodes.data[i]);
    double insMs = (NowNs() - t0) / 1e6;
    t0 = NowNs();
    for (int i = 0; i < n; ++i) g_sink += HashTableFind(t, nodes.data[i]->id) != nullptr;
    double findNs = (NowNs() - t0) / n;
    HashTableStats st = HashTableComputeStats(t);
    std::printf("%-12s n=%-6d insert total %8.2f ms | find %9.0f ns | peak chain %6zu | probes/hit %8.1f | resizes %zu\n",
                name, n, insMs, findNs, st.maxChain, st.avgProbesHit, st.resizeCount);
    HashTableDestroy(t);
    FreeKeys(nodes);
}

int main() {
    std::printf("=== Custom hash table vs std::unordered_map vs array scan ===\n");
    std::printf("(keys: \"Server_<i>\", random lookup/delete order, fixed seed)\n\n");
    CompareContainers(1000);
    CompareContainers(10000);
    CompareContainers(100000);

    std::printf("=== Collision stress: weak hash functions (resize triggers still fire) ===\n");
    WeakHashStress("FNV-1a", HashFnv1a, 10000);
    WeakHashStress("first-char", HashWeakFirstChar, 10000);
    WeakHashStress("length", HashWeakLength, 10000);
    WeakHashStress("constant", HashWeakConstant, 10000);
    std::printf("\nNote: with a weak hash, doubling the table cannot split chains that share one hash\n"
                "value, so peak chain stays huge - this is exactly why a good hash function matters.\n");
    return 0;
}
