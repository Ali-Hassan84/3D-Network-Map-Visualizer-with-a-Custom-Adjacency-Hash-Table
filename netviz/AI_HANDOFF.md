# AI_HANDOFF.md — read this first if you are an AI (or human) continuing this project

**Project:** NetViz 3D — "3D Network Map Visualizer with a Custom Adjacency Hash Table"
(university DSA project, C++17 + raylib 5.x).

**Status:** feature-complete against the project proposal; all tests pass (5,399 checks,
also under ASan/UBSan); the GUI was run headlessly (Xvfb + Mesa) and driven with synthetic
mouse/keyboard input, ASan/LSan clean.

**Revision 2 visual overhaul (2026-11):**
- `renderer.cpp / renderer.h` — dual-shader pipeline: a lit sphere shader and a separate
  additive glow/halo shader.  Selected nodes (A, B) get three layered glow passes; hop-1
  neighbours get a subtle single-layer halo; path nodes get a mint-green halo.  High-res
  sphere mesh (20×20 segments vs 14×14 before).  Clean edge colour palette.
  `RenderLabels` now uses `DrawRectangleRounded` for label backgrounds.
- `ui.cpp` — column-aligned key/value rows (fixed `kValCol` offset); consistent font sizes
  (`kFontSm/Md/Hdr/Pnl`); rounded panel and bar-chart backgrounds; minimal bottom bar;
  transient status message moved out of the bottom bar so it does not overlap the hints;
  search overlay uses a rounded rectangle.
- `main.cpp` — window opens at 1600×900 (was 1366×768); `MSAA 4X` and `VSYNC` flags kept;
  background colour is now `{8, 11, 22}` (deep space) to match renderer palette.

**Primary user environment:** Windows, VS Code, g++ from MSYS2 UCRT64 (keep code portable,
no POSIX-only APIs, no compiler extensions).  Authors present it in a class demo, so code
should stay *explainable* — prefer plain, readable code.

---

## 1. Hard constraints (from the proposal — do not break)

1. **STL policy.** Graph structures and memory management are custom. Allowed in `src/core`
   and `src/app`: `std::string`, C library headers (`<cstdio> <cstdlib> <cstring> <cmath>
   ...`), `<chrono>` (timing only), `<type_traits>` (a `static_assert`).
   **Forbidden there:** `std::vector/map/unordered_map/set/queue/priority_queue/sort/
   function/unique_ptr`...  `std::unordered_map` is allowed **only in `src/tools/`** as
   the reference/baseline.
   Check with: `grep -rhoE "std::[a-z_0-9]+" src/core src/app | sort | uniq -c`
2. **Hash table** = separate chaining (singly linked lists), FNV-1a, capacity doubles +
   full rehash when `count/capacity > 0.75`.
3. **Ray casting once per click**, not per frame.  Picking must work for 100,000 nodes.
4. **raylib is for graphics only.**  Core never includes raylib (that is what makes it
   unit-testable headless).
5. For huge graphs (> 3,000 nodes or > 30,000 arcs) rendering is scoped to the active
   neighbourhood ("large mode").
6. Coding style: **free functions + plain structs, no classes**, explicit `Init/Destroy`,
   `T*&` only when the callee reassigns the caller's pointer.

---

## 2. Build / test / run

```bash
make                      # needs raylib; set RAYLIB_INC=... RAYLIB_LIB=... if not on path
make test                 # no raylib needed — run after EVERY change to src/core
make asan                 # same tests under -fsanitize=address,undefined (Linux/WSL/clang)
make bench
```

MSYS2 UCRT64 quick-start:
```bash
pacman -S mingw-w64-ucrt-x86_64-raylib mingw-w64-ucrt-x86_64-make
cd netviz
mingw32-make
./netviz.exe data/sample_network.txt
```

