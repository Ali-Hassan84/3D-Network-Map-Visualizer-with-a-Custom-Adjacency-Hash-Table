// ui.cpp - NetViz 3D — 2D HUD, panels, search box, help and load-report overlays.
//
// Revision 3 — "instrument" HUD:
//   • Typography: Space Grotesk for labels / headings, JetBrains Mono for every metric,
//     node ID and histogram figure (monospace => tabular numerals, values never jitter).
//     Fonts are loaded lazily from assets/fonts/ at the exact pixel size they are drawn
//     at; if a file is missing the HUD silently falls back to raylib's default font.
//   • Surfaces: obsidian matte docks (rgba 10,12,16,0.82), 1 px hairline border, a 1 px
//     top-edge specular highlight and small corner reticle ticks.
//   • Palette: slate labels, off-white values, ice-cyan for hash metrics, amber reserved
//     for the selected node A and the route.
//   • Panels hug their content (height animates smoothly) so the 3D viewport stays clear.
//   • Bottom shortcut bar is a floating pill dock with keycaps; low-priority hints are
//     dropped when the window is too narrow.
#include "ui.h"
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include "raylib.h"

namespace {

// -------------------------------------------------------------------------------------
// Palette
// -------------------------------------------------------------------------------------
const Color kSurface   = {  10,  12,  16, 209 };  // rgba(10,12,16,0.82)
const Color kSurfaceHi = {  14,  17,  23, 240 };  // denser surface for overlays / tooltips
const Color kHairline  = { 255, 255, 255,  20 };  // 0.08 border
const Color kDivider   = { 255, 255, 255,  15 };  // 0.06 dividers
const Color kSpecular  = { 255, 255, 255,  26 };  // 0.10 top-edge highlight
const Color kReticle   = { 148, 163, 184,  90 };  // corner ticks
const Color kSlate     = { 148, 163, 184, 255 };  // #94A3B8 labels
const Color kSlateDim  = { 100, 116, 139, 255 };  // #64748B units / axis
const Color kValue     = { 248, 250, 252, 255 };  // #F8FAFC values
const Color kCyan      = {  56, 189, 248, 255 };  // #38BDF8 hash metrics
const Color kAmber     = { 245, 158,  11, 255 };  // #F59E0B selection A + route
const Color kTargetB   = { 240,  70, 190, 255 };  // matches renderer colour for node B
const Color kAlert     = { 248, 113, 113, 255 };  // muted rose for real errors only

Color Alpha(Color c, float a) {
    c.a = static_cast<unsigned char>(c.a * (a < 0 ? 0 : (a > 1 ? 1 : a)));
    return c;
}

// -------------------------------------------------------------------------------------
// Metrics
// -------------------------------------------------------------------------------------
const float kPanelW   = 340.0f;
const int   kPad      = 16;     // panel inner padding
const int   kHeaderH  = 38;     // panel title strip
const int   kRowH     = 22;     // key / value row
const float kRadiusPx = 6.0f;   // corner radius in pixels

// -------------------------------------------------------------------------------------
// Fonts — lazily loaded, one atlas per (face, pixel size) so glyphs are rasterised at
// exactly the size they are drawn at (crisp, no bilinear smearing).
// -------------------------------------------------------------------------------------
enum Face { SANS = 0, SANS_MED, MONO, MONO_MED, FACE_COUNT };
const char* kFaceFile[FACE_COUNT] = {
    "SpaceGrotesk-Regular.ttf", "SpaceGrotesk-Medium.ttf",
    "JetBrainsMono-Regular.ttf", "JetBrainsMono-Medium.ttf",
};

struct FontSlot { int face; int size; bool custom; Font font; };
const int kMaxSlots = 24;
FontSlot g_fonts[kMaxSlots];
int      g_fontCount = 0;
bool     g_faceMissing[FACE_COUNT] = { false, false, false, false };

const char* FindFontFile(const char* name) {
    static char path[512];
    const char* dirs[] = { "assets/fonts/", "../assets/fonts/", nullptr };
    for (int i = 0; dirs[i]; ++i) {
        std::snprintf(path, sizeof(path), "%s%s", dirs[i], name);
        if (FileExists(path)) return path;
    }
    std::snprintf(path, sizeof(path), "%sassets/fonts/%s", GetApplicationDirectory(), name);
    if (FileExists(path)) return path;
    return nullptr;
}

Font GetFace(int face, int size) {
    for (int i = 0; i < g_fontCount; ++i)
        if (g_fonts[i].face == face && g_fonts[i].size == size) return g_fonts[i].font;
    if (g_fontCount >= kMaxSlots || g_faceMissing[face]) return GetFontDefault();

    // ASCII + the handful of typographic symbols the HUD uses.
    int cps[128];
    int n = 0;
    for (int c = 32; c < 127; ++c) cps[n++] = c;
    const int extra[] = { 0x00B5 /*µ*/, 0x00D7 /*×*/, 0x00B7 /*·*/, 0x2013 /*–*/,
                          0x2014 /*—*/, 0x2026 /*…*/, 0x2190, 0x2191, 0x2192, 0x2193,
                          0x2194 /*← ↑ → ↓ ↔*/, 0x21E7 /*⇧*/, 0x203A /*›*/, 0x25B8 /*▸*/ };
    for (int e : extra) cps[n++] = e;

    FontSlot& slot = g_fonts[g_fontCount++];
    slot.face = face;
    slot.size = size;
    slot.custom = false;
    slot.font = GetFontDefault();

    const char* file = FindFontFile(kFaceFile[face]);
    if (!file) {
        g_faceMissing[face] = true;
        return slot.font;
    }
    Font f = LoadFontEx(file, size, cps, n);
    if (f.texture.id != 0) {
        SetTextureFilter(f.texture, TEXTURE_FILTER_BILINEAR);
        slot.font = f;
        slot.custom = true;
    }
    return slot.font;
}

// Letter-spacing (tracking) per face — tight for mono, slightly open for caps headings.
float Tracking(int face, int size) {
    if (face == MONO || face == MONO_MED) return 0.0f;
    return size <= 11 ? 1.2f : 0.2f;
}

// Default font needs size >= 10 and spacing ~ size/10 to stay legible.
float SpacingFor(Font f, int face, int size) {
    if (f.texture.id == GetFontDefault().texture.id) return size / 10.0f;
    return Tracking(face, size);
}

void Text(int face, int size, float x, float y, Color c, const char* s) {
    Font f = GetFace(face, size);
    DrawTextEx(f, s, { std::floor(x), std::floor(y) }, static_cast<float>(size),
               SpacingFor(f, face, size), c);
}

float TextW(int face, int size, const char* s) {
    Font f = GetFace(face, size);
    return MeasureTextEx(f, s, static_cast<float>(size), SpacingFor(f, face, size)).x;
}

void TextF(int face, int size, float x, float y, Color c, const char* fmt, ...) {
    char buf[280];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    Text(face, size, x, y, c, buf);
}

void TextRight(int face, int size, float right, float y, Color c, const char* s) {
    Text(face, size, right - TextW(face, size, s), y, c, s);
}

// -------------------------------------------------------------------------------------
// Surface primitives
// -------------------------------------------------------------------------------------
float Roundness(float w, float h, float radiusPx) {
    float m = w < h ? w : h;
    if (m <= 0) return 0;
    float r = 2.0f * radiusPx / m;
    return r > 1.0f ? 1.0f : r;
}

void RoundRect(Rectangle r, float radiusPx, Color c) {
    DrawRectangleRounded(r, Roundness(r.width, r.height, radiusPx), 8, c);
}

void RoundRectLine(Rectangle r, float radiusPx, Color c) {
    DrawRectangleRoundedLinesEx(r, Roundness(r.width, r.height, radiusPx), 8, 1.0f, c);
}

void HLine(float x0, float x1, float y, Color c) {
    DrawRectangle(static_cast<int>(x0), static_cast<int>(y),
                  static_cast<int>(x1 - x0), 1, c);
}

void VLine(float x, float y0, float y1, Color c) {
    DrawRectangle(static_cast<int>(x), static_cast<int>(y0),
                  1, static_cast<int>(y1 - y0), c);
}

// Obsidian glass surface: matte fill, hairline border, top-edge specular highlight.
void Glass(Rectangle r, float radiusPx, Color fill) {
    RoundRect(r, radiusPx, fill);
    RoundRectLine(r, radiusPx, kHairline);
    HLine(r.x + radiusPx, r.x + r.width - radiusPx, r.y + 1, kSpecular);
}

// Small L-shaped reticle ticks just outside each corner (telemetry framing).
void Reticles(Rectangle r) {
    const float o = 4.0f, len = 7.0f;
    const float l = r.x - o, t = r.y - o, rt = r.x + r.width + o - 1, b = r.y + r.height + o - 1;
    HLine(l, l + len, t, kReticle);           VLine(l, t, t + len, kReticle);
    HLine(rt - len + 1, rt + 1, t, kReticle); VLine(rt, t, t + len, kReticle);
    HLine(l, l + len, b, kReticle);           VLine(l, b - len + 1, b + 1, kReticle);
    HLine(rt - len + 1, rt + 1, b, kReticle); VLine(rt, b - len + 1, b + 1, kReticle);
}

// A realistic keycap: dark skirt, lighter face, inner top highlight, hairline edge.
// Returns the cap width.
float Keycap(float x, float y, const char* label, int size = 11) {
    const float padX = 6.0f, h = static_cast<float>(size) + 9.0f;
    float tw = TextW(MONO_MED, size, label);
    float w = tw + padX * 2 < h ? h : tw + padX * 2;
    RoundRect({ x, y + 1, w, h }, 4, Color{ 4, 5, 8, 255 });           // skirt / shadow
    RoundRect({ x, y, w, h - 1.5f }, 4, Color{ 30, 35, 45, 255 });     // key face
    RoundRectLine({ x, y, w, h }, 4, Color{ 255, 255, 255, 22 });
    HLine(x + 3, x + w - 3, y + 1, Color{ 255, 255, 255, 30 });         // top bevel
    Text(MONO_MED, size, x + (w - tw) * 0.5f, y + 4, Color{ 226, 232, 240, 255 }, label);
    return w;
}

// Draws a chord like "Shift+Click" or alternatives like "/,Ctrl+F" as keycaps.
float KeyCombo(float x, float y, const char* spec, int size = 11) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s", spec);
    float cx = x;
    char* alt = buf;
    bool firstAlt = true;
    while (alt) {
        char* nextAlt = std::strchr(alt, ',');
        if (nextAlt) *nextAlt++ = '\0';
        if (!firstAlt) {
            Text(MONO, size, cx + 3, y + 4, kSlateDim, "/");
            cx += TextW(MONO, size, "/") + 6;
        }
        firstAlt = false;
        char* key = alt;
        bool firstKey = true;
        while (key) {
            char* nextKey = std::strchr(key, '+');
            if (nextKey) *nextKey++ = '\0';
            if (!firstKey) {
                Text(MONO, size, cx + 2, y + 4, kSlateDim, "+");
                cx += TextW(MONO, size, "+") + 4;
            }
            firstKey = false;
            cx += Keycap(cx, y, key, size) + 2;
            key = nextKey;
        }
        alt = nextAlt;
    }
    return cx - x;
}

