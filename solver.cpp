// solver.cpp — Temple Trap A* solver
// Compiles to the `temple_solver` binary.

#include "solver.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <queue>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace temple {

// ─── Tile property table ──────────────────────────────────────────────────────
//
// open_mask bitmask: bit 0 = top, bit 1 = right, bit 2 = bottom, bit 3 = left
//
// Stair tiles have open_mask = (1 << face) — only their face side is stored here
// for visual purposes; movement connectivity is computed from stair_face + caller
// level via tile_has_side() logic in the solver.

const TileInfo TILE_INFO[15] = {
    //  open_mask   stair  level  face  name
    { 0b0011u, false,  1, 0, "A"  },  //  0 TID_A  : top+right,   level 1
    { 0b0110u, false,  1, 0, "B"  },  //  1 TID_B  : right+bot,   level 1
    { 0b1100u, false,  1, 0, "A1" },  //  2 TID_A1 : bot+left,    level 1
    { 0b1001u, false,  1, 0, "B1" },  //  3 TID_B1 : left+top,    level 1
    { 0b0101u, false,  1, 0, "C"  },  //  4 TID_C  : top+bot,     level 1
    { 0b1010u, false,  1, 0, "C1" },  //  5 TID_C1 : right+left,  level 1
    { 0b0001u, true,  -1, 0, "D"  },  //  6 TID_D  : stair face=0 (top)
    { 0b0010u, true,  -1, 1, "E"  },  //  7 TID_E  : stair face=1 (right)
    { 0b0100u, true,  -1, 2, "D1" },  //  8 TID_D1 : stair face=2 (bot)
    { 0b1000u, true,  -1, 3, "E1" },  //  9 TID_E1 : stair face=3 (left)
    { 0b0011u, false,  0, 0, "F"  },  // 10 TID_F  : top+right,   level 0
    { 0b0110u, false,  0, 0, "G"  },  // 11 TID_G  : right+bot,   level 0
    { 0b1100u, false,  0, 0, "H"  },  // 12 TID_H  : bot+left,    level 0
    { 0b1001u, false,  0, 0, "I"  },  // 13 TID_I  : left+top,    level 0
    { 0b0000u, false, -1, 0, "BL" },  // 14 TID_BL : blank slot
};

// ─── State ────────────────────────────────────────────────────────────────────

StateKey State::key() const noexcept {
    uint64_t k = 0;
    for (int i = 0; i < 9; ++i) {
        k |= (static_cast<uint64_t>(tile_id[i])         << (i * 5));
        k |= (static_cast<uint64_t>(stair_lvl[i] & 1u) << (i * 5 + 4));
    }
    k |= (static_cast<uint64_t>(pawn_pos)  << 45);
    k |= (static_cast<uint64_t>(blank_pos) << 49);
    return k;
}

// ─── Initial board ────────────────────────────────────────────────────────────
//
// Layout from the original puzzle (row-major, 0=top-left):
//
//   B1  E   F
//   C1  I   [blank]
//   A1  G   E
//
// Pawn starts at position 4 (tile I, ground level).
// Blank slot is at position 5.

State BoardConfig::initial_state() noexcept {
    State s;
    s.tile_id   = { TID_B1, TID_E,  TID_F,
                    TID_C1, TID_I,  TID_BL,
                    TID_A1, TID_G,  TID_E };
    s.stair_lvl = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    s.pawn_pos  = 4;
    s.blank_pos = 5;
    return s;
}

// ─── Adjacency table ─────────────────────────────────────────────────────────
//
// For each cell 0-8 in the 3×3 grid, list the (neighbor, src_side, nbr_side)
// triples that can be reached in one step.
//
// Side encoding: 0=top, 1=right, 2=bottom, 3=left
// DELTA_TO_SIDE: -3→0, +1→1, +3→2, -1→3
// OPP_SIDE:  0↔2,  1↔3

struct NbrEntry {
    int8_t  pos;
    uint8_t src_side;
    uint8_t nbr_side;
};

struct NbrList {
    NbrEntry entries[4];
    uint8_t  count;
};

