#include "picking.h"
#include <cmath>

bool RaySphereIntersect(const PickRay& ray, Vec3 c, float r, float* tHit) {
    Vec3 oc = V3Sub(ray.origin, c);
    float b = V3Dot(oc, ray.dir);                    // ray.dir is unit length, so a == 1
    float cc = V3Dot(oc, oc) - r * r;
    float disc = b * b - cc;
    if (disc < 0.0f) return false;
    float t = -b - std::sqrt(disc);                  // nearer root
    if (t < 0.0f) {
        t = -b + std::sqrt(disc);                    // origin inside the sphere
        if (t < 0.0f) return false;                  // sphere is behind the camera
    }
    if (tHit) *tHit = t;
    return true;
}

int PickNode(const Graph& g, const PickRay& ray, float nodeRadius, float minPixelRadius,
             float fovyDeg, int screenH) {
    // world size of one pixel per unit of distance
    float anglePerPixel = 2.0f * std::tan(fovyDeg * 0.5f * 3.14159265f / 180.0f) / (screenH > 0 ? screenH : 1);
    int best = -1;
    float bestT = 1e30f;
    for (int i = 0; i < g.nodes.size; ++i) {
        const Node* n = g.nodes.data[i];
        float along = V3Dot(V3Sub(n->scene, ray.origin), ray.dir);
        if (along <= 0.0f) continue;                 // behind the camera
        float r = nodeRadius;
        float minR = along * anglePerPixel * minPixelRadius;
        if (minR > r) r = minR;
        float t;
        if (RaySphereIntersect(ray, n->scene, r, &t) && t < bestT) { bestT = t; best = i; }
    }
    return best;
}
