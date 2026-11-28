// generator.h - deterministic synthetic network (benchmarks + demos of big graphs).
#pragma once
#include <cstdint>
#include "graph.h"

// Builds `nodeCount` nodes named Node_1..Node_N on a (slightly jittered) sphere of radius 100 using
// a golden-angle spiral, linked by a backbone chain plus seeded local and long-range links.
// Latency ~ distance + jitter. avgDegree is the target average number of neighbours (>= 2).
// Same (nodeCount, seed, avgDegree) always gives the exact same graph.
void GenerateSyntheticGraph(Graph& g, int nodeCount, uint64_t seed, int avgDegree = 6);
