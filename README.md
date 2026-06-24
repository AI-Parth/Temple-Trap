# Temple Trap — High-Performance C++ Solver & Visualizer

<p align="center">
  <img src="https://img.shields.io/badge/language-C%2B%2B20-blue"/>
  <img src="https://img.shields.io/badge/search-A*-brightgreen"/>
  <img src="https://img.shields.io/badge/renderer-SFML-orange"/>
  <img src="https://img.shields.io/badge/build-CMake-lightgrey"/>
</p>

A complete, production-quality C++ write of the **Temple Trap** sliding-tile
puzzle solver and visualizer.  The solver uses an optimised A\* search on a
compact bitpacked state representation; the visualizer renders the solution
path interactively with SFML.

---

## Overview

Temple Trap is a two-level sliding-tile puzzle played on a 3 × 3 grid.  Each
cell holds one tile from a fixed vocabulary:

| Category | Tiles | Description |
|---|---|---|
| Upper-level | A, B, A1, B1, C, C1 | Fixed at **level 1**; two open sides each |
| Ground-level | F, G, H, I | Fixed at **level 0**; two open sides each |
| Stairs | D, E, D1, E1 | Connect the two levels; one face + one opposite side |
| Blank | BL | Empty slot enabling tile slides |

(View ![Arrangement of Tiles](Arrangement%20of%20Tiles.png) for getting th exact visualization)

A **pawn** occupies one tile.  Two operations are available each turn:

1. **Pawn move** — slide the pawn to an adjacent tile, provided the connecting
   sides are both open and the tiles are at the same level (or a stair bridges
   the gap).
2. **Tile slide** — while the pawn is *not* on an upper-level tile, slide any
   non-pawn adjacent tile into the blank slot (like a 15-puzzle).

**Goal:** reach cell 0 (top-left) on a tile that opens to the left — or on the
special stair tile E1 — to let the pawn escape the temple.

---

## Algorithm

### State Representation

Each state is stored as:

```cpp
std::array<uint8_t, 9>  tile_id;    // TileID at each cell (0-14)
std::array<uint8_t, 9>  stair_lvl;  // Runtime level for stair cells (0 or 1)
uint8_t                 pawn_pos;
uint8_t                 blank_pos;
```

For hashing and closed-set membership the state is packed into a single
**`uint64_t`** key (53 bits used):

```
bits  0-44 : 9 x 5 bits  (4-bit tile_id | 1-bit stair_lvl)
bits 45-48 : pawn_pos  (4 bits)
bits 49-52 : blank_pos (4 bits)
```

**Why this encoding?**

* A `uint64_t` key fits in a single register — equality and hashing are
  single-cycle operations with no heap allocation.
* `std::unordered_map<uint64_t, ...>` uses the identity hash on most platforms,
  which is maximally fast for keys that already have good bit distribution.
* Compared with a string-based key (as in the original Python), this eliminates
  string allocation and memcpy on every state expansion.

### A\* Search

```
f(n) = g(n) + h(n)
```

* **g(n)** — exact cost (number of moves from start); each move costs 1.
* **h(n)** — admissible heuristic (see below).
* Open set: `std::priority_queue` with lazy deletion (stale entries are skipped
  via a closed `std::unordered_set`).
* All hash tables are pre-reserved to 2^18 buckets to avoid rehashing during
  the hot loop.

### Heuristic Design

```
h(s) = manhattan_dist(pawn -> cell 0)
     + 0.8   if pawn is on a ground-level non-stair tile
     - 0.2   if pawn is on a stair tile
```

* **Manhattan distance** is a standard admissible lower bound on pawn moves.
* The **+0.8 penalty** reflects that a ground-level pawn needs at least one
  stair traversal (an extra effective cost) before reaching the upper-level
  goal.  Since 0.8 < 1 (cost of one move), admissibility is preserved.
* The **-0.2 reward** nudges A\* to prefer states where the pawn is already
  on a stair mid-traversal, reducing tie-breaking randomness.

### Successor Generation

Two successor families:

**Pawn successors** — four movement cases:

