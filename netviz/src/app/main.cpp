// main.cpp - NetViz 3D entry point.
//   netviz [dataset.txt]                 open a dataset
//   netviz --synthetic N [seed]          generate a synthetic network  (e.g. 100000)
//
// Usage notes:
//   The window opens at 1600×900 (resizable).  MSAA 4× is requested for anti-aliasing.
//   VSYNC is on.  The Esc key clears the selection instead of quitting — use Alt+F4 or
//   close the window normally to exit.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "raylib.h"
#include "app_state.h"
#include "input.h"
#include "renderer.h"
#include "ui.h"

static bool AutoShotDue(int frame);

int main(int argc, char** argv) {
    // ---- window & rendering flags -------------------------------------------------
    SetConfigFlags(FLAG_MSAA_4X_HINT        // 4× MSAA — smooth sphere silhouettes
                 | FLAG_WINDOW_RESIZABLE    // drag corner to resize
                 | FLAG_VSYNC_HINT);        // prevent screen tearing

    InitWindow(1600, 900, "NetViz 3D — Network Map Visualizer");
    SetExitKey(KEY_NULL);         // Esc = clear selection, not quit
    SetTargetFPS(60);

    // Bilinear texture filter on the default font (crisper text)
    SetTextureFilter(GetFontDefault().texture, TEXTURE_FILTER_BILINEAR);

    // ---- initialise application state & renderer ----------------------------------
    AppState  state;
    AppInit(state);
    Renderer  renderer;
    RendererInit(renderer);
    state.now = GetTime();

    // ---- decide what to open ------------------------------------------------------
    if (argc >= 3 && std::strcmp(argv[1], "--synthetic") == 0) {
        int n = std::atoi(argv[2]);
        unsigned long long seed = argc >= 4 ? std::strtoull(argv[3], nullptr, 10) : 42ULL;
        if (n < 1) n = 1000;
        AppLoadSynthetic(state, n, seed);
    } else if (argc >= 2) {
        AppLoadFile(state, argv[1]);
    } else if (FileExists("data/sample_network.txt")) {
        AppLoadFile(state, "data/sample_network.txt");
    }

    // ---- developer hooks via environment variables --------------------------------
    // NETVIZ_K, NETVIZ_A, NETVIZ_B, NETVIZ_COLORS — pre-select nodes for demos.
    if (const char* e = std::getenv("NETVIZ_K"))      state.k = std::atoi(e) < 1 ? 1 : std::atoi(e);
    if (const char* e = std::getenv("NETVIZ_A"))      AppSelectSource(state, GraphFindNode(state.graph, e));
    if (const char* e = std::getenv("NETVIZ_B"))      AppSelectTarget(state, GraphFindNode(state.graph, e));
    if (std::getenv("NETVIZ_COLORS"))                  state.colorByComponent = true;

    // ---- main loop ----------------------------------------------------------------
    int frame = 0;
    while (!WindowShouldClose()) {
        AppHandleInput(state);
        if (AutoShotDue(frame++)) { TakeScreenshot("netviz_autoshot.png"); break; }

        Camera3D cam = MakeRaylibCamera(state.cam);

        BeginDrawing();
        ClearBackground(Color{ 255, 255, 255, 255 });  // white background

        BeginMode3D(cam);
        RenderScene(renderer, state, cam);
        EndMode3D();

        RenderLabels(state, cam);   // 2D labels projected from 3D positions
        UiDraw(state);              // HUD panels, search, help
        EndDrawing();
    }

    // ---- clean shutdown -----------------------------------------------------------
    UiShutdown();
    RendererShutdown(renderer);
    AppShutdown(state);
    CloseWindow();
    return 0;
}

// Developer hook: NETVIZ_AUTOSHOT=<frame> saves a screenshot at that frame and exits.
// Used by headless CI checks (clean shutdown so sanitisers see no leaks).
static bool AutoShotDue(int frame) {
    const char* env = std::getenv("NETVIZ_AUTOSHOT");
    return env && frame == std::atoi(env);
}
