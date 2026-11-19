// orbit_camera.h - orbit/pan/zoom camera maths and screen-point -> 3D ray conversion.
// Pure maths (no raylib), so ray picking can be unit-tested without a window.
#pragma once
#include "vec3.h"

struct OrbitCamera {
    Vec3  target   = V3(0, 0, 0);
    float yaw      = 0.6f;        // radians around the Y axis
    float pitch    = 0.35f;       // radians above the horizon
    float distance = 250.0f;
    float fovyDeg  = 45.0f;
};

Vec3 OrbitPosition(const OrbitCamera& c);
Vec3 OrbitForward(const OrbitCamera& c);     // unit vector from camera towards target
Vec3 OrbitRight(const OrbitCamera& c);
Vec3 OrbitUp(const OrbitCamera& c);

void OrbitRotate(OrbitCamera& c, float dxPixels, float dyPixels);        // drag to orbit
void OrbitPan(OrbitCamera& c, float dxPixels, float dyPixels, int screenH);
void OrbitZoom(OrbitCamera& c, float wheelSteps);

struct PickRay { Vec3 origin; Vec3 dir; };   // dir is unit length
// Perspective unprojection: pixel (mx,my) (origin top-left) -> world-space ray.
PickRay ScreenPointToRay(const OrbitCamera& c, float mx, float my, int screenW, int screenH);
