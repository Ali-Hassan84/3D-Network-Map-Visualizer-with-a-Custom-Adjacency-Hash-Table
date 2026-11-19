# AI_HANDOFF.md — read this first if you are an AI (or human) continuing this project

**Project:** NetViz 3D — "3D Network Map Visualizer with a Custom Adjacency Hash Table" (university DSA project, C++17 + raylib 5.x).
**Status:** feature-complete against the project proposal; all tests pass (5,399 checks, also under ASan/UBSan); the GUI was
run headlessly (Xvfb + Mesa) and driven with synthetic mouse/keyboard input, ASan/LSan clean.
**Primary user environment:** Windows, VS Code, g++ from MSYS2 UCRT64 (so: keep code portable, no POSIX-only APIs, no
compiler extensions). Authors present it in a class demo, so code should stay *explainable* — prefer plain, readable code.

---

## 1. Hard constraints (from the proposal — do not break)
1. **STL policy.** Graph structures and memory management are custom. Allowed in `src/core` and `src/app`:
   `std::string`, C library headers (`<cstdio> <cstdlib> <cstring> <cmath> ...`), `<chrono>` (timing only), `<type_traits>`
   (a `static_assert`). **Forbidden there:** `std::vector/map/unordered_map/set/queue/priority_queue/sort/function/unique_ptr`...
   `std::unordered_map` is allowed **only in `src/tools/`** as the reference/baseline. Check with:
   `grep -rhoE "std::[a-z_0-9]+" src/core src/app | sort | uniq -c`
2. **Hash table** = separate chaining (singly linked lists), FNV-1a, capacity doubles + full rehash when `count/capacity > 0.75`.
3. **Ray casting once per click**, not per frame. Picking must work for 100,000 nodes.
4. **raylib is for graphics only.** Core never includes raylib (that is what makes it unit-testable headless).
5. For huge graphs (> 3,000 nodes or > 30,000 arcs) rendering is scoped to the active neighbourhood ("large mode").
6. Coding style the authors use: **free functions + plain structs, no classes**, explicit `Init/Destroy`, `T*&` only when the
   callee reassigns the caller's pointer. Keep it that way.

## 2. Build / test / run
```
make                      # needs raylib (see README §1);  RAYLIB_INC=... RAYLIB_LIB=... if not on the default path
make test                 # no raylib needed — run after EVERY change to src/core
make asan                 # same tests under -fsanitize=address,undefined (Linux/WSL/clang)
make bench
```
Headless GUI check on Linux (this is how the GUI was verified; reuse it):
```
LIBGL_ALWAYS_SOFTWARE=1 NETVIZ_A=Server_London NETVIZ_B=Server_Sydney NETVIZ_K=2 NETVIZ_AUTOSHOT=25 \
  xvfb-run -a -s "-screen 0 1366x768x24" ./netviz data/sample_network.txt      # writes netviz_autoshot.png and exits cleanly
```
`NETVIZ_A / NETVIZ_B / NETVIZ_K / NETVIZ_COLORS / NETVIZ_AUTOSHOT=<frame>` are developer hooks in `main.cpp`.
For scripted input use `xdotool`; **hold keys/buttons ≥ 100 ms** (`keydown; sleep 0.1; keyup`) — raylib polls once per frame, so
instantaneous `xdotool click/key` events are missed (this looked like a bug once; it is a test artefact).