Headless GUI check on Linux (Xvfb):
```bash
LIBGL_ALWAYS_SOFTWARE=1 \
  NETVIZ_A=Server_London NETVIZ_B=Server_Sydney NETVIZ_K=2 NETVIZ_AUTOSHOT=25 \
  xvfb-run -a -s "-screen 0 1600x900x24" ./netviz data/sample_network.txt
# writes netviz_autoshot.png and exits cleanly
```

Environment hooks: `NETVIZ_A / NETVIZ_B / NETVIZ_K / NETVIZ_COLORS / NETVIZ_AUTOSHOT=<frame>`.

---

## 3. File map

```
src/core/   (no raylib — unit-testable headless)
  vec3.h  rng.h  timer.h  dyn_array.h  int_queue.h  min_heap.h     utilities / own containers
  node.h/.cpp          Node {id, pos, scene, index, component, out: DynArray<Edge>, in: DynArray<Node*>}
                        Edge {to, weight, undirectedLink}
  hash_table.h/.cpp    HashTable (id -> Node*), FNV-1a + 3 weak hashes for tests, stats
  graph.h/.cpp         Graph = HashTable + dense DynArray<Node*> + arcCount
  algorithms.h/.cpp    BfsKHop, DijkstraShortestPath, LabelComponents
  dataset_loader.h/.cpp  two-pass text parser + LoadReport; WriteDatasetFile (serializer)
  generator.h/.cpp     GenerateSyntheticGraph (golden-angle sphere + seeded links)
  orbit_camera.h/.cpp  OrbitCamera maths, ScreenPointToRay (perspective unprojection)
  picking.h/.cpp       RaySphereIntersect, PickNode

src/app/   (raylib)
  app_state.h/.cpp     AppState + selection logic (raylib-free), dataset install/reload, metrics
  input.h/.cpp         mouse/keyboard -> AppState (click vs drag, search box, hotkeys, file drop)
  renderer.h/.cpp      3D scene: dual-shader (lit sphere + additive glow halo), edges, labels
  ui.h/.cpp            2D panels (column-aligned rows), search overlay, help + load-report
  main.cpp             window (1600×900, MSAA 4×, VSYNC), main loop, arg parsing, dev hooks

src/tools/   tests.cpp  benchmark.cpp  gen_dataset.cpp

data/sample_network.txt   illustrative 21-node GEO example
```

---

## 4. Key design decisions and invariants

* **Node ownership.** `Graph` owns every `Node` (heap-allocated, pointer-stable).
  `HashTable` stores `Node*` and **does not own them**.  Edges store `Node*` (not ids).