// Ease a value toward a target with frame-rate independent smoothing.
float Approach(float cur, float target, float speed) {
    float dt = GetFrameTime();
    if (dt > 0.1f) dt = 0.1f;
    float k = 1.0f - std::exp(-speed * dt);
    return cur + (target - cur) * k;
}

bool MouseIn(Rectangle r) { return CheckCollisionPointRec(GetMousePosition(), r); }

// -------------------------------------------------------------------------------------
// Formatting
// -------------------------------------------------------------------------------------
const char* Duration(double ns) {
    static char ring[6][32];
    static int idx = 0;
    char* b = ring[idx++ % 6];
    if (ns < 1000.0)     std::snprintf(b, 32, "%.0f ns", ns);
    else if (ns < 1.0e6) std::snprintf(b, 32, "%.1f µs", ns / 1e3);
    else                 std::snprintf(b, 32, "%.2f ms", ns / 1e6);
    return b;
}

// Thousands separators: 1204567 -> "1,204,567".
const char* Group(long long v) {
    static char ring[6][32];
    static int idx = 0;
    char* out = ring[idx++ % 6];
    char raw[32];
    std::snprintf(raw, sizeof(raw), "%lld", v < 0 ? -v : v);
    int len = static_cast<int>(std::strlen(raw));
    int o = 0;
    if (v < 0) out[o++] = '-';
    for (int i = 0; i < len; ++i) {
        out[o++] = raw[i];
        int rem = len - 1 - i;
        if (rem > 0 && rem % 3 == 0) out[o++] = ',';
    }
    out[o] = '\0';
    return out;
}

