// ui.cpp - NetViz 3D — 2D HUD, panels, search box, help and load-report overlays.
//
// Design goals for this revision:
//   • No unnecessary clutter: panels are compact, unambiguous.
//   • Column-aligned key/value rows — the value column always starts at the same X.
//   • Clean typography: a fixed column offset, consistent font sizes, generous line-height.
//   • No hard-coded absolute pixel positions that break on window resize.
//   • Rounded rectangles for backgrounds (DrawRectangleRounded).
//   • Bottom bar is minimal: only the most common controls are listed.
#include "ui.h"
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include "raylib.h"

namespace {

// -------------------------------------------------------------------------------------
// Colour palette  (matches renderer.cpp colours for A / B / path)
// -------------------------------------------------------------------------------------
const Color kBg      = {  8, 11, 22, 255 };   // scene background colour
const Color kPanel   = { 11, 15, 28, 220 };   // panel fill
const Color kBorder  = { 48, 68, 108, 255 };  // panel border
const Color kSep     = { 35, 50, 82, 220 };   // section separator line
const Color kText    = { 215, 225, 240, 255 }; // primary text
const Color kMuted   = { 120, 138, 165, 200 }; // secondary / hint text
const Color kAccent  = {  95, 195, 255, 255 }; // accent (panel titles, links)
const Color kGood    = { 110, 232, 148, 255 }; // success / healthy
const Color kWarn    = { 255, 175,  75, 255 }; // warning
const Color kBad     = { 255,  90,  90, 255 }; // error
const Color kGold    = { 255, 210,  50, 255 }; // source node A
const Color kPink    = { 240,  70, 190, 255 }; // target node B
const Color kPath    = {  60, 240, 130, 255 }; // shortest-path mint

// -------------------------------------------------------------------------------------
// Typography constants
// -------------------------------------------------------------------------------------
const int kFontSm   = 14;  // small / muted info
const int kFontMd   = 15;  // body rows
const int kFontHdr  = 13;  // section headings (all-caps)
const int kFontPnl  = 17;  // panel title
const int kLineH    = 21;  // body row height
const int kValCol   = 148; // pixels from row-left to value column  (key / value alignment)
const float kPanelW = 340.0f;  // panel width in pixels
const float kRadius = 0.12f;   // rounded-rect corner radius ratio

// -------------------------------------------------------------------------------------
// Low-level drawing helpers
// -------------------------------------------------------------------------------------

void Txt(int x, int y, int size, Color c, const char* fmt, ...) {
    char buf[280];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    DrawText(buf, x, y, size, c);
}

// Rounded panel background + 1-px border line
void PanelBg(float x, float y, float w, float h) {
    Rectangle rect{ x, y, w, h };
    DrawRectangleRounded(rect, kRadius, 6, kPanel);
    DrawRectangleRoundedLines(rect, kRadius, 6, 1.5f, kBorder);
}

// Panel with title bar
void Panel(UiRect r, const char* title) {
    PanelBg(r.x, r.y, r.w, r.h);
    DrawText(title, static_cast<int>(r.x) + 14, static_cast<int>(r.y) + 10, kFontPnl, kAccent);
    // Thin title separator
    DrawLineEx({ r.x + 1, r.y + 32 }, { r.x + r.w - 1, r.y + 32 }, 1.0f, kSep);
}

// Section heading (small all-caps label + horizontal rule)
void Heading(int x, int& y, const char* text) {
    y += 8;
    DrawText(text, x, y, kFontHdr, kMuted);
    DrawLineEx({ static_cast<float>(x),     static_cast<float>(y + 15) },
               { static_cast<float>(x + static_cast<int>(kPanelW) - 28), static_cast<float>(y + 15) },
               1.0f, kSep);
    y += 22;
}

// Key / value row — key is left-aligned, value starts at kValCol from x
void Row(int x, int& y, const char* label, Color vc, const char* fmt, ...) {
    char buf[220];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    DrawText(label, x, y, kFontMd, kMuted);
    DrawText(buf,   x + kValCol, y, kFontMd, vc);
    y += kLineH;
}

// Duration formatter: ns / µs / ms
const char* Duration(double ns) {
    static char ring[4][32];
    static int idx = 0;
    char* b = ring[idx++ & 3];
    if (ns < 1000.0)     std::snprintf(b, 32, "%.0f ns", ns);
    else if (ns < 1.0e6) std::snprintf(b, 32, "%.1f µs", ns / 1e3);
    else                 std::snprintf(b, 32, "%.2f ms", ns / 1e6);
    return b;
}

const char* DirName(Direction d) {
    return d == DIR_BOTH ? "both" : (d == DIR_OUT ? "outgoing" : "incoming");
}

// -------------------------------------------------------------------------------------
// Dataset / hash-table panel (left side)
// -------------------------------------------------------------------------------------
void DrawDatasetPanel(const AppState& s, UiRect r) {
    Panel(r, "DATASET  &  HASH TABLE");
    BeginScissorMode(static_cast<int>(r.x), static_cast<int>(r.y),
                     static_cast<int>(r.w), static_cast<int>(r.h));

    int x = static_cast<int>(r.x) + 14;
    int y = static_cast<int>(r.y) + 40;
    const Graph& g = s.graph;

    // --- Dataset section ---
    Heading(x, y, "DATASET");
    char lbl[36];
    std::snprintf(lbl, sizeof(lbl), "%.30s",
                  s.sourceLabel.empty() ? "(none)" : s.sourceLabel.c_str());
    Row(x, y, "Source",       kText, "%s", lbl);
    Row(x, y, "Nodes / arcs", kText, "%d / %lld", g.nodes.size, g.arcCount);
    Row(x, y, "Components",   kText, "%d  (largest %d)", s.comps.count, s.comps.largest);
    Row(x, y, "Loaded in",    kText, "%.1f ms", s.report.loadMs);
    int warn = LoadReportWarningCount(s.report);
    if (warn > 0) Row(x, y, "Warnings", kWarn, "%d  (press L)", warn);
    else          Row(x, y, "Warnings", kGood, "none");
    Row(x, y, "Render mode",  kText, "%s", s.largeMode ? "large (neighbourhood)" : "full graph");

    // --- Hash table section ---
    Heading(x, y, "HASH TABLE  —  FNV-1a, chaining");
    const HashTableStats& t = s.tableStats;
    Row(x, y, "Buckets / items",  kText, "%zu / %zu", t.capacity, t.count);
    Row(x, y, "Load factor",
        t.loadFactor <= 0.75 ? kGood : kBad,
        "%.3f  (max 0.75)", t.loadFactor);
    Row(x, y, "Resizes",         kText, "%zu  (×2 each)", t.resizeCount);
    Row(x, y, "Chain avg / peak", kText, "%.2f / %zu", t.avgChainUsed, t.maxChain);
    Row(x, y, "Probes / hit",    kText, "%.2f", t.avgProbesHit);
    Row(x, y, "Empty buckets",   kText, "%.1f %%",
        t.capacity ? 100.0 * t.emptyBuckets / t.capacity : 0.0);

    // Chain-length histogram (log-scale bar chart, 0..7+ buckets)
    y += 4;
    size_t peak = 1;
    for (int i = 0; i < 8; ++i) if (t.histogram[i] > peak) peak = t.histogram[i];
    const int bh = 44, bw = 28, gap = 10;
    const int bx = x;
    for (int i = 0; i < 8; ++i) {
        int h = static_cast<int>(
            std::log10(1.0 + static_cast<double>(t.histogram[i])) /
            std::log10(1.0 + static_cast<double>(peak)) * bh);
        Color bc = i == 0 ? Color{ 55, 70, 110, 220 } : kAccent;
        DrawRectangleRounded({ static_cast<float>(bx + i * (bw + gap)),
                               static_cast<float>(y + bh - h),
                               static_cast<float>(bw),
                               static_cast<float>(h) },
                             0.2f, 3, bc);
        Txt(bx + i * (bw + gap) + (i == 7 ? 2 : 7), y + bh + 3, kFontSm, kMuted,
            i == 7 ? "7+" : "%d", i);
    }
    y += bh + 20;
    DrawText("chain length histogram (log scale)", x, y, kFontSm, kMuted);
    y += 18;

    // --- Lookup timing section ---
    Heading(x, y, "LOOKUP  —  last selected node");
    if (s.m.hasLookup) {
        Row(x, y, "Hash find", kGood,
            "%s  (%d probe%s)", Duration(s.m.lookupNs),
            s.m.lookupProbes, s.m.lookupProbes == 1 ? "" : "s");
        Row(x, y, "Linear scan", kWarn, "%s", Duration(s.m.scanNs));
        double ratio = s.m.lookupNs > 0 ? s.m.scanNs / s.m.lookupNs : 0.0;
        Row(x, y, "Speed-up",
            ratio >= 1.0 ? kGood : kMuted,
            ratio < 10.0 ? "×%.1f" : "×%.0f", ratio);
    } else {
        DrawText("Select a node to benchmark the lookup.", x, y, kFontMd, kMuted);
        y += kLineH;
    }
    if (s.m.hasPick)
        Row(x, y, "Ray cast (click)", kText, "%s", Duration(s.m.pickUs * 1000.0));

    EndScissorMode();
}

// -------------------------------------------------------------------------------------
// Selection / route panel (right side)
// -------------------------------------------------------------------------------------
void DrawSelectionPanel(const AppState& s, UiRect r) {
    Panel(r, "SELECTION  &  ROUTE");

    // FPS — top-right corner of panel
    Txt(static_cast<int>(r.x + r.w) - 68,
        static_cast<int>(r.y) + 11, kFontSm, kMuted, "%d FPS", GetFPS());

    BeginScissorMode(static_cast<int>(r.x), static_cast<int>(r.y),
                     static_cast<int>(r.w), static_cast<int>(r.h));

    int x = static_cast<int>(r.x) + 14;
    int y = static_cast<int>(r.y) + 40;

    if (!s.a) {
        // Empty-state hint
        y += 10;
        DrawText("Click a node to inspect it.", x, y, kFontMd, kText);
        y += kLineH + 4;
        DrawText("Shift+click a second node", x, y, kFontMd, kMuted); y += kLineH;
        DrawText("to find the shortest route.", x, y, kFontMd, kMuted); y += kLineH + 4;
        DrawText("Press / to search by ID.",   x, y, kFontMd, kMuted);
        EndScissorMode();
        return;
    }

    const Node* a = s.a;

    // Source (A)
    Heading(x, y, "SOURCE NODE  A");
    Row(x, y, "ID",         kGold, "%.28s",    a->id.c_str());
    Row(x, y, "Position",   kText, "%.1f  %.1f  %.1f", a->pos.x, a->pos.y, a->pos.z);
    Row(x, y, "Out / in degree", kText, "%d / %d", a->out.size, a->in.size);
    Row(x, y, "Distinct neighbours", kText, "%d", AppDistinctNeighbors(a));
    if (a->component >= 0 && a->component < s.comps.sizes.size)
        Row(x, y, "Component", kText,
            "#%d  (%d nodes)", a->component, s.comps.sizes.data[a->component]);

    // K-hop neighbourhood
    Heading(x, y, "K-HOP NEIGHBOURHOOD");
    Row(x, y, "Hops  k  (↑ ↓ or 1-9)", kAccent, "%d", s.k);
    Row(x, y, "Direction  (D)",          kText,   "%s", DirName(s.dir));
    Row(x, y, "Nodes within k hops",     kText,   "%d  (deepest %d)",
        s.khop.count, s.khop.maxHop);
    Row(x, y, "BFS time",                kText,   "%s", Duration(s.m.bfsUs * 1000.0));

    // Direct link list (capped to avoid overflow)
    y += 2;
    DrawText("Direct links:", x, y, kFontHdr, kMuted); y += 18;
    int shown = 0;
    for (int i = 0; i < a->out.size && shown < 7; ++i, ++shown) {
        const Edge& e = a->out.data[i];
        const char* arrow = e.undirectedLink ? "<>" : "->";
        Txt(x + 6, y, kFontSm, kText, "%s  %.22s", arrow, e.to->id.c_str());
        Txt(x + kValCol + 70, y, kFontSm, kMuted, "%.1f ms", e.weight);
        y += 18;
    }
    if (a->out.size > shown) {
        Txt(x + 6, y, kFontSm, kMuted, "… +%d more", a->out.size - shown);
        y += 18;
    }

    // Route A→B
    if (s.b) {
        y += 4;
        Heading(x, y, "ROUTE  A → B  (Dijkstra)");
        Row(x, y, "Target  B",    kPink, "%.26s", s.b->id.c_str());
        if (s.path.found) {
            Row(x, y, "Total latency",  kGood, "%.2f ms", s.path.totalWeight);
            Row(x, y, "Hops",           kText, "%d", s.path.length - 1);
            Row(x, y, "Nodes settled",  kText, "%d of %d", s.path.settled, s.graph.nodes.size);
            Row(x, y, "Search time",    kText, "%s", Duration(s.m.dijkstraUs * 1000.0));
            y += 4;
            // Node chain — wrap onto new lines if needed
            int lx = x;
            const int maxX = static_cast<int>(r.x + r.w) - 14;
            const int maxShow = s.path.length < 14 ? s.path.length : 14;
            for (int i = 0; i < maxShow; ++i) {
                char tok[50];
                std::snprintf(tok, sizeof(tok), "%s%.14s",
                              i ? " > " : "",
                              s.graph.nodes.data[s.path.nodes[i]]->id.c_str());
                int tw = MeasureText(tok, kFontSm);
                if (lx + tw > maxX && lx > x) { lx = x; y += 17; }
                DrawText(tok, lx, y + 2, kFontSm, kPath);
                lx += tw;
            }
            if (s.path.length > maxShow)
                DrawText(" > ...", lx, y + 2, kFontSm, kPath);
        } else {
            Row(x, y, "Result", kBad, "no route exists");
            DrawText("B is unreachable from A.", x, y, kFontMd, kMuted); y += kLineH;
            DrawText("(different component or wrong arc direction)", x, y, kFontSm, kMuted);
        }
    }

    EndScissorMode();
}

// -------------------------------------------------------------------------------------
// Search overlay (centred at top)
// -------------------------------------------------------------------------------------
void DrawSearch(const AppState& s, int sw) {
    const SearchBox& b = s.search;
    const int w  = 460, h = 44 + b.suggestionCount * 23;
    const int bx = (sw - w) / 2, by = 14;

    // Background
    Rectangle rect{ static_cast<float>(bx), static_cast<float>(by),
                    static_cast<float>(w),  static_cast<float>(h) };
    DrawRectangleRounded(rect, 0.08f, 6, Color{ 11, 15, 28, 248 });
    DrawRectangleRoundedLines(rect, 0.08f, 6, 1.5f, b.slot == 0 ? kGold : kPink);

    // Slot label + typed text + blinking caret
    Color slotCol = b.slot == 0 ? kGold : kPink;
    const char* slotLabel = b.slot == 0 ? "A:" : "B:";
    int lw = MeasureText(slotLabel, 16);
    DrawText(slotLabel, bx + 14, by + 14, 16, slotCol);
    bool caret = (static_cast<int>(GetTime() * 2.0) & 1) == 0;
    Txt(bx + 14 + lw + 8, by + 14, 16, kText, "%s%s", b.text, caret ? "|" : "");

    // Suggestion list
    for (int i = 0; i < b.suggestionCount; ++i) {
        int sy = by + 42 + i * 23;
        if (i == b.highlighted)
            DrawRectangle(bx + 2, sy, w - 4, 23,
                          Color{ 35, 55, 100, 220 });
        Color sc = i == b.highlighted ? kText : kMuted;
        Txt(bx + 14, sy + 4, kFontMd, sc, "%.52s", b.suggestions[i]->id.c_str());
    }

    // Hint line below the box
    DrawText("Enter: select   Tab: switch A / B   ↑ ↓: highlight   Esc: close",
             bx, by + h + 5, kFontSm, kMuted);
}

// -------------------------------------------------------------------------------------
// Overlay helper (dim background + centred panel)
// -------------------------------------------------------------------------------------
void DrawOverlayBox(int sw, int sh, int w, int h, const char* title, UiRect* out) {
    DrawRectangle(0, 0, sw, sh, Color{ 0, 0, 0, 155 });
    UiRect r = { (sw - w) * 0.5f, (sh - h) * 0.5f,
                 static_cast<float>(w), static_cast<float>(h) };
    Panel(r, title);
    *out = r;
}

// -------------------------------------------------------------------------------------
// Controls / help overlay
// -------------------------------------------------------------------------------------
void DrawHelp(int sw, int sh) {
    UiRect r;
    DrawOverlayBox(sw, sh, 660, 490, "CONTROLS  —  press H to close", &r);
    int x  = static_cast<int>(r.x) + 20;
    int y  = static_cast<int>(r.y) + 42;
    int vx = x + 240;  // value column

    const char* rows[][2] = {
        { "Left click node",       "select as source A — highlights k-hop ring" },
        { "Shift + left click",    "select as target B — runs Dijkstra route" },
        { "Left drag",             "orbit camera" },
        { "Right / middle drag",   "pan camera" },
        { "Scroll wheel",          "zoom in / out" },
        { "Click empty / Esc",     "clear selection" },
        { "↑ / ↓  or  1–9",        "change hop radius k" },
        { "D",                     "cycle traversal direction (both / out / in)" },
        { "/  or  Ctrl+F",         "search node by ID (O(1) hash lookup)" },
        { "C",                     "colour nodes by connected component" },
        { "T",                     "toggle node labels" },
        { "G",                     "toggle globe wireframe (geo datasets)" },
        { "F  /  Home",            "focus on selected node  /  reset view" },
        { "Delete",                "remove selected node (hash-table demo)" },
        { "F5",                    "reload dataset file" },
        { "L",                     "show load report (skipped / warned lines)" },
        { "F12",                   "save screenshot" },
        { "Drop a .txt file",      "drag onto window to open another dataset" },
    };
    for (auto& row : rows) {
        DrawText(row[0], x,  y, kFontMd, kAccent);
        DrawText(row[1], vx, y, kFontMd, kText);
        y += 23;
    }
}

// -------------------------------------------------------------------------------------
// Load-report overlay
// -------------------------------------------------------------------------------------
void DrawReport(const AppState& s, int sw, int sh) {
    UiRect r;
    DrawOverlayBox(sw, sh, 820, 500, "LOAD REPORT  —  press L to close", &r);
    const LoadReport& rep = s.report;
    int x = static_cast<int>(r.x) + 20;
    int y = static_cast<int>(r.y) + 42;

    // Summary row
    Txt(x, y, kFontMd, kText,
        "lines %d  |  nodes %d  |  edges %d  |  dup. nodes %d  |  malformed %d",
        rep.linesRead, rep.nodesLoaded, rep.edgesLoaded,
        rep.duplicateNodes, rep.malformedLines);
    y += 22;
    Txt(x, y, kFontMd, kText,
        "unknown endpoints %d  |  self-loops %d  |  redundant %d  |  bad weights %d  |  auto-placed %d",
        rep.missingEndpoints, rep.selfLoops, rep.redundantEdges,
        rep.badWeights, rep.autoPlaced);
    y += 32;

    if (rep.messages.size == 0) {
        DrawText("All lines parsed without issues.", x, y, kFontMd, kGood);
        EndScissorMode();
        return;
    }
    for (int i = 0; i < rep.messages.size && i < 18; ++i) {
        Txt(x,      y, kFontSm, kWarn, "line %-5d", rep.messages.data[i].line);
        Txt(x + 80, y, kFontSm, kText, "%.86s",     rep.messages.data[i].text);
        y += 20;
    }
    if (rep.messages.size > 18)
        Txt(x, y + 4, kFontSm, kMuted,
            "… %d more  (first %d problems stored)",
            rep.messages.size - 18, kMaxStoredMessages);
}

}  // namespace

