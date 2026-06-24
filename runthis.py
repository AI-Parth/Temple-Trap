
import heapq
import itertools
import time
import os
from typing import List, Tuple


TILE_MAP = {
        0: "B1", 1: "E", 2: "F",
        3: "C1",  4: "I", 5: None,
        6: "A1",  7: "G", 8: "E",
    }
PAWN_POS = 4
BLANK_POS = 5

# ========== Tile defs ==========

TILE_DEFS = {
    'A':  {'openings': [0, 1], 'stair': False, 'level': 1},
    'B':  {'openings': [1, 2], 'stair': False, 'level': 1},
    'A1': {'openings': [2, 3], 'stair': False, 'level': 1},
    'B1': {'openings': [3, 0], 'stair': False, 'level': 1},
    'C':  {'openings': [0, 2], 'stair': False, 'level': 1},
    'C1': {'openings': [1, 3], 'stair': False, 'level': 1},
    'D':  {'openings': [0], 'stair': True,  'level': None},
    'E':  {'openings': [1], 'stair': True,  'level': None},
    'D1': {'openings': [2], 'stair': True,  'level': None},
    'E1': {'openings': [3], 'stair': True,  'level': None},
    'F':  {'openings': [0, 1], 'stair': False, 'level': 0},
    'G':  {'openings': [1, 2], 'stair': False, 'level': 0},
    'H':  {'openings': [2, 3], 'stair': False, 'level': 0},
    'I':  {'openings': [3, 0], 'stair': False, 'level': 0},
}

DELTA_TO_SIDE = {-1: 3, 1: 1, -3: 0, 3: 2}
OPP_SIDE = {0: 2, 1: 3, 2: 0, 3: 1}

NEIGHBORS = {
    i: [d for d in (-1, 1, -3, 3)
        if not (d == -1 and i % 3 == 0)
        and not (d == 1 and i % 3 == 2)
        and 0 <= i + d < 9]
    for i in range(9)
}

# ========== Classes with __slots__ ==========
class Variables:
    # Slot makes attribute access faster
    __slots__ = ('openings', 'name', 'level', 'blank', 'stair', 'pawn')
    def __init__(self, openings, name, level=0, blank=False, stair=False, pawn=False):

        self.openings = list(openings)
        self.name = name
        self.level = level
        self.blank = bool(blank)
        self.stair = bool(stair)
        self.pawn = bool(pawn)
        
    def copy(self):
        return Variables(self.openings, self.name, self.level, self.blank, self.stair, self.pawn)
    def __repr__(self):
        flags = ("B" if self.blank else "") + ("S" if self.stair else "") + ("P" if self.pawn else "")
        return f"{self.name}(open={self.openings},lvl={self.level},{flags})"

class State:
    __slots__ = ('board', 'cost', 'blank', 'pawn')
    def __init__(self, board: Tuple[Variables, ...], cost:int, blank:int, pawn:int):
        self.board = board
        self.cost = cost
        self.blank = blank
        self.pawn = pawn
    def __repr__(self):
        return f"State(pawn={self.pawn}, blank={self.blank}, cost={self.cost})"

def state_key(state: State) -> str:
    parts = []
    b = state.board
    for v in b:
        lvl = v.level if v.level is not None else -1
        flags = ('1' if v.blank else '0') + ('1' if v.stair else '0') + ('1' if v.pawn else '0')
        parts.append(f"{v.name}:{lvl}:{flags}")
    return "|".join(parts) + f"@{state.pawn},{state.blank}"


#=================================Helper Functions===============================

def tile_has_side(tile_obj: Variables, side_query: int, other_level=None) -> bool:
    if not tile_obj.stair:
        return side_query in tile_obj.openings
    stored = tile_obj.openings[0]
    if other_level == 0:
        return side_query == (stored + 2) % 4
    else:
        return side_query == stored

