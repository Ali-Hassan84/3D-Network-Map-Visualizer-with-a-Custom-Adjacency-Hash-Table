# NetViz 3D — Network Map Visualizer with a Custom Adjacency Hash Table

An interactive C++ / raylib application that draws a network (e.g. global server infrastructure) as 3D spheres
joined by edge lines, and lets you inspect it live: click a node, expand its k-hop neighbourhood, compute the
lowest-latency route between two nodes, and watch the hash-table metrics update in real time.

![Route London → Sydney](docs/screenshot_route.png)

| Proposal requirement | Where it lives |
|---|---|
| Custom hash table: string keys, separate chaining, FNV-1a, doubles + rehashes at load factor > 0.75 | `src/core/hash_table.*` |
| Adjacency lists with weights inside each node record | `src/core/node.h`, `src/core/graph.*` |
| BFS (k-hop, components) with a queue; Dijkstra with a priority queue | `src/core/algorithms.*`, `int_queue.h`, `min_heap.h` |
| Mouse raycasting, once per click | `src/core/orbit_camera.*`, `src/core/picking.*` |
| Deterministic synthetic graphs (1k / 10k / 100k, fixed seeds) | `src/core/generator.*` |
| Dataset file parser + edge-case handling | `src/core/dataset_loader.*` |
| Benchmarks vs. `std::unordered_map` and array scan, chain statistics, weak-hash stress | `src/tools/benchmark.cpp` |
| Verification (100,000 random ops, ground-truth BFS/Dijkstra, edge cases) | `src/tools/tests.cpp` |
| raylib used only for window, camera feed, primitives, text | `src/app/*` |

---

## 1. Build

You need a C++17 compiler and **raylib 5.x**. The core (hash table, graph, algorithms, parser, tests, benchmarks)
does **not** need raylib — only the visualizer does.

### Windows (MSYS2 UCRT64 — same setup as VS Code + g++)
```bash
pacman -S mingw-w64-ucrt-x86_64-raylib mingw-w64-ucrt-x86_64-make
mingw32-make            # builds netviz.exe
mingw32-make test       # runs the test-suite
```
No `make`? Compile by hand from the project folder (MSYS2 UCRT64 shell):
```bash
g++ -std=c++17 -O2 -Isrc/core src/core/*.cpp src/app/*.cpp -o netviz.exe -lraylib -lopengl32 -lgdi32 -lwinmm
g++ -std=c++17 -O2 src/core/*.cpp src/tools/tests.cpp     -o netviz_tests.exe
g++ -std=c++17 -O2 src/core/*.cpp src/tools/benchmark.cpp -o netviz_bench.exe
g++ -std=c++17 -O2 src/core/*.cpp src/tools/gen_dataset.cpp -o netviz_gen.exe
```
If `pacman` cannot find raylib, download the Windows MinGW release from the raylib GitHub page and pass
`make RAYLIB_INC=<dir with raylib.h> RAYLIB_LIB=<dir with libraylib.a>`.

### Linux / macOS
Install raylib (package manager, or build from source), then `make`. On Linux you also need the X11/OpenGL dev
packages raylib itself needs (`libgl-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev`).

### Make targets
| Command | Result |
|---|---|
| `make` | the visualizer `netviz` |
| `make test` | all tests (≈5,400 checks, ~1 s) |
| `make bench` | hash-table benchmark + collision / weak-hash stress |
| `make gen` | `netviz_gen` dataset generator |
| `make asan` | tests under AddressSanitizer + UBSan (Linux/WSL/clang) |

---

## 2. Run

```
netviz                                  # opens data/sample_network.txt if it exists, else a drop-zone screen
netviz path/to/your_dataset.txt         # open a dataset
netviz --synthetic 100000 42            # generate 100,000 nodes in memory (seed 42) — no file needed
```
You can also **drag a `.txt` file onto the window** at any time.