static constexpr NbrList NEIGHBORS[9] = {
    // 0: right=1, down=3
    { { {1,1,3},{3,2,0},{-1,0,0},{-1,0,0} }, 2 },
    // 1: left=0, right=2, down=4
    { { {0,3,1},{2,1,3},{4,2,0},{-1,0,0} }, 3 },
    // 2: left=1, down=5
    { { {1,3,1},{5,2,0},{-1,0,0},{-1,0,0} }, 2 },
    // 3: up=0, right=4, down=6
    { { {0,0,2},{4,1,3},{6,2,0},{-1,0,0} }, 3 },
    // 4: up=1, left=3, right=5, down=7
    { { {1,0,2},{3,3,1},{5,1,3},{7,2,0} }, 4 },
    // 5: up=2, left=4, down=8
    { { {2,0,2},{4,3,1},{8,2,0},{-1,0,0} }, 3 },
    // 6: up=3, right=7
    { { {3,0,2},{7,1,3},{-1,0,0},{-1,0,0} }, 2 },
    // 7: up=4, left=6, right=8
    { { {4,0,2},{6,3,1},{8,1,3},{-1,0,0} }, 3 },
    // 8: up=5, left=7
    { { {5,0,2},{7,3,1},{-1,0,0},{-1,0,0} }, 2 },
};

// ─── Stair connectivity helper ────────────────────────────────────────────────
//
// A stair tile has two physical openings:
//   • Upper opening  (face side)          — connects to a level-1 tile
//   • Lower opening  ((face+2)%4 side)    — connects to a level-0 tile
//
// Returns true if the stair tile at `stair_face` can be entered/exited from
// `side` when the adjacent normal tile has level `adj_level`.

static inline bool stair_has_side(int stair_face, int side, int adj_level) noexcept {
    if (adj_level == 0) return side == (stair_face + 2) % 4;
    else                return side == stair_face;
}

// ─── Successor generation ─────────────────────────────────────────────────────

void TempleSolver::gen_pawn_successors(const State& s,
                                        std::vector<State>& out) const {
    const int   cpos     = s.pawn_pos;
    const auto& cur_info = TILE_INFO[s.tile_id[cpos]];
    const int   cur_lvl  = cur_info.is_stair
                               ? static_cast<int>(s.stair_lvl[cpos])
                               : static_cast<int>(cur_info.fixed_level);
    const int   cur_face = static_cast<int>(cur_info.stair_face);

    const auto& nbrs = NEIGHBORS[cpos];
    for (int ni = 0; ni < nbrs.count; ++ni) {
        const int pos      = nbrs.entries[ni].pos;
        const int src_side = nbrs.entries[ni].src_side;
        const int nbr_side = nbrs.entries[ni].nbr_side;

        const uint8_t nxt_id = s.tile_id[pos];
        if (nxt_id == TID_BL) continue;

        const auto& nxt_info = TILE_INFO[nxt_id];
        const int   nxt_lvl  = nxt_info.is_stair
                                   ? static_cast<int>(s.stair_lvl[pos])
                                   : static_cast<int>(nxt_info.fixed_level);
        const int   nxt_face = static_cast<int>(nxt_info.stair_face);

        if (!cur_info.is_stair && !nxt_info.is_stair) {
            // ── Normal → Normal ───────────────────────────────────────────────
            // Requires same level and both tiles open on the connecting sides.
            if (cur_lvl != nxt_lvl) continue;
            if (!((cur_info.open_mask >> src_side) & 1u)) continue;
            if (!((nxt_info.open_mask >> nbr_side) & 1u)) continue;

            State ns      = s;
            ns.pawn_pos   = static_cast<uint8_t>(pos);
            out.push_back(ns);

        } else if (!cur_info.is_stair && nxt_info.is_stair) {
            // ── Normal → Stair (enter stair) ──────────────────────────────────
            if (!((cur_info.open_mask >> src_side) & 1u)) continue;
            if (!stair_has_side(nxt_face, nbr_side, cur_lvl)) continue;

            State ns           = s;
            ns.pawn_pos        = static_cast<uint8_t>(pos);
            ns.stair_lvl[pos]  = static_cast<uint8_t>(1 - cur_lvl);
            out.push_back(ns);

        } else if (cur_info.is_stair && !nxt_info.is_stair) {
            // ── Stair → Normal (exit stair) ───────────────────────────────────
            if (!stair_has_side(cur_face, src_side, nxt_lvl)) continue;
            if (!((nxt_info.open_mask >> nbr_side) & 1u)) continue;

            State ns    = s;
            ns.pawn_pos = static_cast<uint8_t>(pos);
            out.push_back(ns);

        } else {
            // ── Stair → Stair ─────────────────────────────────────────────────
            // Both faces must be opposite, and the movement must align with faces.
            if ((cur_face + 2) % 4 != nxt_face) continue;
            if (src_side != cur_face)            continue;
            if (nbr_side != nxt_face)            continue;

            State ns           = s;
            ns.pawn_pos        = static_cast<uint8_t>(pos);
            ns.stair_lvl[pos]  = static_cast<uint8_t>(cur_lvl);
            out.push_back(ns);
        }
    }
}