#==================Heuristic=================
def heuristic_fn(state: State) -> float:
    p = state.pawn
    base = (p // 3) + (p % 3)                     # manhattan distance
    tile = state.board[p]
    penalty = 0.0
    
    if tile.level == 0 and not tile.stair:
        penalty += 0.8
        
    # If pawn on stair and not goal, give credit
    if tile.stair and p != 0:
        penalty -= 0.2
    return (base + penalty)

# ========== State Generator ==========
def pawn_successors(state: State, out: List[State]) -> None:
    cpos = state.pawn
    board = state.board
    cur_tile = board[cpos]
    for d in NEIGHBORS[cpos]:
        pos = cpos + d
        nxt_tile = board[pos]
        if nxt_tile.blank:
            continue
        src_side = DELTA_TO_SIDE[d]
        nbr_side = OPP_SIDE[src_side]

        # 1. Normal <-> Normal (same level)
        if (not cur_tile.stair) and (not nxt_tile.stair):
            if tile_has_side(cur_tile, src_side) and tile_has_side(nxt_tile, nbr_side) and cur_tile.level == nxt_tile.level:
                nb = list(board)
                nc = nb[cpos].copy(); nc.pawn = False
                nn = nb[pos].copy(); nn.pawn = True
                nb[cpos] = nc; nb[pos] = nn
                out.append(State(tuple(nb), state.cost + 1, state.blank, pos))
                continue

        # 2. Normal -> Stair (entering stair)
        if (not cur_tile.stair) and nxt_tile.stair:
            if tile_has_side(cur_tile, src_side) and tile_has_side(nxt_tile, nbr_side, other_level=cur_tile.level):
                nb = list(board)
                nc = nb[cpos].copy(); nc.pawn = False
                nn = nb[pos].copy(); nn.pawn = True
                nn.level = 1 if cur_tile.level == 0 else 0
                nb[cpos] = nc; nb[pos] = nn
                out.append(State(tuple(nb), state.cost + 1, state.blank, pos))
                #print("Entered stair")
                continue

        # 3. Stair -> Normal (exiting stair)
        if cur_tile.stair and (not nxt_tile.stair):
            if tile_has_side(cur_tile, src_side, other_level=nxt_tile.level) and tile_has_side(nxt_tile, nbr_side):
                nb = list(board)
                nc = nb[cpos].copy(); nc.pawn = False
                nn = nb[pos].copy(); nn.pawn = True
                nb[cpos] = nc; nb[pos] = nn
                out.append(State(tuple(nb), state.cost + 1, state.blank, pos))
                #print("Exited stair")
                continue

        # 4. Stair <-> Stair
        if cur_tile.stair and nxt_tile.stair:
            cur_open = cur_tile.openings[0]
            nxt_open = nxt_tile.openings[0]
            if (cur_open + 2) % 4 == nxt_open and src_side == cur_open and nbr_side == nxt_open:
                nb = list(board)
                nc = nb[cpos].copy(); nc.pawn = False
                nn = nb[pos].copy(); nn.pawn = True
                nn.level = cur_tile.level
                nb[cpos] = nc; nb[pos] = nn
                out.append(State(tuple(nb), state.cost + 1, state.blank, pos))
                #print("Moved stair to stair")
                continue

def tile_successors(state: State, out: List[State]) -> None:
    blank = state.blank
    board = state.board
    pawn_tile = board[state.pawn]
    # rule: if pawn on top-level non-stair, no tile movement allowed
    if (pawn_tile.level == 1) and (not pawn_tile.stair):
        return
    #print(1)
    for d in NEIGHBORS[blank]:
        pos = blank + d
        if board[pos].pawn:
            continue
        nb = list(board)
        a = nb[blank].copy(); b = nb[pos].copy()
        # swap
        a.blank = True; b.blank = False
        nb[blank] = b
        nb[pos] = a
        # ensure no tile gets pawn incorrectly
        for i in (blank, pos):
            nb[i].pawn = False
        nb[state.pawn].pawn = True
        #print(2)
        out.append(State(tuple(nb), state.cost + 1, pos, state.pawn))

# ========== A* ==========

def a_star_search(start_state: State, max_iters=100000000) -> List[State] or None:
    counter = itertools.count()
    open_heap = []
    start_key = state_key(start_state)
    g_score = {start_key: 0}
    f0 = heuristic_fn(start_state)
    heapq.heappush(open_heap, (f0, next(counter), start_state, start_key))
    came_from = {}
    key_to_state = {start_key: start_state}
    closed = set()
    iterations = 0

    while open_heap:
        iterations += 1
        if iterations % 100000 == 0:
            print(f"Iter {iterations} open={len(open_heap)} closed={len(closed)} gsize={len(g_score)}", flush=True)
        if iterations > max_iters:
            print("Reached max iters, stoping.")
            return None

        f, _, current, cur_key = heapq.heappop(open_heap)
        if cur_key in closed:
            continue
        closed.add(cur_key)

        if goal_test(current):
            path_keys = []
            k = cur_key
            while True:
                path_keys.append(k)
                if k not in came_from:
                    break
                k = came_from[k]
            path_keys.reverse()
            path_states = [key_to_state[k] for k in path_keys]
            return path_states

        succs = []
        pawn_successors(current, succs)
        tile_successors(current, succs)

        for nxt in succs:
            nxt_key = state_key(nxt)
            tentative_g = g_score[cur_key] + 1
            oldg = g_score.get(nxt_key, None)
            if oldg is None or tentative_g < oldg:
                came_from[nxt_key] = cur_key
                key_to_state[nxt_key] = nxt
                g_score[nxt_key] = tentative_g
                fscore = tentative_g + heuristic_fn(nxt)
                #print(3)
                heapq.heappush(open_heap, (fscore, next(counter), nxt, nxt_key))

    return None

# ========== Goal test==========

def goal_test(state: State) -> bool:
    if state.pawn != 0:
        return False
    tile = state.board[0]
    if tile is None:
        return False
    
    if tile.stair and len(tile.openings) == 1 and tile.openings[0] == 3:
        return True
    
    if tile.level == 1 and 3 in tile.openings:
        return True
    return False


# ========== Board initialization (keeps names) ==========

def initialize_board(tile_map: dict, pawn_pos: int, blank_pos: int) -> State:
    lst = []
    for i in range(9):
        tile_name = tile_map.get(i)
        if tile_name is None:
            lst.append(Variables([], 'BL', level=0, blank=True))
        else:
            data = TILE_DEFS[tile_name]
            lvl = 0 if data['stair'] else data['level']
            lst.append(Variables(data['openings'], tile_name, level=lvl, stair=data['stair']))
    lst[pawn_pos].pawn = True
    lst[blank_pos].blank = True
    return State(tuple(lst), cost=0, blank=blank_pos, pawn=pawn_pos)

# ========== Main (run solver then call visualizer) ==========
if __name__ == "__main__":
    # initialize
    start = initialize_board(TILE_MAP, PAWN_POS, BLANK_POS)
    t0 = time.time()
    path = a_star_search(start)
    t1 = time.time()
    elapsed = t1 - t0
    if path:
        print(f"Path found in {len(path)-1} moves, time {elapsed:.2f}s")
        import pickle
        fname = "temple_path.pkl"
        with open(fname, "wb") as f:
            pickle.dump(path, f)

        try:
            os.system(f"python visualizer.py {fname}")
        except Exception as e:
            print("Could not start visualizer automatically:", e)
            print("Path saved to", fname)
    else:
        print(f"No path found. Elapsed {elapsed:.2f}s")