const char* DirName(Direction d) {
    return d == DIR_BOTH ? "both" : (d == DIR_OUT ? "outgoing" : "incoming");
}

// -------------------------------------------------------------------------------------
// Panel layout cursor
// -------------------------------------------------------------------------------------
struct Cursor {
    float x, right;   // inner content bounds
    float y;
};

// Panel chrome: glass surface, reticles, title strip with optional right meta text.
void PanelChrome(UiRect r, const char* title) {
    Rectangle rect{ r.x, r.y, r.w, r.h };
    Glass(rect, kRadiusPx, kSurface);
    Reticles(rect);
    // Title marker: tiny filled square, then the tracked uppercase title.
    DrawRectangle(static_cast<int>(r.x) + kPad, static_cast<int>(r.y) + 17, 4, 4, kSlate);
    Text(SANS_MED, 12, r.x + kPad + 12, r.y + 13, kValue, title);
    HLine(r.x + 1, r.x + r.w - 1, r.y + kHeaderH, kDivider);
}

// Section label: small tracked caps + optional right-aligned mono annotation.
void Section(Cursor& c, const char* label, const char* note = nullptr, bool divider = true) {
    if (divider) {
        c.y += 6;
        HLine(c.x - kPad + 1, c.right + kPad - 1, c.y, kDivider);
        c.y += 12;
    } else {
        c.y += 12;
    }
    Text(SANS_MED, 11, c.x, c.y, kSlateDim, label);
    if (note) TextRight(MONO, 11, c.right, c.y, kSlateDim, note);
    c.y += 22;
}

// Key / value row: slate label left, mono value right-aligned, optional dim unit suffix.
void Row(Cursor& c, const char* label, Color vc, const char* unit, const char* fmt, ...) {
    char buf[200];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    Text(SANS, 13, c.x, c.y + 1, kSlate, label);
    float right = c.right;
    if (unit && *unit) {
        float uw = TextW(MONO, 12, unit);
        Text(MONO, 12, right - uw, c.y + 2, kSlateDim, unit);
        right -= uw + 6;
    }
    TextRight(MONO, 13, right, c.y + 1, vc, buf);
    c.y += kRowH;
}

// -------------------------------------------------------------------------------------
// Animated state (plain statics — UI-only, never read by the engine)
// -------------------------------------------------------------------------------------
float g_panelH[2]     = { 0, 0 };   // animated panel heights (left, right)
float g_contentH[2]   = { 0, 0 };   // last measured content heights
float g_barHover[8]   = { 0 };
float g_linkHover[7]  = { 0 };
float g_lfGauge       = -1.0f;

struct Tooltip { bool show; float x, y; char text[96]; };

void DrawTooltip(const Tooltip& t) {
    if (!t.show) return;
    float tw = TextW(MONO, 11, t.text);
    float w = tw + 16, h = 22;
    float x = t.x - w * 0.5f, y = t.y - h - 6;
    Glass({ x, y, w, h }, 4, kSurfaceHi);
    Text(MONO, 11, x + 8, y + 5, kValue, t.text);
}

