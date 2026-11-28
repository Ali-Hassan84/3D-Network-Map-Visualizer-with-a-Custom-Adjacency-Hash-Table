#include "dataset_loader.h"
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "timer.h"

namespace {

const int  kMaxTokens   = 8;
const size_t kMaxIdLen  = 128;
const float  kGeoRadius = 100.0f;

struct PendingEdge {
    int   line;
    int   ntok;
    char* tok[kMaxTokens];
};

void AddMessage(LoadReport& r, int line, const char* fmt, const char* a = "", const char* b = "") {
    if (r.messages.size >= kMaxStoredMessages) return;
    LoadMessage m;
    m.line = line;
    std::snprintf(m.text, sizeof(m.text), fmt, a, b);
    DynArrayPush(r.messages, m);
}

bool EqualsNoCase(const char* a, const char* b) {
    for (; *a && *b; ++a, ++b)
        if (std::toupper(static_cast<unsigned char>(*a)) != std::toupper(static_cast<unsigned char>(*b)))
            return false;
    return *a == *b;
}

bool ParseNumber(const char* s, double* out) {
    char* end = nullptr;
    double v = std::strtod(s, &end);
    if (end == s || *end != '\0' || !std::isfinite(v)) return false;
    *out = v;
    return true;
}

// Splits `line` in place on spaces/tabs. Returns the token count, or kMaxTokens+1 if too many.
int Tokenize(char* line, char** tok) {
    int n = 0;
    char* p = line;
    while (*p) {
        while (*p == ' ' || *p == '\t') ++p;
        if (!*p) break;
        if (n == kMaxTokens) return kMaxTokens + 1;
        tok[n++] = p;
        while (*p && *p != ' ' && *p != '\t') ++p;
        if (*p) *p++ = '\0';
    }
    return n;
}

Vec3 GeoToCartesian(double latDeg, double lonDeg, float radius) {
    const double d2r = 3.14159265358979323846 / 180.0;
    double lat = latDeg * d2r, lon = lonDeg * d2r;
    // Y is up; lon 0 / lat 0 sits on +Z; east (+lon) is towards +X (appears on the right).
    return V3(static_cast<float>(radius * std::cos(lat) * std::sin(lon)),
              static_cast<float>(radius * std::sin(lat)),
              static_cast<float>(radius * std::cos(lat) * std::cos(lon)));
}

// Evenly spreads M points over a sphere (golden-angle spiral): deterministic auto layout.
Vec3 FibonacciSpherePoint(int i, int m, Vec3 center, float radius) {
    double y = 1.0 - 2.0 * (i + 0.5) / m;
    double r = std::sqrt(1.0 - y * y);
    double phi = i * 2.39996322972865332;
    return V3(center.x + radius * static_cast<float>(std::cos(phi) * r),
              center.y + radius * static_cast<float>(y),
              center.z + radius * static_cast<float>(std::sin(phi) * r));
}

}  // namespace

int LoadReportWarningCount(const LoadReport& r) {
    return r.duplicateNodes + r.malformedLines + r.missingEndpoints + r.selfLoops +
           r.redundantEdges + r.badWeights;
}

void LoadReportFree(LoadReport& r) { DynArrayFree(r.messages); }

