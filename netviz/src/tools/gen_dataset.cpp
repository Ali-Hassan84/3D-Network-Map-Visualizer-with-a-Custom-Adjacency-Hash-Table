// gen_dataset.cpp - writes a deterministic synthetic dataset file.
//   netviz_gen <nodes> <seed> <out.txt> [avgDegree]
#include <cstdio>
#include <cstdlib>
#include "../core/dataset_loader.h"
#include "../core/generator.h"

int main(int argc, char** argv) {
    if (argc < 4) {
        std::printf("usage: %s <nodes> <seed> <out.txt> [avgDegree=6]\n", argv[0]);
        return 1;
    }
    int n = std::atoi(argv[1]);
    unsigned long long seed = std::strtoull(argv[2], nullptr, 10);
    int deg = argc > 4 ? std::atoi(argv[4]) : 6;
    if (n < 1 || n > 5000000) { std::printf("nodes must be 1..5000000\n"); return 1; }

    Graph g;
    GraphInit(g);
    GenerateSyntheticGraph(g, n, seed, deg);
    bool ok = WriteDatasetFile(g, argv[3]);
    std::printf("%s: %d nodes, %lld arcs -> %s\n", ok ? "wrote" : "FAILED", g.nodes.size, g.arcCount, argv[3]);
    GraphDestroy(g);
    return ok ? 0 : 1;
}