// -------------------------------------------------------------------------------------
// Dataset / hash-table panel (left)
// -------------------------------------------------------------------------------------
void DrawDatasetPanel(const AppState& s, UiRect r) {
    PanelChrome(r, "DATASET & HASH TABLE");
    TextRight(MONO, 11, r.x + r.w - kPad, r.y + 14, kSlateDim, "FNV-1a");

    BeginScissorMode(static_cast<int>(r.x), static_cast<int>(r.y) + kHeaderH + 1,
                     static_cast<int>(r.w), static_cast<int>(r.h) - kHeaderH - 1);

    Cursor c{ r.x + kPad, r.x + r.w - kPad, r.y + kHeaderH };
    const Graph& g = s.graph;
    Tooltip tip{};

    // --- Dataset ---
    Section(c, "DATASET", nullptr, false);
    char lbl[40];
    std::snprintf(lbl, sizeof(lbl), "%.26s",
                  s.sourceLabel.empty() ? "(none)" : s.sourceLabel.c_str());
    Row(c, "Source",      kValue, nullptr, "%s", lbl);
    Row(c, "Nodes",       kValue, nullptr, "%s", Group(g.nodes.size));
    Row(c, "Arcs",        kValue, nullptr, "%s", Group(g.arcCount));
    Row(c, "Components",  kValue, nullptr, "%s  ·  max %s",
        Group(s.comps.count), Group(s.comps.largest));
    Row(c, "Load time",   kValue, "ms", "%.1f", s.report.loadMs);
    int warn = LoadReportWarningCount(s.report);
    if (warn > 0) Row(c, "Warnings", kAlert, "[L]", "%d", warn);
    else          Row(c, "Warnings", kSlate, nullptr, "none");
    Row(c, "Render mode", kValue, nullptr, "%s", s.largeMode ? "neighbourhood" : "full graph");

    // --- Hash table ---
    const HashTableStats& t = s.tableStats;
    Section(c, "HASH TABLE", "separate chaining");
    Row(c, "Buckets / items", kValue, nullptr, "%s / %s",
        Group(static_cast<long long>(t.capacity)), Group(static_cast<long long>(t.count)));

    // Load-factor row + precision gauge with the 0.75 rehash threshold marked.
    Row(c, "Load factor", t.loadFactor <= 0.75 ? kCyan : kAlert, "/ 0.75", "%.3f", t.loadFactor);
    {
        float target = static_cast<float>(t.loadFactor / 1.0);
        if (target > 1.0f) target = 1.0f;
        g_lfGauge = g_lfGauge < 0 ? target : Approach(g_lfGauge, target, 10.0f);
        float gx = c.x, gw = c.right - c.x, gy = c.y - 3;
        DrawRectangle(static_cast<int>(gx), static_cast<int>(gy), static_cast<int>(gw), 2,
                      Color{ 255, 255, 255, 14 });
        DrawRectangle(static_cast<int>(gx), static_cast<int>(gy),
                      static_cast<int>(gw * g_lfGauge), 2,
                      t.loadFactor <= 0.75 ? kCyan : kAlert);
        VLine(gx + gw * 0.75f, gy - 3, gy + 5, kSlate);
        c.y += 8;
    }
    Row(c, "Resizes",        kValue, "×2", "%zu", t.resizeCount);
    Row(c, "Chain avg / peak", kCyan, nullptr, "%.2f / %zu", t.avgChainUsed, t.maxChain);
    Row(c, "Probes per hit", kCyan, nullptr, "%.2f", t.avgProbesHit);
    Row(c, "Empty buckets",  kValue, "%", "%.1f",
        t.capacity ? 100.0 * t.emptyBuckets / t.capacity : 0.0);

    // --- Chain length histogram (log scale) ---
    c.y += 10;
    Text(SANS_MED, 11, c.x, c.y, kSlateDim, "CHAIN LENGTH");
    TextRight(MONO, 11, c.right, c.y, kSlateDim, "log scale");
    c.y += 20;
    {
        size_t peak = 1;
        for (int i = 0; i < 8; ++i) if (t.histogram[i] > peak) peak = t.histogram[i];
        const float chartH = 56.0f, gap = 6.0f;
        const float axisX = c.x + 22;  // room for y-axis labels
        const float bw = (c.right - axisX - gap * 7) / 8.0f;
        const float top = c.y, base = c.y + chartH;

        // Light grid at 50 % and 100 % + y labels.
        HLine(axisX, c.right, top, Color{ 255, 255, 255, 8 });
        HLine(axisX, c.right, top + chartH * 0.5f, Color{ 255, 255, 255, 8 });
        Text(MONO, 10, c.x, top - 4, kSlateDim, "max");
        Text(MONO, 10, c.x, base - 7, kSlateDim, "0");

        for (int i = 0; i < 8; ++i) {
            double v = static_cast<double>(t.histogram[i]);
            float h = static_cast<float>(std::log10(1.0 + v) /
                                         std::log10(1.0 + static_cast<double>(peak)) * chartH);
            if (v > 0 && h < 2) h = 2;
            float bx = axisX + i * (bw + gap);
            Rectangle hit{ bx - gap * 0.5f, top, bw + gap, chartH + 18 };
            bool hov = MouseIn(hit);
            g_barHover[i] = Approach(g_barHover[i], hov ? 1.0f : 0.0f, 18.0f);

            Color bar = i == 0 ? Alpha(kSlate, 0.35f + 0.35f * g_barHover[i])
                               : Alpha(kCyan, 0.55f + 0.45f * g_barHover[i]);
            if (g_barHover[i] > 0.01f)
                DrawRectangle(static_cast<int>(bx), static_cast<int>(top),
                              static_cast<int>(bw), static_cast<int>(chartH),
                              Color{ 255, 255, 255, static_cast<unsigned char>(8 * g_barHover[i]) });
            DrawRectangle(static_cast<int>(bx), static_cast<int>(base - h),
                          static_cast<int>(bw), static_cast<int>(h), bar);
            if (h >= 2)  // crisp cap line
                HLine(bx, bx + bw, base - h, i == 0 ? kSlate : Color{ 186, 230, 253, 255 });

            char lab[4];
            std::snprintf(lab, sizeof(lab), i == 7 ? "7+" : "%d", i);
            float lw = TextW(MONO, 11, lab);
            Text(MONO, 11, bx + (bw - lw) * 0.5f, base + 6, hov ? kValue : kSlateDim, lab);

            if (hov) {
                tip.show = true;
                tip.x = bx + bw * 0.5f;
                tip.y = base - h;
                std::snprintf(tip.text, sizeof(tip.text), "len %s · %s bucket%s",
                              i == 7 ? "7+" : lab, Group(static_cast<long long>(t.histogram[i])),
                              t.histogram[i] == 1 ? "" : "s");
            }
        }
        HLine(axisX, c.right, base, Color{ 255, 255, 255, 40 });  // baseline axis
        c.y = base + 24;
    }

    // --- Lookup benchmark ---
    Section(c, "LOOKUP", "last selected");
    if (s.m.hasLookup) {
        char probes[16];
        std::snprintf(probes, sizeof(probes), "%dp", s.m.lookupProbes);
        Row(c, "Hash find",   kCyan,  probes, "%s", Duration(s.m.lookupNs));
        Row(c, "Linear scan", kValue, nullptr, "%s", Duration(s.m.scanNs));
        double ratio = s.m.lookupNs > 0 ? s.m.scanNs / s.m.lookupNs : 0.0;
        Row(c, "Speed-up", ratio >= 1.0 ? kCyan : kSlate, nullptr,
            ratio < 10.0 ? "×%.1f" : "×%.0f", ratio);
    } else {
        Text(SANS, 13, c.x, c.y + 1, kSlateDim, "Select a node to benchmark lookup.");
        c.y += kRowH;
    }
    if (s.m.hasPick)
        Row(c, "Ray cast", kValue, nullptr, "%s", Duration(s.m.pickUs * 1000.0));

    EndScissorMode();
    g_contentH[0] = c.y + 8 - r.y;
    DrawTooltip(tip);
}