bool ParseDatasetText(const char* text, size_t length, Graph& g, LoadReport& rep) {
    double t0 = NowNs();
    std::string path = rep.path;                       // keep what the caller set
    LoadReportFree(rep);
    rep = LoadReport();
    rep.path = path;

    char* buf = static_cast<char*>(std::malloc(length + 1));
    if (!buf) { rep.error = "out of memory"; return false; }
    std::memcpy(buf, text, length);
    buf[length] = '\0';

    char* p = buf;
    if (length >= 3 && static_cast<unsigned char>(p[0]) == 0xEF &&
        static_cast<unsigned char>(p[1]) == 0xBB && static_cast<unsigned char>(p[2]) == 0xBF)
        p += 3;                                        // skip UTF-8 byte-order mark

    bool geo = false, defaultUndirected = true, sawNode = false;
    DynArray<PendingEdge> edges;
    DynArray<Node*> autoNodes;
    std::string name;

    // ---------------- pass 1: directives + NODE lines (EDGE lines are queued for pass 2)
    int lineNo = 0;
    while (*p) {
        char* lineStart = p;
        while (*p && *p != '\n') ++p;
        if (*p == '\n') *p++ = '\0';
        ++lineNo;
        char* hash = std::strchr(lineStart, '#');      // strip comments
        if (hash) *hash = '\0';
        size_t len = std::strlen(lineStart);
        while (len > 0 && (lineStart[len - 1] == '\r' || lineStart[len - 1] == ' ' ||
                           lineStart[len - 1] == '\t')) lineStart[--len] = '\0';

        char* tok[kMaxTokens];
        int nt = Tokenize(lineStart, tok);
        if (nt == 0) continue;

        const char* kw = tok[0];
        if (nt > kMaxTokens) {
            rep.malformedLines++; AddMessage(rep, lineNo, "too many fields on this line"); continue;
        }

        if (EqualsNoCase(kw, "NAME")) {
            name.clear();
            for (int i = 1; i < nt; ++i) { if (i > 1) name += ' '; name += tok[i]; }
        } else if (EqualsNoCase(kw, "COORDS")) {
            if (nt != 2 || sawNode) {
                rep.malformedLines++;
                AddMessage(rep, lineNo, sawNode ? "COORDS must come before the first NODE" : "COORDS needs CARTESIAN or GEO");
            } else if (EqualsNoCase(tok[1], "GEO")) geo = true;
            else if (EqualsNoCase(tok[1], "CARTESIAN")) geo = false;
            else { rep.malformedLines++; AddMessage(rep, lineNo, "COORDS needs CARTESIAN or GEO"); }
        } else if (EqualsNoCase(kw, "DEFAULT_LINK")) {
            if (nt == 2 && EqualsNoCase(tok[1], "DIRECTED")) defaultUndirected = false;
            else if (nt == 2 && EqualsNoCase(tok[1], "UNDIRECTED")) defaultUndirected = true;
            else { rep.malformedLines++; AddMessage(rep, lineNo, "DEFAULT_LINK needs DIRECTED or UNDIRECTED"); }
        } else if (EqualsNoCase(kw, "NODE")) {
            sawNode = true;
            int want = geo ? 4 : 5;                    // NODE id lat lon  |  NODE id x y z
            if (nt != 2 && nt != want) {
                rep.malformedLines++;
                AddMessage(rep, lineNo, geo ? "NODE needs: id lat lon" : "NODE needs: id x y z");
                continue;
            }
            if (std::strlen(tok[1]) > kMaxIdLen) {
                rep.malformedLines++; AddMessage(rep, lineNo, "node id longer than 128 characters"); continue;
            }
            Vec3 pos;
            bool placed = (nt == want);
            if (placed) {
                double a, b, c = 0.0;
                bool ok = ParseNumber(tok[2], &a) && ParseNumber(tok[3], &b) && (geo || ParseNumber(tok[4], &c));
                if (ok && geo && (a < -90.0 || a > 90.0 || b < -180.0 || b > 180.0)) ok = false;
                if (!ok) {
                    rep.malformedLines++;
                    AddMessage(rep, lineNo, geo ? "bad coordinates (lat -90..90, lon -180..180)" : "coordinates are not numbers");
                    continue;
                }
                pos = geo ? GeoToCartesian(a, b, kGeoRadius)
                          : V3(static_cast<float>(a), static_cast<float>(b), static_cast<float>(c));
            }
            Node* n = GraphAddNode(g, tok[1], pos);
            if (!n) {
                rep.duplicateNodes++; AddMessage(rep, lineNo, "duplicate node '%s' ignored (first one kept)", tok[1]);
                continue;
            }
            rep.nodesLoaded++;
            if (!placed) { n->component = -2; DynArrayPush(autoNodes, n); rep.autoPlaced++; }   // -2 = temp marker
        } else if (EqualsNoCase(kw, "EDGE")) {
            PendingEdge pe;
            pe.line = lineNo;
            pe.ntok = nt;
            for (int i = 0; i < nt; ++i) pe.tok[i] = tok[i];
            for (int i = nt; i < kMaxTokens; ++i) pe.tok[i] = nullptr;
            DynArrayPush(edges, pe);
        } else {
            rep.malformedLines++; AddMessage(rep, lineNo, "unknown keyword '%s'", kw);
        }
    }
    rep.linesRead = lineNo;

    // ---------------- deterministic layout for nodes that came without coordinates
    if (autoNodes.size > 0) {
        Vec3 c = V3(0, 0, 0);
        float radius = 100.0f;
        int explicitCount = g.nodes.size - autoNodes.size;
        if (!geo && explicitCount >= 2) {
            for (int i = 0; i < g.nodes.size; ++i) {
                if (g.nodes.data[i]->component != -2) c = V3Add(c, g.nodes.data[i]->pos);
            }
            c = V3Scale(c, 1.0f / explicitCount);
            float maxD = 0.0f;
            for (int i = 0; i < g.nodes.size; ++i) {
                if (g.nodes.data[i]->component != -2) maxD = std::fmax(maxD, V3Dist(g.nodes.data[i]->pos, c));
            }
            if (maxD > 0.0f) radius = maxD;
        }
        for (int i = 0; i < autoNodes.size; ++i) {
            autoNodes.data[i]->pos = FibonacciSpherePoint(i, autoNodes.size, c, radius);
            autoNodes.data[i]->component = -1;           // remove the temp marker
        }
    }

    // ---------------- pass 2: EDGE lines (all nodes are known now, so order in the file is free)
    for (int i = 0; i < edges.size; ++i) {
        const PendingEdge& e = edges.data[i];
        if (e.ntok != 4 && e.ntok != 5) {
            rep.malformedLines++; AddMessage(rep, e.line, "EDGE needs: source target weight [DIRECTED|UNDIRECTED]");
            continue;
        }
        bool undirected = defaultUndirected;
        if (e.ntok == 5) {
            if (EqualsNoCase(e.tok[4], "DIRECTED")) undirected = false;
            else if (EqualsNoCase(e.tok[4], "UNDIRECTED")) undirected = true;
            else { rep.malformedLines++; AddMessage(rep, e.line, "last EDGE field must be DIRECTED or UNDIRECTED"); continue; }
        }
        double w;
        if (!ParseNumber(e.tok[3], &w)) {
            rep.malformedLines++; AddMessage(rep, e.line, "weight '%s' is not a number", e.tok[3]); continue;
        }
        switch (GraphAddEdge(g, e.tok[1], e.tok[2], w, undirected)) {
            case EDGE_ADDED:          rep.edgesLoaded++; break;
            case EDGE_MERGED:         rep.redundantEdges++; AddMessage(rep, e.line, "redundant edge %s: kept the smaller weight", e.tok[1]); break;
            case EDGE_SELF_LOOP:      rep.selfLoops++; AddMessage(rep, e.line, "self-loop on '%s' ignored", e.tok[1]); break;
            case EDGE_MISSING_SOURCE: rep.missingEndpoints++; AddMessage(rep, e.line, "unknown source node '%s'", e.tok[1]); break;
            case EDGE_MISSING_TARGET: rep.missingEndpoints++; AddMessage(rep, e.line, "unknown target node '%s'", e.tok[2]); break;
            case EDGE_BAD_WEIGHT:     rep.badWeights++; AddMessage(rep, e.line, "weight must be a finite number >= 0"); break;
        }
    }

    DynArrayFree(edges);
    DynArrayFree(autoNodes);
    std::free(buf);

    if (g.nodes.size == 0) {
        rep.ok = false;
        rep.error = "no valid NODE lines found";
        rep.loadMs = (NowNs() - t0) / 1e6;
        return false;
    }
    g.name = name;
    if (geo) GraphUseGeoScene(g); else GraphFitScene(g);
    rep.geoCoords = geo;
    rep.ok = true;
    rep.loadMs = (NowNs() - t0) / 1e6;
    return true;
}

