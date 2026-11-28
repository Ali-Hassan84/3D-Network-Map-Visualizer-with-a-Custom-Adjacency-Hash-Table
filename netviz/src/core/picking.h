// picking.h - ray vs. sphere node picking (one pass over the nodes, run once per click).
#pragma once
#include "graph.h"
#include "orbit_camera.h"

// Exact ray/sphere test. On hit writes the distance along the ray to the first surface point.
bool RaySphereIntersect(const PickRay& ray, Vec3 center, float radius, float* tHit);

// Returns the index of the nearest node hit by the ray, or -1. Far-away nodes shrink to a few
// pixels, so every node's collision sphere is inflated to at least `minPixelRadius` on screen.
int PickNode(const Graph& g, const PickRay& ray, float nodeRadius, float minPixelRadius,
             float fovyDeg, int screenH);
