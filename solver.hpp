#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace temple {

// ─── Tile types ───────────────────────────────────────────────────────────────
//
// Side encoding: 0=top, 1=right, 2=bottom, 3=left
//
// Level-1 tiles (upper floor): A, B, A1, B1, C, C1
// Level-0 tiles (ground floor): F, G, H, I
// Stair tiles (connect levels): D, E, D1, E1
// Blank tile (empty slot):       BL
enum TileID : uint8_t {
    TID_A  =  0,  ///< Level 1, opens top+right    (sides 0,1)
    TID_B  =  1,  ///< Level 1, opens right+bottom (sides 1,2)
    TID_A1 =  2,  ///< Level 1, opens bottom+left  (sides 2,3)
    TID_B1 =  3,  ///< Level 1, opens left+top     (sides 3,0)
    TID_C  =  4,  ///< Level 1, opens top+bottom   (sides 0,2)
    TID_C1 =  5,  ///< Level 1, opens right+left   (sides 1,3)
    TID_D  =  6,  ///< Stair, face = top   (side 0)
    TID_E  =  7,  ///< Stair, face = right (side 1)
    TID_D1 =  8,  ///< Stair, face = bot   (side 2)
    TID_E1 =  9,  ///< Stair, face = left  (side 3)
    TID_F  = 10,  ///< Level 0, opens top+right    (sides 0,1)
    TID_G  = 11,  ///< Level 0, opens right+bottom (sides 1,2)
    TID_H  = 12,  ///< Level 0, opens bottom+left  (sides 2,3)
    TID_I  = 13,  ///< Level 0, opens left+top     (sides 3,0)
    TID_BL = 14,  ///< Blank tile (empty slot)
};

/// Static, immutable properties for each tile type.
struct TileInfo {
    uint8_t     open_mask;    ///< Bitmask: bit i = side i is open
    bool        is_stair;     ///< True for D, E, D1, E1
    int8_t      fixed_level;  ///< 0=ground, 1=upper, -1=stair or blank
    uint8_t     stair_face;   ///< For stair tiles: the upper-opening side index
    const char* name;         ///< Human-readable tile name
};

/// Tile property table, indexed by TileID (0–14).
extern const TileInfo TILE_INFO[15];

// ─── State ────────────────────────────────────────────────────────────────────
//
// Key packing (53 bits → uint64_t):
//
//   bits  0-44 : 9 cells × 5 bits  (4-bit tile_id | 1-bit stair_lvl)
//   bits 45-48 : pawn_pos  (4 bits, 0-8)
//   bits 49-52 : blank_pos (4 bits, 0-8)
//
// This makes equality checks, hashing, and closed-set membership O(1)
// with no heap allocation.
using StateKey = uint64_t;

/// Full board state.
///
/// tile_id[i]   - which TileID occupies cell i (row-major, 0=top-left)
/// stair_lvl[i] - runtime level (0 or 1) for stair tiles; undefined for non-stair
struct State {
    std::array<uint8_t, 9> tile_id;
    std::array<uint8_t, 9> stair_lvl;
    uint8_t pawn_pos;
    uint8_t blank_pos;

    [[nodiscard]] StateKey key() const noexcept;
};

// ─── Board configuration ──────────────────────────────────────────────────────

/// Produces the canonical initial game state as defined by the puzzle.
class BoardConfig {
public:
    [[nodiscard]] static State initial_state() noexcept;
};

// ─── Solution path ────────────────────────────────────────────────────────────

/// One step in the solution path — a snapshot of the board at that step.
struct StepInfo {
    std::array<uint8_t, 9> tile_id;
    std::array<uint8_t, 9> stair_lvl;
    uint8_t pawn_pos;
    uint8_t blank_pos;
};

/// Write the solution path to a JSON file readable by the visualizer.
void write_path_json(const std::vector<StepInfo>& path,
                     const std::string& filename);

// ─── Solver ───────────────────────────────────────────────────────────────────

/// A* solver for the Temple Trap puzzle.
///
/// Design choices:
///   - StateKey (uint64_t) as the hash key → O(1) lookup, no string allocation
///   - std::unordered_map with pre-reserved buckets → minimises rehashing
///   - std::priority_queue with lazy deletion → simple and cache-friendly
///   - Admissible heuristic: Manhattan distance + level penalty
class TempleSolver {
public:
    TempleSolver() = default;

    /// Solve from @p initial.  Returns the path (start→goal inclusive),
    /// or an empty vector if no solution exists.
    [[nodiscard]] std::vector<StepInfo> solve(const State& initial);

private:
    [[nodiscard]] float heuristic(const State& s) const noexcept;
    [[nodiscard]] bool  is_goal  (const State& s) const noexcept;

    void gen_pawn_successors(const State& s, std::vector<State>& out) const;
    void gen_tile_successors(const State& s, std::vector<State>& out) const;
};

} // namespace temple
