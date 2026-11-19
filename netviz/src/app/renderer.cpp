// renderer.cpp - NetViz 3D scene renderer.
// Improvements: multi-layer glow for selected nodes, clean high-res sphere mesh,
// per-node colour palette, anti-aliased edges, sharp labels.
#include "renderer.h"
#include <cmath>
#include "raymath.h"
#include "rlgl.h"

// -------------------------------------------------------------------------------------
// Colour palette  (all colours in one place, easy to adjust)
// -------------------------------------------------------------------------------------
namespace {

const Color kBg          = {  8, 11, 22, 255 };   // scene background (also used in main.cpp)

// Node base colours
const Color kNodeBase    = { 100, 160, 230, 255 }; // normal node: cool blue
const Color kNodeDim     = {  28,  38,  60, 180 }; // dimmed when something is selected

// Edge colours
const Color kEdgeBase    = {  55,  85, 130, 180 }; // normal edge
const Color kEdgeDim     = {  22,  32,  52, 100 }; // dimmed edge

// Selection / path colours  (matches UI palette in ui.cpp)
const Color kSourceCol   = { 255, 210,  50, 255 }; // selected A  — amber/gold
const Color kTargetCol   = { 240,  70, 190, 255 }; // selected B  — magenta/pink
const Color kPathCol     = {  60, 240, 130, 255 }; // shortest path — mint green

// -------------------------------------------------------------------------------------
// GLSL shaders — lit sphere (key light + rim + specular) with glow pass support
// -------------------------------------------------------------------------------------

// --- Standard lit sphere (used for every node) ---
const char* kVertSphere =
    "#version 330\n"
    "in vec3 vertexPosition;\n"
    "in vec3 vertexNormal;\n"
    "uniform mat4 mvp;\n"
    "uniform mat4 matModel;\n"
    "out vec3 fragPos;\n"
    "out vec3 fragNormal;\n"
    "void main() {\n"
    "    fragPos    = vec3(matModel * vec4(vertexPosition, 1.0));\n"
    "    fragNormal = normalize(mat3(matModel) * vertexNormal);\n"
    "    gl_Position = mvp * vec4(vertexPosition, 1.0);\n"
    "}\n";

const char* kFragSphere =
    "#version 330\n"
    "in vec3 fragPos;\n"
    "in vec3 fragNormal;\n"
    "uniform vec4 colDiffuse;\n"
    "uniform vec3 viewPos;\n"
    "out vec4 finalColor;\n"
    "void main() {\n"
    "    vec3 N   = normalize(fragNormal);\n"
    "    vec3 V   = normalize(viewPos - fragPos);\n"
    "    vec3 L   = normalize(V + vec3(-0.5, 0.75, 0.3));\n"   // key light above-left
    "    float diff = max(dot(N, L), 0.0);\n"
    "    float rim  = pow(1.0 - max(dot(N, V), 0.0), 2.5);\n"  // softer rim
    "    float spec = pow(max(dot(N, normalize(L + V)), 0.0), 60.0) * 0.45;\n" // tight specular
    "    vec3 col = colDiffuse.rgb * (0.22 + 0.78 * diff)\n"
    "             + colDiffuse.rgb * 0.18 * rim\n"             // tinted rim
    "             + vec3(1.0) * spec;\n"                       // white specular
    "    finalColor = vec4(col, 1.0);\n"
    "}\n";

// --- Glow / halo pass: an oversized sphere rendered additive, fades with surface angle ---
const char* kVertGlow =
    "#version 330\n"
    "in vec3 vertexPosition;\n"
    "in vec3 vertexNormal;\n"
    "uniform mat4 mvp;\n"
    "uniform mat4 matModel;\n"
    "out vec3 fragNormal;\n"
    "out vec3 fragPos;\n"
    "void main() {\n"
    "    fragNormal  = normalize(mat3(matModel) * vertexNormal);\n"
    "    fragPos     = vec3(matModel * vec4(vertexPosition, 1.0));\n"
    "    gl_Position = mvp * vec4(vertexPosition, 1.0);\n"
    "}\n";

const char* kFragGlow =
    "#version 330\n"
    "in vec3 fragNormal;\n"
    "in vec3 fragPos;\n"
    "uniform vec4 colDiffuse;\n"  // glow colour (alpha = overall intensity)
    "uniform vec3 viewPos;\n"
    "out vec4 finalColor;\n"
    "void main() {\n"
    "    vec3 V  = normalize(viewPos - fragPos);\n"
    "    float f = 1.0 - max(dot(fragNormal, V), 0.0);\n"     // Fresnel-like edge factor
    "    f = pow(f, 1.6);\n"                                   // sharpen the glow ring
    "    finalColor = vec4(colDiffuse.rgb, colDiffuse.a * f);\n"
    "}\n";

// -------------------------------------------------------------------------------------
// Helpers
// -------------------------------------------------------------------------------------
Vector3 ToRl(Vec3 v) { return Vector3{ v.x, v.y, v.z }; }

Color Scale(Color c, float f) {
    auto clamp255 = [](float x) -> unsigned char {
        return x < 0.f ? 0 : (x > 255.f ? 255 : static_cast<unsigned char>(x));
    };
    return Color{ clamp255(c.r * f), clamp255(c.g * f), clamp255(c.b * f), c.a };
}

Color ScaleAlpha(Color c, float a) {
    return Color{ c.r, c.g, c.b, static_cast<unsigned char>(c.a * a) };
}

Color LerpColor(Color a, Color b, float t) {
    return Color{
        static_cast<unsigned char>(a.r + (b.r - a.r) * t),
        static_cast<unsigned char>(a.g + (b.g - a.g) * t),
        static_cast<unsigned char>(a.b + (b.b - a.b) * t),
        255
    };
}

Color ComponentColor(int comp, float sat, float val) {
    float hue = std::fmod(static_cast<float>(comp) * 137.508f, 360.0f);
    return ColorFromHSV(hue, sat, val);
}

Color HopColor(int hop, int maxHop) {
    if (hop <= 0) return kSourceCol;
    float t = maxHop > 1 ? static_cast<float>(hop - 1) / static_cast<float>(maxHop - 1) : 0.0f;
    return ColorFromHSV(185.0f + 100.0f * t, 0.80f, 1.0f);  // cyan → violet
}

// -------------------------------------------------------------------------------------
// Core draw helpers
// -------------------------------------------------------------------------------------

// Draw a lit sphere using the custom shader
void DrawNodeSphere(Renderer& r, Vec3 p, float radius, Color c) {
    if (r.lit) {
        r.sphereMat.maps[MATERIAL_MAP_DIFFUSE].color = c;
        Matrix m = MatrixMultiply(MatrixScale(radius, radius, radius),
                                  MatrixTranslate(p.x, p.y, p.z));
        DrawMesh(r.sphere, r.sphereMat, m);
    } else {
        DrawSphereEx(ToRl(p), radius, 16, 16, c);
    }
}

// Draw the outer glow halo (additive blend, two layers at different sizes/intensities)
void DrawGlowHalo(Renderer& r, Vec3 p, float nodeRadius, Color glowCol,
                  float scale1, float alpha1,
                  float scale2, float alpha2) {
    if (!r.glowLit) return;

    rlEnableColorBlend();
    rlSetBlendMode(RL_BLEND_ADDITIVE);

    r.glowMat.maps[MATERIAL_MAP_DIFFUSE].color = ScaleAlpha(glowCol, alpha1);
    Matrix m1 = MatrixMultiply(MatrixScale(nodeRadius * scale1, nodeRadius * scale1, nodeRadius * scale1),
                               MatrixTranslate(p.x, p.y, p.z));
    DrawMesh(r.sphere, r.glowMat, m1);

    r.glowMat.maps[MATERIAL_MAP_DIFFUSE].color = ScaleAlpha(glowCol, alpha2);
    Matrix m2 = MatrixMultiply(MatrixScale(nodeRadius * scale2, nodeRadius * scale2, nodeRadius * scale2),
                               MatrixTranslate(p.x, p.y, p.z));
    DrawMesh(r.sphere, r.glowMat, m2);

    rlSetBlendMode(RL_BLEND_ALPHA);
}

// Draw a single directed or undirected arc.
// Undirected links are stored as two arcs; we draw them once (lower-index end).
void DrawArcLine(const Node* u, const Edge& e, Color c, float nodeRadius) {
    if (e.undirectedLink && u->index > e.to->index) return;
    Vec3 p = u->scene, q = e.to->scene;
    DrawLine3D(ToRl(p), ToRl(q), c);
    if (!e.undirectedLink) {
        Vec3 d = V3Sub(q, p);
        float len = V3Length(d);
        float cone = nodeRadius * 2.2f;
        if (len > cone + nodeRadius * 2.0f) {
            Vec3 dir = V3Scale(d, 1.0f / len);
            Vec3 tip  = V3Sub(q, V3Scale(dir, nodeRadius * 1.1f));
            Vec3 base = V3Sub(tip, V3Scale(dir, cone));
            DrawCylinderEx(ToRl(base), ToRl(tip), nodeRadius * 0.6f, 0.0f, 7, c);
        }
    }
}

// Thick tube for the shortest-path highlight
void DrawThickSegment(Vec3 p, Vec3 q, float radius, Color c) {
    DrawCylinderEx(ToRl(p), ToRl(q), radius, radius, 8, c);
}

}  // namespace