bool LoadDatasetFile(const char* path, Graph& g, LoadReport& rep) {
    LoadReportFree(rep);
    rep = LoadReport();
    rep.path = path ? path : "";
    FILE* f = path ? std::fopen(path, "rb") : nullptr;
    if (!f) { rep.error = std::string("cannot open file: ") + (path ? path : "(null)"); return false; }
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size < 0) { std::fclose(f); rep.error = "cannot read file size"; return false; }
    char* data = static_cast<char*>(std::malloc(static_cast<size_t>(size) + 1));
    size_t got = data ? std::fread(data, 1, static_cast<size_t>(size), f) : 0;
    std::fclose(f);
    if (!data) { rep.error = "out of memory"; return false; }
    bool ok = ParseDatasetText(data, got, g, rep);
    std::free(data);
    return ok;
}

bool WriteDatasetFile(const Graph& g, const char* path) {
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    std::fprintf(f, "# NetViz 3D dataset (written by WriteDatasetFile)\n");
    if (!g.name.empty()) std::fprintf(f, "NAME %s\n", g.name.c_str());
    std::fprintf(f, "COORDS CARTESIAN\nDEFAULT_LINK UNDIRECTED\n\n");
    for (int i = 0; i < g.nodes.size; ++i) {
        const Node* n = g.nodes.data[i];
        std::fprintf(f, "NODE %s %.6g %.6g %.6g\n", n->id.c_str(), n->pos.x, n->pos.y, n->pos.z);
    }
    std::fprintf(f, "\n");
    for (int i = 0; i < g.nodes.size; ++i) {
        const Node* n = g.nodes.data[i];
        for (int j = 0; j < n->out.size; ++j) {
            const Edge& e = n->out.data[j];
            if (e.undirectedLink) {
                if (n->index < e.to->index)            // write each undirected link once
                    std::fprintf(f, "EDGE %s %s %.6g\n", n->id.c_str(), e.to->id.c_str(), e.weight);
            } else {
                std::fprintf(f, "EDGE %s %s %.6g DIRECTED\n", n->id.c_str(), e.to->id.c_str(), e.weight);
            }
        }
    }
    bool ok = (std::fflush(f) == 0);
    std::fclose(f);
    return ok;
}