void TempleSolver::gen_tile_successors(const State& s,
                                        std::vector<State>& out) const {
    // Tile sliding is disabled while the pawn stands on an upper-level non-stair tile.
    const auto& p_info = TILE_INFO[s.tile_id[s.pawn_pos]];
    if (!p_info.is_stair && p_info.fixed_level == 1) return;

    const int   blank = s.blank_pos;
    const auto& nbrs  = NEIGHBORS[blank];

    for (int ni = 0; ni < nbrs.count; ++ni) {
        const int pos = nbrs.entries[ni].pos;
        if (pos == static_cast<int>(s.pawn_pos)) continue;  // cannot slide pawn's tile

        // Swap the blank slot with the tile at `pos`.
        State ns = s;
        std::swap(ns.tile_id[blank],   ns.tile_id[pos]);
        std::swap(ns.stair_lvl[blank], ns.stair_lvl[pos]);
        ns.blank_pos = static_cast<uint8_t>(pos);
        out.push_back(ns);
    }
}

// ─── Heuristic ────────────────────────────────────────────────────────────────
//
// Admissible heuristic = Manhattan distance to cell 0
//                      + 0.8 penalty  if pawn is on a ground-level non-stair tile
//                      − 0.2 reward   if pawn is on a stair (transitioning levels)
//
// The Manhattan-distance component is a lower bound on the number of pawn moves
// required.  The fractional penalties/rewards are < 1 so they cannot make the
// heuristic inadmissible (each move costs exactly 1).

float TempleSolver::heuristic(const State& s) const noexcept {
    const int p    = s.pawn_pos;
    float     base = static_cast<float>((p / 3) + (p % 3));

    const auto& info = TILE_INFO[s.tile_id[p]];
    float penalty    = 0.0f;

    if (!info.is_stair && info.fixed_level == 0) penalty += 0.8f;
    if ( info.is_stair && p != 0)                penalty -= 0.2f;

    return base + penalty;
}

// ─── Goal test ────────────────────────────────────────────────────────────────
//
// The pawn wins when it reaches cell 0 on a tile that opens to the left (side 3)
// at the upper level — or is the special stair tile E1 (face=3), which opens
// directly to the left and lets the pawn escape.

bool TempleSolver::is_goal(const State& s) const noexcept {
    if (s.pawn_pos != 0) return false;
    const auto& info = TILE_INFO[s.tile_id[0]];
    if (info.is_stair) return info.stair_face == 3;           // E1
    return info.fixed_level == 1 && (info.open_mask & 0x8u);  // level-1, side-3 open
}

// ─── A* search ────────────────────────────────────────────────────────────────
//
// Implementation notes:
//   • Priority queue stores (f, tie-break counter, key) — 16 bytes per entry.
//   • Lazy deletion: nodes are re-pushed on improvement; closed set skips stale.
//   • All hash tables are pre-reserved to avoid rehashing during hot loop.
//   • Path reconstruction walks `came_from` from goal back to start.

struct PQEntry {
    float    f;
    uint32_t seq;    // tie-break: earlier node wins
    StateKey key;

    bool operator>(const PQEntry& o) const noexcept {
        if (f   != o.f)   return f   > o.f;
        return seq > o.seq;
    }
};