// =====================================================================================
// Public API
// =====================================================================================

void RendererInit(Renderer& r) {
    // High-quality sphere mesh (more segments = rounder look at close zoom)
    r.sphere = GenMeshSphere(1.0f, 20, 20);

    // --- Lit sphere material ---
    r.sphereShader = LoadShaderFromMemory(kVertSphere, kFragSphere);
    r.lit = (r.sphereShader.id != 0 && r.sphereShader.id != rlGetShaderIdDefault());
    r.sphereMat = LoadMaterialDefault();
    if (r.lit) {
        r.sphereShader.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(r.sphereShader, "matModel");
        r.sphereViewPosLoc = GetShaderLocation(r.sphereShader, "viewPos");
        r.sphereMat.shader = r.sphereShader;
    }

    // --- Glow / halo material ---
    r.glowShader = LoadShaderFromMemory(kVertGlow, kFragGlow);
    r.glowLit = (r.glowShader.id != 0 && r.glowShader.id != rlGetShaderIdDefault());
    r.glowMat = LoadMaterialDefault();
    if (r.glowLit) {
        r.glowShader.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(r.glowShader, "matModel");
        r.glowViewPosLoc = GetShaderLocation(r.glowShader, "viewPos");
        r.glowMat.shader = r.glowShader;
    }
}

