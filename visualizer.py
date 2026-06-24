import sys, pickle, pygame, time

import importlib.util
from typing import List, Tuple


#==========Classes==========
class Variables:
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
    
def load_path(fname):
    # Make sure runthis module is imported before unpickling
    spec = importlib.util.spec_from_file_location("runthis", "runthis.py")
    runthis = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(runthis)

    with open(fname, "rb") as f:
        path = pickle.load(f)
    return path


def state_to_cells(state):
    cells = []
    for v in state.board:
        cells.append({
            'name': v.name,
            'level': v.level,
            'stair': v.stair,
            'blank': v.blank,
            'pawn': v.pawn,
            'openings': v.openings
        })
    return cells

def display_path(path):
    pygame.init()
    TILE = 120
    WIDTH, HEIGHT = 3*TILE, 3*TILE
    screen = pygame.display.set_mode((WIDTH, HEIGHT))
    pygame.display.set_caption("Temple Trap - Visualization")
    COLORS = {
        "ground": (150, 75, 0),
        "top": (50, 150, 50),
        "stair": (30, 144, 255),
        "blank": (40, 40, 40),
        "pawn": (255, 255, 255),
        "open": (255, 255, 255),
    }
    font = pygame.font.SysFont("arial", 18, bold=True)

    def draw_openings(cell, x, y):
        cx, cy = x + TILE//2, y + TILE//2
        m = 8; h = TILE//2
        for s in cell['openings']:
            if s == 0: start, end = (cx, y+m), (cx, y+h)
            elif s == 1: start, end = (x+TILE-m, cy), (x+h, cy)
            elif s == 2: start, end = (cx, y+TILE-m), (cx, y+h)
            else: start, end = (x+m, cy), (x+h, cy)
            pygame.draw.line(screen, COLORS["open"], start, end, 8)

    clock = pygame.time.Clock()
    for state in path:
        for e in pygame.event.get():
            if e.type == pygame.QUIT:
                pygame.quit(); return
        screen.fill((0, 0, 0))
        cells = state_to_cells(state)
        for i, cell in enumerate(cells):
            x, y = (i % 3) * TILE, (i // 3) * TILE
            if cell['blank']:
                color = COLORS["blank"]
            elif cell['stair']:
                color = COLORS["stair"]
            else:
                color = COLORS["top"] if cell['level'] == 1 else COLORS["ground"]
            pygame.draw.rect(screen, color, (x+4, y+4, TILE-8, TILE-8), border_radius=10)
            if not cell['blank']:
                draw_openings(cell, x, y)
                screen.blit(font.render(cell['name'], True, (0,0,0)), (x+8, y+8))
            if cell['pawn']:
                pygame.draw.circle(screen, COLORS["pawn"], (x+TILE//2, y+TILE//2), 22)
        pygame.display.flip()
        clock.tick(2)   
    
    while True:
       for e in pygame.event.get():
            if e.type == pygame.QUIT:
                pygame.quit(); return
       time.sleep(0.05)

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python visualizer.py temple_path.pkl")
        sys.exit(1)
    fname = sys.argv[1]
    path = load_path(fname)
    display_path(path)