// -------------------------------------------------------------------------------------
// Selection / route panel (right)
// -------------------------------------------------------------------------------------
void DrawSelectionPanel(const AppState& s, UiRect r) {
    PanelChrome(r, "SELECTION & ROUTE");
    {
        char fps[16];
        std::snprintf(fps, sizeof(fps), "%d", GetFPS());
        float uw = TextW(MONO, 11, " FPS");
        TextRight(MONO, 11, r.x + r.w - kPad, r.y + 14, kSlateDim, " FPS");
        TextRight(MONO, 11, r.x + r.w - kPad - uw, r.y + 14, kValue, fps);
    }

    BeginScissorMode(static_cast<int>(r.x), static_cast<int>(r.y) + kHeaderH + 1,
                     static_cast<int>(r.w), static_cast<int>(r.h) - kHeaderH - 1);

    Cursor c{ r.x + kPad, r.x + r.w - kPad, r.y + kHeaderH };

    if (!s.a) {
        c.y += 16;
        Text(SANS, 14, c.x, c.y, kValue, "No node selected");
        c.y += 26;
        const char* hints[][2] = {
            { "Click",       "inspect a node" },
            { "Shift+Click", "route to a second node" },
            { "/",           "search by ID" },
        };
        for (auto& h : hints) {
            float kw = KeyCombo(c.x, c.y, h[0]);
            Text(SANS, 13, c.x + kw + 8, c.y + 2, kSlate, h[1]);
            c.y += 28;
        }
        EndScissorMode();
        g_contentH[1] = c.y + 6 - r.y;
        for (float& v : g_linkHover) v = 0;
        return;
    }

    const Node* a = s.a;

    // --- Source A ---
    Section(c, "SOURCE", "A", false);
    // ID row with an amber swatch so it reads as the active selection.
    {
        DrawCircle(static_cast<int>(c.x) + 3, static_cast<int>(c.y) + 9, 3.0f, kAmber);
        Text(SANS, 13, c.x + 12, c.y + 1, kSlate, "ID");
        char id[40];
        std::snprintf(id, sizeof(id), "%.28s", a->id.c_str());
        TextRight(MONO_MED, 13, c.right, c.y + 1, kAmber, id);
        c.y += kRowH;
    }
    Row(c, "Position", kValue, nullptr, "%.1f  %.1f  %.1f", a->pos.x, a->pos.y, a->pos.z);
    Row(c, "Degree out / in", kValue, nullptr, "%d / %d", a->out.size, a->in.size);
    Row(c, "Neighbours", kValue, nullptr, "%d", AppDistinctNeighbors(a));
    if (a->component >= 0 && a->component < s.comps.sizes.size)
        Row(c, "Component", kValue, nullptr, "#%d  ·  %s nodes",
            a->component, Group(s.comps.sizes.data[a->component]));

    // --- K-hop ---
    Section(c, "K-HOP NEIGHBOURHOOD", "BFS");
    Row(c, "Hop radius",  kValue, "[↑↓]", "k = %d", s.k);
    Row(c, "Direction",   kValue, "[D]", "%s", DirName(s.dir));
    Row(c, "Reached",     kValue, nullptr, "%s  ·  depth %d", Group(s.khop.count), s.khop.maxHop);
    Row(c, "BFS time",    kValue, nullptr, "%s", Duration(s.m.bfsUs * 1000.0));

    // --- Direct links ---
    {
        char note[24];
        std::snprintf(note, sizeof(note), "%d out", a->out.size);
        Section(c, "DIRECT LINKS", note);
    }
    const int kMaxLinks = 7;
    int shown = 0;
    for (int i = 0; i < a->out.size && shown < kMaxLinks; ++i, ++shown) {
        const Edge& e = a->out.data[i];
        Rectangle row{ c.x - 6, c.y - 2, c.right - c.x + 12, 24 };
        bool hov = MouseIn(row);
        g_linkHover[shown] = Approach(g_linkHover[shown], hov ? 1.0f : 0.0f, 16.0f);
        float hv = g_linkHover[shown];
        if (hv > 0.01f) {
            RoundRect(row, 4, Color{ 255, 255, 255, static_cast<unsigned char>(12 * hv) });
            DrawRectangle(static_cast<int>(row.x), static_cast<int>(row.y) + 5, 2, 14,
                          Alpha(kSlate, hv));
        }
        float nudge = 2.0f * hv;
        Text(MONO, 12, c.x + nudge, c.y + 3, kSlateDim, e.undirectedLink ? "↔" : "→");
        char id[32];
        std::snprintf(id, sizeof(id), "%.22s", e.to->id.c_str());
        Text(MONO, 12, c.x + 18 + nudge, c.y + 3, hov ? kValue : Color{ 203, 213, 225, 255 }, id);

        // Latency pill
        char ms[24];
        std::snprintf(ms, sizeof(ms), "%.1f ms", e.weight);
        float tw = TextW(MONO, 11, ms);
        Rectangle pill{ c.right - tw - 14, c.y + 1, tw + 14, 18 };
        RoundRect(pill, 9, Color{ 255, 255, 255, static_cast<unsigned char>(10 + 8 * hv) });
        RoundRectLine(pill, 9, Color{ 255, 255, 255, static_cast<unsigned char>(18 + 14 * hv) });
        Text(MONO, 11, pill.x + 7, pill.y + 3, hov ? kValue : kSlate, ms);
        c.y += 26;
    }
    for (int i = shown; i < kMaxLinks; ++i) g_linkHover[i] = 0;
    if (a->out.size > shown) {
        TextF(MONO, 11, c.x, c.y + 2, kSlateDim, "+ %d more", a->out.size - shown);
        c.y += 20;
    } else if (a->out.size == 0) {
        Text(SANS, 13, c.x, c.y + 1, kSlateDim, "No outgoing links.");
        c.y += kRowH;
    }

    // --- Route A -> B ---
    if (s.b) {
        Section(c, "ROUTE", "Dijkstra");
        {
            DrawCircleLines(static_cast<int>(c.x) + 3, static_cast<int>(c.y) + 9, 3.0f, kTargetB);
            Text(SANS, 13, c.x + 12, c.y + 1, kSlate, "Target B");
            char id[40];
            std::snprintf(id, sizeof(id), "%.26s", s.b->id.c_str());
            TextRight(MONO, 13, c.right, c.y + 1, kValue, id);
            c.y += kRowH;
        }
        if (s.path.found) {
            Row(c, "Total latency", kAmber, "ms", "%.2f", s.path.totalWeight);
            Row(c, "Hops",          kValue, nullptr, "%d", s.path.length - 1);
            Row(c, "Settled",       kValue, nullptr, "%s / %s",
                Group(s.path.settled), Group(s.graph.nodes.size));
            Row(c, "Search time",   kValue, nullptr, "%s", Duration(s.m.dijkstraUs * 1000.0));

            // Path chain: amber node tokens separated by dim chevrons, wrapped.
            c.y += 6;
            float lx = c.x;
            const int maxShow = s.path.length < 14 ? s.path.length : 14;
            const float sepW = TextW(MONO, 11, " › ");
            for (int i = 0; i < maxShow; ++i) {
                char tok[24];
                std::snprintf(tok, sizeof(tok), "%.14s",
                              s.graph.nodes.data[s.path.nodes[i]]->id.c_str());
                float tw = TextW(MONO, 11, tok) + (i ? sepW : 0);
                if (lx + tw > c.right && lx > c.x) { lx = c.x; c.y += 18; }
                if (i && lx > c.x) {
                    Text(MONO, 11, lx, c.y, kSlateDim, " › ");
                    lx += sepW;
                }
                Text(MONO, 11, lx, c.y, kAmber, tok);
                lx += TextW(MONO, 11, tok);
            }
            if (s.path.length > maxShow) Text(MONO, 11, lx, c.y, kSlateDim, " › …");
            c.y += 20;
        } else {
            Row(c, "Result", kAlert, nullptr, "no route");
            Text(SANS, 12, c.x, c.y + 1, kSlateDim, "B is unreachable from A — different");
            c.y += 18;
            Text(SANS, 12, c.x, c.y + 1, kSlateDim, "component or arc direction.");
            c.y += 20;
        }
    }

    EndScissorMode();
    g_contentH[1] = c.y + 6 - r.y;
}