Generate a synthetic dataset *file* (same format as below):
```
netviz_gen 10000 42 data/synthetic_10000.txt      # nodes, seed, output  [avgDegree, default 6]
```
Same arguments always give the identical graph (fixed seeds ⇒ reproducible benchmarks).

### Controls
| Input | Action |
|---|---|
| **Left click** a node | select it (**A**): highlights neighbours, k-hop rings, fills the analytics panel |
| **Shift + left click** another node | route target (**B**): lowest-latency path from A (Dijkstra) drawn in green |
| Left drag / Right drag / Wheel | orbit / pan / zoom (Shift + left drag also pans) |
| Click empty space, `Esc` | clear selection |
| `↑` `↓` or `1`–`9` | change **k** (hop radius) |
| `D` | traversal direction: both / outgoing / incoming |
| `/` or `Ctrl+F` | **search by id** (hash lookup). `Tab` switches between "select A" and "route to B". Substring suggestions appear while typing |
| `C` | colour nodes by connected component |
| `T` / `G` | toggle labels / globe wireframe (lat-lon datasets) |
| `F` / `Home` | focus selected node / reset view |
| `Delete` | delete the selected node (live hash-table removal) — `F5` reloads the dataset |
| `L` | load report: every skipped / repaired line with its line number |
| `H` | in-app help · `F12` screenshot |

### What the panels show
* **Dataset & hash table** — nodes / arcs, connected components, load time; buckets, load factor (must stay ≤ 0.75),
  resize count, **average and peak chain length**, average probes per successful lookup, and a histogram of chain lengths.
* **Lookup** — measured time of one hash `find` for the node you just selected (averaged over 2,000 repeats) next to a
  linear array scan of the same key, plus the speed-up. Also the cost of the last ray cast.
* **Selection & route** — id, coordinates, out/in degree, distinct neighbours, component, k-hop size and BFS time, the
  direct links with latencies, and for a route: total latency, hop count, nodes settled by Dijkstra, search time and the path.

---

## 3. Dataset format  ← how to structure your `.txt` file

Plain text, **one record per line**, fields separated by spaces or tabs. `#` starts a comment (anywhere on a line).
Blank lines are ignored. Keywords are case-insensitive. Windows (CRLF) line endings and a UTF-8 BOM are fine.

```text
NAME Global Server Network            # optional title shown in the panel
COORDS GEO                            # optional: CARTESIAN (default) or GEO — must come before the first NODE
DEFAULT_LINK UNDIRECTED               # optional: UNDIRECTED (default) or DIRECTED — applies to the whole file

NODE <id> <x> <y> <z>                 # COORDS CARTESIAN
NODE <id> <latitude> <longitude>      # COORDS GEO   (degrees; lat -90..90, lon -180..180)
NODE <id>                             # no coordinates: placed automatically on a sphere

EDGE <source_id> <target_id> <latency> [DIRECTED|UNDIRECTED]
```

### Rules
* **`id`** is the unique key: no spaces, no `#`, up to 128 characters, **case-sensitive**. Use underscores: `Server_London`.
* **`latency`** (the edge weight) is a number ≥ 0, e.g. milliseconds. Decimals are fine.
* An `EDGE` is **two-way by default** (stored as two arcs, drawn as one line). End it with `DIRECTED` for a one-way link
  (drawn with an arrowhead), or set `DEFAULT_LINK DIRECTED` to make every edge one-way unless it says `UNDIRECTED`.
* **Order does not matter:** `NODE` lines can appear after the `EDGE` lines that use them.
* **Coordinates:** in `CARTESIAN` mode use any units — the scene is auto-scaled to fit. In `GEO` mode just type real
  latitude / longitude; nodes are placed on a globe (east is to the right when viewed from outside).
* `NODE <id>` with no coordinates is auto-placed (deterministic golden-angle sphere), handy for quick tests.

### Example (CARTESIAN)
```text
NAME Tiny demo
NODE A   0   0   0
NODE B  50   0   0
NODE C  25  40  10
NODE D  25 -30 -20
EDGE A B 12.5
EDGE B C  8
EDGE A C 20
EDGE C D  5 DIRECTED     # one-way: C -> D
```