* **Dense index.** `node->index` = position in `Graph::nodes`.  `GraphRemoveNode`
  swap-removes (and updates the last node's index), so **indices are only stable between
  removals** — never cache across a removal.
* **Arcs.** An undirected link = two arcs (a→b, b→a), both with `undirectedLink = true`.
  Renderers draw it once (lower-index end), no arrowhead.
* **Duplicate arcs are merged** (smaller weight wins).  Self-loops, negative/NaN/inf
  weights, unknown endpoints are rejected by `GraphAddEdgeNodes`.
* **Hash table:** capacity is always a power of two; bucket = `(h ^ (h >> 32)) & (cap-1)`.
  Entries cache the full 64-bit hash.  No shrinking on delete (by design).
* **DynArray<T>** is `realloc`-based → T must be trivially copyable (`static_assert`).
  Never put `std::string` in one.
* **Scene normalisation.** `Node::pos` = raw file coordinate; `Node::scene` = `(pos -
  center) * scale`, fitted to radius-100 sphere.  Renderer and picking use only `scene`.
* **Parser** (`ParseDatasetText`): two passes.  Pass 1 handles directives + `NODE` lines
  and queues `EDGE` lines (token pointers into the mutated buffer); pass 2 resolves edges.
  Directives (`DEFAULT_LINK`, `COORDS`) are file-global.
* **Name clash trap:** raylib defines `Ray`, `Camera`, `Vector3`, `Color` etc.  Core types
  are therefore `PickRay`, `Vec3`, `OrbitCamera`.
* **Raycast accuracy.** `ScreenPointToRay` validated against raylib's projection to
  0.0003 px.  Picking inflates every sphere to ≥ 7 px so 100 k-graph nodes stay clickable.
* **Large mode** (`AppState::largeMode`): all nodes drawn as ~2 px dashes; only the k-hop
  set (≤ 3,000 spheres / 20,000 edges) and the route are drawn in full detail.
* **Dual-shader renderer** (rev 2): `Renderer` now holds two shaders and two materials.
  `sphereShader` / `sphereMat` = lit (key-light + rim + specular).
  `glowShader` / `glowMat` = Fresnel-edge glow, rendered additive.
  `RendererShutdown` frees both; `RendererInit` gracefully falls back to `DrawSphereEx`
  if either shader fails to compile (`lit` / `glowLit` flags).

---

## 5. Complexity cheat-sheet (for the report / presentation)

| Operation | Cost |
|---|---|
| hash insert / find / remove | O(1) average, O(chain) worst; rehash O(n) amortised O(1) |
| add edge | O(deg(a)) — duplicate scan |
| remove node | O(deg) arcs + O(1) table + O(1) dense swap |
| BFS k-hop | O(V) setup (hop array) + O(visited · deg) |
| Dijkstra (lazy-deletion binary heap) | O((V+E) log V) worst, early exit when target settled |
| component labelling | O(V+E) |
| ray pick | O(V) once per click |

---

## 6. Known limitations / honest notes

1. **BFS setup is O(V)**, not O(degree): `BfsKHop` allocates/clears per-node arrays each
   call (~20 µs at 100 k nodes).  Epoch-stamped scratch would make k=1 truly O(degree).
2. `GraphAddEdgeNodes` scans `out` for duplicates: O(deg).  A node with ~10⁵ neighbours
   would make loading quadratic.
3. Large mode draws one `DrawLine3D` per node/edge each frame.  Upgrade path: static VBO.
4. Globe datasets draw edges as straight chords; great-circle arcs would look better.
5. Labels can overlap (no collision avoidance).
6. Pick tolerance is 7 px for every node — two nodes closer than that resolve to the
   nearer-to-camera one.
7. No undo for `Delete`; F5 reloads the file.
8. No Windows/macOS build was executed in the original authoring environment (Linux only).
9. `IsKeyPressedRepeat` requires raylib ≥ 5.0.
10. The glow halo is a geometry-based Fresnel approximation, not a screen-space blur.
    For a true bloom effect, render the scene to a framebuffer texture and apply a Gaussian
    blur pass with `rlgl`.

---

## 7. Good next tasks (rough priority order)

* Screen-space bloom via framebuffer + Gaussian blur (replaces geometry glow, looks better
  at large node counts, cheaper for many glowing nodes simultaneously).
* Static-buffer rendering for large mode (limitation 3) + frustum culling.
* Epoch-stamped BFS scratch (limitation 1).
* Great-circle edge arcs for GEO datasets; screen-space label de-overlap.
* A* / bidirectional Dijkstra, k-shortest paths, betweenness-based node sizing.
* Edge picking (click an edge to see its latency).
* In-app add/remove nodes and edges + export button.
* CI: GitHub Actions running `make test`, `make asan`, and the headless screenshot.

---

## 8. Checklist before you hand changes back

- [ ] `make test` and `make asan` pass (add tests for new core behaviour in `tests.cpp`).
- [ ] No new STL containers in `src/core` or `src/app` (grep from §1).
- [ ] Core still compiles without raylib (`make test` proves it).
- [ ] README / this file updated if controls, file format, or invariants changed.
- [ ] If you touched rendering/input: run the headless screenshot + (optionally) xdotool script.
- [ ] If you added a shader: add a `lit` / `glowLit` style fallback flag so it degrades
  gracefully on software GL.