// -------------------------------------------------------------------------------------
// Bottom dock (floating pill with keycaps)
// -------------------------------------------------------------------------------------
struct DockItem { const char* keys; const char* label; };
const DockItem kDock[] = {        // ordered by priority; tail items drop first
    { "Click",       "select" },
    { "Shift+Click", "route" },
    { "/",           "search" },
    { "H",           "help" },
    { "Drag",        "orbit" },
    { "Scroll",      "zoom" },
    { "RMB",         "pan" },
    { "C",           "colours" },
    { "T",           "labels" },
};
const int kDockCount = sizeof(kDock) / sizeof(kDock[0]);
const float kDockH = 38.0f, kDockGap = 18.0f, kDockPadX = 14.0f;

float ComboWidth(const char* spec) {
    // Mirrors KeyCombo/Keycap geometry without drawing.
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s", spec);
    float w = 0;
    char* key = buf;
    bool first = true;
    while (key) {
        char* next = std::strchr(key, '+');
        if (next) *next++ = '\0';
        if (!first) w += TextW(MONO, 11, "+") + 4;
        first = false;
        float tw = TextW(MONO_MED, 11, key);
        float cw = tw + 12 < 20 ? 20 : tw + 12;
        w += cw + 2;
        key = next;
    }
    return w;
}

float DockItemWidth(int i) {
    return ComboWidth(kDock[i].keys) + 4 + TextW(SANS, 12, kDock[i].label);
}

// How many dock items fit between the side panels, and the resulting pill width.
int DockFit(int sw, float* outW) {
    float avail = static_cast<float>(sw) - 2.0f * (kPanelW + 30.0f);
    if (avail < 260.0f) avail = static_cast<float>(sw) - 40.0f;
    float w = kDockPadX * 2;
    int n = 0;
    for (int i = 0; i < kDockCount; ++i) {
        float add = DockItemWidth(i) + (n ? kDockGap : 0);
        if (w + add > avail) break;
        w += add;
        ++n;
    }
    *outW = w;
    return n;
}

void DrawDock(UiRect r, int count) {
    Rectangle rect{ r.x, r.y, r.w, r.h };
    Glass(rect, r.h * 0.5f, kSurface);
    float x = r.x + kDockPadX;
    float y = r.y + (r.h - 20.0f) * 0.5f;
    for (int i = 0; i < count; ++i) {
        if (i) {
            DrawCircle(static_cast<int>(x + kDockGap * 0.5f - 1), static_cast<int>(r.y + r.h * 0.5f),
                       1.2f, Color{ 255, 255, 255, 40 });
            x += kDockGap;
        }
        x += KeyCombo(x, y, kDock[i].keys) + 4;
        Text(SANS, 12, x, y + 3, kSlate, kDock[i].label);
        x += TextW(SANS, 12, kDock[i].label);
    }
}