std::vector<StepInfo> TempleSolver::solve(const State& initial) {
    constexpr std::size_t RESERVE = 1u << 18;  // 262 144 buckets

    std::priority_queue<PQEntry, std::vector<PQEntry>, std::greater<PQEntry>> open;
    std::unordered_map<StateKey, StateKey> came_from;
    std::unordered_map<StateKey, float>    g_score;
    std::unordered_map<StateKey, State>    key_to_state;
    std::unordered_set<StateKey>           closed;

    came_from.reserve(RESERVE);
    g_score.reserve(RESERVE);
    key_to_state.reserve(RESERVE);
    closed.reserve(RESERVE);

    uint32_t  seq       = 0;
    StateKey  start_key = initial.key();

    g_score[start_key]      = 0.0f;
    key_to_state[start_key] = initial;
    open.push({ heuristic(initial), seq++, start_key });

    std::vector<State> succs;
    succs.reserve(16);

    std::size_t iterations = 0;

    while (!open.empty()) {
        auto [f, s_seq, cur_key] = open.top();
        open.pop();

        if (closed.count(cur_key)) continue;
        closed.insert(cur_key);

        ++iterations;
        if (iterations % 100'000 == 0) {
            std::printf("iter %zu  open=%zu  closed=%zu\n",
                        iterations, open.size(), closed.size());
        }

        const State& current = key_to_state[cur_key];

        if (is_goal(current)) {
            // ── Reconstruct path ──────────────────────────────────────────────
            std::vector<StepInfo> path;
            StateKey k = cur_key;
            while (true) {
                const State& st = key_to_state[k];
                path.push_back({ st.tile_id, st.stair_lvl, st.pawn_pos, st.blank_pos });
                auto it = came_from.find(k);
                if (it == came_from.end()) break;
                k = it->second;
            }
            std::reverse(path.begin(), path.end());
            return path;
        }

        succs.clear();
        gen_pawn_successors(current, succs);
        gen_tile_successors(current, succs);

        const float g_cur = g_score[cur_key];

        for (const State& nxt : succs) {
            const StateKey  nxt_key      = nxt.key();
            const float     tentative_g  = g_cur + 1.0f;
            const auto      git          = g_score.find(nxt_key);

            if (git == g_score.end() || tentative_g < git->second) {
                came_from[nxt_key]      = cur_key;
                key_to_state[nxt_key]   = nxt;
                g_score[nxt_key]        = tentative_g;
                open.push({ tentative_g + heuristic(nxt), seq++, nxt_key });
            }
        }
    }

    return {};  // no solution found
}

// ─── JSON output ──────────────────────────────────────────────────────────────

void write_path_json(const std::vector<StepInfo>& path,
                     const std::string& filename) {
    std::ofstream out(filename);
    if (!out) throw std::runtime_error("Cannot open output file: " + filename);

    out << "{\n  \"steps\": [\n";
    for (std::size_t si = 0; si < path.size(); ++si) {
        const StepInfo& step = path[si];
        out << "    {\n";
        out << "      \"pawn\":  " << static_cast<int>(step.pawn_pos)  << ",\n";
        out << "      \"blank\": " << static_cast<int>(step.blank_pos) << ",\n";
        out << "      \"tiles\": [";
        for (int i = 0; i < 9; ++i) {
            if (i) out << ", ";
            out << "{\"id\": "   << static_cast<int>(step.tile_id[i])
                << ", \"name\": \"" << TILE_INFO[step.tile_id[i]].name << "\""
                << ", \"slvl\": " << static_cast<int>(step.stair_lvl[i]) << "}";
        }
        out << "]\n";
        out << "    }";
        if (si + 1 < path.size()) out << ',';
        out << '\n';
    }
    out << "  ]\n}\n";
}

} // namespace temple

// ─── Entry point ──────────────────────────────────────────────────────────────

int main() {
    using namespace temple;

    const State initial = BoardConfig::initial_state();

    std::printf("Temple Trap Solver — starting A* search...\n");

    TempleSolver solver;
    const auto   t0   = std::chrono::high_resolution_clock::now();
    auto         path = solver.solve(initial);
    const auto   t1   = std::chrono::high_resolution_clock::now();

    const double elapsed =
        std::chrono::duration<double>(t1 - t0).count();

    if (path.empty()) {
        std::fprintf(stderr, "No solution found. (%.3fs)\n", elapsed);
        return 1;
    }

    std::printf("Solution: %zu moves  |  %.3fs\n", path.size() - 1, elapsed);

    const std::string out_file = "temple_path.json";
    write_path_json(path, out_file);
    std::printf("Path written to %s\n", out_file.c_str());

    return 0;
}