## 3. File map
```
src/core/   (no raylib)
  vec3.h  rng.h  timer.h  dyn_array.h  int_queue.h  min_heap.h     small utilities / own containers
  node.h/.cpp          Node {id, pos, scene, index, component, out: DynArray<Edge>, in: DynArray<Node*>}, Edge {to, weight, undirectedLink}
  hash_table.h/.cpp    HashTable (id -> Node*), FNV-1a + 3 deliberately weak hashes for tests, stats
  graph.h/.cpp         Graph = HashTable + dense DynArray<Node*> + arcCount; add/remove node & edge; GraphFitScene
  algorithms.h/.cpp    BfsKHop, DijkstraShortestPath, LabelComponents
  dataset_loader.h/.cpp  two-pass text parser + LoadReport; WriteDatasetFile (serializer)
  generator.h/.cpp     GenerateSyntheticGraph (golden-angle sphere + seeded links)
  orbit_camera.h/.cpp  OrbitCamera maths, ScreenPointToRay (perspective unprojection)
  picking.h/.cpp       RaySphereIntersect, PickNode
src/app/    (raylib)
  app_state.h/.cpp     AppState + selection logic (raylib-free!), dataset install/reload, metrics measurement
  input.h/.cpp         mouse/keyboard -> AppState (click vs drag, search box, hotkeys, file drop)
  renderer.h/.cpp      3D scene: lit sphere mesh + shader (falls back to DrawSphereEx), edges, arrows, globe, labels
  ui.h/.cpp            2D panels, search box, help + load-report overlays
  main.cpp             window loop, arg parsing, dev hooks
src/tools/   tests.cpp  benchmark.cpp  gen_dataset.cpp
data/sample_network.txt   illustrative 21-node GEO example (NOT measured data)
```

## 4. Key design decisions and invariants (violating these causes subtle bugs)
* **Node ownership.** `Graph` owns every `Node` (heap-allocated, pointer-stable). `HashTable` stores `Node*` and **does not own
  them**; it owns only its `HashEntry` cells. Edges store `Node*` (not ids), so traversal never hashes.
* **Dense index.** `node->index` is the position in `Graph::nodes`; algorithms use it for per-node scratch arrays.
  `GraphRemoveNode` swap-removes (moves the last node into the hole and updates its `index`), so **indices are only stable
  between removals** — never cache an index across a removal. `AppDeleteSelected` clears selection first for this reason.
* **Arcs.** An undirected link = two arcs (a→b, b→a), both with `undirectedLink = true`; renderers draw it once (lower index
  end) with no arrow. Directed arcs get arrowheads. `arcCount` counts arcs (undirected link = 2).
* **Reverse adjacency** (`Node::in`) exists so removal is O(degree), in-degree is O(1), and weak components can traverse both
  directions. Any code that adds/removes arcs must keep `out` and `in` consistent — use `AddArc/RemoveArc` in `graph.cpp`.
* **Duplicate arcs are merged** (smaller weight wins). Self-loops, negative/NaN/inf weights, unknown endpoints are rejected by
  `GraphAddEdgeNodes` (returns an `EdgeStatus`).
* **Hash table:** capacity is always a power of two; bucket index = `(h ^ (h >> 32)) & (cap-1)` (the xor-fold matters: FNV's
  multiply only propagates entropy upwards). Entries cache the full 64-bit hash. No shrinking on delete (by design).
* **DynArray<T>** is `realloc`-based → **T must be trivially copyable** (enforced by `static_assert`). Never put `std::string`
  in one.
* **Scene normalisation.** `Node::pos` is the raw file coordinate (shown in the UI); `Node::scene` = `(pos - center) * scale`,
  fitted to a sphere of radius 100 by `GraphFitScene` (GEO files: `GraphUseGeoScene`, radius-100 globe). Renderer and picking
  use only `scene`. After adding nodes programmatically, call `GraphFitScene` again.
* **Parser** (`ParseDatasetText`): in-memory, two passes. Pass 1 handles directives + `NODE` and *queues* `EDGE` lines (token
  pointers into the mutated buffer — do not free the buffer before pass 2); pass 2 resolves edges, so node/edge order in the
  file is irrelevant. Directives (`DEFAULT_LINK`, `COORDS`) are **file-global**, wherever they appear (`COORDS` must precede the
  first `NODE`). Auto-placed nodes are temporarily tagged `component = -2` (reset to -1 afterwards).
* **Name clash trap:** raylib defines `Ray`, `Camera`, `Vector3`, `Color`... Core types are therefore `PickRay`, `Vec3`, `OrbitCamera`.
  Don't introduce core names that collide with raylib; don't include `raymath.h` in files that define `Clamp/Lerp`-style helpers.
* **Raycast accuracy.** `ScreenPointToRay` was validated against raylib's projection to 0.0003 px (raylib's own
  `GetScreenToWorldRay` is *less* precise because it unprojects to a far plane in `float`). Picking inflates every sphere to
  ≥ 7 px on screen so tiny 100k-graph nodes stay clickable.
