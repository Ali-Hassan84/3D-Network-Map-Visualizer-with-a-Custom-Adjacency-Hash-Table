#include "input.h"
#include <cctype>
#include <cmath>
#include <cstring>
#include "raylib.h"
#include "../core/timer.h"
#include "ui.h"

namespace {

const float kClickSlopPixels = 5.0f;     // movement below this counts as a click, not a drag
const float kPickMinPixels   = 7.0f;     // every node is at least this many pixels wide for clicking

bool ContainsNoCase(const std::string& hay, const char* needle) {
    size_t nl = std::strlen(needle), hl = hay.size();
    if (nl == 0) return true;
    if (nl > hl) return false;
    for (size_t i = 0; i + nl <= hl; ++i) {
        size_t j = 0;
        while (j < nl && std::tolower(static_cast<unsigned char>(hay[i + j])) == std::tolower(static_cast<unsigned char>(needle[j]))) ++j;
        if (j == nl) return true;
    }
    return false;
}

void UpdateSuggestions(AppState& s) {
    SearchBox& b = s.search;
    b.suggestionCount = 0;
    b.highlighted = 0;
    if (b.len == 0) return;
    for (int i = 0; i < s.graph.nodes.size && b.suggestionCount < 6; ++i)
        if (ContainsNoCase(s.graph.nodes.data[i]->id, b.text))
            b.suggestions[b.suggestionCount++] = s.graph.nodes.data[i];
}

void OpenSearch(AppState& s, int slot) {
    SearchBox& b = s.search;
    b.active = true;
    b.slot = slot;
    b.len = 0;
    b.text[0] = '\0';
    b.suggestionCount = 0;
    while (GetCharPressed() != 0) {}                       // drop the '/' that opened the box
}

void SubmitSearch(AppState& s) {
    SearchBox& b = s.search;
    if (b.len > 0) {
        Node* n = GraphFindNode(s.graph, b.text);          // the O(1) hash lookup
        if (!n && b.suggestionCount > 0) n = b.suggestions[b.highlighted];
        if (n) {
            if (b.slot == 0) { AppSelectSource(s, n); s.cam.target = n->scene; }
            else             { AppSelectTarget(s, n); }
            b.active = false;
        } else {
            AppSetStatus(s, "No node with id '%s' (hash lookup returned nothing)", b.text);
        }
    } else {
        b.active = false;
    }
}

void HandleSearchKeys(AppState& s) {
    SearchBox& b = s.search;
    bool changed = false;
    for (int c = GetCharPressed(); c != 0; c = GetCharPressed()) {
        if (c >= 33 && c <= 126 && b.len < 64) { b.text[b.len++] = static_cast<char>(c); b.text[b.len] = '\0'; changed = true; }
    }
    if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && b.len > 0) {
        b.text[--b.len] = '\0';
        changed = true;
    }
    if (changed) UpdateSuggestions(s);
    if (IsKeyPressed(KEY_DOWN) && b.suggestionCount > 0) b.highlighted = (b.highlighted + 1) % b.suggestionCount;
    if (IsKeyPressed(KEY_UP) && b.suggestionCount > 0)   b.highlighted = (b.highlighted + b.suggestionCount - 1) % b.suggestionCount;
    if (IsKeyPressed(KEY_TAB)) b.slot = 1 - b.slot;
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) SubmitSearch(s);
    if (IsKeyPressed(KEY_ESCAPE)) b.active = false;
}

void HandleHotkeys(AppState& s) {
    bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    if (IsKeyPressed(KEY_SLASH) || (ctrl && IsKeyPressed(KEY_F))) { OpenSearch(s, 0); return; }

    if (IsKeyPressed(KEY_ESCAPE)) {
        if (s.showHelp || s.showReport) s.showHelp = s.showReport = false;
        else AppClearSelection(s);
    }
    if (IsKeyPressed(KEY_H) || IsKeyPressed(KEY_F1)) { s.showHelp = !s.showHelp; s.showReport = false; }
    if (IsKeyPressed(KEY_L)) { s.showReport = !s.showReport; s.showHelp = false; }
    if (IsKeyPressed(KEY_C)) {
        s.colorByComponent = !s.colorByComponent;
        AppSetStatus(s, "Component colours %s  (%d components)", s.colorByComponent ? "on" : "off", s.comps.count);
    }
    if (IsKeyPressed(KEY_T)) s.labelsAll = !s.labelsAll;
    if (IsKeyPressed(KEY_G)) s.showGlobe = !s.showGlobe;
    if (IsKeyPressed(KEY_D)) AppCycleDirection(s);
    if (IsKeyPressed(KEY_HOME)) AppResetView(s);
    if (IsKeyPressed(KEY_F) && !ctrl && s.a) s.cam.target = s.a->scene;
    if (IsKeyPressed(KEY_DELETE) && s.a) AppDeleteSelected(s);
    if (IsKeyPressed(KEY_F5)) {
        if (!AppReload(s)) AppSetStatus(s, "Nothing to reload");
    }
    if (s.a) {
        if (IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP))     AppSetHops(s, s.k + 1);
        if (IsKeyPressed(KEY_DOWN) || IsKeyPressedRepeat(KEY_DOWN)) AppSetHops(s, s.k - 1);
        for (int d = 1; d <= 9; ++d)
            if (IsKeyPressed(KEY_ZERO + d)) AppSetHops(s, d);
    }
}

void HandleClick(AppState& s, Vector2 m, bool shift) {
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    PickRay ray = ScreenPointToRay(s.cam, m.x, m.y, sw, sh);      // 2D click -> 3D ray, ONCE per click
    double t0 = NowNs();
    int idx = PickNode(s.graph, ray, s.nodeRadius, kPickMinPixels, s.cam.fovyDeg, sh);
    s.m.pickUs = (NowNs() - t0) / 1e3;
    s.m.hasPick = true;
    if (idx >= 0) {
        Node* n = s.graph.nodes.data[idx];
        if (shift) AppSelectTarget(s, n); else AppSelectSource(s, n);
    } else if (!shift) {
        AppClearSelection(s);
    }
}

}  // namespace

void AppHandleInput(AppState& s) {
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    s.now = GetTime();

    if (IsFileDropped()) {
        FilePathList files = LoadDroppedFiles();
        if (files.count > 0) AppLoadFile(s, files.paths[0]);
        UnloadDroppedFiles(files);
    }

    if (s.search.active) HandleSearchKeys(s); else HandleHotkeys(s);

    Vector2 m = GetMousePosition();
    bool overUi = UiPointOverPanel(s, m.x, m.y, sw, sh);
    bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    Vector2 d = GetMouseDelta();

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !overUi) { s.leftDown = true; s.dragDist = 0.0f; }
    if (s.leftDown && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        s.dragDist += std::fabs(d.x) + std::fabs(d.y);
        if (s.dragDist > kClickSlopPixels) {                       // dragging: orbit (or pan with Shift)
            if (shift) OrbitPan(s.cam, d.x, d.y, sh); else OrbitRotate(s.cam, d.x, d.y);
        }
    }
    if (s.leftDown && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        if (s.dragDist <= kClickSlopPixels) HandleClick(s, m, shift);
        s.leftDown = false;
    }

    if ((IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) || IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE)) && !overUi) s.panDown = true;
    if (s.panDown && (IsMouseButtonDown(MOUSE_BUTTON_RIGHT) || IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)))
        OrbitPan(s.cam, d.x, d.y, sh);
    else
        s.panDown = false;

    float wheel = GetMouseWheelMove();
    if (wheel != 0.0f && !overUi) OrbitZoom(s.cam, wheel);
}