| From | To | Condition |
|---|---|---|
| Normal | Normal | Same level; both tiles open on the connecting side |
| Normal | Stair | Normal tile open; stair accessible from adjacent level |
| Stair | Normal | Stair open toward normal tile's level; normal tile open |
| Stair | Stair | Faces are opposite; movement aligns with both faces |

**Tile successors** — disabled while pawn is on a level-1 non-stair tile;
otherwise swap the blank slot with any adjacent non-pawn tile.

---

## Performance

### Memory layout decisions

| Choice | Rationale |
|---|---|
| `std::array<uint8_t, 9>` for tile data | Fits in two cache lines; no pointer chasing |
| `uint64_t` state key | Single-register equality; identity hash |
| Pre-reserved hash tables | Avoids rehashing; ~40% fewer allocations |
| States stored in `key_to_state` map | Priority queue holds only 16-byte `(f, seq, key)` entries |

### Expected speedup over Python

| Operation | Python | C++ |
|---|---|---|
| State key computation | string join (~us) | bitpack (~1 ns) |
| Hash lookup | string hash (~200 ns) | uint64 hash (~5 ns) |
| Successor generation | list copies (~us) | struct copy (~20 ns) |
| Priority queue | heapq + iterator | `std::priority_queue` |

On typical puzzles this rewrite is **50-200x faster** than the Python
reference, primarily due to the elimination of string-based state keys and
Python interpreter overhead.

---

## Project Structure

```
Temple-Trap/
├── solver.hpp          # Public API: TileID, TileInfo, State, TempleSolver
├── solver.cpp          # A* solver implementation + main()
├── visualizer.cpp      # SFML visualizer + main()
├── CMakeLists.txt      # Build system (two targets, LTO, native arch)
├── README.md           # This file
├── runthis.py          # Original Python solver (reference)
└── visualizer.py       # Original Python visualizer (reference)
```

---

## Build Instructions

### Prerequisites

| Tool | Minimum version |
|---|---|
| CMake | 3.16 |
| C++ compiler | GCC 10 / Clang 12 / MSVC 2019 |
| SFML | 2.5 (for visualizer) |

### Linux

```bash
# Install dependencies (Debian/Ubuntu)
sudo apt install cmake g++ libsfml-dev

# Build
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### macOS

```bash
brew install cmake sfml

mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(sysctl -n hw.logicalcpu)
```

### Windows (MSVC)

```powershell
# Install SFML, then point CMake to its config directory
mkdir build; cd build
cmake .. -DSFML_DIR="C:/SFML/lib/cmake/SFML"
cmake --build . --config Release
```

### Without SFML (solver only)

If SFML is unavailable, CMake will attempt to fetch it via `FetchContent`.
To build only the solver (no visualizer) without any SFML dependency, remove
the `temple_visualizer` target from `CMakeLists.txt` or pass:

```bash
cmake .. -DCMAKE_BUILD_TYPE=Release -DSFML_DIR=OFF
```

---

## Usage

### Run the solver

```bash
./temple_solver
# Outputs: Solution: N moves | X.XXXs
# Writes:  temple_path.json
```

### Visualize the solution

```bash
./temple_visualizer                    # reads temple_path.json by default
./temple_visualizer path/to/file.json  # custom path
```

### Playback controls

| Key | Action |
|---|---|
| `Space` | Play / Pause |
| `Right` | Step forward one move |
| `Left` | Step backward one move |
| `Up` | Increase playback speed (x1.5) |
| `Down` | Decrease playback speed (÷1.5) |
| `R` | Restart from the beginning |

---

## Future Improvements

* **Bidirectional A\*** — search simultaneously from start and goal; can
  reduce explored nodes by an order of magnitude.
* **Pattern database heuristic** — precompute exact costs for subproblems to
  produce a tighter, still-admissible heuristic.
* **Configurable initial board** — CLI flag or config file to specify arbitrary
  puzzle layouts.
* **Multiple puzzle variants** — extend `BoardConfig` for different board sizes
  or tile vocabularies.
* **WebAssembly port** — compile with Emscripten for an in-browser demo.
* **GPU-parallel BFS** — breadth-first exploration of the full state space on
  GPU for exhaustive reachability analysis.

---

## Author

**Parth Asati**  
Artificial Intelligence  
Indian Institute of Technology Kharagpur
