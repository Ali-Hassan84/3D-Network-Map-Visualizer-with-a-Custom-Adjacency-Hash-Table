// renderer.h - draws the 3D scene with raylib (spheres, edges, arrows, globe, labels).
// Two shaders: a lit sphere (key-light + rim + specular) and an additive glow halo.
#pragma once
#include "raylib.h"
#include "app_state.h"

struct Renderer {
    // Sphere geometry (shared between both materials)
    Mesh     sphere     = {};

    // Lit-sphere shader + material
    Shader   sphereShader   = {};
    Material sphereMat      = {};
    bool     lit            = false;
    int      sphereViewPosLoc = -1;

    // Glow / halo shader + material (additive blend)
    Shader   glowShader   = {};
    Material glowMat      = {};
    bool     glowLit      = false;
    int      glowViewPosLoc = -1;
};

void     RendererInit(Renderer& r);
void     RendererShutdown(Renderer& r);
Camera3D MakeRaylibCamera(const OrbitCamera& c);
void     RenderScene(Renderer& r, const AppState& s, const Camera3D& cam);  // inside BeginMode3D
void     RenderLabels(const AppState& s, const Camera3D& cam);              // after EndMode3D