// =====================================================================================
// Public API
// =====================================================================================

UiLayout UiComputeLayout(int sw, int sh) {
    UiLayout l;
    float ph = static_cast<float>(sh) - 56.0f;
    ph = ph < 200.0f ? 200.0f : (ph > 680.0f ? 680.0f : ph);
    l.left   = UiRect{ 10,                         10, kPanelW, ph };
    l.right  = UiRect{ static_cast<float>(sw) - kPanelW - 10, 10, kPanelW, ph };
    l.bottom = UiRect{ 0, static_cast<float>(sh) - 38, static_cast<float>(sw), 38 };
    return l;
}

static bool Inside(UiRect r, float x, float y) {
    return x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
}

bool UiPointOverPanel(const AppState& s, float x, float y, int sw, int sh) {
    UiLayout l = UiComputeLayout(sw, sh);
    if (s.showHelp || s.showReport) return true;
    return Inside(l.left, x, y) || Inside(l.right, x, y) || Inside(l.bottom, x, y);
}

void UiDraw(const AppState& s) {
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    UiLayout l = UiComputeLayout(sw, sh);

    if (s.graph.nodes.size == 0) {
        // Welcome / empty-state screen
        const char* t1 = "Drop a dataset .txt file onto this window";
        const char* t2 = "or run:   netviz data/sample_network.txt     netviz --synthetic 1000 42";
        int y0 = sh / 2 - 24;
        DrawText(t1, (sw - MeasureText(t1, 26)) / 2, y0,      26, kText);
        DrawText(t2, (sw - MeasureText(t2, 15)) / 2, y0 + 36, 15, kMuted);
    } else {
        DrawDatasetPanel(s, l.left);
        DrawSelectionPanel(s, l.right);
    }

    // ---- Bottom hint bar ----
    // Thin translucent strip — just the most essential shortcuts
    DrawRectangle(0, static_cast<int>(l.bottom.y), sw, 38,
                  Color{ 8, 11, 22, 230 });
    DrawLineEx({ 0, l.bottom.y }, { static_cast<float>(sw), l.bottom.y }, 1.0f, kSep);

    const char* hints =
        "Click: select A   Shift+click: route to B   Drag: orbit   Right-drag: pan"
        "   Scroll: zoom   /: search   C: colours   T: labels   H: help";
    DrawText(hints, 12, static_cast<int>(l.bottom.y) + 12, kFontSm, kMuted);

    // Transient status message (displayed for a few seconds after an action)
    if (s.now < s.statusUntil)
        DrawText(s.status, 12, static_cast<int>(l.bottom.y) - 22, kFontMd, kAccent);

    // ---- Overlays ----
    if (s.search.active) DrawSearch(s, sw);
    if (s.showHelp)      DrawHelp(sw, sh);
    if (s.showReport)    DrawReport(s, sw, sh);
}
