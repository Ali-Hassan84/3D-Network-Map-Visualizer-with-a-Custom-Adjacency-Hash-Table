// dataset_loader.h - reads / writes the plain-text network format (see README.md, "Dataset format").
#pragma once
#include <cstddef>
#include <string>
#include "dyn_array.h"
#include "graph.h"

struct LoadMessage {
    int  line;            // 1-based line number in the file (0 = not tied to a line)
    char text[120];
};

struct LoadReport {
    bool        ok = false;            // false => nothing usable was loaded
    std::string error;                 // reason when ok == false
    std::string path;
    int    linesRead = 0;
    int    nodesLoaded = 0;
    int    edgesLoaded = 0;            // accepted EDGE lines (new connections)
    int    duplicateNodes = 0;
    int    malformedLines = 0;
    int    missingEndpoints = 0;       // EDGE referenced an unknown node id
    int    selfLoops = 0;
    int    redundantEdges = 0;         // same connection listed again (smaller weight kept)
    int    badWeights = 0;             // negative / NaN / inf
    int    autoPlaced = 0;             // NODE lines without coordinates
    bool   geoCoords = false;
    double loadMs = 0.0;
    DynArray<LoadMessage> messages;    // first kMaxStoredMessages problems, for the on-screen report
};

const int kMaxStoredMessages = 200;

int  LoadReportWarningCount(const LoadReport& r);   // everything that was skipped or adjusted
void LoadReportFree(LoadReport& r);

// Both functions expect an initialised, empty Graph. On failure the graph is left empty.
bool LoadDatasetFile(const char* path, Graph& g, LoadReport& report);
bool ParseDatasetText(const char* text, size_t length, Graph& g, LoadReport& report);

// Writes the graph back out in the same format (CARTESIAN coordinates).
bool WriteDatasetFile(const Graph& g, const char* path);
