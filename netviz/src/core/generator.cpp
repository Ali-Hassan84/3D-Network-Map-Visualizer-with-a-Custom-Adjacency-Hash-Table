#include "generator.h"
#include <cmath>
#include <cstdio>
#include "rng.h"

void GenerateSyntheticGraph(Graph& g, int n, uint64_t seed, int avgDegree) {
    if (n < 1) return;
    if (avgDegree < 2) avgDegree = 2;
    Rng rng;
    RngSeed(rng, seed);

    char name[48];
    std::snprintf(name, sizeof(name), "Synthetic %d nodes (seed %llu)", n, static_cast<unsigned long long>(seed));
    g.name = name;

    for (int i = 0; i < n; ++i) {
        double y = 1.0 - 2.0 * (i + 0.5) / n;                 // golden-angle spiral on a sphere
        double r = std::sqrt(1.0 - y * y);
        double phi = i * 2.39996322972865332;
        double radius = 100.0 * (1.0 + (RngDouble(rng) - 0.5) * 0.08);   // +-4 % radial jitter
        char id[24];
        std::snprintf(id, sizeof(id), "Node_%d", i + 1);
        GraphAddNode(g, id, V3(static_cast<float>(radius * std::cos(phi) * r),
                               static_cast<float>(radius * y),
                               static_cast<float>(radius * std::sin(phi) * r)));
    }

    auto link = [&](int a, int b) {
        Node* na = g.nodes.data[a];
        Node* nb = g.nodes.data[b];
        double latency = 0.1 * V3Dist(na->pos, nb->pos) + 1.0 + RngDouble(rng) * 4.0;
        GraphAddEdgeNodes(g, na, nb, std::round(latency * 100.0) / 100.0, true);
    };

    for (int i = 0; i + 1 < n; ++i) link(i, i + 1);           // backbone: whole graph is connected
    int extra = (avgDegree - 2) / 2;                          // each undirected link adds 2 to the degree sum
    uint32_t window = static_cast<uint32_t>(n / 10 < 8 ? 8 : (n / 10 > 200 ? 200 : n / 10));
    for (int i = 0; i < n; ++i) {
        for (int k = 0; k < extra; ++k) {                     // nearby in the spiral == nearby in space
            int j = i + 2 + static_cast<int>(RngInt(rng, window));
            if (j < n) link(i, j);
        }
        if (RngInt(rng, 100) == 0 && n > 2)                   // ~1 % long-range links
            link(i, static_cast<int>(RngInt(rng, static_cast<uint32_t>(n))));
    }
    GraphFitScene(g);
}