### What happens with bad input (the program never crashes; everything is reported with its line number — press `L`)
| Problem | Behaviour |
|---|---|
| duplicate `NODE` id | first one kept, later ones ignored |
| `EDGE` naming an unknown id (missing identifier) | edge skipped |
| self-loop (`EDGE A A 5`) | skipped |
| same edge listed twice (redundant) | merged, **smaller latency kept** |
| negative / non-numeric / NaN weight | edge skipped |
| wrong field count, unknown keyword, bad coordinates | line skipped |
| nodes with no edges (isolated) / separate islands | loaded; shown as their own components (`C` to colour) |
| empty file / no valid `NODE` line / file not found | load refused with a message; the current dataset stays open |

### Preparing the ~40-city real-world dataset
1. List ~40 hubs with real latitude/longitude (Wikipedia / Google Maps) and write them as `NODE` lines in `COORDS GEO` mode.
   Use ids like `Server_London`, `Server_Sydney` so the live demo (London → Sydney) works.
2. For each realistic link, add an `EDGE` with a latency. Use **real measured values** (e.g. published inter-city ping tables,
   or your own `ping`/`traceroute` runs) and cite the source. If you only estimate, say so — a rough fibre estimate is
   `latency_ms ≈ great_circle_km × 0.014`.
3. Make sure the graph is connected enough for interesting routes (every hub should have 2–4 links). Run the program and
   press `L` — the load report tells you about any typo.

`data/sample_network.txt` is a small **illustrative** example (not measured data) that demonstrates every feature, including a
one-way link, an isolated node and a separate island.

---

## 4. Tests, benchmarks, verification
```
make test     # hash table (FNV vectors, resize invariant, weak hashes, 100,000 random ops vs std::unordered_map),
              # graph edits, BFS / Dijkstra / components against hand-computed answers + brute-force Bellman-Ford,
              # parser edge cases, generator determinism, write→load round trip, ray picking maths, 100k-node scale
make bench    # insert / find / delete vs std::unordered_map and array scan at 1k, 10k, 100k; chain stats; weak-hash stress
```
Sample benchmark output (this machine, `-O2`; yours will differ):
```
 100000 | custom  insert 117  find 94  delete 129 ns/op        std::unordered_map  208 / 109 / 178 ns/op
        | array scan (find) 289,195 ns/op  -> hash is 3080x faster
        | chains: load 0.381  avg(used) 1.20  peak 6  probes/hit 1.19  resizes 14
 weak hash 'constant' n=10000: peak chain 10000, 12,801 ns/find   (FNV-1a: peak chain 5, 23 ns/find)
```
The visualizer itself was also exercised end-to-end under AddressSanitizer/UBSan/LeakSanitizer with scripted mouse and
keyboard input (select, route, delete, reload, search, overlays) with no findings.

![100,000 nodes](docs/screenshot_100k.png)

---

## 5. Notes for the live demo
1. `netviz data/your_network.txt` → point out node/arc counts, load factor, chain stats.
2. Press `/`, type `Server_London`, Enter → neighbours light up; read the **hash find vs array scan** line.
3. `↑` to widen to 2–3 hops; `D` to show direction handling.
4. `/`, `Tab`, type `Server_Sydney`, Enter → green route, total latency, nodes settled.
5. `C` to colour components; `Delete` to remove a node and watch the table stats and components update; `F5` restores.
6. `netviz --synthetic 100000 42` → large mode (point cloud + active neighbourhood only), still instant lookups.

**STL policy** (from the proposal): graph structures, hash table, queue, heap and dynamic arrays are all custom. The core uses
only `std::string`, C library headers, a `<chrono>` clock for timing and one `static_assert`. `std::unordered_map` appears
**only** in `src/tools/` as the reference to validate and benchmark against.

See **`AI_HANDOFF.md`** for architecture, invariants and ideas for further work.