void RendererShutdown(Renderer& r) {
    if (r.lit)    UnloadShader(r.sphereShader);
    if (r.glowLit) UnloadShader(r.glowShader);
    // Don't free the default shader; raylib owns it.
    r.sphereMat.shader = Shader{ 0, nullptr };
    r.glowMat.shader   = Shader{ 0, nullptr };
    MemFree(r.sphereMat.maps);
    MemFree(r.glowMat.maps);
    r.sphereMat.maps = nullptr;
    r.glowMat.maps   = nullptr;
    UnloadMesh(r.sphere);
}

Camera3D MakeRaylibCamera(const OrbitCamera& c) {
    Camera3D cam = {};
    cam.position   = { OrbitPosition(c).x, OrbitPosition(c).y, OrbitPosition(c).z };
    cam.target     = { c.target.x, c.target.y, c.target.z };
    cam.up         = { 0.0f, 1.0f, 0.0f };
    cam.fovy       = c.fovyDeg;
    cam.projection = CAMERA_PERSPECTIVE;
    return cam;
}

// =====================================================================================
// RenderScene
// =====================================================================================
void RenderScene(Renderer& r, const AppState& s, const Camera3D& cam) {
    const Graph& g = s.graph;
    if (g.nodes.size == 0) return;

    const bool  sel = (s.a != nullptr);
    const float nr  = s.nodeRadius;
    const float hiR = nr > 0.9f ? nr : 0.9f;  // highlighted nodes are always visible

    // Upload camera position to both shaders
    float vp[3] = { cam.position.x, cam.position.y, cam.position.z };
    if (r.lit)    SetShaderValue(r.sphereShader, r.sphereViewPosLoc, vp, SHADER_UNIFORM_VEC3);
    if (r.glowLit) SetShaderValue(r.glowShader,  r.glowViewPosLoc,  vp, SHADER_UNIFORM_VEC3);

    // Globe wireframe (subtle, only for geo datasets)
    if (g.geo && s.showGlobe)
        DrawSphereWires({ 0, 0, 0 }, 98.0f, 9, 18, Color{ 20, 32, 52, 220 });

    // ---- 1. Whole graph (full mode) or point-cloud dashes (large mode) ----------------
    if (!s.largeMode) {
        // Edges first (behind nodes)
        for (int i = 0; i < g.nodes.size; ++i) {
            const Node* u = g.nodes.data[i];
            Color ec = sel
                ? kEdgeDim
                : (s.colorByComponent ? ComponentColor(u->component, 0.45f, 0.5f) : kEdgeBase);
            for (int j = 0; j < u->out.size; ++j)
                DrawArcLine(u, u->out.data[j], ec, nr);
        }
        // Then nodes
        for (int i = 0; i < g.nodes.size; ++i) {
            const Node* u = g.nodes.data[i];
            if (sel && s.hopOf && s.hopOf[i] >= 0) continue;  // drawn highlighted below
            Color nc = s.colorByComponent
                ? ComponentColor(u->component, 0.70f, 1.0f)
                : kNodeBase;
            if (sel) nc = s.colorByComponent ? Scale(nc, 0.30f) : kNodeDim;
            DrawNodeSphere(r, u->scene, nr, nc);
        }
    } else {
        // Large mode: each node = a short screen-aligned dash (~2 px)
        float pxWorld = s.cam.distance * 2.0f
            * std::tan(s.cam.fovyDeg * 0.5f * 3.14159265f / 180.0f)
            / static_cast<float>(GetScreenHeight());
        Vec3 half = V3Scale(OrbitRight(s.cam), pxWorld * 1.4f);
        for (int i = 0; i < g.nodes.size; ++i) {
            const Node* u = g.nodes.data[i];
            Color nc = s.colorByComponent
                ? ComponentColor(u->component, 0.60f, 0.90f)
                : Color{ 80, 130, 200, 200 };
            if (sel) nc = Scale(nc, 0.40f);
            DrawLine3D(ToRl(V3Sub(u->scene, half)), ToRl(V3Add(u->scene, half)), nc);
        }
    }

    // ---- 2. K-hop neighbourhood of A (lit + subtle glow on hop-1 ring) ---------------
    if (sel && s.hopOf) {
        const int shown = s.khop.count < kMaxHighlightNodes ? s.khop.count : kMaxHighlightNodes;
        int edgeBudget  = kMaxHighlightEdges;

        // Highlighted edges first
        for (int i = 0; i < shown; ++i) {
            const Node* u = g.nodes.data[s.khop.nodes[i]];
            for (int j = 0; j < u->out.size && edgeBudget > 0; ++j) {
                const Edge& e = u->out.data[j];
                int hv = s.hopOf[e.to->index];
                if (hv < 0) continue;
                int hu = s.hopOf[u->index];
                if (hv - hu > 1 || hu - hv > 1) continue;
                int far = hu > hv ? hu : hv;
                Color ec = HopColor(far == 0 ? 1 : far, s.khop.maxHop);
                ec = ScaleAlpha(ec, 160);    // slightly transparent edges in hop set
                DrawArcLine(u, e, ec, nr);
                --edgeBudget;
            }
        }

        // Highlighted spheres
        for (int i = 0; i < shown; ++i) {
            const Node* u = g.nodes.data[s.khop.nodes[i]];
            Color hc = HopColor(s.khop.hops[i], s.khop.maxHop);
            DrawNodeSphere(r, u->scene, hiR, hc);
            // Soft glow only on the immediate (hop-1) ring
            if (s.khop.hops[i] == 1)
                DrawGlowHalo(r, u->scene, hiR, hc, 1.9f, 0.28f, 2.8f, 0.12f);
        }
    }

    // ---- 3. Shortest path A→B: thick mint-green tubes + glow -----------------------
    if (s.path.found && s.path.length > 1) {
        for (int i = 0; i + 1 < s.path.length; ++i) {
            const Node* u = g.nodes.data[s.path.nodes[i]];
            const Node* v = g.nodes.data[s.path.nodes[i + 1]];
            DrawThickSegment(u->scene, v->scene, hiR * 0.32f, kPathCol);
        }
        for (int i = 1; i + 1 < s.path.length; ++i) {
            Vec3 p = g.nodes.data[s.path.nodes[i]]->scene;
            DrawNodeSphere(r, p, hiR * 1.05f, kPathCol);
            DrawGlowHalo(r, p, hiR * 1.05f, kPathCol, 1.85f, 0.30f, 2.70f, 0.13f);
        }
    }

    // ---- 4. Selected endpoints — bright + strong glow, drawn on top -----------------
    if (s.a) {
        const float aR = hiR * 1.55f;
        DrawNodeSphere(r, s.a->scene, aR, kSourceCol);
        // Three glow layers: inner halo, outer halo, far soft bloom
        DrawGlowHalo(r, s.a->scene, aR, kSourceCol, 1.65f, 0.50f, 2.40f, 0.25f);
        DrawGlowHalo(r, s.a->scene, aR, kSourceCol, 3.20f, 0.09f, 0.f, 0.f);
    }
    if (s.b) {
        const float bR = hiR * 1.55f;
        DrawNodeSphere(r, s.b->scene, bR, kTargetCol);
        DrawGlowHalo(r, s.b->scene, bR, kTargetCol, 1.65f, 0.50f, 2.40f, 0.25f);
        DrawGlowHalo(r, s.b->scene, bR, kTargetCol, 3.20f, 0.09f, 0.f, 0.f);
    }
}

