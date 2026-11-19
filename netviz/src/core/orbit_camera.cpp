#include "orbit_camera.h"
#include <cmath>

static float ClampF(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

Vec3 OrbitPosition(const OrbitCamera& c) {
    return V3Add(c.target, V3(c.distance * std::cos(c.pitch) * std::sin(c.yaw),
                              c.distance * std::sin(c.pitch),
                              c.distance * std::cos(c.pitch) * std::cos(c.yaw)));
}
Vec3 OrbitForward(const OrbitCamera& c) { return V3Normalize(V3Sub(c.target, OrbitPosition(c))); }
Vec3 OrbitRight(const OrbitCamera& c)   { return V3Normalize(V3Cross(OrbitForward(c), V3(0, 1, 0))); }
Vec3 OrbitUp(const OrbitCamera& c)      { return V3Cross(OrbitRight(c), OrbitForward(c)); }

void OrbitRotate(OrbitCamera& c, float dx, float dy) {
    c.yaw   -= dx * 0.005f;                       // drag right -> scene turns right
    c.pitch  = ClampF(c.pitch + dy * 0.005f, -1.5f, 1.5f);
}

void OrbitPan(OrbitCamera& c, float dx, float dy, int screenH) {
    float worldPerPixel = 2.0f * c.distance * std::tan(c.fovyDeg * 0.5f * 3.14159265f / 180.0f) / (screenH > 0 ? screenH : 1);
    c.target = V3Sub(c.target, V3Scale(OrbitRight(c), dx * worldPerPixel));
    c.target = V3Add(c.target, V3Scale(OrbitUp(c), dy * worldPerPixel));
}

void OrbitZoom(OrbitCamera& c, float wheel) {
    c.distance = ClampF(c.distance * std::pow(0.9f, wheel), 3.0f, 2500.0f);
}

PickRay ScreenPointToRay(const OrbitCamera& c, float mx, float my, int sw, int sh) {
    float ndcX = 2.0f * mx / sw - 1.0f;           // -1 .. 1, right is positive
    float ndcY = 1.0f - 2.0f * my / sh;           // -1 .. 1, up is positive
    float tanHalf = std::tan(c.fovyDeg * 0.5f * 3.14159265f / 180.0f);
    float aspect = static_cast<float>(sw) / static_cast<float>(sh);
    Vec3 dir = V3Add(OrbitForward(c),
               V3Add(V3Scale(OrbitRight(c), ndcX * tanHalf * aspect),
                     V3Scale(OrbitUp(c),    ndcY * tanHalf)));
    PickRay r;
    r.origin = OrbitPosition(c);
    r.dir = V3Normalize(dir);
    return r;
}