* **Large mode** (`AppState::largeMode`): all nodes drawn as ~2 px dashes (cheap), only the k-hop set (capped at 3,000 spheres /
  20,000 edge lines) and the route are drawn in full detail. Constants are at the top of `app_state.h`.
* **Lit sphere shader** is GLSL 330 (headlight + key light). If it fails to compile `Renderer::lit` is false and
  `DrawSphereEx` is used. `viewPos` must be set every frame (done in `RenderScene`).
* **Status line** timing uses `AppState::now` (set each frame in `AppHandleInput`).

## 5. Complexity cheat-sheet (for the report/presentation)
| Operation | Cost |
|---|---|
| hash insert / find / remove | O(1) average, O(chain) worst; rehash O(n) amortised O(1) |
| add edge | O(deg(a)) (duplicate scan) |
| remove node | O(deg) arcs + O(1) table + O(1) dense swap |
| BFS k-hop | O(V) setup (hop array) + O(visited · deg) |
| Dijkstra (lazy-deletion binary heap) | O((V+E) log V) worst, early exit when target settled |
| component labeling | O(V+E) |
| ray pick | O(V) once per click |

## 6. Known limitations / honest notes
1. **BFS setup is O(V)**, not O(degree): `BfsKHop` and `AppRecompute` allocate/clear per-node arrays each call (~20 µs at 100k
   nodes). To make k = 1 truly O(degree): keep a persistent scratch array in `Graph` with epoch stamps and reset only touched
   entries. The proposal's "O(degree) highlighting" claim is about the hash lookup + adjacency access, which *is* O(1)+O(degree).
2. `GraphAddEdgeNodes` scans `out` for duplicates: O(deg). A node with ~10⁵ neighbours would make loading quadratic.
   Fix if needed: per-node tiny hash set, or sort-then-dedup at load time.
3. Large mode draws one `DrawLine3D` per node/edge each frame (fine on a GPU; ~18 FPS on CPU-only software GL at 100k).
   Upgrade path: upload a static vertex buffer (`rlgl`) once per dataset and draw it with one call.
4. Globe datasets draw edges as straight chords through the globe; great-circle arcs lifted above the surface would read better.
5. Labels can overlap (no collision avoidance); labels of far-side globe nodes are hidden, nodes/edges are not.
6. Pick tolerance is 7 px for *every* node, so two nodes closer than that resolve to the nearer-to-camera one.
7. No undo for `Delete`; `F5` reloads the file. There is no UI for adding nodes/edges (the API supports it).
8. No Windows/macOS build was executed in the authoring environment (Linux only); the Makefile has Windows/macOS branches and
   sticks to portable APIs, but the first Windows build may need `RAYLIB_INC/RAYLIB_LIB` tweaks.
9. Key auto-repeat (Backspace in the search box, Up/Down for k) uses raylib `IsKeyPressedRepeat`, which needs raylib ≥ 5.0.

## 7. Good next tasks (in rough priority order)
* Static-buffer rendering for large mode (limitation 3) and frustum culling of highlighted sets.
* Epoch-stamped BFS scratch (limitation 1) + show "k-hop work = nodes touched" in the panel.
* Great-circle edge arcs for GEO datasets; screen-space label de-overlap.
* A* / bidirectional Dijkstra option, k-shortest paths, betweenness-based node sizing.
* Edge picking (click an edge to see its latency); in-app add/remove nodes and edges + `WriteDatasetFile` export button.
* Optional: shrink the hash table on heavy deletion (load factor < 0.25) with a test.
* CI: GitHub Actions running `make test`, `make asan`, and the headless screenshot on Ubuntu.

## 8. Checklist before you hand changes back
- [ ] `make test` and `make asan` pass (add tests for new core behaviour in `src/tools/tests.cpp`).
- [ ] No new STL containers in `src/core` or `src/app` (grep from §1).
- [ ] Core still compiles without raylib (`make test` proves it).
- [ ] README / this file updated if controls, file format, or invariants changed.
- [ ] If you touched rendering/input: run the headless screenshot + (optionally) the xdotool script idea from §2.