// =====================================================================================
// RenderLabels  — called after EndMode3D (2D screen space)
// =====================================================================================
static void DrawLabel(const Camera3D& cam, const Node* n, Color textCol, int size,
                      Vec3 camPos, Vec3 fwd, bool globe) {
    Vec3 toNode = V3Sub(n->scene, camPos);
    if (V3Dot(toNode, fwd) <= 0.0f) return;              // behind camera
    if (globe && V3Dot(n->scene, camPos) < 0.0f) return; // far side of globe

    Vector2 sp = GetWorldToScreen(ToRl(n->scene), cam);
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    if (sp.x < -50 || sp.x > sw + 50 || sp.y < -50 || sp.y > sh + 50) return;

    // Use a slightly larger font size for readability on hi-res displays
    const int fs = size;
    int tw = MeasureText(n->id.c_str(), fs);
    int tx = static_cast<int>(sp.x) - tw / 2;
    int ty = static_cast<int>(sp.y) - fs - 12;

    // Dark pill background with rounded look (approximated by overlapping rects)
    DrawRectangleRounded({ static_cast<float>(tx - 5), static_cast<float>(ty - 2),
                           static_cast<float>(tw + 10), static_cast<float>(fs + 6) },
                         0.4f, 4, Color{ 6, 9, 20, 200 });

    DrawText(n->id.c_str(), tx, ty, fs, textCol);
}