// -------------------------------------------------------------------------------------
// Search overlay (centred at top)
// -------------------------------------------------------------------------------------
void DrawSearch(const AppState& s, int sw) {
    const SearchBox& b = s.search;
    const float w = 480, inputH = 46, rowH = 28;
    const float h = inputH + (b.suggestionCount ? b.suggestionCount * rowH + 8 : 0);
    const float bx = std::floor((sw - w) * 0.5f), by = 16;
    Rectangle rect{ bx, by, w, h };
    Glass(rect, 8, kSurfaceHi);
    Reticles(rect);

    // Slot chip (A = amber, B = target colour)
    Color slotCol = b.slot == 0 ? kAmber : kTargetB;
    const char* slotLabel = b.slot == 0 ? "A" : "B";
    Rectangle chip{ bx + 12, by + 12, 26, 22 };
    RoundRect(chip, 5, Alpha(slotCol, 0.14f));
    RoundRectLine(chip, 5, Alpha(slotCol, 0.55f));
    Text(MONO_MED, 13, chip.x + 9, chip.y + 4, slotCol, slotLabel);

    // Typed text + blinking caret
    float tx = chip.x + chip.width + 12;
    Text(MONO, 15, tx, by + 15, kValue, b.text);
    float caretX = tx + TextW(MONO, 15, b.text) + 2;
    float blink = 0.5f + 0.5f * std::cos(static_cast<float>(GetTime()) * 6.0f);
    DrawRectangle(static_cast<int>(caretX), static_cast<int>(by) + 13, 1, 20, Alpha(slotCol, blink));
    if (b.text[0] == '\0') Text(SANS, 14, caretX + 6, by + 15, kSlateDim, "Search node ID…");

    TextRight(MONO, 11, bx + w - 14, by + 17, kSlateDim, "O(1) lookup");

    if (b.suggestionCount) {
        HLine(bx + 1, bx + w - 1, by + inputH, kDivider);
        for (int i = 0; i < b.suggestionCount; ++i) {
            float sy = by + inputH + 4 + i * rowH;
            bool hi = i == b.highlighted;
            if (hi) {
                RoundRect({ bx + 6, sy, w - 12, rowH - 2 }, 4, Color{ 255, 255, 255, 14 });
                DrawRectangle(static_cast<int>(bx) + 6, static_cast<int>(sy) + 7, 2, 12, slotCol);
            }
            TextF(MONO, 13, bx + 18, sy + 6, hi ? kValue : kSlate, "%.52s",
                  b.suggestions[i]->id.c_str());
            if (hi) TextRight(MONO, 11, bx + w - 16, sy + 7, kSlateDim, "Enter");
        }
    }

    // Hint row under the box, with keycaps
    float hx = bx + 2, hy = by + h + 10;
    const char* hints[][2] = { { "Enter", "select" }, { "Tab", "A / B" },
                               { "↑,↓", "move" }, { "Esc", "close" } };
    for (auto& hnt : hints) {
        hx += KeyCombo(hx, hy, hnt[0], 10) + 6;
        Text(SANS, 12, hx - 2, hy + 2, kSlate, hnt[1]);
        hx += TextW(SANS, 12, hnt[1]) + 14;
    }
}

// -------------------------------------------------------------------------------------
// Overlay helper (dim background + centred glass panel)
// -------------------------------------------------------------------------------------
UiRect DrawOverlayBox(int sw, int sh, int w, int h, const char* title, const char* closeKey) {
    DrawRectangle(0, 0, sw, sh, Color{ 2, 3, 6, 150 });
    UiRect r = { std::floor((sw - w) * 0.5f), std::floor((sh - h) * 0.5f),
                 static_cast<float>(w), static_cast<float>(h) };
    Rectangle rect{ r.x, r.y, r.w, r.h };
    Glass(rect, 8, kSurfaceHi);
    Reticles(rect);
    DrawRectangle(static_cast<int>(r.x) + kPad + 2, static_cast<int>(r.y) + 17, 4, 4, kSlate);
    Text(SANS_MED, 12, r.x + kPad + 14, r.y + 13, kValue, title);
    float cw = ComboWidth(closeKey);
    float cx = r.x + r.w - kPad - cw - TextW(SANS, 12, "close") - 6;
    cx += KeyCombo(cx, r.y + 10, closeKey) + 6;
    Text(SANS, 12, cx - 2, r.y + 13, kSlate, "close");
    HLine(r.x + 1, r.x + r.w - 1, r.y + kHeaderH, kDivider);
    return r;
}

// -------------------------------------------------------------------------------------
// Controls / help overlay
// -------------------------------------------------------------------------------------
void DrawHelp(int sw, int sh) {
    const char* rows[][2] = {
        { "Click",        "Select source A — highlights the k-hop ring" },
        { "Shift+Click",  "Select target B — runs Dijkstra route" },
        { "Drag",         "Orbit camera" },
        { "RMB,MMB",      "Drag to pan camera" },
        { "Scroll",       "Zoom in / out" },
        { "Esc",          "Clear selection (or click empty space)" },
        { "↑,↓,1–9",      "Change hop radius k" },
        { "D",            "Cycle direction: both / out / in" },
        { "/,Ctrl+F",     "Search node by ID — O(1) hash lookup" },
        { "C",            "Colour nodes by connected component" },
        { "T",            "Toggle node labels" },
        { "G",            "Toggle globe wireframe (geo datasets)" },
        { "F,Home",       "Focus selected node / reset view" },
        { "Del",          "Remove selected node (hash-table demo)" },
        { "F5",           "Reload dataset file" },
        { "L",            "Show load report" },
        { "F12",          "Save screenshot" },
        { "Drop",         "Drag a .txt onto the window to open it" },
    };
    const int n = sizeof(rows) / sizeof(rows[0]);
    const int rowH = 27;
    UiRect r = DrawOverlayBox(sw, sh, 620, kHeaderH + 16 + n * rowH + 12, "CONTROLS", "H");
    float x = r.x + kPad + 2, y = r.y + kHeaderH + 14;
    const float descX = x + 190;
    for (int i = 0; i < n; ++i) {
        if (i) HLine(x, r.x + r.w - kPad - 2, y - 4, Color{ 255, 255, 255, 8 });
        KeyCombo(x, y, rows[i][0]);
        Text(SANS, 13, descX, y + 2, kSlate, rows[i][1]);
        y += rowH;
    }
}

