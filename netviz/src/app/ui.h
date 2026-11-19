// ui.h - 2D overlay: analytics panels, search box, help and load-report overlays.
#pragma once
#include "app_state.h"

struct UiRect { float x, y, w, h; };
struct UiLayout { UiRect left, right, bottom; };

UiLayout UiComputeLayout(int screenW, int screenH);
bool     UiPointOverPanel(const AppState& s, float x, float y, int screenW, int screenH);
void     UiDraw(const AppState& s);