void RenderLabels(const AppState& s, const Camera3D& cam) {
    const Graph& g = s.graph;
    if (g.nodes.size == 0) return;

    Vec3 camPos = V3(cam.position.x, cam.position.y, cam.position.z);
    Vec3 fwd    = OrbitForward(s.cam);
    bool globe  = g.geo && s.showGlobe;

    // All-labels mode (small graphs only, to avoid clutter)
    if (s.labelsAll && g.nodes.size <= 350) {
        Color dim = s.a ? Color{ 100, 115, 145, 220 } : Color{ 185, 200, 220, 230 };
        for (int i = 0; i < g.nodes.size; ++i)
            DrawLabel(cam, g.nodes.data[i], dim, 15, camPos, fwd, globe);
    }

    // K-hop ring labels (hop-1 neighbours of A)
    if (s.a && s.hopOf) {
        int drawn = 0;
        for (int i = 0; i < s.khop.count && drawn < 50; ++i) {
            if (s.khop.hops[i] > 1) break;
            DrawLabel(cam, g.nodes.data[s.khop.nodes[i]],
                      Color{ 140, 220, 255, 240 }, 15, camPos, fwd, globe);
            ++drawn;
        }
    }

    // Shortest-path node labels
    if (s.path.found)
        for (int i = 0; i < s.path.length; ++i)
            DrawLabel(cam, g.nodes.data[s.path.nodes[i]], kPathCol, 16, camPos, fwd, globe);

    // Always show selected endpoints' labels (largest font, most prominent)
    if (s.a) DrawLabel(cam, s.a, kSourceCol, 18, camPos, fwd, globe);
    if (s.b) DrawLabel(cam, s.b, kTargetCol, 18, camPos, fwd, globe);
}