// -------------------------------------------------------------------------------------
// Load-report overlay
// -------------------------------------------------------------------------------------
void DrawReport(const AppState& s, int sw, int sh) {
    UiRect r = DrawOverlayBox(sw, sh, 820, 520, "LOAD REPORT", "L");
    const LoadReport& rep = s.report;
    float x = r.x + kPad + 2, y = r.y + kHeaderH + 16;

    // Metric grid: 5 columns × 2 rows of label-over-value cells.
    struct Cell { const char* label; int value; bool bad; };
    const Cell cells[] = {
        { "LINES",     rep.linesRead,        false },
        { "NODES",     rep.nodesLoaded,      false },
        { "EDGES",     rep.edgesLoaded,      false },
        { "DUP NODES", rep.duplicateNodes,   rep.duplicateNodes > 0 },
        { "MALFORMED", rep.malformedLines,   rep.malformedLines > 0 },
        { "UNKNOWN",   rep.missingEndpoints, rep.missingEndpoints > 0 },
        { "SELF-LOOPS", rep.selfLoops,       rep.selfLoops > 0 },
        { "REDUNDANT", rep.redundantEdges,   false },
        { "BAD WEIGHT", rep.badWeights,      rep.badWeights > 0 },
        { "AUTO-PLACED", rep.autoPlaced,     false },
    };
    const float cellW = (r.w - 2 * (kPad + 2)) / 5.0f;
    for (int i = 0; i < 10; ++i) {
        float cx = x + (i % 5) * cellW, cy = y + (i / 5) * 46;
        if (i % 5) VLine(cx - 10, cy, cy + 36, kDivider);
        Text(SANS_MED, 10, cx, cy, kSlateDim, cells[i].label);
        Text(MONO, 16, cx, cy + 15, cells[i].bad ? kAlert : kValue, Group(cells[i].value));
    }
    y += 100;
    HLine(r.x + 1, r.x + r.w - 1, y, kDivider);
    y += 14;

    if (rep.messages.size == 0) {
        Text(SANS, 13, x, y, kSlate, "All lines parsed without issues.");
        return;
    }
    const int maxRows = 15;
    for (int i = 0; i < rep.messages.size && i < maxRows; ++i) {
        TextF(MONO, 11, x, y + 1, kSlateDim, "L%-5d", rep.messages.data[i].line);
        TextF(MONO, 12, x + 60, y, Color{ 226, 232, 240, 255 }, "%.92s", rep.messages.data[i].text);
        y += 22;
    }
    if (rep.messages.size > maxRows)
        TextF(MONO, 11, x, y + 6, kSlateDim, "… %d more  (first %d problems stored)",
              rep.messages.size - maxRows, kMaxStoredMessages);
}

// Animated panel height that hugs content, clamped to the available height.
float PanelHeight(int idx, float maxH) {
    float h = g_panelH[idx] > 0 ? g_panelH[idx] : maxH;
    if (h > maxH) h = maxH;
    if (h < kHeaderH + 40) h = kHeaderH + 40;
    return h;
}

}  // namespace

// =====================================================================================
// Public API
// =====================================================================================

UiLayout UiComputeLayout(int sw, int sh) {
    UiLayout l;
    float ph = static_cast<float>(sh) - 72.0f;
    ph = ph < 200.0f ? 200.0f : (ph > 760.0f ? 760.0f : ph);
    l.left  = UiRect{ 14, 14, kPanelW, PanelHeight(0, ph) };
    l.right = UiRect{ static_cast<float>(sw) - kPanelW - 14, 14, kPanelW, PanelHeight(1, ph) };
    float dockW = 0;
    DockFit(sw, &dockW);
    l.bottom = UiRect{ std::floor((sw - dockW) * 0.5f), static_cast<float>(sh) - kDockH - 14,
                       dockW, kDockH };
    return l;
}

static bool Inside(UiRect r, float x, float y) {
    return x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
}

bool UiPointOverPanel(const AppState& s, float x, float y, int sw, int sh) {
    if (s.showHelp || s.showReport) return true;
    UiLayout l = UiComputeLayout(sw, sh);
    bool panels = s.graph.nodes.size > 0 && (Inside(l.left, x, y) || Inside(l.right, x, y));
    return panels || Inside(l.bottom, x, y);
}

void UiDraw(const AppState& s) {
    int sw = GetScreenWidth(), sh = GetScreenHeight();

    // Advance the panel-height animation once per frame (layout reads it).
    float ph = static_cast<float>(sh) - 72.0f;
    ph = ph < 200.0f ? 200.0f : (ph > 760.0f ? 760.0f : ph);
    for (int i = 0; i < 2; ++i) {
        float target = g_contentH[i] > 0 ? g_contentH[i] : ph;
        if (target > ph) target = ph;
        g_panelH[i] = g_panelH[i] <= 0 ? target : Approach(g_panelH[i], target, 14.0f);
    }
    UiLayout l = UiComputeLayout(sw, sh);

    if (s.graph.nodes.size == 0) {
        const char* t1 = "Drop a dataset onto this window";
        float y0 = sh * 0.5f - 40;
        Text(SANS_MED, 22, (sw - TextW(SANS_MED, 22, t1)) * 0.5f, y0, kValue, t1);
        const char* c1 = "netviz data/sample_network.txt";
        const char* c2 = "netviz --synthetic 1000 42";
        float cw = TextW(MONO, 13, c1);
        float cx = (sw - cw) * 0.5f;
        Text(MONO, 13, cx, y0 + 44, kSlate, c1);
        Text(MONO, 13, cx, y0 + 66, kSlate, c2);
        Text(MONO, 13, cx - 18, y0 + 44, kSlateDim, "$");
        Text(MONO, 13, cx - 18, y0 + 66, kSlateDim, "$");
    } else {
        DrawDatasetPanel(s, l.left);
        DrawSelectionPanel(s, l.right);
    }

    float dockW = 0;
    int dockN = DockFit(sw, &dockW);
    DrawDock(l.bottom, dockN);

    // Transient status toast, floating just above the dock.
    if (s.now < s.statusUntil) {
        float tw = TextW(SANS, 13, s.status);
        float w = tw + 34, h = 28;
        Rectangle t{ std::floor((sw - w) * 0.5f), l.bottom.y - h - 10, w, h };
        Glass(t, h * 0.5f, kSurfaceHi);
        DrawCircle(static_cast<int>(t.x) + 14, static_cast<int>(t.y + h * 0.5f), 2.5f, kCyan);
        Text(SANS, 13, t.x + 24, t.y + 7, kValue, s.status);
    }

    // ---- Overlays ----
    if (s.search.active) DrawSearch(s, sw);
    if (s.showHelp)      DrawHelp(sw, sh);
    if (s.showReport)    DrawReport(s, sw, sh);
}

void UiShutdown() {
    for (int i = 0; i < g_fontCount; ++i)
        if (g_fonts[i].custom) UnloadFont(g_fonts[i].font);
    g_fontCount = 0;
}
